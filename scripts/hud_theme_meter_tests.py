"""Emulator tests for Theme v2, multi-HUD layout, and overhead timing meter:
  - Theme v2: Display > Theme Menu Accent presets (P+ Teal / Brawl Navy /
    Subspace Violet / Crimson Steel) and Info Display Background + Opacity
    with drop shadow, verified visually in D3D and across SD round-trip.
  - Overhead Timing Helper Meter ("TMTM" RAM debug block): real P+ 3.2.1
    L-Cancel (hit + miss), Wavedash (frame-1 hit), and Act-Out countdown/flash.
  - Multi-HUD 4:3 and 16:9 stress layouts (Info Display + Input Viewer + DI
    Compass + OSD + Timing Meter) captured as crisp D3D evidence screenshots.

  test-hud-theme-meter REL [--only NAME ...] [--list]
"""
import argparse
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import charge_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import plugin_input as PI  # noqa: E402
import shots  # noqa: E402
import stage4a_core_tests as S4A  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

CHARS = {
    "mario": (0x00, 0x00),
    "fox": (0x07, 0x07),
}

# Settings offsets (src/main/settings.h).
S_LEDGE_HUD = 12
S_LEDGE_GALINT = 14
S_LEDGE_PANEL = 15
S_DI_DISPLAY = 19
S_INPUT_VIEWER = 52
S_DI_VECTOR_MODE = 54
S_HMN_INFO_SIZE = 57
S_HMN_INFO_ROWS = 58
S_CPU_INFO_SIZE = 73
S_CPU_INFO_ROWS = 74
S_OSD_POSITION = 88
S_OSD_WAVEDASH = 92
S_OSD_LCANCEL = 93
S_OSD_LEDGEDASH = 100
S_OSD_BG_COLOR = 110
S_OSD_BG_ALPHA = 111
S_MENU_ACCENT = 112
S_INFO_BG_COLOR = 113
S_INFO_BG_ALPHA = 114
S_TIMING_METER = 115

# TimingMeterDebugBlock constants (src/extra/timing_meter.cpp).
TMTM_MAGIC = 0x544D544D
TMTM_VERSION = 1
TMTM_HEADER_SIZE = 16
TMTM_SLOT_SIZE = 40
TMTM_MAX_SLOTS = 4
TMTM_RE = re.compile(r"timing_meter: debug block at ([0-9a-f]{8})")

TM_KIND_LCANCEL = 1
TM_KIND_WAVEDASH = 2
TM_KIND_ACTOUT = 3

TM_OUT_HIT = 1
TM_OUT_EARLY = 2
TM_OUT_LATE = 3
TM_OUT_MISS = 4

SCENARIOS = [
    "theme-v2-persist",
    "timing-meter-and-hud-16x9",
    "hud-and-meter-4x3",
]


class HudThemeMeterError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def find_tmtm(emu):
    m = list(TMTM_RE.finditer(emu.synced_log_text()))
    if not m:
        raise HudThemeMeterError("timing_meter TMTM debug block address not found in log")
    return int(m[-1].group(1), 16)


def read_tmtm(emu, addr):
    raw = emu.read(addr, TMTM_HEADER_SIZE + TMTM_MAX_SLOTS * TMTM_SLOT_SIZE)
    magic, version, frame, mode = struct.unpack_from(">IIIB", raw, 0)
    slots = []
    for i in range(TMTM_MAX_SLOTS):
        base = TMTM_HEADER_SIZE + i * TMTM_SLOT_SIZE
        (active, kind, outcome, hold_frames, last_delta, act_in,
         ratio, world_x, world_y, ev_count, lc_hits, wd_hits,
         act_flashes, draw_count) = struct.unpack_from(">BBBBhhfffIIIII", raw, base)
        slots.append({
            "active": active,
            "kind": kind,
            "outcome": outcome,
            "holdFrames": hold_frames,
            "lastDelta": last_delta,
            "actInFrames": act_in,
            "ratio": ratio,
            "worldX": world_x,
            "worldY": world_y,
            "eventCount": ev_count,
            "lCancelHits": lc_hits,
            "wavedashHits": wd_hits,
            "actOutFlashes": act_flashes,
            "drawCount": draw_count,
        })
    return {
        "magic": magic,
        "version": version,
        "frame": frame,
        "mode": mode,
        "slots": slots,
    }


