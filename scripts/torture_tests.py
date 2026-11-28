"""11-category live Dolphin torture & adversarial stress suite (run through emu.py).

  make test-torture [TORTURE_ARGS="--bucket 1" | "--case cat1-savestate-record-swap-chaos" | "--list"]

Designed to stress-test Wii/Dolphin failure modes across 4 parallelizable
buckets (up to 3 isolated Dolphin user dirs simultaneously with zero SD collisions):

  Bucket 1 (Headless, read-only SD):
    cat1-savestate-record-swap-chaos  50+ rapid save/load cycles (hitlag, grab,
                                      shieldstun, recording playback, IC, Olimar
                                      6-Pikmin, Snake C4/grenades) + 15+ fighter
                                      & costume swaps with all overlays/OSDs ON.
    cat5-long-soak-combat             3,600+ continuous active combat frames with
                                      Adaptive CPU + PPlusAI + Heatmap + Frame
                                      Meter + Combo Trainer + Info + all OSDs;
                                      asserts flat memory across start/mid/end.
    cat6-transform-tether-multiactor  Zelda<->Sheik transforms, IC Nana KO & zero
                                      Nana double-OSD spam, Olimar 6-Pikmin/D-Pad
                                      presets, Snake C4/Cypher/grenades, Lucas/
                                      Samus/Ivysaur tether Frame Meter cells, and
                                      Wario/Diddy/Dedede/ROB item fighters.

  Bucket 2 (Strict Wii MMU/LLE, read-only SD):
    cat4-strict-mode-menu-drill-hop   16:9 & 4:3 under MMU=True / Fastmem=False /
                                      DSPHLE=False (LLE): all 4 Boot tabs, Z
                                      Credits, Y Quick Setup, Browser, Scenarios,
                                      Tech Practice, BTT, Float/Hover, Snake C4,
                                      Lab scroll/Credits/double-Y/Colorblind.
    cat10-scene-loop-vs4p-mmu         10 full consecutive Training -> CSS -> BTT
                                      -> CSS -> Offline VS 4P (Displays ON) ->
                                      CSS -> Training loops under MMU=True.

  Bucket 3 (D3D Render + Freeze/Modal Races + 4-Controller Hybrid, read-only SD):
    cat7-freeze-fa-modal-race         Lab Menu, Credits, Brawl Training (Z),
                                      Retry/End-of-Set Drill modals, Start Paused
                                      hold + Return to CSS, mid-Frame-Advance,
                                      mid-hitlag, mid-savestate, mid-Ledgedash
                                      fade & OSD eviction; hunts stuck freeze bit.
    cat8-4ctrl-hybrid-port-control    GC + Classic + Nunchuk + Sideways Wii
                                      Remote across Boot Menu, Credits, Lab Menu,
                                      Savestates, Frame Advance, and CPU Port
                                      Control (sustained Shield + C-Stick buffer).
    cat9-extreme-stage-camera-gx-d3d  Real D3D across FoD, Yoshi's Story, PS2,
                                      Battlefield, FD, Castle Siege with Camera
                                      Zoom/Fixed, Hazards Off, all 3D/2D overlays
                                      ON while rapidly toggling Models/Stage/FX/HUD.

  Bucket 4 (Writable SD Card - sequential boots on isolated sd-storage.raw):
    cat2-4p-maxload-heap-display      4P on FoD & Yoshi's Story with all displays
                                      + 4P OSDs + fighter/costume swaps + Browser
                                      + SD save/load; Heaps::Network >= 0x10BA20.
    cat3-hostile-sd-fuzz-live         Zero-byte, truncated, random-garbage,
                                      oversized, mismatched-CRC, and future v99
                                      settings/bests/.tmr files on live SD.
    cat11-schema-migration-every-reset v1 upgrade modal (all 3 choices), 16-area
                                      v2 silent Area 17 migration, fresh-SD Quick
                                      Setup partial save, Custom 1..3 OSD slots +
                                      6 P1/6 CPU .tmr slots, double-Y reset on
                                      EVERY Lab page/subpage, cold reboot verify.
"""
import argparse
import math
import os
import random
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import charge_tests as CHG  # noqa: E402
import cpu_tests as CT  # noqa: E402
import display_options_tests as DO  # noqa: E402
import drill_tests as DT  # noqa: E402
import emu as E  # noqa: E402
import fatimage  # noqa: E402
import fighter_set_tests as FS  # noqa: E402
import fighter_swap_tests as FSW  # noqa: E402
import grab_visualizer_tests as GV  # noqa: E402
import hud_theme_meter_tests as HTM  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import olimar_lab_tests as OL  # noqa: E402
import psa_inspector_tests as PSA  # noqa: E402
import record_tests as RT  # noqa: E402
import recovery_combo_tests as RC  # noqa: E402
import shots  # noqa: E402
import skill_drills_ui_tests as SD  # noqa: E402
import stage3d_tests as S3D  # noqa: E402
import stage3f_tests as S3F  # noqa: E402
import stage4c_tests as S4C  # noqa: E402
import stage_options_tests as SO  # noqa: E402
import storage_tests as ST  # noqa: E402
import stream3_break_targets_tests as S3B  # noqa: E402
import wii_strict_tests as WS  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# Stage IDs (srStageKind, src/menu/names.cpp).
STAGE_BF = 0x01
STAGE_FD = 0x02
STAGE_CASTLE_SIEGE = 0x19
STAGE_FOD = 0x1F
STAGE_YOSHIS = 0x2A
STAGE_PS2 = 0x2E

# Main settings offsets (src/main/settings.h).
S_LEDGE_HUD = 12
S_LEDGE_GALINT = 14
S_LEDGE_PANEL = 15
S_CAMERA = 16
S_DI_DISPLAY = 19
S_OFFLINE_VS = 21
S_ACTION_LOG = 22
S_HUD = 41
S_HITBOXES = 42
S_MODELS = 43
S_ENV_COLL = 44
S_STAGE_MODEL = 45
S_EFFECTS = 46
S_HEATMAP = 47
S_HEATMAP_BOUNDS = 48
S_GRAB_VIZ = 51
S_INPUT_VIEWER = 52
S_DI_VECTOR = 54
S_PSA = 55
S_HMN_INFO_SIZE = 57
S_HMN_INFO_ROWS = 58
S_CPU_INFO_SIZE = 73
S_CPU_INFO_ROWS = 74
S_OSD_POS = 88
S_OSD_FA_SHIELD = 89
S_OSD_FA_HIT = 90
S_OSD_WAVEDASH = 92
S_OSD_ITEM_THROW = 108
S_OSD_MAX_MSGS = 109
S_OSD_TIMING_METER = 115
S_OSD_PRESET = 116
S_OSD_SPARKLE = 117
S_OSD_COLORBLIND = 118
S_CPU_CONTROL = 123
S_CPU_BEHAVIOR = 124
S_CPU_TDI = 125
S_CPU_CONTROLLED_BY = 149
S_CPU_RECOVERY = 162
S_CPU_ADAPTIVE = 165
S_CPU_TECH_OSD = 182
S_REC_START_PAUSED = 194
S_OSD_CUSTOM_MASKS = 464

# ActionLogDebugBlock ("TMAL", src/extra/action_log_core.h).
TMAL_MAGIC = 0x544D414C
TMAL_RE = re.compile(r"action_log: debug block at ([0-9a-f]{8})")
SF6_KIND_MASK = 0x0F
SF6_GRAB = 6

SS_BLOCK_RE = re.compile(r"savestate: test block at ([0-9a-f]{8})")
SS_SAVED_RE = re.compile(r"savestate: saved frame (\d+)")
SS_LOADED_RE = re.compile(r"savestate: loaded frame (\d+)")
SS_REFUSED_RE = re.compile(r"savestate: (save|load) refused: ([^\r\n]+)")

BUCKETS = {
    1: [
        "cat1-savestate-record-swap-chaos",
        "cat5-long-soak-combat",
        "cat6-transform-tether-multiactor",
    ],
    2: [
        "cat4-strict-mode-menu-drill-hop",
        "cat10-scene-loop-vs4p-mmu",
    ],
    3: [
        "cat7-freeze-fa-modal-race",
        "cat8-4ctrl-hybrid-port-control",
        "cat9-extreme-stage-camera-gx-d3d",
    ],
    4: [
        "cat2-4p-maxload-heap-display",
        "cat3-hostile-sd-fuzz-live",
        "cat11-schema-migration-every-reset",
    ],
}
ALL_CASES = [c for b in sorted(BUCKETS) for c in BUCKETS[b]]


class TortureError(RuntimeError):
    pass


# ---- shared low-level helpers ----------------------------------------------

def u8(emu, addr):
    return emu.u8(addr)


def w8(emu, addr, val):
    emu.h.call("write_mem", addr=addr, hex=f"{int(val) & 0xFF:02x}")


def w16(emu, addr, val):
    emu.h.call("write_mem", addr=addr, hex=f"{int(val) & 0xFFFF:04x}")


def wbytes(emu, addr, data):
    emu.h.call("write_mem", addr=addr, hex=bytes(data).hex())


def is_finite(v, limit=1e5):
    return isinstance(v, (int, float)) and not math.isnan(v) and not math.isinf(v) and abs(v) <= limit


def scheduler_frozen(emu):
    sched = emu.ptr(nav.TASK_SCHEDULER)
    if not sched:
        return False
    return bool(emu.u8(sched + nav.SCHEDULER_FLAGS) & nav.SCHEDULER_STOP_TASKS)


def check_no_problems(emu, since_pos, label):
    tail = emu.log_text()[since_pos:]
    bad = L.problems(tail)
    if bad:
        raise TortureError(f"{label}: problem log lines: {bad[:3]}")
    if "back to baseline: NO" in tail:
        raise TortureError(f"{label}: heap failed to return to baseline")
    for fault in ("DSI exception", "ISI exception", "Machine Check", "Data TLB", "Program exception"):
        if fault in tail:
            raise TortureError(f"{label}: Wii CPU/MMU fault detected: {fault}")


def check_network_heap(emu, label):
    largest, total_free, nfree, allocs = WS._network_heap_stats(emu)
    if largest < WS.MIN_COSTUME_FREE:
        raise TortureError(
            f"{label}: Heaps::Network largest={largest:#x} < min {WS.MIN_COSTUME_FREE:#x} "
            f"(free={total_free:#x}, blocks={nfree}, alloc={allocs})"
        )
    return largest, total_free, nfree, allocs


def enable_all_overlays_and_osds(emu, settings_addr, *, psa=1, frame_meter=1, combo_enabled=1):
    """Turn on every visualizer, HUD, info panel, floating meter, and all 20 OSDs."""
    w8(emu, settings_addr + S_LEDGE_HUD, 1)
    w8(emu, settings_addr + S_LEDGE_GALINT, 1)
    w8(emu, settings_addr + S_LEDGE_PANEL, 1)
    w8(emu, settings_addr + S_DI_DISPLAY, 1)
    w8(emu, settings_addr + S_ACTION_LOG, frame_meter)
    w8(emu, settings_addr + S_HUD, 1)
    w8(emu, settings_addr + S_HITBOXES, 3)       # Both
    w8(emu, settings_addr + S_MODELS, 3)         # Both
    w8(emu, settings_addr + S_ENV_COLL, 1)
    w8(emu, settings_addr + S_STAGE_MODEL, 1)
    w8(emu, settings_addr + S_EFFECTS, 1)
    w8(emu, settings_addr + S_HEATMAP, 3)        # Both
    w8(emu, settings_addr + S_HEATMAP_BOUNDS, 1)
    w8(emu, settings_addr + S_GRAB_VIZ, 3)       # Both
    w8(emu, settings_addr + S_INPUT_VIEWER, 3)   # Both
    w8(emu, settings_addr + S_PSA, psa)
    # P1 & CPU Info Display: all 8 rows populated.
    w8(emu, settings_addr + S_HMN_INFO_SIZE, 0)
    wbytes(emu, settings_addr + S_HMN_INFO_ROWS, bytes([1, 2, 3, 4, 5, 6, 7, 8]))
    w8(emu, settings_addr + S_CPU_INFO_SIZE, 0)
    wbytes(emu, settings_addr + S_CPU_INFO_ROWS, bytes([1, 2, 3, 4, 5, 6, 7, 8]))
    # All 19 main OSD toggles + maxMessages=6 + Timing Meter All + Tech Sparkle Both + P+ Tech OSD.
    w8(emu, settings_addr + S_OSD_FA_SHIELD, 1)
    w8(emu, settings_addr + S_OSD_FA_HIT, 1)
    wbytes(emu, settings_addr + S_OSD_WAVEDASH, bytes([1] * (S_OSD_ITEM_THROW - S_OSD_WAVEDASH + 1)))
    w8(emu, settings_addr + S_OSD_MAX_MSGS, 6)
    w8(emu, settings_addr + S_OSD_TIMING_METER, 1)
    w8(emu, settings_addr + S_OSD_SPARKLE, 3)
    w8(emu, settings_addr + S_CPU_TECH_OSD, 1)
    # Combo Trainer floating meters (all 7).
    RC.write_combo_settings(emu, settings_addr, enabled=combo_enabled, enabled_meters=RC.METER_ALL)


def find_ss_block(emu):
    hits = SS_BLOCK_RE.findall(emu.log_text())
    if not hits:
        raise TortureError("savestate test block not found in log")
    return int(hits[-1], 16)


