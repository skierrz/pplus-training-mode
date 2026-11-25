"""Time measurements of the plugin in the emulated console's clock (run
through emu.py; needs the PERF=1 build, src/shared/perf.h).

  test-perf REL [--frames N] [--only NAME ...] [--skip-drills] [--json FILE]
      One boot. The stub and the match module each keep a block of
      sections (ticks of the console's timebase, 60.75 MHz on a Wii, around
      each hook and the big pieces inside it); the script zeroes them,
      plays a scenario and reads them back. Each scenario starts from the
      same Dolphin state of a Training match (Mario against a Fox CPU on
      Final Destination, the CPU standing under the lab's CPU Options), so
      the fights are the same frame for frame:
        idle         the lab with its defaults, nobody moving
        fight        Mario walks to the CPU and attacks it (jabs, tilts)
        *-game       the same with the stub calling no module callback: the
                     game alone, for its own frame time
        osds         fight with every OSD on
        info-ledge   fight with both info panels (8 rows) and the Ledgedash HUD
        di           fight with the DI Display
        boxes        fight with both fighters' hitboxes and the stage collision
        heatmap      fight with the Hitbox Heatmap and reach bounds (both)
        grabviz      fight with the Grab / Search / Item-Pickup Visualizer (both)
        everything   all of the above (except heatmap/grabviz) at once
        everything-pplus  everything plus Hitbox Heatmap, reach bounds, and
                          Grab Visualizer at once
        menu         Training paused, the lab menu open
        savestate    five saves and five loads through the D-pad
        record       fight with HMN Mode Record
        playback     the recording played back (Loop on)
        calibrate    fixed work timed once (a counted loop, copies, a
                     checksum, a cache flush, log lines)
      Prints one table per scenario: per frame, the average and the worst
      frame of each section in microseconds and as a share of a 60 Hz frame.
      Nothing here is a pass/fail check beyond the runs completing.
"""
import argparse
import json
import os
import re
import struct
import time

import emu as E
import lab_tests as LT
import loader_tests as L
import nav
from harness import HarnessError, Timeout

TICKS_PER_US = 60.75  # the Wii's timebase: a quarter of the 243 MHz bus
FRAME_US = 1e6 / 60.0
SECTION = 20
HEADER = 20
CONTROL_BYPASS, CONTROL_CALIBRATE, CONTROL_INVALID_READ, CONTROL_LEGACY, CONTROL_DAMAGE = 1, 2, 4, 8, 16
LOAD_REFUSED = re.compile(r"savestate: load refused: ([^\r\n]+)")
# The addresses the invalid-read probe reads (src/main/module.cpp perfOnRender).
PROBE_READS = re.compile(r"(Invalid read from|Unable to resolve read address) (0x)?(00000010|7e000010|81800010|94000010)\b",
                         re.I)
BLOCK_LINE = re.compile(r"perf: (stub|main) block at ([0-9a-f]{8}), (\d+) sections: ([^\r\n]*)")

# main::Settings offsets (src/main/settings.h; the module checks them at build time).
S_LEDGE_HUD, S_DI = 12, 19
S_HITBOXES, S_ENV = 42, 44
S_HEATMAP, S_HEATMAP_BOUNDS, S_GRAB_VIZ = 47, 48, 51
S_HMN_ROWS, S_CPU_ROWS = 58, 74
S_OSD_ADVANTAGE, S_OSD_FIRST, S_OSD_LAST = 89, 92, 108
# The Damage preset's eight rows (ui/info_display.cpp applyInfoPreset).
INFO_ROWS = [2, 3, 4, 5, 6, 15, 16, 18]
# CPU Control Lab, Behavior Stand, TDI None, no SDI (cpu_settings.h).
CPU_STILL = {123: 1, 124: 0, 125: 4, 126: 0}

RIGHT = (228, 128)


class PerfError(RuntimeError):
    pass


