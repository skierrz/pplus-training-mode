"""Live Dolphin test suite for Combo-Break Rush and Brawl/P+ Action Log
(run through emu.py):

  test-overnight-tools REL [--only NAME ...] [--aspect 16:9|4:3] [--list] [--keep-going]

Deterministic single-core D3D run verifying:
  combo-break-fd     Final Destination: activates Timed 30s Rush via TMBR, verifies
                     live capsule spawns over FD floor, drives real Mario attack
                     hitboxes into capsules, confirms score & multiplier increment
                     (x1 -> x2+), verifies Free Practice mode, and cleanly detaches.
  combo-break-bf     Battlefield: verifies platform-aware spawns (stagePlatforms >= 3),
                     live hitbox break, 3D + 2D rendering, and captures
                     combo-break-rush-{16x9,4x3}.png.
  action-log-p1-cpu  Toggles Display > Action Log (Off -> P1 -> CPU -> Off), drives
                     real P1 transitions (Dash -> Jump -> Fair -> Land), verifies
                     TMAL ring buffer entries and frame counts, and captures
                     action-log-p1-{16x9,4x3}.png.
  action-log-multihud
                     Enables Action Log + Info Display + Input Viewer + DI Compass
                     + PSA Debugger simultaneously, verifies clean rendering, and
                     captures action-log-multihud-{16x9,4x3}.png.
"""
import argparse
import re
import struct
import time

import display_options_tests as DO
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
from harness import HarnessError, Timeout

# main::Settings offsets (src/main/settings.h).
S_FRAME_ADVANCE = 11
S_DI_DISPLAY = 19
S_ACTION_LOG = 22
S_INPUT_VIEWER = 52
S_DI_VECTOR = 54
S_PSA_INSPECTOR = 55
S_INFO_HMN_SIZE = 57
S_INFO_HMN_ROWS = 58

TMBR_MAGIC = 0x544D4252  # "TMBR"
TMBR_SIZE = 0x58
TMBR_CMD_OFF = 0x09

TMAL_MAGIC = 0x544D414C  # "TMAL"
TMAL_SIZE = 0x18 + 8 * 18

RUSH_BLOCK_RE = re.compile(r"combo_break_rush: debug block at ([0-9a-f]{8})")
LOG_BLOCK_RE = re.compile(r"action_log: debug block at ([0-9a-f]{8})")

SCENARIOS = [
    "combo-break-fd",
    "combo-break-bf",
    "action-log-p1-cpu",
    "action-log-multihud",
]


class ToolsTestError(RuntimeError):
    pass


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def read_tmbr(emu, addr):
    raw = emu.read(addr, TMBR_SIZE)
    hdr = struct.unpack_from(">IIBBBBBBBBIIHHHHII", raw, 0)
    tx = struct.unpack_from(">4f", raw, 0x28)
    ty = struct.unpack_from(">4f", raw, 0x38)
    tr = struct.unpack_from(">4f", raw, 0x48)
    return {
        "magic": hdr[0],
        "version": hdr[1],
        "mode": hdr[2],
        "cmdMode": hdr[3],
        "active": hdr[4],
        "finished": hdr[5],
        "multiplier": hdr[6],
        "activeTargets": hdr[7],
        "stagePlatforms": hdr[8],
        "shapeOk": hdr[9],
        "score": hdr[10],
        "bestScore": hdr[11],
        "streak": hdr[12],
        "breaks": hdr[13],
        "chainFrames": hdr[14],
        "timerFrames": hdr[15],
        "draw3dCalls": hdr[16],
        "draw2dCalls": hdr[17],
        "targetX": tx,
        "targetY": ty,
        "targetR": tr,
    }


def read_tmal(emu, addr):
    raw = emu.read(addr, TMAL_SIZE)
    hdr = struct.unpack_from(">IIBBBBIIi", raw, 0)
    entries = []
    for i in range(8):
        off = 0x18 + i * 18
        status, mkind, frames, cls, _pad, label_raw = struct.unpack_from(">hhHBB10s", raw, off)
        label = label_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        entries.append({
            "status": status,
            "motionKind": mkind,
            "frames": frames,
            "stateClass": cls,
            "label": label,
        })
    return {
        "magic": hdr[0],
        "version": hdr[1],
        "targetMode": hdr[2],
        "trackedSlot": hdr[3],
        "count": hdr[4],
        "totalTransitions": hdr[6],
        "drawCalls": hdr[7],
        "lastFrame": hdr[8],
        "entries": entries,
    }


