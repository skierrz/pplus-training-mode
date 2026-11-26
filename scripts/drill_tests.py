"""Emulator test of the boot launcher (the menu module) on Training's character
select, run through emu.py like the other tests.

  test-drills REL [--cycles N]
      One boot into Training. The Draft B 3-section launcher ("All Modes",
      "Offense", "Defense") must open by itself over the character select with
      Training Lab highlighted, switch sections with L/R, scroll and clamp with
      key repeat, and keep every input from the character select. Training Lab
      closes it; a normal match is then played from the character select (the
      menu module unloaded with its heap back at baseline, the match module
      loaded). Back on the character select the list stays closed until L+R.
      Arming a Scenario from "Offense" auto-activates it on match load. Then
      the menu module's test block starts Mario against a Fox CPU on Final
      Destination directly, the lab menu opens in that match, and Finish
      brings the list back; N such cycles (default 10), then one quick match
      on each of Final Destination, Battlefield and Green Hill Zone, every
      load and unload checked. Wii Remote, Nunchuk, and Classic Controller
      navigation, browser import, and in-match lab controls are also checked.
"""
import argparse
import math
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import emu as E  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import shots  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

MENU_LOADED = (r"\[{name}\] module MENU: loaded v\S+ at ([0-9a-f]{{8}}) in (\w+); free (\w+) largest (\w+) "
               r"blocks (\d+) -> free (\w+) largest (\w+)")
MENU_UNLOADED = (r"\[{name}\] module MENU: unloaded at scene exit; free (\w+) largest (\w+) blocks (\d+), "
                 r"back to baseline: (yes|NO)")
# The test quick match: Mario (kind 0) against a Fox CPU (kind 7) on Final
# Destination (stage kind 2) by default.
QUICK_SETUP = {"stage": nav.QUICK_STAGES["final_destination"], "kinds": [0x00, 0x07], "states": [0, 1]}

DOWN, UP = (128, 28), (128, 228)


def args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-drills")
    ap.add_argument("rel")
    ap.add_argument("--cycles", type=int, default=10)
    ap.add_argument("--only", choices=("all", "gc", "wii", "wii-list", "wii-record", "wii-inmatch"),
                    default="all")
    return ap.parse_args(rest)


def count(emu, pattern):
    return len(L.rx(pattern).findall(emu.log_text()))


def wait_count(emu, what, pattern, n, frames=600):
    """Wait until `pattern` has matched at least `n` times in the log."""
    emu.wait_for(what, lambda: count(emu, pattern) >= n, frames, every=5)


def last_row(emu):
    """The list's highlighted row on this visit, from its log lines."""
    log = emu.log_text()
    tail = log[log.rfind("] menu: attached"):]
    # A new page starts on its first row.
    rows = re.findall(r"\] drills: (?:(?:list opened .*?, row|row) (\d+) |page [^\r\n]+)", tail)
    if not rows:
        raise nav.NavError("no drill list row in the log for this visit")
    return int(rows[-1] or 0)


def last_page(emu):
    """The list's page on this visit, from its log lines."""
    log = emu.log_text()
    tail = log[log.rfind("] menu: attached"):]
    pages = re.findall(r"\] drills: page ([^\r\n]+)", tail)
    if pages:
        return pages[-1].strip()
    if re.search(r"\] drills: list opened .*?, row 0 Training Lab", tail):
        return "All Modes"
    raise nav.NavError("no drill list page in the log for this visit")


def go_to_page(emu, title, button="R"):
    """R steps to the next page, L to the one before."""
    for _ in range(8):
        try:
            cur = last_page(emu)
        except nav.NavError:
            cur = None
        if cur == title:
            return
        n = count(emu, r"\] drills: page ")
        emu.press(0, [button], frames=3, after=6)
        wait_count(emu, f"the list's page to change toward {title}", r"\] drills: page ", n + 1, 120)
    raise nav.NavError(f"the list did not reach page {title} (on {last_page(emu)})")


def go_to_row(emu, row):
    for _ in range(30):
        now = last_row(emu)
        if now == row:
            return
        n = count(emu, r"\] drills: row ")
        emu.press(0, stick=DOWN if row > now else UP, frames=3, after=4)
        wait_count(emu, f"the list to move toward row {row}", r"\] drills: row ", n + 1, 120)
    raise nav.NavError(f"the list did not reach row {row} (at {last_row(emu)})")


def css_snapshot(emu):
    """What the character select shows: P1's hand and both tokens."""
    out = [nav.scene(emu)[:2]]
    for i in (0, 1):
        a = nav.css_area(emu, i)
        if a is None:
            return out
        out.append({k: (round(v, 3) if isinstance(v, float) else v) for k, v in a.items() if k != "area"})
    return out


def band_colour(path, y_frac, x0_frac, x1_frac):
    """Mean (r, g, b) of a 3-pixel-high band of the screenshot."""
    w, h, ch, rows = shots.read_png(path)
    y = int(y_frac * h)
    xs = range(int(x0_frac * w), int(x1_frac * w))
    total = [0, 0, 0]
    n = 0
    for yy in (y - 1, y, y + 1):
        row = rows[yy]
        for x in xs:
            for c in range(3):
                total[c] += row[x * ch + c]
            n += 1
    return tuple(t / n for t in total)


def row_highlighted(path, row, x0=260.0, x1=295.0):
    """True if the boot menu's solid P+ yellow highlight (0xFFD300FF) is on
    visible row `row` (kColTop = 100, kColPitch = 30, kColRowH = 26)."""
    centre = 100.0 + 26.0 / 2.0 + row * 30.0
    r, g, b = band_colour(path, centre / 480.0, x0 / 640.0, x1 / 640.0)
    return r > 200 and g > 165 and b < 60, (round(r), round(g), round(b))


def shot(emu, taken, name):
    taken.append(emu.screenshot(name))
    return taken[-1][0]


def check_list_screens(emu, taken, notes):
    """4-section boot menu (All Modes: 19 rows, Offense: 5 rows, Defense: 7
    rows, Tech Practice: 8 rows), section switching, scrolling + key repeat,
    Recordings browser (X), Quick OSDs (Y), and the character select left
    alone under it."""
    emu.advance(40)
    path = shot(emu, taken, "drills-list-top")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    notes.append(f"first screenshot (All Modes, row 0 Training Lab): row 0 {c0}, row 1 {c1}")
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Training Lab alone (row 0 {c0}, row 1 {c1})")

    before = css_snapshot(emu)
    # On All Modes (Page 0, 19 rows), step down to row 1 (L-Cancel).
    n = count(emu, r"\] drills: row ")
    emu.press(0, ["DDOWN"], frames=4, after=10)
    wait_count(emu, "row 1 (L-Cancel)", r"\] drills: row ", n + 1, 90)
    if last_row(emu) != 1:
        raise nav.NavError(f"expected row 1 after Down on All Modes, at {last_row(emu)}")
    path = shot(emu, taken, "drills-list-row1")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if hi0 or not hi1:
        raise nav.NavError(f"the highlight is not on L-Cancel alone (row 0 {c0}, row 1 {c1})")

    # Hold Down with key repeat to row 26 (Browse Recordings, scrolled by 17 -> visible row 9)
    # and verify bottom clamp.
    emu.hold(0, stick=DOWN)
    emu.advance(220)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 26:
        raise nav.NavError(f"Down repeat did not reach bottom All Modes row 26, at {last_row(emu)}")
    emu.hold(0, stick=DOWN)
    emu.advance(20)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 26:
        raise nav.NavError(f"Down at bottom row moved off row 26 to {last_row(emu)}")
    path = shot(emu, taken, "drills-list-all-bottom")
    hi9, c9 = row_highlighted(path, 9)
    hi8, c8 = row_highlighted(path, 8)
    if not hi9 or hi8:
        raise nav.NavError(f"the highlight is not on visible row 9 alone (row 8 {c8}, row 9 {c9})")

    # Switch to Offense (Page 1, 5 rows: Combo Lab .. Grab Defense / Throw DI), check top and bottom clamp.
    go_to_page(emu, "Offense", "R")
    emu.advance(10)
    path = shot(emu, taken, "drills-list-offense-top")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Offense row 0 alone (row 0 {c0}, row 1 {c1})")
    emu.hold(0, stick=DOWN)
    emu.advance(50)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 4:
        raise nav.NavError(f"Down repeat did not reach bottom Offense row 4, at {last_row(emu)}")
    emu.hold(0, stick=DOWN)
    emu.advance(20)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 4:
        raise nav.NavError(f"Down at bottom Offense row moved off row 4 to {last_row(emu)}")
    path = shot(emu, taken, "drills-list-offense-bottom")
    hi4, c4 = row_highlighted(path, 4)
    hi3, c3 = row_highlighted(path, 3)
    if not hi4 or hi3:
        raise nav.NavError(f"the highlight is not on Offense row 4 alone (row 3 {c3}, row 4 {c4})")

    # Switch to Defense (Page 2, 7 rows: Shield Pressure / OoS .. P+ Multi-Hit SDI).
    go_to_page(emu, "Defense", "R")
    emu.advance(10)
    path = shot(emu, taken, "drills-list-defense")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Defense row 0 alone (row 0 {c0}, row 1 {c1})")

    # Switch to Tech Practice (Page 3, 8 single-page rows: L-Cancel .. Float / Hover & DJC).
    go_to_page(emu, "Tech Practice", "R")
    emu.advance(10)
    path = shot(emu, taken, "drills-list-tech-practice")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Tech Practice row 0 alone (row 0 {c0}, row 1 {c1})")
    emu.hold(0, stick=DOWN)
    emu.advance(80)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 7:
        raise nav.NavError(f"Down repeat did not reach bottom Tech Practice row 7, at {last_row(emu)}")

    # Switch to Challenges (Page 4, 7 rows) and Character Trainers (Page 5, 1 row), then wrap to All Modes.
    go_to_page(emu, "Challenges", "R")
    emu.advance(10)
    path = shot(emu, taken, "drills-list-challenges")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Challenges row 0 alone (row 0 {c0}, row 1 {c1})")
    emu.hold(0, stick=DOWN)
    emu.advance(70)
    emu.neutral(0)
    emu.advance(10)
    if last_row(emu) != 6:
        raise nav.NavError(f"Down repeat did not reach bottom Challenges row 6, at {last_row(emu)}")

    go_to_page(emu, "Character Trainers", "R")
    emu.advance(10)
    path = shot(emu, taken, "drills-list-character-trainers")
    hi0, c0 = row_highlighted(path, 0)
    hi1, c1 = row_highlighted(path, 1)
    if not hi0 or hi1:
        raise nav.NavError(f"the highlight is not on Character Trainers row 0 alone (row 0 {c0}, row 1 {c1})")
    go_to_page(emu, "All Modes", "R")
    emu.advance(10)

    # Inputs that would act on the character select: none may reach it.
    for buttons in (["X"], ["Y"], ["Z"], ["START"]):
        if buttons == ["X"]:
            # X opens Recordings browser over the list; B goes back to it.
            n = count(emu, r"\] browser: (opened|closed)")
            emu.press(0, buttons, frames=4, after=6)
            emu.wait_for("the recordings browser", lambda: count(emu, r"\] browser: (opened|closed)") >= n + 1, 60,
                         every=2)
            emu.press(0, ["B"], frames=4, after=6)
            emu.wait_for("the drill list again", lambda: count(emu, r"\] browser: (opened|closed)") >= n + 2, 60,
                         every=2)
        elif buttons == ["Y"]:
            # Y opens Quick OSDs over the list; B returns to the boot menu.
            n = count(emu, r"\] osdquick: (opened|closed)")
            emu.press(0, buttons, frames=4, after=6)
            emu.wait_for("Quick OSDs screen", lambda: count(emu, r"\] osdquick: (opened|closed)") >= n + 1, 60,
                         every=2)
            emu.advance(8)
            shot(emu, taken, "drills-osd-quick")
            emu.press(0, ["B"], frames=4, after=6)
            emu.wait_for("the drill list after Quick OSDs", lambda: count(emu, r"\] osdquick: (opened|closed)") >= n + 2,
                         60, every=2)
        else:
            emu.press(0, buttons, frames=4, after=6)
    emu.hold(0, stick=(228, 128))
    emu.advance(30)
    emu.hold(0, stick=(28, 200))
    emu.advance(30)
    emu.neutral(0)
    emu.advance(20)
    go_to_row(emu, 0)
    after = css_snapshot(emu)
    if before != after:
        raise nav.NavError(f"the character select changed under the list:\n  {before}\n  {after}")
    if nav.drill_list_state(emu) != "open":
        raise nav.NavError("the list closed on input that should not close it")
    notes.append(f"character select unchanged under the list: {after}")


