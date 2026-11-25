"""Emulator tests for Stage 3F pure P+ backend upgrades
(run through emu.py; settings come from the environment the same way).

  test-stage3f REL [--only NAME ...] [--list]

Scenarios:
  - ledge-options: Fox CPU on FD's right ledge (Cliff_Wait 0x75) executes each
    of Neutral (Cliff_Climb 0x77), Roll (Cliff_Escape 0x78), Attack
    (Cliff_Attack 0x76), Jump (Cliff_Jump1..3 0x79..0x7B), Drop (Fall 0x0E),
    and respects counterDelay while preempting Game AI on the ledge.
  - clatter-escape: Donkey Kong P1 buries Fox CPU with Side-B (Headbutt ->
    Bury 0xC7 / Bury_Wait 0xC8); proves clatterEscape = Max breaks out in
    significantly fewer frames than clatterEscape = None from the exact same
    savestate.
  - adaptive-cpu: enables Adaptive CPU = Adaptive, feeds repeated P1 Attacks
    and asserts live TMAC RAM block detects p1DominantRps == Attack,
    p1Confidence >= 0.55, p1AttackProb > 0, fires adaptive bias
    (biasTriggers > 0), and resets cleanly on a lab savestate load.
  - multi-reversal: configures Multi-Slot Reversals with weighted Slot 2 +
    per-slot delay on On-Shield (primary Grab 0f vs Slot 2 Up Smash 8f) and
    On-Ledge (primary Neutral 0f vs Slot 2 Roll 10f), proving Slot 2 fires with
    its per-slot delay when enabled and primary Slot 1 fires when disabled.
  - tech-tracker: reads live TMTT RAM block with Fox P1 vs Mario CPU, verifies
    Fox JC Shine increments attempts/successes (no OSD when techTrackerOsd=0,
    posts OSD when techTrackerOsd=1), and verifies clean reset on savestate load.
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

CHARS = {
    "mario": (0x00, 0x00),
    "donkey_kong": (0x01, 0x01),
    "fox": (0x07, 0x07),
}

# LedgeOption enum (src/main/cpu/cpu_settings.h).
LEDGE_DEFAULT = 0
LEDGE_NEUTRAL = 1
LEDGE_ROLL = 2
LEDGE_ATTACK = 3
LEDGE_JUMP = 4
LEDGE_DROP = 5
LEDGE_RANDOM = 6

# AdaptiveMode enum (src/main/cpu/cpu_settings.h).
ADAPTIVE_OFF = 0
ADAPTIVE_TRACK = 1
ADAPTIVE_ACTIVE = 2

# Verified P+ 3.2.1 status IDs (lib/BrawlHeaders/Brawl/Include/ft/fighter.h).
ST_FALL = 0x0E
ST_CLIFF_CATCH_MOVE = 0x73
ST_CLIFF_CATCH = 0x74
ST_CLIFF_WAIT = 0x75
ST_CLIFF_ATTACK = 0x76
ST_CLIFF_CLIMB = 0x77
ST_CLIFF_ESCAPE = 0x78
ST_CLIFF_JUMP1 = 0x79
ST_CLIFF_JUMP2 = 0x7A
ST_CLIFF_JUMP3 = 0x7B
ST_BURY = 0xC7
ST_BURY_WAIT = 0xC8
ST_BURY_JUMP = 0xC9

# AdaptiveDebugBlock ("TMAC", src/extra/adaptive_cpu.cpp).
ADAPTIVE_MAGIC = 0x544D4143
ADAPTIVE_LOG = re.compile(r"adaptive: debug block at ([0-9a-f]{8})")
ADAPTIVE_FMT = ">IIIIIIffffII"
ADAPTIVE_SIZE = struct.calcsize(ADAPTIVE_FMT)

# TechTrackerBlock ("TMTT", src/extra/pplus_tech_tracker_core.h).
TECH_TRACKER_MAGIC = 0x544D5454
TECH_TRACKER_LOG = re.compile(r"tech_tracker: debug block at ([0-9a-f]{8})")
TECH_TRACKER_HDR_FMT = ">IIII"
TECH_TRACKER_SLOT_FMT = ">IIIiBBBB"
TECH_TRACKER_HDR_SIZE = struct.calcsize(TECH_TRACKER_HDR_FMT)
TECH_TRACKER_SLOT_SIZE = struct.calcsize(TECH_TRACKER_SLOT_FMT)
TECH_TRACKER_SLOTS = 4
TECH_TRACKER_SIZE = TECH_TRACKER_HDR_SIZE + TECH_TRACKER_SLOTS * TECH_TRACKER_SLOT_SIZE

# Multi-slot reversal and tech tracker CpuOptions tail offsets (offset 46..59).
REV_TAIL_OFFSET = 46
REV_ON_HIT, REV_ON_SHIELD, REV_ON_GETUP, REV_ON_LEDGE = 0, 1, 2, 3
SHIELD_GRAB = 1
SHIELD_UP_SMASH = 12
ST_GUARD = 0x1B
ST_GUARD_DAMAGE = 0x1D
ST_ATTACK_HI4_START = 0x30
ST_CATCH = 0x34

# Lab savestate probe block (src/main/savestate/trace.h).
SAVESTATE_BLOCK_LOG = re.compile(r"savestate: test block at ([0-9a-f]{8})")
SAVESTATE_SAVED_LOG = re.compile(r"savestate: saved frame (\d+)")
SAVESTATE_LOADED_LOG = re.compile(r"savestate: loaded frame (\d+)")


class TestError(RuntimeError):
    pass


class AdaptiveBlock:
    """Reads extra::AdaptiveDebugBlock directly from game RAM."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        found = ADAPTIVE_LOG.findall(self.emu.log_text())
        if not found:
            raise TestError("no adaptive debug block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != ADAPTIVE_MAGIC:
            raise TestError(f"bad adaptive debug magic at {self.addr:#x}")

    def read(self):
        raw = self.emu.read(self.addr, ADAPTIVE_SIZE)
        vals = struct.unpack(ADAPTIVE_FMT, raw)
        return {
            "magic": vals[0],
            "mode": vals[1],
            "samples": vals[2],
            "p1CurrentMov": vals[3],
            "p1RpsCount": vals[4],
            "p1DominantRps": vals[5],
            "p1Confidence": vals[6],
            "p1AttackProb": vals[7],
            "p1ShieldProb": vals[8],
            "p1GrabProb": vals[9],
            "biasAction": vals[10],
            "biasTriggers": vals[11],
        }


class TechTrackerBlock:
    """Reads extra::TechTrackerBlock ("TMTT") directly from game RAM."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        found = TECH_TRACKER_LOG.findall(self.emu.log_text())
        if not found:
            raise TestError("no tech_tracker debug block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != TECH_TRACKER_MAGIC:
            raise TestError(f"bad tech_tracker debug magic at {self.addr:#x}")

    def read(self):
        raw = self.emu.read(self.addr, TECH_TRACKER_SIZE)
        magic, version, frame, reset_count = struct.unpack_from(TECH_TRACKER_HDR_FMT, raw, 0)
        slots = []
        off = TECH_TRACKER_HDR_SIZE
        for _ in range(TECH_TRACKER_SLOTS):
            att, suc, last_id, last_f, ft_k, tech_k, act, osd = struct.unpack_from(TECH_TRACKER_SLOT_FMT, raw, off)
            slots.append({
                "attempts": att,
                "successes": suc,
                "lastTechId": last_id,
                "lastFrame": last_f,
                "ftKind": ft_k,
                "techKind": tech_k,
                "active": act,
                "osdPosted": osd,
            })
            off += TECH_TRACKER_SLOT_SIZE
        return {
            "magic": magic,
            "version": version,
            "frame": frame,
            "resetCount": reset_count,
            "slots": slots,
        }


def write_reversal_tech_opts(r, multi=0, actions=(0, 0, 0, 0), delays=(0, 0, 0, 0),
                         weights=(50, 50, 50, 50), tech_osd=0):
    """Writes bytes 46..59 of cpu::CpuOptions (multiReversal .. techTrackerOsd)."""
    payload = bytes([multi]) + bytes(actions) + bytes(delays) + bytes(weights) + bytes([tech_osd])
    r.emu.h.call("write_mem", addr=r.opt.base + CT.SET_CPU + REV_TAIL_OFFSET, hex=payload.hex())


def wait_fighter_grounded(r, player, max_frames=240):
    en = r.g.entry(player)
    for _ in range(max_frames):
        f = r.g.fighter(en)
        if not f["air"] and f["status"] not in CT.GONE and abs(f["x"]) < 80.0:
            return f
        r.emu.advance(1)
    return r.g.fighter(en)


def wait_cpu_grounded(r, max_frames=240):
    en = r.g.entry(CT.CPU)
    for _ in range(max_frames):
        f = r.g.fighter(en)
        if not f["air"] and f["status"] not in CT.GONE and abs(f["x"]) < 80.0:
            return f
        r.emu.advance(1)
    return r.g.fighter(en)


def grab_fd_right_ledge(r):
    """Walk Fox CPU to FD's right edge, hop off backward, and grab Cliff_Wait."""
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], ledgeOption=LEDGE_DEFAULT))
    r.emu.advance(8)
    wait_cpu_grounded(r)
    cpu_en = r.g.entry(CT.CPU)
    r.drv.play(CT.CPU, [(0, 0.6, 0.0, 300)])
    hopped = False
    grabbed = False
    for _ in range(360):
        r.emu.advance(1)
        f = r.g.fighter(cpu_en)
        if not hopped and not f["air"] and f["x"] >= 76.0:
            # Turn left to face stage, hop backward off right lip, drift left to ledge.
            r.drv.play(CT.CPU, [(0, -0.5, 0.0, 6), (CT.JUMP, 1.0, 0.0, 4),
                                (0, 1.0, 0.0, 10), (0, -1.0, 0.0, 90)])
            hopped = True
        if f["status"] in (ST_CLIFF_CATCH, ST_CLIFF_WAIT):
            grabbed = True
            break
    r.drv.stop()
    return grabbed, r.g.fighter(cpu_en)