def ss_cmd(emu, ss_addr, cmd, max_adv=60):
    """Trigger savestate test block cmd (1=save, 2=load) while running; return (kind, detail)."""
    pos = len(emu.log_text())
    w8(emu, ss_addr + 5, cmd)
    for _ in range(max_adv // 2):
        emu.advance(2)
        tail = emu.log_text()[pos:]
        ref = SS_REFUSED_RE.search(tail)
        if ref:
            return ("refused", ref.group(2))
        if cmd == 1 and SS_SAVED_RE.search(tail):
            return ("saved", int(SS_SAVED_RE.search(tail).group(1)))
        if cmd == 2 and SS_LOADED_RE.search(tail):
            return ("loaded", int(SS_LOADED_RE.search(tail).group(1)))
    return ("timeout", "")


def find_tmal(emu, since_pos=0):
    hits = TMAL_RE.findall(emu.log_text()[since_pos:])
    if not hits:
        hits = TMAL_RE.findall(emu.log_text())
    if not hits:
        raise TortureError("action_log TMAL debug block not found in log")
    addr = int(hits[-1], 16)
    if emu.u32(addr) != TMAL_MAGIC:
        raise TortureError(f"bad TMAL magic at {addr:#x}: {emu.u32(addr):#x}")
    return addr


def read_tmal_sf6(emu, addr):
    raw = emu.read(addr + 0xA8, 8 + 64)
    adv, has_adv, vis, reloc, filled = struct.unpack_from(">hBBBB", raw, 0)
    p1_cells = list(raw[8:40])
    cpu_cells = list(raw[40:72])
    return {
        "advantage": adv,
        "hasAdvantage": has_adv,
        "visibleCells": vis,
        "relocatedTop": reloc,
        "filled": filled,
        "p1": p1_cells,
        "cpu": cpu_cells,
    }


def apply_swap_live(emu, lab, sb, p1_name, p1_cos, cpu_name, cpu_cos):
    """Apply Fighter Swap through real Lab Menu navigation + live scratch selection."""
    d_before = sb.read()
    lab.open_menu(keep=True)
    lab.menu_nav_to("Stage & Cast > Fighter Swap > Apply Swap")
    sb.write_scratch(FSW.ROSTER_IDX[p1_name], p1_cos, FSW.ROSTER_IDX[cpu_name], cpu_cos)
    lab.tap(["A"])
    sw = FSW.wait_swap_done(emu, sb, d_before["completedCount"], max_frames=360)
    if sw["refusalCount"] != d_before["refusalCount"]:
        raise TortureError(f"swap {p1_name}(c{p1_cos})/{cpu_name}(c{cpu_cos}) refused: {sw}")
    return sw


def check_d3d_not_blank(shot_tuple, label):
    """Verify a D3D screenshot is non-blank and has real color/spatial variance."""
    path, w, h = shot_tuple
    if w < 320 or h < 240:
        raise TortureError(f"{label}: invalid D3D screenshot size {w}x{h}")
    pic = DO.picture(shot_tuple)
    # Sample a 24x18 grid covering all 4 quadrants.
    colors = set()
    non_black = 0
    total = 0
    for gy in range(18):
        y = int((gy + 0.5) * h / 18)
        for gx in range(24):
            x = int((gx + 0.5) * w / 24)
            r, g, b = pic.px(x, y)
            total += 1
            if r + g + b > 24:
                non_black += 1
            colors.add((r >> 3, g >> 3, b >> 3))
    if non_black < total * 0.15 or len(colors) < 12:
        raise TortureError(
            f"{label}: D3D frame appears blank or corrupted (non_black={non_black}/{total}, unique={len(colors)}, {path})"
        )


def assert_clock_advances(emu, label, adv=20):
    f0 = nav.game_frame(emu)
    emu.advance(adv)
    f1 = nav.game_frame(emu)
    if f1 <= f0:
        raise TortureError(f"{label}: game clock stuck at {f0} -> {f1} (frozen={scheduler_frozen(emu)})")
    return f1 - f0


def training_css_to_versus_css(emu):
    """Navigate from Training CSS (with drill list closed or open) to Versus CSS (sqVsMelee)."""
    if nav.drill_list_state(emu) == "open":
        nav.close_drill_list(emu)
    emu.advance(12)
    emu.hold(0, buttons=["B"])
    emu.wait_for("Main Menu from Training CSS", lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
    emu.neutral(0)
    emu.advance(25)
    emu.press(0, ["B"], frames=3, after=25)
    emu.press(0, stick=(28, 128), frames=3, after=15)
    emu.press(0, ["A"], frames=3, after=25)
    emu.press(0, ["A"], frames=3, after=25)
    emu.wait_for(
        "Versus CSS (sqVsMelee)",
        lambda: nav.scene(emu)[0] == "scSelctCharacter" and nav.scene(emu)[1] == "sqVsMelee",
        600, every=5,
    )
    emu.advance(25)


def versus_css_to_training_css(emu):
    """Navigate from Versus CSS (sqVsMelee) back to Training CSS (sqTraining)."""
    emu.neutral(0)
    emu.advance(10)
    emu.hold(0, buttons=["B"])
    emu.wait_for("Main Menu from Versus CSS", lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
    emu.neutral(0)
    emu.advance(25)
    # Main Menu cursor lands on Brawl/Group: press B to top level, Right to Solo, A -> Solo, A -> Training.
    emu.press(0, ["B"], frames=3, after=25)
    emu.press(0, stick=(228, 128), frames=3, after=15)
    emu.press(0, ["A"], frames=3, after=25)
    emu.press(0, ["A"], frames=3, after=25)
    emu.wait_for(
        "Training CSS from Solo",
        lambda: nav.scene(emu)[0] == "scSelctCharacter" and nav.scene(emu)[1] == "sqTraining",
        600, every=5,
    )
    emu.advance(20)


def sd_put(img, files, *, clear=False):
    """Write {filename_in_tm: bytes} into ST.FOLDER on writable SD image `img`."""
    w = fatimage.FatWriter(img)
    try:
        if clear:
            w.clear_folder(ST.FOLDER)
        for name, data in files.items():
            w.put(f"{ST.FOLDER}/{name}", data)
    finally:
        w.close()


def read_bests_file(data):
    if data is None or len(data) < ST.BESTS_HEAD:
        return None
    h = ST.parse_header(data)
    if h is None or h["magic"] != ST.BESTS_MAGIC:
        return None
    slot_count, rev = struct.unpack_from(">HH", data, 32)
    slots = list(struct.unpack_from(f">{slot_count}H", data, 36)) if 36 + 2 * slot_count <= len(data) else []
    h.update(slot_count=slot_count, revision=rev, slots=slots)
    return h


def build_bests(slots, sequence, fmt=1, rev=1, extra_tail=b""):
    body = struct.pack(">HH", len(slots), rev) + struct.pack(f">{len(slots)}H", *slots) + extra_tail
    size = 32 + len(body)
    tail = struct.pack(">I", sequence) + bytes(12) + body
    data = struct.pack(">IHHII", ST.BESTS_MAGIC, fmt, 32, size, ST.fnv(tail)) + tail
    return data + bytes(ST.padded(size) - size)


def build_valid_v1_settings(seq=1):
    """Build a genuine v1 (layout=1) settings file with non-default General (unchanged
    layout) + older-length OsdSettings (changed layout, affected area count >= 1)."""
    # Area 1 (General): 15 bytes -> set cameraMode (offset 8) = 1 (Zoom), moveStaling (offset 10) = 0.
    gen = bytearray(15)
    gen[4] = 1     # ledgeHud
    gen[5] = 50    # ledgeMaxFrames
    gen[6] = 1     # ledgeGalint
    gen[7] = 1     # ledgePanel
    gen[8] = 1     # cameraMode = Zoom
    gen[10] = 0    # moveStaling = Off
    gen[12] = 1    # playerInfiniteShields = Until Hit
    # Area 2 (Controls): 10 bytes -> advanceButton (offset 0) = 2 (X), advanceDelay (offset 1) = 42, startOpensMenu = 1.
    ctrl = bytearray(10)
    ctrl[0] = 2
    ctrl[1] = 42
    ctrl[2] = 1
    # Area 6 (OSD): older v1 size (22 bytes, missing newer Theme/Colorblind tail -> affected area!).
    osd = bytearray(22)
    osd[0] = 1     # position = Sides
    osd[4] = 1     # wavedash = On
    osd[5] = 0     # lCancel = Off (non-default vs v2 Essentials!)
    osd[6] = 0     # actOos = Off
    osd[21] = 5    # maxMessages = 5
    return ST.build_settings([(1, 1, bytes(gen)), (2, 1, bytes(ctrl)), (6, 1, bytes(osd))], seq, fmt=1, layout=1)


# ============================================================================
# CATEGORY 1: Savestate + Recording + Rewind + Swap Chaos
# ============================================================================

def run_cat1(rel):
    print("=== [cat1-savestate-record-swap-chaos] 50+ save/load + recording/rewind + 15+ swaps + ALL displays ===")
    with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu)
        nav.start_quick_match(emu, stage=STAGE_BF, p1=FSW.ROSTER_INFO["Ice Climbers"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(30)

        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        sb = FSW.SwapBlock(emu)
        sb.find()
        ss_addr = find_ss_block(emu)
        enable_all_overlays_and_osds(emu, settings, psa=1, frame_meter=1, combo_enabled=1)
        # Put CPU in Lab Stand first so we can reliably trigger hitlag, grab, shieldstun, and recording.
        w8(emu, settings + S_CPU_CONTROL, CT.LAB)
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
        w8(emu, settings + S_CPU_TDI, CT.TDI["None"])
        emu.advance(15)

        total_cycles = 0

        # Phase A: Ice Climbers mid-hitlag, mid-grab, mid-shieldstun rapid save/load (20 cycles).
        # Walk close to Fox CPU.
        g = CT.Game(emu)
        for _ in range(90):
            dx = g.fighter(g.entry(1))["x"] - g.fighter(g.entry(0))["x"]
            if abs(dx) < 10.0:
                break
            emu.hold(0, stick=(228 if dx > 0 else 28, 128))
            emu.advance(2)
        emu.neutral(0)
        emu.advance(6)

        # Mid-attack / hitlag save + 7 rapid load/re-save cycles.
        emu.hold(0, buttons=["A"], stick=(228 if g.fighter(g.entry(1))["x"] > g.fighter(g.entry(0))["x"] else 28, 128))
        emu.advance(8)
        emu.neutral(0)
        kind, _ = ss_cmd(emu, ss_addr, 1)
        if kind != "saved":
            raise TortureError(f"cat1 IC hitlag initial save failed: {kind}")
        for i in range(7):
            emu.advance(4)
            k2, _ = ss_cmd(emu, ss_addr, 2)
            if k2 != "loaded":
                raise TortureError(f"cat1 IC hitlag load #{i} failed: {k2}")
            emu.advance(2)
            k1, _ = ss_cmd(emu, ss_addr, 1)
            if k1 != "saved":
                raise TortureError(f"cat1 IC hitlag resave #{i} failed: {k1}")
            total_cycles += 1

        # Mid-grab save/load (7 cycles).
        emu.hold(0, buttons=["Z"])
        emu.advance(5)
        emu.neutral(0)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat1 IC grab initial save failed")
        for i in range(7):
            emu.advance(3)
            if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
                raise TortureError(f"cat1 IC grab load #{i} failed")
            total_cycles += 1

        # Mid-shieldstun save/load (6 cycles): CPU shields, P1 attacks shield.
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Shield"])
        emu.advance(12)
        emu.hold(0, buttons=["A"])
        emu.advance(5)
        emu.neutral(0)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat1 IC shieldstun initial save failed")
        for i in range(6):
            emu.advance(3)
            if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
                raise TortureError(f"cat1 IC shieldstun load #{i} failed")
            total_cycles += 1
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])

        # Phase B: Recording Save Positions + Record + Playback + Rewind + Savestate interleaving (10 cycles).
        rec = RT.Rec(emu)
        rec.find()
        # Wait for storage ready.
        for _ in range(40):
            if S3D.read_rec_block(emu, rec.addr)["flags"] & RT.FLAG_STORAGE:
                break
            emu.advance(2)
        rec.request("save")
        emu.advance(12)
        rec.set(hmnMode=RT.HMN_MODE["Record"], hmnSlot=1, cpuMode=RT.CPU_MODE["Off"], cpuSlot=1)
        rec.request("hmn-mode")
        emu.advance(4)
        # Record 40 frames of movement + attacks.
        emu.hold(0, buttons=["A"], stick=(220, 128))
        emu.advance(20)
        emu.hold(0, buttons=["X"], stick=(40, 128))
        emu.advance(20)
        emu.neutral(0)
        # Switch to Playback: first exercise real Frame Decrement (Rewind) mid-playback,
        # then hammer 10 alternating savestate load + recording restore cycles!
        rec.set(hmnMode=RT.HMN_MODE["Playback"], hmnSlot=1)
        rec.request("hmn-mode")
        emu.advance(12)
        # Real Frame Decrement rewind: bind X as Decrement button, freeze via Frame Advance, step back 2 frames.
        w8(emu, settings + RT.SET_DECREMENT, RT.DECREMENT_X)
        w8(emu, settings + LT.SET_FRAME_ADVANCE, 1)
        emu.advance(4)
        rw0 = rec.rewind_status()
        for rw_i in range(2):
            emu.press(0, ["X"], frames=2, after=2)
            emu.wait_for(
                f"cat1 Frame Decrement rewind #{rw_i + 1}",
                lambda prev=rw0["rewound"] + rw_i: rec.rewind_status()["rewound"] > prev,
                240, every=2,
            )
        rw1 = rec.rewind_status()
        if rw1["rewinds"] < rw0["rewinds"] + 2:
            raise TortureError(f"cat1 rewind count did not increment by >=2: {rw0} -> {rw1}")
        w8(emu, settings + LT.SET_FRAME_ADVANCE, 0)
        w8(emu, settings + RT.SET_DECREMENT, 0)
        emu.advance(8)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat1 mid-playback save failed")
        for i in range(10):
            emu.advance(5)
            if i % 2 == 0:
                k, _ = ss_cmd(emu, ss_addr, 2)
                if k != "loaded":
                    raise TortureError(f"cat1 mid-playback savestate load #{i} failed: {k}")
            else:
                rec.request("restore")
                emu.advance(8)
            total_cycles += 1
        rec.set(hmnMode=RT.HMN_MODE["Off"])
        rec.request("hmn-mode")
        emu.advance(8)
        check_network_heap(emu, "cat1-post-ic-record")

        # Phase C: Swap to Olimar -> pluck 6 Pikmin -> 12 rapid save/load cycles.
        apply_swap_live(emu, lab, sb, "Olimar", 1, "Fox", 0)
        # Re-read settings overlays intact and pluck 6 Pikmin via Down-B.
        for _ in range(7):
            emu.hold(0, buttons=["B"], stick=(128, 28))
            emu.advance(6)
            emu.neutral(0)
            emu.advance(22)
        fv_oli = CHG.FighterView(emu, 0)
        piks = OL.pikmin_color_and_maturity(emu, fv_oli)
        print(f"  cat1 Olimar active Pikmin count={len(piks)}")
        if len(piks) < 3:
            raise TortureError(f"cat1 Olimar expected >=3 Pikmin plucked, got {len(piks)}")
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat1 Olimar save failed")
        for i in range(12):
            emu.hold(0, buttons=["B"], stick=(228, 128))  # Pikmin Throw mid-cycle
            emu.advance(6)
            emu.neutral(0)
            k, _ = ss_cmd(emu, ss_addr, 2)
            if k != "loaded":
                raise TortureError(f"cat1 Olimar load #{i} failed: {k}")
            total_cycles += 1

        # Phase D: Swap to Snake -> plant C4 + pull Grenade -> 12 rapid save/load cycles.
        apply_swap_live(emu, lab, sb, "Snake", 2, "Mario", 1)
        emu.hold(0, buttons=["B"], stick=(128, 28))  # Plant C4
        emu.advance(6)
        emu.neutral(0)
        emu.advance(26)
        emu.hold(0, buttons=["B"])  # Pull Grenade
        emu.advance(6)
        emu.neutral(0)
        emu.advance(14)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat1 Snake C4+grenade save failed")
        for i in range(12):
            emu.advance(5)
            k, _ = ss_cmd(emu, ss_addr, 2)
            if k != "loaded":
                raise TortureError(f"cat1 Snake load #{i} failed: {k}")
            if i % 4 == 3:
                emu.advance(4)
                if ss_cmd(emu, ss_addr, 1)[0] != "saved":
                    raise TortureError(f"cat1 Snake resave #{i} failed")
            total_cycles += 1
        check_network_heap(emu, "cat1-post-snake-54-cycles")

        # Phase E: 15+ rapid fighter & costume swaps with ALL visualizers/OSDs active!
        swap_ring = [
            ("Zelda", 1, "Sheik", 2),
            ("Kirby", 3, "Dedede", 1),
            ("Peach", 2, "Wario", 1),
            ("Diddy Kong", 1, "R.O.B.", 2),
            ("Samus", 3, "Lucas", 1),
            ("Ivysaur", 2, "Charizard", 1),
            ("Mewtwo", 1, "Lucario", 2),
            ("Ice Climbers", 2, "Olimar", 3),
            ("Snake", 1, "Meta Knight", 2),
            ("Link", 1, "Toon Link", 2),
            ("Yoshi", 3, "Bowser", 1),
            ("Sonic", 2, "Pikachu", 3),
            ("Marth", 1, "Roy", 2),
            ("Ganondorf", 1, "Captain Falcon", 3),
            ("Zero Suit Samus", 2, "Falco", 1),
        ]
        for idx, (p1n, p1c, cpun, cpuc) in enumerate(swap_ring):
            apply_swap_live(emu, lab, sb, p1n, p1c, cpun, cpuc)
            emu.advance(8)
            if idx % 5 == 4:
                if ss_cmd(emu, ss_addr, 1)[0] != "saved":
                    raise TortureError(f"cat1 post-swap {p1n} save failed")
                emu.advance(5)
                if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
                    raise TortureError(f"cat1 post-swap {p1n} load failed")
                total_cycles += 1
            check_network_heap(emu, f"cat1-swap-{idx + 1}-{p1n}")

        # Verify clean pad & CPU recovery after the entire chaos gauntlet.
        assert_clock_advances(emu, "cat1-post-chaos-clock", 20)
        g.mods.clear()
        x0 = g.fighter(g.entry(0))["x"]
        emu.hold(0, stick=(228, 128))
        emu.advance(20)
        emu.neutral(0)
        x1 = g.fighter(g.entry(0))["x"]
        if abs(x1 - x0) < 0.5:
            raise TortureError(f"cat1 P1 pad unresponsive after chaos ({x0:.2f} -> {x1:.2f})")

        nav.finish_training(emu)
        emu.advance(30)
        check_no_problems(emu, pos0, "cat1")
        print(f"  cat1 PASS: {total_cycles} save/load cycles + {len(swap_ring) + 2} fighter/costume swaps, heap OK")


# ============================================================================
# CATEGORY 5: Sustained Long-Soak Combat Run (3,600+ active combat frames)
# ============================================================================

def run_cat5(rel):
    print("=== [cat5-long-soak-combat] 3,600+ active combat frames + Adaptive/PPlusAI/Heatmap/FM/Combo/OSDs ===")
    with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu)
        nav.start_quick_match(emu, stage=STAGE_BF, p1=FSW.ROSTER_INFO["Fox"],
                              cpu=FSW.ROSTER_INFO["Mario"], seed_rng=True)
        emu.advance(30)

        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        enable_all_overlays_and_osds(emu, settings, psa=0, frame_meter=1, combo_enabled=1)
        # Enable Adaptive CPU + PPlusAI Recovery + active CPU counters & random tech/DI.
        opt = CT.Options(emu)
        opt.find()
        opt.set(
            control=CT.LAB,
            behavior=CT.BEHAVIOR["Stand"],
            tdi=CT.TDI["Random"],
            sdiAmount=2,
            counterGround=CT.GROUND["Jab"],
            counterAir=CT.AIR_ACTIONS["Airdodge"],
            counterShield=CT.SHIELD_ACTIONS["Grab"],
            tech=CT.TECH["Random"],
            getup=CT.GETUP["Random"],
            recovery=CT.RECOVERY["PPlusAI"],
            adaptiveMode=S3F.ADAPTIVE_ACTIVE,
        )
        tmac = S3F.AdaptiveBlock(emu)
        tmac.find()
        hm_hits = S3D.HEATMAP_BLOCK_RE.findall(emu.log_text())
        if not hm_hits:
            raise TortureError("cat5 heatmap block not found")
        hm_addr = int(hm_hits[-1], 16)
        tmal_addr = find_tmal(emu, pos0)
        emu.advance(30)

        f_start = nav.game_frame(emu)
        samples = []

        def sample_heaps(tag):
            ht = L.heap_table(emu)
            net_l, net_free, _, net_alloc = check_network_heap(emu, f"cat5-{tag}")
            mem1_used = sum(v[3] for k, v in ht.items() if v[0] < 0x90000000)
            mem2_used = sum(v[3] for k, v in ht.items() if v[0] >= 0x90000000)
            gf = nav.game_frame(emu)
            samples.append((tag, gf, net_l, net_free, net_alloc, mem1_used, mem2_used))
            print(f"  [soak {tag}] f={gf} netLargest={net_l:#x} netAlloc={net_alloc} mem1Used={mem1_used:#x} mem2Used={mem2_used:#x}")

        sample_heaps("start")

        # Drive 3,600+ frames of varied high-intensity combat (12 chunks x ~320 frames = ~3,840 frames).
        g = CT.Game(emu)
        actions = [
            (dict(buttons=["A"], stick=(228, 128)), 10),
            (dict(buttons=["X"], stick=(228, 128)), 5),
            (dict(buttons=["A"], stick=(128, 28)), 10),
            (dict(buttons=["L"], stick=(210, 72)), 8),     # wavedash / shield
            (dict(buttons=["B"], stick=(128, 28)), 8),     # shine
            (dict(buttons=["Z"]), 8),                      # grab
            (dict(buttons=["A"], stick=(28, 128)), 10),
            (dict(buttons=["X"], stick=(28, 128)), 5),
            (dict(buttons=["L"], stick=(46, 72)), 8),
            (dict(buttons=["B"], stick=(128, 128)), 8),    # laser
            (dict(buttons=["A"], stick=(128, 228)), 10),   # usmash / utilt
        ]
        prev_gf = f_start
        for chunk in range(12):
            for step_idx in range(16):
                pad_kw, dur = actions[(chunk * 7 + step_idx) % len(actions)]
                # Steer toward CPU occasionally so real hits & Adaptive RPS trigger continuously.
                if step_idx % 4 == 0:
                    try:
                        dx = g.fighter(g.entry(1))["x"] - g.fighter(g.entry(0))["x"]
                        emu.hold(0, stick=(228 if dx > 0 else 28, 128))
                        emu.advance(10)
                    except Exception:
                        pass
                emu.hold(0, **pad_kw)
                emu.advance(dur)
                emu.neutral(0)
                emu.advance(6)
            cur_gf = nav.game_frame(emu)
            if cur_gf <= prev_gf + 50:
                raise TortureError(f"cat5 stalled during chunk {chunk}: frame {prev_gf} -> {cur_gf}")
            prev_gf = cur_gf
            if chunk in (2, 5, 8, 11):
                sample_heaps(f"chunk-{chunk + 1}")

        f_end = nav.game_frame(emu)
        elapsed_frames = f_end - f_start
        if elapsed_frames < 3600:
            # Top up to guarantee >= 3600 active combat frames.
            need = 3620 - elapsed_frames
            emu.hold(0, buttons=["A"], stick=(228, 128))
            emu.advance(need // 2)
            emu.neutral(0)
            emu.advance(need - need // 2 + 10)
            f_end = nav.game_frame(emu)
            elapsed_frames = f_end - f_start
            sample_heaps("end-topup")

        if elapsed_frames < 3600:
            raise TortureError(f"cat5 only ran {elapsed_frames} frames, need >= 3600")

        # Verify live subsystems genuinely ran during the soak.
        ac = tmac.read()
        hm = S3D.read_heatmap_block(emu, hm_addr)
        fm = read_tmal_sf6(emu, tmal_addr)
        if ac["samples"] < 100 or hm["totalSampled"] == 0 or fm["filled"] == 0:
            raise TortureError(f"cat5 subsystems idle: adaptive={ac['samples']} hm={hm['totalSampled']} fm={fm['filled']}")

        # Memory flatness check across start -> middle -> end:
        # Heaps::Network allocation count must stay bounded & flat (no monotonic leak),
        # and overall MEM2/MEM1 used bytes must not grow unboundedly.
        s0, smid, send = samples[0], samples[len(samples) // 2], samples[-1]
        if abs(send[4] - s0[4]) > 8:
            raise TortureError(f"cat5 Heaps::Network alloc drift: start={s0[4]} mid={smid[4]} end={send[4]}")
        if (send[5] - s0[5]) > 0x20000 or (send[6] - s0[6]) > 0x40000:
            raise TortureError(f"cat5 heap growth detected: start={s0} mid={smid} end={send}")

        nav.finish_training(emu)
        emu.advance(30)
        check_no_problems(emu, pos0, "cat5")
        print(f"  cat5 PASS: {elapsed_frames} combat frames, adaptiveSamples={ac['samples']}, "
              f"heatmapSampled={hm['totalSampled']}, flat memory verified")


# ============================================================================
# CATEGORY 6: Transform, Tether & Multi-Actor Torture
# ============================================================================

def run_cat6(rel):
    print("=== [cat6-transform-tether-multiactor] Zelda/Sheik, IC Nana OSD, Olimar, Snake, Tethers, Items ===")
    with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu)

        # ---- 6a. Zelda <-> Sheik live transforms + mid-savestate + mid-Frame-Meter ----
        print("  [6a] Zelda <-> Sheik transforms + savestates + Frame Meter")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Zelda"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        ss_addr = find_ss_block(emu)
        tmal_addr = find_tmal(emu, mpos)
        w8(emu, settings + S_ACTION_LOG, 1)
        w8(emu, settings + S_HITBOXES, 3)
        w8(emu, settings + S_CPU_CONTROL, CT.LAB)
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
        g = CT.Game(emu)

        # Save as Zelda (instance 0), transform Zelda -> Sheik (instance 1):
        # savestate MUST cleanly refuse cross-form load while transformed (safety guard),
        # then transform Sheik -> Zelda (instance 0) and verify save/load works again!
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat6a Zelda save failed")
        emu.hold(0, buttons=["B"], stick=(128, 28))
        emu.advance(6)
        emu.neutral(0)
        emu.wait_for("Zelda -> Sheik active kind", lambda: g.entry(0)["kinds"][g.entry(0)["active"]] == 0x0E, 240, every=2)
        emu.advance(30)
        # Attempt cross-form load while transformed -> MUST safely refuse, never crash!
        rk, rmsg = ss_cmd(emu, ss_addr, 2)
        if rk != "refused" or "transformation" not in rmsg:
            raise TortureError(f"cat6a expected safe transform refusal while Sheik, got {(rk, rmsg)}")
        # Also attempt save while transformed -> MUST safely refuse, never crash!
        sk, smsg = ss_cmd(emu, ss_addr, 1)
        if sk != "refused" or "transformation" not in smsg:
            raise TortureError(f"cat6a expected safe transform save refusal while Sheik, got {(sk, smsg)}")
        # Transform back Sheik -> Zelda (instance 0) and verify Frame Meter & savestate work cleanly!
        emu.hold(0, buttons=["B"], stick=(128, 28))
        emu.advance(6)
        emu.neutral(0)
        emu.wait_for("Sheik -> Zelda", lambda: g.entry(0)["kinds"][g.entry(0)["active"]] == 0x0D, 240, every=2)
        emu.advance(35)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat6a post-return Zelda save failed")
        emu.advance(10)
        if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
            raise TortureError("cat6a post-return Zelda load failed")
        emu.advance(15)
        fm_z = read_tmal_sf6(emu, tmal_addr)
        if fm_z["filled"] == 0:
            raise TortureError("cat6a Frame Meter stopped ticking across Zelda/Sheik transforms")
        nav.finish_training(emu)
        emu.advance(20)
        check_no_problems(emu, mpos, "cat6a-zelda-sheik")

        # ---- 6b. Ice Climbers: Nana double-OSD spam hunt + Nana KO/separation/throw ----
        print("  [6b] Ice Climbers Nana zero double-OSD spam + separation/KO/throw")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Ice Climbers"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        settings = lab.find_settings()
        ss_addr = find_ss_block(emu)
        tmtt = S3F.TechTrackerBlock(emu)
        tmtt.find()
        w8(emu, settings + S_OSD_WAVEDASH, 1)
        w8(emu, settings + S_OSD_MAX_MSGS, 6)
        w8(emu, settings + S_CPU_TECH_OSD, 1)  # P+ Tech OSD ON!
        w8(emu, settings + S_CPU_CONTROL, CT.LAB)
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
        emu.advance(10)

        # Perform 3 clean Ice Climbers wavedashes while paused step-by-step (100% deterministic:
        # wait for Wait/Landing finish -> hold X until airborne -> airdodge down-right/down-left).
        # Assert: Wavedash OSD (id 0) fires once per wavedash (techniques.cpp skips partner),
        # AND P+ Tech OSD (id 22 = kIdPPlusTech) + TMTT slot 0 successes MUST ALSO fire ONCE per wavedash!
        wd_log_pos = len(emu.log_text())
        tt_before = tmtt.read()["slots"][0]
        g.mods.clear()
        en_ic = g.entry(0)
        emu.h.call("pause")
        for idx in range(3):
            sx = 210 if idx % 2 == 0 else 46
            # Wait until Popo is grounded & actionable (Wait = 0x00).
            for _ in range(45):
                if g.fighter(en_ic)["status"] == 0:
                    break
                emu.h.call("frame_advance", n=1)
            emu.hold(0, buttons=["X"])
            # Step until Popo leaves ground (exits JumpSquat 0x0A -> JumpF 0x0B).
            for _ in range(8):
                emu.h.call("frame_advance", n=1)
                st_ic = g.fighter(en_ic)["status"]
                if st_ic in (0x0B, 0x0C):
                    break
            emu.hold(0, buttons=["L"], stick=(sx, 68))
            emu.h.call("frame_advance", n=4)
            emu.neutral(0)
            emu.h.call("frame_advance", n=25)
        emu.h.call("resume")
        emu.advance(10)
        tt_after = tmtt.read()["slots"][0]
        wd_tail = emu.log_text()[wd_log_pos:]
        wd_osd_count = len(re.findall(r"osd: P1 id 0: Wavedash:", wd_tail))
        tech_osd_count = len(re.findall(r"osd: P1 id 22: Wavedash", wd_tail))
        tt_delta = tt_after["successes"] - tt_before["successes"]
        print(f"    IC wavedashes: wdOsd={wd_osd_count}, pplusTechOsd={tech_osd_count}, tmttSuccessDelta={tt_delta}")
        if wd_osd_count < 2:
            raise TortureError(f"cat6b IC wavedash input failed to register (wd_osd={wd_osd_count})")
        if tech_osd_count != wd_osd_count or tt_delta != wd_osd_count:
            raise TortureError(
                f"cat6b REAL BUG: IC P+ Tech OSD / TMTT parity mismatch! "
                f"wdOsd={wd_osd_count} vs pplusTechOsd={tech_osd_count}, tmttSuccessDelta={tt_delta}"
            )

        # Nana separation, grab/throw, and Nana KO offstage + savestate save/load recovery.
        g.mods.clear()
        en_ic = g.entry(0)
        # Walk near Fox, grab & forward throw while Nana is active.
        for _ in range(80):
            dx = g.fighter(g.entry(1))["x"] - g.fighter(en_ic)["x"]
            if abs(dx) < 9.0:
                break
            emu.hold(0, stick=(228 if dx > 0 else 28, 128))
            emu.advance(2)
        emu.neutral(0)
        emu.hold(0, buttons=["Z"])
        emu.advance(6)
        emu.hold(0, stick=(228, 128))
        emu.advance(25)
        emu.neutral(0)
        # Walk to left ledge, drop/separate and send Nana offstage or save/load with Nana active/dead.
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat6b IC post-throw save failed")
        # Walk to left edge and knock Nana offstage via quick dash + jump-back.
        emu.hold(0, stick=(28, 128))
        emu.advance(45)
        emu.hold(0, buttons=["X"], stick=(228, 128))
        emu.advance(15)
        emu.neutral(0)
        emu.advance(45)
        if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
            raise TortureError("cat6b IC post-separation load failed")
        assert_clock_advances(emu, "cat6b-ic-recovery", 20)
        nav.finish_training(emu)
        emu.advance(20)
        check_no_problems(emu, mpos, "cat6b-ic")

        # ---- 6c. Olimar 6-Pikmin pluck/whistle/D-Pad presets + swaps + savestates ----
        print("  [6c] Olimar 6-Pikmin pluck/whistle/D-Pad presets + swaps + savestates")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Olimar"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        settings = lab.find_settings()
        ss_addr = find_ss_block(emu)
        sb = FSW.SwapBlock(emu)
        sb.find()
        # Enable Olimar D-Pad Lineup Presets mode (bits 10..11 = 2 -> 0x0800) + preset slots.
        w16(emu, settings + OL.S_OLI_CMAT_MODE, 0x0800)
        w16(emu, settings + OL.S_OLI_PRESETS, 0x1234)
        emu.advance(4)
        for _ in range(7):
            emu.hold(0, buttons=["B"], stick=(128, 28))
            emu.advance(5)
            emu.neutral(0)
            emu.advance(20)
        # Whistle (Down-B when full / or trigger D-Pad Preset).
        emu.press(0, ["DUP"], frames=3, after=12)
        emu.press(0, ["DRIGHT"], frames=3, after=12)
        emu.hold(0, buttons=["B"], stick=(128, 28))  # Whistle
        emu.advance(6)
        emu.neutral(0)
        emu.advance(18)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat6c Olimar save failed")
        emu.advance(10)
        if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
            raise TortureError("cat6c Olimar load failed")
        # Reset Olimar D-Pad mode before swapping.
        w16(emu, settings + OL.S_OLI_CMAT_MODE, 0)
        apply_swap_live(emu, lab, sb, "Mario", 0, "Fox", 0)
        apply_swap_live(emu, lab, sb, "Olimar", 2, "Fox", 1)
        check_network_heap(emu, "cat6c-olimar")
        nav.finish_training(emu)
        emu.advance(20)
        check_no_problems(emu, mpos, "cat6c-olimar")

        # ---- 6d. Snake C4 / Cypher / Grenades + savestates ----
        print("  [6d] Snake C4 / Cypher / Grenades")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Snake"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        ss_addr = find_ss_block(emu)
        tm4x = S4C.find_tm4x(emu)
        emu.hold(0, buttons=["B"])  # Pull grenade
        emu.advance(6)
        emu.neutral(0)
        emu.advance(14)
        emu.hold(0, buttons=["B"], stick=(128, 28))  # Plant C4
        emu.advance(6)
        emu.neutral(0)
        emu.advance(24)
        emu.hold(0, buttons=["B"], stick=(128, 228))  # Cypher Up-B
        emu.advance(10)
        emu.neutral(0)
        emu.advance(20)
        if ss_cmd(emu, ss_addr, 1)[0] != "saved":
            raise TortureError("cat6d Snake save failed")
        emu.hold(0, buttons=["B"], stick=(128, 28))  # Detonate C4
        emu.advance(6)
        emu.neutral(0)
        emu.advance(20)
        if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
            raise TortureError("cat6d Snake load failed")
        s4 = S4C.read_tm4x(emu, tm4x)
        if s4["magic"] != S4C.TM4X_MAGIC:
            raise TortureError("cat6d TM4X corrupted")
        nav.finish_training(emu)
        emu.advance(20)
        check_no_problems(emu, mpos, "cat6d-snake")

        # ---- 6e. Tether grabbers (Lucas, Samus, Ivysaur) -> accurate Frame Meter kSf6Grab cells ----
        for tether_char in ("Lucas", "Samus", "Ivysaur"):
            print(f"  [6e] Tether grabber {tether_char} -> Frame Meter kSf6Grab cells")
            mpos = len(emu.log_text())
            nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO[tether_char],
                                  cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
            emu.advance(25)
            settings = lab.find_settings()
            tmal_addr = find_tmal(emu, mpos)
            tmgv_hits = GV.GRABVIZ_BLOCK_RE.findall(emu.log_text()[mpos:])
            if not tmgv_hits:
                raise TortureError(f"cat6e {tether_char}: TMGV debug block missing")
            tmgv_addr = int(tmgv_hits[-1][0], 16)
            w8(emu, settings + S_ACTION_LOG, 1)
            w8(emu, settings + S_GRAB_VIZ, 1)
            w8(emu, settings + S_CPU_CONTROL, CT.LAB)
            w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
            emu.advance(6)

            # Grounded tether grab (Z) + aerial Z-air (Jump + Z), stepping frame by frame.
            saw_grab_cell = False
            saw_tmgv = False
            emu.h.call("pause")
            emu.hold(0, buttons=["Z"])
            emu.h.call("frame_advance", n=3)
            emu.neutral(0)
            for _ in range(36):
                emu.h.call("frame_advance", n=1)
                fm = read_tmal_sf6(emu, tmal_addr)
                if any((c & SF6_KIND_MASK) == SF6_GRAB for c in fm["p1"][:fm["filled"]]):
                    saw_grab_cell = True
                gv = GV.read_tmgv(emu, tmgv_addr)
                if gv["grabShapes"] > 0 or gv["searchShapes"] > 0:
                    saw_tmgv = True
            # Also exercise aerial Z-air tether.
            emu.h.call("frame_advance", n=25)
            emu.hold(0, buttons=["X"])
            emu.h.call("frame_advance", n=6)
            emu.hold(0, buttons=["Z"])
            emu.h.call("frame_advance", n=3)
            emu.neutral(0)
            for _ in range(30):
                emu.h.call("frame_advance", n=1)
                fm = read_tmal_sf6(emu, tmal_addr)
                if any((c & SF6_KIND_MASK) == SF6_GRAB for c in fm["p1"][:fm["filled"]]):
                    saw_grab_cell = True
            emu.h.call("resume")
            if not saw_grab_cell:
                raise TortureError(f"cat6e {tether_char}: Frame Meter never recorded kSf6Grab cell on tether grab!")
            if not saw_tmgv:
                raise TortureError(f"cat6e {tether_char}: Grab Visualizer recorded 0 grab/search shapes!")
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, mpos, f"cat6e-{tether_char}")

        # ---- 6f. Item-spawn fighters (Wario Bike, Diddy Banana, Dedede Waddles, R.O.B. Gyro) ----
        item_cases = [
            ("Wario", ["B"], (228, 128), "Bike"),
            ("Diddy Kong", ["B"], (128, 28), "Banana"),
            ("Dedede", ["B"], (228, 128), "Waddle"),
            ("R.O.B.", ["B"], (128, 28), "Gyro"),
        ]
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Wario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        settings = lab.find_settings()
        enable_all_overlays_and_osds(emu, settings, psa=0, frame_meter=1, combo_enabled=0)
        ss_addr = find_ss_block(emu)
        sb = FSW.SwapBlock(emu)
        sb.find()
        for idx, (cname, btns, stick, item_name) in enumerate(item_cases):
            print(f"  [6f] Item-spawn fighter {cname} ({item_name})")
            if idx > 0:
                apply_swap_live(emu, lab, sb, cname, 0, "Fox", 0)
            emu.hold(0, buttons=btns, stick=stick)
            emu.advance(10)
            emu.neutral(0)
            emu.advance(20)
            if ss_cmd(emu, ss_addr, 1)[0] != "saved":
                raise TortureError(f"cat6f {cname} save with {item_name} failed")
            emu.advance(12)
            if ss_cmd(emu, ss_addr, 2)[0] != "loaded":
                raise TortureError(f"cat6f {cname} load with {item_name} failed")
            assert_clock_advances(emu, f"cat6f-{cname}", 15)
        nav.finish_training(emu)
        emu.advance(25)
        check_no_problems(emu, pos0, "cat6")
        print("  cat6 PASS: all transform, IC Nana, Olimar, Snake, tether, and item-spawn checks green")


# ============================================================================
# CATEGORY 4: Rapid Mode / Menu / Drill / Colorblind Hopping (16:9 & 4:3, MMU+LLE)
# ============================================================================

def run_cat4(rel):
    print("=== [cat4-strict-mode-menu-drill-hop] 16:9 & 4:3 under strict Wii MMU=True / Fastmem=False / LLE ===")
    strict_args = E.DETERMINISTIC_ARGS + [
        "-a", "LLE",
        "-C", "Dolphin.Core.CPUCore=5",
        "-C", "Dolphin.Core.MMU=True",
        "-C", "Dolphin.Core.Fastmem=False",
        "-C", "Dolphin.Core.PageTableFastmem=False",
        "-C", "Dolphin.Core.DSPHLE=False",
    ]
    with E.harness_run(E.headless_backend(), strict_args, allow_null=True, css_cache=False) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu, drill_list="keep")
        emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)

        def hop_boot_modals(aspect_tag):
            # Hop all 4 Boot tabs -> wrap around.
            for tab in ("Scenarios", "Tech Practice", "Minigames & Tech", "Quick Start"):
                DT.go_to_page(emu, tab, "R")
            # Z: Credits modal (open, scroll down & up, close).
            cpos = len(emu.log_text())
            emu.press(0, ["Z"], frames=4, after=8)
            ST.wait_line(emu, f"{aspect_tag} Boot Credits open", r"drills: credits opened", cpos, 180)
            for _ in range(4):
                emu.press(0, ["DDOWN"], frames=3, after=5)
            for _ in range(4):
                emu.press(0, ["DUP"], frames=3, after=5)
            emu.press(0, ["B"], frames=4, after=10)
            # Y: Quick Setup (open, toggle row, enter All OSDs, back, close).
            qpos = len(emu.log_text())
            emu.press(0, ["Y"], frames=4, after=8)
            ST.wait_line(emu, f"{aspect_tag} Quick Setup open", r"osdquick: opened", qpos, 180)
            emu.press(0, ["DRIGHT"], frames=3, after=6)
            for _ in range(7):
                emu.press(0, ["DDOWN"], frames=2, after=4)
            emu.press(0, ["A"], frames=3, after=6)  # All OSDs sub-view
            emu.press(0, ["B"], frames=3, after=6)  # back to Quick Setup root
            emu.press(0, ["B"], frames=4, after=10) # close Quick Setup
            # X: Recordings Browser (open, close).
            ST.open_browser(emu)
            emu.press(0, ["B"], frames=4, after=10)

        # ---- 16:9 full mode/drill/Lab hop ----
        LT.set_aspect(emu, "16:9", lambda _m: None)
        hop_boot_modals("16:9")

        # 1. Arm Scenario (Scenarios > Combo Lab) -> enter -> Lab scroll/subpages/Credits/double-Y/Colorblind -> Exit!
        DT.go_to_page(emu, "Scenarios", "R")
        DT.go_to_row(emu, 0)  # Combo Lab
        emu.press(0, ["A"], frames=4, after=8)
        emu.wait_for("Combo Lab armed", lambda: nav.drill_list_state(emu) == "closed", 180, every=2)
        mpos = len(emu.log_text())
        nav.start_training_match(emu, "mario", "fox", stage="final_destination", seed_rng=True)
        emu.advance(25)
        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        lab.open_menu()
        # Rapidly cycle categories & subpages.
        for path in ("Lab > Controls", "CPU > Counters & Reversals", "Display > Theme > Colorblind Mode"):
            lab.menu_nav_to(path)
        # Colorblind cycle: Off(0) -> Deuteran(1) -> Protan(2) -> Tritan(3) -> Off(0).
        for want_cb in (1, 2, 3, 0):
            lab.tap(stick=LT.RIGHT if want_cb != 0 else LT.LEFT)
            if want_cb == 0:
                lab.tap(stick=LT.LEFT)
                lab.tap(stick=LT.LEFT)
            got_cb = u8(emu, settings + S_OSD_COLORBLIND)
            if got_cb != want_cb:
                raise TortureError(f"cat4 16:9 Colorblind expected {want_cb}, got {got_cb}")
        # Double-Y reset on Theme page: mutate Menu Accent, press Y twice, assert restored to 0!
        lab.menu_select_row("Menu Accent")
        lab.tap(stick=LT.RIGHT)
        if u8(emu, settings + 112) == 0:
            raise TortureError("cat4 16:9 Menu Accent did not change before double-Y test")
        lab.tap(["Y"])
        if lab.read_menu_state()["reset_confirm_page"] == 0:
            raise TortureError("cat4 16:9 first Y did not arm page reset confirm")
        lab.tap(["Y"])
        if u8(emu, settings + 112) != 0:
            raise TortureError(f"cat4 16:9 second Y did not reset Menu Accent (got {u8(emu, settings + 112)})")
        # Open In-Mod Credits Modal from Lab left rail (utility after Exit), scroll & close!
        lab.menu_nav_to("Return to CSS")
        lab.tap(stick=LT.DOWN)  # Credits rail item below Return to CSS
        cpos = len(emu.log_text())
        lab.tap(["A"])
        ST.wait_line(emu, "cat4 Lab Credits open", r"credits: opened", cpos, 180)
        for _ in range(3):
            emu.press(0, ["DDOWN"], frames=3, after=5)
        emu.press(0, ["B"], frames=4, after=8)
        ST.wait_line(emu, "cat4 Lab Credits close", r"credits: closed", cpos, 180)
        # Return to CSS via Lab Menu rail.
        lab.menu_nav_to("Return to CSS")
        lab.tap(["A"])
        emu.wait_for("CSS after Scenario Exit", lambda: nav.scene(emu)[0] == "scSelctCharacter", 900, every=5)
        emu.advance(20)
        check_no_problems(emu, mpos, "cat4-16x9-scenario")

        # 2. Tech Practice (Wavedash) -> BTT -> Float/Hover & DJC -> Snake C4 -> Exit Drill!
        sd_runner = SD.Runner(emu)
        drill_hops = [
            ("Tech Practice", 1, 2, "fox", "mario"),           # Wavedash
            ("Minigames & Tech", 0, 17, "mario", "fox"),       # Break the Targets!
            ("Minigames & Tech", 1, 4, "peach", "fox"),        # Float / Hover & DJC
            ("Minigames & Tech", 2, 15, "snake", "fox"),       # Snake C4 Recovery
        ]
        for page_name, row_idx, drill_id, p1n, cpun in drill_hops:
            print(f"  [cat4] 16:9 drill hop: {page_name} row {row_idx} (id={drill_id}, {p1n})")
            mpos = len(emu.log_text())
            _p1, _cpu, tmdh = sd_runner.arm_skill_drill_and_enter(
                row_idx, drill_id, p1n, cpun, aspect="16:9", freeze_cpu=True, page_name=page_name
            )
            if drill_id == 17:
                # Break the Targets! is driven by combo_break_rush (TMBR), not drill_hud (TMDH).
                # Verify TMBR activated and DVD target texture loaded under MMU+LLE.
                tmbr = S3B.find_block(emu, S3B.RUSH_BLOCK_RE, mpos, "TMBR")
                for _ in range(90):
                    emu.advance(2)
                    st_br = S3B.read_tmbr(emu, tmbr)
                    if st_br["active"] == 1 and st_br["texLoaded"] == 1 and st_br["activeTargets"] >= 1:
                        break
                else:
                    raise TortureError(f"cat4 BTT did not activate / load DVD tex under MMU+LLE: {st_br}")
            else:
                dh = SD.read_tmdh(emu, tmdh)
                if dh["activeDrill"] != drill_id:
                    raise TortureError(f"cat4 drill {drill_id} did not activate: {dh}")
            emu.advance(15)
            # Exit Drill through real Drill Menu -> Exit Drill / Return to CSS!
            lab_d = LT.Lab(emu, [])
            lab_d.find_settings()
            lab_d.open_menu()
            lab_d.menu_nav_to("Return to CSS")
            lab_d.tap(["A"])
            emu.wait_for(f"CSS after drill {drill_id} Exit", lambda: nav.scene(emu)[0] == "scSelctCharacter", 900, every=5)
            emu.advance(20)
            check_no_problems(emu, mpos, f"cat4-drill-{drill_id}")

        # ---- 4:3 pass: Boot Menu + Credits + Quick Setup + Browser + Lab scroll/double-Y/Colorblind ----
        print("  [cat4] switching to 4:3 under MMU+LLE")
        LT.set_aspect(emu, "4:3", lambda _m: None)
        emu.wait_for("Boot Menu open in 4:3", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
        hop_boot_modals("4:3")
        # Enter Training Lab in 4:3 and exercise Lab scroll + Credits + double-Y + Colorblind cycle.
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(20)
        lab43 = LT.Lab(emu, [])
        s43 = lab43.find_settings()
        lab43.open_menu()
        lab43.menu_nav_to("Display > Theme > Colorblind Mode")
        for want_cb in (1, 2, 3, 0):
            lab43.tap(stick=LT.RIGHT if want_cb != 0 else LT.LEFT)
            if want_cb == 0:
                lab43.tap(stick=LT.LEFT)
                lab43.tap(stick=LT.LEFT)
            if u8(emu, s43 + S_OSD_COLORBLIND) != want_cb:
                raise TortureError(f"cat4 4:3 Colorblind expected {want_cb}, got {u8(emu, s43 + S_OSD_COLORBLIND)}")
        lab43.menu_select_row("Menu Accent")
        lab43.tap(stick=LT.RIGHT)
        if u8(emu, s43 + 112) == 0:
            raise TortureError("cat4 4:3 Menu Accent did not change before double-Y test")
        lab43.tap(["Y"])
        if lab43.read_menu_state()["reset_confirm_page"] == 0:
            raise TortureError("cat4 4:3 first Y did not arm page reset confirm")
        lab43.tap(["Y"])
        if u8(emu, s43 + 112) != 0:
            raise TortureError(f"cat4 4:3 second Y did not reset Menu Accent (got {u8(emu, s43 + 112)})")
        lab43.menu_nav_to("Return to CSS")
        lab43.tap(stick=LT.DOWN)
        cpos = len(emu.log_text())
        lab43.tap(["A"])
        ST.wait_line(emu, "cat4 4:3 Lab Credits open", r"credits: opened", cpos, 180)
        emu.press(0, ["B"], frames=4, after=8)
        ST.wait_line(emu, "cat4 4:3 Lab Credits close", r"credits: closed", cpos, 180)
        lab43.menu_nav_to("Return to CSS")
        lab43.tap(["A"])
        emu.wait_for("CSS after 4:3 exit", lambda: nav.scene(emu)[0] == "scSelctCharacter", 900, every=5)
        emu.advance(20)
        check_no_problems(emu, pos0, "cat4")
        print("  cat4 PASS: 16:9 & 4:3 full mode/menu/drill/Credits/QuickSetup/Colorblind hop under MMU+LLE")


# ============================================================================
# CATEGORY 10: Scene-Loop & Offline VS 4P Leak Torture (MMU=True, 10 cycles)
# ============================================================================

def run_cat10(rel):
    print("=== [cat10-scene-loop-vs4p-mmu] 10 consecutive Training->CSS->BTT->CSS->VS4P->CSS loops (MMU=True) ===")
    mmu_args = E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS + [
        "-C", "Dolphin.Core.CPUCore=5",
        "-C", "Dolphin.Core.MMU=True",
        "-C", "Dolphin.Core.Fastmem=False",
        "-C", "Dolphin.Core.PageTableFastmem=False",
    ]
    with E.harness_run(E.headless_backend(), mmu_args, allow_null=True, css_cache=False) as (_proc, emu):
        pos0 = len(emu.log_text())
        for p in range(4):
            emu.neutral(p)
        nav.boot_to_training_css(emu)

        css_heaps = []
        vs_chars = ("mario", "fox", "luigi", "jigglypuff")

        for cyc in range(10):
            print(f"  [cat10] cycle {cyc + 1}/10: Training -> CSS -> BTT -> CSS -> Offline VS 4P -> CSS")
            # 1. Training Lab match (enable offlineVersus=1 + Wavedash OSD + Frame Meter on cycle 0).
            mpos = len(emu.log_text())
            nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                                  cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
            emu.advance(20)
            if cyc == 0:
                lab = LT.Lab(emu, [])
                settings = lab.find_settings()
                w8(emu, settings + S_OFFLINE_VS, 1)
                w8(emu, settings + S_OSD_WAVEDASH, 1)
                w8(emu, settings + S_ACTION_LOG, 1)
            emu.advance(15)
            check_network_heap(emu, f"cat10-c{cyc + 1}-training-match")
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, mpos, f"cat10-c{cyc + 1}-training")

            # 2. Break the Targets! (DVD stream) -> CSS.
            mpos = len(emu.log_text())
            nav.start_quick_match(emu, stage=STAGE_BF, p1=FSW.ROSTER_INFO["Fox"],
                                  cpu=FSW.ROSTER_INFO["Mario"], seed_rng=True)
            emu.advance(15)
            tmbr = S3B.find_block(emu, S3B.RUSH_BLOCK_RE, mpos, "TMBR")
            w8(emu, tmbr + S3B.TMBR_CMD_OFF, 3)
            for _ in range(80):
                emu.advance(2)
                if S3B.read_tmbr(emu, tmbr)["texLoaded"] == 1:
                    break
            else:
                raise TortureError(f"cat10 cycle {cyc + 1}: BTT DVD texture did not load")
            emu.advance(10)
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, mpos, f"cat10-c{cyc + 1}-btt")

            # 3. Training CSS -> Offline VS 4P (Displays ON, MTCH + XTRA in AssistFigureResource) -> CSS.
            training_css_to_versus_css(emu)
            for port, ch in enumerate(vs_chars):
                a = nav.css_area(emu, port)
                if not a or a["kind"] == 0:
                    emu.press(port, ["A"], frames=4, after=10)
                    emu.wait_for(f"VS P{port + 1} join",
                                 lambda p=port: (nav.css_area(emu, p) or {}).get("kind") in (1, 2), 180, every=2)
                emu.advance(5)
                if nav.css_area(emu, port)["held"]:
                    nav.drop_own_token(emu, port, ch)
            emu.advance(10)

            vs_pos = len(emu.log_text())
            nav.press_until(emu, "VS stage select", ["START"], lambda: nav.scene(emu)[0] == "scSelStage", 600)
            nav.pick_stage(emu, "final_destination")
            nav.wait_match_start(emu)
            emu.advance(20)
            vs_log = emu.log_text()[vs_pos:]
            if not L.rx(L.LOADED).search(vs_log) or not L.rx(L.EXTRA_LOADED).search(vs_log):
                raise TortureError(f"cat10 cycle {cyc + 1}: Offline VS 4P missing MTCH or XTRA load")
            check_network_heap(emu, f"cat10-c{cyc + 1}-vs4p-match")
            # Exit VS 4P cleanly via Pause (Start) -> L+R+A+Start (No Contest), returning to VS CSS.
            exited_fast = False
            for _attempt in range(3):
                emu.hold(0, buttons=["START"])
                emu.advance(18)
                emu.neutral(0)
                emu.advance(8)
                emu.hold(0, buttons=["L", "R", "A", "START"])
                emu.advance(12)
                emu.neutral(0)
                for _ in range(30):
                    emu.advance(4)
                    if nav.scene(emu)[0] != "scMelee":
                        exited_fast = True
                        break
                if exited_fast:
                    break
            if exited_fast:
                # No Contest lands on scVsResult in P+; press A across all 4 ports until VS CSS returns.
                for _ in range(80):
                    if nav.scene(emu)[0] == "scSelctCharacter":
                        break
                    for p in range(4):
                        emu.press(p, ["A"], frames=3, after=4)
                    emu.advance(12)
                if nav.scene(emu)[0] != "scSelctCharacter":
                    raise TortureError(f"cat10 cycle {cyc + 1}: VS No Contest stuck at {nav.scene(emu)[:2]}")
            else:
                seen = nav.finish_versus_by_falling(emu, 4)
                if nav.scene(emu)[1] != "sqVsMelee":
                    raise TortureError(f"cat10 cycle {cyc + 1}: VS did not return to sqVsMelee ({seen})")
            if nav.scene(emu)[1] != "sqVsMelee":
                raise TortureError(f"cat10 cycle {cyc + 1}: VS returned to {nav.scene(emu)[:2]}, not sqVsMelee")
            emu.advance(20)
            check_no_problems(emu, vs_pos, f"cat10-c{cyc + 1}-vs4p")

            # 4. Versus CSS -> Training CSS.
            versus_css_to_training_css(emu)
            emu.wait_for("Training CSS after VS", lambda: nav.scene(emu)[1] == "sqTraining", 600, every=5)
            emu.advance(20)

            # Sample every persistent CSS heap plus aggregate MEM1/MEM2 totals on Training CSS.
            # Note: P+ 3.2.1 retains a 971,424 B scVsResult replay/results buffer in Heaps::Network across CSS
            # after Versus (even with offlineVersus=0) and frees it on next scMelee; in-match Network headroom
            # (>= 0x10BA20) is asserted inside every Training and VS4P match above, while CSS checks flatness.
            ht = L.heap_table(emu)
            required_css_heaps = ("Network", "OverlayMenu", "Sound", "MenuInstance", "System")
            missing_heaps = [h for h in required_css_heaps if h not in ht]
            if missing_heaps:
                raise TortureError(f"cat10 cycle {cyc + 1}: expected CSS heaps missing from heap_table: {missing_heaps}")
            mem1_used = sum(v[3] for v in ht.values() if v[0] < 0x90000000)
            mem2_used = sum(v[3] for v in ht.values() if v[0] >= 0x90000000)
            net_l, _, _, net_alloc = WS._network_heap_stats(emu)
            css_heaps.append((ht, net_l, net_alloc, mem1_used, mem2_used))
            if cyc >= 2:
                # Compare against cycle 1 (post-warmup steady state) for leaks across all persistent heaps + MEM1/MEM2!
                base_ht, _, _, base_mem1, base_mem2 = css_heaps[1]
                for hname in sorted(set(base_ht) & set(ht)):
                    b_alloc, b_used = base_ht[hname][2], base_ht[hname][3]
                    c_alloc, c_used = ht[hname][2], ht[hname][3]
                    if c_alloc > b_alloc + 2 or c_used > b_used + 0x4000:
                        raise TortureError(
                            f"cat10 cycle {cyc + 1}: heap {hname} grew vs warmup: "
                            f"alloc {b_alloc}->{c_alloc}, used {b_used:#x}->{c_used:#x}"
                        )
                if mem1_used > base_mem1 + 0x10000 or mem2_used > base_mem2 + 0x20000:
                    raise TortureError(
                        f"cat10 cycle {cyc + 1}: aggregate MEM1/MEM2 grew vs warmup: "
                        f"MEM1 {base_mem1:#x}->{mem1_used:#x}, MEM2 {base_mem2:#x}->{mem2_used:#x}"
                    )

        # Final Training match check to complete the 10th -> Training transition.
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(20)
        nav.finish_training(emu)
        emu.advance(25)
        log = emu.log_text()[pos0:]
        check_no_problems(emu, pos0, "cat10-final")
        xtra_loads = L.rx(L.EXTRA_LOADED).findall(log)
        xtra_unloads = L.rx(L.EXTRA_UNLOADED.replace("(yes|NO)", "yes")).findall(log)
        if len(xtra_loads) < 31 or len(xtra_unloads) < 31:
            raise TortureError(f"cat10 expected >=31 clean XTRA loads/unloads (3*10+1), got {len(xtra_loads)}/{len(xtra_unloads)}")
        print(f"  cat10 PASS: 10 full Training->BTT->VS4P loops under MMU=True, {len(xtra_loads)} clean XTRA reloads, 0 leaks")


