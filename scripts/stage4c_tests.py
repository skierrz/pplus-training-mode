"""Emulator test suite for Stage 4C Track D novel P+ challenge backends
(run through emu.py):

  test-stage4c REL [--only NAME ...] [--list] [--keep-going]

Headless Null backend by default. Verifies all five challenge backends through
real GameCube controller input + live RAM "TM4X" debug block:

  cancel-sprint        Fox Shine jump-cancel (0x115 -> 0x11A -> 0x0A) and
                       Lucario Aura Sphere Cancel (0x117 -> 0x11E shield).
  flow-course          Battlefield dynamic platform layout (hasPlatformGate = 1),
                       center dash-dance turnaround (Gate 0 -> 1) and platform
                       waveland (Gate 1 -> 2).
  reaction-techchase   Reaction Lab measuring live opponent attack startup cues
                       with exact ring mean/median + Universal Techchase Escape
                       after grab/throw/knockdown.
  recovery-floorhug    Offstage recovery sweetspot grading (LA-Basic[79], XLU,
                       height delta) + Floorhug / ASDI-Down challenge reps.
  snake-recovery       Snake offstage Cypher (LA-Bit[61], LA-Float[52] stacks)
                       + C4 air drop/detonate self-hit Cypher refresh loop +
                       ledge completion.
  savestate-reset      Lab quick savestate save/load increments resetCount,
                       clears transient state cleanly, and match exit unloads
                       with zero heap leak.
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
    "fox": (0x07, 0x07),
    "lucario": (0x24, 0x20),
    "snake": (0x2A, 0x26),
}

RIGHT = (228, 128)
LEFT = (28, 128)
UP = (128, 228)
DOWN = (128, 28)
DOWN_RIGHT = (205, 68)
DOWN_LEFT = (51, 68)
NEUTRAL = (128, 128)

TM4X_MAGIC = 0x544D3458  # "TM4X"
TM4X_LOG_RE = re.compile(r"challenge4c: debug block at ([0-9a-f]{8})")
TM4X_HDR_FMT = ">4I"
TM4X_SLOT_FMT = (
    ">"
    "BbBBI"                 # header (8B)
    "IIIIHHHHBBBB"          # cancel sprint (28B)
    "IIIHHHHHHBBBBff"       # flow course (36B)
    "IHHffHBBIIIHHHHBBBB I" # reaction lab + techchase (48B)
    "IIIIIHHBBhfIIIHH"      # recovery + floorhug (48B)
    "IIHHBBBBfffI"          # snake recovery (32B)
).replace(" ", "")
TM4X_SLOT_SIZE = struct.calcsize(TM4X_SLOT_FMT)
assert TM4X_SLOT_SIZE == 200, f"TM4X slot size mismatch: {TM4X_SLOT_SIZE}"

# Core enum constants matching src/extra/challenge_*_core.h.
CANCEL_SHINE = 1
CANCEL_ASC = 4
REC_GRADE_SWEETSPOT = 1
SNAKE_SELFHIT_C4 = 1

# main::Settings offsets (src/main/settings.h).
S_PLAYER_PERCENT = 8
S_PLAYER_LOCK = 10

SCENARIOS = [
    "cancel-sprint",
    "flow-course",
    "reaction-techchase",
    "recovery-floorhug",
    "snake-recovery",
    "savestate-reset",
]


class Stage4cError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def find_tm4x(emu):
    hits = TM4X_LOG_RE.findall(emu.synced_log_text())
    if not hits:
        raise Stage4cError("challenge4c debug block address not found in log")
    addr = int(hits[-1], 16)
    magic, version, frame, reset_cnt = struct.unpack(TM4X_HDR_FMT, emu.read(addr, 16))
    if magic != TM4X_MAGIC:
        raise Stage4cError(f"bad TM4X magic at {addr:#x}: got {magic:#x}")
    return addr


def read_tm4x(emu, addr, slot=0):
    hdr = struct.unpack(TM4X_HDR_FMT, emu.read(addr, 16))
    raw = emu.read(addr + 16 + slot * TM4X_SLOT_SIZE, TM4X_SLOT_SIZE)
    v = struct.unpack(TM4X_SLOT_FMT, raw)
    return {
        "magic": hdr[0], "version": hdr[1], "frame": hdr[2], "resetCount": hdr[3],
        # Header
        "used": v[0], "playerNo": v[1], "ftKind": v[2], "slotFrame": v[4],
        # 1. Cancel Sprint
        "csAttempts": v[5], "csTotalCancels": v[6], "csPerfectCount": v[7], "csSumFrames": v[8],
        "csBestFrames": v[9], "csLastFrames": v[10], "csStreak": v[11], "csBestStreak": v[12],
        "csLastKind": v[13], "csLastSuccess": v[14], "csActive": v[15],
        # 2. Flow Course
        "fcCompleted": v[17], "fcCurrentElapsed": v[18], "fcBestFrames": v[19],
        "fcSplits": (v[20], v[21], v[22], v[23]),
        "fcStreak": v[24], "fcBestStreak": v[25],
        "fcActiveGate": v[26], "fcHasPlatformGate": v[27], "fcRunning": v[28],
        "fcGateX": v[30], "fcGateY": v[31],
        # 3. Reaction Lab + Techchase
        "rlCount": v[32], "rlMinFrames": v[33], "rlMaxFrames": v[34],
        "rlMeanFrames": v[35], "rlMedianFrames": v[36],
        "rlLastFrames": v[37], "rlLastCueKind": v[38], "rlCueActive": v[39],
        "tcEscapes": v[40], "tcPerfectEscapes": v[41], "tcCaughtCount": v[42],
        "tcStreak": v[43], "tcBestStreak": v[44], "tcLastTightness": v[45], "tcBestTightness": v[46],
        "tcLastTool": v[47], "tcPhase": v[48], "rlRingCount": v[51],
        # 4. Recovery Sweetspot + Floorhug
        "recAttempts": v[52], "recSweetspots": v[53], "recHighCount": v[54],
        "recNoInvulnCount": v[55], "recSdCount": v[56],
        "recStreak": v[57], "recBestStreak": v[58],
        "recLastGrade": v[59], "recLastGrabs": v[60], "recLastXlu": v[61],
        "recLastHeightDelta": v[62],
        "fhReps": v[63], "fhSuccesses": v[64], "fhTruePunishes": v[65],
        "fhStreak": v[66], "fhBestStreak": v[67],
        # 5. Snake Infinite Recovery
        "snkRefreshCount": v[68], "snkCompletedLoops": v[69],
        "snkLoopChain": v[70], "snkBestLoopChain": v[71],
        "snkCypherStacks": v[72], "snkCypherLockout": v[73],
        "snkLastSelfHitKind": v[74], "snkOffstage": v[75],
        "snkTotalSelfDamage": v[76], "snkAltitudeRecovered": v[77],
        "snkLastSelfHitDamage": v[78], "snkTotalCyphers": v[79],
    }


def read_fighter(emu, fighter_ptr):
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0xD0)
    posture = u32(mods, 0x0C)
    sit = u32(mods, 0x14)
    stop = u32(mods, 0x44)
    status_mod = u32(mods, 0x70)
    p = emu.read(posture, 0x44)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    lr = 1.0 if f32(p, 0x40) >= 0.0 else -1.0
    st = emu.u32(status_mod + 0x34)
    air = emu.u32(sit + 0x2C) == 2
    hl = s32(emu.read(stop + 0x10, 4), 0)
    return {"x": x, "y": y, "lr": lr, "status": st, "air": air, "hitlag": hl}


def start_match(emu, p1_name, cpu_name="fox", stage="final_destination"):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"stage4c: {p1_name} vs {cpu_name} on {stage}")
    nav.start_quick_match(emu, stage=stage, p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
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
        raise Stage4cError("missing fighter pointers at match start")
    tm4x = find_tm4x(emu)
    return lab, settings, p1["fighter"], cpu["fighter"], tm4x


def walk_p1_to(emu, p1_ptr, target_x, face_dir=None, max_frames=360):
    emu.h.call("pause")
    # If spawned on a pass-through platform (e.g., Battlefield), drop to main floor first.
    sf = read_fighter(emu, p1_ptr)
    if sf["y"] > 5.0 and not sf["air"]:
        emu.hold(0, stick=DOWN)
        step(emu, 8)
        emu.neutral(0)
        for _ in range(45):
            sf = read_fighter(emu, p1_ptr)
            if not sf["air"] and sf["y"] < 3.0 and sf["status"] == 0x00:
                break
            step(emu, 1)
    # Fast walk/run until within 16 units.
    for _ in range(max_frames // 2):
        sf = read_fighter(emu, p1_ptr)
        dx = target_x - sf["x"]
        if abs(dx) <= 16.0 and not sf["air"]:
            break
        sx = 223 if dx > 0 else 33
        emu.hold(0, stick=(sx, 128))
        step(emu, 1)
    emu.neutral(0)
    for _ in range(30):
        sf = read_fighter(emu, p1_ptr)
        if not sf["air"] and sf["status"] == 0x00:
            break
        step(emu, 1)
    # Fine walk until within 1.5 units.
    for _ in range(max_frames // 2):
        sf = read_fighter(emu, p1_ptr)
        dx = target_x - sf["x"]
        if abs(dx) <= 1.5 and not sf["air"] and sf["status"] in (0x00, 0x01, 0x02, 0x05):
            break
        sx = 172 if dx > 0 else 84
        emu.hold(0, stick=(sx, 128))
        step(emu, 1)
    emu.neutral(0)
    for _ in range(25):
        sf = read_fighter(emu, p1_ptr)
        if not sf["air"] and sf["status"] == 0x00:
            break
        step(emu, 1)
    if face_dir is not None:
        sf = read_fighter(emu, p1_ptr)
        if sf["lr"] * face_dir < 0:
            emu.hold(0, stick=(168 if face_dir > 0 else 88, 128))
            step(emu, 2)
            emu.neutral(0)
            step(emu, 14)
    emu.h.call("resume")


# ---- Scenario 1: Cancel Sprint (Fox Shine JC + Lucario ASC) -----------------

def sc_cancel_sprint(emu, notes):
    fails = []

    # 1A. Fox Shine JC twice on Final Destination.
    _, _, p1_ptr, _, tm4x = start_match(emu, "fox", "fox")
    emu.h.call("pause")
    for _ in range(2):
        emu.neutral(0)
        step(emu, 10)
        emu.hold(0, ["B"], stick=DOWN)
        step(emu, 2)
        emu.hold(0, stick=DOWN)
        # Wait until 0x11A (SpecialLwLoop) opens, then press Jump (X).
        for _f in range(10):
            sf = read_fighter(emu, p1_ptr)
            if sf["status"] == 0x11A:
                break
            step(emu, 1)
        emu.hold(0, ["X"], stick=NEUTRAL)
        step(emu, 3)
        emu.neutral(0)
        # Wait until back grounded in Wait.
        for _f in range(60):
            sf = read_fighter(emu, p1_ptr)
            if not sf["air"] and sf["status"] == 0x00:
                break
            step(emu, 1)
    d_fox = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")
    notes.append(
        f"cancel-sprint fox: att={d_fox['csAttempts']} cancels={d_fox['csTotalCancels']} "
        f"perf={d_fox['csPerfectCount']} best={d_fox['csBestFrames']}f last={d_fox['csLastFrames']}f "
        f"kind={d_fox['csLastKind']} streak={d_fox['csStreak']}"
    )
    if d_fox["csTotalCancels"] < 2 or d_fox["csLastKind"] != CANCEL_SHINE:
        fails.append(f"fox shine cancel expected >=2 cancels kind={CANCEL_SHINE}, got {d_fox['csTotalCancels']} kind={d_fox['csLastKind']}")
    if d_fox["csBestFrames"] < 1 or d_fox["csStreak"] < 2:
        fails.append(f"fox shine metrics invalid: best={d_fox['csBestFrames']} streak={d_fox['csStreak']}")

    # 1B. Lucario Aura Sphere Cancel (0x117 -> L shield -> 0x11E).
    _, _, p1_ptr, _, tm4x = start_match(emu, "lucario", "fox")
    emu.h.call("pause")
    emu.neutral(0)
    step(emu, 6)
    emu.hold(0, ["B"], stick=NEUTRAL)
    step(emu, 2)
    emu.neutral(0)
    for _f in range(25):
        sf = read_fighter(emu, p1_ptr)
        if sf["status"] == 0x117:
            break
        step(emu, 1)
    emu.hold(0, ["L"], stick=NEUTRAL)
    step(emu, 4)
    emu.neutral(0)
    step(emu, 18)
    d_luc = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")
    notes.append(
        f"cancel-sprint lucario: att={d_luc['csAttempts']} cancels={d_luc['csTotalCancels']} "
        f"best={d_luc['csBestFrames']}f last={d_luc['csLastFrames']}f kind={d_luc['csLastKind']} streak={d_luc['csStreak']}"
    )
    if d_luc["csTotalCancels"] < 1 or d_luc["csLastKind"] != CANCEL_ASC:
        fails.append(f"lucario ASC expected >=1 cancel kind={CANCEL_ASC}, got {d_luc['csTotalCancels']} kind={d_luc['csLastKind']}")

    return fails


# ---- Scenario 2: Movement Flow Course on Battlefield ------------------------

def sc_flow_course(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4x = start_match(emu, "fox", "fox", stage="battlefield")
    walk_p1_to(emu, p1_ptr, 0.0, face_dir=1.0)

    d0 = read_tm4x(emu, tm4x, 0)
    if d0["fcHasPlatformGate"] != 1:
        fails.append(f"battlefield expected fcHasPlatformGate=1, got {d0['fcHasPlatformGate']}")

    # Gate 0: Dash-dance turnaround near center.
    emu.h.call("pause")
    step(emu, 2)
    emu.hold(0, stick=RIGHT)
    step(emu, 8)
    emu.hold(0, stick=LEFT)
    step(emu, 8)
    emu.neutral(0)
    step(emu, 8)
    d1 = read_tm4x(emu, tm4x, 0)

    # Gate 1: Jump up to right platform and waveland onto it.
    target_x, target_y = d1["fcGateX"], d1["fcGateY"]
    emu.h.call("resume")
    walk_p1_to(emu, p1_ptr, target_x - 8.0, face_dir=1.0)
    emu.h.call("pause")
    # Full hop up toward platform (Fox full hop reaches y > 35, platform is ~27.2).
    emu.hold(0, ["X"], stick=(190, 128))
    step(emu, 7)
    emu.hold(0, stick=(180, 128))
    waveland_triggered = False
    for _f in range(45):
        sf = read_fighter(emu, p1_ptr)
        if sf["air"] and sf["y"] >= target_y + 1.0 and sf["y"] <= target_y + 10.0 and _f >= 8:
            emu.hold(0, ["L"], stick=DOWN_RIGHT)
            step(emu, 3)
            emu.neutral(0)
            step(emu, 16)
            waveland_triggered = True
            break
        step(emu, 1)
    if not waveland_triggered:
        emu.neutral(0)
        step(emu, 20)
    d2 = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")

    notes.append(
        f"flow-course bf: plat={d0['fcHasPlatformGate']} afterG0(gate={d1['fcActiveGate']}, "
        f"run={d1['fcRunning']}, target=({target_x:.1f},{target_y:.1f}), sp0={d1['fcSplits'][0]}) "
        f"afterG1(gate={d2['fcActiveGate']}, sp1={d2['fcSplits'][1]})"
    )
    if d1["fcActiveGate"] < 1:
        fails.append(f"flow course Gate 0 (dash-dance) did not advance: activeGate={d1['fcActiveGate']}")
    if target_y < 15.0:
        fails.append(f"battlefield platform gate Y too low: {target_y:.1f}")
    if d2["fcActiveGate"] < 2:
        fails.append(f"flow course Gate 1 (platform waveland) did not advance: activeGate={d2['fcActiveGate']}")
    return fails


# ---- Scenario 3: Reaction Lab + Universal Techchase Escape ------------------

def sc_reaction_techchase(emu, notes):
    fails = []
    _, _, p1_ptr, cpu_ptr, tm4x = start_match(emu, "fox", "fox")
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 32.0, face_dir=1.0)

    # 3A. Reaction Lab: CPU fires two Neutral-B Lasers; P1 dashes after 8f and 14f.
    inp = PI.PluginInput(emu)
    emu.h.call("pause")
    for delay in (8, 14):
        emu.neutral(0)
        step(emu, 12)
        cpu_steps = PI.frames_to_steps({2: (PI.B_SPECIAL, 0.0, 0.0)}, 3)
        cue_frame = [None]
        responded = [False]

        def track(idx):
            d = read_tm4x(emu, tm4x, 0)
            if cue_frame[0] is None and d["rlCueActive"] != 0:
                cue_frame[0] = idx
            if cue_frame[0] is not None and not responded[0] and idx >= cue_frame[0] + delay:
                emu.hold(0, stick=LEFT)
                responded[0] = True
            elif responded[0] and idx >= cue_frame[0] + delay + 3:
                emu.neutral(0)

        try:
            inp.run({1: cpu_steps}, read=track, extra=30)
        finally:
            inp.stop()
            emu.neutral(0)
        step(emu, 12)

    d_rl = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")

    # 3B. Universal Techchase Escape: CPU grabs P1 and throws -> P1 jumps/airdodges on actionable.
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 6.0, face_dir=1.0)
    emu.h.call("pause")
    # CPU stands facing left: neutral standing grab on frame 2-3, then forward throw left on frame 18-19.
    cpu_grab_throw = PI.frames_to_steps({
        2: (PI.B_GRAB, 0.0, 0.0),
        3: (PI.B_GRAB, 0.0, 0.0),
        18: (0, -1.0, 0.0),
        19: (0, -1.0, 0.0),
    }, 22)
    tc_seen = []

    def track_tc(_idx):
        d = read_tm4x(emu, tm4x, 0)
        sf = read_fighter(emu, p1_ptr)
        if len(tc_seen) < 20 and (not tc_seen or tc_seen[-1][0] != sf["status"]):
            tc_seen.append((sf["status"], d["tcPhase"]))
        # Rising-edge jump/dash input once P1 reaches actionable wait (tcPhase == 2).
        if d["tcPhase"] == 2:
            emu.hold(0, ["X"], stick=LEFT)
        else:
            emu.neutral(0)

    try:
        inp.run({1: cpu_grab_throw}, read=track_tc, extra=85)
    finally:
        inp.stop()
        emu.neutral(0)
    step(emu, 12)
    d_tc = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")
    if d_tc["tcEscapes"] < 1:
        notes.append("techchase trace: " + " ".join(f"{s:#x}:p{p}" for s, p in tc_seen))

    notes.append(
        f"reaction-lab: cnt={d_rl['rlCount']} ring={d_rl['rlRingCount']} "
        f"min={d_rl['rlMinFrames']} max={d_rl['rlMaxFrames']} "
        f"mean={d_rl['rlMeanFrames']:.1f} med={d_rl['rlMedianFrames']:.1f} cue={d_rl['rlLastCueKind']} | "
        f"techchase: esc={d_tc['tcEscapes']} perf={d_tc['tcPerfectEscapes']} "
        f"tool={d_tc['tcLastTool']} tight={d_tc['tcLastTightness']}f"
    )
    if d_rl["rlCount"] < 2 or d_rl["rlRingCount"] < 2:
        fails.append(f"reaction lab expected >=2 samples, got count={d_rl['rlCount']} ring={d_rl['rlRingCount']}")
    if d_rl["rlMinFrames"] < 1 or d_rl["rlMaxFrames"] < d_rl["rlMinFrames"] or d_rl["rlMedianFrames"] <= 0.0:
        fails.append(f"reaction lab stats invalid: min={d_rl['rlMinFrames']} max={d_rl['rlMaxFrames']} med={d_rl['rlMedianFrames']}")
    if d_tc["tcEscapes"] < 1 or d_tc["tcLastTool"] == 0:
        fails.append(f"techchase escape expected >=1 escape with tool != 0, got esc={d_tc['tcEscapes']} tool={d_tc['tcLastTool']}")
    return fails


# ---- Scenario 4: Recovery Sweetspot + Floorhug Challenge --------------------

def sc_recovery_floorhug(emu, notes):
    fails = []

    # 4A. Recovery Sweetspot: Fox jumps backwards off FD right edge (facing left
    # toward the ledge), falls below the lip, and double-jumps left to sweetspot.
    _, _, p1_ptr, _, tm4x = start_match(emu, "fox", "fox")
    walk_p1_to(emu, p1_ptr, 80.0, face_dir=-1.0)
    emu.h.call("pause")
    step(emu, 2)
    # Short-hop right while facing left so Fox goes offstage facing the right ledge.
    emu.hold(0, ["X"], stick=RIGHT)
    step(emu, 3)
    emu.hold(0, stick=RIGHT)
    for _ in range(45):
        sf = read_fighter(emu, p1_ptr)
        if sf["air"] and sf["x"] > 96.0:
            break
        step(emu, 1)
    emu.neutral(0)
    for _ in range(45):
        sf = read_fighter(emu, p1_ptr)
        if sf["y"] < -24.0:
            break
        step(emu, 1)
    emu.hold(0, ["X"], stick=LEFT)
    step(emu, 4)
    emu.hold(0, stick=LEFT)
    for _ in range(65):
        sf = read_fighter(emu, p1_ptr)
        if sf["status"] == 0x75 or (not sf["air"] and sf["status"] not in (0x73, 0x74)):
            break
        step(emu, 1)
    emu.neutral(0)
    step(emu, 6)
    d_rec = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")

    # 4B. Floorhug / ASDI-Down Tech Challenge: Mario C-Stick Down vs Fox Up-Tilt.
    _, settings, p1_ptr, cpu_ptr, tm4x = start_match(emu, "mario", "fox")
    cpu_x = read_fighter(emu, cpu_ptr)["x"]
    walk_p1_to(emu, p1_ptr, cpu_x - 10.5, face_dir=1.0)
    inp = PI.PluginInput(emu)
    emu.h.call("pause")
    emu.h.call("write_mem", addr=settings + S_PLAYER_PERCENT, hex="0000")
    step(emu, 4)
    fox_utilt = PI.frames_to_steps({2: (PI.B_ATTACK, 0.0, 0.55)}, 3)

    def track_fh(_idx=None):
        sf = read_fighter(emu, p1_ptr)
        if sf["hitlag"] > 0:
            emu.hold(0, stick=NEUTRAL, cstick=DOWN)

    try:
        inp.run({1: fox_utilt}, read=track_fh, extra=28)
    finally:
        inp.stop()
        emu.neutral(0)
    step(emu, 4)
    d_fh = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")

    notes.append(
        f"recovery: att={d_rec['recAttempts']} sweet={d_rec['recSweetspots']} "
        f"grade={d_rec['recLastGrade']} grabs={d_rec['recLastGrabs']} xlu={d_rec['recLastXlu']} "
        f"dy={d_rec['recLastHeightDelta']:.1f} | "
        f"floorhug: reps={d_fh['fhReps']} succ={d_fh['fhSuccesses']} streak={d_fh['fhStreak']}"
    )
    if d_rec["recAttempts"] < 1 or d_rec["recSweetspots"] < 1 or d_rec["recLastGrade"] != REC_GRADE_SWEETSPOT:
        fails.append(
            f"recovery sweetspot expected grade={REC_GRADE_SWEETSPOT} sweet>=1, "
            f"got att={d_rec['recAttempts']} sweet={d_rec['recSweetspots']} grade={d_rec['recLastGrade']} dy={d_rec['recLastHeightDelta']:.1f}"
        )
    if d_rec["recLastGrabs"] < 1 or d_rec["recLastXlu"] <= 0:
        fails.append(f"recovery sweetspot expected grabs>=1 xlu>0, got grabs={d_rec['recLastGrabs']} xlu={d_rec['recLastXlu']}")
    if d_fh["fhReps"] < 1 or d_fh["fhSuccesses"] < 1:
        fails.append(f"floorhug challenge expected reps>=1 successes>=1, got reps={d_fh['fhReps']} succ={d_fh['fhSuccesses']}")
    return fails


# ---- Scenario 5: Snake Infinite Recovery (Cypher + C4 Self-Hit Loop) --------

def sc_snake_recovery(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4x = start_match(emu, "snake", "fox")
    walk_p1_to(emu, p1_ptr, 80.0, face_dir=1.0)

    emu.h.call("pause")
    step(emu, 2)
    # Jump cleanly off the right edge of FD (walking stops at edge teeter) to x > 94.
    emu.hold(0, ["X"], stick=RIGHT)
    step(emu, 4)
    emu.hold(0, stick=(185, 128))
    for _ in range(40):
        sf = read_fighter(emu, p1_ptr)
        if sf["air"] and sf["x"] > 95.0:
            break
        step(emu, 1)
    emu.neutral(0)
    step(emu, 4)

    # 1st Cypher (SpecialHi 0x114 -> 0x117): sets LA-Bit[61] = 1 and LA-Float[52] = 1.
    emu.hold(0, ["B"], stick=UP)
    step(emu, 4)
    emu.neutral(0)
    step(emu, 22)
    d_c1 = read_tm4x(emu, tm4x, 0)

    # Cut Cypher with jump (X), wait 4f, drop C4 in air (0x11D), wait until 0x11D
    # finishes into Fall (0x0E), then press B+DOWN to enter C4 Detonate (0x119).
    emu.hold(0, ["X"], stick=NEUTRAL)
    step(emu, 4)
    emu.neutral(0)
    step(emu, 4)
    emu.hold(0, ["B"], stick=DOWN)
    step(emu, 4)
    emu.neutral(0)
    # Wait until Snake enters 0x11D and then exits 0x11D so detonate input is accepted.
    saw_drop = False
    for _ in range(40):
        sf = read_fighter(emu, p1_ptr)
        if sf["status"] == 0x11D:
            saw_drop = True
        elif saw_drop and sf["status"] != 0x11D:
            break
        step(emu, 1)
    emu.hold(0, ["B"], stick=DOWN)
    step(emu, 4)
    emu.neutral(0)

    # Wait up to 55 frames for C4 detonate self-hit clearing LA-Bit[61].
    refreshed = False
    snk_trace = []
    for f_i in range(55):
        d_mid = read_tm4x(emu, tm4x, 0)
        sf = read_fighter(emu, p1_ptr)
        if f_i % 5 == 0:
            snk_trace.append(f"{sf['status']:#x}@({sf['x']:.0f},{sf['y']:.0f}):l{d_mid['snkCypherLockout']}")
        if d_mid["snkRefreshCount"] >= 1:
            refreshed = True
            break
        step(emu, 1)
    d_ref = read_tm4x(emu, tm4x, 0)
    if not refreshed:
        notes.append("snake c4_trace: " + " ".join(snk_trace))

    # Wait for self-hit hitstun to end, then 2nd Cypher (LA-Float[52] -> 2) back toward ledge!
    for _ in range(45):
        sf = read_fighter(emu, p1_ptr)
        if sf["status"] not in (0x44, 0x45, 0x46):
            break
        step(emu, 1)
    emu.hold(0, ["B"], stick=UP)
    step(emu, 5)
    emu.hold(0, stick=LEFT)
    for _ in range(80):
        sf = read_fighter(emu, p1_ptr)
        if sf["status"] in (0x73, 0x74, 0x75) or not sf["air"]:
            break
        step(emu, 1)
    emu.neutral(0)
    step(emu, 8)
    d_end = read_tm4x(emu, tm4x, 0)
    emu.h.call("resume")

    notes.append(
        f"snake-recovery: c1(cyph={d_c1['snkTotalCyphers']}, stk={d_c1['snkCypherStacks']}, lock={d_c1['snkCypherLockout']}, off={d_c1['snkOffstage']}) "
        f"ref(cnt={d_ref['snkRefreshCount']}, chain={d_ref['snkLoopChain']}, kind={d_ref['snkLastSelfHitKind']}, dmg={d_ref['snkLastSelfHitDamage']:.1f}) "
        f"end(cyph={d_end['snkTotalCyphers']}, stk={d_end['snkCypherStacks']}, comp={d_end['snkCompletedLoops']}, best={d_end['snkBestLoopChain']}, alt={d_end['snkAltitudeRecovered']:.1f})"
    )
    if d_c1["snkTotalCyphers"] < 1 or d_c1["snkCypherStacks"] < 1 or d_c1["snkCypherLockout"] != 1:
        fails.append(f"snake 1st cypher not tracked: cyph={d_c1['snkTotalCyphers']} stk={d_c1['snkCypherStacks']} lock={d_c1['snkCypherLockout']}")
    if not refreshed or d_ref["snkRefreshCount"] < 1 or d_ref["snkLastSelfHitKind"] != SNAKE_SELFHIT_C4:
        fails.append(f"snake C4 self-hit refresh failed: cnt={d_ref['snkRefreshCount']} kind={d_ref['snkLastSelfHitKind']} dmg={d_ref['snkLastSelfHitDamage']:.1f}")
    if d_end["snkTotalCyphers"] < 2 or d_end["snkBestLoopChain"] < 1:
        fails.append(f"snake 2nd cypher loop not tracked: cyph={d_end['snkTotalCyphers']} best={d_end['snkBestLoopChain']}")
    return fails


# ---- Scenario 6: Savestate Reset + Clean Unload -----------------------------

def sc_savestate_reset(emu, notes):
    fails = []
    _, _, p1_ptr, _, tm4x = start_match(emu, "fox", "fox")
    walk_p1_to(emu, p1_ptr, -30.0, face_dir=1.0)

    # Save lab quick savestate via real controller (hold DRIGHT).
    emu.press(0, ["DRIGHT"], frames=25, after=15)
    d_saved = read_tm4x(emu, tm4x, 0)

    # Start a shine (active transient watch) then load quick savestate via DLEFT.
    emu.h.call("pause")
    emu.hold(0, ["B"], stick=DOWN)
    step(emu, 5)
    d_dirty = read_tm4x(emu, tm4x, 0)
    emu.neutral(0)
    emu.h.call("resume")

    emu.press(0, ["DLEFT"], frames=15, after=20)
    d_after = read_tm4x(emu, tm4x, 0)

    notes.append(
        f"savestate-reset: saved(rst={d_saved['resetCount']}) "
        f"dirty(rst={d_dirty['resetCount']}, csAct={d_dirty['csActive']}) "
        f"after(rst={d_after['resetCount']}, csAct={d_after['csActive']})"
    )
    if d_after["resetCount"] <= d_saved["resetCount"]:
        fails.append(f"expected resetCount increment on savestate load: {d_saved['resetCount']} -> {d_after['resetCount']}")
    if d_after["csActive"] != 0:
        fails.append(f"expected transient csActive=0 after savestate load, got {d_after['csActive']}")
    return fails


SCENARIO_FNS = {
    "cancel-sprint": sc_cancel_sprint,
    "flow-course": sc_flow_course,
    "reaction-techchase": sc_reaction_techchase,
    "recovery-floorhug": sc_recovery_floorhug,
    "snake-recovery": sc_snake_recovery,
    "savestate-reset": sc_savestate_reset,
}


def cmd_test_stage4c(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage4c")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    label = "test-stage4c"
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
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError, PI.InputError, Stage4cError, ValueError) as exc:
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

    print("\n--- stage4c measurements ---")
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
