"""Emulator test suite for Stage 3D features (run through emu.py):

  test-stage3d REL [--only NAME ...] [--list] [--keep-going]

One boot, Mario (P1) against a Fox CPU on Final Destination, deterministic
single-core with D3D rendering (for screenshot verification of the hitbox
heatmap and reach bounds).

Scenarios:
  start-on-input  SF6-style Start On First Input recording: standalone mode
                  holds s_t=0 and skips slotWrite while neutral, triggers on
                  first stick/button press and replays exact timing;
                  co-playback mode advances s_t while armed so the playing
                  side stays synced and records slot.start on first input.
  cc-hitstun      Crouch-Cancel counter timing: crouching Fox CPU at 0% with
                  ASDI Down and Ground Counter struck by P1 Mario jab, dtilt,
                  and fireball counters on the exact first actionable frame
                  without delay (both Landing-from-Damage_Air and grounded Damage).
  heatmap         Clean-room hitbox heatmap and live reach bounds: verifies
                  shapeOk == 1 and bubbleOk == 1, samples active attack
                  hitboxes into the ring buffer, computes non-empty
                  HitboxBounds width/height, draws via kBubble + GX_LINESTRIP
                  with screenshot check, and resets cleanly on idle and
                  savestate load.
"""
import argparse
import os
import re
import struct
import time

import cpu_tests as C
import display_options_tests as DO
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import record_tests as R
from harness import HarnessError, Timeout

# main::Settings offsets (src/main/settings.h).
S_HEATMAP = 47
S_HEATMAP_BOUNDS = 48
S_HEATMAP_OPACITY = 49
S_HEATMAP_COLOR_FRAMES = 50
S_REC_START_ON_INPUT = R.SET_REC + 27  # offset 211

FLAG_REC_ARMED = 0x1000
HEATMAP_MAGIC = 0x544D484D  # "TMHM"
BOUNDS_STRIDE = 52  # 12 floats + u8 valid + u8 pad[3]

HEATMAP_BLOCK_RE = re.compile(r"heatmap: debug block at ([0-9a-f]{8})")
SS_BLOCK_RE = re.compile(r"savestate: test block at ([0-9a-f]{8})")
SS_SAVED_RE = re.compile(r"savestate: saved frame (\d+)")
SS_LOADED_RE = re.compile(r"savestate: loaded frame (\d+)")

SCENARIOS = ["start-on-input", "cc-hitstun", "heatmap"]


class Stage3DError(RuntimeError):
    pass


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def read_rec_block(emu, addr):
    raw = emu.read(addr, 0x60)
    magic, req, res, _, steps, t0, t1, flags, restores = struct.unpack_from(">9I", raw, 0)
    starts = [[struct.unpack_from(">H", raw, 0x2C + (s * 6 + i) * 2)[0] for i in range(6)] for s in range(2)]
    ends = [[struct.unpack_from(">H", raw, 0x44 + (s * 6 + i) * 2)[0] for i in range(6)] for s in range(2)]
    return {"magic": magic, "req": req, "res": res, "steps": steps, "t": t0, "flags": flags,
            "restores": restores, "start": starts, "end": ends}


def read_heatmap_block(emu, addr):
    raw = emu.read(addr, 0x20 + 4 * BOUNDS_STRIDE)
    magic, shape_ok, bubble_ok, ring_count, total_sampled, draw_calls, bounds_drawn, resets = \
        struct.unpack_from(">8I", raw, 0)
    bounds = []
    for p in range(4):
        off = 0x20 + p * BOUNDS_STRIDE
        vals = struct.unpack_from(">12fB", raw, off)
        bounds.append({
            "xmin": vals[0], "xmax": vals[1], "ymin": vals[2], "ymax": vals[3],
            "width": vals[4], "height": vals[5],
            "worldXMin": vals[6], "worldXMax": vals[7], "worldYMin": vals[8], "worldYMax": vals[9],
            "originX": vals[10], "originY": vals[11], "valid": vals[12],
        })
    return {"magic": magic, "shapeOk": shape_ok, "bubbleOk": bubble_ok, "ringCount": ring_count,
            "totalSampled": total_sampled, "drawCalls": draw_calls, "boundsDrawn": bounds_drawn,
            "resets": resets, "bounds": bounds}


