"""Emulator test of the frame data service (src/extra/frame_data.*), run
through emu.py; settings come from the environment the same way.

  test-frames REL [--chars NAME ...] [--only NAME ...] [--keep-going]

One boot. For each character in --chars (default mario fox bowser
jigglypuff) a Training match against a Fox CPU (a Mario CPU for Fox) on
Final Destination, the CPU standing still under the lab's CPU control with
no counter action. Three states are saved per character: far (the fighters
where the match put them), near (P1 walked up to the CPU) and shield (near,
the CPU's Behavior on Shield). Every scenario starts from one of them; P1
is driven through the plugin's script source, so every input lands on its
game frame.

While a scenario runs, the trackers and the frame data service log every
frame (their trace bytes): the service's line carries, for the frame that
just ended, the frames until the fighter can act as read from its move's
scripts ("in", with how and whether the scripts settle it) next to the
trackers' live reading of that frame ("act"). The check, for every move:

  - the first frame after the move began in which the trackers say the
    fighter could act is the move's first actionable frame;
  - on every earlier frame of the status the move ended in, a prediction
    that says it is certain must name exactly that frame (frame + in);
  - the frame before it must carry a certain prediction of 1, unless the
    scenario is listed as not statically knowable.

Dodges and rolls are then played twice more with a jab on the predicted
frame and on the frame before: the jab must come out on the first and not
on the second (an input on the predicted frame acts in it; this is what the
CPU's gate relies on). Their intangibility window, as the service read it
from the script, is compared with the body's real collision state on every
frame of the dodge, and so are the frames it counted ahead to the window's
beginning and end. (The hitbox, grab box and throw windows of the record
are not checked by any test yet.)

Aerials are played from a full hop (they end in the air if the hop is long
enough), from a short hop (they land with their landing lag) and from the
short hop with shield pressed 3 frames before the landing (Project+'s
L-cancel); what is checked is the status the fighter was last in before it
could act, from the landing on.

Hits (near) and hits on shield (shield): the service's "advantage
predicted" line, written when the hit lands, against the advantage
tracker's measured value ("advantage check" line, written by the service
when the measurement arrives). A grounded attack on shield is shown when
the hit lands: a check line that says DIFFERENT fails the test, and a match
must have posted exactly one message. A hit that connects is predicted in
the log only: a difference is counted and reported. The attacker's own
first actionable frame is checked from the last hit on (a prediction made
before a hit cannot know its hitlag). The run ends with the counts per kind
of hit. "corrected": a jab on the shield of a CPU that grabs out of shield;
the value shown at the hit must be followed by the measured one, marked
"Measured:", although it is past the display limit.

NAME for --only: a scenario name (jab, utilt, fsmash, nair, fair, bair,
uair, dair, grab, spotdodge, rollf, rollb, airdodge, land, shield, hit-jab,
hit-utilt, hit-fsmash, shield-jab, shield-utilt, shield-fsmash, corrected,
info).
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
import plugin_input as PI
from harness import HarnessError, Timeout
from plugin_input import B_ATTACK, B_FLICK, B_GRAB, B_GUARD, B_JUMP, B_SMASH, frames_to_steps

CHARS = ["mario", "fox", "bowser", "jigglypuff"]
NEAR = {"jigglypuff": 9.0}  # how close P1 walks up to the CPU; 16 for the others
S_OSD_ADVANTAGE, S_OSD_ON_HIT = 89, 90  # osd.frameAdvantage, osd.frameAdvantageOnHit
BEHAVIOR_SHIELD = 1                      # cpu.options.behavior
S_CPU_COUNTERS = (128, 129, 130)         # cpu.options.counterGround, counterAir, counterShield (0 = None)
S_CPU_COUNTER_SHIELD, COUNTER_GRAB = 130, 1
INFO_ACT_IN = 35                         # ui::kInfoActIn
SET_HUMAN_ROWS = LT.SET_HMN_INFO + 2
JUMP = B_JUMP | B_FLICK
ST_WAIT, ST_JUMP, ST_ATTACK, ST_LANDING_AIR, ST_ESCAPE_AIR = 0x0, 0xB, 0x24, 0x18, 0x21
ST_GUARD_ON, ST_GUARD, ST_GUARD_OFF = 0x1A, 0x1B, 0x1C
ST_LANDING, ST_LANDING_LIGHT = 0x16, 0x17
ST_ESCAPES = (0x1E, 0x1F, 0x20)  # spot dodge, roll forward, roll back
GROUP_ATTACK = 1 << 4  # game::kGroupGroundAttack
HOW = {0: "none", 1: "open", 2: "script", 3: "end", 4: "rule", 5: "stun"}

FD = re.compile(r"fd f=(\d+) p=(\d)(b?) st=(\w+) sub=(-?\d+) clk=(-?\d+) fr=(-?\d+) rate=(-?\d+) len=(\d+) "
                r"hl=(\d+) in=(-?\d+) how=(\d) sure=(\d) flags=(\w+) grp=(\w+) act=(\d) "
                r"body=(-?\d+) win=(\d+) bon=(-?\d+) boff=(-?\d+)")
WALK = re.compile(r"fd walk (\d+) st=(\w+) sub=(-?\d+) open=(-?\d+) groups=(\w+) end=(\d)/(-?\w+),(\d)/(-?\w+) "
                  r"rule=(\d) flags=(\w+) hit=(\d+) body=(\d+) rate=(\d+) throw=(-?\d+) cmds=(\d+)")
PREDICTED = re.compile(r"advantage predicted: P(\d)b? -> P(\d)b? on (hit|shield) ([+-]\d+) \(hit (\d+), attacker in "
                       r"(-?\d+) how (\d), defender in (\d+)\)")
CHECK = re.compile(r"advantage check: P(\d) -> P(\d) on (hit|shield) predicted (none|[+-]\d+) measured ([+-]\d+) "
                   r"(same|DIFFERENT|\([^)]*\))")
INFO = re.compile(r"info P1: Actionable In:( -?\d+)?")
OSD = re.compile(r"osd: P(\d) id (\d+): ([^\r\n]*)")
OSD_ADVANTAGE = "14"  # osd::kIdFrameAdvantage
TRACKER_BYTE = re.compile(r"tracker: trace byte at ([0-9a-f]{8})")
FD_BYTE = re.compile(r"fd: trace byte at ([0-9a-f]{8})")


class FrameError(RuntimeError):
    pass


class Tester:
    def __init__(self, emu, out, keep_going):
        self.emu = emu
        self.out = out
        self.keep_going = keep_going
        self.r = DO.Runner(emu, [], [])
        self.results = []
        self.failures = []
        self.shots = []
        self.step = "boot"
        self.char = ""
        self.states = {}
        self.toward = 1.0
        # Per kind of hit: predictions that matched the measured value, that
        # differed, and hits with no prediction.
        self.stats = {"hit": [0, 0, 0], "shield": [0, 0, 0]}
        self.differences = []

    def say(self, text):
        print(text)
        self.out.append(text)

    def ok(self, name, detail):
        self.results.append((f"{self.char} {name}", detail))
        self.say(f"ok   {self.char} {name}: {detail}")

    def fail(self, name, detail):
        if not self.keep_going:
            raise FrameError(f"{self.char} {name}: {detail}")
        self.failures.append(f"{self.char} {name}: {detail}")
        self.say(f"FAIL {self.char} {name}: {detail}")

    # ---- a match per character --------------------------------------------------

    def start(self, char, first, states):
        e, r = self.emu, self.r
        self.char = char
        if first:
            nav.boot_to_training_css(e)
        else:
            r.inp.stop()
            e.h.call("resume")
            nav.finish_training(e)
        # The CPU is Fox; against Fox it is Mario (two of one character
        # cannot both wear costume 0, which the character select steps to).
        nav.start_training_match(e, char, "mario" if char == "fox" else "fox", seed_rng=True)
        nav.pause_at_game_frame(e, DO.BASE_FRAME)
        r.lab.find_settings()
        r.m, r.c = r.f.by_player(0), r.f.by_player(1)
        r.inp = PI.PluginInput(e)
        log = e.log_text()
        tb, fb = TRACKER_BYTE.findall(log), FD_BYTE.findall(log)
        if not tb or not fb:
            raise FrameError("no trace byte lines in the log (is the extra module loaded?)")
        self.bytes = (int(tb[-1], 16), int(fb[-1], 16))
        for offset, value in DO.CPU_STILL.items():
            r.set(offset, value)
        # No counter action: after a hit the CPU stands, after a hit on its
        # shield it keeps shielding (its defaults are a spotdodge and a grab,
        # which would hit P1 before the measurement ends).
        for offset in S_CPU_COUNTERS:
            r.set(offset, 0)
        r.set(S_OSD_ADVANTAGE, 1)
        r.set(S_OSD_ON_HIT, 1)
        e.h.call("frame_advance", n=30)
        self.toward = 1.0 if r.distance() > 0 else -1.0
        self.states = {}
        self.save("far", states)
        # Near enough for a jab to reach (Jigglypuff's does not from 16). A
        # walk is 240 frames at most, which a slow walker needs twice.
        near = NEAR.get(char, 16.0)
        for _ in range(4):
            if abs(r.distance()) >= near:
                r.walk(near)
        self.save("near", states)
        r.set(DO.S_CPU_BEHAVIOR, BEHAVIOR_SHIELD)
        e.h.call("frame_advance", n=40)
        self.save("shield", states)
        # The same with the CPU grabbing out of shield: it grabs P1 before
        # P1 can act, so the measured advantage is far from the predicted.
        r.set(S_CPU_COUNTER_SHIELD, COUNTER_GRAB)
        e.h.call("frame_advance", n=2)
        self.save("shield-grab", states)
        self.say(f"{char}: far, near and shield states saved; P1 faces {'right' if self.toward > 0 else 'left'}, "
                 f"{abs(r.distance()):.1f} from the CPU when near")

    def save(self, name, states):
        self.states[name] = os.path.join(states, f"frames-{self.char}-{name}.sav")
        self.emu.h.call("save_state", path=self.states[name])

    # ---- one run ----------------------------------------------------------------

    def play(self, state, frames, total, extra=2):
        """Loads `state`, plays P1's {driven frame: (buttons, x, y)} over
        `total` frames plus `extra`, the traces on. Returns P1's service rows
        (row i is driven frame i; row 0 is the frame before the script), the
        statuses read from memory after each driven frame (index 0 = frame
        1) and the log text of the run."""
        e, r = self.emu, self.r
        e.h.call("load_state", path=self.states[state])
        e.h.call("pause")
        # After a load the first step only completes the saved frame; the
        # load also put the trace bytes back to 0.
        e.h.call("frame_advance", n=1)
        for addr in self.bytes:
            e.h.call("write_mem", addr=addr, hex="01")
        pos = len(e.log_text())
        steps = frames_to_steps(frames, total)
        seen = r.inp.run({0: steps}, read=lambda i: r.f.state(r.m)["status"], extra=extra)
        r.inp.stop()
        for addr in self.bytes:
            e.h.call("write_mem", addr=addr, hex="00")
        text = e.synced_log_text()[pos:]
        rows = [{"f": int(g[0]), "st": int(g[3], 16), "sub": int(g[4]), "clk": int(g[5]) / 100.0,
                 "fr": int(g[6]) / 100.0, "rate": int(g[7]) / 100.0, "len": int(g[8]), "hl": int(g[9]),
                 "in": int(g[10]), "how": int(g[11]), "sure": g[12] == "1", "flags": int(g[13], 16),
                 "grp": int(g[14], 16), "act": g[15] == "1", "body": int(g[16]), "win": int(g[17]),
                 "bon": int(g[18]), "boff": int(g[19])}
                for g in FD.findall(text) if g[1] == "1" and not g[2]]
        if len(rows) < total:
            raise FrameError(f"{len(rows)} service rows for P1 over {total} frames")
        # Row i must be driven frame i: its status is the one read from
        # memory after that frame.
        off = [i for i in range(1, min(len(rows), len(seen) + 1)) if rows[i]["st"] != seen[i - 1]]
        if off:
            raise FrameError(f"service rows and driven frames do not line up (first at frame {off[0]}: row status "
                             f"{rows[off[0]]['st']:#x}, memory {seen[off[0] - 1]:#x})")
        return rows, seen, text


def first(rows, start, test):
    for i in range(start, len(rows)):
        if test(rows[i]):
            return i
    return None


def check_move(t, name, rows, unknown=False, live=None, start=1):
    """The rule of the module docstring. `live`: the live reading to use in
    place of the trackers' (a row -> could act). `start`: the first row
    looked at (after a hit: a prediction made before it could not know the
    hitlag). Returns the index (driven frame) of the first actionable
    frame."""
    live = live or (lambda x: x["act"])
    busy = first(rows, start, lambda x: not live(x))
    if busy is None:
        t.fail(name, "P1 never left its actionable state: the input did not take")
        return None
    act = first(rows, busy, live)
    if act is None:
        t.fail(name, f"not actionable again within {len(rows) - busy} frames")
        return None
    last = rows[act - 1]
    k = act - 1
    while k > busy and (rows[k - 1]["st"], rows[k - 1]["sub"]) == (last["st"], last["sub"]):
        k -= 1
    told = [(i, x) for i, x in enumerate(rows[k:act], k) if x["sure"] and x["in"] >= 0]
    wrong = [(i, x["in"]) for i, x in told if i + x["in"] != act]
    span = f"status {last['st']:#x} subaction {last['sub']}, frames {k}..{act - 1}"
    if wrong:
        t.fail(name, f"actionable on frame {act}, but {span} predicted {wrong[:6]} (frame, in)")
        return act
    if not unknown and not (last["sure"] and last["in"] == 1):
        t.fail(name, f"actionable on frame {act}, but the frame before said in={last['in']} how="
                     f"{HOW.get(last['how'])} certain={last['sure']} flags={last['flags']:#x} ({span})")
        return act
    t.ok(name, f"first actionable frame {act - busy + 1} of the busy stretch (driven frame {act}); {len(told)} of "
               f"{act - k} frames of {span} predicted it, by {HOW.get(last['how'])}"
               + (" (not statically knowable, as listed)" if unknown and not told else ""))
    return act


def press(frame, buttons, x=0.0, y=0.0, hold=2):
    return {frame + k: (buttons, x, y) for k in range(hold)}


def sc_ground(t, name, buttons, x=0.0, y=0.0, total=90, unknown=False):
    rows, _, _ = t.play("far", press(1, buttons, x * t.toward, y), total)
    return check_move(t, name, rows, unknown)


def check_body(t, name, rows):
    """The dodge's intangibility window as the service read it from the
    script, against the body's real collision state (the hit module's
    whole-body state, logged beside it) on every frame of the dodge: the
    state the window gives for the frame, and the frames it counted ahead
    to the window's beginning and end (frame + bon, frame + boff)."""
    seg = [i for i, x in enumerate(rows) if x["st"] in ST_ESCAPES]
    if not seg:
        t.fail(name + " intangibility", "no dodge status in the run")
        return
    a, b = seg[0], seg[-1]
    on = first(rows, a, lambda x: x["body"] != 0)
    off = first(rows, on, lambda x: x["body"] == 0) if on is not None else None
    if on is None or off is None or off > b:
        t.fail(name + " intangibility", f"the body was never intangible and normal again inside the dodge (frames {a}..{b}: "
                                        f"on {on}, off {off})")
        return
    now = [(i, rows[i]["win"], rows[i]["body"]) for i in range(a, b + 1) if rows[i]["win"] != rows[i]["body"]]
    ahead_on = [(i, rows[i]["bon"]) for i in range(a, on) if rows[i]["bon"] > 0]
    ahead_off = [(i, rows[i]["boff"]) for i in range(a, off) if rows[i]["boff"] > 0]
    wrong = [("on", i, n) for i, n in ahead_on if i + n != on] + [("off", i, n) for i, n in ahead_off if i + n != off]
    if now:
        t.fail(name + " intangibility", f"window and body differ on {len(now)} frames, first {now[:4]} (frame, window, "
                                        f"body); the body is intangible on frames {on}..{off - 1}")
    elif wrong or len(ahead_on) != on - a or len(ahead_off) != off - a:
        t.fail(name + " intangibility", f"the body is intangible on frames {on}..{off - 1}; counted ahead wrongly "
                                        f"{wrong[:4]} (which, frame, frames), {len(ahead_on)} of {on - a} and "
                                        f"{len(ahead_off)} of {off - a} frames counted")
    else:
        t.ok(name + " intangibility", f"state {rows[on]['body']} on frames {on - a + 1}..{off - a} of the dodge, as the "
                                      f"script's window says on each of its {b - a + 1} frames, and counted ahead on "
                                      f"every frame before ({len(ahead_on)} to its start, {len(ahead_off)} to its end)")


