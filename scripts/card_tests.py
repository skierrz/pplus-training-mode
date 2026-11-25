"""Emulator and static test of a ready-made Project+ SD card image (run through
emu.py; settings come from the environment the same way).

  test-card CARD [--rel REL] [--ref REF] [--only NAME ...] [--keep-copy]
      Static checks (before copying or booting CARD):
        static   /Project+/pf/plugins/<basename(REL)> on CARD is byte-identical
                 to REL, and (with --ref) CARD differs from REF only in the
                 plugin's data clusters, its directory entries in pf/plugins,
                 the matching FAT entries (every FAT copy identical), and
                 FSInfo's free-cluster fields. Reports the count of differing
                 512-byte sectors.

      Emulator scenarios on a writable copy of CARD next to SD_IMAGE (never
      booting or writing CARD itself; writes allowed, folder sync off, one CPU
      thread, test pads):
        play     boot with no button held to Training's character select with
                 the drill list open; check the boot log's plugin version;
                 choose Training Lab and start a match through the real
                 controller path; check the on-screen watermark; open the lab
                 menu with Start, change persisted settings and check the save
                 log and the file on the card; leave the match cleanly back to
                 Training's character select.
        persist  close Dolphin and boot again on the same copy; check the
                 changed settings come back in memory and no "Lab settings"
                 error notice is logged.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import cpu_tests as C
import emu as E
import fatimage
import lab_tests as LT
import loader_tests as L
import nav
import shots
import storage_tests as ST
from harness import HarnessError, Timeout

PLUGIN_DIR = "/Project+/pf/plugins"

# main::Settings offsets read from memory and from parsed settings files.
SET = dict(ST.SET, startOpensMenu=LT.SET_START_OPENS, techInvisMode=288, techInvisDelay=289)

# Persisted settings changed through the lab menu in the fresh-card scenario.
FRESH_CHANGES = {"cameraMode": 1, "moveStaling": 0}

# Lab page rows (src/main/ui/pages/general.cpp).
GEN_CAMERA = LT.GEN_CAMERA
GEN_MOVE_STALING = LT.GEN_STALING

Result = C.Result


class CardError(RuntimeError):
    pass


def read_settings(emu, base, names):
    return {n: (emu.u16(base + SET[n]) if n in ST.HALF else emu.u8(base + SET[n])) for n in names}


def setting_in_file(parsed, name):
    at = SET[name]
    for aid, start in ST.AREA_AT.items():
        gen_bytes = parsed["areas"].get(aid)
        if gen_bytes is None:
            continue
        b = gen_bytes[1]
        if start <= at < start + len(b):
            if name in ST.HALF:
                return struct.unpack_from(">H", b, at - start)[0]
            return b[at - start]
    return None


# ---- static checks (read-only on CARD and REF) -------------------------------------

def _dir_cluster_slots(fat, clus):
    """(byte offset from image start, 32-byte entry) for every slot in directory `clus`."""
    if clus == 0 and not fat.fat32:
        raw = fat._read(fat.rootstart, fat.rootsecs * fat.bps)
        return [(fat.base + fat.rootstart + i, raw[i:i + 32]) for i in range(0, len(raw), 32)]
    out = []
    for cl in fat._chain(clus if clus else fat.rootclus):
        base = fat.datastart + (cl - 2) * fat.csize
        raw = fat._read(base, fat.csize)
        for i in range(0, fat.csize, 32):
            out.append((fat.base + base + i, raw[i:i + 32]))
    return out


def _locate_entry_offsets(fat, dir_clus, target_name):
    """List of image-relative byte offsets of `target_name`'s LFN + 8.3 directory slots."""
    pending, lfn = [], []
    for off, e in _dir_cluster_slots(fat, dir_clus):
        if e[0] == 0:
            break
        if e[0] == 0xE5:
            pending, lfn = [], []
            continue
        if e[11] == 0x0F:
            pending.append(off)
            lfn.insert(0, e[1:11] + e[14:26] + e[28:32])
            continue
        if e[11] & 0x08:
            pending, lfn = [], []
            continue
        if lfn:
            name = b"".join(lfn).decode("utf-16-le", "replace").split("\x00")[0]
        else:
            b = e[0:8].decode("latin-1").rstrip()
            x = e[8:11].decode("latin-1").rstrip()
            name = b + ("." + x if x else "")
        if name.lower() == target_name.lower():
            return pending + [off]
        pending, lfn = [], []
    return []


