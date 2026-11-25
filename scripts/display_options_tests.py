"""Emulator test of Display Options and the General rows Camera Mode, Input
Display, Game Speed and Move Staling (run through emu.py; settings come from
the environment the same way).

  test-display REL [--only NAME ...]
      One boot, Mario (P1) against a Fox CPU on Final Destination, one CPU
      thread. The base state every scenario starts from is made the same
      way on every run: the random number generators are seeded on the
      match's first frame (as match-compare does), the game is stepped to
      a fixed frame, the CPU is handed to the lab's CPU control standing
      still with no DI (Project+'s random DI for AI-run CPUs would move it
      differently each time it is hit), and Mario walks up to it through
      the plugin's script source, one frame at a time. Settings are changed
      by writing the settings block (the menu rows themselves are driven
      once, through the Preset row). Every option is checked in game
      memory, independently of the plugin, and on screenshots (screenshot
      folder, display-*.png). Screenshots compared with each other are
      taken of one frame held still by the task scheduler's stop bit, the
      setting changed in between, so camera, animation and effects are the
      same in both:
        - HUD, Stage Model, Effects: the scene root's layer bits (layer 8,
          0, 4 and 5); the damage readouts and the watermark gone, the stage
          black; the HUD layer shown again while Training's pause menu is
          open; dust and trails gone on the frame after a hit;
        - Move Staling: the attacker's stale queue (owner data +0xCC8) and
          the damage of three jabs played on exact frames;
        - Character Models: the fighters' visibility bytes;
        - Game Speed: gfApplication's frame rate and the game steps per
          rendered frame at each speed, Training's own Speed kept and put
          back, the rate back to Training's while its pause menu is open;
          frame advance at 1/2 speed;
        - Camera Mode: the camera kind (2, 0xD for Zoom, 0xC for Fixed) and
          Training's camera option;
        - Hitboxes: hitbox colours on the frame Mario's forward smash
          connects (found from the CPU's hitlag): Project+'s colour words
          (red hitboxes), hurtboxes for the chosen side only, each fighter's
          place on screen measured by hiding its model; Project+'s colour
          words left intact after the draw;
        - HMN Color Overlays, Wait set through the lab menu: Show Collision
          draws Mario's hurtboxes as Hitboxes HMN does and takes a Green
          off him; Play Sound logs one sound (with the game's handle) each time
          Mario becomes idle, none after Training's pause;
        - Environment Collision: collision lines, with and without the stage;
        - Item Grab Sizes: grey outlines for the side chosen, standing (the
          grounded pickup area in use in memory: area module +0x30, the
          gfArea's byte +0x1C bit 0x80) and blue ones in the air (the air
          catch area in use);
        - Input Display: the CPU's A white from the exact frame the plugin's
          script presses it, the human's Y (and not X) while held on the
          pad; hidden while the lab menu is open;
        - Preset through the menu;
        - everything set non-default, then Finish: the character select and
          the next match have every layer on, the normal frame rate and
          camera, and the same HUD portraits;
        - three CPUs: models and the input display with four fighters;
        - everything drawn at once: on one frame held by frame advance
          (Mario's forward smash connecting, just after a ledgedash) the
          hitboxes, the collision lines, the DI Display's line, both info
          panels, the Ledgedash HUD, the recording HUD and the ledgedash's
          messages; each is also photographed alone, and its pixels must be
          the same in the picture with all of them.
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
import plugin_input as PI
import shots as SH
from harness import HarnessError, Timeout
from plugin_input import B_ATTACK, B_FLICK, B_JUMP, B_SMASH, frames_to_steps

ROOT = 0x80672F40          # gfSceneRoot (g_gfSceneRoot at 0x8059FF80)
LAYER_BYTE, LAYER_STEP, LAYER_ON = 0x399, 0x18, 0x80
APP = 0x8059FFAC           # g_gfApplication; frame rate u16 at +0xF8
FRAME_RATE = 0xF8
CAMERA = 0x805A0278        # CameraController instance; kind at +0x40
CAMERA_KIND = 0x40
TRAINING_SPEED, TRAINING_CAMERA = 0x18, 0x2C
HITBOX_COLOURS = 0x80548464
STALE = 0xCC8
BASE_FRAME = 120           # match frame the base state is built from

# main::Settings (src/main/settings.h).
S_FRAME_ADVANCE = 11
S_CAMERA, S_SPEED, S_STALING = 16, 17, 18
S_HUD, S_HITBOXES, S_MODELS, S_ENV, S_STAGE, S_EFFECTS = 41, 42, 43, 44, 45, 46
S_INPUT_VIEWER, S_HITBOX_COLORS, S_DI_VECTOR = 52, 53, 54
S_CPU_CONTROL, S_CPU_BEHAVIOR, S_CPU_TDI, S_CPU_SDI = 123, 124, 125, 126  # cpu::CpuOptions
# P1 Color Overlays (main::OverlaySettings at +248: preset, then the 17
# groups), its Wait row, and Display page row indices (src/main/ui/pages/display_options.cpp).
S_OVERLAY_HMN, GROUP_WAIT = 248, 8
DISPLAY_MODELS, DISPLAY_HMN_OVERLAYS = 8, 12
SHOW_COLLISION, PLAY_SOUND = 10, 12
OVERLAY_SOUND = re.compile(r"overlays: P(\d) play sound, group (\d+) \(handle (-?\d+)\)")
WAIT, JUMP_SQUAT = 0x0, 0xA
DEFAULTS = {S_CAMERA: 0, S_SPEED: 0, S_STALING: 1, S_HUD: 1, S_HITBOXES: 0,
            S_MODELS: 3, S_ENV: 0, S_STAGE: 1, S_EFFECTS: 1, S_FRAME_ADVANCE: 0,
            S_INPUT_VIEWER: 0, S_HITBOX_COLORS: 0, S_DI_VECTOR: 0}
# CPU Control Lab, Behavior Stand, TDI None, no SDI (cpu_settings.h).
CPU_STILL = {S_CPU_CONTROL: 1, S_CPU_BEHAVIOR: 0, S_CPU_TDI: 4, S_CPU_SDI: 0}
RATES = [60, 50, 40, 30, 15]
STEPS_PER_6 = [6, 5, 4, 3, 1.5]

# Controller display anchors (ui/hud_layout.h padAnchor, shifted into the 5%
# CRT safe area) and button sample offsets in 640x480 space; x offsets are
# squeezed on 16:9.
SLOT_X, SLOT_Y = [450.4, 558.7, 450.4, 558.7], [50.7, 50.7, 149.6, 149.6]
A_OFFSET = (28.9, -3.4)
X_OFFSET = (41.3, -4.7)
Y_OFFSET = (25.0, -14.3)

HUD_BAND = (40, 380, 460, 470)   # the damage readouts (left of the watermark)
WATERMARK = (470, 436, 640, 468)
STAGE_BAND = (0, 330, 640, 420)  # the stage's body below the fighters
# P1's and P2's portraits in the damage readouts (640x480 coordinates, 16:9
# layout): opaque, so they do not depend on what the camera shows behind.
HUD_PORTRAITS = ((66, 398, 92, 426), (212, 398, 238, 426))
FULL = (0, 0, 640, 480)
FIGHTERS = (220, 120, 440, 320)  # Mario and the CPU in the base state, standing

DISPLAY_LINE = re.compile(r"display: [^\r\n]*")


class DisplayError(RuntimeError):
    pass


# ---- pixels (scripts/shots.py's PNG reader: standard library only) ----------------

class Picture:
    def __init__(self, shot):
        self.w, self.h, self.ch, self.rows = SH.read_png(shot[0])

    def px(self, x, y):
        i = x * self.ch
        r = self.rows[y]
        return r[i], r[i + 1], r[i + 2]


_pictures = {}


def picture(shot):
    key = (shot[0], os.path.getmtime(shot[0]))
    if key not in _pictures:
        if len(_pictures) > 8:
            _pictures.clear()
        _pictures[key] = Picture(shot)
    return _pictures[key]


def box(shot, x0, y0, x1, y1):
    """Pixel box of a 640x480-space rectangle."""
    _, w, h = shot
    return int(x0 * w / 640), int(y0 * h / 480), int(x1 * w / 640), int(y1 * h / 480)


def count(shot, rect, test):
    p = picture(shot)
    x0, y0, x1, y1 = box(shot, *rect)
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if test(p.px(x, y)))


def differs(a, b, tol=40):
    return sum(abs(p - q) for p, q in zip(a, b)) > tol


def differing(a, b, rect, tol=40):
    pa, pb = picture(a), picture(b)
    x0, y0, x1, y1 = box(a, *rect)
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if differs(pa.px(x, y), pb.px(x, y), tol))


def changed(base, shot, test=lambda p: True, tol=40, rect=FULL):
    """Pixels that differ from `base` and pass `test` in `shot`, inside
    `rect` (640x480 space): (count, mean x in 640 space)."""
    pa, pb = picture(base), picture(shot)
    _, w, h = shot
    x0, y0, x1, y1 = box(shot, *rect)
    n, sx = 0, 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            q = pb.px(x, y)
            if test(q) and differs(pa.px(x, y), q, tol):
                n += 1
                sx += x
    return n, (sx / n * 640 / w if n else 0.0)


def dark(p):
    return sum(p) < 60


def white(p):
    return min(p) > 215


def red(p):
    return p[0] > 150 and p[1] < 110 and p[2] < 110


def green(p):
    return p[1] > 150 and p[0] < 120 and p[2] < 120


def cyan(p):
    return p[1] > 150 and p[2] > 150 and p[0] < 100


def orange(p):
    return p[0] > 200 and 60 < p[1] < 170 and p[2] < 80


def yellow(p):
    return p[0] > 150 and p[1] > 150 and p[2] < 110


def line_colour(p):
    return green(p) or cyan(p) or orange(p)


class Runner:
    def __init__(self, emu, notes, shots):
        self.emu = emu
        self.lab = LT.Lab(emu, [])
        self.f = self.lab.f
        self.notes = notes
        self.shots = shots
        self.results = []
        self.xscale = 0.75
        self.held = False
        self.suffix = ""

    def note(self, text):
        self.notes.append(text)
        print("  " + text)

    def ok(self, name, detail):
        self.results.append((name, detail))
        self.note(f"ok   {name}: {detail}")

    def check(self, cond, name, detail):
        if not cond:
            raise DisplayError(f"{name}: {detail}")
        self.ok(name, detail)

    # ---- settings and memory --------------------------------------------------

    def set(self, offset, value):
        self.emu.h.call("write_mem", addr=self.lab.settings + offset, hex=f"{value:02x}")

    def defaults(self):
        for offset, value in DEFAULTS.items():
            self.set(offset, value)

    def layer_on(self, n):
        return bool(self.emu.u8(ROOT + LAYER_BYTE + LAYER_STEP * n) & LAYER_ON)

    def layers(self):
        return [int(self.layer_on(n)) for n in range(9)]

    def rate(self):
        return self.emu.u16(self.emu.ptr(APP) + FRAME_RATE)

    def kind(self):
        cc = self.emu.ptr(CAMERA)
        return self.emu.u32(cc + CAMERA_KIND) if cc else None

    def training(self):
        return self.emu.ptr(self.emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU)

    def vis(self, ft):
        mods = self.f.modules(ft)
        return self.emu.u8(struct.unpack_from(">I", mods, 0x58)[0] + 0xE)

    def colour_on(self, ft):
        """The colour-blend module's sub colour switch (+0x14F)."""
        mods = self.f.modules(ft)
        return self.emu.u8(struct.unpack_from(">I", mods, 0xAC)[0] + 0x14F)

    def hitlag(self, ft):
        mods = self.f.modules(ft)
        return struct.unpack(">i", self.emu.read(struct.unpack_from(">I", mods, 0x44)[0] + 0x10, 4))[0]

    def anim(self, ft):
        return self.f.state(ft)["anim"]

    def distance(self):
        return self.f.state(self.c)["x"] - self.f.state(self.m)["x"]

    # ---- time -----------------------------------------------------------------

    def load(self, running=True):
        """The base state; running, or paused on its frame."""
        self.emu.h.call("load_state", path=self.base)
        self.emu.h.call("resume" if running else "pause")
        self.held = False

    def steps(self, ft, frames):
        """Game steps over `frames` rendered frames, from the fighter's
        animation frame (Wait: one per step); emulation left paused."""
        e = self.emu
        e.h.call("pause")
        prev = self.anim(ft)
        pattern = []
        for _ in range(frames):
            e.h.call("frame_advance", n=1)
            a = self.anim(ft)
            d = a - prev
            pattern.append(1 if d < 0 else int(round(d)))
            prev = a
        return pattern

    def pause(self):
        """Training's pause menu, pressing Start until it opens (with Lab
        Menu Button = Start, until the lab menu opens, then Z in it)."""
        pos = len(self.emu.log_text())
        for _ in range(30):
            if nav.training_paused(self.emu):
                return
            if nav.LAB_OPENED_START in self.emu.log_text()[pos:]:
                nav.lab_menu_to_training(self.emu)
                return
            self.emu.press(0, ["START"], frames=8, after=0)
            self.emu.advance(37)
        raise DisplayError("Training's pause menu did not open")

    def resume(self):
        for _ in range(30):
            if not nav.training_paused(self.emu):
                return
            self.emu.press(0, ["START"], frames=8, after=0)
            self.emu.advance(37)
        raise DisplayError("Training did not resume")

    def walk(self, close, frames=240):
        """From a paused game: Mario walks toward the CPU through the
        plugin's script, one frame at a time, until the two are less than
        `close` apart, then stands for 40 frames. Leaves the game paused."""
        sign = 1.0 if self.distance() > 0 else -1.0
        steps = frames_to_steps({k: (0, 0.6 * sign, 0.0) for k in range(1, frames + 1)}, frames)
        self.inp.run({0: steps}, read=lambda i: self.distance(), until=lambda i, rows: abs(rows[-1]) < close)
        self.inp.stop()
        self.emu.h.call("frame_advance", n=40)

    # ---- held pictures --------------------------------------------------------

    def hold(self):
        """Holds the current frame still (the scheduler's stop bit) with
        emulation running, so settings can be changed between screenshots
        of the same frame."""
        nav.hold_game_still(self.emu, True)
        self.emu.h.call("resume")
        self.held = True

    def unhold(self, running=True):
        """Lets the game run again; with running=False emulation is paused
        first, so no game step passes."""
        if not running:
            self.emu.h.call("pause")
        nav.hold_game_still(self.emu, False)
        if running:
            self.emu.h.call("resume")
        self.held = False

    def snap(self, name):
        """Screenshot once a few frames were drawn with the current settings
        (layers and models change on the frame after the setting)."""
        self.emu.advance(4)
        time.sleep(0.2)
        s = self.emu.screenshot(name + self.suffix)
        self.shots.append(s)
        return s

    def shot(self, name):
        s = self.emu.screenshot(name + self.suffix)
        self.shots.append(s)
        return s


