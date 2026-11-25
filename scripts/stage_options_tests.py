"""Emulator test of Stage Options (src/extra/stage_options.cpp and
src/main/ui/pages/stage_cast.cpp), run through emu.py.

  test-stage-options REL [--only NAME ...] [--list] [--keep-going]

One boot, cycling through Project+ 3.2.1 stages via quick Training matches:
  - luigis-mansion (0x04): Main Menu Stage row enabled, Split (f=0), Combined
    (f=1800), Freeze, Cycle, instant teleport with a fighter grounded on the
    platform without fling, and lab savestate load re-apply / Freeze preserve.
  - fountain-of-dreams (0x1F): independent Left & Right platform heights
    (Hidden collision off vs Highest/Lowest/Average collision on) and live
    Hazards ON/OFF with zero StageInstance heap leak.
  - smashville (0x21): live Hazards ON/OFF range difference, Freeze & Center
    with ModelData[80] platform and ModelData[81] shadow locked in phase, and
    zero StageInstance heap leak.
  - green-hill-zone (0x23): live Hazards ON/OFF pendulum swing arc, Center,
    and Freeze.
  - dream-land (0x2D): live Hazards OFF/ON Whispy wind timer/state, top
    blastzone (Dead0N.y 230 <-> 220), zero StageInstance leak, savestate
    load safety (no kErrResources), and restoring global m_isHazardOff on
    exit back to CSS.
  - delfino-secret (0x03): Tri-Plat (f=0) vs Two-Plat (f=2000) PlatformCenter
    height and Freeze.
  - final-destination (0x02): static stage leaves Main Menu Stage row (row 7)
    disabled and skipped by the cursor.
"""
import argparse
import math
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
from plugin_input import B_JUMP, frames_to_steps

# main::Settings offsets (src/main/settings.h).
S_STAGE_HAZARDS = 372
S_STAGE_PLATFORM = 373
S_STAGE_FOD_LEFT = 374
S_STAGE_FOD_RIGHT = 375

# Game addresses (RSBE01 rev 2, P+ 3.2.1).
G_STAGE_PTR = 0x80B8A428          # Stage* g_Stage
G_GR_COLLISION_LIST = 0x8049E57C  # grCollisionList (sentinel at +4, node at coll + 0x30)
G_HEAP_INFOS = 0x80494958         # HeapInfo g_HeapInfos[0x47] (stride 16, pool at +4)
HEAP_STAGE_INSTANCE = 15

# Stage / Ground / grCollision / grIzumiSpout / grDxGreensWhispy offsets.
STAGE_DEAD_RANGE = 0x58           # Rect2D (left, right, up, down)
STAGE_HEAD_GROUND = 0x88          # Ground* m_headGround
STAGE_CAM_PARAM1 = 0x78           # cmStageParam*
GROUND_MODEL_ANIMS = 0x48         # gfModelAnimation**
GROUND_NEXT = 0x50                # Ground*
GROUND_ANIM_NUM = 0x58            # u32
GROUND_MDL_INDEX = 0x5C           # u16
GROUND_FLAGS_6C = 0x6C            # bit 0x20: m_isEnableCollisionStatus
GROUND_FLAGS_6D = 0x6D            # bit 0x10: m_isPauseAnim
ANIM_CHR_RES = 0x0C               # nw4r::g3d::AnmObjChrRes*
ANMOBJ_FRAME = 0x18               # float mFrame (AnmObjChrRes::SetFrame 0x8019B104)

SPOUT_STATE = 0x160
SPOUT_CUR_CHR = 0x1A8
SV_CUR_ANIM = 0x168
GHZ_CUR_ANIM = 0x174
WHISPY_TIMER = 0x154
WHISPY_STATE = 0x15C

# Fighter posture & situation (scripts/cpu_tests.py:130-131).
MOD_POSTURE = 0xC
MOD_SITUATION = 0x14
POSTURE_X = 0x0C
POSTURE_Y = 0x10
SITUATION_KIND = 0x2C  # 0 = Ground, 1 = Cliff, 2 = Air

SS_BLOCK = re.compile(r"savestate: test block at ([0-9a-f]{8})")
SS_SAVED = re.compile(r"savestate: saved frame (\d+)")
SS_LOADED = re.compile(r"savestate: loaded frame (\d+)")
SS_REFUSED = re.compile(r"savestate: (save|load) refused: ([^\r\n]+)")
DETECT_LINE = re.compile(r"stage_options: detected preset (\d+)")

SCENARIOS = [
    "luigis-mansion",
    "fountain-of-dreams",
    "smashville",
    "green-hill-zone",
    "dead-line",
    "dream-land",
    "delfino-secret",
    "yoshis-story",
    "yoshis-island",
    "final-destination",
]


class StageOptError(RuntimeError):
    pass


def stage_ptr(e):
    return e.ptr(G_STAGE_PTR)


def find_ground(e, mdl_index):
    st = stage_ptr(e)
    if not st:
        return None
    g = e.ptr(st + STAGE_HEAD_GROUND)
    for _ in range(64):
        if not g:
            break
        if e.u16(g + GROUND_MDL_INDEX) == mdl_index:
            return g
        g = e.ptr(g + GROUND_NEXT)
    return None


def ground_frame(e, g, anim_index=0):
    anims = e.ptr(g + GROUND_MODEL_ANIMS)
    if not anims:
        return None
    anim = e.ptr(anims + 4 * anim_index)
    if not anim:
        return None
    chr_obj = e.ptr(anim + ANIM_CHR_RES)
    if not chr_obj:
        return None
    return e.f32(chr_obj + ANMOBJ_FRAME)


def ground_paused(e, g):
    return (e.u8(g + GROUND_FLAGS_6D) & 0x10) != 0


def ground_coll_enabled(e, g):
    return (e.u8(g + GROUND_FLAGS_6C) & 0x20) != 0


def ground_visible(e, g):
    return (e.u8(g + GROUND_FLAGS_6C) & 0x80) != 0


def find_moving_coll_ground(e):
    st = stage_ptr(e)
    if not st:
        return None
    g = e.ptr(st + STAGE_HEAD_GROUND)
    for _ in range(64):
        if not g:
            break
        if ground_frame(e, g, 0) is not None and ground_coll_bboxes(e, g):
            return g
        g = e.ptr(g + GROUND_NEXT)
    return None


