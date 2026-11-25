"""Emulator test suite for Grab / Tether / Item-Catch Visualizer and
multi-jumper Fly (0x0D) ledgedash classification (run through emu.py):

  test-grab-visualizer REL [--only NAME ...] [--list] [--keep-going]

One boot on Final Destination, deterministic single-core with D3D rendering.

Scenarios:
  off-noop                grabVisualizer = Off produces 0 draw calls and 0 shapes.
  standard-grab           Mario P1 presses Z (Catch 0x34) with P1 enabled: TMGV
                          reports shapeOk == 1, bubbleOk == 1, grabShapes > 0,
                          totalGrabDrawn > 0, drawCalls > 0; switching to CPU
                          while only P1 grabs reports grabShapes == 0.
  tether-search           Link P1 triggers grounded tether grab and aerial Z-air
                          (plus search/grab boxes): TMGV reports grabShapes > 0
                          and/or searchShapes > 0.
  item-catch              Peach P1 pulls a Turnip (Down+B) and checks grounded
                          and aerial item-pickup boxes: TMGV reports itemShapes > 0
                          and totalItemDrawn > 0.
  multijump-fly-ledgedash Meta Knight drops from ledge, midair jumps with Fly
                          (0x0D), and airdodges onto Final Destination: Ledgedash
                          HUD records a kCellJump ('2') cell.
"""
import argparse
import os
import re
import struct
import time

import charge_tests as CT
import display_options_tests as DO
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
from harness import HarnessError, Timeout

# (gmCharacterKind, CSS icon id) from src/menu/names.cpp and scripts/nav.py.
CHARS = {
    "mario": (0x00, 0x00),
    "link": (0x02, 0x02),
    "samus": (0x03, 0x03),
    "kirby": (0x06, 0x06),
    "fox": (0x07, 0x07),
    "captain_falcon": (0x0A, 0x0A),
    "peach": (0x0D, 0x0D),
    "meta_knight": (0x18, 0x16),
}

# main::Settings offsets (src/main/settings.h).
S_LEDGE_HUD = 12
S_GRAB_VIZ = 51

TMGV_MAGIC = 0x544D4756  # "TMGV"
GRABVIZ_BLOCK_RE = re.compile(r"grabviz: debug block at ([0-9a-f]{8}); shape (ok|off), bubble (ok|off)")
LEDGE_LOG_RE = re.compile(
    r"ledgedash: P(\d) galint (-?\d+) ledge (\d+) angle (-?[\d.]+) airdodge (\d) cells ([0-9]+) no intangibility (\d)"
)

STATUS_FLY = 0x0D
STATUS_FALL = 0x0E
STATUS_ESCAPE_AIR = 0x21
STATUS_CATCH = 0x34
STATUS_CLIFF_WAIT = 0x75
CELL_JUMP = "2"

SCENARIOS = [
    "off-noop",
    "standard-grab",
    "tether-search",
    "item-catch",
    "multijump-fly-ledgedash",
]


class GrabVizError(RuntimeError):
    pass


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def send_pad(emu, buttons=(), stick=(128, 128), cstick=(128, 128), l_analog=0, r_analog=0):
    emu.hold(0, buttons=buttons, stick=stick, cstick=cstick, l=l_analog, r=r_analog)


def clear_pad(emu):
    emu.neutral(0)


def read_tmgv(emu, addr):
    raw = emu.read(addr, 0x2C)
    vals = struct.unpack_from(">11I", raw, 0)
    return {
        "magic": vals[0],
        "shapeOk": vals[1],
        "bubbleOk": vals[2],
        "frame": vals[3],
        "grabShapes": vals[4],
        "searchShapes": vals[5],
        "itemShapes": vals[6],
        "totalGrabDrawn": vals[7],
        "totalSearchDrawn": vals[8],
        "totalItemDrawn": vals[9],
        "drawCalls": vals[10],
    }


