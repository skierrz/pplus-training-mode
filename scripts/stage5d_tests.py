"""Emulator tests for Stage 5D (run through emu.py):
  - P+ 5-ledge-grab limit OSD & Ledgedash HUD (LA-Basic[79]) + Damage_Fly (0x45) probe
  - Can Walljump (23189) & Wall Cling (23190 + Knuckles 0x2D) Info Display
  - P+ Nana Throw (ai_nana 0x6100 override + Popo/Nana vs Sopo menu gating)

  test-stage5d REL [--only NAME ...] [--list] [--keep-going]
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
from harness import HarnessError, Timeout  # noqa: E402

CHARS = {
    "mario": (0x00, 0x00),
    "fox": (0x07, 0x07),
    "sheik": (0x0F, 0x0F),
    "ice_climbers": (0x10, 0x10),
    "lucario": (0x24, 0x20),
    "mewtwo": (0x33, 0x2E),
    "knuckles": (0x35, 0x30),
}

RIGHT, LEFT, UP, DOWN, NEUTRAL = (228, 128), (28, 128), (128, 228), (128, 28), (128, 128)

# Statuses & motions (BrawlHeaders ft/fighter.h).
ST_WAIT = 0x00
ST_FALL = 0x0E
ST_ESCAPE_AIR = 0x21
ST_CATCH = 0x34
ST_CATCH_PULL = 0x35
ST_CATCH_WAIT = 0x39
ST_CATCH_ATTACK = 0x3A
ST_THROW = 0x3C
ST_DAMAGE_AIR = 0x44
ST_DAMAGE_FLY = 0x45
ST_DAMAGE_FLY_ROLL = 0x46
ST_WALL_JUMP = 0x67
ST_ATTACH_WALL = 0x68
ST_ATTACH_WALL_WAIT = 0x69
ST_CLIFF_CATCH = 0x74
ST_CLIFF_WAIT = 0x75
ST_DEAD = 0xBD
ST_STANDBY = 0x10B

MOT_THROW_B = 0x72
MOT_THROW_F = 0x73
MOT_THROW_HI = 0x74
MOT_THROW_LW = 0x75

# main::Settings offsets (src/main/settings.h).
S_PLAYER_PERCENT = 8
S_PLAYER_LOCK = 10
S_LEDGE_HUD = 12
S_LEDGE_GALINT = 14
S_LEDGE_PANEL = 15
S_HMN_INFO_ROWS = 56 + 2
S_OSD_LEDGEDASH = 100
S_NANA_THROW = 454

# ui::InfoValue (src/main/ui/info_display.h).
IV_STATE = 2
IV_WALLJUMPS = 23
IV_CAN_WALLJUMP = 24
IV_LINES = 26
IV_WALL_CLING = 36

OSD_RE = re.compile(r"osd: P(\d) id (\d+): ([^\r\n]*)")
LEDGE_RE = re.compile(
    r"ledgedash: P(\d) galint (-?\d+) ledge (\d+) angle (-?[\d.]+) airdodge (\d) cells ([0-9]+) "
    r"no intangibility (\d) grabs (\d+)"
)
INFO_RE = re.compile(r"info P(\d): ([^\r\n]*)")
TRACE_BYTE_RE = re.compile(r"tracker: trace byte at ([0-9a-f]{8})")

ID_GALINT = 20
ID_CLIFFWAIT = 17

SCENARIOS = [
    "ledge-5-grab",
    "wall-info",
    "nana-throw",
]


class Stage5dError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def state_path(name):
    p = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states", f"stage5d-{name}.sav")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def read_fighter_full(emu, fighter_ptr):
    """Reads status, motion kind, x, y, lr, air, xluFrames, and selected LA ints directly."""
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.read(emu.ptr(acc + 0xD8), 0xD0)
    motion = u32(mods, 0x08)
    posture = u32(mods, 0x0C)
    situation = u32(mods, 0x14)
    hit = u32(mods, 0x20)
    work = u32(mods, 0x64)
    status_mod = u32(mods, 0x70)

    p = emu.read(posture, 0x44)
    x, y = f32(p, 0x0C), f32(p, 0x10)
    lr = f32(p, 0x40)
    st = emu.u32(status_mod + 0x34)
    mkind = s32(emu.read(motion + 0x58, 4), 0)
    air = emu.u32(situation + 0x2C) == 2

    arr = emu.u32(hit + 0x58)
    xlu = s32(emu.read(arr + 0x0C + 0x24, 4), 0) if emu.is_ram(arr) else 0

    la = emu.ptr(work + 0x20)
    la_ints_ptr = emu.u32(la + 0x0C)
    la_raw = emu.read(la_ints_ptr, 84 * 4)
    return {
        "x": x, "y": y, "lr": lr, "status": st, "mkind": mkind, "air": air, "xlu": xlu,
        "la2": s32(la_raw, 2 * 4),
        "la3": s32(la_raw, 3 * 4),
        "la4": s32(la_raw, 4 * 4),
        "la11": s32(la_raw, 11 * 4),
        "la12": s32(la_raw, 12 * 4),
        "la79": s32(la_raw, 79 * 4),
        "posture": posture,
    }


def put_fighter(emu, posture, x, y):
    raw = struct.pack(">ff", float(x), float(y)).hex()
    emu.h.call("write_mem", addr=posture + 0x0C, hex=raw)
    emu.h.call("write_mem", addr=posture + 0x18, hex=raw)


def find_trace_byte(emu):
    m = list(TRACE_BYTE_RE.finditer(emu.synced_log_text()))
    if not m:
        raise Stage5dError("tracker trace byte address not found in log")
    return int(m[-1].group(1), 16)


def start_match(emu, p1_name, cpu_name="fox"):
    emu.neutral(0)
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"stage5d: {p1_name} vs {cpu_name}")
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    # Clear CPU options, frame advance, FighterMechanics, and Info rows.
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    emu.h.call("write_mem", addr=settings + 432, hex="00" * 32)
    emu.h.call("write_mem", addr=settings + S_HMN_INFO_ROWS, hex="00" * 8)
    CT.wait_game_frames(emu, 24)
    f = LT.Fighters(emu)
    p1 = f.by_player(0)
    cpu = f.by_player(1)
    if p1 is None or cpu is None:
        raise Stage5dError("fighters not found after match start")
    return lab, settings, p1, cpu


def find_nana_fighter(emu, entry_id):
    arr = emu.ptr(LT.ENTRY_MANAGER)
    n = min(emu.u32(LT.ENTRY_MANAGER + 4), 9)
    for i in range(n):
        raw = emu.read(arr + i * LT.ENTRY_SIZE, 0x60)
        if s32(raw, 4) == entry_id:
            ptr = u32(raw, 0x3C)
            if emu.is_ram(ptr):
                return ptr
    return None


# ---- Scenario 1: P+ 5-Ledge-Grab Limit OSD/HUD + Damage_Fly (0x45) check ---

def sc_ledge_5_grab(emu, notes, shots, has_d3d):
    fails = []
    lab, settings, p1, cpu = start_match(emu, "mario", "fox")
    # Enable Ledgedash Info OSD + Ledgedash HUD + Panel + GALINT bar.
    emu.h.call("write_mem", addr=settings + S_OSD_LEDGEDASH, hex="01")
    emu.h.call("write_mem", addr=settings + S_LEDGE_HUD, hex="01")
    emu.h.call("write_mem", addr=settings + S_LEDGE_GALINT, hex="01")
    emu.h.call("write_mem", addr=settings + S_LEDGE_PANEL, hex="01")

    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)

    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]
    posture_p1 = read_fighter_full(emu, p1_ptr)["posture"]

    def step(n=1):
        emu.h.call("frame_advance", n=n)

    def grab_right_ledge_from_air(start_from_stage=False):
        """Gets Mario onto FD's right ledge without touching the stage floor (after initial launch)."""
        if start_from_stage:
            # Turn left so lr = -1 (facing the right ledge), jump, wait until airborne and falling,
            # then step clear around FD's right edge so Mario snaps to the ledge without landing.
            emu.hold(0, stick=LEFT)
            step(3)
            emu.hold(0, ["X"], stick=LEFT)
            step(3)
            emu.neutral(0)
            step(16)
            for wx, wy in ((100.0, 15.0), (100.0, -10.0), (92.0, -12.0)):
                put_fighter(emu, posture_p1, wx, wy)
                step(1)
        # Drift left toward FD's right ledge (x ~ 85.5, y ~ 0) until CliffWait (0x75).
        for i in range(90):
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
        raise Stage5dError(f"Mario never grabbed right ledge (last={read_fighter_full(emu, p1_ptr)})")

    grabs = []
    pos_before_6 = len(emu.log_text())
    for idx in range(1, 7):
        if idx == 6:
            pos_before_6 = len(emu.log_text())
        s = grab_right_ledge_from_air(start_from_stage=(idx == 1))
        emu.neutral(0)
        # Wait 1 extra frame so tracker/techniques finishes processing CliffWait frame 1.
        step(1)
        s1 = read_fighter_full(emu, p1_ptr)
        grabs.append((idx, s["la79"], s["xlu"], s1["xlu"]))
        notes.append(f"ledge grab #{idx}: LA79={s['la79']} xlu_f0={s['xlu']} xlu_f1={s1['xlu']}")
        if idx < 6:
            # Drop off ledge cleanly (wait until CliffWait frame >= 3, press Down), wait out 29f lockout,
            # then double-jump back to the ledge without ever touching the stage floor.
            step(3)
            emu.hold(0, stick=DOWN)
            for _ in range(12):
                step(1)
                if read_fighter_full(emu, p1_ptr)["status"] != ST_CLIFF_WAIT:
                    break
            emu.neutral(0)
            # Fall slightly offstage right until cliff-regrab lockout LA-Basic[2] == 0.
            for k in range(45):
                cur = read_fighter_full(emu, p1_ptr)
                if cur["la2"] == 0 and k >= 30:
                    break
                if cur["y"] < -35.0:
                    put_fighter(emu, posture_p1, 95.0, -12.0)
                emu.hold(0, stick=(160, 128))
                step(1)
            # Fall/drift back onto right ledge without jumping above y=0.
            put_fighter(emu, posture_p1, 92.0, -10.0)
            emu.hold(0, stick=LEFT)
            step(1)

    # Check grabs 1..5 had XLU > 0 and LA-Basic[79] == 1..5; grab 6 had LA-Basic[79] == 6 and XLU == 0.
    for idx, la79, xlu0, _ in grabs:
        if la79 != idx:
            fails.append(f"grab #{idx}: expected LA-Basic[79]={idx}, got {la79}")
        if idx <= 5 and xlu0 <= 0:
            fails.append(f"grab #{idx}: expected XLU > 0, got {xlu0}")
        if idx == 6 and xlu0 != 0:
            fails.append(f"grab #6: expected XLU == 0, got {xlu0}")

    log6 = emu.log_text()[pos_before_6:]
    osds6 = [(int(p), int(wid), txt) for p, wid, txt in OSD_RE.findall(log6)]
    notes.append(f"grab #6 immediate OSDs: {osds6}")
    if not any(p == 1 and wid == ID_GALINT and txt == "Ledgegrab Limit | Grab 6/5" for p, wid, txt in osds6):
        fails.append(f"grab #6 missing immediate Ledgegrab Limit OSD; got {osds6}")

    # Now ledgedash onto FD from grab #6!
    pos_ld = len(emu.log_text())
    step(3)
    emu.hold(0, stick=DOWN)
    for _ in range(10):
        step(1)
        if read_fighter_full(emu, p1_ptr)["status"] == ST_FALL:
            break
    emu.hold(0, ["X"], stick=LEFT)
    step(3)
    emu.hold(0, stick=LEFT)
    # Wait until Mario rises above FD's lip (y > 1.0) before airdodging down-left onto the stage.
    for _ in range(20):
        step(1)
        if read_fighter_full(emu, p1_ptr)["y"] > 1.0:
            break
    emu.hold(0, ["R"], stick=(40, 50))
    step(3)
    emu.neutral(0)
    landed = False
    for _ in range(50):
        step(1)
        cur = read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == ST_WAIT:
            landed = True
            break
    step(4)
    log_ld = emu.log_text()[pos_ld:]
    ld_matches = LEDGE_RE.findall(log_ld)
    osds_ld = [(int(p), int(wid), txt) for p, wid, txt in OSD_RE.findall(log_ld)]
    post_land = read_fighter_full(emu, p1_ptr)
    notes.append(f"grab #6 ledgedash: landed={landed} post_LA79={post_land['la79']} log={ld_matches} osds={osds_ld}")

    if not landed:
        fails.append("grab #6 ledgedash did not land onstage")
    if not ld_matches or int(ld_matches[0][6]) != 1 or int(ld_matches[0][7]) != 6:
        fails.append(f"grab #6 ledgedash log mismatch: {ld_matches}")
    if not any(p == 1 and wid == ID_GALINT and txt == "GALINT | None (Grab 6/5)" for p, wid, txt in osds_ld):
        fails.append(f"grab #6 GALINT OSD mismatch: {osds_ld}")
    if post_land["la79"] != 0:
        fails.append(f"landing onstage did not reset LA-Basic[79]: got {post_land['la79']}")

    if has_d3d:
        emu.h.call("resume")
        shots.append(emu.screenshot("stage5d-ledge-limit-hud"))
        emu.h.call("pause")

    # ---- Mandatory live check: RSBE01.TXT:1052 Damage_Fly (0x45) offstage reset test ----
    # Place Fox at right edge facing right, set Mario to 120%, have Mario grab right ledge TWICE
    # (LA-Basic[79] == 2), wait for ledge invuln to expire, then have Fox F-Smash Mario off ledge
    # into Damage_Fly (0x45) using Lab's CPU Control? Actually we have port 2 plugged in via
    # Training's Control option OR Lab's CPU Options! Let's use Training's CPU Control? No,
    # even simpler: switch Fox to port 2 via Training's pause menu OR just write IfMinigameTraining
    # OR let's set Mario to 120%, grab ledge twice, hang until XLU == 0, then put CPU Behavior =
    # Attack (or use Lab's CPU Smash / Counter? No: port 2 works immediately if we set Training
    # CPU = Control via Training pause menu, OR even simpler: use `nav.set_training_cpu` if it exists,
    # or just set Fox CPU behavior via lab's CPU Options? Wait: let's check if Fox attacks Mario when
    # Fox is set to CPU Behavior = Attack (Training) or Counter!).
    # Wait: even cleaner and 100% deterministic: set P1 Mario to 120% (via S_PLAYER_PERCENT),
    # set CPU Fox to Lab Control + Neutral = F-Smash? No, Lab CPU Options has Behavior = Stand/Crouch/
    # Jump/Shield/Walk/Dash/Attack (or we can use PluginInput!).
    # Look at scripts/plugin_input.py! Actually we can set Training's CPU = Control via Training's pause
    # menu in 2 seconds, OR set `g_stOperatorInfoTraining` -> actually let's just use `LT.Lab`?
    # Better yet: Mario is P1 (port 1) and Fox is CPU (port 2 in Training Control)! Wait: in sc_ledge_5_grab,
    # we can do the Damage_Fly test with MARIO hitting FOX off the ledge instead!
    # THINK ABOUT THAT!
    # Wait: if Fox (CPU) is controlled by port 2 (or knocked off ledge), OR Mario hits Fox offstage?
    # Wait: if we have Mario at right edge and Fox grabs ledge twice... no, we want a fighter to have
    # LA-Basic[79] == 2, then get hit directly into Damage_Fly (0x45) offstage without landing!
    # Can Mario hit Fox with F-Smash while Fox is hanging on ledge with LA-Basic[79] == 2?
    # Or can Fox (in Lab CPU Options: CPU Control = Lab, Counter Ground = F-Smash, or Attack) hit Mario?
    # Let's use `plugin_input`! `plugin_input.py` drives ANY fighter (both P1 and CPU) on exact frames!
    import plugin_input as PI
    emu.h.call("resume")
    # Set P1 Mario to 120% via Lab's own percent setting (Mario will fly into Damage_Fly 0x45!).
    emu.h.call("write_mem", addr=settings + S_PLAYER_PERCENT, hex="0078")
    emu.h.call("write_mem", addr=settings + S_PLAYER_LOCK, hex="01")
    CT.wait_game_frames(emu, 3)
    emu.h.call("write_mem", addr=settings + S_PLAYER_LOCK, hex="00")
    emu.h.call("pause")

    # Have Mario grab right ledge twice (LA-Basic[79] == 2) and wait until XLU == 0.
    for g in range(2):
        grab_right_ledge_from_air(start_from_stage=(g == 0))
        if g == 0:
            step(3)
            emu.hold(0, stick=DOWN)
            for _ in range(10):
                step(1)
                if read_fighter_full(emu, p1_ptr)["status"] != ST_CLIFF_WAIT:
                    break
            emu.neutral(0)
            for k in range(40):
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
    emu.neutral(0)
    # Wait out the 31f ledge XLU while Mario hangs on the right ledge.
    for _ in range(40):
        step(1)
        if read_fighter_full(emu, p1_ptr)["xlu"] == 0:
            break
    pre_hit = read_fighter_full(emu, p1_ptr)
    # Move Fox to the right edge via a short jump and wait until Fox lands cleanly in Wait.
    posture_cpu = read_fighter_full(emu, cpu_ptr)["posture"]
    inp = PI.PluginInput(emu)
    fox_jump = PI.frames_to_steps({1: (PI.B_JUMP | PI.B_FLICK, 0.0, 0.0)}, 6)
    inp.run({1: fox_jump}, extra=4)
    put_fighter(emu, posture_cpu, 80.0, 3.0)
    for _ in range(30):
        step(1)
        fs = read_fighter_full(emu, cpu_ptr)
        if not fs["air"] and fs["status"] == ST_WAIT:
            break
    # Refresh Mario's pre_hit snapshot right before Fox swings.
    pre_hit = read_fighter_full(emu, p1_ptr)
    # Fox Down-Smash (B_ATTACK | B_SMASH, down) hits hanging fighters cleanly and sends into Damage_Fly at 120%!
    fox_steps = PI.frames_to_steps({
        1: (0, 1.0, 0.0),
        3: (PI.B_ATTACK | PI.B_SMASH, 0.0, -1.0),
        4: (PI.B_ATTACK | PI.B_SMASH, 0.0, -1.0),
    }, 8)
    trace_fly = []

    def read_p1(_i=None):
        s = read_fighter_full(emu, p1_ptr)
        trace_fly.append((s["status"], s["air"], s["la79"], round(s["x"], 1), round(s["y"], 1)))
        return s

    inp.run({1: fox_steps}, read=read_p1, extra=35)
    fly_rows = [row for row in trace_fly if row[0] in (ST_DAMAGE_FLY, ST_DAMAGE_FLY_ROLL)]
    air_dmg_rows = [row for row in trace_fly if row[0] == ST_DAMAGE_AIR]
    notes.append(
        f"Damage_Fly edge case: pre_hit=(st={pre_hit['status']:#x}, la79={pre_hit['la79']}, xlu={pre_hit['xlu']}) "
        f"saw_0x44={len(air_dmg_rows)} saw_0x45_0x46={len(fly_rows)} "
        f"sample_fly={[(hex(r[0]), r[1], r[2]) for r in fly_rows[:4]]}"
    )
    if not fly_rows:
        fails.append(f"Damage_Fly (0x45/0x46) never triggered off ledge; trace={[(hex(r[0]), r[2]) for r in trace_fly[:20]]}")
    else:
        # Wait until hitstun ends (leaves Damage_Fly), double-jump to cancel outward KB, then re-grab ledge.
        for _ in range(60):
            cur = read_fighter_full(emu, p1_ptr)
            if cur["x"] > 120.0 or cur["y"] < -60.0 or cur["y"] > 80.0:
                put_fighter(emu, posture_p1, 102.0, -10.0)
            if cur["status"] not in (ST_DAMAGE_FLY, ST_DAMAGE_FLY_ROLL):
                break
            emu.hold(0, ["X"], stick=LEFT)
            step(1)
        emu.neutral(0)
        put_fighter(emu, posture_p1, 92.0, -10.0)
        step(1)
        next_grab = grab_right_ledge_from_air(start_from_stage=False)
        notes.append(
            f"Damage_Fly next grab: la79_before={pre_hit['la79']} la79_during_fly={fly_rows[0][2]} "
            f"la79_on_next_grab={next_grab['la79']}"
        )

    emu.h.call("resume")
    return fails