def ground_coll_bboxes(e, target_ground):
    """Return list of (joint_node_idx, x_min, x_max, y_min, y_max) for all
    grCollisionJoints owned by `target_ground`."""
    sentinel = G_GR_COLLISION_LIST + 4
    node = e.ptr(sentinel)
    out = []
    for _ in range(64):
        if not node or node == sentinel:
            break
        coll = node - 0x30
        jlen = e.u16(coll + 6)
        joints = e.ptr(coll + 0x18)
        if joints and 0 < jlen <= 128:
            for j in range(jlen):
                ja = joints + j * 0x60
                if e.ptr(ja + 0x58) == target_ground:
                    # clAABBox2D at +0x10: m_min (x, y), m_max (x, y)
                    xmin, ymin, xmax, ymax = struct.unpack(">ffff", e.read(ja + 0x10, 16))
                    node_idx = e.u16(ja + 0x50)
                    out.append((node_idx, xmin, xmax, ymin, ymax))
        node = e.ptr(node)
    return out


def stage_instance_usage(e):
    """(allocated_blocks, used_bytes) of Heaps::StageInstance."""
    pool = e.ptr(G_HEAP_INFOS + HEAP_STAGE_INSTANCE * 16 + 4)
    if not pool:
        return (0, 0)
    return (e.u16(pool + 0x14), e.u32(pool + 0x1C))


def read_fighter_pos_sit(e, r, player=0):
    f = r.f.by_player(player)
    mods = r.f.modules(f)
    posture = struct.unpack(">I", mods[MOD_POSTURE:MOD_POSTURE + 4])[0]
    sit = struct.unpack(">I", mods[MOD_SITUATION:MOD_SITUATION + 4])[0]
    x = e.f32(posture + POSTURE_X)
    y = e.f32(posture + POSTURE_Y)
    sk = e.u32(sit + SITUATION_KIND)
    return x, y, sk


def reset_stage_settings(r):
    r.set(S_STAGE_HAZARDS, 0)
    r.set(S_STAGE_PLATFORM, 0)
    r.set(S_STAGE_FOD_LEFT, 0)
    r.set(S_STAGE_FOD_RIGHT, 0)


def step_frames(e, n):
    e.h.call("frame_advance", n=n)


def ss_trigger(e, block_addr, cmd):
    """Ask the savestate test block to save (1) or load (2) and step until done."""
    pos = len(e.log_text())
    e.h.call("write_mem", addr=block_addr + 5, hex=f"{cmd:02x}")
    for _ in range(40):
        step_frames(e, 1)
        tail = e.log_text()[pos:]
        ref = SS_REFUSED.search(tail)
        if ref:
            return ("refused", ref.group(2))
        if cmd == 1 and SS_SAVED.search(tail):
            return ("saved", int(SS_SAVED.search(tail).group(1)))
        if cmd == 2 and SS_LOADED.search(tail):
            return ("loaded", int(SS_LOADED.search(tail).group(1)))
    return ("timeout", "")


class Runner:
    def __init__(self, emu, keep_going=False):
        self.e = emu
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.step = "boot"
        self.r = DO.Runner(emu, self.results, self.failures)

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise StageOptError(f"{name}: {detail}")

    def start_stage(self, stage_kind):
        pos = len(self.e.log_text())
        nav.start_quick_match(self.e, stage=stage_kind, seed_rng=0x12345678)
        nav.pause_at_game_frame(self.e, DO.BASE_FRAME)
        self.r.lab.find_settings()
        for off, val in DO.CPU_STILL.items():
            self.r.set(off, val)
        reset_stage_settings(self.r)
        step_frames(self.e, 4)
        tail = self.e.log_text()[pos:]
        m = DETECT_LINE.findall(tail)
        preset = int(m[-1]) if m else -1
        ss = SS_BLOCK.findall(self.e.log_text())
        ss_addr = int(ss[-1], 16) if ss else None
        return preset, ss_addr

    def end_stage(self, reset_settings=True):
        if reset_settings:
            reset_stage_settings(self.r)
            step_frames(self.e, 2)
        self.e.h.call("resume")
        nav.finish_training(self.e)