def read_fighter_pos_air(emu, fighter_ptr):
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0x78)
    posture = u32(mods, 0x0C)
    situation = u32(mods, 0x14)
    status_mod = u32(mods, 0x70)
    p = emu.read(posture, 0x18)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    air = emu.u32(situation + 0x2C) == 2
    st = emu.u32(status_mod + 0x34)
    return {"x": x, "y": y, "air": air, "status": st}


def tap(emu, buttons, hold=3, settle=10):
    emu.hold(0, buttons)
    emu.advance(hold)
    emu.neutral(0)
    emu.advance(settle)


def shot_while_paused(emu, name):
    emu.h.call("resume")
    path = emu.screenshot(name)[0]
    emu.h.call("pause")
    return path


def open_theme_page(emu, lab):
    """Opens the Lab Menu with Start and navigates to Display > Theme."""
    tap(emu, ["Start"], hold=3, settle=12)
    # Category Rail starts on Lab (0) -> press R four times to reach Display (4).
    for _ in range(4):
        tap(emu, ["R"], hold=3, settle=10)
    # Row 17 on Display is Theme.
    for _ in range(17):
        tap(emu, ["DDOWN"], hold=2, settle=5)
    tap(emu, ["A"], hold=3, settle=12)


def sample_header_rgb(png_path):
    """Average RGB of the Lab Menu header band (top-center inside the frame)."""
    im = shots.Shot(png_path, 1.0)
    r_sum = g_sum = b_sum = n = 0
    for x in range(int(im.w * 0.35), int(im.w * 0.65), 4):
        for y in range(int(im.h * 0.075), int(im.h * 0.105), 2):
            r, g, b = im.px(x, y)
            r_sum += r
            g_sum += g
            b_sum += b
            n += 1
    if n == 0:
        return (0, 0, 0)
    return (r_sum // n, g_sum // n, b_sum // n)


def start_match(emu, p1_name="fox", cpu_name="mario", aspect=None, notes=None):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    if aspect is not None:
        LT.set_aspect(emu, aspect, notes.append if notes is not None else (lambda _s: None))
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    CT.wait_game_frames(emu, 24)
    f = LT.Fighters(emu)
    p1 = f.by_player(0)
    cpu = f.by_player(1)
    if p1 is None or cpu is None:
        raise HudThemeMeterError("fighters not found after match start")
    return lab, settings, p1, cpu


def run_theme_v2_persist(rel, backend, notes, shots_taken):
    fails = []
    img = E.storage_image(rel)
    try:
        # Boot 1: drive Display > Theme via controller, capture Menu Accents + translucent Info, save to SD.
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, writable_image=img, locked=False, allow_null=False) as (
            _proc, emu
        ):
            lab, settings, _p1, _cpu = start_match(emu, "mario", "fox", notes=notes)
            # Enable HMN Info Display rows (State, Frame, Actionable, ActIn) so we can photograph translucent Info.
            emu.h.call("write_mem", addr=settings + S_HMN_INFO_SIZE, hex="01")
            emu.h.call("write_mem", addr=settings + S_HMN_INFO_ROWS, hex="0203040500000000")
            emu.advance(8)

            open_theme_page(emu, lab)
            # Row 0: OSD Background, Row 1: OSD Opacity, Row 2: Menu Accent, Row 3: Info Background, Row 4: Info Opacity.
            tap(emu, ["DDOWN"], hold=2, settle=6)
            tap(emu, ["DDOWN"], hold=2, settle=6)

            # Menu Accent = 1 (Brawl Navy).
            tap(emu, ["DRIGHT"], hold=2, settle=10)
            p_navy = emu.screenshot("theme-menu-brawl-navy-16x9")[0]
            shots_taken.append(p_navy)

            # Menu Accent = 2 (Subspace Violet).
            tap(emu, ["DRIGHT"], hold=2, settle=10)
            p_violet = emu.screenshot("theme-menu-subspace-violet-16x9")[0]
            shots_taken.append(p_violet)

            # Menu Accent = 3 (Crimson Steel).
            tap(emu, ["DRIGHT"], hold=2, settle=10)
            p_crimson = emu.screenshot("theme-menu-crimson-steel-16x9")[0]
            shots_taken.append(p_crimson)

            # Row 3: Info Background -> 2 (Brawl Navy).
            tap(emu, ["DDOWN"], hold=2, settle=6)
            tap(emu, ["DRIGHT"], hold=2, settle=6)
            tap(emu, ["DRIGHT"], hold=2, settle=6)

            # Row 4: Info Opacity -> 2 (20% with drop-shadow).
            tap(emu, ["DDOWN"], hold=2, settle=6)
            tap(emu, ["DRIGHT"], hold=2, settle=6)
            tap(emu, ["DRIGHT"], hold=2, settle=6)

            accent_val = emu.u8(settings + S_MENU_ACCENT)
            info_bg_val = emu.u8(settings + S_INFO_BG_COLOR)
            info_alpha_val = emu.u8(settings + S_INFO_BG_ALPHA)
            notes.append(f"theme-v2 boot1 menu values: accent={accent_val} infoBg={info_bg_val} infoAlpha={info_alpha_val}")
            if (accent_val, info_bg_val, info_alpha_val) != (3, 2, 2):
                fails.append(f"Theme v2 menu nav mismatch: got {(accent_val, info_bg_val, info_alpha_val)}, want (3, 2, 2)")

            # Close menu -> triggers SD settings save; wait for save + photograph translucent Info Display.
            tap(emu, ["Start"], hold=3, settle=30)
            p_info = emu.screenshot("theme-info-translucent-16x9")[0]
            shots_taken.append(p_info)
            emu.advance(30)

            rgb_navy = sample_header_rgb(p_navy)
            rgb_violet = sample_header_rgb(p_violet)
            rgb_crimson = sample_header_rgb(p_crimson)
            notes.append(f"header RGBs: navy={rgb_navy} violet={rgb_violet} crimson={rgb_crimson}")
            if rgb_navy == rgb_violet or rgb_violet == rgb_crimson or rgb_navy == rgb_crimson:
                fails.append(f"Menu Accent presets did not change rendered header RGBs: {rgb_navy}, {rgb_violet}, {rgb_crimson}")
            # Crimson header should have higher R than B; Navy header should have higher B than R.
            if not (rgb_navy[2] > rgb_navy[0] and rgb_crimson[0] > rgb_crimson[2]):
                fails.append(f"Menu Accent header hue check failed: navy={rgb_navy} crimson={rgb_crimson}")

            bad = L.problems(E.read_log(emu.log_path))
            if bad:
                fails.append(f"boot1 problem lines: {bad[:3]}")

        # Boot 2: verify Theme v2 settings persisted across SD card round-trip!
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, writable_image=img, locked=False, allow_null=False) as (
            _proc, emu
        ):
            _lab, settings, _p1, _cpu = start_match(emu, "mario", "fox", notes=notes)
            accent_val = emu.u8(settings + S_MENU_ACCENT)
            info_bg_val = emu.u8(settings + S_INFO_BG_COLOR)
            info_alpha_val = emu.u8(settings + S_INFO_BG_ALPHA)
            notes.append(f"theme-v2 boot2 persisted: accent={accent_val} infoBg={info_bg_val} infoAlpha={info_alpha_val}")
            if (accent_val, info_bg_val, info_alpha_val) != (3, 2, 2):
                fails.append(f"Theme v2 SD persistence mismatch: got {(accent_val, info_bg_val, info_alpha_val)}, want (3, 2, 2)")
            bad = L.problems(E.read_log(emu.log_path))
            if bad:
                fails.append(f"boot2 problem lines: {bad[:3]}")
    finally:
        E.remove_storage_image(img)
    return fails


