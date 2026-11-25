"""Emulator test of the technique OSD messages, the Ledgedash HUD, the colour
overlays and the tech invisibility (run through emu.py; settings come from
the environment the same way).

  test-osds REL [--only NAME ...]
      One boot, Mario (P1) against a Fox CPU on Final Destination, Training's
      CPU option on "Control" so the CPU is driven like a player: through
      port 2 by the harness, or on exact frames by the plugin's script
      source (scripts/plugin_input.py) in the timing-sensitive scenarios.
      The OSD Menu rows, the Ledgedash HUD and HMN Color Overlays are turned
      on through the lab menu. Each scenario starts from the same savestate
      and is stepped one game frame at a time while both fighters are read
      from game memory, independently of the plugin (status, position,
      hitlag, hitstun, P+'s L-cancel counter, the hurtbox group's
      intangibility timer, the subaction and its frame, the colour-blend and
      visibility bytes, damage). The expected value of each message is worked
      out from those reads; the plugin's "osd:" log line must carry it, and a
      screenshot is taken once it is on screen (screenshot folder, osds-*.png).
      The osd-position-* scenarios set OSD Position through the lab menu and
      find the wavedash message at that position's spot on the screenshot
      (Sides by its shift from HUD, the two spots being close on 16:9).
      See SCENARIOS for what each one checks.
"""
import argparse
import math
import os
import re
import struct
import time

import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import plugin_input as PI
from harness import HarnessError, Timeout
from plugin_input import B_ATTACK, B_FLICK, B_GRAB, B_GUARD, B_JUMP, B_SMASH, B_SPECIAL, frames_to_steps

# Statuses.
WAIT, DASH, JUMP_SQUAT, JUMP, JUMP_AERIAL, FALL = 0x0, 0x3, 0xA, 0xB, 0xC, 0xE
LANDING_AIR, LANDING_SPECIAL, GUARD, GUARD_DAMAGE = 0x18, 0x19, 0x1B, 0x1D
ESCAPE_AIR, ATTACK, ATTACK_AIR, CATCH = 0x21, 0x24, 0x33, 0x34
GUARD_OFF, ATTACK_HI4_START = 0x1C, 0x30
WALK, TURN, TURN_DASH, GUARD_ON, ATTACK_DASH, CATCH_DASH = 0x1, 0x6, 0x7, 0x1A, 0x26, 0x36
CAPTURE_PULLED, CAPTURE_DAMAGE, CAPTURE_CUT, KNOCKDOWN, PASSIVE, SPECIAL_N = 0x3D, 0x3F, 0x40, 0x4A, 0x60, 0x112
CLIFF_CATCH, CLIFF_WAIT = 0x74, 0x75
FREE = {WAIT, 0x1, 0x2, DASH, 0x4, 0x6, 0x7, 0x8, 0x11, 0x12, FALL, 0xF, JUMP_SQUAT, 0x1A, GUARD, 0x1C}
TECH_MOTIONS = set(range(0xAF, 0xB3)) | set(range(0xB8, 0xBC)) | set(range(0xBE, 0xC1)) | set(range(0xE6, 0xEA))
CPU_CONTROL = 2
NEUTRAL, RIGHT, LEFT, DOWN, UP = (128, 128), (228, 128), (28, 128), (128, 28), (128, 228)

# main::Settings (src/main/settings.h; offsets checked at build time).
S_LEDGE_HUD, S_LEDGE_MAX, S_LEDGE_GALINT, S_LEDGE_PANEL = 12, 13, 14, 15
S_OSD = {"wavedash": 92, "lcancel": 93, "actoos": 94, "actoolag": 95, "jumpcancel": 96, "fastfall": 97,
         "combo": 98, "sdi": 99, "ledgedash": 100, "actoohitstun": 101, "actooairborne": 102, "dashback": 103,
         "powershield": 104, "lockout": 105, "boostgrab": 106, "grabbreakout": 107, "itemthrow": 108}
S_PLAYER_PERCENT, S_PLAYER_LOCK = 8, 10
S_CPU_PERCENT, S_CPU_LOCK = 120, 122  # CPU Options > CPU Percent and its lock
S_OVERLAY = (248, 268)  # preset, then 17 groups
S_TECH_INVIS, S_TECH_DELAY = 288, 289
FOX_DISTINGUISHABLE = 4  # src/main/game/tech_frames.cpp
GROUP_WAIT, GROUP_DASH, GROUP_RUN, GROUP_IASA = 8, 10, 11, 15
COLOR_NONE, COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, COLOR_ORANGE, COLOR_INVISIBLE = 0, 1, 2, 3, 7, 8, 11

# Lab menu rows (src/main/ui/pages/display_options.cpp, osd_menu.cpp).
DISPLAY_HMN_OVERLAYS, DISPLAY_LEDGE_HUD, DISPLAY_THEME = 12, 16, 17
OSD_ROWS = {"wavedash": 1, "lcancel": 2, "actoos": 3, "dashback": 4, "powershield": 5, "sdi": 6, "lockout": 7,
            "itemthrow": 8, "boostgrab": 9, "actoolag": 10, "actooairborne": 11, "jumpcancel": 12, "fastfall": 13,
            "combo": 15, "grabbreakout": 16, "ledgedash": 17, "actoohitstun": 18}
# Rows not built yet: the cursor skips them.
OSD_UNBUILT = ()

# Sequential osd::Id values (src/main/ui/osd.h).
ID_WAVEDASH, ID_LCANCEL, ID_ACTOOS, ID_DASHBACK, ID_POWERSHIELD, ID_SDI = 0, 1, 2, 3, 4, 5
ID_LOCKOUT, ID_ITEM_THROW, ID_BOOST_GRAB, ID_MISC, ID_ACT_OOLAG, ID_ACT_OOAIRBORNE = 6, 7, 8, 9, 10, 11
ID_JUMP_CANCEL, ID_FASTFALL, ID_FRAME_ADV, ID_COMBO, ID_GRAB_BREAKOUT = 12, 13, 14, 15, 16
ID_CLIFFWAIT, ID_ACT_OOHITSTUN, ID_FRAME_ADV_HIT, ID_GALINT = 17, 18, 19, 20

OSD = re.compile(r"osd: P(\d) id (\d+): ([^\r\n]*)")
LEDGE = re.compile(r"ledgedash: P(\d) galint (-?\d+) ledge (\d+) angle (-?[\d.]+)")
GREEN, RED, WHITE, YELLOW = (74, 222, 128), (255, 148, 138), (241, 245, 249), (250, 204, 21)
SHOT_DELAY = 10
# No speed limit (emu.speed_args): every wait here is counted in game frames.
# A message is photographed SHOT_DELAY frames after it was posted plus the
# frames the screenshot request lets pass (about a dozen at this speed), and
# it stays on screen for 150.
SPEED = "0"


# OSD Menu > OSD Position (settings +88, osd::Position) and the newest
# box's centre for each, 640x480 space (src/main/ui/osd_place.h): HUD above
# the damage readout; Sides and the top rows at the configured screen-space spots.
S_OSD_POSITION = 88
S_OSD_BG_COLOR, S_OSD_BG_ALPHA = 110, 111
POS_HUD, POS_SIDES, POS_TOP_LEFT, POS_TOP_RIGHT = 0, 1, 2, 3
SIDES_X, SIDES_Y = (96.0, 245.0, 395.0, 544.0), 360.0
# TopLeft / TopRight step below timerRoom (y1=65) + kStepGap (6) = 71 -> centre 94, pitch 56.
TOP_Y = (94.0, 150.0, 206.0, 262.0)


def squeeze_of(shot):
    """The 2D pass's x scale: boxes are narrowed on 16:9."""
    _, w, h = shot
    return 0.75 if w * 3 > h * 4 + 8 else 1.0


def osd_spot(position, player, squeeze):
    half_w = 124.0 * squeeze / 2
    if position == POS_SIDES:
        x, y = SIDES_X[player - 1], SIDES_Y
    elif position == POS_TOP_LEFT:
        margin = 32.0 if squeeze >= 1.0 else 12.0
        x, y = margin + half_w, TOP_Y[player - 1]
    elif position == POS_TOP_RIGHT:
        margin = 32.0 if squeeze >= 1.0 else 12.0
        x, y = 640.0 - margin - half_w, TOP_Y[player - 1]
    else:
        hud_right = [144.0, 290.0, 436.0, 575.0] if squeeze < 1.0 else [155.0, 301.0, 448.0, 595.0]
        x, y = hud_right[player - 1] - half_w, 360.0
    if squeeze >= 1.0:
        x = max(32.0 + half_w, min(640.0 - 32.0 - half_w, x))
    return x, y


class OsdError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


class Runner:
    def __init__(self, emu, notes, shots):
        self.emu = emu
        self.lab = LT.Lab(emu, [])
        self.f = self.lab.f
        self.notes = notes
        self.shots = shots
        self.results = []
        self.ft = {}
        self.inp = None
        self.trace = None
        self.cache = {}
        self.position_shots = {}
        # Both fighters, their reads sent to the harness as one batch.
        self.both = emu.watch(lambda *_: {"m": self.read("m"), "c": self.read("c")})

    def note(self, text):
        self.notes.append(text)
        print("  " + text)

    # ---- memory (independent of the plugin) --------------------------------

    def read(self, which):
        e = self.emu
        ft = self.ft[which]
        mods = self.f.modules(ft)
        posture, motion, work, status = u32(mods, 0xC), u32(mods, 0x8), u32(mods, 0x64), u32(mods, 0x70)
        p = e.read(posture, 0x44)
        r = {"x": struct.unpack_from(">f", p, 0xC)[0], "y": struct.unpack_from(">f", p, 0x10)[0],
             "lr": struct.unpack_from(">f", p, 0x40)[0], "status": e.u32(status + 0x34)}
        mot = e.read(motion, 0x5C)
        r["mframe"] = struct.unpack_from(">f", mot, 0x40)[0]
        r["mkind"] = struct.unpack_from(">i", mot, 0x58)[0]
        la = e.ptr(work + 0x20)
        la_ints = e.read(e.u32(la + 0xC), 0x170)
        r["hitstun"] = struct.unpack_from(">i", la_ints, 0x38 * 4)[0]
        r["lc"] = struct.unpack_from(">i", la_ints, 90 * 4)[0]
        ra = e.ptr(work + 0x24)
        r["raf"] = e.read(e.u32(ra + 0x1C), 8).hex()
        r["hitlag"] = struct.unpack(">i", e.read(u32(mods, 0x44) + 0x10, 4))[0]
        r["air"] = e.u32(u32(mods, 0x14) + 0x2C) == 2
        group = e.read(e.u32(u32(mods, 0x20) + 0x58) + 0xC, 0x28)
        r["gx"] = struct.unpack_from(">f", group, 0x8)[0]
        r["xlu"] = struct.unpack_from(">i", group, 0x24)[0]
        r["vis"] = e.read(u32(mods, 0x58) + 0xE, 1)[0]
        sh = e.read(u32(mods, 0xA4) + 0x44, 1)[0]
        r["shadow"] = (sh >> 6) & 1       # soShadowModuleImpl activeStatus (bit 6, 0x40)
        r["shadow_proj"] = (sh >> 1) & 1  # soShadowModuleImpl isDispProjection (bit 1, 0x02)
        cur = e.read(ft["fighter"] + 0x138, 1)[0]
        r["cursor"] = (cur >> 2) & 1      # Fighter::setCursor (bit 2, 0x04)
        r["name_cursor"] = (cur >> 1) & 1 # Fighter::setNameCursor (bit 1, 0x02)
        col = e.read(u32(mods, 0xAC) + 0x14A, 6)
        r["sub"] = col[:4].hex()
        r["subon"] = col[5]
        r["damage"] = self.f.damage(ft)
        # P+'s powershield sound bit (LA bit 96: instance work flag word 3,
        # bit 0) and the grab timer (the float at +0xC of what the
        # controller module's +0x10 points to; observed counting down while
        # grabbed, see src/main/game/body.cpp).
        r["psbit"] = e.u32(e.u32(la + 0x1C) + 12) & 1
        clatter = e.u32(u32(mods, 0x5C) + 0x10)
        r["clatter"] = e.f32(clatter + 0xC) if e.is_ram(clatter) else None
        return r

    # ---- running a scenario ------------------------------------------------

    def run(self, state, frames, inputs, settings=(), shot=None, osd_player=None, stop_after_osd=True,
            keep_overlays=False):
        """Steps `frames` frames from `state` playing inputs(i, rows, ctx);
        returns rows ({"m": Mario, "c": CPU} per frame, read before the frame
        is stepped) and the plugin log it produced. With `shot`, a screenshot
        is taken after the run, which stops SHOT_DELAY frames after the
        first OSD message for `osd_player` (1-based) unless
        `stop_after_osd` is false."""
        emu = self.emu
        emu.h.call("load_state", path=state)
        emu.h.call("pause")
        if not keep_overlays:
            # The saved state has HMN overlays on (the ProjectPunch preset).
            settings = [(S_OVERLAY[0] + 1 + g, "00") for g in range(17)] + list(settings)
        for offset, value in settings:
            emu.h.call("write_mem", addr=self.lab.settings + offset, hex=value)
        emu.h.call("frame_advance", n=1)
        pos = len(emu.log_text())
        rows, ctx = [], {}
        posted = None
        try:
            row = self.both.read()
            for i in range(frames):
                rows.append(row)
                inputs(self, i, rows, ctx)
                if ctx.get("stop"):
                    break
                # The frame, and with it the next row's reads (not used if
                # the run ends here).
                row = emu.step(self.both)
                if shot and posted is None:
                    m = [g for g in OSD.findall(emu.log_text()[pos:]) if osd_player is None
                         or int(g[0]) == osd_player]
                    if m:
                        posted = i
                if shot and stop_after_osd and posted is not None and i - posted >= SHOT_DELAY:
                    break
            # Taken once the run is over: resuming for it lets frames pass
            # unread.
            if shot and posted is not None:
                emu.h.call("resume")
                self.shots.append(emu.screenshot(shot))
                emu.h.call("pause")
        finally:
            emu.neutral(0)
            emu.neutral(1)
            emu.h.call("resume")
        return rows, emu.log_text()[pos:]

    def play(self, state, scripts, extra=20, settings=(), shot=None, osd_player=None, stop_after_osd=True,
             keep_overlays=False):
        """As run(), with every input played by the plugin on exact frames
        (scripts/plugin_input.py): scripts = {player (0-based): ({driven
        frame: (buttons, x, y)}, length)}. rows[n] holds both fighters after
        driven frame n (rows[0]: before the first). The plugin's trace is on
        meanwhile (its status changes go to the notes)."""
        emu = self.emu
        emu.h.call("load_state", path=state)
        emu.h.call("pause")
        if not keep_overlays:
            settings = [(S_OVERLAY[0] + 1 + g, "00") for g in range(17)] + list(settings)
        for offset, value in settings:
            emu.h.call("write_mem", addr=self.lab.settings + offset, hex=value)
        emu.h.call("write_mem", addr=self.trace, hex="01")
        emu.h.call("frame_advance", n=1)
        pos = len(emu.log_text())
        posted = [None]

        def until(i, rows):
            if shot and posted[0] is None and any(osd_player is None or int(g[0]) == osd_player
                                                  for g in OSD.findall(emu.log_text()[pos:])):
                posted[0] = i
            return bool(shot and stop_after_osd and posted[0] is not None and i - posted[0] >= SHOT_DELAY)
        steps = {p: frames_to_steps(fr, n) for p, (fr, n) in scripts.items()}
        try:
            rows = [self.both.read()]
            rows += self.inp.run(steps, read=self.both.read, extra=extra, until=until)
            if shot and posted[0] is not None:
                emu.h.call("resume")
                self.shots.append(emu.screenshot(shot))
                emu.h.call("pause")
            # A frame's trace is written when the next one starts.
            emu.h.call("frame_advance", n=1)
        finally:
            emu.h.call("write_mem", addr=self.trace, hex="00")
            emu.h.call("resume")
        return rows, emu.log_text()[pos:]

    @staticmethod
    def messages(text, player, wid):
        return [g[2].split(" | ") for g in OSD.findall(text) if int(g[0]) == player and int(g[1]) == wid]

    def expect_osd(self, name, text, player, wid, want):
        got = self.messages(text, player, wid)
        if not got:
            raise OsdError(f"{name}: no OSD id {wid} for P{player}; log has {OSD.findall(text)}")
        if got[0] != want:
            raise OsdError(f"{name}: OSD {got[0]}, expected {want}")
        self.results.append((name, " / ".join(want), " / ".join(got[0])))
        self.note(f"{name}: expected {want}, shown {got[0]}")
        return got[0]

    @staticmethod
    def colour_in_box(shot, cx, cy, colour):
        """Pixels of `colour` inside a message box centred on (cx, cy),
        640x480 space."""
        from PIL import Image
        path, w, h = shot
        im = Image.open(path).convert("RGB")
        sx, sy = w / 640.0, h / 480.0
        half = 124.0 * squeeze_of(shot) / 2
        n = 0
        for y in range(int((cy - 23) * sy), int((cy + 23) * sy)):
            for x in range(int((cx - half + 4) * sx), int((cx + half - 4) * sx)):
                if sum(abs(a - b) for a, b in zip(im.getpixel((x, y)), colour)) < 40:
                    n += 1
        return n

    def check_color(self, shot, player, colour, name, position=POS_SIDES):
        """The message's colour inside `player`'s newest box at OSD Position
        `position`; for the top rows, also absent from the HUD spot."""
        cx, cy = osd_spot(position, player, squeeze_of(shot))
        n = self.colour_in_box(shot, cx, cy, colour)
        if n < 12:
            raise OsdError(f"{name}: colour {colour} not found in P{player}'s box at ({cx:.1f}, {cy:.1f}) "
                           f"({n} pixels)")
        if position in (POS_TOP_LEFT, POS_TOP_RIGHT):
            hx, hy = osd_spot(POS_HUD, player, squeeze_of(shot))
            k = self.colour_in_box(shot, hx, hy, colour)
            if k >= 12:
                raise OsdError(f"{name}: colour {colour} also above P{player}'s damage ({k} pixels)")
            self.note(f"{name}: {n} pixels of {colour} in the box at ({cx:.1f}, {cy:.1f}), {k} at the HUD spot")
        return n