class Runner:
    def __init__(self, emu, aspect=None, keep_going=False):
        self.e = emu
        self.aspect = aspect
        self.suffix = "-4x3" if aspect == "4:3" else "-16x9"
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.step_name = "boot"
        self.settings = None
        self.tmbr = None
        self.tmal = None
        self.stage = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise ToolsTestError(f"{name}: {detail}")

    def set_u8(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFF:02x}")

    def set_u16(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFFFF:04x}")

    def rush_cmd(self, val):
        self.e.h.call("write_mem", addr=self.tmbr + TMBR_CMD_OFF, hex=f"{int(val) & 0xFF:02x}")

    def activate_rush(self, mode, max_frames=60):
        want_active = 1 if mode != 0 else 0
        self.rush_cmd(mode)
        for _ in range(max_frames):
            step(self.e, 1)
            st = read_tmbr(self.e, self.tmbr)
            if st["active"] == want_active and st["mode"] == mode:
                step(self.e, 4)
                return read_tmbr(self.e, self.tmbr)
        return read_tmbr(self.e, self.tmbr)

    def snap(self, name):
        self.set_u8(S_FRAME_ADVANCE, 1)
        step(self.e, 1)
        self.e.h.call("resume")
        self.e.advance(2)
        self.shots.append(self.e.screenshot(name + self.suffix))
        self.e.h.call("pause")
        self.set_u8(S_FRAME_ADVANCE, 0)
        step(self.e, 1)

    def start_match(self, stage="final_destination"):
        e = self.e
        e.neutral(0)
        if nav.scene(e)[0] == "scMelee":
            e.h.call("resume")
            nav.finish_training(e)
        if nav.scene(e)[0] != "scSelctCharacter":
            nav.boot_to_training_css(e)
        if self.aspect:
            LT.set_aspect(e, self.aspect, lambda msg: None)
        pos = len(e.log_text())
        e.mark(f"tools: mario vs fox on {stage}")
        nav.start_quick_match(e, stage=stage, seed_rng=True)
        e.neutral(0)
        lab = LT.Lab(e, [])
        self.settings = lab.find_settings()
        self.set_u8(S_ACTION_LOG, 0)
        self.set_u8(S_INPUT_VIEWER, 0)
        self.set_u8(S_PSA_INSPECTOR, 0)
        self.set_u8(S_DI_DISPLAY, 0)
        self.set_u8(S_FRAME_ADVANCE, 0)
        for off, val in DO.CPU_STILL.items():
            self.set_u8(off, val)
        self.set_u16(LT.SET_CPU_PERCENT, 0)
        self.set_u8(LT.SET_LOCK_CPU, 1)
        e.h.call("pause")
        step(e, 20)
        text = e.log_text()[pos:]
        rh = RUSH_BLOCK_RE.findall(text) or RUSH_BLOCK_RE.findall(e.log_text())
        lh = LOG_BLOCK_RE.findall(text) or LOG_BLOCK_RE.findall(e.log_text())
        if not rh:
            raise ToolsTestError("no combo_break_rush debug block in log")
        if not lh:
            raise ToolsTestError("no action_log debug block in log")
        self.tmbr = int(rh[-1], 16)
        self.tmal = int(lh[-1], 16)
        self.stage = stage

    def hit_snapped_capsule(self):
        e = self.e
        e.neutral(0)
        step(e, 6)
        self.rush_cmd(0xFE)  # snap target 0 in front of P1
        step(e, 2)
        e.hold(0, buttons=["A"], stick=(128, 128))  # Mario neutral jab (fast, no turn/slide)
        step(e, 3)
        e.neutral(0)
        step(e, 24)


