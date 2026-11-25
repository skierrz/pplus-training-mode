"""Emulator tests of the match timer (run through emu.py):
Brawl's own HUD timer piece, driven by src/main/ui/match_hud.cpp.

  test-match-hud REL [--only NAME ...] [--aspect 16:9|4:3]
  test-match-hud REL --versus   (a build with EXPERIMENTS=1 ANY_MATCH=1)
      One boot into Training with the training menu open, then:
        lab       a lab match: no counter, the lab's timer counting; hidden
                  with HUD Off and Training's pause, held by frame advance,
                  under the lab menu opened with Start; savestates saved and
                  loaded with the timer shown and heap 40 unchanged.
        gametimer a lab match whose HUD has its timer bit set before the
                  module first looks (as a match with a time limit has): the
                  module leaves it alone though the lab asks for its timer.
      With --versus instead: a two-player Versus match, where the module
      loads only in that build. The lab asks for its timer there too, but
      the match is not Training: the game's HUD is left as the game runs it
      (no line of ours, the timer's bit and time as the game sets them).
      Every step reads the centre HUD (IfMngr+0x44): byte 0's timer bit, the
      time set (+0x17C), the timer's and the counter's models (+0x154, +0x160)
      and whether each is in the HUD's scene group (the model's +4), and the
      HUD group's members.
"""
import argparse
import os
import re
import struct
import time

import emu as E
import lab_tests as LT
import loader_tests as L
import memdump as M
import nav
import shots
from harness import HarnessError, Timeout

KIND_TRAINING = nav.CENTRE_HUD_MATCH_TRAINING
INFO_HEAP = 40
MADE = re.compile(r"match hud: counter made: model ([0-9a-f]{8}); heap 40 free (\d+) -> (\d+)")


class HudError(RuntimeError):
    pass


hud = nav.centre_hud
fmt = nav.centre_hud_text


def heap_used(emu, hid=INFO_HEAP):
    raw = emu.read(M.HEAP_INFOS + 16 * hid, 16)
    _, pool, _, _ = struct.unpack(">4I", raw)
    (used,) = struct.unpack(">I", emu.read(pool + M.POOL_USED, 4))
    return used


# Near-white pixels in a piece's place: at least SHOWN when it shows, at
# most HIDDEN when it does not (shots.match_hud_pixels).
SHOWN, HIDDEN = 15, 3


def aspect(emu):
    m = re.findall(r"lab: screen (\S+);", emu.log_text())
    return m[-1] if m else "16:9"


class Run:
    def __init__(self, emu, notes, shots_list):
        self.emu = emu
        self.notes = notes
        self.shots = shots_list

    def note(self, line):
        self.notes.append(line)

    def shot(self, name):
        s = self.emu.screenshot(name)
        self.shots.append(s)
        return s

    def expect(self, what, h, timer=None, counter=None, shot=None):
        """The centre HUD's state `h` against what should show; with `shot`
        (a screenshot name), the picture too."""
        problems = []
        if h.get("kind") != KIND_TRAINING:
            problems.append(f"HUD kind {h.get('kind')}, not Training's {KIND_TRAINING}")
        if timer is not None and h.get("timer") != timer:
            problems.append("timer not shown" if timer else "timer not hidden")
        if counter is not None and h.get("counter") != counter:
            problems.append("counter not shown" if counter else "counter not hidden")
        # each piece in the HUD's group once when shown, never when hidden
        for piece, want in (("timer", timer), ("counter", counter)):
            n = h.get("members", []).count(h.get(piece + "_mdl"))
            if want is not None and n != (1 if want else 0):
                problems.append(f"the {piece} is in the HUD's group {n} times")
        if not h.get("members_ok", True):
            problems.append("a member of the HUD's group has another parent")
        seen = ""
        if shot is not None:
            path = self.shot(shot)[0]
            px = {}
            for piece, want in (("timer", timer), ("counter", counter)):
                if want is None:
                    continue
                px[piece] = shots.match_hud_pixels(path, aspect(self.emu), piece)
                if px[piece] is None:
                    continue
                if (want and px[piece] < SHOWN) or (not want and px[piece] > HIDDEN):
                    problems.append(f"{piece} {'not ' if want else ''}on the picture ({px[piece]} pixels)")
            seen = f"; on {os.path.basename(path)}: {px}"
        self.note(f"{what}: {fmt(h)}{seen}")
        if problems:
            raise HudError(f"{what}: {problems} ({fmt(h)})")


