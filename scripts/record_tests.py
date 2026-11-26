"""Emulator test of input recording and playback (run through emu.py;
settings come from the environment the same way).

  test-record REL [--only NAME ...] [--hmn NAME] [--cpu NAME] [--stage NAME]
      One boot, Mario (P1) against a Fox CPU on Final Destination (--hmn,
      --cpu and --stage choose others; the checks were written for these,
      and hmn, cpu, loop and rewind ask nothing of a character's moves), one
      CPU thread. Everything is stepped a game frame at a time while both
      fighters and their input objects are read from game memory; the
      plugin's test block (src/main/record/record.cpp) is only used to
      trigger the Recording page's actions and to read its counters.

      Scenarios, in order (later ones use the slots earlier ones recorded):
        refuse      modes refused without saved positions
        restore     the lite restore from the air, a run and a shield
        hmn         P1 driven by a scripted sequence (movement, jumps, jab,
                    shield, a C-stick smash, a taunt) while HMN Mode is
                    Record; restore; Playback: the per-frame trace (position,
                    status, animation frame, damage, hitlag, hitstun and the
                    input object) must equal the recorded run's
        cpu         CPU Mode Control copies the pad onto the CPU and holds
                    P1 neutral; Record under Control; Playback: the CPU's
                    trace must equal the recorded run's; CPU Control =
                    Training refuses
        edit        Alter Inputs through the lab menu on a copy of the HMN
                    recording: a jump added on one frame, Stick X changed on
                    another; the slot and its playback's inputs differ on
                    exactly those frames
        rewind      Frame Decrement with both sides playing back, 5, 300 and
                    600 steps in: the state of the first run one step
                    earlier, then one step forward again
        loop        Loop Input Playback twice: inputs equal frame for frame
        autorestore Auto Restore (Playback Ends): restored 20 frames after
                    the end, then the trace equals the first run
        takeover    live input takes the HMN and the CPU playback over on
                    the frame it arrives
        rerecord    Re-Record overwrites from the takeover frame
        paused      Start Paused holds frame 0 until input
        slots       six slots with six recordings; copy and delete
        full        a recording stops cleanly at 3600 frames
        advance     frame advance steps the playback one frame per press;
                    Training's pause holds it
        fallback    three CPUs: positions saved without a savestate, recording
                    and playback still work; Frame Decrement is refused; the
                    lite restore puts a CPU back standing out of a hit's
                    hitlag and flight (the launch does not stay in it), and
                    a fighter saved in the air back in the air
        oncpuhit    CPU Counter On CPU Hit: P1 jabs the CPU while its recording
                    of standing still plays; the playback ends on the hit and
                    the CPU Options counter on the first actionable frame, as
                    with no recording
        menu        the Recording page through the lab menu
        finish      the match ends with a recording loaded: storage freed,
                    heap and module back at baseline
        resetti     seeded match (0x1E6C6E03, Mr. Resetti's draw weight) keeps
                    Resetti off, preloads another assist, allocates storage and
                    records/plays back; an ordinary unseeded match still
                    preloads an assist
"""
import argparse
import os
import re
import struct
import time
import zlib

import cpu_tests as C
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import shots
from harness import HarnessError, Timeout

P1, CPU = C.P1, C.CPU
A, JUMP, GUARD = 0x1, 0x4, 0x8
FLICK = 0x8000
STATUS, sname = C.STATUS, C.sname

# main::RecordingSettings at main::Settings +184 (src/main/settings.h).
SET_REC = 184
REC = {"randomPercent": 0, "hmnMode": 2, "hmnSlot": 3, "cpuMode": 4, "cpuSlot": 5, "mirrored": 6,
       "cpuCounter": 7, "loop": 8, "autoRestore": 9, "startPaused": 10, "takeover": 11, "hmnChance": 12,
       "cpuChance": 18, "manageSide": 24, "manageSlot": 25, "manageCopyTo": 26}
HMN_MODE = {"Off": 0, "Record": 1, "Playback": 2, "Re-Record": 3}
CPU_MODE = {"Off": 0, "Control": 1, "Record": 2, "Playback": 3, "Re-Record": 4}
TAKEOVER = {"HMN": 0, "CPU": 1, "None": 2}
SET_CPU_CONTROL = C.SET_CPU  # cpu::CpuOptions::control
SET_CONTROLLED_BY = C.SET_CPU + C.CPU_FIELDS["controlledBy"]
SET_FRAME_ADVANCE = LT.SET_FRAME_ADVANCE
# Controls > Frame Decrement Button (main::Settings +29): None, Z, L, R, X, Y.
SET_DECREMENT, DECREMENT_X = 29, 4
REQ = {"save": 1, "restore": 2, "delete": 3, "resave": 4, "prune": 5, "copy": 6, "delete-slot": 7,
       "hmn-mode": 8, "cpu-mode": 9, "slot": 10, "mirror": 11}
FLAG_SAVED, FLAG_HOLDING, FLAG_HMN_CANCELLED, FLAG_CPU_CANCELLED, FLAG_SWAPPED = 0x1, 0x2, 0x4, 0x8, 0x10
FLAG_STORAGE = 0x20
FLAG_EXACT, FLAG_DETACHED, FLAG_SAVE_PENDING, FLAG_MESSAGE = 0x100, 0x200, 0x400, 0x800
# Brawl's stOperatorRuleTraining per-step CPU port reassignment site (0x80962d0c).
PATCH_ADDR, PATCH_ORIG, PATCH_SKIP = 0x80962D0C, 0x8B1BFFFF, 0x48000170

# Seeded Mr. Resetti regression check (src/main/record/record.cpp):
# g_itManager (0x80B8B7F4): +0x4C phase, +0x10BC nextAssistKind (s32),
# +0x119C m_itemSwitch (6 words; bit 161 = word 5, bit 1).
# g_mtRand seed word at 0x805A00BC: 0x1E6C6E03 gives randf() = 537/790,
# inside Resetti's [528, 546)/790 interval in COMMON3.PAC's ItmGenParam.
# gmGlobalModeMelee +0x3C: m_itSwitch.m_assist; bit 18 (1 << 18) is m_mrResetti.
# g_HeapInfos (0x80494958), 16 bytes/entry: 31 FighterTechqniq,
# 35 AssistFigureResource, 38 PokemonResource; pool +0x14 allocated block count (u16), +0x1C used.
IT_MANAGER = 0x80B8B7F4
IT_PHASE, IT_NEXT_ASSIST, IT_SWITCH = 0x4C, 0x10BC, 0x119C
MT_RAND_SEED = 0x805A00BC
RESETTI_KIND, RESETTI_SEED = 0xA1, 0x1E6C6E03
MODE_ASSIST_SWITCH, ASSIST_MR_RESETTI = 0x3C, 1 << 18
HEAP_INFOS, HEAP_FTQ, HEAP_ASSIST, HEAP_POKEMON = 0x80494958, 31, 35, 38
# Recording page rows (src/main/ui/pages/recording.cpp): Save/Restore
# Positions, HMN Mode, ...
ROW_HMN_MODE = 1
# record::Slot (src/main/record/slots.h): a 20-byte header, four 256-entry
# float tables (stick x, y, C-stick x, y), a 256-entry (L, R) table, 3600
# 8-byte frames (buttons, x, y, cx, cy, lr, press).
SLOT_HEADER, SLOT_TABLE, SLOT_PAIRS, SLOT_FRAMES = 20, 1024, 20 + 4 * 1024, 20 + 4 * 1024 + 2048
SLOT_SIZE = SLOT_FRAMES + 8 * 3600
MAX_FRAMES = 3600
# The storage block: the twelve slots, then the two packed run-log slots Frame
# Decrement replays (record::runLogSlot, 8 bytes/frame + exact float palettes).
SLOT_BYTES = 14 * SLOT_SIZE

# P1's scripted sequence (buttons, stick x, stick y, frames): walk, dash
# back, a jump and a double jump, a jab, shield, a C-stick forward smash as a
# human's arrives (one frame of attack + smash + C-stick with the stick
# pushed, then C-stick alone), an up taunt.
SEQUENCE = [
    (0, 0.0, 0.0, 6), (0, 0.5, 0.0, 20), (0, -1.0, 0.0, 15), (0, 0.0, 0.0, 12),
    (JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 14), (JUMP, 0.3, 0.0, 1), (0, 0.0, 0.0, 85),
    (A, 0.0, 0.0, 1), (0, 0.0, 0.0, 30),
    (GUARD, 0.0, 0.0, 30), (0, 0.0, 0.0, 20),
    (0x121, 1.0, 0.0, 1), (0x100, 0.0, 0.0, 4), (0, 0.0, 0.0, 60),
    (0x480, 0.0, 0.0, 1), (0, 0.0, 0.0, 70),
]
SEQUENCE_FRAMES = sum(s[3] for s in SEQUENCE)
# The CPU under Control: pad states per frame range (buttons, main stick, C-stick).
RIGHT, LEFT, UP, NEUTRAL = (228, 128), (28, 128), (128, 228), (128, 128)
# (from, to, buttons, main stick, C-stick, analog R): run left (towards the
# middle), jab, jump, a C-stick smash, shield with R pressed in, a light
# analog-only R press (Project+'s analog variables only), a taunt.
PAD_SEQUENCE = [
    (0, 20, [], LEFT, NEUTRAL, 0), (20, 32, [], NEUTRAL, NEUTRAL, 0), (32, 33, ["A"], NEUTRAL, NEUTRAL, 0),
    (33, 60, [], NEUTRAL, NEUTRAL, 0), (60, 61, ["X"], NEUTRAL, NEUTRAL, 0), (61, 110, [], NEUTRAL, NEUTRAL, 0),
    (110, 112, [], NEUTRAL, (255, 128), 0), (112, 170, [], NEUTRAL, NEUTRAL, 0),
    (170, 200, ["R"], NEUTRAL, NEUTRAL, 0), (200, 230, [], NEUTRAL, NEUTRAL, 0),
    (230, 250, [], NEUTRAL, NEUTRAL, 120), (250, 270, [], NEUTRAL, NEUTRAL, 0),
    (270, 271, ["DUP"], NEUTRAL, NEUTRAL, 0), (271, 330, [], NEUTRAL, NEUTRAL, 0),
]
PAD_FRAMES = 330
# The CPU run off the right edge under Control, then back on the respawn platform.
KO_SEQUENCE = [(0, 70, [], RIGHT, NEUTRAL, 0), (70, 400, [], NEUTRAL, NEUTRAL, 0)]
KO_FRAMES = 400
GONE = {STATUS["Dead"], STATUS["Standby"]}


class RecError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


# ---- the plugin's settings and test block ----------------------------------------

