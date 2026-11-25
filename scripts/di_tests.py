"""Emulator test of the DI Display, and the measurements it is built on (run
through emu.py; settings come from the environment the same way).

  test-di REL [--only NAME ...]
      The test: the paths the DI Display draws against where the fighter
      really goes (docs/testing.md, test-di). NAME: accuracy (or fsmash,
      usmash, utilt), frozen, boxes (the line with Project+'s hitbox and
      collision drawing in the same pass), off, second, pad, savestate,
      land, wall, follow, drawdi, cpu-sdi (the lab CPU's own DI makes no
      smash DI of its own: trajectory DI alone, ASDI Up, Smash DI amounts
      with and without DI, P1's attacks through its pad), platform
      (Battlefield; it leaves the match, so it runs last).
  test-di REL --measure
      The measurements: the Code Menu values, game code read live, and the
      CPU's motion (position, hitlag, hitstun, status, kinetic energies)
      frame by frame for many sticks, written to di-measure.txt next to
      SD_IMAGE. They set the rules src/extra/di_display.cpp follows.
  test-di REL --measure-pad
      P1's pad stick against the stick the game makes of it (di-pad.txt).
  test-di REL --measure-bounce
      The CPU's rows with its kinetic energies around landings, slides and
      bounces (di-bounce.txt).
  test-di REL --measure-ray [--only code fd ecb time bf]
      With a `make EXPERIMENTS=1` build (src/main/experiments/ray_probe.cpp):
      the game's stage ray check and the fighter's ECB in flight, measured
      (di-ray.txt).

One boot, Mario (P1) against a Fox CPU on Final Destination (then on
Battlefield for the platform scenario). The base state
is made as test-display makes it (random number generators seeded, the CPU
standing still under the lab's CPU control, Mario walked up to it), with a
percent set and locked. Hits and the CPU's stick are played through the
plugin's script source, so every input lands on an exact game frame.
"""
import argparse
import math
import os
import re
import struct
import time

import display_options_tests as DO
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import plugin_input as PI
from harness import HarnessError, Timeout
from plugin_input import B_ATTACK, B_SMASH, B_SPECIAL, frames_to_steps

S_PLAYER_PERCENT = 8  # general.playerPercent (u16), then lockPlayerPercent
S_CPU_PERCENT = 120  # cpu.percent (u16), then lockPercent
S_DI, S_FRAME_ADVANCE = 19, 11  # main::Settings general.diDisplay, general.frameAdvance
STATUS_DAMAGE = (0x43, 0x44, 0x45, 0x46, 0x49)
TRACE_BYTE = re.compile(r"di: trace byte at ([0-9a-f]{8})")
TRACE_POINT = re.compile(r"di: ([pnl]) (\d+) (-?[\d.]+) (-?[\d.]+)")
TRACE_FOLLOW = re.compile(r"di: ([pnl])\+ (\d+) (-?[\d.]+) (-?[\d.]+)")
TRACE_END = re.compile(r"di: ([pnl]) (lands|touches) (\d+) (-?[\d.]+) (-?[\d.]+)")
TRACE_HEAD = re.compile(r"di: P(\d) at (-?[\d.]+) (-?[\d.]+) hitlag (\d+) hitstun (\d+) speed (-?[\d.]+) "
                        r"(-?[\d.]+) stick (-?[\d.]+) (-?[\d.]+)")

# The game's common parameters the DI rules read. Project+ sets several of
# them (its Code Menu writes the SDI and ASDI distances and the decay every
# frame); the ASDI threshold is the game's own. Notes in di_display.cpp.
CONSTANTS = {"ASDI threshold": 0x80B88350, "hitstun mult": 0x80B87AA8, "hitlag mult": 0x80B87AEC,
             "hitlag max": 0x80B87AE8,
             "SDI distance": 0x80B88354, "ASDI distance": 0x80B88358, "bounce mult": 0x80B88510,
             "decay": 0x80B88534}
# Game code read live (sora_melee at its usual 0x8070AA14; getDamageLog is
# the damage module's vtable slot +0x88, also printed from the vtable).
DISASM = {"getDamageLog": (0x8076C8EC, 4), "correctDamageVector": (0x808778D8, 0xAB),
          "execStop": (0x80876A58, 0x110), "leaveStop": (0x80876E68, 0x80), "P+ stick routine": (0x80585750, 0x24),
          "execNormalDamageCommon": (0x808776E0, 0x80)}


class DiError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


class Probe:
    """Reads one fighter's motion state straight from game memory."""

    def __init__(self, emu, fighter):
        self.e = emu
        self.f = fighter
        self.energies = None

    def mods(self):
        acc = self.e.ptr(self.f["fighter"] + 0x60)
        return self.e.read(self.e.ptr(acc + 0xD8), 0xD0)

    def work(self, mods, which):
        g = self.e.ptr(u32(mods, 0x64) + which)
        head = self.e.read(g + 0xC, 0x10)
        return u32(head, 0), u32(head, 4), u32(head, 8), u32(head, 0xC)  # ints, n, floats, n

    def find_energies(self):
        """Energy objects the kinetic module points at (pointers in its
        first 0x100 bytes whose target holds a pointer into sora_melee's
        data as its first word: a vtable)."""
        mods = self.mods()
        kin = u32(mods, 0x7C)
        raw = self.e.read(kin, 0x100)
        out = []
        for off in range(0, 0x100, 4):
            p = u32(raw, off)
            if self.e.is_ram(p) and p != kin:
                vt = self.e.u32(p)
                if 0x80B00000 <= vt < 0x80C00000:
                    out.append((off, p))
        self.energies = out
        return kin, out

    def read(self):
        e = self.e
        mods = self.mods()
        pos = e.read(u32(mods, 0xC) + 0xC, 8)
        stop = e.read(u32(mods, 0x44) + 0x10, 4)
        status = e.u32(u32(mods, 0x70) + 0x34)
        ints, n_i, floats, n_f = self.work(mods, 0x20)
        hitstun = s32(e.read(ints + 4 * 0x38, 4), 0)
        lf = e.read(floats, 4 * min(n_f, 0x24))
        row = {"x": f32(pos, 0), "y": f32(pos, 4), "hitlag": s32(stop, 0), "hitstun": hitstun, "status": status,
               "res": (f32(lf, 0x3C), f32(lf, 0x40)), "c": (f32(lf, 0x88), f32(lf, 0x8C)) if n_f > 0x23 else None,
               "en": {}}
        for off, p in self.energies or []:
            b = e.read(p + 8, 8)
            row["en"][off] = (f32(b, 0), f32(b, 4))
        return row


def fmt_row(i, r):
    en = " ".join(f"{off:x}:({v[0]:.4f},{v[1]:.4f})" for off, v in r["en"].items() if v != (0.0, 0.0))
    c = f" c=({r['c'][0]:.3f},{r['c'][1]:.3f})" if r["c"] and r["c"] != (0.0, 0.0) else ""
    return (f"{i:3d} st={r['status']:x} hl={r['hitlag']} hs={r['hitstun']} x={r['x']:.4f} y={r['y']:.4f} "
            f"res=({r['res'][0]:.4f},{r['res'][1]:.4f}){c} {en}")


class Bench:
    def __init__(self, emu, out):
        self.emu = emu
        self.out = out
        self.r = DO.Runner(emu, [], [])

    def say(self, text):
        print(text)
        self.out.append(text)

    def setup(self, states, percent):
        r = self.r
        e = self.emu
        if getattr(self, "made", False):
            self.emu.h.call("load_state", path=r.base)
            self.emu.h.call("pause")
        else:
            DO.make_base(r, states)
            self.made = True
        # Each fighter gets the percent through its own lock: Player Percent
        # for Mario, CPU Options > CPU Percent for the CPU (the CPU stays the
        # CPU for the locks while the lab drives it).
        e.h.call("write_mem", addr=r.lab.settings + S_PLAYER_PERCENT, hex=f"{percent:04x}01")
        e.h.call("write_mem", addr=r.lab.settings + S_CPU_PERCENT, hex=f"{percent:04x}01")
        e.h.call("frame_advance", n=3)
        if abs(r.f.damage(r.c) - percent) > 0.01:
            raise DiError(f"CPU damage {r.f.damage(r.c)}, wanted {percent}")
        self.base = os.path.join(states, f"di-base{getattr(self, 'tag', '')}-{percent}.sav")
        e.h.call("save_state", path=self.base)
        self.mario = Probe(e, r.m)
        self.fox = Probe(e, r.c)
        self.toward = 1.0 if r.distance() > 0 else -1.0

    def new_stage(self, states, kind, name, percent, on_css=None):
        """Leaves the match for a new Training match, Mario against a Fox
        CPU on stage `kind` (srStageKind; Battlefield is 1), and makes its
        base state as setup() does. The stage select takes its first icon
        and the match setup's stage kind is replaced before the match loads
        it (as test-extra-heaps does)."""
        e = self.emu
        r = self.r
        r.inp.stop()
        e.h.call("resume")
        nav.finish_training(e)
        if on_css:
            on_css()
        nav.choose_on_css(e, "mario", "fox")
        nav.press_until(e, "the stage select", ["START"], lambda: nav.scene(e)[0] == "scSelStage", 600)
        nav.pick_stage(e, 0)
        e.wait_for("the stage select to exit", lambda: nav.scene(e)[0] != "scSelStage", 600)
        mode = e.ptr(e.ptr(nav.GAME_GLOBAL) + 0x8)
        e.h.call("write_mem", addr=mode + nav.MELEE_STAGE, hex=f"{kind:04x}")
        # Seeded as the first match is, so the fighters start the same way
        # on every run.
        nav.seed_match_rng(e)
        nav.wait_match_start(e)
        got = nav.melee_setup(e)["stage"]
        if got != kind:
            raise DiError(f"match on stage kind {got:#x}, wanted {kind:#x}")
        nav.pause_at_game_frame(e, DO.BASE_FRAME)
        r.lab.find_settings()
        r.m, r.c = r.f.by_player(0), r.f.by_player(1)
        r.inp = PI.PluginInput(e)
        for offset, value in DO.CPU_STILL.items():
            r.set(offset, value)
        e.h.call("frame_advance", n=30)
        # Fighters that start on a platform (Battlefield) drop through it:
        # the stick pushed down at once, from the centre.
        down = frames_to_steps({k: (0, 0.0, -1.0) for k in range(1, 4)}, 60)
        if r.f.state(r.m)["y"] > 5 or r.f.state(r.c)["y"] > 5:
            r.inp.run({0: down, 1: down})
            r.inp.stop()
        r.walk(16.0)
        if abs(r.f.state(r.m)["y"]) > 1 or abs(r.f.state(r.c)["y"]) > 1:
            raise DiError(f"fighters not on the main floor: Mario y {r.f.state(r.m)['y']:.2f}, CPU "
                          f"{r.f.state(r.c)['y']:.2f}")
        self.say(f"{name}: Mario at ({r.f.state(r.m)['x']:.2f}, {r.f.state(r.m)['y']:.2f}), the CPU at "
                 f"({r.f.state(r.c)['x']:.2f}, {r.f.state(r.c)['y']:.2f})")
        r.base = os.path.join(states, f"di-{name}-start.sav")
        e.h.call("save_state", path=r.base)
        self.made = True
        self.tag = "-" + name
        self.setup(states, percent)

    def load(self):
        self.emu.h.call("load_state", path=self.base)
        self.emu.h.call("pause")

    def hit(self, move, fox_frames, frames=80):
        """Loads the base state, plays Mario's `move` (attack buttons, stick)
        and the CPU's frames {driven frame: (buttons, x, y)}, and returns the
        CPU's rows per driven frame (1-based)."""
        self.load()
        b, x, y = move
        mario = frames_to_steps({1: (b, x * self.toward, y), 2: (b, x * self.toward, y)}, 4)
        fox = frames_to_steps(fox_frames, frames)
        if self.fox.energies is None:
            kin, en = self.fox.find_energies()
            self.say(f"kinetic module {kin:08x}; energies at " + ", ".join(f"+{o:x} {p:08x}" for o, p in en))
        rows = self.r.inp.run({0: mario, 1: fox}, read=lambda i: self.fox.read())
        self.r.inp.stop()
        return rows


def first_hit(rows):
    """Driven frame (1-based) on which the CPU is first in hitlag."""
    for i, r in enumerate(rows):
        if r["hitlag"] > 0:
            return i + 1
    raise DiError("no hit")


def held(x, y, start, end=400):
    return {k: (0, x, y) for k in range(start, end)}


def find_floats(e, say, what, ranges, targets, tol):
    """Words in `ranges` holding a float within `tol` of one of `targets`."""
    for lo, hi in ranges:
        raw = e.read(lo, hi - lo)
        for o in range(0, len(raw) - 3, 4):
            v = f32(raw, o)
            if any(abs(v - t) <= tol for t in targets):
                say(f"{what}: {lo + o:08x} = {v:.6f}")


def dump(e, say, what, addr, n):
    n = (n + 15) // 16 * 16
    raw = e.read(addr, n)
    for o in range(0, n, 16):
        words = " ".join(f"{u32(raw, o + k):08x}" for k in range(0, 16, 4))
        floats = " ".join(f"{f32(raw, o + k):11.4f}" for k in range(0, 16, 4))
        say(f"{what} +{o:03x}: {words} | {floats}")