def sc_dodge(t, name, x, y):
    """A spotdodge or roll: its first actionable frame, its intangibility
    window, then the jab on the predicted frame and the one before."""
    dodge = press(1, B_GUARD, x * t.toward, y)
    rows, _, _ = t.play("far", dodge, 90)
    check_body(t, name, rows)
    act = check_move(t, name, rows)
    if act is None:
        return
    outcomes = {}
    for at in (act, act - 1):
        frames = dict(dodge)
        frames.update(press(at, B_ATTACK))
        _, seen, _ = t.play("far", frames, at + 6)
        outcomes[at] = seen[at - 1]
    if outcomes[act] != ST_ATTACK:
        t.fail(name + " jab", f"a jab on the predicted frame {act} gave status {outcomes[act]:#x} in that frame")
    elif outcomes[act - 1] == ST_ATTACK:
        t.fail(name + " jab", f"a jab on frame {act - 1}, before the predicted frame, already came out")
    else:
        t.ok(name + " jab", f"a jab on frame {act} comes out in it, one on frame {act - 1} does not "
                            f"(status {outcomes[act - 1]:#x})")


def sc_aerial(t, name, x, y):
    """The aerial on the frame after takeoff: from a full hop (it ends in
    the air if the hop is long enough), from a short hop (it lands with its
    landing lag), and from the short hop again with shield pressed 3 frames
    before the landing (Project+'s L-cancel)."""
    hop = {k: (JUMP, 0.0, 0.0) for k in range(1, 30)}
    _, seen, _ = t.play("far", hop, 30)
    takeoff = next((i + 1 for i, s in enumerate(seen) if s == ST_JUMP), None)
    if takeoff is None:
        t.fail(name, f"no jump within 30 frames of holding jump: {[hex(s) for s in seen[:12]]}")
        return
    frames = {k: (JUMP, 0.0, 0.0) for k in range(1, takeoff + 1)}
    frames.update(press(takeoff + 1, B_ATTACK, x * t.toward, y))
    rows, seen, _ = t.play("far", frames, 150)
    check_move(t, name + " (full hop)", rows)
    # Jump let go inside the jump squat: a short hop.
    frames = press(1, JUMP, hold=2)
    frames.update(press(takeoff + 1, B_ATTACK, x * t.toward, y))
    rows, seen, _ = t.play("far", frames, 110)
    landing = next((i + 1 for i, s in enumerate(seen) if s == ST_LANDING_AIR), None)
    if landing is None:
        check_move(t, name + " (short hop)", rows)
        t.say(f"note {t.char} {name}: the short hop did not land in the aerial (statuses "
              f"{sorted(set(hex(s) for s in seen))}); no landing lag to check")
        return
    act = check_move(t, name + " landing", rows, start=landing)
    frames.update(press(landing - 3, B_GUARD))
    rows2, seen2, _ = t.play("far", frames, 110)
    landing2 = next((i + 1 for i, s in enumerate(seen2) if s == ST_LANDING_AIR), None)
    if landing2 is None:
        t.fail(name + " L-cancel", "no landing lag status in the run with the shield press")
        return
    act2 = check_move(t, name + " L-cancel", rows2, start=landing2)
    if act is not None and act2 is not None and not act2 < act:
        t.fail(name + " L-cancel", f"actionable on frame {act2}, no earlier than without the press ({act})")


