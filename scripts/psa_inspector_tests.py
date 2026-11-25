"""Live Dolphin test suite for the PSA AnimCmd 11-Thread & FSM Inspector
(run through emu.py):

  test-psa-inspector REL [--only NAME ...] [--aspect 16:9|4:3] [--list] [--keep-going]

One boot on Final Destination, deterministic single-core with D3D rendering.

Scenarios:
  off-noop       psaInspector = Off produces 0 drawCalls, playerNo == -1,
                 activeThreadCount == 0.
  live-threads   Snake (P1) vs Fox (CPU) with psaInspector = P1: verifies
                 real active PSA threads (ActMain, SubMain, etc.), advancing
                 m_logicalFrame (+0x24), non-zero PSA opcodes (cmdHeader != 0),
                 live SyncWait / AsyncWait timers, and real non-1.0 FSM values
                 during Snake Down-B (C4 plant, FSM ~1.79) and Neutral-B
                 (Grenade, FSM 1.25). Captures standalone and combined HUD
                 (PSA + Info Display + Input Viewer) D3D screenshots.
  toggle-and-reenter
                 Toggles Off -> P1 -> CPU -> Off, verifies playerNo switches
                 cleanly between P1 (0) and CPU (1) and stops drawing on Off,
                 then exits to CSS and re-enters a second Training match with
                 zero crash/leak and clean re-attach.
"""
import argparse
import os
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
S_INPUT_VIEWER = 52
S_PSA_INSPECTOR = 55
S_INFO_HMN_PRESET = 56
S_INFO_HMN_SIZE = 57
S_INFO_HMN_ROWS = 58

TMPS_MAGIC = 0x544D5053  # "TMPS"
TMPS_SIZE = 0x130
PSA_BLOCK_RE = re.compile(r"psa: debug block at ([0-9a-f]{8})")

SCENARIOS = [
    "off-noop",
    "live-threads",
    "toggle-and-reenter",
]


class PsaTestError(RuntimeError):
    pass


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def send_pad(emu, buttons=(), stick=(128, 128)):
    emu.hold(0, buttons=buttons, stick=stick)


def clear_pad(emu):
    emu.neutral(0)


