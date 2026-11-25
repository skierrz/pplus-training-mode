"""Emulator tests for the Hover/Float-Cancel, DJC, and Powershield backends:
  - Hover / Float-Cancel & DJC (src/extra/drill_float_djc_core.h)
  - P+ Powershield & 5-frame PS-Drop (src/extra/drill_powershield_core.h)
  - Live runtime adapter + RAM debug block "TM4P" (src/extra/drill_hover_ps_engines.*)

  test-stage4a-hover-ps REL [--only NAME ...] [--list] [--keep-going]

Final Destination only. Verifies live detection through controller input
and RAM debug block "TM4P" across:
  1. peach-fc: Peach Float Cancel success + actOoFcFrame and dropped-float miss
  2. mewtwo-hc: Mewtwo Hover Cancel success + actOoHcFrame and dropped-hover miss
  3. lucas-djc: Lucas instant Double-Jump Cancel (djcFrame, vy drop, landing)
  4. ps-physical-drop: Physical PS (Frame 1-2) + 5f PS-Drop punish + Early/Late miss
  5. ps-reflect-lth: Projectile Reflect PS (Frame 1-2) + Frame-2 light-to-hard
     Frame-3 reflect + lab savestate transient reset + clean unload
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
    "lucas": (0x1B, 0x19),
    "mewtwo": (0x33, 0x2E),
}

RIGHT, LEFT, UP, DOWN, NEUTRAL = (228, 128), (28, 128), (128, 228), (128, 28), (128, 128)

TM4P_MAGIC = 0x544D3450  # "TM4P"
TM4P_LOG_RE = re.compile(r"hover_ps: debug block at ([0-9a-f]{8})")
SLOT_FMT = ">BBbBhhIIIIBBBBhhhhhBBiffIIIIBBBBhhifffIIIIIIIIIIIBBBBBBhhhi"
SLOT_SIZE = struct.calcsize(SLOT_FMT)
assert SLOT_SIZE == 152, f"unexpected TM4P slot size {SLOT_SIZE}"

SLOT_FIELDS = (
    "used", "partner", "playerNo", "ftKind", "curStatus", "curMotion",
    "fcAttempts", "fcSuccesses", "fcStreak", "fcBestStreak",
    "lastFcOutcome", "lastFcIsHover", "inFloat", "floatAerialActive",
    "lastFcDuration", "lastFcAerialFrame", "lastFcActFrame", "bestActFrame",
    "lastFcMotion", "lastFcLandingLCancel", "padFc", "lastFcFrame",
    "lastFcEntryY", "lastFcMinY",
    "djcAttempts", "djcInstant", "djcStreak", "djcBestStreak",
    "lastDjcGrade", "lastDjcLandOutcome", "lastDjcLandedClean", "lastDjcHadBit33",
    "lastDjcFrame", "lastDjcMotion", "lastDjcEventFrame",
    "lastDjcVyBefore", "lastDjcVyAfter", "lastDjcHeight",
    "psAttempts", "psPhysicalHits", "psReflectHits", "psRePsHits",
    "psLightToHardHits", "psEarlyMisses", "psLateMisses", "psBufferedMisses",
    "psDropCancels", "psStreak", "psBestStreak",
    "lastPsOutcome", "lastPsRePs", "lastPsLightToHard",
    "lastPsDropUsed", "lastPsDropChainedRePs", "guardArmed",
    "lastPsWindowFrame", "lastPsOffsetFrames", "lastPsDropActFrame",
    "lastPsEventFrame",
)
assert len(SLOT_FIELDS) == 59

# Outcome & grade constants (drill_float_djc_core.h, drill_powershield_core.h).
FC_NONE, FC_SUCCESS, FC_MISS_FULL_LAG = 0, 1, 2
DJC_NONE, DJC_INSTANT, DJC_LOW_RISING, DJC_LATE = 0, 1, 2, 3
PS_NONE, PS_PHYSICAL, PS_REFLECT, PS_MISS_BUFFERED, PS_MISS_EARLY, PS_MISS_LATE = 0, 1, 2, 3, 4, 5

ST_WAIT = 0x00
ST_JUMP = 0x0B
ST_JUMP_AERIAL = 0x0C
ST_FALL = 0x0E
ST_LANDING = 0x16
ST_LANDING_LIGHT = 0x17
ST_LANDING_ATTACK_AIR = 0x18
ST_GUARD_ON = 0x1A
ST_GUARD = 0x1B
ST_GUARD_OFF = 0x1C
ST_GUARD_DAMAGE = 0x1D
ST_ATTACK_AIR = 0x33
ST_DAMAGE_FIRST = 0x43
ST_DAMAGE_LAST = 0x46
ST_PEACH_FLOAT = 0x11E
ST_MEWTWO_HOVER = 0x120

SCENARIOS = [
    "peach-fc",
    "mewtwo-hc",
    "lucas-djc",
    "ps-physical-drop",
    "ps-reflect-lth",
]


class HoverPsError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def state_path(name):
    p = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states", f"s4a-hps-{name}.sav")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


class DebugBlock:
    """Reads extra::HoverPsDebugBlock ("TM4P") directly from game RAM."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        matches = TM4P_LOG_RE.findall(self.emu.synced_log_text())
        if not matches:
            raise HoverPsError("no hover_ps debug block line in log")
        self.addr = int(matches[-1], 16)
        hdr = self.header()
        if hdr["magic"] != TM4P_MAGIC or hdr["version"] != 1:
            raise HoverPsError(f"bad TM4P header at {self.addr:#x}: {hdr}")
        return hdr

    def header(self):
        raw = self.emu.read(self.addr, 16)
        magic, version, frame, reset_count = struct.unpack(">IIII", raw)
        return {"magic": magic, "version": version, "frame": frame, "resetCount": reset_count}

    def slot(self, index):
        raw = self.emu.read(self.addr + 16 + index * SLOT_SIZE, SLOT_SIZE)
        vals = struct.unpack(SLOT_FMT, raw)
        return dict(zip(SLOT_FIELDS, vals))

    def by_player(self, player_no=0):
        for i in range(8):
            s = self.slot(i)
            if s["used"] and not s["partner"] and s["playerNo"] == player_no:
                return s
        raise HoverPsError(f"no active TM4P slot for player {player_no}")