def human_stick(e, entry_index=None):
    """P1's input object stick (aiInput +0x10/+0x14)."""
    arr = e.ptr(LT.ENTRY_MANAGER)
    for i in range(min(e.u32(LT.ENTRY_MANAGER + 4), 9)):
        raw = e.read(arr + i * LT.ENTRY_SIZE, 0x60)
        if s32(raw, 0x58) == 0 and s32(raw, 4) >= 0:
            inp = e.u32(u32(raw, 0x2C) + 0xC)
            b = e.read(inp + 0x10, 8)
            return f32(b, 0), f32(b, 4)
    return None


def measure_pad(bench, states):
    """The human's pad stick (gfPadStatus units, as the lab captures it)
    against the stick the game makes of it (P1's input object)."""
    e = bench.emu
    bench.setup(states, 0)
    for vx, vy in [(v, 0) for v in (10, 20, 21, 22, 23, 24, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 90, 100,
                                    127)] + [(-40, 0), (-100, 0), (0, 40), (0, -40), (30, 30), (40, 40), (50, 50),
                                             (60, 60), (70, 70), (80, 80), (100, 100), (60, 20), (90, 40)]:
        bench.load()
        e.hold(0, stick=(128 + vx, 128 + vy))
        e.h.call("frame_advance", n=4)
        sx, sy = human_stick(e)
        bench.say(f"pad {vx:4d} {vy:4d} -> stick {sx:.4f} {sy:.4f} ({sx * 80:.2f}, {sy * 80:.2f})")
    e.neutral(0)


def measure(bench, states):
    """See the module docstring; what each part settles is in the
    research notes."""
    e = bench.emu
    say = bench.say
    fsmash = (B_ATTACK | B_SMASH, 1.0, 0.0)
    bench.setup(states, 60)
    # In the match: sora_melee is loaded, the Code Menu has written its values.
    for name, addr in CONSTANTS.items():
        say(f"constant {name} at {addr:08x}: {e.f32(addr):.6f} (word {e.u32(addr):08x})")
    find_floats(e, say, "float 0.051/0.4/6/3 in sora_melee data", [(0x80B00000, 0x80BA0000)],
                [0.051, 0.4, 6.0, 3.0], 1e-6)
    for name, (addr, n) in DISASM.items():
        lines = e.h.call("disasm", addr=addr, count=n)["lines"]
        say(f"--- {name}")
        for line in lines:
            say(line)
    # The state of the CPU during hitlag: where the launch speed is kept.
    bench.load()
    mario = frames_to_steps({1: fsmash, 2: fsmash}, 4)
    fox = frames_to_steps({}, 30)
    rows = bench.r.inp.run({0: mario, 1: fox}, read=lambda i: bench.fox.read(),
                           until=lambda i, rows: rows[-1]["hitlag"] > 0 and rows[-1]["hitlag"] < 6)
    mods = bench.fox.mods()
    kin, dmg = u32(mods, 0x7C), u32(mods, 0x38)
    say(f"hitlag {rows[-1]['hitlag']}: kinetic {kin:08x}, damage module {dmg:08x}")
    dump(e, say, "kinetic", kin, 0x100)
    raw = e.read(kin, 0x100)
    for o in range(0, 0x100, 4):
        p = u32(raw, o)
        if 0x81000000 <= p < 0x81800000 or 0x90000000 <= p < 0x94000000:
            dump(e, say, f"kinetic+{o:x} -> {p:08x}", p, 0x60)
    dump(e, say, "damage module", dmg, 0x200)
    vt = e.u32(dmg + 8)
    fn = e.u32(vt + 0x88)
    say(f"damage module vtable {vt:08x}, getDamageLog {fn:08x}")
    for line in e.h.call("disasm", addr=fn, count=12)["lines"]:
        say(line)
    ints, n_i, floats, n_f = bench.fox.work(mods, 0x20)
    dump(e, say, f"LA floats ({n_f})", floats, 4 * n_f)
    ints, n_i, floats, n_f = bench.fox.work(mods, 0x24)
    dump(e, say, f"RA floats ({n_f})", floats, 4 * n_f)
    # The launch speed is about (1.3993, 1.3513) before decay, (1.3626, 1.3159) after one.
    find_floats(e, say, "launch-like float near the fighter", [(dmg - 0x400, dmg + 0x2000)],
                [1.3993, 1.3513, 1.3626, 1.3159, 1.9453, 1.8943], 0.002)
    e.h.call("frame_advance", n=8)
    say("after launch:")
    dump(e, say, "kinetic", kin, 0x100)
    for o in range(0, 0x100, 4):
        p = u32(raw, o)
        if 0x81000000 <= p < 0x81800000 or 0x90000000 <= p < 0x94000000:
            dump(e, say, f"kinetic+{o:x} -> {p:08x}", p, 0x60)
    bench.r.inp.stop()

    runs = []
    for mag in (0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95):
        runs.append((f"fsmash 60: stick up mag {mag} from hit+1", fsmash, held(0.0, mag, 14)))
    runs.append(("fsmash 60: stick 45 deg mag 0.9 from hit+1", fsmash, held(0.6364, 0.6364, 14)))
    runs.append(("fsmash 60: stick up mag 0.9 then 1.0 from hit+3", fsmash,
                  dict(list(held(0.0, 0.9, 14, 16).items()) + list(held(0.0, 1.0, 16).items()))))
    runs.append(("fsmash 60: stick up then up-right (cardinal change) on hit+3", fsmash,
                  dict(list(held(0.0, 1.0, 14, 16).items()) + list(held(0.7071, 0.7071, 16).items()))))
    for title, move, frames in runs:
        rows = bench.hit(move, frames)
        h = first_hit(rows)
        say(f"=== {title} (hit {h})")
        for i, r in enumerate(rows):
            if h - 1 <= i + 1 <= h + 20:
                say(fmt_row(i + 1, r))

    bench.setup(states, 100)
    moves = {"fsmash": fsmash, "dsmash": (B_ATTACK | B_SMASH, 0.0, -1.0), "usmash": (B_ATTACK | B_SMASH, 0.0, 1.0),
             "utilt": (B_ATTACK, 0.0, 0.5), "ftilt": (B_ATTACK, 0.5, 0.0)}
    for name, move in moves.items():
        for title, frames in (("neutral", {}), ("stick up from hit+1", None)):
            if frames is None:
                frames = held(0.0, 1.0, h + 1)
            try:
                rows = bench.hit(move, frames, frames=150)
                h = first_hit(rows)
            except DiError:
                say(f"=== {name} 100%: no hit")
                break
            say(f"=== {name} 100% {title} (hit {h})")
            for i, r in enumerate(rows):
                if i + 1 >= h - 1:
                    say(fmt_row(i + 1, r))


# ---- stage rays (make EXPERIMENTS=1: src/main/experiments/ray_probe.cpp) -----------

PROBE_LINE = re.compile(r"ray: probe at ([0-9a-f]{8})")
ECB_LINE = re.compile(r"ray: ecb P(\d) pos (\S+) (\S+) down (\S+) (\S+) up (\S+) (\S+) left (\S+) (\S+) right (\S+) "
                      r"(\S+) centre (\S+) (\S+)")
RAY_SIZE = 0x3C
# sora_melee .text 0x8070AA14 + the decomp symbol map's offsets.
RAY_FUNCS = {"stRayCheck(pos, dir, b, coll, b, mask)": (0x80932600, 0xD8),
             "stRayCheck(pos, dir, hit, normal, b, coll, b, mask)": (0x809326D8, 0x114),
             "stRayCheck(pos, dir, line, hit, normal, b, b, mask)": (0x809327EC, 0x100)}


class Rays:
    def __init__(self, bench):
        self.b = bench
        self.e = bench.emu
        m = PROBE_LINE.findall(self.e.log_text())
        if not m:
            raise DiError("no ray probe line (not an EXPERIMENTS=1 build?)")
        self.at = int(m[-1], 16)

    def cast(self, rays):
        """rays: [(pos (x, y), dir (dx, dy), kind, b1, b2, mask)] -> [(ret, hit, normal, line)]."""
        out = []
        for k in range(0, len(rays), 16):
            part = rays[k:k + 16]
            raw = b""
            for (px, py), (dx, dy), kind, b1, b2, mask in part:
                raw += struct.pack(">6f4B6f2i", px, py, 0.0, dx, dy, 0.0, kind, b1, b2, mask, *([0.0] * 6), -7, -7)
            self.e.h.call("write_mem", addr=self.at + 0x10, hex=raw.hex())
            self.e.h.call("write_mem", addr=self.at, hex=f"{len(part):08x}")
            self.e.h.call("frame_advance", n=1)
            if self.e.u32(self.at) != 0:
                raise DiError("the ray probe did not run")
            res = self.e.read(self.at + 0x10, RAY_SIZE * len(part))
            for i in range(len(part)):
                o = RAY_SIZE * i
                v = struct.unpack_from(">6f", res, o + 0x1C)
                out.append((s32(res, o + 0x34), v[:3], v[3:], s32(res, o + 0x38)))
        return out

    def ticks(self):
        return self.e.u32(self.at + 8)

    def report(self, title, rays):
        res = self.cast(rays)
        say = self.b.say
        say(f"--- {title} (last batch {self.ticks()} ticks)")
        for (p, d, kind, b1, b2, mask), (ret, hit, nrm, line) in zip(rays, res):
            say(f"  from ({p[0]:7.2f},{p[1]:7.2f}) by ({d[0]:7.2f},{d[1]:7.2f}) kind {kind} b {b1}{b2} mask {mask:#04x}: "
                f"ret {ret} hit ({hit[0]:.3f},{hit[1]:.3f},{hit[2]:.3f}) normal ({nrm[0]:.3f},{nrm[1]:.3f}) line {line}")
        return res


def flight_ecb(bench, title, move, stick=None, extra=120, bits=1):
    """One hit with the ECB log on: the CPU's ECB points against its
    position per frame, and its rows around the first non-damage status."""
    e = bench.emu
    rays = Rays(bench)
    bench.load()
    bench.set(S_DI, 1)
    e.h.call("write_mem", addr=rays.at + 0xC, hex=f"{bits:08x}")
    at = len(e.log_text())
    b, x, y = move
    mario = frames_to_steps({1: (b, x * bench.toward, y), 2: (b, x * bench.toward, y)}, 4)
    fox = frames_to_steps(held(stick[0] * bench.toward, stick[1], 5, 22) if stick else {}, 22)
    rows = bench.r.inp.run({0: mario, 1: fox}, read=lambda i: bench.fox.read(), extra=extra)
    bench.r.inp.stop()
    e.h.call("write_mem", addr=rays.at + 0xC, hex="00000000")
    text = e.log_text()[at:]
    ecb = [m for m in ECB_LINE.finditer(text) if m.group(1) == "2"]
    times = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"di: time P2 (\d+) ticks (\d+) rays", text)]
    if times:
        busy = [t for t in times if t[1] > 0]
        bench.say(f"time per drawing of P2's paths: {len(times)} drawings, {len(busy)} with rays; largest "
                  f"{max(t[0] for t in times) / 60.75:.1f} us ({max(times)[1]} rays); mean "
                  f"{sum(t[0] for t in times) / len(times) / 60.75:.1f} us; first ten {times[:10]}")
    h = first_hit(rows)
    bench.say(f"=== {title} (hit {h}); ECB lines {len(ecb)}")
    land = next((k for k in range(h, len(rows)) if rows[k]["status"] not in STATUS_DAMAGE), None)
    for i, r in enumerate(rows):
        if i + 1 >= h - 1 and (land is None or i <= land + 2):
            bench.say(f"{i + 1:3d} st={r['status']:x} hl={r['hitlag']} hs={r['hitstun']} x={r['x']:.4f} y={r['y']:.4f}")
    # One ECB line per drawing; matched to rows by position.
    shown = 0
    for m in ecb:
        x, y = float(m.group(2)), float(m.group(3))
        k = next((k for k, r in enumerate(rows) if abs(r["x"] - x) < 0.001 and abs(r["y"] - y) < 0.001), None)
        if k is not None and k + 1 >= h - 1 and (land is None or k <= land + 2) and shown < 150:
            shown += 1
            bench.say(f"  ecb row {k + 1} st={rows[k]['status']:x}: " + " ".join(m.groups()[3:]))
    return rows, land


# The driven frame each of HITS first hits the CPU from the base state.
HIT_FRAME = {"utilt": 5, "fsmash": 13, "usmash": 10}


def flight_rows(t, move, stick=None, moved_to=None, frames=60):
    """Mario's `move` on the CPU from the base state; the CPU holds `stick`
    through hitlag and the launch, and is moved to `moved_to` (paused, with
    3 hitlag frames left) if given. Returns the CPU's rows from its first
    hitlag frame on (Probe.read, energies included)."""
    e = t.emu
    t.load()
    b, x, y = move
    mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
    hit = HIT_FRAME[[k for k, v in HITS.items() if v == move][0]]
    fox = frames_to_steps(held(stick[0] * t.toward, stick[1], hit, 40) if stick else {}, 40)
    t.r.inp.run({0: mario, 1: fox}, read=lambda i: t.fox.read(), until=lambda i, r: r[-1]["hitlag"] > 0, extra=30)
    fox_w = e.watch(lambda *_: t.fox.read())
    rows = [fox_w.read()]
    while rows[-1]["hitlag"] > 3:
        rows.append(e.step(fox_w))
    if moved_to:
        posture = u32(t.fox.mods(), 0xC)
        e.h.call("write_mem", addr=posture + 0xC, hex=struct.pack(">ff", *moved_to).hex())
        rows.append(fox_w.read())
    rows += e.trace(frames, fox_w)
    t.r.inp.stop()
    return rows


