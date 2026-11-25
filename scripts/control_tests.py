"""Emulator test of the buttons the lab keeps for itself (run through
emu.py; settings come from the environment the same way).

  test-controls REL [--only NAME ...] [--keep-going]
      One boot, Mario (P1) against a Fox CPU on Final Destination, one CPU
      thread, from the base state test-display builds (the CPU standing
      still under the lab's CPU control, Mario next to it). The game is
      stepped one emulated frame at a time with the harness's pad on port
      1, and after every frame the test reads Mario from the game's memory,
      never through the plugin: his status and animation frame, his input
      object (the buttons and stick his controller acts on), and Project+'s
      analog trigger variables (LA-Float 36 and 37, LA-Basic 77).

      advance-l, advance-z, advance-x, advance-y, advance-r: Frame Advance
      on with that Frame Advance Button.
        - six presses of two frames: one game step each, Mario stays in
          Wait, his input object never holds a button, the analog trigger
          variables stay 0;
        - the button held 80 frames: the game runs after the hold delay
          and stops again on release, with the same three checks;
        - the stick held toward the CPU and three presses: three steps,
          Mario moves that way with the stick at full tilt in his input
          object, and no button;
        - a jump button held (X; Y when X is the advance button) and two
          presses: two steps, a jump squat, the input object holding that
          button's bits and nothing else;
        - L and R only: the other trigger held and two presses: Mario
          shields, the other trigger's analog variable is set and the
          advance button's stays 0.

      shortcuts: the Y-held shortcuts against the lab menu's own button.
        Lab Menu Button must start at Start.
        - Lab Menu Button = Z in Pause: positions are saved (Recording >
          Save Positions), then with the menu open again every shortcut is
          pressed with Y held: A, X, D-pad left, right, up, down and Z.
          Each must log its shortcut and none may close the menu; Y + Z
          saves the positions again. Z alone then closes the menu.
        - Lab Menu Button = Start: all seven again, Y + Z saving the
          positions again without opening Training's pause. D-pad left and right
          are the savestate bindings while the match runs: with the menu
          open they must be the menu's (no state saved or loaded, also when
          held as long as a save takes), and the savestate's again once it
          is closed (savestate directions do nothing while the menu shows).
          Start closes the menu. A button held while the menu closes reaches
          the fighter only once let go and pressed again.
"""
import argparse
import os
import re
import struct
import time

import display_options_tests as DT
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import plugin_input as PI
from harness import HarnessError, Timeout

# main::Settings (src/main/settings.h).
S_FRAME_ADVANCE, S_ADVANCE_BUTTON, S_ADVANCE_DELAY, S_START_OPENS = 11, 24, 25, 26
S_HITBOXES = 42
# main::AdvanceButton order, with the harness's button names.
BUTTONS = ["L", "Z", "X", "Y", "R"]
HOLD_DELAY = 30
# Fighter statuses (lib/BrawlHeaders ft/fighter.h).
WAIT, JUMP_SQUAT, GUARD_ON, GUARD = 0, 0xA, 0x1A, 0x1B
# The input object's button word for a pad button (Project+'s default
# controls, notes in src/main/cpu/fighter_input.h): X and Y are jump with a
# 2-bit direction, L and R guard.
INPUT_BITS = {"X": 0xC4, "Y": 0x84, "L": 0x8, "R": 0x8}
LA_ANALOG_L, LA_NEW_LR, LA_CLOCK = 36, 77, 0x4E


class ControlError(RuntimeError):
    pass


class Probe:
    """Mario's state after a frame, read from the game's memory."""

    def __init__(self, r):
        e = self.emu = r.emu
        mods = r.f.modules(r.m)
        self.status = LT.u32(mods, 0x70) + 0x34
        self.motion = LT.u32(mods, 0x8) + 0x40
        self.posture = LT.u32(mods, 0xC) + 0xC
        la = e.ptr(LT.u32(mods, 0x64) + 0x20)
        self.ints, self.floats = e.u32(la + 0xC), e.u32(la + 0x14)
        arr = e.ptr(LT.ENTRY_MANAGER)
        self.input = None
        for i in range(min(e.u32(LT.ENTRY_MANAGER + 4), 9)):
            raw = e.read(arr + i * LT.ENTRY_SIZE, 0x60)
            if LT.s32(raw, 4) == r.m["entry"]:
                # ftEntry +0x2C -> ftInput, whose +0xC is the input object:
                # buttons at +0xC, the main stick at +0x10.
                self.input = e.u32(e.u32(arr + i * LT.ENTRY_SIZE + 0x2C) + 0xC)
        if not self.input or not e.is_ram(self.input):
            raise ControlError("Mario's input object was not found")

    def row(self):
        e = self.emu
        raw = e.read(self.input + 0xC, 0xC)
        analog = struct.unpack(">2f", e.read(self.floats + 4 * LA_ANALOG_L, 8))
        return {"status": e.u32(self.status), "anim": e.f32(self.motion), "x": e.f32(self.posture),
                "buttons": LT.u32(raw, 0) & 0x7FFF, "stick": LT.f32(raw, 4), "analog": analog,
                "lr": LT.s32(e.read(self.ints + 4 * LA_NEW_LR, 4), 0),
                "clock": LT.s32(e.read(self.ints + 4 * LA_CLOCK, 4), 0)}