# ============================================================================
# CATEGORY 7: Freeze / Frame-Advance / Modal Race Torture
# ============================================================================

def run_cat7(rel):
    print("=== [cat7-freeze-fa-modal-race] Lab/Credits/BrawlPause/DrillModal/StartPaused/Hitlag/Fade races ===")
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=False) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu)

        # ---- 7a. Start Paused hold (`s_holding == true`) + Lab Menu Return to CSS (Real Bug #1!) ----
        print("  [7a] Recording Start Paused hold + Lab Menu Return to CSS & Training Menu (Z)")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        rec = RT.Rec(emu)
        rec.find()
        for _ in range(40):
            if S3D.read_rec_block(emu, rec.addr)["flags"] & RT.FLAG_STORAGE:
                break
            emu.advance(2)
        # Enable Start Paused, save positions, set CPU Playback, and restore -> s_holding == true!
        w8(emu, settings + S_REC_START_PAUSED, 1)
        rec.request("save")
        emu.advance(10)
        rec.set(cpuMode=RT.CPU_MODE["Playback"], cpuSlot=1, hmnMode=RT.HMN_MODE["Off"])
        rec.request("restore")
        emu.advance(12)
        if not scheduler_frozen(emu):
            raise TortureError("cat7a Start Paused did not freeze match as precondition")
        # First test: open Lab Menu while s_holding == true, press Z -> Brawl Training pause must open (not softlock)!
        lab.open_menu()
        lab.tap(["Z"])
        emu.wait_for("cat7a Brawl Training pause from Start Paused hold", lambda: nav.training_paused(emu), 180, every=2)
        nav.resume_training(emu)
        emu.advance(10)
        # Restore again so s_holding == true, then open Lab Menu -> Return to CSS -> MUST NOT SOFTLOCK!
        rec.request("restore")
        emu.advance(12)
        if not scheduler_frozen(emu):
            raise TortureError("cat7a second Start Paused restore did not hold freeze")
        lab.open_menu()
        lab.menu_nav_to("Return to CSS")
        lab.tap(["A"])
        try:
            emu.wait_for("cat7a Return to CSS while Start Paused holding",
                         lambda: nav.scene(emu)[0] == "scSelctCharacter", 360, every=5)
        except Timeout:
            raise TortureError(
                f"cat7a REAL BUG: permanent softlock on Return to CSS while Recording Start Paused is holding! "
                f"(scene={nav.scene(emu)[:2]}, frozen={scheduler_frozen(emu)})"
            )
        emu.advance(15)
        if scheduler_frozen(emu):
            raise TortureError("cat7a freeze bit stuck after Return to CSS!")
        check_no_problems(emu, mpos, "cat7a")

        # ---- 7b. Rapid open/close Lab Menu, Credits Modal, Brawl Training (Z), mid-Frame-Advance ----
        print("  [7b] Rapid modal storm mid-Frame-Advance & mid-hitlag")
        mpos = len(emu.log_text())
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        settings = lab.find_settings()
        ss_addr = find_ss_block(emu)
        w8(emu, settings + S_REC_START_PAUSED, 0)
        w8(emu, settings + LT.SET_FRAME_ADVANCE, 1)
        emu.advance(10)
        for i in range(6):
            lab.open_menu()
            if i % 2 == 0:
                lab.menu_nav_to("Return to CSS")
                lab.tap(stick=LT.DOWN)  # Credits
                cpos = len(emu.log_text())
                lab.tap(["A"])
                ST.wait_line(emu, "cat7b Credits open", r"credits: opened", cpos, 120)
                emu.press(0, ["B"], frames=3, after=6)
                ST.wait_line(emu, "cat7b Credits close", r"credits: closed", cpos, 120)
            if i % 3 == 1:
                lab.tap(["Z"])
                emu.wait_for("cat7b Brawl pause", lambda: nav.training_paused(emu), 120, every=2)
                nav.resume_training(emu)
                emu.advance(6)
            else:
                lab.close_menu()
                emu.advance(4)
            # Step 2 frames with L (Frame Advance button).
            emu.press(0, ["L"], frames=2, after=4)
            emu.press(0, ["L"], frames=2, after=4)
        w8(emu, settings + LT.SET_FRAME_ADVANCE, 0)
        emu.advance(10)
        if scheduler_frozen(emu):
            raise TortureError("cat7b freeze bit stuck after Frame Advance modal storm!")
        assert_clock_advances(emu, "cat7b-post-fa-storm", 20)

        # ---- 7c. Mid-hitlag + mid-savestate-load + mid-Ledgedash fade & OSD eviction races ----
        print("  [7c] Mid-hitlag, mid-savestate, mid-Ledgedash fade & OSD eviction modal races")
        enable_all_overlays_and_osds(emu, settings, psa=0, frame_meter=1, combo_enabled=0)
        w8(emu, settings + S_CPU_CONTROL, CT.LAB)
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
        g = CT.Game(emu)
        for _ in range(80):
            dx = g.fighter(g.entry(1))["x"] - g.fighter(g.entry(0))["x"]
            if abs(dx) < 9.5:
                break
            emu.hold(0, stick=(228 if dx > 0 else 28, 128))
            emu.advance(2)
        emu.neutral(0)
        # Trigger Mario F-Smash hitlag -> open/close Lab + Credits + savestate mid-hitlag!
        emu.hold(0, buttons=["A"], stick=(228 if g.fighter(g.entry(1))["x"] > g.fighter(g.entry(0))["x"] else 28, 128))
        emu.advance(12)
        emu.neutral(0)
        ss_cmd(emu, ss_addr, 1)
        lab.open_menu()
        lab.close_menu()
        ss_cmd(emu, ss_addr, 2)
        # Rapidly flood OSDs (wavedash / shorthop / fastfall) and race Lab Menu + Brawl Pause mid-eviction!
        for _ in range(4):
            emu.hold(0, buttons=["X"])
            emu.advance(2)
            emu.hold(0, buttons=["L"], stick=(210, 68))
            emu.advance(4)
            emu.neutral(0)
            emu.advance(6)
            lab.open_menu()
            lab.close_menu()
        assert_clock_advances(emu, "cat7c-post-osd-eviction", 20)
        # Capture D3D screenshot to confirm zero stuck scissor / stuck fade overlay!
        shot_7c = emu.screenshot("torture-cat7-post-races")
        check_d3d_not_blank(shot_7c, "cat7c-d3d-no-stuck-scissor-or-fade")
        # Exit mid-hitlag via Return to CSS.
        lab.open_menu()
        lab.menu_nav_to("Return to CSS")
        lab.tap(["A"])
        emu.wait_for("cat7c Return to CSS", lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
        emu.advance(15)
        if scheduler_frozen(emu):
            raise TortureError("cat7c freeze bit stuck on CSS")

        # ---- 7d. Retry / End-of-Set Drill Modal + Frame Advance race ----
        print("  [7d] Retry / End-of-Set Drill Modal + Frame Advance race (Retry / Lab / CSS)")
        sd_runner = SD.Runner(emu)
        mpos = len(emu.log_text())
        _p1, _cpu, tmdh = sd_runner.arm_skill_drill_and_enter(
            2, 1, "mario", "fox", aspect="16:9", freeze_cpu=True, page_name="Tech Practice"
        )
        # Turn Frame Advance ON, arm End-of-Set completion, and step frames with L so
        # drillHudFrame() completes mid-Frame-Advance and opens the End-of-Set modal!
        w8(emu, sd_runner.settings + LT.SET_FRAME_ADVANCE, 1)
        emu.advance(6)
        SD.trigger_force_complete(emu, tmdh)
        for _ in range(12):
            emu.press(0, ["L"], frames=2, after=4)
            if SD.read_tmdh(emu, tmdh)["modalOpen"] == 1:
                break
        if SD.read_tmdh(emu, tmdh)["modalOpen"] != 1:
            raise TortureError(f"cat7d modal did not open on Frame-Advance step: {SD.read_tmdh(emu, tmdh)}")
        # Press A (Retry) with Frame Advance turned off right before -> must unpause & advance cleanly!
        w8(emu, sd_runner.settings + LT.SET_FRAME_ADVANCE, 0)
        emu.press(0, ["A"], frames=4, after=12)
        emu.wait_for("cat7d modal closed on Retry", lambda: SD.read_tmdh(emu, tmdh)["modalOpen"] == 0, 120, every=2)
        if SD.read_tmdh(emu, tmdh)["retryCount"] < 1:
            raise TortureError("cat7d Retry did not increment retryCount")
        assert_clock_advances(emu, "cat7d-post-retry", 20)

        # Trigger End-of-Set modal again -> press Start (Switch to Lab) -> verify clean live Lab!
        SD.trigger_force_complete(emu, tmdh)
        emu.wait_for("cat7d modal open #2", lambda: SD.read_tmdh(emu, tmdh)["modalOpen"] == 1, 120, every=2)
        emu.press(0, ["START"], frames=4, after=15)
        emu.wait_for("cat7d modal closed on Start->Lab", lambda: SD.read_tmdh(emu, tmdh)["modalOpen"] == 0, 120, every=2)
        assert_clock_advances(emu, "cat7d-post-start-lab", 20)
        nav.finish_training(emu)
        emu.advance(15)

        # Trigger End-of-Set modal WITH Frame Advance ON -> step with L -> press B (Return to CSS)
        # while Frame Advance is STILL ON -> must exit cleanly and NOT stick freeze bit!
        _p1, _cpu, tmdh2 = sd_runner.arm_skill_drill_and_enter(
            1, 2, "fox", "mario", aspect="16:9", freeze_cpu=True, page_name="Tech Practice"
        )
        w8(emu, sd_runner.settings + LT.SET_FRAME_ADVANCE, 1)
        emu.advance(6)
        SD.trigger_force_complete(emu, tmdh2)
        for _ in range(12):
            emu.press(0, ["L"], frames=2, after=4)
            if SD.read_tmdh(emu, tmdh2)["modalOpen"] == 1:
                break
        if SD.read_tmdh(emu, tmdh2)["modalOpen"] != 1:
            raise TortureError(f"cat7d modal #3 did not open on Frame-Advance step: {SD.read_tmdh(emu, tmdh2)}")
        emu.press(0, ["B"], frames=4, after=12)
        emu.wait_for("cat7d modal B -> CSS", lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
        emu.advance(15)
        if scheduler_frozen(emu):
            raise TortureError("cat7d freeze bit stuck after Drill modal B -> CSS")
        check_no_problems(emu, pos0, "cat7")
        print("  cat7 PASS: zero stuck freeze bits, zero softlocks, zero scissor/fade corruption")


# ============================================================================
# CATEGORY 8: 4-Controller Hybrid & Port-Control Chaos
# ============================================================================

def run_cat8(rel):
    print("=== [cat8-4ctrl-hybrid-port-control] GC + Classic + Nunchuk + Sideways + CPU Port Control ===")
    # Hybrid setup: GC ports 1 & 2 (via FOUR_PADS_ARGS) + Emulated Wiimote 1 & 2 (ports 5 & 6).
    with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS,
                       allow_null=True, wiimotes=(1, 1, 0, 0), css_cache=False) as (_proc, emu):
        pos0 = len(emu.log_text())
        for p in range(4):
            emu.neutral(p)
        nav.boot_to_training_css(emu, drill_list="keep")
        emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)

        # ---- 8a. Boot Menu & Credits Modal across GC Port 1, GC Port 2, Classic, Nunchuk, Sideways ----
        print("  [8a] Hybrid 4-controller Boot Menu & Credits Modal")
        before_css = DT.css_snapshot(emu)
        # GC Port 1 opens & closes Credits Modal.
        cpos = len(emu.log_text())
        emu.press(0, ["Z"], frames=4, after=8)
        ST.wait_line(emu, "cat8a GC1 Credits open", r"drills: credits opened", cpos, 120)
        emu.press(0, ["DDOWN"], frames=3, after=6)
        emu.press(0, ["B"], frames=4, after=10)
        # GC Port 2 takes over & switches page.
        emu.press(1, ["DDOWN"], frames=4, after=8)
        ST.wait_line(emu, "cat8a GC2 takeover", r"drills: list handed to port 2", cpos, 120)
        emu.press(1, ["R"], frames=4, after=8)
        emu.press(1, ["L"], frames=4, after=8)

        # Wii Port 5 across Classic, Nunchuk, Sideways + Wii Port 6 Classic simultaneously attached!
        DT.wii_set_extension(emu, "classic", DT.PAD_TYPE_CLASSIC, port=5)
        for label, ext, ptype, ctrl in DT.WII_CONTROLS:
            DT.wii_set_extension(emu, ext, ptype, port=4)
            # Claim ownership if needed (first press claims, second acts).
            hpos = len(emu.log_text())
            DT.wii_press(emu, ctrl["down"], port=4)
            emu.advance(8)
            DT.wii_press(emu, ctrl["down"], port=4)
            DT.wii_press(emu, ctrl["up"], port=4)
            # Open Credits modal with each Wii controller's Credits binding:
            # Classic '+', Nunchuk '1', Sideways '- + 2'
            c_open_pos = len(emu.log_text())
            if label == "classic":
                DT.wii_press(emu, ctrl["start"], port=4)
            elif label == "nunchuk":
                DT.wii_press(emu, ctrl["one"], port=4)
            else:
                DT.wii_press(emu, {"wiimote": {"Buttons/-": 1, "Buttons/2": 1}}, port=4)
            ST.wait_line(emu, f"cat8a {label} Credits open", r"drills: credits opened", c_open_pos, 180)
            DT.wii_press(emu, ctrl["down"], port=4)
            DT.wii_press(emu, ctrl["back"], port=4)
            emu.advance(8)

        # Close Boot Menu via GC Port 1 and assert ZERO leaked input into CSS!
        emu.press(0, ["B"], frames=4, after=8)  # takeover
        emu.press(0, ["B"], frames=4, after=10) # close
        emu.wait_for("cat8a Boot Menu closed", lambda: nav.drill_list_state(emu) == "closed", 120, every=2)
        if DT.css_snapshot(emu) != before_css:
            raise TortureError(f"cat8a leaked CSS input across hybrid controllers: {before_css} -> {DT.css_snapshot(emu)}")

        # ---- 8b. In-Match Lab Menu, Savestates, Frame Advance & Zero Leaked Input on Close ----
        print("  [8b] Hybrid Lab Menu, Savestates, Frame Advance & zero-leak close")
        nav.start_quick_match(emu, stage=STAGE_FD, p1=FSW.ROSTER_INFO["Mario"],
                              cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
        emu.advance(25)
        lab = LT.Lab(emu, [])
        settings = lab.find_settings()
        g = CT.Game(emu)
        w8(emu, settings + S_CPU_CONTROL, CT.LAB)
        w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])

        # Test GC Port 1 button held across Lab Menu close -> zero leaked jump!
        lab.open_menu()
        emu.hold(0, buttons=["X"])
        emu.advance(5)
        emu.hold(0, buttons=["X", "START"])
        emu.advance(3)
        emu.hold(0, buttons=["X"])
        leaked = False
        for _ in range(25):
            emu.advance(1)
            if g.fighter(g.entry(0))["status"] != CT.STATUS["Wait"]:
                leaked = True
        emu.neutral(0)
        if leaked:
            raise TortureError("cat8b GC Port 1 leaked X jump across Lab Menu close!")

        # Exercise Classic, Nunchuk, and Sideways Wii Remote (port 5) on Lab Menu, Savestate, Frame Advance.
        for label, ext, ptype, ctrl in DT.WII_CONTROLS:
            DT.wii_set_extension(emu, ext, ptype, port=4)
            m_pos = len(emu.log_text())
            DT.wii_press(emu, ctrl["start"], port=4)
            ST.wait_line(emu, f"cat8b {label} Lab open", r"lab: menu opened by port 5", m_pos, 180)
            DT.wii_press(emu, ctrl["down"], port=4)
            DT.wii_press(emu, ctrl["start"], port=4)
            ST.wait_line(emu, f"cat8b {label} Lab close", r"lab: menu closed", m_pos, 180)
            emu.advance(10)
            # Frame Advance ON -> verify clean stepping & off.
            w8(emu, settings + LT.SET_FRAME_ADVANCE, 1)
            emu.advance(6)
            fa_btn = ctrl["l"] if label == "classic" else (ctrl["z"] if label == "nunchuk" else ctrl["minus"])
            f0 = nav.game_frame(emu)
            DT.wii_press(emu, fa_btn, frames=4, after=8, port=4)
            f1 = nav.game_frame(emu)
            w8(emu, settings + LT.SET_FRAME_ADVANCE, 0)
            emu.advance(6)
            if f1 <= f0:
                raise TortureError(f"cat8b {label} Frame Advance did not step ({f0} -> {f1})")

        # Quick Savestate via GC D-Pad with hybrid Wii controllers active.
        _, serr = FS.dpad_save(emu)
        if serr is not None:
            raise TortureError(f"cat8b GC D-pad save failed: {serr}")
        emu.advance(20)
        _, lerr = FS.dpad_load(emu)
        if lerr is not None:
            raise TortureError(f"cat8b GC D-pad load failed: {lerr}")

        # ---- 8c. CPU > Port Control + Sustained Shield + C-Stick Buffering + Rapid Port Switching ----
        print("  [8c] CPU > Port Control: sustained Shield + C-Stick buffer + rapid port switching")
        # Assign CPU to Port 2 (GC port 2 -> controlledBy = 2).
        w8(emu, settings + S_CPU_CONTROLLED_BY, 2)
        emu.advance(12)
        cpu_en = g.entry(1)
        # Hold sustained Shield (L) on Port 2 -> verify CPU enters Guard!
        emu.hold(1, buttons=["L"], l=255)
        emu.wait_for("cat8c Port 2 Shield",
                     lambda: g.fighter(cpu_en)["status"] in (CT.STATUS["Guard_On"], CT.STATUS["Guard"]),
                     90, every=1)
        # While holding Shield, buffer C-Stick Left/Right/Up (roll / spotdodge / jump-upsmash OoS)!
        emu.hold(1, buttons=["L"], l=255, cstick=(28, 128))
        saw_roll_or_act = False
        for _ in range(30):
            emu.advance(1)
            st = g.fighter(cpu_en)["status"]
            if st in (CT.STATUS["Escape_B"], CT.STATUS["Escape_F"], CT.STATUS["Escape"]):
                saw_roll_or_act = True
                break
        if not saw_roll_or_act:
            raise TortureError(f"cat8c Port 2 Shield + C-Stick buffer did not roll (st={g.fighter(cpu_en)['status']:#x})")
        # Rapidly switch controlledBy 2 -> 3 -> 4 -> 2 -> 0 WHILE Port 2 is still holding buttons!
        emu.hold(1, buttons=["L", "A"], l=255, stick=(228, 128), cstick=(128, 228))
        for target_port in (3, 4, 2, 3, 0):
            w8(emu, settings + S_CPU_CONTROLLED_BY, target_port)
            emu.advance(6)
        emu.neutral(1)
        # Brain::tools runs restorePort() when controlledBy returns to 0; open/close Lab Menu also verifies clean round-trip.
        lab.open_menu()
        lab.close_menu()
        emu.advance(15)
        ai_main, _ = g.inputs(cpu_en)
        pad_after = g.pad(ai_main)
        if (pad_after["buttons"] & 0x00FF) != 0:
            raise TortureError(f"cat8c stuck CPU buttons after Port Control off: {pad_after}")
        assert_clock_advances(emu, "cat8-final-clock", 20)

        nav.finish_training(emu)
        emu.advance(25)
        check_no_problems(emu, pos0, "cat8")
        print("  cat8 PASS: 4-controller hybrid + Credits + Lab + FA + Port Control + Shield/C-Stick buffer clean")