def sc_land(t):
    """A full hop with no input: the landing opens by the fighter's landing
    frame parameter (the status's own rule)."""
    rows, seen, _ = t.play("far", {k: (JUMP, 0.0, 0.0) for k in range(1, 12)}, 130)
    landing = next((i + 1 for i, s in enumerate(seen) if s in (ST_LANDING, ST_LANDING_LIGHT)), None)
    if landing is None:
        t.fail("landing", f"no landing: {sorted(set(hex(s) for s in seen))}")
        return
    check_move(t, "landing", rows, start=landing)


def sc_airdodge(t):
    hop = {k: (JUMP, 0.0, 0.0) for k in range(1, 30)}
    _, seen, _ = t.play("far", hop, 30)
    takeoff = next((i + 1 for i, s in enumerate(seen) if s == ST_JUMP), None)
    if takeoff is None:
        t.fail("airdodge", "no jump")
        return
    frames = {k: (JUMP, 0.0, 0.0) for k in range(1, takeoff + 1)}
    frames.update(press(takeoff + 8, B_GUARD))
    rows, seen, _ = t.play("far", frames, 160)
    if ST_ESCAPE_AIR not in seen:
        t.fail("airdodge", f"no air dodge: {sorted(set(hex(s) for s in seen))}")
        return
    # The air dodge itself ends in the special fall (not knowable from its
    # scripts); the landing after it ends with its animation, which is.
    air = [x for x in rows if x["st"] == ST_ESCAPE_AIR and x["sure"] and x["in"] >= 0]
    if air:
        t.fail("airdodge", f"the air dodge was given a certain prediction: in={air[0]['in']} how={air[0]['how']}")
    check_move(t, "airdodge", rows)