def check_quick_match(emu, cycle, taken, notes, stage="final_destination"):
    setup = nav.melee_setup(emu)
    kinds = [p["kind"] for p in setup["players"][:2]]
    states = [p["state"] for p in setup["players"][:2]]
    want_stage = nav.QUICK_STAGES[stage]
    if (setup["mode"] != nav.MODE_TRAINING or setup["stage"] != want_stage
            or kinds != QUICK_SETUP["kinds"] or states != QUICK_SETUP["states"]):
        raise nav.NavError(f"the quick match ({stage}) is {setup}")
    f0 = nav.game_frame(emu)
    emu.advance(10)
    if nav.game_frame(emu) <= f0:
        raise nav.NavError(f"the quick match ({stage}) did not advance past frame {f0}")
    if cycle == 0:
        emu.advance(120)
        shot(emu, taken, "drills-quick-match")
        # The lab menu works in the match.
        n = count(emu, r"\] lab: menu opened by port 1 \(Start\)")
        emu.press(0, ["START"])
        wait_count(emu, "the lab menu to open", r"\] lab: menu opened by port 1 \(Start\)", n + 1, 120)
        emu.advance(20)
        shot(emu, taken, "drills-quick-labmenu")
        n = count(emu, r"\] lab: menu closed")
        emu.press(0, ["START"])
        wait_count(emu, "the lab menu to close", r"\] lab: menu closed", n + 1, 120)
        if nav.training_paused(emu):
            raise nav.NavError("closing the lab menu with Start opened Training's pause")
        notes.append(f"quick match: {setup['players'][:2]}, stage {setup['stage']:#x}; lab menu opened")
    emu.advance(60)


