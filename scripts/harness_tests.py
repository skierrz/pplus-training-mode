"""Emulator test of the harness client's batched calls (run through emu.py;
settings come from the environment the same way).

  test-harness REL [--bench] [--dual-core] [--frames N]
      One boot, Mario (P1) against a Fox CPU on Final Destination, one CPU
      thread. A Dolphin savestate is taken in the match; every check then
      plays the same frames from it twice and compares what was read, bit
      for bit: once the way the tests stepped before batches (a
      frame_advance request, then every read as a request of its own), once
      through harness.Emu's batched calls.

        repeat     the frames played twice the old way give the same rows
                   (without that the other comparisons would say nothing)
        trace      Emu.trace against the loop, with the pad changed on port 1
                   from each() on given frames
        script     plugin_input.PluginInput.run against the loop it replaced,
                   both fighters driven by the plugin's script blocks
        until      Emu.advance_until stops on the frame the loop stops on
        moving     a function whose reads change from frame to frame, and
                   one that writes memory between its reads
        errors     a batch with a failing request raises, and the requests
                   after it were answered in order

      --bench also prints what a frame costs on this machine: the game's
      speed with and without the speed limit, a frame stepped with its reads
      sent one by one, as one batch per frame, and as one batch for many
      frames. --dual-core prints the same with two CPU threads and skips
      the comparisons: two threads do not repeat a run frame for frame.
"""
import argparse
import os
import struct
import time

import emu as E
import lab_tests as LT
import nav
import plugin_input as PI
from harness import HarnessError, Timeout

# The game's random number generators (as nav.RNG_SEEDS).
RNG = [(0x805A00B8 + 4, 4), (0x805A0420 + 4, 4), (0x8059FF58, 8)]
LEFT, RIGHT = (28, 128), (228, 128)
# No speed limit: nothing here depends on how long a frame takes.
SPEED = "0"


class Check(RuntimeError):
    pass


class Bench:
    def __init__(self, emu):
        self.emu = emu
        self.f = LT.Fighters(emu)
        self.inp = PI.PluginInput(emu)
        self.state = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states", "harness.sav")
        os.makedirs(os.path.dirname(self.state), exist_ok=True)

    def ok(self, name, text):
        print(f"  ok   {name}: {text}")

    def row(self, i=None):
        """Read as the tests read: through the entry table and each
        fighter's modules, pointer by pointer."""
        e = self.emu
        out = {"i": i, "frame": nav.game_frame(e), "rng": b"".join(e.read(a, n) for a, n in RNG)}
        for player in (0, 1):
            out[player] = self.f.state(self.f.by_player(player))
        return out

    def load(self):
        """The saved frame again, paused, the pads let go."""
        h = self.emu.h
        h.call("pause")
        h.call("pad_clear", port=0)
        self.emu.neutral(0)
        self.inp.stop()
        h.call("load_state", path=self.state)
        # The first advance after a load only finishes the saved frame.
        h.call("frame_advance", n=1)


def same(name, old, new):
    if len(old) != len(new):
        raise Check(f"{name}: {len(old)} rows one by one, {len(new)} batched")
    for k, (a, b) in enumerate(zip(old, new)):
        if repr(a) != repr(b):
            raise Check(f"{name}: row {k} differs: one by one {a!r}, batched {b!r}")


def pad_each(emu, moved):
    """An each() that runs P1 right, makes it jump and runs it back."""
    def each(i, rows):
        if i == 5:
            emu.hold(0, stick=RIGHT)
        elif i == 30:
            emu.hold(0, ["X"], stick=RIGHT)
        elif i == 34:
            emu.hold(0, stick=LEFT)
        elif i == 70:
            emu.neutral(0)
        if rows:
            moved.append(rows[-1][0]["x"])
    return each


def loop(b, frames, each=None):
    """The stepping loop as the tests had it: a request per frame, then a
    request per read."""
    emu = b.emu
    rows = []
    for i in range(frames):
        if each:
            each(i, rows)
        emu.h.call("frame_advance", n=1)
        rows.append(b.row(i))
    return rows


def run_one_by_one(inp, scripts, read, extra=0):
    """plugin_input.PluginInput.run as it was before batches."""
    emu = inp.emu
    emu.h.call("pause")
    totals = {}
    for block, (player, steps) in zip(inp.blocks, scripts.items()):
        block.play(player, steps, ai_off=True)
        totals[player] = (block, sum(s[3] for s in steps))
    longest = max(t for _, t in totals.values())
    rows = []
    for i in range(1, longest + extra + 1):
        emu.h.call("frame_advance", n=1)
        if i == 1:
            for _ in range(PI.START_WAIT_FRAMES):
                if any(block.status()["driven"] for block, _ in totals.values()):
                    break
                emu.h.call("frame_advance", n=1)
        for player, (block, total) in totals.items():
            if i == 1 or i == total:
                driven = block.status()["driven"]
                if driven != min(i, total):
                    raise PI.InputError(f"player {player + 1}: {driven} script frames driven after {i}")
        rows.append(read(i))
    return rows