# ---- scenarios -----------------------------------------------------------------

def first(rows, start, test):
    for i in range(start, len(rows)):
        if test(rows[i]):
            return i
    return None


def entered(rows, side, status, start=1):
    """The first row from `start` on where `side` has just entered `status`."""
    return next((i for i in range(max(start, 1), len(rows)) if rows[i][side]["status"] == status
                 and rows[i - 1][side]["status"] != status), None)


def sc_wavedash(r, delay, name):
    """Short hop (X for 2 frames), then R with the stick down-right `delay`
    frames after the first airborne frame."""
    stick = (200, 60)

    def inputs(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"])
        elif i == 2:
            r.emu.neutral(0)
        if s["status"] == JUMP and "air" not in ctx:
            ctx["air"] = i
        if "air" in ctx and i == ctx["air"] + delay:
            r.emu.hold(0, ["R"], stick=stick)
        elif "air" in ctx and i == ctx["air"] + delay + 3:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], 60, inputs, shot="osds-" + name, osd_player=1)
    jump = entered(rows, "m", JUMP)
    dodge = entered(rows, "m", ESCAPE_AIR)
    land = entered(rows, "m", LANDING_SPECIAL)
    if None in (jump, dodge, land) or not jump < dodge < land:
        raise OsdError(f"{name}: no wavedash in memory (jump {jump}, airdodge {dodge}, landing {land})")
    frame = dodge - jump + 1
    angle = math.degrees(math.atan2(abs(stick[1] - 128), abs(stick[0] - 128)))
    got = r.messages(text, 1, 0)
    if not got:
        raise OsdError(f"{name}: no wavedash message: {OSD.findall(text)}")
    shown = float(got[0][1].split(": ")[1])
    if abs(shown - angle) > 2.0:
        raise OsdError(f"{name}: angle {shown}, stick gives {angle:.1f}")
    want = [f"Wavedash: f{frame}", got[0][1], "SH (2f)"]
    r.expect_osd(name, text, 1, ID_WAVEDASH, want)
    r.note(f"{name}: angle {shown} (stick {angle:.1f})")
    r.check_color(r.shots[-1], 1, GREEN if frame == 1 else WHITE, name)
    return frame


def sc_lcancel(r, press, name):
    """Short-hop neutral air; `press`: None (no press), or the height while
    falling below which R is pressed for 2 frames."""
    def inputs(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"])
        elif i == 2:
            r.emu.neutral(0)
        if s["status"] == JUMP and "nair" not in ctx:
            ctx["nair"] = i
            r.emu.hold(0, ["A"])
        elif "nair" in ctx and i == ctx["nair"] + 2:
            r.emu.neutral(0)
        if press is not None and s["status"] == ATTACK_AIR and len(rows) > 1 and s["y"] < rows[-2]["m"]["y"] \
                and s["y"] < press and "lc" not in ctx:
            ctx["lc"] = i
            r.emu.hold(0, ["R"])
        elif "lc" in ctx and i == ctx["lc"] + 2:
            r.emu.neutral(0)
        # A jab once the landing lag is over (Act OoLag).
        if "land" not in ctx and s["status"] == LANDING_AIR:
            ctx["land"] = i
        if "land" in ctx and s["status"] == WAIT and "jab" not in ctx:
            ctx["jab"] = i + 2
        if "jab" in ctx and i == ctx["jab"]:
            r.emu.hold(0, ["A"])
        elif "jab" in ctx and i == ctx["jab"] + 2:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], 90, inputs, shot="osds-" + name, osd_player=1, stop_after_osd=False)
    land = entered(rows, "m", LANDING_AIR)
    if land is None:
        raise OsdError(f"{name}: no aerial landing")
    lc = rows[land]["m"]["lc"]
    lag = sum(1 for k in range(land, len(rows)) if rows[k]["m"]["status"] == LANDING_AIR)
    rises = [k for k in range(1, land + 1) if rows[k]["m"]["lc"] > rows[k - 1]["m"]["lc"]
             and rows[k]["m"]["status"] in (ATTACK_AIR, LANDING_AIR)]
    ok = lc > 0
    if rises:
        line2 = f"Frame {land - rises[-1] + 1}/7"
    else:
        line2 = "Missed"
    want = [f"L-Cancel ({100 if ok else 0}%)", line2]
    r.expect_osd(name, text, 1, ID_LCANCEL, want)
    r.note(f"{name}: P+ counter {lc} on landing, landing lag {lag} frames")
    # Act OoLag: frame 1 is the first frame a jump can come out, the same
    # number of frames after the landing as in the scripted short-hop
    # neutral air with the same L-cancel result (probes).
    jab = entered(rows, "m", ATTACK, land)
    if jab is not None:
        base, probe_land = nair_probe(r)
        fr = dict(base)
        if ok:
            fr[probe_land] = (B_GUARD, 0.0, 0.0)
        e = land + earliest_jump(r, fr, probe_land, ("lag", "nair-lc" if ok else "nair")) - probe_land
        n = jab - e + 1
        if n <= ACT_OOLAG_SHOWN:
            r.expect_osd(name + "-actoolag", text, 1, ID_ACT_OOLAG, ["Post-Lag", f"Frame {n}"])
        else:
            no_osd(r, name + "-actoolag", text, 1, ID_ACT_OOLAG, f"a jab on frame {n} after the landing")
    return ok, lag


def sc_lcancel_hitlag(r, name):
    """Late falling short-hop neutral air into Fox's held shield; R is pressed
    into hitlag so the L-cancel succeeds in P+. P+'s L-cancel counter
    (LA-Basic[90]) pauses during hitlag (L-Cancel.asm:123-154), so the OSD
    must report the P+ window frame (8 - counter, within 1..7), not raw game
    frames including hitlag."""
    def aerial_on_shield(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(1, ["R"])
            r.emu.hold(0, ["X"])
        elif i == 2:
            r.emu.neutral(0)
        if s["status"] == JUMP and "jump" not in ctx:
            ctx["jump"] = i
        if "jump" in ctx and i == ctx["jump"] + 22 and "nair" not in ctx:
            ctx["nair"] = i
            r.emu.hold(0, ["A"])
        elif "nair" in ctx and i == ctx["nair"] + 2:
            r.emu.neutral(0)
    probe, _ = r.run(r.states["control"], 80, aerial_on_shield)
    hit = first(probe, 1, lambda x: x["m"]["status"] == ATTACK_AIR and x["m"]["hitlag"] > 0)
    land_probe = entered(probe, "m", LANDING_AIR)
    if hit is None or land_probe is None:
        raise OsdError(f"{name}: probe did not hit shield and land (hit={hit}, land={land_probe})")
    press_at = hit - 1

    def inputs(r, i, rows, ctx):
        aerial_on_shield(r, i, rows, ctx)
        if i == press_at:
            r.emu.hold(0, ["R"])
        elif i == press_at + 2:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], land_probe + 20, inputs, shot="osds-" + name, osd_player=1,
                       stop_after_osd=False)
    land = entered(rows, "m", LANDING_AIR)
    if land is None:
        raise OsdError(f"{name}: no aerial landing")
    press = next((k for k in range(1, land + 1) if rows[k]["m"]["status"] in (ATTACK_AIR, LANDING_AIR)
                  and rows[k]["m"]["lc"] >= 7), None)
    if press is None:
        raise OsdError(f"{name}: no L-cancel press recorded in memory")
    hitlag_frames = sum(1 for k in range(press + 1, land) if rows[k]["m"]["hitlag"] > 0 and rows[k - 1]["m"]["hitlag"] > 0)
    lc = rows[land]["m"]["lc"]
    raw_frames = land - press + 1
    win_frame = max(1, 8 - lc)
    r.note(f"{name}: hit {hit}, press at row {press} (lc={rows[press]['m']['lc']}), land at {land} (lc={lc}), "
           f"hitlag frames {hitlag_frames}, raw {raw_frames}, window {win_frame}")
    if hitlag_frames < 2 or lc <= 0 or raw_frames <= 7:
        raise OsdError(f"{name}: hitlag_frames={hitlag_frames}, lc={lc}, raw={raw_frames}, win={win_frame}")
    want = ["L-Cancel (100%)", f"Frame {win_frame}/7"]
    r.expect_osd(name, text, 1, ID_LCANCEL, want)
    r.check_color(r.shots[-1], 1, GREEN, name)
    return win_frame


def sc_lcancel_double_press(r, name):
    """Short-hop neutral air (whiff, jumping away from Fox); L pressed 7 frames
    before landing (too early on its own) and R pressed on the very next frame
    (6 frames before landing, resetting LA-Basic[90] from 7 to 7). P+ succeeds
    on window frame 7/7; the OSD must not miss the back-to-back trigger press."""
    def whiff(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"], stick=LEFT)
        elif i == 2:
            r.emu.hold(0, stick=LEFT)
        if s["status"] == JUMP and "nair" not in ctx:
            ctx["nair"] = i
            r.emu.hold(0, ["A"])
        elif "nair" in ctx and i == ctx["nair"] + 2:
            r.emu.neutral(0)
    probe_rows, _ = r.run(r.states["control"], 70, whiff)
    land = entered(probe_rows, "m", LANDING_AIR)
    if land is None:
        raise OsdError(f"{name}: probe neutral air never landed")

    def inputs(r, i, rows, ctx):
        whiff(r, i, rows, ctx)
        if i == land - 9:
            r.emu.hold(0, ["L"], l=200)
        elif i == land - 8:
            r.emu.hold(0, ["L", "R"], l=200, r=200)
        elif i == land - 7:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], land + 15, inputs, shot="osds-" + name, osd_player=1, stop_after_osd=False)
    got_land = entered(rows, "m", LANDING_AIR)
    if got_land != land:
        raise OsdError(f"{name}: landed on {got_land}, probe on {land}")
    lc_seq = [rows[k]["m"]["lc"] for k in range(land - 8, land + 1)]
    lc = rows[land]["m"]["lc"]
    r.note(f"{name}: land {land}, LA-Basic[90] from land-8..land: {lc_seq}")
    if lc != 1 or rows[land - 7]["m"]["lc"] != 7 or rows[land - 6]["m"]["lc"] != 7:
        raise OsdError(f"{name}: unexpected LA-Basic[90] sequence {lc_seq} (expected 7, 7, ..., 1)")
    want = ["L-Cancel (100%)", "Frame 7/7"]
    r.expect_osd(name, text, 1, ID_LCANCEL, want)
    r.check_color(r.shots[-1], 1, GREEN, name)
    return lc


def sc_lcancel_very_early(r, name):
    """Fox (P2) full-hops + double-jumps in place and starts forward air
    (which keeps landing lag past frame 45), pressing R right after the aerial
    starts so landing arrives > 30 window frames later (techniques.cpp
    kLCancelShown = 30 -> Missed)."""
    def inputs(r, i, rows, ctx):
        c = rows[-1]["c"]
        if i == 0:
            r.emu.hold(1, ["X"])
        elif i == 8:
            r.emu.neutral(1)
        elif i == 10:
            r.emu.hold(1, ["X"])
        elif i == 12:
            # Fox faces left; stick LEFT + A starts forward air.
            r.emu.hold(1, ["A"], stick=LEFT)
        elif i == 14:
            r.emu.neutral(1)
        elif i == 42:
            r.emu.hold(1, stick=DOWN)
        elif i == 45:
            r.emu.neutral(1)
        if c["status"] == ATTACK_AIR and "lc" not in ctx:
            ctx["lc"] = i
            r.emu.hold(1, ["R"])
        elif "lc" in ctx and i == ctx["lc"] + 2:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], 90, inputs, shot="osds-" + name, osd_player=2, stop_after_osd=False)
    land = entered(rows, "c", LANDING_AIR)
    if land is None:
        raise OsdError(f"{name}: no aerial landing (statuses {[hex(x['c']['status']) for x in rows[10:85]]})")
    press = next((k for k in range(1, land + 1) if rows[k]["c"]["status"] in (ATTACK_AIR, LANDING_AIR)
                  and rows[k]["c"]["lc"] >= 7), None)
    if press is None:
        raise OsdError(f"{name}: no L-cancel press recorded in memory")
    lc = rows[land]["c"]["lc"]
    win_frame = 8 - lc
    r.note(f"{name}: press at row {press}, land at {land} (lc={lc}, window frame {win_frame})")
    if win_frame <= 30:
        raise OsdError(f"{name}: window frame {win_frame} is not above 30")
    want = ["L-Cancel (0%)", "Missed"]
    r.expect_osd(name, text, 2, ID_LCANCEL, want)
    r.check_color(r.shots[-1], 2, RED, name)
    return win_frame


def sc_actoos(r, delay, name):
    """The CPU holds shield; Mario jabs; the CPU jumps `delay` frames after
    its shield stun ended (a fresh X press)."""
    def inputs(r, i, rows, ctx):
        c = rows[-1]["c"]
        if i == 0:
            r.emu.hold(1, ["R"])
        if i == 12:
            r.emu.hold(0, ["A"])
        elif i == 14:
            r.emu.neutral(0)
        if c["status"] == GUARD_DAMAGE:
            ctx["stun"] = True
        if ctx.get("stun") and c["status"] != GUARD_DAMAGE and "out" not in ctx:
            ctx["out"] = i
        if "out" in ctx and i == ctx["out"] + delay:
            r.emu.hold(1, ["R", "X"])
        elif "out" in ctx and i == ctx["out"] + delay + 2:
            r.emu.hold(1, ["R"])
    rows, text = r.run(r.states["control"], 80, inputs, shot="osds-" + name, osd_player=2)
    stun = first(rows, 1, lambda x: x["c"]["status"] == GUARD_DAMAGE)
    out = first(rows, stun or 1, lambda x: x["c"]["status"] != GUARD_DAMAGE) if stun else None
    act = entered(rows, "c", JUMP_SQUAT, out or 1)
    if None in (stun, out, act):
        raise OsdError(f"{name}: shield stun {stun}, out {out}, jump {act}")
    n = act - out + 1
    r.expect_osd(name, text, 2, ID_ACTOOS, ["OoS Action", f"Frame {n}"])
    r.check_color(r.shots[-1], 2, GREEN if n == 1 else WHITE, name)
    return n


def sc_fastfall(r, delay, name):
    """Full hop (X for 14 frames); down `delay` frames after Mario starts
    falling."""
    def inputs(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"])
        elif i == 14:
            r.emu.neutral(0)
        if len(rows) > 1 and s["air"] and s["y"] < rows[-2]["m"]["y"] and "fall" not in ctx:
            ctx["fall"] = i
        if "fall" in ctx and i == ctx["fall"] + delay:
            r.emu.hold(0, stick=(128, 0))
        elif "fall" in ctx and i == ctx["fall"] + delay + 2:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], 80, inputs, shot="osds-" + name, osd_player=1)
    ys = [x["m"]["y"] for x in rows]
    vy = [0.0] + [ys[k] - ys[k - 1] for k in range(1, len(ys))]
    fall = next(k for k in range(2, len(rows)) if rows[k]["m"]["air"] and rows[k - 1]["m"]["air"] and vy[k] <= 0)
    ff = next((k for k in range(fall, len(rows)) if rows[k]["m"]["air"] and vy[k] - vy[k - 1] < -1.0), None)
    if ff is None:
        raise OsdError(f"{name}: no fastfall in memory")
    n = ff - fall + 1
    r.note(f"{name}: falling from row {fall}, fastfall at {ff}: "
           f"{[(x['m']['status'], round(x['m']['y'], 2), x['m']['raf']) for x in rows[ff - 3:ff + 3]]}")
    r.expect_osd(name, text, 1, ID_FASTFALL, ["Fastfall", f"Frame {n}"])
    return n


def sc_jumpcancel(r, delay, name):
    """X, then Z (grab) `delay` frames into the jump squat."""
    def inputs(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"])
        if s["status"] == JUMP_SQUAT and "js" not in ctx:
            ctx["js"] = i
        if "js" in ctx and i == ctx["js"] + delay:
            r.emu.hold(0, ["Z"])
        elif "js" in ctx and i == ctx["js"] + delay + 2:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], 40, inputs, shot="osds-" + name, osd_player=1)
    js = entered(rows, "m", JUMP_SQUAT)
    grab = entered(rows, "m", CATCH)
    if js is None or grab is None:
        raise OsdError(f"{name}: jump squat {js}, grab {grab}")
    n = max(1, grab - js)
    r.expect_osd(name, text, 1, ID_JUMP_CANCEL, ["Jump Cancel", f"Frame {n}"])
    return n


def sc_combo(r, name):
    """Jab, jab on the CPU."""
    def inputs(r, i, rows, ctx):
        if i in (1, 9):
            r.emu.hold(0, ["A"])
        elif i in (3, 11):
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], 50, inputs, shot="osds-" + name, osd_player=1)
    hits = [k for k in range(1, len(rows)) if rows[k]["c"]["hitlag"] > rows[k - 1]["c"]["hitlag"]
            and rows[k]["c"]["hitstun"] > 0]
    if len(hits) < 2:
        raise OsdError(f"{name}: {len(hits)} hits in memory")
    between = all(rows[k]["c"]["hitstun"] > 0 or rows[k]["c"]["hitlag"] > 0 for k in range(hits[0], hits[1] + 1))
    if not between:
        raise OsdError(f"{name}: the CPU got out of hitstun between the jabs")
    dealt = int(rows[hits[1]]["c"]["damage"]) - int(rows[hits[0] - 1]["c"]["damage"])
    r.expect_osd(name, text, 1, ID_COMBO, ["Combo", f"2 Hits ({dealt}%)"])
    return dealt


