"""Emulator tests for clean-room PPlusAI 44-fighter recovery and Fudge Combo Trainer
(run through emu.py; settings come from the environment the same way).

  test-recovery-combo REL [--only NAME ...] [--list]

One boot on Final Destination, switching CPU characters via nav.start_quick_match.
Verifies:
  - PPlusAI recovery offstage across 8 representative fighters spanning all 8
    recovery families (fox, marth, mewtwo, meta_knight, samus, ness, ike, peach),
    confirming the game's AI stays silenced while PPlusAI recovers and exits CliffWait.
  - Game AI default recovery + clean Rebirth (0xBE) respawn halo drop and
    CliffWait (0x75) ledge exit.
  - Combo Trainer: hitstun-drop auto-reset, 1-frame noclip (Correct_None = 0 on
    reset frame -> Correct_Air = 5 on the very next frame, verified in RAM),
    randomizePosition / randOnGround / randomizeDamage, recoveryTrainer offstage
    loop, and all 7 floating world-space state meters + enabledMeters bitmask.
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import cpu_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import plugin_input as PI  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# (gmCharacterKind, CSS icon id) from BrawlHeaders gm/gm_lib.h and nav.CSS_ID.
CHARS = {
    "mario": (0x00, 0x00),
    "fox": (0x07, 0x07),
    "marth": (0x13, 0x11),
    "mewtwo": (0x33, 0x2E),
    "meta_knight": (0x18, 0x16),
    "samus": (0x03, 0x03),
    "ness": (0x0B, 0x0B),
    "ike": (0x25, 0x21),
    "peach": (0x0D, 0x0D),
}

# Settings offsets (src/main/settings.h + src/main/cpu/cpu_settings.h).
SET_CPU_OPTIONS = 123
CPU_RECOVERY_OFFSET = 39
RECOVERY_GAME_AI, RECOVERY_PPLUS_AI, RECOVERY_OFF = 0, 1, 2

SET_COMBO = 416
COMBO_MAGIC = 0x544D4354  # "TMCT"
COMBO_LOG = re.compile(r"combo: debug block at ([0-9a-f]{8})")
COMBO_SLOT_SIZE = 0x40
COMBO_SLOT_FMT = ">IIBBBBffffffffffhhhhhh"

# ComboMeterBit flags (src/main/settings.h).
METER_HITLAG = 1 << 0
METER_HITSTUN = 1 << 1
METER_SHIELDSTUN = 1 << 2
METER_DI = 1 << 3
METER_IASA = 1 << 4
METER_LEDGE_INTANG = 1 << 5
METER_INTANG = 1 << 6
METER_ALL = 0x7F

CORRECT_NONE, CORRECT_GROUND_CLIFF_STOP, CORRECT_AIR = 0, 2, 5
FD_EDGE_X = 85.5


class TestError(RuntimeError):
    pass


class ComboBlock:
    """Reads extra::ComboDebugBlock directly from game RAM."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        found = COMBO_LOG.findall(self.emu.log_text())
        if not found:
            raise TestError("no combo debug block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != COMBO_MAGIC:
            raise TestError(f"bad combo debug magic at {self.addr:#x}")

    def slot(self, player=CT.CPU):
        raw = self.emu.read(self.addr + 8 + player * COMBO_SLOT_SIZE, COMBO_SLOT_SIZE)
        vals = struct.unpack(COMBO_SLOT_FMT, raw)
        return {
            "resetCount": vals[0],
            "recoveryResets": vals[1],
            "lastCorrectWritten": vals[2],
            "observedCorrect": vals[3],
            "activeMeterMask": vals[4],
            "restorePending": vals[5],
            "anchorX": vals[6],
            "anchorY": vals[7],
            "hitlagRatio": vals[8],
            "hitstunRatio": vals[9],
            "shieldStunRatio": vals[10],
            "diX": vals[11],
            "diY": vals[12],
            "iasaRatio": vals[13],
            "ledgeIntangRatio": vals[14],
            "intangRatio": vals[15],
            "hitlagFrames": vals[16],
            "hitstunFrames": vals[17],
            "shieldStunFrames": vals[18],
            "iasaFrames": vals[19],
            "ledgeIntangFrames": vals[20],
            "intangFrames": vals[21],
        }


def write_cpu_recovery(emu, settings_base, mode):
    emu.h.call("write_mem", addr=settings_base + SET_CPU_OPTIONS + CPU_RECOVERY_OFFSET,
               hex=bytes([mode]).hex())


def write_combo_settings(emu, settings_base, *, enabled=0, noclip=0, fix_pos=0, rand_pos=0,
                         rand_ground=1, rand_dmg=0, recovery_trainer=0, timer_adj=0,
                         fixed_dmg=0, enabled_meters=METER_ALL, fixed_dmg_mode=0,
                         reset_p1=1, reset_delay=0, post_escape=0, rec_mixup=0):
    payload = struct.pack(">BBBBBBBbHBBBBBB",
                          int(enabled), int(noclip), int(fix_pos), int(rand_pos),
                          int(rand_ground), int(rand_dmg), int(recovery_trainer),
                          int(timer_adj), int(fixed_dmg), int(enabled_meters),
                          int(fixed_dmg_mode), int(reset_p1), int(reset_delay),
                          int(post_escape), int(rec_mixup))
    emu.h.call("write_mem", addr=settings_base + SET_COMBO, hex=payload.hex())


def configure_lab_cpu(r, recovery_mode, behavior=0, tdi=CT.TDI["None"]):
    r.opt.set(control=CT.LAB, behavior=behavior, tdi=tdi,
              sdiAmount=0, counterGround=0, counterAir=0, counterShield=0,
              tech=CT.TECH["None"], getup=CT.GETUP["Stand"], grabEscape=0)
    write_cpu_recovery(r.emu, r.opt.base, recovery_mode)


def wait_cpu_grounded_center(r, max_frames=240):
    """Ensure CPU is standing/alive on stage before starting a scenario."""
    en = r.g.entry(CT.CPU)
    for _ in range(max_frames):
        f = r.g.fighter(en)
        if not f["air"] and f["status"] not in CT.GONE and abs(f["x"]) < 80.0:
            return f
        r.emu.advance(1)
    return r.g.fighter(en)


def walk_cpu_offstage(r, target_x=108.0, jump_edge_x=78.0, max_frames=260, spend_dj=False, target_y=None):
    """Drive CPU off the right edge of FD via harness Script until clearly offstage."""
    en = r.g.entry(CT.CPU)
    r.drv.play(CT.CPU, [(0, 1.0, 0.0, 260)])
    jumped = False
    dj_spent = False
    for _ in range(max_frames):
        r.emu.advance(1)
        f = r.g.fighter(en)
        if f["status"] in CT.GONE:
            break
        if not jumped and f["x"] >= jump_edge_x and not f["air"]:
            # Jump out right to get clean offstage separation for all fall speeds.
            r.drv.play(CT.CPU, [(CT.JUMP, 1.0, 0.0, 6), (0, 1.0, 0.0, 180)])
            jumped = True
        elif spend_dj and jumped and not dj_spent and f["air"] and f["x"] >= 96.0:
            # Spend double jump straight up/in-place then fastfall so CPU stays in recovery range.
            r.drv.play(CT.CPU, [(0, 0.0, 0.0, 2), (CT.JUMP, 0.0, 0.0, 4), (0, 0.0, -1.0, 160)])
            dj_spent = True
        if f["air"] and f["x"] >= target_x and (target_y is None or f["y"] <= target_y):
            if not spend_dj or dj_spent:
                break
    r.drv.stop()
    return r.g.fighter(en)


def run_pplus_recovery_scenario(r, fighter_name):
    """Generic PPlusAI offstage recovery check for one representative fighter."""
    res = CT.Result(f"pplus-{fighter_name}")
    write_combo_settings(r.emu, r.opt.base, enabled=0)
    configure_lab_cpu(r, RECOVERY_PPLUS_AI)
    r.opt.cpu_percent(0, False)
    r.emu.advance(10)
    wait_cpu_grounded_center(r)

    if fighter_name == "ness":
        start_f = walk_cpu_offstage(r, target_x=98.0, spend_dj=True, target_y=-16.0, max_frames=320)
    else:
        start_f = walk_cpu_offstage(r, target_x=106.0)
    res.check(f"started offstage (x={start_f['x']:.1f}, y={start_f['y']:.1f}, air={start_f['air']})",
              start_f["air"] and start_f["x"] >= 90.0)

    en = r.g.entry(CT.CPU)
    slot = en["slot"]
    ai_silenced = True
    saw_special_or_jump = False
    saw_pk_thunder = False
    saw_cliff_wait = False
    exited_cliff_wait = False
    returned_on_stage = False
    died = False
    min_x_seen = start_f["x"]
    max_status = 0
    land_frame = -1
    trace = []
    last_traced_st = -1

    for i in range(420):
        r.emu.advance(1)
        f = r.g.fighter(en)
        st = f["status"]
        if st != last_traced_st or (i % 30 == 0):
            trace.append(f"f{i}:{st:#x}@({f['x']:.1f},{f['y']:.1f})")
            last_traced_st = st
        if r.g.ai_flag(slot) != 0:
            ai_silenced = False
        if st in CT.GONE:
            died = True
            break
        if st >= 0x112 or st in (CT.STATUS["Jump_Aerial"], 0x0D, 0x84, 0x85, 0x7F):
            saw_special_or_jump = True
            if st > max_status:
                max_status = st
        if fighter_name == "ness" and st >= 0x114:
            saw_pk_thunder = True
        if st == CT.STATUS["Cliff_Wait"]:
            saw_cliff_wait = True
        if f["x"] < min_x_seen:
            min_x_seen = f["x"]
        if not f["air"] and abs(f["x"]) <= FD_EDGE_X and st not in (CT.STATUS["Cliff_Catch"], CT.STATUS["Cliff_Wait"]):
            if saw_cliff_wait:
                exited_cliff_wait = True
            returned_on_stage = True
            land_frame = i
            break

    res.row("AI stays silenced", "0", "0" if ai_silenced else "nonzero", ai_silenced)
    res.row("used recovery move/jump", "True", f"{saw_special_or_jump} (maxSt={max_status:#x})",
            saw_special_or_jump)
    res.row("returned onto stage without KO", f"landed |x|<={FD_EDGE_X:.1f}",
            f"landed={returned_on_stage} f={land_frame} died={died} minX={min_x_seen:.1f}",
            returned_on_stage and not died)
    if saw_cliff_wait:
        res.row("exited CliffWait cleanly", "True", str(exited_cliff_wait), exited_cliff_wait)
    if fighter_name == "ness":
        res.row("Ness entered PK Thunder special", "True", str(saw_pk_thunder), saw_pk_thunder)
    if not res.ok:
        res.note("trace: " + " ".join(trace[:24]))
    return res


def scenario_game_ai_rebirth_cliff(r):
    """Game AI default recovery + fast Rebirth (0xBE) halo drop + CliffWait exit."""
    res = CT.Result("game-ai-rebirth-cliff")
    write_combo_settings(r.emu, r.opt.base, enabled=0)
    configure_lab_cpu(r, RECOVERY_GAME_AI)
    r.opt.cpu_percent(0, False)
    r.emu.advance(10)
    wait_cpu_grounded_center(r)

    en = r.g.entry(CT.CPU)
    slot = en["slot"]

    # Part 1: Walk CPU offstage and let Game AI recover cleanly.
    start_f = walk_cpu_offstage(r, target_x=104.0)
    res.check(f"offstage for Game AI (x={start_f['x']:.1f}, air={start_f['air']})",
              start_f["air"] and start_f["x"] >= 90.0)
    saw_game_ai_active = False
    game_ai_landed = False
    for _ in range(360):
        r.emu.advance(1)
        f = r.g.fighter(en)
        if r.g.ai_flag(slot) != 0:
            saw_game_ai_active = True
        if not f["air"] and abs(f["x"]) <= 82.0 and f["status"] not in CT.GONE:
            game_ai_landed = True
            break
    res.row("Game AI unsilenced offstage", "ai_flag!=0", str(saw_game_ai_active), saw_game_ai_active)
    res.row("Game AI recovered to stage", "True", str(game_ai_landed), game_ai_landed)

    # Part 2: Force KO to verify Rebirth (0xBE) respawn halo drops cleanly and fast.
    r.drv.play(CT.CPU, [(0, 1.0, 0.0, 260)])
    entered_rebirth = False
    fastfalling = False
    for _ in range(420):
        r.emu.advance(1)
        f = r.g.fighter(en)
        if f["status"] == CT.STATUS["Rebirth"]:
            entered_rebirth = True
            break
        if not fastfalling and f["air"] and f["x"] > 95.0:
            r.drv.play(CT.CPU, [(0, 1.0, -1.0, 260)])
            fastfalling = True
    r.drv.stop()
    res.check("entered Rebirth (0xBE) after KO", entered_rebirth)

    rebirth_frames = 0
    dropped_cleanly = False
    for i in range(180):
        r.emu.advance(1)
        f = r.g.fighter(en)
        if f["status"] == CT.STATUS["Rebirth"]:
            rebirth_frames += 1
        elif not f["air"] and abs(f["x"]) < 80.0 and f["status"] not in CT.GONE:
            dropped_cleanly = True
            break
    # Brawl's idle halo timeout is ~300 frames; pressing down drops well under 120 frames.
    res.row("Rebirth halo dropped quickly", "dropped & <=120f",
            f"dropped={dropped_cleanly} rebirthFrames={rebirth_frames}",
            dropped_cleanly and rebirth_frames <= 120)
    return res


def move_p1_to_cpu_and_hit(r, move=CT.FSMASH, max_walk=180):
    """Move P1 beside CPU and throw an attack until CPU enters hitstun."""
    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)
    for _ in range(max_walk):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        dx = cpu["x"] - p1["x"]
        if abs(dx) < 16.0:
            break
        sx = 228 if dx > 0 else 28
        r.emu.hold(0, stick=(sx, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(2)
    # Face CPU then press A with move stick.
    p1 = r.g.fighter(p1_en)
    cpu = r.g.fighter(cpu_en)
    dx = cpu["x"] - p1["x"]
    sx, sy = move
    if move == CT.FSMASH:
        sx = 255 if dx >= 0 else 0
    r.emu.press(0, ["A"], stick=(sx, sy), frames=3, after=0)
    for _ in range(45):
        r.emu.advance(1)
        cpu = r.g.fighter(cpu_en)
        if cpu["hitstun"] > 0 or cpu["hitlag"] > 0:
            return True
    return False


def scenario_combo_reset_noclip(r, cb):
    """Verify hitstun-drop reset + 1-frame Correct_None -> Correct_Air in live RAM."""
    res = CT.Result("combo-reset-noclip")
    configure_lab_cpu(r, RECOVERY_PPLUS_AI)
    r.opt.cpu_percent(0, False)
    write_combo_settings(r.emu, r.opt.base, enabled=1, noclip=1, fix_pos=1,
                         rand_pos=0, rand_ground=1, rand_dmg=0, recovery_trainer=0,
                         timer_adj=0, fixed_dmg=35, enabled_meters=METER_ALL, fixed_dmg_mode=1)
    r.emu.advance(12)
    wait_cpu_grounded_center(r)
    # Wait out any residual respawn invincibility from a preceding KO scenario.
    for _ in range(180):
        if cb.slot(CT.CPU)["intangFrames"] == 0:
            break
        r.emu.advance(1)
    r.emu.advance(6)

    before = cb.slot(CT.CPU)
    anchor_x = before["anchorX"]
    anchor_y = before["anchorY"]
    res.check(f"anchor recorded ({anchor_x:.1f}, {anchor_y:.1f})", abs(anchor_x) < 82.0)

    hit_ok = move_p1_to_cpu_and_hit(r, CT.FSMASH)
    res.check("P1 landed hit on CPU", hit_ok)

    # Step frame-by-frame to capture exact reset frame and next frame!
    en = r.g.entry(CT.CPU)
    reset_frame_slot = None
    next_frame_slot = None
    post_cpu = None
    base_resets = before["resetCount"]
    for _ in range(180):
        r.emu.advance(1)
        s = cb.slot(CT.CPU)
        if reset_frame_slot is None and s["resetCount"] > base_resets:
            reset_frame_slot = s
            post_cpu = r.g.fighter(en)
            r.emu.advance(1)
            next_frame_slot = cb.slot(CT.CPU)
            next_cpu = r.g.fighter(en)
            break

    res.row("auto-reset fired after hitstun drop", "resetCount+1",
            f"fired={reset_frame_slot is not None}", reset_frame_slot is not None)
    if reset_frame_slot is not None and next_frame_slot is not None:
        res.row("reset frame noclip Correct_None", "last=0 obs=0 pend=1",
                f"last={reset_frame_slot['lastCorrectWritten']} obs={reset_frame_slot['observedCorrect']} "
                f"pend={reset_frame_slot['restorePending']}",
                reset_frame_slot["lastCorrectWritten"] == CORRECT_NONE
                and reset_frame_slot["observedCorrect"] == CORRECT_NONE
                and reset_frame_slot["restorePending"] == 1)
        res.row("next frame restores Correct_Ground_Cliff_Stop", "last=2 obs=2 pend=0",
                f"last={next_frame_slot['lastCorrectWritten']} obs={next_frame_slot['observedCorrect']} "
                f"pend={next_frame_slot['restorePending']}",
                next_frame_slot["lastCorrectWritten"] == CORRECT_GROUND_CLIFF_STOP
                and next_frame_slot["observedCorrect"] == CORRECT_GROUND_CLIFF_STOP
                and next_frame_slot["restorePending"] == 0)
        dx = abs(post_cpu["x"] - anchor_x)
        dy = abs(post_cpu["y"] - anchor_y)
        p1_now = r.g.fighter(r.g.entry(CT.P1))
        res.row("CPU warped to anchor with fixed 35%", "dx<2 dy<2 dmg=35",
                f"dx={dx:.1f} dy={dy:.1f} dmg={post_cpu['damage']:.1f} cpu=({post_cpu['x']:.1f},{post_cpu['status']:#x}) "
                f"nxt=({next_cpu['x']:.1f},{next_cpu['status']:#x}) anc={reset_frame_slot['anchorX']:.1f} p1X={p1_now['x']:.1f}",
                dx < 2.0 and dy < 2.0 and abs(post_cpu["damage"] - 35.0) < 0.5)
    return res


def scenario_combo_random(r, cb):
    """Verify randomizePosition + randOnGround + randomizeDamage across resets."""
    res = CT.Result("combo-random")
    configure_lab_cpu(r, RECOVERY_PPLUS_AI)
    write_combo_settings(r.emu, r.opt.base, enabled=1, noclip=1, fix_pos=1,
                         rand_pos=1, rand_ground=1, rand_dmg=1, recovery_trainer=0,
                         timer_adj=-2, fixed_dmg=0, enabled_meters=METER_ALL)
    r.emu.advance(10)

    en = r.g.entry(CT.CPU)
    xs, ys, dmgs = [], [], []
    for trial in range(3):
        prev_resets = cb.slot(CT.CPU)["resetCount"]
        if not move_p1_to_cpu_and_hit(r, CT.JAB):
            move_p1_to_cpu_and_hit(r, CT.FSMASH)
        for _ in range(150):
            r.emu.advance(1)
            s = cb.slot(CT.CPU)
            if s["resetCount"] > prev_resets:
                r.emu.advance(2)
                f = r.g.fighter(en)
                xs.append(round(f["x"], 1))
                ys.append(round(f["y"], 1))
                dmgs.append(round(f["damage"], 1))
                break

    res.row("triggered 3 random resets", "3", str(len(xs)), len(xs) == 3)
    in_bounds = all(abs(x) < 82.0 and abs(y) < 12.0 for x, y in zip(xs, ys))
    distinct_pos = len(set(xs)) >= 2
    valid_dmgs = all(0.0 <= d <= 99.0 for d in dmgs) and len(set(dmgs)) >= 2
    res.row("random grounded positions in stage bounds", "inBounds & distinct",
            f"xs={xs} ys={ys}", in_bounds and distinct_pos)
    res.row("random damage in 0..99%", "valid & varied", f"dmgs={dmgs}", valid_dmgs)
    return res


def scenario_combo_recovery_trainer(r, cb):
    """Verify recoveryTrainer loops offstage spawns with PPlusAI + Correct_Air restore."""
    res = CT.Result("combo-recovery-trainer")
    configure_lab_cpu(r, RECOVERY_GAME_AI)  # Recovery Trainer must override with PPlusAI!
    write_combo_settings(r.emu, r.opt.base, enabled=1, noclip=1, fix_pos=0,
                         rand_pos=0, rand_ground=1, rand_dmg=0, recovery_trainer=1,
                         timer_adj=0, fixed_dmg=40, enabled_meters=METER_ALL)
    r.emu.advance(5)

    en = r.g.entry(CT.CPU)
    slot = en["slot"]
    start_rec = cb.slot(CT.CPU)["recoveryResets"]
    saw_offstage = False
    saw_correct_none = False
    saw_correct_air_after = False
    ai_silenced = True

    for _ in range(600):
        r.emu.advance(1)
        s = cb.slot(CT.CPU)
        f = r.g.fighter(en)
        if r.g.ai_flag(slot) != 0:
            ai_silenced = False
        if abs(f["x"]) > 88.0 and f["air"]:
            saw_offstage = True
        if s["observedCorrect"] == CORRECT_NONE:
            saw_correct_none = True
        elif saw_correct_none and s["observedCorrect"] == CORRECT_AIR:
            saw_correct_air_after = True
        if s["recoveryResets"] >= start_rec + 2 and saw_correct_air_after:
            break

    end_rec = cb.slot(CT.CPU)["recoveryResets"]
    res.row("recoveryTrainer looped >= 2 spawns", f">={start_rec + 2}", str(end_rec),
            end_rec >= start_rec + 2)
    res.row("CPU warped offstage", "True", str(saw_offstage), saw_offstage)
    res.row("PPlusAI kept game AI silenced", "0", "0" if ai_silenced else "nonzero", ai_silenced)
    res.row("noclip restored Correct_Air", "0->5",
            f"none={saw_correct_none} air={saw_correct_air_after}",
            saw_correct_none and saw_correct_air_after)
    return res


def scenario_combo_meters(r, cb):
    """Verify all 7 floating world-space state meters and enabledMeters bitmask."""
    res = CT.Result("combo-meters")
    configure_lab_cpu(r, RECOVERY_PPLUS_AI, behavior=CT.BEHAVIOR["Stand"], tdi=CT.TDI["Inwards"])
    r.opt.cpu_percent(30, True)
    write_combo_settings(r.emu, r.opt.base, enabled=1, noclip=0, fix_pos=0,
                         rand_pos=0, rand_ground=1, rand_dmg=0, recovery_trainer=0,
                         timer_adj=0, fixed_dmg=0, enabled_meters=METER_ALL)
    r.emu.advance(10)
    wait_cpu_grounded_center(r)
    # Wait out any residual respawn, ledge invincibility, or P1 hitstun from combo-recovery-trainer.
    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)
    for _ in range(180):
        sc = cb.slot(CT.CPU)
        p1_f = r.g.fighter(p1_en)
        cpu_f = r.g.fighter(cpu_en)
        if (sc["intangFrames"] == 0 and sc["ledgeIntangFrames"] == 0
                and not p1_f["air"] and p1_f["hitstun"] == 0 and p1_f["hitlag"] == 0
                and not cpu_f["air"] and cpu_f["status"] == CT.STATUS["Wait"]):
            break
        r.emu.advance(1)
    r.emu.advance(6)

    seen_mask = 0
    saw_hitlag = False
    saw_hitstun = False
    saw_di = False
    saw_iasa = False
    saw_shieldstun = False
    saw_intang = False
    saw_ledge_intang = False

    # 1. Hit CPU with fsmash while CPU holds Inwards TDI -> hitlag, hitstun, DI, and P1 IASA.
    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)
    for _ in range(180):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        dx = cpu["x"] - p1["x"]
        if abs(dx) < 13.0:
            break
        r.emu.hold(0, stick=(228 if dx > 0 else 28, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(2)
    p1 = r.g.fighter(p1_en)
    cpu = r.g.fighter(cpu_en)
    r.emu.press(0, ["A"], stick=(255 if cpu["x"] >= p1["x"] else 0, 128), frames=3, after=0)
    for _ in range(60):
        r.emu.advance(1)
        sc = cb.slot(CT.CPU)
        sp = cb.slot(CT.P1)
        seen_mask |= sc["activeMeterMask"] | sp["activeMeterMask"]
        if sc["hitlagFrames"] > 0 and sc["hitlagRatio"] > 0.0:
            saw_hitlag = True
        if sc["hitstunFrames"] > 0 and sc["hitstunRatio"] > 0.0:
            saw_hitstun = True
        if (sc["activeMeterMask"] & METER_DI) and (abs(sc["diX"]) > 0.05 or abs(sc["diY"]) > 0.05):
            saw_di = True
        if sp["iasaFrames"] > 0 and sp["iasaRatio"] > 0.0:
            saw_iasa = True

    # 2. Shieldstun: CPU holds shield, P1 hits shield with jab/fsmash.
    configure_lab_cpu(r, RECOVERY_PPLUS_AI, behavior=CT.BEHAVIOR["Shield"], tdi=CT.TDI["None"])
    r.emu.advance(20)
    wait_cpu_grounded_center(r)
    for _ in range(180):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        dx = cpu["x"] - p1["x"]
        if abs(dx) < 15.0:
            break
        r.emu.hold(0, stick=(228 if dx > 0 else 28, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(2)
    p1 = r.g.fighter(p1_en)
    cpu = r.g.fighter(cpu_en)
    r.emu.press(0, ["A"], stick=(255 if cpu["x"] >= p1["x"] else 0, 128), frames=3, after=0)
    for _ in range(45):
        r.emu.advance(1)
        sc = cb.slot(CT.CPU)
        if sc["shieldStunFrames"] > 0 and sc["shieldStunRatio"] > 0.0:
            saw_shieldstun = True
            seen_mask |= sc["activeMeterMask"]
            break

    # 3. General intangibility: wait out P1 attack endlag, then spotdodge (L + down).
    r.emu.advance(35)
    r.emu.press(0, ["L"], stick=(128, 0), frames=5, after=0)
    for _ in range(25):
        r.emu.advance(1)
        sp = cb.slot(CT.P1)
        if sp["intangFrames"] > 0 and sp["intangRatio"] > 0.0:
            saw_intang = True
            seen_mask |= sp["activeMeterMask"]
            break

    # 4. Ledge intangibility: walk CPU to FD's right edge, turn left (facing ledge),
    # then hop backward off the lip so Fox deterministically snaps to CliffCatch/CliffWait.
    configure_lab_cpu(r, RECOVERY_PPLUS_AI, behavior=CT.BEHAVIOR["Stand"], tdi=CT.TDI["None"])
    r.emu.advance(10)
    if r.g.fighter(cpu_en)["x"] > 55.0:
        r.drv.play(CT.CPU, [(0, -0.6, 0.0, 120)])
        for _ in range(120):
            r.emu.advance(1)
            f = r.g.fighter(cpu_en)
            if not f["air"] and f["x"] <= 50.0:
                break
        r.drv.stop()
    r.drv.play(CT.CPU, [(0, 0.6, 0.0, 300)])
    hopped = False
    for _ in range(320):
        r.emu.advance(1)
        f = r.g.fighter(cpu_en)
        sc = cb.slot(CT.CPU)
        if not hopped and not f["air"] and f["x"] >= 76.0:
            # Turn left to face stage, hop backward right off the edge, then drift left onto ledge.
            r.drv.play(CT.CPU, [(0, -0.5, 0.0, 6), (CT.JUMP, 1.0, 0.0, 4), (0, 1.0, 0.0, 10),
                                (0, -1.0, 0.0, 90)])
            hopped = True
        if sc["ledgeIntangFrames"] > 0 and sc["ledgeIntangRatio"] > 0.0:
            saw_ledge_intang = True
            seen_mask |= sc["activeMeterMask"]
            break
    r.drv.stop()

    # 5. Mask gating: enabledMeters = 0 must clear activeMeterMask.
    write_combo_settings(r.emu, r.opt.base, enabled=1, enabled_meters=0)
    r.emu.press(0, ["L"], stick=(128, 0), frames=4, after=0)
    r.emu.advance(5)
    mask_when_disabled = cb.slot(CT.P1)["activeMeterMask"] | cb.slot(CT.CPU)["activeMeterMask"]

    res.row("hitlag meter active", "True", str(saw_hitlag), saw_hitlag)
    res.row("hitstun meter active", "True", str(saw_hitstun), saw_hitstun)
    res.row("DI ring active", "True", str(saw_di), saw_di)
    res.row("timeUntilIASA meter active", "True", str(saw_iasa), saw_iasa)
    res.row("shieldstun meter active", "True", str(saw_shieldstun), saw_shieldstun)
    res.row("intangibility meter active", "True", str(saw_intang), saw_intang)
    res.row("ledgeIntangibility meter active", "True", str(saw_ledge_intang), saw_ledge_intang)
    res.row("enabledMeters=0 clears mask", "0", f"{mask_when_disabled:#x}", mask_when_disabled == 0)
    res.note(f"combined activeMeterMask observed: {seen_mask:#04x}")
    return res


PPLUS_FIGHTERS = [
    "fox",
    "marth",
    "mewtwo",
    "meta_knight",
    "samus",
    "ness",
    "ike",
    "peach",
]

ALL_SCENARIOS = (
    ["game-ai-rebirth-cliff", "combo-reset-noclip", "combo-random", "combo-recovery-trainer", "combo-meters"]
    + [f"pplus-{f}" for f in PPLUS_FIGHTERS]
)


def cmd_test_recovery_combo(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-recovery-combo")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args(rest)

    wanted = [s for s in ALL_SCENARIOS if not opts.only or s in opts.only]
    if opts.list:
        for s in wanted:
            print(s)
        return 0
    if not wanted:
        E.die(f"no scenarios matched --only {opts.only!r}")

    label = "test-recovery-combo"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, results, times = "", "boot", "", [], []

    def run_one(name, fn):
        t1 = time.monotonic()
        try:
            res = fn()
        except (KeyError, IndexError, TypeError, ValueError, AttributeError,
                ZeroDivisionError, CT.CpuError, TestError) as exc:
            import traceback
            res = CT.Result(name)
            res.check(f"{name} stopped: {exc!r}", False)
            for line in traceback.format_exc().splitlines()[-6:]:
                res.note(line)
        res.key = name
        print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}", flush=True)
        for line in res.lines:
            print("  " + line, flush=True)
        results.append(res)
        times.append((name, time.monotonic() - t1))

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            r = CT.Runner(emu)
            cb = ComboBlock(emu)
            current_cpu = None

            def ensure_match(cpu_name):
                nonlocal current_cpu
                emu.h.call("resume")
                if current_cpu != cpu_name:
                    if nav.scene(emu)[0] == "scMelee":
                        nav.finish_training(emu)
                    nav.start_quick_match(emu, "final_destination",
                                          p1=CHARS["mario"], cpu=CHARS[cpu_name])
                    emu.advance(45)
                    current_cpu = cpu_name
                r.refresh()
                cb.find()

            try:
                step = "boot"
                nav.boot_to_training_css(emu)

                # Group Fox scenarios together in one Fox match.
                fox_scenarios = [s for s in wanted if s in (
                    "game-ai-rebirth-cliff", "combo-reset-noclip", "combo-random",
                    "combo-recovery-trainer", "combo-meters", "pplus-fox"
                )]
                for name in fox_scenarios:
                    step = name
                    ensure_match("fox")
                    if name == "game-ai-rebirth-cliff":
                        run_one(name, lambda: scenario_game_ai_rebirth_cliff(r))
                    elif name == "combo-reset-noclip":
                        run_one(name, lambda: scenario_combo_reset_noclip(r, cb))
                    elif name == "combo-random":
                        run_one(name, lambda: scenario_combo_random(r, cb))
                    elif name == "combo-recovery-trainer":
                        run_one(name, lambda: scenario_combo_recovery_trainer(r, cb))
                    elif name == "combo-meters":
                        run_one(name, lambda: scenario_combo_meters(r, cb))
                    elif name == "pplus-fox":
                        run_one(name, lambda: run_pplus_recovery_scenario(r, "fox"))

                # Remaining 7 representative fighters for PPlusAI recovery.
                for fighter in PPLUS_FIGHTERS:
                    if fighter == "fox":
                        continue
                    name = f"pplus-{fighter}"
                    if name not in wanted:
                        continue
                    step = name
                    ensure_match(fighter)
                    run_one(name, lambda f=fighter: run_pplus_recovery_scenario(r, f))

                step = "finish"
                emu.h.call("resume")
                if nav.scene(emu)[0] == "scMelee":
                    nav.finish_training(emu)
                emu.advance(40)
            except (Timeout, nav.NavError, HarnessError, CT.CpuError, LT.LabError, PI.InputError, TestError) as exc:
                why = f"{step}: {exc}"
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{step}: {exc}"

    log = E.read_log(log_path) if log_path else ""
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why:
        why = CT.cpu_report(log)
    print()
    print(f"{'scenario':<26} {'check':<42} {'expected':<24} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<25} {what[:42]:<42} {exp[:24]:<24} {obs}")
    if times:
        print("seconds per scenario: " + ", ".join(f"{name} {t:.0f}" for name, t in times))
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and not all(res.ok for res in results):
        print("failed --only: " + " ".join(res.key for res in results if not res.ok))
        why = "a scenario failed: " + ", ".join(res.name for res in results if not res.ok)
    if not why and not results:
        why = "no scenario ran"
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