def send_pad(emu, buttons=(), stick=(128, 128), cstick=(128, 128), l_analog=0, r_analog=0):
    emu.hold(0, buttons=buttons, stick=stick, cstick=cstick, l=l_analog, r=r_analog)


def clear_pad(emu):
    emu.neutral(0)


def read_fighter_detail(game, en):
    st = game.fighter(en)
    ai_main, _ = game.inputs(en)
    p = game.pad(ai_main) if ai_main else {"buttons": 0, "x": 0.0, "y": 0.0}
    st["buttons"] = p["buttons"]
    st["sx"] = p["x"]
    st["sy"] = p["y"]
    return st


def walk_close(emu, game, gap=9.0, max_frames=300):
    """Run then walk P1 Mario next to Fox CPU using real port 1 stick input."""
    m = game.entry(0)
    c = game.entry(1)
    # Phase 1: full run until close.
    for _ in range(max_frames):
        ms = game.fighter(m)
        cs = game.fighter(c)
        dx = cs["x"] - ms["x"]
        if abs(dx) <= max(gap + 14.0, 22.0):
            break
        stick_x = 240 if dx > 0 else 16
        send_pad(emu, stick=(stick_x, 128))
        step(emu, 1)
    clear_pad(emu)
    step(emu, 14)
    # Phase 2: controlled walk to exact spacing.
    for _ in range(max_frames):
        ms = game.fighter(m)
        cs = game.fighter(c)
        dx = cs["x"] - ms["x"]
        if abs(dx) <= gap:
            break
        stick_x = 185 if dx > 0 else 71
        send_pad(emu, stick=(stick_x, 128))
        step(emu, 1)
    clear_pad(emu)
    step(emu, 14)


class Runner:
    def __init__(self, emu, states_dir, keep_going=False):
        self.e = emu
        self.states_dir = states_dir
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.step_name = "boot"
        self.base_state = os.path.join(states_dir, "stage3d-base.sav")
        self.lab = LT.Lab(emu, [])
        self.g = C.Game(emu)
        self.rec = R.Rec(emu)
        self.hm_addr = None
        self.ss_addr = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise Stage3DError(f"{name}: {detail}")

    def set_u8(self, offset, val):
        self.e.h.call("write_mem", addr=self.lab.settings + offset, hex=f"{int(val) & 0xFF:02x}")

    def set_u16(self, offset, val):
        self.e.h.call("write_mem", addr=self.lab.settings + offset, hex=f"{int(val) & 0xFFFF:04x}")

    def wait_rec_req(self, req_name, max_steps=40):
        before = read_rec_block(self.e, self.rec.addr)["res"]
        self.rec.request(req_name)
        for _ in range(max_steps):
            step(self.e, 1)
            b = read_rec_block(self.e, self.rec.addr)
            if b["res"] != before and (b["flags"] & R.FLAG_SAVE_PENDING) == 0:
                return b
        raise Stage3DError(f"recording request {req_name!r} timed out")

    def wait_restore(self, prev_restores, max_steps=60):
        for _ in range(max_steps):
            step(self.e, 1)
            b = read_rec_block(self.e, self.rec.addr)
            if b["restores"] > prev_restores:
                return b
        raise Stage3DError("recording restore timed out")

    def load_base(self):
        clear_pad(self.e)
        self.e.h.call("load_state", path=self.base_state)
        self.e.h.call("pause")
        self.g.mods.clear()
        step(self.e, 2)