def test_luigis_mansion(t):
    e, r = t.e, t.r
    preset, ss_addr = t.start_stage(0x04)
    t.check(preset == 6, "lm-detect", f"detected preset {preset} (want 6)")

    g = find_ground(e, 2)
    t.check(g is not None, "lm-ground", f"ModelData[2] Ground at {g:#x}" if g else "missing")

    # Menu reachability through real controller input: Start -> L (Stage & Cast) -> down 2 -> Platforms (row 3).
    e.h.call("resume")
    e.advance(4)
    pos = len(e.log_text())
    e.press(0, ["START"])
    e.wait_for("lab menu open", lambda: "lab: menu opened" in e.log_text()[pos:], 60)
    # Draft C Rail starts on Lab (cat 0); L wraps to Stage & Cast (cat 6).
    # On Luigi's Mansion (Mario/Fox), visible rows are 0 (Fighter Swap), 1 (Show Inactive), 3 (Platforms).
    e.press(0, ["L"], frames=2, after=6)
    for _ in range(2):
        e.press(0, [], stick=LT.DOWN, frames=2, after=4)
    e.press(0, [], stick=LT.RIGHT, frames=2, after=4)  # Platforms: Cycle -> Freeze (1)
    e.press(0, [], stick=LT.RIGHT, frames=2, after=4)  # Freeze -> Split (2)
    e.press(0, ["START"], frames=2, after=6)
    e.wait_for("lab menu close", lambda: "lab: menu closed" in e.log_text()[pos:], 60)
    e.h.call("pause")
    step_frames(e, 4)
    t.check(e.u8(r.lab.settings + S_STAGE_PLATFORM) == 2, "lm-menu-nav",
            f"Stage menu set platformMode={e.u8(r.lab.settings + S_STAGE_PLATFORM)} (want 2=Split)")

    # Split (f=0, X ~ -50.86) and frozen over 20 frames.
    f0 = ground_frame(e, g, 0)
    boxes_split = ground_coll_bboxes(e, g)
    step_frames(e, 20)
    f1 = ground_frame(e, g, 0)
    cx_split = sorted(0.5 * (b[1] + b[2]) for b in boxes_split)
    t.check(abs(f0 - 0.0) < 0.1 and abs(f1 - 0.0) < 0.1 and ground_paused(e, g)
            and len(cx_split) == 2 and cx_split[0] < -40.0 and cx_split[1] > 40.0,
            "lm-split", f"frame {f0:.2f}->{f1:.2f} paused={ground_paused(e, g)} centers={[round(x, 2) for x in cx_split]}")

    # Combined (f=1800, X ~ +-17.94).
    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    fc = ground_frame(e, g, 0)
    boxes_comb = ground_coll_bboxes(e, g)
    cx_comb = sorted(0.5 * (b[1] + b[2]) for b in boxes_comb)
    t.check(abs(fc - 1800.0) < 0.1 and ground_paused(e, g)
            and len(cx_comb) == 2 and -25.0 < cx_comb[0] < -10.0 and 10.0 < cx_comb[1] < 25.0,
            "lm-combined", f"frame {fc:.2f} centers={[round(x, 2) for x in cx_comb]}")

    # Zero-fling test: jump P1 onto the Combined platform (around X=-18, ~24 units above floor),
    # wait until grounded on the platform, then swap Combined -> Split and confirm no horizontal fling.
    _, floor_y, _ = read_fighter_pos_sit(e, r, 0)
    plat_y = 0.5 * (boxes_comb[0][3] + boxes_comb[0][4]) if boxes_comb else (floor_y + 24.0)
    inp = PI.PluginInput(e)
    landed = False
    for attempt in range(4):
        px, py, sk = read_fighter_pos_sit(e, r, 0)
        if sk == 0 and abs(py - plat_y) < 4.0:
            landed = True
            break
        dx_to = -18.0 - px
        sx = -0.8 if dx_to < -4.0 else (0.8 if dx_to > 4.0 else 0.0)
        steps = [(0, sx, 0.0, 12), (B_JUMP, sx, 0.0, 6), (0, sx, 0.0, 18), (B_JUMP, 0.0, 0.0, 4), (0, 0.0, 0.0, 35)]
        inp.blocks[0].play(0, steps)
        for _ in range(75):
            step_frames(e, 1)
            px, py, sk = read_fighter_pos_sit(e, r, 0)
            if sk == 0 and abs(py - plat_y) < 4.0:
                landed = True
                break
        if landed:
            break
    inp.stop()
    px_before, py_before, sk_before = read_fighter_pos_sit(e, r, 0)
    t.check(landed, "lm-land-on-plat", f"P1 pos=({px_before:.2f},{py_before:.2f}) sit={sk_before}")
    if landed:
        r.set(S_STAGE_PLATFORM, 2)  # Combined -> Split instant teleport!
        step_frames(e, 4)
        px_after, py_after, sk_after = read_fighter_pos_sit(e, r, 0)
        dx = abs(px_after - px_before)
        t.check(dx < 5.0, "lm-no-fling",
                f"pos ({px_before:.2f},{py_before:.2f}) -> ({px_after:.2f},{py_after:.2f}) |dx|={dx:.2f} (<5.0)")

    # Savestate re-apply (Split saved -> change to Combined -> load -> Combined re-applied).
    t.check(ss_addr is not None, "lm-ss-block", f"savestate block={ss_addr}")
    if ss_addr is not None:
        r.set(S_STAGE_PLATFORM, 2)
        step_frames(e, 4)
        res_s = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 3)
        step_frames(e, 2)
        res_l = ss_trigger(e, ss_addr, 2)
        step_frames(e, 3)
        f_after_load = ground_frame(e, g, 0)
        t.check(res_s[0] == "saved" and res_l[0] == "loaded" and abs(f_after_load - 1800.0) < 0.1,
                "lm-savestate-reapply", f"save={res_s} load={res_l} frame={f_after_load:.2f}")

        # Freeze preserves savestate frame across load.
        r.set(S_STAGE_PLATFORM, 0)  # Cycle
        step_frames(e, 25)
        r.set(S_STAGE_PLATFORM, 1)  # Freeze
        step_frames(e, 2)
        f_frozen = ground_frame(e, g, 0)
        res_s2 = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 0)
        step_frames(e, 20)
        r.set(S_STAGE_PLATFORM, 1)
        res_l2 = ss_trigger(e, ss_addr, 2)
        step_frames(e, 4)
        f_restored = ground_frame(e, g, 0)
        t.check(res_s2[0] == "saved" and res_l2[0] == "loaded" and abs(f_restored - f_frozen) < 1.5
                and ground_paused(e, g),
                "lm-savestate-freeze", f"saved f={f_frozen:.2f} restored f={f_restored:.2f}")

    # Cycle advances frame.
    r.set(S_STAGE_PLATFORM, 0)
    step_frames(e, 2)
    fa = ground_frame(e, g, 0)
    step_frames(e, 15)
    fb = ground_frame(e, g, 0)
    t.check(abs(fb - fa) > 10.0 and not ground_paused(e, g), "lm-cycle", f"frame {fa:.2f}->{fb:.2f}")

    t.end_stage()