class Rec:
    def __init__(self, emu):
        self.emu = emu
        self.addr = None
        self.settings = None

    def find(self):
        found = re.findall(r"record: test block at ([0-9a-f]{8})", self.emu.log_text())
        if not found:
            raise RecError("no record test block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != 0x544D5243:
            raise RecError(f"no record test block at {self.addr:#x}")
        self.settings = LT.Lab(self.emu, []).find_settings()

    def set(self, **values):
        for name, value in values.items():
            data = bytes(value) if isinstance(value, (list, tuple)) else bytes([value])
            self.emu.h.call("write_mem", addr=self.settings + SET_REC + REC[name], hex=data.hex())

    def get(self, name):
        return self.emu.u8(self.settings + SET_REC + REC[name])

    def cpu_control(self, lab):
        self.emu.h.call("write_mem", addr=self.settings + SET_CPU_CONTROL, hex=f"{int(lab):02x}")

    def request(self, name):
        """Queued; carried out in the next game-pads pass."""
        self.emu.h.call("write_mem", addr=self.addr + 4, hex=f"{REQ[name]:08x}")

    def status(self):
        raw = self.emu.read(self.addr, 0x80)
        return {"request": u32(raw, 4), "result": u32(raw, 8), "paused_calls": u32(raw, 0xC), "steps": u32(raw, 0x10),
                "t": (u32(raw, 0x14), u32(raw, 0x18)), "flags": u32(raw, 0x1C), "restores": u32(raw, 0x20),
                "storage": u32(raw, 0x24), "bytes": u32(raw, 0x28),
                "start": [struct.unpack_from(">6H", raw, 0x2C + 12 * s) for s in range(2)],
                "end": [struct.unpack_from(">6H", raw, 0x44 + 12 * s) for s in range(2)],
                "lossy": u32(raw, 0x5C), "takeover": (u32(raw, 0x60), u32(raw, 0x64)), "loops": u32(raw, 0x68),
                "held": u32(raw, 0x6C), "play": (u32(raw, 0x70), u32(raw, 0x74)),
                "step": u32(raw, 0x78), "start_frame": u32(raw, 0x7C)}

    def rewind_status(self):
        """Frame Decrement's counters (TestBlock +0x80)."""
        raw = self.emu.read(self.addr + 0x80, 0x18)
        return {"rewinds": u32(raw, 0), "rewound": u32(raw, 4), "target": u32(raw, 8), "ticks": u32(raw, 0xC),
                "passes": u32(raw, 0x10), "refused": u32(raw, 0x14)}

    def slot_bytes(self, side, slot, frames):
        """The slot's first `frames` packed frames, raw (8 bytes each)."""
        base = self.status()["storage"] + (side * 6 + slot) * SLOT_SIZE
        return self.emu.read(base + SLOT_FRAMES, 8 * frames)

    def set_setting(self, offset, value):
        self.emu.h.call("write_mem", addr=self.settings + offset, hex=f"{value:02x}")

    def slot_end(self, side, slot):
        """The slot's end read from the storage (the test block is updated
        before each step, so after a step it is one frame behind)."""
        base = self.status()["storage"] + (side * 6 + slot) * SLOT_SIZE
        return struct.unpack(">HH", self.emu.read(base, 4))

    def slot_frame(self, side, slot, t):
        """(buttons, x, y) stored for frame t, decoded from the storage."""
        base = self.status()["storage"] + (side * 6 + slot) * SLOT_SIZE
        b, xi, yi = struct.unpack(">HBB", self.emu.read(base + SLOT_FRAMES + 8 * t, 4))
        x = struct.unpack(">f", self.emu.read(base + SLOT_HEADER + 4 * xi, 4))[0]
        y = struct.unpack(">f", self.emu.read(base + SLOT_HEADER + SLOT_TABLE + 4 * yi, 4))[0]
        return b, x, y


# ---- per-frame traces ------------------------------------------------------------

class Tracer:
    """Both fighters, read the way scripts/cpu_tests.py reads them, plus the
    animation frame (motion module +0x40) and the input object, as raw words
    so that floats compare bit for bit."""

    def __init__(self, emu):
        self.emu = emu
        self.g = C.Game(emu)
        self.cache = {}

    def clear(self):
        self.cache.clear()
        self.g.mods.clear()

    def parts(self, player):
        en = self.g.entry(player)
        ft = en["fighters"][en["active"]]
        key = (player, ft)
        if key not in self.cache:
            e = self.emu
            enum = e.read(e.u32(e.u32(ft + 0x60) + 0xD8), 0x74)
            work = e.u32(u32(enum, 0x64) + 0x20)
            self.cache[key] = {"posture": u32(enum, 0xC), "motion": u32(enum, 0x8), "status": u32(enum, 0x70),
                               "stop": u32(enum, 0x44), "situation": u32(enum, 0x14),
                               "ints": e.u32(work + 0xC), "floats": e.u32(work + 0x14),
                               "owner": e.u32(en["owner"]),
                               "input": self.g.inputs(en)[0]}
        return self.cache[key]

    def raw(self, player):
        """Project+'s copy of the controller's buttons (instance work int 85),
        written after the fighter-input hook: the press as the fighter's
        controller saw it, whatever was written into the input object."""
        return self.emu.u32(self.parts(player)["ints"] + 85 * 4)

    def row(self, player):
        e, m = self.emu, self.parts(player)
        p = e.read(m["posture"] + 0xC, 0x38)
        inp = e.read(m["input"] + 0xC, 0xC)
        return {"x": p[0:4], "y": p[4:8], "lr": p[0x34:0x38], "status": e.u32(m["status"] + 0x34),
                "anim": e.read(m["motion"] + 0x40, 4), "hitlag": e.u32(m["stop"] + 0x10),
                "hitstun": e.u32(m["ints"] + 0x38 * 4), "air": e.u32(m["situation"] + 0x2C),
                "damage": e.read(m["owner"] + 0x24, 4), "input": inp,
                "pp": e.read(m["floats"] + 34 * 4, 16) + e.read(m["ints"] + 77 * 4, 4)}


def fl(b):
    return struct.unpack(">f", b)[0]


def show(r):
    i = r["input"]
    return (f"x {fl(r['x']):.3f} y {fl(r['y']):.3f} {sname(r['status'])} anim {fl(r['anim']):.1f} "
            f"lag {r['hitlag']} stun {r['hitstun']} dmg {fl(r['damage']):.1f} "
            f"in {u32(i, 0):#x} ({fl(i[4:8]):.3f},{fl(i[8:12]):.3f}) "
            f"pp {[round(fl(r['pp'][k:k + 4]), 3) for k in (0, 4, 8, 12)]} {u32(r['pp'], 16)}")


def compare(a, b, player, start=0, count=None, offset=0):
    """First frame i (from start) where a[i] and b[i + offset] differ for
    `player`, with the fields that differ; None if equal."""
    n = min(len(a), len(b) - offset) if count is None else min(count, len(a), len(b) - offset)
    for i in range(start, n):
        if a[i] is None or b[i + offset] is None:
            continue
        ra, rb = a[i][player], b[i + offset][player]
        diff = [k for k in ra if ra[k] != rb[k]]
        if diff:
            return i, diff, show(ra), show(rb)
    return None


# The game's random number generators (as nav.RNG_SEEDS): g_mtRand's and the
# second mtRand's seed words and the C library's rand state.
RNG = [(0x805A00B8 + 4, 4), (0x805A0420 + 4, 4), (0x8059FF58, 8)]

# The game's main thread: the SDK's DefaultThread (.bss 0x804DD558 in the
# Brawl decompilation's symbols for this disc), state u16 at OSThread +0x2C8,
# 4 = sleeping. A frame advance stops at the end of a video field. Between
# two passes of the main loop the main thread sleeps (it waits for the next
# retrace), so memory then holds whole game steps. A pass longer than a
# field (the one that saves or loads a savestate copies megabytes) is cut by
# the field: the main thread is still running, and memory holds half a pass
# (the test block or the frame counter moved, the fighters not yet stepped).
# Where in that pass the field falls depends on the emulator's timing of
# the game's code: it changed between Brawl Online 0.2.1 and 0.2.3
# (measured), and the work the game does around the pass moves it too
# (with the savestate trace on, 0.2.3 reads like 0.2.1). So such reads are
# left out (Runner.step), and after the start state's restores the game's
# own trace covers the frame they hide (s_restore).
MAIN_THREAD_STATE, THREAD_SLEEPING = 0x804DD558 + 0x2C8, 4
# Steps a read inside a pass may take to reach the pass's end.
SETTLE_LIMIT = 10


def trace_lines(text):
    """The savestate probe's trace lines (src/main/savestate/trace.cpp: one
    per main-loop pass when the frame counter moved, written at the top of
    the pass by the game itself) in `text`, cut into runs where the frame
    counter goes back (a restore): [{frame: line}]. The fighter heaps' hash
    (h=) is left out of the line: a load does not bring back the heaps'
    free space, which blocks allocated after it start with, so that hash
    can differ on frames where every game value is equal."""
    runs, last = [], None
    for m in re.finditer(r"sst (.) f=(\d+) (.*)", text):
        f = int(m.group(2))
        if m.group(1) != " ":
            continue
        if last is None or f <= last:
            runs.append({})
        runs[-1].setdefault(f, re.sub(r" h=[0-9a-f]+", "", m.group(3).strip()))
        last = f
    return runs


class Quick:
    """The savestate feature's test block: +5 asks for a quick save (1) or
    load (2) at the next top of the main loop (src/main/savestate/trace.h)."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None

    def find(self):
        found = re.findall(r"savestate: test block at ([0-9a-f]{8})", self.emu.log_text())
        if not found:
            raise RecError("no savestate test block line in the log")
        self.addr = int(found[-1], 16)

    def request(self, what):
        self.emu.h.call("write_mem", addr=self.addr + 5, hex="01" if what == "save" else "02")

    def trace(self, on):
        """The per-pass trace lines in the log (+4)."""
        self.emu.h.call("write_mem", addr=self.addr + 4, hex="01" if on else "00")


class Runner:
    def rng(self):
        return [self.emu.read(a, n) for a, n in RNG]

    def __init__(self, emu, hmn="mario", cpu="fox", stage="final_destination"):
        self.emu = emu
        self.hmn = hmn
        self.cpu = cpu
        self.stage = stage
        self.rec = Rec(emu)
        self.quick = Quick(emu)
        self.drv = C.Script(emu)
        self.tr = Tracer(emu)
        self.recordings = {}
        self.rows = {}
        # Reads Runner.step left out because the game was inside a pass
        # (their frame of the run as the half-done pass showed it).
        self.inside = []
        # snap() with its reads in one batch.
        self.snaps = emu.watch(lambda: snap(self))
        self._rec_watch = emu.watch(self.rec.status)

    def exact(self):
        return bool(self.rec.status()["flags"] & FLAG_EXACT)

    def refresh(self):
        self.quick.find()
        self.rec.find()
        self.drv.find()
        self.tr.clear()

    def row(self, i, players):
        """What step() reads after frame i: the test block, the random
        state, the match's frame counter, P1's raw pad and the fighters."""
        row = {"i": i, "rec": self.rec.status(), "rng": b"".join(self.rng()), "frame": nav.game_frame(self.emu),
               "raw": self.tr.raw(P1),
               "settled": struct.unpack(">H", self.emu.read(MAIN_THREAD_STATE, 2))[0] == THREAD_SLEEPING}
        for p in players:
            row[p] = self.tr.row(p)
        return row

    def step(self, frames, each=None, players=(P1, CPU)):
        """Advance one frame at a time (emulation paused); one row per frame:
        both fighters and the test block, read after the frame (each frame
        and its reads go to the harness as one batch, Emu.trace). each(i,
        rows) runs before frame i. A read taken while the game was inside a
        pass (see MAIN_THREAD_STATE) is left out of the rows; if the last
        one was, the game is stepped on until the pass has ended, so the
        last row is always a whole one."""
        emu = self.emu
        emu.h.call("pause")
        if players not in self.rows:
            self.rows[players] = emu.watch(lambda i: self.row(i, players))
        rows = emu.trace(frames, self.rows[players], each)
        i = frames
        while rows and not rows[-1]["settled"] and i < frames + SETTLE_LIMIT:
            rows.append(emu.step(self.rows[players], i))
            i += 1
        if rows and not rows[-1]["settled"]:
            raise RecError(f"the game was still inside a pass {SETTLE_LIMIT} frames after the last step")
        kept = [x for x in rows if x["settled"]]
        self.inside += [t_of(x) for x in rows if not x["settled"]]
        return kept

    def settle(self):
        """Pause on a frame boundary: the first advance after pausing a
        running emulator may only finish the frame in progress, whose
        game-pads pass is over."""
        self.emu.h.call("pause")
        self.emu.h.call("frame_advance", n=1)

    def request(self, name):
        """Queue a request for the next advance (carried out in its game-pads
        pass, before that frame's step)."""
        self.settle()
        self.rec.request(name)

    def run_request(self, name):
        """Carry out one request: usually one frame. A pass that outlasts
        its video field (a fighter's files loading, a savestate) holds no
        game-pads pass in that frame, so the request can wait a few."""
        before = self.rec.status()["result"]
        self.request(name)
        for _ in range(SETTLE_LIMIT):
            st = self.emu.step(self._rec_watch)
            if st["request"] == 0:
                break
        if st["result"] != before + 1:
            raise RecError(f"request {name} not carried out ({st})")
        return st

    def pad_each(self, sequence):
        """An each() that presents the pad sequence on port 1, frame by frame."""
        def each(i, rows):
            for a, b, buttons, stick, cstick, rr in sequence:
                if a <= i < b:
                    self.emu.hold(0, buttons=buttons, stick=stick, cstick=cstick, r=rr)
                    return
            self.emu.neutral(0)
        return each


class Result(C.Result):
    pass


def log_since(emu, pos):
    return emu.log_text()[pos:]


# ---- scenarios -------------------------------------------------------------------

def s_refuse(r):
    res = Result("refusals")
    emu, rec = r.emu, r.rec
    pos = len(emu.log_text())
    rec.set(hmnMode=HMN_MODE["Record"], hmnSlot=1)
    r.run_request("hmn-mode")
    rows = r.step(30)
    st = rows[-1]["rec"]
    log = log_since(emu, pos)
    res.row("HMN Record without saved positions", "refused, nothing stored",
            f"log {'refused' if 'record: HMN mode 2 refused: no saved positions' in log else 'none'}, "
            f"slot 1 end {st['end'][0][0]}, saved {st['flags'] & FLAG_SAVED}",
            "record: HMN mode 2 refused: no saved positions" in log and st["end"][0][0] == 0
            and not st["flags"] & FLAG_SAVED)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


def s_restore(r):
    """Save Positions takes a savestate (the start state is exact); a restore
    from the air, a run and a shield gives back the frames that followed the
    save, both fighters and the random state, frame for frame."""
    res = Result("start state")
    emu, rec = r.emu, r.rec
    r.drv.stop()
    # The game's own trace, one line at the top of every pass: it also has
    # the frames a read from the harness cannot see whole (the first after
    # a save or a restore, see MAIN_THREAD_STATE).
    r.quick.trace(True)
    pos = len(emu.log_text())
    before_restores = rec.status()["restores"]
    r.request("save")
    base = r.step(14)
    st = base[-1]["rec"]
    start = st["start_frame"]
    base = from_step0(base)
    log = log_since(emu, pos)
    base_trace = (trace_lines(log) or [{}])[-1]
    res.row("Save Positions takes a savestate", "saved, exact, no restore",
            f"flags {st['flags']:#x}, restores +{st['restores'] - before_restores}, "
            f"{'logged' if 'record: positions saved (savestate)' in log else 'no log line'}",
            st["flags"] & FLAG_SAVED and st["flags"] & FLAG_EXACT and st["restores"] == before_restores
            and "record: positions saved (savestate)" in log)
    states = {"air": [(JUMP, 0.0, 0.0, 1), (0, 0.4, 0.0, 12)], "run": [(0, 1.0, 0.0, 25)],
              "shield": [(GUARD, 0.0, 0.0, 30)]}
    for name, steps in states.items():
        r.drv.play(P1, steps)
        mid = r.step(sum(s[3] for s in steps) + 2)[-1][P1]
        r.drv.stop()
        before = rec.status()["restores"]
        pos = len(emu.log_text())
        r.request("restore")
        rows = since_restore(r.step(14), before)
        trace = (trace_lines(log_since(emu, pos)) or [{}])[-1]
        both = [i for i in range(min(10, len(rows), len(base))) if rows[i] is not None and base[i] is not None]
        bad = [i for i in both if rows[i][P1] != base[i][P1] or rows[i][CPU] != base[i][CPU]
               or rows[i]["rng"] != base[i]["rng"] or rows[i]["frame"] != base[i]["frame"]]
        bad += [] if len(both) >= 8 else ["fewer than 8 frames read"]
        for i in [k for k in bad if isinstance(k, int)][:2]:
            res.note(f"{name} row {i}: frame {rows[i]['frame']} vs {base[i]['frame']}, t {rows[i]['rec']['t'][0]} vs "
                     f"{base[i]['rec']['t'][0]}, P1 {[k for k in base[i][P1] if base[i][P1][k] != rows[i][P1][k]]}, "
                     f"CPU {[k for k in base[i][CPU] if base[i][CPU][k] != rows[i][CPU][k]]}, "
                     f"rng {base[i]['rng'] != rows[i]['rng']}")
        res.row(f"restore from {name} ({sname(mid['status'])})", "the 10 frames after the save, exactly",
                f"{len(both)} frames compared, {len(bad)} differ {bad[:3]}; {show(last(rows)[P1])}", not bad)
        # The game's trace: the line at the top of the pass with frame
        # counter start + 1 + t is the state after step t.
        ts = [t for t in range(10) if start + 1 + t in trace and start + 1 + t in base_trace]
        differ = [t for t in ts if trace[start + 1 + t] != base_trace[start + 1 + t]]
        if differ:
            res.note(f"{name} trace t {differ[0]}: {trace[start + 1 + differ[0]]} / save "
                     f"{base_trace[start + 1 + differ[0]]}")
        res.row(f"restore from {name}: the game's trace", "steps 0 to 9 as after the save, step 0 included",
                f"steps {ts[:1]}..{ts[-1:]} ({len(ts)}) compared, differ {differ[:3]}",
                0 in ts and len(ts) >= 8 and not differ)
    r.quick.trace(False)
    return res


def record_hmn(r, slot=1, sequence=SEQUENCE, frames=None, rerecord=False):
    """HMN Record into `slot` from a restore with P1 scripted; returns the rows."""
    rec = r.rec
    frames = frames or sum(s[3] for s in sequence) + 10
    rec.set(hmnMode=HMN_MODE["Record"], hmnSlot=slot, cpuMode=0)
    r.run_request("hmn-mode")
    r.drv.stop()
    before = rec.status()["restores"]
    r.request("restore")

    def each(i, rows):
        if i == 0:
            r.drv.play(P1, sequence)
    rows = since_restore(r.step(frames, each), before)
    r.drv.stop()
    return rows


def t_of(row):
    """The row's frame of the run: game steps since the run started, from
    the match's frame counter (which the savestate restores) and the
    counter's value at the start the plugin wrote down. One advance of the
    harness is not always one game step around a savestate's stall."""
    return row["frame"] - row["rec"]["start_frame"] - 1


def dense(rows):
    """Rows indexed by the run's frame: rows[t] is the state after step t
    (None for a frame no row was read at)."""
    out = []
    for x in rows:
        t = t_of(x)
        if t < 0:
            continue
        while len(out) < t:
            out.append(None)
        if len(out) == t:
            out.append(x)
    return out


def from_step0(rows):
    """The run that started last in `rows` (a save), by frame."""
    start = rows[-1]["rec"]["start_frame"]
    return dense([x for x in rows if x["rec"]["start_frame"] == start])


def since_restore(rows, before):
    """The run started by the first restore after the count `before`, by frame."""
    at = next((i for i, x in enumerate(rows) if x["rec"]["restores"] > before), None)
    if at is None:
        # A restore without saved positions does nothing (no storage for
        # the recording, for one): the flags say whether they were saved.
        st = rows[-1]["rec"] if rows else None
        raise RecError("no restore in the run" + (f" (restores {st['restores']}, flags {st['flags']:#x}: positions "
                                                  f"{'saved' if st['flags'] & FLAG_SAVED else 'not saved'})"
                                                  if st else ""))
    start = rows[at]["rec"]["start_frame"]
    return dense([x for x in rows[at:] if x["rec"]["start_frame"] == start])


def play_hmn(r, slot, frames, setup=None, each=None):
    """HMN Playback of `slot` from a restore."""
    rec = r.rec
    rec.set(hmnMode=HMN_MODE["Playback"], hmnSlot=slot, cpuMode=0, loop=0, autoRestore=0, startPaused=0)
    if setup:
        setup()
    emu = r.emu
    emu.neutral(0)
    before = rec.status()["restores"]
    r.request("hmn-mode")
    return since_restore(r.step(frames + 1, each), before)[:frames]


def s_hmn(r):
    res = Result("HMN record and playback")
    emu, rec = r.emu, r.rec
    emu.neutral(0)
    # The CPU stands under the lab's CPU Options (AI off). With Training's
    # own AI running it, its idle motion is not reproduced: see the report.
    rec.cpu_control(True)
    rows = record_hmn(r)
    st = last(rows)["rec"]
    st["start"][0], st["end"][0] = (rec.slot_end(0, 0)[0],), (rec.slot_end(0, 0)[1],)
    r.recordings["hmn"] = rows
    statuses = sorted({sname(x[P1]["status"]) for x in rows if x is not None})
    res.note(f"recorded {len(rows)} frames; P1 statuses {statuses}")
    steps = len(rows)
    res.row("slot 1 holds the run", f"start 0, end {steps} (every step since the restore)",
            f"start {st['start'][0][0]}, end {st['end'][0][0]}, lossy {st['lossy']}",
            st["start"][0][0] == 0 and st["end"][0][0] == steps and st["lossy"] == 0)
    want = {"Dash", "Jump_Squat", "Jump", "Jump_Aerial", "Attack", "Guard", "Attack_S4_Start"}
    res.row("the sequence ran", "dash, jumps, jab, shield, forward smash", sorted(want & set(statuses)),
            want <= set(statuses))
    # Every frame of the slot is the input object's final value of that frame.
    mism = [i for i in range(0, len(rows), 7)
            if rows[i] is not None
            and rec.slot_frame(0, 0, i) != (u32(rows[i][P1]["input"], 0), fl(rows[i][P1]["input"][4:8]),
                                            fl(rows[i][P1]["input"][8:12]))]
    res.row("stored frames equal the input objects (every 7th)", "none differ", mism[:5], not mism)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    play = play_hmn(r, 1, len(rows))
    r.recordings["hmn-play"] = play
    d1 = compare(rows, play, P1)
    ok2, v2 = trace_check(rows, play, CPU, idle=not r.exact())
    split = rng_split(rows, play)
    res.row(f"P1 trace, {len(rows)} frames", "equal", d1 or "equal", d1 is None)
    res.row("CPU trace (standing, not played back)", "equal" if r.exact() else "equal but idle motions", v2, ok2)
    res.row("the game's random state every frame", "equal" if r.exact() else "(lite: not restored)",
            f"first differs at {split}", split is None or not r.exact())
    return res


IDLE = {STATUS["Wait"], STATUS["Rebirth"]}


def rng_split(a, b):
    return next((i for i in range(min(len(a), len(b)))
                 if a[i] is not None and b[i] is not None and a[i]["rng"] != b[i]["rng"]), None)


def last(rows):
    return next(x for x in reversed(rows) if x is not None)


def at(rows, i):
    """rows[i], or None past the end (dense rows have None where no read was kept)."""
    return rows[i] if 0 <= i < len(rows) else None


def trace_check(a, b, player, gone_input=False, idle=True):
    """Every frame equal, except the animation frame of a fighter standing
    idle (Wait, on the respawn platform), which may differ once the game's
    random state differs between the runs: the game picks its idle motions
    at random and other parts of the game (stage, effects) draw from the same
    generators in each run differently. With gone_input, the input object is
    not compared while the fighter is Dead or in Standby (nothing reads it;
    the driver leaves it alone then). With idle False (an exact start
    state) nothing may differ. Returns (ok, verdict)."""
    split = rng_split(a, b)
    idled = []
    for i in range(min(len(a), len(b))):
        if a[i] is None or b[i] is None:
            continue
        ra, rb = a[i][player], b[i][player]
        keys = [k for k in ra if ra[k] != rb[k]
                and not (gone_input and k == "input" and ra["status"] in GONE and rb["status"] in GONE)]
        if not keys:
            continue
        if (idle and keys == ["anim"] and ra["status"] == rb["status"] and ra["status"] in IDLE
                and split is not None and split <= i):
            idled.append(i)
            continue
        return False, f"frame {i}: {keys}: {show(ra)} / {show(rb)}; random state differs from {split}"
    what = f"idle motion differs on {len(idled)} frames from {idled[0]}" if idled else "equal"
    return True, f"{what}; random state differs from row {split}"


def s_cpu(r):
    res = Result("CPU control, record and playback")
    emu, rec = r.emu, r.rec
    rec.set(hmnMode=0, loop=0, autoRestore=0)
    # CPU Control = Training: refused.
    pos = len(emu.log_text())
    rec.cpu_control(False)
    rec.set(cpuMode=CPU_MODE["Control"])
    r.run_request("cpu-mode")
    rows = r.step(20, r.pad_each([(0, 20, [], LEFT, NEUTRAL, 0)]))
    emu.neutral(0)
    refused = "record: CPU mode 1 refused: CPU Options > CPU Control is not Lab" in log_since(emu, pos)
    moved = rows[-1][CPU]["x"] != rows[0][CPU]["x"]
    res.row("CPU Control with CPU Control = Training", "refused, the CPU does not follow the pad",
            f"refused {refused}, CPU moved {moved}", refused and not moved)
    # Control: the pad drives the CPU, P1 stands.
    rec.cpu_control(True)
    r.request("restore")
    r.step(3)
    rows = r.step(40, r.pad_each([(0, 20, [], LEFT, NEUTRAL, 0), (25, 26, ["A"], NEUTRAL, NEUTRAL, 0)]))
    emu.neutral(0)
    p1s = {sname(x[P1]["status"]) for x in rows}
    cpus = [sname(x[CPU]["status"]) for x in rows]
    dx = fl(rows[-1][CPU]["x"]) - fl(rows[0][CPU]["x"])
    # The A comes 5 frames after the stick is let go, while Fox still brakes
    # from the run (status 0x5), so it gives a dash attack (Attack_Dash,
    # 0x26); later it would be a jab (Attack). Either shows the pad's A.
    attacked = {C.STATUS["Attack"], C.STATUS["Attack_Dash"]} & {x[CPU]["status"] for x in rows}
    res.row("Control: the CPU runs left and attacks with the pad, P1 stays in Wait",
            "x -10 or less, Attack or Attack_Dash / Wait",
            f"CPU x {dx:+.1f}, {sorted(set(cpus))}, P1 {sorted(p1s)}",
            dx < -10 and bool(attacked) and p1s == {"Wait"}
            and rows[-1][P1]["x"] == rows[0][P1]["x"])
    # Record under Control.
    rec.set(cpuMode=CPU_MODE["Record"], cpuSlot=1)
    r.run_request("cpu-mode")
    before = rec.status()["restores"]
    r.request("restore")
    rows = since_restore(r.step(PAD_FRAMES + 1, r.pad_each(PAD_SEQUENCE)), before)
    emu.neutral(0)
    r.recordings["cpu"] = rows
    res.note(f"CPU statuses while recorded: {sorted({sname(x[CPU]['status']) for x in rows if x is not None})}")
    start, end = rec.slot_end(1, 0)
    steps = len(rows)
    res.row("CPU slot 1", f"start 0, end {steps}", f"start {start}, end {end}", start == 0 and end == steps)
    rec.set(cpuMode=CPU_MODE["Playback"], cpuCounter=0)
    before = rec.status()["restores"]
    r.request("cpu-mode")
    play = since_restore(r.step(PAD_FRAMES + 1), before)
    r.recordings["cpu-play"] = play
    d1 = compare(rows, play, CPU)
    ok2, v2 = trace_check(rows, play, P1, idle=not r.exact())
    res.row(f"CPU trace with Project+'s analog variables, {PAD_FRAMES} frames", "equal", d1 or "equal",
            d1 is None)
    res.row("P1 trace (held neutral, then neutral)", "equal" if r.exact() else "equal but idle motions", v2, ok2)
    light = [x["i"] for x in rows if x is not None and fl(x[CPU]["pp"][12:16]) > 0.0
             and not u32(x[CPU]["input"], 0) & GUARD]
    res.note(f"rows with an analog R and no guard bit (light press): {light[:3]}..{light[-1:]} ({len(light)})")
    # A KO and the respawn.
    rec.set(cpuMode=CPU_MODE["Record"], cpuSlot=2)
    r.run_request("cpu-mode")
    before = rec.status()["restores"]
    r.request("restore")
    rows = since_restore(r.step(KO_FRAMES + 1, r.pad_each(KO_SEQUENCE)), before)[:KO_FRAMES]
    emu.neutral(0)
    rec.set(cpuMode=CPU_MODE["Playback"], cpuSlot=2)
    before = rec.status()["restores"]
    r.request("cpu-mode")
    play = since_restore(r.step(KO_FRAMES + 1), before)[:KO_FRAMES]
    gone = [i for i, x in enumerate(rows) if x is not None and x[CPU]["status"] in GONE]
    ok, v = trace_check(rows, play, CPU, gone_input=True, idle=not r.exact())
    res.row("KO under Control, respawn: CPU trace", "equal but the unread input while gone",
            f"gone rows {gone[:1]}..{gone[-1:]}; {v}", gone and ok)
    rec.set(cpuMode=0)
    r.run_request("cpu-mode")
    return res


def s_loop(r):
    res = Result("loop")
    rec, rows = r.rec, r.recordings["hmn"]
    end = rec.slot_end(0, 0)[1]
    play = play_hmn(r, 1, 2 * end + 6, setup=lambda: rec.set(loop=1))
    loops = last(play)["rec"]["loops"]
    bad = [i for i, x in enumerate(play)
           if x is not None and rows[i % end] is not None and x[P1]["input"] != rows[i % end][P1]["input"]]
    t_at = [x["rec"]["t"][0] for x in play[end - 1:end + 2] if x is not None]
    res.row(f"two loops of {end} frames: P1's input each frame", "equal to the recording",
            f"{len(bad)} differ (first {bad[:3]}), loops {loops}, t around the end {t_at}",
            not bad and loops >= 2)
    rec.set(loop=0)
    return res


def s_autorestore(r):
    res = Result("auto restore")
    emu, rec, first = r.emu, r.rec, r.recordings["hmn-play"]
    end = rec.slot_end(0, 0)[1]
    rec.set(hmnMode=HMN_MODE["Playback"], hmnSlot=1, cpuMode=0, loop=0, autoRestore=1, startPaused=0)
    emu.neutral(0)
    before = rec.status()["restores"]
    r.request("hmn-mode")
    raw = r.step(end + 30 + 120)
    run1 = since_restore(raw, before)
    run2 = since_restore(raw, before + 1)
    # The restore came after the last frame of the first run (kAutoRestoreDelay = 24).
    res.row("restored 24 frames after the playback ends", f"{end + 23} frames played", len(run1),
            len(run1) == end + 23)
    d = compare(first, run2, P1, count=120)
    res.row("after it, the P1 trace equals the first playback's (120 frames)", "equal", d or "equal", d is None)
    rec.set(autoRestore=0)
    return res


def s_takeover(r):
    res = Result("takeover")
    emu, rec, rows = r.emu, r.rec, r.recordings["hmn"]
    k = 30  # inside the walk

    def press(i, _rows):
        if i == k:
            emu.hold(0, ["A"])
        elif i == k + 3:
            emu.neutral(0)
    play = play_hmn(r, 1, k + 30, setup=lambda: rec.set(takeover=TAKEOVER["HMN"]), each=press)
    emu.neutral(0)
    tk = last(play)["rec"]["takeover"][0]
    arrived = next((i for i in range(len(play)) if play[i] is not None and play[i]["raw"] & 0x100), None)
    first_live = next((i for i in range(len(play)) if play[i] is not None and u32(play[i][P1]["input"], 0) & A),
                      None)
    # Frames read in one run and not the other (see MAIN_THREAD_STATE) are
    # not compared.
    same_before = tk < len(play) and all(at(play, i) is None or at(rows, i) is None
                                         or play[i][P1]["input"] == rows[i][P1]["input"] for i in range(tk))
    no_a = at(rows, tk) is not None and u32(rows[tk][P1]["input"], 0) & A == 0
    res.row("HMN: taken over on the frame the press reaches P1's controller",
            "takeover = press seen (raw pad) = first A in the input",
            f"takeover {tk}, press seen {arrived}, first A {first_live}, recorded before: {same_before}, "
            f"recording has no A there: {no_a}", tk == arrived == first_live and same_before and no_a)
    if tk < len(play) and play[tk] is not None:
        res.note(f"P1 at the takeover: {show(play[tk][P1])}")
    # CPU takeover.
    crows = r.recordings["cpu"]
    rec.set(hmnMode=0, cpuMode=CPU_MODE["Playback"], cpuSlot=1, takeover=TAKEOVER["CPU"], cpuCounter=0)
    r.run_request("hmn-mode")
    before = rec.status()["restores"]
    r.request("cpu-mode")
    k = 40
    play = since_restore(r.step(k + 31, press), before)
    emu.neutral(0)
    tk = last(play)["rec"]["takeover"][1]
    first_live = next((i for i in range(k, len(play)) if play[i] is not None and u32(play[i][CPU]["input"], 0) & A
                       and at(crows, i) is not None and u32(crows[i][CPU]["input"], 0) & A == 0), None)
    p1a = [i for i in range(len(play)) if play[i] is not None and u32(play[i][P1]["input"], 0) & A]
    same_before = all(at(play, i) is None or at(crows, i) is None or play[i][CPU]["input"] == crows[i][CPU]["input"]
                      for i in range(min(tk, len(play))))
    arrived = next((i for i in range(len(play)) if play[i] is not None and play[i]["raw"] & 0x100), None)
    res.row("CPU: taken over on the frame the press arrives; P1 neutral", "takeover = press seen = live A, P1 no A",
            f"takeover {tk}, press seen {arrived}, first live A on the CPU {first_live}, P1 A rows {p1a[:3]}, "
            f"recorded before: {same_before}", tk == arrived == first_live and not p1a and same_before)
    rec.set(cpuMode=0, takeover=TAKEOVER["HMN"])
    r.run_request("cpu-mode")
    return res


def s_rerecord(r):
    res = Result("re-record")
    emu, rec, rows = r.emu, r.rec, r.recordings["hmn"]
    rec.set(manageSide=0, manageSlot=0, manageCopyTo=1)
    r.run_request("copy")  # slot 2 = slot 1
    k = 40

    def each(i, _rows):
        if k <= i < k + 20:
            emu.hold(0, stick=LEFT)
        else:
            emu.neutral(0)
    rec.set(hmnMode=HMN_MODE["Re-Record"], hmnSlot=2, takeover=TAKEOVER["None"])
    before = rec.status()["restores"]
    r.request("hmn-mode")
    rr_all = since_restore(r.step(k + 61, each), before)
    rr = rr_all[:k + 60]
    emu.neutral(0)
    tk = last(rr)["rec"]["takeover"][0]
    end = rec.slot_end(0, 1)[1]
    res.row("Re-Record forces the takeover on HMN; slot 2 ends where the run stopped",
            f"takeover in {k}..{k + 4}, end {len(rr_all)}", f"takeover {tk}, end {end}",
            k <= tk <= k + 4 and end == len(rr_all))
    play = play_hmn(r, 2, k + 60)
    d_before = compare(rows, play, P1, count=tk)
    bad = [i for i in range(tk, min(k + 60, len(play), len(rr)))
           if play[i] is not None and rr[i] is not None and play[i][P1]["input"] != rr[i][P1]["input"]]
    res.row("playback of slot 2: the original before the takeover, the new inputs after",
            "equal / equal", f"{d_before or 'equal'} / {len(bad)} differ {bad[:3]}", d_before is None and not bad)
    rec.set(hmnMode=0, takeover=TAKEOVER["HMN"])
    r.run_request("hmn-mode")
    return res


def s_paused(r):
    res = Result("start paused")
    emu, rec = r.emu, r.rec
    rec.set(hmnMode=0, cpuMode=CPU_MODE["Playback"], cpuSlot=1, cpuCounter=0, startPaused=0,
            takeover=TAKEOVER["None"])
    emu.neutral(0)
    before = rec.status()["restores"]
    r.request("cpu-mode")
    crows = since_restore(r.step(121), before)
    rec.set(startPaused=1)
    before = rec.status()["restores"]
    r.request("restore")
    held = r.step(40)
    held = held[next(i for i, x in enumerate(held) if x["rec"]["restores"] > before):]
    t = [x["rec"]["t"][1] for x in held]
    anim = {x[CPU]["anim"] for x in held[1:]}
    res.row("held on frame 0 without input", "t stays 0, CPU frozen", f"t {sorted(set(t))}, "
            f"CPU animation frames {len(anim)}, held passes {held[-1]['rec']['held']}",
            set(t) == {0} and len(anim) == 1 and held[-1]["rec"]["flags"] & FLAG_HOLDING)

    def each(i, _rows):
        if i == 0:
            emu.hold(0, ["X"])
        elif i == 3:
            emu.neutral(0)
    go = r.step(120, each)
    # The match's frame counter goes on while the game is held, so here the
    # rows are placed by the plugin's own step index (the frame of the last
    # step the player's fighter ran), from the first step after the release.
    steps0 = held[-1]["rec"]["steps"]
    start = next((i for i, x in enumerate(go) if x["rec"]["steps"] > steps0), None)
    run = []
    for x in go[start:] if start is not None else []:
        t = x["rec"]["step"]
        while len(run) < t:
            run.append(None)
        if len(run) == t:
            run.append(x)
    d = compare(crows, run, CPU, count=100) if start is not None else "never"
    res.row("released by the first input; the CPU then plays from frame 0", "equal for 100 frames",
            f"released at row {start}, {d or 'equal'}", start is not None and d is None)
    rec.set(cpuMode=0, startPaused=0, takeover=TAKEOVER["HMN"])
    r.run_request("cpu-mode")
    return res


def s_slots(r):
    res = Result("six slots")
    emu, rec = r.emu, r.rec
    xs = [0.25, -0.25, 0.5, -0.5, 0.75, -0.75]
    for k in range(6):
        record_hmn(r, slot=k + 1, sequence=[(0, xs[k], 0.0, 10 + 3 * k), (0, 0.0, 0.0, 5)], frames=15 + 3 * k)
    ends = [rec.slot_end(0, k)[1] for k in range(6)]
    firsts = [rec.slot_frame(0, k, 2)[1] for k in range(6)]
    res.row("six recordings, six lengths and sticks", "ends 15..30 by 3, x as recorded",
            f"ends {ends}, x {firsts}", ends == [15 + 3 * k for k in range(6)] and firsts == xs)
    play = play_hmn(r, 4, 12)
    xin = fl(play[5][P1]["input"][4:8]) if play[5] is not None else None
    res.row("playing slot 4 plays slot 4", xs[3], xin, xin == xs[3])
    rec.set(manageSide=0, manageSlot=2, manageCopyTo=5)
    r.run_request("copy")
    e6 = rec.slot_end(0, 5)[1]
    res.row("Copy Slot 3 to Slot 6", f"end {ends[2]}, x {xs[2]}", f"end {e6}, x {rec.slot_frame(0, 5, 2)[1]}",
            e6 == ends[2] and rec.slot_frame(0, 5, 2)[1] == xs[2])
    rec.set(manageSlot=5)
    r.run_request("delete-slot")
    e6 = rec.slot_end(0, 5)[1]
    res.row("Delete Slot 6", "end 0", e6, e6 == 0)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


def s_random(r):
    """The chance rows after the six recordings (one deleted again), then a
    Random slot weighted to slot 4; recording never uses Random."""
    res = Result("random slot")
    emu, rec = r.emu, r.rec
    chances = [rec.emu.u8(rec.settings + SET_REC + REC["hmnChance"] + k) for k in range(6)]
    res.row("HMN chances after recording slots 1-6 and deleting slot 6", "5 shares summing to 100, slot 6 0",
            chances, sum(chances) == 100 and chances[5] == 0 and all(c > 0 for c in chances[:5]))
    rec.set(hmnChance=[0, 0, 0, 100, 0, 0], hmnSlot=0)
    play = play_hmn(r, 0, 4)
    res.row("Random with all the weight on slot 4", "slot 4 played", last(play)["rec"]["play"][0] + 1,
            last(play)["rec"]["play"][0] == 3)
    # A quick load (the D-pad's) draws a Random slot again, on each quick load; with no Random slot it is not drawn again.
    quick_op(r, "save", 3)
    rec.set(hmnChance=[0, 100, 0, 0, 0, 0])
    drawn = last(quick_op(r, "load", 3))["rec"]["play"][0] + 1
    rec.set(hmnChance=[0, 0, 0, 100, 0, 0], hmnSlot=2)
    kept = last(quick_op(r, "load", 3))["rec"]["play"][0] + 1
    res.row("quick load with Random, the weight moved to slot 2 after the save; again with slot 2 chosen and "
            "the weight on slot 4", "slot 2, then slot 2", f"slot {drawn}, then slot {kept}", drawn == 2 and kept == 2)
    rec.set(hmnMode=HMN_MODE["Record"], hmnSlot=0)
    r.run_request("hmn-mode")
    res.row("Record with the slot on Random", "slot set to 1", rec.get("hmnSlot"), rec.get("hmnSlot") == 1)
    rec.set(hmnMode=0, hmnSlot=1)
    r.run_request("hmn-mode")
    return res


def s_prune(r):
    """Prune Positions 6 frames into a playback of slot 3 (16 frames of x 0.5,
    then 5 neutral); Re-Save Positions keeps the slots."""
    res = Result("prune and re-save")
    emu, rec = r.emu, r.rec
    play_hmn(r, 3, 6)
    r.run_request("prune")
    start, end = rec.slot_end(0, 2)
    d = 21 - end
    xs = [rec.slot_frame(0, 2, k)[1] for k in (0, 15 - d, 16 - d)]
    res.row("slot 3 after Prune", "starts at 0, the first frames dropped, x 0.5 until old frame 15",
            f"start {start}, end {end} (dropped {d}), x at 0/{15 - d}/{16 - d}: {xs}",
            start == 0 and 5 <= d <= 8 and xs == [0.5, 0.5, 0.0])
    ends = [rec.slot_end(0, k)[1] for k in range(5)]
    exact = r.exact()
    play_hmn(r, 2, 5)
    pos = len(emu.log_text())
    r.run_request("resave")
    rows = from_step0(r.step(5))
    after = [rec.slot_end(0, k)[1] for k in range(5)]
    saved = "record: positions saved again (savestate)" in log_since(emu, pos)
    # The new start: Restore Positions must bring back the frames that
    # followed the re-save.
    before = rec.status()["restores"]
    r.request("restore")
    again = since_restore(r.step(5), before)
    pairs = [(again[i], rows[i]) for i in range(min(len(again), len(rows)))
             if again[i] is not None and rows[i] is not None]
    same = len(pairs) >= 2 and all(a[P1] == b[P1] and a["rng"] == b["rng"] for a, b in pairs)
    res.row("Re-Save Positions: a new savestate, the slots as they were; Restore returns to it",
            "slots kept, savestate, restore exact", f"ends {ends} -> {after}, savestate {saved}, restore exact {same}",
            after == ends and (saved or not exact) and (same or not exact))
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


MIRROR_SEQUENCE = [(0, 0.6, 0.0, 20), (0, 0.0, 0.0, 6), (JUMP, 0.0, 0.0, 1), (0, -0.4, 0.0, 40),
                   (A, 0.0, 0.0, 1), (0, 0.0, 0.0, 30)]


def s_mirror(r):
    """Mirrored Playback: P1 mirrored in x frame for frame (Final Destination
    is symmetrical about x = 0)."""
    res = Result("mirrored playback")
    rec = r.rec
    # The mirrored run starts from the fighters placed mirrored, in Wait:
    # the positions are saved again with both standing (a savestate cannot
    # be mirrored, the placement only covers position, facing and Wait).
    rec.set(hmnMode=0, cpuMode=0)
    r.run_request("hmn-mode")
    r.request("restore")
    r.step(30)
    r.run_request("resave")
    rows = record_hmn(r, slot=5, sequence=MIRROR_SEQUENCE)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    play = play_hmn(r, 5, len(rows), setup=lambda: rec.set(mirrored=1))
    # The recording starts from the exact state, the mirrored run from the
    # fighters placed mirrored in Wait with no velocity (a savestate cannot
    # be mirrored): positions drift apart (measured and reported), statuses
    # and facing must not.
    bad, worst, anims, n = None, 0.0, 0, 0
    for i in range(min(len(rows), len(play))):
        if rows[i] is None or play[i] is None:
            continue
        n += 1
        a, b = rows[i][P1], play[i][P1]
        dx = abs(fl(a["x"]) + fl(b["x"]))
        worst = max(worst, dx)
        anims += a["anim"] != b["anim"]
        if a["status"] != b["status"] or fl(a["lr"]) != -fl(b["lr"]):
            bad = (i, show(a), show(b))
            break
    res.row(f"P1 mirrored, {n} frames", "status equal, facing negated (x: see the difference)",
            f"{bad or 'mirrored'}; largest x difference {worst:.4f}, animation frame differs on {anims}",
            bad is None and last(play)["rec"]["flags"] & 0x40)
    rec.set(mirrored=0, hmnMode=0)
    r.run_request("hmn-mode")
    return res


def s_counter(r):
    """CPU Counter After Playback Ends: the CPU goes back to the CPU Options
    when its recording ends."""
    res = Result("CPU counter")
    emu, rec = r.emu, r.rec
    end = rec.slot_end(1, 0)[1]
    rec.cpu_control(True)
    rec.set(hmnMode=0, cpuMode=CPU_MODE["Playback"], cpuSlot=1, cpuCounter=1, takeover=TAKEOVER["HMN"])
    pos = len(emu.log_text())
    r.request("cpu-mode")
    r.step(end + 6)
    log = log_since(emu, pos)
    ended = re.search(r"record: CPU playback ended at frame (\d+)", log)
    back = re.search(r"cpu: take slot 1 \(P2 entry \w+, cpu\), flags 3", log[ended.end():]) if ended else None
    res.row("the CPU leaves the recording at its end and the CPU Options take it", f"ended at {end}, taken",
            f"ended at {ended.group(1) if ended else None}, taken {bool(back)}",
            ended is not None and int(ended.group(1)) == end and back is not None)
    rec.set(cpuMode=0)
    r.run_request("cpu-mode")
    return res


COUNTER_CPU_HIT = 2  # Recording > CPU Counter: On CPU Hit
STAND_SLOT, STAND_FRAMES = 4, 400  # a CPU recording of standing still


def s_oncpuhit(r):
    """CPU Counter On CPU Hit (CPU counter on CPU hit):
    the CPU's recording plays (the CPU standing still), P1 walks up through
    its pad and jabs it; the playback ends on the hit, the hit is counted,
    and the CPU Options counter (Counter Action (Ground) Spotdodge, delay 0)
    on the CPU's first actionable frame: the same number of frames after
    the hit as with no recording playing. Measured before the fix: the
    playback ended and the CPU only stood."""
    res = Result("CPU counter On CPU Hit")
    emu, rec = r.emu, r.rec
    opt = C.Options(emu)
    opt.find()
    rec.cpu_control(True)
    opt.set(behavior=0, counterGround=C.GROUND["Spotdodge"], counterAir=0, counterDelay=0, tdi=C.TDI["None"],
            sdiAmount=0)
    rec.set(hmnMode=0, cpuMode=CPU_MODE["Record"], cpuSlot=STAND_SLOT, loop=0, autoRestore=0,
            takeover=TAKEOVER["None"], cpuCounter=COUNTER_CPU_HIT)
    r.run_request("cpu-mode")
    r.request("restore")
    r.step(STAND_FRAMES, r.pad_each([]))
    rec.set(cpuMode=0)
    r.run_request("cpu-mode")
    end = rec.slot_end(1, STAND_SLOT - 1)[1]

    def run(playback):
        """P1 walks to the CPU and jabs it; (frames from the hit to the
        first Escape frame, the rows' hit index, log)."""
        if playback:
            rec.set(cpuMode=CPU_MODE["Playback"], cpuSlot=STAND_SLOT)
            pos = len(emu.log_text())
            r.request("cpu-mode")
        else:
            pos = len(emu.log_text())
            r.request("restore")
        s = {"jab": None}

        def each(i, rows):
            if s["jab"] is None and rows:
                gap = fl(rows[-1][CPU]["x"]) - fl(rows[-1][P1]["x"])
                if abs(gap) < 14.0:
                    s["jab"] = i
                else:
                    emu.hold(0, stick=RIGHT if gap > 0 else LEFT)
                    return
            if s["jab"] is not None and i < s["jab"] + 2:
                emu.hold(0, buttons=["A"])
            else:
                emu.neutral(0)
        rows = r.step(200, each)
        emu.neutral(0)
        log = log_since(emu, pos)
        hit = next((k for k in range(1, len(rows)) if rows[k][CPU]["hitlag"] > rows[k - 1][CPU]["hitlag"]), None)
        dodge = next((k for k in range(hit or 0, len(rows)) if hit is not None
                      and rows[k][CPU]["status"] == C.STATUS["Escape"]), None)
        return (dodge - hit if dodge is not None else None), hit, log

    base, base_hit, _ = run(False)
    got, hit, log = run(True)
    countered = re.search(r"record: CPU playback countered \(CPU hit\) at frame (\d+)", log)
    handed = re.search(r"cpu: P2 hit (\d+) handed over by the playback: counter", log)
    res.note(f"standing recording: {end} frames; no recording: hit at row {base_hit}, Spotdodge {base} frames "
             f"after; playback: hit at row {hit}, Spotdodge {got} frames after")
    res.row("On CPU Hit: the playback ends on the hit, the hit goes to the CPU Options",
            "countered, handed over as hit 1",
            f"countered {bool(countered)}, handed {handed.group(1) if handed else None}",
            countered is not None and handed is not None and handed.group(1) == "1")
    res.row("the counter (Spotdodge) on the first actionable frame, as with no recording",
            f"{base} frames after the hit", got, base is not None and got == base)
    rec.set(cpuMode=0, cpuCounter=0)
    r.run_request("cpu-mode")
    opt.set(counterGround=1)
    return res


PLAY_SLOT_1 = 25  # Counter Action (Ground): the first Play Slot entry


def s_playslot(r):
    """Counter Action (Ground) = Play Slot 1: after Mario's forward smash
    the CPU Options play the CPU's recording slot 1 as the counter."""
    res = Result("Play Slot counter")
    emu, rec = r.emu, r.rec
    opt = C.Options(emu)
    opt.find()
    rec.cpu_control(True)
    opt.set(behavior=0, counterGround=PLAY_SLOT_1, counterDelay=0, tdi=C.TDI["None"])
    rec.set(hmnMode=0, cpuMode=0)
    r.run_request("cpu-mode")
    r.request("restore")
    r.step(2)
    # The CPU walks up to Mario (script, AI off), then stands.
    r.drv.play(CPU, [(0, -0.6, 0.0, 300)], ai_off=True)
    near = None
    for _ in range(200):
        row = r.step(1)[-1]
        if fl(row[CPU]["x"]) - fl(row[P1]["x"]) < 22.0:
            near = row
            break
    r.drv.stop()
    r.step(20)
    pos = len(emu.log_text())
    first = rec.slot_frame(1, 0, rec.slot_end(1, 0)[0])

    def each(i, _rows):
        if i == 0:
            emu.hold(0, ["A"], stick=RIGHT)
        elif i == 4:
            emu.neutral(0)
    rows = r.step(160, each)
    log = log_since(emu, pos)
    countered = re.search(r"cpu: P2 counter list 0 action %d" % PLAY_SLOT_1, log)
    played = re.search(r"cpu: slot 1 played (\d+) frames", log)
    start = next((i for i, x in enumerate(rows) if countered and x[CPU]["input"][4:8] == struct.pack(">f", first[1])
                  and u32(x[CPU]["input"], 0) == first[0]), None)
    res.row("hit, then the counter plays CPU slot 1", "counter Play Slot 1, its first frame on the CPU",
            f"near {near is not None}, counter {bool(countered)}, first frame seen at row {start}, "
            f"played {played.group(1) if played else None} frames",
            countered is not None and start is not None)
    opt.set(counterGround=1)
    return res


def quick_op(r, what, frames):
    """A quick save or load (the D-pad's) at the next top of the main loop;
    then `frames` more frames. Returns the rows."""
    r.emu.h.call("pause")
    r.quick.request(what)
    return r.step(frames)


def s_dpad(r):
    """Quick states (D-pad right/left) with a recording loaded: a load during
    playback continues the playback from the frame the state was saved at;
    during recording, recording goes on from there (the frames after it are
    recorded again); a state saved before other positions stops both until
    a restore."""
    res = Result("quick states")
    emu, rec = r.emu, r.rec
    first = play_hmn(r, 1, 90)
    # Playback: a quick save after step ~20, a quick load 40 frames later.
    # (The test block's step is the frame of the last step the player's
    # fighter ran: a save at the next top of the main loop is at step + 1.)
    expect = t_of(last(play_hmn(r, 1, 20))) + 1
    pos = len(emu.log_text())
    quick_op(r, "save", 40)
    after = quick_op(r, "load", 30)
    log = log_since(emu, pos)
    back = re.search(r"record: quick state loaded: back to frame (\d+)", log)
    n = int(back.group(1)) if back else -1
    start = after[-1]["rec"]["start_frame"]
    rows = [x for x in after if x["rec"]["start_frame"] == start and n <= t_of(x) < n + 25
            and first[t_of(x)] is not None]
    same = len(rows) >= 20 and all(x[P1] == first[t_of(x)][P1] and x[CPU] == first[t_of(x)][CPU]
                                   and x["rng"] == first[t_of(x)]["rng"] for x in rows)
    res.row("load during playback: back to the saved frame, the playback goes on exactly",
            f"frame about {expect}, 20+ frames from it equal to a playback's (fighters and random state)",
            f"frame {n}, {len(rows)} frames compared, equal {same}", abs(n - expect) <= 1 and same)
    # Recording into slot 4.
    rec.set(hmnMode=HMN_MODE["Record"], hmnSlot=4)
    r.run_request("hmn-mode")
    r.request("restore")
    r.drv.play(P1, SEQUENCE)
    expect = t_of(r.step(30)[-1]) + 1
    pos = len(emu.log_text())
    quick_op(r, "save", 30)
    quick_op(r, "load", 15)
    r.drv.stop()
    final = t_of(r.step(2)[-1])
    back = re.search(r"record: quick state loaded: back to frame (\d+)", log_since(emu, pos))
    end = rec.slot_end(0, 3)[1]
    n = int(back.group(1)) if back else -1
    res.row("load during recording: recording goes on from the saved frame (later frames recorded again)",
            f"frame {expect}, end {final + 1}", f"frame {n}, end {end}", n == expect and end == final + 1)
    # A quick state from before the positions were saved again.
    quick_op(r, "save", 3)
    r.run_request("resave")
    pos = len(emu.log_text())
    rows = quick_op(r, "load", 20)
    log = log_since(emu, pos)
    e0, e1 = rows[1]["rec"]["end"][0][3], rows[-1]["rec"]["end"][0][3]
    detached = bool(rows[-1]["rec"]["flags"] & FLAG_DETACHED)
    r.request("restore")
    rows2 = r.step(5)
    cleared = not rows2[-1]["rec"]["flags"] & FLAG_DETACHED
    stopped = "other positions loaded" in log
    res.row("a state from before the positions: recording stops until a restore",
            "logged, slot unchanged while detached, cleared by the restore",
            f"logged {stopped}, detached {detached}, slot end {e0} -> {e1}, cleared {cleared}",
            stopped and detached and e0 == e1 and cleared)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


def s_percent(r):
    """Random Percent (CPU page) on every Restore Positions."""
    res = Result("random percent")
    emu, rec = r.emu, r.rec
    rec.set(randomPercent=[0, 50])
    got = []
    for _ in range(4):
        r.request("restore")
        row = r.step(3)[-1]
        got.append((round(fl(row[P1]["damage"]), 1), round(fl(row[CPU]["damage"]), 1)))
    rec.set(randomPercent=[0, 0])
    r.request("restore")
    base = r.step(3)[-1]
    p1 = {g[0] for g in got}
    cpu = [g[1] for g in got]
    b = fl(base[CPU]["damage"])
    res.row("CPU damage on four restores with Random Percent 50", "saved + 0..50, not all equal; P1 unchanged",
            f"saved {b:.0f}, CPU {cpu}, P1 {sorted(p1)}",
            all(b <= c <= b + 50 for c in cpu) and len(set(cpu)) > 1 and p1 == {round(fl(base[P1]["damage"]), 1)})
    return res


AIR_KIND = 2  # the situation kind of a fighter in the air


def xy(row, player):
    return fl(row[player]["x"]), fl(row[player]["y"])


def restored_rows(r, frames):
    """Restore Positions, then `frames` rows from the one the restore ran in."""
    before = r.rec.status()["restores"]
    r.request("restore")
    rows = r.step(frames + 2)
    at = next((i for i, x in enumerate(rows) if x["rec"]["restores"] > before), None)
    if at is None:
        raise RecError("no restore in the run")
    return rows[at:at + frames]


def lite_placement(r, res):
    """What the lite restore puts back, with the caller's lite positions
    (three CPUs). A launch: Mario walks up to the CPU (at 80%, held neutral
    by a script, its AI off) and forward smashes it; Restore Positions two
    frames into the hit's hitlag, and six frames into the flight after it,
    must put the CPU back for good: on the ground where it was saved in
    every row from the restore's on, in Wait at the end (out of the flight
    it stands in Landing first). Before, the hit's launch stayed in the
    fighter: it slid on and left the ground again. Then positions saved in
    the air: Mario is put back there, falls and lands."""
    emu, rec = r.emu, r.rec
    opt = C.Options(emu)
    opt.find()
    hold = C.PI.PluginInput(emu).blocks[1]
    # The positions may have been saved in the air (an earlier scenario
    # re-saves them in mid-run): saved again with both standing.
    restored_rows(r, 6)
    for _ in range(300):
        row = r.step(1)[-1]
        if all(row[p]["status"] == STATUS["Wait"] and row[p]["air"] != AIR_KIND for p in (P1, CPU)):
            break
    r.run_request("resave")
    home = restored_rows(r, 6)[-1]
    hx, hy = xy(home, CPU)
    toward = 1.0 if hx > xy(home, P1)[0] else -1.0
    opt.cpu_percent(80, True)
    for case in ("hitlag", "flight"):
        restored_rows(r, 6)
        hold.play(CPU, [C.NEUTRAL_STEP], loop=True, ai_off=True)
        r.drv.play(P1, [(0, 0.6 * toward, 0.0, 600)])
        for _ in range(400):
            row = r.step(1)[-1]
            if abs(xy(row, CPU)[0] - xy(row, P1)[0]) <= 19.5:
                break
        r.drv.play(P1, [(0, 0.0, 0.0, 20), (0x121, toward, 0.0, 1), (0x100, 0.0, 0.0, 4), (0, 0.0, 0.0, 200)])
        rows = r.step(20)
        reached = None
        for _ in range(80):
            rows += r.step(1)
            c = rows[-1][CPU]
            if case == "hitlag" and c["hitlag"] > 0 and rows[-2][CPU]["hitlag"] > 0:
                reached = len(rows)
                break
            if c["hitlag"] == 0 and c["hitstun"] > 0 and c["air"] == AIR_KIND:
                reached = reached or len(rows)
                if len(rows) >= reached + 6:
                    break
        c, at = rows[-1][CPU], xy(rows[-1], CPU)
        back = restored_rows(r, 60)
        r.drv.stop()
        hold.stop()
        off = [i for i, x in enumerate(back) if x[CPU]["air"] == AIR_KIND or abs(xy(x, CPU)[0] - hx) > 0.05
               or abs(xy(x, CPU)[1] - hy) > 0.05 or x[CPU]["status"] not in (STATUS["Wait"], STATUS["Landing"])]
        wait = next((i for i, x in enumerate(back) if x[CPU]["status"] == STATUS["Wait"]), None)
        end = back[-1][CPU]
        res.row(f"lite restore of a CPU in a hit's {case}", "on the ground where it was saved in every row, Wait at "
                "the end", f"restored from ({at[0]:.1f}, {at[1]:.1f}) in {sname(c['status'])} (hitlag {c['hitlag']}, "
                f"hitstun {c['hitstun']}) to ({hx:.1f}, {hy:.2f}): rows off {off[:10]} of {len(back)}, Wait from row "
                f"{wait}, at the end ({fl(end['x']):.2f}, {fl(end['y']):.2f}) {sname(end['status'])}",
                reached is not None and c["status"] in C.FLYING and len(back) == 60 and not off
                and end["status"] == STATUS["Wait"])
    opt.cpu_percent(0, False)
    # Positions saved with Mario in the air.
    ground = xy(restored_rows(r, 6)[-1], P1)
    r.drv.play(P1, [(JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 200)])
    # The save is carried out before the next frame's step: it takes the
    # position read here.
    saved = {P1: r.step(14)[-1][P1]}
    sx, sy = xy(saved, P1)
    before = rec.status()["result"]
    rec.request("resave")
    rows = r.step(100)
    landed = next((i for i, x in enumerate(rows) if x[P1]["air"] != AIR_KIND), None)
    back = restored_rows(r, 80)
    r.drv.stop()
    first_row = back[0][P1]
    down = next((i for i, x in enumerate(back) if x[P1]["air"] != AIR_KIND), None)
    res.row("lite restore of positions saved in the air", "in the air there, then a fall and a landing",
            f"saved at ({sx:.2f}, {sy:.2f}) in {sname(saved[P1]['status'])} (ground y {ground[1]:.2f}), landed on row "
            f"{landed}; restored: ({fl(first_row['x']):.2f}, {fl(first_row['y']):.2f}) {sname(first_row['status'])}, "
            f"on the ground on row {down}",
            rows[0]["rec"]["result"] == before + 1 and saved[P1]["air"] == AIR_KIND and landed is not None
            and first_row["air"] == AIR_KIND and abs(fl(first_row["x"]) - sx) < 0.05
            and abs(fl(first_row["y"]) - sy) < 1.0 and first_row["status"] == STATUS["Fall"]
            and down is not None and down > 3 and abs(fl(back[down][P1]["y"]) - ground[1]) < 0.05)
    emu.h.call("resume")
    emu.wait_for("Mario to stand", lambda: r.tr.row(P1)["status"] == STATUS["Wait"], 600, every=5)


def wait_fighters(emu, count):
    """Until the match has `count` fighters in memory."""
    fighters = LT.Fighters(emu)
    emu.wait_for(f"{count} fighters in the match", lambda: len(fighters.entries()) == count, 600, every=5)


def s_fallback(r):
    """Three CPUs: the savestate gives its buffers up, Save Positions falls
    back to the lite start state, recording and playback still work.
    Fighters 3 and 4 are in no start state: CPU Control goes off so they
    stay on Training's Stop (with it on, the CPU Options take them during
    one run and not the other)."""
    res = Result("lite fallback")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    rec.cpu_control(False)
    nav.set_training_cpus(emu, 3)
    # The setup lists the CPUs at once; their fighters come some frames
    # later, once their files have loaded.
    wait_fighters(emu, 4)
    emu.advance(30)
    r.tr.clear()
    pos = len(emu.log_text())
    r.request("restore")
    r.step(3)
    gone = "record: the positions' savestate is gone: lite restore" in log_since(emu, pos)
    res.row("Restore Positions after the CPUs were raised", "the savestate is gone: lite restore",
            f"logged {gone}", gone)
    pos = len(emu.log_text())
    r.run_request("resave")
    r.step(3)
    log = log_since(emu, pos)
    lite = re.search(r"record: positions saved again \(lite: ([^)]*)\)", log)
    res.row("Re-Save Positions with 3 CPUs", "lite, with the savestate's reason",
            f"{lite.group(0) if lite else None}, exact flag {r.exact()}", lite is not None and not r.exact())
    pos = len(emu.log_text())
    rows = record_hmn(r, slot=5, sequence=MIRROR_SEQUENCE)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    play = play_hmn(r, 5, len(rows))
    double = re.findall(r"record: two game steps in one pass of the main loop at frame \d+", log_since(emu, pos))
    res.note(f"two game steps in one pass: {double or 'none'}")
    d = compare(rows, play, P1)
    res.row(f"P1 trace with the lite restore, {len(rows)} frames", "equal", d or "equal", d is None)
    if d is not None:
        for t in range(max(0, d[0] - 8), min(d[0] + 3, len(rows), len(play))):
            res.note(f"t {t} slot {rec.slot_frame(0, 4, t)}")
            res.note(f"  rec  {show(rows[t][P1]) if rows[t] else None} f {rows[t]['frame'] if rows[t] else None}")
            res.note(f"  play {show(play[t][P1]) if play[t] else None} f {play[t]['frame'] if play[t] else None}")
    # Frame Decrement with these lite positions: refused, the frame stays.
    rec.set_setting(SET_DECREMENT, DECREMENT_X)
    rec.set(hmnMode=HMN_MODE["Playback"], hmnSlot=5)
    r.request("hmn-mode")
    r.step(20)
    rec.set_setting(SET_FRAME_ADVANCE, 1)
    r.step(3)
    st0, rw0, s0 = rec.status(), rec.rewind_status(), snap(r)
    pos = len(emu.log_text())
    emu.h.call("resume")
    emu.press(0, ["X"], frames=2, after=20)
    emu.h.call("pause")
    st1, rw1, moved = rec.status(), rec.rewind_status(), same(snap(r), s0)
    refused = "record: frame decrement refused (1)" in log_since(emu, pos)
    res.row("Frame Decrement with lite positions", "refused (no savestate start), the game stays on its frame",
            f"logged {refused}, refusals +{rw1['refused'] - rw0['refused']}, rewinds +{rw1['rewinds'] - rw0['rewinds']}"
            f", t {st0['t'][0]} -> {st1['t'][0]}, changed {moved or 'nothing'}",
            refused and rw1["rewinds"] == rw0["rewinds"] and st1["t"] == st0["t"] and not moved)
    rec.set_setting(SET_FRAME_ADVANCE, 0)
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()
    lab.open_menu()
    lab.go_controls()
    lab.down(2)
    res.note(f"Controls page, Frame Decrement Button's description: {emu.screenshot('record-decrement-lite')[0]}")
    lab.close_menu()
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    lite_placement(r, res)
    emu.h.call("resume")
    nav.set_training_cpus(emu, 1)
    wait_fighters(emu, 2)
    emu.advance(30)
    r.tr.clear()
    pos = len(emu.log_text())
    r.run_request("resave")
    r.step(3)
    how = re.search(r"record: positions saved again \((savestate|lite[^)]*)\)", log_since(emu, pos))
    res.note(f"back to 1 CPU, Save Positions: {how.group(1) if how else None}")
    return res


def s_full(r):
    res = Result("3600-frame limit")
    emu, rec = r.emu, r.rec
    rec.set(hmnMode=HMN_MODE["Record"], hmnSlot=6)
    r.run_request("hmn-mode")
    r.request("restore")
    pos = len(emu.log_text())
    emu.h.call("resume")
    emu.hold(0, stick=RIGHT)
    emu.advance(20)
    emu.neutral(0)
    emu.wait_for("3610 recorded frames", lambda: rec.status()["t"][0] >= MAX_FRAMES + 10, 5000, every=60)
    shot = emu.screenshot("record-hud-full")[0]
    st = rec.status()
    e6 = rec.slot_end(0, 5)[1]
    full = re.search(r"record: HMN slot 6 full at 3600 frames", log_since(emu, pos))
    res.row("recording stops at 3600", "end 3600, logged", f"end {e6}, logged {bool(full)}",
            e6 == MAX_FRAMES and full)
    res.row("storage", f"{SLOT_BYTES} bytes", st["bytes"], st["bytes"] == SLOT_BYTES)
    res.note(f"screenshot {shot}")
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


def s_advance(r):
    res = Result("frame advance and pause")
    emu, rec = r.emu, r.rec
    lab = LT.Lab(emu, [])
    lab.find_settings()
    rec.set(hmnMode=HMN_MODE["Playback"], hmnSlot=1, loop=1)
    r.request("hmn-mode")
    r.step(5)
    emu.h.call("write_mem", addr=lab.settings + SET_FRAME_ADVANCE, hex="01")
    emu.h.call("resume")
    emu.advance(10)
    t0 = rec.status()["steps"]
    emu.advance(60)
    t1 = rec.status()["steps"]
    steps = []
    for _ in range(5):
        a = rec.status()["steps"]
        emu.press(0, ["L"], frames=1, after=6)
        steps.append(rec.status()["steps"] - a)
    res.row("frame advance on: no step for 60 frames, then one per L press", "0 / [1, 1, 1, 1, 1]",
            f"{t1 - t0} / {steps}", t1 == t0 and steps == [1] * 5)
    emu.h.call("write_mem", addr=lab.settings + SET_FRAME_ADVANCE, hex="00")
    emu.advance(10)
    nav.pause_training(emu)
    a = rec.status()
    emu.advance(60)
    b = rec.status()
    nav.resume_training(emu)
    res.row("Training's pause holds the playback", "no step over 60 frames",
            f"steps +{b['steps'] - a['steps']}, t {a['t'][0]} -> {b['t'][0]}, input hook calls while paused "
            f"+{b['paused_calls'] - a['paused_calls']}", b["steps"] == a["steps"] and b["t"] == a["t"])
    rec.set(hmnMode=0, loop=0)
    r.run_request("hmn-mode")
    return res


# ---- Alter Inputs and Frame Decrement ---------------------------------------------

# Alter Inputs rows (src/main/ui/pages/alter_inputs.cpp): Frame, Stick X, ...,
# Attack 7, Special 8, Jump 9.
ALTER_STICK_X_ROW, ALTER_JUMP_ROW = 1, 9
# Steps of the HMN recording (SEQUENCE) edited: 3 is in the first 6 frames
# of standing still, 12 in the walk to the right (stick x 0.5, shown 40).
EDIT_JUMP_T, EDIT_STICK_T, EDIT_STICK = 3, 12, -40


# Recording page row indices (src/main/ui/pages/recording.cpp).
ROW_RESAVE, ROW_SLOT_MANAGE = 12, 15
# CPU page row index for Port Control (src/main/ui/pages/cpu_options.cpp).
CPU_CONTROLLED_BY_ROW = 13


def rec_down_to(lab, rec, target_row):
    """From row 0 on Recording (positions saved), move down to `target_row`,
    accounting for rows skipped by the cursor when disabled."""
    mirrored = rec.get("mirrored")
    hmn = rec.get("hmnMode")
    cpu = rec.get("cpuMode")
    cpu_lab = lab.setting(SET_CPU_CONTROL) == 1
    recording = hmn in (1, 3) or cpu in (2, 4)
    playing = hmn in (2, 3) or cpu in (3, 4)

    def enabled(row):
        if row in (1, 2):
            return mirrored == 0
        if row in (3, 4, 6):
            return cpu_lab and mirrored == 0
        if row == 5:
            return (not recording) and playing
        if row in (7, 8):
            return not recording
        return True

    lab.down(sum(1 for row in range(1, target_row + 1) if enabled(row)))


def alter_stick(r, lab, slot, t, target, limit=60):
    """Left/Right on the Stick X row until the slot's frame t holds
    target / 80: held with R (a step every 2 frames) while far, then taps."""
    for _ in range(limit):
        v = round(r.rec.slot_frame(0, slot, t)[1] * 80)
        if v == target:
            return v
        diff = target - v
        stick = LT.RIGHT if diff > 0 else LT.LEFT
        if abs(diff) > 8:
            r.emu.press(0, ["R"], frames=2 * (abs(diff) - 4), after=4, stick=stick)
        else:
            lab.tap(stick=stick)
    raise RecError(f"Stick X of frame {t} stuck at {r.rec.slot_frame(0, slot, t)[1]:.4f}, wanted {target}/80")


def s_edit(r):
    """Alter Inputs through the lab menu, on a copy of the HMN recording
    (slot 1 copied to slot 5): Jump on frame 4 (step 3, P1 standing) and
    Stick X from 40 (walking right) to -40 on frame 13 (step 12). The slot
    changes on those two steps only; its playback's inputs differ from slot
    1's playback on exactly those steps, and P1's state from step 3 on."""
    res = Result("Alter Inputs")
    emu, rec = r.emu, r.rec
    rec.cpu_control(True)
    rec.set(hmnMode=0, cpuMode=0, manageSide=0, manageSlot=0, manageCopyTo=4)
    r.run_request("hmn-mode")
    r.run_request("copy")
    end = rec.slot_end(0, 0)[1]
    before = rec.slot_bytes(0, 0, end)
    rec.set(manageSlot=4)
    lab = LT.Lab(emu, [])
    lab.find_settings()
    emu.h.call("resume")
    rec_menu_open(lab)
    rec_down_to(lab, rec, ROW_SLOT_MANAGE)
    lab.tap(["A"])  # Slot Management
    lab.down(2)
    lab.tap(["A"])  # Modify Inputs: Alter Inputs, on Frame 1
    pos = len(emu.log_text())
    for _ in range(EDIT_JUMP_T):
        lab.tap(stick=LT.RIGHT)
    lab.down(ALTER_JUMP_ROW)
    lab.tap(stick=LT.RIGHT)
    shot = emu.screenshot("record-alter-inputs")[0]
    lab.up(ALTER_JUMP_ROW)
    for _ in range(EDIT_STICK_T - EDIT_JUMP_T):
        lab.tap(stick=LT.RIGHT)
    lab.down(ALTER_STICK_X_ROW)
    alter_stick(r, lab, 4, EDIT_STICK_T, EDIT_STICK)
    shot2 = emu.screenshot("record-alter-inputs-stick")[0]
    lab.close_menu()
    res.note(f"screenshots: {shot}, {shot2}")
    frames = sorted({int(f) for f in re.findall(r"record: frame (\d+) edited", log_since(emu, pos))})
    after = rec.slot_bytes(0, 4, end)
    changed = [t for t in range(end) if before[8 * t:8 * t + 8] != after[8 * t:8 * t + 8]]
    jb, sx = rec.slot_frame(0, 4, EDIT_JUMP_T)[0], rec.slot_frame(0, 4, EDIT_STICK_T)[1]
    res.row("edits through the menu", f"frame {EDIT_JUMP_T + 1}: Jump; frame {EDIT_STICK_T + 1}: Stick X {EDIT_STICK}",
            f"edits logged on frames {frames}; step {EDIT_JUMP_T} buttons {jb:#x}, step {EDIT_STICK_T} x {sx:.4f}",
            jb & JUMP and sx == EDIT_STICK / 80.0 and frames == [EDIT_JUMP_T + 1, EDIT_STICK_T + 1])
    res.row("slot 5 against slot 1, frame by frame", f"steps {EDIT_JUMP_T} and {EDIT_STICK_T} only, same length",
            f"differ on {changed[:6]}, start/end {rec.slot_end(0, 4)} / {rec.slot_end(0, 0)}",
            changed == [EDIT_JUMP_T, EDIT_STICK_T] and rec.slot_end(0, 4) == rec.slot_end(0, 0))
    base = r.recordings["hmn-play"]
    play = play_hmn(r, 5, len(base))
    n = min(len(base), len(play))
    inputs = [i for i in range(n) if base[i] is not None and play[i] is not None
              and base[i][P1]["input"] != play[i][P1]["input"]]
    first = compare(base, play, P1)
    res.row("playback of slot 5 against slot 1's", f"inputs differ on steps {EDIT_JUMP_T}, {EDIT_STICK_T} only; "
            f"state first at {EDIT_JUMP_T}",
            f"inputs differ on {inputs[:6]}; state first differs at {first[0] if first else None} "
            f"({first[1] if first else ''}): {first[3] if first else ''}",
            inputs == [EDIT_JUMP_T, EDIT_STICK_T] and first is not None and first[0] == EDIT_JUMP_T
            and play[EDIT_JUMP_T][P1]["status"] == STATUS["Jump_Squat"])
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    return res


# Rewinds from step counts small and large: within both playbacks, past both
# ends (the playbacks were just played), and after a live takeover of the
# HMN playback with Random Percent on: (steps, live pad (from, frames),
# random percent).
REWIND_AT = ((5, None, 0), (300, None, 0), (600, None, 0), (120, (40, 20), 30))


def snap(r):
    """Both fighters, the random state and the match's frame counter now."""
    return {"frame": nav.game_frame(r.emu), "rng": b"".join(r.rng()), P1: r.tr.row(P1), CPU: r.tr.row(CPU)}


def same(a, b):
    """The fields of `a` and `b` that differ: the fighters and the random
    state. Not the match's frame counter: it also counts the steps frame
    advance holds (the game's step function runs, its tasks do not)."""
    out = ["rng"] if a["rng"] != b["rng"] else []
    for p, name in ((P1, "P1"), (CPU, "CPU")):
        out += [f"{name} {k}" for k in a[p] if a[p][k] != b[p][k]]
    return out


def held_at(r, n, live=None):
    """From a restore of the playbacks, runs the game until n steps ran and
    has frame advance hold it there. With `live` = (first, frames), the pad
    holds the stick right from about step `first` for `frames` frames (it
    takes the HMN playback over). Returns the states after the last three
    steps (the last is the one held)."""
    emu, rec = r.emu, r.rec
    before = rec.status()["restores"]
    r.request("restore")
    for _ in range(10):
        emu.h.call("frame_advance", n=1)
        if rec.status()["restores"] > before:
            break
    start = rec.status()["start_frame"]
    if live:
        emu.h.call("frame_advance", n=live[0] - (nav.game_frame(emu) - start))
        emu.hold(0, stick=LT.RIGHT)
        emu.h.call("frame_advance", n=live[1])
        emu.neutral(0)
    ran = nav.game_frame(emu) - start
    if n - ran > 3:
        emu.h.call("frame_advance", n=n - ran - 3)
    rows = []
    for _ in range(10):
        if nav.game_frame(emu) - start >= n:
            break
        rows.append(emu.step(r.snaps))
    if nav.game_frame(emu) - start != n or len(rows) < 2:
        raise RecError(f"wanted {n} steps from the restore, {nav.game_frame(emu) - start} ran")
    rec.set_setting(SET_FRAME_ADVANCE, 1)
    emu.h.call("frame_advance", n=3)
    return rows


def s_rewind(r):
    """Frame Decrement: both sides play back (HMN slot 1, CPU slot 1), frame
    advance holds the game n steps after the restore, X (the Frame Decrement
    Button) goes back one step. The state must be the one the first run had
    after step n - 2: both fighters (position, facing, status, animation
    frame, hitlag, hitstun, damage, input object, Project+'s analog
    variables), the game's random state and the frame counter; one press of
    Z (Frame Advance) then gives the state after step n - 1 again."""
    res = Result("Frame Decrement")
    emu, rec = r.emu, r.rec
    rec.cpu_control(True)
    rec.set_setting(SET_DECREMENT, DECREMENT_X)
    rec.set(hmnMode=HMN_MODE["Playback"], hmnSlot=1, cpuMode=CPU_MODE["Playback"], cpuSlot=1, cpuCounter=0,
            loop=0, autoRestore=0, startPaused=0)
    r.run_request("hmn-mode")
    for n, live, percent in REWIND_AT:
        rec.set(randomPercent=(percent, percent))
        rows = held_at(r, n, live)
        if live:
            st = rec.status()
            res.note(f"n {n}: HMN taken over at step {st['takeover'][0]}, cancelled "
                     f"{bool(st['flags'] & FLAG_HMN_CANCELLED)}; damage P1 {fl(rows[-1][P1]['damage']):.0f}, "
                     f"CPU {fl(rows[-1][CPU]['damage']):.0f}")
        held = r.snaps.read()
        hold_ok = not same(held, rows[-1])
        rw0 = rec.rewind_status()
        emu.h.call("resume")
        emu.press(0, ["X"], frames=2, after=2)
        emu.wait_for("the rewind", lambda: rec.rewind_status()["rewound"] > rw0["rewound"], 240, every=2)
        # Held there: the frame in progress ends with no step, and the test
        # block (written before each step) shows the frame it holds.
        r.settle()
        back = r.snaps.read()
        rw = rec.rewind_status()
        t = rec.status()["t"][0]
        diff = same(back, rows[-2])
        # One step on, with the pad neutral (past the playbacks' ends P1
        # reads the pad): frame advance off for one frame.
        rec.set_setting(SET_FRAME_ADVANCE, 0)
        emu.h.call("frame_advance", n=1)
        rec.set_setting(SET_FRAME_ADVANCE, 1)
        ahead = r.snaps.read()
        fwd = same(ahead, rows[-1])
        res.row(f"held {n} steps in, X" + (", after a takeover" if live else ""),
                f"state after step {n - 2} (as first run), one step on: after {n - 1}",
                f"held {hold_ok}; t {t}, target {rw['target']}, differs {diff or 'nothing'}; one step on differs "
                f"{fwd or 'nothing'}; {rw['ticks'] / 60750.0:.0f} ms, {rw['passes']} passes drawn",
                hold_ok and not diff and not fwd and rw["target"] == n - 1 and t == n - 1
                and rw["rewinds"] == rw0["rewinds"] + 1)
        res.note(f"n {n}: P1 {show(back[P1])}; CPU {show(back[CPU])}")
        for name, a, b in (("rewound", back, rows[-2]), ("one step on", ahead, rows[-1])):
            for p in (P1, CPU):
                if a[p] != b[p]:
                    res.note(f"  {name} {'P1' if p == P1 else 'CPU'}: {show(a[p])} / first run {show(b[p])}")
        rec.set_setting(SET_FRAME_ADVANCE, 0)
        emu.h.call("resume")
        emu.advance(5)
    rec.set(hmnMode=0, cpuMode=0, randomPercent=(0, 0))
    r.run_request("hmn-mode")
    r.run_request("cpu-mode")
    return res


def s_menu(r):
    """Delete the positions, then through the lab menu: Recording before Save
    (rows grey), Save Positions, the row turns into Restore Positions, HMN
    Mode to Record; screenshots of the page and of the HUD."""
    res = Result("Recording page")
    emu, rec = r.emu, r.rec
    lab = LT.Lab(emu, [])
    lab.find_settings()
    emu.h.call("resume")
    r.run_request("delete")
    emu.h.call("resume")
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    before = emu.screenshot("record-menu-before-save")[0]
    pos = len(emu.log_text())
    lab.tap(["A"])
    lab.wait_log("Save Positions", r"record: positions to save", pos)
    lab.down(ROW_HMN_MODE)
    lab.tap(stick=LT.RIGHT)
    mode = rec.get("hmnMode")
    after = emu.screenshot("record-menu-after-save")[0]
    lab.back_to_main(1)
    lab.close_menu()
    emu.advance(30)
    st = rec.status()
    log = log_since(emu, pos)
    how = re.search(r"record: positions saved \((savestate|lite[^)]*)\)", log)
    save_ref = re.findall(r"savestate: recording state save refused: [^\r\n]+", log)
    res.row("Save Positions from the menu, then HMN Mode Right", "saved once the match runs, HMN Mode Record (1), 0 refusals",
            f"saved {bool(st['flags'] & FLAG_SAVED)} ({how.group(1) if how else None}), HMN Mode {mode}, "
            f"refusals {len(save_ref)}",
            st["flags"] & FLAG_SAVED and mode == 1 and how is not None and not save_ref)
    rec_shot = emu.screenshot("record-hud-recording")[0]
    rec.set(hmnMode=HMN_MODE["Playback"])
    r.request("hmn-mode")
    emu.h.call("resume")
    emu.advance(12)
    play_shot = emu.screenshot("record-hud-playback")[0]
    res.note(f"screenshots: {before}, {after}, {rec_shot}, {play_shot}")
    xscale = 0.75 if "lab: screen 16:9" in emu.log_text() else 1.0
    px, ok = hud_check(rec_shot, xscale)
    res.row("HUD bar in the recording screenshot", "about (20, 26, 36)", px, ok)
    rec.set(hmnMode=0)
    r.run_request("hmn-mode")
    emu.h.call("resume")
    # Start-opened menu left open after Restore Positions and HMN Mode Playback:
    # no savestate refusals while open or on close, fighters wait for close.
    saved_x = fl(r.tr.row(P1)["x"])
    emu.hold(0, stick=RIGHT)
    emu.advance(30)
    emu.neutral(0)
    emu.advance(10)
    moved_x = fl(r.tr.row(P1)["x"])
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    pos = len(emu.log_text())
    lab.tap(["A"])  # Restore Positions
    emu.advance(30)
    open_x = fl(r.tr.row(P1)["x"])
    open_ref = re.findall(r"savestate: recording state load refused: [^\r\n]+", log_since(emu, pos))
    lab.close_menu()
    emu.advance(10)
    close_x = fl(r.tr.row(P1)["x"])
    all_ref = re.findall(r"savestate: recording state load refused: [^\r\n]+", log_since(emu, pos))
    res.row("Restore Positions in Start menu (exact, 30f open)", "0 refusals, P1 waits for close then restores",
            f"refusals open {len(open_ref)}/all {len(all_ref)}, x moved {moved_x:.1f} -> open {open_x:.1f} -> "
            f"close {close_x:.1f} (saved {saved_x:.1f})",
            not all_ref and abs(moved_x - saved_x) > 5.0 and abs(open_x - saved_x) > 5.0
            and abs(close_x - saved_x) < 1.0)
    lab.open_menu(keep=True)
    lab.down(ROW_HMN_MODE)
    pos = len(emu.log_text())
    lab.tap(stick=LT.RIGHT)  # Record
    lab.tap(stick=LT.RIGHT)  # Playback (requests restore)
    emu.advance(30)
    lab.tap(stick=LT.LEFT)
    lab.tap(stick=LT.LEFT)   # back to Off
    lab.up(ROW_HMN_MODE)
    lab.close_menu()
    emu.advance(10)
    mode_ref = re.findall(r"savestate: recording state load refused: [^\r\n]+", log_since(emu, pos))
    res.row("HMN Mode -> Playback in Start menu (30f open)", "0 load refusals",
            f"refusals {len(mode_ref)}", not mode_ref)
    # Without an exact savestate (3 CPUs): lite restore and its message wait for
    # the menu to close (do not move fighters or burn the 150f message behind it).
    rec.cpu_control(False)
    nav.set_training_cpus(emu, 3)
    wait_fighters(emu, 4)
    emu.advance(30)
    r.tr.clear()
    emu.hold(0, stick=RIGHT)
    emu.advance(30)
    emu.neutral(0)
    emu.advance(10)
    moved_x = fl(r.tr.row(P1)["x"])
    lab.open_menu(keep=True)
    pos = len(emu.log_text())
    lab.tap(["A"])  # Restore Positions (savestate gone -> lite fallback)
    emu.advance(160)
    open_x = fl(r.tr.row(P1)["x"])
    open_log = log_since(emu, pos)
    open_restored = "record: restored (lite)" in open_log
    open_msg = "record: Restored without the savestate." in open_log
    pos_close = len(emu.log_text())
    lab.close_menu()
    emu.advance(10)
    close_x = fl(r.tr.row(P1)["x"])
    close_log = log_since(emu, pos_close)
    st_close = rec.status()
    res.row("Restore Positions in Start menu (savestate gone, 160f open)",
            "waits for close, message still active after close",
            f"open x {open_x:.1f} restored {open_restored} msg {open_msg}; close x {close_x:.1f} "
            f"restored {'record: restored (lite)' in close_log} msg_flag {bool(st_close['flags'] & FLAG_MESSAGE)}",
            abs(open_x - saved_x) > 5.0 and not open_restored and not open_msg
            and abs(close_x - saved_x) < 1.0 and "record: restored (lite)" in close_log
            and bool(st_close["flags"] & FLAG_MESSAGE))
    # Already lite (s_exact false): Restore Positions also waits for close.
    emu.hold(0, stick=RIGHT)
    emu.advance(30)
    emu.neutral(0)
    emu.advance(10)
    moved_x = fl(r.tr.row(P1)["x"])
    lab.open_menu(keep=True)
    before_restores = rec.status()["restores"]
    lab.tap(["A"])  # Restore Positions
    emu.advance(30)
    open_x = fl(r.tr.row(P1)["x"])
    open_restores = rec.status()["restores"]
    lab.close_menu()
    emu.advance(10)
    close_x = fl(r.tr.row(P1)["x"])
    close_restores = rec.status()["restores"]
    res.row("Restore Positions in Start menu (lite, 30f open)", "waits for close",
            f"open x {open_x:.1f} restores +{open_restores - before_restores}; "
            f"close x {close_x:.1f} restores +{close_restores - before_restores}",
            abs(open_x - saved_x) > 5.0 and open_restores == before_restores
            and abs(close_x - saved_x) < 1.0 and close_restores == before_restores + 1)
    nav.set_training_cpus(emu, 1)
    wait_fighters(emu, 2)
    emu.advance(30)
    r.tr.clear()
    rec.cpu_control(True)
    return res


def player_controllers(emu):
    """m_playersInitData[p].m_controllerNo (gmGlobalModeMelee +0x98 + 0x5C*p + 7)
    for P1 and CPU (P2)."""
    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
    return tuple(emu.u8(mode + nav.MELEE_PLAYERS + p * nav.MELEE_PLAYER_SIZE + 7) for p in range(2))


def rec_menu_open(lab):
    """Open the lab menu to Recording, row 0 (Save/Restore Positions)."""
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.up(18)


def set_cpu_control_menu(lab, target):
    """Set CPU Options > CPU Control (0 Training, 1 Lab) through the lab menu."""
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.up(25)
    lab.down(C.CPU_CONTROL_ROW)
    lab.set_choice(SET_CPU_CONTROL, target)
    lab.close_menu()


def set_controlled_by_menu(lab, target, before_close=None):
    """Set CPU Options > Controlled By (0 None, 1..4 Port 1..4) through the lab menu."""
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.emu.press(getattr(lab, "port", 0), [], frames=50, after=4, stick=LT.UP)
    lab.down(CPU_CONTROLLED_BY_ROW)
    lab.set_choice(SET_CONTROLLED_BY, target)
    if before_close is not None:
        before_close()
    lab.close_menu()


def resave_positions_menu(lab, rec):
    """From row 0 on Recording (positions saved), select Re-Save Positions."""
    rec_down_to(lab, rec, ROW_RESAVE)
    lab.tap(["A"])


def s_cpu_controller_leak(r):
    """Saving positions or a quick state while CPU Mode = Control, setting CPU
    Mode = Off, then restoring must not leave P1's controller port on the CPU's
    gmPlayerInitData.m_controllerNo or leak P1's input onto the CPU."""
    res = Result("CPU controller leak")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)

    ctrl_before = player_controllers(emu)
    ai_before = r.tr.g.ai_flag(1)
    res.note(f"before: controllers P1={ctrl_before[0]}, CPU={ctrl_before[1]}, cpu aiFlag={ai_before}")

    # Initial Save Positions (needed to enable CPU Mode on the Recording page)
    # and switch CPU Mode -> Control through the Recording page.
    rec_menu_open(lab)
    if not (rec.status()["flags"] & FLAG_SAVED):
        pos = lab.log_pos()
        lab.tap(["A"])  # Save Positions
        lab.wait_log("Save Positions queued", r"record: positions to save", pos)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Control"])
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(20)
    ctrl_control = player_controllers(emu)
    ai_control = r.tr.g.ai_flag(1)
    res.note(f"in CPU Mode=Control: controllers P1={ctrl_control[0]}, CPU={ctrl_control[1]}, "
             f"cpu aiFlag={ai_control}, swapped={bool(rec.status()['flags'] & FLAG_SWAPPED)}")

    # Re-Save Positions (via Y+Z inside the menu) AND D-pad right quick save
    # while CPU Mode = Control so both savestate slots capture the Control state.
    rec_menu_open(lab)
    pos = lab.log_pos()
    resave_positions_menu(lab, rec)
    lab.wait_log("Re-Save Positions queued", r"record: positions to save \(2\)", pos)
    lab.close_menu()
    lab.wait_log("Re-Save Positions saved", r"record: positions saved again \(savestate\)", pos, frames=180)
    emu.advance(10)

    pos_q = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("quick save in Control", r"savestate: saved frame \d+", pos_q, frames=120)
    emu.advance(35)  # clear 30f save lockout

    ctrl_after_save = player_controllers(emu)
    ai_after_save = r.tr.g.ai_flag(1)
    res.note(f"after save (Re-Save + D-pad right): controllers P1={ctrl_after_save[0]}, CPU={ctrl_after_save[1]}, "
             f"cpu aiFlag={ai_after_save}")

    # Set CPU Mode = Off through the Recording page.
    rec_menu_open(lab)
    lab.down(3)
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Off"])
    pos = lab.log_pos()
    lab.close_menu()
    emu.advance(20)
    ctrl_after_off = player_controllers(emu)
    ai_after_off = r.tr.g.ai_flag(1)
    swapped_after_off = bool(rec.status()["flags"] & FLAG_SWAPPED)
    res.note(f"after CPU Mode=Off: controllers P1={ctrl_after_off[0]}, CPU={ctrl_after_off[1]}, "
             f"cpu aiFlag={ai_after_off}, swapped={swapped_after_off}")

    # Restore Positions through the Recording page.
    rec_menu_open(lab)
    pos = lab.log_pos()
    lab.tap(["A"])  # Restore Positions
    lab.close_menu()
    lab.wait_log("Restore Positions loaded", r"savestate: recording state loaded frame \d+", pos, frames=180)
    emu.advance(15)
    ctrl_after_restore = player_controllers(emu)
    ai_after_restore = r.tr.g.ai_flag(1)
    swapped_after_restore = bool(rec.status()["flags"] & FLAG_SWAPPED)
    res.note(f"after Restore Positions: controllers P1={ctrl_after_restore[0]}, CPU={ctrl_after_restore[1]}, "
             f"cpu aiFlag={ai_after_restore}, swapped={swapped_after_restore}")

    res.row("CPU m_controllerNo after Restore Positions (CPU Mode Off)",
            f"CPU={ctrl_before[1]} (unchanged)",
            f"before {ctrl_before} -> save {ctrl_after_save} -> off {ctrl_after_off} -> restore {ctrl_after_restore}",
            ctrl_after_restore[1] == ctrl_before[1])

    # Check 1-frame leak on quick load while holding Left on P1 with CPU Mode = Off:
    # first with CPU Control = Lab, then with CPU Control = Training (Brawl CPU = Stop).
    def probe_quickload_leak(label_mode):
        emu.h.call("resume")
        emu.advance(10)
        emu.h.call("pause")
        pos_l = lab.log_pos()
        emu.hold(0, ["DLEFT"], stick=LEFT)
        trace = []
        for f in range(8):
            if f == 2:
                emu.hold(0, [], stick=LEFT)
            emu.h.call("frame_advance", n=1)
            en_c = r.tr.g.entry(CPU)
            ai_c = r.tr.g.inputs(en_c)[0]
            pad_c = r.tr.g.pad(ai_c)
            row_c = r.tr.row(CPU)
            trace.append((f, r.tr.g.ai_flag(1), player_controllers(emu)[1], pad_c["cont"],
                          round(pad_c["x"], 3), pad_c["buttons"], sname(row_c["status"]), round(fl(row_c["x"]), 2)))
        emu.neutral(0)
        emu.h.call("resume")
        loaded = bool(re.search(r"savestate: loaded frame \d+", log_since(emu, pos_l)))
        leaked = [t for t in trace if abs(t[4]) > 0.05]
        res.note(f"quick load holding Left ({label_mode}, loaded={loaded}): "
                 + "; ".join(f"f{t[0]}:ai={t[1]},ctrl={t[2]},cont={t[3]},x={t[4]},st={t[6]},pos={t[7]}" for t in trace))
        return loaded, leaked, trace

    loaded_lab, leaked_lab, _ = probe_quickload_leak("CPU Control=Lab, CPU Mode=Off")

    # Switch Lab CPU Control to Training (Brawl CPU is still Stop).
    set_cpu_control_menu(lab, 0)
    emu.advance(15)
    loaded_stop, leaked_stop, _ = probe_quickload_leak("CPU Control=Training, Brawl CPU=Stop, CPU Mode=Off")
    res.row("quick load with CPU Mode Off + Brawl CPU Stop does not leak P1 input",
            "loaded, 0 leaked frames",
            f"Lab leaked {len(leaked_lab)}; Training/Stop leaked {len(leaked_stop)} {leaked_stop[:2]}",
            loaded_lab and loaded_stop and not leaked_lab and not leaked_stop)

    # With Brawl's own CPU setting on Control, hold a direction on P1 for 60 frames.
    C.set_training_option(emu, "Control")
    emu.advance(20)
    ctrl_brawl = player_controllers(emu)
    ai_brawl = r.tr.g.ai_flag(1)
    p1_x0 = fl(r.tr.row(P1)["x"])
    cpu_x0 = fl(r.tr.row(CPU)["x"])
    stick_dir = LEFT if cpu_x0 > 0 else RIGHT
    dir_name = "LEFT" if stick_dir == LEFT else "RIGHT"
    emu.hold(0, stick=stick_dir)
    cpu_statuses = []
    for _ in range(60):
        emu.advance(1)
        cpu_statuses.append(sname(r.tr.row(CPU)["status"]))
    emu.neutral(0)
    emu.advance(5)
    p1_x60 = fl(r.tr.row(P1)["x"])
    cpu_x60 = fl(r.tr.row(CPU)["x"])
    dx_p1 = p1_x60 - p1_x0
    dx_cpu = cpu_x60 - cpu_x0
    res.note(f"Brawl CPU=Control, hold {dir_name} 60f: controllers P1={ctrl_brawl[0]}, CPU={ctrl_brawl[1]}, "
             f"aiFlag={ai_brawl}; P1 x {p1_x0:.2f}->{p1_x60:.2f} (dx={dx_p1:+.2f}); "
             f"CPU x {cpu_x0:.2f}->{cpu_x60:.2f} (dx={dx_cpu:+.2f}), CPU statuses {sorted(set(cpu_statuses))}")
    res.row("Brawl CPU=Control, P1 holds direction 60f: CPU must stay still",
            "CPU |dx| < 0.01, status Wait only",
            f"CPU dx={dx_cpu:+.2f}, statuses {sorted(set(cpu_statuses))}, P1 dx={dx_p1:+.2f}",
            abs(dx_cpu) < 0.01 and set(cpu_statuses) == {"Wait"})

    # Clean up Training CPU option and Lab CPU Control.
    C.set_training_option(emu, "Stop")
    set_cpu_control_menu(lab, 1)
    emu.advance(10)
    return res


def s_quick_load_after_end(r):
    """A quick save (D-pad right) taken mid-playback, followed by the slot
    finishing (or a takeover) and a quick load (D-pad left), must resume
    playback from the saved frame rather than leaving CPU playback cancelled."""
    res = Result("quick load after playback end")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)

    # Record a ~110-frame CPU Slot 1 (CPU running left) via the Recording page.
    rec_menu_open(lab)
    already_saved = bool(rec.status()["flags"] & FLAG_SAVED)
    if not already_saved:
        pos = lab.log_pos()
        lab.tap(["A"])  # Save Positions
        lab.wait_log("Save Positions queued", r"record: positions to save", pos)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Record"])
    lab.down(1)  # CPU Record Slot
    lab.set_choice(SET_REC + REC["cpuSlot"], 1)
    if already_saved:
        lab.up(4)  # Restore Positions so the slot records cleanly from frame 0
        pos = lab.log_pos()
        lab.tap(["A"])
        lab.close_menu()
        lab.wait_log("restore before record", r"savestate: recording state loaded frame \d+", pos, frames=180)
    else:
        pos = lab.log_pos()
        lab.close_menu()
        lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)

    # Hold Left on Port 1 for ~105 frames (recorded into CPU Slot 1).
    emu.hold(0, stick=LEFT)
    emu.advance(105)
    emu.neutral(0)

    # Switch to Playback on the Recording page (CPU Counter defaults to After
    # Playback Ends = 1, Takeover defaults to HMN = 0).
    rec_menu_open(lab)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Playback"])
    start_f, end_f = rec.slot_end(1, 0)
    res.note(f"recorded CPU slot 1: start={start_f}, end={end_f}, cpuCounter={rec.get('cpuCounter')}, "
             f"takeover={rec.get('takeover')}")
    if end_f < 90:
        raise RecError(f"CPU slot 1 too short ({end_f} frames)")

    # Verify the recorded slot actually has stick-left inputs mid-slot.
    sample_slot = [(t, round(rec.slot_frame(1, 0, t)[1], 3)) for t in range(15, 75, 10)]
    res.note(f"CPU slot 1 recorded stick_x samples: {sample_slot}")

    # Close menu -> Playback starts from frame 0 (Restore Positions).
    pos_play = lab.log_pos()
    lab.close_menu()
    lab.wait_log("playback restore", r"savestate: recording state loaded frame \d+", pos_play, frames=180)

    # Advance into playback, then quick save with D-pad right (held 14f).
    emu.advance(15)
    mid_st = rec.status()
    mid_in = r.tr.row(CPU)["input"]
    res.note(f"mid-playback before D-pad right: t={mid_st['t'][1]}, "
             f"cpu_cancelled={bool(mid_st['flags'] & FLAG_CPU_CANCELLED)}, "
             f"cpu_input_x={fl(mid_in[4:8]):.3f}, cpu_x={fl(r.tr.row(CPU)['x']):.2f}")

    pos_qs = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("quick save mid-playback", r"savestate: saved frame \d+", pos_qs, frames=120)
    qs_t = rec.status()["t"][1]
    res.note(f"after D-pad right quick save: t={qs_t}, "
             f"cpu_cancelled={bool(rec.status()['flags'] & FLAG_CPU_CANCELLED)}")

    # Let the slot finish (and clear the 30f save lockout).
    emu.advance(max(40, end_f - qs_t + 25))
    end_st = rec.status()
    end_log = log_since(emu, pos_qs)
    ended_m = re.search(r"record: CPU playback ended at frame (\d+)", end_log)
    res.note(f"after slot finished: t={end_st['t'][1]}, "
             f"cpu_cancelled={bool(end_st['flags'] & FLAG_CPU_CANCELLED)}, "
             f"ended_log={ended_m.group(0) if ended_m else None}")

    # Press D-pad left (real controller) and trace per frame after the load.
    pos_ql = lab.log_pos()

    def press_dleft(i, _rows):
        if i < 2:
            emu.hold(0, ["DLEFT"])
        else:
            emu.neutral(0)

    rows = r.step(40, press_dleft)
    emu.neutral(0)
    ql_log = log_since(emu, pos_ql)
    back_m = re.search(r"record: quick state loaded: back to frame (\d+)", ql_log)
    back_t = int(back_m.group(1)) if back_m else -1
    loaded_m = re.search(r"savestate: loaded frame (\d+)", ql_log)
    res.note(f"after D-pad left: savestate_loaded={loaded_m.group(0) if loaded_m else None}, "
             f"record_back={back_m.group(0) if back_m else None}")

    # Inspect per-frame CPU inputs after the quick load.
    post_frames = []
    for row in rows:
        t_idx = t_of(row)
        if back_t >= 0 and back_t <= t_idx < min(back_t + 30, end_f):
            exp_b, exp_x, _ = rec.slot_frame(1, 0, t_idx)
            got_b = u32(row[CPU]["input"], 0)
            got_x = fl(row[CPU]["input"][4:8])
            canc = bool(row["rec"]["flags"] & FLAG_CPU_CANCELLED)
            post_frames.append((t_idx, canc, round(got_x, 3), round(exp_x, 3), got_b, exp_b,
                                sname(row[CPU]["status"]), round(fl(row[CPU]["x"]), 2)))

    active_exp = [pf for pf in post_frames if abs(pf[3]) > 0.1]
    matched = [pf for pf in active_exp if abs(pf[2] - pf[3]) < 0.01 and not pf[1]]
    still_cancelled = [pf for pf in post_frames if pf[1]]
    res.note("per-frame after D-pad left (t, cancelled, got_x, exp_x, status, cpu_x):")
    for pf in post_frames[:20]:
        res.note(f"  t={pf[0]:3d} cancelled={pf[1]} got_x={pf[2]:+.3f} exp_x={pf[3]:+.3f} "
                 f"status={pf[6]} cpu_x={pf[7]:.2f}")
    res.row("after slot finishes + D-pad left: CPU receives recorded inputs",
            f"back to frame < {end_f}, cancelled False, all active frames match slot",
            f"back_t={back_t}, {len(matched)}/{len(active_exp)} active frames matched, "
            f"{len(still_cancelled)}/{len(post_frames)} frames had CPU cancelled=True",
            0 <= back_t < end_f and len(active_exp) >= 15 and len(matched) == len(active_exp)
            and not still_cancelled)

    # Confirm a full Restore Positions DOES clear cancelled and resume playback.
    emu.h.call("resume")
    rec_menu_open(lab)
    pos_full = lab.log_pos()
    lab.tap(["A"])  # Restore Positions
    lab.close_menu()
    lab.wait_log("full restore", r"savestate: recording state loaded frame \d+", pos_full, frames=180)
    full_rows = r.step(25)
    full_active = [(t_of(x), bool(x["rec"]["flags"] & FLAG_CPU_CANCELLED),
                    round(fl(x[CPU]["input"][4:8]), 3), round(rec.slot_frame(1, 0, t_of(x))[1], 3))
                   for x in full_rows if 5 <= t_of(x) < 22]
    full_ok = bool(full_active) and all(not a[1] and abs(a[2] - a[3]) < 0.01 for a in full_active)
    res.note(f"after full Restore Positions (sample frames): {full_active[:6]}")
    res.row("full Restore Positions clears cancelled and resumes playback",
            "cancelled False, inputs match slot",
            f"{len(full_active)} frames checked, ok={full_ok}, sample={full_active[:3]}",
            full_ok)

    # Second variant: quick save mid-playback, take over CPU playback, then D-pad left.
    emu.h.call("resume")
    rec_menu_open(lab)
    lab.down(10)  # Playback Takeover (all rows 0..10 enabled in Playback)
    lab.set_choice(SET_REC + REC["takeover"], TAKEOVER["CPU"])
    lab.up(10)    # Restore Positions
    pos_tk = lab.log_pos()
    lab.tap(["A"])
    lab.close_menu()
    lab.wait_log("takeover test restore", r"savestate: recording state loaded frame \d+", pos_tk, frames=180)
    emu.advance(15)
    pos_qs2 = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("quick save before takeover", r"savestate: saved frame \d+", pos_qs2, frames=120)
    emu.advance(35)  # clear 30f lockout, still inside ~105f slot
    pos_take = lab.log_pos()
    emu.press(0, ["A"], frames=3, after=5)
    take_log = log_since(emu, pos_take)
    taken_m = re.search(r"record: CPU playback taken over at frame (\d+)", take_log)
    res.note(f"after takeover press A: {taken_m.group(0) if taken_m else None}, "
             f"cpu_cancelled={bool(rec.status()['flags'] & FLAG_CPU_CANCELLED)}")
    pos_ql2 = lab.log_pos()
    rows2 = r.step(30, press_dleft)
    emu.neutral(0)
    back2_m = re.search(r"record: quick state loaded: back to frame (\d+)", log_since(emu, pos_ql2))
    back2_t = int(back2_m.group(1)) if back2_m else -1
    post2 = []
    for row in rows2:
        t_idx = t_of(row)
        if back2_t >= 0 and back2_t <= t_idx < min(back2_t + 20, end_f):
            _, exp_x, _ = rec.slot_frame(1, 0, t_idx)
            got_x = fl(row[CPU]["input"][4:8])
            canc = bool(row["rec"]["flags"] & FLAG_CPU_CANCELLED)
            post2.append((t_idx, canc, round(got_x, 3), round(exp_x, 3)))
    active2 = [p for p in post2 if abs(p[3]) > 0.1]
    matched2 = [p for p in active2 if abs(p[2] - p[3]) < 0.01 and not p[1]]
    res.note(f"after takeover + D-pad left (back_t={back2_t}): sample={post2[:8]}")
    res.row("after CPU takeover + D-pad left: CPU resumes recorded inputs",
            "taken over, then back_t < end, cancelled False, inputs match",
            f"taken={bool(taken_m)}, back_t={back2_t}, {len(matched2)}/{len(active2)} active frames matched",
            taken_m is not None and 0 <= back2_t < end_f and len(active2) >= 10 and len(matched2) == len(active2))

    # Clean up Recording settings.
    emu.h.call("resume")
    rec_menu_open(lab)
    lab.down(10)
    lab.set_choice(SET_REC + REC["takeover"], TAKEOVER["HMN"])
    lab.up(7)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Off"])
    lab.close_menu()
    emu.advance(10)
    return res


