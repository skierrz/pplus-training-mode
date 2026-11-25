"""Emulator tests of the savestate feature (src/main/savestate/), run through
emu.py; settings come from the environment the same way.

  test-savestate REL [--long] [--only NAME ...] [--cycles N]
                     [--hmn NAME] [--cpu NAME] [--stage NAME|KIND]
                     [--sweep NAME ...] [--side hmn|cpu] [--stages NAME|KIND ...]
                     [--situations TEXT ...] [--window N] [--fix-ids] [--dump]
      One boot. The game is driven at full speed with frame-exact controller
      scripts on port 1 (the harness's pad_script) and held at exact frames
      by the plugin's test block: it parks the game at the top of the main
      loop once the match's frame counter reaches a target, takes save and
      load requests there, and logs one `sst` line per frame: the frame
      counter, the random number generator, both fighters (position, status,
      animation frame, damage, hitstun, hitlag; partners too), a hash of the
      fighters' instance heaps byte for byte and a hash of every set heap's
      block count and bytes in use. A save logs the saved frame's line (tag
      S), a load the restored frame's line (tag L). (Stepping with the
      harness's frame_advance is not used around saves and loads: every
      store is slow then, a save took about 30 s.)

      Checked every cycle: the restored line equals the saved line in every
      field; after the load the same inputs as after the save give the same
      line on every frame (the first pass is the reference). The two inputs
      read before the save are replayed before every load, so the game's
      pad edges are the same after both.

      Quick scenarios (default): basic (Mario vs Fox CPU on Final
      Destination, 12 cycles), dpad (hold right 10 frames to save, left to
      load, the D-pad hidden from the game only while bound), refusals
      (load with no state, save while paused, two CPUs: the state is cleared
      and saving refused), exit (Finish after loads: back on the character
      select, the module unloaded with its heap at baseline, the slot freed,
      observers balanced). --long adds the matrix (LONG below); --only picks
      scenarios by name.

      --hmn, --cpu and --stage choose the match of the scenarios that do not
      name their own (Mario, Fox, Final Destination). Two scenarios run only
      when named with --only: sweep, a reduced scenario for any character
      and stage (see "the cast sweep" below; --sweep lists the actors,
      --side makes them the CPU, --stages the stages), and transform (what
      saves and loads answer around Zelda's and Sheik's change).

The development session commands (`session start REL | run ARGS | quit`)
keep one Dolphin running across invocations; only the process `start`
launched is ever stopped.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True

import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import Emu, Harness, HarnessError, Timeout  # noqa: E402

SAVE, LOAD = 1, 2
PROBE_MAGIC = 0x544D5350
# Probe block offsets (src/main/savestate/trace.h).
PR_TRACE, PR_REQUEST, PR_PARK_AT, PR_RELEASE, PR_PARK_SEQ, PR_PARKED = 0x4, 0x5, 0x8, 0xC, 0x10, 0x14
PR_RULES = 0x6
# The CPU code's harness script block (src/main/cpu/script_source.h): steps
# of {game-action buttons, stick x, y (-1..1), frames}, played into one
# player's input object from the first input call after the generation
# changes, one step frame per game step.
SCRIPT_BLOCK = re.compile(r"cpu script: control block at ([0-9a-f]{8})")
SCRIPT_MAGIC, SCRIPT_STEPS = 0x544D4344, 96
SCRIPT_BLOCK_SIZE = 0x3C + 16 * SCRIPT_STEPS  # the second block follows the first
# Controller buttons as the game-action bits (ipPadButton) the plugin's
# script writes into the human's input object: attack, special, jump, guard
# (a grab is attack with guard); the C-stick is a smash attack.
ACTION_BITS = {"A": 0x1, "B": 0x2, "X": 0x4, "Y": 0x4, "R": 0x8, "L": 0x8, "Z": 0x9}
SMASH = 0x20
BLOCK = re.compile(r"savestate: test block at ([0-9a-f]{8}); layout (\w+)")
SAVED = re.compile(r"savestate: saved frame (\d+): (\d+) bytes in (\d+) segments \(without fighter free space "
                   r"(\d+)\), slot (\d+), copy ([\d.]+) ms")
LOADED = re.compile(r"savestate: loaded frame (\d+) \(load (\d+)\): copy ([\d.]+) ms, checksum ([\d.]+) ms")
REFUSED = re.compile(r"savestate: (save|load) refused: ([^\r\n]+)")
SST = re.compile(r"sst (.) f=(\d+) (r=\w+ h=\w+ u=\w+[^\r\n]*)")
DETACHED = re.compile(r"savestate: (\d+) saves, (\d+) loads, (\d+) refused; slot (freed|never taken)")
CLEARED = re.compile(r"savestate: No\. of CPUs raised: state cleared")

# Fighter statuses (lib/BrawlHeaders ft/fighter.h).
ST_CATCH_WAIT, ST_CLIFF_CATCH, ST_CLIFF_WAIT = 0x39, 0x74, 0x75
ST_CAPTURE_PULLED, ST_CAPTURE_WAIT = 0x3D, 0x3E
ST_DEAD, ST_REBIRTH, ST_APPEAL = 0xBD, 0xBE, 0x10C
ST_ATTACK_AIR, ST_SPECIAL_N = 0x33, 0x112
# Settings block offsets (src/main/settings.h, checked at build time):
# controls.dpadLeft/dpadRight, recording.randomPercent[2], cpu.options,
# the HMN colour overlay groups.
SET_SS_LEFT, SET_SS_RIGHT, SET_SS_RANDOM = 27, 28, 184
SET_CPU_OPTIONS = LT.SET_CPU_PERCENT + 3
SET_HMN_OVERLAY_GROUPS = 249
# cpu::CpuOptions fields and values (scripts/cpu_tests.py).
CPU_CONTROL, CPU_BEHAVIOR, CPU_COUNTER_GROUND, CPU_COUNTER_AIR, CPU_COUNTER_SHIELD = 0, 1, 5, 6, 7
CPU_LAB, BEHAVIOR_SHIELD, COUNTER_JAB, SHIELD_GRAB, AIR_JUMP_NEUTRAL = 1, 1, 13, 1, 4
CPU_TDI = 2
CPU_RECOVERY, RECOVERY_OFF = 39, 2
# Random is the last choice of each counter list, the first of TDI's.
COUNTER_GROUND_RANDOM, COUNTER_SHIELD_RANDOM, TDI_RANDOM = 40, 33, 0
# Random counter action pools (src/main/cpu/actions.cpp), by the counter's list.
GROUND_POOL = {1, 2, 3, 11, 12, 13, 14, 15, 16, 18, 20, 23}
SHIELD_POOL = {1, 2, 4, 5, 6, 7, 12, 15, 16, 20}
OVERLAY_GROUP_WAIT, OVERLAY_GREEN = 8, 2
PR_OSD = 0x7
OSD_LINE = re.compile(r"osd: P(\d) id (\d+): ([^\r\n]*)")
CPU_COUNTER = re.compile(r"cpu: P2 counter list (\d+) action (\d+)")
# What the driver logs for a KO'd CPU: suspended, AI flag back, back on the
# stage, AI off again.
KO_LINE = re.compile(r"cpu: slot 1 (?:KO'd|back \(status|AI flag back to \d+ \(KO\)|AI off \(flag was)")
ST_GUARD_FIRST, ST_GUARD_LAST, ST_CATCH = 0x1A, 0x1D, 0x34
PR_HELD, DPAD_LEFT, DPAD_RIGHT = 0x18, 0x1, 0x2
# Lab menu rows (src/main/ui/pages/): Recording > Save State, Load State,
# State; Controls > DPad Left.
CONTROLS_DPAD_LEFT = 5

# Pads: (buttons, main stick, C-stick).
N = ((), (128, 128), (128, 128))
RIGHT, LEFT, UP, DOWN = (228, 128), (28, 128), (128, 228), (128, 28)


def P(buttons=(), stick=(128, 128), c=(128, 128)):
    return (tuple(buttons), tuple(stick), tuple(c))


class SaveStateError(RuntimeError):
    pass


# ---- input scripts ------------------------------------------------------------------

def gameplay(n=600):
    """Run in, three jabs, a forward tilt, a short hop with a neutral air, a
    shield, a dash back, a jump, a run back in and a forward smash (C-stick);
    neutral to the end (the experiments' script, on a controller)."""
    s = []

    def add(k, p=N):
        s.extend([p] * k)
    add(5)
    add(58, P(stick=RIGHT))
    add(12)
    for _ in range(3):
        add(1, P(["A"]))
        add(9)
    add(15)
    add(1, P(["A"], (188, 128)))
    add(30)
    add(1, P(["X"]))
    add(5)
    add(1, P(["A"]))
    add(40)
    add(40, P(["R"]))
    add(10)
    add(25, P(stick=LEFT))
    add(10)
    add(1, P(["X"]))
    add(60)
    add(30, P(stick=RIGHT))
    add(1, P(c=RIGHT))
    add(60)
    while len(s) < n:
        add(1)
    return s[:n]


def specials(n=600):
    """gameplay(), with a neutral special every 15 frames from frame 440
    (projectiles for the characters that have them)."""
    s = gameplay(n)
    for i in range(440, min(n, 560), 15):
        s[i] = P(["B"])
    return s


def roam(n=600):
    """Run right and left pressing A every 30 frames (picks items up, uses them)."""
    out = []
    for i in range(n):
        d = RIGHT if (i // 90) % 2 == 0 else LEFT
        out.append(P(["A"], d) if i % 30 == 0 else P(stick=d))
    return out


# ---- the plugin and the log -------------------------------------------------------------

class Log:
    """New text of dolphin.log since the last look, read incrementally."""

    def __init__(self, path):
        self.path = path
        self.pos = 0
        self.buf = ""

    def pull(self):
        try:
            with open(self.path, "rb") as f:
                f.seek(0, 2)
                if f.tell() < self.pos:
                    # Dolphin started the log over (it caps its size).
                    self.pos = 0
                f.seek(self.pos)
                data = f.read()
        except OSError:
            return
        self.pos += len(data)
        self.buf += data.decode("utf-8", "replace")

    def skip(self):
        self.pull()
        self.buf = ""

    def find(self, regex, wait_s=3.0):
        t0 = time.monotonic()
        while True:
            self.pull()
            m = regex.search(self.buf)
            if m or time.monotonic() - t0 > wait_s:
                return m
            time.sleep(0.05)

    def take(self):
        self.pull()
        text, self.buf = self.buf, ""
        return text


def parse_rows(text):
    """[(tag, frame, values)] of the sst lines in `text`."""
    return [(m.group(1), int(m.group(2)), m.group(3).strip()) for m in SST.finditer(text)]


def fighter_fields(values):
    """{player: [x, y, status, anim, damage, hitstun, hitlag]} of a row."""
    out = {}
    for part in values.split("|")[1:]:
        w = part.split()
        if w and w[0].startswith("P"):
            out[int(w[0][1:])] = [float(w[1]), float(w[2]), int(w[3], 16), float(w[4]), float(w[5]), int(w[6]),
                                  int(w[7])]
    return out


def script_step(p):
    """One frame of controls as the script block holds them: game-action
    bits and the main stick (-1..1); the C-stick becomes a smash attack in
    its direction."""
    def c(v):
        return max(-100, min(100, v - 128)) / 100.0
    bits = 0
    for b in p[0]:
        bits |= ACTION_BITS.get(b, 0)
    x, y = c(p[1][0]), c(p[1][1])
    if p[2] != (128, 128):
        bits |= ACTION_BITS["A"] | SMASH
        x, y = c(p[2][0]), c(p[2][1])
    return bits, x, y


def pad_entry(p, hold=1):
    return {"buttons": list(p[0]), "main": list(p[1]), "c": list(p[2]), "l": 255 if "L" in p[0] else 0,
            "r": 255 if "R" in p[0] else 0, "hold": hold}


class Plugin:
    def __init__(self, emu, notes):
        self.emu = emu
        self.log = Log(emu.log_path)
        self.notes = notes
        self.block = None
        self.saved = None      # the S row of the last save
        self.pre = (N, N)      # the two inputs read before the last save point
        self.last2 = (N, N)    # the last two inputs of the last script
        self.costs = {"save": [], "load": [], "check": []}
        self.hash_only = []  # (scenario, cycles, cycles with hash-only frames, such frames, frames compared)
        self.sizes = []

    def note(self, text):
        self.notes.append(text)
        print("  " + text)

    def attach(self):
        """The current match's test block (after each match start)."""
        m = None
        for m in BLOCK.finditer(self.emu.log_text()):
            pass
        if not m:
            raise SaveStateError("no savestate line in the log: is the feature in this build?")
        if m.group(2) != "known":
            raise SaveStateError("the plugin does not know this memory layout")
        self.block = int(m.group(1), 16)
        if self.emu.u32(self.block) != PROBE_MAGIC:
            raise SaveStateError(f"no test block at {self.block:#x}")
        found = SCRIPT_BLOCK.findall(self.emu.log_text())
        if not found or self.emu.u32(int(found[-1], 16)) != SCRIPT_MAGIC:
            raise SaveStateError("no CPU script block in the log")
        self.script_block = int(found[-1], 16)
        self.log.skip()
        self.saved = None
        self.pre = self.last2 = (N, N)

    def put(self, off, fmt, *values):
        self.emu.h.call("write_mem", addr=self.block + off, hex=struct.pack(fmt, *values).hex())

    def u32(self, off):
        return self.emu.u32(self.block + off)

    def frame(self):
        return nav.game_frame(self.emu)

    def set_trace(self, on):
        self.put(PR_TRACE, ">B", 1 if on else 0)

    def parked(self):
        return self.u32(PR_PARKED)

    def wait_park(self, seq, timeout_s=120):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout_s:
            cur_seq, parked = struct.unpack(">II", self.emu.read(self.block + PR_PARK_SEQ, 8))
            if cur_seq != seq and parked:
                return parked
            time.sleep(0.005)
        raise SaveStateError(f"the game did not park (frame counter {self.frame()}, park at {self.u32(PR_PARK_AT)})")

    def park_at(self, frame):
        """From a running game: hold it at the loop top of `frame` (or the
        next loop top if the counter is past it)."""
        seq = self.u32(PR_PARK_SEQ)
        self.put(PR_PARK_AT, ">I", max(frame, 1))
        return self.wait_park(seq)

    def unpark(self):
        """Let the game run on (no further park)."""
        self.put(PR_PARK_AT, ">I", 0)
        if self.parked():
            self.put(PR_RELEASE, ">I", self.u32(PR_PARK_SEQ))
        t0 = time.monotonic()
        while self.parked() and time.monotonic() - t0 < 10:
            time.sleep(0.02)
        self.emu.neutral(0)

    def harness_script(self, pads):
        """Port 1 plays `pads` through the harness from about the first pad
        read after the release (the pad thread keeps sampling while the game
        is parked, so not frame-exact); then neutral."""
        entries = []
        for p in list(pads) + [N]:
            if entries and entries[-1][0] == p:
                entries[-1][1] += 1
            else:
                entries.append([p, 1])
        self.emu.h.call("pad_script", port=0, frames=[pad_entry(p, n) for p, n in entries], start="next")

    def play(self, pads, cpu=None):
        """The CPU code's script source plays `pads` into P1's input from
        its next input call (a fresh generation): frame-exact after a park,
        whether or not a load runs at that loop top. With `cpu` (as many
        pads), its second block plays those into the CPU's input the same
        way, the CPU's AI off meanwhile."""
        self.write_script(self.script_block, 0, pads, 0)
        if cpu is not None:
            if len(cpu) != len(pads):
                raise SaveStateError(f"{len(pads)} pads for P1, {len(cpu)} for the CPU")
            self.write_script(self.script_block + SCRIPT_BLOCK_SIZE, 1, cpu, 1)

    def write_script(self, block, player, pads, ai_off):
        steps = []
        for p in pads:
            st = script_step(p)
            if steps and tuple(steps[-1][:3]) == st:
                steps[-1][3] += 1
            else:
                steps.append(list(st) + [1])
        if len(steps) > SCRIPT_STEPS:
            raise SaveStateError(f"a script of {len(steps)} steps, at most {SCRIPT_STEPS}")
        if self.emu.u32(block) != SCRIPT_MAGIC:
            raise SaveStateError(f"no script block at {block:#x}")
        body = b"".join(struct.pack(">IffI", *x) for x in steps)
        self.emu.h.call("write_mem", addr=block + 0x3C, hex=body.hex())
        generation = self.emu.u32(block + 4) + 1
        head = struct.pack(">IiIIIII", generation, player, len(steps), 0, 0, ai_off, 0)
        self.emu.h.call("write_mem", addr=block + 4, hex=head.hex())

    def stop_script(self):
        self.emu.h.call("write_mem", addr=self.script_block + 8, hex="ffffffff")

    def go(self, pads, request=0, pre=None, cpu=None):
        """From a park: optionally ask for a save or load at this loop top,
        play `pads` (frame-exact, from the plugin; `cpu` for the CPU, see
        play()) and park again after them.
        `pre` are the two inputs the game read before this point (by default
        the last two of the previous script; for a load, the save's).
        Returns the log text since."""
        if not self.parked():
            raise SaveStateError("go() needs the game parked")
        here = self.parked()
        pads = list(pads) or [N]
        start = self.saved[1] if request == LOAD and self.saved is not None else here
        if pre is None:
            pre = self.pre if request == LOAD else self.last2
        self.play(pads, cpu)
        self.last2 = (([N] * 2 + pads)[-2], pads[-1])
        self.log.take()
        if request:
            self.put(PR_REQUEST, ">B", request)
        seq = self.u32(PR_PARK_SEQ)
        self.put(PR_PARK_AT, ">I", start + len(pads))
        self.put(PR_RELEASE, ">I", seq)
        self.wait_park(seq)
        time.sleep(0.05)
        return self.log.take()

    def hold_real(self, button, frames, after=150):
        """From a park: the harness holds `button` on port 1 (the real
        controller path) for about `frames` frames, then neutral for `after`
        frames, then the game parks again. The game counts the frames, so the
        press lasts within a frame or two of `frames`. Returns the log text."""
        self.log.take()
        self.stop_script()
        self.unpark()
        start = self.frame()
        self.emu.hold(0, buttons=[button])
        t0 = time.monotonic()
        while self.frame() < start + frames and time.monotonic() - t0 < 30:
            time.sleep(0.005)
        self.emu.neutral(0)
        end = self.frame()
        self.park_at(end + after)
        self.last2 = (N, N)
        time.sleep(0.05)
        return self.log.take()

    def go_until(self, pads, regex, timeout_s=30):
        """From a park: play `pads` with no park target (a load the plugin
        does by itself moves the frame counter back) until `regex` shows in
        the log, then park two frames later. Returns the log text."""
        if not self.parked():
            raise SaveStateError("go_until() needs the game parked")
        self.log.take()
        self.stop_script()
        self.put(PR_PARK_AT, ">I", 0)
        self.put(PR_RELEASE, ">I", self.u32(PR_PARK_SEQ))
        self.harness_script(pads)
        m = self.log.find(regex, wait_s=timeout_s)
        time.sleep(0.5)
        self.park_at(self.frame() + 2)
        time.sleep(0.05)
        return self.log.take() if m else ""

    def go_harness(self, pads, margin=6):
        """From a park: the harness plays `pads` on port 1 (the real
        controller path, as a player's D-pad; timed by the pad thread, so
        about one frame loose) and the game parks `margin` frames after them.
        Returns the log text since."""
        if not self.parked():
            raise SaveStateError("go_harness() needs the game parked")
        here = self.parked()
        self.log.take()
        self.stop_script()
        seq = self.u32(PR_PARK_SEQ)
        self.put(PR_PARK_AT, ">I", here + len(pads) + margin)
        self.put(PR_RELEASE, ">I", seq)
        # Only once the game runs: the pad thread keeps reading the
        # controller while the game is parked, and those reads would play
        # the script into the void.
        self.harness_script(pads)
        self.wait_park(seq)
        self.last2 = (N, N)
        time.sleep(0.05)
        return self.log.take()

    def act(self, kind, pads, pre=None, cpu=None):
        """go() with a save or load request; returns (ok, match or reason, S/L row, text)."""
        text = self.go(pads, kind, pre, cpu)
        r = REFUSED.search(text)
        done = SAVED if kind == SAVE else LOADED
        m = done.search(text)
        if r and not m:
            return False, r.group(2), None, text
        if not m:
            raise SaveStateError(f"no answer to the {'save' if kind == SAVE else 'load'} request")
        tag = "S" if kind == SAVE else "L"
        row = next((x for x in parse_rows(text) if x[0] == tag), None)
        if kind == SAVE:
            self.saved = row or ("S", int(m.group(1)), None)
            self.costs["save"].append(float(m.group(6)))
            self.sizes.append((int(m.group(2)), int(m.group(4)), int(m.group(3)), int(m.group(5))))
        else:
            self.costs["load"].append(float(m.group(3)))
            self.costs["check"].append(float(m.group(4)))
        return True, m, row, text

    def save(self, pads, pre=None):
        """Save at this park, then play `pads`. `pre`: the two inputs the game
        read before this point (by default the last script's)."""
        pre = pre or self.last2
        ok, m, row, text = self.act(SAVE, pads, pre)
        if not ok:
            raise SaveStateError(f"save refused: {m}")
        self.pre = pre
        return row, text

    def load(self, pads):
        """Load, then play `pads`; the game sees the save's two earlier inputs
        as the ones before the loaded frame."""
        ok, m, row, text = self.act(LOAD, pads)
        if not ok:
            raise SaveStateError(f"load refused: {m}")
        return row, text


# ---- memory reads (independent of the plugin) -------------------------------------------

class Game:
    def __init__(self, emu):
        self.emu = emu
        self.f = LT.Fighters(emu)
        self.classes = {}

    def fighters(self):
        return sorted(self.f.entries(), key=lambda e: e["player"])

    def set_damage(self, player, value):
        f = self.f.by_player(player)
        self.emu.h.call("write_mem", addr=self.emu.ptr(f["owner"]) + 0x24, hex=struct.pack(">f", value).hex())

    def heap_blocks(self, name):
        import memdump
        raw = self.emu.read(memdump.HEAP_INFOS, 16 * memdump.HEAP_COUNT)
        for i in range(memdump.HEAP_COUNT):
            name_p, pool = struct.unpack_from(">II", raw, 16 * i)
            if self.emu.is_ram(pool) and self.emu.cstr(name_p) == name:
                return struct.unpack(">H", self.emu.read(pool + memdump.POOL_ALLOCATED, 2))[0]
        return None

    def heap_pools(self):
        """{name: pool address} of the game's heap table."""
        import memdump
        raw = self.emu.read(memdump.HEAP_INFOS, 16 * memdump.HEAP_COUNT)
        out = {}
        for i in range(memdump.HEAP_COUNT):
            name_p, pool = struct.unpack_from(">II", raw, 16 * i)
            if self.emu.is_ram(pool):
                out[self.emu.cstr(name_p)] = pool
        return out

    def heap_bytes(self, names):
        """{name: (start address, the heap's whole range)} for the heaps
        that exist, read while the game is parked."""
        import memdump
        pools = self.heap_pools()
        out = {}
        for name in names:
            pool = pools.get(name)
            if pool is None:
                continue
            start, end = struct.unpack(">II", self.emu.read(pool + memdump.POOL_START, 8))
            if self.emu.is_ram(start) and start < end <= start + (16 << 20):
                out[name] = (start, self.emu.read(start, end - start))
        return out

    def class_name(self, vtable):
        """The class a vtable belongs to, from the game's run-time type
        information (the vtable's first word points at the type's record,
        whose first word points at its name), or None."""
        if vtable not in self.classes:
            name = None
            if self.emu.is_ram(vtable) and vtable % 4 == 0:
                info = self.emu.ptr(vtable)
                text = self.emu.ptr(info) if info else None
                name = self.emu.cstr(text, 72) if text else None
            self.classes[vtable] = name if name and len(name) > 3 else None
        return self.classes[vtable]

    def place(self, start, raw, off):
        """A name for the word at `off` of a heap read at `start`: the class
        of the nearest object before it (the nearest vtable pointer within
        0x400 bytes) and the offset from there, or the address."""
        for back in range(off & ~3, max(off - 0x400, -4), -4):
            word = struct.unpack_from(">I", raw, back)[0]
            # A vtable is in a module's data, not in this heap (whose
            # blocks point at the heap itself, which starts with its name).
            name = None if start <= word < start + len(raw) else self.class_name(word)
            if name:
                return f"{name}+{off - back:#x}"
        return f"{start + off:08x}"


# ---- scenarios ------------------------------------------------------------------------

def without_hash(v):
    """A row without the fighter-heap hash (h=...)."""
    return re.sub(r"h=\w+ ", "", v)


def compare(first, rows, upto):
    """Frame-matched comparison with the first pass for frames <= upto:
    (frames compared, frames whose values differ, frames that differ only in
    the fighter-heap hash, first value difference)."""
    ref = {f: v for t, f, v in first if t == " "}
    n, bad, hashed, where = 0, 0, 0, None
    for t, f, v in rows:
        if t != " " or f not in ref or f > upto:
            continue
        n += 1
        if without_hash(v) != without_hash(ref[f]):
            bad += 1
            if where is None:
                where = (f, ref[f], v)
        elif v != ref[f]:
            hashed += 1
    return n, bad, hashed, where


def same_row(a, b):
    return a is not None and b is not None and (a[1], a[2]) == (b[1], b[2])


def cycles(pl, name, script, count, ks, pre=None):
    """With the game parked at the save point (the inputs read before it
    were `pre`): save, play the script (the first pass), then `count` times:
    load, check the restored row, replay the first ks[i] inputs and compare
    each frame with the first pass."""
    pl.set_trace(True)
    longest = max(ks[:count]) if count else 60
    if pre is None:
        # The plugin's script for the frames before the save too, so the
        # first pass and the replays follow the same controller history.
        pl.go([N] * 3)
    saved, text = pl.save(script[:longest], pre)
    first = parse_rows(text)
    f0 = saved[1]
    if len([r for r in first if r[0] == " " and f0 < r[1] <= f0 + longest]) < longest - 1:
        raise SaveStateError(f"{name}: {len(first)} trace rows for {longest} frames")
    fails = []
    compared = 0
    hash_only = []
    for i in range(count):
        k = ks[i]
        row, text = pl.load(script[:k])
        if not same_row(row, saved):
            fails.append(f"cycle {i + 1}: restored row\n      {row}\n      saved\n      {saved}")
        n, bad, hashed, where = compare(first, parse_rows(text), f0 + k)
        compared += n
        if hashed:
            hash_only.append((i + 1, hashed))
        if n < k - 1:
            fails.append(f"cycle {i + 1}: {n} rows compared for {k} frames")
        if bad:
            fails.append(f"cycle {i + 1}: {bad} of {n} frames differ from the first pass, first at frame "
                         f"{where[0]}:\n      first  {where[1]}\n      replay {where[2]}")
        if len(fails) > 5:
            break
    pl.set_trace(False)
    pl.note(f"{name}: saved frame {f0}; {count} loads, restored rows equal to the saved row"
            f"{'' if not fails else ' EXCEPT ' + str(len(fails)) + ' failure(s)'}; {compared} replayed frames "
            f"compared with the first pass; {len(hash_only)} cycles with frames equal in every value but the "
            f"fighter-heap hash ({sum(h for _, h in hash_only)} frames)")
    pl.hash_only.append((name, count, len(hash_only), sum(h for _, h in hash_only), compared))
    for f in fails:
        print("    FAIL " + f)
    return fails, first


def ks_for(count, lo, hi):
    return [lo + (i * 97) % (hi - lo + 1) for i in range(count)]


class Runner:
    def __init__(self, emu, opts):
        self.emu = emu
        self.opts = opts
        self.notes = []
        self.pl = Plugin(emu, self.notes)
        self.g = Game(emu)
        self.match = None
        self.results = []  # (scenario, cycles, verdict, notes)
        self.shots = True

    def shot(self, name):
        """A screenshot for the record, from a park: the harness takes one
        only while the game draws, so the game's tasks are held still with
        the scheduler's stop bit, the game runs on drawing the same picture
        and parks again at the next loop top. Returns the name, or why there
        is none."""
        if not self.shots or self.emu.headless:
            return "none"
        pl = self.pl
        try:
            nav.hold_game_still(self.emu, True)
            pl.put(PR_PARK_AT, ">I", 0)
            pl.put(PR_RELEASE, ">I", pl.u32(PR_PARK_SEQ))
            time.sleep(0.15)
            self.emu.screenshot(name)
            return name
        except (OSError, HarnessError, ValueError) as e:
            self.shots = False
            return f"none ({e})"
        finally:
            pl.park_at(pl.frame())
            nav.hold_game_still(self.emu, False)

    def chosen(self, p1=None, cpu=None, stage=None):
        return (p1 or self.opts.hmn or "mario", cpu or self.opts.cpu or "fox",
                stage or self.opts.stage or "final_destination")

    def into_match(self, p1=None, cpu=None, stage=None):
        """A fresh Training match (seeded RNG), parked at game frame 100.
        The match is --hmn against --cpu on --stage (Mario, Fox, Final
        Destination) unless the scenario names its own."""
        p1, cpu, stage = self.chosen(p1, cpu, stage)
        if nav.scene(self.emu)[0] == "scMelee":
            if not self.pl.block:
                self.pl.attach()
            self.pl.unpark()
            nav.finish_training(self.emu)
        if nav.scene(self.emu)[0] != "scSelctCharacter":
            nav.boot_to_training_css(self.emu)
        self.emu.mark(f"savestate test: {p1} vs {cpu} on {stage}")
        if isinstance(stage, int):
            start_on_stage_kind(self.emu, p1, cpu, stage)
        else:
            try:
                nav.start_training_match(self.emu, p1, cpu, stage, seed_rng=True)
            except Timeout as e:
                # The token sometimes stops just off an icon's edge.
                if nav.scene(self.emu)[0] != "scSelctCharacter":
                    raise
                print(f"    character select: {e}; trying again")
                nav.start_training_match(self.emu, p1, cpu, stage, seed_rng=True)
        self.emu.neutral(0)
        self.pl.attach()
        # Lab settings this test relies on: CPU percent unlocked, savestate
        # settings at their defaults.
        settings = LT.Lab(self.emu, []).find_settings()
        self.emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
        self.emu.h.call("write_mem", addr=settings + SET_CPU_OPTIONS, hex="00")  # CPU Control: Training
        self.emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
        self.emu.h.call("write_mem", addr=settings + SET_SS_LEFT, hex="0000")
        self.emu.h.call("write_mem", addr=settings + SET_SS_RANDOM, hex="0000")
        self.pl.park_at(100)
        self.match = (p1, cpu, stage)

    def title(self):
        """The current match, for the results table."""
        p1, cpu, stage = self.match
        if self.match == ("mario", "fox", "final_destination"):
            return "Mario vs Fox, FD"
        return f"{p1} vs {cpu}, {stage if isinstance(stage, str) else f'stage kind {stage:#x}'}"

    def ensure(self, p1=None, cpu=None, stage=None):
        """In such a match and parked."""
        p1, cpu, stage = self.chosen(p1, cpu, stage)
        if self.match != (p1, cpu, stage) or nav.scene(self.emu)[0] != "scMelee":
            self.into_match(p1, cpu, stage)
        elif not self.pl.parked():
            self.pl.park_at(self.pl.frame() + 2)

    def record(self, name, count, fails, note=""):
        verdict = "PASS" if not fails else "FAIL"
        self.results.append((name, count, verdict, note if not fails else fails[0].splitlines()[0]))

    def refused(self, name, kind, want):
        ok, why, _, _ = self.pl.act(kind, [N])
        if ok or want not in why:
            raise SaveStateError(f"{name}: {'save' if kind == SAVE else 'load'} answered "
                                 f"{'ok' if ok else repr(why)}, wanted a refusal ({want})")
        self.pl.note(f"{name}: {'save' if kind == SAVE else 'load'} refused: {why}")
        return why

    def refused_running(self, name, kind, want):
        """A request while the game runs (e.g. paused in Training's menu)."""
        self.pl.log.take()
        self.pl.put(PR_REQUEST, ">B", kind)
        m = self.pl.log.find(REFUSED, wait_s=5)
        if not m or want not in m.group(2):
            raise SaveStateError(f"{name}: wanted a refusal ({want}), got {m.group(0) if m else None}")
        self.pl.note(f"{name}: {m.group(1)} refused: {m.group(2)}")


# -- quick scenarios --

def sc_basic(r):
    r.ensure()
    n = r.opts.cycles or 12
    fails, _ = cycles(r.pl, "basic", gameplay(), n, ks_for(n, 60, 300))
    s = r.pl.sizes[-1]
    r.pl.note(f"basic: set {s[0]} bytes in {s[2]} segments ({s[1]} without the fighters' free space), slot "
              f"{s[3]} bytes")
    r.record(r.title(), n, fails)


def statuses(rows, player):
    return [fighter_fields(v).get(player, [0, 0, -1])[2] for t, f, v in rows if t == " "]


def sc_dpad(r):
    """Default D-pad bindings. Exact counts through the test block (port 1's
    pads hold a D-pad direction for N passes of the main loop): D-pad right
    saves on the 12th frame held (not on the 11th, once per hold, 24 frames of
    lockout), D-pad left loads on the press. Then through the real
    controller path (the harness's port 1, about a frame loose): a held
    D-pad saves and loads, and never taunts while bound."""
    r.into_match()
    pl = r.pl
    fails = []
    pl.set_trace(True)

    def hold(bits, passes, frames):
        pl.put(PR_HELD, ">HH", bits, passes)
        return pl.go([N] * frames)
    text = hold(DPAD_RIGHT, 11, 40)
    if SAVED.search(text):
        fails.append("saved after 11 frames of D-pad right")
    here = pl.parked()
    text = hold(DPAD_RIGHT, 12, 40)
    m = SAVED.search(text)
    if not m or int(m.group(1)) != here + 11:
        fails.append(f"holding D-pad right 12 frames from frame {here}: {m.group(0) if m else 'no save'}, "
                     f"wanted the save at frame {here + 11}")
    text = hold(DPAD_RIGHT, 60, 80)
    if len(SAVED.findall(text)) != 1:
        fails.append(f"{len(SAVED.findall(text))} saves holding D-pad right 60 frames, wanted 1")
    srow = next((x for x in parse_rows(text) if x[0] == "S"), None)
    pl.saved = srow
    pl.pre = (N, N)
    text = hold(DPAD_LEFT, 30, 20)
    lrow = next((x for x in parse_rows(text) if x[0] == "L"), None)
    if len(LOADED.findall(text)) != 1 or not same_row(lrow, srow):
        fails.append(f"holding D-pad left 30 frames: {len(LOADED.findall(text))} loads, row equal to the save: "
                     f"{same_row(lrow, srow)}")
    exact = not fails
    # The real controller path.
    text = pl.hold_real("DRIGHT", 40)
    saves = len(SAVED.findall(text))
    taunts = statuses(parse_rows(text), 1).count(ST_APPEAL)
    srow = next((x for x in parse_rows(text) if x[0] == "S"), None)
    text = pl.hold_real("DLEFT", 30)
    loads = len(LOADED.findall(text))
    lrow = next((x for x in parse_rows(text) if x[0] == "L"), None)
    taunts += statuses(parse_rows(text), 1).count(ST_APPEAL)
    if saves != 1 or loads != 1 or not same_row(lrow, srow):
        fails.append(f"through the controller: {saves} saves holding D-pad right, {loads} loads holding left, "
                     f"row back: {same_row(lrow, srow)}")
    if taunts:
        fails.append(f"P1 taunted ({taunts} frames) while the D-pad was bound")
    pl.note(f"dpad: exact counts {'as specified' if exact else 'FAILED'} (no save at 11 frames, a save on the 12th, "
            f"one per hold, a load on the press with the saved row back); through the controller {saves} save, "
            f"{loads} load, {taunts} taunt frames")
    # One direction unbound at a time: that one reaches the game (a taunt)
    # and does nothing else; the other stays hidden.
    settings = LT.Lab(r.emu, []).find_settings()
    seen = {}
    for left, right, press in ((1, 0, "DLEFT"), (0, 1, "DRIGHT"), (0, 0, "DLEFT"), (0, 0, "DRIGHT")):
        r.emu.h.call("write_mem", addr=settings + SET_SS_LEFT, hex=f"{left:02x}{right:02x}")
        text = pl.hold_real(press, 20)
        taunts = statuses(parse_rows(text), 1).count(ST_APPEAL)
        unbound = (left if press == "DLEFT" else right) == 1
        seen[(left, right, press)] = taunts
        if unbound and not taunts:
            fails.append(f"{press} set to None did not reach the game (no taunt)")
        if not unbound and taunts:
            fails.append(f"{press} bound (left {left}, right {right}) taunted")
        if unbound and (SAVED.search(text) or LOADED.search(text)):
            fails.append(f"{press} set to None saved or loaded")
    r.emu.h.call("write_mem", addr=settings + SET_SS_LEFT, hex="0000")
    pl.set_trace(False)
    pl.note(f"dpad: taunt frames per (left None, right None, button): {seen}")
    for f in fails:
        print("    FAIL " + f)
    r.record("D-pad bindings", 1, fails)


def sc_refusals(r):
    r.into_match()
    fails = []
    try:
        # The lab takes a silent state on its first running frame, so a load
        # before any save finds that state.
        log = r.emu.log_text()
        log = log[log.rfind("savestate: test block at"):]
        taken = re.findall(r"savestate: match-start state taken at frame (\d+)", log)
        if len(taken) != 1:
            fails.append(f"{len(taken)} match-start states logged, wanted 1")
        _, text = r.pl.load([N])
        back = LOADED.search(text)
        if taken and (not back or back.group(1) != taken[0]):
            fails.append(f"the first load: {back.group(0) if back else 'no line'}, wanted the match-start "
                         f"state's frame {taken[0]}")
        r.pl.note(f"refusals: match-start state at frame {taken[0] if taken else None}, loaded before any save")
        r.pl.save([N] * 10)
        r.pl.unpark()
        nav.pause_training(r.emu)
        r.refused_running("refusals", SAVE, "not in a running Training match")
        r.pl.log.take()
        nav.menu_select(r.emu, nav.MENU_CPUS)
        r.emu.press(0, stick=RIGHT, frames=3, after=8)
        if not r.pl.log.find(CLEARED):
            fails.append("raising No. of CPUs did not clear the state")
        nav.resume_training(r.emu)
        r.emu.wait_for("3 entries", lambda: len(r.g.fighters()) == 3, 600, every=5)
        r.emu.advance(30)
        r.pl.park_at(r.pl.frame() + 2)
        r.refused("refusals", SAVE, "needs exactly 2 fighters")
        r.refused("refusals", LOAD, "needs exactly 2 fighters")
        r.pl.unpark()
        nav.set_training_cpus(r.emu, 1)
        r.emu.advance(60)
        r.pl.park_at(r.pl.frame() + 2)
        r.pl.save([N] * 20)
        r.pl.load([N] * 20)
        r.pl.note("refusals: back to 1 CPU, save and load work again")
    except SaveStateError as e:
        fails.append(str(e))
    for f in fails:
        print("    FAIL " + f)
    r.record("Refusals (empty slot, paused, 2 CPUs)", 1, fails)


def sc_menu(r):
    """The lab menu: Controls shows the D-pad rows (Recording keeps only
    recording position controls). Then Random Percent: every load adds up to
    the set damage."""
    r.into_match()
    pl, emu = r.pl, r.emu
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    fails = []
    shots = []
    pl.unpark()
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    emu.advance(10)
    shots.append(emu.screenshot("savestate-menu-state"))
    lab.tap(["B"])
    lab.up(LT.MAIN_RECORDING)
    lab.go_controls()
    lab.down(CONTROLS_DPAD_LEFT)
    emu.advance(10)
    shots.append(emu.screenshot("savestate-menu-controls"))
    lab.close_menu()
    emu.advance(20)
    # Random Percent: Player 0, CPU 50.
    emu.h.call("write_mem", addr=settings + SET_SS_RANDOM, hex="0032")
    pl.park_at(pl.frame() + 2)
    pl.set_trace(True)
    pl.go([N] * 3)
    saved, _ = pl.save([N] * 10)
    base = fighter_fields(saved[2])
    got = []
    for i in range(8):
        row, _ = pl.load([N] * 10)
        ff = fighter_fields(row[2])
        got.append(ff[2][4] - base[2][4])
        if ff[1][4] != base[1][4]:
            fails.append(f"player damage changed with Player Random Percent 0: {ff[1][4]} vs {base[1][4]}")
    emu.h.call("write_mem", addr=settings + SET_SS_RANDOM, hex="0000")
    pl.set_trace(False)
    if any(d < 0 or d > 50 for d in got) or len(set(got)) < 2:
        fails.append(f"CPU damage added on 8 loads with Random Percent 50: {got}")
    pl.note(f"menu: screenshots {[x[0] for x in shots]}; Random Percent "
            f"50 added {got} to the CPU over 8 loads, 0 to the player")
    for f in fails:
        print("    FAIL " + f)
    r.record("Lab menu rows, Random Percent", 1, fails)
    r.match = None


def sc_exit(r):
    """Leave the match after loads: the character select comes back, the
    module unloads with its heap at baseline, the slot is freed."""
    r.into_match()
    r.pl.save(gameplay(200))
    r.pl.load([N] * 30)
    r.pl.unpark()
    pos = len(r.emu.log_text())
    nav.finish_training(r.emu)
    r.emu.advance(60)
    log = r.emu.log_text()[pos:]
    fails = []
    m = DETACHED.search(log)
    if not m or m.group(4) != "freed":
        fails.append(f"no slot-freed line at detach ({m.group(0) if m else None})")
    if not L.rx(L.UNLOADED.replace("(yes|NO)", "yes")).search(log):
        fails.append("the module did not unload back to baseline")
    t = re.search(r"tracker: (\d+) registrations, (\d+) removals, (\d+) lost, (\d+) moved", log)
    if not t or t.group(1) != t.group(2) or t.group(3) != "0" or t.group(4) != "0":
        fails.append(f"tracker observers unbalanced: {t.group(0) if t else None}")
    if nav.scene(r.emu)[0] != "scSelctCharacter":
        fails.append(f"not back on the character select: {nav.scene(r.emu)[:2]}")
    r.match = None
    r.pl.block = None
    r.pl.note(f"exit: {m.group(0) if m else '?'}; {t.group(0) if t else '?'}; back on {nav.scene(r.emu)[0]}")
    for f in fails:
        print("    FAIL " + f)
    r.record("Exit after loads", 1, fails)


# -- the lab's own state across a load --

def replays(pl, name, script, count):
    """Save here, play `script` (the first pass), then `count` loads each
    replaying all of it. Returns (fails, first pass text, replay texts)."""
    saved, first_text = pl.save(script)
    first = parse_rows(first_text)
    fails, texts = [], []
    for i in range(count):
        row, text = pl.load(script)
        texts.append(text)
        if not same_row(row, saved):
            fails.append(f"{name} load {i + 1}: restored row differs from the saved row")
        n, bad, _, where = compare(first, parse_rows(text), saved[1] + len(script))
        if n < len(script) - 1 or bad:
            fails.append(f"{name} load {i + 1}: {bad} of {n} frames differ from the first pass"
                         + (f", first at frame {where[0]}" if where else ""))
    return fails, first_text, texts


def sc_cpu_options(r):
    """CPU Options drive the CPU (CPU Control Lab, Behavior Shield, Counter
    Action Jab after a ground hit, Grab out of shield) while P1 attacks: the
    save lands while the CPU is driven, and every load replays exactly as the
    first pass did, counters included (the driver's slots and the options'
    per-fighter state and random numbers come back with the match)."""
    cpu_replays(r, "cpu-options", "CPU Options driving the CPU",
                ((CPU_BEHAVIOR, BEHAVIOR_SHIELD), (CPU_COUNTER_GROUND, COUNTER_JAB),
                 (CPU_COUNTER_SHIELD, SHIELD_GRAB)), 1)


def sc_cpu_random(r):
    """As cpu-options, with the choices random: Counter Action (Ground) and
    (Shield) Random, Trajectory DI Random (drawn on every hit). The options'
    generator is stirred with the frame counter, so each load draws again
    (as a human replaying the state meets new choices): every load must
    still counter, only with actions from the Random pools (an air counter
    with the Air row's own action), and some load must choose differently
    from the first pass."""
    # The random rolls, dashes and jumps can take the CPU over the stage's
    # edge: a jab that meets it in the air gives an air counter (Counter
    # Action (Air), set to Jump Neutral here), and a fall can KO it.
    cpu_replays(r, "cpu-random", "CPU Options random choices",
                ((CPU_BEHAVIOR, BEHAVIOR_SHIELD), (CPU_COUNTER_GROUND, COUNTER_GROUND_RANDOM),
                 (CPU_COUNTER_AIR, AIR_JUMP_NEUTRAL), (CPU_COUNTER_SHIELD, COUNTER_SHIELD_RANDOM),
                 (CPU_TDI, TDI_RANDOM), (CPU_RECOVERY, RECOVERY_OFF)), 2,
                pools={"0": GROUND_POOL, "1": {AIR_JUMP_NEUTRAL}, "2": SHIELD_POOL})


def cpu_replays(r, name, title, values, distinct, pools=None):
    """Set the CPU options `values` ((field, value), CPU Control Lab last),
    then save, attack for a while and load the state again several times:
    every replay must match the first pass, which must show at least
    `distinct` different counters. With `pools` ({list: actions}) the
    choices are random: each load must counter from the pools instead,
    and some load differently from the first pass."""
    r.into_match()
    pl, emu = r.pl, r.emu
    settings = LT.Lab(emu, []).find_settings()
    fails = []
    base = settings + SET_CPU_OPTIONS
    for off, v in tuple(values) + ((CPU_CONTROL, CPU_LAB),):
        emu.h.call("write_mem", addr=base + off, hex=f"{v:02x}")
    try:
        pl.set_trace(True)
        pl.go([P(stick=RIGHT)] * 58 + [N] * 4)
        attack = []
        for _ in range(14):
            attack += [P(["A"])] + [N] * 11
        n = r.opts.cycles or 6
        fails, first_text, texts = replays(pl, name, attack + [N] * 30, n)
        firsts = CPU_COUNTER.findall(first_text)
        guards = sum(1 for s in statuses(parse_rows(first_text), 2) if ST_GUARD_FIRST <= s <= ST_GUARD_LAST)
        if len(set(firsts)) < distinct or not guards:
            fails.append(f"the CPU options did not act in the first pass ({len(firsts)} counters, "
                         f"{len(set(firsts))} different, {guards} guard frames)")
        if pools is not None:
            fails = []
            seen = [CPU_COUNTER.findall(t) for t in texts]
            for i, got in enumerate([firsts] + seen):
                bad = [c for c in got if int(c[1]) not in pools.get(c[0], ())]
                if not got or bad:
                    fails.append(f"pass {i}: counters {got}, outside the pools {bad}")
            if all(got == firsts for got in seen):
                fails.append(f"every load chose as the first pass did: {firsts}")
        for i, t in enumerate(texts):
            if pools is None and CPU_COUNTER.findall(t) != firsts:
                fails.append(f"load {i + 1}: counters {CPU_COUNTER.findall(t)}, first pass {firsts}")
        # The driver had the CPU's slot at the save: a load must not take,
        # release or touch its AI flag again. A KO in a replay is the one
        # exception: the driver lets the AI flag go while the CPU is gone and
        # takes it again when it is back (driver.cpp), every line of that
        # logged in the same pass.
        took, kos = [], 0
        for t in texts:
            lines = re.findall(r"cpu: (?:take|release) slot 1[^\r\n]*|cpu: slot 1 [^\r\n]*", t)
            ko = any("KO'd" in x for x in lines)
            kos += sum(1 for x in lines if "KO'd" in x)
            took += [x for x in lines if not (ko and KO_LINE.match(x))]
        if took:
            fails.append(f"the driver redid the CPU's slot during the loads: {took}")
        pl.note(f"{name}: first pass {guards} CPU guard frames, counters (list, action) {firsts}; {n} loads "
                + ("replayed the same rows and counters" if pools is None else
                   f"counters {[CPU_COUNTER.findall(t) for t in texts]}")
                + f"; CPU slot lines during the loads: {len(took)}; KOs in the replays: {kos}")
    finally:
        pl.set_trace(False)
        emu.h.call("write_mem", addr=base, hex="00")
    for f in fails:
        print("    FAIL " + f)
    r.record(title, r.opts.cycles or 6, fails)
    r.match = None


def colour(r, player=0):
    """A fighter's colour-blend bytes the overlays set, and whether it is on."""
    f = r.g.fighters()[player]
    mods = r.g.f.modules(f)
    col = r.emu.read(struct.unpack_from(">I", mods, 0xAC)[0] + 0x14A, 6)
    return col[:4].hex(), col[5]


def sc_osd_overlay(r):
    """Frame advantage on hit and HMN Wait = Green: save while an advantage
    message shows and Mario stands green, load while it still shows. One
    frame after a load no message from before it is left and Mario is green
    again; the replays match the first pass, messages included."""
    r.into_match()
    pl, emu = r.pl, r.emu
    settings = LT.Lab(emu, []).find_settings()
    group = settings + SET_HMN_OVERLAY_GROUPS + OVERLAY_GROUP_WAIT
    emu.h.call("write_mem", addr=settings + LT.SET_ADVANTAGE, hex="0101")
    emu.h.call("write_mem", addr=group, hex=f"{OVERLAY_GREEN:02x}")
    fails = []
    try:
        pl.set_trace(True)
        pl.go([P(stick=RIGHT)] * 58 + [N] * 4 + [P(["A"])] + [N] * 6)
        for _ in range(30):
            if emu.u8(pl.block + PR_OSD):
                break
            pl.go([N] * 2)
        pl.go([N] * 20)  # back to Wait, the message still up
        at_save = (emu.u8(pl.block + PR_OSD), colour(r))
        script = [N] * 60
        saved, first_text = pl.save(script)
        first_osds = OSD_LINE.findall(first_text)
        before_load = (emu.u8(pl.block + PR_OSD), colour(r))
        pl.load([N])
        after_load = (emu.u8(pl.block + PR_OSD), colour(r))
        n = r.opts.cycles or 6
        rows_ok = 0
        for i in range(n):
            visible_before = emu.u8(pl.block + PR_OSD)
            row, text = pl.load(script)
            if not same_row(row, saved):
                fails.append(f"load {i + 1}: restored row differs from the saved row")
            c, bad, _, where = compare(parse_rows(first_text), parse_rows(text), saved[1] + len(script))
            if bad or c < len(script) - 1:
                fails.append(f"load {i + 1}: {bad} of {c} frames differ, first at {where and where[0]}")
            else:
                rows_ok += 1
            if OSD_LINE.findall(text) != first_osds:
                fails.append(f"load {i + 1}: messages {OSD_LINE.findall(text)}, first pass {first_osds}")
            if visible_before or colour(r) != at_save[1]:
                fails.append(f"load {i + 1}: message shown {visible_before} before it, colour {colour(r)} after")
        if at_save[0] != 1 or at_save[1][1] == 0:
            fails.append(f"at the save: message shown {at_save[0]}, colour {at_save[1]}")
        if before_load[0] != 1 or before_load[1] != at_save[1]:
            fails.append(f"at the first load: message shown {before_load[0]}, colour {before_load[1]}")
        if after_load[0] != 0:
            fails.append("a message from before the load still showed one frame after it")
        if after_load[1] != at_save[1]:
            fails.append(f"colour one frame after the load {after_load[1]}, at the save {at_save[1]}")
        pl.note(f"osd-overlay: at the save message shown {at_save[0]}, Mario colour {at_save[1]}; first pass "
                f"messages {first_osds}; at the load shown {before_load[0]}; one frame after the load shown "
                f"{after_load[0]}, colour {after_load[1]}; {rows_ok} of {n} replays equal with the same messages")
    finally:
        pl.set_trace(False)
        emu.h.call("write_mem", addr=settings + LT.SET_ADVANTAGE, hex="0000")
        emu.h.call("write_mem", addr=group, hex="00")
    for f in fails:
        print("    FAIL " + f)
    r.record("OSD and colour overlay at save and load", r.opts.cycles or 6, fails)
    r.match = None


def moves(rows):
    """Game steps the fighters took over these trace rows: how often both
    fighters' position, status and animation frame changed from one row to
    the next (the frame counter runs on while frame advance holds the game)."""
    states = [tuple(tuple(v[:4]) for _, v in sorted(fighter_fields(x).items())) for t, f, x in rows if t == " "]
    return sum(1 for a, b in zip(states, states[1:]) if a != b)


def sc_frame_advance(r):
    """Frame advance on: a save and a load while it holds the game. After
    the save and after the load the fighters stand still, the load brings the
    saved row back, one press of the Frame Advance Button still runs exactly
    one step, and the setting is untouched."""
    r.into_match()
    pl, emu = r.pl, r.emu
    settings = LT.Lab(emu, []).find_settings()
    fails = []
    press = [P(["L"])] * 2 + [N] * 10
    try:
        pl.set_trace(True)
        pl.go([P(stick=RIGHT)] * 30 + [N] * 10)
        emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="01")
        held = moves(parse_rows(pl.go([N] * 30))[5:])
        saved, text = pl.save([N] * 30)
        after_save = moves(parse_rows(text))
        one = moves(parse_rows(pl.go_harness(press)))
        row, text = pl.load([N] * 30)
        after_load = moves(parse_rows(text))
        one_after = moves(parse_rows(pl.go_harness(press)))
        setting = emu.u8(settings + LT.SET_FRAME_ADVANCE)
        if held or after_save or after_load:
            fails.append(f"the fighters moved while frame advance held the game: {held} steps before the save, "
                         f"{after_save} after it, {after_load} after the load")
        if one != 1 or one_after != 1:
            fails.append(f"one press ran {one} steps after the save, {one_after} after the load")
        if not same_row(row, saved):
            fails.append("the load did not bring back the saved row")
        if setting != 1:
            fails.append(f"Frame Advance is {setting} after the load")
        pl.note(f"frame-advance: saved at frame {saved[1]} while held ({held} steps in the 25 frames before, "
                f"{after_save} in the 30 after); one press ran {one} step; the load brought the saved row "
                f"{'back' if same_row(row, saved) else 'NOT back'}, then {after_load} steps in 30 frames, one press "
                f"ran {one_after}; setting {setting}")
    finally:
        emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
        pl.set_trace(False)
    for f in fails:
        print("    FAIL " + f)
    r.record("Frame advance on", 1, fails)
    r.match = None


# -- long scenarios --

def sc_cycles100(r):
    r.ensure()
    n = r.opts.cycles or 100
    fails, _ = cycles(r.pl, "cycles100", gameplay(600), n, ks_for(n, 120, 600))
    r.record("Mario vs Fox, FD, long", n, fails)


def sc_rules(r):
    """The set with and without the fighters' free space (the experiments'
    v4 and v3 rules): the same 30 cycles each, counting replayed frames
    that match in every value but differ in the fighter heaps' bytes."""
    r.into_match()
    out = []
    for v3 in (0, 1):
        r.pl.put(PR_RULES, ">B", v3)
        fails, _ = cycles(r.pl, "rules v3" if v3 else "rules v4", gameplay(600), 30, ks_for(30, 120, 600))
        out.append((v3, fails, r.pl.sizes[-1], r.pl.hash_only[-1]))
    r.pl.put(PR_RULES, ">B", 0)
    for v3, fails, size, h in out:
        r.record(f"Mario vs Fox, FD, {'v3' if v3 else 'v4'} rules", 30, fails,
                 f"set {size[0]} B; hash-only frames {h[3]} of {h[4]} in {h[2]} cycles")
    r.match = None


def sc_loads300(r):
    r.ensure()
    fails, _ = cycles(r.pl, "loads300", gameplay(40), 300, ks_for(300, 20, 40))
    r.record("300 loads of one slot", 300, fails)


def pair(p1, cpu, script=gameplay, count=20):
    def sc(r):
        r.ensure(p1, cpu)
        # Some pairs still stream files a few frames past 100 (a save there
        # is refused: files loading).
        r.pl.go([N] * 20)
        fails, _ = cycles(r.pl, f"{p1}-{cpu}", script(600), count, ks_for(count, 120, 600))
        s = r.pl.sizes[-1]
        r.record(f"{p1} vs {cpu}, FD", count, fails, f"set {s[0]} B ({s[1]} B without free space)")
    return sc


def stage(name, count=20, frames=600):
    def sc(r):
        r.ensure("mario", "fox", name)
        fails, _ = cycles(r.pl, name, gameplay(frames), count, ks_for(count, 120, frames))
        s = r.pl.sizes[-1]
        r.record(f"Mario vs Fox, {name}", count, fails, f"set {s[0]} B")
    return sc


def situation(name, pads, test, match=("mario", "fox"), count=20, window=None):
    """Find the first frame of `pads` (played from the park at 100) where
    test(P1 fields, CPU fields) holds, go back to the park (emulator state),
    play up to that frame and save there; then cycles."""
    def sc(r):
        r.into_match(*match)
        pl = r.pl
        base = pl.parked()
        state = os.path.join(session_dir(), "situation.sav")
        r.emu.h.call("save_state", path=state)
        seq = pl.u32(PR_PARK_SEQ)
        last2 = pl.last2
        pl.set_trace(True)
        rows = parse_rows(pl.go(pads))
        hit = None
        thrown = False
        for t, f, v in rows:
            ff = fighter_fields(v)
            if t != " " or f <= base or 1 not in ff or 2 not in ff:
                continue
            if test == "projectile":
                # The first Wait frame after the neutral special: the
                # fireball has left Mario's hand and is in flight.
                thrown = thrown or ff[1][2] == ST_SPECIAL_N
                ok = thrown and ff[1][2] == 0
            else:
                ok = test(ff[1], ff[2])
            if ok:
                hit = f
                break
        if hit is None:
            raise SaveStateError(f"{name}: the situation never happened in {len(pads)} frames")
        r.emu.h.call("load_state", path=state)
        time.sleep(0.2)
        if pl.u32(PR_PARK_SEQ) != seq or pl.parked() != base:
            raise SaveStateError(f"{name}: the emulator state did not bring back the park at {base}")
        n = hit - base
        pl.last2 = last2
        pl.log.take()
        pl.go(pads[:n])
        at = parse_rows(pl.log.take())
        pre = (pads[n - 2], pads[n - 1])
        script = window() if window else [pads[min(n + i, len(pads) - 1)] for i in range(400)]
        fails, first = cycles(pl, name, script, count, ks_for(count, 60, 400), pre)
        ff = fighter_fields(pl.saved[2])
        r.record(f"save {name}", count, fails,
                 f"at the save: P1 status {ff[1][2]:#x} hitlag {ff[1][6]}, CPU status {ff[2][2]:#x} "
                 f"hitlag {ff[2][6]}")
        pl.note(f"{name}: saved at frame {pl.saved[1]}: P1 {ff[1]}, CPU {ff[2]}")
    return sc


SITUATIONS = {
    # The forward smash connects: both fighters in hitlag.
    "hitlag": ([P(stick=RIGHT)] * 58 + [N] * 4 + [P(c=RIGHT)] + [N] * 200,
               lambda p, c: c[6] > 0),
    # A neutral air's hitbox out (Mario's is active for many frames).
    "hitbox": ([N] * 4 + [P(["X"])] + [N] * 5 + [P(["A"])] + [N] * 200,
               lambda p, c: p[2] == ST_ATTACK_AIR and p[3] >= 8),
    # P1 holds the CPU after a grab.
    "grab": ([P(stick=RIGHT)] * 66 + [N] * 2 + [P(["Z"])] + [N] * 200,
             lambda p, c: p[2] == ST_CATCH_WAIT and c[2] == ST_CAPTURE_WAIT),
    # P1 hangs on Final Destination's left ledge.
    "ledge": ([P(stick=LEFT)] * 40 + [N] * 15 + [P(["B"], (178, 228))] + [P(stick=RIGHT)] * 150,
              lambda p, c: p[2] == ST_CLIFF_WAIT),
    # A fireball in flight, well after it left Mario's hand.
    "projectile": ([N] * 4 + [P(["B"])] + [N] * 200, "projectile"),
}


def sc_ko(r):
    """Save, then P1 runs off the stage and is knocked out (the CPU on Stop
    sometimes recovered from a forward smash at 300%), wait until it is
    back on the respawn platform, load: back as saved (5 times)."""
    r.into_match()
    pl = r.pl
    pl.go([N] * 3)
    pl.set_trace(True)
    saved, text = pl.save([P(stick=LEFT)] * 200 + [N] * 160)
    seen = set(statuses(parse_rows(text), 1))
    fails = []
    if not ({ST_DEAD, ST_REBIRTH} & seen):
        fails.append(f"P1 was not knocked out (statuses {sorted(seen)})")
    for i in range(5):
        row, text = pl.load([N] * 60)
        if not same_row(row, saved):
            fails.append(f"load {i + 1} after the KO: {row} != {saved}")
    pl.set_trace(False)
    pl.note(f"ko: P1 knocked out in the window (Dead/Rebirth seen: {bool({ST_DEAD, ST_REBIRTH} & seen)}); 5 loads "
            f"back to frame {saved[1]}, rows equal: {not fails}")
    for f in fails:
        print("    FAIL " + f)
    r.record("save, KO, load", 5, fails)
    r.match = None


def sc_cpus3(r):
    r.ensure()
    r.pl.unpark()
    nav.set_training_cpus(r.emu, 3)
    r.emu.advance(60)
    r.pl.park_at(r.pl.frame() + 2)
    fails = []
    try:
        r.refused("cpus3", SAVE, "needs exactly 2 fighters")
        blocks = [r.g.heap_blocks("Fighter3Resoruce"), r.g.heap_blocks("Fighter4Resoruce")]
        r.pl.note(f"cpus3: Fighter3/4Resoruce hold {blocks} blocks (the extra CPUs' files)")
    except SaveStateError as e:
        fails.append(str(e))
    r.pl.unpark()
    nav.set_training_cpus(r.emu, 1)
    r.record("3 CPUs", 1, fails, "refused: needs exactly 2 fighters")
    r.match = None


def sc_items(r):
    """Items on stage at the save (Training's Items row); P1 runs through
    them, picking up and using them, in every window."""
    r.ensure()
    r.pl.unpark()
    nav.spawn_items(r.emu, kinds=3, each=1)
    r.emu.advance(30)
    r.pl.park_at(r.pl.frame() + 2)
    fails, _ = cycles(r.pl, "items", roam(400), 20, ks_for(20, 60, 400))
    r.record("items on stage at the save", 20, fails)
    r.match = None


def sc_kirby_copy(r):
    """Kirby inhales the CPU and takes its power (its hat is a file the game
    may load then), then a load to before the copy: every load must either
    bring back the saved row or be refused with a reason, and the game must
    keep running."""
    r.into_match("kirby", "fox")
    pl = r.pl
    pl.set_trace(True)
    pl.go([N] * 3)
    saved, _ = pl.save([N] * 2)
    copy = [P(stick=RIGHT)] * 50 + [N] * 4 + [P(["B"])] + [N] * 40 + [P(stick=DOWN)] + [N] * 150
    kirby_heaps = ("FighterKirbyResource1", "FighterKirbyResource2", "FighterKirbyResource3")
    before = [r.g.heap_blocks(h) for h in kirby_heaps]
    text = pl.go(copy)
    after = [r.g.heap_blocks(h) for h in kirby_heaps]
    swallowed = any(fighter_fields(v).get(1, [0, 0, -1])[2] in (0x117, 0x118) for _, _, v in parse_rows(text))
    ok, why, row, text = pl.act(LOAD, [N] * 60)
    fails = []
    if ok and not same_row(row, saved):
        fails.append(f"restored row {row} != saved {saved}")
    note = (f"inhale and swallow seen: {swallowed}; FighterKirbyResource1-3 blocks {before} -> {after}; "
            + ("loaded back to the save" if ok else f"load refused: {why}"))
    pl.go([N] * 60)
    # A save after the copy, then a load from it.
    ok2, why2, srow, _ = pl.act(SAVE, [N] * 30)
    if ok2:
        row2, _ = pl.load([N] * 30)
        if not same_row(row2, srow):
            fails.append("after the copy: restored row differs from the saved one")
        note += "; a save after the copy loads back exactly"
    else:
        note += f"; save after the copy refused: {why2}"
    pl.set_trace(False)
    pl.note(f"kirby-copy: {note}")
    r.record("Kirby copies the CPU between save and load", 2, fails, note)
    r.match = None


def sc_assist(r):
    """An Assist Trophy (the first entry of Training's Items row): saved
    before P1 takes it, the assist figure out, then loads."""
    r.into_match()
    pl = r.pl
    pl.unpark()
    nav.spawn_items(r.emu, kinds=1, each=1)
    r.emu.advance(30)
    pl.park_at(pl.frame() + 2)
    pl.set_trace(True)
    pl.go([N] * 3)
    saved, _ = pl.save([N] * 2)
    text = pl.go(roam(300))
    ok, why, row, _ = pl.act(LOAD, [N] * 60)
    fails = []
    if ok and not same_row(row, saved):
        fails.append(f"restored row {row} != saved {saved}")
    note = "load after the assist came out: " + ("back to the save" if ok else f"refused: {why}")
    ok2, why2, srow, _ = pl.act(SAVE, roam(200))
    if ok2:
        row2, _ = pl.load([N] * 60)
        if not same_row(row2, srow):
            fails.append("restored row differs from the saved one (save with the assist out)")
        note += "; save with the assist out: loads back exactly"
    else:
        note += f"; save with the assist out refused: {why2}"
    pl.set_trace(False)
    pl.note(f"assist: {note}")
    r.record("Assist Trophy", 2, fails, note)
    r.match = None


# ---- the cast sweep ---------------------------------------------------------------------
# A reduced scenario that asks nothing of a character's moves, for any pair
# of characters on any stage (--only sweep). One character is the actor, as
# the player's character or as the CPU (--side); the other fighter is the
# partner. Both are driven by the plugin's script source. Each situation
# starts from the same emulator state of the match (frame 160): the actor
# plays the situation's inputs, the state is saved while it acts, both play
# on for SWEEP_WINDOW frames (the first pass), then the state is loaded
# twice and the same inputs replayed, the whole window and about half of it.
# Checked with the machinery of every other scenario: the restored row equals
# the saved row, and every replayed frame's row equals the first pass's
# (random number, both fighters and partners, heap usage). Beyond the rows,
# the heaps in SWEEP_HEAPS and SWEEP_NOTED are read at the end of the first
# pass and at the end of the whole replay and compared byte for byte
# (allocated blocks; the fighters' heaps whole), which covers what the rows
# do not show: articles, items and stage objects. A difference in
# ItemInstance or StageInstance fails the situation; the others are listed
# in the results (see heap_diffs() and UPDATE_ID_COUNTER for what is known
# to differ). --close plays the situations of sweep_close() instead, with
# the actor next to the partner.

SWEEP_WINDOW = 240
SWEEP_BASE = 160
SWEEP_CLOSE = 12.0  # --close: units between the fighters once the actor has walked up
SWEEP_UNTIL = 40    # frames a situation waits for its condition before it saves anyway
# Heaps compared byte for byte at the end of the window (name, whole range).
# Of the first group ItemInstance and StageInstance fail the situation; the
# fighters' heaps and the second group are only listed.
SWEEP_HEAPS = (("Fighter1Instance", True), ("Fighter2Instance", True), ("ItemInstance", False),
               ("StageInstance", False))
SWEEP_NOTED = (("Physics", False), ("Effect", False), ("FighterEffect", False), ("InfoInstance", False))
# Block counts noted at the save: items, effects.
SWEEP_COUNTS = ("ItemInstance", "Effect", "FighterEffect")
# gfTask::updateId (0x8002DB20) gives a task the id 0x80000000 | counter and
# counts up; its one caller activates a stage object that already exists (an
# article: a projectile). The counter is a word of the DOL's small data that
# a state does not hold, so articles activated after a load carry later ids
# than in the first pass. heap_diffs() tells such words from the rest; with
# --fix-ids the test writes the counter back before every load.
UPDATE_ID_COUNTER = 0x8059C66C
SWEEP_FIELDS = {"r": "random", "h": "fighter heaps", "u": "heap usage"}
FIGHTER_FIELDS = ("x", "y", "status", "animation frame", "damage", "hitstun", "hitlag")


def run_of(*parts):
    """One pad per frame from (frames, pad) parts."""
    out = []
    for n, p in parts:
        out.extend([p] * n)
    return out


def mirrored(pads):
    """The same inputs for a fighter that faces left."""
    return [(b, (256 - s[0], s[1]), (256 - c[0], c[1])) for b, s, c in pads]


def sweep_tail(n):
    """What the actor does in every window after the situation's own
    inputs, `n` frames of it: a jab, a jump with an aerial, a shield, a
    neutral special, a down smash, a few steps back and a side special; in a
    longer window then a jump, a jab and a neutral special every few seconds."""
    once = run_of((20, N), (1, P(["A"])), (18, N), (1, P(["X"])), (8, N), (1, P(["A"])), (36, N),
                  (12, P(["R"])), (10, N), (1, P(["B"])), (40, N), (1, P(c=DOWN)), (45, N),
                  (10, P(stick=(68, 128))), (6, N), (1, P(["B"], RIGHT)), (60, N))
    out = list(once)
    while len(out) < n:
        out += run_of((1, P(["X"])), (99, N), (1, P(["A"])), (99, N), (1, P(["B"])), (99, N))
    return out[:n]


def sweep_partner(n):
    """The partner's inputs from the base frame on: three neutral specials,
    a walk forward, a jump with an aerial, a down special, a shield, a
    forward smash, more neutral specials."""
    s = run_of((5, N), (1, P(["B"])), (14, N), (1, P(["B"])), (14, N), (1, P(["B"])), (14, N),
               (20, P(stick=(178, 128))), (5, N), (1, P(["X"])), (8, N), (1, P(["A"])), (40, N),
               (1, P(["B"], DOWN)), (25, N), (15, P(["R"])), (10, N), (1, P(c=RIGHT)), (50, N),
               (1, P(["B"])), (20, N), (1, P(["X"])), (10, N), (1, P(["B"])), (50, N))
    while len(s) < n:
        s += run_of((1, P(["B"])), (119, N), (1, P(["A"])), (119, N))
    return s[:n]


def sweep_situations(actor):
    """[(name, inputs before the save, the window's first inputs, frames
    the partner stands still first)] for an actor that faces right."""
    b, fb, ub, db = P(["B"]), P(["B"], RIGHT), P(["B"], UP), P(["B"], DOWN)
    throw = P(["A"], (188, 128))
    out = [
        ("mix", run_of((4, N), (16, P(stick=(178, 128))), (3, N), (1, P(["X"])), (6, N), (1, P(["A"])), (4, N)),
         [], 0),
        ("neutral-b held 20", run_of((4, N), (20, b)), run_of((15, b)), 0),
        ("neutral-b +30", run_of((4, N), (1, b), (30, N)), [], 0),
        ("side-b held 20", run_of((4, N), (20, fb)), run_of((10, fb)), 0),
        ("side-b +45", run_of((4, N), (1, fb), (45, N)), [], 0),
        ("up-b +25", run_of((4, N), (1, ub), (25, N)), run_of((30, P(stick=(68, 200))), (40, N)), 0),
        ("down-b held 20", run_of((4, N), (20, db)), run_of((10, db), (20, N), (1, throw)), 0),
        ("down-b held 30, A +10", run_of((4, N), (30, db), (20, N), (1, throw), (10, N)), [], 0),
    ]
    if actor in SWEEP_CHARGERS:
        out.append(("neutral-b held 130, +10", run_of((4, N), (130, b), (10, N)), run_of((5, N), (1, b)), 0))
    return out + SWEEP_EXTRA.get(actor, [])


def sweep_close():
    """Situations with the actor standing next to the partner (--close):
    [(name, inputs before the save, the window's first inputs, the
    partner's inputs before the save or None to stand still, a condition on
    (actor fields, partner fields) to wait for before the save or None)]."""
    b, fb, db = P(["B"]), P(["B"], RIGHT), P(["B"], DOWN)
    jabs = run_of(*([(1, P(["A"])), (14, N)] * 4))
    return [
        # The partner is held: saved once it is in a capture status.
        ("close: grab", run_of((2, N), (1, P(["Z"]))), run_of((20, N), (1, P(stick=RIGHT))), None,
         lambda a, p: p[2] in (ST_CAPTURE_PULLED, ST_CAPTURE_WAIT)),
        # A jab connects: saved on the partner's first hitlag frame.
        ("close: jab", run_of((2, N), (1, P(["A"]))), [], None, lambda a, p: p[6] > 0),
        ("close: neutral-b held 20, partner jabs", run_of((2, N), (20, b)), run_of((15, b)), jabs, None),
        ("close: side-b +20", run_of((2, N), (1, fb), (20, N)), [], None, None),
        ("close: down-b held 20, partner jabs", run_of((2, N), (20, db)), run_of((10, db)), jabs, None),
    ]


def sweep_approach(r, side):
    """From the base park: the actor walks up to the partner, which stands
    still, until they are SWEEP_CLOSE apart; returns the distance left."""
    pl = r.pl
    walk, wait = [P(stick=(183, 128))] * 4, [N] * 4
    gap = None
    for _ in range(90):
        rows = parse_rows(pl.go(walk, cpu=wait) if side == "hmn" else pl.go(wait, cpu=mirrored(walk)))
        ff = fighter_fields(rows[-1][2]) if rows else {}
        if 1 not in ff or 2 not in ff:
            break
        gap = abs(ff[1][0] - ff[2][0])
        if gap < SWEEP_CLOSE:
            break
    pl.go([N] * 10, cpu=[N] * 10)
    return gap


KIRBY_COPY = run_of((60, P(stick=RIGHT)), (4, N), (45, P(["B"])), (26, N), (1, P(["B"])), (45, N))
# Characters whose neutral special charges or draws while held.
SWEEP_CHARGERS = ("samus", "zero_suit_samus", "lucario", "mewtwo", "donkey_kong", "sheik", "diddy_kong",
                  "squirtle", "link", "toon_link", "pit", "rob")
SWEEP_EXTRA = {
    # Run to the partner (it stands still), inhale it, swallow it with a
    # second B: the copy ability is on at the save, and used in the window.
    # Then the same from a save before the copy: every load goes back to
    # before it. (The partner's damage at the save shows that it was
    # swallowed; Kirby's neutral special is then another status.)
    "kirby": [("copy the partner, +45", run_of((4, N)) + KIRBY_COPY,
               run_of((5, N), (1, P(["B"])), (30, N), (1, P(["B"]))), 4 + len(KIRBY_COPY)),
              ("copy the partner in the window", run_of((4, N)), KIRBY_COPY + run_of((1, P(["B"])), (20, N)),
               4 + len(KIRBY_COPY))],
    # Four plucks, then a throw.
    "olimar": [("pluck x4, side-b +12",
                run_of((4, N), *([(1, P(["B"])), (24, N)] * 4), (1, P(["B"], RIGHT)), (12, N)), [], 0)],
    # Off the bike: it stays on the stage.
    "wario": [("side-b, jump off, +25", run_of((4, N), (1, P(["B"], RIGHT)), (30, N), (1, P(["X"])), (25, N)), [],
               0)],
    # The down smash (a mine in Brawl).
    "snake": [("down smash +60", run_of((4, N), (1, P(c=DOWN)), (60, N)), [], 0)],
}


def start_on_stage_kind(emu, p1, cpu, kind):
    """As nav.start_training_match, for a stage that is not on the stage
    select's first page: the first page's Final Destination is taken, and the
    match setup's stage kind replaced before the match scene reads it."""
    nav.choose_on_css(emu, p1, cpu)
    nav.press_until(emu, "the stage select", ["START"], lambda: nav.scene(emu)[0] == "scSelStage", 600)
    nav.pick_stage(emu, "final_destination")
    emu.wait_for("the stage select to exit", lambda: nav.scene(emu)[0] != "scSelStage", 600)
    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
    emu.h.call("write_mem", addr=mode + nav.MELEE_STAGE, hex=struct.pack(">H", kind).hex())
    nav.seed_match_rng(emu)
    nav.wait_match_start(emu)
    setup = nav.melee_setup(emu)
    if setup["mode"] != nav.MODE_TRAINING or setup["stage"] != kind:
        raise nav.NavError(f"match setup is mode {setup['mode']:#x}, stage {setup['stage']:#x}")


def row_fields(values):
    """A row's values by name."""
    head, *parts = values.split("|")
    out = {}
    for w in head.split():
        k, _, v = w.partition("=")
        out[SWEEP_FIELDS.get(k, k)] = v
    for part in parts:
        w = part.split()
        if not w:
            continue
        for name, v in zip(FIGHTER_FIELDS, w[1:8]):
            out[f"{w[0]} {name}"] = v
        for name, v in zip(FIGHTER_FIELDS, w[8:]):
            out[f"{w[0]} partner {name}"] = v.lstrip("+")
    return out


def row_diff(first, replay):
    """The values in which two rows differ (the fighter-heap hash aside)."""
    a, b = row_fields(first), row_fields(replay)
    return [f"{k} {a.get(k)} -> {b.get(k)}" for k in sorted(set(a) | set(b))
            if a.get(k) != b.get(k) and k != "fighter heaps"]


def heap_layout(start, raw):
    """[(offset, size, free)] of a heap's blocks from its bytes (the layout
    is in scripts/memdump.py), or None if the walk fails."""
    import memdump as M

    def u32(off):
        return struct.unpack_from(">I", raw, off)[0]
    free = set()
    sentinel = u32(M.POOL_SENTINEL)
    if start <= sentinel < start + len(raw):
        b = u32(sentinel - start + M.BLOCK_NEXT)
        while start <= b < start + len(raw) and b != sentinel and b not in free and len(free) < 100000:
            free.add(b)
            b = u32(b - start + M.BLOCK_NEXT)
    out = []
    a = M.POOL_OBJECT
    while a < len(raw):
        size = u32(a + M.BLOCK_SIZE)
        if size < M.BLOCK_HEADER or size % 0x20 or a + size > len(raw):
            return None
        out.append((a, size, start + a in free))
        a += size
    return out


def heap_diffs(heaps, first, replay, place):
    """Differences between two reads of `heaps`: (lines, ids). A line per
    heap gives the number of 4-byte words that differ and where the first
    ones are (place(start, bytes, offset) names the place), with both
    values. Words that hold an updateId task id in
    both reads (see UPDATE_ID_COUNTER) are left out of the lines and
    counted in ids, {heap: (words, the steps between the two ids)}. Free
    blocks' contents are not compared (a state does not hold them), except
    in the fighters' heaps, which a state holds whole."""
    out, ids = [], {}
    for name, whole in heaps:
        if name not in first or name not in replay:
            if (name in first) != (name in replay):
                out.append(f"{name}: read in one pass only")
            continue
        (start, a), (_, b) = first[name], replay[name]
        if a == b:
            continue
        if len(a) != len(b):
            out.append(f"{name}: {len(a)} bytes, then {len(b)}")
            continue
        spans = [(0, len(a))]
        if not whole:
            la, lb = heap_layout(start, a), heap_layout(start, b)
            if la is None or la != lb:
                out.append(f"{name}: the blocks differ ({len(la or [])} blocks, then {len(lb or [])})")
                continue
            spans = [(0, 0x80)] + [(off, off + size if not free else off + 0x20) for off, size, free in la]
        words = []
        for lo, hi in spans:
            if a[lo:hi] != b[lo:hi]:
                words += [o for o in range(lo, hi, 4) if a[o:o + 4] != b[o:o + 4]]
        def task_id(off):
            return a[off:off + 2] == b[off:off + 2] == b"\x80\x00"
        moved = [o for o in words if task_id(o)]
        if moved:
            ids[name] = (len(moved), sorted(set(int.from_bytes(b[o:o + 4], "big") - int.from_bytes(a[o:o + 4], "big")
                                               for o in moved)))
        words = [o for o in words if not task_id(o)]
        if not words:
            continue

        places = [f"{place(start, a, o)} {a[o:o + 4].hex()}>{b[o:o + 4].hex()}" for o in words[:12]]
        out.append(f"{name}: {len(words)} words differ ({', '.join(places)}{', ...' if len(words) > 12 else ''})")
    return out, ids


def sweep_situation(r, res, hmn_pre, hmn_window, cpu_pre, cpu_window, actor_player, shot, until=None):
    """One situation from the base park: fills `res` (a dict for the
    results file) and returns the failures. With `until`, the save waits
    (both fighters neutral) until until(actor fields, partner fields)
    holds, at most SWEEP_UNTIL frames."""
    pl, emu = r.pl, r.emu
    fails = []
    rows = parse_rows(pl.go(hmn_pre, cpu=cpu_pre))
    if until is not None:
        res["reached"] = False
        for _ in range(SWEEP_UNTIL):
            ff = fighter_fields(rows[-1][2]) if rows else {}
            if actor_player in ff and 3 - actor_player in ff and until(ff[actor_player], ff[3 - actor_player]):
                res["reached"] = True
                break
            rows += parse_rows(pl.go([N], cpu=[N]))
    seen = set(statuses(rows, actor_player))
    res["counts"] = {h: r.g.heap_blocks(h) for h in SWEEP_COUNTS}
    res["shot"] = r.shot(shot)
    counter = emu.u32(UPDATE_ID_COUNTER)
    ok, m, saved, text = pl.act(SAVE, hmn_window, cpu=cpu_window)
    if not ok:
        res["save"] = f"refused: {m}"
        # Asked again with a short wait, for a picture of what the player is told.
        again = pl.act(SAVE, [N] * 20, cpu=[N] * 20)
        r.shot(shot + "-refused")
        res["save again after the window"] = "ok" if again[0] else f"refused: {again[1]}"
        return [f"save refused: {m}"]
    first = parse_rows(text)
    f0 = saved[1]
    if saved[2] is None:
        return ["the save logged no row"]
    size = pl.sizes[-1]
    at = fighter_fields(saved[2])
    res.update({"save": "ok", "frame": f0, "bytes": size[0], "segments": size[2], "save_ms": pl.costs["save"][-1],
                "ids in the window": emu.u32(UPDATE_ID_COUNTER) - counter,
                "status": {f"P{p}": f"{v[2]:#x}" for p, v in at.items()},
                "damage": {f"P{p}": v[4] for p, v in at.items()},
                "statuses": sorted(f"{s:#x}" for s in seen | set(statuses(first, actor_player)) if s >= 0)})
    if len([x for x in first if x[0] == " " and f0 < x[1] <= f0 + len(hmn_window)]) < len(hmn_window) - 1:
        fails.append(f"{len(first)} trace rows for {len(hmn_window)} frames in the first pass")
    names = [h for h, _ in SWEEP_HEAPS + SWEEP_NOTED]
    end = r.g.heap_bytes(names)
    if r.opts.dump:
        import memdump
        memdump.capture(emu, os.path.join(session_dir(), shot + "-first"))
    res["loads"] = []
    for k in (len(hmn_window), len(hmn_window) // 2 + 7):
        if r.opts.fix_ids:
            emu.h.call("write_mem", addr=UPDATE_ID_COUNTER, hex=f"{counter:08x}")
        ok, m, row, text = pl.act(LOAD, hmn_window[:k], cpu=cpu_window[:k])
        if not ok:
            res["loads"].append(f"refused: {m}")
            fails.append(f"load refused: {m}")
            continue
        entry = {"frames": k, "restored": same_row(row, saved), "load_ms": pl.costs["load"][-1],
                 "check_ms": pl.costs["check"][-1]}
        if not entry["restored"]:
            fails.append("restored row differs from the saved row: "
                         + "; ".join(row_diff(saved[2], row[2]) if row else ["no row"]))
        n, bad, hashed, where = compare(first, parse_rows(text), f0 + k)
        entry.update({"compared": n, "differ": bad, "hash_only": hashed})
        if n < k - 1:
            fails.append(f"{n} rows compared for {k} frames")
        if bad:
            entry["first"] = {"frame": where[0] - f0, "values": row_diff(where[1], where[2])}
            fails.append(f"{bad} of {n} replayed frames differ, first {where[0] - f0} frames after the save: "
                         + "; ".join(entry["first"]["values"][:4]))
        if k == len(hmn_window):
            again = r.g.heap_bytes(names)
            if r.opts.dump:
                memdump.capture(emu, os.path.join(session_dir(), shot + "-replay"))
            entry["heaps"], entry["ids"] = heap_diffs(SWEEP_HEAPS, end, again, r.g.place)
            entry["noted"], _ = heap_diffs(SWEEP_NOTED, end, again, r.g.place)
            # The fighters' heaps are reported, not judged: single words in
            # them differ between two runs of the same match from boot too.
            real = [d for d in entry["heaps"] if not d.startswith("Fighter")]
            if real:
                fails.append("memory at the end of the replay differs from the first pass: " + "; ".join(real))
        res["loads"].append(entry)
    return fails


def sweep_match(r, actor, side, other, stage, out):
    """Every situation of `actor` in one match; appends a result per
    situation to the file `out`."""
    pl, emu = r.pl, r.emu
    p1, cpu = (actor, other) if side == "hmn" else (other, actor)
    r.into_match(p1, cpu, stage)
    pl.set_trace(True)
    pl.go([N] * (SWEEP_BASE - pl.parked()), cpu=[N] * (SWEEP_BASE - pl.parked()))
    base, seq, last2 = pl.parked(), pl.u32(PR_PARK_SEQ), pl.last2
    state = os.path.join(session_dir(), "sweep-base.sav")
    emu.h.call("save_state", path=state)
    close = r.opts.close
    if close:
        gap = sweep_approach(r, side)
        print(f"    walked up: {gap} units apart")
        base, seq, last2 = pl.parked(), pl.u32(PR_PARK_SEQ), pl.last2
        emu.h.call("save_state", path=state)
    wanted = [s for s in (sweep_close() if close else sweep_situations(actor))
              if not r.opts.situations or any(w in s[0] for w in r.opts.situations)]
    all_fails, lines = [], []
    stage_name = stage if isinstance(stage, str) else f"{stage:#x}"
    for i, situation in enumerate(wanted):
        name, pre, window = situation[:3]
        if i:
            emu.h.call("load_state", path=state)
            time.sleep(0.2)
            if pl.u32(PR_PARK_SEQ) != seq or pl.parked() != base:
                raise SaveStateError(f"{actor}: the emulator state did not bring back the park at {base}")
            pl.last2, pl.saved = last2, None
            pl.log.take()
        length = r.opts.window or SWEEP_WINDOW
        window = (window + sweep_tail(length))[:length]
        if close:
            partner_pre = ((situation[3] or []) + [N] * len(pre))[:len(pre)]
            partner_window, until = sweep_partner(len(window)), situation[4]
        else:
            partner = [N] * situation[3] + sweep_partner(len(pre) + len(window))
            partner_pre, partner_window, until = partner[:len(pre)], partner[len(pre):len(pre) + len(window)], None
        if side == "hmn":
            scripts = (pre, window, mirrored(partner_pre), mirrored(partner_window))
        else:
            scripts = (partner_pre, partner_window, mirrored(pre), mirrored(window))
        res = {"actor": actor, "side": side, "p1": p1, "cpu": cpu, "stage": stage_name, "situation": name}
        shot = f"sweep-{actor}-{side}-{stage_name}-{re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')}"
        try:
            fails = sweep_situation(r, res, *scripts, 1 if side == "hmn" else 2, shot, until)
        except SaveStateError as e:
            fails = [f"error: {e}"]
            res["error"] = str(e)
        res["fails"] = fails
        with open(out, "a", encoding="utf-8") as f:
            f.write(json.dumps(res) + "\n")
        loads = [x for x in res.get("loads", []) if isinstance(x, dict)]
        print(f"    {name:32s} {'ok  ' if not fails else 'FAIL'} save {res.get('save', '-'):10s} "
              f"status {res.get('status', {})} items/effects {list(res.get('counts', {}).values())} "
              f"bytes {res.get('bytes', 0)} hash-only {[x['hash_only'] for x in loads]} "
              f"ids {[x.get('ids') for x in loads[:1]]} "
              f"heaps {[x.get('heaps', []) + x.get('noted', []) for x in loads[:1]]}")
        for f in fails:
            print("      FAIL " + f)
        all_fails += [f"{name}: {f}" for f in fails]
        lines.append(res)
    pl.set_trace(False)
    pl.unpark()
    sizes = sorted(set(x["bytes"] for x in lines if "bytes" in x))
    r.record(f"sweep {p1} vs {cpu}, {stage_name} ({side})", len(wanted), all_fails,
             f"{len(wanted)} situations; set {sizes[0] if sizes else 0}-{sizes[-1] if sizes else 0} B")
    r.match = None


def sc_sweep(r):
    """The reduced scenario for every --sweep character on every --stages
    stage (by default the one match of --hmn, --cpu and --stage)."""
    opts = r.opts
    out = os.path.join(session_dir(), "sweep-results.jsonl")
    hmn, cpu, stage = r.chosen()
    actors = opts.sweep or [hmn if opts.side == "hmn" else cpu]
    other = cpu if opts.side == "hmn" else hmn
    stages = opts.stages or [stage]
    print(f"   results appended to {out}")
    broken = 0
    for stage in stages:
        for actor in actors:
            partner = other if other != actor or not opts.sweep else ("mario" if actor != "mario" else "fox")
            print(f"  -- {actor} as the {'player' if opts.side == 'hmn' else 'CPU'} with {partner}, "
                  f"{stage if isinstance(stage, str) else f'stage kind {stage:#x}'}")
            t0 = time.monotonic()
            try:
                sweep_match(r, actor, opts.side, partner, stage, out)
                broken = 0
            except (SaveStateError, Timeout, nav.NavError, HarnessError) as e:
                print(f"    ERROR {e}")
                r.results.append((f"sweep {actor} ({opts.side}), {stage}", 0, "ERROR", str(e)))
                with open(out, "a", encoding="utf-8") as f:
                    f.write(json.dumps({"actor": actor, "side": opts.side, "stage": str(stage),
                                        "situation": "(match)", "error": str(e), "fails": [str(e)]}) + "\n")
                try:
                    r.emu.screenshot(f"sweep-fail-{actor}-{opts.side}")
                    r.emu.h.call("resume")
                except (OSError, HarnessError, ValueError):
                    pass
                r.match = None
                broken += 1
                if broken >= 2:
                    print("    two matches in a row failed: the sweep stops here")
                    return
            print(f"     ({time.monotonic() - t0:.0f} s)")


def sc_transform(r):
    """A character that changes into another with its down special (Zelda
    and Sheik; --hmn or every --sweep character, by default zelda): the
    game loads the other fighter's files and swaps the fighter of the
    entry. Nothing is expected here but a clean answer: the test notes what
    a save answers every 10 frames through the change, what a load of a
    state from before the change answers once changed and once changed
    back, and takes a picture of each refusal. A state that does load must
    bring back its saved row, and a replay that changes again must match
    the first pass."""
    for p1 in r.opts.sweep or [r.opts.hmn or "zelda"]:
        transform(r, p1)


def transform(r, p1):
    pl, emu = r.pl, r.emu
    change = run_of((1, P(["B"], DOWN)), (9, N))
    fails, notes = [], []

    def active():
        """The active fighter instance of P1's entry and its status, from memory."""
        try:
            me = min(r.g.fighters(), key=lambda f: f["player"])
            entries = emu.ptr(LT.ENTRY_MANAGER)
            for i in range(9):
                raw = emu.read(entries + i * LT.ENTRY_SIZE, 0x10)
                if struct.unpack_from(">i", raw, 4)[0] == me["entry"]:
                    return raw[0xA], r.g.f.state(me)["status"]
        except (TypeError, ValueError, struct.error):
            pass
        return -1, -1

    def probe(kind, frames=10):
        ok, m, row, text = pl.act(kind, [N] * frames)
        return ("ok" if ok else f"refused: {m}"), row

    r.into_match(p1, None, None)
    pl.set_trace(True)
    pl.go([N] * 60)
    base, seq, last2 = pl.parked(), pl.u32(PR_PARK_SEQ), pl.last2
    state = os.path.join(session_dir(), "transform-base.sav")
    emu.h.call("save_state", path=state)
    # 1. A save every 10 frames from the down special on.
    pl.go(change)
    answers = []
    for i in range(36):
        inst, status = active()
        a, _ = probe(SAVE)
        if not answers or answers[-1][1:] != (a, inst):
            answers.append((10 * (i + 1), a, inst))
            if a != "ok":
                r.shot(f"transform-{p1}-save-{len(answers)}")
    notes.append("saves through the change (frames after the down special, answer, active instance): "
                 + "; ".join(f"+{f} {a}, instance {inst}" for f, a, inst in answers))
    # 2. From the base again: a state from before the change.
    emu.h.call("load_state", path=state)
    time.sleep(0.2)
    if pl.u32(PR_PARK_SEQ) != seq or pl.parked() != base:
        raise SaveStateError("transform: the emulator state did not bring back the park")
    pl.last2, pl.saved = last2, None
    pl.log.take()
    script = change + [N] * 350
    saved, first_text = pl.save(script)
    first = parse_rows(first_text)
    inst1, status1 = active()
    a, row = probe(LOAD, 60)
    notes.append(f"changed (instance {inst1}, status {status1:#x}): load of the state from before answers {a}")
    if a == "ok":
        if not same_row(row, saved):
            fails.append("load once changed: restored row differs from the saved row")
        inst, status = active()
        notes.append(f"after that load: instance {inst}, status {status:#x}")
    else:
        r.shot(f"transform-{p1}-load-changed")
        # Change back, then load.
        pl.go(script)
        inst2, status2 = active()
        ok, m, row, text = pl.act(LOAD, script)
        notes.append(f"changed back (instance {inst2}, status {status2:#x}): load answers "
                     + ("ok" if ok else f"refused: {m}"))
        if ok:
            if not same_row(row, saved):
                fails.append("load once changed back: restored row differs from the saved row")
            n, bad, hashed, where = compare(first, parse_rows(text), saved[1] + len(script))
            notes.append(f"replay of the change after that load: {bad} of {n} frames differ"
                         + (f", first {where[0] - saved[1]} frames after the save: "
                            + "; ".join(row_diff(where[1], where[2])[:4]) if bad else ""))
            if bad:
                fails.append(f"the replayed change differs from the first pass on {bad} of {n} frames")
        else:
            r.shot(f"transform-{p1}-load-changed-back")
        a, _ = probe(SAVE, 30)
        b, row2 = probe(LOAD, 30)
        notes.append(f"then a new save answers {a}, its load {b}")
    pl.set_trace(False)
    pl.unpark()
    for n in notes:
        pl.note("transform: " + n)
    for f in fails:
        print("    FAIL " + f)
    r.record(f"{p1}: saves and loads around the change", 1, fails, notes[0][:120])
    r.match = None


LONG = {
    "cycles100": sc_cycles100,
    "loads300": sc_loads300,
    "rules": sc_rules,
    "link-samus": pair("link", "samus", specials),
    "zelda-sheik": pair("zelda", "sheik", specials),
    "iceclimbers-kirby": pair("ice_climbers", "kirby", specials),
    "kirby-olimar": pair("kirby", "olimar", specials),
    "battlefield": stage("battlefield"),
    "smashville": stage("smashville"),
    "yoshis_story": stage("yoshis_story"),
    "pokemon_stadium_2": stage("pokemon_stadium_2"),
    "hitlag": situation("hitlag", *SITUATIONS["hitlag"]),
    "hitbox": situation("hitbox", *SITUATIONS["hitbox"]),
    "grab": situation("grab", *SITUATIONS["grab"]),
    "ledge": situation("ledge", *SITUATIONS["ledge"]),
    "projectile": situation("projectile", *SITUATIONS["projectile"]),
    "ko": sc_ko,
    "items": sc_items,
    "kirby-copy": sc_kirby_copy,
    "assist": sc_assist,
    "cpus3": sc_cpus3,
}
QUICK = {"basic": sc_basic, "dpad": sc_dpad, "refusals": sc_refusals, "menu": sc_menu, "cpu-options": sc_cpu_options,
         "cpu-random": sc_cpu_random,
         "osd-overlay": sc_osd_overlay, "frame-advance": sc_frame_advance, "exit": sc_exit}
# Run only when named with --only.
EXTRA = {"sweep": sc_sweep, "transform": sc_transform}


def run_scenarios(emu, opts):
    r = Runner(emu, opts)
    r.failed_keys = []
    names = opts.only or (list(QUICK) if not opts.long else list(QUICK)[:-1] + list(LONG) + ["exit"])
    table = dict(QUICK, **LONG, **EXTRA)
    t0 = time.monotonic()
    for name in names:
        print(f"== {name}")
        emu.mark(f"savestate scenario {name}")
        t1 = time.monotonic()
        before = len(r.results)
        try:
            table[name](r)
        except (SaveStateError, Timeout, nav.NavError, HarnessError) as e:
            print(f"    ERROR {e}")
            r.results.append((name, 0, "ERROR", str(e)))
            try:
                emu.screenshot(f"savestate-fail-{name}")
                emu.h.call("resume")
            except (OSError, HarnessError, ValueError):
                pass
            r.match = None
        if any(v != "PASS" for _, _, v, _ in r.results[before:]):
            r.failed_keys.append(name)
        print(f"   ({time.monotonic() - t1:.0f} s)")
    return r, time.monotonic() - t0


def args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-savestate")
    ap.add_argument("rel")
    ap.add_argument("--long", action="store_true")
    ap.add_argument("--only", nargs="+", choices=sorted(list(QUICK) + list(LONG) + list(EXTRA)))
    ap.add_argument("--cycles", type=int, default=0)
    cast = sorted(n for n in nav.CSS_POS if n != "random")
    ap.add_argument("--hmn", choices=cast, help="the player's character (mario)")
    ap.add_argument("--cpu", choices=cast, help="the CPU's character (fox)")
    ap.add_argument("--stage", type=stage_arg, help="a stage name or a stage kind number (final_destination)")
    ap.add_argument("--sweep", nargs="+", choices=cast + ["all"], default=[],
                    help="sweep: the actors, one match each (all: the whole cast)")
    ap.add_argument("--side", choices=("hmn", "cpu"), default="hmn",
                    help="sweep: the actor is the player's character or the CPU")
    ap.add_argument("--stages", nargs="+", type=stage_arg, default=[], help="sweep: every actor on each of these")
    ap.add_argument("--situations", nargs="+", default=[], help="sweep: only situations whose name holds one of these")
    ap.add_argument("--window", type=int, default=0, help=f"sweep: frames played after the save ({SWEEP_WINDOW})")
    ap.add_argument("--close", action="store_true",
                    help="sweep: the situations with the actor next to the partner (a grab, a jab that hits, ...)")
    ap.add_argument("--fix-ids", action="store_true",
                    help="sweep: write the article id counter back before every load (see UPDATE_ID_COUNTER)")
    ap.add_argument("--dump", action="store_true",
                    help="sweep: whole-memory dumps at the end of the first pass and of the replay (memdump.py diff)")
    opts = ap.parse_args(rest)
    if "all" in opts.sweep:
        opts.sweep = [n for row in nav.CSS_GRID for n, _ in row if n != "random"]
    return opts


def stage_arg(text):
    """A stage of nav.STAGES by name, or any stage kind as a number."""
    if text in nav.STAGES:
        return text
    try:
        return int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{text!r}: one of {', '.join(sorted(nav.STAGES))}, or a stage kind number")


def summary(r, log, seconds, label):
    print()
    print(f"{'scenario':42s} {'cycles':>6s}  result  notes")
    for name, n, verdict, note in r.results:
        print(f"{name:42s} {n:6d}  {verdict:6s}  {note}")
    c = r.pl.costs
    for k in ("save", "load", "check"):
        if c[k]:
            print(f"{k}: {min(c[k]):.2f}-{max(c[k]):.2f} ms emulated over {len(c[k])}")
    for name, n, cyc, frames, compared in r.pl.hash_only:
        print(f"hash-only differences: {name}: {cyc} of {n} cycles, {frames} of {compared} frames")
    if r.pl.sizes:
        print("set sizes (bytes, without free space, segments, slot): " + str(sorted(set(r.pl.sizes))[:12]))
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    why = ""
    if any(v != "PASS" for _, _, v, _ in r.results):
        if not bad and getattr(r, "failed_keys", None):
            print("failed --only: " + " ".join(r.failed_keys))
        why = "scenario failures"
    elif bad:
        why = "problem lines in the log"
    print(f"time: {seconds:.0f} s")
    return why


def cmd_test_savestate(rest):
    opts = args(rest)
    label = "test-savestate"
    E.cmd_install(opts.rel)
    backend = E.headless_backend()
    why, log_path = "", ""
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            nav.boot_to_training_css(emu)
            r, seconds = run_scenarios(emu, opts)
            log = emu.log_text()
            why = summary(r, log, seconds, label)
    except (OSError, HarnessError, Timeout, nav.NavError, ValueError) as e:
        why = f"{e}"
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0


# ---- development session (one Dolphin across invocations) --------------------------------

def session_dir():
    d = E.env("SAVESTATE_DIR", required=False) or os.path.join(
        os.path.dirname(os.path.abspath(E.env("DOLPHIN_USER"))), "savestate")
    os.makedirs(d, exist_ok=True)
    return d


class Proc:
    """Enough of subprocess.Popen for Harness.connect, for a Dolphin
    started by an earlier invocation."""

    def __init__(self, pid):
        self.pid = pid
        self.returncode = None

    def poll(self):
        out = subprocess.run(["tasklist", "/FI", f"PID eq {self.pid}", "/NH"], capture_output=True, text=True)
        if str(self.pid) in out.stdout:
            return None
        self.returncode = -1
        return -1


def session_start(rel):
    E.check_launch_guards(os.path.abspath(E.env("DOLPHIN_USER")))
    E.cmd_install(rel)
    port = int(E.env("HARNESS_PORT", required=False) or E.free_port())
    dolphin, user, base = E.prepare(E.pad_ports([]))
    log_path = os.path.join(user, "Logs", "dolphin.log")
    for old in (log_path, E.rotated_log_path(log_path)):
        if os.path.exists(old):
            os.remove(old)
    cmd = [dolphin] + base + E.QUIET_ARGS + E.DETERMINISTIC_ARGS + ["-v", "D3D", "--harness-port", str(port)]
    E.check_pads(os.path.join(user, "Config", "Dolphin.ini"), cmd)
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            **E.low_priority())
    with open(os.path.join(session_dir(), "session.json"), "w", encoding="utf-8") as f:
        json.dump({"pid": proc.pid, "port": port}, f)
    h = Harness(port)
    if not h.connect(proc, 30):
        raise HarnessError("could not connect to the harness")
    emu = Emu(h, log_path, os.path.abspath(E.env("SCREENSHOT_DIR", required=False) or user))
    emu.wait_running(proc, 90)
    nav.boot_to_training_css(emu)
    emu.h.call("save_state", path=os.path.join(session_dir(), "css.sav"))
    print(f"session: Dolphin pid {proc.pid}, harness port {port}, on Training's character select")
    h.close()
    return 0


def session_connect():
    with open(os.path.join(session_dir(), "session.json"), encoding="utf-8") as f:
        s = json.load(f)
    proc = Proc(s["pid"])
    h = Harness(s["port"])
    if not h.connect(proc, 20):
        raise HarnessError(f"cannot reach the session (pid {s['pid']})")
    user = os.path.abspath(E.env("DOLPHIN_USER"))
    emu = Emu(h, os.path.join(user, "Logs", "dolphin.log"),
              os.path.abspath(E.env("SCREENSHOT_DIR", required=False) or user))
    return proc, emu, s


def session_quit():
    proc, emu, s = session_connect()
    try:
        emu.h.call("quit")
    except (OSError, HarnessError, ValueError):
        pass
    emu.h.close()
    for _ in range(30):
        if proc.poll() is not None:
            break
        time.sleep(0.5)
    if proc.poll() is None:
        subprocess.run(["taskkill", "/PID", str(s["pid"]), "/F"], capture_output=True)
    print(f"session pid {s['pid']} stopped")
    return 0


def session_run(rest):
    proc, emu, s = session_connect()
    try:
        opts = args(["-"] + rest)
        r, seconds = run_scenarios(emu, opts)
        why = summary(r, emu.log_text(), seconds, "session")
        print("session run: " + (why or "PASS"))
        return 1 if why else 0
    finally:
        emu.h.close()


def main():
    sys.stdout.reconfigure(line_buffering=True)
    if len(sys.argv) >= 3 and sys.argv[1] == "session":
        cmd, rest = sys.argv[2], sys.argv[3:]
        if cmd == "start" and len(rest) == 1:
            return session_start(rest[0])
        if cmd == "quit":
            return session_quit()
        if cmd == "run":
            return session_run(rest)
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