def sc_shield(t):
    """Shield held 20 frames, then let go. The trackers count the whole of
    GuardOff as actionable (it is entered by choice), so the live reading
    here is the interrupt groups themselves: the attack group open, or the
    status left."""
    frames = {k: (B_GUARD, 0.0, 0.0) for k in range(1, 21)}
    rows, _, _ = t.play("far", frames, 70)
    check_move(t, "shield release", rows,
               live=lambda x: (x["st"] == ST_GUARD_OFF and bool(x["grp"] & GROUP_ATTACK))
               or x["st"] not in (ST_GUARD_ON, ST_GUARD, ST_GUARD_OFF))


def sc_hit(t, name, state, buttons, x=0.0, y=0.0, total=120):
    """A hit on the standing or shielding CPU: predicted against measured.
    On shield the predicted value is shown when the hit lands, so one that
    differs from the measured value fails. A hit that connects is predicted
    in the log only (the defender often lands before its hitstun ends), so a
    difference there is counted and reported, not failed. The attacker's own
    first actionable frame is checked from the last hit on."""
    rows, _, text = t.play(state, press(1, buttons, x * t.toward, y), total)
    kind = "shield" if state == "shield" else "hit"
    predicted = [g for g in PREDICTED.findall(text) if g[0] == "1" and g[2] == kind]
    checks = [g for g in CHECK.findall(text) if g[0] == "1" and g[2] == kind]
    hits = [i for i in range(1, len(rows)) if rows[i]["hl"] > rows[i - 1]["hl"]]
    if not hits:
        # Not every move reaches a standing fighter (Jigglypuff's up tilt
        # goes over Fox): nothing to compare, the move itself is checked.
        t.say(f"note {t.char} {name}: the attack did not connect (P1 was never in hitlag)")
        check_move(t, name + " attacker (whiffed)", rows)
        return
    for g in checks:
        t.stats[kind][0 if g[5] == "same" else (1 if g[5] == "DIFFERENT" else 2)] += 1
        if g[5] == "DIFFERENT":
            t.differences.append(f"{t.char} {name}: predicted {g[3]}, measured {g[4]}")
    osd = [m for m in OSD.findall(text) if m[0] == "1" and m[1] == OSD_ADVANTAGE]
    if not checks:
        # The measured value comes when the defender can act again, which
        # can be after the run (a knockdown), and is not passed on at all
        # past the display limit.
        t.say(f"note {t.char} {name}: no measured value within {total} frames (predicted lines: {predicted})")
    elif checks[-1][5] == "DIFFERENT":
        detail = (f"predicted {checks[-1][3]}, measured {checks[-1][4]} (attacker in "
                  f"{predicted[-1][5] if predicted else '?'}, defender in {predicted[-1][7] if predicted else '?'})")
        if kind == "shield":
            if not osd or not osd[-1][2].endswith(f"Actual: {int(checks[-1][4]):+d} f"):
                t.fail(name, detail + f"; and the last message is not the marked correction: {osd[-1:]}")
            else:
                t.fail(name, detail + "; the correction was posted, marked as measured")
        else:
            t.say(f"note {t.char} {name}: {detail}; on hit only the measured value is shown")
    elif checks[-1][5] == "same":
        how = HOW.get(int(predicted[-1][6])) if predicted else "?"
        if kind == "shield" and len(osd) != len(predicted):
            t.fail(name, f"{len(osd)} advantage messages for {len(predicted)} hits on shield whose prediction "
                         f"matched: {osd}")
        else:
            t.ok(name, f"predicted {checks[-1][3]} when the hit landed (attacker by {how}), measured the same"
                       + ("; shown once, at the hit" if kind == "shield" else "; shown at the measurement"))
    else:
        t.ok(name, f"no prediction {checks[-1][5]}; measured {checks[-1][4]} shown as before")
    check_move(t, name + " attacker", rows, start=hits[-1])