def s_quick_load_holding(r):
    """With Start Paused = On and CPU Mode = Playback, QuickMark must save and
    restore s_holding:
    1) quick save mid-run, Restore Positions (starts held), quick load -> must
       resume running immediately without staying frozen;
    2) quick save while held at frame 0, release and advance mid-run, quick
       load -> must restore the hold at frame 0."""
    res = Result("quick load Start Paused holding")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)

    # Record a ~90f CPU Slot 1 (CPU moving left).
    rec_menu_open(lab)
    already_saved = bool(rec.status()["flags"] & FLAG_SAVED)
    if not already_saved:
        pos = lab.log_pos()
        lab.tap(["A"])  # Save Positions
        lab.wait_log("Save Positions queued", r"record: positions to save", pos)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Record"])
    lab.down(1)  # CPU Record Slot
    lab.set_choice(SET_REC + REC["cpuSlot"], 1)
    if already_saved:
        lab.up(4)
        pos = lab.log_pos()
        lab.tap(["A"])  # Restore Positions
        lab.close_menu()
        lab.wait_log("restore before record", r"savestate: recording state loaded frame \d+", pos, frames=180)
    else:
        pos = lab.log_pos()
        lab.close_menu()
        lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)

    emu.hold(0, stick=LEFT)
    emu.advance(90)
    emu.neutral(0)

    # Set CPU Mode = Playback, Start Paused = On, Takeover = None.
    rec_menu_open(lab)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Playback"])
    lab.down(6)  # Start Paused (row 9)
    lab.set_choice(SET_REC + REC["startPaused"], 1)
    lab.down(1)  # Playback Takeover (row 10)
    lab.set_choice(SET_REC + REC["takeover"], TAKEOVER["None"])
    pos_play = lab.log_pos()
    lab.close_menu()
    lab.wait_log("playback restore", r"savestate: recording state loaded frame \d+", pos_play, frames=180)
    emu.advance(6)
    st_held0 = rec.status()
    res.note(f"after Restore with Start Paused=On: t={st_held0['t'][1]}, holding={bool(st_held0['flags'] & FLAG_HOLDING)}")
    if not (st_held0["flags"] & FLAG_HOLDING):
        raise RecError("expected Start Paused to hold after Restore Positions")

    # Release the hold with a live button (X), advance mid-run, quick save (D-pad right).
    emu.press(0, ["X"], frames=3, after=15)
    st_mid = rec.status()
    res.note(f"mid-run before quick save: t={st_mid['t'][1]}, holding={bool(st_mid['flags'] & FLAG_HOLDING)}")
    pos_qs = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("quick save mid-run", r"savestate: saved frame \d+", pos_qs, frames=120)
    emu.advance(35)  # clear 30f save lockout

    # Restore Positions -> sets s_holding = true at frame 0.
    rec_menu_open(lab)
    pos_rst = lab.log_pos()
    lab.tap(["A"])  # Restore Positions
    lab.close_menu()
    lab.wait_log("restore into hold", r"savestate: recording state loaded frame \d+", pos_rst, frames=180)
    emu.advance(6)
    st_before_ql = rec.status()
    res.note(f"re-held before D-pad left: t={st_before_ql['t'][1]}, holding={bool(st_before_ql['flags'] & FLAG_HOLDING)}")

    # Quick load with D-pad left (not a live button) and step 30 frames with no live input.
    def press_dleft(i, _rows):
        if i < 2:
            emu.hold(0, ["DLEFT"])
        else:
            emu.neutral(0)

    pos_ql = lab.log_pos()
    rows = r.step(30, press_dleft)
    emu.neutral(0)
    ql_log = log_since(emu, pos_ql)
    back_m = re.search(r"record: quick state loaded: back to frame (\d+)", ql_log)
    back_t = int(back_m.group(1)) if back_m else -1
    after_rows = rows[6:]
    ts = [x["rec"]["t"][1] for x in after_rows]
    holds = [bool(x["rec"]["flags"] & FLAG_HOLDING) for x in after_rows]
    res.note(f"after quick load from hold: back_t={back_t}, t={ts[0]}->{ts[-1]}, any_holding={any(holds)}")
    res.row("quick load mid-run state while Start Paused is holding",
            "back_t > 0, holding False, t advances >= 15f",
            f"back_t={back_t}, t {ts[0]}->{ts[-1]} (+{ts[-1] - ts[0]}), holding={holds[-1]}",
            back_t > 0 and not any(holds) and ts[-1] >= ts[0] + 15)

    # Reverse: Restore Positions (held at frame 0), quick save while held,
    # release and advance mid-run, then quick load -> must restore holding=True at t=0.
    emu.h.call("resume")
    rec_menu_open(lab)
    pos_rst2 = lab.log_pos()
    lab.tap(["A"])  # Restore Positions
    lab.close_menu()
    lab.wait_log("restore for reverse test", r"savestate: recording state loaded frame \d+", pos_rst2, frames=180)
    emu.advance(6)
    pos_qs2 = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("quick save while held", r"savestate: saved frame \d+", pos_qs2, frames=120)
    emu.advance(35)  # clear lockout while held

    emu.press(0, ["X"], frames=3, after=18)
    st_mid2 = rec.status()
    res.note(f"reverse mid-run before quick load: t={st_mid2['t'][1]}, holding={bool(st_mid2['flags'] & FLAG_HOLDING)}")
    pos_ql2 = lab.log_pos()
    rows2 = r.step(25, press_dleft)
    emu.neutral(0)
    back2_m = re.search(r"record: quick state loaded: back to frame (\d+)", log_since(emu, pos_ql2))
    back2_t = int(back2_m.group(1)) if back2_m else -1
    after2 = rows2[6:]
    ts2 = [x["rec"]["t"][1] for x in after2]
    holds2 = [bool(x["rec"]["flags"] & FLAG_HOLDING) for x in after2]
    res.note(f"after quick load of held state: back_t={back2_t}, t={sorted(set(ts2))}, all_holding={all(holds2)}")
    res.row("quick load held state from mid-run restores hold",
            "back_t == 0, holding True, t stays 0",
            f"back_t={back2_t}, t={sorted(set(ts2))}, holding={all(holds2)}",
            back2_t == 0 and all(holds2) and set(ts2) == {0})

    # Clean up Recording settings (release the restored Start Paused hold first).
    emu.h.call("resume")
    emu.press(0, ["X"], frames=3, after=5)
    rec_menu_open(lab)
    lab.down(9)  # Start Paused
    lab.set_choice(SET_REC + REC["startPaused"], 0)
    lab.down(1)  # Playback Takeover
    lab.set_choice(SET_REC + REC["takeover"], TAKEOVER["HMN"])
    lab.up(7)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Off"])
    lab.close_menu()
    emu.advance(10)
    return res