class Runner:
    def __init__(self, emu, keep_going=False):
        self.e = emu
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.step_name = "boot"
        self.settings = None
        self.tmgv = None
        self.cur_match = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise GrabVizError(f"{name}: {detail}")

    def set_u8(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFF:02x}")

    def set_u16(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFFFF:04x}")

    def snap(self, name):
        self.set_u8(DO.S_FRAME_ADVANCE, 1)
        step(self.e, 1)
        self.e.h.call("resume")
        self.e.advance(2)
        self.shots.append(self.e.screenshot(name))
        self.e.h.call("pause")
        self.set_u8(DO.S_FRAME_ADVANCE, 0)
        step(self.e, 1)

    def start_match(self, p1_name, cpu_name="fox"):
        e = self.e
        clear_pad(e)
        if nav.scene(e)[0] == "scMelee":
            e.h.call("resume")
            nav.finish_training(e)
        if nav.scene(e)[0] != "scSelctCharacter":
            nav.boot_to_training_css(e)
        pos = len(e.log_text())
        e.mark(f"grabviz: {p1_name} vs {cpu_name}")
        nav.start_quick_match(e, stage="final_destination", p1=CHARS[p1_name], cpu=CHARS[cpu_name], seed_rng=True)
        e.neutral(0)
        lab = LT.Lab(e, [])
        self.settings = lab.find_settings()
        # Keep grabVisualizer Off and CPU standing still on match entry.
        self.set_u8(S_GRAB_VIZ, 0)
        self.set_u8(DO.S_FRAME_ADVANCE, 0)
        for off, val in DO.CPU_STILL.items():
            self.set_u8(off, val)
        self.set_u16(LT.SET_CPU_PERCENT, 0)
        self.set_u8(LT.SET_LOCK_CPU, 1)
        e.h.call("pause")
        step(e, 20)
        hits = GRABVIZ_BLOCK_RE.findall(e.log_text()[pos:])
        if not hits:
            hits = GRABVIZ_BLOCK_RE.findall(e.log_text())
        if not hits:
            raise GrabVizError("no grabviz debug block line in log")
        self.tmgv = int(hits[-1][0], 16)
        self.cur_match = (p1_name, cpu_name)
        p1 = CT.FighterView(e, 0)
        cpu = CT.FighterView(e, 1)
        return p1, cpu


# ---- Scenario 1: Off no-op --------------------------------------------------

def sc_off_noop(t):
    e = t.e
    t.start_match("mario", "fox")
    t.set_u8(S_GRAB_VIZ, 0)
    step(e, 4)

    g0 = read_tmgv(e, t.tmgv)
    t.check(g0["magic"] == TMGV_MAGIC and g0["shapeOk"] == 1 and g0["bubbleOk"] == 1,
            "off-prologues-ok",
            f"magic={g0['magic']:#x} shapeOk={g0['shapeOk']} bubbleOk={g0['bubbleOk']}")

    # Press Z (grab) while visualizer is Off: zero draw calls and zero shapes.
    send_pad(e, buttons=["Z"])
    step(e, 3)
    clear_pad(e)
    peak_grab = peak_search = peak_item = 0
    for _ in range(18):
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_grab = max(peak_grab, g["grabShapes"])
        peak_search = max(peak_search, g["searchShapes"])
        peak_item = max(peak_item, g["itemShapes"])

    g1 = read_tmgv(e, t.tmgv)
    t.check(peak_grab == 0 and peak_search == 0 and peak_item == 0
            and g1["drawCalls"] == 0 and g1["totalGrabDrawn"] == 0
            and g1["totalSearchDrawn"] == 0 and g1["totalItemDrawn"] == 0,
            "off-noop-zero-work",
            f"grab={g1['totalGrabDrawn']} search={g1['totalSearchDrawn']} "
            f"item={g1['totalItemDrawn']} draws={g1['drawCalls']}")


# ---- Scenario 2: Standard grab + Who filter ---------------------------------