def do_fox_wavedash(emu):
    """Executes a frame-1 Fox wavedash via PluginInput."""
    inp = PI.PluginInput(emu)
    wd_frames = {
        1: (PI.B_JUMP, 0.0, 0.0),
        4: (PI.B_GUARD, 0.96, -0.28),
        5: (PI.B_GUARD, 0.96, -0.28),
    }
    inp.run({0: PI.frames_to_steps(wd_frames, 7)}, extra=5)


def do_shorthop_nair(emu, p1_ptr, press_lcancel):
    """Short-hop Nair on Mario with optional L-cancel R press right before landing."""
    def step(n=1):
        emu.h.call("frame_advance", n=n)

    emu.hold(0, ["X"])
    step(2)
    emu.neutral(0)
    for _ in range(8):
        step(1)
        if read_fighter_pos_air(emu, p1_ptr)["air"]:
            break
    emu.hold(0, ["A"])
    step(2)
    emu.neutral(0)

    pressed = False
    prev_y = -100.0
    for _ in range(55):
        cur = read_fighter_pos_air(emu, p1_ptr)
        falling = cur["y"] < prev_y
        prev_y = cur["y"]
        if press_lcancel and not pressed and cur["air"] and falling and cur["y"] < 4.5:
            emu.hold(0, ["R"])
            step(2)
            emu.neutral(0)
            pressed = True
            continue
        step(1)
        cur2 = read_fighter_pos_air(emu, p1_ptr)
        if not cur2["air"] and cur2["status"] == 0x18:
            step(2)
            break
    emu.neutral(0)


