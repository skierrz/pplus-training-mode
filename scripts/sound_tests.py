"""Emulator test of the menu sounds (run through emu.py; settings come from
the environment the same way).

  test-sounds REL
      One boot into Training with the Draft B boot launcher open. The plugin
      logs every sound it asks the game for (`menu: sound N` from the boot
      launcher and the browser, `ui: sound N` from the lab menu) with the
      handle the game's sndSystem::playSE returned. Every action is one
      scripted press; the log lines between the press and a few frames later
      must be exactly the sound of that action, once, with a handle the game
      gave out (not -1):
        - the Draft B boot launcher ("All Modes", "Offense", "Defense"):
          nothing when it opens by itself; a move (0) for a new row and for
          L/R section changes, nothing for a move past the top or bottom end;
          a confirm (1) for X (the browser) and for Training Lab; a back (2)
          for B; an open (5) for L+R;
        - the recordings browser, with no recordings on the card: an error (3)
          for A and for X, nothing for the stick, a back (2) for B;
        - the in-match Draft C Rail lab menu, Menu Open = Start (the default):
          an open (5) for Start, a close (6) for Z (Training's pause opens in
          its place, and closes with Start without a lab sound), a close (6)
          for Start closing it;
        - the same with Z in Pause: an open and a close for Z, B from options
          back to the rail (back), B on the rail closing the menu (close); a
          move for a new rail/option row, a changed value and a toggle,
          nothing at the top or bottom of a page and at a value's end; a
          confirm for entering a category (Lab, Display) and for the Return to
          CSS rail utility (which closes without the close sound and leaves
          the match: no sound until Training's character select).
"""
import argparse
import re
import time

import drill_tests as DT
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
from harness import HarnessError, Timeout

MOVE, CONFIRM, BACK, ERROR, OPEN, CLOSE = 0, 1, 2, 3, 5, 6
NAMES = {MOVE: "move", CONFIRM: "confirm", BACK: "back", ERROR: "error", OPEN: "open", CLOSE: "close"}
SOUND = re.compile(r"\] (menu|ui): sound (\d+) \(handle (-?\d+)\)")
UP, DOWN, LEFT, RIGHT = LT.UP, LT.DOWN, LT.LEFT, LT.RIGHT
DISPLAY_ROWS = LT.DISPLAY_ROWS  # Display, every row built
S_FRAME_ADVANCE, S_PLAYER_PERCENT = LT.SET_FRAME_ADVANCE, LT.SET_PLAYER_PERCENT


class SoundError(RuntimeError):
    pass


class Sounds:
    def __init__(self, emu):
        self.emu = emu
        self.rows = []
        self.bad = []
        self.counted = 0

    def since(self, pos):
        return [(m.group(1), int(m.group(2)), int(m.group(3))) for m in SOUND.finditer(self.emu.log_text(), pos)]

    def expect(self, where, name, action, want):
        """`action` must make exactly the sounds `want` (ids), from `where`."""
        pos = len(self.emu.log_text())
        action()
        self.emu.advance(8)
        got = self.since(pos)
        self.counted += len(got)
        ok = [g[1] for g in got] == want and all(g[0] == where and g[2] >= 0 for g in got)
        told = ", ".join(f"{NAMES.get(i, i)} ({i}) handle {h}" for _, i, h in got) or "none"
        wanted = ", ".join(f"{NAMES[i]} ({i})" for i in want) or "none"
        self.rows.append((ok, name, wanted, told))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: wanted {wanted}; got {told}")
        if not ok:
            self.bad.append(name)

    def moves(self, where, name, action, count):
        """`action` moves the cursor `count()` rows: one move sound each."""
        pos = len(self.emu.log_text())
        action()
        self.emu.advance(8)
        got = self.since(pos)
        self.counted += len(got)
        n = count(pos)
        ok = len(got) == n and all(g == (where, MOVE, g[2]) and g[2] >= 0 for g in got)
        self.rows.append((ok, name, f"{n} moves", f"{len(got)} sounds {sorted({g[1] for g in got})}"))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {n} rows moved, {len(got)} sounds, ids {sorted({g[1] for g in got})}")
        if not ok:
            self.bad.append(name)

    def check(self, ok, name, wanted, told):
        self.rows.append((ok, name, wanted, told))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: wanted {wanted}; got {told}")
        if not ok:
            self.bad.append(name)