def sc_standard_grab(t):
    e = t.e
    if t.cur_match != ("mario", "fox"):
        t.start_match("mario", "fox")
    else:
        step(e, 25)
    p1 = CT.FighterView(e, 0)

    # Enable P1 visualizer and press Z (Catch 0x34).
    t.set_u8(S_GRAB_VIZ, 1)  # P1
    step(e, 2)
    g_before = read_tmgv(e, t.tmgv)
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)

    peak_grab = 0
    saw_catch = False
    shot_taken = False
    for _ in range(24):
        step(e, 1)
        st = p1.status()
        if st == STATUS_CATCH:
            saw_catch = True
        g = read_tmgv(e, t.tmgv)
        peak_grab = max(peak_grab, g["grabShapes"])
        if not shot_taken and g["grabShapes"] > 0:
            shot_taken = True
            t.snap("grabviz-standard-grab")

    g_after = read_tmgv(e, t.tmgv)
    t.check(saw_catch and g_after["shapeOk"] == 1 and g_after["bubbleOk"] == 1
            and peak_grab > 0 and g_after["totalGrabDrawn"] > g_before["totalGrabDrawn"]
            and g_after["drawCalls"] > g_before["drawCalls"],
            "standard-grab-p1-drawn",
            f"sawCatch={saw_catch} peakGrab={peak_grab} "
            f"totalGrab={g_before['totalGrabDrawn']}->{g_after['totalGrabDrawn']} "
            f"draws={g_before['drawCalls']}->{g_after['drawCalls']}")

    # Let Mario recover to Wait, then switch Who = CPU (2) while ONLY P1 grabs.
    step(e, 30)
    t.set_u8(S_GRAB_VIZ, 2)  # CPU only
    step(e, 2)
    g_cpu_before = read_tmgv(e, t.tmgv)
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)

    cpu_peak_grab = 0
    saw_catch_2 = False
    for _ in range(24):
        step(e, 1)
        if p1.status() == STATUS_CATCH:
            saw_catch_2 = True
        g = read_tmgv(e, t.tmgv)
        cpu_peak_grab = max(cpu_peak_grab, g["grabShapes"])

    g_cpu_after = read_tmgv(e, t.tmgv)
    t.check(saw_catch_2 and cpu_peak_grab == 0
            and g_cpu_after["totalGrabDrawn"] == g_cpu_before["totalGrabDrawn"],
            "standard-grab-who-filter-cpu",
            f"sawCatch={saw_catch_2} cpuPeakGrab={cpu_peak_grab} "
            f"totalGrab={g_cpu_before['totalGrabDrawn']}->{g_cpu_after['totalGrabDrawn']}")
    t.set_u8(S_GRAB_VIZ, 0)


# ---- Scenario 3: Tether & Search boxes (Link) -------------------------------

def sc_tether_search(t):
    e = t.e
    p1, _ = t.start_match("link", "fox")
    t.set_u8(S_GRAB_VIZ, 1)  # P1
    step(e, 2)
    g0 = read_tmgv(e, t.tmgv)

    # 1) Grounded tether grab (Z -> Hookshot/Clawshot).
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)
    peak_grab = 0
    peak_search = 0
    shot_taken = False
    for _ in range(45):
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_grab = max(peak_grab, g["grabShapes"])
        peak_search = max(peak_search, g["searchShapes"])
        if not shot_taken and (g["grabShapes"] > 0 or g["searchShapes"] > 0):
            shot_taken = True
            t.snap("grabviz-link-tether")

    # Wait for grab endlag to finish.
    step(e, 35)

    # 2) Aerial Z-air (short-hop + Z -> AirLasso / tether search & attack).
    send_pad(e, buttons=["X"])
    step(e, 2)
    clear_pad(e)
    step(e, 6)
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)
    for _ in range(35):
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_grab = max(peak_grab, g["grabShapes"])
        peak_search = max(peak_search, g["searchShapes"])

    g1 = read_tmgv(e, t.tmgv)
    d_grab = g1["totalGrabDrawn"] - g0["totalGrabDrawn"]
    t.check(peak_grab > 0 and d_grab > 0 and g1["drawCalls"] > g0["drawCalls"],
            "tether-link-grab-drawn",
            f"peakGrab={peak_grab} dGrab={d_grab} draws={g0['drawCalls']}->{g1['drawCalls']}")
    t.set_u8(S_GRAB_VIZ, 0)

    # 3) Kirby Neutral-B (Inhale) activates soCollisionSearchModuleImpl (0x0610).
    t.start_match("kirby", "fox")
    t.set_u8(S_GRAB_VIZ, 1)  # P1
    step(e, 2)
    gs0 = read_tmgv(e, t.tmgv)
    send_pad(e, buttons=["B"])
    peak_search = 0
    search_shot = False
    for fidx in range(32):
        if fidx == 24:
            clear_pad(e)
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_search = max(peak_search, g["searchShapes"])
        if not search_shot and g["searchShapes"] > 0:
            search_shot = True
            t.snap("grabviz-search-box")
    clear_pad(e)
    gs1 = read_tmgv(e, t.tmgv)
    d_search = gs1["totalSearchDrawn"] - gs0["totalSearchDrawn"]
    t.check(peak_search > 0 and d_search > 0 and gs1["drawCalls"] > gs0["drawCalls"],
            "search-box-kirby-inhale-drawn",
            f"peakSearch={peak_search} dSearch={d_search} draws={gs0['drawCalls']}->{gs1['drawCalls']}")
    t.set_u8(S_GRAB_VIZ, 0)