# ============================================================================
# CATEGORY 9: Extreme Stage, Camera & GX State-Leak Torture (D3D Render)
# ============================================================================

def run_cat9(rel):
    print("=== [cat9-extreme-stage-camera-gx-d3d] 6 stages x Camera/Hazards/AllOverlays/Toggles (real D3D) ===")
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    stages = [
        ("fountain_of_dreams", STAGE_FOD),
        ("yoshis_story", STAGE_YOSHIS),
        ("pokemon_stadium_2", STAGE_PS2),
        ("battlefield", STAGE_BF),
        ("final_destination", STAGE_FD),
        ("castle_siege", STAGE_CASTLE_SIEGE),
    ]
    with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=False) as (_proc, emu):
        pos0 = len(emu.log_text())
        nav.boot_to_training_css(emu)

        for s_name, s_id in stages:
            print(f"  [cat9] stage {s_name} ({s_id:#x})")
            mpos = len(emu.log_text())
            nav.start_quick_match(emu, stage=s_id, p1=FSW.ROSTER_INFO["Fox"],
                                  cpu=FSW.ROSTER_INFO["Mario"], seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            settings = lab.find_settings()
            enable_all_overlays_and_osds(emu, settings, psa=0, frame_meter=1, combo_enabled=1)
            w8(emu, settings + S_CPU_CONTROL, CT.LAB)
            w8(emu, settings + S_CPU_BEHAVIOR, CT.BEHAVIOR["Stand"])
            tmtm_addr = HTM.find_tmtm(emu)
            cb = RC.ComboBlock(emu)
            cb.find()
            hm_hits = S3D.HEATMAP_BLOCK_RE.findall(emu.log_text()[mpos:])
            hm_addr = int(hm_hits[-1], 16) if hm_hits else None

            # Cycle Camera Mode (Normal 0 -> Zoom 1 -> Fixed 2 -> Normal 0) and Stage Hazards (Off=2 -> Default=0).
            for cam_mode, haz in ((1, 2), (2, 2), (0, 0)):
                w8(emu, settings + S_CAMERA, cam_mode)
                w8(emu, settings + SO.S_STAGE_HAZARDS, haz)
                # Perform attack + grab + shield while rapidly toggling Fighter Models, Stage Model, Effects, HUD!
                emu.hold(0, buttons=["A"], stick=(228, 128))
                for step_i in range(6):
                    w8(emu, settings + S_MODELS, step_i % 4)
                    w8(emu, settings + S_STAGE_MODEL, step_i % 2)
                    w8(emu, settings + S_EFFECTS, (step_i + 1) % 2)
                    w8(emu, settings + S_HUD, step_i % 2)
                    emu.advance(3)
                emu.neutral(0)
                # Restore all models/stage/effects/HUD ON and verify finite geometry!
                w8(emu, settings + S_MODELS, 3)
                w8(emu, settings + S_STAGE_MODEL, 1)
                w8(emu, settings + S_EFFECTS, 1)
                w8(emu, settings + S_HUD, 1)
                emu.advance(6)

                # Assert camera eye & target are finite (zero NaN/Inf).
                cam_vals = struct.unpack(">6f", emu.read(WS.CAMERA_MGR_ADDR + 0x60, 0x18))
                if not all(is_finite(v, 5000.0) for v in cam_vals):
                    raise TortureError(f"cat9 {s_name} cam={cam_mode}: non-finite camera {cam_vals}")
                # Assert TMTM & TMCT world anchors are finite.
                tm = HTM.read_tmtm(emu, tmtm_addr)
                for sl in tm["slots"]:
                    if sl["active"] and not (is_finite(sl["worldX"], 5000.0) and is_finite(sl["worldY"], 5000.0)
                                             and is_finite(sl["ratio"], 10.0)):
                        raise TortureError(f"cat9 {s_name}: non-finite TMTM slot {sl}")
                cs = cb.slot(1)
                if not (is_finite(cs["anchorX"], 5000.0) and is_finite(cs["anchorY"], 5000.0)):
                    raise TortureError(f"cat9 {s_name}: non-finite TMCT anchor {cs}")
                if hm_addr is not None:
                    hb = S3D.read_heatmap_block(emu, hm_addr)
                    for b in hb["bounds"]:
                        if b["valid"] and not all(is_finite(b[k], 5000.0) for k in ("worldXMin", "worldXMax", "worldYMin", "worldYMax")):
                            raise TortureError(f"cat9 {s_name}: non-finite TMHM bounds {b}")

            # Verify scene root layer bits are cleanly restored (layers 0, 4, 5, 8 ON).
            for layer_i in (0, 4, 5, 8):
                bit = emu.u8(DO.ROOT + DO.LAYER_BYTE + DO.LAYER_STEP * layer_i) & DO.LAYER_ON
                if not bit:
                    raise TortureError(f"cat9 {s_name}: gfSceneRoot layer {layer_i} stuck OFF after toggles!")

            # Real D3D render verification screenshot on every stage with all overlays active!
            shot = emu.screenshot(f"torture-cat9-{s_name}")
            check_d3d_not_blank(shot, f"cat9-{s_name}")

            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, mpos, f"cat9-{s_name}")

        check_no_problems(emu, pos0, "cat9")
        print("  cat9 PASS: all 6 stages rendered cleanly in D3D with zero GX/layer/NaN leaks")