def sc_corrected(t):
    """A value shown at the hit that turns out wrong is always corrected: a
    jab on the shield of a CPU that grabs out of shield. The jab's advantage
    is shown when it lands; the CPU's grab then keeps P1 from acting for far
    longer, past the display limit of 50, and the measured value must still
    come as the marked correction."""
    name = "jab on shield, grabbed"
    rows, _, text = t.play("shield-grab", press(1, B_ATTACK), 230)
    predicted = [g for g in PREDICTED.findall(text) if g[0] == "1" and g[2] == "shield"]
    checks = [g for g in CHECK.findall(text) if g[0] == "1" and g[2] == "shield"]
    osd = [m for m in OSD.findall(text) if m[0] == "1" and m[1] == OSD_ADVANTAGE]
    if not predicted or not osd:
        t.fail(name, f"no prediction shown at the hit (predicted {predicted}, messages {osd})")
    elif not checks:
        t.fail(name, f"predicted {predicted[-1][3]} shown, but no measured value within 230 frames")
    elif checks[-1][5] != "DIFFERENT":
        t.say(f"note {t.char} {name}: predicted {checks[-1][3]}, measured {checks[-1][4]}: the grab did not come "
              f"in time, nothing to correct")
    elif len(osd) < 2 or not osd[-1][2].endswith(f"Actual: {int(checks[-1][4]):+d} f"):
        t.fail(name, f"predicted {checks[-1][3]}, measured {checks[-1][4]}, but the messages are {osd}")
    else:
        t.ok(name, f"predicted {checks[-1][3]} shown at the hit, measured {checks[-1][4]}"
                   f"{' (past the display limit)' if abs(int(checks[-1][4])) >= 50 else ''}: corrected with "
                   f"'{osd[-1][2]}'")


