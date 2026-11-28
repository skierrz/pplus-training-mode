"""Emulator test of the fighter trackers and the frame advantage OSD messages (run
through emu.py; settings come from the environment the same way).

  test-displays REL [--only NAME ...]
      One boot, Mario (P1) against a Fox CPU on Final Destination, with
      ports 1 and 2 as standard controllers (Training's CPU option "Control"
      lets the harness drive the CPU through port 2). Each scenario starts
      from the same savestate and is stepped one game frame at a time while
      both fighters are read from game memory (status, hitstun, shield stun,
      hitlag), independently of the plugin:

        probe run  after the hit lands, the fighters hold shield (or jump):
                   the first frame each one's new action starts is the first
                   frame it could act. Expected advantage = defender's frame
                   minus attacker's frame;
        idle run   the same without the held buttons: the trackers must find
                   the same frames from the game's state alone.

      The plugin's "advantage:" log line of each run must match: the value,
      the attacker's and the defender's frame counted from the hit, on hit
      or on shield, and for multi-hit moves more than one hit; melee hits
      must be attributed to the fighter whose attack connected. The OSD
      message the plugin posts ("osd:" line) must carry the same value, and
      in the screenshot taken just after it (screenshot folder,
      displays-NAME.png) the message must be drawn in its colour inside
      the box above the attacker's damage readout. The idle run also checks
      the trackers' per-frame trace against memory (see check_trace).

The observers' bookkeeping is checked from the plugin's log: every match
ends with as many removals as registrations, none lost, and every
re-registration landed in the list slot the first one had.
"""
import argparse
import os
import re
import struct
import time

import emu as E
import lab_tests as LT
import loader_tests as L
import nav
from harness import HarnessError, Timeout

GUARD_ON, GUARD, GUARD_DAMAGE, JUMP_SQUAT, ATTACK_AIR, LANDING_AIR = 0x1A, 0x1B, 0x1D, 0xA, 0x33, 0x18
JUMP, JUMP_AERIAL = 0xB, 0xC
CPU_CONTROL, CPU_SHIELD = 2, 6
NEUTRAL = (128, 128)
RIGHT, LEFT, DOWN, UP = (228, 128), (28, 128), (128, 28), (128, 228)
# main::Settings (src/main/settings.h; checked at build time against the menu).
SET_HUMAN_ROWS, SET_CPU_ROWS = LT.SET_HMN_INFO + 2, LT.SET_CPU_INFO + 2
SET_ON_HIT, SET_ON_SHIELD, SET_TOLERANCE = LT.SET_ADVANTAGE_HIT, LT.SET_ADVANTAGE, LT.SET_TOLERANCE
# ui::InfoValue (src/main/ui/info_display.h).
INFO_ROWS = {"state": 2, "stateframe": 3, "subaction": 30, "actionable": 32, "hitlag": 15, "iasa": 31,
             "airborne": 33, "lasthit": 34, "hitstun": 16, "shield": 18}
# Lab menu rows (src/main/ui/pages/display_options.cpp, osd_menu.cpp).
MAIN_OSD, MAIN_DISPLAY = LT.MAIN_OSD, LT.MAIN_DISPLAY
OSD_POSITION, OSD_ADVANTAGE, OSD_ADVANTAGE_HIT = 1, 9, 10
ID_FRAME_ADVANTAGE, ID_FRAME_ADVANTAGE_HIT = 14, 19
SET_OSD_POSITION = LT.SET_OSD_POSITION
STOP_HITLAG = 0x10  # soStopModuleImpl: hitlag frames left
# soModuleEnumeration +0x14: the situation module; soSituationModuleImpl
# +0x2C: its kind (header layout); 2 = in the air.
SITUATION, SITUATION_KIND, AIR = 0x14, 0x2C, 2
SHOT_DELAY = 10      # frames from the OSD message to its screenshot