class Checks:
    """Collects failed checks; with keep_going a scenario runs to its end."""

    def __init__(self, r, keep_going):
        self.r, self.keep_going, self.failed = r, keep_going, []

    def check(self, cond, name, detail):
        if cond:
            self.r.ok(name, detail)
            return
        self.failed.append(f"{name}: {detail}")
        self.r.note(f"FAIL {name}: {detail}")
        if not self.keep_going:
            raise ControlError(f"{name}: {detail}")


def play(r, probe, pads):
    """One emulated frame per entry of `pads` (keyword arguments of
    Emu.hold), the game paused in between; Mario's row after each."""
    rows = []
    for pad in pads:
        r.emu.hold(0, **pad)
        r.emu.h.call("frame_advance", n=1)
        rows.append(probe.row())
    return rows


def steps(before, rows):
    """Game steps over `rows`: a step moves the animation frame or changes
    the status (also when a looping animation goes back to its start)."""
    n, prev = 0, before
    for row in rows:
        n += (row["status"], row["anim"]) != (prev["status"], prev["anim"])
        prev = row
    return n


def arm(r, probe, index):
    """The base state with Frame Advance on and Frame Advance Button
    `index`, the game held; returns Mario's row."""
    r.load(running=False)
    r.set(S_ADVANCE_BUTTON, index)
    r.set(S_ADVANCE_DELAY, HOLD_DELAY)
    r.set(S_FRAME_ADVANCE, 1)
    rows = play(r, probe, [{}] * 24)
    if steps(rows[-13], rows[-12:]) != 0 or rows[-1]["status"] != WAIT:
        raise ControlError(f"frame advance did not hold Mario standing: {rows[-1]}")
    return rows[-1]


def unseen(c, label, rows, button):
    """The advance button never reached the fighter in `rows`."""
    acted = sorted({hex(x["status"]) for x in rows if x["status"] != WAIT})
    held = sorted({hex(x["buttons"]) for x in rows if x["buttons"]})
    c.check(not acted and not held, f"{label}: the fighter never saw {button}",
            f"statuses besides Wait {acted or 'none'}, input object buttons {held or 'none'}")
    analog = sorted({x["analog"] for x in rows if any(x["analog"])})
    lr = sorted({x["lr"] for x in rows if x["lr"]})
    c.check(not analog and not lr, f"{label}: Project+'s analog trigger variables stay 0",
            f"LA-Float 36/37 {analog or 'always 0'}, LA-Basic 77 {lr or 'always 0'}")