def test_fod(t):
    e, r = t.e, t.r
    preset, _ = t.start_stage(0x1F)
    t.check(preset == 1, "fod-detect", f"detected preset {preset} (want 1)")
    gl = find_ground(e, 80)
    gr = find_ground(e, 81)
    t.check(gl is not None and gr is not None, "fod-grounds", f"L={gl} R={gr}")

    # Left = Hidden (1, f=0 -> collision OFF), Right = Highest (6, f=75 -> collision ON).
    r.set(S_STAGE_FOD_LEFT, 1)
    r.set(S_STAGE_FOD_RIGHT, 6)
    step_frames(e, 6)
    fl, fr = ground_frame(e, gl, 0), ground_frame(e, gr, 0)
    cl, cr = ground_coll_enabled(e, gl), ground_coll_enabled(e, gr)
    stl, str_ = e.u32(gl + SPOUT_STATE), e.u32(gr + SPOUT_STATE)
    t.check(abs(fl - 0.0) < 0.1 and not cl and stl == 0
            and abs(fr - 75.0) < 0.1 and cr and str_ == 0,
            "fod-hidden-highest", f"L f={fl:.1f} coll={cl} st={stl}, R f={fr:.1f} coll={cr} st={str_}")

    # Left = Lowest (2, f=39 -> collision RE-ENABLES), Right = Average (4, f=57).
    r.set(S_STAGE_FOD_LEFT, 2)
    r.set(S_STAGE_FOD_RIGHT, 4)
    step_frames(e, 6)
    fl2, fr2 = ground_frame(e, gl, 0), ground_frame(e, gr, 0)
    cl2, cr2 = ground_coll_enabled(e, gl), ground_coll_enabled(e, gr)
    t.check(abs(fl2 - 39.0) < 0.1 and cl2 and abs(fr2 - 57.0) < 0.1 and cr2,
            "fod-lowest-average", f"L f={fl2:.1f} coll={cl2}, R f={fr2:.1f} coll={cr2}")

    # Live Hazards ON/OFF with default heights + StageInstance leak check.
    r.set(S_STAGE_FOD_LEFT, 0)
    r.set(S_STAGE_FOD_RIGHT, 0)
    r.set(S_STAGE_HAZARDS, 2)  # Off
    step_frames(e, 6)
    chr_off = (e.u32(gl + SPOUT_CUR_CHR), e.u32(gr + SPOUT_CUR_CHR))
    haz_flag_off = nav.melee_setup(e)["hazard_off"]
    u_base = stage_instance_usage(e)
    t.check(chr_off == (0, 0) and haz_flag_off == 1, "fod-haz-off", f"curChr={chr_off} flag={haz_flag_off}")

    r.set(S_STAGE_HAZARDS, 1)  # On
    step_frames(e, 6)
    chr_on = (e.u32(gl + SPOUT_CUR_CHR), e.u32(gr + SPOUT_CUR_CHR))
    st_on = (e.u32(gl + SPOUT_STATE), e.u32(gr + SPOUT_STATE))
    haz_flag_on = nav.melee_setup(e)["hazard_off"]
    t.check(chr_on == (2, 2) and st_on == (1, 1) and haz_flag_on == 0,
            "fod-haz-on", f"curChr={chr_on} st={st_on} flag={haz_flag_on}")

    # Repeated toggles Off -> On -> Off -> On -> Off: verify zero StageInstance leak!
    for _ in range(3):
        r.set(S_STAGE_HAZARDS, 2)
        step_frames(e, 4)
        r.set(S_STAGE_HAZARDS, 1)
        step_frames(e, 4)
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 4)
    u_after = stage_instance_usage(e)
    t.check(u_after == u_base, "fod-no-heap-leak", f"StageInstance blocks/bytes {u_base} -> {u_after}")

    t.end_stage()


def test_smashville(t):
    e, r = t.e, t.r
    preset, _ = t.start_stage(0x21)
    t.check(preset == 3, "sv-detect", f"detected preset {preset} (want 3)")
    g80 = find_ground(e, 80)
    g81 = find_ground(e, 81)
    t.check(g80 is not None and g81 is not None, "sv-grounds", f"80={g80} 81={g81}")

    # Hazards ON -> wide range at Right (mode=4, f=0) and Left (mode=3, f=600).
    r.set(S_STAGE_HAZARDS, 1)
    r.set(S_STAGE_PLATFORM, 4)
    step_frames(e, 4)
    u_base = stage_instance_usage(e)
    a_on = (e.u8(g80 + SV_CUR_ANIM), e.u8(g81 + SV_CUR_ANIM))
    b_on_r = ground_coll_bboxes(e, g80)
    x_on_r = 0.5 * (b_on_r[0][1] + b_on_r[0][2]) if b_on_r else 0.0
    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    b_on_l = ground_coll_bboxes(e, g80)
    x_on_l = 0.5 * (b_on_l[0][1] + b_on_l[0][2]) if b_on_l else 0.0
    t.check(a_on == (0, 0) and abs(x_on_r - x_on_l) > 180.0,
            "sv-haz-on-range", f"anim={a_on} X_right={x_on_r:.2f} X_left={x_on_l:.2f} span={abs(x_on_r - x_on_l):.2f}")

    # Center on HazOn: check phase lock 80 == 81 and X near 0.
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    f80_on, f81_on = ground_frame(e, g80, 0), ground_frame(e, g81, 0)
    b_on_c = ground_coll_bboxes(e, g80)
    x_on_c = 0.5 * (b_on_c[0][1] + b_on_c[0][2]) if b_on_c else 999.0
    t.check(abs(f80_on - f81_on) < 0.01 and abs(x_on_c) < 1.5 and ground_paused(e, g80) and ground_paused(e, g81),
            "sv-center-hazon", f"f80={f80_on:.2f} f81={f81_on:.2f} X_center={x_on_c:.2f}")

    # Hazards OFF -> narrowed range.
    r.set(S_STAGE_HAZARDS, 2)
    r.set(S_STAGE_PLATFORM, 4)
    step_frames(e, 4)
    a_off = (e.u8(g80 + SV_CUR_ANIM), e.u8(g81 + SV_CUR_ANIM))
    b_off_r = ground_coll_bboxes(e, g80)
    x_off_r = 0.5 * (b_off_r[0][1] + b_off_r[0][2]) if b_off_r else 0.0
    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    b_off_l = ground_coll_bboxes(e, g80)
    x_off_l = 0.5 * (b_off_l[0][1] + b_off_l[0][2]) if b_off_l else 0.0
    t.check(a_off == (1, 1) and 110.0 < abs(x_off_r - x_off_l) < 155.0,
            "sv-haz-off-range", f"anim={a_off} X_right={x_off_r:.2f} X_left={x_off_l:.2f} span={abs(x_off_r - x_off_l):.2f}")

    # Center on HazOff + shadow phase lock + Freeze.
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    f80_off, f81_off = ground_frame(e, g80, 0), ground_frame(e, g81, 0)
    b_off_c = ground_coll_bboxes(e, g80)
    x_off_c = 0.5 * (b_off_c[0][1] + b_off_c[0][2]) if b_off_c else 999.0
    t.check(abs(f80_off - f81_off) < 0.01 and abs(x_off_c) < 5.0,
            "sv-center-hazoff", f"f80={f80_off:.2f} f81={f81_off:.2f} X_center={x_off_c:.2f}")

    # Repeated hazard toggles -> zero leak.
    for _ in range(3):
        r.set(S_STAGE_HAZARDS, 1)
        step_frames(e, 4)
        r.set(S_STAGE_HAZARDS, 2)
        step_frames(e, 4)
    r.set(S_STAGE_HAZARDS, 1)
    step_frames(e, 4)
    u_after = stage_instance_usage(e)
    t.check(u_after == u_base, "sv-no-heap-leak", f"StageInstance blocks/bytes {u_base} -> {u_after}")

    t.end_stage()