def s_ctrl_orig_persist(r):
    """Starting a new match with CPU Options > Controlled By = Port 1 already
    persisted in settings, then setting Controlled By = None, Saving Positions,
    and toggling CPU Mode Control -> Off must restore the CPU's real original
    controller number (0), not Port 1."""
    res = Result("CPU controller orig across match start")
    emu = r.emu
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)
    ctrl_baseline = player_controllers(emu)[1]

    # Set Controlled By = Port 1, leave the match, and enter a new match
    # (settings persist across matches).
    set_controlled_by_menu(lab, 1)
    emu.advance(10)
    if lab.setting(SET_CONTROLLED_BY) != 1:
        raise RecError(f"Controlled By is {lab.setting(SET_CONTROLLED_BY)}, expected 1")

    nav.finish_training(emu)
    emu.advance(60)
    nav.start_training_match(emu, r.hmn, r.cpu, r.stage, seed_rng=True)
    emu.advance(60)
    r.refresh()
    lab = LT.Lab(emu, [])
    lab.find_settings()
    C.set_training_option(emu, "Stop")
    emu.advance(10)

    cb_persisted = lab.setting(SET_CONTROLLED_BY)
    ctrl_start = player_controllers(emu)
    res.note(f"new match start: Controlled By={cb_persisted}, controllers={ctrl_start} (baseline CPU={ctrl_baseline})")

    # Turn Controlled By back to None.
    set_controlled_by_menu(lab, 0)
    emu.advance(15)
    ctrl_none = player_controllers(emu)
    res.note(f"after Controlled By=None: controllers={ctrl_none}")

    # Save Positions, set CPU Mode = Control, then CPU Mode = Off (no savestate load).
    rec_menu_open(lab)
    pos = lab.log_pos()
    lab.tap(["A"])  # Save Positions
    lab.wait_log("Save Positions queued", r"record: positions to save", pos)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Control"])
    lab.close_menu()
    lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)
    emu.advance(15)
    ctrl_control = player_controllers(emu)
    res.note(f"in CPU Mode=Control: controllers={ctrl_control}")

    rec_menu_open(lab)
    lab.down(3)  # CPU Mode
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Off"])
    lab.close_menu()
    emu.advance(15)
    ctrl_off = player_controllers(emu)
    res.note(f"after CPU Mode=Off: controllers={ctrl_off}")

    res.row("CPU m_controllerNo after Controlled By=None, CPU Mode Control->Off",
            f"CPU={ctrl_baseline} (not Port 1)",
            f"start {ctrl_start} -> none {ctrl_none} -> control {ctrl_control} -> off {ctrl_off}",
            cb_persisted == 1 and ctrl_off[1] == ctrl_baseline)

    # Switch to Training / Brawl CPU = Control and hold a direction on P1 for 60 frames.
    set_cpu_control_menu(lab, 0)
    C.set_training_option(emu, "Control")
    emu.advance(15)
    p1_x0 = fl(r.tr.row(P1)["x"])
    cpu_x0 = fl(r.tr.row(CPU)["x"])
    stick_dir = LEFT if cpu_x0 > 0 else RIGHT
    emu.hold(0, stick=stick_dir)
    cpu_statuses = []
    for _ in range(60):
        emu.advance(1)
        cpu_statuses.append(sname(r.tr.row(CPU)["status"]))
    emu.neutral(0)
    emu.advance(5)
    dx_p1 = fl(r.tr.row(P1)["x"]) - p1_x0
    dx_cpu = fl(r.tr.row(CPU)["x"]) - cpu_x0
    res.note(f"60f hold (Brawl CPU=Control): P1 dx={dx_p1:+.2f}, CPU dx={dx_cpu:+.2f}, statuses={sorted(set(cpu_statuses))}")
    res.row("60f stick hold on P1: CPU must stay still",
            "CPU |dx| < 0.01, status Wait only",
            f"CPU dx={dx_cpu:+.2f}, statuses {sorted(set(cpu_statuses))}, P1 dx={dx_p1:+.2f}",
            abs(dx_cpu) < 0.01 and set(cpu_statuses) == {"Wait"} and abs(dx_p1) > 10.0)

    # Clean up for subsequent scenarios (e.g. s_finish).
    C.set_training_option(emu, "Stop")
    set_cpu_control_menu(lab, 1)
    emu.advance(10)
    return res