# ============================================================================
# CATEGORY 2: 4-Player Max-Load Heap & Display Stress (Writable SD)
# ============================================================================

def run_cat2(rel):
    print("=== [cat2-4p-maxload-heap-display] 4P on FoD & Yoshi's Story + ALL displays + swaps + Browser + SD ===")
    img = E.storage_image(rel)
    try:
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS,
                           allow_null=True, writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu)

            # Match 1: Fountain of Dreams (0x1F) - export recording first (1P+1CPU), then ramp to 4P!
            nav.start_quick_match(emu, stage=STAGE_FOD, p1=FSW.ROSTER_INFO["Ice Climbers"],
                                  cpu=FSW.ROSTER_INFO["Olimar"], seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            settings = lab.find_settings()
            # Export one recording so Browser scans a real .tmr later.
            r = RT.Runner(emu)
            r.refresh()
            emu.advance(10)
            r.run_request("save")
            RT.record_hmn(r)
            emu.h.call("resume")
            lab.open_menu()
            lab.menu_nav_to("Recording > Playback & Positions > Export Recording")
            epos = lab.log_pos()
            lab.tap(["A"])
            ST.wait_line(emu, "cat2 export", r"storage: exported \S+", epos, 300)
            lab.close_menu()
            emu.advance(10)

            # Ramp to 4 fighters (3 CPUs) + turn ALL displays & 4P OSDs ON!
            nav.set_training_cpus(emu, 3)
            emu.advance(20)
            enable_all_overlays_and_osds(emu, settings, psa=1, frame_meter=1, combo_enabled=1)
            emu.advance(45)
            check_network_heap(emu, "cat2-fod-4p-all-displays")

            # Mid-match fighter & costume swaps in 4P on FoD!
            sb = FSW.SwapBlock(emu)
            sb.find()
            for p1n, p1c, cpun, cpuc in (("Snake", 1, "Dedede", 2), ("Kirby", 2, "Samus", 3)):
                apply_swap_live(emu, lab, sb, p1n, p1c, cpun, cpuc)
                emu.advance(20)
                check_network_heap(emu, f"cat2-fod-swap-{p1n}")

            # Trigger real SD settings save on menu close under 4P max load!
            lab.open_menu(keep=True)
            lab.menu_nav_to("Lab > Move Staling")
            lab.tap(["A"])  # toggle persisted field
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "cat2 FoD settings save", r"storage: settings saved to \S+", spos, 300)
            check_network_heap(emu, "cat2-fod-post-sd-save")

            # Exit to CSS, open & scan Recordings Browser, close!
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat2-fod-exit")
            check_network_heap(emu, "cat2-css-post-fod")
            cnt, _ = ST.open_browser(emu)
            if cnt < 1:
                raise TortureError("cat2 Browser did not list exported recording")
            emu.advance(20)
            check_network_heap(emu, "cat2-css-browser-open")
            emu.press(0, ["B"], frames=4, after=12)

            # Match 2: Yoshi's Story (0x2A) with 4P + ALL displays + mid-match swap + clean exit!
            m2_pos = len(emu.log_text())
            nav.start_quick_match(emu, stage=STAGE_YOSHIS, p1=FSW.ROSTER_INFO["Peach"],
                                  cpu=FSW.ROSTER_INFO["Wario"], seed_rng=True)
            emu.advance(20)
            nav.set_training_cpus(emu, 3)
            emu.advance(20)
            settings2 = lab.find_settings()
            enable_all_overlays_and_osds(emu, settings2, psa=1, frame_meter=1, combo_enabled=1)
            emu.advance(45)
            check_network_heap(emu, "cat2-yoshis-4p-all-displays")
            sb2 = FSW.SwapBlock(emu)
            sb2.find()
            apply_swap_live(emu, lab, sb2, "Olimar", 1, "Ice Climbers", 2)
            emu.advance(20)
            check_network_heap(emu, "cat2-yoshis-post-swap")
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, m2_pos, "cat2-yoshis-exit")
    finally:
        E.remove_storage_image(img)
    print("  cat2 PASS: 4P max-load on FoD & Yoshi's Story + swaps + Browser + SD save/load, Network heap >= 0x10BA20")