def test_ghz(t):
    e, r = t.e, t.r
    preset, _ = t.start_stage(0x23)
    t.check(preset == 4, "ghz-detect", f"detected preset {preset} (want 4)")
    g80 = find_ground(e, 80)
    t.check(g80 is not None, "ghz-ground", f"ModelData[80]={g80}")

    # Hazards ON extreme vs Center.
    r.set(S_STAGE_HAZARDS, 1)
    r.set(S_STAGE_PLATFORM, 4)  # Right extreme (f=151)
    step_frames(e, 4)
    u_base = stage_instance_usage(e)
    b_on_ext = ground_coll_bboxes(e, g80)
    x_on_ext = 0.5 * (b_on_ext[0][1] + b_on_ext[0][2]) if b_on_ext else 0.0
    r.set(S_STAGE_PLATFORM, 2)  # Center (f=0)
    step_frames(e, 4)
    b_on_c = ground_coll_bboxes(e, g80)
    x_on_c = 0.5 * (b_on_c[0][1] + b_on_c[0][2]) if b_on_c else 999.0
    t.check(e.u8(g80 + GHZ_CUR_ANIM) == 0 and abs(x_on_ext) > 45.0 and abs(x_on_c) < 3.0,
            "ghz-haz-on", f"anim={e.u8(g80 + GHZ_CUR_ANIM)} X_ext={x_on_ext:.2f} X_center={x_on_c:.2f}")

    # Hazards OFF extreme vs Center.
    r.set(S_STAGE_HAZARDS, 2)
    r.set(S_STAGE_PLATFORM, 4)  # f=0 (-46.8 deg)
    step_frames(e, 4)
    b_off_ext = ground_coll_bboxes(e, g80)
    x_off_ext = 0.5 * (b_off_ext[0][1] + b_off_ext[0][2]) if b_off_ext else 0.0
    r.set(S_STAGE_PLATFORM, 2)  # Center (f=115)
    step_frames(e, 4)
    b_off_c = ground_coll_bboxes(e, g80)
    x_off_c = 0.5 * (b_off_c[0][1] + b_off_c[0][2]) if b_off_c else 999.0
    t.check(e.u8(g80 + GHZ_CUR_ANIM) == 1 and 30.0 < abs(x_off_ext) < abs(x_on_ext) - 10.0 and abs(x_off_c) < 3.0,
            "ghz-haz-off", f"anim={e.u8(g80 + GHZ_CUR_ANIM)} X_ext={x_off_ext:.2f} X_center={x_off_c:.2f}")

    # Freeze holds frame.
    r.set(S_STAGE_PLATFORM, 0)
    step_frames(e, 15)
    r.set(S_STAGE_PLATFORM, 1)
    step_frames(e, 2)
    f0 = ground_frame(e, g80, 0)
    step_frames(e, 20)
    f1 = ground_frame(e, g80, 0)
    t.check(abs(f1 - f0) < 0.05 and ground_paused(e, g80), "ghz-freeze", f"frame {f0:.2f}->{f1:.2f}")

    # Zero leak across hazard toggles.
    r.set(S_STAGE_HAZARDS, 1)
    step_frames(e, 4)
    u_after = stage_instance_usage(e)
    t.check(u_after == u_base, "ghz-no-heap-leak", f"StageInstance blocks/bytes {u_base} -> {u_after}")

    t.end_stage()


def test_dream_land(t):
    e, r = t.e, t.r
    preset, ss_addr = t.start_stage(0x2D)
    haz_pre = nav.melee_setup(e)["hazard_off"]
    t.check(preset == 2, "dl-detect", f"detected preset {preset} (want 2)")
    whispy = find_ground(e, 2)
    st = stage_ptr(e)
    t.check(whispy is not None and st is not None, "dl-whispy", f"whispy={whispy} stage={st}")

    # Hazards OFF: Whispy timer NaN, top blastzone == 220.0.
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 6)
    u_off = stage_instance_usage(e)
    timer_off = e.f32(whispy + WHISPY_TIMER)
    dead_up_off = e.f32(st + STAGE_DEAD_RANGE + 8)
    t.check(math.isnan(timer_off) and abs(dead_up_off - 220.0) < 0.1,
            "dl-haz-off", f"timer={timer_off} dead_up={dead_up_off:.2f}")

    # Hazards ON: Whispy timer re-seeds to [800, 1800] and counts down, top blastzone == 230.0.
    r.set(S_STAGE_HAZARDS, 1)
    step_frames(e, 6)
    t1 = e.f32(whispy + WHISPY_TIMER)
    dead_up_on = e.f32(st + STAGE_DEAD_RANGE + 8)
    step_frames(e, 15)
    t2 = e.f32(whispy + WHISPY_TIMER)
    t.check((not math.isnan(t1)) and 700.0 <= t1 <= 1805.0 and t2 < t1 - 10.0 and abs(dead_up_on - 230.0) < 0.1,
            "dl-haz-on", f"timer {t1:.1f}->{t2:.1f} dead_up={dead_up_on:.2f}")

    # Live mid-blow abort: fast-forward Whispy's cooldown timer so Whispy enters
    # blow state 4 or 5, then switch Hazards OFF and verify clean abort to idle.
    e.h.call("write_mem", addr=whispy + WHISPY_TIMER, hex=struct.pack(">f", 2.0).hex())
    blow_state = None
    for _ in range(180):
        step_frames(e, 1)
        st_val = e.u8(whispy + WHISPY_STATE)
        if st_val in (4, 5):
            blow_state = st_val
            break
    t.check(blow_state in (4, 5), "dl-enter-blow", f"whispy blow state={blow_state}")
    if blow_state in (4, 5):
        r.set(S_STAGE_HAZARDS, 2)  # Hazards OFF mid-blow!
        step_frames(e, 4)
        st_after = e.u8(whispy + WHISPY_STATE)
        tm_after = e.f32(whispy + WHISPY_TIMER)
        ef_after = e.u32(whispy + 0x158)
        dead_after = e.f32(st + STAGE_DEAD_RANGE + 8)
        t.check(st_after == 0 and math.isnan(tm_after) and ef_after == 0 and abs(dead_after - 220.0) < 0.1,
                "dl-mid-blow-abort", f"state {blow_state}->{st_after} timer={tm_after} efId={ef_after} dead_up={dead_after:.2f}")

    # Repeated toggles OFF -> ON -> OFF: check zero StageInstance leak!
    for _ in range(3):
        r.set(S_STAGE_HAZARDS, 2)
        step_frames(e, 4)
        r.set(S_STAGE_HAZARDS, 1)
        step_frames(e, 4)
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 4)
    u_off_after = stage_instance_usage(e)
    t.check(u_off_after == u_off, "dl-no-heap-leak", f"StageInstance blocks/bytes {u_off} -> {u_off_after}")

    # Savestate save + load after live toggle: must succeed (never kErrResources!).
    if ss_addr is not None:
        res_s = ss_trigger(e, ss_addr, 1)
        step_frames(e, 10)
        res_l = ss_trigger(e, ss_addr, 2)
        t.check(res_s[0] == "saved" and res_l[0] == "loaded",
                "dl-savestate-safe", f"save={res_s} load={res_l}")

    # Force Hazards = Off and exit back to CSS without resetting Stage Options
    # first: stageOptionsDetach() must restore global m_isHazardOff.
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 4)
    haz_forced = nav.melee_setup(e)["hazard_off"]
    t.end_stage(reset_settings=False)
    haz_after = nav.melee_setup(e)["hazard_off"]
    reset_stage_settings(r)
    t.check(haz_pre == 0 and haz_forced == 1 and haz_after == haz_pre,
            "dl-exit-restores-hazard-flag",
            f"m_isHazardOff pre={haz_pre} forced={haz_forced} after_exit={haz_after}")