def read_fighter(emu, fighter_ptr):
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0xD0)
    motion = u32(mods, 0x08)
    posture = u32(mods, 0x0C)
    situation = u32(mods, 0x14)
    work = u32(mods, 0x64)
    status_mod = u32(mods, 0x70)

    p = emu.read(posture, 0x44)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    prev_y = f32(p, 0x1C)
    lr = f32(p, 0x40)
    st = emu.u32(status_mod + 0x34)
    mkind = s32(emu.read(motion + 0x58, 4), 0)
    air = emu.u32(situation + 0x2C) == 2

    la = emu.ptr(work + 0x20)
    ra = emu.ptr(work + 0x24)
    ra_ints_ptr = emu.u32(ra + 0x0C) if ra else 0
    ra_int_cnt = emu.u32(ra + 0x10) if ra else 0
    ra_flags_ptr = emu.u32(ra + 0x1C) if ra else 0
    ra_flag_cnt = emu.u32(ra + 0x20) if ra else 0
    la_flags_ptr = emu.u32(la + 0x1C) if la else 0
    la_flag_cnt = emu.u32(la + 0x20) if la else 0

    ra1 = s32(emu.read(ra_ints_ptr + 4, 4), 0) if ra_ints_ptr and ra_int_cnt > 1 else 0
    ra3 = s32(emu.read(ra_ints_ptr + 12, 4), 0) if ra_ints_ptr and ra_int_cnt > 3 else 0
    ra_bit10 = (emu.u32(ra_flags_ptr) >> 10) & 1 if ra_flags_ptr and ra_flag_cnt > 0 else 0
    la_bit96 = emu.u32(la_flags_ptr + 12) & 1 if la_flags_ptr and la_flag_cnt > 3 else 0
    la_bit97 = (emu.u32(la_flags_ptr + 12) >> 1) & 1 if la_flags_ptr and la_flag_cnt > 3 else 0

    return {
        "x": x, "y": y, "vy": y - prev_y, "lr": lr, "status": st, "mkind": mkind,
        "air": air, "posture": posture,
        "ra1": ra1, "ra3": ra3, "raBit10": ra_bit10, "laBit96": la_bit96, "laBit97": la_bit97,
    }


def put_fighter(emu, posture, x, y):
    raw = struct.pack(">ff", float(x), float(y)).hex()
    emu.h.call("write_mem", addr=posture + 0x0C, hex=raw)
    emu.h.call("write_mem", addr=posture + 0x18, hex=raw)


def start_match(emu, p1_name, cpu_name="fox"):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"stage4a-hover-ps: {p1_name} vs {cpu_name}")
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    emu.h.call("write_mem", addr=settings + 432, hex="00" * 32)
    CT.wait_game_frames(emu, 24)
    f = LT.Fighters(emu)
    p1 = f.by_player(0)
    cpu = f.by_player(1)
    if p1 is None or cpu is None:
        raise HoverPsError("fighters not found after match start")
    dbg = DebugBlock(emu)
    dbg.find()
    return lab, settings, p1, cpu, dbg


def wait_p1_wait(emu, p1_ptr, limit=120):
    emu.neutral(0)
    for _ in range(limit):
        s = read_fighter(emu, p1_ptr)
        if not s["air"] and s["status"] == ST_WAIT:
            return s
        emu.h.call("frame_advance", n=1)
    return read_fighter(emu, p1_ptr)


