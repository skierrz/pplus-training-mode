"""Live Dolphin verification for the Olimar Lab Kit (Stage & Cast > Olimar).

Runs all 7 end-to-end checks in one headless boot on Final Destination:
  1. Stage & Cast -> Olimar > hidden for Mario vs Fox, visible & opens P_OLIMAR for Olimar
  2. Pull Pool Filter (Purple+White subset + all-0 safe clamp to 0x1F)
  3. Per-Color Maturity Locks (Red=Flower, Blue=Leaf, Purple=Bud, Yellow=Default aging)
  4. Starting Lineup (4-slot Purple/Yellow/White/Red + 2-slot Purple/Yellow)
  5. Live B + D-Pad Pluck on frame 0 with zero Savestate/Taunt collisions
  6. Move Lead Now + Down-B Whistle Lead (List 0 reorder + Magus antenna LA-Basic[93])
  7. Instant D-Pad Lineup Presets + SpecialHi/attack state gate & pending latch + 12 rapid swaps + clean exit
"""
import argparse
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import charge_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import fighter_mechanics_tests as FM  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

MARIO = (0x00, 0x00)
FOX = (0x07, 0x07)
OLIMAR = (0x1A, 0x18)

# FighterMechanicsSettings offsets (base 432).
S_FM = 432
S_OLI_PIKMIN = S_FM + 23       # 455 (u8)
S_OLI_MATURITY = S_FM + 24     # 456 (u8)
S_OLI_PULL_WHISTLE = S_FM + 25 # 457 (u8: bits 0..4 pull pool, bits 5..7 whistle color)
S_OLI_CMAT_MODE = S_FM + 26    # 458 (u16: 5x 2-bit per-color mat + 2-bit dpad mode)
S_OLI_START = S_FM + 28        # 460 (u16: 4x 3-bit start slots)
S_OLI_PRESETS = S_FM + 30      # 462 (u16: 4x 4-bit dpad presets)

# Log patterns for menu navigation & detach verification.
OPEN_RE = re.compile(r"lab: menu opened .* on page (\d+) \(([^)]+)\)")
MOVE_LEAD_RE = re.compile(r"olimar: moved lead color (\d+) to front \(List0 size (\d+)\)")
SS_SAVED_RE = re.compile(r"savestate: saved")
SS_LOADED_RE = re.compile(r"savestate: loaded")

P_STAGE_CAST = 6
P_OLIMAR = 29


def write_u8(emu, base, off, val):
    emu.h.call("write_mem", addr=base + off, hex=f"{val & 0xFF:02x}")
    CT.wait_game_frames(emu, 2)


def write_u16(emu, base, off, val):
    emu.h.call("write_mem", addr=base + off, hex=f"{val & 0xFFFF:04x}")
    CT.wait_game_frames(emu, 2)


def reset_olimar_settings(emu, settings):
    write_u8(emu, settings, S_OLI_PIKMIN, 0)
    write_u8(emu, settings, S_OLI_MATURITY, 0)
    write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F)
    write_u16(emu, settings, S_OLI_CMAT_MODE, 0)
    write_u16(emu, settings, S_OLI_START, 0)
    write_u16(emu, settings, S_OLI_PRESETS, 0)


def read_list0_colors(emu, fv):
    """Walk ftPikmin + 0x1FD08 (soArrayList<wnPikminPikmin*, 10>) in logical order."""
    f = fv._entry()["fighter"]
    if not emu.is_ram(f):
        return []
    mgr = f + 0x1FD08
    vptr = emu.u32(mgr)
    if vptr != 0x80B19960:
        return []
    packed = emu.u32(mgr + 0x04)
    top = (packed >> 22) & 0x1F
    size = (packed >> 12) & 0x1F
    if top == 0x1F or size == 0 or size > 10:
        return []
    units = emu.read(mgr + 0x08, 10 * 12)
    out = []
    idx = top
    seen = set()
    while 0 <= idx < 10 and idx not in seen and len(out) < size:
        seen.add(idx)
        base = idx * 12
        pik = struct.unpack_from(">I", units, base + 4)[0]
        nxt = struct.unpack_from(">b", units, base + 9)[0]
        if emu.is_ram(pik):
            color = CT.s32(emu.read(pik + 0x24FC, 4), 0)
            out.append(color)
        idx = nxt
    return out