def check_quick_stages(emu, taken, notes, state):
    """Start the test quick match on each supported stage and check its stage
    id and that the match runs."""
    for stage in ("final_destination", "battlefield", "green_hill_zone"):
        state["step"] = f"quick match stage: {stage}"
        emu.mark(state["step"])
        if nav.drill_list_state(emu) != "open":
            emu.wait_for("the list to open by itself", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
        emu.advance(20)
        nav.start_quick_match(emu, stage=stage)
        check_quick_match(emu, -1, taken, notes, stage=stage)
        if stage == "green_hill_zone":
            shot(emu, taken, "drills-stage-ghz-1")
            emu.advance(120)
            shot(emu, taken, "drills-stage-ghz-2")
        notes.append(f"quick match stage {stage} ({nav.QUICK_STAGES[stage]:#x}): ok")
        nav.finish_training(emu)
        state["matches"] += 1
        state["drills"] += 1


def css_task(emu):
    name, _, obj = nav.scene(emu)
    return emu.ptr(obj + nav.CSS_TASK) if name == "scSelctCharacter" else None


def at_select(emu):
    return css_task(emu) is not None and nav.css_area(emu, 0) is not None


def run(emu, opts, taken, notes, state):
    state["step"] = "boot into Training with the list open"
    nav.boot_to_training_css(emu, drill_list="keep")
    wait_count(emu, "the list to open by itself", r"\[{name}\] drills: list opened \(auto\) for port 1, "
               r"row 0 Training Lab", 1, 300)
    state["step"] = "the list over the character select"
    check_list_screens(emu, taken, notes)

    state["step"] = "Training Lab"
    go_to_row(emu, 0)
    emu.press(0, ["A"])
    wait_count(emu, "Training Lab to close the list", r"\] drills: list closed \(Training Lab\)", 1, 120)
    state["step"] = "a normal match from the character select"
    nav.start_training_match(emu, "mario", "fox")
    state["matches"] += 1
    emu.advance(60)
    nav.finish_training(emu)
    state["step"] = "back on the character select: the list reopens after a match"
    wait_count(emu, "the list to reopen after match",
               r"\] drills: list opened \(auto\) for port 1, row 0 Training Lab", 2, 300)
    emu.advance(15)
    # Enter Training Lab with A -> CSS, verify B recalls coin then steps back to
    # Boot Menu cleanly, and verify L+R still reopens without moving the CSS hand.
    emu.press(0, ["A"], frames=4, after=10)
    wait_count(emu, "A (Training Lab) to close the reopened list", r"\] drills: list closed \(Training Lab\)", 2, 120)
    emu.advance(20)
    before = css_snapshot(emu)
    state["step"] = "L+R reopens the list"
    emu.press(0, ["L", "R"], frames=4, after=10)
    wait_count(emu, "L+R to open the list", r"\] drills: list opened \(L\+R\) for port 1", 1, 120)
    emu.advance(10)
    if css_snapshot(emu) != before:
        raise nav.NavError(f"L+R moved the character select: {before} -> {css_snapshot(emu)}")
    # Test natural B step-back from CSS (after recalling placed coin): A -> CSS,
    # B recalls placed Mario token into hand, second B reopens Boot Menu.
    emu.press(0, ["A"], frames=4, after=10)
    wait_count(emu, "A (Training Lab) to close before CSS B check", r"\] drills: list closed \(Training Lab\)", 3, 120)
    emu.advance(15)
    if not nav.css_area(emu, 0)["held"]:
        emu.press(0, ["B"], frames=4, after=12)
        emu.wait_for("B to recall P1 token on CSS", lambda: nav.css_area(emu, 0)["held"], 60, every=2)
    emu.advance(10)
    state["step"] = "B on CSS (empty coin) reopens Boot Menu"
    emu.press(0, ["B"], frames=4, after=10)
    wait_count(emu, "B on CSS to reopen Boot Menu", r"\] drills: list opened \(B\) for port 1", 1, 120)
    emu.advance(10)

    # Arm a Scenario (Shield Pressure / OoS, preset 4) from Offense, verify backing
    # out of Stage Select with B keeps the list closed and preserves the armed
    # scenario, then start the match and confirm extra/scenario_presets.cpp
    # auto-activates it on match load.
    state["step"] = "Scenario auto-activation from boot menu (Shield Pressure / OoS)"
    go_to_page(emu, "Offense", "R")
    go_to_row(emu, 3)
    n_armed = count(emu, r"\] drills: scenario 4 \(Shield Pressure / OoS\) armed")
    emu.press(0, ["A"], frames=4, after=10)
    wait_count(emu, "Shield Pressure / OoS armed", r"\] drills: scenario 4 \(Shield Pressure / OoS\) armed", n_armed + 1, 120)
    # Stage Select -> B -> CSS must NOT reopen the boot list.
    state["step"] = "Stage Select back to CSS keeps boot list closed"
    nav.choose_on_css(emu, "mario", "fox")
    nav.press_until(emu, "the stage select", ["START"], lambda: nav.scene(emu)[0] == "scSelStage", 600)
    emu.wait_for("stage select ready", lambda: nav.sss(emu) is not None, 300, every=2)
    emu.advance(15)
    nav.press_until(emu, "return to CSS from stage select", ["B"],
                    lambda: nav.scene(emu)[0] == "scSelctCharacter", 300, every=20)
    state["visits"] += 1
    state["stage_bounces"] = state.get("stage_bounces", 0) + 1
    wait_count(emu, "the list to stay closed after backing out of stage select",
               r"\] drills: list stays closed", 1, 300)
    emu.advance(15)
    n_scen = count(emu, r"\] scenarios: armed preset 4 from menu session")
    nav.start_training_match(emu, "mario", "fox")
    state["matches"] += 1
    wait_count(emu, "scenario 4 armed on match attach", r"\] scenarios: armed preset 4 from menu session",
               n_scen + 1, 180)
    emu.advance(30)
    m_sb = re.findall(r"\] scenarios: debug block ([0-9a-f]{8})", emu.log_text())
    if not m_sb:
        raise nav.NavError("ScenarioDebugBlock log line missing after armed scenario")
    sb_addr = int(m_sb[-1], 16)
    active_preset = emu.u8(sb_addr + 0x3C)
    reset_count = emu.u32(sb_addr + 0x0C)
    if active_preset != 4 or reset_count < 1:
        raise nav.NavError(f"boot menu Scenario did not auto-activate (activePreset={active_preset}, resets={reset_count})")
    notes.append(f"Scenario handoff ok: Shield Pressure / OoS (preset={active_preset}, resets={reset_count})")
    nav.finish_training(emu)
    state["visits"] += 1
    wait_count(emu, "the list to reopen on remembered Shield Pressure / OoS row after scenario match",
               r"\] drills: list opened \(auto\) for port 1, row 3 Shield Pressure / OoS", 1, 300)
    emu.advance(10)
    n_all = count(emu, r"\] drills: page All Modes")
    emu.press(0, ["L"], frames=4, after=10)
    wait_count(emu, "L to return to All Modes page", r"\] drills: page All Modes", n_all + 1, 120)

    for cycle in range(opts.cycles):
        state["step"] = f"quick match, cycle {cycle + 1}"
        emu.mark(state["step"])
        if nav.drill_list_state(emu) != "open":
            emu.wait_for("the list to open by itself after the match", lambda: nav.drill_list_state(emu) == "open",
                         600, every=5)
        emu.advance(20)
        nav.start_quick_match(emu)
        check_quick_match(emu, cycle, taken, notes)
        state["step"] = f"quick match, cycle {cycle + 1}: Finish"
        nav.finish_training(emu)
        state["matches"] += 1
        state["drills"] += 1
    check_quick_stages(emu, taken, notes, state)
    emu.wait_for("the list to open by itself", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
    emu.advance(15)
    # Natural Nintendo B hierarchy: B on Boot Menu exits cleanly to Solo menu.
    state["step"] = "B on Boot Menu exits to Solo menu"
    emu.press(0, ["B"], frames=4, after=10)
    wait_count(emu, "B on Boot Menu to exit to Solo", r"\] drills: exiting to Solo \(B\)", 1, 120)
    emu.wait_for("Solo menu after Boot Menu B", lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
    emu.advance(20)
    state["left_css"] = True
    notes.append("natural B hierarchy: CSS B -> Boot Menu, Boot Menu B -> Solo menu (muMenuMain) ok")


def analyse(log, opts, state):
    """Every load and unload of both modules, from the log."""
    visits = 2 + state["drills"] + state["visits"]
    menu_loads = L.rx(MENU_LOADED).findall(log)
    menu_unloads = L.rx(MENU_UNLOADED).findall(log)
    match_loads = L.rx(L.LOADED).findall(log)
    match_unloads = L.rx(L.UNLOADED).findall(log)
    print(f"menu module: {len(menu_loads)} loads, {len(menu_unloads)} unloads; match module: "
          f"{len(match_loads)} loads, {len(match_unloads)} unloads")
    if menu_loads:
        print(f"menu module at {menu_loads[0][0]} in {menu_loads[0][1]}; free {menu_loads[0][2]} -> "
              f"{menu_loads[0][5]}")
    expected_unloads = visits if state.get("left_css") else visits - 1
    if len(menu_loads) != visits or len(menu_unloads) != expected_unloads:
        return f"expected {visits} menu module loads and {expected_unloads} unloads"
    if len(match_loads) != state["matches"] or len(match_unloads) != state["matches"]:
        return f"expected {state['matches']} match module loads and unloads"
    if any(u[3] != "yes" for u in menu_unloads + match_unloads):
        return "an unload was not back to baseline"
    if len({(m[0], m[1], m[2]) for m in menu_loads}) != 1:
        return f"menu module loads differ: {sorted({(m[0], m[1], m[2]) for m in menu_loads})}"
    if len({(m[0], m[1], m[2]) for m in match_loads}) != 1:
        return f"match module loads differ: {sorted({(m[0], m[1], m[2]) for m in match_loads})}"
    if menu_loads[0][1] != match_loads[0][1] or menu_loads[0][2] != match_loads[0][2]:
        return "the two modules did not start from the same heap state"
    if L.rx(r"\[{name}\] drills: test start refused").search(log):
        return "the direct start was refused"
    exits = L.rx(r"\[{name}\] drills: detached, exit code (\d+)").findall(log)
    expected_exits = state["matches"] + state.get("stage_bounces", 0)
    if exits.count("1") != expected_exits:
        return f"expected {expected_exits} clean detaches into matches/stage-select, saw {exits}"
    return ""


# ---- Wii Remote, Nunchuk and Classic Controller -------------------------------

# g_gfPadSystem (0x805A0040) -> +0x244 m_debugGcnPads[4], +0x344 m_debugWiiPads[4]
# (0x40 bytes each; +0x38 s8 m_error, +0x3C u32 m_controllerType).
GF_PAD_SYSTEM_PTR = 0x805A0040
DEBUG_PADS_OFFSET = 0x244
PAD_STATUS_SIZE = 0x40
PAD_TYPE_CLASSIC = 1
PAD_TYPE_WIIMOTE = 2
PAD_TYPE_NUNCHUK = 3


def pad_status_info(emu, port):
    """Read (buttons, m_error, m_controllerType) for `port` (0-3 GC, 4-7 Wii)
    from gfPadSystem's debug pad table."""
    base = emu.ptr(GF_PAD_SYSTEM_PTR)
    if not base:
        return None
    raw = emu.read(base + DEBUG_PADS_OFFSET + port * PAD_STATUS_SIZE, PAD_STATUS_SIZE)
    buttons = struct.unpack(">I", raw[0:4])[0]
    err = struct.unpack(">b", raw[0x38:0x39])[0]
    ctype = struct.unpack(">I", raw[0x3C:0x40])[0]
    return {"buttons": buttons, "err": err, "type": ctype}


def wii_set_extension(emu, ext, want_type, port=4, timeout_frames=240):
    """Hot-plug `ext` ('none', 'nunchuk', 'classic') on emulated Wiimote 1 and
    wait until Brawl's gfPadSystem reports `want_type` with no error."""
    emu.h.call("wiimote_input", index=port - 4, extension=ext)
    emu.wait_for(
        f"Wiimote port {port + 1} to report type {want_type} ({ext})",
        lambda: (s := pad_status_info(emu, port)) is not None and s["err"] == 0 and s["type"] == want_type,
        timeout_frames, every=4,
    )
    # Unplugging an extension ('none') updates m_controllerType to 2 on frame 1
    # while WPAD still spends ~30 frames renegotiating core-only report mode.
    emu.advance(45 if ext == "none" else 16)


def wii_press(emu, payload, frames=10, after=12, port=4):
    """Hold `payload` on emulated Wiimote `port - 4` for `frames` (enough for
    Bluetooth/WPAD report latency, well below the 15-frame key-repeat delay),
    then return to neutral for `after` frames."""
    idx = port - 4
    emu.h.call("wiimote_input", index=idx, **payload)
    emu.advance(frames)
    emu.h.call("wiimote_input", index=idx)
    emu.advance(after)


def check_sideways_reopen(emu, before, tag):
    """With the list closed on a sideways Wii Remote: '-' alone neither opens
    the list nor is hidden from the game (P+ reads '-' for special fighters at
    match start); holding '-' then pressing B opens the list without stepping
    the costume."""
    reopen_rx = r"\] drills: list opened \(- \+ B\) for port 5"
    n_reopen = count(emu, reopen_rx)
    # Hold '-' alone: must stay closed and remain visible in gfPadSystem.
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1})
    emu.advance(10)
    minus_bits = (pad_status_info(emu, 4) or {}).get("buttons", 0)
    if nav.drill_list_state(emu) != "closed":
        emu.h.call("wiimote_input", index=0)
        raise nav.NavError(f"{tag}: sideways '-' alone opened the drill list")
    if (minus_bits & 0x80000) == 0:
        emu.h.call("wiimote_input", index=0)
        raise nav.NavError(f"{tag}: sideways '-' alone was hidden from the game (buttons={minus_bits:#x})")
    # While still holding '-', press B: reopens without stepping the costume.
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1, "Buttons/B": 1})
    emu.advance(10)
    emu.h.call("wiimote_input", index=0)
    emu.advance(12)
    wait_count(emu, f"{tag}: sideways - + B to reopen list", reopen_rx, n_reopen + 1, 120)
    emu.advance(10)
    if nav.drill_list_state(emu) != "open":
        raise nav.NavError(f"{tag}: list did not reopen on - + B")
    if css_snapshot(emu) != before:
        raise nav.NavError(f"{tag}: - + B moved CSS (costume stepped?):\n  {before}\n  {css_snapshot(emu)}")