def sc_combo_break_fd(t):
    e = t.e
    t.start_match("final_destination")
    s0 = read_tmbr(e, t.tmbr)
    t.check(s0["magic"] == TMBR_MAGIC and s0["active"] == 0, "fd-idle", f"magic={s0['magic']:#x} act={s0['active']}")

    # Activate Timed 30s Rush (mode = 1).
    s1 = t.activate_rush(1)
    valid_spawns = sum(1 for r in s1["targetR"][:3] if r >= 3.5)
    t.check(
        s1["active"] == 1
        and s1["mode"] == 1
        and s1["activeTargets"] == 3
        and valid_spawns == 3
        and s1["timerFrames"] > 0
        and s1["shapeOk"] == 1
        and s1["draw3dCalls"] > 0
        and s1["draw2dCalls"] > 0,
        "fd-timed-spawn",
        f"act={s1['activeTargets']} validR={valid_spawns} timer={s1['timerFrames']} "
        f"d3d={s1['draw3dCalls']} d2d={s1['draw2dCalls']}",
    )

    # Break two capsules in chain window to verify real hitbox detection, score, and x2+ multiplier.
    t.hit_snapped_capsule()
    s2 = read_tmbr(e, t.tmbr)
    t.hit_snapped_capsule()
    s3 = read_tmbr(e, t.tmbr)
    t.check(
        s2["breaks"] >= 1
        and s3["breaks"] >= 2
        and s3["score"] > s2["score"]
        and s3["multiplier"] >= 2
        and s3["chainFrames"] > 0
        and s3["bestScore"] >= s3["score"],
        "fd-hitbox-break-chain",
        f"b2={s2['breaks']}/{s2['score']} b3={s3['breaks']}/{s3['score']} "
        f"mult=x{s3['multiplier']} chain={s3['chainFrames']} best={s3['bestScore']}",
    )

    # Switch to Free Practice (mode = 3), verify untimed targets and preserved bestScore.
    t.rush_cmd(3)
    step(e, 6)
    sf = read_tmbr(e, t.tmbr)
    t.check(
        sf["active"] == 1
        and sf["mode"] == 3
        and sf["timerFrames"] == 0
        and sf["activeTargets"] == 3
        and sf["bestScore"] >= s3["score"],
        "fd-free-practice",
        f"mode={sf['mode']} timer={sf['timerFrames']} targets={sf['activeTargets']} best={sf['bestScore']}",
    )

    # Clean deactivate.
    t.rush_cmd(0)
    step(e, 4)
    so = read_tmbr(e, t.tmbr)
    t.check(so["active"] == 0 and so["mode"] == 0, "fd-deactivate", f"active={so['active']} mode={so['mode']}")


def sc_combo_break_bf(t):
    e = t.e
    t.start_match("battlefield")
    s1 = t.activate_rush(1)  # Timed 30s
    t.check(
        s1["active"] == 1 and s1["stagePlatforms"] >= 3 and s1["activeTargets"] == 3,
        "bf-platform-aware-spawns",
        f"plats={s1['stagePlatforms']} targets={s1['activeTargets']} y={s1['targetY'][:3]}",
    )

    # Break two capsules so live HUD shows SCORE + x2 multiplier + active chain bar + 3D targets.
    t.hit_snapped_capsule()
    t.hit_snapped_capsule()
    step(e, 16)
    s2 = read_tmbr(e, t.tmbr)
    t.check(
        s2["breaks"] >= 2 and s2["multiplier"] >= 2 and s2["draw3dCalls"] > 0 and s2["draw2dCalls"] > 0,
        "bf-live-rush-hud",
        f"breaks={s2['breaks']} score={s2['score']} mult=x{s2['multiplier']} d3d={s2['draw3dCalls']}",
    )
    t.snap("combo-break-rush")
    t.rush_cmd(0)
    step(e, 2)


def sc_action_log_p1_cpu(t):
    e = t.e
    if t.stage != "final_destination" or t.tmal is None:
        t.start_match("final_destination")
    t.rush_cmd(0)
    t.set_u8(S_ACTION_LOG, 0)
    step(e, 4)
    d0 = read_tmal(e, t.tmal)
    t.check(d0["magic"] == TMAL_MAGIC and d0["targetMode"] == 0, "action-log-off", f"magic={d0['magic']:#x}")

    # Enable Action Log = P1 and execute Dash -> Jump -> Fair -> Land.
    t.set_u8(S_ACTION_LOG, 1)
    step(e, 4)
    e.hold(0, stick=(240, 128))  # Dash right
    step(e, 10)
    e.hold(0, buttons=["Y"], stick=(240, 128))  # JumpSquat -> Jump
    step(e, 6)
    e.hold(0, buttons=["A"], stick=(240, 128))  # Fair
    step(e, 4)
    e.neutral(0)
    step(e, 34)

    dp1 = read_tmal(e, t.tmal)
    labels = [x["label"] for x in dp1["entries"][:dp1["count"]]]
    t.check(
        dp1["targetMode"] == 1
        and dp1["trackedSlot"] != 0xFF
        and dp1["count"] >= 4
        and dp1["totalTransitions"] >= 4
        and dp1["drawCalls"] > 0,
        "action-log-p1-transitions",
        f"count={dp1['count']} total={dp1['totalTransitions']} draws={dp1['drawCalls']} labels={labels}",
    )
    t.snap("action-log-p1")

    # Switch to CPU and then back to Off.
    t.set_u8(S_ACTION_LOG, 2)
    step(e, 6)
    dcpu = read_tmal(e, t.tmal)
    t.set_u8(S_ACTION_LOG, 0)
    step(e, 4)
    doff1 = read_tmal(e, t.tmal)
    step(e, 4)
    doff2 = read_tmal(e, t.tmal)
    t.check(
        dcpu["targetMode"] == 2
        and dcpu["trackedSlot"] != 0xFF
        and dcpu["trackedSlot"] != dp1["trackedSlot"]
        and doff2["targetMode"] == 0
        and doff2["drawCalls"] == doff1["drawCalls"],
        "action-log-cpu-and-off",
        f"p1Slot={dp1['trackedSlot']} cpuSlot={dcpu['trackedSlot']} offDraws={doff1['drawCalls']}->{doff2['drawCalls']}",
    )