def _cpu_type(r):
    en = r.tr.g.entry(CPU)
    ai = r.tr.g.inputs(en)[0]
    return r.emu.u8(ai + 0x113)


def _set_rec_cpu_state(lab, rec, cpu_mode, takeover, slot, cpu_counter=None, restore=False):
    """Set Recording's CPU Mode, CPU Record Slot, Playback Takeover (and
    optionally CPU Counter) through the lab menu; if `restore` is True, also
    trigger Restore Positions before closing."""
    rec_menu_open(lab)
    # Row 0: Restore Positions, 1: HMN Mode, 2: HMN Slot, 3: CPU Mode, 4: CPU Slot.
    # Force CPU Mode = Off first so Mirrored Playback (row 5) is disabled and
    # Loop / Auto Restore (rows 7, 8) are enabled, making row offsets fixed.
    lab.down(3)
    if rec.get("cpuMode") != CPU_MODE["Off"]:
        lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Off"])
    need_slot = rec.get("cpuSlot") != slot
    need_counter = cpu_counter is not None and rec.get("cpuCounter") != cpu_counter
    need_take = rec.get("takeover") != takeover
    row = 3
    if need_slot:
        lab.down(1)
        row = 4
        lab.set_choice(SET_REC + REC["cpuSlot"], slot)
    if need_counter:
        lab.down(5 - row)  # Row 6 (index 5 with Mirrored skipped)
        row = 5
        lab.set_choice(SET_REC + REC["cpuCounter"], cpu_counter)
    if need_take:
        lab.down(9 - row)  # Row 10 (index 9 with Mirrored skipped)
        row = 9
        lab.set_choice(SET_REC + REC["takeover"], takeover)
    if row != 3:
        lab.up(row - 3)
        row = 3
    pos = lab.log_pos()
    if cpu_mode != CPU_MODE["Off"]:
        lab.set_choice(SET_REC + REC["cpuMode"], cpu_mode)
    if restore:
        lab.up(3)  # Row 0: Restore Positions
        lab.tap(["A"])
    lab.close_menu()
    if restore or need_slot or cpu_mode in (CPU_MODE["Playback"], CPU_MODE["Re-Record"]):
        lab.wait_log("recording state load", r"savestate: recording state loaded frame \d+", pos, frames=180)