def check_wii_controller(emu, state, notes, label, ext, pad_type, controls, expect_takeover):
    """Exercise one emulated Wii controller type on the drill list: optional
    takeover from idle owner, page change, browser open/leave, select & close,
    and reopen with CSS unchanged under every step."""
    state["step"] = f"wii ({label}): attach {ext}"
    emu.mark(state["step"])
    wii_set_extension(emu, ext, pad_type)

    if expect_takeover:
        state["step"] = f"wii ({label}): takeover from idle GC port 1"
        n_hand = count(emu, r"\] drills: list handed to port 5")
        n_close = count(emu, r"\] drills: list closed")
        # First press claims ownership only (release-before-acting guard).
        wii_press(emu, controls["back"])
        wait_count(emu, f"{label} to claim list ownership", r"\] drills: list handed to port 5", n_hand + 1, 90)
        if count(emu, r"\] drills: list closed") != n_close:
            raise nav.NavError(f"{label}: takeover press also closed the list")
        if nav.drill_list_state(emu) != "open":
            raise nav.NavError(f"{label}: list did not stay open on takeover press")

    before = css_snapshot(emu)

    # Switch to Offense, move down a row and back up, then return to All Modes.
    state["step"] = f"wii ({label}): switch page and move rows"
    n_page = count(emu, r"\] drills: page Offense")
    wii_press(emu, controls["page_next"])
    wait_count(emu, f"{label} to open Offense page", r"\] drills: page Offense", n_page + 1, 90)
    n_row = count(emu, r"\] drills: row ")
    wii_press(emu, controls["down"])
    wait_count(emu, f"{label} to move down a row", r"\] drills: row ", n_row + 1, 90)
    n_row = count(emu, r"\] drills: row ")
    wii_press(emu, controls["up"])
    wait_count(emu, f"{label} to move back to row 0", r"\] drills: row ", n_row + 1, 90)
    if last_row(emu) != 0:
        raise nav.NavError(f"{label}: expected row 0, at {last_row(emu)}")
    n_page = count(emu, r"\] drills: page All Modes")
    wii_press(emu, controls["page_prev"])
    wait_count(emu, f"{label} to return to All Modes page", r"\] drills: page All Modes", n_page + 1, 90)

    # Open and leave the recordings browser.
    state["step"] = f"wii ({label}): open and leave recordings browser"
    n_open = count(emu, r"\] browser: opened for port 5")
    wii_press(emu, controls["browser"])
    wait_count(emu, f"{label} to open recordings browser", r"\] browser: opened for port 5", n_open + 1, 120)
    emu.advance(10)
    n_bclose = count(emu, r"\] browser: closed")
    wii_press(emu, controls["back"])
    wait_count(emu, f"{label} to leave recordings browser", r"\] browser: closed", n_bclose + 1, 120)

    if css_snapshot(emu) != before:
        raise nav.NavError(f"{label}: character select changed while list/browser was open:\n"
                           f"  {before}\n  {css_snapshot(emu)}")

    # Select Training Lab to close the list.
    state["step"] = f"wii ({label}): select Training Lab and close list"
    n_close = count(emu, r"\] drills: list closed \(Training Lab\)")
    wii_press(emu, controls["confirm"])
    wait_count(emu, f"{label} to select Training Lab", r"\] drills: list closed \(Training Lab\)", n_close + 1, 90)
    emu.advance(10)
    if nav.drill_list_state(emu) != "closed":
        raise nav.NavError(f"{label}: list did not close on confirm")
    if css_snapshot(emu) != before:
        raise nav.NavError(f"{label}: closing the list leaked into CSS:\n  {before}\n  {css_snapshot(emu)}")

    # Reopen the list with this controller type's CSS-free reopen combo.
    state["step"] = f"wii ({label}): reopen list ({controls['reopen_log']})"
    if label == "sideways":
        check_sideways_reopen(emu, before, label)
    else:
        reopen_rx = r"\] drills: list opened \(" + re.escape(controls["reopen_log"]) + r"\) for port 5"
        n_reopen = count(emu, reopen_rx)
        wii_press(emu, controls["reopen"])
        wait_count(emu, f"{label} to reopen list", reopen_rx, n_reopen + 1, 120)
        emu.advance(10)
        if nav.drill_list_state(emu) != "open":
            raise nav.NavError(f"{label}: list did not reopen")
        if css_snapshot(emu) != before:
            raise nav.NavError(f"{label}: reopen ({controls['reopen_log']}) moved CSS:\n"
                               f"  {before}\n  {css_snapshot(emu)}")

    notes.append(f"wii ({label}): row, page, browser, select/close, reopen ({controls['reopen_log']}) ok")


WII_CONTROLS = [
    ("classic", "classic", PAD_TYPE_CLASSIC, {
        "up": {"classic": {"D-Pad/Up": 1}},
        "down": {"classic": {"D-Pad/Down": 1}},
        "left": {"classic": {"D-Pad/Left": 1}},
        "right": {"classic": {"D-Pad/Right": 1}},
        "page_prev": {"classic": {"Triggers/L": 1}},
        "page_next": {"classic": {"Triggers/R": 1}},
        "l": {"classic": {"Triggers/L": 1}},
        "r": {"classic": {"Triggers/R": 1}},
        "zl": {"classic": {"Buttons/ZL": 1}},
        "y": {"classic": {"Buttons/Y": 1}},
        "confirm": {"classic": {"Buttons/A": 1}},
        "back": {"classic": {"Buttons/B": 1}},
        "browser": {"classic": {"Buttons/X": 1}},
        "start": {"classic": {"Buttons/+": 1}},
        "reopen": {"classic": {"Triggers/L": 1, "Triggers/R": 1}},
        "reopen_log": "L+R",
    }),
    # Upright Dolphin Wiimote buttons: Brawl rotates D-pad 90 deg CCW for
    # sideways Wii Remote (Right=Up, Left=Down, Up=Left, Down=Right).
    ("sideways", "none", PAD_TYPE_WIIMOTE, {
        "up": {"wiimote": {"D-Pad/Right": 1}},
        "down": {"wiimote": {"D-Pad/Left": 1}},
        "left": {"wiimote": {"D-Pad/Up": 1}},
        "right": {"wiimote": {"D-Pad/Down": 1}},
        "page_prev": {"wiimote": {"D-Pad/Up": 1}},
        "page_next": {"wiimote": {"D-Pad/Down": 1}},
        "confirm": {"wiimote": {"Buttons/2": 1}},
        "back": {"wiimote": {"Buttons/1": 1}},
        "browser": {"wiimote": {"Buttons/A": 1}},
        "a": {"wiimote": {"Buttons/A": 1}},
        "b": {"wiimote": {"Buttons/B": 1}},
        "minus": {"wiimote": {"Buttons/-": 1}},
        "start": {"wiimote": {"Buttons/+": 1}},
        "reopen": {"wiimote": {"Buttons/-": 1, "Buttons/B": 1}},
        "reopen_log": "- + B",
    }),
    ("nunchuk", "nunchuk", PAD_TYPE_NUNCHUK, {
        "up": {"nunchuk": {"Stick/Y": 1.0}},
        "down": {"nunchuk": {"Stick/Y": -1.0}},
        "d_up": {"wiimote": {"D-Pad/Up": 1}},
        "d_down": {"wiimote": {"D-Pad/Down": 1}},
        "d_left": {"wiimote": {"D-Pad/Left": 1}},
        "d_right": {"wiimote": {"D-Pad/Right": 1}},
        "page_prev": {"wiimote": {"D-Pad/Left": 1}},
        "page_next": {"wiimote": {"D-Pad/Right": 1}},
        "confirm": {"wiimote": {"Buttons/A": 1}},
        "back": {"wiimote": {"Buttons/B": 1}},
        "browser": {"nunchuk": {"Buttons/C": 1}},
        "z": {"nunchuk": {"Buttons/Z": 1}},
        "minus": {"wiimote": {"Buttons/-": 1}},
        "one": {"wiimote": {"Buttons/1": 1}},
        "start": {"wiimote": {"Buttons/+": 1}},
        "reopen": {"nunchuk": {"Buttons/C": 1, "Buttons/Z": 1}},
        "reopen_log": "C+Z",
    }),
]
WII_BY_NAME = {label: ctrl for label, _, _, ctrl in WII_CONTROLS}


def wii_go_to_page(emu, title, controls):
    for _ in range(7):
        if last_page(emu) == title:
            return
        n = count(emu, r"\] drills: page ")
        wii_press(emu, controls["page_next"])
        wait_count(emu, f"list page toward {title}", r"\] drills: page ", n + 1, 120)
    raise nav.NavError(f"list did not reach page {title} (on {last_page(emu)})")


def wii_go_to_row(emu, row, controls):
    for _ in range(30):
        now = last_row(emu)
        if now == row:
            return
        n = count(emu, r"\] drills: row ")
        wii_press(emu, controls["down"] if row > now else controls["up"])
        wait_count(emu, f"list row toward {row}", r"\] drills: row ", n + 1, 120)
    raise nav.NavError(f"list did not reach row {row} (at {last_row(emu)})")


def wii_classic_steer(emu, what, read_xy, tx, ty, done, timeout_frames=600):
    """Steer a CSS/SSS cursor with Classic Controller Left Stick until `done()`
    holds at rest."""
    start = emu.polls()
    while emu.polls() - start < timeout_frames:
        if done():
            emu.h.call("wiimote_input", index=0)
            emu.advance(12)
            if done():
                return
        x, y = read_xy()
        dx, dy = tx - x, ty - y
        dist = math.hypot(dx, dy)
        if dist < 0.35:
            emu.h.call("wiimote_input", index=0)
            emu.advance(8)
            if done():
                return
            continue
        mag = 1.0 if dist >= 4.0 else (0.55 if dist >= 1.5 else 0.35)
        emu.h.call("wiimote_input", index=0, classic={"Left Stick/X": dx / dist * mag, "Left Stick/Y": dy / dist * mag})
        emu.advance(2)
    emu.h.call("wiimote_input", index=0)
    raise Timeout(f"{what} (cursor at {read_xy()}, aimed at ({tx:.1f}, {ty:.1f}))")


def wii_classic_drop_on(emu, character):
    """If P1's token is held on CSS, steer it onto `character` with Classic
    Controller and drop it with A."""
    area = nav.css_area(emu, 0)
    if area is None or not area["held"]:
        return
    tx, ty = nav.icon_pos(emu, character)
    wii_classic_steer(emu, f"Classic P1 token onto {character}", lambda: nav._hand_xy(emu), tx, ty,
                      lambda: nav._hand_over(emu, character, "p1"))
    wii_press(emu, WII_BY_NAME["classic"]["confirm"])
    emu.wait_for(f"Classic P1 token to drop on {character}", lambda: not nav.css_area(emu, 0)["held"], 60, every=2)


def wii_classic_pick_stage(emu, stage="final_destination"):
    """Pick `stage` on Training's stage select using Classic Controller only."""
    kind, position = nav.STAGES[stage]
    emu.wait_for("stage select cursor (Wii)", lambda: nav.sss(emu), 600, every=2)
    emu.advance(20)
    row, col = divmod(position, 7)
    tx, ty = nav.SSS_COL_X0 + nav.SSS_COL_STEP * col, nav.SSS_ROW_Y[row]

    def read_xy():
        s = nav.sss(emu)
        if s is None:
            raise nav.NavError(f"left stage select ({nav.scene(emu)[:2]})")
        return s["x"], s["y"]

    wii_classic_steer(emu, f"Classic stage cursor onto {stage}", read_xy, tx, ty,
                      lambda: (nav.sss(emu) or {}).get("hovered") == position + nav.SSS_HOVER_BASE)
    wii_press(emu, WII_BY_NAME["classic"]["confirm"])
    emu.wait_for(f"Classic stage select to take {stage}",
                 lambda: (nav.sss(emu) or {}).get("taken") or nav.scene(emu)[0] != "scSelStage", 120, every=2)