def trigger_ledgedash_for_hud(emu, p1_ptr):
    """Performs a clean Fox ledgedash on Final Destination so Ledgedash HUD + GALINT OSD appear."""
    posture = S4A.read_fighter_full(emu, p1_ptr)["posture"]
    S4A.grab_fd_right_ledge(emu, p1_ptr, posture, start_from_stage=True)
    emu.neutral(0)
    emu.h.call("frame_advance", n=1)
    emu.hold(0, stick=S4A.DOWN)
    for _ in range(8):
        emu.h.call("frame_advance", n=1)
        if S4A.read_fighter_full(emu, p1_ptr)["status"] == S4A.ST_FALL:
            break
    emu.hold(0, ["X"], stick=S4A.LEFT)
    emu.h.call("frame_advance", n=2)
    emu.hold(0, stick=S4A.LEFT)
    for _ in range(18):
        emu.h.call("frame_advance", n=1)
        if S4A.read_fighter_full(emu, p1_ptr)["y"] > 1.0:
            break
    emu.hold(0, ["R"], stick=(38, 60))
    emu.h.call("frame_advance", n=3)
    emu.neutral(0)
    for _ in range(35):
        emu.h.call("frame_advance", n=1)
        cur = S4A.read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == S4A.ST_WAIT:
            break
    emu.h.call("frame_advance", n=2)


def enable_multi_hud(emu, settings):
    emu.h.call("write_mem", addr=settings + S_LEDGE_HUD, hex="01")
    emu.h.call("write_mem", addr=settings + S_LEDGE_GALINT, hex="01")
    emu.h.call("write_mem", addr=settings + S_LEDGE_PANEL, hex="01")
    emu.h.call("write_mem", addr=settings + S_TIMING_METER, hex="01")
    emu.h.call("write_mem", addr=settings + S_HMN_INFO_SIZE, hex="01")
    emu.h.call("write_mem", addr=settings + S_HMN_INFO_ROWS, hex="0203040506070000")
    emu.h.call("write_mem", addr=settings + S_CPU_INFO_SIZE, hex="01")
    emu.h.call("write_mem", addr=settings + S_CPU_INFO_ROWS, hex="0203040500000000")
    emu.h.call("write_mem", addr=settings + S_INPUT_VIEWER, hex="03")
    emu.h.call("write_mem", addr=settings + S_DI_DISPLAY, hex="01")
    emu.h.call("write_mem", addr=settings + S_DI_VECTOR_MODE, hex="08")
    emu.h.call("write_mem", addr=settings + S_OSD_POSITION, hex="01")  # Sides
    emu.h.call("write_mem", addr=settings + S_OSD_WAVEDASH, hex="01")
    emu.h.call("write_mem", addr=settings + S_OSD_LEDGEDASH, hex="01")