def _wait_standing(r, frames=140):
    r.emu.wait_for("both fighters Wait",
                   lambda: sname(r.tr.row(P1)["status"]) == "Wait" and sname(r.tr.row(CPU)["status"]) == "Wait",
                   frames, every=2)


def _hold_60(r, port, towards_center_of):
    """Hold stick on `port` for 60 exact game frames toward the center of FD
    relative to `towards_center_of` (P1 or CPU); return (dx_p1, dx_cpu,
    cpu_statuses, p1_statuses)."""
    emu = r.emu
    emu.h.call("pause")
    try:
        p1_x0 = fl(r.tr.row(P1)["x"])
        cpu_x0 = fl(r.tr.row(CPU)["x"])
        ref_x = p1_x0 if towards_center_of == P1 else cpu_x0
        stick_dir = LEFT if ref_x > 0 else RIGHT
        emu.hold(port, stick=stick_dir)
        w = emu.watch(lambda _i=None: (r.tr.row(CPU)["status"], r.tr.row(P1)["status"]))
        rows = emu.trace(60, w)
        emu.neutral(port)
    finally:
        emu.h.call("resume")
    emu.advance(4)
    cpu_st = [sname(c) for c, _ in rows]
    p1_st = [sname(p) for _, p in rows]
    return (fl(r.tr.row(P1)["x"]) - p1_x0, fl(r.tr.row(CPU)["x"]) - cpu_x0,
            sorted(set(cpu_st)), sorted(set(p1_st)))


def s_cpu_ctrl_matrix(r):
    """Every combination of Recording CPU Mode at save x at load (Off, Control,
    Record, Re-Record, Playback without takeover, Playback with takeover) across
    Recording slot (Restore Positions), Quick slot (D-pad left), and Frame
    Decrement (rewind) must reconcile m_controllerNo and record's swap
    bookkeeping to the current CPU Mode, and 60 frames of P1 stick input must
    move the expected fighter only."""
    res = Result("CPU controller mode matrix")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)
    if lab.setting(SET_CONTROLLED_BY) != 0:
        set_controlled_by_menu(lab, 0)
        emu.advance(10)
    ctrl_orig = player_controllers(emu)[1]
    ctrl_hmn = player_controllers(emu)[0]

    # Save positions fresh (if needed) and record 90 frames of neutral into
    # CPU Slot 1 so Playback (no takeover) keeps CPU standing for 60+ frames.
    rec_menu_open(lab)
    if not (rec.status()["flags"] & FLAG_SAVED):
        pos = lab.log_pos()
        lab.tap(["A"])  # Save Positions
        lab.wait_log("Save Positions queued", r"record: positions to save", pos)
        lab.close_menu()
        lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)
        emu.advance(10)
    else:
        lab.close_menu()

    _set_rec_cpu_state(lab, rec, CPU_MODE["Record"], TAKEOVER["HMN"], 1, cpu_counter=0)
    emu.advance(95)
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1, cpu_counter=0)
    emu.advance(10)
    _, end1 = rec.slot_end(1, 0)
    res.note(f"baseline ctrl P1={ctrl_hmn} CPU={ctrl_orig}; neutral CPU slot 1 end={end1}")
    if end1 < 80:
        raise RecError(f"neutral CPU slot 1 too short ({end1})")

    # (label, cpu_mode, takeover, slot, expect_swap)
    # Slot 1 holds 95f of neutral (Playback); Slot 2 is written by Record;
    # Slot 3 is written by Re-Record loads; Slot 4 stays neutral for Re-Record saves/rewind.
    modes = [
        ("Off", CPU_MODE["Off"], TAKEOVER["HMN"], 1, False),
        ("Control", CPU_MODE["Control"], TAKEOVER["HMN"], 1, True),
        ("Record", CPU_MODE["Record"], TAKEOVER["HMN"], 2, True),
        ("Re-Record", CPU_MODE["Re-Record"], TAKEOVER["HMN"], 3, True),
        ("Play(HMN)", CPU_MODE["Playback"], TAKEOVER["HMN"], 1, False),
        ("Play(CPU)", CPU_MODE["Playback"], TAKEOVER["CPU"], 1, True),
    ]

    rec_fails = []
    quick_fails = []
    frame0_fails = []

    def check_after_load(kind, s_name, l_name, exp_swap):
        exp_ctrl = ctrl_hmn if exp_swap else ctrl_orig
        ctrl_now = player_controllers(emu)[1]
        swapped_now = bool(rec.status()["flags"] & FLAG_SWAPPED)
        if not (ctrl_now == exp_ctrl and swapped_now == exp_swap):
            frame0_fails.append(f"{kind} {s_name}->{l_name}: f0 ctrl={ctrl_now}(exp {exp_ctrl}), "
                                f"swapped={swapped_now}(exp {exp_swap})")
        _wait_standing(r)
        ctrl_settle = player_controllers(emu)[1]
        swapped_settle = bool(rec.status()["flags"] & FLAG_SWAPPED)
        dx_p1, dx_cpu, cpu_st, p1_st = _hold_60(r, 0, CPU if exp_swap else P1)
        if exp_swap:
            # P1 is held neutral (stays Wait; allow minor pushbox nudge when Fox runs past).
            move_ok = abs(dx_cpu) > 10.0 and set(p1_st) == {"Wait"}
        else:
            move_ok = set(cpu_st) == {"Wait"} and abs(dx_p1) > 10.0
        ok = ctrl_settle == exp_ctrl and swapped_settle == exp_swap and move_ok
        if not ok:
            entry = (f"{s_name}->{l_name}: ctrl={ctrl_settle}(exp {exp_ctrl}) "
                     f"swapped={swapped_settle}(exp {exp_swap}) dx_p1={dx_p1:+.2f} dx_cpu={dx_cpu:+.2f} "
                     f"cpu={cpu_st[:3]} p1={p1_st[:3]}")
            if kind == "rec":
                rec_fails.append(entry)
            else:
                quick_fails.append(entry)
        if l_name == "Re-Record":
            rec.set(manageSide=1, manageSlot=2)
            r.run_request("delete-slot")
            emu.h.call("resume")

    for s_name, s_mode, s_take, s_slot, s_swap in modes:
        # Restore clean standing positions, set save mode, and save BOTH
        # recording slot (Y+Z) and quick slot (D-pad right).
        save_slot = 4 if s_name == "Re-Record" else s_slot
        _set_rec_cpu_state(lab, rec, s_mode, s_take, save_slot, cpu_counter=0, restore=True)
        _wait_standing(r)
        emu.advance(10)

        rec_menu_open(lab)
        pos_rs = lab.log_pos()
        resave_positions_menu(lab, rec)
        lab.wait_log(f"Re-Save ({s_name})", r"record: positions to save \(2\)", pos_rs)
        lab.close_menu()
        lab.wait_log(f"Re-Save done ({s_name})", r"record: positions saved again \(savestate\)", pos_rs, frames=180)
        emu.advance(10)

        pos_qs = lab.log_pos()
        emu.hold(0, ["DRIGHT"])
        emu.advance(14)
        emu.neutral(0)
        lab.wait_log(f"Quick save ({s_name})", r"savestate: saved frame \d+", pos_qs, frames=120)
        emu.advance(35)  # clear 30f save lockout

        for l_name, l_mode, l_take, l_slot, l_swap in modes:
            # 1) Recording slot load (set mode + Restore Positions in one menu visit).
            _set_rec_cpu_state(lab, rec, l_mode, l_take, l_slot, cpu_counter=0, restore=True)
            check_after_load("rec", s_name, l_name, l_swap)

            # 2) Quick slot load (D-pad left).
            pos_ql = lab.log_pos()
            emu.press(0, ["DLEFT"], frames=2, after=0)
            lab.wait_log(f"Quick load {s_name}->{l_name}", r"savestate: loaded frame \d+", pos_ql, frames=120)
            check_after_load("quick", s_name, l_name, l_swap)

    res.note(f"frame0 mismatches ({len(frame0_fails)}): {frame0_fails[:8]}")
    res.note(f"rec load failures ({len(rec_fails)}): {rec_fails[:8]}")
    res.note(f"quick load failures ({len(quick_fails)}): {quick_fails[:8]}")
    res.row("36 combinations (6x6) Restore Positions: byte, swapped flag, 60f stick",
            "0 failures", f"{len(rec_fails)} failures: {rec_fails[:3]}", not rec_fails)
    res.row("36 combinations (6x6) Quick Load: byte, swapped flag, 60f stick",
            "0 failures", f"{len(quick_fails)} failures: {quick_fails[:3]}", not quick_fails)
    res.row("immediate post-load (frame 0) byte & swapped agree across all 72 loads",
            "0 mismatches", f"{len(frame0_fails)} mismatches: {frame0_fails[:3]}", not frame0_fails)

    # 3) Rewind's load (Frame Decrement): save in Off -> Re-Record & Play(CPU),
    # and save in Control -> Play(HMN).
    rewind_fails = []
    emu.h.call("write_mem", addr=lab.settings + SET_DECREMENT, hex=f"{DECREMENT_X:02x}")
    for s_name, s_mode, l_name, l_mode, l_take, l_slot, exp_swap in (
        ("Off", CPU_MODE["Off"], "Re-Record", CPU_MODE["Re-Record"], TAKEOVER["HMN"], 4, True),
        ("Off", CPU_MODE["Off"], "Play(CPU)", CPU_MODE["Playback"], TAKEOVER["CPU"], 1, True),
        ("Control", CPU_MODE["Control"], "Play(HMN)", CPU_MODE["Playback"], TAKEOVER["HMN"], 1, False),
    ):
        _set_rec_cpu_state(lab, rec, s_mode, TAKEOVER["HMN"], 1, cpu_counter=0, restore=True)
        _wait_standing(r)
        emu.advance(10)
        rec_menu_open(lab)
        pos_rs = lab.log_pos()
        resave_positions_menu(lab, rec)
        lab.wait_log("Re-Save for rewind", r"record: positions to save \(2\)", pos_rs)
        lab.close_menu()
        lab.wait_log("Re-Save done for rewind", r"record: positions saved again \(savestate\)", pos_rs, frames=180)
        emu.advance(10)

        _set_rec_cpu_state(lab, rec, l_mode, l_take, l_slot, cpu_counter=0)
        emu.advance(25)
        emu.h.call("write_mem", addr=lab.settings + SET_FRAME_ADVANCE, hex="01")
        emu.advance(5)
        pos_rw = lab.log_pos()
        emu.press(0, ["X"], frames=2, after=5)
        lab.wait_log(f"rewind {s_name}->{l_name}", r"record: rewound to frame \d+", pos_rw, frames=240)
        ctrl_rw = player_controllers(emu)[1]
        swapped_rw = bool(rec.status()["flags"] & FLAG_SWAPPED)
        emu.h.call("write_mem", addr=lab.settings + SET_FRAME_ADVANCE, hex="00")
        _wait_standing(r)
        dx_p1, dx_cpu, cpu_st, p1_st = _hold_60(r, 0, CPU if exp_swap else P1)
        exp_ctrl = ctrl_hmn if exp_swap else ctrl_orig
        move_ok = (abs(dx_cpu) > 10.0 and set(p1_st) == {"Wait"}) if exp_swap else (set(cpu_st) == {"Wait"} and abs(dx_p1) > 10.0)
        if not (ctrl_rw == exp_ctrl and swapped_rw == exp_swap and move_ok):
            rewind_fails.append(f"{s_name}->{l_name}: ctrl={ctrl_rw}(exp {exp_ctrl}) "
                                f"swapped={swapped_rw}(exp {exp_swap}) dx_p1={dx_p1:+.2f} dx_cpu={dx_cpu:+.2f} "
                                f"cpu={cpu_st[:3]} p1={p1_st[:3]}")
    emu.h.call("write_mem", addr=lab.settings + SET_FRAME_ADVANCE, hex="00")
    emu.h.call("write_mem", addr=lab.settings + SET_DECREMENT, hex="00")
    res.row("Rewind (Frame Decrement) reconciles controller across Off->Re-Record/Play(CPU) & Control->Play(HMN)",
            "0 failures", f"{len(rewind_fails)} failures: {rewind_fails}", not rewind_fails)

    # Clean up Recording settings.
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1, cpu_counter=1)
    emu.advance(10)
    return res