def scenario_ledge_options(r):
    """Verify Neutral, Roll, Attack, Jump, Drop, and counterDelay on Cliff_Wait."""
    res = CT.Result("ledge-options")
    grabbed, f0 = grab_fd_right_ledge(r)
    res.check(f"Fox grabbed right ledge (st={f0['status']:#x}, x={f0['x']:.1f}, lr={f0['lr']:.1f})", grabbed)
    if not grabbed:
        return res

    r.states.save(r.emu, "stage3f-cliff")
    cpu_en = r.g.entry(CT.CPU)

    cases = [
        ("Neutral", LEDGE_NEUTRAL, {ST_CLIFF_CLIMB}, CT.RECOVERY["Game"], 0),
        ("Roll", LEDGE_ROLL, {ST_CLIFF_ESCAPE}, CT.RECOVERY["Game"], 0),
        ("Attack", LEDGE_ATTACK, {ST_CLIFF_ATTACK}, CT.RECOVERY["Game"], 0),
        ("Jump", LEDGE_JUMP, {ST_CLIFF_JUMP1, ST_CLIFF_JUMP2, ST_CLIFF_JUMP3}, CT.RECOVERY["Game"], 0),
        ("Drop", LEDGE_DROP, {ST_FALL}, CT.RECOVERY["Off"], 0),
        ("Neutral+delay10", LEDGE_NEUTRAL, {ST_CLIFF_CLIMB}, CT.RECOVERY["Game"], 10),
    ]

    for label, opt_val, expected_statuses, rec_mode, delay in cases:
        r.states.load(r.emu, "stage3f-cliff")
        r.g.mods.clear()
        r.opt.set(**CT.on(recovery=rec_mode, ledgeOption=opt_val, counterDelay=delay))
        r.emu.h.call("resume")
        cpu_en = r.g.entry(CT.CPU)
        seen = []
        cliff_wait_frames = 0
        matched_status = None
        for _ in range(90):
            r.emu.advance(1)
            st = r.g.fighter(cpu_en)["status"]
            if not seen or seen[-1] != st:
                seen.append(st)
            if st == ST_CLIFF_WAIT:
                cliff_wait_frames += 1
            if st in expected_statuses:
                matched_status = st
                break
        ok = matched_status is not None
        if delay > 0:
            ok = ok and cliff_wait_frames >= delay
        exp_str = "/".join(f"{s:#x}" for s in sorted(expected_statuses))
        if delay > 0:
            exp_str += f" after>={delay}f"
        obs_str = (f"{matched_status:#x} (wait={cliff_wait_frames}f)"
                   if matched_status is not None
                   else f"none (seen={[hex(s) for s in seen[:8]]})")
        res.row(f"ledge {label}", exp_str, obs_str, ok)

    return res