class Blocks:
    def __init__(self, emu):
        self.emu = emu
        self.blocks = {}
        self.legacy = False  # --legacy: the match module as before the cuts (CONTROL_LEGACY)

    def find(self, since=0):
        for m in BLOCK_LINE.finditer(self.emu.log_text(), since):
            self.blocks[m.group(1)] = (int(m.group(2), 16), m.group(4).split())
        if "stub" not in self.blocks or "main" not in self.blocks:
            raise PerfError(f"no perf block lines in the log (have {sorted(self.blocks)}): is this a PERF=1 build?")

    def zero(self, control=0):
        for who, (addr, names) in self.blocks.items():
            if self.emu.u32(addr) != 0x50455246:
                raise PerfError(f"no perf block at {addr:#x} ({who})")
            # frames and control; the section count stays
            self.emu.h.call("write_mem", addr=addr + 4,
                            hex=struct.pack(">II", 0, control if who == "stub" else self.main_bits()).hex())
            self.emu.h.call("write_mem", addr=addr + 16, hex="00000000")
            self.emu.h.call("write_mem", addr=addr + HEADER, hex=(b"\0" * (SECTION * len(names))).hex())

    def main_bits(self):
        return CONTROL_LEGACY if self.legacy else 0

    def control(self, who, value):
        if who == "main":
            value |= self.main_bits()
        self.emu.h.call("write_mem", addr=self.blocks[who][0] + 8, hex=f"{value:08x}")

    def read(self):
        out = {}
        for who, (addr, names) in self.blocks.items():
            raw = self.emu.read(addr, HEADER + SECTION * len(names))
            frames = struct.unpack_from(">I", raw, 4)[0]
            rows = {}
            for i, name in enumerate(names):
                calls, ticks, worst_call, worst_frame, _ = struct.unpack_from(">5I", raw, HEADER + SECTION * i)
                rows[name] = {"calls": calls, "ticks": ticks, "worst_call": worst_call, "worst_frame": worst_frame}
            out[who] = {"frames": frames, "rows": rows}
        return out


def fmt_table(name, data, note=""):
    lines = [f"== {name}" + (f" ({note})" if note else "")]
    for who in ("stub", "main"):
        d = data[who]
        frames = max(d["frames"], 1)
        lines.append(f"   {who}: {d['frames']} frames")
        for sec, r in d["rows"].items():
            if r["calls"] == 0:
                continue
            if sec.startswith("n_"):
                lines.append(f"     {sec:18} {r['ticks'] / frames:9.1f} per frame   worst frame {r['worst_frame']:6d}"
                             f"   calls {r['calls']}")
                continue
            avg = r["ticks"] / frames / TICKS_PER_US
            worst = r["worst_frame"] / TICKS_PER_US
            lines.append(f"     {sec:18} avg {avg:8.1f} us {100 * avg / FRAME_US:5.1f}%   worst frame {worst:8.1f} us "
                         f"{100 * worst / FRAME_US:5.1f}%   calls/frame {r['calls'] / frames:5.2f}   worst call "
                         f"{r['worst_call'] / TICKS_PER_US:8.1f} us")
    return "\n".join(lines)