def counting(run, what, frames=30, still=False):
    """The time set over `frames` frames, and the timer's place on the
    picture changed (or, `still`, the same)."""
    emu = run.emu
    pa = run.shot("match-hud-count-a")[0]
    a = hud(emu)["time"]
    emu.advance(frames)
    b = hud(emu)["time"]
    pb = run.shot("match-hud-count-b")[0]
    sa, sb = shots.Shot(pa, 1.0), shots.Shot(pb, 1.0)
    rect = shots.MATCH_HUD_RECTS.get(aspect(emu), {}).get("timer")
    if rect is None:
        run.note(f"{what}: time {a} -> {b} in {frames} frames; the picture not checked ({aspect(emu)})")
        if not frames - 2 <= b - a <= frames + 2 and not still:
            raise HudError(f"{what}: the time went {a} -> {b} in {frames} frames")
        return
    # the stage's background moves behind it: only its near-white digits count
    changed = sum(1 for x, y in sa.area(*rect) if (min(sa.px(x, y)) >= 200) != (min(sb.px(x, y)) >= 200))
    run.note(f"{what}: time {a} -> {b} in {frames} frames; {changed} near-white pixels changed in the timer's "
             "place")
    if still and (a != b or changed > 0):
        raise HudError(f"{what}: the time moved {a} -> {b} ({changed} pixels)")
    if not still and (not frames - 2 <= b - a <= frames + 2 or changed < 10):
        raise HudError(f"{what}: the time went {a} -> {b} in {frames} frames ({changed} pixels changed)")


def log_since(emu, pos):
    return emu.log_text()[pos:]


# ---- the free lab ------------------------------------------------------------------

def hold_dpad(emu, button, frames):
    emu.hold(0, buttons=[button])
    emu.advance(frames)
    emu.neutral(0)
    emu.advance(10)


def lab(run):
    emu = run.emu
    nav.close_drill_list(emu)
    pos = len(emu.log_text())
    nav.start_training_match(emu, "mario", "fox")
    emu.advance(30)
    lb = LT.Lab(emu, [])
    lb.find_settings()
    h = hud(emu)
    run.expect("lab, at the start", h, timer=True, counter=False, shot="match-hud-lab")
    if h["counter_mdl"] != 0 or MADE.search(log_since(emu, pos)):
        raise HudError("lab: a counter was made")
    counting(run, "lab timer")
    LT.write_u8(emu, lb, LT.SET_HUD, 0)
    emu.advance(4)
    run.expect("lab, HUD Off", hud(emu), timer=False, shot="match-hud-lab-hud-off")
    LT.write_u8(emu, lb, LT.SET_HUD, 1)
    emu.advance(4)
    run.expect("lab, HUD on again", hud(emu), timer=True)
    nav.pause_training(emu)
    emu.advance(6)
    run.expect("lab, Training's pause", hud(emu), timer=False, shot="match-hud-lab-paused")
    lb.open_menu()
    emu.advance(6)
    run.expect("lab, lab menu from the pause", hud(emu), timer=True)
    run.shot("match-hud-lab-menu-from-pause")
    lb.close_menu()
    nav.resume_training(emu)
    emu.advance(6)
    run.expect("lab, resumed", hud(emu), timer=True)
    # Start opens the lab menu over the running match
    menu_button = lb.setting(LT.SET_START_OPENS)
    LT.write_u8(emu, lb, LT.SET_START_OPENS, 1)
    p = lb.log_pos()
    emu.press(0, ["START"], frames=3, after=10)
    lb.wait_log("the lab menu to open with Start", LT.MENU_OPENED, p)
    emu.advance(6)
    h = hud(emu)
    run.note(f"lab, menu opened with Start: {fmt(h)}; HUD layer shown {LT.hud_layer_shown(emu)}")
    run.shot("match-hud-lab-menu-start")
    p = lb.log_pos()
    emu.press(0, ["START"], frames=3, after=10)
    lb.wait_log("the lab menu to close with Start", r"lab: menu closed", p)
    LT.write_u8(emu, lb, LT.SET_START_OPENS, menu_button)
    emu.advance(6)
    run.expect("lab, menu closed", hud(emu), timer=True)
    # frame advance holds the game: the time stays
    LT.write_u8(emu, lb, LT.SET_FRAME_ADVANCE, 1)
    emu.advance(30)
    counting(run, "lab, frame advance", still=True)
    LT.write_u8(emu, lb, LT.SET_FRAME_ADVANCE, 0)
    emu.advance(10)
    # Savestates: save and load with the timer shown; heap 40 stays unchanged.
    before = heap_used(emu)
    hold_dpad(emu, "DRIGHT", 20)
    run.expect("lab, saved with the timer shown", hud(emu), timer=True)
    emu.advance(30)
    hold_dpad(emu, "DLEFT", 4)
    run.expect("lab, loaded with the timer shown", hud(emu), timer=True, shot="match-hud-lab-loaded-shown")
    counting(run, "lab timer after the load")
    used = heap_used(emu)
    run.note(f"lab: heap 40 used {before} -> {used} over save and load")
    if used != before:
        raise HudError(f"lab: heap 40 used {before} -> {used}")
    # Finish: from the pause (hidden there) to the scene's end, every frame
    nav.pause_training(emu)
    nav.menu_select(emu, nav.MENU_FINISH)
    emu.press(0, ["A"], frames=2, after=0)
    frames, shown = 0, []
    while nav.scene(emu)[0] == "scMelee" and frames < 600:
        h = hud(emu)
        if h.get("timer") or h.get("counter"):
            shown.append(frames)
        emu.advance(1)
        frames += 1
    run.note(f"lab: Finish to the end of the match scene in {frames} frames; a piece shown on frames {shown[:10]}")
    if shown or frames >= 600:
        raise HudError(f"lab: a piece shown while the match ended (frames {shown[:10]})")
    emu.wait_for("Training's character select", lambda: nav.scene(emu)[0] == "scSelctCharacter", 900, every=5)