def measure_bounce(t, states):
    """What a fighter in knockback does after it meets the stage without a
    tech: landing in tumble, landing from a grounded hit (slide), wall and
    ceiling bounces. Rows with the kinetic energies (di-bounce.txt)."""
    say = t.say
    cases = [(50, "utilt 50% none", HITS["utilt"], None, None, 40),
             (40, "utilt 40% none", HITS["utilt"], None, None, 40),
             (60, "utilt 60% none", HITS["utilt"], None, None, 50),
             (50, "utilt 50% in", HITS["utilt"], (-1.0, 0.0), None, 50),
             (50, "utilt 50% out", HITS["utilt"], (1.0, 0.0), None, 50),
             (100, "fsmash 100% from (-100, -30)", HITS["fsmash"], None, (-100.0, -30.0), 30),
             (100, "fsmash 100% from (-95, -45)", HITS["fsmash"], None, (-95.0, -45.0), 30),
             (100, "fsmash 100% from (-100, -38)", HITS["fsmash"], None, (-100.0, -38.0), 30),
             (100, "fsmash 100% from (-92, -55)", HITS["fsmash"], None, (-92.0, -55.0), 30),
             (100, "fsmash 100% up from (-95, -60)", HITS["fsmash"], (0.0, 1.0), (-95.0, -60.0), 30),
             (30, "fsmash 30% DI down (grounded)", HITS["fsmash"], (0.0, -1.0), None, 30)]
    t.setup(states, 100)
    t.fox.find_energies()
    for pc, title, move, stick, moved, n in cases:
        t.setup(states, pc)
        t.fox.find_energies()
        rows = flight_rows(t, move, stick, moved, n)
        say(f"=== {title}")
        for i, r in enumerate(rows):
            say(fmt_row(i, r))


def measure_ray(bench, states, only):
    """How stRayCheck answers (which lines, which way, the flags), what it
    costs, and the fighters' ECB in flight. Writes di-ray.txt. Parts (only):
    code, fd, ecb, bf."""
    e = bench.emu
    say = bench.say
    bench.setup(states, 100)
    bench.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    K = 0
    down, up = (0.0, -20.0), (0.0, 20.0)
    want = lambda part: not only or part in only  # noqa: E731
    if want("code"):
        measure_ray_code(bench)
    if want("fd"):
        measure_ray_fd(bench, K, down, up)
    if want("ecb"):
        measure_ray_ecb(bench, states)
    if want("time"):
        for pc, name, stick in ((100, "fsmash", None), (100, "utilt", None), (50, "utilt", None), (100, "usmash", None),
                                (100, "fsmash", (0.0, -1.0))):
            bench.setup(states, pc)
            flight_ecb(bench, f"time: {name} {pc}% stick {stick}", HITS[name], stick, extra=100, bits=2)
    if want("bf"):
        measure_ray_bf(bench, states, K)