# ---- Scenario 1: Peach Float Cancel + Dropped-Float Miss --------------------

def sc_peach_fc(emu, notes):
    fails = []
    _, _, p1, _, dbg = start_match(emu, "peach", "fox")
    p1_ptr = p1["fighter"]

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    wait_p1_wait(emu, p1_ptr)

    # Subtest A: Ground-level Float Cancel (hold Down + Jump -> 0x11E low float ->
    # press A for float aerial -> release Jump to drop straight into Landing 0x16 -> Dash).
    emu.hold(0, ["X"], stick=DOWN)
    entered_float = False
    for _ in range(25):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if s["status"] == ST_PEACH_FLOAT:
            entered_float = True
            break
    # Hold float 2f, then trigger Float Nair (A while holding X), then release X to drop and land!
    emu.hold(0, ["X"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=2)
    emu.hold(0, ["X", "A"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=2)
    emu.neutral(0)

    fc_trace = []
    landed_fc = False
    for i in range(45):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        fc_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2)))
        if s["status"] in (ST_LANDING, ST_LANDING_LIGHT):
            landed_fc = True
            # Dash right immediately out of 4-frame FC landing lag!
            emu.hold(0, stick=RIGHT)
            emu.h.call("frame_advance", n=10)
            emu.neutral(0)
            break
    wait_p1_wait(emu, p1_ptr)
    slot_a = dbg.by_player(0)
    notes.append(
        f"peach FC success: entered={entered_float} landed={landed_fc} "
        f"attempts={slot_a['fcAttempts']} ok={slot_a['fcSuccesses']} outcome={slot_a['lastFcOutcome']} "
        f"isHover={slot_a['lastFcIsHover']} mot={slot_a['lastFcMotion']:#x} "
        f"dur={slot_a['lastFcDuration']} airF={slot_a['lastFcAerialFrame']} "
        f"actF={slot_a['lastFcActFrame']} bestAct={slot_a['bestActFrame']} "
        f"entryY={slot_a['lastFcEntryY']:.2f} minY={slot_a['lastFcMinY']:.2f}"
    )
    if not entered_float or not landed_fc:
        fails.append(f"peach did not complete FC landing: entered={entered_float} landed={landed_fc} trace={fc_trace[:15]}")
    if slot_a["fcSuccesses"] < 1 or slot_a["lastFcOutcome"] != FC_SUCCESS or slot_a["lastFcIsHover"] != 0:
        fails.append(f"peach FC TM4P mismatch: {slot_a}")
    if slot_a["lastFcActFrame"] < 1 or slot_a["bestActFrame"] < 1:
        fails.append(f"peach actOoFcFrame not recorded: actF={slot_a['lastFcActFrame']} best={slot_a['bestActFrame']}")

    # Subtest B: Dropped-float early miss -> Fall (0x0E) -> Attack_Air (0x33) -> Landing_Attack_Air (0x18).
    # Full hop, enter float, release float into Fall, then press Nair (0x62, landing lag starts frame 4)
    # and fastfall so Peach lands during Attack_Air landing-lag frames into 0x18!
    emu.hold(0, ["X"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=14)
    emu.hold(0, ["X"], stick=DOWN)
    entered2 = False
    for _ in range(20):
        emu.h.call("frame_advance", n=1)
        if read_fighter(emu, p1_ptr)["status"] == ST_PEACH_FLOAT:
            entered2 = True
            break
    emu.neutral(0)
    fell = False
    for _ in range(20):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if s["status"] == ST_FALL:
            fell = True
            break
    # Start Nair (A + neutral stick) within dropped-float window, wait 4f so landing lag bit turns on,
    # then fastfall to land cleanly in Landing_Attack_Air (0x18)!
    emu.hold(0, ["A"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=2)
    emu.neutral(0)
    emu.h.call("frame_advance", n=4)
    emu.hold(0, stick=DOWN)
    miss_trace = []
    saw_18 = False
    for _ in range(45):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        miss_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2)))
        if s["status"] == ST_LANDING_ATTACK_AIR:
            saw_18 = True
            break
        if not s["air"] and s["status"] == ST_WAIT:
            break
    emu.neutral(0)
    emu.h.call("frame_advance", n=4)
    wait_p1_wait(emu, p1_ptr)
    slot_b = dbg.by_player(0)
    notes.append(
        f"peach FC miss: entered2={entered2} fell={fell} saw18={saw_18} "
        f"attempts={slot_b['fcAttempts']} ok={slot_b['fcSuccesses']} streak={slot_b['fcStreak']} "
        f"outcome={slot_b['lastFcOutcome']} mot={slot_b['lastFcMotion']:#x}"
    )
    if not saw_18:
        fails.append(f"peach dropped-float aerial did not land in 0x18: trace={miss_trace[:20]}")
    if slot_b["fcAttempts"] != slot_a["fcAttempts"] + 1 or slot_b["lastFcOutcome"] != FC_MISS_FULL_LAG:
        fails.append(f"peach FC miss TM4P mismatch: before={slot_a['fcAttempts']} after={slot_b}")
    if slot_b["fcStreak"] != 0:
        fails.append(f"peach FC streak did not reset on miss: {slot_b['fcStreak']}")

    emu.h.call("resume")
    return fails