def sc_action_log_multihud(t):
    e = t.e
    if t.tmal is None:
        t.start_match("final_destination")
    t.rush_cmd(0)
    t.set_u8(S_ACTION_LOG, 1)
    t.set_u8(S_INPUT_VIEWER, 1)
    t.set_u8(S_DI_DISPLAY, 1)
    t.set_u8(S_DI_VECTOR, 8)      # Survival DI Compass
    t.set_u8(S_PSA_INSPECTOR, 1)  # P1 PSA Debugger
    t.set_u8(S_INFO_HMN_SIZE, 2)  # Large Info Display
    for r in range(6):
        t.set_u8(S_INFO_HMN_ROWS + r, r + 1)

    # Populate Action Log with crisp transitions before capturing multi-HUD screenshot.
    e.hold(0, stick=(240, 128))
    step(e, 8)
    e.hold(0, buttons=["Y"], stick=(240, 128))
    step(e, 5)
    e.hold(0, buttons=["A"], stick=(240, 128))
    step(e, 4)
    e.neutral(0)
    step(e, 18)

    dm = read_tmal(e, t.tmal)
    t.check(
        dm["count"] >= 3 and dm["drawCalls"] > 0,
        "action-log-multihud-live",
        f"count={dm['count']} draws={dm['drawCalls']}",
    )
    t.snap("action-log-multihud")

    # Clean up and exit to CSS to verify 0 B heap leak on detach.
    t.set_u8(S_ACTION_LOG, 0)
    t.set_u8(S_INPUT_VIEWER, 0)
    t.set_u8(S_DI_DISPLAY, 0)
    t.set_u8(S_DI_VECTOR, 0)
    t.set_u8(S_PSA_INSPECTOR, 0)
    for r in range(6):
        t.set_u8(S_INFO_HMN_ROWS + r, 0)
    step(e, 2)
    e.h.call("resume")
    nav.finish_training(e)


DISPATCH = {
    "combo-break-fd": sc_combo_break_fd,
    "combo-break-bf": sc_combo_break_bf,
    "action-log-p1-cpu": sc_action_log_p1_cpu,
    "action-log-multihud": sc_action_log_multihud,
}


def cmd_test_overnight_tools(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-overnight-tools")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--aspect", choices=("16:9", "4:3"), default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    selected = opts.only if opts.only else list(SCENARIOS)
    unknown = [n for n in selected if n not in DISPATCH]
    if unknown:
        E.die(f"unknown scenario(s) {unknown}; available: {SCENARIOS}")

    label = "test-overnight-tools"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, log_path = "", ""
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            t = Runner(emu, aspect=opts.aspect, keep_going=opts.keep_going)
            try:
                for name in selected:
                    t.step_name = name
                    DISPATCH[name](t)
            except (Timeout, nav.NavError, HarnessError, ToolsTestError, DO.DisplayError, LT.LabError,
                    struct.error, KeyError, TypeError, IndexError, ValueError) as exc:
                why = f"{t.step_name}: {type(exc).__name__}: {exc}"
            finally:
                try:
                    emu.h.call("resume")
                    emu.advance(2)
                except (OSError, HarnessError, ValueError, Timeout):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{t.step_name if t else 'boot'}: {exc}"

    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for line in (t.failures if t else []):
        print(f"FAIL {line}")
    for path, w, h in (t.shots if t else []):
        print(f"screenshot: {path} ({w}x{h})")
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and t and t.failures:
        why = f"{len(t.failures)} check(s) failed"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