def test_delfino(t):
    e, r = t.e, t.r
    preset, _ = t.start_stage(0x03)
    t.check(preset == 7, "delfino-detect", f"detected preset {preset} (want 7)")
    g1 = find_ground(e, 1)
    t.check(g1 is not None, "delfino-ground", f"ModelData[1]={g1}")

    # Tri-Plat (mode=2, f=0): highest platform joint Y > 40.
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    f_tri = ground_frame(e, g1, 0)
    ys_tri = sorted(0.5 * (b[3] + b[4]) for b in ground_coll_bboxes(e, g1))
    t.check(abs(f_tri - 0.0) < 0.1 and ys_tri and max(ys_tri) > 40.0,
            "delfino-tri-plat", f"f={f_tri:.1f} plat_ys={[round(y, 2) for y in ys_tri]}")

    # Two-Plat (mode=3, f=2000): center platform sinks ~50 units below the Tri-Plat height.
    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    f_two = ground_frame(e, g1, 0)
    ys_two = sorted(0.5 * (b[3] + b[4]) for b in ground_coll_bboxes(e, g1))
    t.check(abs(f_two - 2000.0) < 0.1 and ys_two and max(ys_tri) - min(ys_two) > 45.0
            and max(ys_tri) > max(ys_two) + 20.0,
            "delfino-two-plat", f"f={f_two:.1f} plat_ys={[round(y, 2) for y in ys_two]}")

    t.end_stage()


def test_dead_line(t):
    e, r = t.e, t.r
    preset, ss_addr = t.start_stage(0x44)
    t.check(preset == 5, "dl2-detect", f"detected preset {preset} (want 5)")
    g80 = find_ground(e, 80)
    t.check(g80 is not None, "dl2-ground", f"ModelData[80]={g80}")

    # Close (2, f=0), Far (3, f=3000), Mid (4, f=6000) + Freeze & Cycle.
    r.set(S_STAGE_HAZARDS, 1)
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    u_base = stage_instance_usage(e)
    f_close = ground_frame(e, g80, 0)
    b_close = ground_coll_bboxes(e, g80)
    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    f_far = ground_frame(e, g80, 0)
    b_far = ground_coll_bboxes(e, g80)
    r.set(S_STAGE_PLATFORM, 4)
    step_frames(e, 4)
    f_mid = ground_frame(e, g80, 0)
    t.check(abs(f_close - 0.0) < 0.1 and abs(f_far - 3000.0) < 0.1 and abs(f_mid - 6000.0) < 0.1
            and ground_paused(e, g80) and len(b_close) >= 2 and len(b_far) >= 2,
            "dl2-presets", f"frames=({f_close:.1f},{f_far:.1f},{f_mid:.1f}) joints=({len(b_close)},{len(b_far)})")

    # Live Hazards OFF / ON swaps anim 0 <-> 1 without StageInstance heap leak.
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 4)
    a_off = e.u8(g80 + GHZ_CUR_ANIM)
    r.set(S_STAGE_HAZARDS, 1)
    step_frames(e, 4)
    a_on = e.u8(g80 + GHZ_CUR_ANIM)
    u_after = stage_instance_usage(e)
    t.check(a_off == 1 and a_on == 0 and u_after == u_base,
            "dl2-haz-no-leak", f"anim off={a_off} on={a_on} heap {u_base}->{u_after}")

    # Savestate re-apply & no kErrResources.
    if ss_addr is not None:
        r.set(S_STAGE_PLATFORM, 2)
        step_frames(e, 4)
        res_s = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 3)
        step_frames(e, 2)
        res_l = ss_trigger(e, ss_addr, 2)
        step_frames(e, 3)
        f_re = ground_frame(e, g80, 0)
        t.check(res_s[0] == "saved" and res_l[0] == "loaded" and abs(f_re - 3000.0) < 0.1,
                "dl2-savestate-reapply", f"save={res_s} load={res_l} frame={f_re:.2f}")

    # Cycle advances frame.
    r.set(S_STAGE_PLATFORM, 0)
    step_frames(e, 2)
    fa = ground_frame(e, g80, 0)
    step_frames(e, 15)
    fb = ground_frame(e, g80, 0)
    t.check(abs(fb - fa) > 10.0 and not ground_paused(e, g80), "dl2-cycle", f"frame {fa:.2f}->{fb:.2f}")

    t.end_stage()