# OSD placement (src/main/ui/osd_place.h), in the 640x480 2D space: default
# Sides has each player's newest box centred at (SIDES_X[player], BOX_Y);
# 124 x 46, narrowed by 0.75 on 16:9. Message colours (src/main/ui/osd.h).
SIDES_X = [96.0, 245.0, 395.0, 544.0]
BOX_Y, BOX_W, BOX_H = 366.0, 96.0, 40.0
GREEN, RED, WHITE = (74, 222, 128), (255, 148, 138), (241, 245, 249)

ADV = re.compile(r"advantage: P(\d)(b?) -> P(\d)(b?) on (hit|shield) ([+-]\d+) \(hit (\d+), attacker (\d+) "
                 r"status (\w+) from (\w+), defender (\d+) status (\w+) from (\w+), hits (\d+), by (\w+)\)"
                 r"( not shown)?")
OSD = re.compile(r"osd: P(\d) id (\d+)(): ([^\r\n]*)")
TRACE = re.compile(r"trace f=(\d+) p=(\d)(b?) st=(\w+) pv=(\w+) ch=(\d+) sf=(-?\d+) hl=(\d+) hs=(\d+) "
                   r"ss=(\d+) air=(\d) act=(\d)")
SUMMARY = re.compile(r"tracker: (\d+) registrations, (\d+) removals, (\d+) lost, (\d+) moved, (\d+) frames")
OBSERVERS_ON = re.compile(r"tracker: P(\d)( partner)? entry (\w+) observers on: status unit (\d+) send (\d+)")


class DisplayError(RuntimeError):
    pass