def run(emu, s, state):
    press = lambda buttons=(), stick=(128, 128): emu.press(0, list(buttons), frames=3, after=6, stick=stick)  # noqa: E731
    state["step"] = "boot into Training with the launcher open"
    nav.boot_to_training_css(emu, drill_list="keep")
    DT.wait_count(emu, "the launcher to open by itself", r"\[{name}\] drills: list opened \(auto\)", 1, 300)
    emu.advance(40)

    state["step"] = "4-tab boot launcher (Quick Start / Scenarios / Tech Practice / Minigames & Tech)"
    s.expect("menu", "launcher: opening by itself", lambda: None, [])
    if s.since(0):
        raise SoundError(f"sounds before any press: {s.since(0)}")
    # Page 0 ("Quick Start"): row 0 = Training Lab, row 1 = Combo Lab.
    s.expect("menu", "launcher: stick up on the first row", lambda: press(stick=UP), [])
    s.expect("menu", "launcher: down to Combo Lab", lambda: press(stick=DOWN), [MOVE])
    s.expect("menu", "launcher: back up to Training Lab", lambda: press(stick=UP), [MOVE])
    # Switch sections with R: Quick Start -> Scenarios (10 rows) -> Tech Practice -> Minigames & Tech -> Quick Start.
    s.expect("menu", "launcher: R to Scenarios page", lambda: press(["R"]), [MOVE])
    if DT.last_page(emu) != "Scenarios":
        raise SoundError(f"expected Scenarios after R, got {DT.last_page(emu)!r}")
    s.moves("menu", "launcher: down to bottom of Scenarios (row 9)",
            lambda: [press(stick=DOWN) for _ in range(9)], lambda pos: 9)
    s.expect("menu", "launcher: stick down on the last row of Scenarios", lambda: press(stick=DOWN), [])
    s.expect("menu", "launcher: R to Tech Practice page", lambda: press(["R"]), [MOVE])
    if DT.last_page(emu) != "Tech Practice":
        raise SoundError(f"expected Tech Practice after R, got {DT.last_page(emu)!r}")
    s.expect("menu", "launcher: R to Minigames & Tech page", lambda: press(["R"]), [MOVE])
    if DT.last_page(emu) != "Minigames & Tech":
        raise SoundError(f"expected Minigames & Tech after R, got {DT.last_page(emu)!r}")
    s.expect("menu", "launcher: R wraps to Quick Start page", lambda: press(["R"]), [MOVE])
    if DT.last_page(emu) != "Quick Start":
        raise SoundError(f"expected Quick Start after R wrap, got {DT.last_page(emu)!r}")

    s.expect("menu", "launcher: Y opens Quick Setup", lambda: press(["Y"]), [CONFIRM])
    s.expect("menu", "Quick Setup: B returns to launcher", lambda: press(["B"]), [MOVE])
    s.expect("menu", "launcher: X opens the browser", lambda: press(["X"]), [CONFIRM])
    if not re.search(r"\] browser: 0 recording\(s\) listed", emu.log_text()):
        raise SoundError("the browser lists recordings: this test expects none on the card")
    state["step"] = "the browser"
    s.expect("menu", "browser: A with nothing to load", lambda: press(["A"]), [ERROR])
    s.expect("menu", "browser: X with nothing to delete", lambda: press(["X"]), [ERROR])
    s.expect("menu", "browser: the stick with nothing listed", lambda: press(stick=DOWN), [])
    s.expect("menu", "browser: B goes back", lambda: press(["B"]), [BACK])
    state["step"] = "the launcher again"
    s.expect("menu", "launcher: A on Training Lab", lambda: press(["A"]), [CONFIRM])
    if nav.drill_list_state(emu) != "closed":
        raise SoundError("Training Lab did not close the launcher")
    s.expect("menu", "CSS: L+R reopens launcher", lambda: press(["L", "R"]), [OPEN])
    emu.advance(20)
    s.expect("menu", "launcher: Tap B closes to CSS", lambda: press(["B"]), [BACK])
    if nav.drill_list_state(emu) != "closed":
        raise SoundError("Tap B did not close the launcher")
    s.expect("menu", "CSS: L+R reopens launcher (2)", lambda: press(["L", "R"]), [OPEN])
    emu.advance(20)
    s.expect("menu", "launcher: A on Training Lab (2)", lambda: press(["A"]), [CONFIRM])
    if nav.drill_list_state(emu) != "closed":
        raise SoundError("Training Lab did not close the launcher (2)")
    in_css = s.counted
    if len(s.since(0)) != in_css:
        raise SoundError(f"{len(s.since(0))} sound lines on the character select, {in_css} of them from a press")

    state["step"] = "a Training match"
    nav.start_training_match(emu, "mario", "fox")
    emu.advance(90)
    lab = LT.Lab(emu, [])
    lab.find_settings()
    before_menu = len(s.since(0))
    tap = lambda buttons=(), stick=(128, 128): lab.tap(buttons, stick)  # noqa: E731
    state["step"] = "the lab menu with Start (the default Menu Open)"
    if lab.setting(LT.SET_START_OPENS) != 1:
        raise SoundError("Menu Open is not Start")
    s.expect("ui", "lab menu: Start opens it", lambda: tap(["START"]), [OPEN])
    s.expect("ui", "lab menu: Z leaves it for Training's pause", lambda: tap(["Z"]), [CLOSE])
    emu.wait_for("Training's pause after Z", lambda: nav.training_paused(emu), 60, every=2)
    s.expect("ui", "Training's pause: Start closes it", lambda: tap(["START"]), [])
    emu.wait_for("Training to resume", lambda: not nav.training_paused(emu), 60, every=2)
    emu.advance(20)
    s.expect("ui", "lab menu: Start opens it again", lambda: tap(["START"]), [OPEN])
    s.expect("ui", "lab menu: Start closes it", lambda: tap(["START"]), [CLOSE])
    emu.advance(20)

    state["step"] = "the Draft C Rail lab menu with Z in Pause"
    LT.write_u8(emu, lab, LT.SET_START_OPENS, 0)
    nav.pause_training(emu)
    emu.advance(20)
    # Opens on left category rail (Lab highlighted).
    s.expect("ui", "lab menu: Z opens it", lambda: tap(["Z"]), [OPEN])
    s.expect("ui", "lab menu: stick up on first rail row", lambda: tap(stick=UP), [])
    s.expect("ui", "lab menu: down to Scenarios on rail", lambda: tap(stick=DOWN), [MOVE])
    s.expect("ui", "lab menu: back up to Lab on rail", lambda: tap(stick=UP), [MOVE])
    s.expect("ui", "lab menu: A enters Lab", lambda: tap(["A"]), [CONFIRM])
    s.expect("ui", "lab menu: A turns Frame Advance on", lambda: tap(["A"]), [MOVE])
    s.expect("ui", "lab menu: A turns it off", lambda: tap(["A"]), [MOVE])
    if lab.setting(S_FRAME_ADVANCE) != 0:
        raise SoundError("Frame Advance is still on")
    s.expect("ui", "lab menu: down to P1 Damage", lambda: tap(stick=DOWN), [MOVE])
    s.expect("ui", "lab menu: Left at the lowest value", lambda: tap(stick=LEFT), [])
    s.expect("ui", "lab menu: Right changes the value", lambda: tap(stick=RIGHT), [MOVE])
    s.expect("ui", "lab menu: Left changes it back", lambda: tap(stick=LEFT), [MOVE])
    if lab.setting(S_PLAYER_PERCENT, half=True) != 0:
        raise SoundError("P1 Damage is not back at 0")
    s.expect("ui", "lab menu: B returns to Draft C rail", lambda: tap(["B"]), [MOVE])
    s.moves("ui", "lab menu: down to Display on rail", lambda: lab.main_to(LT.MAIN_DISPLAY),
            lambda pos: sum(1 for r in range(1, LT.MAIN_DISPLAY + 1) if r not in LT.MAIN_DISABLED))
    s.expect("ui", "lab menu: A enters Display", lambda: tap(["A"]), [CONFIRM])
    s.moves("ui", "lab menu: down to last row of Display", lambda: lab.down(DISPLAY_ROWS - 1),
            lambda pos: DISPLAY_ROWS - 1)
    s.expect("ui", "lab menu: stick down on last row of Display", lambda: tap(stick=DOWN), [])
    s.expect("ui", "lab menu: Z closes it from Display", lambda: tap(["Z"]), [CLOSE])
    # Reopens where it was left: on Display; B returns to rail (Display), second B closes.
    s.expect("ui", "lab menu: Z opens it again", lambda: tap(["Z"]), [OPEN])
    s.expect("ui", "lab menu: B back to Draft C rail", lambda: tap(["B"]), [MOVE])
    s.expect("ui", "lab menu: B on the rail closes it", lambda: tap(["B"]), [CLOSE])
    s.expect("ui", "lab menu: Z opens it again on rail", lambda: tap(["Z"]), [OPEN])
    s.moves("ui", "lab menu: down to Return to CSS on rail", lambda: lab.main_to(LT.MAIN_EXIT, LT.MAIN_DISPLAY),
            lambda pos: sum(1 for r in range(LT.MAIN_DISPLAY + 1, LT.MAIN_EXIT + 1) if r not in LT.MAIN_DISABLED))
    pos = len(emu.log_text())
    s.expect("ui", "lab menu: A on Return to CSS", lambda: tap(["A"]), [CONFIRM])
    if "lab: menu closed" not in emu.log_text()[pos:]:
        raise SoundError("Return to CSS did not close the lab menu")
    in_menu = len(s.since(0)) - before_menu
    if s.counted - in_css != in_menu:
        raise SoundError(f"{in_menu} sound lines from the lab menu, {s.counted - in_css} of them from a press")
    state["step"] = "leaving the match"
    emu.wait_for("Training's character select after Return to CSS", lambda: nav.scene(emu)[0] == "scSelctCharacter",
                 300, every=2)
    emu.advance(30)
    if len(s.since(0)) != s.counted:
        raise SoundError("a sound without a press after the menu closed")


def cmd_test_sounds(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-sounds")
    ap.add_argument("rel")
    ap.parse_args(rest)
    label = "test-sounds"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    state = {"step": "boot"}
    s = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            s = Sounds(emu)
            try:
                run(emu, s, state)
            except (Timeout, nav.NavError, HarnessError, SoundError, LT.LabError) as e:
                why = f"{state['step']}: {e}"
                try:
                    emu.screenshot("fail-sounds")
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{state['step']}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    if s is not None:
        for ok, name, wanted, told in s.rows:
            print(f"{'ok  ' if ok else 'FAIL'} {name}: wanted {wanted}; got {told}")
        print(f"{len(s.rows)} actions, {s.counted} sounds")
        if s.bad and not why:
            why = f"{len(s.bad)} actions with the wrong sounds: {s.bad}"
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