def test_yoshis_story(t):
    e, r = t.e, t.r
    preset, ss_addr = t.start_stage(0x2A)
    t.check(preset == 8, "ys-detect", f"detected preset {preset} (want 8)")
    g = find_moving_coll_ground(e)
    t.check(g is not None, "ys-ground", f"Randall Ground at {g:#x}" if g else "missing")

    def randall_x():
        boxes = ground_coll_bboxes(e, g)
        # STGYOSHISTORY COLL[2] binds both the static main stage (width ~119)
        # and Randall (width ~11.9) to the same Ground; pick Randall's joint.
        narrow = [b for b in boxes if (b[2] - b[1]) < 30.0]
        if not narrow:
            return 999.0
        return 0.5 * (narrow[0][1] + narrow[0][2])

    # Left Out (2, f=0), Right Out (3, f=600), Hidden (4, f=340).
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    fl = ground_frame(e, g, 0)
    xl = randall_x()

    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    fr = ground_frame(e, g, 0)
    xr = randall_x()

    r.set(S_STAGE_PLATFORM, 4)
    step_frames(e, 4)
    fh = ground_frame(e, g, 0)
    xh = randall_x()

    t.check(abs(fl - 0.0) < 0.1 and xl < -45.0 and abs(fr - 600.0) < 0.1 and xr > 45.0
            and abs(fh - 340.0) < 0.1 and abs(xh) < 45.0 and ground_paused(e, g),
            "ys-randall-presets",
            f"L(f={fl:.1f},x={xl:.1f}) R(f={fr:.1f},x={xr:.1f}) H(f={fh:.1f},x={xh:.1f})")

    # Savestate re-apply (Left Out saved -> switch to Right Out -> load -> Right Out) & Freeze preserve.
    if ss_addr is not None:
        r.set(S_STAGE_PLATFORM, 2)
        step_frames(e, 4)
        res_s = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 3)
        step_frames(e, 2)
        res_l = ss_trigger(e, ss_addr, 2)
        step_frames(e, 3)
        f_re = ground_frame(e, g, 0)
        t.check(res_s[0] == "saved" and res_l[0] == "loaded" and abs(f_re - 600.0) < 0.1,
                "ys-savestate-reapply", f"save={res_s} load={res_l} frame={f_re:.2f}")

        r.set(S_STAGE_PLATFORM, 0)
        step_frames(e, 20)
        r.set(S_STAGE_PLATFORM, 1)
        step_frames(e, 2)
        f_frz = ground_frame(e, g, 0)
        res_s2 = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 0)
        step_frames(e, 20)
        r.set(S_STAGE_PLATFORM, 1)
        res_l2 = ss_trigger(e, ss_addr, 2)
        step_frames(e, 4)
        f_rest = ground_frame(e, g, 0)
        t.check(res_s2[0] == "saved" and res_l2[0] == "loaded" and abs(f_rest - f_frz) < 1.5 and ground_paused(e, g),
                "ys-savestate-freeze", f"saved f={f_frz:.2f} restored f={f_rest:.2f}")

    # Cycle advances frame.
    r.set(S_STAGE_PLATFORM, 0)
    step_frames(e, 2)
    fa = ground_frame(e, g, 0)
    step_frames(e, 15)
    fb = ground_frame(e, g, 0)
    t.check(abs(fb - fa) > 10.0 and not ground_paused(e, g), "ys-cycle", f"frame {fa:.2f}->{fb:.2f}")

    t.end_stage()


def test_yoshis_island(t):
    e, r = t.e, t.r
    preset, ss_addr = t.start_stage(0x0D)
    haz_pre = nav.melee_setup(e)["hazard_off"]
    t.check(preset == 10, "yi-detect", f"detected preset {preset} (want 10)")
    g80 = find_ground(e, 80)
    g81 = find_ground(e, 81)
    t.check(g80 is not None and g81 is not None, "yi-grounds", f"80={g80} 81={g81}")

    def cloud_ys():
        # Returns (left_y, right_y) from COLL[3] joints on ModelData[80].
        boxes = ground_coll_bboxes(e, g80)
        left_y = next((0.5 * (b[3] + b[4]) for b in boxes if 0.5 * (b[1] + b[2]) < 0.0), -999.0)
        right_y = next((0.5 * (b[3] + b[4]) for b in boxes if 0.5 * (b[1] + b[2]) > 0.0), -999.0)
        return left_y, right_y

    # Left Out (2, f=1020), Right Out (3, f=2460), Hidden (4, f=0) with g81 synced.
    r.set(S_STAGE_HAZARDS, 1)
    r.set(S_STAGE_PLATFORM, 2)
    step_frames(e, 4)
    u_base = stage_instance_usage(e)
    fl80, fl81 = ground_frame(e, g80, 3), ground_frame(e, g81, 0)
    ly_l, ry_l = cloud_ys()

    r.set(S_STAGE_PLATFORM, 3)
    step_frames(e, 4)
    fr80, fr81 = ground_frame(e, g80, 3), ground_frame(e, g81, 0)
    ly_r, ry_r = cloud_ys()

    r.set(S_STAGE_PLATFORM, 4)
    step_frames(e, 4)
    fh80, fh81 = ground_frame(e, g80, 3), ground_frame(e, g81, 0)
    ly_h, ry_h = cloud_ys()

    t.check(abs(fl80 - 1020.0) < 0.1 and abs(fl81 - 1020.0) < 0.1 and ly_l > -20.0 and ry_l < -150.0
            and abs(fr80 - 2460.0) < 0.1 and abs(fr81 - 2460.0) < 0.1 and ry_r > -20.0 and ly_r < -150.0
            and abs(fh80 - 0.0) < 0.1 and abs(fh81 - 0.0) < 0.1 and ly_h < -150.0 and ry_h < -150.0,
            "yi-cloud-presets",
            f"L({fl80:.0f}/{fl81:.0f}, y={ly_l:.1f}/{ry_l:.1f}) "
            f"R({fr80:.0f}/{fr81:.0f}, y={ly_r:.1f}/{ry_r:.1f}) "
            f"H({fh80:.0f}/{fh81:.0f}, y={ly_h:.1f}/{ry_h:.1f})")

    # Freeze & Cycle.
    r.set(S_STAGE_PLATFORM, 0)
    step_frames(e, 2)
    fa = ground_frame(e, g80, 3)
    step_frames(e, 15)
    fb = ground_frame(e, g80, 3)
    r.set(S_STAGE_PLATFORM, 1)
    step_frames(e, 2)
    fc0 = ground_frame(e, g80, 3)
    step_frames(e, 15)
    fc1 = ground_frame(e, g80, 3)
    t.check(abs(fb - fa) > 10.0 and abs(fc1 - fc0) < 0.05 and ground_paused(e, g80) and ground_paused(e, g81),
            "yi-cycle-freeze", f"cycle {fa:.1f}->{fb:.1f} freeze {fc0:.1f}->{fc1:.1f}")

    # Live Hazards OFF hides g80/g81, disables collision, parks at f=0; Hazards ON restores both.
    r.set(S_STAGE_PLATFORM, 2)  # Left Out active underneath
    r.set(S_STAGE_HAZARDS, 2)   # Hazards OFF
    step_frames(e, 4)
    off_coll = ground_coll_enabled(e, g80)
    off_vis = (ground_visible(e, g80), ground_visible(e, g81))
    off_fr = ground_frame(e, g80, 3)
    haz_off_flag = nav.melee_setup(e)["hazard_off"]
    t.check((not off_coll) and off_vis == (False, False) and abs(off_fr - 0.0) < 0.1 and haz_off_flag == 1,
            "yi-haz-off", f"coll={off_coll} vis={off_vis} f={off_fr:.1f} flag={haz_off_flag}")

    r.set(S_STAGE_HAZARDS, 1)   # Hazards ON -> restores Left Out (f=1020)
    step_frames(e, 4)
    on_coll = ground_coll_enabled(e, g80)
    on_vis = (ground_visible(e, g80), ground_visible(e, g81))
    on_fr = ground_frame(e, g80, 3)
    haz_on_flag = nav.melee_setup(e)["hazard_off"]
    t.check(on_coll and on_vis == (True, True) and abs(on_fr - 1020.0) < 0.1 and haz_on_flag == 0,
            "yi-haz-on", f"coll={on_coll} vis={on_vis} f={on_fr:.1f} flag={haz_on_flag}")

    # Repeated toggles -> zero StageInstance leak!
    for _ in range(3):
        r.set(S_STAGE_HAZARDS, 2)
        step_frames(e, 4)
        r.set(S_STAGE_HAZARDS, 1)
        step_frames(e, 4)
    u_after = stage_instance_usage(e)
    t.check(u_after == u_base, "yi-no-heap-leak", f"StageInstance blocks/bytes {u_base} -> {u_after}")

    # Savestate preservation: save Left Out -> switch to Right Out -> load -> Right Out re-applied;
    # and Hazards OFF stays OFF after load.
    if ss_addr is not None:
        r.set(S_STAGE_HAZARDS, 1)
        r.set(S_STAGE_PLATFORM, 2)
        step_frames(e, 4)
        res_s = ss_trigger(e, ss_addr, 1)
        r.set(S_STAGE_PLATFORM, 3)
        step_frames(e, 2)
        res_l = ss_trigger(e, ss_addr, 2)
        step_frames(e, 3)
        f_re = ground_frame(e, g80, 3)
        r.set(S_STAGE_HAZARDS, 2)
        step_frames(e, 2)
        res_l2 = ss_trigger(e, ss_addr, 2)
        step_frames(e, 3)
        coll_l2 = ground_coll_enabled(e, g80)
        t.check(res_s[0] == "saved" and res_l[0] == "loaded" and abs(f_re - 2460.0) < 0.1
                and res_l2[0] == "loaded" and not coll_l2,
                "yi-savestate", f"save={res_s} load1={res_l} f={f_re:.1f} load2={res_l2} coll_off={coll_l2}")

    # Dirty exit with Hazards OFF must restore global m_isHazardOff back to 0.
    r.set(S_STAGE_HAZARDS, 2)
    step_frames(e, 4)
    haz_forced = nav.melee_setup(e)["hazard_off"]
    t.end_stage(reset_settings=False)
    haz_after = nav.melee_setup(e)["hazard_off"]
    reset_stage_settings(r)
    t.check(haz_pre == 0 and haz_forced == 1 and haz_after == haz_pre,
            "yi-exit-restores-hazard-flag",
            f"m_isHazardOff pre={haz_pre} forced={haz_forced} after_exit={haz_after}")