def c_repeat(b, frames):
    moved = []
    b.load()
    first = loop(b, frames, pad_each(b.emu, moved))
    b.load()
    again = loop(b, frames, pad_each(b.emu, []))
    same("repeat", first, again)
    xs = {round(x, 2) for x in moved}
    statuses = {r[0]["status"] for r in first}
    if len(xs) < 20 or len(statuses) < 3:
        raise Check(f"repeat: the pad did not move P1 ({len(xs)} positions, statuses {sorted(statuses)})")
    b.ok("repeat", f"{frames} frames twice from the savestate, one request at a time: equal rows; P1 in "
         f"{len(statuses)} statuses at {len(xs)} positions")
    return first


def c_trace(b, frames, first):
    emu = b.emu
    watch = emu.watch(b.row)
    b.load()
    rows = emu.trace(frames, watch, pad_each(emu, []))
    same("trace", first, rows)
    b.ok("trace", f"Emu.trace, {frames} frames with pad changes from each(): equal to the loop's rows "
         f"({len(watch.plan)} reads a frame)")


def c_script(b):
    mario = PI.frames_to_steps({1: (PI.B_JUMP, 0.0, 0.0), 2: (PI.B_JUMP, 0.0, 0.0), 9: (PI.B_GUARD, 0.7, -0.7),
                                40: (PI.B_ATTACK, 0.0, 0.0), 60: (0, -1.0, 0.0), 61: (0, -1.0, 0.0)}, 80)
    fox = PI.frames_to_steps({n: (0, 0.6, 0.0) for n in range(1, 30)}, 50)
    scripts = {0: mario, 1: fox}
    b.load()
    old = run_one_by_one(b.inp, scripts, b.row, extra=10)
    b.load()
    new = b.inp.run(scripts, read=b.row, extra=10)
    same("script", old, new)
    statuses = {r[0]["status"] for r in old}
    moved = len({round(r[1]["x"], 2) for r in old})
    if len(statuses) < 4 or moved < 10:
        raise Check(f"script: the scripts did not drive the fighters (P1 statuses {sorted(statuses)}, CPU at "
                    f"{moved} positions)")
    b.ok("script", f"PluginInput.run, {len(old)} frames, both fighters scripted: equal to the loop it replaced "
         f"(P1 in {len(statuses)} statuses, the CPU at {moved} positions)")


def c_until(b):
    emu = b.emu
    b.load()
    emu.hold(0, ["X"])
    n_old = None
    for n in range(1, 200):
        emu.h.call("frame_advance", n=1)
        value = b.row()
        if value[0]["status"] == 0xB:  # Jump: the first airborne frame
            n_old, old = n, value
            break
    b.load()
    emu.hold(0, ["X"])
    n_new, new = emu.advance_until("P1 in the air", emu.watch(b.row), lambda v: v[0]["status"] == 0xB, 200)
    emu.neutral(0)
    if n_old is None or n_old != n_new:
        raise Check(f"until: the loop stopped after {n_old} frames, advance_until after {n_new}")
    same("until", [old], [new])
    try:
        emu.advance_until("a status no fighter has", emu.watch(b.row), lambda v: v[0]["status"] == 0x7FFF, 5)
        raise Check("until: no Timeout when the condition never holds")
    except Timeout:
        pass
    b.ok("until", f"Emu.advance_until stopped after {n_new} frames, on the frame the loop stopped on, with the "
         f"same row; Timeout at its limit")


def c_moving(b, frames=40):
    """Reads that are not the same from one frame to the next, and a write
    between two reads of the same word."""
    emu = b.emu
    scratch = b.inp.blocks[1].addr + 0x3C  # the second script block's steps: nothing plays them here

    def fn(i=None):
        out = b.row(i)
        # Another fighter's modules on odd frames, more of them every third.
        f = b.f.by_player(i % 2)
        out["mods"] = b.f.modules(f)[:0x20 + 4 * (i % 3)]
        before = emu.u32(scratch)
        emu.h.call("write_mem", addr=scratch, hex=struct.pack(">I", 0x1000 + i).hex())
        out["word"] = (before, emu.u32(scratch))
        return out
    b.load()
    old = []
    for i in range(frames):
        emu.h.call("frame_advance", n=1)
        old.append(fn(i))
    b.load()
    new = emu.trace(frames, emu.watch(fn))
    same("moving", old, new)
    if any(r["word"][1] != 0x1000 + k for k, r in enumerate(new)):
        raise Check("moving: a word read after it was written held the batch's older value")
    b.ok("moving", f"{frames} frames, the reads different on every frame and a write between two reads of one "
         f"word: equal to the loop's rows")