def pikmin_color_and_maturity(emu, fv):
    """Return [(color, la8_maturity, la9_timer, vis0), ...] for all active Pikmin articles."""
    mgr = CT.u32(fv._mods(), 0x84)
    if not emu.is_ram(mgr):
        return []
    arr = emu.ptr(mgr + 0x2C)
    if not arr:
        return []
    vt = emu.ptr(arr)
    sz_fn = emu.ptr(vt + 0x14) if vt else 0
    cap_fn = emu.ptr(vt + 0x3C) if vt else 0
    if not sz_fn or not cap_fn:
        return []
    w0, w1, w2 = struct.unpack(">3I", emu.read(sz_fn, 12))
    word = emu.u32(arr + 0x08)
    if (w1 >> 26) != 21 or (w2 >> 26) != 31:
        return []
    sh, mb, me, sr = (w1 >> 11) & 31, (w1 >> 6) & 31, (w1 >> 1) & 31, (w2 >> 11) & 31
    rot = ((word << sh) | (word >> ((32 - sh) & 31))) & 0xFFFFFFFF
    mask = ((1 << (32 - mb)) - 1) ^ ((1 << (31 - me)) - 1)
    count = (rot & mask) >> sr
    width = 32 - sr
    top = (word >> (32 - width)) & ((1 << width) - 1)
    cap = emu.u32(cap_fn) & 0xFFFF
    if count <= 0 or cap <= 0 or count > 16:
        return []
    out = []
    for i in range(count):
        slot_idx = (top + i) % cap
        art = emu.ptr(arr + 0x0C + 4 * slot_idx)
        if not art:
            continue
        avt = emu.ptr(art)
        if not avt:
            continue
        top_off = CT.s32(emu.read(avt + 4, 4), 0)
        if top_off > 0 or top_off < -0x200:
            continue
        obj = (art + top_off) & 0xFFFFFFFF
        acc = emu.ptr(obj + 0x60)
        enum_ptr = emu.ptr(acc + 0xD8) if acc else 0
        if not enum_ptr:
            continue
        mods = emu.read(enum_ptr, 0xCC)
        wm = CT.u32(mods, 0x64)
        vis = CT.u32(mods, 0x58)
        if not emu.is_ram(wm) or not emu.is_ram(vis):
            continue
        la = emu.ptr(wm + 0x20)
        la_ints = emu.ptr(la + 0x0C) if la else 0
        if not la_ints:
            continue
        color = CT.s32(emu.read(la_ints + 5 * 4, 4), 0)
        la8 = CT.s32(emu.read(la_ints + 8 * 4, 4), 0)
        la9 = CT.s32(emu.read(la_ints + 9 * 4, 4), 0)
        set_data = emu.ptr(vis + 0x24)
        vis0 = struct.unpack(">b", emu.read(set_data, 1))[0] if set_data else -1
        out.append((color, la8, la9, vis0))
    return out


def tap_btn(emu, buttons, frames=3, wait=6):
    emu.hold(0, buttons=list(buttons))
    CT.wait_game_frames(emu, frames)
    emu.neutral(0)
    CT.wait_game_frames(emu, wait)


def menu_tap(emu, buttons, hold_polls=4, release_polls=6):
    emu.press(0, buttons=list(buttons), frames=hold_polls, after=release_polls)