class Runner:
    def __init__(self, emu, notes, shots):
        self.emu = emu
        self.lab = LT.Lab(emu, [])
        self.f = self.lab.f
        self.notes = notes
        self.shots = shots
        self.human = self.cpu = None
        self._watches = {}

    def note(self, text):
        self.notes.append(text)
        print("  " + text)

    # ---- memory -------------------------------------------------------------

    def read(self, f):
        s = self.f.state(f)
        mods = self.f.modules(f)
        stop = struct.unpack_from(">I", mods, 0x44)[0]
        s["hitlag"] = struct.unpack(">i", self.emu.read(stop + STOP_HITLAG, 4))[0]
        situation = struct.unpack_from(">I", mods, SITUATION)[0]
        s["air"] = self.emu.u32(situation + SITUATION_KIND) == AIR
        return s

    # ---- one run --------------------------------------------------------------

    def run(self, sc, probe, frames, phase=0):
        """Steps `frames` game frames from the scenario's state, playing its
        inputs; returns per-frame rows and the plugin log text it produced.
        A run without probes ends SHOT_DELAY frames after the plugin logs
        the measured advantage, so the caller can photograph its message."""
        emu = self.emu
        emu.h.call("load_state", path=sc["state"])
        emu.h.call("pause")
        # Runs without probes log every tracked fighter's frame (the load
        # put the byte back to 0).
        if not probe:
            m = re.search(r"tracker: trace byte at ([0-9a-f]{8})", emu.log_text())
            if not m:
                raise DisplayError("no trace byte line in the log")
            emu.h.call("write_mem", addr=int(m.group(1), 16), hex="01")
        # After a load the first step only completes the saved frame.
        emu.h.call("frame_advance", n=1)
        pos = len(emu.log_text())
        att, dfd = (self.human, self.cpu) if sc["attacker"] == 0 else (self.cpu, self.human)
        rows = []
        hit = posted = None
        ctx = {"probe": probe, "hit": None, "rows": rows}
        both = self._watches.setdefault(
            (att["fighter"], dfd["fighter"]),
            emu.watch(lambda a=att, d=dfd: (self.read(a), self.read(d))))
        try:
            a, d = both.read()
            for i in range(frames):
                rows.append((a, d))
                stunned = (d["hitlag"] > (rows[-2][1]["hitlag"] if len(rows) > 1 else 0)
                           and (d["status"] == GUARD_DAMAGE or (d["hitstun"] or 0) > 0))
                if stunned:
                    hit = i
                    ctx["hit"] = i
                    ctx["hits"] = ctx.get("hits", 0) + 1
                sc["inputs"](self, i, ctx)
                if probe and hit is not None and not ctx.get("probed") \
                        and ctx["hits"] >= sc.get("probe_hits", 1):
                    ctx["probed"] = True
                    for port, buttons in sc["probe"].items():
                        emu.hold(port, buttons)
                if probe and sc.get("tap") and ctx["hit"] is not None:
                    # A jump needs a fresh press: tap every other frame, in
                    # two runs a frame apart, so some press falls on each frame.
                    port, held, tapped = sc["tap"]
                    emu.hold(port, tapped if (i - ctx["hit"] + phase) % 2 == 0 else held)
                a, d = emu.step(both)
                if not probe and hit is not None:
                    # The measured advantage, not the first message: with the
                    # extra module a message is posted when the hit lands.
                    if posted is None and ADV.search(emu.log_text(), pos):
                        posted = i
                    if posted is not None and i - posted >= SHOT_DELAY:
                        break
        finally:
            emu.neutral(0)
            emu.neutral(1)
            emu.h.call("resume")
        return rows, emu.log_text()[pos:]

    @staticmethod
    def first(rows, start, test):
        for i in range(start, len(rows)):
            if test(rows[i]):
                return i
        return None

    def expected(self, sc, rows):
        """Hit frame (the last frame the defender's hitlag was set), and the
        first frames each side started its held action."""
        hits = [i for i in range(1, len(rows)) if rows[i][1]["hitlag"] > rows[i - 1][1]["hitlag"]
                and (rows[i][1]["status"] == GUARD_DAMAGE or (rows[i][1]["hitstun"] or 0) > 0)]
        if not hits:
            raise DisplayError(f"{sc['name']}: the attack never connected")
        h = hits[-1]
        a = self.first(rows, h, lambda r: r[0]["status"] in sc["attacker_acts"])
        d = self.first(rows, h + 1, lambda r: r[1]["status"] in sc["defender_acts"])
        if a is None or d is None:
            raise DisplayError(f"{sc['name']}: no action seen (attacker {a}, defender {d} after hit {h})")
        return {"hit": h, "a": a - h, "d": d - h, "value": d - a, "hits": len(hits),
                "shield": rows[h][1]["status"] == GUARD_DAMAGE}

    @staticmethod
    def logged(text, players):
        """The run's first advantage line between the scenario's players (a
        probe that attacks may start an exchange of its own after it)."""
        found = [g for g in ADV.findall(text) if (int(g[0]), int(g[2])) == players]
        if not found:
            raise DisplayError(f"no advantage line for P{players[0]} -> P{players[1]}: {ADV.findall(text)}")
        g = found[0]
        return {"value": int(g[5]), "a": int(g[7]) - int(g[6]), "d": int(g[10]) - int(g[6]),
                "shield": g[4] == "shield", "hits": int(g[13]), "attacker": int(g[0]), "defender": int(g[2]),
                "by": g[14], "shown": not g[15]}

    @staticmethod
    def message(text, player, expected_id):
        """The run's last OSD message for `player` with `expected_id`: (shown, lines).
        With the extra module the advantage is posted when each hit lands, read
        from the attacker's script (extra/frame_data.h), and again when it is
        measured only if the two differ: the last message is the one that must
        carry the measured value."""
        found = [m for m in OSD.finditer(text) if int(m.group(1)) == player and int(m.group(2)) == expected_id]
        if not found:
            raise DisplayError(f"no OSD message id {expected_id} for P{player}: {OSD.findall(text)}")
        return True, found[-1].group(4).split(" | ")

    def check_trace(self, sc, rows, text):
        """The trackers' per-frame values (trace lines) against the same
        frames read from game memory by the harness: status kind, the status
        before a change and the frame count in status at a change, hitlag,
        hitstun, shield stun, airborne. Rows and tracker frames are lined up
        on the last hit."""
        hits = [i for i in range(1, len(rows)) if rows[i][1]["hitlag"] > rows[i - 1][1]["hitlag"]]
        adv = [g for g in ADV.findall(text) if (int(g[0]), int(g[2])) == sc["players"]]
        if not hits or not adv:
            raise DisplayError(f"{sc['name']}: nothing to line the trace up on")
        h, H = hits[-1], int(adv[0][6])
        trace = {}
        for g in TRACE.findall(text):
            if not g[2]:
                trace[(int(g[0]), int(g[1]))] = [int(g[3], 16), int(g[4], 16)] + [int(x) for x in g[5:]]
        compared = 0
        for i in range(len(rows)):
            for side, player in ((0, sc["players"][0]), (1, sc["players"][1])):
                t = trace.get((H + i - h, player))
                if t is None:
                    continue
                st, pv, ch, sf, hl, hs, ss, air = t[:8]
                r = rows[i][side]
                want = {"st": r["status"], "hl": max(0, r["hitlag"]), "hs": max(0, r["hitstun"] or 0),
                        "ss": max(0, r["ra5"] or 0) if r["status"] == GUARD_DAMAGE else 0, "air": int(r["air"])}
                got = {"st": st, "hl": hl, "hs": hs, "ss": ss, "air": air}
                if i > 0 and rows[i - 1][side]["status"] != r["status"]:
                    want["sf"], got["sf"] = 1, sf
                    if ch == 1:
                        want["pv"], got["pv"] = rows[i - 1][side]["status"], pv
                if got != want:
                    raise DisplayError(f"{sc['name']}: P{player} at row {i} (tracker frame {H + i - h}): "
                                       f"tracker {got}, memory {want}")
                compared += 1
        if compared < len(rows):
            raise DisplayError(f"{sc['name']}: only {compared} fighter-frames of trace for {len(rows)} rows")
        return compared

    def check_shot(self, shot, player, value, shield):
        """The message's colour inside the newest box above `player`'s
        damage readout, and next to none of it beside the box at that height."""
        from PIL import Image
        path, w, h = shot
        im = Image.open(path).convert("RGB")
        sx, sy = w / 640.0, h / 480.0
        squeeze = 0.75 if w * 3 > h * 4 + 8 else 1.0
        # Sign-based frame advantage colour (src/main/ui/osd.h frameAdvantageColor):
        # positive green, zero white, negative red.
        colour = GREEN if value > 0 else RED if value < 0 else WHITE
        half = BOX_W * squeeze / 2
        margin = 32.0 if squeeze >= 1.0 else 12.0
        cx = (margin + half) if player == 1 else (640.0 - margin - half)

        def count(x0, x1):
            n = 0
            for y in range(int((BOX_Y - BOX_H / 2) * sy), int((BOX_Y + BOX_H / 2) * sy)):
                for x in range(max(0, int(x0 * sx)), min(w, int(x1 * sx))):
                    p = im.getpixel((x, y))
                    if sum(abs(a - b) for a, b in zip(p, colour)) < 40:
                        n += 1
            return n
        inside = count(cx - half + 4, cx + half - 4)
        beside = count(cx - 3 * half, cx - half - 4)
        if inside < 12 or beside > inside // 2:
            raise DisplayError(f"{os.path.basename(path)}: value colour {colour} in the box above P{player}: "
                               f"{inside} pixels, beside it {beside}")
        return inside

    def table(self, rows, upto):
        for i, (a, d) in enumerate(rows[:upto]):
            print(f"    {i:3d} A st={a['status']:02x} hl={a['hitlag']} y={a['y']:.1f} | "
                  f"D st={d['status']:02x} hl={d['hitlag']} hs={d['hitstun']} ss={d['ra5']}")

    def scenario(self, sc):
        name = sc["name"]
        frames = sc.get("frames", 100)
        rows, text = self.run(sc, True, frames)
        try:
            exp = self.expected(sc, rows)
        except DisplayError:
            self.table(rows, frames)
            raise
        probe_log = self.logged(text, sc["players"])
        if sc.get("tap"):
            rows2, text2 = self.run(sc, True, frames, phase=1)
            exp2 = self.expected(sc, rows2)
            other = self.logged(text2, sc["players"])
            self.note(f"{name}: jumps out of shield {exp['d']} and {exp2['d']} frames after the hit (two tap phases)")
            if (exp2["hit"], exp2["a"]) != (exp["hit"], exp["a"]) or other != probe_log:
                raise DisplayError(f"{name}: the two tap phases disagree ({exp} / {exp2}, {probe_log} / {other})")
            if exp2["d"] < exp["d"]:
                exp = exp2
        rows_idle, text_idle = self.run(sc, False, frames)
        idle_log = self.logged(text_idle, sc["players"])
        shot = self.emu.screenshot("displays-" + name)
        self.shots.append(shot)
        if abs(exp["value"]) >= 50:
            # ProjectPunch's display limit: logged, no message.
            if probe_log["shown"] or idle_log["shown"] or OSD.search(text_idle):
                raise DisplayError(f"{name}: {exp['value']:+d} is past the display limit but was shown")
            lines, shown = ["(none: past the display limit)"], False
            self.note(f"{name}: {exp['value']:+d} logged as not shown, no OSD message (display limit 50)")
        else:
            want_id = ID_FRAME_ADVANTAGE if exp["shield"] else ID_FRAME_ADVANTAGE_HIT
            shown, lines = self.message(text_idle, sc["players"][0], want_id)
            title, fmt = ("Shield Adv.", "{:+d}f") if exp["shield"] else ("Hit Adv.", "{:+d}f")
            # A value shown at the hit that the measurement corrects comes
            # back marked "Actual:".
            if lines not in ([title, fmt.format(exp["value"])], [title, "Actual: " + fmt.format(exp["value"])]):
                raise DisplayError(f"{name}: OSD message {lines}, wanted {[title, fmt.format(exp['value'])]}")
            if shown:
                pixels = self.check_shot(shot, sc["players"][0], exp["value"], exp["shield"])
                self.note(f"{name}: OSD {lines} drawn above P{sc['players'][0]}'s damage meter "
                          f"({pixels} value pixels)")
            else:
                self.note(f"{name}: OSD {lines} posted for P{sc['players'][0]}, not drawn (not a human player)")
        compared = self.check_trace(sc, rows_idle, text_idle)
        self.note(f"{name}: tracker values matched memory on {compared} fighter-frames")
        if (probe_log["by"], idle_log["by"]) != ("inflict", "inflict"):
            raise DisplayError(f"{name}: hit attributed by {probe_log['by']} / {idle_log['by']}, wanted inflict")
        want = (exp["value"], exp["a"], exp["d"], exp["shield"])
        got = [(g["value"], g["a"], g["d"], g["shield"]) for g in (probe_log, idle_log)]
        self.note(f"{name}: expected {'shield' if exp['shield'] else 'hit'} {exp['value']:+d} (attacker +{exp['a']}, "
                  f"defender +{exp['d']} from the hit, {exp['hits']} hit(s)); plugin probe {probe_log['value']:+d} "
                  f"(+{probe_log['a']}, +{probe_log['d']}), idle {idle_log['value']:+d} "
                  f"(+{idle_log['a']}, +{idle_log['d']}), hits {idle_log['hits']}")
        if got[0] != want or got[1] != want:
            self.table(rows, min(frames, exp["hit"] + exp["d"] + 3))
            raise DisplayError(f"{name}: plugin {got} != expected {want}")
        if sc.get("multi") and (exp["hits"] < 2 or idle_log["hits"] != exp["hits"]):
            raise DisplayError(f"{name}: wanted a multi-hit exchange, harness saw {exp['hits']} hits, plugin "
                               f"{idle_log['hits']}")
        others = [g for g in ADV.findall(text_idle) if (int(g[0]), int(g[2])) != sc["players"]]
        if others:
            raise DisplayError(f"{name}: idle run attributed another exchange: {others}")
        if sc.get("check"):
            sc["check"](self, rows, rows_idle, exp)
        exp["shown"] = idle_log["value"]
        exp["osd"] = " / ".join(lines) + ("" if shown else " (hidden)")
        return exp

    # ---- setup ----------------------------------------------------------------

    def menu_osds(self):
        """Pause, lab menu > OSDs: Shield Advantage and Hit Advantage on
        (tolerance left at its default, 1); Display > Lab Debug > Debug Overlay
        on; close and resume."""
        lab, emu = self.lab, self.emu
        lab.open_menu(keep=True)
        lab.menu_nav_to("OSDs > OSD Position")
        if lab.setting(SET_OSD_POSITION) != 1:
            raise DisplayError(f"OSD Position is {lab.setting(SET_OSD_POSITION)}, wanted the default Sides")
        self.shots.append(emu.screenshot("displays-menu-osds"))
        lab.menu_nav_to("OSDs > Individual OSDs > Shield Advantage")
        lab.toggle_on(SET_ON_SHIELD)
        lab.menu_select_row("Hit Advantage")
        lab.toggle_on(SET_ON_HIT)
        if lab.setting(SET_TOLERANCE) != 1:
            raise DisplayError(f"Advantage Tolerance is {lab.setting(SET_TOLERANCE)}, wanted the default 1")
        lab.menu_nav_to("Display > Lab Debug > Debug Overlay")
        lab.toggle_on(LT.SET_OVERLAY)
        lab.close_menu()

    def set_rows(self, offset, names):
        values = [INFO_ROWS[n] for n in names] + [0] * (8 - len(names))
        self.emu.h.call("write_mem", addr=self.lab.settings + offset, hex=bytes(values).hex())