def sc_sdi(r, name):
    """Forward smash on the CPU; during its hitlag the CPU's stick goes
    right, up, right, up... two frames each."""
    pattern = [RIGHT, RIGHT, UP, UP]

    def inputs(r, i, rows, ctx):
        c = rows[-1]["c"]
        if i == 1:
            r.emu.hold(0, ["A"], stick=RIGHT)
        elif i == 4:
            r.emu.neutral(0)
        if c["hitlag"] > 0 and c["hitstun"] > 0 and "hl" not in ctx:
            ctx["hl"] = i
        if "hl" in ctx and c["hitlag"] > 0:
            r.emu.hold(1, stick=pattern[(i - ctx["hl"]) % 4])
        elif "hl" in ctx:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], 60, inputs, shot="osds-" + name, osd_player=2)
    start = first(rows, 1, lambda x: x["c"]["hitlag"] > 0 and x["c"]["hitstun"] > 0)
    if start is None:
        raise OsdError(f"{name}: no hit in memory")
    shifts = sum(1 for k in range(start + 1, len(rows)) if rows[k]["c"]["hitlag"] > 0
                 and (abs(rows[k]["c"]["x"] - rows[k - 1]["c"]["x"]) > 0.01
                      or abs(rows[k]["c"]["y"] - rows[k - 1]["c"]["y"]) > 0.01))
    got = r.messages(text, 2, ID_SDI)
    if not got:
        raise OsdError(f"{name}: no SDI message: {OSD.findall(text)}")
    inputs_shown = int(got[0][1].split("/")[1].split()[0])
    r.expect_osd(name, text, 2, ID_SDI, ["SDI", f"{shifts}/{inputs_shown} Shifts"])
    if shifts < 1 or inputs_shown < shifts:
        raise OsdError(f"{name}: {shifts} shifts in memory, {inputs_shown} inputs shown")
    return shifts


def sc_actoohitstun(r, name):
    """Mario jabs the CPU. Probe runs tap X on the CPU's port every other
    frame from the hit (two phases a frame apart): the earliest jump squat
    is its first actionable frame. Then the CPU jumps 3 frames later."""
    def jab(r, i, rows, ctx):
        if i == 1:
            r.emu.hold(0, ["A"])
        elif i == 3:
            r.emu.neutral(0)
        c = rows[-1]["c"]
        if c["hitstun"] > 0 and "hit" not in ctx:
            ctx["hit"] = i

    earliest = []
    for phase in (0, 1):
        def probe(r, i, rows, ctx, phase=phase):
            jab(r, i, rows, ctx)
            if "hit" in ctx:
                r.emu.hold(1, ["X"] if (i - ctx["hit"] + phase) % 2 == 0 else [])
        prow, _ = r.run(r.states["control"], 60, probe)
        js = entered(prow, "c", JUMP_SQUAT)
        if js is not None:
            earliest.append(js)
    if not earliest:
        raise OsdError(f"{name}: the CPU never jumped in the probe runs")
    f0 = min(earliest)

    def late(r, i, rows, ctx):
        jab(r, i, rows, ctx)
        if i == f0 + 1:
            r.emu.hold(1, ["X"])
        elif i == f0 + 3:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], 60, late, shot="osds-" + name, osd_player=2)
    js = entered(rows, "c", JUMP_SQUAT)
    if js is None:
        raise OsdError(f"{name}: no jump in the timed run")
    r.expect_osd(name, text, 2, ID_ACT_OOHITSTUN, ["Out of Hitstun", f"Frame {js - f0 + 1}"])
    return js - f0 + 1


def sc_ledgedash(r, wait, name):
    """Mario's down smash sends the CPU off the edge, where it grabs the
    ledge. `wait` frames into CliffWait it drops (down), jumps on the first
    falling frame, airdodges toward the stage 3 frames later and lands."""
    def inputs(r, i, rows, ctx):
        c = rows[-1]["c"]
        if i == 1:
            r.emu.hold(0, ["A"], stick=DOWN)
        elif i == 4:
            r.emu.neutral(0)
        if c["status"] == CLIFF_WAIT and "wait" not in ctx:
            ctx["wait"] = i
        if "wait" in ctx and i >= ctx["wait"] + wait and c["status"] == CLIFF_WAIT:
            r.emu.hold(1, stick=DOWN)
        if "wait" in ctx and c["status"] == FALL and "jump" not in ctx:
            ctx["jump"] = i
            r.emu.hold(1, ["X"])
        elif "jump" in ctx and i == ctx["jump"] + 2:
            r.emu.neutral(1)
        if "jump" in ctx and i > ctx["jump"] + 2 and c["y"] > 0.5 and "dodge" not in ctx:
            ctx["dodge"] = i
            r.emu.hold(1, ["R"], stick=(40, 50))
        elif "dodge" in ctx and i == ctx["dodge"] + 3:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], 200, inputs, shot="osds-" + name, osd_player=2, stop_after_osd=False)
    w0 = entered(rows, "c", CLIFF_WAIT)
    left = first(rows, (w0 or 0) + 1, lambda x: x["c"]["status"] != CLIFF_WAIT) if w0 else None
    if left is None:
        raise OsdError(f"{name}: the CPU never left CliffWait (entered at {w0})")
    landed = first(rows, left, lambda x: not x["c"]["air"] and x["c"]["status"] in FREE)
    if landed is None:
        statuses = [hex(x["c"]["status"]) for x in rows[left:left + 60]]
        raise OsdError(f"{name}: no actionable landing after the ledge: {statuses}")
    xlu = rows[landed]["c"]["xlu"]
    if xlu > 0:
        galint = xlu
    else:
        last = max(k for k in range(landed) if rows[k]["c"]["xlu"] > 0)
        galint = -(landed - last)  # the first vulnerable frame counts 1
    ledge_frame = left - w0 + 1
    if ledge_frame <= 24:
        r.expect_osd(name + "-cliffwait", text, 2, ID_CLIFFWAIT, ["Ledge Hang", f"{ledge_frame} Frames"])
    if galint >= -24:
        r.expect_osd(name, text, 2, ID_GALINT, ["GALINT", f"{galint:+d} Frames"])
    else:
        no_osd(r, name, text, 2, ID_GALINT, "over 24 vulnerable frames (no OSD shown)")
    m = LEDGE.findall(text)
    if not m or int(m[0][1]) != galint:
        raise OsdError(f"{name}: ledgedash log {m}, GALINT {galint}")
    r.note(f"{name}: path {[(hex(x['c']['status']), round(x['c']['x'], 1), round(x['c']['y'], 1)) for x in rows[left:landed + 1:2]]}")
    r.note(f"{name}: CliffWait from row {w0}, left at {left}, actionable on the ground at {landed} "
           f"(status {rows[landed]['c']['status']:#x}), intangibility timer {xlu}")
    return galint


def ledge_panel_grey(shot):
    """Count neutral-grey pixels inside the 3 label strips of the Ledgedash
    HUD's right info panel (ui/ledgedash_hud.cpp, hud_layout.h)."""
    from PIL import Image
    path, w, h = shot
    im = Image.open(path).convert("RGB")
    sx, sy = w / 640.0, h / 480.0
    sq = squeeze_of(shot)
    left = 640.0 - (640.0 - 515.1) * sq
    width = (617.0 - 515.1) * sq
    n = 0
    for row in range(3):
        y0 = int((96.6 + row * 54.4 + 4.0) * sy)
        y1 = int((96.6 + row * 54.4 + 20.0) * sy)
        x0 = int((left + 4.0) * sx)
        x1 = int((left + width - 4.0) * sx)
        for y in range(y0, y1):
            for x in range(x0, x1):
                r_, g_, b_ = im.getpixel((x, y))
                avg = (r_ + g_ + b_) // 3
                if 75 <= avg <= 165 and max(r_, g_, b_) - min(r_, g_, b_) < 25:
                    n += 1
    return n


def sc_ledgedash_hud_cpu(r, name):
    """Fox (CPU in match setup) is knocked to the ledge and recovers onto the
    stage while the Ledgedash HUD is enabled and the human player (Mario) has
    never grabbed the ledge. The Ledgedash HUD belongs to the human player, so
    the CPU's recovery must not populate or draw it."""
    def inputs(r, i, rows, ctx):
        c = rows[-1]["c"]
        if i == 1:
            r.emu.hold(0, ["A"], stick=DOWN)
        elif i == 4:
            r.emu.neutral(0)
        if c["status"] == CLIFF_WAIT and "wait" not in ctx:
            ctx["wait"] = i
        if "wait" in ctx and i >= ctx["wait"] + 5 and c["status"] == CLIFF_WAIT:
            r.emu.hold(1, stick=DOWN)
        if "wait" in ctx and c["status"] == FALL and "jump" not in ctx:
            ctx["jump"] = i
            r.emu.hold(1, ["X"])
        elif "jump" in ctx and i == ctx["jump"] + 2:
            r.emu.neutral(1)
        if "jump" in ctx and i > ctx["jump"] + 2 and c["y"] > 0.5 and "dodge" not in ctx:
            ctx["dodge"] = i
            r.emu.hold(1, ["R"], stick=(40, 50))
        elif "dodge" in ctx and i == ctx["dodge"] + 3:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], 180, inputs,
                       settings=[(S_LEDGE_HUD, "01"), (S_LEDGE_PANEL, "01")],
                       shot="osds-" + name, osd_player=2, stop_after_osd=False)
    m = LEDGE.findall(text)
    if not m or int(m[0][0]) != 2:
        raise OsdError(f"{name}: Fox CPU did not complete a ledge recovery ({m})")
    grey = ledge_panel_grey(r.shots[-1])
    r.note(f"{name}: Fox CPU logged {m[0]}, Ledgedash HUD panel grey pixels = {grey}")
    if grey >= 100:
        raise OsdError(f"{name}: Ledgedash HUD panel drawn for CPU recovery ({grey} grey pixels)")
    r.results.append((name, "no Ledgedash HUD panel for CPU", f"{grey} panel px"))
    return grey


def sc_overlay(r, name):
    """HMN Color Overlays set by the menu to the ProjectPunch preset (Wait
    green): Mario idles; the colour-blend bytes and the pixels must show it;
    with the groups off again they must be cleared."""
    rows, _ = r.run(r.states["control"], 12, lambda *a: None, keep_overlays=True)
    on = rows[-1]["m"]
    if on["status"] != WAIT or on["sub"] != "2dd273af" or on["subon"] != 1:
        raise OsdError(f"{name}: Mario in {on['status']:#x}, colour bytes {on['sub']} on={on['subon']}")
    # Training's "Control" option hands the CPU's fighter to port 2: the
    # trackers then count it as a human player, and HMN overlays apply.
    r.emu.h.call("pause")
    r.emu.h.call("frame_advance", n=1)
    r.emu.h.call("resume")
    shot_on = r.emu.screenshot("osds-overlay-wait-green")
    r.shots.append(shot_on)
    off = [(S_OVERLAY[0] + 1 + g, "00") for g in range(17)]
    rows2, _ = r.run(r.states["control"], 12, lambda *a: None, settings=off, keep_overlays=True)
    if rows2[-1]["m"]["subon"] != 0:
        raise OsdError(f"{name}: colour still on with every group off ({rows2[-1]['m']['sub']})")
    shot_off = r.emu.screenshot("osds-overlay-off")
    r.shots.append(shot_off)
    from PIL import Image
    a = Image.open(shot_on[0]).convert("RGB")
    b = Image.open(shot_off[0]).convert("RGB")
    greener = 0
    for y in range(0, a.size[1], 2):
        for x in range(0, a.size[0], 2):
            pa, pb = a.getpixel((x, y)), b.getpixel((x, y))
            if pa != pb and pa[1] - pb[1] > 40 and pa[1] > pa[0] and pa[1] > pa[2]:
                greener += 1
    if greener < 50:
        raise OsdError(f"{name}: only {greener} pixels turned green")
    r.results.append((name, "Mario green in Wait, cleared when off", f"{greener} px greener; bytes 2dd273af/on, off"))
    r.note(f"{name}: {greener} sampled pixels greener with the overlay; bytes {on['sub']} on, cleared when off")


def sc_invisible_overlay(r, name):
    """HMN & CPU Wait = Invisible: whole-model byte, shadow projection bit and
    cursor/name cursor are 0 while idle, and all restored when the group is off."""
    base_rows, _ = r.run(r.states["control"], 3, lambda *a: None)
    pre_m, pre_c = base_rows[-1]["m"], base_rows[-1]["c"]
    sets = [(S_OVERLAY[0] + 1 + g, "00") for g in range(17)] + [(S_OVERLAY[1] + 1 + g, "00") for g in range(17)] + [
        (S_OVERLAY[0] + 1 + GROUP_WAIT, "0b"), (S_OVERLAY[1] + 1 + GROUP_WAIT, "0b")]
    rows, _ = r.run(r.states["control"], 8, lambda *a: None, settings=sets)
    hid_m, hid_c = rows[-1]["m"], rows[-1]["c"]
    r.note(f"{name}: HMN before=(vis={pre_m['vis']}, proj={pre_m['shadow_proj']}, "
           f"cur={pre_m['cursor']}, ncur={pre_m['name_cursor']}) "
           f"hidden=(vis={hid_m['vis']}, proj={hid_m['shadow_proj']}, "
           f"cur={hid_m['cursor']}, ncur={hid_m['name_cursor']})")
    r.note(f"{name}: CPU before=(vis={pre_c['vis']}, proj={pre_c['shadow_proj']}, "
           f"cur={pre_c['cursor']}, ncur={pre_c['name_cursor']}) "
           f"hidden=(vis={hid_c['vis']}, proj={hid_c['shadow_proj']}, "
           f"cur={hid_c['cursor']}, ncur={hid_c['name_cursor']})")
    r.shots.append(r.emu.screenshot("osds-overlay-invisible"))
    for who, hid in (("HMN", hid_m), ("CPU", hid_c)):
        if hid["vis"] != 0 or hid["shadow_proj"] != 0 or hid["cursor"] != 0 or hid["name_cursor"] != 0:
            raise OsdError(f"{name}: {who} while hidden got vis={hid['vis']} proj={hid['shadow_proj']} "
                           f"cursor={hid['cursor']} name_cursor={hid['name_cursor']}")
    r.emu.h.call("pause")
    for offset, value in [(S_OVERLAY[0] + 1 + GROUP_WAIT, "00"), (S_OVERLAY[1] + 1 + GROUP_WAIT, "00")]:
        r.emu.h.call("write_mem", addr=r.lab.settings + offset, hex=value)
    r.emu.h.call("frame_advance", n=3)
    after_m, after_c = r.read("m"), r.read("c")
    r.emu.h.call("resume")
    for who, pre, after in (("HMN", pre_m, after_m), ("CPU", pre_c, after_c)):
        if (after["vis"] != 1 or after["shadow_proj"] != 1 or after["cursor"] != pre["cursor"]
                or after["name_cursor"] != pre["name_cursor"]):
            raise OsdError(f"{name}: {who} after off got vis={after['vis']} proj={after['shadow_proj']} "
                           f"cursor={after['cursor']} name_cursor={after['name_cursor']} "
                           f"(before cursor={pre['cursor']} name_cursor={pre['name_cursor']})")
    r.results.append((name, "hidden in Wait (model, shadow proj, tag), shown again when off",
                      "HMN & CPU vis/proj/cur/ncur 0, then restored"))
    r.note(f"{name}: visibility, shadow projection and cursors 0 while Invisible applied, restored after")


def sc_tech(r, name, delay=0):
    """CPU (Fox, dist=4) at 50% (locked), Mario's up smash; the CPU techs in
    place (guard 5 frames before landing). Tech Invisibility with `delay`:
    the CPU's whole-model byte must be 0 exactly while its subaction is a
    tech/getup one on 1-indexed frames dist+delay+1..20 (visible again by
    frame 21)."""
    # Under Training's "Control" (port 2 operates it) the CPU is still the
    # match's CPU player, so CPU Options > CPU Percent and its lock set its
    # damage; Tech Invisibility = All Fighters reaches it.
    sets = [(S_CPU_PERCENT, "0032"), (S_CPU_LOCK, "01"), (S_TECH_INVIS, "02"), (S_TECH_DELAY, f"{delay:02x}")]

    def smash(r, i, rows, ctx):
        if i == 1:
            r.emu.hold(0, ["A"], stick=UP)
        elif i == 4:
            r.emu.neutral(0)
        if ctx.get("press") is not None:
            if i == ctx["press"]:
                r.emu.hold(1, ["R"])
            elif i == ctx["press"] + 1:
                r.emu.neutral(1)
    rows, _ = r.run(r.states["control"], 200, smash, settings=sets)
    land = next((k for k in range(10, len(rows)) if rows[k - 1]["c"]["air"] and not rows[k]["c"]["air"]), None)
    if land is None:
        raise OsdError(f"{name}: the CPU never landed")

    def teched(r, i, rows, ctx):
        ctx["press"] = land - 6
        smash(r, i, rows, ctx)

    def shoot(r, i, rows, ctx):
        teched(r, i, rows, ctx)
        c = rows[-1]["c"]
        if c["mkind"] in TECH_MOTIONS and c["mframe"] >= 10:
            r.emu.h.call("resume")
            r.shots.append(r.emu.screenshot("osds-tech-invisible"))
            r.emu.h.call("pause")
            ctx["stop"] = True
    rows, _ = r.run(r.states["control"], 200, teched, settings=sets)
    if any(x["c"]["status"] >= 0xBD for x in rows):
        raise OsdError(f"{name}: the CPU was KO'd")
    checked = hidden = 0
    kinds = set()
    first_hidden = FOX_DISTINGUISHABLE + delay + 1
    for k in range(1, len(rows)):
        c = rows[k]["c"]
        sf = int(c["mframe"]) + 1
        want = 0 if c["mkind"] in TECH_MOTIONS and first_hidden <= sf < 21 else 1
        if c["mkind"] in TECH_MOTIONS:
            kinds.add(hex(c["mkind"]))
        bad_hide = want == 0 and (c["vis"] != 0 or c["shadow_proj"] != 0 or c["cursor"] != 0 or c["name_cursor"] != 0)
        bad_show = want == 1 and (c["vis"] != 1
                                  or (c["mkind"] in TECH_MOTIONS and sf == 21
                                      and (c["shadow_proj"] != 1 or c["name_cursor"] != 1)))
        if bad_hide or bad_show:
            seq = [(hex(x["c"]["status"]), hex(x["c"]["mkind"]), x["c"]["mframe"],
                    x["c"]["vis"], x["c"]["shadow_proj"], x["c"]["cursor"], x["c"]["name_cursor"])
                   for x in rows[max(0, k - 4):k + 3]]
            raise OsdError(f"{name}: row {k}: got vis={c['vis']} proj={c['shadow_proj']} cur={c['cursor']} "
                           f"ncur={c['name_cursor']}, expected want={want}: {seq}")
        checked += 1
        hidden += want == 0
    if hidden < 10:
        statuses = sorted({hex(x["c"]["status"]) for x in rows[land:land + 40]})
        motions = sorted({hex(x["c"]["mkind"]) for x in rows[land:land + 40]})
        raise OsdError(f"{name}: only {hidden} hidden frames (statuses {statuses}, motions {motions})")
    if delay == 0:
        r.run(r.states["control"], 200, shoot, settings=sets)
    r.results.append((name, f"hidden on tech frames {first_hidden}-20", f"{hidden} hidden of {checked} frames, motions {kinds}"))
    r.note(f"{name}: visibility matched the tech window on {checked} frames ({hidden} hidden; motions {kinds})")