def sc_advance(r, c, index):
    name = BUTTONS[index]
    probe = r.probe
    press = dict(buttons=[name])
    label = f"Frame Advance Button {name}"

    start = arm(r, probe, index)
    rows = play(r, probe, ([press] * 2 + [{}] * 10) * 6)
    n = steps(start, rows)
    c.check(n == 6 and rows[-1]["clock"] - start["clock"] == 6, f"{label}: six presses, six steps",
            f"{n} steps, Mario's Wait counter {start['clock']} -> {rows[-1]['clock']}")
    unseen(c, f"{label}, presses", rows, name)

    start = arm(r, probe, index)
    rows = play(r, probe, [press] * 80)
    after = play(r, probe, [{}] * 12)
    n, late = steps(start, rows), steps(after[-7], after[-6:])
    c.check(80 - HOLD_DELAY - 8 <= n <= 80 - HOLD_DELAY + 2 and late == 0,
            f"{label}: held 80 frames, the game runs after the hold delay and stops on release",
            f"{n} steps while held (hold delay {HOLD_DELAY}), {late} in the last 6 frames after release")
    unseen(c, f"{label}, held", rows + after, name)

    start = arm(r, probe, index)
    left = r.distance() < 0  # toward the CPU, the way Mario faces
    stick = dict(stick=(28, 128) if left else (228, 128))
    both = dict(stick, buttons=[name])
    rows = play(r, probe, [stick] * 12)
    c.check(steps(start, rows) == 0, f"{label}: the stick alone runs no step", "0 steps in 12 frames")
    rows = play(r, probe, ([both] * 2 + [stick] * 10) * 3)
    n, last = steps(start, rows), rows[-1]
    moved = (last["x"] - start["x"]) * (-1 if left else 1)
    c.check(n == 3 and moved > 0.05 and abs(last["stick"]) > 0.9 and last["status"] != WAIT,
            f"{label}: the stick held with it reaches the fighter on the advanced frames",
            f"{n} steps, status {last['status']:#x}, input object stick {last['stick']:+.2f}, moved {moved:.2f}")
    held = sorted({hex(x["buttons"]) for x in rows if x["buttons"]})
    c.check(not held, f"{label}: the stick arrives without the button", f"input object buttons {held or 'none'}")

    other = "Y" if name == "X" else "X"
    start = arm(r, probe, index)
    jump = dict(buttons=[other])
    rows = play(r, probe, [jump] * 12)
    c.check(steps(start, rows) == 0, f"{label}: {other} alone runs no step", "0 steps in 12 frames")
    rows = play(r, probe, ([dict(buttons=[other, name])] * 2 + [jump] * 10) * 2)
    n = steps(start, rows)
    stepped = [x for x in rows if (x["status"], x["anim"]) != (start["status"], start["anim"])]
    seen = sorted({hex(x["buttons"]) for x in stepped})
    c.check(n == 2 and stepped and all(x["status"] == JUMP_SQUAT for x in stepped)
            and seen == [hex(INPUT_BITS[other])],
            f"{label}: {other} held with it reaches the fighter, and only {other}",
            f"{n} steps, statuses {sorted({hex(x['status']) for x in stepped})}, input object buttons {seen} "
            f"({other} alone is {INPUT_BITS[other]:#x})")

    if name in ("L", "R"):
        other = "R" if name == "L" else "L"
        mine, theirs = (0, 1) if name == "L" else (1, 0)
        start = arm(r, probe, index)
        shield = dict(buttons=[other])
        rows = play(r, probe, [shield] * 12 + ([dict(buttons=[other, name])] * 2 + [shield] * 10) * 2)
        n = steps(start, rows)
        stepped = [x for x in rows if (x["status"], x["anim"]) != (start["status"], start["anim"])]
        c.check(n == 2 and stepped and all(x["status"] in (GUARD_ON, GUARD) for x in stepped)
                and all(x["analog"][mine] == 0 for x in rows) and rows[-1]["analog"][theirs] > 0.9,
                f"{label}: {other} held with it shields, with {other}'s analog value only",
                f"{n} steps, statuses {sorted({hex(x['status']) for x in stepped})}, LA-Float 36/37 at the end "
                f"{rows[-1]['analog']}, {name}'s largest {max(x['analog'][mine] for x in rows)}")
    r.set(S_FRAME_ADVANCE, 0)


# ---- lab menu button and D-pad isolation ------------------------------------------


def since(emu, pos, regex):
    return re.findall(regex, emu.log_text()[pos:])


def menu_open(emu):
    log = emu.log_text()
    return len(re.findall(r"lab: menu opened", log)) > len(re.findall(r"lab: menu closed", log))


# The savestate's own lines for the quick state (DPad Left, DPad Right); the
# recording's state has "recording state" before the verb.
QUICK_STATE = r"savestate: (?:saved frame|loaded frame|save refused|load refused)[^\r\n]*"