def s_controlled_by_record(r):
    """CPU Options > Controlled By and Recording's CPU Mode both write the CPU's
    controller byte. Controlled By owns the
    CPU's controller while active and Recording's CPU Mode yields cleanly;
    switching either off in either order and loading savestates in between must
    restore cleanly without driving both fighters from Port 1 or leaking the
    controller byte."""
    res = Result("Controlled By vs Recording CPU Mode")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)
    if lab.setting(SET_CONTROLLED_BY) != 0:
        set_controlled_by_menu(lab, 0)
        emu.advance(10)

    ctrl_hmn, ctrl_orig = player_controllers(emu)
    type_orig = _cpu_type(r)
    res.note(f"baseline: ctrl=({ctrl_hmn},{ctrl_orig}), cpuType={type_orig}")

    # Ensure positions are saved so CPU Mode is enabled.
    rec_menu_open(lab)
    if not (rec.status()["flags"] & FLAG_SAVED):
        pos = lab.log_pos()
        lab.tap(["A"])
        lab.wait_log("Save Positions", r"record: positions to save", pos)
        lab.close_menu()
        lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)
        emu.advance(10)
    else:
        lab.close_menu()

    def verify_cb_port2(label):
        _wait_standing(r)
        ctrl = player_controllers(emu)[1]
        ctype = _cpu_type(r)
        swapped = bool(rec.status()["flags"] & FLAG_SWAPPED)
        patch = emu.u32(PATCH_ADDR)
        p1_dx1, cpu_dx1, cpu_st1, p1_st1 = _hold_60(r, 0, P1)
        _wait_standing(r)
        p1_dx2, cpu_dx2, cpu_st2, p1_st2 = _hold_60(r, 1, CPU)
        ok = (ctrl == 2 and ctype == 25 and not swapped and patch == PATCH_SKIP
              and abs(p1_dx1) > 10.0 and set(cpu_st1) == {"Wait"}
              and set(p1_st2) == {"Wait"} and abs(cpu_dx2) > 10.0)
        res.row(label,
                f"ctrl=2, type=25, swapped=False, patch={PATCH_SKIP:#x}; P1->P1, P2->CPU",
                f"ctrl={ctrl}, type={ctype}, swapped={swapped}, patch={patch:#x}; "
                f"P1_hold({p1_dx1:+.1f},{cpu_dx1:+.1f},cpu={cpu_st1[:2]}) "
                f"P2_hold({p1_dx2:+.1f},{cpu_dx2:+.1f},p1={p1_st2[:2]})",
                ok)

    def verify_rec_control(label):
        _wait_standing(r)
        ctrl = player_controllers(emu)[1]
        ctype = _cpu_type(r)
        swapped = bool(rec.status()["flags"] & FLAG_SWAPPED)
        patch = emu.u32(PATCH_ADDR)
        p1_dx, cpu_dx, cpu_st, p1_st = _hold_60(r, 0, CPU)
        ok = (ctrl == ctrl_hmn and ctype == type_orig and swapped and patch == PATCH_SKIP
              and set(p1_st) == {"Wait"} and abs(cpu_dx) > 10.0)
        res.row(label,
                f"ctrl={ctrl_hmn}, type={type_orig}, swapped=True, patch={PATCH_SKIP:#x}; P1->CPU",
                f"ctrl={ctrl}, type={ctype}, swapped={swapped}, patch={patch:#x}; "
                f"P1_hold({p1_dx:+.1f},{cpu_dx:+.1f},p1={p1_st[:2]})",
                ok)

    def verify_idle(label):
        _wait_standing(r)
        ctrl = player_controllers(emu)[1]
        ctype = _cpu_type(r)
        swapped = bool(rec.status()["flags"] & FLAG_SWAPPED)
        patch = emu.u32(PATCH_ADDR)
        p1_dx, cpu_dx, cpu_st, p1_st = _hold_60(r, 0, P1)
        ok = (ctrl == ctrl_orig and ctype == type_orig and not swapped and patch == PATCH_ORIG
              and abs(p1_dx) > 10.0 and set(cpu_st) == {"Wait"})
        res.row(label,
                f"ctrl={ctrl_orig}, type={type_orig}, swapped=False, patch={PATCH_ORIG:#x}; P1->P1",
                f"ctrl={ctrl}, type={ctype}, swapped={swapped}, patch={patch:#x}; "
                f"P1_hold({p1_dx:+.1f},{cpu_dx:+.1f},cpu={cpu_st[:2]})",
                ok)

    def restore_pos():
        rec_menu_open(lab)
        pos = lab.log_pos()
        lab.tap(["A"])
        lab.close_menu()
        lab.wait_log("restore", r"savestate: recording state loaded frame \d+", pos, frames=180)
        emu.advance(10)

    # 1) Controlled By = Port 2 ON first, then CPU Mode = Control ON (must also
    # show the HUD refusal message once, not every frame).
    restore_pos()
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    pos_msg = lab.log_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1)
    emu.advance(5)
    msg_shown = bool(rec.status()["flags"] & FLAG_MESSAGE)
    msg_log = log_since(emu, pos_msg)
    msg_text_ok = "record: CPU Mode is off while Port Control is on" in msg_log
    emu.advance(165)  # past kMessageFrames (150)
    msg_cleared = not bool(rec.status()["flags"] & FLAG_MESSAGE)
    emu.advance(20)
    msg_stayed_clear = not bool(rec.status()["flags"] & FLAG_MESSAGE)
    res.row("HUD refusal shown once when choosing CPU Mode with Controlled By on",
            "FLAG_MESSAGE set then clears after 150f and stays clear",
            f"shown={msg_shown}, log_ok={msg_text_ok}, cleared={msg_cleared}, stayed={msg_stayed_clear}",
            msg_shown and msg_text_ok and msg_cleared and msg_stayed_clear)
    verify_cb_port2("CB=Port 2 first, then CPU Mode=Control: CB owns byte")

    # 2) Switch Controlled By = None FIRST (CPU Mode=Control still ON) -> patch
    # word must stay patched right when CB goes to None (before menu even closes)
    # and Control takes over cleanly.
    restore_pos()
    patch_in_menu = []
    set_controlled_by_menu(lab, 0, before_close=lambda: patch_in_menu.append(emu.u32(PATCH_ADDR)))
    emu.advance(10)
    res.row("0x80962d0c stays patched right after CB->None while CPU Mode=Control stays on",
            f"{PATCH_SKIP:#x} in menu and after close",
            f"in_menu={patch_in_menu[0]:#x}, after={emu.u32(PATCH_ADDR):#x}",
            patch_in_menu[0] == PATCH_SKIP and emu.u32(PATCH_ADDR) == PATCH_SKIP)
    verify_rec_control("CB=None first (CPU Mode=Control stays): Control takes over cleanly")

    # Then switch CPU Mode = Off -> baseline restored.
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    verify_idle("then CPU Mode=Off: baseline restored")

    # 3) Reverse order: CPU Mode = Control ON first, then Controlled By = Port 2 ON.
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    verify_cb_port2("CPU Mode=Control first, then CB=Port 2: CB owns byte")

    # 4) Switch CPU Mode = Off FIRST (CB=Port 2 still ON) -> CB stays active cleanly.
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    verify_cb_port2("CPU Mode=Off first (CB=Port 2 stays): CB stays active")

    # Then switch Controlled By = None -> baseline restored.
    restore_pos()
    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    verify_idle("then CB=None: baseline restored")

    # 5) Load in between:
    # (a) Save in CPU Mode=Control (CB=None), turn CB=Port 2 ON, load both slots,
    # then turn CB=None (Control must resume), then CPU Mode=Off (orig must return).
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1)
    emu.advance(15)
    rec_menu_open(lab)
    pos_rs = lab.log_pos()
    resave_positions_menu(lab, rec)
    lab.wait_log("Re-Save in Control", r"record: positions to save \(2\)", pos_rs)
    lab.close_menu()
    lab.wait_log("Re-Save in Control done", r"record: positions saved again \(savestate\)", pos_rs, frames=180)
    emu.advance(10)
    pos_qs = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("Quick save in Control", r"savestate: saved frame \d+", pos_qs, frames=120)
    emu.advance(35)

    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    restore_pos()
    verify_cb_port2("load in between (save Control -> CB=Port 2 -> Restore): CB owns byte")
    pos_ql = lab.log_pos()
    emu.press(0, ["DLEFT"], frames=1, after=0)
    lab.wait_log("Quick load Control->CB", r"savestate: loaded frame \d+", pos_ql, frames=120)
    emu.advance(10)
    verify_cb_port2("load in between (save Control -> CB=Port 2 -> Quick Load): CB owns byte")

    restore_pos()
    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    verify_rec_control("after load, CB=None -> Control resumes cleanly")
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    verify_idle("after load, CB=None then CPU Mode=Off -> orig restored (no port leak)")

    # (b) Save in CB=Port 2 (CPU Mode=Off), turn CB=None & CPU Mode=Control, load both slots.
    restore_pos()
    set_controlled_by_menu(lab, 2)
    emu.advance(15)
    rec_menu_open(lab)
    pos_rs2 = lab.log_pos()
    resave_positions_menu(lab, rec)
    lab.wait_log("Re-Save in CB", r"record: positions to save \(2\)", pos_rs2)
    lab.close_menu()
    lab.wait_log("Re-Save in CB done", r"record: positions saved again \(savestate\)", pos_rs2, frames=180)
    emu.advance(10)
    pos_qs2 = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("Quick save in CB", r"savestate: saved frame \d+", pos_qs2, frames=120)
    emu.advance(35)

    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    restore_pos()
    verify_rec_control("load in between (save CB=Port 2 -> CB=None + Control -> Restore): Control owns")
    pos_ql2 = lab.log_pos()
    emu.press(0, ["DLEFT"], frames=1, after=0)
    lab.wait_log("Quick load CB->Control", r"savestate: loaded frame \d+", pos_ql2, frames=120)
    emu.advance(10)
    verify_rec_control("load in between (save CB=Port 2 -> CB=None + Control -> Quick Load): Control owns")

    # (c) Controlled By = Port 1 + CPU Mode = Control (both orders): CB owns
    # (Port 1 drives both), then CB=None hands cleanly to Control (Port 1 drives
    # CPU only), then CPU Mode=Off restores baseline.
    restore_pos()
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    set_controlled_by_menu(lab, 1)
    emu.advance(10)
    _wait_standing(r)
    ctrl_p1 = player_controllers(emu)[1]
    type_p1 = _cpu_type(r)
    swap_p1 = bool(rec.status()["flags"] & FLAG_SWAPPED)
    dx_p1_both, dx_cpu_both, _, _ = _hold_60(r, 0, P1)
    res.row("CPU Mode=Control then CB=Port 1: CB owns (Port 1 drives both)",
            "ctrl=1, type=25, swapped=False; both move",
            f"ctrl={ctrl_p1}, type={type_p1}, swapped={swap_p1}; P1={dx_p1_both:+.1f} CPU={dx_cpu_both:+.1f}",
            ctrl_p1 == 1 and type_p1 == 25 and not swap_p1 and abs(dx_p1_both) > 10.0 and abs(dx_cpu_both) > 10.0)
    restore_pos()
    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    verify_rec_control("CB=Port 1 -> None while Control on: Control resumes (CPU only)")

    # 6) Reset All Settings while Controlled By = Port 2 + CPU Mode = Control are
    # active: port, CPU type and 0x80962d0c must reconcile immediately (menu open).
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    lab.open_menu()
    lab.main_to(LT.MAIN_RESET)
    lab.tap(["A"])  # "A: Confirm"
    lab.tap(["A"])  # resetLabSettings()
    emu.advance(4)
    patch_reset_open = emu.u32(PATCH_ADDR)
    ctrl_reset_open = player_controllers(emu)[1]
    type_reset_open = _cpu_type(r)
    lab.close_menu()
    emu.advance(10)
    patch_reset_closed = emu.u32(PATCH_ADDR)
    ctrl_reset_closed = player_controllers(emu)[1]
    type_reset_closed = _cpu_type(r)
    res.row("Reset All Settings restores 0x80962d0c, port and cpuType immediately",
            f"patch={PATCH_ORIG:#x}, ctrl={ctrl_orig}, type={type_orig} (open & closed)",
            f"open=({patch_reset_open:#x},{ctrl_reset_open},{type_reset_open}) "
            f"closed=({patch_reset_closed:#x},{ctrl_reset_closed},{type_reset_closed})",
            patch_reset_open == PATCH_ORIG and patch_reset_closed == PATCH_ORIG
            and ctrl_reset_open == ctrl_orig and ctrl_reset_closed == ctrl_orig
            and type_reset_open == type_orig and type_reset_closed == type_orig)

    # 7) Match exit while patch is active: 0x80962d0c must read 0x8B1BFFFF on CSS.
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    patch_before_exit = emu.u32(PATCH_ADDR)
    nav.finish_training(emu)
    emu.advance(30)
    patch_on_css = emu.u32(PATCH_ADDR)
    res.row("0x80962d0c restored to 0x8B1BFFFF on CSS after match ends",
            f"in-match={PATCH_SKIP:#x}, on-CSS={PATCH_ORIG:#x}",
            f"in-match={patch_before_exit:#x}, on-CSS={patch_on_css:#x}",
            patch_before_exit == PATCH_SKIP and patch_on_css == PATCH_ORIG)
    return res