# ---- Scenario 4: Item-Catch / Item-Pickup boxes (Peach Turnip) --------------

def sc_item_catch(t):
    e = t.e
    p1, _ = t.start_match("peach", "fox")
    t.set_u8(S_GRAB_VIZ, 1)  # P1
    step(e, 4)

    g_ground = read_tmgv(e, t.tmgv)
    peak_item_ground = g_ground["itemShapes"]

    # Pull a Turnip (Down+B), then gently toss/drop it and jump-catch (Z).
    send_pad(e, buttons=["B"], stick=(128, 28))
    step(e, 3)
    clear_pad(e)
    step(e, 35)

    # Toss Turnip downward/forward so a live item is on stage, then step & check pickup box.
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)
    peak_item = peak_item_ground
    shot_taken = False
    for _ in range(25):
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_item = max(peak_item, g["itemShapes"])
        if not shot_taken and g["itemShapes"] > 0:
            shot_taken = True
            t.snap("grabviz-item-catch")

    # Also verify aerial item-catch box via jump + Z.
    send_pad(e, buttons=["X"])
    step(e, 2)
    clear_pad(e)
    step(e, 6)
    send_pad(e, buttons=["Z"])
    step(e, 2)
    clear_pad(e)
    peak_item_air = 0
    for _ in range(20):
        step(e, 1)
        g = read_tmgv(e, t.tmgv)
        peak_item_air = max(peak_item_air, g["itemShapes"])
        peak_item = max(peak_item, g["itemShapes"])

    g_end = read_tmgv(e, t.tmgv)
    t.check(peak_item > 0 and g_end["totalItemDrawn"] > 0 and g_end["drawCalls"] > 0,
            "item-catch-peach-drawn",
            f"peakGround={peak_item_ground} peakAir={peak_item_air} "
            f"totalItem={g_end['totalItemDrawn']} draws={g_end['drawCalls']}")
    t.set_u8(S_GRAB_VIZ, 0)


# ---- Scenario 5: Multi-Jumper Fly (0x0D) Ledgedash --------------------------