# ============================================================================
# CATEGORY 3: Hostile / Corrupt / Truncated SD Fuzzing (Live Dolphin)
# ============================================================================

def run_cat3(rel):
    print("=== [cat3-hostile-sd-fuzz-live] Zero-byte, truncated, garbage, oversized, bad-CRC, v99 on live SD ===")
    rng = random.Random(0xC0FFEE3)
    img = E.storage_image(rel)
    try:
        # ---- Boot A: Corrupt/zero/truncated/garbage/bad-CRC settings, bests, and .tmr files ----
        print("  [cat3-BootA] Corrupt settings/bests + 6 hostile .tmr files -> graceful recovery & valid re-save")
        valid_set = ST.build_settings([(1, 1, bytes(15))], 1, fmt=1, layout=2)
        bad_crc_set = bytearray(valid_set)
        bad_crc_set[-35] ^= 0xFF
        valid_tmr = DT.build_start_paused_tmr("good-crc-base", frames=20)
        bad_crc_tmr = bytearray(valid_tmr)
        bad_crc_tmr[-35] ^= 0x5A
        future_tmr = bytearray(valid_tmr)
        struct.pack_into(">H", future_tmr, 4, 99)  # format = 99
        struct.pack_into(">I", future_tmr, 12, ST.fnv(bytes(future_tmr[16:struct.unpack_from(">I", future_tmr, 8)[0]])))

        sd_put(img, {
            "settings-a.bin": b"",
            "settings-b.bin": bytes(bad_crc_set),
            "bests-a.bin": bytes(rng.getrandbits(8) for _ in range(17)),
            "bests-b.bin": bytes(rng.getrandbits(8) for _ in range(128)),
            "00-zero.tmr": b"",
            "01-trunc.tmr": bytes(rng.getrandbits(8) for _ in range(24)),
            "02-garbage.tmr": bytes(rng.getrandbits(8) for _ in range(512)),
            "03-badcrc.tmr": bytes(bad_crc_tmr),
            "04-oversize.tmr": bytes(rng.getrandbits(8) for _ in range(160 * 1024)),
            "05-future99.tmr": bytes(future_tmr),
        }, clear=True)

        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu, drill_list="keep")
            emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
            # Open Browser -> wait for all 6 hostile headers to classify gracefully without crash!
            cnt, bpos = ST.open_browser(emu)
            if cnt != 6:
                raise TortureError(f"cat3 BootA expected 6 hostile .tmr listed, got {cnt}")
            m_hdr = ST.wait_line(emu, "cat3 hostile browser headers",
                                 r"browser: headers read: (\d+) ready, (\d+) newer, (\d+) older, (\d+) damaged", bpos, 300)
            if int(m_hdr.group(2)) < 1 or int(m_hdr.group(4)) < 4:
                raise TortureError(f"cat3 BootA unexpected hostile .tmr classification: {m_hdr.group(0)}")
            # Attempt pressing A on several hostile entries -> must refuse cleanly without crash/hang!
            for _ in range(3):
                emu.press(0, ["A"], frames=3, after=8)
                emu.press(0, ["DDOWN"], frames=3, after=6)
            emu.press(0, ["B"], frames=4, after=10)

            # Enter Training match -> defaults used gracefully -> mutate a setting & close menu -> valid re-save!
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            lab.find_settings()
            lab.open_menu()
            lab.menu_nav_to("Lab > Move Staling")
            lab.tap(["A"])
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "cat3 BootA re-save", r"storage: settings saved to \S+", spos, 300)
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat3-BootA")

        # Verify SD now holds a valid CRC settings file overwriting the corrupt slot!
        files_a = ST.tm_files(img)
        parsed_any = [ST.parse_settings(files_a.get(n)) for n in ST.SETTINGS_FILES]
        good_a = [p for p in parsed_any if p and p["checksum_ok"] and p["format"] == 1 and p["layout"] == 2]
        if not good_a:
            raise TortureError(f"cat3 BootA did not write a valid settings file over corrupt files: {parsed_any}")

        # ---- Boot B: Future-version (v99) & oversized (> cap) settings + bests -> ZERO CLOBBERING! ----
        print("  [cat3-BootB] Future-version (v99) oversized settings & bests -> zero clobbering (kNewer)")
        # Oversized v99 settings-a.bin (1024 B > kSettingsFileCap 768) + v99-layout settings-b.bin + oversized v99 bests-a.bin.
        big_v99_set = bytearray(1024)
        for i in range(32, 1024):
            big_v99_set[i] = (i * 37) & 0xFF
        struct.pack_into(">IHHII", big_v99_set, 0, ST.SETTINGS_MAGIC, 99, 32, 1024, 0)
        struct.pack_into(">I", big_v99_set, 16, 42)  # sequence = 42
        struct.pack_into(">I", big_v99_set, 12, ST.fnv(bytes(big_v99_set[16:1024])))
        big_v99_set = bytes(big_v99_set)

        v99_layout_set = ST.build_settings([(1, 1, bytes(15))], 41, fmt=1, layout=99)
        big_v99_bests = build_bests([5] * 32, 42, fmt=99, rev=99, extra_tail=bytes([0xAB] * 500))

        sd_put(img, {
            "settings-a.bin": big_v99_set,
            "settings-b.bin": v99_layout_set,
            "bests-a.bin": big_v99_bests,
        }, clear=True)

        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu, drill_list="keep")
            emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
            # Open Quick Setup, attempt edit & close -> MUST stay read-only!
            qpos = len(emu.log_text())
            emu.press(0, ["Y"], frames=4, after=8)
            ST.wait_line(emu, "cat3 BootB Quick Setup", r"osdquick: opened", qpos, 180)
            emu.press(0, ["DRIGHT"], frames=3, after=6)
            emu.press(0, ["B"], frames=4, after=15)
            # Enter Training match -> attempt edit & close -> MUST log "not saved: newer format"!
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            lab.find_settings()
            lab.open_menu()
            lab.menu_nav_to("Lab > Move Staling")
            lab.tap(["A"])
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "cat3 BootB kNewer guard", r"storage: settings not saved: newer format", spos, 240)
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat3-BootB")

        files_b = ST.tm_files(img)
        if files_b.get("settings-a.bin") != big_v99_set:
            raise TortureError("cat3 BootB CLOBBERED oversized v99 settings-a.bin!")
        if files_b.get("settings-b.bin") != v99_layout_set:
            raise TortureError("cat3 BootB CLOBBERED v99-layout settings-b.bin!")
        if files_b.get("bests-a.bin") != big_v99_bests or "bests-b.bin" in files_b:
            raise TortureError("cat3 BootB CLOBBERED oversized v99 bests-a.bin!")

        # ---- Boot C: Oversized current-format (> cap) settings-a + truncated settings-b -> recover & re-save ----
        print("  [cat3-BootC] Oversized current-format (> cap) + truncated -> recover & valid re-save")
        oversize_cur = bytearray(1280)
        struct.pack_into(">IHHII", oversize_cur, 0, ST.SETTINGS_MAGIC, 1, 32, 1280, 0)
        struct.pack_into(">I", oversize_cur, 16, 5)
        struct.pack_into(">I", oversize_cur, 12, ST.fnv(bytes(oversize_cur[16:1280])))
        sd_put(img, {
            "settings-a.bin": bytes(oversize_cur),
            "settings-b.bin": valid_set[:16],
        }, clear=True)

        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu)
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            lab.find_settings()
            lab.open_menu()
            lab.menu_nav_to("Lab > Move Staling")
            lab.tap(["A"])
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "cat3 BootC re-save", r"storage: settings saved to \S+", spos, 300)
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat3-BootC")

        files_c = ST.tm_files(img)
        parsed_c = [ST.parse_settings(files_c.get(n)) for n in ST.SETTINGS_FILES]
        if not any(p and p["checksum_ok"] and p["layout"] == 2 for p in parsed_c):
            raise TortureError(f"cat3 BootC did not recover and write valid settings: {parsed_c}")
    finally:
        E.remove_storage_image(img)
    print("  cat3 PASS: all hostile/corrupt/truncated/oversized/v99 SD payloads handled gracefully with zero clobbering")