def make_base(t):
    e = t.e
    nav.boot_to_training_css(e)
    nav.start_training_match(e, "mario", "fox", seed_rng=True)
    nav.pause_at_game_frame(e, DO.BASE_FRAME)
    t.lab.find_settings()
    t.rec.find()
    hm = HEATMAP_BLOCK_RE.findall(e.log_text())
    if not hm:
        raise Stage3DError("no heatmap debug block line in log")
    t.hm_addr = int(hm[-1], 16)
    ss = SS_BLOCK_RE.findall(e.log_text())
    t.ss_addr = int(ss[-1], 16) if ss else None
    # Put CPU under Lab control, standing still, no DI, 0%.
    for off, val in DO.CPU_STILL.items():
        t.set_u8(off, val)
    t.set_u16(C.SET_CPU_PERCENT, 0)
    t.set_u8(C.SET_CPU_LOCK, 1)
    step(e, 10)
    e.h.call("save_state", path=t.base_state)
    t.results.append(("base-state", f"settings {t.lab.settings:#x}, rec {t.rec.addr:#x}, hm {t.hm_addr:#x}"))


# ---- Scenario 1: Start On First Input recording ------------------------------

def sc_start_on_input(t):
    e = t.e
    t.load_base()

    # Wait for recording storage and save positions.
    for _ in range(30):
        b = read_rec_block(e, t.rec.addr)
        if b["flags"] & R.FLAG_STORAGE:
            break
        step(e, 1)
    b = t.wait_rec_req("save")
    t.check((b["flags"] & R.FLAG_SAVED) != 0, "soi-save-positions", f"flags={b['flags']:#x}")

    # Standalone HMN Record with Start On Input = On.
    t.set_u8(S_REC_START_ON_INPUT, 1)
    t.rec.set(hmnMode=R.HMN_MODE["Record"], hmnSlot=1, cpuMode=R.CPU_MODE["Off"], cpuSlot=1)
    b = t.wait_rec_req("hmn-mode")
    t.check((b["flags"] & FLAG_REC_ARMED) != 0 and b["t"] == 0,
            "soi-standalone-armed", f"flags={b['flags']:#x} t={b['t']}")

    # Hold neutral for 15 frames: clock must stay at 0, armed must stay set, slot 1 empty.
    clear_pad(e)
    step(e, 15)
    b = read_rec_block(e, t.rec.addr)
    t.check((b["flags"] & FLAG_REC_ARMED) != 0 and b["t"] == 0 and b["end"][0][0] == 0,
            "soi-holds-zero-while-neutral", f"flags={b['flags']:#x} t={b['t']} end={b['end'][0][0]}")

    # Record a real movement + attack sequence starting with a right stick press.
    m_en = t.g.entry(0)
    rec_trace = []
    actions = [((), (228, 128))] * 10 + [((), (128, 128))] * 5 + [(["A"], (128, 128))] * 2 + [((), (128, 128))] * 15
    for btns, stk in actions:
        send_pad(e, buttons=btns, stick=stk)
        step(e, 1)
        b = read_rec_block(e, t.rec.addr)
        if b["t"] > 0:
            st = t.g.fighter(m_en)
            rec_trace.append((round(st["x"], 2), st["status"]))
    clear_pad(e)
    b = read_rec_block(e, t.rec.addr)
    t.check((b["flags"] & FLAG_REC_ARMED) == 0 and b["start"][0][0] == 0 and b["end"][0][0] >= 30,
            "soi-standalone-triggered",
            f"flags={b['flags']:#x} t={b['t']} start={b['start'][0][0]} end={b['end'][0][0]}")
    rec_len = b["end"][0][0]

    # Play back HMN slot 1 and verify exact movement + attack timing from frame 0.
    prev_res = b["restores"]
    t.set_u8(S_REC_START_ON_INPUT, 0)
    t.rec.set(hmnMode=R.HMN_MODE["Playback"], hmnSlot=1)
    t.rec.request("hmn-mode")
    t.wait_restore(prev_res)
    play_trace = []
    for _ in range(rec_len):
        step(e, 1)
        st = t.g.fighter(m_en)
        play_trace.append((round(st["x"], 2), st["status"]))
    exact = (len(play_trace) == len(rec_trace) and play_trace == rec_trace)
    moved = play_trace[-1][0] > play_trace[0][0] + 5.0
    attacked = any(s in (C.STATUS["Attack"], C.STATUS["Attack_Dash"]) for _, s in play_trace)
    t.check(exact and moved and attacked, "soi-standalone-playback-exact",
            f"exact={exact} x {play_trace[0][0]:.2f}->{play_trace[-1][0]:.2f}, atk={attacked}, frames={rec_len}")

    # Co-playback + record: record CPU slot 1 first (25 frames standing/neutral with Start On Input Off).
    t.set_u8(S_REC_START_ON_INPUT, 0)
    t.rec.set(hmnMode=R.HMN_MODE["Off"], cpuMode=R.CPU_MODE["Record"], cpuSlot=1)
    t.wait_rec_req("hmn-mode")
    t.wait_rec_req("cpu-mode")
    prev_res = read_rec_block(e, t.rec.addr)["restores"]
    t.rec.request("restore")
    t.wait_restore(prev_res)
    send_pad(e, stick=(28, 128))
    step(e, 10)
    clear_pad(e)
    step(e, 15)
    b = read_rec_block(e, t.rec.addr)
    t.check(b["end"][1][0] >= 24, "soi-coplay-cpu-slot-ready", f"cpu slot 1 end={b['end'][1][0]}")

    # Now co-playback CPU slot 1 + HMN Record slot 2 with Start On Input = On.
    t.set_u8(S_REC_START_ON_INPUT, 1)
    t.rec.set(hmnMode=R.HMN_MODE["Record"], hmnSlot=2, cpuMode=R.CPU_MODE["Playback"], cpuSlot=1)
    prev_res = read_rec_block(e, t.rec.addr)["restores"]
    t.rec.request("hmn-mode")
    step(e, 2)
    t.rec.request("cpu-mode")
    t.wait_restore(prev_res)
    b = read_rec_block(e, t.rec.addr)
    t.check((b["flags"] & FLAG_REC_ARMED) != 0, "soi-coplay-armed", f"flags={b['flags']:#x} t={b['t']}")

    # Step 10 neutral frames: s_t MUST advance (CPU playback stays synced), HMN slot 2 stays empty.
    clear_pad(e)
    step(e, 10)
    b = read_rec_block(e, t.rec.addr)
    t.check((b["flags"] & FLAG_REC_ARMED) != 0 and b["t"] >= 9 and b["end"][0][1] == 0,
            "soi-coplay-advances-clock",
            f"flags={b['flags']:#x} t={b['t']} hmnSlot2 end={b['end'][0][1]}")

    # Press A on port 1: unarms and sets slot.start to the non-neutral frame (~10-11).
    send_pad(e, buttons=["A"])
    step(e, 2)
    clear_pad(e)
    step(e, 5)
    b = read_rec_block(e, t.rec.addr)
    t.check((b["flags"] & FLAG_REC_ARMED) == 0 and 9 <= b["start"][0][1] <= 12 and b["end"][0][1] > b["start"][0][1],
            "soi-coplay-records-at-start",
            f"flags={b['flags']:#x} start={b['start'][0][1]} end={b['end'][0][1]}")

    t.wait_rec_req("delete")
    t.set_u8(S_REC_START_ON_INPUT, 0)