def c_errors(b):
    h = b.emu.h
    try:
        h.batch([("read_mem", {"addr": nav.GAME_FRAME_COUNTER, "len": 4}), ("read_mem", {"addr": 0x10, "len": 4}),
                 ("read_mem", {"addr": nav.GAME_FRAME_COUNTER, "len": 4})])
        raise Check("errors: a read outside RAM did not fail")
    except HarnessError as e:
        text = str(e)
    two = h.batch([("read_mem", {"addr": nav.GAME_FRAME_COUNTER, "len": 4})] * 2)
    one = b.emu.read(nav.GAME_FRAME_COUNTER, 4).hex()
    if "request 2 of 3" not in text or two[0]["hex"] != one or two[1]["hex"] != one:
        raise Check(f"errors: {text!r}; the reads after it {two} / {one}")
    b.ok("errors", "a failing request in a batch raised its error; the next batch and the next request were "
         "answered in order")


def fps(emu, frames):
    a = emu.polls()
    t = time.perf_counter()
    emu.wait_polls(a + frames)
    return frames / (time.perf_counter() - t)


def set_speed(emu, value):
    emu.h.call("config_set", system="Dolphin", section="Core", key="EmulationSpeed", value=value, layer="current")


def bench(b):
    """What a frame costs here. Changes the speed limit while it runs."""
    emu, h = b.emu, b.emu.h
    watch = emu.watch(b.row)
    print(f"bench ({'two CPU threads' if h.call('status').get('cpu_thread') else 'one CPU thread'}):")
    for speed, name in (("1", "limit 100%"), ("0", "no limit")):
        b.load()
        h.call("resume")
        set_speed(emu, speed)
        emu.advance(30)
        line = f"  {name}: running {fps(emu, 600):.0f} frames a second"
        h.call("pause")
        h.call("frame_advance", n=1)
        t = time.perf_counter()
        h.call("frame_advance", n=300)
        line += f"; paused, per frame: frame_advance n=300 {(time.perf_counter() - t) / 0.3:.2f} ms"
        t = time.perf_counter()
        loop(b, 200)
        line += f", a frame and its reads one by one {(time.perf_counter() - t) / 0.2:.2f} ms"
        watch.read()
        t = time.perf_counter()
        emu.trace(200, watch)
        line += f", as one batch (Emu.trace) {(time.perf_counter() - t) / 0.2:.2f} ms"
        reads = [("read_mem", {"addr": a, "len": n}) for a, n in watch.plan]
        t = time.perf_counter()
        h.batch(([("frame_advance", {"n": 1})] + reads) * 200)
        line += f", 200 frames as one batch {(time.perf_counter() - t) / 0.2:.2f} ms ({len(reads)} reads a frame)"
        print(line)
    set_speed(emu, SPEED)
    t = time.perf_counter()
    h.call("save_state", path=b.state + ".bench")
    saved = time.perf_counter() - t
    t = time.perf_counter()
    h.call("load_state", path=b.state + ".bench")
    print(f"  Dolphin savestate: saved in {saved:.2f}s ({os.path.getsize(b.state + '.bench') / 1e6:.0f} MB), "
          f"loaded in {time.perf_counter() - t:.2f}s")
    os.remove(b.state + ".bench")
    h.call("resume")


def cmd_test_harness(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-harness")
    ap.add_argument("rel")
    ap.add_argument("--bench", action="store_true")
    ap.add_argument("--dual-core", action="store_true")
    ap.add_argument("--frames", type=int, default=120)
    opts = ap.parse_args(rest)
    label = "test-harness"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step = "", "boot"
    try:
        with E.harness_run(backend, () if opts.dual_core else E.DETERMINISTIC_ARGS, speed=SPEED, allow_null=True) as (proc, emu):
            try:
                print(f"emulation running after {time.monotonic() - t0:.1f}s")
                step = "into the match"
                nav.enter_training_match(emu, "mario", "fox")
                print(f"in the match after {time.monotonic() - t0:.1f}s")
                emu.advance(120)
                b = Bench(emu)
                emu.h.call("pause")
                emu.h.call("frame_advance", n=1)
                emu.h.call("save_state", path=b.state)
                if not opts.dual_core:
                    step = "repeat"
                    first = c_repeat(b, opts.frames)
                    step = "trace"
                    c_trace(b, opts.frames, first)
                    step = "script"
                    c_script(b)
                    step = "until"
                    c_until(b)
                    step = "moving"
                    c_moving(b)
                    step = "errors"
                    c_errors(b)
                if opts.bench or opts.dual_core:
                    step = "bench"
                    bench(b)
                emu.h.call("resume")
            except (Timeout, nav.NavError, HarnessError, Check, PI.InputError, LT.LabError) as e:
                why = f"{step}: {e}"
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        print(f"{label}: FAIL ({why})")
        return 1
    print(f"{label}: PASS")
    return 0
