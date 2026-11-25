"""Emulator tests for Stage 4A Stream 1 core drill backends (run through emu.py):
  - Ledgedash engine: intentional ledgedash (GALINT, dropFrame, cells), Tourney
    Winner (0x79/0x7A), 5-ledge-grab limit (LA-Basic[79] == 6 -> NoInvulnLimit),
    and multi-jumper Fly (0x0D) on Kirby.
  - Wavedash & Platform Waveland engine: frame-1 grounded wavedash on Final
    Destination + platform waveland on Battlefield (onPlatform = 1, distinct
    from grounded wavedash) + horizontal slide distance.
  - L-Cancel engine: manual L-cancel on whiff, manual L-cancel on hit through
    hitlag, real late miss (+2..+4f press after entering 0x18), and live ALC
    rule flag detection.
  - Savestate transient reset + clean extra module detach (zero heap leak).

  test-stage4a-core REL [--only NAME ...] [--list] [--keep-going]
"""
import argparse
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
    "kirby": (0x06, 0x06),
    "fox": (0x07, 0x07),
}

RIGHT, LEFT, UP, DOWN, NEUTRAL = (228, 128), (28, 128), (128, 228), (128, 28), (128, 128)

# Statuses (BrawlHeaders ft/fighter.h).
ST_WAIT = 0x00
ST_JUMP = 0x0B
ST_FLY = 0x0D
ST_FALL = 0x0E
ST_LANDING_ATTACK_AIR = 0x18
ST_LANDING_FALL_SPECIAL = 0x19
ST_CLIFF_CATCH = 0x74
ST_CLIFF_WAIT = 0x75
ST_CLIFF_JUMP1 = 0x79
ST_CLIFF_JUMP2 = 0x7A

# CoreDrillDebugBlock constants (src/extra/drill_core_engines.h).
TM4C_MAGIC = 0x544D3443
TM4C_VERSION = 1
TM4C_HEADER_SIZE = 24
TM4C_SLOT_SIZE = 284
TM4C_MAX_SLOTS = 8
TM4C_RE = re.compile(r"drill4c: debug block at ([0-9a-f]{8})")

# Enums from drill_ledge_core.h, drill_wavedash_core.h, drill_lcancel_core.h.
LEDGE_OUT_LEDGEDASH = 1
LEDGE_OUT_NO_INVULN = 2
LEDGE_OUT_TOURNEY_WINNER = 3
LEDGE_FAIL_NO_INVULN = 1 << 0
LEDGE_CELL_JUMP = 2

WD_KIND_WAVEDASH = 1
WD_KIND_WAVELAND = 2
WD_OUT_SUCCESS = 1

LC_OUT_SUCCESS_MANUAL = 1
LC_OUT_MISS_LATE = 5
LC_CTX_WHIFF = 0
LC_CTX_HIT = 1

SCENARIOS = [
    "ledge-core",
    "ledge-fly",
    "wavedash-waveland",
    "lcancel-core",
    "savestate-detach",
]


class Stage4aCoreError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def read_fighter_full(emu, fighter_ptr):
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0xD0)
    posture = u32(mods, 0x0C)
    situation = u32(mods, 0x14)
    hit = u32(mods, 0x20)
    work = u32(mods, 0x64)
    status_mod = u32(mods, 0x70)

    p = emu.read(posture, 0x44)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    lr = f32(p, 0x40)
    st = emu.u32(status_mod + 0x34)
    air = emu.u32(situation + 0x2C) == 2

    arr = emu.u32(hit + 0x58)
    xlu = s32(emu.read(arr + 0x0C + 0x24, 4), 0) if emu.is_ram(arr) else 0

    la = emu.ptr(work + 0x20)
    la_ints_ptr = emu.u32(la + 0x0C)
    la_raw = emu.read(la_ints_ptr, 92 * 4)
    return {
        "x": x, "y": y, "lr": lr, "status": st, "air": air, "xlu": xlu,
        "la2": s32(la_raw, 2 * 4),
        "la79": s32(la_raw, 79 * 4),
        "la90": s32(la_raw, 90 * 4),
        "posture": posture,
    }


def put_fighter(emu, posture, x, y):
    raw = struct.pack(">ff", float(x), float(y)).hex()
    emu.h.call("write_mem", addr=posture + 0x0C, hex=raw)
    emu.h.call("write_mem", addr=posture + 0x18, hex=raw)


def find_debug_block(emu):
    m = list(TM4C_RE.finditer(emu.synced_log_text()))
    if not m:
        raise Stage4aCoreError("drill4c TM4C debug block address not found in log")
    return int(m[-1].group(1), 16)