# ---- Scenario 2: Can Walljump & Wall Cling Info Display --------------------

def sc_wall_info(emu, notes, shots, has_d3d):
    fails = []

    def set_wall_rows(settings):
        rows = [IV_CAN_WALLJUMP, IV_WALL_CLING, IV_WALLJUMPS, IV_LINES, IV_STATE, 0, 0, 0]
        emu.h.call("write_mem", addr=settings + S_HMN_INFO_ROWS, hex="".join(f"{r:02x}" for r in rows))

    def collect_info(log_text, player=1):
        out = {}
        for p, line in INFO_RE.findall(log_text):
            if int(p) == player and ":" in line:
                k, v = line.split(":", 1)
                out[k.strip()] = v.strip()
        return out

    # 1. Mario: Can Walljump: Yes -> CD Nf (counts down 20..1) -> Yes; Wall Cling: No.
    lab, settings, p1, _ = start_match(emu, "mario")
    trace_addr = find_trace_byte(emu)
    set_wall_rows(settings)
    emu.h.call("pause")
    emu.h.call("write_mem", addr=trace_addr, hex="01")
    pos = len(emu.log_text())
    emu.h.call("frame_advance", n=3)
    mario_gnd = collect_info(emu.log_text()[pos:])
    notes.append(f"mario grounded info: {mario_gnd}")
    if mario_gnd.get("Can Walljump") != "Yes" or mario_gnd.get("Wall Cling") != "No":
        fails.append(f"mario grounded: got {mario_gnd}")

    # Move Mario beside FD's right wall and walljump (exact path from lab_tests.py:1486-1503).
    p1_ptr = p1["fighter"]
    posture = read_fighter_full(emu, p1_ptr)["posture"]
    emu.hold(0, ["X"])
    emu.h.call("frame_advance", n=3)
    emu.neutral(0)
    emu.h.call("frame_advance", n=9)
    for wx, wy in ((100.0, 15.0), (100.0, -10.0), (73.0, -36.0)):
        put_fighter(emu, posture, wx, wy)
        emu.h.call("frame_advance", n=1)

    touched = False
    wj_frame = None
    la4_seq = []
    cd_lines = []
    pos_wj = len(emu.log_text())
    for i in range(110):
        s = read_fighter_full(emu, p1_ptr)
        cur_log = emu.log_text()[pos_wj:]
        pos_wj += len(cur_log)
        for p, line in INFO_RE.findall(cur_log):
            if int(p) == 1:
                if line.startswith("Coll:") and "L:" in line and "L:-1" not in line:
                    touched = True
                if line.startswith("Can Walljump: CD "):
                    cd_lines.append(line)
        if s["status"] == ST_WALL_JUMP and wj_frame is None:
            wj_frame = i
        if wj_frame is not None:
            la4_seq.append((i - wj_frame, s["la4"], s["la3"], s["air"]))
            if i - wj_frame >= 24:
                break
            emu.neutral(0)
        elif touched:
            emu.hold(0, stick=(255, 128))
        else:
            emu.hold(0, stick=LEFT)
        emu.h.call("frame_advance", n=1)
    emu.neutral(0)
    notes.append(f"mario walljump: wj_frame={wj_frame} la4_seq={la4_seq[:8]} ... {la4_seq[-4:]}")
    notes.append(f"mario CD info lines ({len(cd_lines)}): {cd_lines[:5]} ... {cd_lines[-2:] if cd_lines else []}")
    if wj_frame is None:
        fails.append("mario never performed a walljump")
    else:
        # Verify LA-Basic[4] counts DOWN from 20 toward 0!
        nonzero = [v for _, v, _, _ in la4_seq if v > 0]
        if not nonzero or max(nonzero) < 15 or nonzero != sorted(nonzero, reverse=True):
            fails.append(f"mario LA-Basic[4] did not count down cleanly from ~20: {la4_seq}")
        if not cd_lines:
            fails.append("mario never showed 'Can Walljump: CD Nf' during cooldown")
        if la4_seq[-1][1] != 0:
            fails.append(f"mario LA-Basic[4] did not reach 0 after 24f: {la4_seq[-1]}")
    # Land and confirm Can Walljump returns to Yes.
    for wx, wy in ((100.0, -10.0), (100.0, 15.0), (0.0, 15.0)):
        put_fighter(emu, posture, wx, wy)
        emu.h.call("frame_advance", n=1)
    emu.h.call("frame_advance", n=45)
    pos_after = len(emu.log_text())
    emu.h.call("frame_advance", n=3)
    mario_after = collect_info(emu.log_text()[pos_after:])
    if mario_after.get("Can Walljump") != "Yes":
        fails.append(f"mario after landing: got {mario_after}")
    emu.h.call("write_mem", addr=trace_addr, hex="00")
    emu.h.call("resume")

    # 2. Sheik: Can Walljump: Yes, Wall Cling: Yes -> cling -> Used (airborne) -> Yes (landed).
    lab, settings, p1, _ = start_match(emu, "sheik")
    trace_addr = find_trace_byte(emu)
    set_wall_rows(settings)
    emu.h.call("pause")
    emu.h.call("write_mem", addr=trace_addr, hex="01")
    pos = len(emu.log_text())
    emu.h.call("frame_advance", n=3)
    sheik_gnd = collect_info(emu.log_text()[pos:])
    notes.append(f"sheik grounded info: {sheik_gnd}")
    if sheik_gnd.get("Can Walljump") != "Yes" or sheik_gnd.get("Wall Cling") != "Yes":
        fails.append(f"sheik grounded: got {sheik_gnd}")

    p1_ptr = p1["fighter"]
    posture = read_fighter_full(emu, p1_ptr)["posture"]
    emu.hold(0, ["X"])
    emu.h.call("frame_advance", n=3)
    emu.neutral(0)
    emu.h.call("frame_advance", n=9)
    for wx, wy in ((100.0, 15.0), (100.0, -10.0), (73.0, -36.0)):
        put_fighter(emu, posture, wx, wy)
        emu.h.call("frame_advance", n=1)

    clung = None
    detached = None
    used_seen = False
    sheik_cd_seen = False
    pos_sh = len(emu.log_text())
    for i in range(110):
        s = read_fighter_full(emu, p1_ptr)
        cur_log = emu.log_text()[pos_sh:]
        pos_sh += len(cur_log)
        for p, line in INFO_RE.findall(cur_log):
            if int(p) == 1:
                if line == "Wall Cling: Used":
                    used_seen = True
                if line.startswith("Can Walljump: CD "):
                    sheik_cd_seen = True
        if s["status"] in (ST_ATTACH_WALL, ST_ATTACH_WALL_WAIT) and clung is None:
            clung = (i, s["status"], s["air"], s["la11"], s["la12"])
        if clung is None:
            emu.hold(0, stick=LEFT)
        elif i < clung[0] + 8:
            emu.hold(0, stick=LEFT)
        elif detached is None:
            # Walljump / detach away from wall with Jump + Right.
            emu.hold(0, ["X"], stick=RIGHT)
            if s["status"] not in (ST_ATTACH_WALL, ST_ATTACH_WALL_WAIT):
                detached = (i, s["status"], s["air"], s["la4"], s["la11"], s["la12"])
        else:
            emu.neutral(0)
            if i >= detached[0] + 10:
                break
        emu.h.call("frame_advance", n=1)
    emu.neutral(0)
    notes.append(f"sheik cling: clung={clung} detached={detached} used_seen={used_seen} cd_seen={sheik_cd_seen}")
    if clung is None or detached is None:
        fails.append(f"sheik cling/detach failed: clung={clung} detached={detached}")
    if not used_seen:
        fails.append("sheik never showed 'Wall Cling: Used' while airborne after clinging")

    # Land Sheik back on FD and confirm Wall Cling resets to Yes.
    for wx, wy in ((100.0, -10.0), (100.0, 15.0), (0.0, 15.0)):
        put_fighter(emu, posture, wx, wy)
        emu.h.call("frame_advance", n=1)
    emu.h.call("frame_advance", n=50)
    pos_sh_land = len(emu.log_text())
    emu.h.call("frame_advance", n=3)
    sheik_land = collect_info(emu.log_text()[pos_sh_land:])
    notes.append(f"sheik landed info: {sheik_land}")
    if sheik_land.get("Wall Cling") != "Yes":
        fails.append(f"sheik after landing: got {sheik_land}")
    emu.h.call("write_mem", addr=trace_addr, hex="00")
    emu.h.call("resume")

    # 3. Lucario (Yes / No), Mewtwo (No / No), Knuckles (Yes / Climb).
    checks = [
        ("lucario", "Yes", "No"),
        ("mewtwo", "No", "No"),
        ("knuckles", "Yes", "Climb"),
    ]
    for cname, want_wj, want_wc in checks:
        lab, settings, p1, _ = start_match(emu, cname)
        trace_addr = find_trace_byte(emu)
        set_wall_rows(settings)
        emu.h.call("pause")
        emu.h.call("write_mem", addr=trace_addr, hex="01")
        pos = len(emu.log_text())
        emu.h.call("frame_advance", n=3)
        got = collect_info(emu.log_text()[pos:])
        notes.append(f"{cname} grounded info: {got}")
        if got.get("Can Walljump") != want_wj or got.get("Wall Cling") != want_wc:
            fails.append(f"{cname}: got {got}, expected WJ={want_wj} WC={want_wc}")
        if cname == "knuckles" and has_d3d:
            emu.h.call("write_mem", addr=trace_addr, hex="00")
            emu.h.call("resume")
            shots.append(emu.screenshot("stage5d-knuckles-wall-info"))
            continue
        emu.h.call("write_mem", addr=trace_addr, hex="00")
        emu.h.call("resume")

    return fails