def wii_pause_toggle(emu, controls, want_paused, what, timeout_frames=240):
    """Press '+' on the emulated Wii controller until Training's pause state
    reaches `want_paused`."""
    start = emu.polls()
    while emu.polls() - start < timeout_frames:
        if nav.training_paused(emu) == want_paused:
            emu.advance(8)
            return
        wii_press(emu, controls["start"], frames=10, after=16)
    raise Timeout(f"{what} (training_paused={nav.training_paused(emu)})")


def wii_finish_from_pause(emu, controls, tag):
    """With Brawl's Training pause menu already open, pick Finish using only
    the emulated Wii controller."""
    for _ in range(nav.MENU_ITEMS + 2):
        cur = nav.menu_cursor(emu)
        if cur == nav.MENU_FINISH:
            break
        if cur is None:
            raise nav.NavError(f"{tag}: Brawl pause menu cursor unreadable")
        up = (cur - nav.MENU_FINISH) % nav.MENU_ITEMS <= nav.MENU_ITEMS // 2
        wii_press(emu, controls["up"] if up else controls["down"], frames=8, after=10)
    if nav.menu_cursor(emu) != nav.MENU_FINISH:
        raise nav.NavError(f"{tag}: pause cursor did not reach Finish (at {nav.menu_cursor(emu)})")
    wii_press(emu, controls["confirm"], frames=10, after=12)
    emu.wait_for(f"{tag}: return to CSS after Brawl pause Finish",
                 lambda: nav.scene(emu)[0] == "scSelctCharacter", 1800, every=10)
    emu.advance(30)


def wii_finish_training(emu, controls, tag):
    """Leave a Training Lab match through Brawl's own pause menu -> Finish
    using only the emulated Wii controller (handles Start-opened Lab Menu by
    jumping to Training's pause via pause_jump)."""
    wii_pause_toggle(emu, controls, True, f"{tag}: open Brawl pause for Finish")
    wii_finish_from_pause(emu, controls, tag)


def wii_menu_open(emu, controls, what):
    """Open the Lab/Drill Menu with '+' (Start mode) on port 5."""
    rx = r"\] lab: menu opened by port 5 \(Start\)"
    n = count(emu, rx)
    wii_press(emu, controls["start"], frames=10, after=12)
    wait_count(emu, what, rx, n + 1, 180)


def wii_menu_close(emu, controls, what):
    """Close a Start-opened Lab/Drill Menu with '+' on port 5."""
    rx = r"\] lab: menu closed with Start"
    n = count(emu, rx)
    wii_press(emu, controls["start"], frames=10, after=12)
    wait_count(emu, what, rx, n + 1, 180)


def wii_drill_exit(emu, controls, tag):
    """Leave a drill through its Drill Menu -> Main Menu -> Exit (last row of
    Main Menu) using only the emulated Wii controller."""
    wii_menu_open(emu, controls, f"{tag}: open Drill Menu for Exit")
    # A drill's Start menu opens on its drill page (depth 1): back drops to
    # Main Menu (depth 0, row 0). menuStep clamps at the bottom row (Exit).
    wii_press(emu, controls["back"], frames=9, after=10)
    for _ in range(12):
        wii_press(emu, controls["down"], frames=8, after=8)
    wii_press(emu, controls["confirm"], frames=10, after=12)
    emu.wait_for(f"{tag}: return to CSS after Drill Menu Exit",
                 lambda: nav.scene(emu)[0] == "scSelctCharacter", 1800, every=10)
    emu.advance(30)


def build_start_paused_tmr(name="260102-030405", frames=30):
    """Build a valid Start Paused = On, Auto Restore = Playback Ends recording
    (.tmr) for Mario vs Fox CPU on Final Destination (CPU Slot 1)."""
    def fnv1a(data):
        h = 0x811C9DC5
        for b in data:
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
        return h

    # RecordingSettings (src/main/settings.h): 27 used bytes (offsetof reserved).
    rec_settings = bytearray(27)
    rec_settings[3] = 1   # hmnSlot = Slot 1
    rec_settings[4] = 3   # cpuMode = Playback
    rec_settings[5] = 1   # cpuSlot = Slot 1
    rec_settings[9] = 1   # autoRestore = Playback Ends (restores after 30f -> holds!)
    rec_settings[10] = 1  # startPaused = On
    rec_settings[11] = 2  # takeover = None
    rec_body = bytes(rec_settings) + b"\x00"  # padded to 4-byte multiple (28 bytes)

    info = bytearray(64)
    nb = name.encode("ascii")[:31]
    info[0:len(nb)] = nb
    info[32] = 0   # hmnKind Mario
    info[35] = 0   # hmnIcon Mario
    info[36] = 7   # cpuKind Fox
    info[39] = 7   # cpuIcon Fox
    struct.pack_into(">H", info, 40, 2)       # stage Final Destination
    struct.pack_into(">H", info, 44, frames)  # frames
    info[47] = 1   # cpuSlots bit 0 = Slot 1
    struct.pack_into(">H", info, 48, 2026)
    info[50:55] = bytes([1, 2, 3, 4, 5])

    area_offset = 32 + 64 + 8 + 8  # FileHeader + RecordingInfo + RecordingHead + 1 AreaEntry = 112
    placements_offset = area_offset + len(rec_body)  # 140
    rec_head = struct.pack(">HHI", 1, 1, placements_offset)
    area_entry = struct.pack(">BBHI", 8, 1, len(rec_settings), area_offset)

    # Two Placement structs (32 bytes each): HMN at (-30, 0), CPU at (+30, 0).
    p_hmn = struct.pack(">ffffffB7x", -30.0, 0.0, 0.0, 1.0, 0.0, 60.0, 1)
    p_cpu = struct.pack(">ffffffB7x", 30.0, 0.0, 0.0, -1.0, 0.0, 60.0, 1)

    # One SlotHead (20 bytes): CPU (side 1) Slot 1 (index 0), frames 0..30.
    slot_head = struct.pack(">BBHH4HHI", 1, 0, 0, frames, 1, 1, 1, 1, 1, 0)
    slot_data = struct.pack(">4f2f", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) + bytes(8 * frames)

    payload = bytes(info) + rec_head + area_entry + rec_body + p_hmn + p_cpu + slot_head + slot_data
    size = 32 + len(payload)
    tail = struct.pack(">I12x", 0) + payload
    header = struct.pack(">IHHII", 0x544D5246, 1, 32, size, fnv1a(tail))
    raw = header + tail
    padded_size = (len(raw) + 31) & ~31
    return raw + bytes(padded_size - len(raw))


def run_wii_with_gc(emu, notes, state):
    """GC port 1 neutral + emulated Wiimote 1: GC works with a Wii pad
    connected, and Classic, sideways Wii Remote and Remote + Nunchuk each
    operate, close and reopen the list."""
    state["step"] = "wii+gc: boot into Training with both pads"
    nav.boot_to_training_css(emu, drill_list="keep")
    wait_count(emu, "the list to open by itself", r"\[{name}\] drills: list opened \(auto\) for port 1, "
               r"row 0 Training Lab", 1, 300)
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)

    # GameCube port 1 still works while a Wii pad is connected.
    state["step"] = "wii+gc: GC port 1 works with Wii pad connected"
    n_page = count(emu, r"\] drills: page Offense")
    emu.press(0, ["R"], frames=3, after=6)
    wait_count(emu, "GC port 1 to switch to Offense with Wii pad connected", r"\] drills: page Offense",
               n_page + 1, 90)
    n_row = count(emu, r"\] drills: row ")
    emu.press(0, ["DDOWN"], frames=3, after=6)
    wait_count(emu, "GC port 1 to move down with Wii pad connected", r"\] drills: row ", n_row + 1, 90)
    n_row = count(emu, r"\] drills: row ")
    emu.press(0, ["DUP"], frames=3, after=6)
    wait_count(emu, "GC port 1 to move back to row 0", r"\] drills: row ", n_row + 1, 90)
    n_page = count(emu, r"\] drills: page All Modes")
    emu.press(0, ["L"], frames=3, after=6)
    wait_count(emu, "GC port 1 to return to All Modes", r"\] drills: page All Modes", n_page + 1, 90)
    notes.append("wii+gc: GC port 1 navigated list while Classic Controller was connected")

    # Exercise Classic (takes over from GC), sideways Wii Remote, and Nunchuk.
    for idx, (label, ext, pad_type, controls) in enumerate(WII_CONTROLS):
        check_wii_controller(emu, state, notes, label, ext, pad_type, controls, expect_takeover=(idx == 0))

    # Leave Wii pad neutral; GC port 1 takes back ownership and enters Training Lab.
    state["step"] = "wii+gc: GC port 1 takes back list from idle Wii pad"
    n_hand = count(emu, r"\] drills: list handed to port 1")
    emu.press(0, ["B"], frames=3, after=6)
    wait_count(emu, "GC port 1 to take back list ownership", r"\] drills: list handed to port 1", n_hand + 1, 90)
    n_close = count(emu, r"\] drills: list closed \(Training Lab\)")
    emu.press(0, ["A"], frames=3, after=6)
    wait_count(emu, "GC port 1 to enter Training Lab with A", r"\] drills: list closed \(Training Lab\)", n_close + 1, 90)
    emu.h.call("wiimote_input", index=0, clear=True)
    notes.append("wii+gc: GC port 1 took back ownership and entered Training Lab with Wii pad connected")