class Perf:
    def __init__(self, emu, frames, notes):
        self.emu = emu
        self.frames = frames
        self.notes = notes
        self.blocks = Blocks(emu)
        self.lab = LT.Lab(emu, [])
        self.results = {}
        self.base = None
        self.shots = None  # --shots: a screenshot of every scenario's last frame
        self.shots_prefix = "perf"

    def note(self, text):
        self.notes.append(text)
        print("  " + text, flush=True)

    def set(self, offset, value):
        data = bytes(value) if isinstance(value, (list, tuple)) else bytes([value])
        self.emu.h.call("write_mem", addr=self.lab.settings + offset, hex=data.hex())

    def measure(self, name, frames=None, bypass=False, script=None, during=None, note=""):
        """Zero the blocks, run `frames` frames (with the pad script on
        port 1, and `during` called once the run starts), read them."""
        e = self.emu
        e.h.call("pause")
        self.blocks.zero(CONTROL_BYPASS if bypass else 0)
        if script:
            e.h.call("pad_script", port=0, frames=script, start="next")
        if during:
            e.h.call("resume")
            during()
            e.h.call("pause")
        else:
            # Frame-exact from the paused state, so runs of two builds end
            # on the same frame (--shots compares them).
            e.h.call("frame_advance", n=frames or self.frames)
        data = self.blocks.read()
        if self.shots is not None:
            # The harness captures only while emulation runs: the game's
            # tasks are held still for it (nav.screenshot_at_game_frame).
            nav.hold_game_still(e, True)
            e.h.call("resume")
            time.sleep(0.5)
            self.shots.append(e.screenshot(f"{self.shots_prefix}-{name}")[0])
            nav.hold_game_still(e, False)
            e.h.call("pause")
        self.blocks.zero(0)
        e.neutral(0)
        self.results[name] = data
        print(fmt_table(name, data, note), flush=True)
        return data

    # ---- the lab ------------------------------------------------------------

    def into_lab(self, states):
        e = self.emu
        nav.start_training_match(e, "mario", "fox", seed_rng=True)
        self.lab.find_settings()
        self.blocks.find()
        for off, v in CPU_STILL.items():
            self.set(off, v)
        e.advance(60)
        # Mario walks until the CPU is within reach, then stands.
        f = self.lab.f

        def close():
            m, c = f.by_player(0), f.by_player(1)
            return abs(f.state(c)["x"] - f.state(m)["x"]) < 14.0
        # A fixed walk from a fixed frame: every run builds the same base.
        nav.pause_at_game_frame(e, 180)
        e.hold(0, stick=RIGHT)
        for _ in range(600):
            e.h.call("frame_advance", n=1)
            if close():
                break
        else:
            raise PerfError("Mario did not reach the CPU")
        e.neutral(0)
        e.h.call("frame_advance", n=60)
        self.base = os.path.join(states, "perf-base.sav")
        e.h.call("save_state", path=self.base)
        self.note("base state saved")

    def load(self):
        self.emu.h.call("pause")
        self.emu.h.call("load_state", path=self.base)
        self.blocks.zero(0)

    def fight_script(self):
        """Jabs, forward tilts and steps toward the CPU, about `frames` long."""
        steps = []
        pad = lambda buttons=(), stick=(128, 128), hold=1: dict(buttons=list(buttons), main=list(stick),
                                                              c=[128, 128], l=0, r=0, hold=hold)
        n = 0
        while n < self.frames + 60:
            for _ in range(3):
                steps += [pad(["A"], hold=3), pad(hold=7)]
            steps += [pad(["A"], stick=(200, 128), hold=3), pad(hold=25)]
            steps += [pad(stick=RIGHT, hold=8), pad(hold=6)]
            n += 3 * 10 + 28 + 14
        return steps

    def lab_scenarios(self, only):
        def want(n):
            return not only or n in only
        s = self.set
        fight = self.fight_script()
        all_osds = lambda: [s(o, 1) for o in [S_OSD_ADVANTAGE, S_OSD_ADVANTAGE + 1]
                            + list(range(S_OSD_FIRST, S_OSD_LAST + 1))]

        def info(ledge):
            s(S_HMN_ROWS, INFO_ROWS)
            s(S_CPU_ROWS, INFO_ROWS)
            if ledge:
                s(S_LEDGE_HUD, 1)

        scenarios = [
            ("idle", None, None, False),
            ("idle-game", None, None, True),
            ("fight", None, fight, False),
            ("fight-game", None, fight, True),
            ("osds", all_osds, fight, False),
            ("info-ledge", lambda: info(True), fight, False),
            ("di", lambda: s(S_DI, 1), fight, False),
            ("boxes", lambda: (s(S_HITBOXES, 3), s(S_ENV, 1)), fight, False),
            ("heatmap", lambda: (s(S_HEATMAP, 3), s(S_HEATMAP_BOUNDS, 1)), fight, False),
            ("grabviz", lambda: s(S_GRAB_VIZ, 3), fight, False),
            ("everything", lambda: (all_osds(), info(True), s(S_DI, 1),
                                    s(S_HITBOXES, 3), s(S_ENV, 1)), fight, False),
            ("everything-pplus", lambda: (all_osds(), info(True), s(S_DI, 1),
                                          s(S_HITBOXES, 3), s(S_ENV, 1),
                                          s(S_HEATMAP, 3), s(S_HEATMAP_BOUNDS, 1),
                                          s(S_GRAB_VIZ, 3)), fight, False),
        ]
        for name, setup, script, bypass in scenarios:
            if not want(name):
                continue
            self.load()
            if setup:
                setup()
            self.measure(name, script=script, bypass=bypass)
        for name, setup, script, bypass in scenarios:
            if "headroom-" + name in only:
                self.headroom(name, setup, script, bypass)
        if want("menu"):
            self.load()
            self.emu.h.call("resume")
            nav.pause_training(self.emu)
            self.lab.open_menu(keep=True)
            self.measure("menu", frames=300)
            self.emu.h.call("resume")
            self.lab.close_menu()
            nav.resume_training(self.emu)
        if want("savestate") or want("calibrate"):
            self.load()
            self.savestates(want("savestate"), want("calibrate"))
        if want("record") or want("playback"):
            self.load()
            self.recording(fight)
        if "invalid" in only:
            self.load()
            self.invalid_reads()
        if "damage" in only:
            self.load()
            self.damaged_slots()

    def wait_after(self, what, regex, pos, frames=180):
        rx = re.compile(regex)
        return self.emu.wait_for(what, lambda: rx.search(self.emu.log_text(), pos), frames, every=5)

    def damaged_slots(self):
        """A quick state whose slot was overwritten (a record's length, then
        a sampled data word) must be refused on load, with nothing read
        past the slot (not a scenario of the default list)."""
        e = self.emu
        e.h.call("resume")
        for how, what in ((1, "a record's length"), (2, "a data word")):
            pos = len(e.log_text())
            e.press(0, ["DRIGHT"], frames=14, after=20)
            self.wait_after("the save", r"savestate: saved frame", pos)
            self.blocks.control("main", CONTROL_DAMAGE | (how << 8))
            self.wait_after("the damage", r"perf: damage: quick state's slot", pos)
            e.press(0, ["DLEFT"], frames=3, after=20)
            self.wait_after("the load's answer", r"savestate: (load refused|loaded frame)", pos)
            text = e.synced_log_text()[pos:]
            m = LOAD_REFUSED.search(text)
            loaded = re.search(r"savestate: loaded frame", text)
            bad = re.findall(r"Invalid (?:read|write)[^\r\n]*", text)
            self.note(f"damaged {what}: load {'refused: ' + m.group(1) if m else 'NOT refused'}"
                      f"{'; it LOADED' if loaded else ''}; invalid accesses: {bad or 'none'}")
            if not m or m.group(1) != "state memory overwritten" or loaded or bad:
                raise PerfError(f"damaged {what}: the load was not refused cleanly")

    def headroom(self, name, setup, script, bypass, frames=120):
        """The most time the render hook can waste each frame before the
        game runs late (a frame of the scenario longer than 17.1 ms; one field
        is 16.68 ms and the longest seen without waste is 16.98 ms):
        what the frame has left over. Binary search, each try from the base
        state."""
        e = self.emu
        lo, hi = 0.0, 16000.0
        tries = []
        while hi - lo > 50.0:
            mid = (lo + hi) / 2
            self.load()
            if setup:
                setup()
            self.blocks.zero(CONTROL_BYPASS if bypass else 0)
            e.h.call("write_mem", addr=self.blocks.blocks["stub"][0] + 16, hex=f"{int(mid * TICKS_PER_US):08x}")
            if script:
                e.h.call("pad_script", port=0, frames=script, start="next")
            e.h.call("resume")
            e.advance(frames)
            e.h.call("pause")
            data = self.blocks.read()
            e.h.call("write_mem", addr=self.blocks.blocks["stub"][0] + 16, hex="00000000")
            e.neutral(0)
            worst = data["stub"]["rows"]["frame"]["worst_frame"] / TICKS_PER_US
            ok = worst < 17100.0
            tries.append((round(mid), round(worst)))
            if ok:
                lo = mid
            else:
                hi = mid
        self.results["headroom-" + name] = {"us": lo, "tries": tries}
        self.note(f"headroom {name}: {lo:.0f} us of the frame left over ({100 * lo / FRAME_US:.1f}%); "
                  f"tries (burn us, worst frame us): {tries}")

    def invalid_reads(self):
        """What the emulator does with reads no console maps (not a scenario
        of the default list: it puts invalid-read lines in the log)."""
        e = self.emu
        pos = len(e.log_text())
        self.blocks.control("main", CONTROL_INVALID_READ)
        e.h.call("resume")
        e.wait_log("the probe's reads", r"perf: probe read of 94000010", 120)
        e.advance(30)
        for line in e.log_text()[pos:].splitlines():
            if "invalid read" in line.lower() or "unable to resolve" in line.lower() or "exception" in line.lower():
                self.note(line.strip()[:220])

    def savestates(self, measure, calibrate):
        e = self.emu
        pos = len(e.log_text())

        def presses():
            e.press(0, ["DRIGHT"], frames=14, after=40)
            for _ in range(5):
                e.press(0, ["DLEFT"], frames=3, after=40)
            for _ in range(4):
                e.press(0, ["DRIGHT"], frames=14, after=40)
        if measure:
            self.measure("savestate", during=presses, note="per save and load: see save_capture and load_restore")
            text = e.log_text()[pos:]
            for line in re.findall(r"savestate: [^\r\n]*(?:saved|loaded)[^\r\n]*", text)[:12]:
                self.note(line.strip())
        else:
            e.h.call("resume")
            e.press(0, ["DRIGHT"], frames=14, after=40)
        if calibrate:
            e.h.call("pause")
            pos = len(e.log_text())
            self.blocks.control("main", CONTROL_CALIBRATE)
            e.h.call("resume")
            line = e.wait_log("the calibration line", r"perf: calibrate[^\r\n]*ticks:[^\r\n]*|perf: calibrate: no slot",
                              120)
            for l in re.findall(r"perf: calibrate[^\r\n]*", e.log_text()[pos:]):
                self.note(l.strip())
            m = re.search(r"perf: calibrate ticks: ([^\r\n]*)", e.log_text()[pos:])
            if m:
                self.results["calibrate"] = dict(kv.split("=") for kv in m.group(1).split())

    def recording(self, fight):
        e = self.emu
        found = re.findall(r"record: test block at ([0-9a-f]{8})", e.log_text())
        if not found:
            raise PerfError("no record test block line")
        rec = int(found[-1], 16)
        settings = self.lab.settings

        def request(code):
            e.h.call("pause")
            before = e.u32(rec + 8)
            e.h.call("write_mem", addr=rec + 4, hex=f"{code:08x}")
            for _ in range(30):
                e.h.call("frame_advance", n=1)
                if e.u32(rec + 4) == 0:
                    break
            if e.u32(rec + 8) != before + 1:
                raise PerfError(f"record request {code} not carried out")
        request(1)  # Save Positions
        e.h.call("write_mem", addr=settings + 186, hex="0101")  # HMN Mode Record, slot 1
        request(8)
        self.measure("record", script=fight)
        e.h.call("write_mem", addr=settings + 186, hex="0201")  # HMN Mode Playback, slot 1
        e.h.call("write_mem", addr=settings + 192, hex="01")  # Loop
        request(8)
        self.measure("playback")