def sc_info(t):
    """The info display's Actionable In row during a jab: every number it
    shows is one the service logged in that run."""
    e, r = t.emu, t.r
    e.h.call("load_state", path=t.states["far"])
    e.h.call("pause")
    e.h.call("write_mem", addr=r.lab.settings + SET_HUMAN_ROWS, hex=bytes([INFO_ACT_IN] + [0] * 7).hex())
    e.h.call("frame_advance", n=2)
    t.save("info", os.path.dirname(t.states["far"]))
    rows, _, text = t.play("info", press(1, B_ATTACK), 40)
    shown = [m.group(1) for m in INFO.finditer(text)]
    numbers = sorted(set(int(s) for s in shown if s))
    logged = set(x["in"] for x in rows if x["sure"] and x["in"] >= 0)
    if not shown:
        t.fail("info", "no 'info P1: Actionable In' line was logged")
    elif len(numbers) < 5 or not set(numbers) <= logged:
        t.fail("info", f"the row showed {numbers}, the service logged {sorted(logged)}")
    else:
        t.ok("info", f"Actionable In counted {max(numbers)}..{min(numbers)} through the jab")


SCENARIOS = [
    ("jab", lambda t: sc_ground(t, "jab", B_ATTACK)),
    ("utilt", lambda t: sc_ground(t, "utilt", B_ATTACK, 0.0, 0.5)),
    # A smash goes through its start status first; the walk of that status
    # does not know the next one, so only the last status is checked.
    ("fsmash", lambda t: sc_ground(t, "fsmash", B_ATTACK | B_SMASH, 1.0, 0.0, total=110)),
    ("nair", lambda t: sc_aerial(t, "nair", 0.0, 0.0)),
    ("fair", lambda t: sc_aerial(t, "fair", 1.0, 0.0)),
    ("bair", lambda t: sc_aerial(t, "bair", -1.0, 0.0)),
    ("uair", lambda t: sc_aerial(t, "uair", 0.0, 1.0)),
    ("dair", lambda t: sc_aerial(t, "dair", 0.0, -1.0)),
    ("grab", lambda t: sc_ground(t, "grab", B_GRAB)),
    ("spotdodge", lambda t: sc_dodge(t, "spotdodge", 0.0, -1.0)),
    ("rollf", lambda t: sc_dodge(t, "roll forward", 1.0, 0.0)),
    ("rollb", lambda t: sc_dodge(t, "roll back", -1.0, 0.0)),
    ("airdodge", sc_airdodge),
    ("land", sc_land),
    ("shield", sc_shield),
    ("hit-jab", lambda t: sc_hit(t, "jab on hit", "near", B_ATTACK)),
    ("hit-utilt", lambda t: sc_hit(t, "utilt on hit", "near", B_ATTACK, 0.0, 0.5)),
    ("hit-fsmash", lambda t: sc_hit(t, "fsmash on hit", "near", B_ATTACK | B_SMASH, 1.0, 0.0, total=160)),
    ("shield-jab", lambda t: sc_hit(t, "jab on shield", "shield", B_ATTACK)),
    ("shield-utilt", lambda t: sc_hit(t, "utilt on shield", "shield", B_ATTACK, 0.0, 0.5)),
    ("shield-fsmash", lambda t: sc_hit(t, "fsmash on shield", "shield", B_ATTACK | B_SMASH, 1.0, 0.0)),
    ("corrected", sc_corrected),
    ("info", sc_info),
]