# ---- frame-exact scenarios (plugin-side input) ------------------------------------

J = B_JUMP | B_FLICK
DOWN_RIGHT, DOWN_LEFT = (0.7, -0.7), (-0.7, -0.7)
SIDE = {0: "m", 1: "c"}
TRACE_EV = re.compile(r"trace ev f=(\d+) p=(\d)b? #\d+ (\w+)<-(\w+)")
TRACE_FRAME = re.compile(r"trace f=(\d+) p=(\d) st=(\w+) pv=(\w+) ch=(\d+) sf=(-?\d+)")
ADVANTAGE = re.compile(r"advantage: P1 -> P2 on shield ([+-]\d+) \(hit (\d+), attacker (\d+) status \w+ from \w+, "
                       r"defender (\d+) status (\w+) from (\w+)")


def events_by_frame(text, player):
    """The plugin's status changes per tracker frame (its trace), for the
    notes: what P+ did within each frame."""
    out = {}
    for f, p, st, pv in TRACE_EV.findall(text):
        if int(p) == player:
            out.setdefault(int(f), []).append(f"{pv}->{st}")
    return out


def jump_squat(r, who):
    """Frames `who`'s jump squat lasts (a probe, cached)."""
    key = ("js", who)
    if key not in r.cache:
        rows, _ = r.play(r.states["control"], {who: ({1: (J, 0.0, 0.0)}, 1)}, extra=12)
        side = SIDE[who]
        if rows[1][side]["status"] != JUMP_SQUAT:
            raise OsdError(f"P{who + 1} is not in its jump squat on the jump frame ({rows[1][side]['status']:#x})")
        r.cache[key] = next(n for n in range(1, len(rows)) if rows[n][side]["status"] != JUMP_SQUAT) - 1
        r.note(f"P{who + 1}: jump squat {r.cache[key]} frames")
    return r.cache[key]


def sc_wavedash_exact(r, who, k, full, name, shot=False, state=None, position=POS_SIDES):
    """Jump, then the airdodge (shield, stick down-forward) on airborne frame
    k exactly. Short hop: jump for one frame; full hop: held through the jump
    squat. From memory: the jump squat's frames, then the frames up to the
    airdodge counted from the first frame out of the jump squat."""
    js = jump_squat(r, who)
    side, player = SIDE[who], who + 1
    fr = {n: (J, 0.0, 0.0) for n in range(1, (js + 1 if full else 1) + 1)}
    dodge = js + k
    fr[dodge] = (B_GUARD | fr.get(dodge, (0,))[0], *DOWN_RIGHT)
    fr[dodge + 1] = (0, *DOWN_RIGHT)
    rows, text = r.play(state or r.states["control"], {who: (fr, dodge + 1)}, extra=40,
                        shot=("osds-" + name) if shot else None, osd_player=player)
    st = [x[side]["status"] for x in rows]
    if st[1:js + 1] != [JUMP_SQUAT] * js or st[js + 1] == JUMP_SQUAT:
        raise OsdError(f"{name}: jump squat in memory {[hex(v) for v in st[:js + 3]]}")
    air = js + 1
    if any(v != JUMP for v in st[air:dodge]) or st[dodge] not in (ESCAPE_AIR, LANDING_SPECIAL):
        raise OsdError(f"{name}: from the first airborne frame {[hex(v) for v in st[air:dodge + 2]]}")
    if LANDING_SPECIAL not in st[dodge:]:
        raise OsdError(f"{name}: the airdodge did not land in LandingFallSpecial")
    frame = dodge - air + 1
    held = js if full else 1
    want = [f"Wavedash: f{frame}", "Angle: 45.0", f"{'FH' if full else 'SH'} ({held}f)"]
    r.expect_osd(name, text, player, ID_WAVEDASH, want)
    trace = events_by_frame(text, player)
    land = next((f for f in sorted(trace) if any(e.endswith("->19") for e in trace[f])), None)
    if land is None:
        raise OsdError(f"{name}: no change into LandingFallSpecial in the trace")
    r.note(f"{name}: status changes per frame: "
           + "; ".join(f"{f - land:+d}: {', '.join(trace[f])}" for f in sorted(trace) if land - 8 <= f <= land))
    # The info display's State Frame and change count on that frame, and on
    # the next (one change-free frame later it must count on).
    frames = {int(g[0]): g for g in TRACE_FRAME.findall(text) if int(g[1]) == player}
    got = [(int(frames[f][4]), int(frames[f][5])) if f in frames else None for f in (land, land + 1)]
    if got != [(len(trace[land]), 1), (0, 2)]:
        raise OsdError(f"{name}: trace (changes, frames in status) on the landing frame and the next: {got}, "
                       f"expected {[(len(trace[land]), 1), (0, 2)]}")
    if shot:
        r.check_color(r.shots[-1], player, GREEN if frame == 1 else WHITE, name, position)
    return frame


def wavedash_angle_colour(angle):
    """Wavedash angle bands (src/main/ui/osd.h): 16.2..22.0 green, 22.0..30.0
    yellow, else neutral white."""
    if angle < 16.2 or angle > 30.0:
        return WHITE
    return GREEN if angle < 22.0 else YELLOW


def sc_wavedash_angle(r, stick, name):
    """A full hop airdodged on airborne frame 2 with `stick` (the frame and
    hop lines then white): the shown angle is floored to a tenth of the
    processed stick's angle, and its line takes that band's colour, seen on
    the screenshot."""
    js = jump_squat(r, 0)
    fr = {n: (J, 0.0, 0.0) for n in range(1, js + 2)}
    dodge = js + 2
    fr[dodge] = (B_GUARD | fr.get(dodge, (0,))[0], *stick)
    fr[dodge + 1] = (0, *stick)
    rows, text = r.play(r.states["control"], {0: (fr, dodge + 1)}, extra=60, shot="osds-" + name, osd_player=1)
    got = r.messages(text, 1, ID_WAVEDASH)
    if not got:
        raise OsdError(f"{name}: no wavedash message: {OSD.findall(text)}")
    m = re.match(r"Angle: (\d+\.\d)$", got[0][1])
    if got[0][0] != "Wavedash: f2" or m is None:
        raise OsdError(f"{name}: shown {got[0]}")
    angle = float(m.group(1))
    want = math.floor(math.degrees(math.atan2(abs(stick[1]), abs(stick[0]))) * 10) / 10
    if abs(angle - want) > 0.1:
        r.note(f"{name}: stick {stick} gives {want:.1f} unprocessed; shown {angle} (P+'s processing)")
    colour = wavedash_angle_colour(angle)
    r.note(f"{name}: shown {got[0]}, angle colour {colour}")
    if colour != WHITE:
        r.check_color(r.shots[-1], 1, colour, name)
    r.results.append((name, f"angle {angle} in {colour}", " / ".join(got[0])))
    return angle


def sc_osd_position(r, position, name):
    """OSD Position set through the lab menu (OSD Menu's first row), then
    wavedash-1's wavedash: its message must be drawn at that position's
    spot. The position is the plugin's, set by the row's callback, so the
    scenario saves its own state after the menu."""
    lab, emu = r.lab, r.emu
    emu.h.call("load_state", path=r.states["control"])
    lab.open_menu()
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    lab.set_choice(S_OSD_POSITION, position)
    r.shots.append(emu.screenshot("osds-menu-" + name))
    lab.close_menu()
    emu.advance(30)
    state = os.path.join(os.path.dirname(r.states["control"]), f"osds-{name}.sav")
    emu.h.call("save_state", path=state)
    sc_wavedash_exact(r, 0, 1, False, name, shot=True, state=state, position=position)
    r.note(f"{name}: OSD Position {lab.setting(S_OSD_POSITION)} set through the menu")
    r.position_shots[position] = r.shots[-1]
    hud = r.position_shots.get(POS_HUD)
    if position == POS_SIDES:
        # P1's Sides and HUD boxes overlap on 16:9: the same message must
        # move by the difference of the two spots.
        if hud is None:
            raise OsdError(f"{name}: needs osd-position-hud's screenshot first")
        (hx, hy), (sx, sy) = osd_spot(POS_HUD, 1, squeeze_of(hud)), osd_spot(POS_SIDES, 1, squeeze_of(hud))
        (ax, ay, an), (bx, by, bn) = green_box(hud), green_box(r.shots[-1])
        dx, dy = bx - ax, by - ay
        if an < 50 or bn < 50 or abs(dx - (sx - hx)) > 1.5 or abs(dy - (sy - hy)) > 1.5:
            raise OsdError(f"{name}: text moved ({dx:.2f}, {dy:.2f}) from HUD ({an}, {bn} pixels), expected "
                           f"({sx - hx:.2f}, {sy - hy:.2f})")
        r.note(f"{name}: text moved ({dx:.2f}, {dy:.2f}) from HUD, expected ({sx - hx:.2f}, {sy - hy:.2f})")


def osd_body_mean_rgb(shot, player, position=POS_SIDES):
    """Mean RGB in the upper-left interior corner of `player`'s newest OSD
    card (away from centred text lines), 640x480 space."""
    from PIL import Image
    path, w, h = shot
    im = Image.open(path).convert("RGB")
    sx, sy = w / 640.0, h / 480.0
    cx, cy = osd_spot(position, player, squeeze_of(shot))
    half = 124.0 * squeeze_of(shot) / 2
    x0, x1 = int((cx - half + 3) * sx), int((cx - half + 14) * sx)
    y0, y1 = int((cy - 22) * sy), int((cy - 14) * sy)
    rs, gs, bs, n = 0, 0, 0, 0
    for y in range(y0, max(y0 + 1, y1)):
        for x in range(x0, max(x0 + 1, x1)):
            r_, g_, b_ = im.getpixel((x, y))
            rs += r_
            gs += g_
            bs += b_
            n += 1
    return (rs / n, gs / n, bs / n) if n else (0.0, 0.0, 0.0)


def sc_osd_theme(r, name):
    """Display > Theme navigated through the lab menu, then live frame-1
    wavedash OSDs photographed across default Teal 50%, low-opacity 20% and
    0% (drop-shadow), Brawl Navy 50%, and Crimson 60%."""
    lab, emu = r.lab, r.emu
    emu.h.call("load_state", path=r.states["control"])
    lab.open_menu()
    lab.main_to(LT.MAIN_DISPLAY)
    lab.tap(["A"])
    lab.down(DISPLAY_THEME)
    lab.tap(["A"])
    if lab.setting(S_OSD_BG_COLOR) != 0 or lab.setting(S_OSD_BG_ALPHA) != 5:
        raise OsdError(f"{name}: Theme defaults ({lab.setting(S_OSD_BG_COLOR)}, {lab.setting(S_OSD_BG_ALPHA)}), "
                       f"wanted (0, 5)")
    r.shots.append(emu.screenshot("osds-menu-theme"))
    lab.set_choice(S_OSD_BG_COLOR, 2)
    lab.down(1)
    lab.set_choice(S_OSD_BG_ALPHA, 0)
    if lab.setting(S_OSD_BG_COLOR) != 2 or lab.setting(S_OSD_BG_ALPHA) != 0:
        raise OsdError(f"{name}: Theme menu change gave ({lab.setting(S_OSD_BG_COLOR)}, "
                       f"{lab.setting(S_OSD_BG_ALPHA)}), wanted (2, 0)")
    lab.close_menu()

    js = jump_squat(r, 0)
    fr = {1: (J, 0.0, 0.0), js + 1: (B_GUARD, *DOWN_RIGHT), js + 2: (0, *DOWN_RIGHT)}
    means = {}
    for tag, bg, alpha in (("teal-50", 0, 5), ("teal-20", 0, 2), ("teal-0", 0, 0),
                           ("navy-50", 2, 5), ("crimson-60", 5, 6)):
        _, text = r.play(r.states["control"], {0: (fr, js + 2)}, extra=40,
                         settings=[(S_OSD_BG_COLOR, f"{bg:02x}"), (S_OSD_BG_ALPHA, f"{alpha:02x}")],
                         shot=f"osds-theme-{tag}", osd_player=1)
        r.expect_osd(f"{name}-{tag}", text, 1, ID_WAVEDASH, ["Wavedash: f1", "Angle: 45.0", "SH (1f)"])
        r.check_color(r.shots[-1], 1, GREEN, f"{name}-{tag}")
        means[tag] = osd_body_mean_rgb(r.shots[-1], 1)
        r.note(f"{name}-{tag}: card corner RGB ({means[tag][0]:.1f}, {means[tag][1]:.1f}, {means[tag][2]:.1f})")
    crim_rb = means["crimson-60"][0] - means["crimson-60"][2]
    navy_rb = means["navy-50"][0] - means["navy-50"][2]
    if crim_rb <= navy_rb + 4.0:
        raise OsdError(f"{name}: Crimson (R-B={crim_rb:.1f}) did not shift warmer than Brawl Navy (R-B={navy_rb:.1f})")
    diff_0_50 = sum(abs(a - b) for a, b in zip(means["teal-50"], means["teal-0"]))
    if diff_0_50 < 4.0:
        raise OsdError(f"{name}: 50% and 0% OSD card bodies look identical (L1 RGB diff {diff_0_50:.1f})")
    r.results.append((name, "Theme menu + 5 presets/alphas",
                      f"crimson-navy R-B delta {crim_rb - navy_rb:.1f}, 50%-0% diff {diff_0_50:.1f}"))