def scenario_clatter_escape(r):
    """Donkey Kong Side-B (Bury): Clatter Escape Max breaks out faster than None."""
    res = CT.Result("clatter-escape")
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], clatterEscape=CT.MASH["None"]))
    r.opt.cpu_percent(80, True)
    r.emu.advance(10)
    wait_cpu_grounded(r)

    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)

    # Walk DK (P1) right beside Fox CPU so Side-B (Headbutt) buries cleanly.
    for _ in range(180):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        dx = cpu["x"] - p1["x"]
        if abs(dx) < 14.0:
            break
        r.emu.hold(0, stick=(220 if dx > 0 else 36, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(4)

    r.states.save(r.emu, "stage3f-dk-near")

    def measure_bury(mash_mode):
        r.states.load(r.emu, "stage3f-dk-near")
        r.g.mods.clear()
        r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], clatterEscape=mash_mode))
        r.opt.cpu_percent(80, True)
        r.emu.h.call("resume")
        p1_en_l = r.g.entry(CT.P1)
        cpu_en_l = r.g.entry(CT.CPU)
        p1 = r.g.fighter(p1_en_l)
        cpu = r.g.fighter(cpu_en_l)
        sx = 255 if cpu["x"] >= p1["x"] else 0
        r.emu.press(0, ["B"], stick=(sx, 128), frames=4, after=0)
        entered = False
        exited = False
        bury_frames = 0
        exit_st = None
        seen = []
        for _ in range(360):
            r.emu.advance(1)
            st = r.g.fighter(cpu_en_l)["status"]
            if not seen or seen[-1] != st:
                seen.append(st)
            if st in (ST_BURY, ST_BURY_WAIT):
                entered = True
                bury_frames += 1
            elif entered:
                exited = True
                exit_st = st
                break
        return entered, exited, bury_frames, exit_st, seen

    ent_none, ext_none, frames_none, exit_none, seen_none = measure_bury(CT.MASH["None"])
    ent_max, ext_max, frames_max, exit_max, seen_max = measure_bury(CT.MASH["Perfect"])

    res.row("None entered & exited Bury", "entered & exited",
            f"ent={ent_none} ext={ext_none} frames={frames_none} exit={exit_none:#x}" if exit_none is not None
            else f"ent={ent_none} ext={ext_none} seen={[hex(s) for s in seen_none[:10]]}",
            ent_none and ext_none)
    res.row("Max entered & exited Bury", "entered & exited",
            f"ent={ent_max} ext={ext_max} frames={frames_max} exit={exit_max:#x}" if exit_max is not None
            else f"ent={ent_max} ext={ext_max} seen={[hex(s) for s in seen_max[:10]]}",
            ent_max and ext_max)
    faster = ent_none and ext_none and ent_max and ext_max and (frames_max <= frames_none - 10)
    res.row("Max breaks out faster than None", f"max <= none - 10",
            f"none={frames_none}f vs max={frames_max}f (saved {frames_none - frames_max}f)",
            faster)
    return res