def run_wii_only(emu, notes, state):
    """GC port 1 unplugged (SIDevice0=0) + emulated Wiimote 1 only: auto-open
    for port 5, sideways 2 enters Training Lab, '-' alone unhidden, '- + B'
    reopens, and sideways 1 on Boot Menu exits cleanly to Solo."""
    state["step"] = "wii-only: boot into Training with no GC pads"
    emu.wait_for("Training CSS with no GC pads", lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
    name, seq, _ = nav.scene(emu)
    if seq != "sqTraining":
        raise nav.NavError(f"wii-only: booted to {name} / {seq}, not Training")
    wait_count(emu, "the list to auto-open for Wii port 5",
               r"\[{name}\] drills: list opened \(auto\) for port 5, row 0 Training Lab", 1, 300)
    # Wait for CSS P1/CPU panels to finish init under the list.
    emu.wait_for("wii-only P1 and CPU panels",
                 lambda: (p1 := nav.css_area(emu, 0)) is not None and (cpu := nav.css_area(emu, 1)) is not None
                 and p1["kind"] == 1 and cpu["kind"] == 2, 600, every=2)
    emu.advance(20)
    # Switch to sideways Wii Remote, select Training Lab with 2, verify '-' alone
    # reaches the game, reopen with '- + B' without stepping costume, then exit to Solo with 1.
    wii_set_extension(emu, "none", PAD_TYPE_WIIMOTE)
    before = css_snapshot(emu)
    state["step"] = "wii-only: select Training Lab with sideways 2"
    n_close = count(emu, r"\] drills: list closed \(Training Lab\)")
    wii_press(emu, {"wiimote": {"Buttons/2": 1}})
    wait_count(emu, "sideways 2 to enter Training Lab", r"\] drills: list closed \(Training Lab\)", n_close + 1, 90)
    emu.advance(10)
    if nav.drill_list_state(emu) != "closed":
        raise nav.NavError("wii-only: list stayed open after sideways 2")
    if css_snapshot(emu) != before:
        raise nav.NavError(f"wii-only: sideways 2 leaked into CSS: {before} -> {css_snapshot(emu)}")

    state["step"] = "wii-only: sideways - alone unhidden, - + B reopens"
    check_sideways_reopen(emu, before, "wii-only")
    state["step"] = "wii-only: sideways 1 on Boot Menu exits to Solo"
    wii_press(emu, {"wiimote": {"Buttons/1": 1}})
    wait_count(emu, "sideways 1 to exit to Solo", r"\] drills: exiting to Solo \(1\)", 1, 120)
    emu.wait_for("wii-only: Solo menu after sideways 1", lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
    notes.append("wii-only (no GC): list auto-opened for port 5, sideways 2 entered Lab, '-' unhidden, '- + B' reopened, 1 exited to Solo")
    emu.h.call("wiimote_input", index=0, clear=True)


def run_wii_start_paused(emu, notes, state):
    """GC port 1 unplugged (SIDevice0=0) + emulated Wiimote 1 on a card with a
    Start Paused recording: import from browser on sideways Wii Remote, verify
    Start Paused holds until input, release with sideways movement, and leave
    Training Lab through Brawl's own pause -> Finish."""
    sideways = WII_BY_NAME["sideways"]
    state["step"] = "wii-record: boot into Training with no GC pads"
    emu.wait_for("Training CSS (wii-record)", lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
    wait_count(emu, "drill list auto-open (wii-record)",
               r"\[{name}\] drills: list opened \(auto\) for port 5, row 0 Training Lab", 1, 300)
    emu.advance(20)
    wii_set_extension(emu, "none", PAD_TYPE_WIIMOTE)

    # Open recordings browser with sideways A, load the Start Paused recording with sideways 2.
    state["step"] = "wii-record: import Start Paused recording via browser"
    n_open = count(emu, r"\] browser: opened for port 5")
    wii_press(emu, sideways["browser"])
    wait_count(emu, "browser opened (wii-record)", r"\] browser: opened for port 5", n_open + 1, 120)
    emu.advance(90)  # let async header read finish
    n_start = count(emu, r"\] browser: starting 260102-030405\.tmr")
    wii_press(emu, sideways["confirm"])
    wait_count(emu, "browser starting 260102-030405.tmr", r"\] browser: starting 260102-030405\.tmr", n_start + 1, 240)
    nav.wait_match_start(emu)

    # Wait for import + auto-restore to hold frame 0 under Start Paused.
    state["step"] = "wii-record: Start Paused holds then releases on sideways Wii Remote input"
    wait_count(emu, "imported positions placed", r"\] record: positions imported ", 1, 600)
    wait_count(emu, "Start Paused held until input", r"\] record: restored .*, held until input", 1, 300)
    m_tb = re.findall(r"\] record: test block at ([0-9a-f]{8})", emu.log_text())
    if not m_tb:
        raise nav.NavError("wii-record: no record test block line in log")
    rec_tb = int(m_tb[-1], 16)
    # TestBlock (src/main/record/record.cpp): +0x18 t[1], +0x1C flags (0x2 holding), +0x6C held passes.
    held0 = emu.u32(rec_tb + 0x6C)
    emu.advance(25)
    t_held = emu.u32(rec_tb + 0x18)
    flags_held = emu.u32(rec_tb + 0x1C)
    held1 = emu.u32(rec_tb + 0x6C)
    if t_held != 0 or (flags_held & 0x2) == 0 or held1 <= held0:
        raise nav.NavError(f"wii-record: Start Paused not holding (t={t_held}, flags={flags_held:#x}, held={held0}->{held1})")
    if count(emu, r"\] record: Start Paused released by input") != 0:
        raise nav.NavError("wii-record: Start Paused released before any input")

    # Sideways movement (physical Right = upright D-Pad/Down) must release Start Paused!
    wii_press(emu, sideways["right"])
    wait_count(emu, "Start Paused released by sideways Wii Remote input",
               r"\] record: Start Paused released by input", 1, 120)
    emu.advance(20)
    t_after = emu.u32(rec_tb + 0x18)
    flags_after = emu.u32(rec_tb + 0x1C)
    if (flags_after & 0x2) != 0 or t_after == 0:
        raise nav.NavError(f"wii-record: match did not advance after Start Paused release (t={t_after}, flags={flags_after:#x})")

    # Leave Training Lab via Lab Menu ('+' opens Lab Menu, '-' jumps to Brawl's
    # Training pause, then Finish).
    state["step"] = "wii-record: leave Training Lab via Lab Menu -> Brawl pause Finish"
    wii_menu_open(emu, sideways, "wii-record: Sideways '+' opens Lab Menu")
    wii_press(emu, sideways["minus"], frames=10, after=16)
    emu.wait_for("wii-record: Sideways '-' jumps to Brawl pause", lambda: nav.training_paused(emu), 240, every=4)
    wii_finish_from_pause(emu, sideways, "wii-record")
    emu.h.call("wiimote_input", index=0, clear=True)
    notes.append("wii-record (no GC): Start Paused imported via browser, held, released on sideways input, "
                 "left lab via Lab Menu -> Brawl pause Finish ok")


def wii_enter_match(emu, page, row, fixed_fd=False, rebuilds_css=False):
    """From CSS with drill list open (or closed after Training Lab), pick
    (page, row) on Classic Controller, drop Mario, start match, and pick Final
    Destination if not fixed."""
    classic = WII_BY_NAME["classic"]
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)
    if nav.drill_list_state(emu) != "open":
        n_open = count(emu, r"\] drills: list opened ")
        wii_press(emu, classic["reopen"])
        wait_count(emu, "wii-inmatch: reopen drill list (L+R)", r"\] drills: list opened ", n_open + 1, 180)
        emu.wait_for("wii-inmatch: drill list open", lambda: nav.drill_list_state(emu) == "open", 120, every=5)
    emu.advance(16)
    if page is not None:
        wii_go_to_page(emu, page, classic)
    wii_go_to_row(emu, row, classic)
    if rebuilds_css:
        n_built = count(emu, r"\] select: built for drill")
        wii_press(emu, classic["confirm"])
        wait_count(emu, f"wii-inmatch: select built ({page}:{row})", r"\] select: built for drill", n_built + 1, 900)
        emu.wait_for("wii-inmatch: rebuilt CSS", lambda: at_select(emu), 900, every=5)
        emu.advance(40)
    else:
        n_close = count(emu, r"\] drills: list closed \(")
        wii_press(emu, classic["confirm"])
        wait_count(emu, f"wii-inmatch: list closed ({page}:{row})", r"\] drills: list closed \(", n_close + 1, 120)
        emu.advance(20)
    wii_classic_drop_on(emu, "mario")
    want_scene = (lambda: nav.scene(emu)[0] != "scSelctCharacter") if fixed_fd else (
        lambda: nav.scene(emu)[0] == "scSelStage")
    start_p = emu.polls()
    while emu.polls() - start_p < 600 and not want_scene():
        wii_press(emu, classic["start"], frames=10, after=30)
    if not want_scene():
        raise Timeout(f"wii-inmatch: Classic '+' did not leave CSS (scene={nav.scene(emu)[:2]})")
    if not fixed_fd:
        wii_classic_pick_stage(emu, "final_destination")
    nav.wait_match_start(emu)


def run_wii_inmatch(emu, notes, state, taken):
    """End-to-end Part 2 Wii in-match check (no GC pads, emulated Wiimote 1):
    per-controller CSS/browser screenshots, Lab Menu (Start & Z-in-Pause modes,
    pause hints, jump to Training pause, Return to CSS rail utility), Frame
    Advance (sideways B never hijacks L, '-' advances; Nunchuk 1, Classic L),
    and savestates (Classic/Nunchuk bare D-pad vs sideways '-' + D-pad chord)."""
    import lab_tests as LT

    classic = WII_BY_NAME["classic"]
    nunchuk = WII_BY_NAME["nunchuk"]
    sideways = WII_BY_NAME["sideways"]

    state["step"] = "wii-inmatch: boot to Training CSS (no GC)"
    emu.wait_for("Training CSS (wii-inmatch)", lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
    wait_count(emu, "drill list auto-open (wii-inmatch)",
               r"\[{name}\] drills: list opened \(auto\) for port 5, row 0 Training Lab", 1, 300)
    emu.wait_for("wii-inmatch CSS panels",
                 lambda: (p1 := nav.css_area(emu, 0)) is not None and (cpu := nav.css_area(emu, 1)) is not None
                 and p1["kind"] == 1 and cpu["kind"] == 2, 600, every=2)
    emu.advance(20)

    # Capture per-controller CSS footers & recordings browser header.
    state["step"] = "wii-inmatch: CSS & browser per-controller screenshots"
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)
    emu.advance(12)
    taken.append(emu.screenshot("wii-list-classic"))

    wii_set_extension(emu, "nunchuk", PAD_TYPE_NUNCHUK)
    emu.advance(12)
    taken.append(emu.screenshot("wii-list-nunchuk"))

    wii_set_extension(emu, "none", PAD_TYPE_WIIMOTE)
    emu.advance(12)
    taken.append(emu.screenshot("wii-list-sideways"))

    n_bopen = count(emu, r"\] browser: opened for port 5")
    wii_press(emu, sideways["browser"])
    wait_count(emu, "wii-inmatch: browser open (Sideways)", r"\] browser: opened for port 5", n_bopen + 1, 120)
    emu.advance(20)
    taken.append(emu.screenshot("wii-browser-sideways"))
    n_bclose = count(emu, r"\] browser: closed")
    wii_press(emu, sideways["back"])
    wait_count(emu, "wii-inmatch: browser close (Sideways)", r"\] browser: closed", n_bclose + 1, 120)
    emu.advance(10)

    # Enter Training Lab (Mario vs Fox on FD).
    state["step"] = "wii-inmatch: enter Training Lab"
    wii_enter_match(emu, None, 0, fixed_fd=False)
    # Wait for Training item file loads to finish so savestates are ready.
    emu.advance(180)
    fighters = LT.Fighters(emu)

    # 1. Start-mode Lab Menu (opens directly on Lab row 0 = Frame Advance) and
    # jump-to-Training-pause on all 3 Wii types.
    for label, ext, ptype, ctrl, pause_jump in [
        ("classic", "classic", PAD_TYPE_CLASSIC, classic, classic["zl"]),
        ("nunchuk", "nunchuk", PAD_TYPE_NUNCHUK, nunchuk, nunchuk["z"]),
        ("sideways", "none", PAD_TYPE_WIIMOTE, sideways, sideways["minus"]),
    ]:
        state["step"] = f"wii-inmatch: Lab Menu Start mode ({label})"
        wii_set_extension(emu, ext, ptype)
        wii_menu_open(emu, ctrl, f"wii-inmatch: {label} '+' opens Lab Menu")
        if label == "classic":
            emu.advance(8)
            taken.append(emu.screenshot("wii-lab-menu-classic"))
        # Toggle Frame Advance (Lab row 0) on then off without leaving Lab.
        n_fa_on = count(emu, r"\] lab: frame advance on")
        wii_press(emu, ctrl["confirm"], frames=10, after=12)
        wait_count(emu, f"wii-inmatch: {label} toggles FA on", r"\] lab: frame advance on", n_fa_on + 1, 120)
        n_fa_off = count(emu, r"\] lab: frame advance off")
        wii_press(emu, ctrl["confirm"], frames=10, after=12)
        wait_count(emu, f"wii-inmatch: {label} toggles FA off", r"\] lab: frame advance off", n_fa_off + 1, 120)

        # Jump from Lab Menu directly to Brawl's Training pause, then resume with '+'.
        wii_press(emu, pause_jump, frames=10, after=16)
        emu.wait_for(f"wii-inmatch: {label} jumps to Brawl pause", lambda: nav.training_paused(emu), 240, every=4)
        wii_pause_toggle(emu, ctrl, False, f"wii-inmatch: {label} '+' resumes Brawl pause")
        emu.advance(12)
    notes.append("wii-inmatch: Lab Menu Start mode, Lab FA toggle, and Training pause jump ok (Classic, Nunchuk, Sideways)")

    # 2. Z-in-Pause mode + pause hints on all 3 Wii controller types.
    state["step"] = "wii-inmatch: switch Menu Open to Z in Pause"
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)
    wii_menu_open(emu, classic, "wii-inmatch: Classic open for Controls")
    # Lab row 0 -> row 9 (Controls; row 8 is Offline VS Displays): 9 Downs, then A enters Controls.
    for _ in range(9):
        wii_press(emu, classic["down"], frames=9, after=10)
    wii_press(emu, classic["confirm"], frames=9, after=10)
    # Controls row 0 -> row 7 (Menu Open): 7 Downs, then Left to switch Start (1) -> Z in Pause (0).
    for _ in range(7):
        wii_press(emu, classic["down"], frames=9, after=10)
    wii_press(emu, classic["left"], frames=9, after=10)
    wii_menu_close(emu, classic, "wii-inmatch: Classic close after setting Z in Pause")
    emu.advance(12)

    wii_pause_toggle(emu, classic, True, "wii-inmatch: Classic '+' opens Brawl pause in Z-in-Pause mode")
    emu.advance(12)
    for label, ext, ptype, ctrl, pause_open, shot in [
        ("classic", "classic", PAD_TYPE_CLASSIC, classic, classic["zl"], "wii-pause-hint-classic"),
        ("nunchuk", "nunchuk", PAD_TYPE_NUNCHUK, nunchuk, nunchuk["z"], "wii-pause-hint-nunchuk"),
        ("sideways", "none", PAD_TYPE_WIIMOTE, sideways, sideways["minus"], "wii-pause-hint-sideways"),
    ]:
        state["step"] = f"wii-inmatch: Z-in-Pause hint & open/close ({label})"
        wii_set_extension(emu, ext, ptype)
        emu.advance(16)
        taken.append(emu.screenshot(shot))
        n_open = count(emu, r"\] lab: menu opened by port 5 \(Z in pause\)")
        wii_press(emu, pause_open, frames=10, after=12)
        wait_count(emu, f"wii-inmatch: {label} opens Lab Menu in pause",
                   r"\] lab: menu opened by port 5 \(Z in pause\)", n_open + 1, 180)
        n_close = count(emu, r"\] lab: menu closed(?! with)")
        # Draft C back chain: Controls -> Lab -> Rail -> Close (up to 3 backs;
        # once on Rail, subsequent reopens close on the first back).
        for _ in range(3):
            if count(emu, r"\] lab: menu closed(?! with)") > n_close:
                break
            wii_press(emu, ctrl["back"], frames=8, after=10)
        wait_count(emu, f"wii-inmatch: {label} closes Z-in-Pause menu",
                   r"\] lab: menu closed(?! with)", n_close + 1, 180)

    # Restore Menu Open back to Start (still paused; Classic ZL reopens on Rail -> Lab).
    state["step"] = "wii-inmatch: restore Menu Open to Start"
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)
    n_open = count(emu, r"\] lab: menu opened by port 5 \(Z in pause\)")
    wii_press(emu, classic["zl"], frames=10, after=12)
    wait_count(emu, "wii-inmatch: reopen Lab Menu to restore Start mode",
               r"\] lab: menu opened by port 5 \(Z in pause\)", n_open + 1, 180)
    # Leave Rail into Lab (row 0), down 9 to Controls, confirm, down 7 to Menu Open, Right restores Start (1).
    wii_press(emu, classic["confirm"], frames=9, after=10)
    for _ in range(9):
        wii_press(emu, classic["down"], frames=9, after=10)
    wii_press(emu, classic["confirm"], frames=9, after=10)
    for _ in range(7):
        wii_press(emu, classic["down"], frames=9, after=10)
    wii_press(emu, classic["right"], frames=9, after=10)
    n_close = count(emu, r"\] lab: menu closed(?! with)")
    for _ in range(3):
        if count(emu, r"\] lab: menu closed(?! with)") > n_close:
            break
        wii_press(emu, classic["back"], frames=9, after=10)
    wait_count(emu, "wii-inmatch: close Z-in-Pause after restoring Start mode",
               r"\] lab: menu closed(?! with)", n_close + 1, 180)
    wii_pause_toggle(emu, classic, False, "wii-inmatch: resume Brawl pause after restoring Start mode")
    emu.advance(16)
    notes.append("wii-inmatch: Z-in-Pause hints & open/close ok (Classic ZL, Nunchuk Z, Sideways -)")

    # 3. Frame Advance (default L): Sideways B must NOT advance or be hidden; '-' / '1' / 'L' advance.
    state["step"] = "wii-inmatch: Frame Advance on Sideways, Nunchuk, Classic"
    wii_set_extension(emu, "none", PAD_TYPE_WIIMOTE)
    wii_menu_open(emu, sideways, "wii-inmatch: Sideways open for Frame Advance")
    # Reopens on Rail (Lab): confirm focuses Lab row 0 (Frame Advance), second confirm toggles FA on.
    wii_press(emu, sideways["confirm"], frames=10, after=12)
    n_fa_on = count(emu, r"\] lab: frame advance on")
    wii_press(emu, sideways["confirm"], frames=10, after=12)
    wait_count(emu, "wii-inmatch: Sideways FA on", r"\] lab: frame advance on", n_fa_on + 1, 120)
    wii_menu_close(emu, sideways, "wii-inmatch: Sideways close with FA on")
    emu.advance(12)

    LT.wait_frozen(emu, True)
    p1 = fighters.by_player(0)
    f0 = fighters.state(p1)["la78"]
    emu.advance(20)
    if fighters.state(p1)["la78"] != f0:
        raise nav.NavError(f"wii-inmatch: game did not freeze under Frame Advance ({f0} -> {fighters.state(p1)['la78']})")

    # Sideways B (sets kPadWiiB | kPadL = 0x400040) must NOT advance and must NOT be hidden!
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/B": 1})
    emu.advance(10)
    b_bits = (pad_status_info(emu, 4) or {}).get("buttons", 0)
    emu.h.call("wiimote_input", index=0)
    emu.advance(10)
    f_after_b = fighters.state(p1)["la78"]
    if f_after_b != f0:
        raise nav.NavError(f"wii-inmatch: Sideways B hijacked Frame Advance L ({f0} -> {f_after_b})")
    if (b_bits & 0x400040) != 0x400040:
        raise nav.NavError(f"wii-inmatch: Sideways B was hidden under Frame Advance L (buttons={b_bits:#x})")

    # Sideways '-' advances by 1 frame.
    wii_press(emu, sideways["minus"], frames=8, after=12)
    f_after_minus = fighters.state(p1)["la78"]
    if f_after_minus != f0 + 1:
        raise nav.NavError(f"wii-inmatch: Sideways '-' expected +1 frame ({f0} -> {f_after_minus})")

    # Nunchuk '1' advances by 1 frame.
    wii_set_extension(emu, "nunchuk", PAD_TYPE_NUNCHUK)
    f_nun = fighters.state(p1)["la78"]
    wii_press(emu, nunchuk["one"], frames=8, after=12)
    if fighters.state(p1)["la78"] != f_nun + 1:
        raise nav.NavError(f"wii-inmatch: Nunchuk '1' expected +1 frame ({f_nun} -> {fighters.state(p1)['la78']})")

    # Classic 'L' advances by 1 frame.
    wii_set_extension(emu, "classic", PAD_TYPE_CLASSIC)
    f_cls = fighters.state(p1)["la78"]
    wii_press(emu, classic["l"], frames=8, after=12)
    if fighters.state(p1)["la78"] != f_cls + 1:
        raise nav.NavError(f"wii-inmatch: Classic 'L' expected +1 frame ({f_cls} -> {fighters.state(p1)['la78']})")

    # Turn Frame Advance back off (menu reopens directly on Lab row 0 Frame Advance).
    wii_menu_open(emu, classic, "wii-inmatch: Classic open to turn FA off")
    n_fa_off = count(emu, r"\] lab: frame advance off")
    wii_press(emu, classic["confirm"], frames=10, after=12)
    wait_count(emu, "wii-inmatch: Classic FA off", r"\] lab: frame advance off", n_fa_off + 1, 120)
    wii_menu_close(emu, classic, "wii-inmatch: Classic close with FA off")
    emu.advance(16)
    notes.append("wii-inmatch: Frame Advance ok (Sideways B untouched, '-' +1f, Nunchuk '1' +1f, Classic 'L' +1f)")

    # 4. Savestates: Classic/Nunchuk bare D-pad vs Sideways '-' + D-pad chord.
    state["step"] = "wii-inmatch: Savestates on Classic, Nunchuk, Sideways"
    for label, ext, ptype, save_btn, load_btn in [
        ("classic", "classic", PAD_TYPE_CLASSIC, classic["right"], classic["left"]),
        ("nunchuk", "nunchuk", PAD_TYPE_NUNCHUK, nunchuk["d_right"], nunchuk["d_left"]),
    ]:
        wii_set_extension(emu, ext, ptype)
        n_save = count(emu, r"\] savestate: saved frame ")
        wii_press(emu, save_btn, frames=16, after=36)
        wait_count(emu, f"wii-inmatch: {label} D-pad right save", r"\] savestate: saved frame ", n_save + 1, 180)
        n_load = count(emu, r"\] savestate: loaded frame ")
        wii_press(emu, load_btn, frames=12, after=20)
        wait_count(emu, f"wii-inmatch: {label} D-pad left load", r"\] savestate: loaded frame ", n_load + 1, 180)

    wii_set_extension(emu, "none", PAD_TYPE_WIIMOTE)
    p1 = fighters.by_player(0)
    x0 = fighters.state(p1)["x"]
    n_save = count(emu, r"\] savestate: saved frame ")
    wii_press(emu, sideways["right"], frames=16, after=12)
    x1 = fighters.state(p1)["x"]
    if count(emu, r"\] savestate: saved frame ") != n_save:
        raise nav.NavError("wii-inmatch: Sideways bare D-pad Right triggered savestate save")
    if x1 <= x0 + 3.0:
        raise nav.NavError(f"wii-inmatch: Sideways bare D-pad Right did not move Mario ({x0:.2f} -> {x1:.2f})")

    # Wait for Mario to settle, then '-' + D-pad Right saves without walking!
    emu.advance(20)
    x_before_save = fighters.state(p1)["x"]
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1})
    emu.advance(6)
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1, "D-Pad/Down": 1})
    emu.advance(16)
    emu.h.call("wiimote_input", index=0)
    emu.advance(36)
    wait_count(emu, "wii-inmatch: Sideways - + D-pad Right save", r"\] savestate: saved frame ", n_save + 1, 180)
    x_after_save = fighters.state(p1)["x"]
    if abs(x_after_save - x_before_save) > 1.5:
        raise nav.NavError(f"wii-inmatch: Sideways - + D-pad Right walked Mario ({x_before_save:.2f} -> {x_after_save:.2f})")

    # Bare D-pad Left moves Mario left without loading; '-' + D-pad Left loads.
    n_load = count(emu, r"\] savestate: loaded frame ")
    wii_press(emu, sideways["left"], frames=16, after=12)
    if count(emu, r"\] savestate: loaded frame ") != n_load:
        raise nav.NavError("wii-inmatch: Sideways bare D-pad Left triggered savestate load")
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1})
    emu.advance(6)
    emu.h.call("wiimote_input", index=0, wiimote={"Buttons/-": 1, "D-Pad/Up": 1})
    emu.advance(12)
    emu.h.call("wiimote_input", index=0)
    emu.advance(20)
    wait_count(emu, "wii-inmatch: Sideways - + D-pad Left load", r"\] savestate: loaded frame ", n_load + 1, 180)
    notes.append("wii-inmatch: Savestates ok (Classic/Nunchuk bare D-pad; Sideways bare moves, '-' + D-pad saves/loads)")

    # 5. Leave Training Lab via Draft C left rail Return to CSS on Sideways (no Brawl pause).
    state["step"] = "wii-inmatch: leave Training Lab via Lab Menu Return to CSS (Sideways)"
    n_paused = count(emu, r"\] match: paused")
    wii_menu_open(emu, sideways, "wii-inmatch: Sideways open Lab Menu for Return to CSS")
    # Lab cursor was left on row 0 (Frame Advance): Back (1) focuses Draft C rail
    # at index 0 (Lab), then LT.MAIN_EXIT (9) Downs reach Return to CSS.
    wii_press(emu, sideways["back"], frames=9, after=10)
    for _ in range(LT.MAIN_EXIT):
        wii_press(emu, sideways["down"], frames=9, after=10)
    wii_press(emu, sideways["confirm"], frames=10, after=12)
    emu.wait_for("wii-inmatch: return to CSS after Lab Menu Exit",
                 lambda: nav.scene(emu)[0] == "scSelctCharacter", 1800, every=10)
    if count(emu, r"\] match: paused") != n_paused:
        raise nav.NavError("wii-inmatch: Brawl pause opened during Sideways Lab Menu Exit")
    emu.advance(30)
    emu.h.call("wiimote_input", index=0, clear=True)
    notes.append("wii-inmatch: Lab Menu Return to CSS ok (Sideways, no Brawl pause)")