def read_debug_block(emu, addr):
    raw = emu.read(addr, TM4C_HEADER_SIZE + TM4C_MAX_SLOTS * TM4C_SLOT_SIZE)
    magic, version, frame, slot_size, ss_resets, alc, buf3f, max_slots, _ = struct.unpack_from(
        ">IIIIIBBBB", raw, 0
    )
    slots = []
    for i in range(TM4C_MAX_SLOTS):
        base = TM4C_HEADER_SIZE + i * TM4C_SLOT_SIZE
        s = {}
        s["used"], s["playerNo"], s["partner"], s["cpu"], s["frame"] = struct.unpack_from(">BbBBI", raw, base)
        (
            s["ledgeSeq"],
            s["ledgeOutcome"], s["ledgeFailFlags"], s["ledgeGrabCount"], s["ledgeFromTether"],
            s["ledgeUsedFly"], s["ledgeTourneySlow"], s["ledgeJumpXluStripped"], s["ledgeCellCount"],
            s["ledgeGalint"], s["ledgeDropFrame"], s["ledgeCliffWaitFrames"], s["ledgeNeutralLatchFrames"],
            s["ledgeFallFrames"], s["ledgeJumpFrames"], s["ledgeAirdodgeAirFrames"], s["ledgeLandingLagFrames"],
            s["ledgeAirdodgeAngle"],
            s["ledgeActivePhase"], s["ledgeActiveGrabs"], s["ledgeActiveNoInvuln"], s["ledgeActiveFromTether"],
            s["ledgeAttempts"], s["ledgeSuccesses"], s["ledgeTourneyWinners"], s["ledgeSds"],
            s["ledgeNoInvulnGrabs"], s["ledgeBestGalint"], s["ledgeStreak"], s["ledgeBestStreak"],
            s["ledgeHasBestGalint"],
        ) = struct.unpack_from(">IBBBBBBBBhhhhhhhhfBBBBHHHHHhHHB", raw, base + 8)
        s["ledgeCells"] = list(raw[base + 64: base + 64 + min(s["ledgeCellCount"], 128)])
        (
            s["wdSeq"],
            s["wdKind"], s["wdOutcome"], s["wdShortHop"], s["wdOnPlatform"],
            s["wdJumpsquatToDodge"], s["wdJumpHoldFrames"], s["wdAirdodgeAirFrames"], _,
            s["wdAngleDeg"], s["wdSlideDistance"], s["wdExpectedSpeed"],
            s["wdAttempts"], s["wdSuccesses"], s["wdPerfect"], s["wdStreak"], s["wdBestStreak"],
            s["wlAttempts"], s["wlSuccesses"], s["wlPerfect"], s["wlStreak"], s["wlBestStreak"],
        ) = struct.unpack_from(">IBBBBhhhhfffHHHHHHHHHH", raw, base + 192)
        (
            s["lcSeq"],
            s["lcOutcome"], s["lcContext"], s["lcInputKind"], s["lcAutoLCancelActive"], s["lcBuffer3fActive"], _,
            s["lcPressFrame"], s["lcEarlyFrames"], s["lcLateFrames"],
            s["lcTotal"], s["lcSuccesses"], s["lcAutoSuccesses"], s["lcAutoCanceled"],
            s["lcWhiffHits"], s["lcWhiffTotal"], s["lcHitHits"], s["lcHitTotal"],
            s["lcShieldHits"], s["lcShieldTotal"], s["lcLateMisses"], s["lcEarlyMisses"],
            s["lcStreak"], s["lcBestStreak"],
        ) = struct.unpack_from(">IBBBBBBhhhHHHHHHHHHHHHHH", raw, base + 240)
        slots.append(s)
    return {
        "magic": magic,
        "version": version,
        "frame": frame,
        "slotSize": slot_size,
        "savestateResets": ss_resets,
        "autoLCancelActive": alc,
        "inputBuffer3fActive": buf3f,
        "maxSlots": max_slots,
        "slots": slots,
    }


def p1_slot(blk):
    for s in blk["slots"]:
        if s["used"] and s["playerNo"] == 0 and s["partner"] == 0:
            return s
    raise Stage4aCoreError("P1 main slot not found in TM4C debug block")