def scenario_adaptive_cpu(r, ab):
    """Verify live TMAC tracking, Attack habit detection, adaptive bias, and savestate reset."""
    res = CT.Result("adaptive-cpu")
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], adaptiveMode=ADAPTIVE_OFF))
    r.emu.advance(6)
    wait_cpu_grounded(r)

    off_snap = ab.read()
    res.row("Off mode reports mode=0", "mode=0", f"mode={off_snap['mode']}", off_snap["mode"] == ADAPTIVE_OFF)

    # Move P1 close enough to CPU for neutral adaptive response, but keep enough
    # room so P1 can cleanly spam jabs without knocking CPU far away.
    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)
    for _ in range(180):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        dx = cpu["x"] - p1["x"]
        if abs(dx) < 28.0:
            break
        r.emu.hold(0, stick=(220 if dx > 0 else 36, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(6)

    # Save a lab savestate (D-pad Right held >= 12 frames) so we can verify reset on load!
    saves_before = len(SAVESTATE_SAVED_LOG.findall(r.emu.log_text()))
    r.emu.hold(0, buttons=["DRIGHT"])
    r.emu.advance(24)
    r.emu.neutral(0)
    r.emu.advance(6)
    saves_after = len(SAVESTATE_SAVED_LOG.findall(r.emu.log_text()))
    res.row("lab savestate saved via D-Right", f">{saves_before}", str(saves_after), saves_after > saves_before)

    # Enable Adaptive CPU = Adaptive (2) and feed 10 repeated P1 Attacks (Jabs).
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], adaptiveMode=ADAPTIVE_ACTIVE,
                      intangible=1, counterGround=0, counterShield=0))
    r.emu.advance(4)

    for _ in range(10):
        r.emu.press(0, ["A"], frames=3, after=0)
        r.emu.advance(24)

    snap = ab.read()
    res.row("TMAC mode=2 & samples>0", "mode=2 samples>0",
            f"mode={snap['mode']} samples={snap['samples']}",
            snap["mode"] == ADAPTIVE_ACTIVE and snap["samples"] > 0)
    res.row("P1 Attack habit detected", "dom=1 conf>=0.55 rps>=6",
            f"dom={snap['p1DominantRps']} conf={snap['p1Confidence']:.3f} rps={snap['p1RpsCount']}",
            snap["p1DominantRps"] == 1 and snap["p1Confidence"] >= 0.55 and snap["p1RpsCount"] >= 6)
    res.row("MovementTracker p1AttackProb > 0", ">0.0",
            f"atk={snap['p1AttackProb']:.3f} shd={snap['p1ShieldProb']:.3f} grab={snap['p1GrabProb']:.3f}",
            snap["p1AttackProb"] > 0.0)
    res.row("adaptive bias fired", "triggers>0 action!=0",
            f"triggers={snap['biasTriggers']} action={snap['biasAction']}",
            snap["biasTriggers"] > 0 and snap["biasAction"] != 0)

    # Load lab savestate via D-pad Left and verify TMAC resets cleanly!
    loads_before = len(SAVESTATE_LOADED_LOG.findall(r.emu.log_text()))
    r.emu.press(0, ["DLEFT"], frames=6, after=0)
    r.emu.advance(4)
    loads_after = len(SAVESTATE_LOADED_LOG.findall(r.emu.log_text()))
    after_load = ab.read()
    res.row("lab savestate loaded via D-Left", f">{loads_before}", str(loads_after), loads_after > loads_before)
    res.row("TMAC reset on savestate load", "rps=0 dom=0 trig=0",
            f"rps={after_load['p1RpsCount']} dom={after_load['p1DominantRps']} "
            f"conf={after_load['p1Confidence']:.3f} trig={after_load['biasTriggers']}",
            after_load["p1RpsCount"] == 0 and after_load["p1DominantRps"] == 0
            and after_load["biasTriggers"] == 0)

    # Leave Adaptive mode off cleanly.
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], adaptiveMode=ADAPTIVE_OFF))
    return res