# ---- Scenario 2: Mewtwo Hover Cancel + Dropped-Hover Miss -------------------

def sc_mewtwo_hc(emu, notes):
    fails = []
    _, _, p1, _, dbg = start_match(emu, "mewtwo", "fox")
    p1_ptr = p1["fighter"]

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    wait_p1_wait(emu, p1_ptr)

    # Subtest A: Mewtwo Hover Cancel (Jump + Down+X into 0x120 near ground -> A for hover aerial ->
    # release X to drop into Landing 0x16 -> Dash right).
    emu.hold(0, ["X"], stick=DOWN)
    entered_hover = False
    for _ in range(25):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if s["status"] == ST_MEWTWO_HOVER:
            entered_hover = True
            break
    # Hover downward slightly closer to floor, press A for hover aerial inside 0x120, release X to land!
    emu.hold(0, ["X"], stick=DOWN)
    emu.h.call("frame_advance", n=3)
    emu.hold(0, ["X", "A"], stick=DOWN)
    emu.h.call("frame_advance", n=2)
    emu.neutral(0)

    hc_trace = []
    landed_hc = False
    for _ in range(50):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        hc_trace.append((hex(s["status"]), hex(s["mkind"]), s["laBit97"], round(s["y"], 2)))
        if s["status"] in (ST_LANDING, ST_LANDING_LIGHT):
            landed_hc = True
            emu.hold(0, stick=RIGHT)
            emu.h.call("frame_advance", n=10)
            emu.neutral(0)
            break
    wait_p1_wait(emu, p1_ptr)
    slot_a = dbg.by_player(0)
    notes.append(
        f"mewtwo HC success: entered={entered_hover} landed={landed_hc} "
        f"attempts={slot_a['fcAttempts']} ok={slot_a['fcSuccesses']} outcome={slot_a['lastFcOutcome']} "
        f"isHover={slot_a['lastFcIsHover']} mot={slot_a['lastFcMotion']:#x} "
        f"dur={slot_a['lastFcDuration']} airF={slot_a['lastFcAerialFrame']} "
        f"actF={slot_a['lastFcActFrame']} entryY={slot_a['lastFcEntryY']:.2f}"
    )
    if not entered_hover or not landed_hc:
        fails.append(f"mewtwo did not complete HC landing: entered={entered_hover} landed={landed_hc} trace={hc_trace[:18]}")
    if slot_a["fcSuccesses"] < 1 or slot_a["lastFcOutcome"] != FC_SUCCESS or slot_a["lastFcIsHover"] != 1:
        fails.append(f"mewtwo HC TM4P mismatch: {slot_a}")
    if slot_a["lastFcActFrame"] < 1:
        fails.append(f"mewtwo actOoHcFrame not recorded: {slot_a['lastFcActFrame']}")

    # Subtest B: Dropped-hover early miss -> 0x120 -> 0x11E/0x0E -> 0x33 -> 0x18!
    emu.hold(0, ["X"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=10)
    emu.hold(0, ["X"], stick=DOWN)
    entered2 = False
    for _ in range(25):
        emu.h.call("frame_advance", n=1)
        if read_fighter(emu, p1_ptr)["status"] == ST_MEWTWO_HOVER:
            entered2 = True
            break
    emu.neutral(0)
    exited = False
    for _ in range(20):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if s["status"] != ST_MEWTWO_HOVER:
            exited = True
            break
    # Wait briefly until Fall (0x0E) if in HoverExit (0x11E), then press A and fastfall into 0x18.
    for _ in range(10):
        s = read_fighter(emu, p1_ptr)
        if s["status"] == ST_FALL:
            break
        emu.h.call("frame_advance", n=1)
    emu.hold(0, ["A"], stick=RIGHT)
    emu.h.call("frame_advance", n=2)
    emu.hold(0, stick=DOWN)
    saw_18 = False
    miss_trace = []
    for _ in range(55):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        miss_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2)))
        if s["status"] == ST_LANDING_ATTACK_AIR:
            saw_18 = True
            break
        if not s["air"] and s["status"] == ST_WAIT:
            break
    emu.neutral(0)
    emu.h.call("frame_advance", n=4)
    wait_p1_wait(emu, p1_ptr)
    slot_b = dbg.by_player(0)
    notes.append(
        f"mewtwo HC miss: entered2={entered2} exited={exited} saw18={saw_18} "
        f"attempts={slot_b['fcAttempts']} ok={slot_b['fcSuccesses']} outcome={slot_b['lastFcOutcome']}"
    )
    if not saw_18:
        fails.append(f"mewtwo dropped-hover aerial did not land in 0x18: trace={miss_trace[:20]}")
    if slot_b["fcAttempts"] != slot_a["fcAttempts"] + 1 or slot_b["lastFcOutcome"] != FC_MISS_FULL_LAG:
        fails.append(f"mewtwo HC miss TM4P mismatch: {slot_b}")

    emu.h.call("resume")
    return fails