def run_timing_meter_and_hud_16x9(backend, notes, shots_taken):
    fails = []
    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=False) as (_proc, emu):
        # 1. Wavedash + Act-Out on Fox (16:9).
        _lab, settings, p1, _cpu = start_match(emu, "fox", "mario", notes=notes)
        tmtm = find_tmtm(emu)
        blk0 = read_tmtm(emu, tmtm)
        if blk0["magic"] != TMTM_MAGIC or blk0["version"] != TMTM_VERSION:
            fails.append(f"TMTM header mismatch: magic={blk0['magic']:#x} ver={blk0['version']}")

        # Enable Overhead Timing Bar = All (1).
        emu.h.call("write_mem", addr=settings + S_TIMING_METER, hex="01")
        emu.h.call("pause")
        emu.h.call("frame_advance", n=2)

        do_fox_wavedash(emu)
        blk_wd = read_tmtm(emu, tmtm)
        s0 = blk_wd["slots"][0]
        notes.append(
            f"16:9 WD TMTM: mode={blk_wd['mode']} active={s0['active']} kind={s0['kind']} "
            f"out={s0['outcome']} delta={s0['lastDelta']} wdHits={s0['wavedashHits']} draws={s0['drawCount']}"
        )
        if s0["kind"] != TM_KIND_WAVEDASH or s0["outcome"] != TM_OUT_HIT or s0["wavedashHits"] < 1:
            fails.append(f"16:9 WD TMTM failed: {s0}")
        if s0["lastDelta"] not in (1, 2) or s0["drawCount"] < 1:
            fails.append(f"16:9 WD delta/drawCount failed: delta={s0['lastDelta']} draws={s0['drawCount']}")
        emu.h.call("frame_advance", n=1)
        shots_taken.append(shot_while_paused(emu, "timing-meter-wavedash-16x9"))

        # Wait for hold to finish, then test Act-Out mode (4) with a grounded spotdodge / roll.
        emu.h.call("frame_advance", n=35)
        emu.h.call("write_mem", addr=settings + S_TIMING_METER, hex="04")
        emu.hold(0, ["R"], stick=(128, 28))
        emu.h.call("frame_advance", n=4)
        emu.neutral(0)
        saw_countdown = False
        saw_flash = False
        for _ in range(45):
            emu.h.call("frame_advance", n=1)
            b = read_tmtm(emu, tmtm)["slots"][0]
            if b["active"] == 1 and b["kind"] == TM_KIND_ACTOUT and b["actInFrames"] > 0:
                saw_countdown = True
            if b["actOutFlashes"] >= 1 and b["kind"] == TM_KIND_ACTOUT:
                saw_flash = True
                shots_taken.append(shot_while_paused(emu, "timing-meter-actout-16x9"))
                break
        b_act = read_tmtm(emu, tmtm)["slots"][0]
        notes.append(f"16:9 Act-Out TMTM: countdown={saw_countdown} flash={saw_flash} flashes={b_act['actOutFlashes']}")
        if not saw_countdown or not saw_flash:
            fails.append(f"16:9 Act-Out TMTM failed: countdown={saw_countdown} flash={saw_flash} slot={b_act}")

        # 2. Multi-HUD stress layout (16:9): Ledgedash HUD + Info Display (HMN+CPU) + Input Viewer (Both) + DI Compass + OSD.
        enable_multi_hud(emu, settings)
        trigger_ledgedash_for_hud(emu, p1["fighter"])
        shots_taken.append(shot_while_paused(emu, "multi-hud-stress-16x9"))
        emu.h.call("resume")

        # 3. L-Cancel (hit + miss) on Mario vs Fox (16:9).
        _lab, settings, p1, _cpu = start_match(emu, "mario", "fox", notes=notes)
        tmtm = find_tmtm(emu)
        emu.h.call("write_mem", addr=settings + S_TIMING_METER, hex="02")  # L-Cancel mode
        emu.h.call("write_mem", addr=settings + S_OSD_LCANCEL, hex="01")
        emu.h.call("pause")
        emu.h.call("frame_advance", n=2)

        do_shorthop_nair(emu, p1["fighter"], press_lcancel=True)
        blk_lc = read_tmtm(emu, tmtm)
        s0 = blk_lc["slots"][0]
        notes.append(
            f"16:9 L-Cancel HIT TMTM: active={s0['active']} kind={s0['kind']} out={s0['outcome']} "
            f"delta={s0['lastDelta']} lcHits={s0['lCancelHits']} draws={s0['drawCount']}"
        )
        if s0["kind"] != TM_KIND_LCANCEL or s0["outcome"] != TM_OUT_HIT or s0["lCancelHits"] < 1:
            fails.append(f"16:9 L-Cancel hit TMTM failed: {s0}")
        shots_taken.append(shot_while_paused(emu, "timing-meter-lcancel-16x9"))

        emu.h.call("frame_advance", n=36)
        do_shorthop_nair(emu, p1["fighter"], press_lcancel=False)
        blk_lcm = read_tmtm(emu, tmtm)
        sm = blk_lcm["slots"][0]
        notes.append(f"16:9 L-Cancel MISS TMTM: active={sm['active']} kind={sm['kind']} out={sm['outcome']} delta={sm['lastDelta']}")
        if sm["kind"] != TM_KIND_LCANCEL or sm["outcome"] != TM_OUT_MISS:
            fails.append(f"16:9 L-Cancel miss TMTM failed: {sm}")

        emu.h.call("resume")
        bad = L.problems(E.read_log(emu.log_path))
        if bad:
            fails.append(f"16:9 problem lines: {bad[:3]}")
    return fails