def versus(run):
    emu = run.emu
    nav.enter_versus_match(emu, ["mario", "fox"])
    emu.advance(60)
    text = emu.log_text()
    if "main: attached in scMelee" not in text:
        raise HudError("the main module did not load in Versus: build with EXPERIMENTS=1 ANY_MATCH=1")
    a = hud(emu)
    run.shot("match-hud-versus")
    emu.advance(60)
    b = hud(emu)
    ours = re.findall(r"match hud: [^\r\n]*", emu.log_text())
    run.note(f"versus: {fmt(a)}; 60 frames later time {b.get('time')}, flags {b.get('flags', 0):#04x}; our lines "
             f"{ours}")
    if a.get("kind") == KIND_TRAINING or ours:
        raise HudError(f"versus: HUD kind {a.get('kind')}, lines of ours {ours}")
    if a.get("flags", 0) & nav.CENTRE_HUD_TIMER_ON and b.get("time") == a.get("time"):
        raise HudError("versus: the game's timer is on but stood still")


def gametimer(run):
    emu = run.emu
    nav.close_drill_list(emu)
    pos = len(emu.log_text())

    def timer_bit_first(e):
        def centre():
            m = e.u32(nav.IF_MANAGER)
            return e.u32(m + 0x44) if e.is_ram(m) else 0
        e.wait_for("the centre HUD", lambda: e.is_ram(centre()), 1800, every=1)
        c = centre()
        # after the game's own start set the flags: the start message's mode
        # (centre +4) goes to 1 some 50 frames before the module first looks
        e.wait_for("the start message", lambda: e.u32(c + 4) != 0, 600, every=1)
        if "match hud: centre" in e.log_text()[pos:]:
            raise HudError("gametimer: the module looked before the timer bit was set")
        e.h.call("write_mem", addr=c, hex=f"{e.u8(c) | nav.CENTRE_HUD_TIMER_ON:02x}")
    nav.start_training_match(emu, "mario", "fox", during=timer_bit_first)
    emu.advance(30)
    a = hud(emu)
    emu.advance(30)
    b = hud(emu)
    lines = re.findall(r"match hud: [^\r\n]*", log_since(emu, pos))
    run.note(f"gametimer: {fmt(a)}; 30 frames later time {b.get('time')}, flags {b.get('flags', 0):#04x}; "
             f"lines {lines}")
    problems = []
    if not any("the game's own timer is on, left alone" in x for x in lines):
        problems.append("no 'left alone' line")
    if any("timer shown" in x or "timer hidden" in x for x in lines):
        problems.append("the module drove the timer")
    if a.get("time") != b.get("time") or not a.get("flags", 0) & nav.CENTRE_HUD_TIMER_ON \
            or a.get("flags", 0) & 0x40:
        problems.append("the time or the flags changed")
    if problems:
        raise HudError(f"gametimer: {problems}")
    nav.finish_training(emu)


def one_look_per_match(run):
    """The module's first look at a match's HUD decides whose timer it is:
    one per match the module attached to."""
    text = run.emu.log_text()
    matches = len(re.findall(r"main: attached in scMelee", text))
    looks = len(re.findall(r"match hud: centre ", text))
    run.note(f"{matches} matches, {looks} first looks")
    if looks != matches:
        raise HudError(f"{looks} first looks in {matches} matches")


def run_all(run, opts, state):
    want = (lambda n: not opts.only or n in opts.only)  # noqa: E731
    if want("lab"):
        state["step"] = "lab"
        lab(run)
    if want("gametimer"):
        state["step"] = "gametimer"
        gametimer(run)
    one_look_per_match(run)


def cmd_test_match_hud(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-match-hud")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--aspect", choices=("16:9", "4:3"))
    ap.add_argument("--versus", action="store_true")
    opts = ap.parse_args(rest)
    label = "test-match-hud"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, log_path, shots_taken, notes = "", "", [], []
    state = {"step": "boot"}
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS) as (proc, emu):
            log_path = emu.log_path
            run = Run(emu, notes, shots_taken)
            try:
                if opts.versus:
                    state["step"] = "versus"
                    versus(run)
                else:
                    state["step"] = "boot into Training with the list open"
                    nav.boot_to_training_css(emu, drill_list="keep")
                    emu.wait_for("the training menu to open", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
                    if opts.aspect:
                        LT.set_aspect(emu, opts.aspect, run.note)
                    run_all(run, opts, state)
            except (Timeout, nav.NavError, HarnessError, LT.LabError, HudError) as e:
                why = f"{state['step']}: {e}"
                try:
                    emu.h.call("resume")
                    shots_taken.append(emu.screenshot("fail-match-hud"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{state['step']}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if "match hud:" in line:
            print("  " + line.strip()[line.strip().find("["):])
    print()
    for n in notes:
        print(n)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    why = why or ("problem lines in the log" if bad else "")
    for path, w, h in shots_taken:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
