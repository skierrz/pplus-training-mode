"""Emulator test of the SD card storage (run through emu.py; settings come
from the environment the same way).

  test-storage REL [--only NAME ...] [--evaluate]
      Seven boots on the storage tests' own copy of the SD card
      (emu.storage_image: SD_IMAGE copied, the plugin written into it, its
      tm/ folder emptied; SD_FOLDER and SD_IMAGE are only read). Between the
      boots the test reads the copy and puts hand-made files into it
      (scripts/fatimage.py). One CPU thread; Mario (P1) against a Fox CPU on
      Final Destination; every match seeded on its first frame (see
      IT_MANAGER).

        save     writable card: lab settings changed through the lab menu
                 (persisted ones and session-only ones), saved when the
                 menu closes; a menu close with only session-only changes
                 writes nothing; Save Positions, an HMN recording made with
                 the plugin's script input, its playback traced, Recording >
                 Export. Then the card: settings-a.bin and the export, every
                 field, checksum and stored input checked from the host.
        load     second boot: the settings come back in a match (memory,
                 and the OSD Menu page photographed), session-only fields
                 at their defaults; the drill list's X opens the browser,
                 the export is loaded: same characters, costumes and stage,
                 positions saved from it, and the playback's trace compared
                 with the first boot's frame by frame.
        bests    bests persistence round-trip: verifies bests-a.bin writes
                 and loads cleanly across boots with zero spurious saves.
        bests-back  the next boot: verifies persisted bests reload cleanly
                 into the session block with nothing written again.
        locked   the same card write-protected (as every other test runs):
                 the files still list and load, but deleting, saving
                 settings and exporting fail with a message; a best raised
                 in the session (written into the session block) stays in
                 the session and is not saved; the card's files are
                 unchanged byte for byte.
        migrate  a damaged newest settings file and an older one (an area
                 missing, one with fewer fields, one of another generation,
                 one unknown): the older one is migrated area by area; the
                 browser with 16 files (two pages; damaged, older, newer,
                 cut short and corrupt ones; their messages), deleting with
                 and without confirming; the older file's Infinite Shields
                 Health of 50 (the old top) comes back as 60; a change made
                 in the menu writes the migrated settings to the other file.
        errors   a cut-short and a newer settings file: defaults, a message,
                 nothing written until a setting changes; an empty folder:
                 the browser's "none found".

  --evaluate measures what storing the savestate itself would take: the
  recording state's size, how well it compresses (zlib, from memory), and
  whether the heaps of the import's match are laid out as the first one's.
"""
import argparse
import os
import re
import struct
import time
import zlib

import cpu_tests as C
import emu as E
import fatimage
import lab_tests as LT
import loader_tests as L
import nav
import record_tests as RT
import shots
from harness import HarnessError, Timeout

P1, CPU = C.P1, C.CPU
FOLDER = E.STORAGE_FOLDER
SETTINGS_FILES = ("settings-a.bin", "settings-b.bin")
# Session bests storage (src/menu/bests_file.cpp, src/shared/drill_session.h).
BESTS_MAGIC, BESTS_HEAD, BESTS_SLOTS = 0x544D4246, 36, 32
SESSION_OFFSET = 640
BEST_SLOT = 1


def combo_best_at(base):
    return base + SESSION_OFFSET + 16 + 2 * BEST_SLOT

# main::Settings offsets (src/main/settings.h).
SET = {"playerPercent": 8, "lockPlayerPercent": 10, "frameAdvance": 11, "ledgeHud": 12,
       "cameraMode": 16, "gameSpeed": 17, "moveStaling": 18, "diDisplay": 19,
       "advanceButton": 24, "advanceDelay": 25, "hitboxes": 42,
       "wavedash": 92, "lCancel": 93, "actOos": 94, "cpuPercent": 120, "cpuLock": 122,
       "cpuControl": 123, "shieldHealth": 133, "hmnMode": 186}
# What the first boot sets through the menu, and what the next boot must find.
PERSISTED = {"cameraMode": 1, "moveStaling": 0, "wavedash": 1, "lCancel": 1,
             "advanceButton": 2, "advanceDelay": 40}
SESSION_SET = {"frameAdvance": 1, "playerPercent": 5, "lockPlayerPercent": 1, "gameSpeed": 1, "cpuControl": 1}
# CPU Control starts on Lab (1); the file holds 0 for it, which a load must not take.
SESSION_DEFAULTS = {"frameAdvance": 0, "playerPercent": 0, "lockPlayerPercent": 0, "gameSpeed": 0, "cpuControl": 1}
DEFAULTS = {"cameraMode": 0, "moveStaling": 1, "wavedash": 0, "lCancel": 0,
            "advanceButton": 0, "advanceDelay": 30, "ledgeHud": 0}
HALF = {"playerPercent", "cpuPercent"}
# Areas (src/main/storage/areas.h) and where they start in main::Settings.
AREA_AT = {1: 8, 2: 24, 3: 40, 4: 56, 5: 72, 6: 88, 7: 120, 8: 184, 9: 248, 10: 268, 11: 288, 12: 296, 13: 292,
           14: 372, 15: 388, 16: 404}
AREA_COUNT = len(AREA_AT)
AREA_GENERAL, AREA_CONTROLS, AREA_OSD = 1, 2, 6
SETTINGS_MAGIC, RECORDING_MAGIC = 0x544D5346, 0x544D5246
# OSD Menu rows (src/main/ui/pages/osd_menu.cpp).
OSD_WAVEDASH, OSD_LCANCEL, OSD_ACT_OOS = 1, 2, 3
CONTROLS_HOLD_DELAY = 1
AREA_CPU = 7


class StorageError(RuntimeError):
    pass


# The assist trophy the game keeps ready in a Training match: itManager's
# instance (pointer at 0x80B8B7F4), kind at +0x10BE (notes on the game's
# code). When it is Mr. Resetti (0xA1), his two files go into
# FighterTechqniq, the heap this build keeps the recording's storage in:
# the storage cannot be taken, Save Positions is refused and a restore does
# nothing. The game draws the kind at random, so every match this test
# starts gets the same random state on its first frame (nav.seed_match_rng),
# which on Final Destination with Mario and Fox draws another assist.
IT_MANAGER, IT_NEXT_ASSIST, MR_RESETTI = 0x80B8B7F4, 0x10BE, 0xA1


def held_assist(emu):
    mgr = emu.ptr(IT_MANAGER)
    return emu.u16(mgr + IT_NEXT_ASSIST) if mgr else None


def check_assist(emu, res, what):
    """Note the assist the game holds; stop with the reason if it is Mr.
    Resetti (the recording's storage cannot be taken in that match)."""
    kind = held_assist(emu)
    res.note(f"{what}: assist trophy held ready {kind if kind is None else hex(kind)}")
    if kind == MR_RESETTI:
        raise StorageError(f"{what}: the game holds Mr. Resetti as its assist (his files fill FighterTechqniq, "
                           "where the recording's storage goes)")


def seeded_match_start(emu):
    """A match the menus started: its random state set on its first frame
    (see IT_MANAGER), then running."""
    nav.seed_match_rng(emu)
    nav.wait_match_start(emu)


Result = C.Result