# ---- scenario inputs ----------------------------------------------------------

def press_at(port, frame, buttons, stick=NEUTRAL, hold=2):
    def inputs(r, i, ctx):
        if i == frame:
            r.emu.hold(port, buttons, stick=stick)
        elif i == frame + hold:
            r.emu.neutral(port)
    return inputs


def mash_until_hits(port, frame, buttons, hits):
    """From `frame`, presses `buttons` two frames on, two off, until the
    defender has been hit `hits` times."""
    def inputs(r, i, ctx):
        if i < frame or ctx.get("hits", 0) >= hits:
            if i >= frame and not ctx.get("mash_done"):
                ctx["mash_done"] = True
                r.emu.neutral(port)
            return
        r.emu.hold(port, buttons if (i - frame) % 4 < 2 else [])
    return inputs


def with_shield(port, inner):
    """Keeps port's shield held (the defender's) besides `inner`."""
    def inputs(r, i, ctx):
        if i == 0:
            r.emu.hold(port, ["R"])
        inner(r, i, ctx)
    return inputs


def aerial(lcancel):
    """Short hop, neutral air on landing frame onwards; with `lcancel` R is
    pressed a few frames before the landing (P+'s L-cancel window)."""
    def inputs(r, i, ctx):
        rows = ctx["rows"]
        a = rows[-1][0]
        if i == 0:
            r.emu.hold(0, ["X"])
        elif i == 2:
            r.emu.neutral(0)
        if a["status"] == JUMP and not ctx.get("nair"):
            ctx["nair"] = True
            r.emu.hold(0, ["A"])
            ctx["nair_at"] = i
        elif ctx.get("nair") and i == ctx["nair_at"] + 2:
            r.emu.neutral(0)
        if lcancel and a["status"] == ATTACK_AIR and len(rows) > 1 and a["y"] < rows[-2][0]["y"] \
                and a["y"] < 4.0 and not ctx.get("lc"):
            ctx["lc"] = True
            r.emu.hold(0, ["R"])
            ctx["lc_at"] = i
        elif ctx.get("lc") and i == ctx["lc_at"] + 2 and not (ctx["probe"] and ctx.get("probed")):
            r.emu.neutral(0)
    return inputs