def measure_ray_code(bench):
    e = bench.emu
    say = bench.say
    for name, (addr, n) in RAY_FUNCS.items():
        say(f"--- {name}")
        for line in e.h.call("disasm", addr=addr, count=n // 4)["lines"]:
            say(line)
    mods = bench.fox.mods()
    ground = u32(mods, 0x10)
    vt = e.u32(ground + 8)
    for slot in range(0x1E0, 0x200, 4):
        fn = e.u32(vt + slot)
        say(f"ground module vtable {vt:08x} slot +{slot:x}: {fn:08x}")
    for slot in (0x1EC, 0x1F0, 0x1F4):
        fn = e.u32(vt + slot)
        say(f"--- ground slot +{slot:x} at {fn:08x}")
        for line in e.h.call("disasm", addr=fn, count=48)["lines"]:
            say(line)


def measure_ray_fd(bench, K, down, up):
    say = bench.say
    rays = Rays(bench)
    rays.report("FD: down onto the floor, segment lengths, start points", [
        ((0, 10), down, K, 1, 1, 1), ((0, 10), (0, -5), K, 1, 1, 1), ((0, 10), (0, -10), K, 1, 1, 1),
        ((0, 10), (0, -10.01), K, 1, 1, 1), ((0, 10), (0, -1000), K, 1, 1, 1), ((0, 0.5), (0, -1), K, 1, 1, 1),
        ((0, 0), (0, -1), K, 1, 1, 1), ((0, -0.5), (0, -1), K, 1, 1, 1), ((0, -1), up, K, 1, 1, 1),
        ((0, -5), up, K, 1, 1, 1), ((0, -60), (0, 30), K, 1, 1, 1), ((0, -200), (0, 400), K, 1, 1, 1),
        ((30, 10), (10, -20), K, 1, 1, 1), ((-30, 10), (-10, -20), K, 1, 1, 1), ((0, 10), (0.001, -20), K, 1, 1, 1),
        ((0, 10), (100, -10.5), K, 1, 1, 1)])
    rays.report("FD: horizontal and the stage's sides", [
        ((-120, -5), (240, 0), K, 1, 1, 1), ((120, -5), (-240, 0), K, 1, 1, 1), ((-120, -20), (240, 0), K, 1, 1, 1),
        ((120, -20), (-240, 0), K, 1, 1, 1), ((-120, -40), (240, 0), K, 1, 1, 1), ((-120, 5), (240, 0), K, 1, 1, 1),
        ((-120, -5), (40, 0), K, 1, 1, 1), ((-80, -5), (20, 0), K, 1, 1, 1), ((-90, 20), (40, -40), K, 1, 1, 1),
        ((90, 20), (-40, -40), K, 1, 1, 1), ((-100, -10), (0, 30), K, 1, 1, 1), ((0, -100), (0, 60), K, 1, 1, 1)])
    rays.report("FD: edges (down at x)", [((x, 10), (0, -80), K, 1, 1, 1) for x in
                                          (-80, -72, -70, -69, -68, -67, -66, -60, 60, 66, 68, 70, 80, 0)])
    variants = []
    for kind in (0, 1, 2):
        for b1, b2 in ((0, 0), (1, 0), (0, 1), (1, 1)):
            variants.append(((0, 10), down, kind, b1, b2, 1))
    rays.report("FD: kinds and flags, down onto the floor", variants)
    variants = []
    for mask in (0, 1, 2, 4, 7, 0xFF):
        variants.append(((0, 10), down, K, 1, 1, mask))
        variants.append(((-120, -5), (240, 0), K, 1, 1, mask))
    rays.report("FD: masks (down; across the side)", variants)
    variants = [((-120, -5), (240, 0), kind, b1, b2, 1) for kind in (0, 1) for b1, b2 in ((0, 0), (1, 0), (0, 1))]
    rays.report("FD: flags, across the side", variants)
    # Cost: 16 rays of each kind.
    for title, ray in (("down hit", ((0, 10), down, K, 1, 1, 1)), ("down miss (short)", ((0, 10), (0, -5), K, 1, 1, 1)),
                       ("far off stage", ((0, 150), (3, -3), K, 1, 1, 1)), ("long across", ((-120, -5), (240, 0), K, 1, 1, 1)),
                       ("kind 2 down hit", ((0, 10), down, 2, 1, 1, 1))):
        rays.cast([ray] * 16)
        t = rays.ticks()
        say(f"cost: 16 x {title}: {t} ticks = {t / 60.75:.1f} us, {t / 16 / 60.75:.2f} us each")


def measure_ray_ecb(bench, states):
    """ECB in flight and the landings."""
    flight_ecb(bench, "ECB: up tilt 100%", HITS["utilt"])
    for pc in (50, 30):
        bench.setup(states, pc)
        flight_ecb(bench, f"ECB: forward smash {pc}%", HITS["fsmash"])
        flight_ecb(bench, f"ECB: forward smash {pc}% DI down", HITS["fsmash"], (0.0, -1.0))
        flight_ecb(bench, f"ECB: up tilt {pc}%", HITS["utilt"])


def measure_ray_bf(bench, states, K):
    """Battlefield: the platforms, from above and below."""
    e = bench.emu
    say = bench.say
    say("=== Battlefield")
    bench.new_stage(states, 1, "bf", 100)
    bench.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    rays = Rays(bench)
    say(f"fighters: Mario {bench.r.f.state(bench.r.m)['x']:.2f}, {bench.r.f.state(bench.r.m)['y']:.2f}; CPU "
        f"{bench.r.f.state(bench.r.c)['x']:.2f}, {bench.r.f.state(bench.r.c)['y']:.2f}")
    xs = list(range(-75, 80, 5))
    scan = []
    for x0 in (xs[:16], xs[16:]):
        scan += rays.report("BF: down from high at x", [((x, 150), (0, -170), K, 1, 1, 1) for x in x0])
    for x0 in (xs[:16], xs[16:]):
        rays.report("BF: up from 5 at x", [((x, 5), (0, 145), K, 1, 1, 1) for x in x0])
    # One x over each platform: the first hit from above higher than 5.
    plats = {}
    for x, r in zip(xs, scan):
        if r[0] and r[1][1] > 5:
            plats.setdefault(round(r[1][1], 1), []).append(x)
    say(f"platforms (height: x hit from above): {plats}")
    picks = [(xl[len(xl) // 2], hgt) for hgt, xl in sorted(plats.items())][:3]
    while len(picks) < 3:
        picks.append(picks[-1] if picks else (0, 30.0))
    tops = [p[1] for p in picks]
    say(f"picked {picks}")
    variants = []
    for (x, _), top in zip(picks, tops):
        for kind in (0, 1):
            for b1, b2 in ((0, 0), (1, 0), (0, 1), (1, 1)):
                variants.append(((x, top - 3), (0, 10), kind, b1, b2, 1))
    rays.report("BF: up through each platform from 3 below, flags", variants)
    variants = []
    for (x, _), top in zip(picks, tops):
        for b1, b2 in ((0, 0), (1, 0), (0, 1), (1, 1)):
            variants.append(((x, top + 3), (0, -10), K, b1, b2, 1))
    rays.report("BF: down onto each platform from 3 above, flags", variants)
    rays.report("BF: across the platforms' edges", [
        ((-100, tops[0]), (200, 0), K, 1, 1, 1), ((-100, tops[0] - 0.5), (200, 0), K, 1, 1, 1),
        ((-100, tops[1] + 0.5), (200, 0), K, 1, 1, 1), ((picks[1][0], tops[1] + 5), (0, -5.5), K, 1, 1, 1),
        ((picks[1][0], tops[1] - 0.2), (0, -10), K, 1, 1, 1), ((picks[1][0], tops[1]), (0, -10), K, 1, 1, 1),
        ((picks[1][0], tops[1]), (0, 10), K, 1, 1, 1), ((picks[1][0] + 40, tops[1] + 10), (-80, -20), K, 1, 1, 1)])
    for title, ray in (("down hit", ((0, 150), (0, -170), K, 1, 1, 1)),
                       ("far off stage", ((0, 250), (3, -3), K, 1, 1, 1))):
        rays.cast([ray] * 16)
        t = rays.ticks()
        say(f"BF cost: 16 x {title}: {t} ticks = {t / 60.75:.1f} us, {t / 16 / 60.75:.2f} us each")
    for pc in (60, 40):
        bench.setup(states, pc)
        flight_ecb(bench, f"BF: usmash {pc}%", HITS["usmash"], extra=100)
        flight_ecb(bench, f"BF: usmash {pc}% DI in", HITS["usmash"], (-1.0, 0.0), extra=100)


# ---- the test -------------------------------------------------------------------

HITS = {"fsmash": (B_ATTACK | B_SMASH, 1.0, 0.0), "usmash": (B_ATTACK | B_SMASH, 0.0, 1.0),
        "utilt": (B_ATTACK, 0.0, 0.5)}
PERCENT = 100
TOLERANCE = 1.0  # units, before the fighter touches anything


class Tester(Bench):
    def __init__(self, emu, out):
        Bench.__init__(self, emu, out)
        self.results = []
        self.shots = []

    def ok(self, name, detail):
        self.results.append((name, detail))
        self.say(f"ok   {name}: {detail}")

    def check(self, cond, name, detail):
        if not cond:
            raise DiError(f"{name}: {detail}")
        self.ok(name, detail)

    def fox_anim(self):
        return self.r.f.state(self.r.c)["anim"]

    def set(self, offset, value):
        self.emu.h.call("write_mem", addr=self.r.lab.settings + offset, hex=f"{value:02x}")

    def trace(self, who):
        """Logs the paths of player `who` (1-based; 0 stops) as they are drawn."""
        self.emu.h.call("write_mem", addr=self.trace_at, hex=f"{who:02x}")

    def shot(self, name):
        """Screenshot of the frame on screen (emulation runs for it; the
        game stays still while frame advance or the stop bit holds it)."""
        self.emu.h.call("resume")
        time.sleep(0.3)
        s = self.emu.screenshot(name)
        self.emu.h.call("pause")
        self.shots.append(s)
        return s

    def paths(self, text, player, hitlag=None):
        """{tag: [(i, x, y)]} and the header and stick lines of the last
        drawing of `player`'s paths in `text` (made with `hitlag` frames of
        hitlag left, if given)."""
        heads = [m for m in TRACE_HEAD.finditer(text) if int(m.group(1)) == player
                 and (hitlag is None or int(m.group(4)) == hitlag)]
        if not heads:
            return None, None, None
        last = heads[-1]
        body = text[last.end():]
        nxt = TRACE_HEAD.search(body)
        body = body[:nxt.start()] if nxt else body
        out = {}
        for m in TRACE_POINT.finditer(body):
            out.setdefault(m.group(1), []).append((int(m.group(2)), float(m.group(3)), float(m.group(4))))
        # Where a path meets the stage: out["p!"] = (lands or touches, frame, x, y).
        for m in TRACE_FOLLOW.finditer(body):
            out.setdefault(m.group(1) + "+", []).append((int(m.group(2)), float(m.group(3)), float(m.group(4))))
        for m in TRACE_END.finditer(body):
            out[m.group(1) + "!"] = (m.group(2), int(m.group(3)), float(m.group(4)), float(m.group(5)))
        return out, last, last


def compare(pred, rows, start):
    """Largest distance between predicted point i and the fighter's real
    position i frames after row `start` (0-based), up to the first frame
    the fighter leaves its damage statuses (it touched something) or its
    hitstun ends. Returns (error, frames compared, why it stopped)."""
    worst, n, why = 0.0, 0, "path ended"
    for i, x, y in pred:
        k = start + i
        if k >= len(rows):
            why = "run ended"
            break
        r = rows[k]
        if r["status"] not in STATUS_DAMAGE:
            why = f"status {r['status']:#x} on frame {i}"
            break
        worst = max(worst, math.hypot(r["x"] - x, r["y"] - y))
        n += 1
        if r["hitstun"] == 0:
            why = "hitstun over"
            break
    return worst, n, why


def sticks(toward):
    return {"none": (0.0, 0.0), "in": (-toward, 0.0), "out": (toward, 0.0), "up": (0.0, 1.0), "down": (0.0, -1.0)}


def sc_accuracy(t, only):
    """Every hit with every stick: the path drawn on the last hitlag frame
    against where the CPU really goes."""
    table = []
    for name, move in HITS.items():
        if only and name not in only and "accuracy" not in only:
            continue
        rows = t.hit(move, {}, frames=20)
        h = first_hit(rows)
        lag = rows[h - 1]["hitlag"]
        for sname, (sx, sy) in sticks(t.toward).items():
            # Held from the first hitlag frame to just past the launch (the
            # stick does nothing in hitstun; held longer it would walk the
            # CPU off the stage afterwards).
            frames = held(sx, sy, h + 1, h + lag + 3) if (sx, sy) != (0.0, 0.0) else {}
            mark = {}

            def each(i):
                # Traced over the whole of hitlag; the drawing made with one
                # hitlag frame left (the last before the launch) is used.
                if i == h - 1:
                    mark["at"] = len(t.emu.log_text())
                    t.trace(2)
                elif i == h + lag + 4:
                    t.trace(0)
            t.load()
            b, x, y = move
            mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
            # The script ends after the launch (the lab's CPU control then
            # holds the CPU still); the run goes on for the flight.
            fox = frames_to_steps(frames, h + lag + 5)
            rows = t.r.inp.run({0: mario, 1: fox}, read=lambda i: t.fox.read(), each=each, extra=150)
            t.r.inp.stop()
            t.trace(0)
            text = t.emu.log_text()[mark.get("at", 0):]
            paths, head, stick = t.paths(text, 2, 1)
            if not paths or ("p" not in paths and "p+" not in paths):
                raise DiError(f"{name} {sname}: no path logged on the last hitlag frame")
            k0 = launch_row(rows, h - 1) - 1
            if int(head.group(4)) != 1 or rows[k0]["hitlag"] != 1:
                raise DiError(f"{name} {sname}: drawn with hitlag {head.group(4)}, read "
                              f"{rows[k0]['hitlag']}; 1 was expected")
            err, n, why = compare(paths.get("p", []), rows, k0)
            table.append((name, sname, err, n, why))
            t.say(f"{name} {sname}: largest error {err:.4f} over {n} frames ({why}); stick "
                  f"{stick.group(8)},{stick.group(9)}")
            if n == 0 and sname != "down":
                raise DiError(f"{name} {sname}: nothing to compare ({why})")
            if err > TOLERANCE:
                raise DiError(f"{name} {sname}: predicted path off by {err:.3f} units")
            if sname == "none":
                ne, nn, _ = compare(paths.get("n", []), rows, k0)
                if ne > TOLERANCE:
                    raise DiError(f"{name}: the no-DI line is off by {ne:.3f} with no DI")
    t.ok("accuracy", "; ".join(f"{a} {b} {e:.3f}/{n}f" for a, b, e, n, _ in table))
    return table


def set_percent(t, states, percent):
    """A new base state at `percent`, the DI Display on."""
    t.setup(states, percent)
    t.set(S_DI, 1)
    t.emu.h.call("frame_advance", n=2)
    t.emu.h.call("save_state", path=t.base)


def play_hit(t, move, stick, extra=150):
    """Mario's `move` on the CPU from the base state, the CPU holding
    `stick` (toward Mario negative x) from its first hitlag frame to just
    past the launch. Returns the CPU's rows, its paths drawn on the last
    hitlag frame (Tester.paths) and the row of that frame."""
    rows = t.hit(move, {}, frames=20)
    h = first_hit(rows)
    lag = rows[h - 1]["hitlag"]
    sx, sy = stick[0] * t.toward, stick[1]
    frames = held(sx, sy, h + 1, h + lag + 3) if (sx, sy) != (0.0, 0.0) else {}
    mark = {}

    def each(i):
        # Traced over the whole of hitlag and a little after; the drawing
        # made with one hitlag frame left is picked out below.
        if i == h - 1:
            mark["at"] = len(t.emu.log_text())
            t.trace(2)
        elif i == h + lag + 4:
            t.trace(0)
    t.load()
    b, x, y = move
    mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
    rows = t.r.inp.run({0: mario, 1: frames_to_steps(frames, h + lag + 5)}, read=lambda i: t.fox.read(), each=each,
                       extra=extra)
    t.r.inp.stop()
    t.trace(0)
    paths, head, _ = t.paths(t.emu.log_text()[mark.get("at", 0):], 2, 1)
    if not paths or "p" not in paths:
        raise DiError("no path logged on the last hitlag frame")
    return rows, paths, launch_row(rows) - 1


def launch_row(rows, start=0):
    """The row of the launch frame, found in the data: the first row
    without hitlag after rows with it. The row before it is the last
    hitlag frame, whose drawing the tests compare: flight frame i is row
    launch_row - 1 + i."""
    for i in range(max(start, 1), len(rows)):
        if rows[i]["hitlag"] == 0 and rows[i - 1]["hitlag"] > 0:
            return i
    raise DiError("no launch in the rows read")


def first_touch(rows, k0):
    """The first row after row k0 where the fighter has left its damage
    statuses (it landed, teched or bounced), or None."""
    return next((k for k in range(k0 + 1, len(rows)) if rows[k]["status"] not in STATUS_DAMAGE), None)


LAND_TOLERANCE = 1.0  # units between the line's end and where the fighter lands


def check_landing(t, name, rows, paths, k0, one_frame=False):
    """The line drawn on the last hitlag frame (paths, row k0) against the
    CPU's flight: if the line ends with a landing, the CPU lands there; if
    not, the CPU touches nothing while the line lasts. Returns a note. With
    one_frame, the line's end may also be one frame's move off: the
    fighter's ECB moves with its tumble animation, which the line does not
    know, so it can land a frame earlier or later."""
    k = first_touch(rows, k0)
    end = paths.get("p!")
    err, n, why = compare(paths["p"], rows, k0)
    if err > TOLERANCE:
        raise DiError(f"{name}: the flight is {err:.3f} off the line")
    if end is None:
        if k is not None and k - k0 <= len(paths["p"]):
            raise DiError(f"{name}: the CPU touches the stage on frame {k - k0} at ({rows[k]['x']:.2f}, "
                          f"{rows[k]['y']:.2f}), inside the line ({len(paths['p'])} frames), which does not end there")
        return f"{name}: no landing within the line ({n} frames, {err:.4f})"
    if k is None or end[0] != "lands":
        raise DiError(f"{name}: the line ends with {end}, the CPU {'touches on row %d' % k if k else 'does not land'}")
    d = math.hypot(end[2] - rows[k]["x"], end[3] - rows[k]["y"])
    t.say(f"{name}: lands on frame {k - k0} at ({rows[k]['x']:.3f}, {rows[k]['y']:.3f}) (hitstun "
          f"{rows[k - 1]['hitstun']} left); the line ends on frame {end[1]} at ({end[2]:.3f}, {end[3]:.3f}): "
          f"{d:.3f} apart; flight before it {err:.4f} over {n}")
    move = math.hypot(rows[k - 1]["x"] - rows[k - 2]["x"], rows[k - 1]["y"] - rows[k - 2]["y"])
    if d > LAND_TOLERANCE and not (one_frame and d <= move + 0.05 and abs(end[1] - (k - k0)) == 1):
        raise DiError(f"{name}: line end {d:.3f} from the landing (one frame's move {move:.3f})")
    return f"{name}: {d:.3f} (frame {end[1]} vs {k - k0}; flight {err:.4f})"


def sc_land(t, states):
    """Hits where the CPU lands on Final Destination before hitstun ends:
    the line's end, marked as a landing, against where the CPU lands, and
    the path before it against the flight."""
    table = []
    for pc in (50, 30):
        set_percent(t, states, pc)
        for sname in ("none", "in", "out", "up"):
            stick = {"none": (0.0, 0.0), "in": (-1.0, 0.0), "out": (1.0, 0.0), "up": (0.0, 1.0)}[sname]
            rows, paths, k0 = play_hit(t, HITS["utilt"], stick)
            name = f"up tilt {pc}% {sname}"
            table.append(check_landing(t, name, rows, paths, k0))
    t.ok("landing on Final Destination", "; ".join(table))
    # The mark on screen: the game held on the last hitlag frame.
    rows = t.hit(HITS["utilt"], {}, frames=20)
    t.load()
    b, x, y = HITS["utilt"]
    t.r.inp.run({0: frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4),
                 1: frames_to_steps({}, 4)}, read=lambda i: t.fox.read(),
                until=lambda i, r: r[-1]["hitlag"] == 1, extra=30)
    t.r.inp.stop()
    nav.hold_game_still(t.emu, True)
    s = t.shot("di-land")
    t.set(S_DI, 0)
    t.emu.h.call("frame_advance", n=3)
    off = t.shot("di-land-off")
    t.set(S_DI, 1)
    nav.hold_game_still(t.emu, False)
    white = lambda p: p[0] > 235 and p[1] > 235 and p[2] > 235  # noqa: E731
    on, gone = DO.count(s, DO.FULL, white), DO.count(off, DO.FULL, white)
    t.check(on - gone > 20, "landing mark on screen", f"white pixels {on} with the display, {gone} without ({s[0]})")
    set_percent(t, states, PERCENT)


BF_PLATFORM = 27.2  # Battlefield's side platforms' height (x 20..55 and -55..-20; di-ray.txt)


def sc_platform(t, states):
    """Battlefield, Fox under the right side platform: up smashes that send
    him up through it (the line must not stop there) and, for some, back
    down onto it within hitstun (the line's end against the landing)."""
    # Leaving the match puts Project+'s Draw DI back: turned on in the Code
    # Menu while ours hides it, it is on again on the character select.
    e = t.emu
    at = draw_di_at(t)
    e.h.call("write_mem", addr=at, hex="00000001")
    e.h.call("frame_advance", n=2)
    seen = [e.u32(at)]
    t.new_stage(states, 1, "bf", 40, on_css=lambda: seen.append(e.u32(at)))
    t.trace_at = int(TRACE_BYTE.findall(e.log_text())[-1], 16)
    t.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    at2 = draw_di_at(t)
    seen.append(e.u32(at2))
    e.h.call("write_mem", addr=at2, hex="00000000")
    t.check(seen == [0, 1, 0] and at2 == at, "Draw DI put back when the match ends",
            f"hidden in the match {seen[0]}, on the character select {seen[1]}, hidden in the next match {seen[2]}")
    table, through, on_platform = [], 0, 0
    for pc in (40, 50, 60, 70, 80, 100):
        set_percent(t, states, pc)
        for sname, stick in (("none", (0.0, 0.0)), ("in", (-1.0, 0.0))):
            name = f"Battlefield up smash {pc}% {sname}"
            rows, paths, k0 = play_hit(t, HITS["usmash"], stick)
            pts = paths["p"]
            k = first_touch(rows, k0)
            last = k if k is not None else len(rows)
            up = [j for j in range(k0 + 1, last) if rows[j - 1]["y"] < BF_PLATFORM <= rows[j]["y"]
                  and 20 <= rows[j]["x"] <= 55]
            path_up = any(pts[i - 1][2] < BF_PLATFORM <= pts[i][2] for i in range(1, len(pts)))
            note = check_landing(t, name, rows, paths, k0, one_frame=True)
            if up:
                if not path_up:
                    raise DiError(f"{name}: the CPU went up through the platform on row {up[0]}, the line did not "
                                  f"(it ends with {paths.get('p!')})")
                through += 1
                note += ", up through the platform"
            on_platform += k is not None and "p!" in paths and rows[k]["y"] > BF_PLATFORM - 1
            t.say(note)
            table.append(note.split(": ", 1)[1] + f" [{pc}% {sname}]")
    t.check(through > 0 and on_platform > 0, "Battlefield platform from below and above",
            f"{through} paths up through the platform, {on_platform} landing back on it; " + "; ".join(table))


WALL_MOVE_HITLAG = 3  # hitlag frames left when the wall test moves the CPU


def sc_wall(t, states):
    """Final Destination's left side: the CPU, moved during hitlag to the
    left of the stage's wall and below the ledge, is sent into it by a
    forward smash from the left. The line's end, marked as a stage hit,
    against the first frame the CPU leaves its damage statuses (a bounce)."""
    set_percent(t, states, PERCENT)
    e = t.emu
    if t.toward < 0:
        raise DiError("Mario stands right of the CPU; the wall test expects him on the left")
    table = []
    for y0 in (-20.0, -30.0):
        t.load()
        b, x, y = HITS["fsmash"]
        mario = frames_to_steps({1: (b, x, y), 2: (b, x, y)}, 4)
        t.r.inp.run({0: mario, 1: frames_to_steps({}, 4)}, read=lambda i: t.fox.read(),
                    until=lambda i, r: r[-1]["hitlag"] > 0, extra=30)
        # The CPU is moved on a known frame: paused, with WALL_MOVE_HITLAG
        # hitlag frames left.
        fox_w = e.watch(lambda *_: t.fox.read())
        cur = fox_w.read()
        for _ in range(30):
            if cur["hitlag"] <= WALL_MOVE_HITLAG:
                break
            cur = e.step(fox_w)
        if cur["hitlag"] != WALL_MOVE_HITLAG:
            raise DiError(f"the CPU's hitlag went from more than {WALL_MOVE_HITLAG} to {cur['hitlag']}")
        posture = u32(t.fox.mods(), 0xC)
        e.h.call("write_mem", addr=posture + 0xC, hex=struct.pack(">ff", -100.0, y0).hex())
        at = len(e.log_text())
        rows = [fox_w.read()]
        t.trace(2)
        shot = y0 != -30.0
        for _ in range(60):
            rows.append(e.step(fox_w))
            if rows[-1]["hitlag"] == 1 and not shot:
                shot = True
                nav.hold_game_still(e, True)
                t.shot("di-wall")
                nav.hold_game_still(e, False)
        t.r.inp.stop()
        t.trace(0)
        paths, _, _ = t.paths(e.log_text()[at:], 2, 1)
        # Flight frame i is row k0 + i, counted from the launch found in the
        # data (holding the game for the screenshot can make a step pass
        # without the game moving).
        k0 = launch_row(rows) - 1
        # The first frame the flight turns sharply (a frame's move differs
        # from the one before by more than gravity and decay allow) or the
        # CPU leaves its damage statuses.
        k = first_touch(rows, k0)
        for j in range(k0 + 3, k if k is not None else len(rows)):
            ax = rows[j]["x"] - 2 * rows[j - 1]["x"] + rows[j - 2]["x"]
            ay = rows[j]["y"] - 2 * rows[j - 1]["y"] + rows[j - 2]["y"]
            if abs(ax) > 0.5 or abs(ay) > 0.5:
                k = j
                break
        end = paths.get("p!") if paths else None
        name = f"wall, CPU moved to (-100, {y0:.0f})"
        t.say(f"{name}: rows " + " ".join(f"[{r['status']:x} {r['x']:.2f},{r['y']:.2f}]" for r in rows[k0:k0 + 16]))
        if k is None:
            raise DiError(f"{name}: the CPU did not touch the stage")
        if end is None or end[0] != "touches":
            raise DiError(f"{name}: the CPU touched on row {k} (status {rows[k]['status']:#x}), the line ends "
                          f"with {end}")
        err, n, why = compare(paths["p"], rows, k0)
        # The touch happens during the move into frame k: the line's end
        # must be on frame k and within that move's reach of frame k - 1.
        d = math.hypot(end[2] - rows[k - 1]["x"], end[3] - rows[k - 1]["y"])
        move = math.hypot(rows[k - 1]["x"] - rows[k - 2]["x"], rows[k - 1]["y"] - rows[k - 2]["y"])
        t.say(f"{name}: touches during the move into frame {k - k0} (status {rows[k]['status']:#x}, from "
              f"({rows[k - 1]['x']:.3f}, {rows[k - 1]['y']:.3f}) to ({rows[k]['x']:.3f}, {rows[k]['y']:.3f})); the "
              f"line ends on frame {end[1]} at ({end[2]:.3f}, {end[3]:.3f}), {d:.3f} on from frame {k - k0 - 1} (a "
              f"frame's move is {move:.3f}); flight {err:.4f} over {n}")
        if end[1] != k - k0 or d > move + 0.25 or err > TOLERANCE:
            raise DiError(f"{name}: line end on frame {end[1]}, {d:.3f} from frame {k - k0 - 1} (a frame's move "
                          f"{move:.3f}), flight {err:.3f}")
        table.append(f"{name}: frame {end[1]} vs {k - k0}, end {d:.3f} into a {move:.3f} move ({rows[k]['status']:#x})")
    t.ok("wall on Final Destination", "; ".join(table))


SLIDE_TOLERANCE = 1.0   # units, the slide after a landing against the real one
BOUNCE_TOLERANCE = 3.0  # units, the flight after a bounce against the real one
BOUNCE_FRAMES = 20      # frames after a bounce the line must cover (if no second contact)


def moved_hit(t, move, stick=(0.0, 0.0), moves=(), shot=None, camera=0, shot_after=None, places=()):
    """Mario's `move` on the CPU from the base state, the CPU holding
    `stick` (toward Mario negative x) through hitlag and the launch, and
    moved to each of `moves` in turn (paused, from MOVE_HITLAG hitlag frames
    left, one per frame). Every drawing is traced. With `shot`, the frame
    with one hitlag frame left is photographed as `shot` with the display
    on and as `shot`-off without it, the game held still. Returns the CPU's
    rows, its paths drawn with one hitlag frame left, the row of that frame
    and the pictures (or None). With `shot_after`, the pictures are taken
    that many frames after the launch instead (the line then drawn from
    where the CPU flies). Effects are hidden for the pictures (Display
    Options > Effects). `camera` is the lab's Camera Mode for the run (2:
    the game's fixed camera, which shows the whole stage). `places` puts
    fighters (Probe, (x, y)) somewhere else on the floor before the hit."""
    e = t.emu
    t.load()
    t.set(DO.S_CAMERA, camera)
    for probe, pos in places:
        e.h.call("write_mem", addr=u32(probe.mods(), 0xC) + 0xC, hex=struct.pack(">ff", *pos).hex())
    if places:
        e.h.call("frame_advance", n=2)
    b, x, y = move
    mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
    hit = HIT_FRAME[[k for k, v in HITS.items() if v == move][0]]
    sx, sy = stick[0] * t.toward, stick[1]
    fox = frames_to_steps(held(sx, sy, hit, hit + 40) if (sx, sy) != (0.0, 0.0) else {}, hit + 40)
    at = len(e.log_text())
    t.trace(2)
    rows = t.r.inp.run({0: mario, 1: fox}, read=lambda i: t.fox.read(),
                       until=lambda i, r: r[-1]["hitlag"] > 0 and r[-1]["hitlag"] <= MOVE_HITLAG)
    fox_w = e.watch(lambda *_: t.fox.read())
    if moves:
        if rows[-1]["hitlag"] != MOVE_HITLAG:
            raise DiError(f"the CPU's hitlag went to {rows[-1]['hitlag']}, not {MOVE_HITLAG}")
        for k, pos in enumerate(moves):
            if k:
                rows.append(e.step(fox_w))
            posture = u32(t.fox.mods(), 0xC)
            e.h.call("write_mem", addr=posture + 0xC, hex=struct.pack(">ff", *pos).hex())
    pictures = None
    for _ in range(90):
        rows.append(e.step(fox_w))
        if shot_after is None:
            due = rows[-1]["hitlag"] == 1
        else:
            launched = next((j for j in range(1, len(rows)) if rows[j]["hitlag"] == 0 and rows[j - 1]["hitlag"] > 0),
                            None)
            due = launched is not None and len(rows) - 1 - launched == shot_after
        if shot and pictures is None and due:
            nav.hold_game_still(e, True)
            t.set(DO.S_EFFECTS, 0)
            e.h.call("frame_advance", n=3)
            on = t.shot(shot)
            t.set(S_DI, 0)
            e.h.call("frame_advance", n=3)
            off = t.shot(shot + "-off")
            t.set(S_DI, 1)
            t.set(DO.S_EFFECTS, 1)
            e.h.call("frame_advance", n=3)
            nav.hold_game_still(e, False)
            pictures = (on, off)
    t.r.inp.stop()
    t.trace(0)
    t.set(DO.S_CAMERA, 0)
    paths, _, _ = t.paths(e.log_text()[at:], 2, 1)
    if paths is None:
        raise DiError("no drawing with one hitlag frame left")
    return rows, paths, launch_row(rows) - 1, pictures


def line_both_sides(pictures):
    """In the slide picture: the white landing mark (white where the picture
    without the display is not) and line-blue pixels (blue first, changed by
    the display) left of it (the flight) and right of it (the dashed slide).
    Returns (mark pixels, left, right)."""
    on, off = pictures
    pa, pb = DO.picture(on), DO.picture(off)
    _, w, h = on
    white, blue = [], []
    for y in range(h):
        for x in range(w):
            a = pa.px(x, y)
            if not DO.differs(a, pb.px(x, y)):
                continue
            if a[0] > 235 and a[1] > 235 and a[2] > 235:
                white.append(x)
            elif a[2] > 90 and a[2] > a[0] + 50 and a[2] >= a[1]:
                blue.append(x)
    if not white:
        return 0, 0, 0
    lo, hi = min(white), max(white)
    return len(white), sum(1 for x in blue if x < lo), sum(1 for x in blue if x > hi)


MOVE_HITLAG = 3
STATUS_REFLECT = 0x47  # DamageFlyReflect, the wall bounce (0x48: the ceiling bounce)


def path_error(points, rows, first, last):
    """Largest distance from a point of `points` to the real path, the
    polyline through rows first..last (the line is a path: a bounce a frame
    early or late shifts the timing, not the place)."""
    worst = 0.0
    for _, x, y in points:
        best = None
        for j in range(first, min(last, len(rows) - 1)):
            ax, ay, bx, by = rows[j]["x"], rows[j]["y"], rows[j + 1]["x"], rows[j + 1]["y"]
            dx, dy = bx - ax, by - ay
            u = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy) if dx or dy else 0.0
            u = max(0.0, min(1.0, u))
            d = math.hypot(ax + u * dx - x, ay + u * dy - y)
            best = d if best is None else min(best, d)
        worst = max(worst, best if best is not None else 0.0)
    return worst


def follow_error(points, rows, k0):
    """Largest distance between continuation point (i, x, y) and row k0 + i."""
    worst = 0.0
    for i, x, y in points:
        if k0 + i < len(rows):
            worst = max(worst, math.hypot(rows[k0 + i]["x"] - x, rows[k0 + i]["y"] - y))
    return worst


def sc_follow(t, states):
    """What the line shows after the first contact, assuming no tech,
    against the real fighter: the slide after landing in tumble and after
    a grounded hit, and the flight after a wall bounce."""
    say = t.say
    table = []
    worst = {"slide": 0.0, "bounce": 0.0}
    for pc, name, move, stick in ((50, "up tilt 50% in", "utilt", (-1.0, 0.0)),
                                  (50, "up tilt 50% out", "utilt", (1.0, 0.0)),
                                  (30, "forward smash 30% DI down, on the ground", "fsmash", (0.0, -1.0)),
                                  (50, "forward smash 50% DI down, on the ground", "fsmash", (0.0, -1.0))):
        set_percent(t, states, pc)
        shot = "di-follow-ground" if name.startswith("forward smash 30%") else None
        rows, paths, k0, _ = moved_hit(t, HITS[move], stick, shot=shot)
        pts = paths.get("p+", [])
        if len(pts) < 3:
            raise DiError(f"{name}: slide of {len(pts)} points drawn")
        err = follow_error(pts, rows, k0)
        last = pts[-1]
        rest = rows[min(k0 + last[0] + 3, len(rows) - 1)]
        moved_after = abs(rest["x"] - rows[k0 + last[0]]["x"])
        say(f"{name}: slide frames {pts[0][0]}-{last[0]}, ends at ({last[1]:.3f}, {last[2]:.3f}); the CPU there "
            f"({rows[k0 + last[0]]['x']:.3f}, {rows[k0 + last[0]]['y']:.3f}), {moved_after:.3f} further 3 frames "
            f"later (status {rest['status']:#x}); largest error {err:.4f}")
        if err > SLIDE_TOLERANCE:
            raise DiError(f"{name}: slide off by {err:.3f}")
        worst["slide"] = max(worst["slide"], err)
        table.append(f"{name}: {len(pts)} frames, {err:.3f}")
    # The picture of a landing with a long slide: Mario and the CPU put at
    # x -50 and -40 before the hit, so the forward smash's flight and slide
    # stay on the stage, away from Mario and his hit; the first percent
    # whose line lands and slides for 8 frames or more.
    for pc in (30, 20, 40, 60):
        set_percent(t, states, pc)
        rows, paths, k0, pictures = moved_hit(t, HITS["fsmash"], shot="di-follow-slide", shot_after=6,
                                              places=[(t.mario, (-50.0, 0.01)), (t.fox, (-40.0, 0.01))])
        end = paths.get("p!")
        if end is None or end[0] != "lands" or len(paths.get("p+", [])) < 8:
            say(f"slide picture, forward smash {pc}% at x -40: line ends with {end}, "
                f"{len(paths.get('p+', []))} points after it")
            continue
        mark, left, right = line_both_sides(pictures)
        t.check(mark > 20 and left > 20 and right > 20, "the slide drawn past the landing mark",
                f"forward smash {pc}% at x -40, {len(paths['p+'])} slide frames: {mark} white mark pixels; "
                f"line-blue pixels left of the marks {left}, right of them {right} ({pictures[0][0]})")
        break
    else:
        raise DiError("no forward smash at x -40 landed and slid for the slide picture")
    set_percent(t, states, PERCENT)
    followed = 0
    for name, move, moves in (("underside, CPU moved to (-100, -80) then (0, -80)", "usmash",
                               [(-100.0, -80.0), (0.0, -80.0)]),
                              ("underside, CPU moved to (-100, -80) then (-30, -90)", "usmash",
                               [(-100.0, -80.0), (-30.0, -90.0)]),
                              ("upright wall, CPU moved to (-100, -12)", "fsmash", [(-100.0, -12.0)]),
                              ("wall, CPU moved to (-100, -30)", "fsmash", [(-100.0, -30.0)]),
                              ("wall, CPU moved to (-92, -55)", "fsmash", [(-92.0, -55.0)]),
                              ("slanted side, CPU moved to (-95, -45)", "fsmash", [(-95.0, -45.0)]),
                              ("ledge corner, CPU moved to (-100, -20)", "fsmash", [(-100.0, -20.0)])):
        shot = "di-follow-ceiling" if name.startswith("underside, CPU moved to (-100, -80) then (0, -80)") else None
        rows, paths, k0, _ = moved_hit(t, HITS[move], moves=moves, shot=shot, camera=2 if shot else 0)
        pts = paths.get("p+", [])
        end = paths.get("p!")
        k = next((j for j in range(k0 + 1, len(rows)) if rows[j]["status"] in (STATUS_REFLECT, STATUS_REFLECT + 1)),
                 None)
        real = f"bounce (status {rows[k]['status']:#x}) on frame {k - k0}" if k is not None else "no bounce"
        if not pts:
            say(f"{name}: the line ends at the contact ({end}); the CPU: {real}")
            table.append(f"{name}: ends at the contact ({real})")
            continue
        if k is None or abs(pts[0][0] - (k - k0)) > 1:
            raise DiError(f"{name}: the line bounces on frame {pts[0][0]}, the CPU: {real}")
        # The drawn path against the real one after the bounce, as places;
        # and frame by frame once the bounce frames are lined up. Compared
        # while the CPU is still in its knockback (until a KO, a tech or the
        # end of the bounce).
        shift = (k - k0) - pts[0][0]
        stop = next((j for j in range(k, len(rows)) if rows[j]["status"] not in STATUS_DAMAGE + (STATUS_REFLECT,
                                                                                               STATUS_REFLECT + 1)),
                    len(rows))
        drawn = len(pts)
        pts = [(i, x, y) for i, x, y in pts if k0 + i + shift < stop]
        err = path_error(pts, rows, k - 1, stop - 1)
        timed = follow_error([(i + shift, x, y) for i, x, y in pts], rows, k0)
        say(f"{name}: {real}; the line bounces on frame {pts[0][0]}; after it {drawn} points, {len(pts)} "
            f"compared; largest distance from the real path {err:.3f}, frame by frame (bounces lined up) "
            f"{timed:.3f}")
        if err > BOUNCE_TOLERANCE:
            raise DiError(f"{name}: the path after the bounce is {err:.3f} off the real one")
        worst["bounce"] = max(worst["bounce"], err)
        followed += drawn >= BOUNCE_FRAMES
        table.append(f"{name}: {real}, line's on frame {pts[0][0]}, {drawn} frames drawn after it "
                     f"({len(pts)} compared), path {err:.3f}, timed {timed:.3f}")
    if not followed:
        raise DiError(f"no bounce followed for {BOUNCE_FRAMES} frames: " + "; ".join(table))
    t.ok("after the contact (no tech)", f"slide up to {worst['slide']:.3f}, bounce up to {worst['bounce']:.3f}; "
         + "; ".join(table))


DRAW_DI_LINE = re.compile(r"di: Project\+ Draw DI line ([0-9a-f]{8})")


def draw_di_at(t):
    m = DRAW_DI_LINE.findall(t.emu.log_text())
    if not m or int(m[-1], 16) == 0:
        raise DiError(f"Project+'s Draw DI line not found by the plugin ({m})")
    return int(m[-1], 16)


def sc_drawdi(t):
    """Project+'s Draw DI (its Code Menu value) is switched off while the DI
    Display is on and put back when it goes off; a change made in the Code
    Menu meanwhile is kept. With Draw DI on in the Code Menu, the frozen
    preview (sc_frozen) is accurate."""
    e = t.emu
    at = draw_di_at(t)
    value = lambda: e.u32(at)  # noqa: E731
    word = lambda v: e.h.call("write_mem", addr=at, hex=f"{v:08x}")  # noqa: E731
    t.load()
    seen = []
    t.set(S_DI, 0)
    e.h.call("frame_advance", n=2)
    word(1)  # the player turns Draw DI on in the Code Menu
    e.h.call("frame_advance", n=2)
    seen.append(("off, Draw DI on", value()))
    t.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    seen.append(("on", value()))
    t.set(S_DI, 0)
    e.h.call("frame_advance", n=2)
    seen.append(("off again", value()))
    word(0)  # the player turns it off while ours is off
    e.h.call("frame_advance", n=2)
    t.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    seen.append(("Draw DI off, ours on", value()))
    word(1)  # and on in the Code Menu while ours is on
    e.h.call("frame_advance", n=2)
    seen.append(("Draw DI turned on while ours is on", value()))
    t.set(S_DI, 0)
    e.h.call("frame_advance", n=2)
    seen.append(("ours off", value()))
    want = [1, 0, 1, 0, 0, 1]
    t.check([v for _, v in seen] == want, "Project+'s Draw DI hidden while on, put back after",
            "; ".join(f"{k}: {v}" for k, v in seen) + f" (line value at {at:08x})")
    # The clash: Draw DI on in the Code Menu, ours on, frame advance in hitlag.
    # sc_frozen starts from t.base, so this state stands in for it, and the
    # base the other scenarios load is put back after. (Saving the state the
    # frozen test ended in as the base left Mario launched off the stage for
    # every later scenario: cpu-sdi's forward smash then hit nothing.)
    base = t.base
    t.base = os.path.splitext(base)[0] + "-drawdi.sav"
    t.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    e.h.call("save_state", path=t.base)
    try:
        sc_frozen(t)
    finally:
        t.base = base
        t.set(S_DI, 0)
        e.h.call("frame_advance", n=2)
        restored = value()
        word(0)
        t.set(S_DI, 1)
        e.h.call("frame_advance", n=2)
    t.check(restored == 1, "Draw DI back on after the frozen test", f"value {restored}")


def sc_frozen(t):
    """Frame advance in hitlag: the human's path follows the pad stick while
    the game stands still, and the last one drawn is where Mario goes."""
    e = t.emu
    t.load()
    toward = -t.toward  # Fox attacks Mario
    fox = frames_to_steps({1: (B_ATTACK | B_SMASH, toward, 0.0), 2: (B_ATTACK | B_SMASH, toward, 0.0)}, 40)
    e.neutral(0)
    rows = t.r.inp.run({1: fox}, read=lambda i: t.mario.read(), until=lambda i, rows: rows[-1]["hitlag"] > 0)
    if rows[-1]["hitlag"] <= 0:
        raise DiError("Fox's forward smash did not reach Mario")
    t.set(S_FRAME_ADVANCE, 1)
    e.h.call("frame_advance", n=3)
    still0 = t.mario.read()
    lag0 = still0["hitlag"]
    anim0 = t.fox_anim()
    seen = {}
    for name, stick in (("up", (128, 228)), ("down", (128, 28)), ("away", (228 if toward < 0 else 28, 128)),
                        ("half up", (128, 168))):
        e.hold(0, stick=stick)
        e.h.call("frame_advance", n=4)
        at = len(e.log_text())
        t.trace(1)
        e.h.call("frame_advance", n=2)
        t.trace(0)
        paths, head, st = t.paths(e.log_text()[at:], 1)
        # A slide alone ("p+") when the stick down keeps Mario on the ground.
        if not paths or not (paths.get("p") or paths.get("p+")):
            raise DiError(f"frozen {name}: no path logged")
        seen[name] = (paths.get("p") or paths.get("p+"), st)
        t.shot(f"di-frozen-{name.replace(' ', '-')}")
    still = t.mario.read()
    t.check((still["hitlag"], still["x"], still["y"], t.fox_anim()) == (lag0, still0["x"], still0["y"], anim0),
            "frame advance holds the game", f"Mario's hitlag {lag0} and position, Fox's animation frame {anim0:.1f} "
            "unchanged over the frozen frames")
    ends = {k: v[0][-1][1:] for k, v in seen.items()}
    t.check(len(set(ends.values())) == len(ends), "the path follows the stick while frozen",
            "; ".join(f"{k}: stick {v[1].group(8)},{v[1].group(9)} end ({ends[k][0]:.1f}, {ends[k][1]:.1f})"
                      for k, v in seen.items()))
    # Hold up and let the game run: Mario goes where the "up" path showed.
    e.hold(0, stick=(128, 228))
    e.h.call("frame_advance", n=4)
    at = len(e.log_text())
    t.trace(1)
    e.h.call("frame_advance", n=2)
    t.trace(0)
    paths, head, st = t.paths(e.log_text()[at:], 1)
    t.set(S_FRAME_ADVANCE, 0)
    mario_w = e.watch(lambda *_: t.mario.read())
    rows = [mario_w.read()] + e.trace(120, mario_w)
    # rows[0] is the frozen hitlag frame. The path's first point is the
    # launch frame: the first row without hitlag.
    start = next(k for k, r in enumerate(rows) if r["hitlag"] == 0) - 1
    err, n, why = compare(paths["p"], rows, start)
    e.neutral(0)
    t.check(err <= TOLERANCE and n > 0, "human pad: Mario flies along the path drawn",
            f"largest error {err:.4f} over {n} frames ({why})")


LINE_BLUE = lambda p: p[2] > 200 and 100 < p[1] < 175 and p[0] < 60  # noqa: E731


def pixels(shot, test):
    """The set of pixels of `shot` that pass `test`."""
    pic = DO.picture(shot)
    return {(x, y) for y in range(pic.h) for x in range(pic.w) if test(pic.px(x, y))}


def sc_boxes(t):
    """Project+'s hitbox and hurtbox capsules and its collision lines are
    drawn in the same pass, before the DI Display's lines (Display Options >
    Hitboxes, Environment Collision). The frozen frame of sc_frozen with
    the stick up is photographed with neither, each and both: the line's
    pixels must stay where they are."""
    e = t.emu
    t.load()
    toward = -t.toward  # Fox attacks Mario
    fox = frames_to_steps({1: (B_ATTACK | B_SMASH, toward, 0.0), 2: (B_ATTACK | B_SMASH, toward, 0.0)}, 40)
    e.neutral(0)
    rows = t.r.inp.run({1: fox}, read=lambda i: t.mario.read(), until=lambda i, rows: rows[-1]["hitlag"] > 0)
    if rows[-1]["hitlag"] <= 0:
        raise DiError("Fox's forward smash did not reach Mario")
    t.set(S_FRAME_ADVANCE, 1)
    e.hold(0, stick=(128, 228))
    e.h.call("frame_advance", n=6)
    still0 = t.mario.read()
    shots = {}
    for name, di, hitboxes, env in (("off", 1, 0, 0), ("hitboxes", 1, 3, 0), ("collision", 1, 0, 1),
                                    ("both", 1, 3, 1), ("no-di", 0, 3, 1)):
        t.set(S_DI, di)
        t.set(DO.S_HITBOXES, hitboxes)
        t.set(DO.S_ENV, env)
        e.h.call("frame_advance", n=3)
        shots[name] = t.shot(f"di-boxes-{name}")
    still = t.mario.read()
    t.set(S_DI, 1)
    t.set(DO.S_HITBOXES, 0)
    t.set(DO.S_ENV, 0)
    t.set(S_FRAME_ADVANCE, 0)
    e.neutral(0)
    if (still["hitlag"], still["x"], still["y"]) != (still0["hitlag"], still0["x"], still0["y"]):
        raise DiError("the game did not stand still for the pictures")
    blue = {name: pixels(s, LINE_BLUE) for name, s in shots.items()}
    base = blue["off"]
    if len(base) < 100:
        raise DiError(f"no line on the frozen frame ({len(base)} line-blue pixels)")
    drawn = len(pixels(shots["no-di"], lambda p: DO.red(p) or DO.yellow(p) or DO.line_colour(p)))
    t.ok("boxes: the capsules and collision lines are on the frozen frame",
         f"{drawn} red, yellow or collision-coloured pixels with both on and the DI Display off")
    for name in ("hitboxes", "collision", "both"):
        kept = len(base & blue[name])
        stray = len(blue[name] - base - blue["no-di"])
        t.check(drawn > 300 and kept >= 0.97 * len(base) and stray <= 0.03 * len(base),
                f"boxes: the line is in the same place with {name} drawn before it",
                f"{len(base)} line-blue pixels alone; with {name}: {kept} of them still line-blue, "
                f"{stray} line-blue pixels elsewhere")


def sc_off(t):
    """Option off: nothing is drawn or computed."""
    e = t.emu
    rows = t.hit(HITS["fsmash"], {}, frames=20)
    h = first_hit(rows)
    t.load()
    b, x, y = HITS["fsmash"]
    mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
    t.r.inp.run({0: mario, 1: frames_to_steps({}, h + 2)})
    nav.hold_game_still(e, True)
    on = t.shot("di-on")
    t.set(S_DI, 0)
    at = len(e.log_text())
    t.trace(2)
    e.h.call("frame_advance", n=3)
    off = t.shot("di-off")
    t.trace(0)
    lines = len(re.findall(r"di: (?:P\d|[pnl] )", e.log_text()[at:]))
    t.set(S_DI, 1)
    nav.hold_game_still(e, False)
    n_on = DO.count(on, DO.FULL, LINE_BLUE)
    n_off = DO.count(off, DO.FULL, LINE_BLUE)
    t.check(n_on > 100 and n_off < 10 and lines == 0, "option off draws nothing",
            f"line-blue pixels on {n_on}, off {n_off}; trace lines while off {lines}")


def drawn_rows(t, text, player, rows):
    """(row index, paths) for every drawing of `player`'s paths in `text`
    made with one hitlag frame left, matched to the row (its position and
    hitlag) it was drawn from."""
    out = []
    for m in TRACE_HEAD.finditer(text):
        if int(m.group(1)) != player or int(m.group(4)) != 1:
            continue
        x, y = float(m.group(2)), float(m.group(3))
        k = next((k for k, r in enumerate(rows) if r["hitlag"] == 1 and abs(r["x"] - x) < 0.002
                  and abs(r["y"] - y) < 0.002), None)
        paths, _, _ = t.paths(text[m.start():], player, 1)
        if k is not None and paths and "p" in paths:
            out.append((k, paths))
    return out


def sc_second(t):
    """Hits during hitstun (Mario's up special hits several times; each hit
    after the first lands while the CPU is still in hitstun): the path drawn
    for the last hit against where the CPU goes."""
    t.load()
    at = len(t.emu.log_text())
    t.trace(2)
    up_b = frames_to_steps({1: (B_SPECIAL, 0.0, 1.0), 2: (B_SPECIAL, 0.0, 1.0)}, 4)
    rows = t.r.inp.run({0: up_b, 1: frames_to_steps({}, 4)}, read=lambda i: t.fox.read(), extra=150)
    t.trace(0)
    hits = [k for k in range(1, len(rows)) if rows[k]["hitlag"] > rows[k - 1]["hitlag"]]
    during = [k for k in hits if rows[k]["hitstun"] > 0 and rows[k - 1]["hitstun"] > 0]
    if not during:
        raise DiError(f"no hit during hitstun (hits on rows {hits})")
    drawn = drawn_rows(t, t.emu.log_text()[at:], 2, rows)
    last = [d for d in drawn if d[0] > during[-1]]
    if not last:
        raise DiError(f"no path drawn after the hit on row {during[-1]} (drawings at rows {[d[0] for d in drawn]})")
    k, paths = last[0]
    err, n, why = compare(paths["p"], rows, k)
    t.check(err <= TOLERANCE and n > 0, "hits during hitstun",
            f"{len(hits)} hits, {len(during)} of them during hitstun (rows {during}); the last one's path: "
            f"largest error {err:.4f} over {n} frames ({why})")


def pad_hit(t, name, stick=(128, 128), cstick=(128, 128), tol=TOLERANCE):
    """Fox hits Mario; Mario's pad holds `stick` and `cstick` from the
    middle of hitlag on. The path drawn on the last hitlag frame against
    where Mario goes."""
    e = t.emu
    t.load()
    toward = -t.toward
    fox = frames_to_steps({1: (B_ATTACK | B_SMASH, toward, 0.0), 2: (B_ATTACK | B_SMASH, toward, 0.0)}, 4)
    e.neutral(0)
    state = {}
    last = [t.mario.read()]

    def read_mario(i):
        r = t.mario.read()
        last[0] = r
        return r

    def each(i):
        r = last[0]
        if r["hitlag"] > 3 and "held" not in state:
            e.hold(0, stick=stick, cstick=cstick)
            state["held"] = i
        if r["hitlag"] == 2 and "at" not in state:
            state["at"] = len(e.log_text())
            t.trace(1)
        elif r["hitlag"] == 0 and "at" in state and "off" not in state:
            t.trace(0)
            state["off"] = i
    rows = t.r.inp.run({1: fox}, read=read_mario, each=each, extra=110)
    t.trace(0)
    e.neutral(0)
    if "at" not in state:
        raise DiError("Fox's forward smash did not reach Mario")
    text = e.log_text()[state["at"]:]
    drawn = drawn_rows(t, text, 1, rows)
    if not drawn:
        raise DiError(f"{name}: no path drawn on Mario's last hitlag frame")
    k, paths = drawn[-1]
    _, _, head = t.paths(text, 1, 1)
    err, n, why = compare(paths["p"], rows, k)
    launch = (rows[k + 1]["x"] - rows[k]["x"], rows[k + 1]["y"] - rows[k]["y"])
    t.check(err <= tol and n > 0, f"human pad, {name}",
            f"stick read {head.group(8)},{head.group(9)}; launch frame moved ({launch[0]:.3f}, {launch[1]:.3f}), "
            f"predicted ({paths['p'][0][1] - rows[k]['x']:.3f}, {paths['p'][0][2] - rows[k]['y']:.3f}); largest "
            f"error {err:.4f} over {n} frames ({why})")


def sc_pad(t):
    """A human's partial stick, and the C-stick's automatic smash DI."""
    pad_hit(t, "partial stick (pad +40, +40)", stick=(128 + 40, 128 + 40))
    pad_hit(t, "C-stick up (ASDI)", cstick=(128, 228))
    pad_hit(t, "partial C-stick down (pad 0, -62)", cstick=(128, 128 - 62))
    pad_hit(t, "shallow diagonal C-stick (pad +70, +22)", cstick=(128 + 70, 128 + 22), tol=0.25)


def sc_savestate(t):
    """The lab's savestate loaded while a path is drawn (D-pad right held to
    save, D-pad left to load)."""
    e = t.emu
    t.load()
    e.h.call("resume")
    e.hold(0, buttons=["DRIGHT"])
    e.advance(20)
    e.neutral(0)
    e.advance(10)
    saved = len(re.findall(r"savestate: saved frame", e.log_text()))
    e.h.call("pause")
    b, x, y = HITS["fsmash"]
    mario = frames_to_steps({1: (b, x * t.toward, y), 2: (b, x * t.toward, y)}, 4)
    t.r.inp.run({0: mario, 1: frames_to_steps({}, 25)}, read=lambda i: t.fox.read(),
                until=lambda i, rows: rows[-1]["hitlag"] > 0)
    e.h.call("frame_advance", n=2)
    t.shot("di-before-load")
    e.h.call("resume")
    e.hold(0, buttons=["DLEFT"])
    e.advance(4)
    e.neutral(0)
    e.advance(30)
    loads = len(re.findall(r"savestate: loaded frame", e.log_text()))
    s = t.shot("di-after-load")
    t.check(saved >= 1 and loads >= 1 and nav.game_frame(e) > 0, "savestate load while a path is drawn",
            f"{saved} save(s), {loads} load(s), the game runs on (frame {nav.game_frame(e)})")
    return s


# The lab's CPU Options (cpu::CpuOptions in main::Settings, from
# DO.S_CPU_CONTROL; the indices are the rows' labels,
# src/main/ui/pages/cpu_options.cpp).
S_CPU_TDI, S_CPU_SDI = DO.S_CPU_TDI, DO.S_CPU_SDI
S_CPU_SDI_DIR, S_CPU_ASDI = DO.S_CPU_CONTROL + 4, DO.S_CPU_CONTROL + 22
CPU_TDI = {"Random": 0, "Inwards": 1, "Outwards": 2, "Survival": 3, "None": 4}
CPU_SDI_DIR = {"Auto": 0, "Left": 4, "Right": 5, "Up": 6}
CPU_ASDI = {"Auto": 0, "Up": 5}
SDI_UNITS = 6.0  # Project+'s smash DI distance (parameter 3174) for a full stick
ASDI_UNITS = 3.0  # and its automatic smash DI's (3175)
LAB_PERCENT = 50  # the CPU's percent for cpu-sdi (the CPU audit's forward smash: 50)
P1_ATTACKS = {"usmash": (["A"], 0.0, 1.0), "fsmash": (["A"], 1.0, 0.0), "utilt": (["A"], 0.0, 0.45), "jab": (["A"], 0.0, 0.0)}


CONTROLLER_VTABLE = 0x80AE4EF0  # soControllerImpl
# The fighter's controller module (module enumeration +0x5C) forwards
# getFlickX and the other controller getters to the controller at its +0x14C
# (its getFlickX, 0x8085B9D4, read in the game's code).
MODULE_CONTROLLER = 0x14C


def cpu_controller(t):
    """The CPU's soControllerImpl, the one smash DI reads the flick counters
    of through the controller module."""
    e = t.emu
    module = u32(t.fox.mods(), 0x5C)
    p = e.u32(module + MODULE_CONTROLLER)
    if not e.is_ram(p) or e.u32(p) != CONTROLLER_VTABLE:
        raise DiError(f"no controller at {module:08x}+{MODULE_CONTROLLER:x}: {p:08x}")
    return p


def lab_hit(t, attack, tdi="None", sdi=0, sdi_dir="Auto", asdi="Auto", frames=40, percent=None,
            di_display=False):
    """The lab's CPU Options drive the CPU's DI (not a script): from the base
    state, P1's `attack` through P1's pad (Dolphin's controller path), and
    the CPU's rows, one per game frame. Returns (rows, index of the first row
    in hitlag, index of the first row after it)."""
    e = t.emu
    t.load()
    if percent is not None:
        e.h.call("write_mem", addr=t.r.lab.settings + S_CPU_PERCENT, hex=f"{percent:04x}01")
    t.set(S_CPU_TDI, CPU_TDI[tdi])
    t.set(S_CPU_SDI, sdi)
    t.set(S_CPU_SDI_DIR, CPU_SDI_DIR[sdi_dir])
    t.set(S_CPU_ASDI, CPU_ASDI[asdi])
    t.set(S_DI, 1 if di_display else 0)
    buttons, x, y = P1_ATTACKS[attack]
    stick = (int(round(128 + 127 * x * t.toward)), int(round(128 + 127 * y)))
    ctl = cpu_controller(t)

    def read_row(_i=None):
        row = t.fox.read()
        row["air"] = e.u32(u32(t.fox.mods(), 0x14) + 0x2C) == 2  # situation: 2 in the air
        raw = e.read(ctl, 0x80)
        # stick, previous stick, flick X and Y counters
        row["ctl"] = (round(f32(raw, 0x8), 2), round(f32(raw, 0xC), 2), round(f32(raw, 0x10), 2),
                      round(f32(raw, 0x14), 2), raw[122], raw[124])
        return row

    def each(i, _rows):
        if i == 0:
            e.hold(0, buttons=buttons, stick=stick)
        elif i == 3:
            e.neutral(0)

    try:
        rows = e.trace(frames, e.watch(read_row), each)
    finally:
        e.release(0)
    start = next((k for k, r in enumerate(rows) if r["hitlag"] > 0), None)
    if start is None:
        raise DiError(f"{attack}: P1's attack did not hit the CPU")
    end = next((k for k in range(start, len(rows)) if rows[k]["hitlag"] == 0), None)
    if end is None:
        raise DiError(f"{attack}: the CPU's hitlag did not end")
    return rows, start, end


def hitlag_jumps(rows, start, end):
    """The CPU's moves of more than a unit from one hitlag row to the next
    (each a smash DI): [(row from the first hitlag row, dx, dy)], and its
    whole move over hitlag."""
    jumps = []
    for k in range(start + 1, end):
        dx, dy = rows[k]["x"] - rows[k - 1]["x"], rows[k]["y"] - rows[k - 1]["y"]
        if math.hypot(dx, dy) > 1.0:
            jumps.append((k - start, round(dx, 2), round(dy, 2)))
    total = math.hypot(rows[end - 1]["x"] - rows[start]["x"], rows[end - 1]["y"] - rows[start]["y"])
    return jumps, total


def sc_cpu_sdi(t):
    """The lab CPU's own DI makes exactly the smash DI its options ask for
    (the CPU input path writes the stick flick counters on its DI frame):
    trajectory DI alone moves the CPU nowhere in hitlag, ASDI Up moves it
    the ASDI distance once, Smash DI Amount N makes N pushes of the SDI
    distance (or as many as hitlag has room for), with or without DI, on
    hits with odd and even hitlag lengths. Measured before the fix (main at
    bac5aed): every DI'd hit smash DIed once more on its last hitlag frame
    (forward smash Inwards (-4.17, +4.32), ASDI Up 9.0 high instead of 3.0,
    Smash DI N with DI N + 1 pushes)."""
    lines, bad = [], []

    def check(cond, name, detail):
        if cond:
            t.ok(name, detail)
        else:
            t.say(f"FAIL {name}: {detail}")
            bad.append(name)

    def still(attack, tdi):
        rows, s, n = lab_hit(t, attack, tdi=tdi, percent=LAB_PERCENT)
        jumps, total = hitlag_jumps(rows, s, n)
        # The controller on the DI frame: stick, previous stick, flick X, Y.
        lines.append(f"{attack} TDI {tdi}: hitlag rows {n - s}, moved {total:.2f}, jumps {jumps}; "
                     f"controller on the last hitlag frame {rows[n - 1]['ctl']}")
        check(not jumps and total < 0.5, f"cpu-sdi {attack} TDI {tdi}: no smash DI in hitlag",
                f"hitlag rows {n - s}, moved {total:.2f}, jumps {jumps}")
        return rows, s, n

    for attack, tdi in (("fsmash", "Inwards"), ("fsmash", "Outwards"), ("jab", "Random"), ("utilt", "Inwards"),
                        ("usmash", "Inwards")):
        still(attack, tdi)
    # ASDI Up with TDI None: the step as hitlag ends, against no ASDI.
    base, bs, bn = still("fsmash", "None")
    up, us, un = lab_hit(t, "fsmash", asdi="Up", percent=LAB_PERCENT)
    jumps, _ = hitlag_jumps(up, us, un)
    step_base = base[bn]["y"] - base[bn - 1]["y"]
    step_up = up[un]["y"] - up[un - 1]["y"]
    extra = (up[un]["y"] - up[us]["y"]) - (base[bn]["y"] - base[bs]["y"])
    lines.append(f"fsmash ASDI Up: hitlag jumps {jumps}, step at hitlag end {step_up:+.2f} "
                 f"(no ASDI {step_base:+.2f}), higher by {extra:.2f}")
    check(not jumps and abs(step_up - step_base - ASDI_UNITS) < 0.25 and abs(extra - ASDI_UNITS) < 0.25,
            "cpu-sdi ASDI Up: the ASDI distance once, no smash DI",
            f"hitlag jumps {jumps}, step at hitlag end {step_up:+.2f} (none: {step_base:+.2f}), "
            f"higher than with no ASDI by {extra:.2f} (want {ASDI_UNITS})")
    # Smash DI Amount N, with and without trajectory DI.
    room = {}
    for attack in ("fsmash", "utilt", "jab"):
        for tdi in ("None", "Inwards"):
            for n in (7, 1, 3):
                rows, s, e_ = lab_hit(t, attack, tdi=tdi, sdi=n, sdi_dir="Up", percent=LAB_PERCENT)
                jumps, _ = hitlag_jumps(rows, s, e_)
                lines.append(f"{attack} TDI {tdi} Smash DI {n} Up: hitlag rows {e_ - s}, jumps {jumps}")
                sizes_ok = all(abs(dy - SDI_UNITS) < 0.1 and abs(dx) < 0.1 for _, dx, dy in jumps)
                if n == 7 and tdi == "None":
                    room[attack] = len(jumps)
                want = min(n, room[attack])
                check(sizes_ok and len(jumps) == want and want > 0,
                        f"cpu-sdi {attack} TDI {tdi} Smash DI {n} Up: {want} push(es) of {SDI_UNITS}",
                        f"hitlag rows {e_ - s}, jumps {jumps}")
    for line in lines:
        t.say("  " + line)
    # The base state again: the CPU Options and the DI Display as the
    # scenarios after this one (platform) found them before cpu-sdi.
    t.load()
    if bad:
        raise DiError(f"cpu-sdi: {len(bad)} check(s) failed: " + "; ".join(bad))
    t.ok("cpu-sdi", f"pushes hitlag has room for: {room}")


IP_PAD_CONFIG_PTR = 0x805A00C8  # ipPadConfig::getInstance() global pointer


def sc_cstick_modes(t):
    """P+ 3.2.1 C-stick bindings (ipPadConfig GC port 0 +0x08): verifies
    di_display.cpp matches actual post-hitlag flight across Smash (5),
    Tilt (0xC), Attack (0), and Taunt (0xA) for both:
      1) hitlag > 1 dual-stick SDI pulse + return-to-main + mode-independent ASDI,
      2) hitlag == 1 launch-frame fresh C-stick TDI override + ASDI."""
    e = t.emu
    cfg = e.ptr(IP_PAD_CONFIG_PTR)
    if not e.is_ram(cfg):
        raise DiError(f"ipPadConfig pointer invalid: {cfg:08x}")
    cstick_addr = cfg + 0x08  # GC contNo 0 C-stick binding byte
    orig_mode = e.u8(cstick_addr)
    modes = [("Smash", 5, 2), ("Tilt", 0xC, 1), ("Attack", 0, 1), ("Taunt", 0xA, 0)]
    toward = -t.toward  # Fox attacks Mario
    fox_fsmash = frames_to_steps({1: (B_ATTACK | B_SMASH, toward, 0.0), 2: (B_ATTACK | B_SMASH, toward, 0.0)}, 40)

    def freeze_mario_hit(target_lag, hold_before=None):
        t.load()
        e.neutral(0)
        held_before = [False]

        def read_m(_i):
            r = t.mario.read()
            if hold_before is not None and r["hitlag"] > target_lag and not held_before[0]:
                e.hold(0, stick=hold_before)
                held_before[0] = True
            return r

        rows = t.r.inp.run({1: fox_fsmash}, read=read_m,
                           until=lambda _i, rs: rs[-1]["hitlag"] == target_lag, extra=40)
        t.r.inp.stop()
        if not rows or rows[-1]["hitlag"] != target_lag:
            raise DiError(f"Mario never reached hitlag {target_lag}")
        t.set(S_FRAME_ADVANCE, 1)
        e.h.call("frame_advance", n=3)

    launch_angles = {}
    try:
        # Part 1: hitlag == 4 (main stick already held Right, fresh C-stick Up).
        for name, mode, want_jumps in modes:
            freeze_mario_hit(4, hold_before=(228, 128))
            e.h.call("write_mem", addr=cstick_addr, hex=f"{mode:02x}")
            e.hold(0, stick=(228, 128), cstick=(128, 228))
            e.h.call("frame_advance", n=4)
            at = len(e.log_text())
            t.trace(1)
            e.h.call("frame_advance", n=2)
            t.trace(0)
            paths, _, head = t.paths(e.log_text()[at:], 1)
            if not paths or "p" not in paths:
                raise DiError(f"cstick-modes hitlag {name}: no predicted path logged")
            t.set(S_FRAME_ADVANCE, 0)
            mario_w = e.watch(lambda *_: t.mario.read())
            rows = [mario_w.read()] + e.trace(110, mario_w)
            e.neutral(0)
            launch_k = next(k for k, r in enumerate(rows) if r["hitlag"] == 0)
            jumps, _ = hitlag_jumps(rows, 0, launch_k)
            err, n, why = compare(paths["p"], rows, launch_k - 1)
            t.check(len(jumps) == want_jumps and err <= TOLERANCE and n > 0,
                    f"cstick-modes hitlag {name} (0x{mode:x}): {want_jumps} SDI jump(s) and path accurate",
                    f"jumps {jumps}, largest error {err:.4f} over {n} frames ({why})")

        # Part 2: hitlag == 1 (Mario neutral until hitlag 1, then main Left + fresh C-stick Up).
        for name, mode, _ in modes:
            freeze_mario_hit(1, hold_before=None)
            e.h.call("write_mem", addr=cstick_addr, hex=f"{mode:02x}")
            e.hold(0, stick=(28, 128), cstick=(128, 228))
            e.h.call("frame_advance", n=4)
            at = len(e.log_text())
            t.trace(1)
            e.h.call("frame_advance", n=2)
            t.trace(0)
            paths, _, head = t.paths(e.log_text()[at:], 1)
            if not paths or "p" not in paths:
                raise DiError(f"cstick-modes launch {name}: no predicted path logged")
            t.set(S_FRAME_ADVANCE, 0)
            mario_w = e.watch(lambda *_: t.mario.read())
            rows = [mario_w.read()] + e.trace(110, mario_w)
            e.neutral(0)
            launch_k = next(k for k, r in enumerate(rows) if r["hitlag"] == 0)
            err, n, why = compare(paths["p"], rows, launch_k - 1)
            vx = rows[launch_k + 1]["x"] - rows[launch_k]["x"]
            vy = rows[launch_k + 1]["y"] - rows[launch_k]["y"]
            ang = math.degrees(math.atan2(vy, vx))
            launch_angles[name] = ang
            t.check(err <= TOLERANCE and n > 0,
                    f"cstick-modes launch {name} (0x{mode:x}): path accurate",
                    f"launch angle {ang:.2f} deg, largest error {err:.4f} over {n} frames ({why})")

        # Verify Smash, Tilt, Attack, and Taunt actually produced distinct launch angles.
        s_a, ti_a, at_a, ta_a = (launch_angles[k] for k in ("Smash", "Tilt", "Attack", "Taunt"))
        t.check(abs(s_a - ta_a) > 8.0 and abs(ti_a - s_a) > 2.0 and abs(ti_a - ta_a) > 4.0 and abs(at_a - ta_a) > 4.0,
                "cstick-modes launch angles reflect each C-stick mode",
                f"Smash {s_a:.2f}, Tilt {ti_a:.2f}, Attack {at_a:.2f}, Taunt {ta_a:.2f} deg")
    finally:
        e.h.call("write_mem", addr=cstick_addr, hex=f"{orig_mode:02x}")
        t.set(S_FRAME_ADVANCE, 0)
        e.neutral(0)
        t.load()


def run_test(t, states, only):
    e = t.emu
    t.setup(states, PERCENT)
    m = TRACE_BYTE.findall(e.log_text())
    if not m:
        raise DiError("no trace byte line in the log")
    t.trace_at = int(m[-1], 16)
    t.set(S_DI, 1)
    e.h.call("frame_advance", n=2)
    e.h.call("save_state", path=t.base)
    for name, fn in (("accuracy", lambda: sc_accuracy(t, only)), ("frozen", lambda: sc_frozen(t)),
                     ("boxes", lambda: sc_boxes(t)),
                     ("off", lambda: sc_off(t)), ("second", lambda: sc_second(t)), ("pad", lambda: sc_pad(t)),
                     ("cstick-modes", lambda: sc_cstick_modes(t)),
                     ("savestate", lambda: sc_savestate(t)), ("land", lambda: sc_land(t, states)),
                     ("wall", lambda: sc_wall(t, states)), ("follow", lambda: sc_follow(t, states)),
                     ("drawdi", lambda: sc_drawdi(t)), ("cpu-sdi", lambda: sc_cpu_sdi(t)),
                     ("platform", lambda: sc_platform(t, states))):
        if not only or name in only or (name == "accuracy" and any(k in HITS for k in only)):
            t.step = name
            fn()


def cmd_test_di(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-di")
    ap.add_argument("rel")
    ap.add_argument("--measure", action="store_true")
    ap.add_argument("--measure-pad", action="store_true")
    ap.add_argument("--measure-ray", action="store_true")
    ap.add_argument("--measure-bounce", action="store_true")
    ap.add_argument("--only", nargs="*", default=None)
    opts = ap.parse_args(rest)
    label = "test-di"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, step, log_path = "", "boot", ""
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    out = []
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS) as (proc, emu):
            log_path = emu.log_path
            t = Tester(emu, out)
            try:
                if opts.measure_bounce:
                    step = "measure bounce"
                    measure_bounce(t, states)
                elif opts.measure_ray:
                    step = "measure ray"
                    measure_ray(t, states, opts.only or [])
                elif opts.measure_pad:
                    step = "measure pad"
                    measure_pad(t, states)
                elif opts.measure:
                    step = "measure"
                    measure(t, states)
                else:
                    t.step = "base state"
                    run_test(t, states, opts.only or [])
            except (Timeout, nav.NavError, HarnessError, DiError, DO.DisplayError, LT.LabError,
                    PI.InputError, struct.error, KeyError, StopIteration, TypeError) as e:
                why = f"{getattr(t, 'step', step)}: {type(e).__name__}: {e}"
                try:
                    emu.h.call("resume")
                    t.shots.append(emu.screenshot("fail-di"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    if out:
        name = "di-measure.txt" if opts.measure else ("di-pad.txt" if opts.measure_pad else
                                                      ("di-ray.txt" if opts.measure_ray else
                                                       ("di-bounce.txt" if opts.measure_bounce else "di-test.txt")))
        path = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), name)
        with open(path, "w") as f:
            f.write("\n".join(out) + "\n")
        print(f"written: {path}")
    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for shot in (t.shots if t else []):
        print(f"screenshot: {shot[0]} ({shot[1]}x{shot[2]})")
    log = E.read_log(log_path) if log_path else ""
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