def a_button(r, slot, shot, offset=A_OFFSET):
    """Mean colour of a button (the A button by default) of controller
    `slot`: 9 pixels around its centre."""
    s = r.xscale
    cx = 640 - (640 - SLOT_X[slot]) * s + offset[0] * s
    cy = SLOT_Y[slot] + offset[1]
    p = picture(shot)
    _, w, h = shot
    pts = [p.px(int(cx * w / 640) + dx, int(cy * h / 480) + dy) for dx in (-1, 0, 1) for dy in (-2, 0, 2)]
    return tuple(sum(q[i] for q in pts) // len(pts) for i in range(3))


# ---- scenarios ------------------------------------------------------------------

def sc_layers(r):
    e = r.emu
    r.load(running=False)
    r.check(r.layers() == [1] * 9, "layers default", f"layer bits {r.layers()}")
    r.hold()
    on = r.snap("display-hud-on")
    r.set(S_HUD, 0)
    off = r.snap("display-hud-off")
    r.check(not r.layer_on(8) and r.layers()[:8] == [1] * 8, "HUD off: layer 8", f"layer bits {r.layers()}")
    d = differing(on, off, HUD_BAND)
    r.check(d > 1500, "HUD off: readouts gone", f"{d} pixels differ in the HUD band (same frame)")
    wm = differing(on, off, WATERMARK)
    r.check(wm > 200, "HUD off: watermark gone", f"{wm} pixels differ in the watermark rectangle")
    r.unhold()
    r.pause()
    e.advance(20)
    r.check(r.layer_on(8), "HUD off: layer 8 shown in Training's pause", f"layer bits {r.layers()}")
    r.shot("display-hud-off-paused")
    r.resume()
    e.advance(10)
    r.check(not r.layer_on(8), "HUD off: hidden again after the pause", f"layer bits {r.layers()}")
    r.set(S_HUD, 1)
    e.advance(5)
    r.check(r.layers() == [1] * 9, "HUD on again", f"layer bits {r.layers()}")
    # A savestate taken with the HUD off, loaded with it on (and the other
    # way round): the layer follows the setting, not the state.
    for saved, loaded in ((0, 1), (1, 0)):
        r.set(S_HUD, saved)
        e.advance(5)
        pos = len(e.log_text())
        e.press(0, ["DRIGHT"], frames=20, after=10)
        e.wait_for("the savestate", lambda: "savestate: saved" in e.log_text()[pos:], 120, every=2)
        r.set(S_HUD, loaded)
        e.advance(40)  # past the 30 frames of lockout after a save
        pos = len(e.log_text())
        e.press(0, ["DLEFT"], frames=20, after=10)
        e.wait_for("the load", lambda: "savestate: loaded" in e.log_text()[pos:], 120, every=2)
        e.advance(5)
        r.check(r.layer_on(8) == bool(loaded), f"HUD {'on' if loaded else 'off'} after loading a state saved "
                f"with it {'on' if saved else 'off'}", f"layer bits {r.layers()}")
    r.set(S_HUD, 1)
    e.advance(5)
    r.check(r.layers() == [1] * 9, "HUD on after the savestates", f"layer bits {r.layers()}")

    r.set(S_STAGE, 0)
    e.advance(5)
    r.check(not r.layer_on(0), "Stage Model off: layer 0", f"layer bits {r.layers()}")
    st = r.snap("display-stage-off")
    n = count(st, STAGE_BAND, dark)
    total = count(st, STAGE_BAND, lambda p: True)
    r.check(n > 0.85 * total, "Stage Model off: stage black", f"{n} of {total} pixels dark below the fighters")
    r.set(S_STAGE, 1)
    e.advance(5)
    r.check(r.layer_on(0), "Stage Model on again", f"layer bits {r.layers()}")


def smash_until_hit(r, frames=40):
    """From the base state: Mario's forward smash toward the CPU through
    the plugin's script, stepped until the CPU is in hitlag. Returns the
    driven frame of the hit; emulation left paused there."""
    r.load(running=False)
    x = 1.0 if r.distance() > 0 else -1.0
    steps = frames_to_steps({1: (B_ATTACK | B_SMASH, x, 0.0), 2: (B_ATTACK | B_SMASH, x, 0.0)}, 4)
    hit = {}

    def until(i, rows):
        if r.hitlag(r.c) > 0 and "at" not in hit:
            hit["at"] = i
        return "at" in hit
    r.inp.run({0: steps}, extra=frames, until=until)
    if "at" not in hit:
        raise DisplayError("the forward smash did not connect")
    return hit["at"]


def sc_effects(r):
    e = r.emu
    at = smash_until_hit(r)
    e.h.call("frame_advance", n=2)
    r.hold()
    on = r.snap("display-effects-on")
    r.set(S_EFFECTS, 0)
    off = r.snap("display-effects-off")
    r.check(not r.layer_on(4) and not r.layer_on(5), "Effects off: layers 4 and 5", f"layer bits {r.layers()}")
    d = differing(on, off, FULL)
    # The dust and trails go; the hit flash is not drawn on these layers.
    r.check(d > 300, "Effects off: dust and trails gone",
            f"{d} pixels differ on driven frame {at + 2} (hit on {at}), same frame held")
    r.set(S_EFFECTS, 1)
    r.snap("display-effects-on-again")
    r.check(r.layers() == [1] * 9, "Effects on again", f"layer bits {r.layers()}")
    r.unhold()


def sc_hitboxes(r):
    e = r.emu
    colours0 = e.read(HITBOX_COLOURS, 16).hex()
    at = smash_until_hit(r)
    r.hold()
    shots = {}
    for who, name in ((0, "none"), (3, "both"), (1, "hmn"), (2, "cpu")):
        r.set(S_HITBOXES, who)
        shots[name] = r.snap(f"display-hitboxes-{name}")
    words_pplus = e.read(HITBOX_COLOURS, 36).hex()
    r.set(S_HITBOXES, 0)
    r.unhold()
    hit = {k: changed(shots["none"], v, red)[0] for k, v in shots.items() if k != "none"}
    grn = {k: changed(shots["none"], v, green)[0] for k, v in shots.items() if k != "none"}
    r.check(hit["both"] > 300 and hit["hmn"] > 300, "Hitboxes: Mario's red hitboxes on the hit frame "
            "(game held still)", f"hit on driven frame {at}; red pixels added: both {hit['both']}, HMN {hit['hmn']}")
    r.check(grn["both"] < 60 and grn["hmn"] < 60, "Hitbox Colors P+: no green added",
            f"green pixels added: both {grn['both']}, HMN {grn['hmn']}")
    pplus_words = [words_pplus[i:i + 8] for i in range(0, 72, 8)]
    # Ids 0-7; the word for 8 and up reads 0 in Project+ 3.2.1 (measured).
    r.check(len(set(pplus_words[:8])) == 1, "Hitbox Colors P+: Project+'s colour words, one for ids 0-7",
            f"{' '.join(pplus_words)}")
    r.check(hit["cpu"] < 60 and grn["cpu"] < 60, "Hitboxes CPU: none of Mario's hitboxes",
            f"pixels: red {hit['cpu']}, green {grn['cpu']}")
    r.check(e.read(HITBOX_COLOURS, 16).hex() == colours0,
            "Hitboxes: Project+'s colour words unchanged", f"{colours0}")

    # Hurtboxes per side: where each fighter is on screen is measured by
    # hiding its model on the same held frame.
    r.load(running=False)
    r.hold()
    none = r.snap("display-hurtboxes-none")
    r.set(S_MODELS, 1)  # the CPU's model hidden
    cpu_gone = r.snap("display-hurtboxes-cpu-model-hidden")
    r.set(S_MODELS, 2)  # Mario's hidden
    mario_gone = r.snap("display-hurtboxes-hmn-model-hidden")
    r.set(S_MODELS, 3)
    r.snap("display-hurtboxes-models-back")
    vis_ok = r.vis(r.m) == 1 and r.vis(r.c) == 1
    r.set(S_HITBOXES, 1)
    hmn = r.snap("display-hurtboxes-hmn")
    r.set(S_HITBOXES, 2)
    cpu = r.snap("display-hurtboxes-cpu")
    r.set(S_HITBOXES, 0)
    r.unhold()
    n_c, x_c = changed(none, cpu_gone)
    n_m, x_m = changed(none, mario_gone)
    if n_c < 300 or n_m < 300 or abs(x_c - x_m) < 20:
        raise DisplayError(f"fighters not apart on screen: Mario at x {x_m:.0f} ({n_m} px), CPU at x {x_c:.0f} "
                           f"({n_c} px)")
    hm, hx = changed(none, hmn, yellow)
    cm, cx = changed(none, cpu, yellow)
    r.check(hm > 300 and cm > 300 and abs(hx - x_m) < abs(hx - x_c) and abs(cx - x_c) < abs(cx - x_m),
            "Hitboxes: hurtboxes on the chosen side only",
            f"Mario's model at x {x_m:.0f}, the CPU's at {x_c:.0f}; yellow added for HMN {hm} px at x {hx:.0f}, "
            f"for CPU {cm} px at x {cx:.0f}")
    r.check(vis_ok, "Hitboxes: models visible with the models back to Both", "visibility bytes 1")


def overlay_wait(r, value, shot, load=True):
    """HMN Color Overlays > Wait set to `value` through the lab menu, from
    the base state unless `load` is false; the game left running."""
    e, lab = r.emu, r.lab
    if load:
        r.load()
    # Over Training's pause: the overlays wait for the match to run again,
    # where the Start menu's held game keeps applying them while the value
    # steps through the colours.
    nav.pause_training(e)
    lab.open_menu()
    lab.main_to(LT.MAIN_DISPLAY)
    lab.tap(["A"])  # Display
    lab.down(DISPLAY_HMN_OVERLAYS)
    lab.tap(["A"])  # P1 Overlays
    lab.down(1 + GROUP_WAIT)
    lab.set_choice(S_OVERLAY_HMN + 1 + GROUP_WAIT, value)
    r.shot(shot)
    lab.close_menu()
    nav.resume_training(e)


def sc_overlay_collision(r):
    """Wait = Show Collision: Mario idles, so the group applies. His
    hurtboxes must be drawn as Display Options > Hitboxes HMN draws them,
    with no colour on him; with the group off again, none are drawn."""
    e = r.emu
    overlay_wait(r, 2, "display-menu-overlay-green")  # Green first: a colour to take off
    e.advance(20)
    green_on = r.colour_on(r.m)
    overlay_wait(r, SHOW_COLLISION, "display-menu-overlay-collision", load=False)
    # The camera eases back after Training's pause: it must be still for
    # the frame compared with the next one.
    e.advance(120)
    status = r.f.state(r.m)["status"]
    r.hold()
    on = r.snap("display-overlay-collision")
    r.unhold(running=False)
    colour = r.colour_on(r.m)
    r.set(S_OVERLAY_HMN + 1 + GROUP_WAIT, 0)
    e.h.call("frame_advance", n=1)
    r.hold()
    none = r.snap("display-overlay-collision-off")
    r.set(S_HITBOXES, 1)
    hmn = r.snap("display-overlay-collision-hitboxes-hmn")
    r.set(S_HITBOXES, 2)
    cpu = r.snap("display-overlay-collision-hitboxes-cpu")
    r.set(S_HITBOXES, 0)
    r.unhold()
    # Around the two fighters only: Final Destination's background moves
    # between the two frames compared.
    n_on, x_on = changed(none, on, yellow, rect=FIGHTERS)
    n_hmn, x_hmn = changed(none, hmn, yellow, rect=FIGHTERS)
    n_cpu, x_cpu = changed(none, cpu, yellow, rect=FIGHTERS)
    r.check(status == WAIT and green_on == 1 and colour == 0, "Show Collision: Mario in Wait, the colour taken off",
            f"status {status:#x}, sub colour switch {green_on} with Green, {colour} with Show Collision")
    r.check(n_on > 300 and abs(n_on - n_hmn) < 0.3 * n_hmn and abs(x_on - x_hmn) < 15
            and abs(x_on - x_cpu) > abs(x_on - x_hmn) + 20,
            "Show Collision: Mario's hurtboxes drawn, gone with the group off",
            f"yellow added {n_on} px at x {x_on:.0f}; Hitboxes HMN {n_hmn} px at x {x_hmn:.0f}, "
            f"CPU {n_cpu} px at x {x_cpu:.0f} (one frame later, group off)")


def sc_overlay_sound(r):
    """Wait = Play Sound: one sound as Mario idles after the menu closes,
    none while he stays idle or after Training's pause, one more when he
    lands from a short hop and is idle again. The plugin logs each sound
    with the handle the game gave it (-1: refused)."""
    e = r.emu
    pos = len(e.log_text())
    overlay_wait(r, PLAY_SOUND, "display-menu-overlay-sound")
    e.advance(60)
    first = OVERLAY_SOUND.findall(e.log_text()[pos:])
    colour = r.colour_on(r.m)
    r.shot("display-overlay-sound")
    nav.pause_training(e)
    nav.resume_training(e)
    e.advance(30)
    paused = OVERLAY_SOUND.findall(e.log_text()[pos:])
    e.h.call("pause")
    rows = r.inp.run({0: frames_to_steps({1: (B_JUMP | B_FLICK, 0.0, 0.0)}, 1)},
                     read=lambda i: r.f.state(r.m)["status"], extra=90)
    e.h.call("resume")
    e.advance(5)
    sounds = OVERLAY_SOUND.findall(e.log_text()[pos:])
    back = sum(1 for i in range(1, len(rows)) if rows[i] == WAIT and rows[i - 1] != WAIT)
    r.set(S_OVERLAY_HMN + 1 + GROUP_WAIT, 0)
    r.check(len(first) == 1 and len(paused) == 1 and colour == 0, "Play Sound: once as the group applies, not "
            "again while idle or after Training's pause; no colour", f"sounds {first}, then {paused}; sub colour "
            f"switch {colour}")
    r.check(JUMP_SQUAT in rows and rows[-1] == WAIT and back == 1 and len(sounds) == 2
            and all(p == "1" and g == str(GROUP_WAIT) and h != "-1" for p, g, h in sounds),
            "Play Sound: once more when Mario is idle again after a short hop",
            f"Wait entered {back} time(s) in {len(rows)} frames; sounds (player, group, handle) {sounds}")


def sc_collision(r):
    r.load(running=False)
    r.hold()
    off = r.snap("display-collision-off")
    r.set(S_ENV, 1)
    on = r.snap("display-collision-on")
    # Project+'s line colours: cyan solid floor, green walls, orange where a
    # fighter stands (and the ECBs).
    lines, _ = changed(off, on, line_colour)
    r.check(lines > 200, "Environment Collision: coloured lines drawn (game held still)",
            f"{lines} line-coloured pixels added")
    r.set(S_STAGE, 0)
    bare = r.snap("display-collision-stage-off")
    n = count(bare, FULL, line_colour)
    r.check(n > 200, "Environment Collision without the stage", f"{n} line-coloured pixels")
    r.set(S_STAGE, 1)
    r.set(S_ENV, 0)
    r.unhold()


def pickup_in_use(r, ft):
    """The shape flags of `ft`'s item pickup areas (group 4) in use."""
    e = r.emu
    arr = e.u32(e.u32(e.ptr(e.ptr(ft["fighter"] + 0x60) + 0xD8) + 0x90) + 0x30)
    out = []
    for i in range(9):
        data, area = e.u32(arr + 0xC + 0x24 * i), e.u32(arr + 0xC + 0x24 * i + 4)
        if e.is_ram(data) and e.is_ram(area) and (e.u32(data) >> 16) & 0xFF == 4 and e.u8(area + 0x1C) & 0x80:
            out.append(e.u32(data) & 0xFFFF)
    return out


def grey(p):
    return min(p) > 150 and max(p) - min(p) < 30


def blue(p):
    return p[2] > 180 and p[0] < 120 and p[1] < 150


DEAD, REBIRTH, STANDBY = 0xBD, 0xBE, 0x10B


def set_models_menu(r, value):
    """Set Display > Fighter Models through the lab menu (controller path)."""
    lab = r.lab
    lab.open_menu()
    lab.main_to(LT.MAIN_DISPLAY)
    lab.tap(["A"])
    lab.down(DISPLAY_MODELS)  # Fighter Models (row 8)
    lab.set_choice(S_MODELS, value)
    lab.close_menu()


def ko_mario(r):
    """Run Mario off the right edge of Final Destination and fastfall into Dead."""
    e = r.emu
    e.hold(0, stick=(228, 128))
    e.wait_for("Mario off the right edge", lambda: r.f.state(r.m)["y"] < -2.0, 180, every=2)
    e.hold(0, stick=(128, 28))
    e.wait_for("Mario to enter Dead/Standby", lambda: r.f.state(r.m)["status"] in (DEAD, STANDBY), 240, every=2)
    e.neutral(0)


def wait_mario_respawn(r):
    """Wait for Mario to leave Dead/Rebirth and stand on the stage in Wait."""
    e = r.emu
    e.wait_for("Mario to enter Rebirth", lambda: r.f.state(r.m)["status"] == REBIRTH, 180, every=2)
    for _ in range(15):
        if r.f.state(r.m)["status"] != REBIRTH:
            break
        e.press(0, stick=(128, 28), frames=4, after=12)
    e.wait_for("Mario to land in Wait",
               lambda: r.f.state(r.m)["status"] == WAIT and abs(r.f.state(r.m)["y"]) < 1.0, 240, every=2)
    e.advance(5)


def sc_models(r):
    e = r.emu
    r.load(running=False)
    r.hold()
    both = r.snap("display-models-both")
    for who, vm, vc in ((1, 1, 0), (2, 0, 1), (0, 0, 0), (3, 1, 1)):
        r.set(S_MODELS, who)
        shot = r.snap(f"display-models-{who}")
        r.check((r.vis(r.m), r.vis(r.c)) == (vm, vc), f"Character Models {who}",
                f"visibility bytes Mario {r.vis(r.m)}, CPU {r.vis(r.c)}")
        if who == 1:
            d = differing(both, shot, FULL)
            r.check(d > 300, "Character Models HMN: the CPU gone from the picture (same frame)",
                    f"{d} pixels differ")
    r.unhold()
    r.set(S_MODELS, 0)
    e.advance(3)
    r.pause()
    e.advance(10)
    r.note(f"     models None, Training paused: visibility Mario {r.vis(r.m)}, CPU {r.vis(r.c)}")
    r.resume()
    e.advance(5)
    r.check((r.vis(r.m), r.vis(r.c)) == (0, 0), "Character Models None after the pause", "both hidden")
    r.set(S_MODELS, 3)
    e.advance(3)

    # Hiding turned on while the game itself has Mario hidden (Dead), then
    # Mario respawns while hidden, a savestate is taken, and hiding turns off:
    # Mario must be visible again, and loading the savestate must also show him.
    r.load()
    ko_mario(r)
    vis_dead = r.vis(r.m)
    set_models_menu(r, 2)  # CPU: hide Mario while Dead
    e.advance(4)
    st_after_hide = r.f.state(r.m)["status"]
    wait_mario_respawn(r)
    vis_hidden_wait = r.vis(r.m)
    pos = len(e.log_text())
    e.press(0, ["DRIGHT"], frames=20, after=10)
    e.wait_for("the savestate while hidden", lambda: "savestate: saved" in e.log_text()[pos:], 120, every=2)
    set_models_menu(r, 3)  # Both: show Mario again
    e.advance(5)
    vis_after_off = r.vis(r.m)
    shot_after = r.shot("display-models-after-ko")
    e.advance(40)  # past the 30-frame save lockout
    pos = len(e.log_text())
    e.press(0, ["DLEFT"], frames=20, after=10)
    e.wait_for("the savestate load", lambda: "savestate: loaded" in e.log_text()[pos:], 120, every=2)
    e.advance(5)
    vis_after_load = r.vis(r.m)
    r.check(vis_dead == 0 and st_after_hide in (DEAD, STANDBY) and vis_hidden_wait == 0 and vis_after_off == 1
            and vis_after_load == 1,
            "Character Models toggled on during Dead, off after respawn",
            f"vis Dead {vis_dead} (status after hide {st_after_hide:#x}), hidden in Wait {vis_hidden_wait}, "
            f"after off {vis_after_off}, after savestate load {vis_after_load} ({shot_after[0]})")

    # Reverse: hiding turned on while Mario is visible, Mario enters Dead
    # while hidden, then hiding turns off while still Dead: Mario must stay
    # hidden while Dead, then become visible on respawn.
    r.load()
    set_models_menu(r, 2)  # CPU: hide Mario while visible
    e.advance(4)
    vis_hide_alive = r.vis(r.m)
    ko_mario(r)
    set_models_menu(r, 3)  # Both: turn hiding off while still Dead
    e.advance(2)
    st_off_dead = r.f.state(r.m)["status"]
    vis_off_dead = r.vis(r.m)
    wait_mario_respawn(r)
    vis_respawned = r.vis(r.m)
    r.check(vis_hide_alive == 0 and st_off_dead in (DEAD, STANDBY) and vis_off_dead == 0 and vis_respawned == 1,
            "Character Models turned off during Dead keeps Mario hidden until respawn",
            f"hidden alive {vis_hide_alive}, off in Dead (status {st_off_dead:#x}) vis {vis_off_dead}, "
            f"after respawn {vis_respawned}")


def sc_staling(r):
    e = r.emu
    jab = frames_to_steps({1: (B_ATTACK, 0.0, 0.0)}, 3)
    for staling in (1, 0):
        r.load(running=False)
        r.set(S_STALING, staling)
        e.h.call("frame_advance", n=2)
        data = e.ptr(r.m["owner"])
        dmg = []
        for k in range(3):
            # Back in range: every jab pushes the CPU away.
            r.walk(12.0)
            d0 = r.f.damage(r.c)
            r.inp.run({0: jab}, extra=40)
            dmg.append(round(r.f.damage(r.c) - d0, 2))
        queue = e.read(data + STALE + 8, 0x70)
        r.note(f"     staling {staling}: jab damage {dmg}, queue count bytes {queue[:2].hex()}")
        if staling:
            r.check(dmg[0] > dmg[1] > dmg[2] > 0, "Move Staling on: jabs get weaker", f"damage {dmg}")
            r.check(queue[1] != 0, "Move Staling on: queue holds the hits", f"count byte {queue[1]}")
        else:
            r.check(dmg[0] == dmg[1] == dmg[2] and dmg[0] > 0, "Move Staling off: every jab full damage",
                    f"damage {dmg}")
            r.check(not any(queue[:2]), "Move Staling off: queue empty", f"count bytes {queue[:2].hex()}")
    e.h.call("resume")


def sc_speed(r):
    e = r.emu
    r.load()
    for idx in range(5):
        r.set(S_SPEED, idx)
        e.advance(4)
        pat = r.steps(r.m, 60)
        e.h.call("resume")
        got = sum(pat[1:])
        want = STEPS_PER_6[idx] * 59 / 6
        r.check(r.rate() == RATES[idx] and abs(got - want) <= 1.5,
                f"Game Speed {['1', '5/6', '2/3', '1/2', '1/4'][idx]}",
                f"rate {r.rate()}, {got} steps in 59 frames: {''.join(map(str, pat[1:31]))}")
        if idx == 2:
            r.shot("display-speed-2of3")
    # A short Start press opens Training's pause at reduced speeds whichever
    # rendered frame it starts on (the plugin keeps it pressed until a game
    # step has run). Lab Menu Button = Z in Pause, where Start goes to the game.
    menu_button = r.lab.setting(LT.SET_START_OPENS)
    r.set(LT.SET_START_OPENS, 0)
    for idx in (3, 4):
        r.set(S_SPEED, idx)
        e.advance(10)
        opened = []
        for k in range(4):
            e.press(0, ["START"], frames=2, after=0)
            e.advance(12)
            opened.append(nav.training_paused(e))
            if opened[-1]:
                r.set(S_SPEED, 0)
                r.resume()
                r.set(S_SPEED, idx)
            e.advance(7 + k)
        r.check(all(opened), f"Game Speed {['1', '5/6', '2/3', '1/2', '1/4'][idx]}: every 2-frame Start press pauses",
                f"paused after each of 4 presses: {opened}")
    r.set(LT.SET_START_OPENS, menu_button)
    # Training's pause at 2/3: Training's rate while it is open.
    r.set(S_SPEED, 2)
    e.advance(4)
    r.pause()
    e.advance(10)
    r.check(r.rate() == 60, "Game Speed 2/3 in Training's pause: Training's rate", f"rate {r.rate()}")
    # Training's own Speed (2/3 = option 2, rate 40) under ours (5/6).
    r.set(S_SPEED, 0)
    nav.menu_select(e, 0)
    tr = r.training()
    for _ in range(8):
        if e.u32(tr + TRAINING_SPEED) == 2:
            break
        e.press(0, stick=(228, 128), frames=3, after=8)
    r.check(e.u32(tr + TRAINING_SPEED) == 2, "Training's Speed set to 2/3", f"option {e.u32(tr + TRAINING_SPEED)}")
    r.set(S_SPEED, 1)
    r.resume()
    e.advance(10)
    r.check(r.rate() == 50, "Game Speed 5/6 over Training's 2/3", f"rate {r.rate()}")
    r.set(S_SPEED, 0)
    e.advance(5)
    r.check(r.rate() == 40, "Game Speed 1: Training's 2/3 back", f"rate {r.rate()}")
    # Frame advance at 1/2: one press, one step.
    r.load()
    r.set(S_SPEED, 3)
    r.set(S_FRAME_ADVANCE, 1)
    e.advance(30)
    a0 = r.anim(r.m)
    e.advance(20)
    still = r.anim(r.m) == a0
    moved = []
    for _ in range(5):
        a = r.anim(r.m)
        e.press(0, ["L"], frames=3, after=12)
        b = r.anim(r.m)
        # Wait loops: a step from its last frame goes back to frame 0.
        moved.append(round(b - a, 2) if b >= a else (1.0 if b < 1.0 else b - a))
    r.check(still and all(m == 1 for m in moved), "Frame advance at 1/2: one step per press",
            f"held still {still}, steps per press {moved}")
    r.set(S_FRAME_ADVANCE, 0)
    r.set(S_SPEED, 0)
    e.advance(5)


def sc_camera(r):
    e = r.emu
    r.load()
    tr = r.training()
    r.check(r.kind() == 2, "Camera Normal", f"kind {r.kind()}")
    r.set(S_CAMERA, 2)
    e.advance(30)
    r.check(r.kind() == 0xC, "Camera Fixed: MeleeFixed", f"kind {r.kind():#x}")
    r.shot("display-camera-fixed")
    r.pause()
    r.resume()
    e.advance(30)
    r.check(r.kind() == 0xC, "Camera Fixed after Training's pause", f"kind {r.kind():#x}")
    r.set(S_CAMERA, 1)
    e.advance(30)
    r.check(r.kind() == 0xD and e.u32(tr + TRAINING_CAMERA) == 1, "Camera Zoom: Training's Zoom",
            f"kind {r.kind():#x}, Training's option {e.u32(tr + TRAINING_CAMERA)}")
    r.shot("display-camera-zoom")
    r.pause()
    r.resume()
    e.advance(30)
    r.check(r.kind() == 0xD, "Camera Zoom after Training's pause", f"kind {r.kind():#x}")
    r.set(S_CAMERA, 0)
    e.advance(30)
    r.check(r.kind() == 2 and e.u32(tr + TRAINING_CAMERA) == 0, "Camera Normal again",
            f"kind {r.kind():#x}, Training's option {e.u32(tr + TRAINING_CAMERA)}")
    r.set(S_CAMERA, 2)
    e.advance(10)
    r.set(S_CAMERA, 0)
    e.advance(10)
    r.check(r.kind() == 2, "Camera Fixed -> Normal", f"kind {r.kind():#x}")
    # The row itself, through the menu: Lab is the first category of the rail,
    # Camera row 5 of Lab (LT.GEN_CAMERA). The row stops at Fixed.
    lab = r.lab
    r.pause()
    lab.open_menu()
    lab.tap(["A"])
    lab.down(LT.GEN_CAMERA)
    lab.set_choice(S_CAMERA, 2)
    lab.tap(stick=LT.RIGHT)
    stops = lab.setting(S_CAMERA)
    lab.close_menu()
    r.resume()
    e.advance(30)
    r.check(stops == 2 and r.kind() == 0xC, "Camera Mode row: Fixed chosen in the menu, Advanced skipped",
            f"setting {stops}, kind {r.kind():#x}")
    r.pause()
    lab.open_menu()
    lab.tap(["A"])
    lab.down(LT.GEN_CAMERA)
    lab.set_choice(S_CAMERA, 0)
    lab.close_menu()
    r.resume()
    e.advance(30)
    r.check(r.kind() == 2, "Camera Mode row: back to Normal in the menu", f"kind {r.kind():#x}")


def sc_four(r):
    e = r.emu
    r.load()
    nav.set_training_cpus(e, 3)
    # The added fighters' files load in host time, so how many frames that
    # takes depends on how fast the emulator runs: wait for the fighters,
    # not for a fixed number of frames.
    ents = []
    for _ in range(60):
        e.advance(30)
        ents = r.f.entries()
        if len(ents) == 4:
            break
    e.advance(30)
    ents = r.f.entries()
    r.set(S_MODELS, 1)
    r.set(S_HITBOXES, 3)
    e.advance(5)
    vis = {f["player"]: r.vis(f) for f in ents}
    r.check(len(ents) == 4 and vis[0] == 1 and all(vis[p] == 0 for p in (1, 2, 3)),
            "Four fighters: CPU models hidden", f"visibility by player {vis}")
    r.shot("display-four-fighters")
    r.set(S_MODELS, 3)
    r.set(S_HITBOXES, 0)
    nav.set_training_cpus(e, 1)
    e.advance(30)


def sc_restore(r):
    """Everything non-default, then Finish: menus and the next match normal."""
    e = r.emu
    r.load()
    for offset, value in ((S_HUD, 0), (S_STAGE, 0), (S_EFFECTS, 0), (S_MODELS, 0), (S_HITBOXES, 3), (S_ENV, 1),
                          (S_SPEED, 3), (S_CAMERA, 2), (S_STALING, 0)):
        r.set(offset, value)
    e.advance(30)
    r.check(r.layers() == [0, 1, 1, 1, 0, 0, 1, 1, 0] and r.rate() == 30 and r.kind() == 0xC,
            "everything set", f"layers {r.layers()}, rate {r.rate()}, kind {r.kind():#x}")
    r.shot("display-all-set")
    r.pause()
    nav.finish_training(e)
    e.advance(60)
    r.check(r.layers() == [1] * 9 and r.rate() == 60, "Finish: character select with every layer, rate 60",
            f"layers {r.layers()}, rate {r.rate()}")
    r.shot("display-css-after")
    r.defaults()
    for offset in CPU_STILL:
        r.set(offset, 0)
    nav.close_drill_list(e)
    nav.start_training_match(e, "mario", "fox", seed_rng=True)
    nav.pause_at_game_frame(e, BASE_FRAME)
    r.check(r.layers() == [1] * 9 and r.rate() == 60 and r.kind() == 2, "next match normal",
            f"layers {r.layers()}, rate {r.rate()}, kind {r.kind()}")
    r.hold()
    nxt = r.snap("display-next-match")
    r.unhold()
    d = sum(differing(r.first_match, nxt, rect) for rect in HUD_PORTRAITS)
    r.check(d < 40, "next match: HUD as in the first match", f"{d} pixels differ in the two HUD portraits")


# ---- everything drawn at once -------------------------------------------------------

# main::Settings offsets of what the scenario turns on (src/main/settings.h).
S_PLAYER_PERCENT, S_LEDGE_HUD, S_DI, S_OSD_LEDGEDASH = 8, 12, 19, 100
S_HMN_ROWS, S_CPU_ROWS = LT.SET_HMN_INFO + 2, LT.SET_CPU_INFO + 2
S_REC_HMN_MODE = 184 + 2              # recording.hmnMode: 1 = Record
# Info rows that show something for a standing or a hit fighter.
INFO_ROWS = [LT.IV_SYS_L, LT.IV_ENG_L, LT.IV_SELF, LT.IV_KB, LT.IV_SHIELD, LT.IV_JUMPS]
# Beside Final Destination's left ledge (Mario hangs at -87.26, -10.59, the
# Ledgedash drill's measurement) and above the floor's height: the game
# checks the way from the old place to the new one against the stage, and
# this way crosses nothing. Mario, facing the stage, falls and catches it.
LEDGE_SPOT = (-89.26, 6.0)
# What a drawing must add to the held frame, in pixels, to count as shown.
SHOWN = 60
GALINT_LINE = re.compile(r"ledgedash: P1 galint (-?[0-9]+)")


def place(r, ft, x, y):
    posture = struct.unpack_from(">I", r.f.modules(ft), 0xC)[0]
    r.emu.h.call("write_mem", addr=posture + 0xC, hex=struct.pack(">ff", x, y).hex())


def differing_set(a, b, tol=40):
    """The pixels where two pictures of the same size differ."""
    pa, pb = picture(a), picture(b)
    return {(x, y) for y in range(pa.h) for x in range(pa.w) if differs(pa.px(x, y), pb.px(x, y), tol)}


def ledgedash_then_hit(r):
    """From the running base state: Mario is put beside the left ledge,
    catches it, drops, jumps back onto the stage and lands (a ledgedash for
    the trackers: its messages and the Ledgedash HUD follow); the CPU is put
    in front of him and his forward smash connects. Leaves the game held
    by frame advance on that frame, emulation running. Returns the GALINT
    the plugin logged."""
    e = r.emu
    e.neutral(0)
    e.h.call("pause")
    floor = r.f.state(r.m)["y"]
    at = len(e.log_text())
    # Moved while in the air: a standing fighter stays on its floor (moved
    # past its end, Mario was put back on the edge).
    r.inp.run({0: frames_to_steps({1: (PI.B_JUMP, 0.0, 0.0)}, 3)}, extra=8)
    r.inp.stop()
    if r.f.state(r.m)["y"] - floor < 1.0:
        raise DisplayError(f"Mario did not leave the ground: {r.f.state(r.m)}")
    place(r, r.m, *LEDGE_SPOT)
    for _ in range(90):
        e.h.call("frame_advance", n=1)
        if r.f.state(r.m)["status"] == LT.CLIFF_WAIT:
            break
    else:
        raise DisplayError(f"Mario did not catch the ledge from {LEDGE_SPOT}: {r.f.state(r.m)}")
    e.h.call("frame_advance", n=8)
    # Down lets go, then a jump toward the stage, the stick held until he lands.
    back = {1: (0, 0.0, -1.0), 2: (0, 0.0, -1.0), 3: (PI.B_JUMP, 1.0, 0.0), 4: (PI.B_JUMP, 1.0, 0.0)}
    back.update({k: (0, 1.0, 0.0) for k in range(5, 91)})

    def landed(i, rows):
        s = r.f.state(r.m)
        return i > 8 and abs(s["y"] - floor) < 0.01 and s["x"] > LEDGE_SPOT[0] + 4.0
    r.inp.run({0: frames_to_steps(back, 90)}, until=landed)
    r.inp.stop()
    for _ in range(60):
        if r.f.state(r.m)["status"] == 0:
            break
        e.h.call("frame_advance", n=1)
    s = r.f.state(r.m)
    galint = GALINT_LINE.findall(e.log_text()[at:])
    if s["status"] != 0 or abs(s["y"] - floor) > 0.01 or not galint:
        raise DisplayError(f"no ledgedash: Mario at ({s['x']:.1f}, {s['y']:.1f}) status {s['status']:#x}, "
                           f"ledgedash lines {galint}")
    place(r, r.c, s["x"] + 14.0, floor)
    e.h.call("frame_advance", n=2)
    smash = frames_to_steps({1: (B_ATTACK | B_SMASH, 1.0, 0.0), 2: (B_ATTACK | B_SMASH, 1.0, 0.0)}, 4)
    r.inp.run({0: smash}, extra=40, until=lambda i, rows: r.hitlag(r.c) > 0)
    if r.hitlag(r.c) <= 0:
        raise DisplayError("the forward smash after the ledgedash did not connect")
    r.set(S_FRAME_ADVANCE, 1)
    e.h.call("frame_advance", n=3)
    e.h.call("resume")
    return int(galint[-1])


def sc_together(r):
    """Every drawing of the mod on one held frame, each alone and all at
    once: a drawing must not move, vanish or change colour because of what
    was drawn before it."""
    e, lab = r.emu, r.lab
    r.load()
    e.advance(10)
    # Positions, for the recording HUD.
    pos = lab.log_pos()
    r.pause()
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.tap(["A"])
    lab.close_menu()
    r.resume()
    lab.wait_log("the positions to be saved", "record: positions saved [(]", pos, frames=600)
    # Closing the menu tried to save the settings; on the tests' locked card
    # that leaves a message on screen for 240 frames. Let it pass.
    e.advance(300)
    # A hit that sends the CPU flying (Player Percent 100, locked: the lab
    # counts the CPU it controls as a human), and the ledgedash's displays.
    e.h.call("write_mem", addr=lab.settings + S_PLAYER_PERCENT, hex="006401")
    r.set(S_OSD_LEDGEDASH, 1)
    r.set(S_LEDGE_HUD, 1)
    galint = ledgedash_then_hit(r)
    r.set(S_LEDGE_HUD, 0)

    def rows(offset, on):
        e.h.call("write_mem", addr=lab.settings + offset, hex=bytes((INFO_ROWS if on else []) + [0] * 8)[:8].hex())
    switches = {
        "hitboxes": lambda on: r.set(S_HITBOXES, 3 if on else 0),
        "collision": lambda on: r.set(S_ENV, int(on)),
        "DI Display": lambda on: r.set(S_DI, int(on)),
        "HMN info": lambda on: rows(S_HMN_ROWS, on),
        "CPU info": lambda on: rows(S_CPU_ROWS, on),
        "Ledgedash HUD": lambda on: r.set(S_LEDGE_HUD, int(on)),
        "recording HUD": lambda on: r.set(S_REC_HMN_MODE, int(on)),
    }

    def still():
        m, c = r.f.state(r.m), r.f.state(r.c)
        return m["x"], m["y"], m["status"], c["x"], c["y"], c["status"], r.hitlag(r.c)
    still0 = still()
    # The frame with none of them: the game, the watermark and the
    # ledgedash's messages (no setting hides a message once it is posted).
    base = r.snap("display-together-none")
    # The CPU's info panel goes below the human's when both show (alone it
    # takes the first place): it is photographed there, over the human's.
    # `touched` uses a tighter tolerance (8) than `own` (40) so a translucent
    # dark panel drawn over a dark background (such as an OSD lifted by the
    # recording bar over Final Destination's floor) is still recognised as
    # covering another drawing's bright lines underneath it.
    under = {"CPU info": "HMN info"}
    alone, own, touched = {}, {}, {}
    for name, switch in switches.items():
        first = under.get(name)
        if first:
            switches[first](True)
        switch(True)
        alone[name] = r.snap("display-together-" + name.lower().replace(" ", "-"))
        switch(False)
        if first:
            switches[first](False)
        ref = alone[first] if first else base
        own[name] = differing_set(ref, alone[name])
        touched[name] = differing_set(ref, alone[name], 8)
        r.check(len(own[name]) >= SHOWN, f"together: the {name} alone is on the held frame",
                f"{len(own[name])} pixels differ from the frame without it")
    again = r.snap("display-together-none-again")
    drift = len(differing_set(base, again))
    drift8 = len(differing_set(base, again, 8))
    r.check(drift < SHOWN and drift8 < SHOWN and still() == still0,
            "together: the frame stood still for the pictures",
            f"{drift} pixels differ (tol 40), {drift8} (tol 8) between the first and the last picture "
            f"without the drawings (the GALINT {galint} message on both)")
    # Every comparison is made before the scenario fails, so that one run
    # names every drawing that differs.
    bad = []

    def compare(cond, name, detail):
        if cond:
            r.ok(name, detail)
        else:
            bad.append(f"{name}: {detail}")
            r.note(f"FAIL {name}: {detail}")
    # The info display panels step down under the Ledgedash HUD's top band
    # where they would meet it (src/main/ui/hud_layout.h): their own pixels
    # with the bar are taken from a picture of the bar with each of them.
    movers = ("HMN info", "CPU info")
    label = "with the Ledgedash HUD"
    bar = "Ledgedash HUD"
    ctx = dict(own)
    tctx = dict(touched)
    switches[bar](True)
    for name in movers:
        first = under.get(name)
        if first:
            switches[first](True)
        switches[name](True)
        pic = r.snap("display-together-" + bar.lower().replace(" ", "-") + "-" + name.lower().replace(" ", "-"))
        switches[name](False)
        if first:
            switches[first](False)
        ctx[name] = differing_set(alone[bar], pic) - own[bar] - (ctx[first] if first else set())
        tctx[name] = differing_set(alone[bar], pic, 8) - touched[bar] - (tctx[first] if first else set())
        alone[name + " " + label] = pic
    switches[bar](False)
    names = list(switches)
    for n in names:
        switches[n](True)
    both = r.snap("display-together-all-hud")
    for n in names:
        switches[n](False)
    everything = set().union(*(ctx[n] for n in names))
    outside = len(differing_set(base, both) - everything)
    compare(outside < SHOWN, f"together, {label}: nothing changes outside the drawings' own pixels",
            f"{outside} pixels differ from the frame without them, outside all of their pixels")
    for n in names:
        others = set().union(*(tctx[m] for m in names if m != n))
        mine = ctx[n] - others
        pa, pb = picture(alone.get(n + " " + label, alone[n])), picture(both)
        wrong = sum(1 for x, y in mine if differs(pa.px(x, y), pb.px(x, y)))
        compare(len(mine) >= SHOWN // 2 and wrong <= max(10, len(mine) // 50),
                f"together, {label}: the {n} is drawn as it is alone",
                f"{len(mine)} of its {len(ctx[n])} pixels are its own; {wrong} of them differ")
    r.set(S_FRAME_ADVANCE, 0)
    r.set(S_OSD_LEDGEDASH, 0)
    e.h.call("write_mem", addr=lab.settings + S_PLAYER_PERCENT, hex="000000")
    e.advance(5)
    if bad:
        raise DisplayError(f"{len(bad)} comparisons failed: " + "; ".join(bad))


def sc_wave3_viz(r):
    """Wave 3A visual tool backends: Multi-Controller Input Viewer (GCC + Wii),
    P+-native Hitbox Color Modes (Property + Priority), and Live DI Vector Picker
    (pure-GX 2D unit-circle DI compass + predicted trajectory)."""
    e = r.emu
    log = e.log_text()
    m_iv = re.findall(r"input_viewer: debug block at ([0-9a-fA-F]{8})", log)
    m_hc = re.findall(r"hitbox_colors: debug block at ([0-9a-fA-F]{8}); shape (ok|off), bubble (ok|off)", log)
    m_dp = re.findall(r"di: trace byte at [0-9a-fA-F]{8}; picker block at ([0-9a-fA-F]{8});", log)
    r.check(bool(m_iv) and bool(m_hc) and bool(m_dp), "wave3-viz: debug blocks logged",
            f"iv={m_iv[-1:] if m_iv else []}, hc={m_hc[-1:] if m_hc else []}, dp={m_dp[-1:] if m_dp else []}")
    iv_addr = int(m_iv[-1], 16)
    hc_addr = int(m_hc[-1][0], 16)
    dp_addr = int(m_dp[-1], 16)
    r.check(m_hc[-1][1] == "ok" and m_hc[-1][2] == "ok", "wave3-viz: hitbox color shape & bubble hooks ok",
            f"shape={m_hc[-1][1]}, bubble={m_hc[-1][2]}")

    # 1. Input Viewer: GCC live presses + analog stick + trigger bar, plus Wii Classic layout.
    r.load(running=False)
    r.hold()
    iv_off = r.snap("display-wave3-iv-off")
    r.set(S_INPUT_VIEWER, 3)  # Both (P1 + CPU)
    e.hold(0, buttons=["A", "Y", "Z"], stick=(215, 200), cstick=(128, 35), l=120, r=0)
    e.advance(6)
    iv_gc = r.snap("display-wave3-input-viewer-gc")
    iv_raw = e.read(iv_addr, 32)
    iv_magic, iv_cards, _ = struct.unpack_from(">III", iv_raw, 0)
    iv_btn0 = struct.unpack_from(">I", iv_raw, 0x0C)[0]
    iv_sx0 = struct.unpack_from(">b", iv_raw, 0x14)[0]
    iv_sy0 = struct.unpack_from(">b", iv_raw, 0x16)[0]
    iv_lt0 = iv_raw[0x18]
    iv_type0 = iv_raw[0x1C]
    iv_box = (460, 20, 612, 150)
    iv_px, _ = changed(iv_off, iv_gc, rect=iv_box)
    r.check(iv_magic == 0x544D4956 and iv_cards == 2 and iv_type0 == 0
            and (iv_btn0 & 0x0910) == 0x0910 and iv_sx0 > 40 and iv_lt0 >= 90 and iv_px > 500,
            "wave3-viz: Input Viewer GCC live buttons, stick, trigger bar and 2 cards",
            f"cards={iv_cards}, type={iv_type0}, btn={iv_btn0:#x}, stick=({iv_sx0},{iv_sy0}), L={iv_lt0}, px={iv_px}")

    # Wii Classic Controller layout verification & screenshot (overrideType = 1).
    e.h.call("write_mem", addr=iv_addr + 0x1E, hex="01")
    e.hold(0, buttons=["A", "B", "X", "R"], stick=(45, 205), cstick=(210, 128), l=0, r=150)
    e.advance(4)
    iv_wii = r.snap("display-wave3-input-viewer-wii")
    iv_wii_type = e.u8(iv_addr + 0x1C)
    wii_px, _ = changed(iv_gc, iv_wii, rect=iv_box)
    e.h.call("write_mem", addr=iv_addr + 0x1E, hex="ff")
    e.neutral(0)
    r.set(S_INPUT_VIEWER, 0)
    r.unhold()
    r.check(iv_wii_type == 1 and wii_px > 200, "wave3-viz: Input Viewer Wii Classic layout renders cleanly",
            f"type={iv_wii_type}, diff_vs_gc={wii_px} px")

    # 2. Hitbox Color Modes: Mario's multi-part F-Smash (Sweetspot vs Sourspot in Property; ID 0..2 in Priority).
    at = smash_until_hit(r)
    r.hold()
    r.set(S_HITBOXES, 3)  # Both fighters so GX state restore across fighter loop is verified too!
    r.set(S_HITBOX_COLORS, 0)
    hc_def = r.snap("display-wave3-hitbox-default")
    r.set(S_HITBOX_COLORS, 1)  # Property
    hc_prop = r.snap("display-wave3-hitbox-property")
    hc_raw_p = e.read(hc_addr, 0x3C)
    hc_parts_p = struct.unpack_from(">I", hc_raw_p, 0x10)[0]
    hc_cols_p = list(struct.unpack_from(">8I", hc_raw_p, 0x1C))[:min(hc_parts_p, 8)]

    r.set(S_HITBOX_COLORS, 2)  # Priority
    hc_prio = r.snap("display-wave3-hitbox-priority")
    hc_raw_r = e.read(hc_addr, 0x3C)
    hc_parts_r = struct.unpack_from(">I", hc_raw_r, 0x10)[0]
    hc_cols_r = list(struct.unpack_from(">8I", hc_raw_r, 0x1C))[:min(hc_parts_r, 8)]

    prop_diff, _ = changed(hc_def, hc_prop, rect=FIGHTERS)
    prio_diff, _ = changed(hc_prop, hc_prio, rect=FIGHTERS)
    r.check(hc_parts_p >= 2 and len(set(hc_cols_p)) >= 2 and prop_diff > 150,
            "wave3-viz: Hitbox Colors Property distinguishes multi-part attack hitboxes",
            f"frame={at}, parts={hc_parts_p}, colors={[hex(c) for c in hc_cols_p]}, diff={prop_diff} px")
    r.check(hc_parts_r >= 2 and len(set(hc_cols_r)) >= 2 and prio_diff > 100,
            "wave3-viz: Hitbox Colors Priority assigns distinct colors per priority ID",
            f"parts={hc_parts_r}, colors={[hex(c) for c in hc_cols_r]}, diff={prio_diff} px")

    # 3. Live DI Vector Picker: pure-GX 2D unit-circle DI compass + predicted trajectory during hitlag.
    r.set(S_HITBOXES, 0)
    r.set(S_HITBOX_COLORS, 0)
    di_off = r.snap("display-wave3-di-off")
    r.set(S_DI, 1)
    r.set(S_DI_VECTOR, 1)  # No DI
    di_none = r.snap("display-wave3-di-none")
    r.set(S_DI_VECTOR, 8)  # Survival
    e.hold(0, stick=(210, 60))
    e.advance(4)
    di_surv = r.snap("display-wave3-di-picker")
    dp_raw = e.read(dp_addr, 32)
    dp_magic, dp_drawn, _, dp_mode, dp_lx, dp_ly, dp_dx, dp_dy = struct.unpack_from(">IIIIffff", dp_raw, 0)
    dot = abs(dp_lx * dp_dx + dp_ly * dp_dy)
    compass_box = (518, 20, 612, 112)
    compass_px, _ = changed(di_off, di_surv, rect=compass_box)
    arc_px, _ = changed(di_none, di_surv, rect=(160, 40, 500, 360))
    e.neutral(0)
    r.set(S_DI, 0)
    r.set(S_DI_VECTOR, 0)
    r.unhold()
    r.check(dp_magic == 0x544D4450 and dp_drawn == 1 and dp_mode == 8 and dot < 0.05 and compass_px > 250
            and arc_px > 40,
            "wave3-viz: DI Vector Picker compass and Survival trajectory active",
            f"drawn={dp_drawn}, mode={dp_mode}, launch=({dp_lx:.2f},{dp_ly:.2f}), di=({dp_dx:.2f},{dp_dy:.2f}), "
            f"dot={dot:.3f}, compass_px={compass_px}, arc_px={arc_px}")


SCENARIOS = ["layers", "effects", "hitboxes", "overlay-collision", "overlay-sound", "collision", "models",
             "staling", "speed", "camera", "wave3-viz", "together", "four", "restore"]


def make_base(r, states, aspect=None):
    """The base state, made the same way on every run (see the module
    docstring), on the screen setting `aspect` (None: as the save has it).
    Leaves the game paused on it."""
    e = r.emu
    nav.boot_to_training_css(e)
    if aspect:
        LT.set_aspect(e, aspect, r.note)
    nav.start_training_match(e, "mario", "fox", seed_rng=True)
    nav.pause_at_game_frame(e, BASE_FRAME)
    r.lab.find_settings()
    m = re.findall(r"lab: screen (16:9|4:3)", e.log_text())
    r.xscale = 0.75 if m and m[-1] == "16:9" else 1.0
    log = e.log_text()
    for what in ("layers ok, staling ok, camera ok", "boxes ok, collision ok"):
        if what not in log:
            raise DisplayError(f"the plugin did not log '{what}': {DISPLAY_LINE.findall(log)}")
    r.ok("game and Project+ routines as expected", "layers, staling, camera, boxes, collision")
    r.m, r.c = r.f.by_player(0), r.f.by_player(1)
    r.inp = PI.PluginInput(e)
    for offset, value in DEFAULTS.items():
        if r.lab.setting(offset) != value:
            raise DisplayError(f"default at +{offset}: {r.lab.setting(offset)}, wanted {value}")
    r.ok("defaults", "every row at its default")
    r.hold()
    r.first_match = r.snap("display-first-match")
    r.unhold(running=False)
    for offset, value in CPU_STILL.items():
        r.set(offset, value)
    e.h.call("frame_advance", n=30)
    r.walk(16.0)
    r.base = os.path.join(states, "display-base" + r.suffix + ".sav")
    e.h.call("save_state", path=r.base)
    r.ok("base state", f"Mario at x {r.f.state(r.m)['x']:.2f}, the CPU at {r.f.state(r.c)['x']:.2f}")


def cmd_test_display(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-display")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--aspect", choices=("16:9", "4:3"), help="the game's screen setting (default: as the save has it)")
    opts = ap.parse_args(rest)
    label = "test-display"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, step, log_path, shots, notes = "", "boot", "", [], []
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    r = None
    fns = {"layers": sc_layers, "effects": sc_effects, "hitboxes": sc_hitboxes,
           "overlay-collision": sc_overlay_collision, "overlay-sound": sc_overlay_sound, "collision": sc_collision,
           "models": sc_models, "staling": sc_staling, "speed": sc_speed, "camera": sc_camera,
           "wave3-viz": sc_wave3_viz, "together": sc_together, "four": sc_four, "restore": sc_restore}
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS) as (proc, emu):
            log_path = emu.log_path
            r = Runner(emu, notes, shots)
            r.suffix = "-4x3" if opts.aspect == "4:3" else ""
            try:
                step = "base state"
                make_base(r, states, opts.aspect)
                for name in SCENARIOS:
                    if not opts.only or name in opts.only:
                        step = name
                        fns[name](r)
                        log = emu.log_text()
                        refused = [x for x in DISPLAY_LINE.findall(log) if "not as expected" in x]
                        if refused:
                            raise DisplayError(f"a display routine was refused: {refused}")
            except (Timeout, nav.NavError, HarnessError, DisplayError, LT.LabError, PI.InputError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    shots.append(emu.screenshot("fail-display"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(rf"\[{L.tag()}\] display", line):
            print("  " + line)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    print()
    for name, detail in (r.results if r else []):
        print(f"ok  {name}: {detail}")
    for path, w, h in shots:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