def landing_lag(rows):
    lag = [i for i, (a, _) in enumerate(rows) if a["status"] == LANDING_AIR]
    return len(lag)


def check_lcancel(r, rows, rows_idle, exp):
    r.lcancel_lag = landing_lag(rows_idle)
    r.note(f"landing lag (status 0x18) {r.lcancel_lag} frames with L-cancel")


def check_plain(r, rows, rows_idle, exp):
    r.plain_lag = landing_lag(rows_idle)
    r.note(f"landing lag (status 0x18) {r.plain_lag} frames without L-cancel")


SCENARIOS = [
    # name, state, attacker port index (0 = P1), inputs, held probe buttons per port,
    # statuses that mark each side's first action, (attacker, defender) players
    dict(name="jab-hit", state="control", attacker=0, inputs=press_at(0, 1, ["A"]),
         probe={0: ["R"], 1: ["R"]}, attacker_acts={GUARD_ON}, defender_acts={GUARD_ON}, players=(1, 2)),
    dict(name="jab-shield", state="control", attacker=0, inputs=with_shield(1, press_at(0, 12, ["A"])),
         probe={0: ["R"]}, attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2)),
    dict(name="jab-shield-jump-oos", state="control", attacker=0, inputs=with_shield(1, press_at(0, 12, ["A"])),
         probe={0: ["R"]}, tap=(1, ["R"], ["R", "X"]), attacker_acts={GUARD_ON}, defender_acts={JUMP_SQUAT},
         players=(1, 2)),
    dict(name="fsmash-shield-cpu-shield-option", state="shield", attacker=0,
         inputs=press_at(0, 1, ["A"], stick=RIGHT, hold=3), probe={0: ["R"]}, attacker_acts={GUARD_ON},
         defender_acts={GUARD}, players=(1, 2), frames=110),
    dict(name="nair-shield", state="control", attacker=0, inputs=with_shield(1, aerial(False)),
         probe={}, attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2), frames=110,
         check=check_plain),
    dict(name="nair-shield-lcancel", state="control", attacker=0, inputs=with_shield(1, aerial(True)),
         probe={}, attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2), frames=110,
         check=check_lcancel),
    dict(name="dsmash-shield", state="control", attacker=0,
         inputs=with_shield(1, press_at(0, 12, ["A"], stick=DOWN, hold=3)), probe={0: ["R"]},
         attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2), frames=120),
    # Jab, jab, jab on the shield: one exchange of several hits. The probe
    # shield waits for the last hit (the jabs need A).
    dict(name="jab-string-shield-multihit", state="control", attacker=0,
         inputs=with_shield(1, mash_until_hits(0, 12, ["A"], 3)), probe={0: ["R"]}, probe_hits=3,
         attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2), frames=120, multi=True),
    # Up special: several hits on the shield, then a special fall and its
    # landing: past ProjectPunch's display limit of 50 frames.
    dict(name="upb-shield-multihit", state="control", attacker=0,
         inputs=with_shield(1, press_at(0, 12, ["B"], stick=UP, hold=3)), probe={0: ["R"]},
         attacker_acts={GUARD_ON}, defender_acts={GUARD}, players=(1, 2), frames=170, multi=True),
    # The jab lifts Mario: his hitstun ends in the air, so his probe taps X
    # (a double jump, or a jump if he has landed), which cannot reach Fox
    # (an aerial could hit Fox's shield and delay Fox's own first action).
    dict(name="cpu-jab-on-human", state="control", attacker=1, inputs=press_at(1, 1, ["A"]),
         probe={1: ["R"]}, tap=(0, [], ["X"]), attacker_acts={GUARD_ON}, defender_acts={JUMP_AERIAL, JUMP_SQUAT},
         players=(2, 1)),
]