def start_match(emu, p1_name, cpu_name="fox", stage="final_destination"):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"stage4a-core: {p1_name} vs {cpu_name} on {stage}")
    nav.start_quick_match(emu, stage=stage, p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    CT.wait_game_frames(emu, 20)
    f = LT.Fighters(emu)
    p1 = f.by_player(0)
    cpu = f.by_player(1)
    if p1 is None or cpu is None:
        raise Stage4aCoreError("fighters not found after match start")
    dbg = find_debug_block(emu)
    return lab, settings, p1, cpu, dbg


def grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=False):
    def step(n=1):
        emu.h.call("frame_advance", n=n)

    if start_from_stage:
        emu.hold(0, stick=LEFT)
        step(3)
        emu.hold(0, ["X"], stick=LEFT)
        step(3)
        emu.neutral(0)
        step(16)
        for wx, wy in ((100.0, 15.0), (100.0, -10.0), (92.0, -12.0)):
            put_fighter(emu, posture_p1, wx, wy)
            step(1)
    for _ in range(95):
        s = read_fighter_full(emu, p1_ptr)
        if s["status"] == ST_CLIFF_WAIT:
            return s
        if s["status"] == ST_CLIFF_CATCH:
            emu.neutral(0)
        elif s["y"] < -28.0:
            emu.hold(0, ["B"], stick=(70, 228))
        else:
            emu.hold(0, stick=LEFT)
        step(1)
    raise Stage4aCoreError(f"fighter never grabbed FD right ledge (last={read_fighter_full(emu, p1_ptr)})")


# ---- Scenario 1: Ledgedash, Tourney Winner & 5-Ledge-Grab Limit ------------