def green_box(shot):
    """Centre of the bounding box (640x480 space) and count of the
    message's green text in the lower left quarter of the screen (the box's
    centre: how many edge pixels match the colour varies with the text's
    sub-pixel place, which moves a mean)."""
    from PIL import Image
    path, w, h = shot
    im = Image.open(path).convert("RGB")
    pts = [(x, y) for y in range(h // 2, h) for x in range(w // 2)
           if sum(abs(a - b) for a, b in zip(im.getpixel((x, y)), GREEN)) < 40]
    if not pts:
        return 0.0, 0.0, 0
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return (min(xs) + max(xs)) / 2 * 640 / w, (min(ys) + max(ys)) / 2 * 480 / h, len(pts)


def nair_probe(r):
    """Mario's short-hop neutral air (on the first airborne frame) without a
    press: its landing frame (cached)."""
    if "nair" not in r.cache:
        js = jump_squat(r, 0)
        fr = {1: (J, 0.0, 0.0), js + 1: (B_ATTACK, 0.0, 0.0)}
        rows, _ = r.play(r.states["control"], {0: (fr, js + 1)}, extra=70)
        land = entered(rows, "m", LANDING_AIR)
        if land is None:
            raise OsdError("the neutral air never landed")
        r.cache["nair"] = (fr, land)
    return r.cache["nair"]


def sc_lcancel_exact(r, before, name, act_after=None):
    """Shield pressed `before` frames before the landing frame (0: on the
    landing frame itself), or never (None). With `act_after`, a jab that
    many frames after the first frame out of the landing lag (Act OoLag;
    the frame is found by a first run)."""
    base, land = nair_probe(r)
    fr = dict(base)
    if before is not None:
        fr[land - before] = (B_GUARD, 0.0, 0.0)
    length = max(fr) + 1
    rows, text = r.play(r.states["control"], {0: (fr, length)}, extra=land + 30 - length)
    if entered(rows, "m", LANDING_AIR) != land:
        raise OsdError(f"{name}: landed on {entered(rows, 'm', LANDING_AIR)}, the probe on {land}")
    lc = rows[land]["m"]["lc"]
    rises = [n for n in range(1, land + 1) if rows[n]["m"]["lc"] > rows[n - 1]["m"]["lc"]
             and rows[n]["m"]["status"] in (ATTACK_AIR, LANDING_AIR)]
    out = first(rows, land, lambda x: x["m"]["status"] != LANDING_AIR)
    lag = (out or len(rows)) - land
    want = [f"L-Cancel ({100 if lc > 0 else 0}%)", f"Frame {land - rises[-1] + 1}/7" if rises else "Missed"]
    r.expect_osd(name, text, 1, ID_LCANCEL, want)
    r.note(f"{name}: P+ counter {lc} on the landing frame {land}, landing lag {lag} frames")
    if act_after is None:
        return lc > 0, lag
    # Act OoLag: frame 1 is the first frame a jump can come out (probes).
    e = earliest_jump(r, fr, land, ("lag", {0: "nair-lc", None: "nair"}.get(before, before)))
    fr[e + act_after] = (B_ATTACK, 0.0, 0.0)
    rows, text = r.play(r.states["control"], {0: (fr, e + act_after + 1)}, extra=5)
    jab = entered(rows, "m", ATTACK, land)
    if jab != e + act_after:
        raise OsdError(f"{name}: jab on {jab}, scripted on {e + act_after} (earliest jump {e}, landing status "
                       f"left on {out})")
    r.expect_osd(name + "-actoolag", text, 1, ID_ACT_OOLAG, ["Post-Lag", f"Frame {jab - e + 1}"])
    r.note(f"{name}: landing on {land}, earliest jump {e}, landing status left on {out}")
    return lc > 0, lag


LANDING, LANDING_LIGHT = 0x16, 0x17
ACT_OOLAG_SHOWN = 15  # techniques.cpp kActOoLagShown


def earliest_jump(r, fr, after, key):
    """The first frame after row `after` on which a jump can come out, with
    the inputs `fr` before it: probe runs tap jump on alternate frames from
    `after` + 1 (two phases), and the earliest jump squat of the two is it
    (cached under `key`)."""
    if key not in r.cache:
        earliest = []
        for phase in (0, 1):
            taps = dict(fr)
            taps.update({n: (J, 0.0, 0.0) for n in range(after + 1, after + 50) if (n + phase) % 2 == 0})
            rows, _ = r.play(r.states["control"], {0: (taps, after + 50)})
            js = entered(rows, "m", JUMP_SQUAT, after + 1)
            if js is not None:
                earliest.append(js)
        if not earliest:
            raise OsdError(f"{key}: Mario never jumped in the probes")
        r.cache[key] = min(earliest)
    return r.cache[key]


def note_lag_frames(r, name, rows, text, land, status, e):
    """For the notes: from the landing row on, the status, P+'s RA-Bit
    word 0 and the plugin's actionable flag, beside the earliest jump."""
    acts = {int(m.group(1)): int(m.group(2)) for m in re.finditer(r"trace f=(\d+) p=1 .*? act=(\d+)", text)}
    trace = events_by_frame(text, 1)
    tl = next((f for f in sorted(trace) if any(x.endswith(f"->{status:x}") for x in trace[f])), None)
    cells = []
    for n in range(land, min(e + 3, len(rows))):
        act = acts.get(tl + n - land) if tl is not None else None
        cells.append(f"{n - land}:{rows[n]['m']['status']:#x}/raf {rows[n]['m']['raf'][:8]}/act {act}")
    r.note(f"{name}: landing row {land}, earliest jump {e} (+{e - land}); " + ", ".join(cells))


def landing_of(rows, start=1):
    """The row the first landing status was entered, that status and the
    one before it (both as read at the ends of the frames)."""
    n = next((i for i in range(max(start, 1), len(rows)) if rows[i]["m"]["status"] in
              (LANDING, LANDING_LIGHT, LANDING_AIR, LANDING_SPECIAL)
              and rows[i - 1]["m"]["status"] != rows[i]["m"]["status"]), None)
    if n is None:
        return None, None, None
    return n, rows[n]["m"]["status"], rows[n - 1]["m"]["status"]


def full_hop_probe(r):
    """Mario's full hop with no other input: its inputs, landing row,
    landing status and the status before it (cached)."""
    if "fullhop" not in r.cache:
        js = jump_squat(r, 0)
        fr = {n: (J, 0.0, 0.0) for n in range(1, js + 2)}
        rows, _ = r.play(r.states["control"], {0: (fr, js + 2)}, extra=90)
        land, st, before = landing_of(rows, js + 2)
        if land is None:
            raise OsdError("the full hop never landed")
        r.cache["fullhop"] = (fr, land, st, before)
    return r.cache["fullhop"]


def autocancel_probe(r):
    """A full-hop neutral air that lands in Landing (0x16) straight out of
    AttackAir: the aerial started d frames before the full hop's landing,
    d tried from 1 up (cached)."""
    if "autocancel" not in r.cache:
        base, hop_land, _, _ = full_hop_probe(r)
        tried = []
        for d in range(1, 40):
            fr = dict(base)
            fr[hop_land - d] = (B_ATTACK, 0.0, 0.0)
            rows, _ = r.play(r.states["control"], {0: (fr, hop_land - d + 1)}, extra=d + 20)
            land, st, before = landing_of(rows, hop_land - d)
            tried.append((d, land, hex(st or 0), hex(before or 0)))
            if st == LANDING and before == ATTACK_AIR:
                r.note(f"autocancel: neutral air {d} frames before the full hop's landing lands in Landing on "
                       f"row {land} (tried {tried})")
                r.cache["autocancel"] = (fr, land)
                break
        else:
            raise OsdError(f"no neutral air landed in Landing out of AttackAir: {tried}")
    return r.cache["autocancel"]


def lag_source(r, kind):
    """Inputs up to a landing of `kind`, the landing row, the landing
    status and what it came out of (from memory)."""
    if kind == "nair":
        fr, land = nair_probe(r)
        return dict(fr), land, LANDING_AIR, ATTACK_AIR
    if kind == "nair-lc":
        fr, land = nair_probe(r)
        fr = dict(fr)
        fr[land] = (B_GUARD, 0.0, 0.0)
        return fr, land, LANDING_AIR, ATTACK_AIR
    if kind == "plain":
        fr, land, st, before = full_hop_probe(r)
        return dict(fr), land, st, before
    if kind == "autocancel":
        fr, land = autocancel_probe(r)
        return dict(fr), land, LANDING, ATTACK_AIR
    if kind == "wavedash":
        js = jump_squat(r, 0)
        fr = {1: (J, 0.0, 0.0), js + 1: (B_GUARD, *DOWN_RIGHT), js + 2: (0, *DOWN_RIGHT)}
        rows, _ = r.play(r.states["control"], {0: (fr, js + 2)}, extra=10)
        land, st, before = landing_of(rows, js + 1)
        if st != LANDING_SPECIAL:
            raise OsdError(f"the wavedash landed in {st}")
        return fr, land, st, ESCAPE_AIR
    raise ValueError(kind)


def sc_actoolag(r, kind, delay, name, shot=False, turn=False, from_wait=False):
    """A landing of `kind`, then a jump `delay` frames after the first frame
    a jump can come out (0: on it; found by probes). Act OoLag counts that
    frame as 1 and shows the count up to 13 after an aerial's landing
    (Act OoWait), an autocancelled aerial (Act OoAutocancel) or a wavedash,
    and nothing after a plain landing. `turn`: the stick held back at half
    tilt from that first frame (a Turn) until the jump. `from_wait`: the
    jump `delay` frames after the landing status ended into Wait instead;
    after an autocancel that shows nothing."""
    fr, land, status, before = lag_source(r, kind)
    e = earliest_jump(r, fr, land, ("lag", kind))
    act = e + delay
    if from_wait:
        rows, _ = r.play(r.states["control"], {0: (fr, max(fr) + 1)}, extra=land + 40 - max(fr))
        wait = first(rows, land, lambda x: x["m"]["status"] == WAIT)
        if wait is None:
            raise OsdError(f"{name}: the landing never ended into Wait")
        r.note(f"{name}: the landing status ended into Wait on row {wait} (+{wait - land}), earliest jump {e} "
               f"(+{e - land}): Wait is frame {wait - e + 1} of the count")
        act = wait + delay
    if turn:
        rows, _ = r.play(r.states["control"], {0: (fr, max(fr) + 1)}, extra=land + 5 - max(fr))
        back = -0.5 if rows[land]["m"]["lr"] > 0 else 0.5
        for n in range(e, act):
            fr[n] = (0, back, 0.0)
    fr[act] = (J, 0.0, 0.0)
    rows, text = r.play(r.states["control"], {0: (fr, act + 1)}, extra=10,
                        shot=("osds-" + name) if shot else None, osd_player=1, stop_after_osd=False)
    got_land, got_st, got_before = landing_of(rows, land)
    if got_land != land or got_st != status:
        raise OsdError(f"{name}: landed on {got_land} in {got_st}, the probe on {land} in {status}")
    js = entered(rows, "m", JUMP_SQUAT, land + 1)
    if js != act:
        raise OsdError(f"{name}: jump on {js}, scripted on {act} (earliest {e})")
    note_lag_frames(r, name, rows, text, land, status, e)
    if turn and rows[e]["m"]["status"] != TURN:
        raise OsdError(f"{name}: not in Turn on the first frame ({rows[e]['m']['status']:#x})")
    n = act - e + 1
    shown = kind in ("nair", "nair-lc", "autocancel", "wavedash") and n <= ACT_OOLAG_SHOWN
    shown = shown and not (from_wait and kind == "autocancel")
    if not shown:
        no_osd(r, name, text, 1, ID_ACT_OOLAG, f"{kind} landing ({status:#x} from {before:#x}), jump on frame {n}")
        return n
    head = "Autocancel" if kind == "autocancel" else "Post-Lag"
    r.expect_osd(name, text, 1, ID_ACT_OOLAG, [head, f"Frame {n}"])
    if shot:
        r.check_color(r.shots[-1], 1, GREEN if n == 1 else RED, name)
    return n


def shield_probe(r):
    """The CPU holds shield; Mario jabs on frame 10: the first frame out of
    the shield stun (cached)."""
    if "shield" not in r.cache:
        jab = ({10: (B_ATTACK, 0.0, 0.0)}, 12)
        rows, _ = r.play(r.states["control"], {0: jab, 1: ({n: (B_GUARD, 0.0, 0.0) for n in range(1, 61)}, 60)})
        stun = first(rows, 1, lambda x: x["c"]["status"] == GUARD_DAMAGE)
        out = first(rows, stun or 1, lambda x: x["c"]["status"] != GUARD_DAMAGE) if stun else None
        if out is None:
            raise OsdError(f"no shield stun in the probe (stun {stun})")
        r.cache["shield"] = (jab, out)
    return r.cache["shield"]


def sc_actoos_exact(r, delay, drop, name):
    """The CPU jumps (or lets go of shield) `delay` frames after the first
    frame out of shield stun (0: on that frame)."""
    jab, out = shield_probe(r)
    act = out + delay
    fr = {n: (B_GUARD, 0.0, 0.0) for n in range(1, act)}
    if not drop:
        fr[act] = (B_GUARD | J, 0.0, 0.0)
    rows, text = r.play(r.states["control"], {0: jab, 1: (fr, act + 1)}, extra=10, shot="osds-" + name,
                        osd_player=2)
    stun = first(rows, 1, lambda x: x["c"]["status"] == GUARD_DAMAGE)
    left = first(rows, stun or 1, lambda x: x["c"]["status"] != GUARD_DAMAGE) if stun else None
    did = entered(rows, "c", GUARD_OFF if drop else JUMP_SQUAT, left or 1) if left else None
    if None in (stun, left, did) or left != out or did != act:
        raise OsdError(f"{name}: shield stun {stun}, out {left} (probe {out}), action {did} (scripted {act})")
    n = did - left + 1
    if drop:
        # GuardOff (shield drop) is not counted as an OoS action.
        no_osd(r, name, text, 2, ID_ACTOOS, "a shield drop and no action after it")
    else:
        r.expect_osd(name, text, 2, ID_ACTOOS, ["OoS Action", f"Frame {n}"])
        r.check_color(r.shots[-1], 2, GREEN if n == 1 else WHITE, name)
    # Frame advantage: the defender's frame is its first frame out of
    # shield stun whether it acts on that frame or not.
    adv = ADVANTAGE.findall(text)
    if not adv or int(adv[0][3]) - int(adv[0][1]) != left - stun:
        raise OsdError(f"{name}: advantage {adv}, memory: shield hit on {stun}, out of stun on {left}")
    if "advantage" not in r.cache:
        r.cache["advantage"] = adv[0][0]
    elif adv[0][0] != r.cache["advantage"]:
        raise OsdError(f"{name}: advantage {adv[0][0]}, {r.cache['advantage']} in the scenario before")
    r.note(f"{name}: frame advantage {adv[0][0]} (defender {adv[0][4]} from {adv[0][5]})")
    return n


def vy(rows, side, n):
    return rows[n][side]["y"] - rows[n - 1][side]["y"]


def sc_fastfall_exact(r, who, delay, name):
    """Full hop; down `delay` frames after the first airborne frame on which
    the fighter does not move up (P+ takes a fastfall from there)."""
    js = jump_squat(r, who)
    side = SIDE[who]
    key = ("fall", who)
    full = {n: (J, 0.0, 0.0) for n in range(1, js + 3)}
    if key not in r.cache:
        rows, _ = r.play(r.states["control"], {who: (full, js + 2)}, extra=60)
        r.cache[key] = next(n for n in range(js + 2, len(rows)) if rows[n][side]["air"] and vy(rows, side, n) <= 0)
    fall = r.cache[key]
    fr = dict(full)
    fr[fall + delay] = (0, 0.0, -1.0)
    rows, text = r.play(r.states["control"], {who: (fr, fall + delay + 1)}, extra=8)
    start = next(n for n in range(js + 2, len(rows)) if rows[n][side]["air"] and vy(rows, side, n) <= 0)
    ff = next((n for n in range(start, len(rows)) if vy(rows, side, n) - vy(rows, side, n - 1) < -1.0), None)
    if ff is None or start != fall:
        raise OsdError(f"{name}: not rising from {start} (probe {fall}), fastfall {ff}")
    n = ff - start + 1
    r.note(f"{name}: first frame not rising {start} (y change there {vy(rows, side, start):.3f}; "
           f"{vy(rows, side, start - 1):.3f} the frame before), fastfall on {ff}")
    r.expect_osd(name, text, who + 1, ID_FASTFALL, ["Fastfall", f"Frame {n}"])
    return n


def sc_jumpcancel_exact(r, at, upsmash, name):
    """Jump, then a grab (or up smash) on jump squat frame `at` (2 is the
    first that is not the jump's own frame; None: its last frame)."""
    js = jump_squat(r, 0)
    at = js if at is None else at
    fr = {1: (J, 0.0, 0.0), at: ((B_ATTACK | B_SMASH, 0.0, 1.0) if upsmash else (B_GRAB, 0.0, 0.0))}
    rows, text = r.play(r.states["control"], {0: (fr, at + 1)}, extra=5)
    jsq = entered(rows, "m", JUMP_SQUAT)
    did = entered(rows, "m", ATTACK_HI4_START if upsmash else CATCH)
    if jsq != 1 or did != at or at > js:
        raise OsdError(f"{name}: jump squat {jsq}, {'up smash' if upsmash else 'grab'} {did} (scripted {at})")
    n = max(1, did - jsq)
    r.expect_osd(name, text, 1, ID_JUMP_CANCEL, ["Jump Cancel", f"Frame {n}"])
    return n


def sc_actoohitstun_exact(r, delay, name):
    """Mario jabs the CPU on frame 2. Probes tap jump on alternate frames
    (two phases): the earliest jump squat is the CPU's first frame able to
    act. Then it jumps `delay` frames after it."""
    jab = ({2: (B_ATTACK, 0.0, 0.0)}, 3)
    if "hitstun" not in r.cache:
        earliest = []
        for phase in (0, 1):
            fr = {n: (J, 0.0, 0.0) for n in range(3, 60) if (n + phase) % 2 == 0}
            rows, _ = r.play(r.states["control"], {0: jab, 1: (fr, 60)})
            js = entered(rows, "c", JUMP_SQUAT)
            if js is not None:
                earliest.append(js)
        if not earliest:
            raise OsdError(f"{name}: the CPU never jumped in the probes")
        r.cache["hitstun"] = min(earliest)
    f0 = r.cache["hitstun"]
    rows, text = r.play(r.states["control"], {0: jab, 1: ({f0 + delay: (J, 0.0, 0.0)}, f0 + delay + 1)}, extra=5)
    js = entered(rows, "c", JUMP_SQUAT)
    if js != f0 + delay:
        raise OsdError(f"{name}: jumped on {js}, scripted {f0 + delay}")
    r.expect_osd(name, text, 2, ID_ACT_OOHITSTUN, ["Out of Hitstun", f"Frame {js - f0 + 1}"])
    return js - f0 + 1


def sc_combo_hits(r, name):
    """Mario's down special: several hits, the first without hitstun
    (observed). Each message must count the hits and the damage in memory
    since the hit that began the combo, none missed."""
    rows, text = r.play(r.states["control"], {0: ({n: (B_SPECIAL, 0.0, -1.0) for n in range(2, 32)}, 32)},
                        extra=40)
    dmg = [x["c"]["damage"] for x in rows]
    hits = [n for n in range(1, len(rows)) if dmg[n] > dmg[n - 1]]
    shown = [m[1] for m in r.messages(text, 1, ID_COMBO)]
    got = [int(m.split(" Hits")[0]) for m in shown]
    if len(hits) < 3 or not got:
        raise OsdError(f"{name}: {len(hits)} hits in memory, messages {shown}")
    start = len(hits) - got[-1]
    if got != list(range(got[0], got[-1] + 1)) or start < 0:
        raise OsdError(f"{name}: messages {shown} for {len(hits)} hits")
    for g, msg in zip(got, shown):
        want = f"{g} Hits ({int(dmg[hits[start + g - 1]]) - int(dmg[hits[start] - 1])}%)"
        if msg != want:
            raise OsdError(f"{name}: message {msg}, memory gives {want}")
    if start > 0:
        gap = range(hits[start - 1] + 1, hits[start])
        if not any(rows[n]["c"]["hitstun"] == 0 and rows[n]["c"]["hitlag"] == 0 for n in gap):
            raise OsdError(f"{name}: the combo restarted at hit {start + 1} though the CPU stayed in stun")
    r.results.append((name, f"{len(hits)} hits in memory", shown[-1]))
    r.note(f"{name}: hits on {hits}, damage {dmg[hits[0] - 1]} -> {dmg[hits[-1]]}; messages {shown}")


def sc_sdi_exact(r, offset, name):
    """Forward smash; from `offset` frames after the hit frame the CPU's
    stick takes a new direction on every frame (right, up, right, ...)."""
    fs = ({2: (B_ATTACK | B_SMASH, 1.0, 0.0), 3: (B_ATTACK | B_SMASH, 1.0, 0.0)}, 4)
    if "fsmash" not in r.cache:
        rows, _ = r.play(r.states["control"], {0: fs}, extra=30)
        r.cache["fsmash"] = first(rows, 1, lambda x: x["c"]["hitlag"] > 0 and x["c"]["hitstun"] > 0)
    hit = r.cache["fsmash"]
    dirs = [(1.0, 0.0), (0.0, 1.0)]
    fr = {n: (0, *dirs[(n - hit - offset) % 2]) for n in range(hit + offset, hit + 20)}
    rows, text = r.play(r.states["control"], {0: fs, 1: (fr, hit + 20)}, extra=10)
    if rows[hit]["c"]["hitlag"] == 0:
        raise OsdError(f"{name}: no hitlag on the probe's hit frame {hit}")
    lag = [n for n in range(hit + 1, len(rows)) if rows[n]["c"]["hitlag"] > 0]
    shifts = sum(1 for n in lag if abs(rows[n]["c"]["x"] - rows[n - 1]["c"]["x"]) > 0.01
                 or abs(rows[n]["c"]["y"] - rows[n - 1]["c"]["y"]) > 0.01)
    # A new direction on every hitlag frame after the hit frame.
    inputs = sum(1 for n in lag if n >= hit + max(offset, 1))
    r.expect_osd(name, text, 2, ID_SDI, ["SDI", f"{shifts}/{inputs} Shifts"])
    r.note(f"{name}: hit on {hit}, {len(lag)} hitlag frames after it, {shifts} moved")
    return shifts


def ledge_probe(r):
    """Mario's down smash sends the CPU to the ledge: its first CliffWait
    frame (cached)."""
    if "ledge" not in r.cache:
        ds = ({2: (B_ATTACK | B_SMASH, 0.0, -1.0), 3: (B_ATTACK | B_SMASH, 0.0, -1.0)}, 4)
        rows, _ = r.play(r.states["control"], {0: ds, 1: ({1: (0, 0.0, 0.0)}, 1)}, extra=150)
        wait = entered(rows, "c", CLIFF_WAIT)
        if wait is None:
            raise OsdError("the CPU never reached CliffWait")
        r.cache["ledge"] = (ds, wait)
    return r.cache["ledge"]


def ledgedash_script(wait, drop, dodge_after):
    """Drop (down) on CliffWait frame `drop`, jump on the next frame,
    airdodge toward the stage `dodge_after` frames after the jump."""
    d = wait + drop - 1
    fr = {d: (0, 0.0, -1.0), d + 1: (J, 0.0, 0.0), d + 1 + dodge_after: (B_GUARD, *DOWN_LEFT),
          d + 2 + dodge_after: (0, *DOWN_LEFT)}
    return fr, d + 3 + dodge_after


def sc_ledgedash_perfect(r, name):
    """The perfect ledgedash: drop on the first CliffWait frame P+ allows it
    (3: down on frames 1 and 2 does nothing, checked), jump on the first
    frame after, airdodge on the first frame from which it lands on the
    stage (tried in order). Checked against memory: the CliffWait frame it
    left on, GALINT on its first actionable frame on the ground; and the
    airdodge (angle and cell) is in the plugin's ledgedash line."""
    ds, wait = ledge_probe(r)
    for drop in (1, 2):
        fr, n = ledgedash_script(wait, drop, 2)
        rows, _ = r.play(r.states["control"], {0: ds, 1: (fr, wait + drop)}, extra=2)
        if rows[wait + drop - 1]["c"]["status"] != CLIFF_WAIT:
            raise OsdError(f"{name}: left the ledge on CliffWait frame {drop} "
                           f"({rows[wait + drop - 1]['c']['status']:#x})")
    chosen = None
    for a in range(1, 12):
        fr, n = ledgedash_script(wait, 3, a)
        rows, _ = r.play(r.states["control"], {0: ds, 1: (fr, n)}, extra=15)
        dodge = wait + 3 + a
        if rows[dodge]["c"]["status"] in (ESCAPE_AIR, LANDING_SPECIAL) and LANDING_SPECIAL in [
                x["c"]["status"] for x in rows[dodge:]]:
            chosen = a
            break
    if chosen is None:
        raise OsdError(f"{name}: no airdodge after the jump landed on the stage")
    fr, n = ledgedash_script(wait, 3, chosen)
    rows, text = r.play(r.states["control"], {0: ds, 1: (fr, n)}, extra=60, shot="osds-" + name, osd_player=2,
                        stop_after_osd=False)
    left = first(rows, wait, lambda x: x["c"]["status"] != CLIFF_WAIT)
    landed = first(rows, left, lambda x: not x["c"]["air"] and x["c"]["status"] in FREE) if left else None
    if left != wait + 2 or landed is None:
        raise OsdError(f"{name}: left CliffWait on {left}, actionable on the ground {landed}")
    xlu = rows[landed]["c"]["xlu"]
    galint = xlu if xlu > 0 else -(landed - max(k for k in range(landed) if rows[k]["c"]["xlu"] > 0))
    r.expect_osd(name + "-cliffwait", text, 2, ID_CLIFFWAIT, ["Ledge Hang", "3 Frames"])
    if galint >= -24:
        r.expect_osd(name, text, 2, ID_GALINT, ["GALINT", f"{galint:+d} Frames"])
    else:
        no_osd(r, name, text, 2, ID_GALINT, "over 24 vulnerable frames (no OSD shown)")
    m = LEDGE.findall(text)
    cells = re.findall(r"ledgedash: P2 .* airdodge (\d) cells (\d+)", text)
    dodge = wait + 3 + chosen
    trace = events_by_frame(text, 2)
    r.note(f"{name}: CliffWait from {wait}, drop {wait + 2}, jump {wait + 3}, airdodge {dodge} "
           f"(status after it {rows[dodge]['c']['status']:#x}), actionable on the ground {landed}, GALINT {galint}, "
           f"cells {cells}")
    r.note(f"{name}: status changes per frame: " + "; ".join(f"{f}: {', '.join(v)}" for f, v in sorted(trace.items())
                                                            if any(("->21" in e or "->19" in e) for e in v)))
    if not m or int(m[0][1]) != galint:
        raise OsdError(f"{name}: ledgedash log {m}, GALINT {galint}")
    if not cells or cells[0][0] != "1" or "3" not in cells[0][1] or abs(float(m[0][3]) - 45.0) > 2.0:
        raise OsdError(f"{name}: the airdodge is not in the ledgedash: {m}, cells {cells}")
    return galint


def no_osd(r, name, text, player, wid, why="row off"):
    """No message with id `wid` for `player` (the row is off, or there was
    nothing to report: `why`)."""
    got = r.messages(text, player, wid)
    if got:
        raise OsdError(f"{name}: {why}, but OSD id {wid} showed {got}")
    r.results.append((name, f"{why}: nothing", "nothing"))
    r.note(f"{name}: {why}, no message")


def off_setting(key, off):
    return [(S_OSD[key], "00")] if off else []


def sc_airborne_jump(r, at, special, name, off=False):
    """Short hop, then a neutral air (or neutral special) on airborne frame
    `at` (1: the first frame out of the jump squat)."""
    js = jump_squat(r, 0)
    act = js + at
    fr = {1: (J, 0.0, 0.0), act: (B_SPECIAL if special else B_ATTACK, 0.0, 0.0)}
    rows, text = r.play(r.states["control"], {0: (fr, act)}, extra=10, settings=off_setting("actooairborne", off),
                        shot=None if off else "osds-" + name, osd_player=1)
    st = [x["m"]["status"] for x in rows]
    did = st[act] >= SPECIAL_N if special else st[act] == ATTACK_AIR
    if st[js] != JUMP_SQUAT or not did or any(v != JUMP for v in st[js + 1:act]):
        raise OsdError(f"{name}: statuses from the jump squat {[hex(v) for v in st[js:act + 1]]}")
    if off:
        return no_osd(r, name, text, 1, ID_ACT_OOAIRBORNE)
    n = act - js
    r.expect_osd(name, text, 1, ID_ACT_OOAIRBORNE, [f"{'Special' if special else 'Aerial'} (Jump)", f"Frame {n}"])
    r.check_color(r.shots[-1], 1, GREEN if n == 1 else RED, name)
    return n


def sc_dbljump(r, at, after, name):
    """Short hop, double jump on airborne frame `at`, then a neutral air
    `after` frames after the double jump's frame."""
    js = jump_squat(r, 0)
    dj = js + at
    fr = {1: (J, 0.0, 0.0), dj: (B_JUMP, 0.0, 0.0), dj + after: (B_ATTACK, 0.0, 0.0)}
    rows, text = r.play(r.states["control"], {0: (fr, dj + after)}, extra=10, shot="osds-" + name, osd_player=1,
                        stop_after_osd=False)
    st = [x["m"]["status"] for x in rows]
    if st[dj] != JUMP_AERIAL or st[dj + after] != ATTACK_AIR or any(v != JUMP for v in st[js + 1:dj]):
        raise OsdError(f"{name}: statuses from the jump squat {[hex(v) for v in st[js:dj + after + 1]]}")
    r.expect_osd(name, text, 1, ID_MISC, ["Double Jump (Jump)", f"Frame {dj - js}"])
    r.expect_osd(name + "-aerial", text, 1, ID_ACT_OOAIRBORNE, ["Aerial (DJ)", f"Frame {after}"])
    return dj - js


def sc_aerial_oofall(r, after, name):
    """The CPU drops from the ledge on the first CliffWait frame it can
    (the 3rd) and does a neutral air `after` frames later."""
    ds, wait = ledge_probe(r)
    d = wait + 2
    fr = {d: (0, 0.0, -1.0), d + after: (B_ATTACK, 0.0, 0.0)}
    rows, text = r.play(r.states["control"], {0: ds, 1: (fr, d + after)}, extra=10, shot="osds-" + name,
                        osd_player=2)
    st = [x["c"]["status"] for x in rows]
    if st[d - 1] != CLIFF_WAIT or st[d] != FALL or st[d + after] != ATTACK_AIR \
            or any(v != FALL for v in st[d:d + after]):
        raise OsdError(f"{name}: statuses from the drop {[hex(v) for v in st[d - 1:d + after + 1]]}")
    r.expect_osd(name, text, 2, ID_ACT_OOAIRBORNE, ["Aerial (Fall)", f"Frame {after}"])
    return after


def dashback_outcomes(rows, fr):
    """Dashbacks in memory: a turn from standing straight into TurnDash,
    a slow turn (Turn) that becomes a dash, or a slow turn whose stick
    reached P+'s dash sensitivity (0.73) after the turn's second frame."""
    out, turn, sign = [], None, 1
    for n in range(1, len(rows)):
        s, p = rows[n]["m"]["status"], rows[n - 1]["m"]["status"]
        standing = p in (WAIT, WALK, 0x2) or 0x16 <= p <= 0x19
        x = fr.get(n, (0, 0.0, 0.0))[1]
        if s == TURN_DASH and standing:
            out.append("Dashback")
        elif s == TURN and standing:
            turn, sign = n, (-1 if x < 0 else 1)
        elif turn is not None and p == TURN and s in (DASH, TURN_DASH):
            out.append("Slow Dashback")
            turn = None
        elif turn is not None and s == TURN and n - turn >= 2 and x * sign >= 0.73:
            out.append("Missed Dashback")
            turn = None
        elif turn is not None and s != TURN:
            turn = None
    return out


def sc_dashback(r, fr, length, name, off=False):
    """Mario turns from standing with the script's stick; every outcome
    in memory must have its message, with the success rate so far."""
    rows, text = r.play(r.states["control"], {0: (fr, length)}, extra=8, settings=off_setting("dashback", off),
                        shot=None if off else "osds-" + name, osd_player=1, stop_after_osd=False)
    outcomes = dashback_outcomes(rows, fr)
    if not outcomes:
        raise OsdError(f"{name}: no dashback in memory {[hex(x['m']['status']) for x in rows]}")
    if off:
        return no_osd(r, name, text, 1, ID_DASHBACK)
    want, hits = [], 0
    for k, o in enumerate(outcomes):
        hits += o != "Missed Dashback"
        want.append([o, f"Rate: {hits * 100 // (k + 1)}%"])
    got = r.messages(text, 1, ID_DASHBACK)
    if got != want:
        raise OsdError(f"{name}: messages {got}, memory gives {want}")
    r.results.append((name, " ; ".join(" / ".join(w) for w in want), " ; ".join(" / ".join(g) for g in got)))
    r.note(f"{name}: statuses {[hex(x['m']['status']) for x in rows[:16]]}; messages {got}")
    colour = {"Dashback": GREEN, "Slow Dashback": YELLOW, "Missed Dashback": RED}[outcomes[-1]]
    r.check_color(r.shots[-1], 1, colour, name)
    return outcomes


def shield_hit(r):
    """The frame Mario's jab (on frame 10) hits the CPU's held shield."""
    if "shield-hit" not in r.cache:
        jab, _ = shield_probe(r)
        rows, _ = r.play(r.states["control"], {0: jab, 1: ({n: (B_GUARD, 0.0, 0.0) for n in range(1, 31)}, 30)})
        r.cache["shield-hit"] = first(rows, 1, lambda x: x["c"]["status"] == GUARD_DAMAGE)
    return r.cache["shield-hit"]


def powershield_want(name, n, bit, fresh):
    """The message for a hit on shield frame `n` (1: the frame the shield
    began), from P+'s powershield bit in memory."""
    if fresh and bit != (n <= 2):
        raise OsdError(f"{name}: powershield bit {bit} on shield frame {n} after a fresh press")
    if bit:
        return ["Powershield", f"Frame {n}"]
    return ["Missed PS", "Buffered" if n <= 2 else f"{n - 2}f Early"]


def sc_powershield(r, before, name, off=False):
    """The CPU presses shield `before` frames before Mario's jab hits it
    (0: on the hit frame) and holds it."""
    jab, _ = shield_probe(r)
    hit = shield_hit(r)
    press = hit - before
    fr = {n: (B_GUARD, 0.0, 0.0) for n in range(press, hit + 12)}
    rows, text = r.play(r.states["control"], {0: jab, 1: (fr, hit + 12)}, extra=5,
                        settings=off_setting("powershield", off), shot=None if off else "osds-" + name, osd_player=2)
    st = [x["c"]["status"] for x in rows]
    if st[hit] != GUARD_DAMAGE or st[hit - 1] == GUARD_DAMAGE or any(v != GUARD_ON for v in st[press:hit]):
        raise OsdError(f"{name}: statuses from the press {[hex(v) for v in st[press - 1:hit + 2]]}")
    if off:
        return no_osd(r, name, text, 2, ID_POWERSHIELD)
    want = powershield_want(name, before + 1, rows[hit]["c"]["psbit"], True)
    r.expect_osd(name, text, 2, ID_POWERSHIELD, want)
    r.check_color(r.shots[-1], 2, GREEN if want[0] == "Powershield" else RED, name)
    return want


def sc_powershield_buffered(r, frame_in, name):
    """The CPU shields, lets go, and presses shield again during the shield
    drop and holds it: the shield comes back by itself when the drop ends.
    Mario's jab is moved so that it hits on that shield's frame
    `frame_in`."""
    jab, _ = shield_probe(r)
    hit = shield_hit(r)
    fr = {n: (B_GUARD, 0.0, 0.0) for n in range(1, 6)}
    fr.update({n: (B_GUARD, 0.0, 0.0) for n in range(8, 70)})
    if "shield-again" not in r.cache:
        rows, _ = r.play(r.states["control"], {1: (fr, 60)})
        r.cache["shield-again"] = entered(rows, "c", GUARD_ON, 9)
    again = r.cache["shield-again"]
    if again is None:
        raise OsdError(f"{name}: the shield did not come back")
    hit2 = again + frame_in - 1
    jab2 = ({10 + hit2 - hit: (B_ATTACK, 0.0, 0.0)}, 11 + hit2 - hit)
    rows, text = r.play(r.states["control"], {0: jab2, 1: (fr, hit2 + 10)}, extra=5, shot="osds-" + name,
                        osd_player=2)
    st = [x["c"]["status"] for x in rows]
    if st[hit2] != GUARD_DAMAGE or any(v != GUARD_ON for v in st[again:hit2]) or st[again - 1] == GUARD_ON:
        raise OsdError(f"{name}: shield back on {again}, hit expected on {hit2}: "
                       f"{[hex(v) for v in st[again - 2:hit2 + 2]]}")
    want = powershield_want(name, frame_in, rows[hit2]["c"]["psbit"], False)
    r.expect_osd(name, text, 2, ID_POWERSHIELD, want)
    r.note(f"{name}: shield dropped on 6, pressed again on 8, back by itself on {again}")
    return want


def sc_powershield_laser(r, name):
    """Fox fires a standing laser; Mario presses shield through the real pad so
    the laser hits the 0.76x reflect bubble (RA-Bit[10] set, no GuardDamage;
    P+ PowerShield.asm:36,97-104 and RSBE01.TXT:1266-1281). The Powershield OSD
    must still report the reflect."""
    def shoot(r, i, rows, ctx):
        if i in (0, 1):
            r.emu.hold(1, ["B"])
        elif i == 2:
            r.emu.neutral(1)
    probe, _ = r.run(r.states["control"], 60, shoot)
    hit = next((k for k in range(1, len(probe)) if probe[k]["m"]["damage"] > probe[k - 1]["m"]["damage"] + 0.01), None)
    if hit is None:
        raise OsdError(f"{name}: Fox's laser did not hit Mario in probe")
    # Pad input has ~2f pipeline delay; press so GuardOn starts 0-1 frames before hit.
    press_at = max(hit - 3, 0)

    def inputs(r, i, rows, ctx):
        shoot(r, i, rows, ctx)
        if i == press_at:
            r.emu.hold(0, ["R"])
        elif i == press_at + 10:
            r.emu.neutral(0)
    rows, text = r.run(r.states["control"], hit + 25, inputs, shot="osds-" + name, osd_player=1, stop_after_osd=False)
    guard_on = entered(rows, "m", GUARD_ON)
    if guard_on is None:
        raise OsdError(f"{name}: Mario never entered GuardOn")
    refl = next((k for k in range(guard_on, len(rows)) if (int(rows[k]["m"]["raf"][:8], 16) >> 10) & 1), None)
    if refl is None:
        raise OsdError(f"{name}: RA-Bit[10] never set (hit={hit}, guard_on={guard_on}, "
                       f"dmg={rows[0]['m']['damage']}->{rows[-1]['m']['damage']}, "
                       f"st={[hex(x['m']['status']) for x in rows[guard_on:hit + 4]]})")
    if any(x["m"]["status"] == GUARD_DAMAGE for x in rows[guard_on:]):
        raise OsdError(f"{name}: Mario entered GuardDamage instead of reflecting")
    n = refl - guard_on + 1
    r.note(f"{name}: laser probe hit {hit}, GuardOn {guard_on}, RA-Bit[10] set on {refl} (frame {n})")
    want = ["Powershield", f"Frame {n}"]
    r.expect_osd(name, text, 1, ID_POWERSHIELD, want)
    r.check_color(r.shots[-1], 1, GREEN, name)
    return n


def sc_powershield_early_guard(r, name):
    """Mario throws a forward smash; Fox raises shield through the real pad 6-8
    frames early so it has already transitioned from GuardOn into Guard when the
    attack lands. The OSD must report Missed PS / Nf Early."""
    def fsmash(r, i, rows, ctx):
        if i in (12, 13, 14):
            r.emu.hold(0, ["A"], stick=RIGHT)
        elif i == 15:
            r.emu.neutral(0)

    def probe_inputs(r, i, rows, ctx):
        fsmash(r, i, rows, ctx)
        if i == 0:
            r.emu.hold(1, ["R"])
    probe, _ = r.run(r.states["control"], 60, probe_inputs)
    hit = entered(probe, "c", GUARD_DAMAGE)
    if hit is None:
        raise OsdError(f"{name}: Mario's forward smash did not hit Fox's shield in probe")
    # Pad input has ~2f pipeline delay; hit - 10 puts GuardOn ~8 frames before hit (n = 9, 7f Early).
    press_at = max(hit - 10, 0)

    def inputs(r, i, rows, ctx):
        fsmash(r, i, rows, ctx)
        if i == press_at:
            r.emu.hold(1, ["R"])
        elif i == hit + 8:
            r.emu.neutral(1)
    rows, text = r.run(r.states["control"], hit + 20, inputs, shot="osds-" + name, osd_player=2, stop_after_osd=False)
    guard_on = entered(rows, "c", GUARD_ON)
    got_hit = entered(rows, "c", GUARD_DAMAGE, guard_on or 1)
    if guard_on is None or got_hit is None:
        raise OsdError(f"{name}: guard_on={guard_on}, hit={got_hit}")
    if rows[got_hit - 1]["c"]["status"] != GUARD:
        raise OsdError(f"{name}: status before GuardDamage was {rows[got_hit - 1]['c']['status']:#x}, expected Guard")
    n = got_hit - guard_on + 1
    r.note(f"{name}: GuardOn {guard_on}, hit {got_hit} from Guard (shield frame {n}, {n - 2}f Early)")
    if not (8 <= n <= 10):
        raise OsdError(f"{name}: shield frame {n} not in 8..10 (6-8 frames early)")
    want = ["Missed PS", f"{n - 2}f Early"]
    r.expect_osd(name, text, 2, ID_POWERSHIELD, want)
    r.check_color(r.shots[-1], 2, RED, name)
    return n - 2


US = ({2: (B_ATTACK | B_SMASH, 0.0, 1.0), 3: (B_ATTACK | B_SMASH, 0.0, 1.0)}, 4)
# Both locks: the fighter that is launched is the CPU for the percent locks
# even while the test steers it.
TECH_SETS = [(S_PLAYER_PERCENT, "0032"), (S_PLAYER_LOCK, "01"), (S_CPU_PERCENT, "0032"), (S_CPU_LOCK, "01")]


def tech_run(r, presses, extra_sets=(), shot=None):
    """Mario's up smash sends the CPU (50%, locked) into tumble; it presses
    shield `presses` frames before its landing frame. Returns the rows, the
    log and whether it teched."""
    if "tech-land" not in r.cache:
        rows, _ = r.play(r.states["control"], {0: US, 1: ({1: (0, 0.0, 0.0)}, 150)}, settings=TECH_SETS, extra=5)
        r.cache["tech-land"] = next((k for k in range(10, len(rows)) if rows[k - 1]["c"]["air"]
                                     and not rows[k]["c"]["air"]), None)
        if r.cache["tech-land"] is None:
            raise OsdError("the CPU never landed after the up smash")
    land = r.cache["tech-land"]
    fr = {land - p: (B_GUARD, 0.0, 0.0) for p in presses}
    rows, text = r.play(r.states["control"], {0: US, 1: (fr, land + 2)}, settings=TECH_SETS + list(extra_sets),
                        extra=8, shot=shot, osd_player=2)
    bad = [p for p in presses if p and (rows[land - p]["c"]["hitlag"] > 0 or rows[land - p]["c"]["status"] != 0x45)]
    if bad:
        raise OsdError(f"presses {presses}: {bad} not in Damage_Fly after the hitlag")
    if rows[land - 1]["c"]["air"] is not True or rows[land]["c"]["air"]:
        raise OsdError(f"presses {presses}: the CPU did not land on {land}")
    st = rows[land]["c"]["status"]
    if st not in (PASSIVE, KNOCKDOWN):
        raise OsdError(f"presses {presses}: landed into {st:#x}")
    return rows, text, st == PASSIVE, land


def tech_lockout(r):
    """The shortest gap between two shield presses (the second 6 frames
    before landing) for which the second one techs: the lockout length."""
    if "lockout" not in r.cache:
        allowed = {}
        for gap in range(36, 46):
            _, _, teched, _ = tech_run(r, (6 + gap, 6))
            allowed[gap] = teched
        ok = [g for g in sorted(allowed) if allowed[g]]
        if not ok or any(allowed[g] for g in allowed if g < ok[0]) or not all(allowed[g] for g in allowed if g > ok[0]):
            raise OsdError(f"no clean lockout edge: {allowed}")
        r.cache["lockout"] = ok[0]
        r.note(f"tech lockout: a second press techs from {ok[0]} frames after the first ({allowed})")
    return r.cache["lockout"]


def sc_lockout(r, presses, name, off=False):
    """Shield presses `presses` frames before landing (all after the hitlag).
    The lockout length comes from tech_lockout(); a press counts from the
    press before it, refused or not (the chain scenario checks that against
    the game). If the last press is in the tech window and the lockout
    refused it, the message gives the lockout frames that were left."""
    lock = tech_lockout(r)
    rows, text, teched, land = tech_run(r, presses, off_setting("lockout", off), shot=None if off else "osds-" + name)
    order = sorted(presses)
    last = order[0]
    in_window = last < 20
    refused = in_window and len(order) > 1 and order[1] - last < lock
    if in_window and teched == refused:
        raise OsdError(f"{name}: teched {teched}, but the lockout model says refused {refused}")
    if off or not refused:
        return no_osd(r, name, text, 2, ID_LOCKOUT, "row off" if off else ("teched" if teched else "no refused press"))
    left = lock - (order[1] - last)
    r.expect_osd(name, text, 2, ID_LOCKOUT, ["Tech Lockout", f"{left}f Left"])
    r.check_color(r.shots[-1], 2, RED, name)
    return left


def sc_boostgrab(r, k, upsmash, name, off=False):
    """Mario dashes left, dash attacks on frame 6 and grabs (or up smashes)
    `k` frames later."""
    fr = {n: (0, -1.0, 0.0) for n in range(1, 6)}
    fr[6] = (B_ATTACK, -1.0, 0.0)
    fr[6 + k] = (B_ATTACK | B_SMASH, 0.0, 1.0) if upsmash else (B_GRAB, -1.0, 0.0)
    rows, text = r.play(r.states["control"], {0: (fr, 6 + k)}, extra=8, settings=off_setting("boostgrab", off),
                        shot=None if off else "osds-" + name, osd_player=1)
    da = entered(rows, "m", ATTACK_DASH)
    did = entered(rows, "m", ATTACK_HI4_START if upsmash else CATCH_DASH, da or 1)
    if da != 6:
        raise OsdError(f"{name}: dash attack on {da}")
    if did is None or off:
        return no_osd(r, name, text, 1, ID_BOOST_GRAB, "row off" if off else "no grab out of the dash attack")
    if did != 6 + k:
        raise OsdError(f"{name}: {'up smash' if upsmash else 'grab'} on {did}, scripted {6 + k}")
    n = did - da + 1
    r.expect_osd(name, text, 1, ID_BOOST_GRAB, ["DACUS" if upsmash else "Boost Grab", f"Frame {n}"])
    return n


def f32(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


def sc_grab(r, mash, name, off=False):
    """Mario grabs the CPU, which mashes (A on every other frame, the stick
    from side to side) or not. Each grabbed frame's message must give the
    grab timer in memory and the mash rate since the grab began."""
    fr = {n: ((B_ATTACK if n % 2 == 0 else 0) if mash else 0, (1.0 if n % 2 else -1.0) if mash else 0.0, 0.0)
          for n in range(1, 95)}
    rows, text = r.play(r.states["control"], {0: ({2: (B_GRAB, 0.0, 0.0)}, 3), 1: (fr, 94)}, extra=10,
                        settings=off_setting("grabbreakout", off), shot=None if off else "osds-" + name,
                        osd_player=2, stop_after_osd=False)
    held = [n for n in range(1, len(rows)) if CAPTURE_PULLED <= rows[n]["c"]["status"] <= CAPTURE_DAMAGE]
    if not held or held != list(range(held[0], held[-1] + 1)) or not any(
            x["c"]["status"] == CAPTURE_CUT for x in rows[held[-1]:]):
        raise OsdError(f"{name}: grabbed on {held[:3]}..{held[-3:]}, no grab break after")
    if off:
        return no_osd(r, name, text, 2, ID_GRAB_BREAKOUT)
    start = rows[held[0]]["c"]["clatter"]
    want = []
    for k, n in enumerate(held, 1):
        c = rows[n]["c"]["clatter"]
        want.append(["Grab Escape", f"Timer: {int(f32(c + 0.5))}", f"{f32(f32(start - c) / k):.1f} / frame"])
    got = r.messages(text, 2, ID_GRAB_BREAKOUT)
    if got != want:
        bad = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
        raise OsdError(f"{name}: {len(got)} messages for {len(want)} grabbed frames; first difference at {bad}: "
                       f"{got[bad:bad + 2]} vs {want[bad:bad + 2]}")
    timers = [rows[n]["c"]["clatter"] for n in held]
    r.results.append((name, f"{len(held)} frames, timer {timers[0]:.2f} -> {timers[-1]:.2f}", " / ".join(got[-1])))
    r.note(f"{name}: grabbed on frames {held[0]}-{held[-1]}, timer {[round(t, 2) for t in timers[:6]]}..., "
           f"last message {got[-1]}")
    return len(held)


ESCAPE_F, ESCAPE_B, ITEM_THROW = 0x1F, 0x20, 0x9B


def item_setup(r, states):
    """A second match, Peach (P1) against Fox: Peach pulls a turnip with her
    down special and holds it; saved for the item scenarios."""
    emu = r.emu
    nav.start_training_match(emu, "peach", "fox")
    emu.advance(60)
    r.ft = {"m": r.f.by_player(0), "c": r.f.by_player(1)}
    r.inp = PI.PluginInput(emu)
    r.trace = int(re.findall(r"tracker: trace byte at ([0-9a-f]{8})", emu.log_text())[-1], 16)
    r.cache = {}
    start = os.path.join(states, "osds-peach.sav")
    emu.h.call("save_state", path=start)
    rows, _ = r.play(start, {0: ({1: (B_SPECIAL, 0.0, -1.0)}, 1)}, extra=70)
    st = [x["m"]["status"] for x in rows]
    if not any(v >= SPECIAL_N for v in st) or st[-1] != WAIT:
        raise OsdError(f"Peach's turnip pull: statuses {sorted(set(hex(v) for v in st))}, last {st[-1]:#x}")
    emu.h.call("pause")
    r.states["items"] = os.path.join(states, "osds-items.sav")
    emu.h.call("save_state", path=r.states["items"])
    emu.h.call("resume")
    r.note(f"Peach pulled a turnip (statuses {[hex(v) for v in sorted(set(st))]}); saved")


def sc_glide_toss(r, k, name, off=False):
    """Peach holds a turnip, shields, rolls forward and presses A (with the
    stick forward) `k` frames after the frame the roll began."""
    roll = {1: (B_GUARD, 0.0, 0.0), 2: (B_GUARD, 1.0, 0.0), 3: (B_GUARD, 1.0, 0.0)}
    if "roll" not in r.cache:
        rows, _ = r.play(r.states["items"], {0: (roll, 3)}, extra=5)
        r.cache["roll"] = next((n for n in range(1, len(rows)) if rows[n]["m"]["status"] in (ESCAPE_F, ESCAPE_B)
                                and rows[n - 1]["m"]["status"] not in (ESCAPE_F, ESCAPE_B)), None)
        if r.cache["roll"] is None:
            raise OsdError(f"{name}: no roll: {[hex(x['m']['status']) for x in rows]}")
    esc = r.cache["roll"]
    fr = {n: v for n, v in roll.items() if n <= esc}
    fr[esc + k] = (B_ATTACK, 1.0, 0.0)
    rows, text = r.play(r.states["items"], {0: (fr, esc + k)}, extra=10, settings=off_setting("itemthrow", off),
                        shot=None if off else "osds-" + name, osd_player=1)
    st = [x["m"]["status"] for x in rows]
    if st[esc] not in (ESCAPE_F, ESCAPE_B):
        raise OsdError(f"{name}: roll on {esc} in the probe, now {[hex(v) for v in st[:esc + 2]]}")
    thr = entered(rows, "m", ITEM_THROW, esc)
    toss = thr is not None and st[thr - 1] in (ESCAPE_F, ESCAPE_B)
    r.note(f"{name}: roll on {esc}, A on {esc + k}, throw on {thr}; statuses {[hex(v) for v in st[esc - 1:esc + k + 3]]}")
    if off or not toss:
        if not off and k <= 3:
            raise OsdError(f"{name}: no throw out of the roll")
        return no_osd(r, name, text, 1, ID_ITEM_THROW, "row off" if off else "no throw out of the roll")
    r.expect_osd(name, text, 1, ID_ITEM_THROW, ["Glide Toss", f"Frame {thr - esc + 1}"])
    return thr - esc + 1


# ---- Ice Climbers partner OSD suppression ---------------------------------------


def ics_setup(r, states):
    """A third match, Ice Climbers (P1) against Fox, Fox standing beside
    Popo; Nana read as well (the entry's second instance) with the first
    word of her and Popo's catch part (2: a grab box out)."""
    emu = r.emu
    nav.start_training_match(emu, "ice_climbers", "fox")
    emu.advance(60)
    r.lab.find_settings()
    m, c = r.f.by_player(0), r.f.by_player(1)
    arr = emu.ptr(LT.ENTRY_MANAGER)
    nana = None
    for i in range(min(emu.u32(LT.ENTRY_MANAGER + 4), 9)):
        raw = emu.read(arr + i * LT.ENTRY_SIZE, 0x60)
        if struct.unpack_from(">i", raw, 4)[0] == m["entry"]:
            nana = {"entry": m["entry"], "player": m["player"], "fighter": u32(raw, 0x3C), "owner": m["owner"]}
    if nana is None or not emu.is_ram(nana["fighter"]):
        raise OsdError("Nana not found in the Ice Climbers' entry")
    r.ft = {"m": m, "c": c, "n": nana}

    def part(which):
        mods = r.f.modules(r.ft[which])
        return emu.u32(emu.u32(u32(mods, 0x30) + 0x2C) + 0xC)

    def reads(*_):
        out = {k: r.read(k) for k in ("m", "c", "n")}
        out["mpart"], out["npart"] = part("m"), part("n")
        return out
    r.both = emu.watch(reads)
    r.inp = PI.PluginInput(emu)
    r.trace = int(re.findall(r"tracker: trace byte at ([0-9a-f]{8})", emu.log_text())[-1], 16)
    r.cache = {}
    LT.walk_to(emu, r.lab, m, c, close=10.0)
    emu.advance(30)
    emu.h.call("pause")
    r.states["ics"] = os.path.join(states, "osds-ics.sav")
    emu.h.call("save_state", path=r.states["ics"])
    emu.h.call("resume")
    r.note(f"Ice Climbers: Popo at {r.read('m')['x']:.2f}, Nana at {r.read('n')['x']:.2f}, Fox at "
           f"{r.read('c')['x']:.2f}; saved")


def sc_ics_nana_osd(r, name):
    """Ice Climbers (P1) perform one wavedash and one L-cancelled aerial on the
    real pad (port 1) so Nana copies both ~6 frames later. Nana (partner) must
    not post a second OSD on P1's stack or overwrite Popo's L-cancel rate."""
    wd_stick = (56, 60)  # down-left, away from Fox

    def wd_inputs(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"])
        elif i == 2:
            r.emu.neutral(0)
        if s["status"] == JUMP and "air" not in ctx:
            ctx["air"] = i
        if "air" in ctx and i == ctx["air"] + 1:
            r.emu.hold(0, ["R"], stick=wd_stick)
        elif "air" in ctx and i == ctx["air"] + 4:
            r.emu.neutral(0)
    wd_rows, wd_text = r.run(r.states["ics"], 55, wd_inputs,
                             settings=[(S_OSD["wavedash"], "01"), (S_OSD["lcancel"], "01")],
                             shot="osds-" + name + "-wd", osd_player=1, stop_after_osd=False)
    popo_wd = entered(wd_rows, "m", LANDING_SPECIAL)
    nana_wd = entered(wd_rows, "n", LANDING_SPECIAL)
    wd_msgs = r.messages(wd_text, 1, ID_WAVEDASH)
    r.note(f"{name}: wavedash landings Popo={popo_wd}, Nana={nana_wd}, P1 OSDs={wd_msgs}")
    if popo_wd is None or nana_wd is None:
        raise OsdError(f"{name}: both climbers did not wavedash (Popo={popo_wd}, Nana={nana_wd})")
    if len(wd_msgs) != 1:
        raise OsdError(f"{name}: expected 1 wavedash OSD for P1, got {len(wd_msgs)}: {wd_msgs}")

    def lc_whiff(r, i, rows, ctx):
        s = rows[-1]["m"]
        if i == 0:
            r.emu.hold(0, ["X"], stick=LEFT)
        elif i == 2:
            r.emu.hold(0, stick=LEFT)
        if s["status"] == JUMP and "nair" not in ctx:
            ctx["nair"] = i
            r.emu.hold(0, ["A"])
        elif "nair" in ctx and i == ctx["nair"] + 2:
            r.emu.neutral(0)
    probe, _ = r.run(r.states["ics"], 70, lc_whiff)
    popo_land = entered(probe, "m", LANDING_AIR)
    nana_land = entered(probe, "n", LANDING_AIR)
    if popo_land is None or nana_land is None:
        raise OsdError(f"{name}: both climbers did not land aerial in probe (Popo={popo_land}, Nana={nana_land})")

    def lc_inputs(r, i, rows, ctx):
        lc_whiff(r, i, rows, ctx)
        if i == popo_land - 4:
            r.emu.hold(0, ["R"])
        elif i == popo_land - 2:
            r.emu.neutral(0)
    lc_rows, lc_text = r.run(r.states["ics"], max(popo_land, nana_land) + 15, lc_inputs,
                             settings=[(S_OSD["wavedash"], "01"), (S_OSD["lcancel"], "01")],
                             shot="osds-" + name + "-lc", osd_player=1, stop_after_osd=False)
    lc_msgs = r.messages(lc_text, 1, ID_LCANCEL)
    r.note(f"{name}: L-cancel landings Popo={entered(lc_rows, 'm', LANDING_AIR)}, "
           f"Nana={entered(lc_rows, 'n', LANDING_AIR)}, P1 OSDs={lc_msgs}")
    if len(lc_msgs) != 1:
        raise OsdError(f"{name}: expected 1 L-cancel OSD for P1, got {len(lc_msgs)}: {lc_msgs}")
    r.results.append((name, "1 wavedash OSD, 1 L-cancel OSD", f"{wd_msgs[0]} ; {lc_msgs[0]}"))


# ---- setup ---------------------------------------------------------------------

def menu_setup(r):
    """Lab menu: OSD Menu rows on, Ledgedash HUD on, HMN Color Overlays
    preset ProjectPunch (screenshots of each page)."""
    lab, emu = r.lab, r.emu
    lab.open_menu()
    lab.main_to(LT.MAIN_OSD)
    lab.tap(["A"])
    row = 0
    for key in sorted(OSD_ROWS, key=OSD_ROWS.get):
        lab.down(sum(1 for k in range(row + 1, OSD_ROWS[key] + 1) if k not in OSD_UNBUILT))
        row = OSD_ROWS[key]
        lab.toggle_on(S_OSD[key])
    r.shots.append(emu.screenshot("osds-menu-osd"))
    lab.tap(["B"])  # Draft C rail, on OSDs
    lab.up(LT.MAIN_OSD - LT.MAIN_DISPLAY)  # Display
    lab.tap(["A"])  # Display
    lab.down(DISPLAY_LEDGE_HUD)
    lab.tap(["A"])  # Ledgedash HUD
    lab.toggle_on(S_LEDGE_HUD)
    for offset, want in ((S_LEDGE_MAX, 50), (S_LEDGE_GALINT, 1), (S_LEDGE_PANEL, 1)):
        if lab.setting(offset) != want:
            raise OsdError(f"Ledgedash HUD default at +{offset}: {lab.setting(offset)}, wanted {want}")
    r.shots.append(emu.screenshot("osds-menu-ledgedash-hud"))
    lab.tap(["B"])  # back to Display (on Ledgedash HUD)
    lab.up(DISPLAY_LEDGE_HUD - DISPLAY_HMN_OVERLAYS)
    lab.tap(["A"])  # P1 Overlays
    lab.set_choice(S_OVERLAY[0], 1)
    groups = [lab.setting(S_OVERLAY[0] + 1 + g) for g in range(17)]
    want = [0] * 17
    want[GROUP_WAIT], want[GROUP_DASH], want[GROUP_RUN], want[GROUP_IASA] = COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, \
        COLOR_ORANGE
    if groups != want:
        raise OsdError(f"ProjectPunch preset set {groups}, wanted {want}")
    r.shots.append(emu.screenshot("osds-menu-hmn-overlays"))
    lab.close_menu()


def cmd_test_osds(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-osds")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--aspect", choices=("16:9", "4:3"), default=None,
                    help="the game's screen setting (default: as the save has it)")
    ap.add_argument("--keep-going", action="store_true",
                    help="run the remaining scenarios after one fails (the run still fails)")
    opts = ap.parse_args(rest)
    label = "test-osds"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, step, log_path, shots, notes = "", "boot", "", [], []
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    r = None
    failed = []

    def want(name):
        return not opts.only or name in opts.only

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS, speed=SPEED) as (proc, emu):
            log_path = emu.log_path
            r = Runner(emu, notes, shots)
            try:
                step = "into the match"
                if opts.aspect:
                    nav.boot_to_training_css(emu)
                    LT.set_aspect(emu, opts.aspect, r.note)
                    nav.start_training_match(emu, "mario", "fox")
                else:
                    nav.enter_training_match(emu, "mario", "fox")
                emu.advance(60)
                r.lab.find_settings()
                r.ft = {"m": r.f.by_player(0), "c": r.f.by_player(1)}
                r.inp = PI.PluginInput(emu)
                r.trace = int(re.findall(r"tracker: trace byte at ([0-9a-f]{8})", emu.log_text())[-1], 16)
                step = "lab menu"
                menu_setup(r)
                step = "CPU on Control"
                LT.set_cpu_behaviour(emu, CPU_CONTROL)
                LT.walk_to(emu, r.lab, r.ft["m"], r.ft["c"], close=9.0)
                emu.advance(30)
                r.states = {"control": os.path.join(states, "osds-control.sav")}
                emu.h.call("save_state", path=r.states["control"])
                for name, fn in (
                        ("aerial-oojump-1", lambda: sc_airborne_jump(r, 1, False, "aerial-oojump-1")),
                        ("aerial-oojump-4", lambda: sc_airborne_jump(r, 4, False, "aerial-oojump-4")),
                        ("special-oojump-1", lambda: sc_airborne_jump(r, 1, True, "special-oojump-1")),
                        ("aerial-oojump-off", lambda: sc_airborne_jump(r, 1, False, "aerial-oojump-off", off=True)),
                        ("dbljump-1", lambda: sc_dbljump(r, 1, 1, "dbljump-1")),
                        ("dbljump-3", lambda: sc_dbljump(r, 3, 3, "dbljump-3")),
                        ("aerial-oofall-1", lambda: sc_aerial_oofall(r, 1, "aerial-oofall-1")),
                        ("aerial-oofall-4", lambda: sc_aerial_oofall(r, 4, "aerial-oofall-4")),
                        ("dashback-direct", lambda: sc_dashback(r, {n: (0, -1.0, 0.0) for n in range(1, 13)}, 12,
                                                                "dashback-direct")),
                        ("dashback-slow-turn", lambda: sc_dashback(
                            r, {1: (0, -0.5, 0.0), **{n: (0, -1.0, 0.0) for n in range(2, 13)}}, 12,
                            "dashback-slow-turn")),
                        ("dashback-failed", lambda: sc_dashback(
                            r, {1: (0, -0.5, 0.0), 2: (0, -0.5, 0.0), **{n: (0, -1.0, 0.0) for n in range(3, 15)},
                                **{n: (0, 1.0, 0.0) for n in range(25, 37)}}, 36, "dashback-failed")),
                        ("dashback-off", lambda: sc_dashback(r, {n: (0, -1.0, 0.0) for n in range(1, 13)}, 12,
                                                             "dashback-off", off=True)),
                        ("powershield-1", lambda: sc_powershield(r, 0, "powershield-1")),
                        ("powershield-2", lambda: sc_powershield(r, 1, "powershield-2")),
                        ("powershield-early-1", lambda: sc_powershield(r, 2, "powershield-early-1")),
                        ("powershield-early-3", lambda: sc_powershield(r, 4, "powershield-early-3")),
                        ("powershield-buffered", lambda: sc_powershield_buffered(r, 1, "powershield-buffered")),
                        ("powershield-laser", lambda: sc_powershield_laser(r, "powershield-laser")),
                        ("powershield-early-guard", lambda: sc_powershield_early_guard(r, "powershield-early-guard")),
                        ("powershield-off", lambda: sc_powershield(r, 0, "powershield-off", off=True)),
                        ("lockout-25", lambda: sc_lockout(r, (31, 6), "lockout-25")),
                        ("lockout-last", lambda: sc_lockout(r, (6 + tech_lockout(r) - 1, 6), "lockout-last")),
                        ("lockout-free", lambda: sc_lockout(r, (6 + tech_lockout(r), 6), "lockout-free")),
                        ("lockout-chain", lambda: sc_lockout(r, (60, 30, 6), "lockout-chain")),
                        ("lockout-early-only", lambda: sc_lockout(r, (25,), "lockout-early-only")),
                        ("lockout-off", lambda: sc_lockout(r, (31, 6), "lockout-off", off=True)),
                        ("boostgrab-2", lambda: sc_boostgrab(r, 1, False, "boostgrab-2")),
                        ("boostgrab-3", lambda: sc_boostgrab(r, 2, False, "boostgrab-3")),
                        ("boostgrab-late", lambda: sc_boostgrab(r, 3, False, "boostgrab-late")),
                        ("dacus-3", lambda: sc_boostgrab(r, 2, True, "dacus-3")),
                        ("boostgrab-off", lambda: sc_boostgrab(r, 1, False, "boostgrab-off", off=True)),
                        ("grab-no-mash", lambda: sc_grab(r, False, "grab-no-mash")),
                        ("grab-mash", lambda: sc_grab(r, True, "grab-mash")),
                        ("grab-off", lambda: sc_grab(r, False, "grab-off", off=True)),
                        ("wavedash-1", lambda: sc_wavedash_exact(r, 0, 1, False, "wavedash-1", shot=True)),
                        ("wavedash-2", lambda: sc_wavedash_exact(r, 0, 2, False, "wavedash-2")),
                        ("wavedash-3", lambda: sc_wavedash_exact(r, 0, 3, False, "wavedash-3")),
                        ("wavedash-5", lambda: sc_wavedash_exact(r, 0, 5, False, "wavedash-5")),
                        ("wavedash-full-1", lambda: sc_wavedash_exact(r, 0, 1, True, "wavedash-full-1")),
                        ("wavedash-full-2", lambda: sc_wavedash_exact(r, 0, 2, True, "wavedash-full-2")),
                        ("wavedash-fox-1", lambda: sc_wavedash_exact(r, 1, 1, False, "wavedash-fox-1")),
                        ("wavedash-fox-2", lambda: sc_wavedash_exact(r, 1, 2, False, "wavedash-fox-2")),
                        ("wavedash-fox-full-1", lambda: sc_wavedash_exact(r, 1, 1, True, "wavedash-fox-full-1")),
                        ("wavedash-fox-3", lambda: sc_wavedash_exact(r, 1, 3, False, "wavedash-fox-3")),
                        ("wavedash-pad", lambda: sc_wavedash(r, 2, "wavedash-pad")),
                        ("wavedash-angle-green", lambda: sc_wavedash_angle(r, (0.95, -0.29), "wavedash-angle-green")),
                        ("wavedash-angle-yellow", lambda: sc_wavedash_angle(r, (0.9, -0.45), "wavedash-angle-yellow")),
                        ("wavedash-angle-white", lambda: sc_wavedash_angle(r, (0.7, -0.7), "wavedash-angle-white")),
                        ("osd-position-hud", lambda: sc_osd_position(r, POS_HUD, "osd-position-hud")),
                        ("osd-position-sides", lambda: sc_osd_position(r, POS_SIDES, "osd-position-sides")),
                        ("osd-position-top-left", lambda: sc_osd_position(r, POS_TOP_LEFT, "osd-position-top-left")),
                        ("osd-position-top-right", lambda: sc_osd_position(r, POS_TOP_RIGHT,
                                                                           "osd-position-top-right")),
                        ("osd-theme", lambda: sc_osd_theme(r, "osd-theme")),
                        ("lcancel-landing-frame", lambda: sc_lcancel_exact(r, 0, "lcancel-landing-frame", 0)),
                        ("lcancel-last-airborne", lambda: sc_lcancel_exact(r, 1, "lcancel-last-airborne")),
                        ("lcancel-window-end", lambda: sc_lcancel_exact(r, 6, "lcancel-window-end")),
                        ("lcancel-too-early", lambda: sc_lcancel_exact(r, 7, "lcancel-too-early")),
                        ("lcancel-none", lambda: sc_lcancel_exact(r, None, "lcancel-none", 2)),
                        ("lcancel-pad", lambda: sc_lcancel(r, 4.0, "lcancel-pad")),
                        ("lcancel-hitlag", lambda: sc_lcancel_hitlag(r, "lcancel-hitlag")),
                        ("lcancel-double-press", lambda: sc_lcancel_double_press(r, "lcancel-double-press")),
                        ("lcancel-very-early", lambda: sc_lcancel_very_early(r, "lcancel-very-early")),
                        ("actoolag-1", lambda: sc_actoolag(r, "nair", 0, "actoolag-1", shot=True)),
                        ("actoolag-3", lambda: sc_actoolag(r, "nair", 2, "actoolag-3", shot=True)),
                        ("actoolag-15", lambda: sc_actoolag(r, "nair", 14, "actoolag-15")),
                        ("actoolag-16", lambda: sc_actoolag(r, "nair", 15, "actoolag-16")),
                        ("actoolag-plain", lambda: sc_actoolag(r, "plain", 0, "actoolag-plain")),
                        ("actoolag-autocancel", lambda: sc_actoolag(r, "autocancel", 0, "actoolag-autocancel")),
                        ("actoolag-autocancel-3", lambda: sc_actoolag(r, "autocancel", 2,
                                                                      "actoolag-autocancel-3")),
                        ("actoolag-turn", lambda: sc_actoolag(r, "nair", 3, "actoolag-turn", turn=True)),
                        ("actoolag-autocancel-wait", lambda: sc_actoolag(r, "autocancel", 0,
                                                                         "actoolag-autocancel-wait",
                                                                         from_wait=True)),
                        ("actoolag-wavedash", lambda: sc_actoolag(r, "wavedash", 1, "actoolag-wavedash")),
                        ("actoos-1", lambda: sc_actoos_exact(r, 0, False, "actoos-1")),
                        ("actoos-4", lambda: sc_actoos_exact(r, 3, False, "actoos-4")),
                        ("actoos-drop-1", lambda: sc_actoos_exact(r, 0, True, "actoos-drop-1")),
                        ("actoos-pad", lambda: sc_actoos(r, 4, "actoos-pad")),
                        ("fastfall-1", lambda: sc_fastfall_exact(r, 0, 0, "fastfall-1")),
                        ("fastfall-3", lambda: sc_fastfall_exact(r, 0, 2, "fastfall-3")),
                        ("fastfall-fox-1", lambda: sc_fastfall_exact(r, 1, 0, "fastfall-fox-1")),
                        ("fastfall-pad", lambda: sc_fastfall(r, 3, "fastfall-pad")),
                        ("jumpcancel-1", lambda: sc_jumpcancel_exact(r, 2, False, "jumpcancel-1")),
                        ("jumpcancel-last", lambda: sc_jumpcancel_exact(r, None, False, "jumpcancel-last")),
                        ("jumpcancel-upsmash-1", lambda: sc_jumpcancel_exact(r, 2, True, "jumpcancel-upsmash-1")),
                        ("jumpcancel-pad", lambda: sc_jumpcancel(r, 1, "jumpcancel-pad")),
                        ("combo", lambda: sc_combo(r, "combo")),
                        ("combo-hits", lambda: sc_combo_hits(r, "combo-hits")),
                        ("actoohitstun-1", lambda: sc_actoohitstun_exact(r, 0, "actoohitstun-1")),
                        ("actoohitstun-3", lambda: sc_actoohitstun_exact(r, 2, "actoohitstun-3")),
                        ("actoohitstun-pad", lambda: sc_actoohitstun(r, "actoohitstun-pad")),
                        ("sdi-every-frame", lambda: sc_sdi_exact(r, 0, "sdi-every-frame")),
                        ("sdi-from-second", lambda: sc_sdi_exact(r, 2, "sdi-from-second")),
                        ("sdi-pad", lambda: sc_sdi(r, "sdi-pad")),
                        ("ledgedash-perfect", lambda: sc_ledgedash_perfect(r, "ledgedash-perfect")),
                        ("ledgedash-late", lambda: sc_ledgedash(r, 30, "ledgedash-late")),
                        ("ledgedash-hud-cpu", lambda: sc_ledgedash_hud_cpu(r, "ledgedash-hud-cpu")),
                        ("overlay", lambda: sc_overlay(r, "overlay")),
                        ("overlay-invisible", lambda: sc_invisible_overlay(r, "overlay-invisible")),
                        ("tech-invisible", lambda: sc_tech(r, "tech-invisible")),
                        ("tech-invisible-delay", lambda: sc_tech(r, "tech-invisible-delay", delay=3))):
                    if want(name):
                        step = name
                        if not opts.keep_going:
                            fn()
                            continue
                        try:
                            fn()
                        except (OsdError, PI.InputError) as e:
                            failed.append(f"{name}: {e}")
                            print(f"  FAILED {name}: {e}")
                if failed:
                    raise OsdError(f"{len(failed)} scenarios failed: " + "; ".join(failed))
                step = "finish"
                emu.h.call("load_state", path=r.states["control"])
                emu.advance(10)
                if r.read("m")["subon"] != 1:
                    raise OsdError("the Wait overlay is not on before Finish")
                nav.pause_training(emu)
                emu.advance(5)
                if r.read("m")["subon"] != 0:
                    raise OsdError("the overlay is still on in Training's pause menu")
                nav.finish_training(emu)
                emu.advance(60)
                r.note("overlay cleared when Training paused, before Finish removed the fighters")
                items = [(name, fn) for name, fn in (
                    ("glide-toss-1", lambda: sc_glide_toss(r, 1, "glide-toss-1")),
                    ("glide-toss-3", lambda: sc_glide_toss(r, 3, "glide-toss-3")),
                    ("glide-toss-late", lambda: sc_glide_toss(r, 12, "glide-toss-late")),
                    ("glide-toss-off", lambda: sc_glide_toss(r, 1, "glide-toss-off", off=True))) if want(name)]
                if items:
                    step = "Peach's match"
                    item_setup(r, states)
                    for name, fn in items:
                        step = name
                        fn()
                ics = [(name, fn) for name, fn in (
                    ("ics-nana-osd", lambda: sc_ics_nana_osd(r, "ics-nana-osd")),) if want(name)]
                if ics:
                    if items:
                        nav.finish_training(emu)
                        emu.advance(60)
                    step = "the Ice Climbers' match"
                    ics_setup(r, states)
                    for name, fn in ics:
                        step = name
                        fn()
            except (Timeout, nav.NavError, HarnessError, OsdError, LT.LabError, PI.InputError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    shots.append(emu.screenshot("fail-osds"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(rf"\[{L.tag()}\] (overlays|tracker: \d)", line):
            print("  " + line)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    print()
    print(f"{'scenario':<28} {'expected':<40} shown")
    for name, exp, got in (r.results if r else []):
        print(f"{name:<28} {exp:<40} {got}")
    for path, w, h in shots:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