# ---- Scenario 3: Nana Throw (Default + F/B/U/D + Sopo gating) --------------

def sc_nana_throw(emu, notes, shots, has_d3d):
    fails = []
    lab, settings, p1, cpu = start_match(emu, "ice_climbers", "fox")
    popo_ptr = p1["fighter"]
    nana_ptr = find_nana_fighter(emu, p1["entry"])
    cpu_ptr = cpu["fighter"]
    if nana_ptr is None:
        return ["Nana fighter pointer not found in Ice Climbers entry"]

    # 1. Verify Fighter RNG menu row is enabled when Popo + Nana are alive, and
    #    confirm the Nana Throw row is reachable at the bottom of Fighter RNG.
    emu.h.call("pause")
    emu.h.call("frame_advance", n=2)
    popo_post = read_fighter_full(emu, popo_ptr)["posture"]
    nana_post = read_fighter_full(emu, nana_ptr)["posture"]
    cpu_post = read_fighter_full(emu, cpu_ptr)["posture"]

    # Move Fox to true center stage (x=0.0, > 30 units from both FD ledges +-85.5 so
    # Default ai_nana 0x6100 chooses U-Throw), then jump Ice Climbers and land Nana
    # in grab range in front of Fox (-5.5) with Popo behind grab range (-16.0) so
    # Popo whiffs Z and Nana's 6-frame delayed Z grabs Fox cleanly at center stage.
    import plugin_input as PI
    inp = PI.PluginInput(emu)
    fox_jump = PI.frames_to_steps({1: (PI.B_JUMP | PI.B_FLICK, 0.0, 0.0)}, 6)
    inp.run({1: fox_jump}, extra=4)
    put_fighter(emu, cpu_post, 0.0, 4.0)
    emu.h.call("frame_advance", n=18)

    emu.hold(0, ["X"])
    emu.h.call("frame_advance", n=3)
    emu.neutral(0)
    # Wait until BOTH Popo and Nana are falling (Fall = 0x0E, vy < 0) so placing
    # them at y=1.0 lands both in 1-2 frames before Nana's AI can walk back.
    for _ in range(45):
        emu.h.call("frame_advance", n=1)
        ps = read_fighter_full(emu, popo_ptr)
        ns = read_fighter_full(emu, nana_ptr)
        if ps["status"] == 0x0E and ns["status"] == 0x0E:
            break
    fs = read_fighter_full(emu, cpu_ptr)
    # Place Popo at +9.0 (past Fox, facing right AWAY from Fox so Popo's Z always
    # whiffs) and Nana at -4.5 (facing right AT Fox, only 13.5u from Popo so she
    # mirrors Popo's Z 6 frames later and grabs Fox cleanly at center stage).
    put_fighter(emu, popo_post, fs["x"] + 9.0, 1.0)
    put_fighter(emu, nana_post, fs["x"] - 4.5, 1.0)
    found_window = False
    for _ in range(10):
        emu.h.call("frame_advance", n=1)
        ps = read_fighter_full(emu, popo_ptr)
        ns = read_fighter_full(emu, nana_ptr)
        fs = read_fighter_full(emu, cpu_ptr)
        if not ps["air"] and not ns["air"]:
            # Wait out 4-frame Landing (0x16) so both enter Wait (0x00) immediately.
            emu.h.call("frame_advance", n=4)
            ps = read_fighter_full(emu, popo_ptr)
            ns = read_fighter_full(emu, nana_ptr)
            fs = read_fighter_full(emu, cpu_ptr)
            found_window = (ps["x"] > fs["x"] + 4.0 and fs["x"] - 8.0 <= ns["x"] <= fs["x"] - 1.5)
            break
    ps = read_fighter_full(emu, popo_ptr)
    ns = read_fighter_full(emu, nana_ptr)
    fs = read_fighter_full(emu, cpu_ptr)
    notes.append(f"ics grab setup (window={found_window}): popo=({ps['x']:.1f},{ps['lr']:+.0f},st={ps['status']:#x}) "
                 f"nana=({ns['x']:.1f},{ns['lr']:+.0f},st={ns['status']:#x}) fox=({fs['x']:.1f})")

    # Save right before the grab press so all 5 modes run from the exact same center-stage state.
    grab_sav = state_path("nana-grab")
    emu.h.call("save_state", path=grab_sav)

    # Expected motionKind for each setting at center stage (Default = U-Throw via ai_nana 0x6100).
    mode_specs = [
        (0, "Default", MOT_THROW_HI),
        (1, "F-Throw", MOT_THROW_F),
        (2, "B-Throw", MOT_THROW_B),
        (3, "U-Throw", MOT_THROW_HI),
        (4, "D-Throw", MOT_THROW_LW),
    ]
    for mode, label, want_mot in mode_specs:
        emu.h.call("load_state", path=grab_sav)
        emu.h.call("pause")
        emu.h.call("write_mem", addr=settings + S_NANA_THROW, hex=f"{mode:02x}")
        emu.h.call("frame_advance", n=1)
        # Press Z for 3 frames on port 1.
        emu.hold(0, ["Z"])
        emu.h.call("frame_advance", n=3)
        emu.neutral(0)

        saw_catch_wait = False
        saw_pummel = False
        throw_mot = None
        nana_lr = None
        nana_sts = []
        popo_sts = []
        fox_before = read_fighter_full(emu, cpu_ptr)
        for i in range(95):
            emu.h.call("frame_advance", n=1)
            ns = read_fighter_full(emu, nana_ptr)
            ps_cur = read_fighter_full(emu, popo_ptr)
            if not nana_sts or nana_sts[-1] != ns["status"]:
                nana_sts.append(ns["status"])
            if not popo_sts or popo_sts[-1] != ps_cur["status"]:
                popo_sts.append(ps_cur["status"])
            if ns["status"] == ST_CATCH_WAIT:
                saw_catch_wait = True
                nana_lr = ns["lr"]
            elif ns["status"] == ST_CATCH_ATTACK:
                saw_pummel = True
            elif ns["status"] == ST_THROW and throw_mot is None:
                throw_mot = ns["mkind"]
                break
        # Step 15 more frames to verify throw motion plays out cleanly.
        emu.h.call("frame_advance", n=15)
        fox_after = read_fighter_full(emu, cpu_ptr)
        notes.append(
            f"nana throw {label} (mode={mode}): catchWait={saw_catch_wait} lr={nana_lr} "
            f"pummel={saw_pummel} throw_mot={hex(throw_mot) if throw_mot is not None else None} "
            f"want={hex(want_mot)} fox_dx={fox_after['x'] - fox_before['x']:+.1f} "
            f"fox_dy={fox_after['y'] - fox_before['y']:+.1f} "
            f"nana_sts={[hex(s) for s in nana_sts[:8]]} popo_sts={[hex(s) for s in popo_sts[:6]]}"
        )
        if not saw_catch_wait:
            fails.append(f"nana throw {label}: Nana never entered Catch_Wait (0x39)")
        elif throw_mot != want_mot:
            fails.append(f"nana throw {label}: got throw motion {hex(throw_mot) if throw_mot else None}, expected {hex(want_mot)}")
        if mode != 0 and saw_pummel:
            fails.append(f"nana throw {label}: stray pummel (Catch_Attack 0x3A) observed")

    # 2. Verify menu gating in Stage & Cast: Nana Throw (raw row 32) is visible
    #    with Popo+Nana on FD, and compacted away when Sopo (Nana KO'd).
    emu.h.call("load_state", path=grab_sav)
    emu.h.call("resume")
    emu.advance(4)
    # Open lab menu (Draft C Rail starts on Lab), L wraps to Stage & Cast (cat 6).
    # On FD with Popo+Nana, visible rows are 0 (Fighter Swap), 1 (Show Inactive), 32 (Nana Throw).
    lab.open_menu()
    lab.tap(["L"])
    lab.tap(stick=DOWN)
    lab.tap(stick=DOWN)
    lab.close_menu()
    # Reopen menu with keep=True: lab.cpp openMenu logs exact page title and raw row index!
    pos_re = lab.log_pos()
    lab.open_menu(keep=True)
    open_log = emu.synced_log_text()[pos_re:]
    m_ic = re.search(LT.MENU_OPENED, open_log)
    notes.append(f"menu with Popo+Nana: {m_ic.groupdict() if m_ic else open_log[-120:]}")
    if not m_ic or m_ic.group("title") != "Stage & Cast" or int(m_ic.group("row")) != 32:
        fails.append(f"Nana Throw row 32 not reached on Stage & Cast with Popo+Nana: {m_ic.groupdict() if m_ic else open_log}")
    lab.close_menu()

    # Now KO Nana: jump so Nana is airborne, then move her below FD's bottom blastzone -> Sopo.
    emu.h.call("pause")
    emu.hold(0, ["X"])
    emu.h.call("frame_advance", n=3)
    emu.neutral(0)
    emu.h.call("frame_advance", n=12)
    put_fighter(emu, nana_post, 120.0, -180.0)
    emu.h.call("frame_advance", n=30)
    nana_dead = read_fighter_full(emu, nana_ptr)
    emu.h.call("resume")
    emu.advance(20)
    notes.append(f"Sopo state: nana status={nana_dead['status']:#x}")
    if nana_dead["status"] not in (ST_DEAD, ST_STANDBY):
        fails.append(f"Nana did not enter Dead/Standby for Sopo check (status={nana_dead['status']:#x})")
    else:
        # On Stage & Cast with Sopo (Nana dead), row 32 (Nana Throw) is dynamically hidden,
        # so pressing L to jump to Stage & Cast and moving down twice clamps at row 1 (Show Inactive).
        lab.open_menu()
        lab.tap(["L"])
        lab.tap(stick=DOWN)
        lab.tap(stick=DOWN)
        lab.close_menu()
        pos_sopo = lab.log_pos()
        lab.open_menu(keep=True)
        sopo_log = emu.synced_log_text()[pos_sopo:]
        m_sopo = re.search(LT.MENU_OPENED, sopo_log)
        notes.append(f"menu with Sopo (Nana dead): {m_sopo.groupdict() if m_sopo else sopo_log[-120:]}")
        if not m_sopo or m_sopo.group("title") != "Stage & Cast" or int(m_sopo.group("row")) != 1:
            fails.append(f"Nana Throw row was NOT hidden for Sopo: {m_sopo.groupdict() if m_sopo else sopo_log}")
        lab.close_menu()

    return fails


SCENARIO_FNS = {
    "ledge-5-grab": sc_ledge_5_grab,
    "wall-info": sc_wall_info,
    "nana-throw": sc_nana_throw,
}


def cmd_test_stage5d(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage5d")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    label = "test-stage5d"
    if opts.only:
        unknown = [n for n in opts.only if n not in SCENARIO_FNS]
        if unknown:
            E.die(f"no scenario named {unknown}; choose from {', '.join(SCENARIOS)}")
    selected = [n for n in SCENARIOS if not opts.only or n in opts.only]

    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    has_d3d = backend.lower() != "null"
    t0 = time.monotonic()
    why, log_path = "", ""
    notes = []
    shots = []
    failed = []

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            for name in selected:
                s0 = time.monotonic()
                try:
                    fails = SCENARIO_FNS[name](emu, notes, shots, has_d3d)
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError, Stage5dError, ValueError) as exc:
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

    print("\n--- stage5d measurements ---")
    for line in notes:
        print(" ", line)
    if shots:
        print("screenshots:", [s[0] for s in shots])
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