def scenario_multi_reversal(r):
    """Verify Multi-Slot Reversals + per-slot delay on On-Shield and On-Ledge."""
    res = CT.Result("multi-reversal")
    write_reversal_tech_opts(r, multi=0)
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], behavior=CT.BEHAVIOR["Stand"],
                      infiniteShields=CT.SHIELDS["On"], counterShield=SHIELD_GRAB, counterDelay=0))
    r.emu.advance(15)
    wait_fighter_grounded(r, CT.CPU)
    wait_fighter_grounded(r, CT.P1)

    p1_en = r.g.entry(CT.P1)
    cpu_en = r.g.entry(CT.CPU)

    # Walk Fox CPU back near center stage if a previous scenario left it near the ledge.
    cpu = r.g.fighter(cpu_en)
    if abs(cpu["x"]) > 35.0:
        step_x = -0.7 if cpu["x"] > 0 else 0.7
        r.drv.play(CT.CPU, [(0, step_x, 0.0, 240)])
        for _ in range(240):
            r.emu.advance(1)
            if abs(r.g.fighter(cpu_en)["x"]) < 25.0:
                break
        r.drv.stop()
        r.emu.advance(8)

    # Walk (not dash) Mario right next to Fox so Mario never slides past Fox.
    for _ in range(240):
        p1 = r.g.fighter(p1_en)
        cpu = r.g.fighter(cpu_en)
        target_x = cpu["x"] - 8.0
        dx = target_x - p1["x"]
        if abs(dx) < 1.5:
            break
        r.emu.hold(0, stick=(180 if dx > 0 else 76, 128))
        r.emu.advance(1)
    r.emu.neutral(0)
    r.emu.advance(10)
    # Ensure Mario faces Fox, then turn on full-health Shield and wait for Guard (0x1B).
    p1 = r.g.fighter(p1_en)
    cpu = r.g.fighter(cpu_en)
    want_lr = 1.0 if cpu["x"] >= p1["x"] else -1.0
    if (p1["lr"] > 0) != (want_lr > 0):
        r.emu.hold(0, stick=(185 if want_lr > 0 else 71, 128))
        r.emu.advance(3)
        r.emu.neutral(0)
        r.emu.advance(8)
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], behavior=CT.BEHAVIOR["Shield"],
                      infiniteShields=CT.SHIELDS["On"], shieldHealth=100,
                      counterShield=SHIELD_GRAB, counterDelay=0))
    for _ in range(40):
        r.emu.advance(1)
        if r.g.fighter(cpu_en)["status"] == ST_GUARD:
            break
    r.states.save(r.emu, "w3-shield-near")

    def run_shield_trial(multi_on):
        r.states.load(r.emu, "w3-shield-near")
        r.g.mods.clear()
        r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], behavior=CT.BEHAVIOR["Shield"],
                          infiniteShields=CT.SHIELDS["On"], shieldHealth=100,
                          counterShield=SHIELD_GRAB, counterDelay=0))
        write_reversal_tech_opts(
            r,
            multi=1 if multi_on else 0,
            actions=(0, SHIELD_UP_SMASH, 0, 0),
            delays=(0, 8, 0, 0),
            weights=(50, 100, 50, 50),
            tech_osd=0,
        )
        r.emu.h.call("resume")
        p1_en_l = r.g.entry(CT.P1)
        cpu_en_l = r.g.entry(CT.CPU)
        for _ in range(20):
            if r.g.fighter(cpu_en_l)["status"] == ST_GUARD:
                break
            r.emu.advance(1)
        p1_0 = r.g.fighter(p1_en_l)
        cpu_0 = r.g.fighter(cpu_en_l)
        r.emu.hold(0, buttons=["A"])
        r.emu.advance(3)
        r.emu.neutral(0)
        saw_stun = False
        post_guard = 0
        hit_status = None
        seen = []
        for _ in range(70):
            r.emu.advance(1)
            st = r.g.fighter(cpu_en_l)["status"]
            if not seen or seen[-1] != st:
                seen.append(st)
            if st == ST_GUARD_DAMAGE:
                saw_stun = True
            elif saw_stun:
                if st == ST_GUARD:
                    post_guard += 1
                elif st in (ST_CATCH, ST_ATTACK_HI4_START):
                    hit_status = st
                    break
        diag = f"p1=({p1_0['x']:.1f},lr={p1_0['lr']:.0f},st={p1_0['status']:#x}) cpu=({cpu_0['x']:.1f}) seen={[hex(s) for s in seen[:8]]}"
        return saw_stun, hit_status, post_guard, diag

    stun_off, st_off, wait_off, diag_off = run_shield_trial(False)
    res.row("shield multi=0 fires primary Grab", "0x34",
            f"{st_off:#x} (wait={wait_off}f)" if st_off is not None else f"none ({diag_off})",
            stun_off and st_off == ST_CATCH)

    stun_on, st_on, wait_on, diag_on = run_shield_trial(True)
    res.row("shield multi=1 fires Slot2 UpSmash+8f", "0x30 after>=8f",
            f"{st_on:#x} (wait={wait_on}f)" if st_on is not None else f"none ({diag_on})",
            stun_on and st_on == ST_ATTACK_HI4_START and wait_on >= 8)

    # On-Ledge: reload clean onstage state before walking Fox to FD right ledge.
    r.states.load(r.emu, "w3-shield-near")
    r.g.mods.clear()
    write_reversal_tech_opts(r, multi=0)
    r.emu.h.call("resume")
    grabbed, f0 = grab_fd_right_ledge(r)
    res.check(f"Fox grabbed right ledge (st={f0['status']:#x}, x={f0['x']:.1f})", grabbed)
    if not grabbed:
        return res
    r.states.save(r.emu, "w3-cliff")

    def run_ledge_trial(multi_on):
        r.states.load(r.emu, "w3-cliff")
        r.g.mods.clear()
        r.opt.set(**CT.on(recovery=CT.RECOVERY["Game"], ledgeOption=LEDGE_NEUTRAL, counterDelay=0))
        write_reversal_tech_opts(
            r,
            multi=1 if multi_on else 0,
            actions=(0, 0, 0, LEDGE_ROLL),
            delays=(0, 0, 0, 10),
            weights=(50, 50, 50, 100),
            tech_osd=0,
        )
        r.emu.h.call("resume")
        cpu_en_l = r.g.entry(CT.CPU)
        wait_frames = 0
        got = None
        seen = []
        for _ in range(90):
            r.emu.advance(1)
            st = r.g.fighter(cpu_en_l)["status"]
            if not seen or seen[-1] != st:
                seen.append(st)
            if st == ST_CLIFF_WAIT:
                wait_frames += 1
            if st in (ST_CLIFF_CLIMB, ST_CLIFF_ESCAPE):
                got = st
                break
        return got, wait_frames, seen

    l_off, lw_off, lseen_off = run_ledge_trial(False)
    res.row("ledge multi=0 fires primary Neutral", "0x77",
            f"{l_off:#x} (wait={lw_off}f)" if l_off is not None else f"none (seen={[hex(s) for s in lseen_off[:8]]})",
            l_off == ST_CLIFF_CLIMB and lw_off < 8)

    l_on, lw_on, lseen_on = run_ledge_trial(True)
    res.row("ledge multi=1 fires Slot2 Roll+10f", "0x78 after>=10f",
            f"{l_on:#x} (wait={lw_on}f)" if l_on is not None else f"none (seen={[hex(s) for s in lseen_on[:8]]})",
            l_on == ST_CLIFF_ESCAPE and lw_on >= 10)

    write_reversal_tech_opts(r, multi=0)
    return res