def sc_multijump_fly_ledgedash(t):
    e = t.e
    p1, _ = t.start_match("meta_knight", "fox")
    t.set_u8(S_LEDGE_HUD, 1)
    step(e, 2)

    # Walk Meta Knight to the left edge of Final Destination (x ~ -85), turn to
    # face right (lr == +1.0, toward the stage), and hop backwards off the lip
    # to grab the left ledge (CliffWait 0x75).
    for _ in range(180):
        if p1.posture()["x"] < -75.0:
            break
        send_pad(e, stick=(78, 128))
        step(e, 1)
    clear_pad(e)
    for _ in range(25):
        if p1.status() == 0x0:
            break
        step(e, 1)
    # Turn right (lr > 0.0, facing the stage) and settle in Wait.
    for _ in range(20):
        if p1.posture()["lr"] > 0.0:
            break
        send_pad(e, stick=(180, 128))
        step(e, 1)
    clear_pad(e)
    for _ in range(20):
        if p1.status() == 0x0:
            break
        step(e, 1)
    # Short-hop backwards (left) off the left edge while facing right, then drift
    # into the ledge grab box.
    send_pad(e, buttons=["X"], stick=(28, 128))
    step(e, 3)
    clear_pad(e)
    grabbed_ledge = False
    for fidx in range(160):
        pos_p1 = p1.posture()
        x, y = pos_p1["x"], pos_p1["y"]
        if p1.status() == STATUS_CLIFF_WAIT:
            grabbed_ledge = True
            break
        if x > -88.0 and y > -2.0:
            send_pad(e, stick=(28, 128))
        elif y < -14.0 and (fidx % 12) < 2:
            send_pad(e, buttons=["X"], stick=(220, 128))
        else:
            send_pad(e, stick=(210, 128))
        step(e, 1)
    clear_pad(e)
    t.check(grabbed_ledge, "fly-ledgedash-grabbed-ledge", f"status={p1.status():#x} posture={p1.posture()}")

    # Hold on ledge briefly, drop (stick down), midair jump (Fly 0x0D), and
    # airdodge down-right onto FD.
    pos = len(e.log_text())
    step(e, 4)
    send_pad(e, stick=(128, 28))
    dropped = False
    for _ in range(15):
        step(e, 1)
        if p1.status() == STATUS_FALL:
            dropped = True
            break
    clear_pad(e)

    # Press X + stick right to trigger multi-jumper Fly (0x0D).
    saw_fly = False
    send_pad(e, buttons=["X"], stick=(228, 128))
    for _ in range(6):
        step(e, 1)
        if p1.status() == STATUS_FLY:
            saw_fly = True
            break
    clear_pad(e)

    # Wait until Meta Knight rises above stage height, then airdodge down-right onto stage.
    for _ in range(10):
        step(e, 1)
        if p1.status() == STATUS_FLY:
            saw_fly = True
        if p1.posture()["y"] > 2.0:
            break
    send_pad(e, buttons=["R"], stick=(220, 60))
    step(e, 3)
    clear_pad(e)

    # Step until ledgedash log line fires (landed + actionable).
    ledge_match = None
    for _ in range(90):
        step(e, 1)
        if p1.status() == STATUS_FLY:
            saw_fly = True
        matches = LEDGE_LOG_RE.findall(e.log_text()[pos:])
        if matches:
            ledge_match = matches[-1]
            break

    cells = ledge_match[5] if ledge_match else ""
    t.check(dropped and saw_fly and ledge_match is not None and CELL_JUMP in cells,
            "fly-ledgedash-records-jump-cell",
            f"dropped={dropped} sawFly={saw_fly} log={ledge_match} cells={cells!r}")


DISPATCH = {
    "off-noop": sc_off_noop,
    "standard-grab": sc_standard_grab,
    "tether-search": sc_tether_search,
    "item-catch": sc_item_catch,
    "multijump-fly-ledgedash": sc_multijump_fly_ledgedash,
}


def cmd_test_grab_visualizer(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-grab-visualizer")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    if opts.list:
        for name in SCENARIOS:
            print(name)
        return 0
    selected = opts.only if opts.only else list(SCENARIOS)
    unknown = [n for n in selected if n not in DISPATCH]
    if unknown:
        E.die(f"unknown scenario(s) {unknown}; available: {SCENARIOS}")

    label = "test-grab-visualizer"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            t = Runner(emu, opts.keep_going)
            try:
                for name in selected:
                    t.step_name = name
                    DISPATCH[name](t)
            except (Timeout, nav.NavError, HarnessError, GrabVizError, DO.DisplayError, LT.LabError,
                    struct.error, KeyError, TypeError, IndexError, ValueError) as exc:
                why = f"{t.step_name}: {type(exc).__name__}: {exc}"
            finally:
                try:
                    emu.h.call("resume")
                    emu.advance(2)
                except (OSError, HarnessError, ValueError, Timeout):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{t.step_name if t else 'boot'}: {exc}"

    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for line in (t.failures if t else []):
        print(f"FAIL {line}")
    for path, w, h in (t.shots if t else []):
        print(f"screenshot: {path} ({w}x{h})")
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and t and t.failures:
        why = f"{len(t.failures)} check(s) failed"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