def with_aerial_probe(sc):
    """For aerials the attacker's shield is held from the landing on (held
    earlier, it would air-dodge or L-cancel)."""
    inner = sc["inputs"]

    def inputs(r, i, ctx):
        inner(r, i, ctx)
        a = ctx["rows"][-1][0]
        if ctx["probe"] and a["status"] == LANDING_AIR and not ctx.get("landprobe"):
            ctx["landprobe"] = True
            r.emu.hold(0, ["R"])
    sc["inputs"] = inputs


for _sc in SCENARIOS:
    if _sc["name"].startswith("nair"):
        with_aerial_probe(_sc)


def tracker_report(log):
    """Per match: registrations == removals, nothing lost, every
    re-registration in the first one's list slot."""
    sums = SUMMARY.findall(log)
    if not sums:
        return "no tracker summary in the log"
    for reg, rem, lost, moved, frames in sums:
        if reg != rem or lost != "0" or moved != "0" or reg == "0":
            return f"tracker summary: {reg} registrations, {rem} removals, {lost} lost, {moved} moved"
    return ""


def cmd_test_displays(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-displays")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    opts = ap.parse_args(rest)
    label = "test-displays"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, step, log_path, shots, notes = "", "boot", "", [], []
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    results = []
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS) as (proc, emu):
            log_path = emu.log_path
            r = Runner(emu, notes, shots)
            try:
                step = "into the match"
                nav.enter_training_match(emu, "mario", "fox")
                emu.advance(60)
                r.lab.find_settings()
                r.human, r.cpu = r.f.by_player(0), r.f.by_player(1)
                step = "OSD menu"
                r.menu_osds()
                r.set_rows(SET_HUMAN_ROWS, ["state", "stateframe", "subaction", "actionable", "iasa", "lasthit"])
                r.set_rows(SET_CPU_ROWS, ["state", "actionable", "hitlag", "hitstun", "shield", "airborne",
                                          "lasthit"])
                step = "CPU on Control"
                LT.set_cpu_behaviour(emu, CPU_CONTROL)
                LT.walk_to(emu, r.lab, r.human, r.cpu, close=9.0)
                emu.advance(30)
                paths = {"control": os.path.join(states, "displays-control.sav"),
                         "shield": os.path.join(states, "displays-shield.sav")}
                emu.h.call("save_state", path=paths["control"])
                step = "CPU on Shield"
                LT.set_cpu_behaviour(emu, CPU_SHIELD)
                emu.wait_for("the CPU to shield", lambda: r.f.state(r.cpu)["status"] == GUARD, 120)
                emu.advance(5)
                emu.h.call("save_state", path=paths["shield"])
                for sc in SCENARIOS:
                    if opts.only and sc["name"] not in opts.only:
                        continue
                    step = sc["name"]
                    sc = dict(sc, state=paths[sc["state"]])
                    results.append((sc["name"], r.scenario(sc)))
                if hasattr(r, "plain_lag") and hasattr(r, "lcancel_lag") and not r.lcancel_lag < r.plain_lag:
                    raise DisplayError(f"L-cancel did not shorten the landing ({r.plain_lag} -> {r.lcancel_lag})")
                if results:
                    vals = {n: e["value"] for n, e in results}
                    if not any(v < 0 for v in vals.values()):
                        raise DisplayError(f"no scenario where the defender acts first: {vals}")
                step = "finish"
                emu.h.call("load_state", path=paths["control"])
                emu.advance(10)
                nav.finish_training(emu)
                emu.advance(60)
            except (Timeout, nav.NavError, HarnessError, DisplayError, LT.LabError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    shots.append(emu.screenshot("fail-displays"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(rf"\[{L.tag()}\] (main|module|tracker)", line):
            print("  " + line)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why:
        why = tracker_report(log)
    if not why and not OBSERVERS_ON.search(log):
        why = "no observer registration line"
    print()
    print(f"{'scenario':<34} {'kind':<7} {'expected':>8} {'shown':>6}  osd")
    for name, e in results:
        print(f"{name:<34} {'shield' if e['shield'] else 'hit':<7} {e['value']:>+8d} {e['shown']:>+6d}  {e['osd']}")
    for path, w, h in shots:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