def sc_shortcuts(r, c):
    e, lab = r.emu, r.lab
    r.load()
    e.advance(20)
    default = lab.setting(S_START_OPENS)
    c.check(default == 1, "Lab Menu Button starts at Start", f"{default}")
    # Z in Pause: Z opens and closes the lab menu.
    r.set(S_START_OPENS, 0)
    r.pause()
    lab.open_menu()
    pos = lab.log_pos()
    lab.tap(["Z"])
    e.advance(6)
    c.check(bool(since(e, pos, r"lab: menu closed")), "Z in Pause: Z closes the menu", "closed")
    if menu_open(e):
        lab.close_menu()
    r.resume()
    e.advance(30)

    # Start mode: the menu opens over the running match.
    r.set(S_START_OPENS, 1)
    pos = lab.log_pos()
    e.press(0, ["START"], frames=3, after=10)
    lab.wait_log("the lab menu to open with Start", r"lab: menu opened by port 1 \(Start\)", pos)
    # Move to a Draft C rail utility row (Reset Settings) so holding DRIGHT
    # does not enter the Lab pane and let DLEFT toggle Frame Advance on.
    lab.down(LT.MAIN_RESET)
    pos = lab.log_pos()
    # D-pad left and right are the savestate bindings in a running match
    # (DPad Left = Load State, DPad Right = Save State, the defaults): with
    # the menu open they must stay isolated from savestates even when held
    # past the save-hold threshold.
    for held in (["DRIGHT"], ["DLEFT"]):
        e.hold(0, held)
        e.advance(16)
        e.neutral(0)
        e.advance(6)
    quick = since(e, pos, QUICK_STATE)
    c.check(not quick, "Start: D-pad left and right neither load nor save a state while the menu is open",
            f"savestate lines {quick or 'none'}")
    pos = lab.log_pos()
    e.press(0, ["START"], frames=3, after=10)
    c.check(bool(since(e, pos, r"lab: menu closed")) and not nav.training_paused(e),
            "Start: Start closes the menu without opening Training's pause",
            f"closed, Training's pause open {nav.training_paused(e)}")
    e.advance(30)
    # A button held while the menu closes stays hidden from the fighter
    # until it is let go (the fighter last saw the pad before the menu held
    # the game, so the game would take it as a new press); then it works.
    pos = lab.log_pos()
    e.press(0, ["START"], frames=3, after=10)
    lab.wait_log("the lab menu to open with Start", r"lab: menu opened by port 1 \(Start\)", pos)
    e.hold(0, ["X"])
    e.advance(6)
    pos = lab.log_pos()
    e.hold(0, ["X", "START"])
    e.advance(3)
    e.hold(0, ["X"])
    seen = set()
    for _ in range(40):
        e.advance(1)
        seen.add(r.f.state(r.m)["status"])
    closed = bool(since(e, pos, r"lab: menu closed"))
    e.neutral(0)
    e.advance(6)
    e.hold(0, ["X"])
    after = set()
    for _ in range(8):
        e.advance(1)
        after.add(r.f.state(r.m)["status"])
    e.neutral(0)
    c.check(closed and seen == {WAIT} and JUMP_SQUAT in after,
            "Start: X held while the menu closes reaches the fighter only once let go and pressed again",
            f"menu closed {closed}; statuses while still held {sorted(hex(s) for s in seen)}, "
            f"after release and a new press {sorted(hex(s) for s in after)}")
    e.wait_for("Mario to stand again", lambda: r.f.state(r.m)["status"] == WAIT, 300, every=5)
    # With the menu closed the bindings are the savestate's again.
    e.advance(30)
    pos = lab.log_pos()
    e.hold(0, ["DRIGHT"])
    e.advance(16)
    e.neutral(0)
    e.advance(40)
    e.press(0, ["DLEFT"], frames=3, after=40)
    quick = since(e, pos, QUICK_STATE)
    c.check(len(quick) == 2 and quick[0].startswith("savestate: saved frame")
            and quick[1].startswith("savestate: loaded frame"),
            "Start: with the menu closed, D-pad right saves a state and D-pad left loads it",
            f"savestate lines {[q[:40] for q in quick] or 'none'}")
    r.set(S_START_OPENS, default)
    e.neutral(0)
    e.advance(30)


SCENARIOS = [f"advance-{b.lower()}" for b in BUTTONS] + ["shortcuts"]


def cmd_test_controls(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-controls")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--keep-going", action="store_true", help="run every check of a scenario after one fails")
    opts = ap.parse_args(rest)
    label = "test-controls"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, shots, notes = "", "boot", "", [], []
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    r = c = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            r = DT.Runner(emu, notes, shots)
            c = Checks(r, opts.keep_going)
            try:
                step = "base state"
                DT.make_base(r, states)
                r.probe = Probe(r)
                for name in SCENARIOS:
                    if opts.only and name not in opts.only:
                        continue
                    step = name
                    if name == "shortcuts":
                        sc_shortcuts(r, c)
                    else:
                        sc_advance(r, c, SCENARIOS.index(name))
            except (Timeout, nav.NavError, HarnessError, DT.DisplayError, LT.LabError, PI.InputError,
                    ControlError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    shots.append(emu.screenshot("fail-controls"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    for name, detail in (r.results if r else []):
        print(f"ok  {name}: {detail}")
    for line in (c.failed if c else []):
        print(f"FAIL {line}")
    if not why and c and c.failed:
        why = f"{len(c.failed)} checks failed"
    for path, w, h in shots:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
