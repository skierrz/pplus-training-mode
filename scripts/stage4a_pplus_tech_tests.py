"""Emulator test suite for P+-Exclusive Tech & Floorhug /
ASDI-Down pure detection backends (run through emu.py):

  test-stage4a-pplus-tech REL [--only NAME ...] [--list] [--keep-going]

Final Destination ONLY, headless Null backend by default. Verifies all five
engines through real GameCube controller input + live RAM "TM4T" debug block:

  rar                  Fox Run -> Turn_Run (0x08) -> Jump_Squat -> Jump -> Bair
                       (kRarSuccessBair, turnRunFrames, momentumRetained > 0.5).
  dacus-gatling        Snake whiff DACUS (0x26 -> 0x30, nonHitlagDashFrame in
                       {2, 3}, gatling = 0) and Sonic Gatling DACUS hitting CPU
                       through hitlag (gatling = 1, rawDashFrame > 3,
                       nonHitlagDashFrame in {2, 3}).
  breverse-wavebounce  Fox airborne Neutral-B Laser verifying exact Special
                       Frame 5 flip + Frame 6 -1.0x reversal for
                       kBRevTurnaroundB, kBRevBReverse, and kBRevWavebounce.
  footstool-agt        Mario footstool on Fox (Tread_Jump 0x6D + victim
                       0x6E/0x70) + Peach turnip AGT (Escape_Air 0x21 ->
                       Item_Throw 0x9B, LA-Basic[81] increment & landing reset).
  floorhug-cc          Mario defending Fox attacks with Standing Floorhug
                       (kFhStandingFloorhugCancel, C-stick ASDI-Down,
                       hitstunSaved > 0), Crouch-Cancel Floorhug
                       (kFhCcFloorhugCancel), and Broken Tumble
                       (kFhBrokenTumble).
  savestate-reset      Lab quick savestate save/load clears transient counters,
                       increments resetCount, and match exit unloads cleanly.
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
from harness import HarnessError, Timeout  # noqa: E402

CHARS = {
    "mario": (0x00, 0x00),
    "fox": (0x07, 0x07),
    "peach": (0x0D, 0x0D),
    "snake": (0x2A, 0x26),
    "sonic": (0x2B, 0x27),
}

RIGHT = (228, 128)
LEFT = (28, 128)
UP = (128, 228)
DOWN = (128, 28)
NEUTRAL = (128, 128)

TM4T_MAGIC = 0x544D3454  # "TM4T"
TM4T_LOG_RE = re.compile(r"pplus_tech: debug block at ([0-9a-f]{8})")
TM4T_HDR_FMT = ">4I"
TM4T_SLOT_FMT = ">IIBBBBffIIIBBBBffIIIIBBBBfffIIIIBBBBhhBBBBffIIIIBBBBhhhBBfff"
TM4T_SLOT_SIZE = struct.calcsize(TM4T_SLOT_FMT)
assert TM4T_SLOT_SIZE == 152, f"TM4T slot size mismatch: {TM4T_SLOT_SIZE}"

# Core outcome enums matching src/extra/drill_*_core.h.
RAR_SUCCESS_BAIR = 1
DACUS_SUCCESS = 1
DACUS_SUCCESS_GATLING = 2
BREV_TURNAROUND = 1
BREV_BREVERSE = 2
BREV_WAVEBOUNCE = 3
FS_GROUNDED = 1
FS_AIRBORNE = 2
FH_CC_FLOORHUG_CANCEL = 1
FH_STANDING_FLOORHUG_CANCEL = 2
FH_BROKEN_TUMBLE = 5

# main::Settings offsets (src/main/settings.h).
S_PLAYER_PERCENT = 8
S_PLAYER_LOCK = 10

SCENARIOS = [
    "rar",
    "dacus-gatling",
    "breverse-wavebounce",
    "footstool-agt",
    "floorhug-cc",
    "savestate-reset",
]


class TechTestError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def state_path(name):
    p = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states", f"stage4a-pt-{name}.sav")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def find_tm4t(emu):
    hits = TM4T_LOG_RE.findall(emu.synced_log_text())
    if not hits:
        raise TechTestError("pplus_tech debug block address not found in log")
    addr = int(hits[-1], 16)
    magic, version, frame, reset_cnt = struct.unpack(TM4T_HDR_FMT, emu.read(addr, 16))
    if magic != TM4T_MAGIC:
        raise TechTestError(f"bad TM4T magic at {addr:#x}: got {magic:#x}")
    return addr


def read_tm4t(emu, addr, slot=0):
    hdr = struct.unpack(TM4T_HDR_FMT, emu.read(addr, 16))
    raw = emu.read(addr + 16 + slot * TM4T_SLOT_SIZE, TM4T_SLOT_SIZE)
    v = struct.unpack(TM4T_SLOT_FMT, raw)
    return {
        "magic": hdr[0],
        "version": hdr[1],
        "frame": hdr[2],
        "resetCount": hdr[3],
        # RAR
        "rarAttempts": v[0],
        "rarBairCount": v[1],
        "rarOutcome": v[2],
        "rarTurnRunFrames": v[3],
        "rarBairDelay": v[4],
        "rarBackwardMomentum": v[5],
        "rarMomentumRetained": v[6],
        "rarTakeoffVx": v[7],
        # DACUS
        "dacusAttempts": v[8],
        "dacusSuccessCount": v[9],
        "dacusGatlingCount": v[10],
        "dacusOutcome": v[11],
        "dacusRawDashFrame": v[12],
        "dacusNonHitlagFrame": v[13],
        "dacusGatling": v[14],
        "dacusVxCarry": v[15],
        "dacusSlideDistance": v[16],
        # B-Reverse
        "brevAttempts": v[17],
        "brevBReverseCount": v[18],
        "brevWavebounceCount": v[19],
        "brevTurnaroundCount": v[20],
        "brevOutcome": v[21],
        "brevEarlyFlick": v[22],
        "brevLateFlick": v[23],
        "brevAirborne": v[24],
        "brevVxBefore": v[25],
        "brevVxAfter": v[26],
        "brevMomentumRatio": v[27],
        # Footstool & AGT
        "footstoolCount": v[28],
        "footstoolLockCount": v[29],
        "agtCount": v[30],
        "agtGroundedCount": v[31],
        "footstoolOutcome": v[32],
        "footstoolShortHop": v[33],
        "footstoolVictimTeched": v[34],
        "footstoolLockHit": v[35],
        "footstoolTauntLockout": v[36],
        "agtCurrentChain": v[37],
        "agtIsAerial": v[38],
        "agtThrowFrame": v[39],
        "agtLastChainCount": v[40],
        "agtChainResetCount": v[41],
        "agtVxCarry": v[42],
        "agtVyCarry": v[43],
        "footstoolFailLockoutCount": v[44],
        # Floorhug
        "fhHitCount": v[45],
        "fhCancelCount": v[46],
        "fhTruePunishCount": v[47],
        "fhOutcome": v[48],
        "fhWasCrouching": v[49],
        "fhAsdiDown": v[50],
        "fhAsdiFromCStick": v[51],
        "fhHitstunAtLanding": v[52],
        "fhHitstunSaved": v[53],
        "fhPunishActFrame": v[54],
        "fhTrueCounterPunish": v[55],
        "fhEffectiveAsdiX": v[57],
        "fhEffectiveAsdiY": v[58],
        "fhLandingFrame": v[59],
    }


def read_fighter(emu, fighter_ptr):
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0xD0)
    motion = u32(mods, 0x08)
    posture = u32(mods, 0x0C)
    sit = u32(mods, 0x14)
    stop = u32(mods, 0x44)
    work = u32(mods, 0x64)
    status_mod = u32(mods, 0x70)
    p = emu.read(posture, 0x44)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    px, py = f32(p, 0x18), f32(p, 0x1C)
    lr = 1.0 if f32(p, 0x40) >= 0.0 else -1.0
    st = emu.u32(status_mod + 0x34)
    mkind = s32(emu.read(motion + 0x58, 4), 0)
    air = emu.u32(sit + 0x2C) == 2
    hl = s32(emu.read(stop + 0x10, 4), 0)
    la = emu.ptr(work + 0x20)
    la_ints = emu.read(emu.u32(la + 0x0C), 84 * 4)
    return {
        "x": x, "y": y, "vx": x - px, "vy": y - py, "lr": lr,
        "status": st, "mkind": mkind, "air": air, "hitlag": hl,
        "la55": s32(la_ints, 55 * 4),
        "la81": s32(la_ints, 81 * 4),
        "la82": s32(la_ints, 82 * 4),
        "posture": posture,
    }


def start_match(emu, p1_name, cpu_name="fox"):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"stage4a-pplus-tech: {p1_name} vs {cpu_name}")
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    CT.wait_game_frames(emu, 28)
    f = LT.Fighters(emu)
    p1 = f.by_player(0)
    cpu = f.by_player(1)
    if p1 is None or cpu is None:
        raise TechTestError("fighters not found after match start")
    tm4t = find_tm4t(emu)
    return lab, settings, p1["fighter"], cpu["fighter"], tm4t


def walk_p1_to(emu, p1_ptr, target_x, face_dir=1.0, max_frames=360):
    for _ in range(max_frames // 2):
        s = read_fighter(emu, p1_ptr)
        dx = target_x - s["x"]
        if abs(dx) <= 18.0:
            break
        sx = (128 + 95) if dx > 0 else (128 - 95)
        emu.hold(0, stick=(sx, 128))
        emu.advance(1)
    emu.neutral(0)
    for _ in range(35):
        s = read_fighter(emu, p1_ptr)
        if s["status"] == 0x00 and abs(s["vx"]) < 0.05:
            break
        emu.advance(1)
    for _ in range(max_frames // 2):
        s = read_fighter(emu, p1_ptr)
        dx = target_x - s["x"]
        if abs(dx) <= 1.2:
            break
        sx = (128 + 42) if dx > 0 else (128 - 42)
        emu.hold(0, stick=(sx, 128))
        emu.advance(1)
    emu.neutral(0)
    for _ in range(25):
        s = read_fighter(emu, p1_ptr)
        if s["status"] == 0x00 and abs(s["vx"]) < 0.05:
            break
        emu.advance(1)
    s = read_fighter(emu, p1_ptr)
    if (face_dir > 0 and s["lr"] < 0) or (face_dir < 0 and s["lr"] > 0):
        emu.hold(0, stick=(176 if face_dir > 0 else 80, 128))
        emu.advance(2)
        emu.neutral(0)
        emu.advance(16)
    return read_fighter(emu, p1_ptr)


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


# ---- Scenario 1: RAR (Fox Run -> Turn_Run 0x08 -> Jump_Squat -> Jump -> Bair)

def sc_rar(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4t = start_match(emu, "fox", "fox")
    walk_p1_to(emu, p1_ptr, -55.0, face_dir=1.0)
    emu.h.call("pause")
    step(emu, 2)

    # Real controller input: dash right into full Run (0x04), flick left into
    # Turn_Run (0x08), press X + stick right next frame for RAR jump, then A +
    # stick right for Attack_Air_B (0x64).
    emu.hold(0, stick=RIGHT)
    entered_run = False
    for _ in range(24):
        step(emu, 1)
        if read_fighter(emu, p1_ptr)["status"] == 0x04:
            entered_run = True
            step(emu, 4)
            break

    # Flick left to trigger Turn_Run (0x08).
    emu.hold(0, stick=LEFT)
    entered_turn_run = False
    for _ in range(8):
        step(emu, 1)
        if read_fighter(emu, p1_ptr)["status"] == 0x08:
            entered_turn_run = True
            break

    # Immediately press X + hold RIGHT so Fox jumps facing left while retaining rightward speed!
    emu.hold(0, ["X"], stick=RIGHT)
    entered_jump = False
    for _ in range(10):
        step(emu, 1)
        st = read_fighter(emu, p1_ptr)["status"]
        if st == 0x0B:
            entered_jump = True
            break

    # Press A + RIGHT (behind Fox since lr = -1) for Bair (0x64).
    emu.hold(0, ["A"], stick=RIGHT)
    step(emu, 3)
    emu.neutral(0)
    step(emu, 10)

    d = read_tm4t(emu, tm4t, 0)
    notes.append(
        f"rar: run={entered_run} turnRun={entered_turn_run} jump={entered_jump} "
        f"outcome={d['rarOutcome']} turnF={d['rarTurnRunFrames']} bairDelay={d['rarBairDelay']} "
        f"backMom={d['rarBackwardMomentum']} momRet={d['rarMomentumRetained']:.3f} "
        f"takeoffVx={d['rarTakeoffVx']:.3f} attempts={d['rarAttempts']} bairs={d['rarBairCount']}"
    )
    if d["rarOutcome"] != RAR_SUCCESS_BAIR:
        fails.append(f"expected rarOutcome={RAR_SUCCESS_BAIR} (kRarSuccessBair), got {d['rarOutcome']}")
    if d["rarTurnRunFrames"] < 1:
        fails.append(f"expected rarTurnRunFrames >= 1, got {d['rarTurnRunFrames']}")
    if d["rarBackwardMomentum"] != 1 or d["rarMomentumRetained"] <= 0.5:
        fails.append(f"expected backwardMomentum==1 & momRet>0.5, got {d['rarBackwardMomentum']}/{d['rarMomentumRetained']:.3f}")
    if d["rarBairDelay"] < 1:
        fails.append(f"expected rarBairDelay >= 1, got {d['rarBairDelay']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 2: DACUS (Snake Whiff) & Gatling DACUS (Sonic Hitlag Freeze) --

def sc_dacus_gatling(emu, notes):
    fails = []

    # Part A: Snake Whiff DACUS via real GameCube pad (C-stick down -> Z + Up).
    _, _, p1_ptr, _, tm4t = start_match(emu, "snake", "fox")
    walk_p1_to(emu, p1_ptr, -45.0, face_dir=1.0)
    emu.h.call("pause")
    step(emu, 2)

    emu.hold(0, stick=RIGHT)
    step(emu, 5)
    # C-stick down triggers Attack_Dash (0x26); next frame Z + Up cancels into 0x30.
    emu.hold(0, stick=RIGHT, cstick=DOWN)
    entered_da = False
    for _ in range(6):
        step(emu, 1)
        if read_fighter(emu, p1_ptr)["status"] == 0x26:
            entered_da = True
            break
    emu.hold(0, ["Z"], stick=UP, cstick=NEUTRAL)
    step(emu, 2)
    emu.neutral(0)
    step(emu, 18)

    ds = read_tm4t(emu, tm4t, 0)
    notes.append(
        f"dacus snake whiff: enteredDA={entered_da} outcome={ds['dacusOutcome']} "
        f"rawF={ds['dacusRawDashFrame']} nhF={ds['dacusNonHitlagFrame']} gatling={ds['dacusGatling']} "
        f"vxCarry={ds['dacusVxCarry']:.3f} slide={ds['dacusSlideDistance']:.2f}"
    )
    if ds["dacusOutcome"] != DACUS_SUCCESS:
        fails.append(f"snake whiff expected outcome={DACUS_SUCCESS}, got {ds['dacusOutcome']}")
    if ds["dacusGatling"] != 0:
        fails.append(f"snake whiff expected gatling=0, got {ds['dacusGatling']}")
    if ds["dacusNonHitlagFrame"] not in (2, 3):
        fails.append(f"snake whiff expected nonHitlagFrame in {{2,3}}, got {ds['dacusNonHitlagFrame']}")
    if ds["dacusSlideDistance"] <= 0.5:
        fails.append(f"snake whiff expected slideDistance > 0.5, got {ds['dacusSlideDistance']:.3f}")
    emu.h.call("resume")

    # Part B: Sonic Gatling DACUS hitting Fox CPU on Frame 2, cancelling through hitlag!
    _, _, p1_ptr, cpu_ptr, tm4t = start_match(emu, "sonic", "fox")
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 10.0, face_dir=1.0)
    sonic_sav = state_path("sonic-gatling")
    emu.h.call("pause")
    emu.h.call("save_state", path=sonic_sav)

    got_gatling = None
    for dash_f in (2, 3, 4, 1):
        emu.h.call("load_state", path=sonic_sav)
        emu.h.call("pause")
        step(emu, 1)
        emu.hold(0, stick=RIGHT)
        step(emu, dash_f)
        emu.hold(0, stick=RIGHT, cstick=DOWN)
        saw_hl = False
        da_hit_steps = 0
        for _ in range(12):
            step(emu, 1)
            sf = read_fighter(emu, p1_ptr)
            if sf["status"] == 0x26 and sf["hitlag"] > 0:
                saw_hl = True
                da_hit_steps += 1
                # Real GC pad has 1f latency: sending Z+Up at hitlag == 2 arrives on
                # last hitlag / post-hitlag frame 1 (rawDashFrame 6..7, RA-Basic[1] <= 3).
                if sf["hitlag"] <= 2:
                    emu.hold(0, ["Z"], stick=UP, cstick=NEUTRAL)
                    step(emu, 3)
                    emu.neutral(0)
                    step(emu, 12)
                    break
            elif saw_hl:
                break
        dg = read_tm4t(emu, tm4t, 0)
        if dg["dacusOutcome"] == DACUS_SUCCESS_GATLING and dg["dacusGatling"] == 1 and dg["dacusRawDashFrame"] > 3:
            got_gatling = (dash_f, dg)
            break

    if got_gatling is None:
        dg_last = read_tm4t(emu, tm4t, 0)
        notes.append(f"dacus sonic gatling FAILED: last={dg_last}")
        fails.append(f"sonic gatling did not trigger (last outcome={dg_last['dacusOutcome']}, rawF={dg_last['dacusRawDashFrame']})")
    else:
        dash_f, dg = got_gatling
        notes.append(
            f"dacus sonic gatling (dash_f={dash_f}): outcome={dg['dacusOutcome']} gatling={dg['dacusGatling']} "
            f"rawF={dg['dacusRawDashFrame']} nhF={dg['dacusNonHitlagFrame']} "
            f"vxCarry={dg['dacusVxCarry']:.3f} slide={dg['dacusSlideDistance']:.2f}"
        )
        if dg["dacusNonHitlagFrame"] not in (2, 3):
            fails.append(f"sonic gatling expected nonHitlagFrame in {{2,3}}, got {dg['dacusNonHitlagFrame']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 3: B-Reverse, Wavebounce, Turnaround-B (Fox Laser) ------------

def sc_breverse_wavebounce(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4t = start_match(emu, "fox", "fox")
    walk_p1_to(emu, p1_ptr, -45.0, face_dir=1.0)
    emu.h.call("pause")
    step(emu, 2)

    # Dash right -> full-hop right -> drift right so Fox is airborne with solid +vx.
    emu.hold(0, stick=RIGHT)
    step(emu, 4)
    emu.hold(0, ["X"], stick=RIGHT)
    step(emu, 7)
    emu.hold(0, stick=RIGHT)
    step(emu, 5)
    emu.neutral(0)
    air_sav = state_path("fox-brev-air")
    emu.h.call("save_state", path=air_sav)

    def run_variant(mode):
        emu.h.call("load_state", path=air_sav)
        emu.h.call("pause")
        step(emu, 1)
        # Step 1: Neutral-B or Turnaround-B entry.
        if mode == "breverse":
            emu.hold(0, ["B"], stick=NEUTRAL)
        else:
            # Slight opposite X on B press flips lr on Frame 1 without triggering Side-B!
            emu.hold(0, ["B"], stick=(84, 128))
        # Wait for Special Frame 1 (status == 0x112, la55 == 4).
        entered = False
        for _ in range(6):
            step(emu, 1)
            sf = read_fighter(emu, p1_ptr)
            if sf["status"] == 0x112 and sf["la55"] == 4:
                entered = True
                break
        if not entered:
            return None, "never entered 0x112 with LA-Basic[55]==4"
        # Special Frames 2 - 3: neutral stick (la55: 4 -> 3 -> 2).
        emu.neutral(0)
        for _ in range(2):
            step(emu, 1)
        # Accounting for 1f real GC pad latency: drive opposite/forward stick across
        # la55: 2 -> 1 -> 0 so it is active on exact Special Frame 5 (la55: 1 -> 0)!
        if mode == "breverse":
            emu.hold(0, stick=LEFT)
        elif mode == "wavebounce":
            emu.hold(0, stick=RIGHT)
        else:
            emu.neutral(0)
        step(emu, 2)  # Special Frames 4 and 5 (flip + reflectSpeed on Frame 5)
        emu.neutral(0)
        step(emu, 3)  # Special Frame 6 (reversed vx appears!) + settle
        return read_tm4t(emu, tm4t, 0), None

    for mode, want_out, want_neg_vx in (
        ("turnaround", BREV_TURNAROUND, False),
        ("breverse", BREV_BREVERSE, True),
        ("wavebounce", BREV_WAVEBOUNCE, True),
    ):
        d, err = run_variant(mode)
        if err is not None:
            fails.append(f"{mode}: {err}")
            continue
        notes.append(
            f"brev {mode}: outcome={d['brevOutcome']} vxBefore={d['brevVxBefore']:+.3f} "
            f"vxAfter={d['brevVxAfter']:+.3f} ratio={d['brevMomentumRatio']:.4f} air={d['brevAirborne']}"
        )
        if d["brevOutcome"] != want_out:
            fails.append(f"{mode}: expected outcome={want_out}, got {d['brevOutcome']}")
        if want_neg_vx:
            if not (d["brevVxBefore"] > 0.3 and d["brevVxAfter"] < -0.3):
                fails.append(f"{mode}: expected +vx -> -vx reversal, got {d['brevVxBefore']:+.3f} -> {d['brevVxAfter']:+.3f}")
            if not (0.85 <= d["brevMomentumRatio"] <= 1.05):
                fails.append(f"{mode}: expected -1.0x ratio (~0.85..1.05, no 0.8x decay), got {d['brevMomentumRatio']:.4f}")
        else:
            if not (d["brevVxBefore"] > 0.3 and d["brevVxAfter"] > 0.3):
                fails.append(f"{mode}: expected preserved +vx, got {d['brevVxBefore']:+.3f} -> {d['brevVxAfter']:+.3f}")

    emu.h.call("resume")
    return fails


# ---- Scenario 4: Footstool (Mario on Fox) + AGT (Peach Turnip + LA-Basic[81])

def sc_footstool_agt(emu, notes):
    fails = []

    # Part A: Live Mario Footstool on grounded Fox (DUP taunt at exact head height).
    _, _, p1_ptr, cpu_ptr, tm4t = start_match(emu, "mario", "fox")
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 8.0, face_dir=1.0)
    emu.h.call("pause")
    step(emu, 2)

    # Full-hop right over Fox's head and save at jump apex right above Fox.
    emu.hold(0, ["X"], stick=(196, 128))
    step(emu, 8)
    for _ in range(18):
        p = read_fighter(emu, p1_ptr)
        c = read_fighter(emu, cpu_ptr)
        dx = c["x"] - p["x"]
        sx = 128 + max(-80, min(80, int(dx * 16)))
        emu.hold(0, stick=(sx, 128))
        step(emu, 1)
        if p["vy"] <= 0.0:
            break
    emu.neutral(0)
    fs_sav = state_path("mario-footstool-apex")
    emu.h.call("save_state", path=fs_sav)

    triggered = False
    fs_trace = []
    for y_thresh in (12.0, 10.0, 14.0, 8.0, 16.0):
        emu.h.call("load_state", path=fs_sav)
        emu.h.call("pause")
        step(emu, 1)
        pressed = False
        fs_trace = []
        for _ in range(28):
            p = read_fighter(emu, p1_ptr)
            c = read_fighter(emu, cpu_ptr)
            dx = c["x"] - p["x"]
            dy = p["y"] - c["y"]
            sx = 128 + max(-80, min(80, int(dx * 16)))
            if not pressed and p["air"] and dy <= y_thresh and abs(dx) < 8.0:
                emu.hold(0, ["DUP"], stick=(sx, 128))
                pressed = True
            else:
                emu.hold(0, stick=(sx, 128))
            step(emu, 1)
            fs_trace.append((round(dx, 1), round(dy, 1), hex(p["status"]), hex(c["status"]), p["la82"]))
            d_now = read_tm4t(emu, tm4t, 0)
            if d_now["footstoolCount"] >= 1:
                triggered = True
                emu.neutral(0)
                step(emu, 4)
                break
        if triggered:
            break
    emu.neutral(0)

    df = read_tm4t(emu, tm4t, 0)
    notes.append(
        f"footstool mario->fox: triggered={triggered} count={df['footstoolCount']} "
        f"outcome={df['footstoolOutcome']} shortHop={df['footstoolShortHop']} "
        f"tauntLockout={df['footstoolTauntLockout']} trace={fs_trace[:8]}"
    )
    if df["footstoolCount"] < 1 or df["footstoolOutcome"] not in (FS_GROUNDED, FS_AIRBORNE):
        fails.append(f"footstool failed: count={df['footstoolCount']} outcome={df['footstoolOutcome']}")
    if df["footstoolTauntLockout"] < 20:
        fails.append(f"footstool expected LA-Basic[82] >= 20, got {df['footstoolTauntLockout']}")
    emu.h.call("resume")

    # Part B: Live Peach Turnip AGT (Escape_Air 0x21 -> Item_Throw 0x9B) + LA-Basic[81] landing reset.
    _, _, p1_ptr, _, tm4t = start_match(emu, "peach", "fox")
    walk_p1_to(emu, p1_ptr, -20.0, face_dir=1.0)
    emu.h.call("pause")
    step(emu, 2)

    # Pull Turnip with Down-B.
    emu.hold(0, ["B"], stick=DOWN)
    step(emu, 3)
    emu.neutral(0)
    for _ in range(70):
        step(emu, 1)
        if read_fighter(emu, p1_ptr)["status"] == 0x00:
            break

    # Full-hop -> Escape_Air (up-right) -> Item_Throw (A + Up) -> land on FD!
    emu.hold(0, ["X"], stick=NEUTRAL)
    step(emu, 11)
    emu.hold(0, ["R"], stick=(210, 210))
    entered_esc = False
    for _ in range(6):
        step(emu, 1)
        if read_fighter(emu, p1_ptr)["status"] == 0x21:
            entered_esc = True
            break
    emu.hold(0, ["A"], stick=UP)
    step(emu, 2)
    emu.neutral(0)
    step(emu, 2)
    d_air = read_tm4t(emu, tm4t, 0)

    # Let Peach land on FD so LA-Basic[81] auto-resets to 0!
    landed = False
    for _ in range(95):
        step(emu, 1)
        sf = read_fighter(emu, p1_ptr)
        if not sf["air"] and sf["status"] in (0x16, 0x00):
            landed = True
            step(emu, 3)
            break
    d_land = read_tm4t(emu, tm4t, 0)
    notes.append(
        f"agt peach: esc={entered_esc} air(cnt={d_air['agtCount']}, isAir={d_air['agtIsAerial']}, "
        f"throwF={d_air['agtThrowFrame']}, chain={d_air['agtLastChainCount']}, curChain={d_air['agtCurrentChain']}, "
        f"v=({d_air['agtVxCarry']:+.2f},{d_air['agtVyCarry']:+.2f})) "
        f"land(landed={landed}, curChain={d_land['agtCurrentChain']}, resets={d_land['agtChainResetCount']})"
    )
    if d_air["agtCount"] < 1 or d_air["agtIsAerial"] != 1:
        fails.append(f"peach AGT not detected: cnt={d_air['agtCount']} isAir={d_air['agtIsAerial']}")
    if d_air["agtLastChainCount"] != 1 or d_air["agtCurrentChain"] != 1:
        fails.append(f"peach AGT expected LA-Basic[81]==1 mid-air, got last={d_air['agtLastChainCount']} cur={d_air['agtCurrentChain']}")
    if not landed or d_land["agtCurrentChain"] != 0 or d_land["agtChainResetCount"] < 1:
        fails.append(f"peach AGT landing reset failed: landed={landed} cur={d_land['agtCurrentChain']} resets={d_land['agtChainResetCount']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 5: Floorhug / ASDI-Down & Crouch-Cancel (Mario vs Fox) --------

def sc_floorhug_cc(emu, notes):
    fails = []
    _, settings, p1_ptr, cpu_ptr, tm4t = start_match(emu, "mario", "fox")
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 10.5, face_dir=1.0)
    fh_sav = state_path("mario-floorhug")
    emu.h.call("pause")
    emu.h.call("save_state", path=fh_sav)
    inp = PI.PluginInput(emu)

    def run_fox_hit(mario_pct, mario_crouch, mario_cstick_down, fox_move):
        emu.h.call("load_state", path=fh_sav)
        emu.h.call("pause")
        emu.h.call("write_mem", addr=settings + S_PLAYER_PERCENT, hex=f"{int(mario_pct):04x}")
        emu.h.call("write_mem", addr=settings + S_PLAYER_LOCK, hex="01")
        step(emu, 2)
        emu.h.call("write_mem", addr=settings + S_PLAYER_LOCK, hex="00")
        if mario_crouch:
            # Control stick DOWN enters Squat_Wait (0x12) without triggering Down-Smash.
            emu.hold(0, stick=DOWN, cstick=NEUTRAL)
            step(emu, 10)
        else:
            emu.neutral(0)
            step(emu, 4)

        if fox_move == "utilt":
            # Fox Up-Tilt (Attack_Hi3): pops defender into Damage_Air (0x44) cleanly at 0% & 20%.
            fox_steps = PI.frames_to_steps({2: (PI.B_ATTACK, 0.0, 0.55)}, 3)
        elif fox_move == "dtilt":
            fox_steps = PI.frames_to_steps({2: (PI.B_ATTACK, 0.0, -0.55)}, 3)
        else:
            # Fox Up-Smash at 120% -> Damage_Fly (0x45) tumble.
            fox_steps = PI.frames_to_steps({2: (PI.B_ATTACK | PI.B_SMASH, 0.0, 1.0), 3: (PI.B_ATTACK | PI.B_SMASH, 0.0, 1.0)}, 4)

        seen_statuses = []

        def track(_i=None):
            sf = read_fighter(emu, p1_ptr)
            st = sf["status"]
            if not seen_statuses or seen_statuses[-1] != st:
                seen_statuses.append(st)
            # Push C-Stick DOWN during hitlag (never before the hit, which would Down-Smash!).
            if mario_cstick_down and sf["hitlag"] > 0:
                emu.hold(0, stick=(DOWN if mario_crouch else NEUTRAL), cstick=DOWN)

        try:
            inp.run({1: fox_steps}, read=track, extra=28)
        finally:
            inp.stop()
            emu.neutral(0)
        return read_tm4t(emu, tm4t, 0), seen_statuses

    # 1. Standing Floorhug Cancel (0%, Wait 0x00, C-Stick Down).
    d_st, sts_st = run_fox_hit(0, False, True, "utilt")
    notes.append(
        f"floorhug standing: outcome={d_st['fhOutcome']} crouch={d_st['fhWasCrouching']} "
        f"asdiDown={d_st['fhAsdiDown']} fromC={d_st['fhAsdiFromCStick']} "
        f"hsLand={d_st['fhHitstunAtLanding']} saved={d_st['fhHitstunSaved']} "
        f"lf={d_st['fhLandingFrame']:.1f} sts={[hex(s) for s in sts_st[:8]]}"
    )
    if d_st["fhOutcome"] != FH_STANDING_FLOORHUG_CANCEL:
        fails.append(f"standing floorhug expected outcome={FH_STANDING_FLOORHUG_CANCEL}, got {d_st['fhOutcome']} (sts={[hex(s) for s in sts_st]})")
    if d_st["fhWasCrouching"] != 0 or d_st["fhAsdiDown"] != 1 or d_st["fhAsdiFromCStick"] != 1:
        fails.append(f"standing floorhug flags mismatch: crouch={d_st['fhWasCrouching']} asdi={d_st['fhAsdiDown']} fromC={d_st['fhAsdiFromCStick']}")
    if d_st["fhHitstunSaved"] <= 0:
        fails.append(f"standing floorhug expected hitstunSaved > 0, got {d_st['fhHitstunSaved']}")

    # 2. Crouch-Cancel Floorhug Cancel (20%, Squat_Wait 0x12, Fox Down-Tilt hits crouching Mario).
    d_cc, sts_cc = run_fox_hit(20, True, True, "dtilt")
    notes.append(
        f"floorhug cc: outcome={d_cc['fhOutcome']} crouch={d_cc['fhWasCrouching']} "
        f"asdiDown={d_cc['fhAsdiDown']} fromC={d_cc['fhAsdiFromCStick']} "
        f"hsLand={d_cc['fhHitstunAtLanding']} saved={d_cc['fhHitstunSaved']} "
        f"sts={[hex(s) for s in sts_cc[:8]]}"
    )
    if d_cc["fhOutcome"] != FH_CC_FLOORHUG_CANCEL:
        fails.append(f"cc floorhug expected outcome={FH_CC_FLOORHUG_CANCEL}, got {d_cc['fhOutcome']} (sts={[hex(s) for s in sts_cc]})")
    if d_cc["fhWasCrouching"] != 1 or d_cc["fhAsdiDown"] != 1 or d_cc["fhHitstunSaved"] <= 0:
        fails.append(f"cc floorhug metrics mismatch: crouch={d_cc['fhWasCrouching']} asdi={d_cc['fhAsdiDown']} saved={d_cc['fhHitstunSaved']}")

    # 3. Broken Tumble (120%, Fox Up-Smash -> Damage_Fly 0x45).
    d_tb, sts_tb = run_fox_hit(120, True, False, "usmash")
    notes.append(
        f"floorhug broken tumble: outcome={d_tb['fhOutcome']} crouch={d_tb['fhWasCrouching']} "
        f"sts={[hex(s) for s in sts_tb[:8]]}"
    )
    if d_tb["fhOutcome"] != FH_BROKEN_TUMBLE:
        fails.append(f"broken tumble expected outcome={FH_BROKEN_TUMBLE}, got {d_tb['fhOutcome']} (sts={[hex(s) for s in sts_tb]})")

    emu.h.call("resume")
    return fails


# ---- Scenario 6: Savestate Load Reset + Clean Unload ------------------------

def sc_savestate_reset(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4t = start_match(emu, "fox", "fox")
    walk_p1_to(emu, p1_ptr, -40.0, face_dir=1.0)

    # Save a lab quick savestate via real controller (hold DRIGHT >= 12 frames).
    emu.press(0, ["DRIGHT"], frames=25, after=15)
    d_saved = read_tm4t(emu, tm4t, 0)

    # Jump and do a quick Neutral-B through Special Frame 6 so brevAttempts > 0.
    emu.h.call("pause")
    emu.hold(0, ["X"], stick=RIGHT)
    step(emu, 6)
    emu.hold(0, ["B"], stick=NEUTRAL)
    step(emu, 3)
    emu.neutral(0)
    step(emu, 14)
    d_dirty = read_tm4t(emu, tm4t, 0)
    emu.h.call("resume")

    # Load lab quick savestate via real controller (DLEFT)!
    emu.press(0, ["DLEFT"], frames=15, after=20)
    d_after = read_tm4t(emu, tm4t, 0)
    notes.append(
        f"savestate-reset: saved(rst={d_saved['resetCount']}, brev={d_saved['brevAttempts']}) "
        f"dirty(rst={d_dirty['resetCount']}, brev={d_dirty['brevAttempts']}) "
        f"afterLoad(rst={d_after['resetCount']}, brev={d_after['brevAttempts']}, out={d_after['brevOutcome']})"
    )
    if d_dirty["brevAttempts"] < 1:
        fails.append(f"expected brevAttempts >= 1 before load, got {d_dirty['brevAttempts']}")
    if d_after["resetCount"] <= d_dirty["resetCount"]:
        fails.append(f"expected resetCount to increment on savestate load: {d_dirty['resetCount']} -> {d_after['resetCount']}")
    if d_after["brevAttempts"] != 0 or d_after["brevOutcome"] != 0:
        fails.append(f"expected transient counters reset to 0 after savestate load: attempts={d_after['brevAttempts']} out={d_after['brevOutcome']}")

    return fails


SCENARIO_FNS = {
    "rar": sc_rar,
    "dacus-gatling": sc_dacus_gatling,
    "breverse-wavebounce": sc_breverse_wavebounce,
    "footstool-agt": sc_footstool_agt,
    "floorhug-cc": sc_floorhug_cc,
    "savestate-reset": sc_savestate_reset,
}


def cmd_test_stage4a_pplus_tech(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage4a-pplus-tech")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    label = "test-stage4a-pplus-tech"
    if opts.only:
        unknown = [n for n in opts.only if n not in SCENARIO_FNS]
        if unknown:
            E.die(f"no scenario named {unknown}; choose from {', '.join(SCENARIOS)}")
    selected = [n for n in SCENARIOS if not opts.only or n in opts.only]

    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    notes = []
    failed = []

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            for name in selected:
                s0 = time.monotonic()
                try:
                    fails = SCENARIO_FNS[name](emu, notes)
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError, PI.InputError, TechTestError, ValueError) as exc:
                    fails = [f"{type(exc).__name__}: {exc}"]
                finally:
                    try:
                        emu.neutral(0)
                        emu.h.call("resume")
                    except (OSError, HarnessError, Timeout):
                        pass
                dt = time.monotonic() - s0
                if fails:
                    failed.append((name, fails))
                    print(f"FAIL {name} ({dt:.1f}s): {'; '.join(fails)}")
                    if not opts.keep_going:
                        why = f"{name}: {fails[0]}"
                        break
                else:
                    print(f"ok   {name} ({dt:.1f}s)")
            if nav.scene(emu)[0] == "scMelee":
                try:
                    nav.finish_training(emu)
                    emu.advance(30)
                except (Timeout, nav.NavError, HarnessError):
                    pass
    except (OSError, HarnessError, Timeout, nav.NavError) as exc:
        why = f"boot: {type(exc).__name__}: {exc}"

    print("\n--- stage4a-pplus-tech measurements ---")
    for line in notes:
        print(" ", line)
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and failed:
        print("failed --only: " + " ".join(n for n, _ in failed))
        why = f"{len(failed)} scenario(s) failed: {', '.join(n for n, _ in failed)}"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