def run_hud_and_meter_4x3(backend, notes, shots_taken):
    fails = []
    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=False) as (_proc, emu):
        _lab, settings, p1, _cpu = start_match(emu, "fox", "mario", aspect="4:3", notes=notes)
        log = emu.synced_log_text()
        if "lab: screen 4:3;" not in log:
            fails.append("4:3 screen mode did not engage (missing 'lab: screen 4:3;')")

        tmtm = find_tmtm(emu)
        emu.h.call("write_mem", addr=settings + S_TIMING_METER, hex="01")
        emu.h.call("pause")
        emu.h.call("frame_advance", n=2)

        do_fox_wavedash(emu)
        s0 = read_tmtm(emu, tmtm)["slots"][0]
        notes.append(f"4:3 WD TMTM: kind={s0['kind']} out={s0['outcome']} delta={s0['lastDelta']} wdHits={s0['wavedashHits']} draws={s0['drawCount']}")
        if s0["kind"] != TM_KIND_WAVEDASH or s0["outcome"] != TM_OUT_HIT or s0["wavedashHits"] < 1 or s0["drawCount"] < 1:
            fails.append(f"4:3 WD TMTM failed: {s0}")
        emu.h.call("frame_advance", n=1)
        shots_taken.append(shot_while_paused(emu, "timing-meter-wavedash-4x3"))

        # Multi-HUD stress layout on 4:3 (Ledgedash HUD + Info Display HMN+CPU + Input Viewer Both + DI Compass + OSD).
        enable_multi_hud(emu, settings)
        trigger_ledgedash_for_hud(emu, p1["fighter"])
        shots_taken.append(shot_while_paused(emu, "multi-hud-stress-4x3"))

        emu.h.call("resume")
        bad = L.problems(E.read_log(emu.log_path))
        if bad:
            fails.append(f"4:3 problem lines: {bad[:3]}")
    return fails


def cmd_test_hud_theme_meter(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-hud-theme-meter")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0

    wanted = opts.only if opts.only else list(SCENARIOS)
    for name in wanted:
        if name not in SCENARIOS:
            print(f"unknown scenario {name!r}; known: {', '.join(SCENARIOS)}", file=sys.stderr)
            return 2

    label = "test-hud-theme-meter"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    notes, shots_taken, all_fails = [], [], []

    runners = {
        "theme-v2-persist": lambda: run_theme_v2_persist(opts.rel, backend, notes, shots_taken),
        "timing-meter-and-hud-16x9": lambda: run_timing_meter_and_hud_16x9(backend, notes, shots_taken),
        "hud-and-meter-4x3": lambda: run_hud_and_meter_4x3(backend, notes, shots_taken),
    }

    for name in wanted:
        try:
            fails = runners[name]()
        except (Timeout, nav.NavError, HarnessError, LT.LabError, HudThemeMeterError, OSError, ValueError) as e:
            fails = [f"{name}: exception: {e}"]
        status = "PASS" if not fails else "FAIL"
        print(f"{name}: {status}")
        for f in fails:
            print(f"  FAIL: {f}")
            all_fails.append(f"{name}: {f}")

    print()
    for n in notes:
        print(f"  {n}")
    for p in shots_taken:
        print(f"screenshot: {p}")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if all_fails:
        print(f"{label}: FAIL ({len(all_fails)} failure(s))")
        return 1
    print(f"{label}: PASS")
    return 0