def scenario_tech_tracker(r, tt):
    """Verify live 44-fighter TMTT tracking (Fox JC Shine), OSD toggle, and savestate reset."""
    res = CT.Result("tech-tracker")
    write_reversal_tech_opts(r, multi=0, tech_osd=0)
    r.opt.set(**CT.on(recovery=CT.RECOVERY["Off"], behavior=CT.BEHAVIOR["Stand"]))
    r.emu.advance(8)
    wait_fighter_grounded(r, CT.P1)

    snap0 = tt.read()
    s0 = snap0["slots"][0]
    s1 = snap0["slots"][1]
    res.row("TMTT header + P1 Fox / P2 Mario", "TMTT v1 ft=0x06/0x00",
            f"magic={snap0['magic']:#x} v={snap0['version']} p1=({s0['ftKind']:#x},t={s0['techKind']},a={s0['active']}) "
            f"p2=({s1['ftKind']:#x},t={s1['techKind']},a={s1['active']})",
            snap0["magic"] == TECH_TRACKER_MAGIC and snap0["version"] == 1
            and s0["active"] == 1 and s0["ftKind"] == 0x06 and s0["techKind"] == 1
            and s1["active"] == 1 and s1["ftKind"] == 0x00 and s1["techKind"] == 5)

    # Save a lab savestate so we can verify TMTT resets cleanly on savestate load.
    saves_before = len(SAVESTATE_SAVED_LOG.findall(r.emu.log_text()))
    r.emu.hold(0, buttons=["DRIGHT"])
    r.emu.advance(24)
    r.emu.neutral(0)
    r.emu.advance(6)
    saves_after = len(SAVESTATE_SAVED_LOG.findall(r.emu.log_text()))
    res.row("lab savestate saved via D-Right", f">{saves_before}", str(saves_after), saves_after > saves_before)

    def perform_jc_shine():
        wait_fighter_grounded(r, CT.P1)
        r.emu.advance(4)
        # Hold Down + press B to enter SpecialLw (0x115), then press X after 5f to jump-cancel.
        r.emu.press(0, ["B"], stick=(128, 0), frames=3, after=0)
        r.emu.advance(4)
        r.emu.press(0, ["X"], stick=(128, 128), frames=3, after=0)
        r.emu.advance(36)
        wait_fighter_grounded(r, CT.P1)

    # First JC Shine with techTrackerOsd = 0 -> counter increments, osdPosted stays 0.
    write_reversal_tech_opts(r, multi=0, tech_osd=0)
    perform_jc_shine()
    snap1 = tt.read()
    p1_1 = snap1["slots"][0]
    res.row("Fox JC Shine tracked (OSD off)", "att>=1 suc>=1 osd=0",
            f"att={p1_1['attempts']} suc={p1_1['successes']} id={p1_1['lastTechId']} "
            f"f={p1_1['lastFrame']} osd={p1_1['osdPosted']}",
            p1_1["attempts"] >= 1 and p1_1["successes"] >= 1 and p1_1["lastTechId"] > 0
            and p1_1["lastFrame"] > 0 and p1_1["osdPosted"] == 0)

    # Second JC Shine with techTrackerOsd = 1 -> counter increments AND osdPosted >= 1.
    write_reversal_tech_opts(r, multi=0, tech_osd=1)
    perform_jc_shine()
    snap2 = tt.read()
    p1_2 = snap2["slots"][0]
    res.row("Fox JC Shine posts OSD (OSD on)", f"suc>{p1_1['successes']} osd>=1",
            f"att={p1_2['attempts']} suc={p1_2['successes']} osd={p1_2['osdPosted']}",
            p1_2["successes"] > p1_1["successes"] and p1_2["osdPosted"] >= 1)

    # Load lab savestate via D-pad Left -> TMTT resets cleanly.
    resets_before = snap2["resetCount"]
    loads_before = len(SAVESTATE_LOADED_LOG.findall(r.emu.log_text()))
    r.emu.press(0, ["DLEFT"], frames=6, after=0)
    r.emu.advance(4)
    loads_after = len(SAVESTATE_LOADED_LOG.findall(r.emu.log_text()))
    snap3 = tt.read()
    p1_3 = snap3["slots"][0]
    res.row("lab savestate loaded via D-Left", f">{loads_before}", str(loads_after), loads_after > loads_before)
    res.row("TMTT reset on savestate load", f"resets>{resets_before} suc=0 osd=0",
            f"resets={snap3['resetCount']} att={p1_3['attempts']} suc={p1_3['successes']} osd={p1_3['osdPosted']}",
            snap3["resetCount"] > resets_before and p1_3["attempts"] == 0
            and p1_3["successes"] == 0 and p1_3["osdPosted"] == 0)

    write_reversal_tech_opts(r, multi=0, tech_osd=0)
    return res