def walks_report(t, log):
    walks = WALK.findall(log)
    if walks:
        most = max(int(g[15]) for g in walks)
        t.say(f"walks: {len(walks)} in the run, at most {most} commands read by one")


def cmd_test_frames(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-frames")
    ap.add_argument("rel")
    ap.add_argument("--chars", nargs="*", default=None)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    label = "test-frames"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    out = []
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            t = Tester(emu, out, opts.keep_going)
            try:
                for n, char in enumerate(opts.chars or CHARS):
                    t.step = f"{char}: states"
                    t.start(char, n == 0, states)
                    for name, fn in SCENARIOS:
                        if not opts.only or name in opts.only:
                            t.step = f"{char}: {name}"
                            fn(t)
                walks_report(t, emu.log_text())
                for kind in ("shield", "hit"):
                    same, different, none = t.stats[kind]
                    t.say(f"advantage on {kind}: {same} predictions matched the measured value, {different} "
                          f"differed, {none} hits had no prediction")
                for line in t.differences:
                    t.say(f"  differed: {line}")
            except (Timeout, nav.NavError, HarnessError, FrameError, DO.DisplayError, LT.LabError, PI.InputError,
                    struct.error, KeyError, StopIteration, TypeError, IndexError) as e:
                why = f"{t.step}: {type(e).__name__}: {e}"
                try:
                    emu.h.call("resume")
                    t.shots.append(emu.screenshot("fail-frames"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{t.step if t else 'boot'}: {e}"
    if out:
        path = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "frames-test.txt")
        with open(path, "w") as f:
            f.write("\n".join(out) + "\n")
        print(f"written: {path}")
    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for line in (t.failures if t else []):
        print(f"FAIL {line}")
    for shot in (t.shots if t else []):
        print(f"screenshot: {shot[0]} ({shot[1]}x{shot[2]})")
    log = E.read_log(log_path) if log_path else ""
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and t and t.failures:
        why = f"{len(t.failures)} scenario(s) failed"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