def sc_ledge_core(emu, notes):
    fails = []
    _lab, _settings, p1, _cpu, dbg = start_match(emu, "mario", "fox", stage="final_destination")
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)

    blk0 = read_debug_block(emu, dbg)
    notes.append(
        f"TM4C header: magic={blk0['magic']:#x} ver={blk0['version']} slotSize={blk0['slotSize']} "
        f"maxSlots={blk0['maxSlots']} alc={blk0['autoLCancelActive']} buf3f={blk0['inputBuffer3fActive']}"
    )
    if blk0["magic"] != TM4C_MAGIC or blk0["version"] != TM4C_VERSION or blk0["slotSize"] != TM4C_SLOT_SIZE:
        return [f"invalid TM4C header: {blk0}"]

    p1_ptr = p1["fighter"]
    posture_p1 = read_fighter_full(emu, p1_ptr)["posture"]

    def step(n=1):
        emu.h.call("frame_advance", n=n)

    def perform_ledgedash(from_stage):
        grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=from_stage)
        emu.neutral(0)
        # Returned right after Cliff_Wait frame 1: step 1 neutral frame for
        # RA-Bit[19] latch on frame 2, then press Down on frame 3 (dropFrame == 1).
        step(1)
        emu.hold(0, stick=DOWN)
        for _ in range(8):
            step(1)
            if read_fighter_full(emu, p1_ptr)["status"] == ST_FALL:
                break
        emu.hold(0, ["X"], stick=LEFT)
        step(2)
        emu.hold(0, stick=LEFT)
        for _ in range(18):
            step(1)
            if read_fighter_full(emu, p1_ptr)["y"] > 1.0:
                break
        emu.hold(0, ["R"], stick=(38, 60))
        step(3)
        emu.neutral(0)
        for _ in range(45):
            step(1)
            cur = read_fighter_full(emu, p1_ptr)
            if not cur["air"] and cur["status"] == ST_WAIT:
                break
        step(2)

    # 1a. Intentional ledgedash on grab #1 (GALINT > 0, dropFrame, cells).
    perform_ledgedash(from_stage=True)
    s1 = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"ledge #1 intentional: seq={s1['ledgeSeq']} out={s1['ledgeOutcome']} galint={s1['ledgeGalint']} "
        f"dropF={s1['ledgeDropFrame']} wait={s1['ledgeCliffWaitFrames']} latch={s1['ledgeNeutralLatchFrames']} "
        f"fall={s1['ledgeFallFrames']} jump={s1['ledgeJumpFrames']} adAir={s1['ledgeAirdodgeAirFrames']} "
        f"land={s1['ledgeLandingLagFrames']} cells={s1['ledgeCellCount']} grabs={s1['ledgeGrabCount']}"
    )
    if s1["ledgeOutcome"] != LEDGE_OUT_LEDGEDASH:
        fails.append(f"ledge #1: expected outcome={LEDGE_OUT_LEDGEDASH}, got {s1['ledgeOutcome']}")
    if s1["ledgeGalint"] <= 0:
        fails.append(f"ledge #1: expected positive GALINT, got {s1['ledgeGalint']}")
    if s1["ledgeDropFrame"] < 1:
        fails.append(f"ledge #1: invalid dropFrame {s1['ledgeDropFrame']}")
    if s1["ledgeCellCount"] < 5 or s1["ledgeGrabCount"] != 1:
        fails.append(f"ledge #1: cells={s1['ledgeCellCount']} grabs={s1['ledgeGrabCount']}")

    # 1b. Tourney Winner (Cliff_Wait -> press X -> 0x79 -> 0x7A -> land on FD).
    prev_seq = s1["ledgeSeq"]
    grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=True)
    emu.neutral(0)
    step(2)
    emu.hold(0, ["X"])
    saw_tw_st = False
    for _ in range(12):
        step(1)
        st = read_fighter_full(emu, p1_ptr)["status"]
        if st in (ST_CLIFF_JUMP1, ST_CLIFF_JUMP2):
            saw_tw_st = True
            break
    emu.neutral(0)
    for _ in range(75):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == ST_WAIT:
            break
    step(2)
    s2 = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"tourney winner: saw_0x79_7A={saw_tw_st} seq={s2['ledgeSeq']} out={s2['ledgeOutcome']} "
        f"slow={s2['ledgeTourneySlow']} twCount={s2['ledgeTourneyWinners']}"
    )
    if not saw_tw_st or s2["ledgeSeq"] <= prev_seq or s2["ledgeOutcome"] != LEDGE_OUT_TOURNEY_WINNER:
        fails.append(f"tourney winner failed: saw={saw_tw_st} seq={s2['ledgeSeq']} out={s2['ledgeOutcome']}")
    if s2["ledgeTourneyWinners"] < 1:
        fails.append(f"tourney winner counter did not increment: {s2['ledgeTourneyWinners']}")

    # 1c. 5-Ledge-Grab limit (6 consecutive grabs without landing -> NoInvulnLimit).
    for idx in range(1, 7):
        grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=(idx == 1))
        emu.neutral(0)
        step(2)
        if idx < 6:
            emu.hold(0, stick=DOWN)
            for _ in range(10):
                step(1)
                if read_fighter_full(emu, p1_ptr)["status"] != ST_CLIFF_WAIT:
                    break
            emu.neutral(0)
            for k in range(45):
                cur = read_fighter_full(emu, p1_ptr)
                if cur["la2"] == 0 and k >= 30:
                    break
                if cur["y"] < -35.0:
                    put_fighter(emu, posture_p1, 95.0, -12.0)
                emu.hold(0, stick=(160, 128))
                step(1)
            put_fighter(emu, posture_p1, 92.0, -10.0)
            emu.hold(0, stick=LEFT)
            step(1)

    s6_hang = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"grab #6 mid-hang: activePhase={s6_hang['ledgeActivePhase']} activeGrabs={s6_hang['ledgeActiveGrabs']} "
        f"activeNoInvuln={s6_hang['ledgeActiveNoInvuln']} noInvulnGrabs={s6_hang['ledgeNoInvulnGrabs']}"
    )
    if s6_hang["ledgeActiveGrabs"] != 6 or s6_hang["ledgeActiveNoInvuln"] != 1 or s6_hang["ledgeNoInvulnGrabs"] < 1:
        fails.append(f"grab #6 mid-hang mismatch: {s6_hang}")

    # Complete ledgedash from grab #6.
    emu.hold(0, stick=DOWN)
    for _ in range(8):
        step(1)
        if read_fighter_full(emu, p1_ptr)["status"] == ST_FALL:
            break
    emu.hold(0, ["X"], stick=LEFT)
    step(2)
    emu.hold(0, stick=LEFT)
    for _ in range(18):
        step(1)
        if read_fighter_full(emu, p1_ptr)["y"] > 1.0:
            break
    emu.hold(0, ["R"], stick=(38, 60))
    step(3)
    emu.neutral(0)
    for _ in range(45):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == ST_WAIT:
            break
    step(2)
    s6_done = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"grab #6 ledgedash: out={s6_done['ledgeOutcome']} failFlags={s6_done['ledgeFailFlags']:#x} "
        f"galint={s6_done['ledgeGalint']} grabs={s6_done['ledgeGrabCount']}"
    )
    if s6_done["ledgeOutcome"] != LEDGE_OUT_NO_INVULN:
        fails.append(f"grab #6 ledgedash: expected NoInvulnLimit ({LEDGE_OUT_NO_INVULN}), got {s6_done['ledgeOutcome']}")
    if (s6_done["ledgeFailFlags"] & LEDGE_FAIL_NO_INVULN) == 0 or s6_done["ledgeGalint"] != 0:
        fails.append(f"grab #6 flags/galint mismatch: flags={s6_done['ledgeFailFlags']:#x} galint={s6_done['ledgeGalint']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 2: Multi-Jumper Fly (0x0D) Ledgedash (Kirby) -----------------

def sc_ledge_fly(emu, notes):
    fails = []
    _lab, _settings, p1, _cpu, dbg = start_match(emu, "kirby", "fox", stage="final_destination")
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)

    p1_ptr = p1["fighter"]
    posture_p1 = read_fighter_full(emu, p1_ptr)["posture"]

    def step(n=1):
        emu.h.call("frame_advance", n=n)

    grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=True)
    emu.neutral(0)
    step(2)
    emu.hold(0, stick=DOWN)
    for _ in range(8):
        step(1)
        if read_fighter_full(emu, p1_ptr)["status"] == ST_FALL:
            break
    emu.hold(0, ["X"], stick=LEFT)
    saw_fly = False
    for _ in range(4):
        step(1)
        if read_fighter_full(emu, p1_ptr)["status"] == ST_FLY:
            saw_fly = True
    emu.hold(0, stick=LEFT)
    for _ in range(22):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if cur["status"] == ST_FLY:
            saw_fly = True
        if cur["y"] > 1.0:
            break
    emu.hold(0, ["R"], stick=(38, 60))
    step(3)
    emu.neutral(0)
    for _ in range(50):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == ST_WAIT:
            break
    step(2)
    sk = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"kirby Fly(0x0D) ledgedash: saw_fly={saw_fly} out={sk['ledgeOutcome']} usedFly={sk['ledgeUsedFly']} "
        f"galint={sk['ledgeGalint']} jumpF={sk['ledgeJumpFrames']} cells={sk['ledgeCells'][:10]}"
    )
    if not saw_fly or sk["ledgeOutcome"] != LEDGE_OUT_LEDGEDASH or sk["ledgeUsedFly"] != 1:
        fails.append(f"kirby Fly ledgedash failed: saw_fly={saw_fly} out={sk['ledgeOutcome']} usedFly={sk['ledgeUsedFly']}")
    if LEDGE_CELL_JUMP not in sk["ledgeCells"]:
        fails.append(f"kirby Fly cell kLedgeCellJump (2) missing from cells={sk['ledgeCells']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 3: Frame-1 Wavedash (FD) & Platform Waveland (Battlefield) ---

def sc_wavedash_waveland(emu, notes):
    fails = []

    # 3a. Frame-1 grounded wavedash on FD (Fox: 3f jumpsquat -> Jump f1, Guard+down-right f4).
    _lab, _settings, p1, _cpu, dbg = start_match(emu, "fox", "mario", stage="final_destination")
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    inp = PI.PluginInput(emu)

    wd_frames = {
        1: (PI.B_JUMP, 0.0, 0.0),
        4: (PI.B_GUARD, 0.96, -0.28),
        5: (PI.B_GUARD, 0.96, -0.28),
    }
    inp.run({0: PI.frames_to_steps(wd_frames, 8)}, extra=20)
    swd = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"FD frame-1 wavedash: seq={swd['wdSeq']} kind={swd['wdKind']} out={swd['wdOutcome']} "
        f"jsToDodge={swd['wdJumpsquatToDodge']} onPlat={swd['wdOnPlatform']} "
        f"angle={swd['wdAngleDeg']:.2f} slide={swd['wdSlideDistance']:.2f} "
        f"wdStats=({swd['wdAttempts']},{swd['wdSuccesses']},{swd['wdPerfect']}) wlAttempts={swd['wlAttempts']}"
    )
    if swd["wdKind"] != WD_KIND_WAVEDASH or swd["wdOutcome"] != WD_OUT_SUCCESS:
        fails.append(f"FD wavedash kind/outcome mismatch: kind={swd['wdKind']} out={swd['wdOutcome']}")
    if swd["wdJumpsquatToDodge"] != 1 or swd["wdPerfect"] < 1:
        fails.append(f"FD wavedash not frame-1: jsToDodge={swd['wdJumpsquatToDodge']} perfect={swd['wdPerfect']}")
    if swd["wdOnPlatform"] != 0 or swd["wdSlideDistance"] < 3.0 or swd["wlAttempts"] != 0:
        fails.append(
            f"FD wavedash platform/slide mismatch: onPlat={swd['wdOnPlatform']} "
            f"slide={swd['wdSlideDistance']:.2f} wlAttempts={swd['wlAttempts']}"
        )
    emu.h.call("resume")

    # 3b. Platform waveland on Battlefield (Fox jumps from under left side platform x=-35, y=0
    #     up above y=27.2 and airdodges down-left onto the platform).
    _lab, _settings, p1, _cpu, dbg = start_match(emu, "fox", "mario", stage="battlefield")
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    p1_ptr = p1["fighter"]
    posture_p1 = read_fighter_full(emu, p1_ptr)["posture"]

    def step(n=1):
        emu.h.call("frame_advance", n=n)

    # Full-hop from main floor underneath the left side platform (platform spans -55..-20 at y ~ 27.2).
    # Wait until Fox rises above y=29 and falls back down to y <= 29.5 (just above platform), then
    # airdodge shallow down-right (stick=(224, 92), sy ~ -0.37) so Fox lands in 1-2f and slides cleanly.
    put_fighter(emu, posture_p1, -48.0, 0.0)
    step(2)
    emu.hold(0, ["X"])
    step(6)
    emu.neutral(0)
    cleared_top = False
    reached = False
    prev_y = -100.0
    for _ in range(40):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if cur["y"] > 30.0:
            cleared_top = True
        if cleared_top and cur["y"] < prev_y and cur["y"] <= 29.5:
            reached = True
            break
        prev_y = cur["y"]
    # Airdodge shallow down-right onto the Battlefield side platform.
    emu.hold(0, ["R"], stick=(224, 92))
    step(2)
    emu.neutral(0)
    landed_plat = False
    wl_trace = []
    for i in range(45):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if i < 20:
            wl_trace.append((hex(cur["status"]), round(cur["x"], 1), round(cur["y"], 1)))
        if not cur["air"] and cur["status"] == ST_WAIT and cur["y"] > 20.0:
            landed_plat = True
            break
    step(2)
    swl = p1_slot(read_debug_block(emu, dbg))
    cur_end = read_fighter_full(emu, p1_ptr)
    notes.append(
        f"BF platform waveland: reached={reached} landed_plat={landed_plat} end=({cur_end['x']:.1f},{cur_end['y']:.1f}) "
        f"seq={swl['wdSeq']} kind={swl['wdKind']} out={swl['wdOutcome']} onPlat={swl['wdOnPlatform']} "
        f"adAirF={swl['wdAirdodgeAirFrames']} slide={swl['wdSlideDistance']:.2f} "
        f"wlStats=({swl['wlAttempts']},{swl['wlSuccesses']},{swl['wlPerfect']}) trace={wl_trace[:10]}"
    )
    if not reached or not landed_plat:
        fails.append(f"BF platform waveland did not land on platform: reached={reached} end={cur_end}")
    if swl["wdKind"] != WD_KIND_WAVELAND or swl["wdOutcome"] != WD_OUT_SUCCESS or swl["wdOnPlatform"] != 1:
        fails.append(
            f"BF waveland mismatch: kind={swl['wdKind']} out={swl['wdOutcome']} onPlat={swl['wdOnPlatform']}"
        )
    if swl["wdSlideDistance"] < 2.0 or swl["wlSuccesses"] < 1:
        fails.append(f"BF waveland slide/stats mismatch: slide={swl['wdSlideDistance']:.2f} succ={swl['wlSuccesses']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 4: L-Cancel (Whiff, Hit/Hitlag, Late Miss & ALC Flag) --------

def sc_lcancel_core(emu, notes):
    fails = []
    _lab, _settings, p1, cpu, dbg = start_match(emu, "mario", "fox", stage="final_destination")
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)

    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]
    posture_p1 = read_fighter_full(emu, p1_ptr)["posture"]
    posture_cpu = read_fighter_full(emu, cpu_ptr)["posture"]

    def step(n=1):
        emu.h.call("frame_advance", n=n)

    # 4a. ALC rule flag live read check (toggle bit 0x01 on modeMelee+0xE5 -> verify -> restore).
    g_global = emu.ptr(0x805A00E0)
    mode_melee = emu.ptr(g_global + 0x08) if g_global else None
    if mode_melee is None:
        return ["could not resolve g_GameGlobal->m_modeMelee for ALC check"]
    orig_flags = emu.read(mode_melee + 0xE5, 1)[0]
    emu.h.call("write_mem", addr=mode_melee + 0xE5, hex=f"{(orig_flags | 0x01):02x}")
    step(2)
    blk_alc_on = read_debug_block(emu, dbg)
    emu.h.call("write_mem", addr=mode_melee + 0xE5, hex=f"{(orig_flags & ~0x01):02x}")
    step(2)
    blk_alc_off = read_debug_block(emu, dbg)
    notes.append(
        f"ALC flag read check: orig={orig_flags:#x} when_set={blk_alc_on['autoLCancelActive']} "
        f"when_clear={blk_alc_off['autoLCancelActive']}"
    )
    if blk_alc_on["autoLCancelActive"] != 1 or blk_alc_off["autoLCancelActive"] != 0:
        fails.append(
            f"ALC flag read mismatch: on={blk_alc_on['autoLCancelActive']} off={blk_alc_off['autoLCancelActive']}"
        )

    # Helper: perform a short-hop Nair (reliable 0x18 Landing_Attack_Air, matching osd_tests.py)
    # with optional R timing for whiff, hit (through hitlag), or +3f late miss.
    def do_shorthop_nair(press_mode):
        emu.hold(0, ["X"])
        step(2)
        emu.neutral(0)
        for _ in range(8):
            step(1)
            if read_fighter_full(emu, p1_ptr)["air"]:
                break
        if press_mode == "hit":
            # Delay Nair until descending onto Fox so hitbox connects right before landing (hitlag!).
            prev_y = -100.0
            for _ in range(20):
                step(1)
                cur = read_fighter_full(emu, p1_ptr)
                if cur["y"] < prev_y and cur["y"] < 14.0:
                    break
                prev_y = cur["y"]
        emu.hold(0, ["A"])
        step(2)
        emu.neutral(0)

        pressed = False
        land_frame = None
        prev_y = -100.0
        for i in range(60):
            cur = read_fighter_full(emu, p1_ptr)
            falling = cur["y"] < prev_y
            prev_y = cur["y"]
            if press_mode == "whiff" and not pressed and cur["air"] and falling and cur["y"] < 4.5:
                emu.hold(0, ["R"])
                step(2)
                emu.neutral(0)
                pressed = True
                continue
            if press_mode == "hit" and not pressed and cur["air"] and falling and cur["y"] < 5.0 and i >= 2:
                emu.hold(0, ["R"])
                step(2)
                emu.neutral(0)
                pressed = True
                continue
            if cur["status"] == ST_LANDING_ATTACK_AIR and land_frame is None:
                land_frame = i
                if press_mode == "late":
                    # Wait 2 frames inside 0x18 then press R on +3f!
                    step(2)
                    emu.hold(0, ["R"])
                    step(2)
                    emu.neutral(0)
                    step(4)
                    return
            if land_frame is not None and i - land_frame >= 6:
                break
            step(1)
        emu.neutral(0)
        for _ in range(30):
            step(1)
            cur = read_fighter_full(emu, p1_ptr)
            if not cur["air"] and cur["status"] == ST_WAIT:
                break

    # 4b. Manual L-Cancel on Whiff (keep Fox far away).
    put_fighter(emu, posture_p1, -40.0, 0.0)
    put_fighter(emu, posture_cpu, 40.0, 0.0)
    step(4)
    do_shorthop_nair("whiff")
    sl_whiff = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"L-cancel whiff: seq={sl_whiff['lcSeq']} out={sl_whiff['lcOutcome']} ctx={sl_whiff['lcContext']} "
        f"pf={sl_whiff['lcPressFrame']} whiff=({sl_whiff['lcWhiffHits']}/{sl_whiff['lcWhiffTotal']})"
    )
    if sl_whiff["lcOutcome"] != LC_OUT_SUCCESS_MANUAL or sl_whiff["lcContext"] != LC_CTX_WHIFF:
        fails.append(f"L-cancel whiff mismatch: out={sl_whiff['lcOutcome']} ctx={sl_whiff['lcContext']}")
    if not (1 <= sl_whiff["lcPressFrame"] <= 7):
        fails.append(f"L-cancel whiff pressFrame out of range: {sl_whiff['lcPressFrame']}")

    # 4c. Manual L-Cancel on Hit (position Mario directly beside Fox so falling Nair hits + hitlag).
    put_fighter(emu, posture_p1, 0.0, 0.0)
    put_fighter(emu, posture_cpu, 4.0, 0.0)
    step(4)
    prev_seq = sl_whiff["lcSeq"]
    do_shorthop_nair("hit")
    sl_hit = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"L-cancel hit: seq={sl_hit['lcSeq']} out={sl_hit['lcOutcome']} ctx={sl_hit['lcContext']} "
        f"pf={sl_hit['lcPressFrame']} hit=({sl_hit['lcHitHits']}/{sl_hit['lcHitTotal']})"
    )
    if sl_hit["lcSeq"] <= prev_seq or sl_hit["lcOutcome"] != LC_OUT_SUCCESS_MANUAL or sl_hit["lcContext"] != LC_CTX_HIT:
        fails.append(f"L-cancel hit mismatch: seq={sl_hit['lcSeq']} out={sl_hit['lcOutcome']} ctx={sl_hit['lcContext']}")
    if sl_hit["lcHitHits"] < 1 or sl_hit["lcHitTotal"] < 1:
        fails.append(f"L-cancel hit stats mismatch: {sl_hit['lcHitHits']}/{sl_hit['lcHitTotal']}")

    # 4d. Real Late Miss (+2..+4f after entering 0x18).
    put_fighter(emu, posture_p1, -40.0, 0.0)
    put_fighter(emu, posture_cpu, 40.0, 0.0)
    step(4)
    prev_seq = sl_hit["lcSeq"]
    do_shorthop_nair("late")
    sl_late = p1_slot(read_debug_block(emu, dbg))
    notes.append(
        f"L-cancel late miss: seq={sl_late['lcSeq']} out={sl_late['lcOutcome']} lateF={sl_late['lcLateFrames']} "
        f"lateMisses={sl_late['lcLateMisses']} total={sl_late['lcTotal']}"
    )
    if sl_late["lcSeq"] <= prev_seq or sl_late["lcOutcome"] != LC_OUT_MISS_LATE:
        fails.append(f"L-cancel late miss mismatch: seq={sl_late['lcSeq']} out={sl_late['lcOutcome']}")
    if not (1 <= sl_late["lcLateFrames"] <= 5) or sl_late["lcLateMisses"] < 1:
        fails.append(f"L-cancel lateFrames/count mismatch: lateF={sl_late['lcLateFrames']} count={sl_late['lcLateMisses']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 5: In-Game Savestate Transient Reset & Clean Detach ----------

def sc_savestate_detach(emu, notes):
    fails = []
    _lab, _settings, _p1, _cpu, dbg = start_match(emu, "mario", "fox", stage="final_destination")
    before = read_debug_block(emu, dbg)

    # Save an in-game Lab savestate via real D-pad Right (12+ frame hold), jump, then load via D-pad Left.
    emu.hold(0, ["DRIGHT"])
    CT.wait_game_frames(emu, 20)
    emu.neutral(0)
    CT.wait_game_frames(emu, 20)

    emu.hold(0, ["X"])
    CT.wait_game_frames(emu, 3)
    emu.neutral(0)
    CT.wait_game_frames(emu, 6)

    emu.hold(0, ["DLEFT"])
    CT.wait_game_frames(emu, 20)
    emu.neutral(0)
    CT.wait_game_frames(emu, 20)

    after = read_debug_block(emu, dbg)
    notes.append(
        f"in-game savestate reset: before={before['savestateResets']} after={after['savestateResets']} "
        f"frame={after['frame']}"
    )
    if after["savestateResets"] <= before["savestateResets"]:
        fails.append(
            f"savestateResets did not increment after in-game D-pad load: "
            f"before={before['savestateResets']} after={after['savestateResets']}"
        )

    # Exit match cleanly to verify extra module detaches without errors/leaks.
    pos_exit = len(emu.log_text())
    nav.finish_training(emu)
    exit_log = emu.synced_log_text()[pos_exit:]
    detached = "extra: detaching" in exit_log
    notes.append(f"detach check: extra_detaching={detached}")
    if not detached:
        fails.append("extra module did not log 'extra: detaching' on match exit")

    return fails


SCENARIO_FNS = {
    "ledge-core": sc_ledge_core,
    "ledge-fly": sc_ledge_fly,
    "wavedash-waveland": sc_wavedash_waveland,
    "lcancel-core": sc_lcancel_core,
    "savestate-detach": sc_savestate_detach,
}


def cmd_test_stage4a_core(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage4a-core")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    label = "test-stage4a-core"
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
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError, Stage4aCoreError,
                        ValueError) as exc:
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
                except (Timeout, nav.NavError, HarnessError):
                    pass
    except (OSError, HarnessError, Timeout, nav.NavError) as exc:
        why = f"boot: {type(exc).__name__}: {exc}"

    print("\n--- stage4a-core measurements ---")
    for line in notes:
        print(" ", line)
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and failed:
        why = f"{len(failed)} scenario(s) failed: {', '.join(n for n, _ in failed)}"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