def cmd_test_perf(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-perf")
    ap.add_argument("rel")
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--skip-drills", action="store_true")
    ap.add_argument("--json", default=None)
    ap.add_argument("--shots", default=None, help="screenshot every scenario's last frame, named PREFIX-scenario")
    ap.add_argument("--legacy", action="store_true", help="the match module as before the cuts (same build)")
    opts = ap.parse_args(rest)
    label = "test-perf"
    backend = (E.env("VIDEO_BACKEND", required=False) or "D3D") if opts.shots else E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, notes = "", "boot", "", []
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    p = None
    only = opts.only or []
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=not opts.shots) as (proc, emu):
            log_path = emu.log_path
            p = Perf(emu, opts.frames, notes)
            if opts.shots:
                p.shots, p.shots_prefix = [], opts.shots
            p.blocks.legacy = opts.legacy
            try:
                step = "boot"
                nav.boot_to_training_css(emu, drill_list="close")
                step = "lab match"
                p.into_lab(states)
                step = "lab scenarios"
                p.lab_scenarios(only)
            except (Timeout, nav.NavError, HarnessError, PerfError, LT.LabError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    emu.screenshot("fail-perf")
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    if p is not None and opts.json:
        with open(opts.json, "w", encoding="utf-8") as f:
            json.dump(p.results, f, indent=1)
        print(f"results: {opts.json}")
    if "invalid" in only:
        log = "\n".join(line for line in log.splitlines() if not PROBE_READS.search(line))
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