# ---- Scenario 3: Lucas Instant Double-Jump Cancel (DJC) ---------------------

def sc_lucas_djc(emu, notes):
    fails = []
    _, _, p1, _, dbg = start_match(emu, "lucas", "fox")
    p1_ptr = p1["fighter"]

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    wait_p1_wait(emu, p1_ptr)

    # Short-hop with X, release to enter Jump (0x0B), wait a few frames, then
    # press X to enter Jump_Aerial (0x0C) and press A right away for instant DJC!
    emu.hold(0, ["X"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=2)
    emu.neutral(0)
    for _ in range(15):
        emu.h.call("frame_advance", n=1)
        if read_fighter(emu, p1_ptr)["status"] == ST_JUMP:
            break
    emu.h.call("frame_advance", n=4)

    # Press X for 1 frame to trigger Jump_Aerial (0x0C), then immediately press A on the very next
    # frame (accounting for 1-frame pad pipeline) so Attack_Air enters on Jump_Aerial frame 2 (instant DJC)!
    djc_trace = []
    emu.hold(0, ["X"], stick=NEUTRAL)
    emu.h.call("frame_advance", n=1)
    s = read_fighter(emu, p1_ptr)
    djc_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2), round(s["vy"], 3)))
    emu.hold(0, ["A"], stick=RIGHT)
    saw_dj = s["status"] == ST_JUMP_AERIAL
    for _ in range(2):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        djc_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2), round(s["vy"], 3)))
        if s["status"] == ST_JUMP_AERIAL:
            saw_dj = True
    emu.neutral(0)
    for _ in range(45):
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if s["status"] == ST_JUMP_AERIAL:
            saw_dj = True
        if len(djc_trace) < 16:
            djc_trace.append((hex(s["status"]), hex(s["mkind"]), round(s["y"], 2), round(s["vy"], 3)))
        if not s["air"] and s["status"] in (ST_WAIT, ST_LANDING, ST_LANDING_ATTACK_AIR):
            emu.h.call("frame_advance", n=2)
            break

    slot = dbg.by_player(0)
    notes.append(
        f"lucas DJC: saw_dj={saw_dj} attempts={slot['djcAttempts']} instant={slot['djcInstant']} "
        f"grade={slot['lastDjcGrade']} frame={slot['lastDjcFrame']} bit33={slot['lastDjcHadBit33']} "
        f"mot={slot['lastDjcMotion']:#x} vy={slot['lastDjcVyBefore']:.3f}->{slot['lastDjcVyAfter']:.3f} "
        f"h={slot['lastDjcHeight']:.2f} landOut={slot['lastDjcLandOutcome']} clean={slot['lastDjcLandedClean']} "
        f"trace={djc_trace[:10]}"
    )
    if not saw_dj:
        fails.append(f"lucas never entered Jump_Aerial 0x0C: trace={djc_trace}")
    if slot["djcAttempts"] < 1 or slot["djcInstant"] < 1 or slot["lastDjcGrade"] != DJC_INSTANT:
        fails.append(f"lucas instant DJC not detected: {slot}")
    if slot["lastDjcFrame"] not in (1, 2):
        fails.append(f"lucas instant DJC frame expected 1 or 2, got {slot['lastDjcFrame']}")
    if slot["lastDjcMotion"] < 0x62 or slot["lastDjcMotion"] > 0x66:
        fails.append(f"lucas DJC aerial motion unexpected: {slot['lastDjcMotion']:#x}")

    emu.h.call("resume")
    return fails


# ---- Scenario 4: Physical Powershield + PS-Drop Punish + Early/Late Miss ----