# ---- Scenario 2: Crouch-Cancel Hitstun Edge Trigger --------------------------

def run_cc_probe(t, move_name, buttons, stick, counter_name, counter_id, expected_statuses,
                 gap=7.0, tdi=None, asdi=None, behavior=None, cpu_pct=0):
    e = t.e
    t.load_base()
    if tdi is None:
        tdi = C.TDI["Outwards"]
    if asdi is None:
        asdi = C.ASDI["Down"]
    if behavior is None:
        behavior = C.BEHAVIOR["Crouch"]
    # Configure CPU: Lab control, Behavior Crouch, TDI, ASDI, 0%, Ground Counter = counter_id, Delay 0.
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["control"], C.LAB)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["behavior"], behavior)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["tdi"], tdi)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["asdi"], asdi)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["sdiAmount"], 0)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["counterGround"], counter_id)
    t.set_u8(C.SET_CPU + C.CPU_FIELDS["counterDelay"], 0)
    t.set_u16(C.SET_CPU_PERCENT, cpu_pct)
    t.set_u8(C.SET_CPU_LOCK, 1)

    # Walk Mario right up next to crouching Fox.
    walk_close(e, t.g, gap=gap)
    m_en = t.g.entry(0)
    c_en = t.g.entry(1)
    # Wait until Fox is settled in Squat_Wait (0x12) if crouching.
    if behavior == C.BEHAVIOR["Crouch"]:
        for _ in range(30):
            if t.g.fighter(c_en)["status"] == C.STATUS["Squat_Wait"]:
                break
            step(e, 1)
    pre_st = t.g.fighter(c_en)["status"]
    ms0 = t.g.fighter(m_en)
    cs0 = t.g.fighter(c_en)
    t.check(pre_st == C.STATUS["Squat_Wait"] or behavior != C.BEHAVIOR["Crouch"],
            f"cc-{move_name}-{counter_name}-crouching",
            f"status={C.sname(pre_st)} ({pre_st:#x}) dx={cs0['x'] - ms0['x']:.2f}")

    # Strike crouching Fox with P1's move (2 frames pressed, then neutral).
    trace = []
    for fidx in range(40):
        if fidx < 2:
            send_pad(e, buttons=buttons, stick=stick)
        else:
            clear_pad(e)
        step(e, 1)
        st = read_fighter_detail(t.g, c_en)
        mst = t.g.fighter(m_en)
        trace.append((fidx, st["status"], st["hitlag"], st["hitstun"], st["buttons"], round(st["sy"], 2), mst["status"]))

    # Print trace for diagnostic clarity.
    print(f"  [cc trace {move_name}/{counter_name}]")
    for row in trace[:28]:
        fidx, st, hl, hs, btns, sy, mst = row
        print(f"    f{fidx:02d}: p1={C.sname(mst):12s} cpu={C.sname(st):14s}({st:#04x}) hl={hl:2d} hs={hs:2d} btns={btns:#06x} sy={sy:+.2f}")

    # Find first hit frame, first Landing frame (if ASDI-down CC), first hs==0 after hit, and counter frame.
    hit_idx = next((i for i, r in enumerate(trace) if r[1] in C.HIT), None)
    land_idx = None
    hs0_idx = None
    counter_idx = None
    if hit_idx is not None:
        land_idx = next((i for i in range(hit_idx, len(trace)) if trace[i][1] == C.STATUS["Landing"]), None)
        hs0_idx = next((i for i in range(hit_idx + 1, len(trace)) if trace[i][2] == 0 and trace[i][3] == 0), None)
        counter_idx = next((i for i in range(hit_idx + 1, len(trace)) if trace[i][1] in expected_statuses), None)
    return hit_idx, land_idx, hs0_idx, counter_idx, trace