def s_ctrl_one_pad_leak(r):
    """On a ONE-pad run:
    1) Save positions with CPU Mode = Control, turn Controlled By = Port 2 on,
       load (Restore Positions and Quick Load), then switch both off -> CPU's
       controller byte must return to 0 (not Port 1), cpuType to original,
       0x80962d0c to 0x8B1BFFFF, and 60f P1 stick hold leaves CPU in Wait.
    2) Same path when Brain::tools() has never run in the match before load
       (CPU Control persisted as Training across match start, then switched to
       Lab + Control, saved, then Controlled By = Port 2 + Restore in one menu
       visit before closing): must still resolve live aiInput and restore 0.
    3) Reset All Settings with Controlled By = Port 2 on restores 0x80962d0c
       and port 0 immediately while the menu is still open."""
    res = Result("one-pad controller baseline & leak")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = LT.Lab(emu, [])
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)
    if lab.setting(SET_CONTROLLED_BY) != 0:
        set_controlled_by_menu(lab, 0)
        emu.advance(10)

    ctrl_hmn, ctrl_orig = player_controllers(emu)
    type_orig = _cpu_type(r)
    res.note(f"one-pad baseline: ctrl=({ctrl_hmn},{ctrl_orig}), cpuType={type_orig}")

    # Ensure positions exist, switch to CPU Mode = Control, Re-Save + Quick Save.
    rec_menu_open(lab)
    if not (rec.status()["flags"] & FLAG_SAVED):
        pos = lab.log_pos()
        lab.tap(["A"])
        lab.wait_log("Save Positions", r"record: positions to save", pos)
        lab.close_menu()
        lab.wait_log("positions saved", r"record: positions saved \(savestate\)", pos, frames=180)
        emu.advance(10)
    else:
        lab.close_menu()

    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1, restore=True)
    _wait_standing(r)
    emu.advance(10)
    rec_menu_open(lab)
    pos_rs = lab.log_pos()
    resave_positions_menu(lab, rec)
    lab.wait_log("Re-Save in Control", r"record: positions to save \(2\)", pos_rs)
    lab.close_menu()
    lab.wait_log("Re-Save in Control done", r"record: positions saved again \(savestate\)", pos_rs, frames=180)
    emu.advance(10)
    pos_qs = lab.log_pos()
    emu.hold(0, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(0)
    lab.wait_log("Quick save in Control", r"savestate: saved frame \d+", pos_qs, frames=120)
    emu.advance(35)

    # Turn Controlled By = Port 2 on, load both slots, then turn both off.
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    rec_menu_open(lab)
    pos_rst = lab.log_pos()
    lab.tap(["A"])
    lab.close_menu()
    lab.wait_log("Restore in CB", r"savestate: recording state loaded frame \d+", pos_rst, frames=180)
    emu.advance(10)
    pos_ql = lab.log_pos()
    emu.press(0, ["DLEFT"], frames=2, after=0)
    lab.wait_log("Quick load in CB", r"savestate: loaded frame \d+", pos_ql, frames=120)
    emu.advance(10)

    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    _wait_standing(r)
    ctrl_after = player_controllers(emu)[1]
    type_after = _cpu_type(r)
    patch_after = emu.u32(PATCH_ADDR)
    p1_dx, cpu_dx, cpu_st, _ = _hold_60(r, 0, P1)
    res.row("one-pad: save Control -> CB=Port 2 -> loads -> both off: orig restored",
            f"ctrl={ctrl_orig}, type={type_orig}, patch={PATCH_ORIG:#x}; CPU Wait",
            f"ctrl={ctrl_after}, type={type_after}, patch={patch_after:#x}, "
            f"P1_dx={p1_dx:+.1f}, CPU_dx={cpu_dx:+.1f}, cpu={cpu_st[:2]}",
            ctrl_after == ctrl_orig and type_after == type_orig and patch_after == PATCH_ORIG
            and abs(p1_dx) > 10.0 and set(cpu_st) == {"Wait"})

    # 2) Fresh match with CPU Control = Training persisted so Brain::tools() has
    # never run; switch Lab + save + Control, Re-Save, then in ONE menu visit set
    # CB=Port 2 and Restore Positions before closing; then both off -> orig 0.
    set_cpu_control_menu(lab, 0)
    emu.advance(10)
    nav.finish_training(emu)
    emu.advance(60)
    nav.start_training_match(emu, r.hmn, r.cpu, r.stage, seed_rng=True)
    emu.advance(60)
    r.refresh()
    lab = LT.Lab(emu, [])
    lab.find_settings()
    C.set_training_option(emu, "Stop")
    emu.advance(10)

    # Save Positions while CPU Control is still Training (Brain::tools has never run),
    # then in ONE menu visit switch CPU Control = Lab and Recording CPU Mode = Control.
    rec_menu_open(lab)
    pos_sp = lab.log_pos()
    lab.tap(["A"])
    lab.wait_log("Save Positions (fresh)", r"record: positions to save", pos_sp)
    lab.close_menu()
    lab.wait_log("positions saved (fresh)", r"record: positions saved \(savestate\)", pos_sp, frames=180)
    emu.advance(10)

    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.up(25)
    lab.down(C.CPU_CONTROL_ROW)
    lab.set_choice(SET_CPU_CONTROL, 1)
    lab.tap(["B"])
    lab.up(12)
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.up(18)
    lab.down(3)
    lab.set_choice(SET_REC + REC["cpuMode"], CPU_MODE["Control"])
    lab.close_menu()
    emu.advance(15)

    # Re-Save in Control (m_controllerNo = 1 in savestate; Brain::tools still never ran).
    rec_menu_open(lab)
    pos_rs2 = lab.log_pos()
    resave_positions_menu(lab, rec)
    lab.wait_log("Re-Save (tools never ran)", r"record: positions to save \(2\)", pos_rs2)
    lab.close_menu()
    lab.wait_log("Re-Save done (tools never ran)", r"record: positions saved again \(savestate\)", pos_rs2, frames=180)
    emu.advance(10)

    # In ONE menu visit: set Controlled By = Port 2, then Restore Positions.
    lab.open_menu()
    lab.main_to(LT.MAIN_CPU)
    lab.tap(["A"])
    lab.emu.press(0, [], frames=50, after=4, stick=LT.UP)
    lab.down(CPU_CONTROLLED_BY_ROW)
    lab.set_choice(SET_CONTROLLED_BY, 2)
    lab.tap(["B"])
    lab.up(12)
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.up(18)
    pos_rst2 = lab.log_pos()
    lab.tap(["A"])
    lab.close_menu()
    lab.wait_log("Restore with CB (tools never ran)", r"savestate: recording state loaded frame \d+", pos_rst2,
                 frames=180)
    emu.advance(15)
    ctrl_cb2 = player_controllers(emu)[1]
    type_cb2 = _cpu_type(r)

    # Switch both off: must restore original 0, NOT leaked Port 1.
    set_controlled_by_menu(lab, 0)
    emu.advance(10)
    _set_rec_cpu_state(lab, rec, CPU_MODE["Off"], TAKEOVER["HMN"], 1)
    emu.advance(10)
    _wait_standing(r)
    ctrl_after2 = player_controllers(emu)[1]
    type_after2 = _cpu_type(r)
    patch_after2 = emu.u32(PATCH_ADDR)
    p1_dx2, cpu_dx2, cpu_st2, _ = _hold_60(r, 0, P1)
    res.row("one-pad (Brain::tools never ran before load): live resolve & no Port 1 leak",
            f"cb=(2,25), after=({ctrl_orig},{type_orig},{PATCH_ORIG:#x}), CPU Wait",
            f"cb=({ctrl_cb2},{type_cb2}), after=({ctrl_after2},{type_after2},{patch_after2:#x}), "
            f"P1_dx={p1_dx2:+.1f}, CPU_dx={cpu_dx2:+.1f}, cpu={cpu_st2[:2]}",
            ctrl_cb2 == 2 and type_cb2 == 25 and ctrl_after2 == ctrl_orig and type_after2 == type_orig
            and patch_after2 == PATCH_ORIG and abs(p1_dx2) > 10.0 and set(cpu_st2) == {"Wait"})

    # 3) Reset All Settings on 1 pad while Controlled By = Port 2 is active.
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    lab.open_menu()
    lab.main_to(LT.MAIN_RESET)
    lab.tap(["A"])
    lab.tap(["A"])
    emu.advance(4)
    patch_ro = emu.u32(PATCH_ADDR)
    ctrl_ro = player_controllers(emu)[1]
    type_ro = _cpu_type(r)
    lab.close_menu()
    emu.advance(10)
    res.row("one-pad: Reset All Settings reconciles port, type and patch while menu open",
            f"patch={PATCH_ORIG:#x}, ctrl={ctrl_orig}, type={type_orig}",
            f"patch={patch_ro:#x}, ctrl={ctrl_ro}, type={type_ro}",
            patch_ro == PATCH_ORIG and ctrl_ro == ctrl_orig and type_ro == type_orig)
    return res


class _PortLab(LT.Lab):
    """Lab menu driver targeting a non-Port-1 controller (e.g. Port 2 only)."""
    def __init__(self, emu, port):
        super().__init__(emu, [])
        self.port = port
        self._opened_rx = LT.MENU_OPENED.replace("port 1", f"port {port + 1}")

    def tap(self, buttons=(), stick=(128, 128), frames=2, after=4):
        self.emu.press(self.port, list(buttons), frames=frames, after=after, stick=stick)

    def open_menu(self, keep=False):
        pos = self.log_pos()
        if not nav.training_paused(self.emu):
            self.tap(["START"])
            rx = re.compile(self._opened_rx)
            self.emu.wait_for("the lab menu or Training's pause to open",
                              lambda: rx.search(self.emu.log_text(), pos) or nav.training_paused(self.emu), 120,
                              every=2)
        if not re.search(self._opened_rx, self.emu.log_text()[pos:]):
            self.tap(["Z"])
        m = self.wait_log("the lab menu to open", self._opened_rx, pos)
        if not keep:
            self.to_main_top(m)
        return m

    def close_menu(self):
        opened = re.findall(self._opened_rx, self.emu.synced_log_text())
        pos = self.log_pos()
        self.tap(["START"] if opened and opened[-1][0] == "Start" else ["Z"])
        self.wait_log("the lab menu to close", r"lab: menu closed", pos)


def s_ctrl_port2_only(r):
    """With P1 on Port 2 and nothing in Port 1 (PORT2_ONLY_ARGS):
    Recording CPU Mode = Control swaps CPU to Port 2 (not Port 1), reconciles
    across Restore Positions and Quick Load, yields to Controlled By = Port 2,
    resumes cleanly when Controlled By returns to None, and restores 0x80962d0c
    on match exit."""
    res = Result("Port 2 only controller handling")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    lab = _PortLab(emu, port=1)
    lab.find_settings()

    if lab.setting(SET_CPU_CONTROL) != 1:
        set_cpu_control_menu(lab, 1)
        emu.advance(10)
    if lab.setting(SET_CONTROLLED_BY) != 0:
        set_controlled_by_menu(lab, 0)
        emu.advance(10)

    ctrl_hmn, ctrl_orig = player_controllers(emu)
    type_orig = _cpu_type(r)
    res.note(f"port2-only baseline: ctrl=({ctrl_hmn},{ctrl_orig}), cpuType={type_orig}")
    res.row("Port 2 only: P1 on controller 2, CPU on controller 0",
            "(2, 0)", f"({ctrl_hmn}, {ctrl_orig})", ctrl_hmn == 2 and ctrl_orig == 0)

    # Save positions while CPU Mode = Off, and Quick Save on Port 2.
    rec_menu_open(lab)
    pos_sp = lab.log_pos()
    lab.tap(["A"])
    lab.wait_log("Save Positions (P2)", r"record: positions to save", pos_sp)
    lab.close_menu()
    lab.wait_log("positions saved (P2)", r"record: positions saved \(savestate\)", pos_sp, frames=180)
    emu.advance(10)
    pos_qs = lab.log_pos()
    emu.hold(1, ["DRIGHT"])
    emu.advance(14)
    emu.neutral(1)
    lab.wait_log("Quick save (P2)", r"savestate: saved frame \d+", pos_qs, frames=120)
    emu.advance(35)

    # Switch to CPU Mode = Control, Restore Positions + Quick Load (reverse direction).
    _set_rec_cpu_state(lab, rec, CPU_MODE["Control"], TAKEOVER["HMN"], 1, restore=True)
    _wait_standing(r)
    c_rec = player_controllers(emu)[1]
    sw_rec = bool(rec.status()["flags"] & FLAG_SWAPPED)
    p_rec = emu.u32(PATCH_ADDR)
    p1_dx, cpu_dx, _, p1_st = _hold_60(r, 1, CPU)
    res.row("Port 2 only: save Off -> Control -> Restore Positions",
            f"ctrl=2, swapped=True, patch={PATCH_SKIP:#x}; Port 2 -> CPU only",
            f"ctrl={c_rec}, swapped={sw_rec}, patch={p_rec:#x}, P1={p1_dx:+.1f} CPU={cpu_dx:+.1f} p1={p1_st[:2]}",
            c_rec == 2 and sw_rec and p_rec == PATCH_SKIP and set(p1_st) == {"Wait"} and abs(cpu_dx) > 10.0)

    pos_ql = lab.log_pos()
    emu.press(1, ["DLEFT"], frames=2, after=0)
    lab.wait_log("Quick load (P2)", r"savestate: loaded frame \d+", pos_ql, frames=120)
    _wait_standing(r)
    c_ql = player_controllers(emu)[1]
    sw_ql = bool(rec.status()["flags"] & FLAG_SWAPPED)
    p_ql = emu.u32(PATCH_ADDR)
    p1_dx2, cpu_dx2, _, p1_st2 = _hold_60(r, 1, CPU)
    res.row("Port 2 only: save Off -> Control -> Quick Load",
            f"ctrl=2, swapped=True, patch={PATCH_SKIP:#x}; Port 2 -> CPU only",
            f"ctrl={c_ql}, swapped={sw_ql}, patch={p_ql:#x}, P1={p1_dx2:+.1f} CPU={cpu_dx2:+.1f} p1={p1_st2[:2]}",
            c_ql == 2 and sw_ql and p_ql == PATCH_SKIP and set(p1_st2) == {"Wait"} and abs(cpu_dx2) > 10.0)

    # Controlled By = Port 2 while Control on -> CB owns (Port 2 moves both).
    set_controlled_by_menu(lab, 2)
    emu.advance(10)
    rec_menu_open(lab)
    pos_r2 = lab.log_pos()
    lab.tap(["A"])
    lab.close_menu()
    lab.wait_log("Restore (P2 CB)", r"savestate: recording state loaded frame \d+", pos_r2, frames=180)
    _wait_standing(r)
    c_cb, t_cb, sw_cb, p_cb = player_controllers(emu)[1], _cpu_type(r), bool(rec.status()["flags"] & FLAG_SWAPPED), emu.u32(PATCH_ADDR)
    p1_b, cpu_b, _, _ = _hold_60(r, 1, P1)
    res.row("Port 2 only: Control + CB=Port 2 -> CB owns (Port 2 moves both)",
            f"ctrl=2, type=25, swapped=False, patch={PATCH_SKIP:#x}; both move",
            f"ctrl={c_cb}, type={t_cb}, swapped={sw_cb}, patch={p_cb:#x}, P1={p1_b:+.1f} CPU={cpu_b:+.1f}",
            c_cb == 2 and t_cb == 25 and not sw_cb and p_cb == PATCH_SKIP and abs(p1_b) > 10.0 and abs(cpu_b) > 10.0)

    # CB -> None while Control stays on -> Control resumes (patch stays patched).
    patch_mid = []
    set_controlled_by_menu(lab, 0, before_close=lambda: patch_mid.append(emu.u32(PATCH_ADDR)))
    emu.advance(10)
    rec_menu_open(lab)
    pos_r3 = lab.log_pos()
    lab.tap(["A"])
    lab.close_menu()
    lab.wait_log("Restore (P2 Control)", r"savestate: recording state loaded frame \d+", pos_r3, frames=180)
    _wait_standing(r)
    c_res, t_res, sw_res = player_controllers(emu)[1], _cpu_type(r), bool(rec.status()["flags"] & FLAG_SWAPPED)
    p1_r, cpu_r, _, p1_sr = _hold_60(r, 1, CPU)
    res.row("Port 2 only: CB->None while Control stays -> Control resumes",
            f"ctrl=2, type={type_orig}, swapped=True, patch_mid={PATCH_SKIP:#x}; Port 2 -> CPU only",
            f"ctrl={c_res}, type={t_res}, swapped={sw_res}, patch_mid={patch_mid[0]:#x}, "
            f"P1={p1_r:+.1f} CPU={cpu_r:+.1f} p1={p1_sr[:2]}",
            c_res == 2 and t_res == type_orig and sw_res and patch_mid[0] == PATCH_SKIP
            and set(p1_sr) == {"Wait"} and abs(cpu_r) > 10.0)

    # Exit match via Main Menu > Exit while patch is active -> CSS reads 0x8B1BFFFF.
    lab.open_menu()
    lab.main_to(LT.MAIN_EXIT)
    lab.tap(["A"])
    emu.wait_for("CSS after Exit (P2)", lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 20, every=5)
    emu.advance(30)
    patch_css = emu.u32(PATCH_ADDR)
    res.row("Port 2 only: 0x80962d0c restored to 0x8B1BFFFF on CSS after Exit",
            f"{PATCH_ORIG:#x}", f"{patch_css:#x}", patch_css == PATCH_ORIG)
    return res


def s_finish(r):
    res = Result("match end")
    emu = r.emu
    pos = len(emu.log_text())
    emu.h.call("resume")
    nav.finish_training(emu)
    emu.advance(60)
    log = log_since(emu, pos)
    freed = re.search(r"record: storage freed; FighterTechqniq free (\w+), back to baseline: (yes|no)", log)
    slots = re.search(r"savestate: \d+ saves, \d+ loads, \d+ refused; slot (freed|never taken)", log)
    unload = re.search(L.rx(L.UNLOADED).pattern, log)
    res.row("storage and savestate buffers freed, FighterTechqniq and the module back at baseline", "yes / yes",
            f"{freed.group(0) if freed else None} / {slots.group(0) if slots else None} / "
            f"{unload.group(4) if unload else None}",
            freed is not None and freed.group(2) == "yes" and slots is not None and unload is not None
            and unload.group(4) == "yes")
    return res


def _heap_counts(emu, hid):
    pool = emu.ptr(HEAP_INFOS + 16 * hid + 4)
    if not pool:
        return None, None
    return emu.u16(pool + 0x14), emu.u32(pool + 0x1C)


def _assist_state(emu):
    gg = emu.ptr(nav.GAME_GLOBAL)
    mode = emu.ptr(gg + 0x8) if gg else None
    mode_assist = emu.u32(mode + MODE_ASSIST_SWITCH) if mode else None
    mgr = emu.ptr(IT_MANAGER)
    next_assist = struct.unpack(">i", emu.read(mgr + IT_NEXT_ASSIST, 4))[0] if mgr else None
    mgr_word5 = emu.u32(mgr + IT_SWITCH + 20) if mgr else None
    ftq_blocks, ftq_used = _heap_counts(emu, HEAP_FTQ)
    assist_blocks, assist_used = _heap_counts(emu, HEAP_ASSIST)
    pokemon_blocks, pokemon_used = _heap_counts(emu, HEAP_POKEMON)
    return {
        "mode_assist": mode_assist,
        "mode_resetti": None if mode_assist is None else bool(mode_assist & ASSIST_MR_RESETTI),
        "next_assist": next_assist,
        "mgr_resetti": None if mgr_word5 is None else bool((mgr_word5 >> 1) & 1),
        "ftq_blocks": ftq_blocks,
        "ftq_used": ftq_used,
        "assist_blocks": assist_blocks,
        "assist_used": assist_used,
        "pokemon_blocks": pokemon_blocks,
        "pokemon_used": pokemon_used,
    }


def _seed_resetti(emu):
    """Pin g_mtRand to RESETTI_SEED while itManager enters phase 5/6 so a
    regression that re-enables preloads would draw Mr. Resetti."""
    emu.wait_for("scMelee", lambda: nav.scene(emu)[0] == "scMelee", 60 * 30, every=1)
    emu.h.call("pause")
    try:
        phase6_frames = 0
        for _ in range(500):
            mgr = emu.ptr(IT_MANAGER)
            if mgr:
                phase = emu.u32(mgr + IT_PHASE)
                nxt = struct.unpack(">i", emu.read(mgr + IT_NEXT_ASSIST, 4))[0]
                if nxt == -1 and phase in (5, 6):
                    emu.h.call("write_mem", addr=MT_RAND_SEED, hex=f"{RESETTI_SEED:08x}")
                if nxt != -1:
                    return
                if phase == 6:
                    phase6_frames += 1
                    if phase6_frames >= 60:
                        return
            emu.h.call("frame_advance", n=1)
    finally:
        emu.h.call("resume")


def s_resetti(r):
    """Training skips unused Assist Trophy and Pokemon preloads (itManager+0x149F=0),
    keeping AssistFigureResource at 1 block (extra module only, next_assist == -1)
    and PokemonResource at 0 B, with recording working under both seeded Resetti
    RNG (0x1E6C6E03) and an ordinary unseeded match."""
    res = Result("Mr. Resetti guard")
    emu, rec = r.emu, r.rec
    emu.h.call("resume")
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
        emu.advance(60)

    # 1. Seeded match targeting Mr. Resetti's draw interval.
    pos = len(emu.log_text())
    nav.start_training_match(emu, r.hmn, r.cpu, r.stage, during=_seed_resetti)
    emu.advance(60)
    st_a = _assist_state(emu)
    log = log_since(emu, pos)
    refusal = re.search(r"record: no storage:[^\r\n]*", log)
    r.refresh()
    tb = rec.status()

    mode_hex = f"{st_a['mode_assist']:#010x}" if st_a["mode_assist"] is not None else "None"
    res.row("Resetti switch bits clear (modeMelee & itManager)", "False / False",
            f"modeMelee {st_a['mode_resetti']} ({mode_hex}), itManager {st_a['mgr_resetti']}",
            st_a["mode_resetti"] is False and st_a["mgr_resetti"] is False)
    res.row("Assist preload skipped (extra only) & Pokemon 0 B", "next -1, assist 1 blk (extra), pokemon 0 B",
            f"next {st_a['next_assist']}, assist {st_a['assist_blocks']} blk/{st_a['assist_used']} B, "
            f"pokemon {st_a['pokemon_blocks']} blk/{st_a['pokemon_used']} B",
            st_a["next_assist"] == -1 and st_a["assist_blocks"] == 1
            and st_a["pokemon_blocks"] == 0 and st_a["pokemon_used"] == 0)
    storage_ok = (bool(tb["flags"] & FLAG_STORAGE) and tb["storage"] != 0 and tb["bytes"] == SLOT_BYTES
                  and st_a["ftq_blocks"] == 1 and refusal is None)
    obs_storage = (f"flags {tb['flags']:#x}, storage {tb['storage']:#x}, bytes {tb['bytes']:#x}, "
                   f"ftq blocks {st_a['ftq_blocks']}")
    if refusal:
        obs_storage += f" ({refusal.group(0)})"
    res.row("recording buffer allocated in FighterTechqniq", f"storage ready, 1 block, {SLOT_BYTES:#x}",
            obs_storage, storage_ok)

    if storage_ok:
        C.set_training_option(emu, "Stop")
        emu.advance(20)
        rec.cpu_control(True)
        st_save = r.run_request("save")
        short_seq = [(0, 0.5, 0.0, 12), (JUMP, 0.0, 0.0, 1), (0, 0.0, 0.0, 20)]
        rec_rows = record_hmn(r, slot=1, sequence=short_seq)
        end_frame = rec.slot_end(0, 0)[1]
        play_rows = play_hmn(r, slot=1, frames=len(rec_rows))
        diff = compare(rec_rows, play_rows, P1)
        res.row("Save Positions, Record and Playback work", "saved, >= 30 frames, exact playback",
                f"saved {bool(st_save['flags'] & FLAG_SAVED)}, end {end_frame}, diff {diff[0] if diff else 'none'}",
                bool(st_save["flags"] & FLAG_SAVED) and end_frame >= 30 and diff is None)
    else:
        res.row("Save Positions, Record and Playback work", "saved, >= 30 frames, exact playback",
                "skipped: no recording storage", False)

    pos_exit = len(emu.log_text())
    emu.h.call("resume")
    nav.finish_training(emu)
    emu.advance(60)
    exit_log = log_since(emu, pos_exit)
    freed = re.search(r"record: storage freed; FighterTechqniq free (\w+), back to baseline: (yes|no)", exit_log)
    res.row("seeded match exit frees FighterTechqniq to baseline", "back to baseline: yes",
            freed.group(0) if (storage_ok and freed) else "skipped: no storage during match",
            storage_ok and freed is not None and freed.group(2) == "yes")

    # 2. Ordinary unseeded match: preloads stay skipped (extra only in assist, pokemon 0 B) and recording works.
    pos_u = len(emu.log_text())
    nav.start_training_match(emu, r.hmn, r.cpu, r.stage, seed_rng=False)
    emu.advance(60)
    st_u = _assist_state(emu)
    log_u = log_since(emu, pos_u)
    r.refresh()
    tb_u = rec.status()
    res.row("ordinary unseeded match skips preloads", "next -1, assist 1 blk (extra), pokemon 0 B, storage ok",
            f"next {st_u['next_assist']}, assist {st_u['assist_blocks']} blk/{st_u['assist_used']} B, "
            f"pokemon {st_u['pokemon_blocks']} blk/{st_u['pokemon_used']} B, storage {bool(tb_u['flags'] & FLAG_STORAGE)}",
            st_u["next_assist"] == -1 and st_u["assist_blocks"] == 1
            and st_u["pokemon_blocks"] == 0 and st_u["pokemon_used"] == 0
            and bool(tb_u["flags"] & FLAG_STORAGE) and "record: no storage" not in log_u)
    nav.finish_training(emu)
    emu.advance(60)
    return res


# ---- screenshots -----------------------------------------------------------------

def hud_check(path, xscale):
    """The bar's colour (hud.cpp kBar = 0x141A24F4, (20, 26, 36)) between its
    left end and the track at y = 375 (kRecBarCentreY)."""
    w, h, ch, rows = shots.read_png(path)
    y = int(375 * h / 480)
    x = int((320 + (190 - 320) * xscale) * w / 640)
    px = rows[y][x * ch:x * ch + 3]
    ok = all(abs(px[i] - c) <= 12 for i, c in enumerate((20, 26, 36)))
    return (tuple(px), ok)


def write_png(path, width, height, rows):
    raw = b"".join(b"\0" + bytes(r) for r in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


# ---- running ---------------------------------------------------------------------

SCENARIOS = [("refuse", s_refuse), ("restore", s_restore), ("hmn", s_hmn), ("cpu", s_cpu), ("edit", s_edit),
             ("rewind", s_rewind), ("loop", s_loop),
             ("autorestore", s_autorestore), ("takeover", s_takeover), ("rerecord", s_rerecord),
             ("paused", s_paused), ("slots", s_slots), ("random", s_random), ("prune", s_prune),
             ("mirror", s_mirror), ("counter", s_counter), ("oncpuhit", s_oncpuhit),
             ("playslot", s_playslot), ("dpad", s_dpad), ("percent", s_percent),
             ("full", s_full), ("advance", s_advance), ("fallback", s_fallback),
             ("menu", s_menu), ("cpu-controller-leak", s_cpu_controller_leak),
             ("quick-load-after-end", s_quick_load_after_end),
             ("quick-load-holding", s_quick_load_holding),
             ("ctrl-orig-persist", s_ctrl_orig_persist),
             ("ctrl-one-pad-leak", s_ctrl_one_pad_leak),
             ("finish", s_finish), ("resetti", s_resetti),
             ("cpu-ctrl-matrix", s_cpu_ctrl_matrix),
             ("controlled-by-record", s_controlled_by_record),
             ("ctrl-port2-only", s_ctrl_port2_only)]
# Per-scenario pad presets: pre-existing recording scenarios stay on ONE pad,
# multi-controller scenarios run on FOUR pads, and Port 2 only runs on PORT2_ONLY.
FOUR_PAD_SCENARIOS = {"cpu-ctrl-matrix", "controlled-by-record"}
PORT2_ONLY_SCENARIOS = {"ctrl-port2-only"}
SELF_MANAGED = {"refuse", "menu", "cpu-controller-leak", "quick-load-after-end",
                "quick-load-holding", "ctrl-orig-persist", "ctrl-one-pad-leak",
                "cpu-ctrl-matrix", "controlled-by-record", "ctrl-port2-only",
                "finish", "full", "resetti"}
# No speed limit (emu.speed_args): every wait here is counted in game frames,
# and the screenshots are of the HUD and the lab menu, which stay as they are
# while the frames of a screenshot request pass.
SPEED = "0"
# What a scenario needs run first (a name, or a tuple of names).
NEEDS = {"loop": "hmn", "autorestore": "hmn", "takeover": "cpu", "rerecord": "hmn", "paused": "cpu",
         "random": "slots", "prune": "slots", "counter": "cpu", "oncpuhit": "cpu", "dpad": "hmn", "playslot": "cpu",
         "edit": "hmn", "rewind": ("hmn", "cpu")}


def cmd_test_record(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-record")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    cast = sorted(n for n in nav.CSS_POS if n != "random")
    ap.add_argument("--hmn", default="mario", choices=cast, help="the player's character")
    ap.add_argument("--cpu", default="fox", choices=cast, help="the CPU's character")
    ap.add_argument("--stage", default="final_destination", choices=sorted(nav.STAGES))
    opts = ap.parse_args(rest)
    label = "test-record"
    visual_backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    headless = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, results = "", "boot", "", []
    wanted = set(n for n, _ in SCENARIOS) if not opts.only else set(opts.only)
    todo = list(wanted)
    while todo:
        need = NEEDS.get(todo.pop(), ())
        for name in (need,) if isinstance(need, str) else need:
            if name not in wanted:
                wanted.add(name)
                todo.append(name)
    if wanted - SELF_MANAGED:
        wanted |= {"restore"}

    # "one-pad" runs "menu" (which checks rendered pixels via hud_check) in the
    # same match; use visual_backend when "menu" is selected and headless
    # otherwise. "four-pads" and "port2-only" never assert on pixels.
    one_pad_sc = [(n, fn) for n, fn in SCENARIOS if n in wanted and n not in FOUR_PAD_SCENARIOS
                  and n not in PORT2_ONLY_SCENARIOS]
    one_pad_visual = any(n == "menu" for n, _ in one_pad_sc)
    groups = [
        ("one-pad", visual_backend if one_pad_visual else headless, not one_pad_visual, E.DETERMINISTIC_ARGS,
         one_pad_sc),
        ("four-pads", headless, True, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS,
         [(n, fn) for n, fn in SCENARIOS if n in wanted and n in FOUR_PAD_SCENARIOS]),
        ("port2-only", headless, True, E.DETERMINISTIC_ARGS + E.PORT2_ONLY_ARGS,
         [(n, fn) for n, fn in SCENARIOS if n in wanted and n in PORT2_ONLY_SCENARIOS]),
    ]
    logs = []
    for gname, gbackend, gallow_null, gargs, gscenarios in groups:
        if not gscenarios or why:
            continue
        gnames = {n for n, _ in gscenarios}
        try:
            with E.harness_run(gbackend, gargs, speed=SPEED, allow_null=gallow_null) as (proc, emu):
                log_path = emu.log_path
                r = Runner(emu, opts.hmn, opts.cpu, opts.stage)
                try:
                    if gnames == {"resetti"}:
                        step = "into Training CSS"
                        nav.boot_to_training_css(emu)
                    elif gname == "port2-only":
                        step = "into Training match (Port 2)"
                        emu.wait_for("the character select screen after boot",
                                     lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
                        emu.wait_for("the drill list to open",
                                     lambda: re.search(r"\] drills: list opened \(auto\)", E.read_log(emu.log_path)),
                                     900, every=5)
                        nav.start_quick_match(emu, stage=opts.stage, port=1, seed_rng=True)
                        emu.advance(60)
                        r.refresh()
                        # Stop the CPU via the lab menu or keep Lab CPU Control (Behavior=Stand).
                    else:
                        step = f"into the match ({gname})"
                        # Fixed match RNG so the scenarios start from the same state.
                        nav.enter_training_match(emu, opts.hmn, opts.cpu, opts.stage, seed_rng=True)
                        emu.advance(60)
                        r.refresh()
                        step = "CPU on Stop"
                        C.set_training_option(emu, "Stop")
                        emu.advance(30)
                    for name, fn in gscenarios:
                        step = name
                        emu.h.call("resume")
                        if name not in SELF_MANAGED and name not in ("restore", "advance") \
                                and not r.rec.status()["flags"] & FLAG_SAVED:
                            r.run_request("save")
                        inside = len(r.inside)
                        res = fn(r)
                        res.key = name
                        if len(r.inside) > inside:
                            res.note(f"{len(r.inside) - inside} reads taken inside a game pass left out (a save or "
                                     f"load's pass; see MAIN_THREAD_STATE), at frames {r.inside[inside:][:12]}")
                        results.append(res)
                        print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}")
                        for line in res.lines:
                            print("  " + line)
                    step = f"finish ({gname})"
                    emu.h.call("resume")
                    if nav.scene(emu)[0] == "scMelee" and gname != "port2-only":
                        nav.finish_training(emu)
                    emu.advance(60)
                except (Timeout, nav.NavError, HarnessError, RecError, C.CpuError, LT.LabError) as e:
                    why = f"{step}: {e}"
                    try:
                        emu.h.call("resume")
                        emu.screenshot("fail-record")
                    except (OSError, HarnessError, ValueError):
                        pass
        except (OSError, HarnessError, Timeout, ValueError) as e:
            why = why or f"{step}: {e}"
        if log_path:
            logs.append((gname, log_path, E.read_log(log_path)))

    print()
    for gname, lpath, log in logs:
        for line in log.splitlines():
            if re.search(rf"\[{L.tag()}\] (record:|module MTCH|cpu: (take|release|\d+ takes))", line):
                print("  " + line)
        bad = L.problems(log)
        for line in bad:
            print(f"problem ({gname}): {line}")
        if not why and bad:
            why = f"problem lines in the log ({gname})"
        if not why:
            installs = re.findall(L.rx(r"\[{name}\] hook fighter input: installed at ([0-9a-f]{{8}})").pattern, log)
            if len(installs) != 1:
                why = f"the fighter-input hook was installed {len(installs)} times ({gname})"
    print()
    print(f"{'scenario':<26} {'check':<58} {'expected':<28} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<25} {what[:58]:<58} {exp[:28]:<28} {obs}")
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