def test_final_destination(t):
    e, r = t.e, t.r
    preset, _ = t.start_stage(0x02)
    t.check(preset == 0, "fd-detect-none", f"detected preset {preset} (want 0)")

    # Verify via real controller input that Stage & Cast compacts away all stage
    # rows on FD (Mario/Fox), leaving only row 0 (Fighter Swap) and row 1 (Show Inactive).
    e.h.call("resume")
    e.advance(4)
    pos = len(e.log_text())
    e.press(0, ["START"])
    e.wait_for("lab menu open", lambda: "lab: menu opened" in e.log_text()[pos:], 60)
    e.press(0, ["L"], frames=2, after=6)  # Wrap Lab -> Stage & Cast
    for _ in range(3):
        e.press(0, [], stick=LT.DOWN, frames=2, after=4)
    # Close & reopen with Start; log prints "on page 6 (Stage & Cast) row 1".
    e.press(0, ["START"], frames=2, after=6)
    e.wait_for("lab menu close", lambda: "lab: menu closed" in e.log_text()[pos:], 60)
    pos2 = len(e.log_text())
    e.press(0, ["START"], frames=2, after=6)
    e.wait_for("lab menu reopen", lambda: "lab: menu opened" in e.log_text()[pos2:], 60)
    tail2 = e.log_text()[pos2:]
    e.press(0, ["START"], frames=2, after=6)
    e.wait_for("lab menu close 2", lambda: "lab: menu closed" in e.log_text()[pos2:], 60)
    t.check("(Stage & Cast) row 1" in tail2,
            "fd-stage-row-disabled", f"reopen log: {tail2.strip().splitlines()[-1] if tail2.strip() else 'none'}")

    e.h.call("pause")
    t.end_stage()


DISPATCH = {
    "luigis-mansion": test_luigis_mansion,
    "fountain-of-dreams": test_fod,
    "smashville": test_smashville,
    "green-hill-zone": test_ghz,
    "dead-line": test_dead_line,
    "dream-land": test_dream_land,
    "delfino-secret": test_delfino,
    "yoshis-story": test_yoshis_story,
    "yoshis-island": test_yoshis_island,
    "final-destination": test_final_destination,
}


def cmd_test_stage_options(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage-options")
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

    label = "test-stage-options"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            t = Runner(emu, opts.keep_going)
            try:
                t.step = "boot"
                nav.boot_to_training_css(emu)
                for name in selected:
                    t.step = name
                    DISPATCH[name](t)
            except (Timeout, nav.NavError, HarnessError, StageOptError, DO.DisplayError, LT.LabError,
                    PI.InputError, struct.error, KeyError, TypeError, IndexError, ValueError) as exc:
                why = f"{t.step}: {type(exc).__name__}: {exc}"
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{t.step if t else 'boot'}: {exc}"

    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for line in (t.failures if t else []):
        print(f"FAIL {line}")
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