# ============================================================================
# CATEGORY 11: Save Schema, Migration & Every-Page Reset Torture
# ============================================================================

def run_cat11(rel):
    print("=== [cat11-schema-migration-every-reset] v1 modal x3, 16-area silent v2, Quick Setup, Custom 1-3, 12 .tmr slots, every-page Yx2 ===")
    img = E.storage_image(rel)
    try:
        # ---- 11a. v1 legacy upgrade modal (ALL 3 user choices: 0=Reset All, 1=Reset Updated, 2=Keep Existing) ----
        v1_bytes = build_valid_v1_settings(seq=3)
        for choice in (0, 1, 2):
            print(f"  [11a] v1 upgrade modal choice {choice}")
            sd_put(img, {"settings-a.bin": v1_bytes}, clear=True)
            with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                               writable_image=img, locked=False) as (_proc, emu):
                pos0 = len(emu.log_text())
                nav.boot_to_training_css(emu)
                nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
                ST.wait_line(emu, f"11a choice {choice} modal pending", r"upgrade modal pending", pos0, 300)
                emu.advance(20)
                # Drive real modal cursor: Down `choice` times, then A!
                for _ in range(choice):
                    emu.press(0, ["DDOWN"], frames=3, after=8)
                apos = len(emu.log_text())
                emu.press(0, ["A"], frames=4, after=12)
                ST.wait_line(emu, f"11a choice {choice} applied",
                             rf"storage: v1 -> v2 upgrade choice {choice} applied", apos, 240)
                ST.wait_line(emu, f"11a choice {choice} saved", r"storage: settings saved to \S+", apos, 300)
                lab = LT.Lab(emu, [])
                base = lab.find_settings()
                cam = u8(emu, base + S_CAMERA)          # unchanged Area 1: v1 had 1 (Zoom), default 0
                staling = u8(emu, base + 18)            # unchanged Area 1: v1 had 0, default 1
                lcan = u8(emu, base + 93)               # affected Area 6: v1 had 0, v2 default 1
                if choice == 0:
                    if cam != 0 or staling != 1 or lcan != 1:
                        raise TortureError(f"11a choice 0 expected v2 defaults (0,1,1), got ({cam},{staling},{lcan})")
                elif choice == 1:
                    if cam != 1 or staling != 0 or lcan != 1:
                        raise TortureError(f"11a choice 1 expected unchanged kept & affected reset (1,0,1), got ({cam},{staling},{lcan})")
                else:
                    if cam != 1 or staling != 0 or lcan != 0:
                        raise TortureError(f"11a choice 2 expected all compatible kept (1,0,0), got ({cam},{staling},{lcan})")
                nav.finish_training(emu)
                emu.advance(20)
                check_no_problems(emu, pos0, f"cat11a-choice-{choice}")
            # Confirm SD now holds a valid v2 (layout=2) file!
            fs = ST.tm_files(img)
            ps = [ST.parse_settings(fs.get(n)) for n in ST.SETTINGS_FILES]
            if not any(p and p["checksum_ok"] and p["layout"] == 2 and p["sequence"] > 3 for p in ps):
                raise TortureError(f"11a choice {choice} did not persist upgraded v2 file: {ps}")

        # ---- 11b. 16-area v2 silent Area 17 migration (zero repeat boot toast) ----
        print("  [11b] 16-area v2 silent Area 17 migration -> zero boot toast + cold reboot verify")
        # Take the freshly saved 17-area settings file and strip Area 17 to construct an authentic 16-area v2 file!
        fs_prev = ST.tm_files(img)
        newest_p = max(
            (p for p in (ST.parse_settings(fs_prev.get(n)) for n in ST.SETTINGS_FILES) if p and p["checksum_ok"]),
            key=lambda p: p["sequence"],
        )
        areas_16 = [(aid, gen, raw_b) for aid, (gen, raw_b) in sorted(newest_p["areas"].items()) if aid != 17]
        if len(areas_16) != 16:
            raise TortureError(f"11b expected 16 areas after stripping Area 17, got {len(areas_16)}")
        v2_16_bytes = ST.build_settings(areas_16, 10, fmt=1, layout=2)
        sd_put(img, {"settings-a.bin": v2_16_bytes}, clear=True)

        # Boot 1: load 16-area v2 -> ZERO "older version" toast -> close menu (force save writes 17 areas!).
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu)
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            log1 = emu.log_text()[pos0:]
            if "older version" in log1:
                raise TortureError(f"11b unexpected older-version toast on silent Area 17 migration!")
            lab = LT.Lab(emu, [])
            lab.find_settings()
            lab.open_menu()
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "11b silent upgrade flush", r"storage: settings saved to settings-b\.bin", spos, 300)
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, pos0, "cat11b-boot1")
        p17 = ST.parse_settings(ST.tm_files(img).get("settings-b.bin"))
        if not p17 or len(p17["areas"]) != 17:
            raise TortureError(f"11b did not flush 17 areas to settings-b.bin: {p17}")

        # Boot 2 (cold reboot): zero migration toast & zero spurious write!
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu)
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            log2 = emu.log_text()[pos0:]
            if "older version" in log2 or "0 with new fields, 0 at defaults" not in log2:
                raise TortureError(f"11b cold reboot did not reload 17 areas cleanly: {log2[-600:]}")
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, pos0, "cat11b-boot2")

        # ---- 11c. Fresh-SD Quick Setup partial save (Movement + P+ Tech OSD) ----
        print("  [11c] Fresh-SD Quick Setup partial save (Movement + P+ Tech OSD)")
        sd_put(img, {}, clear=True)
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu, drill_list="keep")
            emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
            qpos = len(emu.log_text())
            emu.press(0, ["Y"], frames=4, after=8)
            ST.wait_line(emu, "11c Quick Setup open", r"osdquick: opened", qpos, 180)
            # Row 0 is OSD Preset (starts on Essentials=1 -> press Right once for Movement=2, which enables P+ Tech OSD!).
            emu.press(0, ["DRIGHT"], frames=3, after=8)
            # Close Quick Setup -> writes partial 4-area settings-a.bin!
            emu.press(0, ["B"], frames=4, after=12)
            ST.wait_line(emu, "11c Quick Setup saved", r"osdquick: saved", qpos, 300)
            # Enter Training match -> verify Movement preset + P+ Tech OSD == 1 + ZERO older-version toast!
            mpos = len(emu.log_text())
            nav.start_quick_match(emu, stage=STAGE_FD, seed_rng=True)
            emu.advance(25)
            mlog = emu.log_text()[mpos:]
            if "older version" in mlog:
                raise TortureError("11c Quick Setup partial file triggered spurious older-version warning!")
            lab = LT.Lab(emu, [])
            base = lab.find_settings()
            if u8(emu, base + S_OSD_PRESET) != 2 or u8(emu, base + S_CPU_TECH_OSD) != 1:
                raise TortureError(
                    f"11c Quick Setup values lost: preset={u8(emu, base + S_OSD_PRESET)} techOsd={u8(emu, base + S_CPU_TECH_OSD)}"
                )
            nav.finish_training(emu)
            emu.advance(20)
            check_no_problems(emu, pos0, "cat11c")

        # ---- 11d. Custom 1..3 OSD slots + 6 P1 & 6 CPU .tmr slots + Every-Page Double-Y Reset + Cold Reboot ----
        print("  [11d] Custom 1..3 OSD slots + 6 P1 & 6 CPU .tmr slots + Every-Page Double-Y Reset")
        exported_name = None
        expected_masks = []
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu)
            # Use Olimar on Fountain of Dreams so conditional pages (P_OLIMAR + Stage) are ALSO reachable!
            nav.start_quick_match(emu, stage=STAGE_FOD, p1=FSW.ROSTER_INFO["Olimar"],
                                  cpu=FSW.ROSTER_INFO["Fox"], seed_rng=True)
            emu.advance(25)
            lab = LT.Lab(emu, [])
            base = lab.find_settings()

            # 11d-1: Double-Y reset on EVERY reachable Lab page & subpage via live read_menu_def() walk!
            lab.open_menu()
            mdef = lab.read_menu_def()
            visited_pages = set()

            def read_opt_val(opt):
                fl = opt["flags"]
                addr = base + opt["offset"]
                if fl & 0x10:  # kFlagBit (src/main/ui/menu.h)
                    return 1 if (u8(emu, addr) & opt["min"]) else 0
                if fl & 0x01:  # kFlagHalf
                    return struct.unpack(">h" if (fl & 0x02) else ">H", emu.read(addr, 2))[0]
                return struct.unpack(">b" if (fl & 0x02) else ">B", emu.read(addr, 1))[0]

            mutated_pages = 0

            def test_page_double_y(page_id, path_label):
                nonlocal mutated_pages
                if page_id in visited_pages:
                    return
                visited_pages.add(page_id)
                pg = lab.read_page(page_id, mdef)
                # Find an editable non-scratch, available option (exclude kFlagUnavailable 0x04 | kFlagScratch 0x20)
                # to mutate and verify double-Y genuinely resets it back to its default!
                mutated_opt = None
                for opt in pg["options"]:
                    if opt["kind"] in (LT.KIND_TOGGLE, LT.KIND_CHOICE, LT.KIND_INT) and (opt["flags"] & 0x24) == 0:
                        if opt["name"] in ("Lab Menu Button", "Menu Style", "Controlled By", "HUD", "Stage Model", "OSD Preset"):
                            continue
                        try:
                            lab.menu_select_row(opt["name"], mdef)
                            before_val = read_opt_val(opt)
                            lab.tap(stick=LT.RIGHT)
                            after_val = read_opt_val(opt)
                            if after_val == before_val:
                                lab.tap(stick=LT.LEFT)
                                after_val = read_opt_val(opt)
                            if after_val != before_val:
                                mutated_opt = (opt, before_val, after_val)
                                break
                        except LT.LabError:
                            continue
                # Press Y once -> arms confirm; press Y twice -> resets page and restores mutated option to default!
                lab.tap(["Y"])
                st1 = lab.read_menu_state()
                if mutated_opt is not None and st1["reset_confirm_page"] == 0:
                    raise TortureError(
                        f"11d first Y did not arm reset_confirm_page after mutating {mutated_opt[0]['name']!r} "
                        f"on {path_label} (pg {page_id})"
                    )
                if st1["reset_confirm_page"] != 0:
                    lab.tap(["Y"])
                    st2 = lab.read_menu_state()
                    if st2["reset_confirm_page"] != 0:
                        raise TortureError(f"11d double-Y did not clear reset_confirm_page on {path_label} (pg {page_id})")
                    if mutated_opt is not None:
                        opt, _bv, av = mutated_opt
                        fl = opt["flags"]
                        want_def = (1 if opt["def"] else 0) if (fl & 0x10) else (
                            opt["def"] if (fl & 0x02) else (opt["def"] & 0xFF)
                        )
                        got_val = read_opt_val(opt)
                        if got_val != want_def:
                            raise TortureError(
                                f"11d double-Y did not restore {opt['name']!r} on {path_label} (pg {page_id}): "
                                f"mutated={av}, got={got_val}, expected default={want_def}"
                            )
                        mutated_pages += 1
                # Recurse into every reachable submenu on this page!
                for opt in pg["options"]:
                    if opt["kind"] == LT.KIND_MENU and opt["name"]:
                        sub_id = opt["max"]
                        if sub_id in visited_pages:
                            continue
                        try:
                            lab.menu_select_row(opt["name"], mdef)
                        except LT.LabError:
                            continue
                        prev_d = lab.read_menu_state()["depth"]
                        lab.tap(["A"])
                        if lab.read_menu_state()["depth"] == prev_d + 1:
                            test_page_double_y(sub_id, f"{path_label} > {opt['name']}")
                            lab.tap(["B"])

            for cat_i in range(mdef["cat_count"]):
                cat_label = mdef["tab_labels"][cat_i] or lab.read_page(mdef["cat_pages"][cat_i], mdef)["title"]
                lab.menu_nav_to(cat_label)
                test_page_double_y(mdef["cat_pages"][cat_i], cat_label)
            print(f"    double-Y reset verified across {len(visited_pages)} reachable Lab pages/subpages ({mutated_pages} mutated & restored)!")
            if len(visited_pages) < 15 or mutated_pages < 10:
                raise TortureError(f"11d only visited {len(visited_pages)} pages ({mutated_pages} mutated), expected >=15 / >=10")

            # 11d-2: Save all 3 Custom 1..3 OSD slots with 3 distinct masks via real Lab Menu!
            # Slot 0 (Custom 1): Essentials (1), Slot 1 (Custom 2): Movement (2), Slot 2 (Custom 3): Defense (3).
            for slot_idx, preset_val in ((0, 1), (1, 2), (2, 3)):
                lab.menu_nav_to("OSDs > OSD Preset")
                while u8(emu, base + S_OSD_PRESET) != preset_val:
                    lab.tap(stick=LT.RIGHT if u8(emu, base + S_OSD_PRESET) < preset_val else LT.LEFT)
                lab.menu_select_row("Save Target")
                # Step Save Target (0=Custom 1, 1=Custom 2, 2=Custom 3) via Left to 0 then Right slot_idx times.
                lab.tap(stick=LT.LEFT)
                lab.tap(stick=LT.LEFT)
                for _ in range(slot_idx):
                    lab.tap(stick=LT.RIGHT)
                lab.menu_select_row("Save Current OSDs")
                lab.tap(["A"])
                mask = emu.u32(base + S_OSD_CUSTOM_MASKS + 4 * slot_idx)
                if (mask & 0x80000000) == 0 or (mask & 0xFFFFF) == 0:
                    raise TortureError(f"11d Custom {slot_idx + 1} did not save non-empty mask: {mask:#x}")
                expected_masks.append(mask)
            if len(set(expected_masks)) != 3:
                raise TortureError(f"11d Custom 1..3 masks were not distinct: {[hex(m) for m in expected_masks]}")
            spos = lab.log_pos()
            lab.close_menu()
            ST.wait_line(emu, "11d Custom 1..3 settings saved", r"storage: settings saved to \S+", spos, 300)
            emu.advance(10)

            # 11d-3: Record ALL 6 P1 slots + ALL 6 CPU slots -> Export .tmr to SD!
            rec = RT.Rec(emu)
            rec.find()
            for _ in range(40):
                if S3D.read_rec_block(emu, rec.addr)["flags"] & RT.FLAG_STORAGE:
                    break
                emu.advance(2)
            rec.request("save")
            emu.advance(10)
            for slot_num in range(1, 7):
                # Record HMN slot `slot_num` (15 frames).
                rec.set(hmnMode=RT.HMN_MODE["Record"], hmnSlot=slot_num, cpuMode=RT.CPU_MODE["Off"], cpuSlot=1)
                rec.request("hmn-mode")
                emu.advance(4)
                emu.hold(0, buttons=["A"] if slot_num % 2 else ["X"], stick=(228 if slot_num % 2 else 28, 128))
                emu.advance(15)
                emu.neutral(0)
                rec.set(hmnMode=RT.HMN_MODE["Off"])
                rec.request("hmn-mode")
                emu.advance(4)
                # Record CPU slot `slot_num` (15 frames via CPU Lab Stand/Jump/Shield/Crouch).
                w8(emu, base + S_CPU_CONTROL, CT.LAB)
                w8(emu, base + S_CPU_BEHAVIOR, slot_num % 4)
                rec.set(hmnMode=RT.HMN_MODE["Off"], cpuMode=RT.CPU_MODE["Record"], cpuSlot=slot_num)
                rec.request("cpu-mode")
                emu.advance(18)
                rec.set(cpuMode=RT.CPU_MODE["Off"])
                rec.request("cpu-mode")
                emu.advance(4)
            lab.open_menu()
            lab.menu_nav_to("Recording > Playback & Positions > Export Recording")
            epos = lab.log_pos()
            lab.tap(["A"])
            m_exp = ST.wait_line(emu, "11d 12-slot export", r"storage: exported (\S+) \((\d+) bytes\)", epos, 300)
            exported_name = m_exp.group(1)
            lab.close_menu()
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat11d-boot1")

        # Inspect 12-slot .tmr directly on SD from host!
        raw_tmr = ST.tm_files(img).get(exported_name)
        parsed_tmr = ST.parse_recording(raw_tmr)
        if not parsed_tmr or not parsed_tmr["checksum_ok"]:
            raise TortureError(f"11d exported {exported_name} invalid CRC or missing")
        slot_pairs = {(s["side"], s["index"]) for s in parsed_tmr["slots"]}
        want_pairs = {(side, idx) for side in (0, 1) for idx in range(6)}
        if slot_pairs != want_pairs:
            raise TortureError(f"11d expected all 12 P1/CPU slots in .tmr, got {sorted(slot_pairs)}")
        print(f"    verified 12-slot .tmr ({exported_name}, {len(raw_tmr)} B, all 6 P1 + 6 CPU slots valid)")

        # ---- 11e. Cold Reboot Persistence Verification (Custom 1..3 + 12-slot .tmr import) ----
        print("  [11e] Cold reboot verifying Custom 1..3 persistence & 12-slot .tmr Browser import")
        with E.harness_run(E.headless_backend(), E.DETERMINISTIC_ARGS, allow_null=True,
                           writable_image=img, locked=False) as (_proc, emu):
            pos0 = len(emu.log_text())
            nav.boot_to_training_css(emu, drill_list="keep")
            emu.wait_for("Boot Menu open", lambda: nav.drill_list_state(emu) == "open", 600, every=5)
            cnt, bpos = ST.open_browser(emu)
            if cnt < 1:
                raise TortureError("11e Browser missing exported 12-slot .tmr after cold reboot")
            ST.wait_line(emu, "11e browser header ready", r"browser: headers read: 1 ready", bpos, 240)
            ipos = len(emu.log_text())
            emu.press(0, ["A"], frames=4, after=8)
            ST.wait_line(emu, "11e import start", r"browser: starting", ipos, 240)
            ST.seeded_match_start(emu)
            emu.advance(30)
            ST.wait_line(emu, "11e positions imported", r"record: positions imported", ipos, 480)
            lab = LT.Lab(emu, [])
            base = lab.find_settings()
            got_masks = [emu.u32(base + S_OSD_CUSTOM_MASKS + 4 * i) for i in range(3)]
            if got_masks != expected_masks:
                raise TortureError(f"11e Custom 1..3 masks did not persist across cold reboot: {got_masks} != {expected_masks}")
            # Verify all 12 imported recording slots are populated in live RAM!
            rec = RT.Rec(emu)
            rec.find()
            rb = S3D.read_rec_block(emu, rec.addr)
            for side in (0, 1):
                for idx in range(6):
                    if rb["end"][side][idx] <= rb["start"][side][idx]:
                        raise TortureError(f"11e imported slot side={side} idx={idx} empty after reboot: {rb}")
            nav.finish_training(emu)
            emu.advance(25)
            check_no_problems(emu, pos0, "cat11e-reboot")
    finally:
        E.remove_storage_image(img)
    print("  cat11 PASS: v1 modal x3, silent Area 17, Quick Setup, Custom 1..3, 12 .tmr slots, every-page Yx2, reboot OK")