def fnv(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def padded(n):
    return (n + 31) & ~31


# ---- the files, read and built on the host ---------------------------------------

def parse_header(data):
    if data is None or len(data) < 32:
        return None
    magic, fmt, hsize, size, csum, seq = struct.unpack_from(">IHHIII", data, 0)
    ok = size <= len(data) and fnv(data[16:size]) == csum
    return {"magic": magic, "format": fmt, "size": size, "checksum_ok": ok, "sequence": seq}


def parse_settings(data):
    h = parse_header(data)
    if h is None or h["magic"] != SETTINGS_MAGIC:
        return None
    count, layout = struct.unpack_from(">HH", data, 32)
    areas = {}
    for i in range(count):
        aid, gen, used, off = struct.unpack_from(">BBHI", data, 40 + 8 * i)
        areas[aid] = (gen, data[off:off + used])
    h.update(areas=areas, layout=layout)
    return h


def settings_value(parsed, name):
    """A field's value as the file holds it (None if its area is absent)."""
    at = SET[name]
    for aid, start in AREA_AT.items():
        gen_bytes = parsed["areas"].get(aid)
        if gen_bytes is None:
            continue
        b = gen_bytes[1]
        if start <= at < start + len(b):
            if name in HALF:
                return struct.unpack_from(">H", b, at - start)[0]
            return b[at - start]
    return None


def build_settings(areas, sequence, fmt=1, layout=1):
    """A settings file: `areas` [(id, generation, bytes)]."""
    at = 40 + 8 * len(areas)
    table, body = b"", b""
    for aid, gen, b in areas:
        table += struct.pack(">BBHI", aid, gen, len(b), at + len(body))
        body += b + bytes(((len(b) + 3) & ~3) - len(b))
    size = at + len(body)
    rest = struct.pack(">I", sequence) + bytes(12) + struct.pack(">HHI", len(areas), layout, 0) + table + body
    data = struct.pack(">IHHII", SETTINGS_MAGIC, fmt, 32, size, fnv(rest)) + rest
    return data + bytes(padded(size) - size)


def parse_recording(data):
    h = parse_header(data)
    if h is None or h["magic"] != RECORDING_MAGIC:
        return None
    i = data[32:96]
    info = {"name": i[0:32].split(b"\0")[0].decode("latin-1"), "hmn_kind": i[32], "hmn_costume": (i[33], i[34]),
            "hmn_icon": i[35], "cpu_kind": i[36], "cpu_costume": (i[37], i[38]), "cpu_icon": i[39],
            "stage": struct.unpack_from(">H", i, 40)[0], "frames": struct.unpack_from(">H", i, 44)[0],
            "hmn_slots": i[46], "cpu_slots": i[47], "year": struct.unpack_from(">H", i, 48)[0],
            "has_select": i[55], "hmn_select": (i[56], i[57]), "cpu_select": (i[58], i[59]),
            "hazard_off": i[60]}
    areas, slots, placements = struct.unpack_from(">HHI", data, 96)
    place = [struct.unpack_from(">ffffffB", data, placements + 32 * k) for k in range(2)]
    at = placements + 64
    out = []
    for _ in range(slots):
        side, index, start, end = struct.unpack_from(">BBHH", data, at)
        counts = struct.unpack_from(">4H", data, at + 6)
        lr, lossy = struct.unpack_from(">HxxI", data, at + 14)
        at += 20
        tables = []
        for c in counts:
            tables.append(struct.unpack_from(f">{c}f", data, at))
            at += 4 * c
        at += 8 * lr
        frames = []
        for t in range(end - start):
            b, x, y = struct.unpack_from(">HBB", data, at)
            frames.append((b, tables[0][x], tables[1][y]))
            at += 8
        out.append({"side": side, "index": index, "start": start, "end": end, "frames": frames, "lossy": lossy})
    h.update(info=info, slots=out, place=place, areas=areas, end=at)
    return h


def tm_files(img):
    return {n: fatimage.read_file(img, f"{FOLDER}/{n}") for n in fatimage.list_folder(img, FOLDER)}


# ---- in the game ------------------------------------------------------------------

def log_since(emu, pos):
    return emu.log_text()[pos:]


def wait_line(emu, what, regex, pos, frames=600):
    rx = re.compile(regex)
    return emu.wait_for(what, lambda: rx.search(emu.log_text(), pos), frames, every=5)


def read_settings(emu, base, names):
    return {n: (emu.u16(base + SET[n]) if n in HALF else emu.u8(base + SET[n])) for n in names}


def tap_until(lab, name, base, target, stick, limit=40):
    """Left/Right taps until the byte field `name` reads `target`."""
    for _ in range(limit):
        if lab.emu.u8(base + SET[name]) == target:
            return
        lab.tap(stick=stick)
    raise StorageError(f"{name} stuck at {lab.emu.u8(base + SET[name])}, wanted {target}")


def value_box_on(path, xscale, row):
    """True if the lab menu row's value box is the green On state
    (src/main/ui/style.h kLabPillOn): its margins (left and right of the
    centred text, clear of selection triangles) are distinctly green."""
    shot = shots.Shot(path, xscale)
    top = shots.ROW_TOP + row * shots.ROW_PITCH
    parts = [shot.mean(424.0, top + 4, 448.0, top + shots.ROW_H - 4),
             shot.mean(536.0, top + 4, 560.0, top + shots.ROW_H - 4)]
    r = sum(p[0] for p in parts) / 2
    g = sum(p[1] for p in parts) / 2
    b = sum(p[2] for p in parts) / 2
    return g - max(r, b) > 15 and g - r > 30, (round(r), round(g), round(b))


def into_quick_lab(emu):
    """From Training's character select, Mario against a Fox CPU on Final
    Destination via the menu module's test block."""
    nav.start_quick_match(emu, seed_rng=True)
    emu.advance(60)


def open_browser(emu):
    pos = len(emu.log_text())
    emu.press(0, ["X"], frames=4, after=6)
    wait_line(emu, "the browser to open", r"browser: opened", pos, 120)
    m = wait_line(emu, "the browser's listing", r"browser: (\d+) recording\(s\) listed", pos, 120)
    return int(m.group(1)), pos


def heaps_now(emu):
    """(name, start, end, allocated, used) of every heap that exists."""
    raw = emu.read(0x80494958, 0x47 * 16)
    out = []
    for i in range(0x47):
        name_p, pool, size, _ = struct.unpack_from(">IIII", raw, 16 * i)
        if not emu.is_ram(pool):
            continue
        h = emu.read(pool, 0x20)
        start, end = struct.unpack_from(">II", h, 4)
        allocated = struct.unpack_from(">H", h, 0x14)[0]
        used = struct.unpack_from(">I", h, 0x1C)[0]
        out.append((emu.cstr(name_p, 24) or str(i), start, end, allocated, used))
    return out


HEAP_NETWORK, HEAP_FIGHTER_TECHQNIQ = 6, 31  # the import's two buffers: the file, the recording


def heap_blocks(emu, index):
    """One of the game's heaps as (name, allocated count, [(address, bytes,
    free, first bytes)]), blocks in address order. The layout is the one
    src/shared/game/heap.cpp reads: the pool's start and end at +4 and +8,
    its sentinel at +0x20, free blocks linked through +8, every block a
    0x20-byte header with its whole size at +4. The walk from the start
    assumes the blocks follow one another there; it stops where they do not."""
    name_p, pool, _, _ = struct.unpack(">IIII", emu.read(0x80494958 + 16 * index, 16))
    if not emu.is_ram(pool):
        return None
    h = emu.read(pool, 0x24)
    start, end = struct.unpack_from(">II", h, 4)
    free = set()
    block = emu.u32(struct.unpack_from(">I", h, 0x20)[0] + 8)
    for _ in range(512):
        if not start <= block < end:
            break
        free.add(block)
        after = emu.u32(block + 8)
        if after <= block:
            break
        block = after
    blocks, at = [], start
    for _ in range(512):
        if not start <= at < end:
            break
        size = emu.u32(at + 4)
        if size < 0x20 or at + size > end:
            break
        blocks.append((at, size, at in free, b"" if at in free else emu.read(at + 0x20, min(64, size - 0x20))))
        at += size
    return emu.cstr(name_p, 24) or str(index), struct.unpack_from(">H", h, 0x14)[0], blocks


def import_log(emu, pos):
    """What the plugin logged about the import's two buffers since `pos`
    (a refusal of the recording's buffer is logged on the first and the
    last try, with what the heap held)."""
    for line in re.findall(r"(?:record: (?:no )?storage|storage: (?:import|no buffer))[^\r\n]*", log_since(emu, pos)):
        print(f"  import: log: {line}")


def import_diagnosis(emu, pos):
    """Printed when an import does not arrive: what the plugin logged about
    its two buffers since `pos`, and what the two heaps hold now. The
    recording's buffer needs FighterTechqniq empty; a block there (a
    fighter's Entry file is loaded into that heap, for one) keeps it out."""
    import_log(emu, pos)
    for index in (HEAP_FIGHTER_TECHQNIQ, HEAP_NETWORK):
        found = heap_blocks(emu, index)
        if found is None:
            print(f"  import: heap {index} does not exist")
            continue
        name, allocated, blocks = found
        print(f"  import: heap {name}: {allocated} allocated block(s); {len(blocks)} block(s) walked")
        for at, size, free, head in blocks:
            text = "".join(chr(c) if 0x20 <= c < 0x7F else "." for c in head)
            print(f"  import:   {at:08x} {size:#9x} {'free' if free else 'used ' + head[:16].hex() + ' ' + text}")


# ---- scenarios ----------------------------------------------------------------------

def menu_changes(emu, lab, base, res, ctx):
    """The first boot's settings, through the lab menu."""
    nav.pause_training(emu)
    lab.open_menu()
    lab.tap(["A"])                      # Rail > Lab, cursor on Frame Advance
    lab.tap(["A"])                      # Frame Advance On (row 0)
    lab.down(1)                         # P1 Damage (row 1)
    lab.set_int(SET["playerPercent"], SESSION_SET["playerPercent"])
    lab.down(1)                         # Lock P1 Damage (row 2)
    lab.toggle_on(SET["lockPlayerPercent"])
    lab.down(2)                         # Game Speed (row 4)
    tap_until(lab, "gameSpeed", base, SESSION_SET["gameSpeed"], LT.RIGHT)
    lab.down(1)                         # Camera (row 5)
    tap_until(lab, "cameraMode", base, PERSISTED["cameraMode"], LT.RIGHT)
    lab.down(1)                         # Move Staling (row 6)
    lab.toggle_off(SET["moveStaling"])
    lab.back_to_main(1)
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    lab.down(OSD_WAVEDASH)
    lab.toggle_on(SET["wavedash"])
    lab.down(OSD_LCANCEL - OSD_WAVEDASH)
    lab.toggle_on(SET["lCancel"])
    lab.back_to_main(1)
    lab.up(LT.MAIN_OSD)
    lab.go_controls()
    tap_until(lab, "advanceButton", base, PERSISTED["advanceButton"], LT.RIGHT)
    lab.down(1)
    tap_until(lab, "advanceDelay", base, PERSISTED["advanceDelay"], LT.RIGHT)
    lab.back_to_main(2)
    # CPU Control (CPU Options' row) is written directly: its page
    # belongs to the CPU code.
    emu.h.call("write_mem", addr=base + SET["cpuControl"], hex="01")
    pos = lab.log_pos()
    lab.close_menu()
    m = wait_line(emu, "the settings to be saved", r"storage: settings (saved to (\S+) \(save (\d+), (\d+) bytes\)|"
                  r"save to \S+ failed.*)", pos, 300)
    got = read_settings(emu, base, list(PERSISTED) + list(SESSION_SET))
    want = dict(PERSISTED, **SESSION_SET)
    res.row("menu changes in memory", want, got, got == want)
    res.row("saved when the menu closed", "settings-a.bin, save 1", m.group(0),
            m.group(2) == "settings-a.bin" and m.group(3) == "1")
    # The session-only fields back as the recording wants them: no write.
    lab.open_menu()
    lab.main_to(LT.MAIN_GENERAL)
    lab.tap(["A"])
    lab.tap(["A"])                      # Frame Advance Off (row 0)
    lab.down(1)                         # P1 Damage (row 1)
    lab.set_int(SET["playerPercent"], 0)
    lab.down(1)                         # Lock P1 Damage (row 2)
    lab.toggle_off(SET["lockPlayerPercent"])
    lab.down(2)                         # Game Speed (row 4)
    tap_until(lab, "gameSpeed", base, 0, LT.LEFT)
    lab.back_to_main(1)
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(60)
    again = re.search(r"storage: settings (saved|save)", log_since(emu, pos))
    res.row("session-only changes write nothing", "no write", again.group(0) if again else "no write", again is None)
    nav.resume_training(emu)


def s_save(emu, ctx):
    res = Result("save")
    pos0 = len(emu.log_text())
    nav.boot_to_training_css(emu)
    nav.start_training_match(emu, "mario", "fox", p1_costume=2, seed_rng=True)
    emu.advance(60)
    check_assist(emu, res, "the match")
    log = log_since(emu, pos0)
    res.row("card seen by the plugin", "inserted, writable", re.search(r"storage: card ([^\r\n]*)", log).group(1),
            "storage: card inserted, writable" in log)
    res.row("no settings files at first", "a and b not found",
            re.search(r"storage: settings files: ([^\r\n]*)", log).group(1),
            "settings files: a file not found (0), b file not found (0)" in log)
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    ctx["melee_a"] = nav.melee_setup(emu)
    C.set_training_option(emu, "Stop")
    emu.advance(30)
    menu_changes(emu, lab, base, res, ctx)

    r = RT.Runner(emu)
    r.refresh()
    emu.advance(30)
    pos = len(emu.log_text())
    r.run_request("save")
    # Read from a whole pass (the save's pass can still be under way, its
    # flags not yet written: see RT.MAIN_THREAD_STATE).
    st = r.step(1)[-1]["rec"]
    if not st["flags"] & RT.FLAG_SAVED:
        # A restore then does nothing, and the recording below would fail
        # with "no restore in the run": say why here.
        why = re.findall(r"record: (no storage[^\r\n]*|No room[^\r\n]*)", log_since(emu, pos))
        raise StorageError(f"Save Positions not taken (flags {st['flags']:#x}): {why[-2:] or 'no log line'}")
    if ctx.get("evaluate"):
        evaluate_save(emu, ctx, res, "a")
    rows = RT.record_hmn(r)
    st = RT.last(rows)["rec"]
    n = len(rows)
    ctx["frames"] = n
    ctx["slot_a"] = [r.rec.slot_frame(0, 0, t) for t in range(n)]
    r.rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    ctx["play_a"] = RT.play_hmn(r, 1, n)
    r.rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    res.row("recording made", "slot 1 from frame 0, exact start", f"{n} frames, flags {st['flags']:#x}",
            n > 300 and st["flags"] & RT.FLAG_EXACT)

    emu.h.call("resume")
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.down(25)                        # Export, the last row
    pos = lab.log_pos()
    lab.tap(["A"])
    m = wait_line(emu, "the export", r"storage: (exported (\S+) \((\d+) bytes\)|export \S+ failed[^\r\n]*)", pos)
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-export-notice")[0])
    lab.close_menu()
    emu.advance(30)
    res.row("Recording > Export", "exported", m.group(1), m.group(1).startswith("exported"))
    ctx["export"] = m.group(2)
    no_save = re.search(r"storage: settings saved", log_since(emu, pos))
    res.row("closing the menu after the export", "no settings write (nothing persisted changed)",
            no_save.group(0) if no_save else "no write", no_save is None)
    return res


def check_card_after_save(ctx):
    res = Result("save: the card")
    files = tm_files(ctx["img"])
    res.row("files in tm/", f"settings-a.bin, {ctx['export']}", sorted(files),
            set(files) == {"settings-a.bin", ctx["export"]})
    s = parse_settings(files.get("settings-a.bin"))
    ok = s is not None and s["checksum_ok"] and s["format"] == 1 and s["sequence"] == 1
    res.row("settings-a.bin", f"format 1, save 1, checksum good, {AREA_COUNT} areas",
            s and (s["format"], s["sequence"], s["checksum_ok"], len(s["areas"])), ok and len(s["areas"]) == AREA_COUNT)
    if s:
        got = {n: settings_value(s, n) for n in PERSISTED}
        res.row("persisted values in the file", PERSISTED, got, got == PERSISTED)
        zero = {n: settings_value(s, n) for n in SESSION_SET}
        res.row("session-only fields stored as 0", {n: 0 for n in SESSION_SET}, zero, all(v == 0 for v in zero.values()))
        ctx["settings_file"] = files["settings-a.bin"]
    rec = parse_recording(files.get(ctx["export"]))
    ok = rec is not None and rec["checksum_ok"] and rec["format"] == 1
    res.row("the export", "format 1, checksum good, one slot", rec and (rec["format"], rec["checksum_ok"],
                                                                      len(rec["slots"])),
            ok and len(rec["slots"]) == 1)
    if rec:
        slot = rec["slots"][0]
        mism = [t for t in range(min(len(slot["frames"]), len(ctx["slot_a"])))
                if slot["frames"][t] != ctx["slot_a"][t]]
        res.row("stored inputs vs the slot in memory", f"{ctx['frames']} frames, none differ",
                f"{len(slot['frames'])} frames (side {slot['side']}, slot {slot['index'] + 1}), {len(mism)} differ",
                len(slot["frames"]) == ctx["frames"] and not mism and slot["start"] == 0)
        p1 = ctx["melee_a"]["players"][0]
        info = rec["info"]
        res.row("characters and stage", "Mario (0) vs Fox (7), stage 2",
                (info["hmn_kind"], info["cpu_kind"], info["stage"]),
                (info["hmn_kind"], info["cpu_kind"], info["stage"]) == (0, 7, 2))
        res.row("P1 costume file", p1["costume_file"], info["hmn_costume"][0], info["hmn_costume"][0] == p1["costume_file"])
        res.note(f"export header: {info}; placements {rec['place']}")
        ctx["export_file"] = files[ctx["export"]]
    return res


def s_load(emu, ctx):
    res = Result("load")
    pos0 = len(emu.log_text())
    nav.boot_to_training_css(emu, drill_list="keep")
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    into_quick_lab(emu)
    log = log_since(emu, pos0)
    m = re.search(r"storage: settings loaded from ([^\r\n]*)", log)
    res.row("settings read at the first match", f"from settings-a.bin, save 1, {AREA_COUNT} areas, nothing migrated",
            m.group(1) if m else None,
            m is not None and m.group(1).startswith(f"settings-a.bin (save 1, layout 1): {AREA_COUNT} areas, 0 with "
                                                     "new fields, 0 at defaults, 0 unknown"))
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    got = read_settings(emu, base, PERSISTED)
    res.row("persisted settings back", PERSISTED, got, got == PERSISTED)
    got = read_settings(emu, base, SESSION_DEFAULTS)
    res.row("session-only settings at their defaults", SESSION_DEFAULTS, got, got == SESSION_DEFAULTS)
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    emu.advance(10)
    path = emu.screenshot("storage-osd-menu-loaded")[0]
    ctx["shots"].append(path)
    xs = 0.75 if "lab: screen 16:9" in emu.log_text() else 1.0
    boxes = {row: value_box_on(path, xs, row) for row in (OSD_WAVEDASH, OSD_LCANCEL, OSD_ACT_OOS)}
    res.row("OSD Menu on screen: Wavedash, L-Cancel On; Act OoS Off", "green, green, not green",
            {k: v[1] for k, v in boxes.items()},
            boxes[OSD_WAVEDASH][0] and boxes[OSD_LCANCEL][0] and not boxes[OSD_ACT_OOS][0])
    lab.back_to_main(1)
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(30)
    w = re.search(r"storage: settings saved", log_since(emu, pos))
    res.row("menu closed without changes", "no write", w.group(0) if w else "no write", w is None)
    nav.finish_training(emu)
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
    emu.advance(30)
    count, pos = open_browser(emu)
    wait_line(emu, "the headers", r"browser: headers read: 1 ready", pos, 300)
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-browser")[0])
    res.row("browser lists the export", 1, count, count == 1)
    pos = len(emu.log_text())
    emu.press(0, ["A"], frames=4, after=6)
    wait_line(emu, "the import's match to start", r"browser: starting", pos, 300)
    seeded_match_start(emu)
    emu.advance(30)
    check_assist(emu, res, "the import's match")
    try:
        m = wait_line(emu, "the imported positions", r"record: positions imported \((savestate|lite[^)]*)\)", pos, 600)
    except Timeout:
        # The plugin gives the import up when its buffers do not come: say
        # which heap was in the way and what it held.
        emu.advance(30)
        import_diagnosis(emu, pos)
        raise
    import_log(emu, pos)
    emu.advance(30)
    ctx["shots"].append(emu.screenshot("storage-imported")[0])
    setup = nav.melee_setup(emu)
    a = ctx["melee_a"]
    same = [(p["kind"], p["state"], p["costume_file"]) for p in setup["players"][:2]] == \
           [(p["kind"], p["state"], p["costume_file"]) for p in a["players"][:2]]
    res.row("the import's match", "same characters, costumes and stage as the export's",
            f"{[(p['kind'], p['costume_file']) for p in setup['players'][:2]]}, stage {setup['stage']}",
            same and setup["stage"] == a["stage"])
    res.row("positions saved from the file", "savestate (exact from here)", m.group(1), m.group(1) == "savestate")
    r = RT.Runner(emu)
    r.refresh()
    if ctx.get("evaluate"):
        evaluate_save(emu, ctx, res, "b")
    n = ctx["frames"]
    play = RT.play_hmn(r, 1, n)
    a_rows = ctx["play_a"]
    both = [t for t in range(min(len(a_rows), len(play))) if a_rows[t] is not None and play[t] is not None]
    inputs = [t for t in both if a_rows[t][P1]["input"] != play[t][P1]["input"]]
    res.row("P1's input every frame vs the first boot's playback", f"{n} frames equal",
            f"{len(both)} compared, {len(inputs)} differ {inputs[:5]}", len(both) >= n - 2 and not inputs)
    ok1, v1 = RT.trace_check(a_rows, play, P1)
    ok2, v2 = RT.trace_check(a_rows, play, CPU)
    res.row("P1 trace (position, status, damage, hitlag, hitstun, air)", "equal; idle animation may differ", v1, ok1)
    res.row("CPU trace (standing under the lab's control)", "equal; idle animation may differ", v2, ok2)
    split = RT.rng_split(a_rows, play)
    res.note(f"random state first differs at frame {split} (not restored: a new match)")
    emu.h.call("resume")
    return res


def s_locked(emu, ctx):
    res = Result("locked")
    before = tm_files(ctx["img"])
    ctx["locked_before"] = before
    pos0 = len(emu.log_text())
    nav.boot_to_training_css(emu, drill_list="keep")
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    count, pos = open_browser(emu)
    wait_line(emu, "the headers", r"browser: headers read: 1 ready", pos, 300)
    pos = len(emu.log_text())
    emu.press(0, ["X"], frames=4, after=10)
    m = wait_line(emu, "the delete refusal", r"browser: (Cannot delete: [^\r\n]*)", pos, 120)
    ctx["shots"].append(emu.screenshot("storage-locked-delete")[0])
    res.row("delete on a locked card", "refused: the SD card is locked", m.group(1),
            m.group(1) == "Cannot delete: the SD card is locked.")
    emu.press(0, ["B"], frames=4, after=10)
    emu.advance(20)
    into_quick_lab(emu)
    check_assist(emu, res, "the match")
    log = log_since(emu, pos0)
    res.row("card seen by the plugin", "inserted, locked", re.search(r"storage: card ([^\r\n]*)", log).group(1),
            "storage: card inserted, locked" in log)
    res.row("settings still read", "from settings-a.bin", bool(re.search(r"settings loaded from settings-a\.bin", log)),
            bool(re.search(r"settings loaded from settings-a\.bin", log)))
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    lab.down(OSD_ACT_OOS)
    lab.toggle_on(SET["actOos"])
    lab.back_to_main(1)
    pos = lab.log_pos()
    lab.close_menu()
    m = wait_line(emu, "the save refusal", r"storage: notice: (Lab settings not saved: [^\r\n]*)", pos, 120)
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-locked-settings")[0])
    res.row("settings save on a locked card", "not saved: the SD card is locked", m.group(1),
            m.group(1) == "Lab settings not saved: the SD card is locked"
            and "settings saved" not in log_since(emu, pos))
    res.row("the change stays for the session", 1, emu.u8(base + SET["actOos"]), emu.u8(base + SET["actOos"]) == 1)
    nav.resume_training(emu)
    r = RT.Runner(emu)
    r.refresh()
    emu.advance(20)
    r.run_request("save")
    emu.h.call("resume")
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.down(25)
    pos = lab.log_pos()
    lab.tap(["A"])
    m = wait_line(emu, "the export refusal", r"storage: notice: (Export[^\r\n]*)", pos, 120)
    res.row("Export on a locked card", "failed: the SD card is locked", m.group(1),
            m.group(1) == "Export failed: the SD card is locked")
    lab.close_menu()
    emu.advance(20)
    if "combo_best" in ctx:
        got = re.search(r"bests: loaded from bests-a\.bin", log)
        res.row("bests still read", "from bests-a.bin", bool(got), bool(got))
    # A best raised this session, written straight into the session block:
    # the save on the way back to the list does not care how it was raised.
    at = combo_best_at(base)
    raised = ctx.get("combo_best", 0) + 5
    emu.h.call("write_mem", addr=at, hex=f"{raised:04x}")
    pos = len(emu.log_text())
    nav.finish_training(emu)
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
    m = wait_line(emu, "the bests' save", r"bests: (not saved: [^\r\n]*|saved [^\r\n]*|save [^\r\n]*)", pos, 300)
    res.row("a raised best on a locked card", "not saved: the SD card is locked", m.group(1),
            m.group(1) == "not saved: the SD card is locked")
    res.row("the session keeps it", raised, emu.u16(at), emu.u16(at) == raised)
    return res


def check_card_unchanged(ctx):
    res = Result("locked: the card")
    after = tm_files(ctx["img"])
    same = after == ctx["locked_before"]
    res.row("tm/ after the locked boot", "byte for byte as before", sorted(after) if same else "changed", same)
    return res


def prepare_migrate(ctx):
    """Hand-made files for the migrate boot."""
    img = ctx["img"]
    a = parse_settings(ctx["settings_file"])
    areas = dict(a["areas"])
    # Older version: no OSD area; General with only its first 8 bytes (an
    # older build had fewer fields) and LedgeHud on; Controls of another
    # generation; an area this version does not know.
    gen, general = areas[AREA_GENERAL]
    general = bytearray(general[:8])
    general[SET["ledgeHud"] - AREA_AT[AREA_GENERAL]] = 1
    older = []
    for aid in sorted(areas):
        g, b = areas[aid]
        if aid == AREA_OSD:
            continue
        if aid == AREA_GENERAL:
            older.append((aid, g, bytes(general)))
        elif aid == AREA_CONTROLS:
            older.append((aid, g + 1, b))
        else:
            older.append((aid, g, b))
    older.append((30, 1, b"\x01\x02\x03\x04"))
    damaged = bytearray(build_settings([(aid, g, b) for aid, (g, b) in sorted(areas.items())], 7))
    damaged[60] ^= 0x40  # in the area bytes: the checksum no longer matches
    files = {"settings-a.bin": bytes(damaged), "settings-b.bin": build_settings(older, 6)}
    export = ctx["export_file"]
    names = []
    for k in range(1, 11):
        files[f"250101-0000{k:02d}.tmr"] = export
    body = bytearray(export)
    body[len(body) - 40] ^= 0x01                       # a frame byte: the header still reads
    files["250102-000000.tmr"] = bytes(body)
    bad = bytearray(export)
    bad[0] = 0x58                                       # not ours
    files["250103-000000.tmr"] = bytes(bad)
    old = bytearray(export)
    struct.pack_into(">H", old, 4, 0)                   # format 0
    files["250104-000000.tmr"] = bytes(old)
    new = bytearray(export)
    struct.pack_into(">H", new, 4, 2)                   # format 2
    files["250105-000000.tmr"] = bytes(new)
    files["250106-000000.tmr"] = export[:50]            # cut short
    w = fatimage.FatWriter(img)
    try:
        for n, data in files.items():
            w.put(f"{FOLDER}/{n}", data)
    finally:
        w.close()
    names = sorted([n for n in files if n.endswith(".tmr")] + [ctx["export"]], reverse=True)
    ctx["migrate_names"] = names
    return files


def browser_go(emu, row, at):
    """Down/Up to `row` from `at`; returns `row`."""
    for _ in range(abs(row - at)):
        emu.press(0, stick=(128, 28) if row > at else (128, 228), frames=3, after=6)
    return row


def s_migrate(emu, ctx):
    res = Result("migrate")
    names = ctx["migrate_names"]
    pos0 = len(emu.log_text())
    nav.boot_to_training_css(emu, drill_list="keep")
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    count, pos = open_browser(emu)
    m = wait_line(emu, "the headers", r"browser: headers read: ([^\r\n]*)", pos, 600)
    res.row("browser listing", "16 files: 12 ready, 1 newer, 1 older, 2 damaged", f"{count} files: {m.group(1)}",
            count == 16 and m.group(1) == "12 ready, 1 newer, 1 older, 2 damaged")
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-browser-page1")[0])
    at = 0
    expect = {names.index("250106-000000.tmr"): "This file is damaged and cannot be loaded.",
              names.index("250105-000000.tmr"): "Made by a newer version: update the mod to load it.",
              names.index("250104-000000.tmr"): "Made by an older version that cannot load it.",
              names.index("250103-000000.tmr"): "This file is damaged and cannot be loaded."}
    for row, text in sorted(expect.items()):
        at = browser_go(emu, row, at)
        pos = len(emu.log_text())
        emu.press(0, ["A"], frames=4, after=10)
        got = wait_line(emu, f"row {row}'s message", r"browser: (\S+): ([^\r\n]*)", pos, 120)
        res.row(f"A on {names[row]}", text, got.group(2), got.group(1) == names[row] and got.group(2) == text)
    ctx["shots"].append(emu.screenshot("storage-browser-older")[0])
    row = names.index("250102-000000.tmr")
    at = browser_go(emu, row, at)
    pos = len(emu.log_text())
    emu.press(0, ["A"], frames=4, after=10)
    got = wait_line(emu, "the corrupt file's check", r"browser: (\S+) failed its check: ([^\r\n]*)", pos, 300)
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-browser-corrupt")[0])
    res.row("A on a file whose body was changed", "loading stops: the file is damaged", got.group(0),
            got.group(1) == "250102-000000.tmr" and "damaged" in got.group(2) and "browser: starting" not in
            log_since(emu, pos))
    pos = len(emu.log_text())
    emu.press(0, ["R"], frames=4, after=10)
    wait_line(emu, "page 2", r"browser: row 10 ", pos, 60)
    emu.advance(10)
    ctx["shots"].append(emu.screenshot("storage-browser-page2")[0])
    at = 10
    res.row("R: page 2", f"row 10 ({names[10]})", "row 10", True)
    victim = names[12]
    at = browser_go(emu, 12, at)
    pos = len(emu.log_text())
    emu.press(0, ["X"], frames=4, after=10)
    emu.press(0, ["B"], frames=4, after=10)
    emu.advance(20)
    kept = "browser: deleted" not in log_since(emu, pos)
    emu.press(0, ["X"], frames=4, after=10)
    emu.advance(5)
    ctx["shots"].append(emu.screenshot("storage-browser-confirm")[0])
    emu.press(0, ["A"], frames=4, after=10)
    got = wait_line(emu, "the delete", r"browser: (deleted|delete) (\S+)", pos, 300)
    res.row("X then B, then X then A", f"kept, then {victim} deleted", got.group(0),
            kept and got.group(0) == f"browser: deleted {victim}")
    ctx["deleted"] = victim
    emu.press(0, ["B"], frames=4, after=10)
    emu.advance(20)
    into_quick_lab(emu)
    log = log_since(emu, pos0)
    m = re.search(r"storage: settings files: ([^\r\n]*)", log)
    res.row("both files read", "a damaged (0), b done (6)", m.group(1) if m else None,
            m is not None and m.group(1) == "a the file is damaged (0), b done (6)")
    m = re.search(r"storage: settings loaded from ([^\r\n]*)", log)
    copied = AREA_COUNT - 2
    res.row("the older file migrated", f"settings-b.bin: {copied} areas, 1 with new fields, 2 at defaults, 1 unknown",
            m.group(1) if m else None,
            m is not None and m.group(1).startswith(f"settings-b.bin (save 6, layout 1): {copied} areas, 1 with new "
                                                     "fields, 2 at defaults, 1 unknown"))
    res.row("its message", "Lab settings from an older version: new ones at defaults",
            bool(re.search(r"notice: Lab settings from an older version", log)),
            bool(re.search(r"notice: Lab settings from an older version", log)))
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    want = {"ledgeHud": 1, "cameraMode": DEFAULTS["cameraMode"], "moveStaling": DEFAULTS["moveStaling"],
            "wavedash": DEFAULTS["wavedash"], "lCancel": DEFAULTS["lCancel"],
            "advanceButton": DEFAULTS["advanceButton"], "advanceDelay": DEFAULTS["advanceDelay"]}
    got = read_settings(emu, base, want)
    res.row("fields: kept / new in General / OSD and Controls at defaults", want, got, got == want)
    migrated = read_settings(emu, base, DEFAULTS)
    migrated["advanceDelay"] += 1
    ctx["migrated"] = migrated
    emu.advance(30)
    ctx["shots"].append(emu.screenshot("storage-migrated-notice")[0])
    nav.pause_training(emu)
    lab.open_menu()
    lab.go_controls()
    lab.down(CONTROLS_HOLD_DELAY)
    pos = lab.log_pos()
    lab.tap(stick=LT.RIGHT)
    lab.back_to_main(2)
    lab.close_menu()
    m = wait_line(emu, "the change's write", r"storage: settings saved to (\S+) \(save (\d+)", pos, 300)
    res.row("Frame Advance Hold Delay +1, menu closed", "the migrated settings written to settings-a.bin, save 7",
            m.group(0), m.group(1) == "settings-a.bin" and m.group(2) == "7")
    return res


def check_card_after_migrate(ctx):
    res = Result("migrate: the card")
    files = tm_files(ctx["img"])
    a = parse_settings(files.get("settings-a.bin"))
    ok = a is not None and a["checksum_ok"] and a["sequence"] == 7
    want = ctx.get("migrated")
    got = {n: settings_value(a, n) for n in DEFAULTS} if a else None
    res.row("settings-a.bin after the change", f"save 7, good, the migrated settings {want}",
            a and (a["sequence"], got), ok and got == want)
    b = files.get("settings-b.bin")
    res.row("settings-b.bin (the older one) left alone", "unchanged", "unchanged" if b and parse_settings(b)["sequence"] == 6
            else "changed", bool(b) and parse_settings(b)["sequence"] == 6)
    res.row("the deleted recording", f"{ctx['deleted']} gone", ctx["deleted"] not in files, ctx["deleted"] not in files)
    return res


def prepare_errors(ctx):
    img = ctx["img"]
    good = ctx["settings_file"]
    s = parse_settings(good)
    newer = build_settings([(aid, g, b) for aid, (g, b) in sorted(s["areas"].items())], 9, fmt=2)
    files = {"settings-a.bin": good[:100], "settings-b.bin": newer}
    w = fatimage.FatWriter(img)
    try:
        removed = w.clear_folder(FOLDER)
        for n, data in files.items():
            w.put(f"{FOLDER}/{n}", data)
    finally:
        w.close()
    ctx["errors_files"] = files
    return removed


def s_errors(emu, ctx):
    res = Result("errors")
    pos0 = len(emu.log_text())
    nav.boot_to_training_css(emu, drill_list="keep")
    emu.wait_for("the drill list", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    count, pos = open_browser(emu)
    emu.advance(20)
    ctx["shots"].append(emu.screenshot("storage-browser-empty")[0])
    res.row("browser with no recordings", "0 listed (none found)", count, count == 0)
    emu.press(0, ["B"], frames=4, after=10)
    emu.advance(20)
    into_quick_lab(emu)
    log = log_since(emu, pos0)
    m = re.search(r"storage: settings files: ([^\r\n]*)", log)
    res.row("a cut short, b newer", "a damaged (0), b newer (9)", m.group(1) if m else None,
            m is not None and m.group(1) == "a the file is damaged (0), b made by a newer version (9)")
    res.row("its message", "from a newer version: not used",
            bool(re.search(r"notice: Lab settings are from a newer version: not used", log)),
            bool(re.search(r"notice: Lab settings are from a newer version: not used", log)))
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    got = read_settings(emu, base, DEFAULTS)
    res.row("settings at their defaults", DEFAULTS, got, got == DEFAULTS)
    emu.advance(20)
    ctx["shots"].append(emu.screenshot("storage-newer-notice")[0])
    nav.pause_training(emu)
    lab.open_menu()
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(60)
    unchanged = tm_files(ctx["img"]) == ctx["errors_files"]
    w = re.search(r"storage: settings saved", log_since(emu, pos))
    res.row("menu closed without a change", "no write; both files as they were",
            f"{w.group(0) if w else 'no write'}, files {'unchanged' if unchanged else 'changed'}",
            w is None and unchanged)
    lab.open_menu()
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    lab.down(OSD_WAVEDASH)
    lab.toggle_on(SET["wavedash"])
    lab.back_to_main(1)
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(60)
    log_after = log_since(emu, pos)
    w = re.search(r"storage: settings saved", log_after)
    skipped = "storage: settings not saved: newer format on the card" in log_after
    res.row("a change, menu closed", "not saved (newer format on card); change kept in RAM",
            f"{w.group(0) if w else 'no write'}, skipped={skipped}, wavedash={emu.u8(base + SET['wavedash'])}",
            w is None and skipped and emu.u8(base + SET["wavedash"]) == 1)
    return res


def check_card_after_errors(ctx):
    res = Result("errors: the card")
    files = tm_files(ctx["img"])
    res.row("settings-a.bin untouched", "unchanged",
            files.get("settings-a.bin") == ctx["errors_files"]["settings-a.bin"],
            files.get("settings-a.bin") == ctx["errors_files"]["settings-a.bin"])
    res.row("settings-b.bin (newer) kept", "unchanged", files.get("settings-b.bin") == ctx["errors_files"]["settings-b.bin"],
            files.get("settings-b.bin") == ctx["errors_files"]["settings-b.bin"])
    return res


def prepare_hazards(ctx):
    w = fatimage.FatWriter(ctx["img"])
    try:
        w.clear_folder(FOLDER)
    finally:
        w.close()


def _save_and_export_recording(emu, lab, res, label):
    r = RT.Runner(emu)
    r.refresh()
    emu.advance(30)
    pos = len(emu.log_text())
    r.run_request("save")
    st = r.step(1)[-1]["rec"]
    if not st["flags"] & RT.FLAG_SAVED:
        why = re.findall(r"record: (no storage[^\r\n]*|No room[^\r\n]*)", log_since(emu, pos))
        raise StorageError(f"{label}: Save Positions not taken (flags {st['flags']:#x}): {why[-2:] or 'no log line'}")
    emu.h.call("resume")
    nav.pause_training(emu)
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.down(25)
    pos = lab.log_pos()
    lab.tap(["A"])
    m = wait_line(emu, f"{label} export", r"storage: (exported (\S+) \((\d+) bytes\)|export \S+ failed[^\r\n]*)", pos)
    lab.close_menu()
    emu.advance(20)
    res.row(f"{label}: exported", "exported", m.group(1), m.group(1).startswith("exported"))
    return m.group(2)


def _load_newest_from_browser(emu, expected_count):
    state = emu.wait_for("the drill list state", lambda: nav.drill_list_state(emu), 600, every=5)
    emu.advance(20)
    if state == "closed":
        pos = len(emu.log_text())
        emu.press(0, ["L", "R"], frames=4, after=10)
        wait_line(emu, "the drill list to reopen", r"drills: list opened \(L\+R\)", pos, 120)
    count, pos = open_browser(emu)
    wait_line(emu, "the headers", rf"browser: headers read: {expected_count} ready", pos, 300)
    pos = len(emu.log_text())
    emu.press(0, ["A"], frames=4, after=6)
    wait_line(emu, "the import's match to start", r"browser: starting", pos, 300)
    seeded_match_start(emu)
    emu.advance(30)
    wait_line(emu, "the imported positions", r"record: positions imported \((savestate|lite[^)]*)\)", pos, 600)
    return count


def s_hazards(emu, ctx):
    """Exporting on Smashville with Hazards ON and with Hazards OFF via the
    stage select, then loading each back from the browser, must preserve
    gmGlobalModeMelee+0x29 bit 0x20 (m_isHazardOff) in both cases."""
    res = Result("hazards")
    nav.boot_to_training_css(emu)
    lab = LT.Lab(emu, [])

    # 1. Hazards ON on Smashville (explicit SSS pick, no Z).
    setup_on = nav.start_training_match(emu, "mario", "fox", stage="smashville", seed_rng=True, hazard_off=False)
    emu.advance(30)
    check_assist(emu, res, "hazards-on match")
    res.row("Smashville SSS Hazards ON", 0, setup_on["hazard_off"], setup_on["hazard_off"] == 0)
    rec_on = _save_and_export_recording(emu, lab, res, "hazards-on")
    ctx["hazard_rec_on"] = rec_on
    nav.finish_training(emu)

    # Load it back from the browser: m_isHazardOff must stay 0.
    _load_newest_from_browser(emu, 1)
    loaded_on = nav.melee_setup(emu)
    res.row("browser load keeps Hazards ON (m_isHazardOff == 0)", 0, loaded_on["hazard_off"],
            loaded_on["hazard_off"] == 0)
    nav.finish_training(emu)

    # 2. Hazards OFF on Smashville (SSS pick with Z).
    emu.wait_for("the drill list state", lambda: nav.drill_list_state(emu), 600, every=5)
    setup_off = nav.start_training_match(emu, "mario", "fox", stage="smashville", seed_rng=True, hazard_off=True)
    emu.advance(30)
    check_assist(emu, res, "hazards-off match")
    res.row("Smashville SSS Hazards OFF", 1, setup_off["hazard_off"], setup_off["hazard_off"] == 1)
    rec_off = _save_and_export_recording(emu, lab, res, "hazards-off")
    ctx["hazard_rec_off"] = rec_off
    nav.finish_training(emu)

    # Load it back from the browser (newest first -> row 0): m_isHazardOff must stay 1.
    _load_newest_from_browser(emu, 2)
    loaded_off = nav.melee_setup(emu)
    res.row("browser load keeps Hazards OFF (m_isHazardOff == 1)", 1, loaded_off["hazard_off"],
            loaded_off["hazard_off"] == 1)
    return res


def check_card_after_hazards(ctx):
    res = Result("hazards: the card")
    files = tm_files(ctx["img"])
    rec_on = parse_recording(files.get(ctx["hazard_rec_on"]))
    rec_off = parse_recording(files.get(ctx["hazard_rec_off"]))
    res.row("hazards-on export stores hazardOff == 0", 0,
            rec_on and rec_on["info"]["hazard_off"],
            rec_on is not None and rec_on["checksum_ok"] and rec_on["info"]["hazard_off"] == 0)
    res.row("hazards-off export stores hazardOff == 1", 1,
            rec_off and rec_off["info"]["hazard_off"],
            rec_off is not None and rec_off["checksum_ok"] and rec_off["info"]["hazard_off"] == 1)
    return res


# ---- the savestate, measured (--evaluate) -------------------------------------------------

def evaluate_save(emu, ctx, res, which):
    """After a recording-state save: its size, its compressibility read from
    the slot, the heap layout at that moment."""
    log = emu.log_text()
    m = list(re.finditer(r"savestate: recording state saved frame \d+: (\d+) bytes in (\d+) segments", log))
    b = list(re.finditer(r"savestate: slot buffers ([0-9a-f]{8})\+([0-9a-f]+), ([0-9a-f]{8})\+([0-9a-f]+)", log))
    if not m or not b:
        res.note("evaluate: no save line")
        return
    size = int(m[-1].group(1))
    chunks = [(int(b[-1].group(1), 16), int(b[-1].group(2), 16)), (int(b[-1].group(3), 16), int(b[-1].group(4), 16))]
    emu.h.call("pause")
    data, left = bytearray(), size + 8 * int(m[-1].group(2))
    t0 = time.monotonic()
    for addr, length in chunks:
        at = 0
        while left > 0 and at < length:
            n = min(0x40000, left, length - at)
            data += emu.read(addr + at, n)
            at += n
            left -= n
    heaps = heaps_now(emu)
    emu.h.call("resume")
    t1 = time.monotonic()
    fast = len(zlib.compress(bytes(data), 1))
    best = len(zlib.compress(bytes(data), 9))
    zero = bytes(data).count(0)
    ctx.setdefault("eval", {})[which] = {"size": size, "read": len(data), "z1": fast, "z9": best, "zero": zero,
                                         "heaps": heaps}
    res.note(f"evaluate {which}: state {size} bytes, read {len(data)} in {t1 - t0:.1f}s; zlib level 1 {fast} "
             f"({100 * fast / len(data):.1f}%), level 9 {best} ({100 * best / len(data):.1f}%); zero bytes "
             f"{100 * zero / len(data):.1f}%")


def evaluate_report(ctx):
    res = Result("evaluate")
    ev = ctx.get("eval", {})
    if "a" not in ev or "b" not in ev:
        res.note("no measurements")
        return res
    a = {h[0]: h[1:] for h in ev["a"]["heaps"]}
    b = {h[0]: h[1:] for h in ev["b"]["heaps"]}
    moved = [n for n in a if n in b and a[n][:2] != b[n][:2]]
    differ = [n for n in a if n in b and a[n][2:] != b[n][2:]]
    res.note(f"heaps whose range differs between the export's match and the import's: {moved or 'none'}")
    res.note(f"heaps whose block count or bytes in use differ: {len(differ)} of {len(a)}: {differ[:12]}")
    return res


# ---- running ----------------------------------------------------------------------------------

BOOTS = [
    ("save", s_save, False, None, check_card_after_save),
    ("load", s_load, False, None, None),
    ("locked", s_locked, True, None, check_card_unchanged),
    ("migrate", s_migrate, False, prepare_migrate, check_card_after_migrate),
    ("errors", s_errors, False, prepare_errors, check_card_after_errors),
    ("hazards", s_hazards, False, prepare_hazards, check_card_after_hazards),
]


def cmd_test_storage(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-storage")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--evaluate", action="store_true")
    opts = ap.parse_args(rest)
    label = "test-storage"
    visual_backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    headless = E.headless_backend()
    t0 = time.monotonic()
    ctx = {"shots": [], "evaluate": opts.evaluate}
    results, why, logs, failed_keys = [], "", [], []
    wanted = [b[0] for b in BOOTS] if not opts.only else opts.only
    # Every boot except the self-contained hazards boot needs what "save" leaves on the card.
    if "save" not in wanted and wanted != ["hazards"]:
        wanted = ["save"] + wanted
    ctx["img"] = E.storage_image(opts.rel)
    for name, fn, locked, prepare, check in BOOTS:
        if name not in wanted:
            continue
        first = len(results)
        step = f"{name}: setup"
        try:
            if prepare is not None:
                prepare(ctx)
            step = name
            # Only the "load" boot asserts on rendered pixels (value_box_on on the OSD menu).
            need_visual = (name == "load")
            boot_backend = visual_backend if need_visual else headless
            with E.harness_run(boot_backend, E.DETERMINISTIC_ARGS, writable_image=ctx["img"], locked=locked,
                               allow_null=not need_visual) as (proc, emu):
                logs.append(emu.log_path)
                try:
                    res = fn(emu, ctx)
                except (Timeout, nav.NavError, HarnessError, StorageError, RT.RecError, C.CpuError,
                        LT.LabError) as e:
                    why = f"{step}: {e}"
                    try:
                        emu.h.call("resume")
                        emu.screenshot(f"fail-storage-{name}")
                    except (OSError, HarnessError, ValueError):
                        pass
                    res = None
                log = E.read_log(emu.log_path)
            bad = L.problems(E.read_log(logs[-1]))
            if res is not None:
                for line in bad:
                    res.row("problem line", "none", line, False)
                results.append(res)
            if why:
                break
            if check is not None:
                results.append(check(ctx))
        except (OSError, HarnessError, Timeout, ValueError, KeyError, AttributeError) as e:
            why = why or f"{step}: {type(e).__name__}: {e}"
            break
        for r in results[first:]:
            print(f"{r.name}: {'PASS' if r.ok else 'FAIL'}")
            for line in r.lines:
                print("  " + line)
        if any(not r.ok for r in results[first:]) and name not in failed_keys:
            failed_keys.append(name)
    if opts.evaluate:
        results.append(evaluate_report(ctx))
        for line in results[-1].lines:
            print("  " + line)
    print()
    print(f"{'boot':<18} {'check':<58} {'expected':<30} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<17} {what[:58]:<58} {exp[:30]:<30} {obs}")
    print("screenshots: " + ", ".join(os.path.basename(p) for p in ctx["shots"]))
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and not all(res.ok for res in results):
        if failed_keys:
            print("failed --only: " + " ".join(failed_keys))
        why = "a check failed: " + ", ".join(sorted({res.name for res in results if not res.ok}))
    if why:
        return L.report(logs[-1] if logs else "", why, label)
    E.remove_storage_image(ctx["img"])
    print(f"{label}: PASS")
    return 0