ALL_SCENARIOS = ["ledge-options", "clatter-escape", "adaptive-cpu", "multi-reversal", "tech-tracker"]


def cmd_test_stage3f(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage3f")
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

    label = "test-stage3f"
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
            ab = AdaptiveBlock(emu)
            tt = TechTrackerBlock(emu)
            current_pair = None

            def ensure_match(p1_name, cpu_name):
                nonlocal current_pair
                emu.h.call("resume")
                if current_pair != (p1_name, cpu_name):
                    if nav.scene(emu)[0] == "scMelee":
                        nav.finish_training(emu)
                    nav.start_quick_match(emu, "final_destination",
                                          p1=CHARS[p1_name], cpu=CHARS[cpu_name])
                    emu.advance(45)
                    current_pair = (p1_name, cpu_name)
                r.refresh()
                ab.find()
                tt.find()

            try:
                step = "boot"
                nav.boot_to_training_css(emu)

                for name in wanted:
                    step = name
                    if name == "ledge-options":
                        ensure_match("mario", "fox")
                        run_one(name, lambda: scenario_ledge_options(r))
                    elif name == "clatter-escape":
                        ensure_match("donkey_kong", "fox")
                        run_one(name, lambda: scenario_clatter_escape(r))
                    elif name == "adaptive-cpu":
                        ensure_match("mario", "fox")
                        run_one(name, lambda: scenario_adaptive_cpu(r, ab))
                    elif name == "multi-reversal":
                        ensure_match("mario", "fox")
                        run_one(name, lambda: scenario_multi_reversal(r))
                    elif name == "tech-tracker":
                        ensure_match("fox", "mario")
                        run_one(name, lambda: scenario_tech_tracker(r, tt))

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