def cmd_test_drills(rest):
    opts = args(rest)
    label = "test-drills"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    taken, notes = [], []
    state = {"step": "boot", "matches": 0, "drills": 0, "visits": 0}
    why, log_path = "", ""
    if opts.only in ("all", "gc"):
        try:
            with E.harness_run(backend) as (proc, emu):
                log_path = emu.log_path
                try:
                    run(emu, opts, taken, notes, state)
                except (Timeout, nav.NavError, HarnessError) as e:
                    why = f"{state['step']}: {e}"
                    try:
                        taken.append(emu.screenshot("fail-drills"))
                        print("state at failure:", nav.scene(emu)[:2])
                    except (OSError, HarnessError, ValueError):
                        pass
        except (OSError, HarnessError, Timeout, ValueError) as e:
            why = why or f"{state['step']}: {e}"
        log = E.read_log(log_path) if log_path else ""
        print()
        for line in log.splitlines():
            if re.search(rf"\[{L.tag()}\] (menu|drills|module)", line) and "drills: row" not in line:
                print("  " + line)
        print()
        why = why or analyse(log, opts, state)
        bad = L.problems(log)
        for line in bad:
            print(f"problem: {line}")
        why = why or ("problem lines in the log" if bad else "")

    if not why and opts.only in ("all", "wii", "wii-list", "wii-record", "wii-inmatch"):
        wii_backend = "Null"
        wii_subs = []
        if opts.only in ("all", "wii", "wii-list"):
            wii_subs.append(("wii+gc", wii_backend, (), None, run_wii_with_gc))
        if opts.only in ("all", "wii"):
            wii_subs.append(("wii-only", wii_backend, ("-C", "Dolphin.Core.SIDevice0=0"), None, run_wii_only))
        if opts.only in ("all", "wii", "wii-record"):
            import fatimage
            img = E.storage_image(opts.rel)
            w = fatimage.FatWriter(img)
            try:
                w.put(f"{E.STORAGE_FOLDER}/260102-030405.tmr", build_start_paused_tmr())
            finally:
                w.close()
            wii_subs.append(("wii-record", wii_backend, ("-C", "Dolphin.Core.SIDevice0=0"), img, run_wii_start_paused))
        if opts.only in ("all", "wii", "wii-inmatch"):
            wii_subs.append(("wii-inmatch", E.headless_backend(), ("-C", "Dolphin.Core.SIDevice0=0"), None,
                             lambda e, n, s: run_wii_inmatch(e, n, s, taken)))
        for sub_name, sub_backend, extra_args, wimg, fn in wii_subs:
            if why:
                break
            wii_log_path = ""
            try:
                with E.harness_run(sub_backend, extra_args=extra_args, writable_image=wimg,
                                   allow_null=(sub_backend == "Null"), wiimotes=(E.WIIMOTE_EMULATED, 0, 0, 0),
                                   css_cache=False) as (proc, emu):
                    wii_log_path = emu.log_path
                    try:
                        fn(emu, notes, state)
                    except (Timeout, nav.NavError, HarnessError) as e:
                        why = f"{state['step']}: {e}"
                        if sub_backend != "Null":
                            try:
                                taken.append(emu.screenshot(f"fail-{sub_name}"))
                            except (OSError, HarnessError, ValueError):
                                pass
                    finally:
                        try:
                            emu.h.call("wiimote_input", index=0, clear=True)
                        except (OSError, HarnessError, ValueError):
                            pass
            except (OSError, HarnessError, Timeout, ValueError) as e:
                why = why or f"{state['step']}: {e}"
            if wimg is not None and not why:
                E.remove_storage_image(wimg)
            if wii_log_path:
                log_path = wii_log_path
                wlog = E.read_log(wii_log_path)
                print(f"\n--- {sub_name} log ---")
                for line in wlog.splitlines():
                    if re.search(rf"\[{L.tag()}\] (boot|menu|drills|drill|browser|lab|record|savestate|storage)", line) and "drills: row" not in line:
                        print("  " + line)
                bad = L.problems(wlog)
                for line in bad:
                    print(f"problem ({sub_name}): {line}")
                why = why or (f"problem lines in {sub_name} log" if bad else "")

    for note in notes:
        print(note)
    for path, w, h in taken:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"drill cycles: {state['drills']}; matches: {state['matches']}; time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