def sc_cc_hitstun(t):
    # Test 1: Mario Jab at 0% against crouching Fox (ASDI Down -> Landing) with Ground Counter = Shield.
    hit1, land1, _, ctr1, tr1 = run_cc_probe(
        t, "jab", ["A"], C.JAB, "shield", C.GROUND["Shield"],
        {C.STATUS["Guard_On"], C.STATUS["Guard"]}, gap=7.0, tdi=C.TDI["None"], asdi=C.ASDI["Down"])
    t.check(hit1 is not None and land1 is not None and ctr1 == land1 + 4 and tr1[ctr1][3] > 0,
            "cc-jab-asdi-shield-frame1",
            f"hit=f{hit1} land=f{land1} counter=f{ctr1} hs_at_ctr={tr1[ctr1][3] if ctr1 is not None else None}")

    # Test 2: Mario Dtilt at 0% against crouching Fox (ASDI Down -> Landing) with Ground Counter = Jab.
    hit2, land2, _, ctr2, tr2 = run_cc_probe(
        t, "dtilt", ["A"], C.DTILT, "jab", C.GROUND["Jab"],
        {C.STATUS["Attack"]}, gap=7.0, tdi=C.TDI["None"], asdi=C.ASDI["Down"])
    t.check(hit2 is not None and land2 is not None and ctr2 == land2 + 4 and tr2[ctr2][3] > 0,
            "cc-dtilt-asdi-jab-frame1",
            f"hit=f{hit2} land=f{land2} counter=f{ctr2} hs_at_ctr={tr2[ctr2][3] if ctr2 is not None else None}")

    # Test 3: Mario Fireball at 0% against crouching Fox (grounded Damage 0x43) with Ground Counter = Shield.
    hit3, _, hs0_3, ctr3, tr3 = run_cc_probe(
        t, "fireball", ["B"], (128, 128), "shield", C.GROUND["Shield"],
        {C.STATUS["Guard_On"], C.STATUS["Guard"]}, gap=14.0, tdi=C.TDI["None"], asdi=C.ASDI["Down"])
    t.check(hit3 is not None and tr3[hit3][1] == C.STATUS["Damage"]
            and hs0_3 is not None and ctr3 == hs0_3,
            "cc-fireball-grounded-shield-frame1",
            f"hit=f{hit3} st={C.sname(tr3[hit3][1]) if hit3 is not None else None} hs0=f{hs0_3} counter=f{ctr3}")