def read_tmps(emu, addr):
    raw = emu.read(addr, TMPS_SIZE)
    hdr = struct.unpack_from(">IIiiifffiI", raw, 0)
    threads = []
    for i in range(11):
        off = 0x28 + i * 0x18
        tid, ttype, active, wait_kind, cmd_hdr, log_f, speed, wait_f = struct.unpack_from(
            ">IhBBIfff", raw, off
        )
        threads.append({
            "slot": i,
            "id": tid,
            "type": ttype,
            "active": active,
            "waitKind": wait_kind,
            "cmdHeader": cmd_hdr,
            "logicalFrame": log_f,
            "frameSpeed": speed,
            "waitFrames": wait_f,
        })
    return {
        "magic": hdr[0],
        "drawCalls": hdr[1],
        "playerNo": hdr[2],
        "status": hdr[3],
        "subaction": hdr[4],
        "animFrame": hdr[5],
        "fsmRate": hdr[6],
        "secondaryRate": hdr[7],
        "secondaryKind": hdr[8],
        "activeThreadCount": hdr[9],
        "threads": threads,
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
        self.tmps = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise PsaTestError(f"{name}: {detail}")

    def set_u8(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFF:02x}")

    def set_u16(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFFFF:04x}")

    def snap(self, name):
        self.set_u8(S_FRAME_ADVANCE, 1)
        step(self.e, 1)
        self.e.h.call("resume")
        self.e.advance(2)
        self.shots.append(self.e.screenshot(name + self.suffix))
        self.e.h.call("pause")
        self.set_u8(S_FRAME_ADVANCE, 0)
        step(self.e, 1)

    def start_match(self, p1_name="snake", cpu_name="fox"):
        e = self.e
        clear_pad(e)
        if nav.scene(e)[0] == "scMelee":
            e.h.call("resume")
            nav.finish_training(e)
        if nav.scene(e)[0] != "scSelctCharacter":
            nav.boot_to_training_css(e)
        if self.aspect:
            LT.set_aspect(e, self.aspect, lambda msg: None)
        pos = len(e.log_text())
        e.mark(f"psa: {p1_name} vs {cpu_name}")
        nav.start_training_match(e, p1_name, cpu_name, seed_rng=True)
        e.neutral(0)
        lab = LT.Lab(e, [])
        self.settings = lab.find_settings()
        self.set_u8(S_PSA_INSPECTOR, 0)
        self.set_u8(S_INPUT_VIEWER, 0)
        self.set_u8(S_FRAME_ADVANCE, 0)
        for off, val in DO.CPU_STILL.items():
            self.set_u8(off, val)
        self.set_u16(LT.SET_CPU_PERCENT, 0)
        self.set_u8(LT.SET_LOCK_CPU, 1)
        e.h.call("pause")
        step(e, 20)
        hits = PSA_BLOCK_RE.findall(e.log_text()[pos:])
        if not hits:
            hits = PSA_BLOCK_RE.findall(e.log_text())
        if not hits:
            raise PsaTestError("no psa debug block line in log")
        self.tmps = int(hits[-1], 16)


def sc_off_noop(t):
    e = t.e
    t.start_match("snake", "fox")
    t.set_u8(S_PSA_INSPECTOR, 0)
    step(e, 6)
    snap = read_tmps(e, t.tmps)
    t.check(
        snap["magic"] == TMPS_MAGIC
        and snap["drawCalls"] == 0
        and snap["playerNo"] == -1
        and snap["activeThreadCount"] == 0,
        "off-noop",
        f"magic={snap['magic']:#x} draws={snap['drawCalls']} p={snap['playerNo']} active={snap['activeThreadCount']}",
    )


def sc_live_threads(t):
    e = t.e
    if t.tmps is None:
        t.start_match("snake", "fox")
    t.set_u8(S_PSA_INSPECTOR, 1)  # P1
    step(e, 4)

    # Idle check: active threads, valid opcodes, advancing logicalFrame.
    s0 = read_tmps(e, t.tmps)
    step(e, 6)
    s1 = read_tmps(e, t.tmps)
    idle_act_adv = (
        s1["threads"][0]["logicalFrame"] > s0["threads"][0]["logicalFrame"]
        or s1["threads"][1]["logicalFrame"] > s0["threads"][1]["logicalFrame"]
    )
    t.check(
        s1["magic"] == TMPS_MAGIC
        and s1["playerNo"] == 0
        and s1["drawCalls"] > s0["drawCalls"]
        and s1["activeThreadCount"] >= 1
        and idle_act_adv,
        "live-idle-threads",
        f"draws={s0['drawCalls']}->{s1['drawCalls']} active={s1['activeThreadCount']} "
        f"actF={s0['threads'][0]['logicalFrame']:.1f}->{s1['threads'][0]['logicalFrame']:.1f} "
        f"subF={s0['threads'][1]['logicalFrame']:.1f}->{s1['threads'][1]['logicalFrame']:.1f}",
    )

    # Trigger Snake Down-B (Ground C4 Plant, FitSnake.pac FSM 1.7895) and sample every frame.
    send_pad(e, buttons=["B"], stick=(128, 30))
    step(e, 3)
    clear_pad(e)

    saw_non_one_fsm = False
    peak_fsm = 1.0
    saw_sync_wait = False
    saw_async_wait = False
    saw_opcode = False
    saw_sub_adv = False
    prev_sub_f = -1.0
    max_active = 0
    shot_standalone = False
    shot_combined = False

    for frame_idx in range(36):
        step(e, 1)
        st = read_tmps(e, t.tmps)
        if abs(st["fsmRate"] - 1.0) > 0.05:
            saw_non_one_fsm = True
            if abs(st["fsmRate"] - 1.0) > abs(peak_fsm - 1.0):
                peak_fsm = st["fsmRate"]
        if st["activeThreadCount"] > max_active:
            max_active = st["activeThreadCount"]
        sub = st["threads"][1]
        if sub["active"]:
            if prev_sub_f >= 0.0 and sub["logicalFrame"] > prev_sub_f + 0.01:
                saw_sub_adv = True
            prev_sub_f = sub["logicalFrame"]
        for th in st["threads"]:
            if th["active"]:
                if th["cmdHeader"] != 0:
                    saw_opcode = True
                if th["waitKind"] == 1 and th["waitFrames"] > 0.0:
                    saw_sync_wait = True
                if th["waitKind"] == 2 and th["waitFrames"] > 0.0:
                    saw_async_wait = True

        has_wait = any(th["active"] and th["waitKind"] != 0 and th["waitFrames"] > 0.0 for th in st["threads"])

        # Capture standalone screenshot while non-1.0 FSM, active threads, and a wait timer are visible.
        if not shot_standalone and saw_non_one_fsm and st["activeThreadCount"] >= 2 and (has_wait or frame_idx >= 8):
            shot_standalone = True
            t.snap("psa-standalone")

        # Enable Info Display (P1 Large, 6 rows) + Input Viewer (P1) and capture combined HUD shot.
        if shot_standalone and not shot_combined and (st["activeThreadCount"] >= 2 or frame_idx >= 12):
            shot_combined = True
            t.set_u8(S_INPUT_VIEWER, 1)
            t.set_u8(S_INFO_HMN_SIZE, 2)  # Large
            for r in range(6):
                t.set_u8(S_INFO_HMN_ROWS + r, r + 1)
            step(e, 2)
            t.snap("psa-with-hud")
            # Restore Info Display and Input Viewer off after combined shot.
            t.set_u8(S_INPUT_VIEWER, 0)
            for r in range(6):
                t.set_u8(S_INFO_HMN_ROWS + r, 0)
            step(e, 1)

    # Also trigger Snake Up-Tilt (A + Up) which uses AsyncWait (0x0002) and SyncWait (0x0001).
    step(e, 30)
    send_pad(e, buttons=["A"], stick=(128, 220))
    step(e, 3)
    clear_pad(e)
    for _ in range(28):
        step(e, 1)
        st = read_tmps(e, t.tmps)
        if abs(st["fsmRate"] - 1.0) > 0.05:
            saw_non_one_fsm = True
            if abs(st["fsmRate"] - 1.0) > abs(peak_fsm - 1.0):
                peak_fsm = st["fsmRate"]
        for th in st["threads"]:
            if th["active"]:
                if th["cmdHeader"] != 0:
                    saw_opcode = True
                if th["waitKind"] == 1 and th["waitFrames"] > 0.0:
                    saw_sync_wait = True
                if th["waitKind"] == 2 and th["waitFrames"] > 0.0:
                    saw_async_wait = True

    t.check(
        saw_opcode and saw_sub_adv and saw_sync_wait and saw_async_wait and max_active >= 2,
        "live-threads-opcodes-waits",
        f"maxActive={max_active} subAdv={saw_sub_adv} opcode={saw_opcode} "
        f"syncWait={saw_sync_wait} asyncWait={saw_async_wait}",
    )
    t.check(
        saw_non_one_fsm,
        "live-non-1.0-fsm",
        f"peakFsm={peak_fsm:.4f}",
    )


def sc_toggle_and_reenter(t):
    e = t.e
    if t.tmps is None:
        t.start_match("snake", "fox")

    # Off -> P1 -> CPU -> Off.
    t.set_u8(S_PSA_INSPECTOR, 0)
    step(e, 3)
    d_off0 = read_tmps(e, t.tmps)

    t.set_u8(S_PSA_INSPECTOR, 1)
    step(e, 3)
    d_p1 = read_tmps(e, t.tmps)

    t.set_u8(S_PSA_INSPECTOR, 2)
    step(e, 3)
    d_cpu = read_tmps(e, t.tmps)

    t.set_u8(S_PSA_INSPECTOR, 0)
    step(e, 3)
    d_off1 = read_tmps(e, t.tmps)
    step(e, 3)
    d_off2 = read_tmps(e, t.tmps)

    t.check(
        d_off0["playerNo"] == -1
        and d_p1["playerNo"] == 0
        and d_p1["drawCalls"] > d_off0["drawCalls"]
        and d_cpu["playerNo"] == 1
        and d_cpu["drawCalls"] > d_p1["drawCalls"]
        and d_cpu["activeThreadCount"] >= 1
        and d_off2["playerNo"] == -1
        and d_off2["drawCalls"] == d_off1["drawCalls"],
        "toggle-off-p1-cpu-off",
        f"off0={d_off0['playerNo']}/{d_off0['drawCalls']} p1={d_p1['playerNo']}/{d_p1['drawCalls']} "
        f"cpu={d_cpu['playerNo']}/{d_cpu['drawCalls']} off2={d_off2['playerNo']}/{d_off2['drawCalls']}",
    )

    # Exit match and re-enter a second Training match to confirm zero crash/leak.
    t.set_u8(S_PSA_INSPECTOR, 1)
    step(e, 2)
    t.start_match("mario", "fox")
    t.set_u8(S_PSA_INSPECTOR, 1)
    step(e, 4)
    d_re = read_tmps(e, t.tmps)
    t.check(
        d_re["magic"] == TMPS_MAGIC and d_re["playerNo"] == 0 and d_re["activeThreadCount"] >= 1 and d_re["drawCalls"] > 0,
        "reenter-clean",
        f"magic={d_re['magic']:#x} p={d_re['playerNo']} active={d_re['activeThreadCount']} draws={d_re['drawCalls']}",
    )


DISPATCH = {
    "off-noop": sc_off_noop,
    "live-threads": sc_live_threads,
    "toggle-and-reenter": sc_toggle_and_reenter,
}


def cmd_test_psa_inspector(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-psa-inspector")
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

    label = "test-psa-inspector"
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
            except (Timeout, nav.NavError, HarnessError, PsaTestError, DO.DisplayError, LT.LabError,
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