def sc_ps_physical_drop(emu, notes):
    fails = []
    _, _, p1, cpu, dbg = start_match(emu, "mario", "fox")
    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    wait_p1_wait(emu, p1_ptr)

    # Place Fox right next to Mario facing Mario, let both settle in Wait, then save state!
    p1_s = read_fighter(emu, p1_ptr)
    cpu_s = read_fighter(emu, cpu_ptr)
    put_fighter(emu, p1_s["posture"], -5.0, 1.0)
    put_fighter(emu, cpu_s["posture"], 6.0, 1.0)
    emu.h.call("frame_advance", n=8)
    # Turn Fox left toward Mario if needed.
    inp = PI.PluginInput(emu)
    if read_fighter(emu, cpu_ptr)["lr"] > 0:
        inp.run({1: PI.frames_to_steps({1: (0, -1.0, 0.0)}, 4)}, extra=8)
    wait_p1_wait(emu, p1_ptr)

    sav = state_path("ps-phys")
    emu.h.call("save_state", path=sav)

    # Fox Jab on driven frame 10 (gives plenty of room to shield early or late!).
    fox_jab = PI.frames_to_steps({10: (PI.B_ATTACK, 0.0, 0.0), 11: (PI.B_ATTACK, 0.0, 0.0)}, 16)

    # Baseline unshielded probe: find exact driven frame when Fox's Jab hits Mario.
    hit_step = None

    def probe_hit(i):
        nonlocal hit_step
        s = read_fighter(emu, p1_ptr)
        if hit_step is None and ST_DAMAGE_FIRST <= s["status"] <= ST_DAMAGE_LAST:
            hit_step = i

    inp.run({1: fox_jab}, read=probe_hit, extra=12)
    notes.append(f"ps-physical baseline: fox jab unshielded hit_step={hit_step}")
    if hit_step is None or hit_step < 6:
        emu.h.call("resume")
        return [f"Fox Jab did not hit Mario cleanly (hit_step={hit_step})"]

    # Subtest A: Physical PS on Frame 1-2 + PS-Drop cancel into Jab (0x24)!
    emu.h.call("load_state", path=sav)
    emu.h.call("pause")
    emu.neutral(0)

    # Remember harness pad has 1-frame pipeline delay, so sending L at driven frame `hit_step - 1`
    # puts Mario into Guard_On on `hit_step` (Frame 1!) or `hit_step - 2` -> Frame 2!
    ps_hit_seen = False
    drop_fired = False
    phys_trace = []

    # Run Fox Jab step-by-step while controlling Mario's real controller pad!
    blk = inp.blocks[0]
    blk.play(1, fox_jab)
    for step_i in range(1, 36):
        s_pre = read_fighter(emu, p1_ptr)
        if not ps_hit_seen:
            if step_i == hit_step - 1:
                emu.hold(0, ["L"])
        else:
            if not drop_fired and s_pre["status"] == ST_GUARD_OFF:
                emu.hold(0, ["A"])
                drop_fired = True
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        phys_trace.append((step_i, hex(s["status"]), s["laBit96"], s["ra1"], s["ra3"]))
        if s["status"] == ST_GUARD_DAMAGE and s["laBit96"] == 1 and not ps_hit_seen:
            ps_hit_seen = True
            emu.neutral(0)  # Release L right after Guard_Damage starts so Mario enters Guard_Off at end of shieldstun!
    blk.stop()
    emu.neutral(0)
    emu.h.call("frame_advance", n=3)

    slot_a = dbg.by_player(0)
    notes.append(
        f"ps-physical + drop: ps_seen={ps_hit_seen} drop_fired={drop_fired} "
        f"out={slot_a['lastPsOutcome']} winF={slot_a['lastPsWindowFrame']} "
        f"phys={slot_a['psPhysicalHits']} dropUsed={slot_a['lastPsDropUsed']} "
        f"dropActF={slot_a['lastPsDropActFrame']} dropCancels={slot_a['psDropCancels']} "
        f"trace={phys_trace[max(0, hit_step-3):hit_step+14]}"
    )
    if slot_a["lastPsOutcome"] != PS_PHYSICAL or slot_a["psPhysicalHits"] < 1:
        fails.append(f"physical PS not detected: {slot_a}")
    if slot_a["lastPsWindowFrame"] not in (1, 2):
        fails.append(f"physical PS windowFrame expected 1..2, got {slot_a['lastPsWindowFrame']}")
    if slot_a["lastPsDropUsed"] != 1 or slot_a["psDropCancels"] < 1 or not (1 <= slot_a["lastPsDropActFrame"] <= 10):
        fails.append(f"PS-Drop cancel not detected: used={slot_a['lastPsDropUsed']} actF={slot_a['lastPsDropActFrame']}")

    # Subtest B: Early Miss (-Kf) - press L 4 frames before hit_step (Guard Frame 4 -> -2f early).
    emu.h.call("load_state", path=sav)
    emu.h.call("pause")
    emu.neutral(0)
    blk.play(1, fox_jab)
    for step_i in range(1, hit_step + 8):
        if step_i == hit_step - 4:
            emu.hold(0, ["L"])
        emu.h.call("frame_advance", n=1)
    blk.stop()
    emu.neutral(0)
    emu.h.call("frame_advance", n=2)
    slot_b = dbg.by_player(0)
    notes.append(
        f"ps-early miss: out={slot_b['lastPsOutcome']} winF={slot_b['lastPsWindowFrame']} "
        f"off={slot_b['lastPsOffsetFrames']} early={slot_b['psEarlyMisses']}"
    )
    if slot_b["lastPsOutcome"] != PS_MISS_EARLY or slot_b["lastPsOffsetFrames"] >= 0 or slot_b["psEarlyMisses"] < 1:
        fails.append(f"early PS miss not detected: {slot_b}")

    # Subtest C: Late Miss (+Kf) - leave unshielded on hit_step, press L 2 frames later (during hitlag!).
    emu.h.call("load_state", path=sav)
    emu.h.call("pause")
    emu.neutral(0)
    blk.play(1, fox_jab)
    for step_i in range(1, hit_step + 8):
        if step_i == hit_step + 1:
            emu.hold(0, ["L"])  # arrives on game frame hit_step + 2 -> +2f late!
        emu.h.call("frame_advance", n=1)
    blk.stop()
    emu.neutral(0)
    emu.h.call("frame_advance", n=2)
    slot_c = dbg.by_player(0)
    notes.append(
        f"ps-late miss: out={slot_c['lastPsOutcome']} off={slot_c['lastPsOffsetFrames']} "
        f"late={slot_c['psLateMisses']}"
    )
    if slot_c["lastPsOutcome"] != PS_MISS_LATE or slot_c["lastPsOffsetFrames"] <= 0 or slot_c["psLateMisses"] < 1:
        fails.append(f"late PS miss not detected: {slot_c}")

    emu.h.call("resume")
    return fails