# ---- Scenario 3: Hitbox Heatmap + Live Reach Bounds --------------------------

def sc_heatmap(t):
    e = t.e
    t.load_base()

    # Check attach-time prologue verification flags in RAM ("TMHM").
    hm = read_heatmap_block(e, t.hm_addr)
    t.check(hm["magic"] == HEATMAP_MAGIC and hm["shapeOk"] == 1 and hm["bubbleOk"] == 1,
            "heatmap-prologues-ok",
            f"magic={hm['magic']:#x} shapeOk={hm['shapeOk']} bubbleOk={hm['bubbleOk']}")

    # Enable P1 Hitbox Heatmap + Bounds + Opacity 60% + Color Step 5f.
    t.set_u8(S_HEATMAP, 1)  # P1
    t.set_u8(S_HEATMAP_BOUNDS, 1)
    t.set_u8(S_HEATMAP_OPACITY, 60)
    t.set_u8(S_HEATMAP_COLOR_FRAMES, 5)
    step(e, 2)

    # Perform a short-hop neutral air + grounded F-Smash with Mario on port 1.
    send_pad(e, buttons=["X"])
    step(e, 2)
    clear_pad(e)
    step(e, 6)
    send_pad(e, buttons=["A"])
    step(e, 2)
    clear_pad(e)
    peak_ring = 0
    peak_draws = 0
    peak_bounds_drawn = 0
    shot_taken = False
    for fidx in range(25):
        step(e, 1)
        hm = read_heatmap_block(e, t.hm_addr)
        peak_ring = max(peak_ring, hm["ringCount"])
        peak_draws = max(peak_draws, hm["drawCalls"])
        peak_bounds_drawn = max(peak_bounds_drawn, hm["boundsDrawn"])
        if not shot_taken and hm["ringCount"] >= 4 and hm["drawCalls"] > 0 and hm["boundsDrawn"] > 0:
            shot_taken = True
            # Hold game via Frame Advance so screenshot captures this exact frame while running.
            t.set_u8(DO.S_FRAME_ADVANCE, 1)
            step(e, 1)
            e.h.call("resume")
            e.advance(2)
            t.shots.append(e.screenshot("stage3d-heatmap"))
            e.h.call("pause")
            t.set_u8(DO.S_FRAME_ADVANCE, 0)
            step(e, 1)

    hm = read_heatmap_block(e, t.hm_addr)
    b0 = hm["bounds"][0]
    t.check(peak_ring > 0 and hm["totalSampled"] > 0, "heatmap-sampled-capsules",
            f"peakRing={peak_ring} totalSampled={hm['totalSampled']}")
    t.check(peak_draws > 0 and peak_bounds_drawn > 0, "heatmap-rendered-bubbles-and-bounds",
            f"peakDraws={peak_draws} peakBoundsDrawn={peak_bounds_drawn}")
    t.check(b0["valid"] == 1 and b0["width"] > 1.0 and b0["height"] > 1.0
            and b0["xmax"] > b0["xmin"] and b0["ymax"] > b0["ymin"],
            "heatmap-bounds-valid",
            f"valid={b0['valid']} w={b0['width']:.2f} h={b0['height']:.2f} "
            f"x=[{b0['xmin']:.2f},{b0['xmax']:.2f}] y=[{b0['ymin']:.2f},{b0['ymax']:.2f}]")

    # Wait 65 idle frames (> kHeatmapDefaultLife = 60): ring must expire cleanly to 0.
    clear_pad(e)
    step(e, 68)
    hm_idle = read_heatmap_block(e, t.hm_addr)
    t.check(hm_idle["ringCount"] == 0, "heatmap-idle-ring-resets",
            f"ringCount={hm_idle['ringCount']} after idle")

    # Verify lab savestate load immediately resets ring + bounds.
    if t.ss_addr is not None:
        # Save a quick savestate first while idle.
        pos = len(e.log_text())
        e.h.call("write_mem", addr=t.ss_addr + 5, hex="01")
        for _ in range(30):
            step(e, 1)
            if SS_SAVED_RE.search(e.log_text()[pos:]):
                break
        # Throw an F-Smash so ringCount > 0 and bounds[0].valid == 1.
        send_pad(e, buttons=["A"], stick=C.FSMASH)
        step(e, 2)
        clear_pad(e)
        step(e, 14)
        hm_pre = read_heatmap_block(e, t.hm_addr)
        resets_before = hm_pre["resets"]
        t.check(hm_pre["ringCount"] > 0 and hm_pre["bounds"][0]["valid"] == 1,
                "heatmap-fsmash-populated",
                f"ringCount={hm_pre['ringCount']} valid={hm_pre['bounds'][0]['valid']}")
        # Load the quick savestate and confirm immediate reset.
        pos = len(e.log_text())
        e.h.call("write_mem", addr=t.ss_addr + 5, hex="02")
        loaded = False
        for _ in range(30):
            step(e, 1)
            if SS_LOADED_RE.search(e.log_text()[pos:]):
                loaded = True
                break
        hm_post = read_heatmap_block(e, t.hm_addr)
        t.check(loaded and hm_post["ringCount"] == 0 and hm_post["bounds"][0]["valid"] == 0
                and hm_post["resets"] > resets_before,
                "heatmap-savestate-load-resets",
                f"loaded={loaded} ringCount={hm_post['ringCount']} valid={hm_post['bounds'][0]['valid']} "
                f"resets={resets_before}->{hm_post['resets']}")

    t.set_u8(S_HEATMAP, 0)
    t.set_u8(S_HEATMAP_BOUNDS, 0)


DISPATCH = {
    "start-on-input": sc_start_on_input,
    "cc-hitstun": sc_cc_hitstun,
    "heatmap": sc_heatmap,
}


def cmd_test_stage3d(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stage3d")
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

    label = "test-stage3d"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            t = Runner(emu, states, opts.keep_going)
            try:
                t.step_name = "base"
                make_base(t)
                for name in selected:
                    t.step_name = name
                    DISPATCH[name](t)
            except (Timeout, nav.NavError, HarnessError, Stage3DError, DO.DisplayError, LT.LabError,
                    R.RecError, struct.error, KeyError, TypeError, IndexError, ValueError) as exc:
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