def run_all(rel_path):
    E.cmd_install(rel_path)
    backend = E.headless_backend()
    t0 = time.monotonic()
    fails = []
    notes = []
    log_path = ""

    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
        log_path = emu.log_path
        nav.boot_to_training_css(emu)

        # ------------------------------------------------------------------
        # 1. Stage & Cast -> Olimar > hidden for Mario vs Fox
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 1a mario vs fox menu check")
        nav.start_quick_match(emu, stage="final_destination", p1=MARIO, cpu=FOX, seed_rng=True)
        emu.neutral(0)
        CT.wait_game_frames(emu, 35)
        # Open Lab Menu (Start), switch to Stage & Cast (R x6 from Lab), press Down 4 times + A.
        menu_tap(emu, ["START"])
        for _ in range(6):
            menu_tap(emu, ["R"])
        # On FD Mario vs Fox, visible rows in Stage & Cast are ONLY row 0 (Fighter Swap) & row 1 (Show Inactive).
        # Pressing Down twice lands on row 1 (Show Inactive), NOT Olimar; pressing A toggles Show Inactive,
        # and closing/re-opening confirms we are still on Stage & Cast (page 6), never P_OLIMAR (29).
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["A"])
        pos = len(emu.log_text())
        menu_tap(emu, ["START"])
        menu_tap(emu, ["START"])
        m_open = OPEN_RE.findall(emu.log_text()[pos:])
        mario_page = int(m_open[-1][0]) if m_open else -1
        menu_tap(emu, ["START"])
        if mario_page != P_STAGE_CAST:
            fails.append(f"mario vs fox entered page {mario_page} instead of staying on Stage & Cast (6)")
        notes.append(f"1a mario_vs_fox_stage_cast_page={mario_page}")
        nav.finish_training(emu)

        # ------------------------------------------------------------------
        # 1b. Stage & Cast -> Olimar > visible & opens P_OLIMAR for Olimar
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 1b olimar menu check")
        nav.start_quick_match(emu, stage="final_destination", p1=OLIMAR, cpu=FOX, seed_rng=True)
        emu.neutral(0)
        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
        emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
        emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
        reset_olimar_settings(emu, settings)
        CT.wait_game_frames(emu, 30)
        p1 = CT.FighterView(emu, 0)
        CT.wait_idle(emu, p1, 120)

        # Open menu -> Stage & Cast (R x6 from Lab) -> Down x2 (row 0 Fighter Swap -> row 1 Show Inactive -> row 33 Olimar) -> A!
        menu_tap(emu, ["START"])
        for _ in range(6):
            menu_tap(emu, ["R"])
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["A"])
        pos = len(emu.log_text())
        menu_tap(emu, ["START"])  # close while on P_OLIMAR
        menu_tap(emu, ["START"])  # reopen: logs current page!
        m_open = OPEN_RE.findall(emu.log_text()[pos:])
        oli_page = int(m_open[-1][0]) if m_open else -1
        # Back out to Stage & Cast and close menu cleanly.
        menu_tap(emu, ["B"])
        menu_tap(emu, ["START"])
        if oli_page != P_OLIMAR:
            fails.append(f"olimar menu did not open P_OLIMAR (29): got {oli_page}")
        notes.append(f"1b olimar_submenu_page={oli_page}")

        # Find Olimar pullOutParam weight address from log.
        found = FM.OLIMAR_FOUND_RE.findall(emu.log_text())
        if not found:
            fails.append("Olimar Pikmin weights table not validated in log")
            w_addr = 0
        else:
            w_addr = int(found[-1], 16)

        def read_weights():
            return [round(v, 1) for v in struct.unpack(">5f", emu.read(w_addr, 20))] if w_addr else []

        # ------------------------------------------------------------------
        # 2. Pull Pool Filter (Purple + White only = 0x18, plus all-0 clamp)
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 2 pull pool filter")
        # Use Presets mode briefly to drop to 1 Pikmin so we have 3 free pluck slots, then switch back to D-Pad Off.
        write_u16(emu, settings, S_OLI_PRESETS, 14)  # D-Up = 1 Purple (14)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 2 << 10)  # Lineup Presets
        tap_btn(emu, ["DUP"], frames=3, wait=10)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 0)  # D-Pad Off
        CT.dpad_save(emu)  # save 1-Pikmin state for Pull Pool + B+D-Pad tests!

        # Pull Pool = White (bit 3) + Purple (bit 4) = 0x18.
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x18)
        w_pw = read_weights()
        pw_plucks = []
        for _ in range(3):
            tap_btn(emu, ["B"], frames=4, wait=4)
            CT.wait_idle(emu, p1, 80)
            pw_plucks.append(p1.la_int(64))
        CT.dpad_load(emu)
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x18)
        for _ in range(3):
            tap_btn(emu, ["B"], frames=4, wait=4)
            CT.wait_idle(emu, p1, 80)
            pw_plucks.append(p1.la_int(64))
        if w_pw != [0.0, 0.0, 0.0, 1.0, 1.0] or any(c not in (3, 4) for c in pw_plucks):
            fails.append(f"pull pool P+W: weights={w_pw} plucks={pw_plucks}")

        # All-0 Pull Pool safety clamp (0 -> 0x1F original weights, never aborts plucking).
        CT.dpad_load(emu)
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x00)
        w_zero_clamped = read_weights()
        cnt_before = len(read_list0_colors(emu, p1))
        tap_btn(emu, ["B"], frames=4, wait=4)
        CT.wait_idle(emu, p1, 80)
        cnt_after = len(read_list0_colors(emu, p1))
        if w_zero_clamped != [1.0, 1.0, 1.0, 1.0, 1.0] or cnt_after != cnt_before + 1:
            fails.append(f"all-0 pull pool clamp: weights={w_zero_clamped} cnt={cnt_before}->{cnt_after}")
        notes.append(f"2 pull_pool: pw_w={w_pw} pw_plucks={pw_plucks} zero_clamp={w_zero_clamped} cnt={cnt_before}->{cnt_after}")

        # ------------------------------------------------------------------
        # 3. Per-Color Maturity Locks (Red=Flower, Blue=Leaf, Purple=Bud, Yellow=Default)
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 3 per-color maturity")
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F)
        write_u16(emu, settings, S_OLI_PRESETS, 11)  # D-Up = Y+P+B+R (11)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 2 << 10)  # Lineup Presets
        tap_btn(emu, ["DUP"], frames=3, wait=10)
        # Pack per-color maturity: Red=Flower(3), Yellow=Default(0), Blue=Leaf(1), White=Default(0), Purple=Bud(2), Mode=0.
        cmat = (3 << 0) | (0 << 2) | (1 << 4) | (0 << 6) | (2 << 8)
        write_u16(emu, settings, S_OLI_CMAT_MODE, cmat)
        CT.wait_game_frames(emu, 35)
        mat_map = {c: (la8, la9, vis0) for c, la8, la9, vis0 in pikmin_color_and_maturity(emu, p1)}
        ok_mat = (
            0 in mat_map and mat_map[0][0] == 2 and mat_map[0][2] == 2 and 0 <= mat_map[0][1] <= 1
            and 2 in mat_map and mat_map[2][0] == 0 and mat_map[2][2] == 0 and 0 <= mat_map[2][1] <= 1
            and 4 in mat_map and mat_map[4][0] == 1 and mat_map[4][2] == 1 and 0 <= mat_map[4][1] <= 1
            and 1 in mat_map and mat_map[1][1] >= 15
        )
        if not ok_mat:
            fails.append(f"per-color maturity: {mat_map}")
        notes.append(f"3 per_color_maturity={mat_map}")
        write_u16(emu, settings, S_OLI_CMAT_MODE, 0)

        # ------------------------------------------------------------------
        # 4. Starting Lineup (4-slot P+Y+W+R and 2-slot P+Y) on fresh matches
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 4 starting lineup")
        # 4a: Slot 1=Purple(5), Slot 2=Yellow(2), Slot 3=White(4), Slot 4=Red(1).
        start_pywr = 5 | (2 << 3) | (4 << 6) | (1 << 9)
        write_u16(emu, settings, S_OLI_START, start_pywr)
        nav.finish_training(emu)
        nav.start_quick_match(emu, stage="final_destination", p1=OLIMAR, cpu=FOX, seed_rng=True)
        emu.neutral(0)
        CT.wait_game_frames(emu, 40)
        p1 = CT.FighterView(emu, 0)
        CT.wait_idle(emu, p1, 120)
        start_list = read_list0_colors(emu, p1)
        if start_list != [4, 1, 3, 0]:
            fails.append(f"starting lineup 4-slot: got {start_list}, want [4, 1, 3, 0]")
        # 4b: Slot 1=Purple(5), Slot 2=Yellow(2), Slot 3=Empty(6), Slot 4=Empty(6).
        start_py2 = 5 | (2 << 3) | (6 << 6) | (6 << 9)
        write_u16(emu, settings, S_OLI_START, start_py2)
        nav.finish_training(emu)
        nav.start_quick_match(emu, stage="final_destination", p1=OLIMAR, cpu=FOX, seed_rng=True)
        emu.neutral(0)
        CT.wait_game_frames(emu, 40)
        p1 = CT.FighterView(emu, 0)
        CT.wait_idle(emu, p1, 120)
        start_list_2 = read_list0_colors(emu, p1)
        if start_list_2 != [4, 1]:
            fails.append(f"starting lineup 2-slot: got {start_list_2}, want [4, 1]")
        notes.append(f"4 start_lineup_pywr={start_list} start_lineup_py2={start_list_2}")
        write_u16(emu, settings, S_OLI_START, 0)

        # ------------------------------------------------------------------
        # 5. Live B + D-Pad Pluck (Frame-0 Safe, Zero Savestate / Taunt Collisions)
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 5 live B+D-Pad pluck")
        # Prepare a clean 1-Pikmin savestate first (while D-Pad Mode == Off so quick save works).
        write_u16(emu, settings, S_OLI_PRESETS, 14)  # D-Up = 1 Purple
        write_u16(emu, settings, S_OLI_CMAT_MODE, 2 << 10)
        tap_btn(emu, ["DUP"], frames=3, wait=10)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 0)
        CT.dpad_save(emu)

        # Switch to D-Pad Mode = B+D-Pad Pluck (1), Pull Pool = All (0x1F).
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 1 << 10)
        pos_ss = len(emu.log_text())

        # Pluck 3 colors in a row: B+D-Up (Yellow=1), B+D-Left (Blue=2), B+D-Right held 15f (White=3).
        pluck_results = []
        for btns, hold_f in ((["B", "DUP"], 5), (["B", "DLEFT"], 5), (["B", "DRIGHT"], 15)):
            emu.hold(0, buttons=btns)
            CT.wait_game_frames(emu, hold_f)
            emu.neutral(0)
            CT.wait_idle(emu, p1, 80)
            pluck_results.append(p1.la_int(64))

        # Reload 1-Pikmin state (temporarily D-Pad Off -> load -> back to B+D-Pad Pluck),
        # then pluck B+D-Down (Purple=4) and bare B (Red=0).
        ss_log_1 = emu.log_text()[pos_ss:]
        if SS_SAVED_RE.search(ss_log_1) or SS_LOADED_RE.search(ss_log_1):
            fails.append("B+D-Pad Pluck triggered savestate save/load!")
        write_u16(emu, settings, S_OLI_CMAT_MODE, 0)
        CT.dpad_load(emu)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 1 << 10)
        pos_ss2 = len(emu.log_text())
        for btns, hold_f in ((["B", "DDOWN"], 5), (["B"], 5)):
            emu.hold(0, buttons=btns)
            CT.wait_game_frames(emu, hold_f)
            emu.neutral(0)
            CT.wait_idle(emu, p1, 80)
            pluck_results.append(p1.la_int(64))
        ss_log_2 = emu.log_text()[pos_ss2:]
        if SS_SAVED_RE.search(ss_log_2) or SS_LOADED_RE.search(ss_log_2):
            fails.append("B+D-Pad Pluck (Down/bare) triggered savestate!")
        if pluck_results != [1, 2, 3, 4, 0]:
            fails.append(f"B+D-Pad Pluck colors: got {pluck_results}, want [1, 2, 3, 4, 0]")
        notes.append(f"5 b_dpad_pluck={pluck_results} zero_ss_collision=True")

        # ------------------------------------------------------------------
        # 6. Move Lead Now & Down-B Whistle Lead (+ Magus Antenna Sync)
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 6 move lead now & whistle lead")
        write_u16(emu, settings, S_OLI_PRESETS, 10)  # D-Up = P+Y+W+R [4, 1, 3, 0]
        write_u16(emu, settings, S_OLI_CMAT_MODE, 2 << 10)
        tap_btn(emu, ["DUP"], frames=3, wait=10)
        # Set Whistle / Lead = White (4 -> color 3).
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F | (4 << 5))
        # Open Lab Menu -> navigate to Stage & Cast (R x6) -> Olimar (DDOWN x2 -> A) -> row 7 (Move Lead Now) -> A!
        pos_ml = len(emu.log_text())
        menu_tap(emu, ["START"])
        for _ in range(6):
            menu_tap(emu, ["R"])
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["A"])  # open P_OLIMAR
        for _ in range(7):
            menu_tap(emu, ["DDOWN"])
        menu_tap(emu, ["A"])  # Move Lead Now!
        menu_tap(emu, ["B"])
        menu_tap(emu, ["START"])
        CT.wait_game_frames(emu, 6)
        lead_after_move = read_list0_colors(emu, p1)
        ant_after_move = p1.la_int(93)
        ml_logged = bool(MOVE_LEAD_RE.search(emu.log_text()[pos_ml:]))
        if not lead_after_move or lead_after_move[0] != 3 or ant_after_move != 3 or not ml_logged:
            fails.append(f"Move Lead Now: list0={lead_after_move} ant={ant_after_move} logged={ml_logged}")

        # Now set Whistle / Lead = Yellow (2 -> color 1) and press Down-B (SpecialLw).
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F | (2 << 5))
        emu.hold(0, buttons=["B"], stick=CT.DOWN)
        CT.wait_game_frames(emu, 6)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 80)
        CT.wait_game_frames(emu, 6)
        lead_after_whistle = read_list0_colors(emu, p1)
        ant_after_whistle = p1.la_int(93)
        if not lead_after_whistle or lead_after_whistle[0] != 1 or ant_after_whistle != 1:
            fails.append(f"Down-B Whistle Lead: list0={lead_after_whistle} ant={ant_after_whistle}")
        notes.append(f"6 move_lead={lead_after_move}(ant={ant_after_move}) whistle={lead_after_whistle}(ant={ant_after_whistle})")

        # ------------------------------------------------------------------
        # 7. Instant D-Pad Lineup Presets + SpecialHi Gate & Pending Latch + 12 Rapid Swaps
        # ------------------------------------------------------------------
        emu.mark("olimar-lab: 7 dpad presets, state gate, rapid swaps")
        # D-Up = 6 (2Y+2P), D-Down = 13 (P+W), D-Left = 10 (P+Y+W+R), D-Right = 14 (1 Purple).
        presets_word = 6 | (13 << 4) | (10 << 8) | (14 << 12)
        write_u16(emu, settings, S_OLI_PRESETS, presets_word)
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x1F)
        write_u16(emu, settings, S_OLI_CMAT_MODE, 2 << 10)  # Lineup Presets
        pos_pr = len(emu.log_text())

        tap_btn(emu, ["DUP"], frames=3, wait=8)
        p_up = read_list0_colors(emu, p1)
        tap_btn(emu, ["DDOWN"], frames=3, wait=8)
        p_dn = read_list0_colors(emu, p1)
        tap_btn(emu, ["DLEFT"], frames=3, wait=8)
        p_lt = read_list0_colors(emu, p1)
        # Hold D-Right 15f to prove no savestate save triggers.
        tap_btn(emu, ["DRIGHT"], frames=15, wait=8)
        p_rt = read_list0_colors(emu, p1)

        pr_log = emu.log_text()[pos_pr:]
        if SS_SAVED_RE.search(pr_log) or SS_LOADED_RE.search(pr_log):
            fails.append("Lineup Presets triggered savestate save/load!")
        if p_up != [1, 1, 4, 4] or p_dn != [4, 3] or p_lt != [4, 1, 3, 0] or p_rt != [4]:
            fails.append(f"dpad presets: up={p_up} dn={p_dn} lt={p_lt} rt={p_rt}")

        # State-gate & pending latch test:
        # 1) Jump + Up-B (SpecialHi Jetpack) and press D-Up mid-Up-B -> zero crash!
        emu.hold(0, buttons=["X"])
        CT.wait_game_frames(emu, 6)
        emu.neutral(0)
        CT.wait_game_frames(emu, 6)
        emu.hold(0, buttons=["B"], stick=CT.UP)
        upb_statuses = []
        for _ in range(12):
            CT.wait_game_frames(emu, 1)
            upb_statuses.append(p1.status())
        sh_st = upb_statuses[-1]
        emu.hold(0, buttons=["DUP"])
        CT.wait_game_frames(emu, 3)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 180)
        # 2) Press Jab (A, short move < 15 frames) and press D-Up mid-jab -> pending latch fires right on exit!
        tap_btn(emu, ["DRIGHT"], frames=3, wait=8)  # set to 1 Purple [4]
        emu.hold(0, buttons=["A"])
        CT.wait_game_frames(emu, 3)
        emu.hold(0, buttons=["DUP"])
        CT.wait_game_frames(emu, 3)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 60)
        latch_list = read_list0_colors(emu, p1)
        if latch_list != [1, 1, 4, 4]:
            fails.append(f"pending latch after jab: got {latch_list}, want [1, 1, 4, 4]")

        # 12 rapid swaps in a row: verify zero Pikmin slot leak in soInstancePoolSub.
        rapid_seq = [
            ("DUP", [1, 1, 4, 4]),
            ("DDOWN", [4, 3]),
            ("DLEFT", [4, 1, 3, 0]),
            ("DRIGHT", [4]),
        ] * 3
        rapid_ok = True
        for btn, want in rapid_seq:
            tap_btn(emu, [btn], frames=2, wait=5)
            got = read_list0_colors(emu, p1)
            art_n = p1.active_articles()
            if got != want or art_n != len(want):
                fails.append(f"rapid swap {btn}: list0={got} art={art_n} want={want}")
                rapid_ok = False
                break
        notes.append(
            f"7 presets=(up={p_up},dn={p_dn},lt={p_lt},rt={p_rt}) upb_sts={[hex(x) for x in upb_statuses]} "
            f"latch={latch_list} rapid12_ok={rapid_ok}"
        )

        # Leave Pull Pool narrowed (0x18) before exit so detach weight restoration is verified!
        write_u8(emu, settings, S_OLI_PULL_WHISTLE, 0x18)
        pos_exit = len(emu.log_text())
        nav.finish_training(emu)
        exit_log = emu.log_text()[pos_exit:]
        detached = bool(FM.OLIMAR_DETACH_RE.search(exit_log))
        baseline_ok = "back to baseline: yes" in exit_log
        if not detached:
            fails.append("detach did not log restoring Olimar weights")
        if not baseline_ok:
            fails.append("exit did not report back to baseline: yes")
        notes.append(f"exit: detach_restored={detached} back_to_baseline={baseline_ok}")

    print("\n--- olimar lab verification ---")
    for line in notes:
        print(" ", line)
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if bad:
        fails.append("problem lines in Dolphin log")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        return L.report(log_path, fails[0], "olimar-lab")
    print("olimar-lab: PASS")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("rel", nargs="?", default="build/TrainingMode.rel")
    args = ap.parse_args()
    raise SystemExit(run_all(args.rel))