# ---- Scenario 5: Projectile Reflect PS + Frame-2 Light-to-Hard + Savestate ---

def sc_ps_reflect_lth(emu, notes):
    fails = []
    _, _, p1, cpu, dbg = start_match(emu, "mario", "fox")
    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    wait_p1_wait(emu, p1_ptr)

    # Place Fox at mid-range facing Mario so Neutral-B Laser arrives cleanly inside
    # Mario's 0.76x reflect bubble (not grazing the outer 0.76..1.00x shield ring).
    p1_s = read_fighter(emu, p1_ptr)
    cpu_s = read_fighter(emu, cpu_ptr)
    put_fighter(emu, p1_s["posture"], -20.0, 1.0)
    put_fighter(emu, cpu_s["posture"], 17.5, 1.0)
    emu.h.call("frame_advance", n=8)
    inp = PI.PluginInput(emu)
    if read_fighter(emu, cpu_ptr)["lr"] > 0:
        inp.run({1: PI.frames_to_steps({1: (0, -1.0, 0.0)}, 4)}, extra=8)
    wait_p1_wait(emu, p1_ptr)

    sav = state_path("ps-laser")
    emu.h.call("save_state", path=sav)

    fox_laser = PI.frames_to_steps({1: (PI.B_SPECIAL, 0.0, 0.0), 2: (PI.B_SPECIAL, 0.0, 0.0)}, 6)

    # Measure exact step when laser hits unshielded Mario (damage increases!).
    p1_view = CT.FighterView(emu, 0)
    dmg0 = p1_view.damage()
    laser_step = None

    def probe_laser(i):
        nonlocal laser_step
        if laser_step is None and p1_view.damage() > dmg0 + 0.1:
            laser_step = i

    inp.run({1: fox_laser}, read=probe_laser, extra=26)
    notes.append(f"ps-reflect baseline: fox laser hit_step={laser_step}")
    if laser_step is None or laser_step < 5:
        emu.h.call("resume")
        return [f"Fox laser did not hit Mario cleanly (laser_step={laser_step})"]

    blk = inp.blocks[0]

    # Subtest A: Fresh Frame 1-2 Projectile Reflect PS!
    emu.h.call("load_state", path=sav)
    emu.h.call("pause")
    emu.neutral(0)
    blk.play(1, fox_laser)
    for step_i in range(1, laser_step + 5):
        if step_i == laser_step - 1:
            emu.hold(0, ["L"])  # arrives on laser_step -> Guard_On Frame 1 reflect!
        emu.h.call("frame_advance", n=1)
    blk.stop()
    emu.neutral(0)
    emu.h.call("frame_advance", n=2)
    slot_a = dbg.by_player(0)
    notes.append(
        f"ps-reflect fresh: out={slot_a['lastPsOutcome']} winF={slot_a['lastPsWindowFrame']} "
        f"lth={slot_a['lastPsLightToHard']} refl={slot_a['psReflectHits']}"
    )
    if slot_a["lastPsOutcome"] != PS_REFLECT or slot_a["psReflectHits"] < 1:
        fails.append(f"fresh reflect PS not detected: {slot_a}")
    if slot_a["lastPsWindowFrame"] not in (1, 2) or slot_a["lastPsLightToHard"] != 0:
        fails.append(f"fresh reflect PS window/lth unexpected: winF={slot_a['lastPsWindowFrame']} lth={slot_a['lastPsLightToHard']}")

    # Subtest B: Frame-2 Light-to-Hard press reflecting Fox Laser on Frame 3.
    # At Fox x=17.5, laser enters Mario's 0.76x reflect bubble on step laser_step - 1 (step 13).
    # Send analog light shield l=102 at laser_step - 4 (Guard Frame 1 at step 11, RA-Basic[1]=0),
    # then fresh digital ["L"] at laser_step - 3 (Guard Frame 2 at step 12, RA-Basic[1]=2), so the
    # laser reflects cleanly on Guard Frame 3 (step 13, RA-Basic[1]=1, RA-Bit[10]=1)!
    emu.h.call("load_state", path=sav)
    emu.h.call("pause")
    emu.neutral(0)
    blk.play(1, fox_laser)
    lth_trace = []
    for step_i in range(1, laser_step + 5):
        if step_i == laser_step - 4:
            emu.hold(0, buttons=(), l=102)
        elif step_i == laser_step - 3:
            emu.hold(0, buttons=["L"], l=255)
        emu.h.call("frame_advance", n=1)
        s = read_fighter(emu, p1_ptr)
        if step_i >= laser_step - 4:
            lth_trace.append((step_i, hex(s["status"]), s["ra1"], s["raBit10"]))
    blk.stop()
    emu.neutral(0)
    emu.h.call("frame_advance", n=2)
    slot_b = dbg.by_player(0)
    notes.append(
        f"ps-reflect light-to-hard: out={slot_b['lastPsOutcome']} winF={slot_b['lastPsWindowFrame']} "
        f"lth={slot_b['lastPsLightToHard']} lthHits={slot_b['psLightToHardHits']} "
        f"refl={slot_b['psReflectHits']} trace={lth_trace}"
    )
    if slot_b["lastPsOutcome"] != PS_REFLECT or slot_b["lastPsWindowFrame"] != 3:
        fails.append(f"light-to-hard Frame-3 reflect not detected: out={slot_b['lastPsOutcome']} winF={slot_b['lastPsWindowFrame']}")
    if slot_b["lastPsLightToHard"] != 1 or slot_b["psLightToHardHits"] < 1:
        fails.append(f"light-to-hard flag/stat not set: lth={slot_b['lastPsLightToHard']} hits={slot_b['psLightToHardHits']}")

    # Subtest C: Lab savestate load transient reset (wait for Mario Wait -> D-pad Right save ->
    # hold L until guardArmed==1 -> D-pad Left load -> verify resetCount incremented and guardArmed cleared!).
    wait_p1_wait(emu, p1_ptr)
    emu.h.call("resume")
    emu.neutral(0)
    CT.wait_game_frames(emu, 4)
    hdr_before = dbg.header()
    refl_before = dbg.by_player(0)["psReflectHits"]
    CT.dpad_save(emu)
    emu.h.call("pause")
    emu.hold(0, ["L"])
    armed_before = 0
    for _ in range(10):
        emu.h.call("frame_advance", n=1)
        armed_before = dbg.by_player(0)["guardArmed"]
        if armed_before == 1:
            break
    emu.neutral(0)
    emu.h.call("resume")
    CT.dpad_load(emu)
    hdr_after = dbg.header()
    slot_after = dbg.by_player(0)
    notes.append(
        f"savestate reset: resetCount={hdr_before['resetCount']}->{hdr_after['resetCount']} "
        f"guardArmed={armed_before}->{slot_after['guardArmed']} "
        f"reflPreserved={refl_before}->{slot_after['psReflectHits']}"
    )
    if armed_before != 1:
        fails.append(f"expected guardArmed==1 before lab savestate load, got {armed_before}")
    if hdr_after["resetCount"] <= hdr_before["resetCount"]:
        fails.append(f"lab savestate load did not increment TM4P resetCount: {hdr_before} -> {hdr_after}")
    if slot_after["guardArmed"] != 0:
        fails.append(f"lab savestate load did not clear transient guardArmed: {slot_after['guardArmed']}")
    if slot_after["psReflectHits"] != refl_before:
        fails.append(f"savestate transient reset unexpectedly altered psReflectHits: {refl_before} -> {slot_after['psReflectHits']}")

    return fails


SCENARIO_FNS = {
    "peach-fc": sc_peach_fc,
    "mewtwo-hc": sc_mewtwo_hc,
    "lucas-djc": sc_lucas_djc,
    "ps-physical-drop": sc_ps_physical_drop,
    "ps-reflect-lth": sc_ps_reflect_lth,
}


def cmd_test_stage4a_hover_ps(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage4a-hover-ps")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    label = "test-stage4a-hover-ps"
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
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError,
                        PI.InputError, HoverPsError, ValueError) as exc:
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

    print("\n--- stage4a-hover-ps measurements ---")
    for line in notes:
        print(" ", line)
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and failed:
        why = f"{len(failed)} scenario(s) failed: {', '.join(n for n, _ in failed)}"
        print("failed --only:", " ".join(n for n, _ in failed))
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