# ---- dispatch --------------------------------------------------------------

RUNNERS = {
    "cat1-savestate-record-swap-chaos": run_cat1,
    "cat2-4p-maxload-heap-display": run_cat2,
    "cat3-hostile-sd-fuzz-live": run_cat3,
    "cat4-strict-mode-menu-drill-hop": run_cat4,
    "cat5-long-soak-combat": run_cat5,
    "cat6-transform-tether-multiactor": run_cat6,
    "cat7-freeze-fa-modal-race": run_cat7,
    "cat8-4ctrl-hybrid-port-control": run_cat8,
    "cat9-extreme-stage-camera-gx-d3d": run_cat9,
    "cat10-scene-loop-vs4p-mmu": run_cat10,
    "cat11-schema-migration-every-reset": run_cat11,
}


def parse_args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-torture")
    ap.add_argument("rel")
    ap.add_argument("--bucket", type=int, nargs="+", choices=sorted(BUCKETS),
                    help="run one or more parallelizable buckets (1..4)")
    ap.add_argument("--case", nargs="+", choices=ALL_CASES,
                    help="run specific torture case(s)")
    ap.add_argument("--list", action="store_true", help="list all 11 torture categories and buckets")
    ap.add_argument("--keep-going", action="store_true", help="continue running remaining cases after a failure")
    return ap.parse_args(rest)


def cmd_test_torture(rest):
    opts = parse_args(rest)
    if opts.list:
        for b, cases in sorted(BUCKETS.items()):
            print(f"bucket {b}:")
            for c in cases:
                print(f"  {c}")
        return 0

    selected = []
    if opts.case:
        selected = list(opts.case)
    elif opts.bucket:
        for b in opts.bucket:
            for c in BUCKETS[b]:
                if c not in selected:
                    selected.append(c)
    else:
        selected = list(ALL_CASES)

    t0 = time.monotonic()
    passed, failed = [], []
    for name in selected:
        tc = time.monotonic()
        try:
            RUNNERS[name](opts.rel)
            passed.append((name, time.monotonic() - tc))
        except (TortureError, Timeout, nav.NavError, HarnessError, LT.LabError,
                CT.CpuError, ST.StorageError, FSW.SwapError, SD.SkillDrillsUiError,
                S3D.Stage3DError, S3F.TestError, S4C.Stage4cError, GV.GrabVizError,
                HTM.HudThemeMeterError, RC.TestError, WS.StrictError, AssertionError) as exc:
            elapsed = time.monotonic() - tc
            print(f"FAIL [{name}] ({elapsed:.1f}s): {exc}")
            failed.append((name, str(exc), elapsed))
            if not opts.keep_going:
                break

    total_s = time.monotonic() - t0
    print()
    print(f"=== TORTURE SUMMARY ({len(passed)}/{len(selected)} PASS, {len(failed)} FAIL, {total_s:.1f}s) ===")
    for name, dt in passed:
        print(f"  PASS  {name:<38} {dt:6.1f}s")
    for name, why, dt in failed:
        print(f"  FAIL  {name:<38} {dt:6.1f}s  {why}")
    return 0 if not failed and len(passed) == len(selected) else 1