def _cluster_sectors(fat, clusters):
    """Set of 512-byte sector indices covered by `clusters`."""
    out = set()
    for c in clusters:
        start = fat.base + fat.datastart + (c - 2) * fat.csize
        for off in range(start, start + fat.csize, 512):
            out.add(off // 512)
    return out


def _fat_diff_ok(card_fat, ref_fat, allowed_clusters):
    """True if every FAT copy in `card_fat` equals copy 0 and only entries in
    `allowed_clusters` differ from `ref_fat`."""
    size = card_fat.fatsz * card_fat.bps
    copies = [card_fat._read(card_fat.fatstart + k * size, size) for k in range(card_fat.nfats)]
    if not all(c == copies[0] for c in copies[1:]):
        return False, "FAT copies on card are not identical"
    a, b = copies[0], ref_fat.fat
    if len(a) != len(b):
        return False, f"FAT size differs ({len(a)} vs {len(b)})"
    stride = 4 if card_fat.fat32 else 2
    bad = []
    for i in range(0, len(a), stride):
        if a[i:i + stride] != b[i:i + stride]:
            c = i // stride
            if c not in allowed_clusters:
                bad.append(c)
                if len(bad) >= 8:
                    break
    if bad:
        return False, f"FAT entries outside plugin changed: clusters {bad}"
    return True, "ok"


def _fsinfo_diff_ok(card_fat, ref_fat):
    """For FAT32: FSInfo sector may differ only in bytes 488..496 (free count / hint)."""
    if not card_fat.fat32:
        return set(), True, "not FAT32"
    fsinfo = struct.unpack_from("<H", card_fat._read(0, 512), 48)[0]
    if not (0 < fsinfo < 0xFFFF):
        return set(), True, "no FSInfo"
    off = card_fat.base + fsinfo * card_fat.bps
    a = card_fat._read(fsinfo * card_fat.bps, 512)
    b = ref_fat._read(fsinfo * ref_fat.bps, 512)
    if a == b:
        return {off // 512}, True, "unchanged"
    ok = a[:488] == b[:488] and a[496:] == b[496:]
    return {off // 512}, ok, "free count / hint only" if ok else "bytes outside 488..496 changed"


def _scan_diff_sectors(path_a, path_b):
    """List of 512-byte sector indices where `path_a` and `path_b` differ."""
    diffs = []
    chunk = 4 * 1024 * 1024
    with open(path_a, "rb") as fa, open(path_b, "rb") as fb:
        off = 0
        while True:
            a = fa.read(chunk)
            b = fb.read(chunk)
            if not a and not b:
                break
            if a != b:
                n = min(len(a), len(b))
                for i in range(0, n, 512):
                    if a[i:i + 512] != b[i:i + 512]:
                        diffs.append((off + i) // 512)
                if len(a) != len(b):
                    extra_start = off + n
                    extra_end = off + max(len(a), len(b))
                    for pos in range(extra_start, extra_end, 512):
                        diffs.append(pos // 512)
            off += max(len(a), len(b))
    return diffs


def check_static(card_path, rel_path, ref_path):
    res = Result("static")
    if not os.path.isfile(card_path):
        res.row("card image exists", card_path, "missing", False)
        return res
    if not os.path.isfile(rel_path):
        res.row("plugin build exists", rel_path, "missing", False)
        return res
    plugin_name = os.path.basename(rel_path)
    plugin_path = f"{PLUGIN_DIR}/{plugin_name}"
    with open(rel_path, "rb") as f:
        want = f.read()
    got = fatimage.read_file(card_path, plugin_path)
    want_sha = hashlib.sha256(want).hexdigest()[:16]
    got_sha = hashlib.sha256(got).hexdigest()[:16] if got is not None else "missing"
    res.row(f"{plugin_path} matches {os.path.basename(rel_path)}",
            f"{len(want)} bytes ({want_sha})",
            f"{len(got)} bytes ({got_sha})" if got is not None else "missing",
            got == want)

    if not ref_path:
        res.note("no REF given: skipping reference card diff")
        return res
    if not os.path.isfile(ref_path):
        res.row("reference card exists", ref_path, "missing", False)
        return res

    card_size, ref_size = os.path.getsize(card_path), os.path.getsize(ref_path)
    res.row("card size matches reference", f"{ref_size} bytes", f"{card_size} bytes", card_size == ref_size)
    if card_size != ref_size or got is None:
        return res

    cf = fatimage.sd_extract.Fat(card_path)
    rf = fatimage.sd_extract.Fat(ref_path)
    try:
        geom = lambda f: (f.base, f.fat32, f.bps, f.spc, f.rsvd, f.nfats, f.fatsz, f.rootstart, f.datastart, f.csize)
        same_geom = geom(cf) == geom(rf) and cf.bps == 512
        res.row("FAT geometry matches reference", "identical, 512-byte sectors",
                "identical" if same_geom else f"card {geom(cf)} vs ref {geom(rf)}", same_geom)
        if not same_geom:
            return res

        plug_clus, isdir, _ = cf.find(plugin_path)
        if isdir:
            res.row("plugin is a regular file", "file", "directory", False)
            return res
        card_chain = list(cf._chain(plug_clus)) if plug_clus else []
        try:
            ref_plug_clus, _, _ = rf.find(plugin_path)
            ref_chain = list(rf._chain(ref_plug_clus)) if ref_plug_clus else []
        except FileNotFoundError:
            ref_chain = []
        ref_set = set(ref_chain)
        stolen = [c for c in card_chain if rf._next(c) != 0 and c not in ref_set]
        res.row("plugin clusters were free on reference", f"{len(card_chain)} clusters free",
                f"{len(stolen)} already used {stolen[:5]}" if stolen else f"all {len(card_chain)} free", not stolen)

        pdir_clus, _, _ = cf.find(PLUGIN_DIR)
        ref_pdir_clus, _, _ = rf.find(PLUGIN_DIR)
        card_dir_chain = list(cf._chain(pdir_clus))
        ref_dir_chain = list(rf._chain(ref_pdir_clus))
        new_dir_clus = [c for c in card_dir_chain if c not in ref_dir_chain]
        stolen_dir = [c for c in new_dir_clus if rf._next(c) != 0]

        card_entry_offs = set(_locate_entry_offsets(cf, pdir_clus, plugin_name))
        ref_entry_offs = set(_locate_entry_offsets(rf, ref_pdir_clus, plugin_name))
        allowed_entry_offs = card_entry_offs | ref_entry_offs

        # Every non-plugin 32-byte slot in existing pf/plugins clusters must be unchanged.
        stray_slots = []
        for cl in ref_dir_chain:
            base = cf.datastart + (cl - 2) * cf.csize
            a_raw, b_raw = cf._read(base, cf.csize), rf._read(base, cf.csize)
            for i in range(0, cf.csize, 32):
                off = cf.base + base + i
                if a_raw[i:i + 32] != b_raw[i:i + 32] and off not in allowed_entry_offs:
                    stray_slots.append(off)
        res.row("pf/plugins directory entries outside plugin", "unchanged",
                f"{len(stray_slots)} stray slot(s)" if stray_slots else f"unchanged ({len(card_entry_offs)} plugin slots)",
                not stray_slots and not stolen_dir and bool(card_entry_offs))

        changed_clus = set(card_chain) | ref_set | set(new_dir_clus)
        if new_dir_clus and ref_dir_chain:
            changed_clus.add(ref_dir_chain[-1])

        fat_ok, fat_msg = _fat_diff_ok(cf, rf, changed_clus)
        res.row("FAT tables (copies identical, plugin clusters only)", "ok", fat_msg, fat_ok)

        fsinfo_sectors, fs_ok, fs_msg = _fsinfo_diff_ok(cf, rf)
        res.row("FSInfo sector", "free count / hint only", fs_msg, fs_ok)

        data_sectors = _cluster_sectors(cf, set(card_chain) | ref_set)
        dir_sectors = {off // 512 for off in allowed_entry_offs} | _cluster_sectors(cf, new_dir_clus)
        stride = 4 if cf.fat32 else 2
        fat_sectors = set()
        for k in range(cf.nfats):
            fbase = cf.base + cf.fatstart + k * cf.fatsz * cf.bps
            for c in changed_clus:
                fat_sectors.add((fbase + c * stride) // 512)
        allowed = data_sectors | dir_sectors | fat_sectors | fsinfo_sectors

        t0 = time.monotonic()
        diffs = _scan_diff_sectors(card_path, ref_path)
        elapsed = time.monotonic() - t0
        unexpected = [s for s in diffs if s not in allowed]
        n_data = sum(1 for s in diffs if s in data_sectors)
        n_dir = sum(1 for s in diffs if s in dir_sectors)
        n_fat = sum(1 for s in diffs if s in fat_sectors)
        n_fs = sum(1 for s in diffs if s in fsinfo_sectors)
        summary = (f"{len(diffs)} differing 512-byte sectors "
                   f"({n_data} data, {n_dir} dir, {n_fat} FAT, {n_fs} FSInfo; "
                   f"{len(unexpected)} unexpected) in {elapsed:.1f}s")
        res.row("differing 512-byte sectors vs reference", "0 unexpected sectors", summary, not unexpected)
        if unexpected:
            res.note(f"first unexpected sectors: {unexpected[:16]}")
    finally:
        cf.close()
        rf.close()
    return res


# ---- real controller navigation helpers --------------------------------------------

def boot_to_drill_list(emu):
    """Boot with no button held until Training's character select opens with
    the drill list up."""
    emu.wait_for("Training's character select after boot",
                 lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
    name, seq, _ = nav.scene(emu)
    if seq != "sqTraining":
        raise CardError(f"booted to {name} / {seq}, not Training")
    emu.wait_for("the drill list to open", lambda: nav.drill_list_state(emu) == "open", 900, every=5)
    emu.advance(15)


def choose_training_lab(emu):
    """On the open training menu: choose Training Lab (row 0) with A."""
    emu.press(0, ["A"], frames=4, after=6)
    emu.wait_for("the drill list to close after Training Lab",
                 lambda: nav.drill_list_state(emu) == "closed", 180, every=2)
    emu.advance(15)


def open_drill_list_lr(emu):
    """On Training's character select with the list closed: L+R opens it."""
    nav.press_until(emu, "the drill list to open with L+R", ["L", "R"],
                    lambda: nav.drill_list_state(emu) == "open", 300, every=20)
    emu.advance(15)


# ---- scenarios ---------------------------------------------------------------------

def s_play(emu, ctx):
    res = Result("play")
    pos0 = len(emu.log_text())
    boot_to_drill_list(emu)
    log = ST.log_since(emu, pos0)
    ours, syringe = E.expected_lines()
    ver_ok = ours in log and syringe in log and log.index(ours) < log.index(syringe)
    res.row("boot log plugin version", f"{ours} before {syringe}",
            "present in order" if ver_ok else "missing or out of order", ver_ok)
    auto_open = bool(re.search(r"\] drills: list opened \(auto\)", log))
    res.row("Training CSS with drill list open", "sqTraining, drill list opened (auto)",
            f"{nav.scene(emu)[1]}, auto={'yes' if auto_open else 'no'}", auto_open)

    choose_training_lab(emu)
    nav.start_training_match(emu, "mario", "fox", "final_destination")
    emu.advance(60)
    log = ST.log_since(emu, pos0)
    card_m = re.search(r"storage: card ([^\r\n]*)", log)
    res.row("card seen as writable", "inserted, writable", card_m.group(1) if card_m else None,
            "storage: card inserted, writable" in log)

    shot_path = emu.screenshot("card-match")[0]
    ctx["shots"].append(shot_path)
    white, dark = shots.watermark_score(shot_path)
    mark = shots.watermark_visible(shot_path)
    res.row("on-screen watermark drawn", "visible",
            f"{'yes' if mark else 'no'} (white {white:.3f}, outline {dark:.3f})", mark)

    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    opened = lab.open_menu()
    res.row("lab menu opens with Start", "Start", opened.group("how"), opened.group("how") == "Start")

    # open_menu() focuses the Category Rail on row 0 (Lab); A enters Lab
    # (Camera -> Zoom, Move Staling -> Off).
    lab.tap(["A"])
    lab.down(GEN_CAMERA)
    ST.tap_until(lab, "cameraMode", base, FRESH_CHANGES["cameraMode"], LT.RIGHT)
    lab.down(GEN_MOVE_STALING - GEN_CAMERA)
    lab.toggle_off(SET["moveStaling"])
    lab.back_to_main(1)
    pos = lab.log_pos()
    lab.close_menu()
    saved = ST.wait_line(emu, "the settings save",
                         r"storage: settings (saved to (\S+) \(save (\d+), (\d+) bytes\)|save to \S+ failed.*)",
                         pos, 300)
    got = read_settings(emu, base, FRESH_CHANGES)
    res.row("settings changed in memory", FRESH_CHANGES, got, got == FRESH_CHANGES)
    res.row("settings saved to card", "settings-a.bin, save 1", saved.group(0),
            saved.group(2) == "settings-a.bin" and saved.group(3) == "1")

    # Leave the Training Lab match back to Training's character select.
    nav.finish_training(emu)
    emu.wait_for("the drill list to stay closed after leaving Training Lab",
                 lambda: nav.drill_list_state(emu) == "closed", 300, every=5)
    emu.advance(30)
    at_css = nav.scene(emu)[:2] == ("scSelctCharacter", "sqTraining")
    res.row("Training Lab match left", "scSelctCharacter / sqTraining", nav.scene(emu)[:2], at_css)
    return res


def check_card_after_play(ctx):
    res = Result("play: the card")
    files = ST.tm_files(ctx["img"])
    s = ST.parse_settings(files.get("settings-a.bin"))
    ok = s is not None and s["checksum_ok"] and s["format"] == 1 and s["sequence"] == 1 and s["layout"] == 1
    res.row("settings-a.bin on the card", "format 1, save 1, layout 1, checksum good",
            s and (s["format"], s["sequence"], s["layout"], s["checksum_ok"]), ok)
    if s:
        got = {n: setting_in_file(s, n) for n in FRESH_CHANGES}
        res.row("saved values in settings-a.bin", FRESH_CHANGES, got, got == FRESH_CHANGES)
    return res


def s_persist(emu, ctx):
    res = Result("persist")
    pos0 = len(emu.log_text())
    boot_to_drill_list(emu)
    choose_training_lab(emu)
    nav.start_training_match(emu, "mario", "fox", "final_destination")
    emu.advance(30)
    log = ST.log_since(emu, pos0)
    loaded = re.search(r"storage: settings loaded from ([^\r\n]*)", log)
    res.row("settings loaded on second boot", "settings-a.bin (save 1, layout 1)",
            loaded.group(1) if loaded else None,
            loaded is not None and loaded.group(1).startswith("settings-a.bin (save 1, layout 1):"))
    notices = re.findall(r"storage: notice: (Lab settings[^\r\n]*)", log)
    res.row("no Lab settings error notice", "none", notices or "none", not notices)
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    want = dict(FRESH_CHANGES, startOpensMenu=1)
    got = read_settings(emu, base, want)
    res.row("changed settings persisted in memory", want, got, got == want)
    nav.finish_training(emu)
    return res


# ---- runner ------------------------------------------------------------------------

ALL_PARTS = ("static", "play", "persist")


def cmd_test_card(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-card")
    ap.add_argument("card", help="path to the SD card image to test (read-only)")
    ap.add_argument("--rel", default=os.path.join(E.REPO, "build", "TrainingMode.rel"),
                    help="plugin build to compare against /Project+/pf/plugins/<name> on the card")
    ap.add_argument("--ref", default="",
                    help="optional reference SD card image for the sector diff check")
    ap.add_argument("--only", nargs="*", choices=ALL_PARTS, default=None)
    ap.add_argument("--keep-copy", action="store_true", help="keep the test card copy after a pass")
    opts = ap.parse_args(rest)

    label = "test-card"
    visual_backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    headless = E.headless_backend()
    t0 = time.monotonic()
    wanted = list(ALL_PARTS) if not opts.only else list(opts.only)
    if "persist" in wanted and "play" not in wanted:
        wanted.insert(wanted.index("persist"), "play")

    ctx = {"shots": []}
    results, why, logs = [], "", []

    if "static" in wanted:
        res = check_static(opts.card, opts.rel, opts.ref)
        results.append(res)
        print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}")
        for line in res.lines:
            print("  " + line)
        if not res.ok:
            why = "static check failed"

    boot_steps = [
        ("play", s_play, None, check_card_after_play),
        ("persist", s_persist, None, None),
    ]
    need_boots = [b for b in boot_steps if b[0] in wanted]

    if not why and need_boots:
        ctx["img"] = E.copy_card_image(opts.card)
        for name, fn, prep, check in need_boots:
            first = len(results)
            step = f"{name}: setup"
            try:
                if prep is not None:
                    results.append(prep(ctx))
                step = name
                # Only "play" checks rendered pixels (watermark_visible on card-match).
                need_visual = (name == "play")
                boot_backend = visual_backend if need_visual else headless
                with E.harness_run(boot_backend, E.DETERMINISTIC_ARGS, writable_image=ctx["img"], locked=False,
                                   allow_null=not need_visual) as (proc, emu):
                    logs.append(emu.log_path)
                    try:
                        res = fn(emu, ctx)
                    except (Timeout, nav.NavError, HarnessError, CardError, ST.StorageError, LT.LabError) as e:
                        why = f"{step}: {e}"
                        try:
                            emu.h.call("resume")
                            emu.screenshot(f"fail-card-{name}")
                        except (OSError, HarnessError, ValueError):
                            pass
                        res = None
                bad = L.problems(E.read_log(logs[-1]))
                if res is not None:
                    for line in bad:
                        res.row("problem line", "none", line, False)
                    results.append(res)
                if why:
                    break
                if check is not None:
                    results.append(check(ctx))
            except (OSError, HarnessError, Timeout, ValueError, KeyError, AttributeError, CardError) as e:
                why = why or f"{step}: {type(e).__name__}: {e}"
                break
            for r in results[first:]:
                print(f"{r.name}: {'PASS' if r.ok else 'FAIL'}")
                for line in r.lines:
                    print("  " + line)

    print()
    print(f"{'stage':<18} {'check':<54} {'expected':<30} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<17} {what[:54]:<54} {exp[:30]:<30} {obs}")
    if ctx["shots"]:
        print("screenshots: " + ", ".join(os.path.basename(p) for p in ctx["shots"]))
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and not all(res.ok for res in results):
        why = "a check failed: " + ", ".join(sorted({res.name for res in results if not res.ok}))
    if why:
        return L.report(logs[-1] if logs else "", why, label)
    if "img" in ctx and not opts.keep_copy:
        E.remove_storage_image(ctx["img"])
    print(f"{label}: PASS")
    return 0
