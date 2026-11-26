"""Live Dolphin test suite for Draft B Skill Drills launcher section & Native
Brawl/P+ In-Match Drill HUD + End-Screen Modal (run through emu.py):

  test-skill-drills-ui REL [--only NAME ...] [--list] [--keep-going]

One boot, deterministic single-core with D3D rendering. Captures all 8 required
16:9 and 4:3 D3D screenshots:
  boot-skill-drills-16x9.png, boot-skill-drills-4x3.png,
  hud-ledgedash-16x9.png, hud-ledgedash-4x3.png,
  hud-wavedash-16x9.png, hud-lcancel-16x9.png,
  endscreen-retry-16x9.png, endscreen-retry-4x3.png.

Scenarios:
  boot-menu     Draft B boot launcher navigates to "Skill Drills" (Page 3),
                steps across all 6 drill rows, and captures 16:9 shot.
  ledgedash-hud Arms Ledgedash (DrillId 1) via controller A -> real CSS & stage
                select -> verifies "TMDH" RAM telemetry, performs a real FD
                ledgedash rep (positive GALINT), captures 16:9 Live HUD shot,
                opens End-Screen Modal, captures 16:9 modal shot, tests A
                (Retry) reset + retryCount increment, switches to 4:3 and tests
                B (Return to CSS) auto-reopening "Skill Drills" in true 4:3
                with persisted Personal Best (boot-skill-drills-4x3.png), then
                verifies Ledgedash HUD & End-Screen Modal in true 4:3.
  wavedash-hud  Arms Wavedash (DrillId 2) via controller A, performs a real
                Fox frame-1 wavedash on FD, verifies live scoring & Frame-1
                Perfect metric in "TMDH", and captures hud-wavedash-16x9.png.
  lcancel-lab   Arms L-Cancel (DrillId 3) via controller A, performs a real
                Mario short-hop Nair L-cancel on FD, verifies live scoring in
                "TMDH", captures hud-lcancel-16x9.png, opens End-Screen Modal,
                and tests Start (Lab) exiting drill mode into live Training Lab
                followed by clean match unload.
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import charge_tests as CT  # noqa: E402
import display_options_tests as DO  # noqa: E402
import drill_tests as DT  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import plugin_input as PI  # noqa: E402
import stage4a_core_tests as S4  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

TMDH_MAGIC = 0x544D4448  # "TMDH"
TMDH_VERSION = 1
TMDH_SIZE = 28
TMDH_RE = re.compile(r"drillhud: debug block at ([0-9a-f]{8})")

TMPD_MAGIC = 0x544D5044  # "TMPD"
TMPD_VERSION = 1
TMPD_SIZE = 52
TMPD_RE = re.compile(r"drill_practice: debug block at ([0-9a-f]{8})")

SCENARIOS = [
    "boot-menu",
    "ledgedash-hud",
    "wavedash-hud",
    "lcancel-lab",
    "powershield-cpu",
]


class SkillDrillsUiError(RuntimeError):
    pass


def read_tmpd(emu, addr):
    raw = emu.read(addr, TMPD_SIZE)
    (
        magic, version,
        active_drill, cpu_standby, ps_mode, ps_height,
        ledge_side, wd_active, wd_outcome, target_mode,
        ledge_snaps, wd_attempts, wd_hits, wd_streak, wd_best, draw_calls,
        wd_min_x, wd_max_x, wd_y,
    ) = struct.unpack_from(">IIBBBBBBBBIIIIIIfff", raw, 0)
    return {
        "magic": magic,
        "version": version,
        "activeDrill": active_drill,
        "cpuInStandby": cpu_standby,
        "psResolvedMode": ps_mode,
        "psResolvedHeight": ps_height,
        "ledgeActiveSide": ledge_side,
        "wdZoneActive": wd_active,
        "wdZoneLastOutcome": wd_outcome,
        "targetModeActive": target_mode,
        "ledgeSnapCount": ledge_snaps,
        "wdZoneAttempts": wd_attempts,
        "wdZoneHits": wd_hits,
        "wdZoneStreak": wd_streak,
        "wdZoneBestStreak": wd_best,
        "drawCalls": draw_calls,
        "wdZoneMinX": wd_min_x,
        "wdZoneMaxX": wd_max_x,
        "wdZoneY": wd_y,
    }


def find_tmpd(emu, since_pos):
    hits = TMPD_RE.findall(emu.log_text()[since_pos:])
    if not hits:
        hits = TMPD_RE.findall(emu.log_text())
    if not hits:
        raise SkillDrillsUiError("drill_practice TMPD debug block address not found in log")
    return int(hits[-1], 16)


def read_tmdh(emu, addr):
    raw = emu.read(addr, TMDH_SIZE)
    (
        magic, version,
        active_drill, reps_done, reps_hit, streak,
        best_streak, new_best, modal_open, last_grade,
        best_score, retry_count, best_metric,
        force_complete, alc_notice, draw_calls,
    ) = struct.unpack_from(">IIBBBBBBBBHHhBBI", raw, 0)
    return {
        "magic": magic,
        "version": version,
        "activeDrill": active_drill,
        "repsDone": reps_done,
        "repsHit": reps_hit,
        "streak": streak,
        "bestStreak": best_streak,
        "newBest": new_best,
        "modalOpen": modal_open,
        "lastGrade": last_grade,
        "bestScore": best_score,
        "retryCount": retry_count,
        "bestMetric": best_metric,
        "forceComplete": force_complete,
        "alcNotice": alc_notice,
        "drawCalls": draw_calls,
    }


def find_tmdh(emu, since_pos):
    hits = TMDH_RE.findall(emu.log_text()[since_pos:])
    if not hits:
        hits = TMDH_RE.findall(emu.log_text())
    if not hits:
        raise SkillDrillsUiError("drillhud TMDH debug block address not found in log")
    return int(hits[-1], 16)


def trigger_force_complete(emu, tmdh_addr):
    emu.h.call("write_mem", addr=tmdh_addr + 0x16, hex="01")


def perform_fd_ledgedash(emu, p1_ptr, tmpd_addr=None):
    def step(n=1):
        emu.h.call("frame_advance", n=n)

    # In Ledgedash Tech Practice, drillPracticeDriver auto-snaps P1 to the
    # active ledge (initially left, side=0) and D-Pad Right snaps directly
    # to the right ledge (side=1) without saving a savestate!
    if tmpd_addr is not None:
        emu.hold(0, ["DRIGHT"])
        step(2)
        emu.neutral(0)
        for _ in range(40):
            step(1)
            cur = S4.read_fighter_full(emu, p1_ptr)
            if cur["status"] == S4.ST_CLIFF_WAIT and cur["x"] > 70.0:
                break
        else:
            raise SkillDrillsUiError(
                f"D-Pad Right never snapped P1 to FD right ledge (last={S4.read_fighter_full(emu, p1_ptr)}, "
                f"tmpd={read_tmpd(emu, tmpd_addr)})"
            )
    else:
        posture_p1 = S4.read_fighter_full(emu, p1_ptr)["posture"]
        S4.grab_fd_right_ledge(emu, p1_ptr, posture_p1, start_from_stage=True)
    emu.neutral(0)
    step(2)
    emu.hold(0, stick=S4.DOWN)
    for _ in range(8):
        step(1)
        if S4.read_fighter_full(emu, p1_ptr)["status"] == S4.ST_FALL:
            break
    emu.hold(0, ["X"], stick=S4.LEFT)
    step(2)
    emu.hold(0, stick=S4.LEFT)
    for _ in range(18):
        step(1)
        if S4.read_fighter_full(emu, p1_ptr)["y"] > 1.0:
            break
    emu.hold(0, ["R"], stick=(38, 60))
    step(3)
    emu.neutral(0)
    for _ in range(45):
        step(1)
        cur = S4.read_fighter_full(emu, p1_ptr)
        if not cur["air"] and cur["status"] == S4.ST_WAIT:
            break
    step(3)


class Runner:
    def __init__(self, emu, keep_going=False):
        self.e = emu
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.step_name = "boot"
        self.settings = None
        self.tmpd = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
        else:
            self.failures.append(f"{name}: {detail}")
            if not self.keep_going:
                raise SkillDrillsUiError(f"{name}: {detail}")

    def snap(self, name):
        self.shots.append(self.e.screenshot(name))
        return self.shots[-1][0]

    def snap_live_hud(self, name):
        e = self.e
        e.h.call("write_mem", addr=self.settings + LT.SET_FRAME_ADVANCE, hex="01")
        e.h.call("frame_advance", n=2)
        e.h.call("resume")
        e.advance(4)
        self.snap(name)
        e.h.call("pause")
        e.h.call("write_mem", addr=self.settings + LT.SET_FRAME_ADVANCE, hex="00")
        e.h.call("frame_advance", n=1)

    def ensure_skill_drills_open(self):
        e = self.e
        e.neutral(0)
        if nav.scene(e)[0] == "scMelee":
            try:
                e.h.call("resume")
            except HarnessError:
                pass
            nav.finish_training(e)
        if nav.scene(e)[0] != "scSelctCharacter":
            nav.boot_to_training_css(e)
        e.wait_for("menu module attached", lambda: "] menu: attached" in e.log_text(), 240, every=4)
        if nav.drill_list_state(e) != "open":
            e.press(0, ["L", "R"], frames=4, after=8)
            e.wait_for("drill list open", lambda: nav.drill_list_state(e) == "open", 120, every=2)
        e.advance(6)
        log = e.log_text()
        tail = log[log.rfind("] menu: attached"):]
        pages = re.findall(r"\] drills: page ([^\r\n]+)", tail)
        already_tp = (pages and pages[-1].strip() == "Tech Practice") or (
            not pages
            and re.search(
                r"\] drills: list opened .*?, row \d+ (?:L-Cancel|Wavedash|Ledgedash|Powershield|RAR|DACUS|B-Reverse|Float)",
                tail,
            )
            is not None
        )
        if not already_tp:
            DT.go_to_page(e, "Tech Practice", "R")
        e.advance(6)

    def arm_skill_drill_and_enter(self, row_idx, drill_id, p1_name, cpu_name="fox", aspect="16:9", freeze_cpu=True):
        e = self.e
        self.ensure_skill_drills_open()
        DT.go_to_row(e, row_idx)
        e.advance(6)
        pos = len(e.log_text())
        e.press(0, ["A"], frames=4, after=8)
        e.wait_for(
            f"drill {drill_id} armed and list closed",
            lambda: nav.drill_list_state(e) == "closed",
            120,
            every=2,
        )
        tail = e.log_text()[pos:]
        self.check(
            f"drills: tech practice {drill_id}" in tail,
            f"arm-drill-{drill_id}-{aspect}",
            f"log tail matched tech practice {drill_id}",
        )
        LT.set_aspect(e, aspect, lambda _m: None)
        e.advance(4)
        match_pos = len(e.log_text())
        e.mark(f"skill-drills-ui: drill {drill_id} ({p1_name} vs {cpu_name}, {aspect})")
        nav.start_training_match(e, p1_name, cpu_name, stage="final_destination", seed_rng=True)
        e.neutral(0)
        lab = LT.Lab(e, [])
        self.settings = lab.find_settings()
        e.h.call("write_mem", addr=self.settings + LT.SET_CPU_PERCENT, hex="000000")
        e.h.call("write_mem", addr=self.settings + LT.SET_FRAME_ADVANCE, hex="00")
        if freeze_cpu:
            e.h.call("write_mem", addr=self.settings + CT.SET_CPU_OPTIONS, hex="00")
            for off, val in DO.CPU_STILL.items():
                e.h.call("write_mem", addr=self.settings + off, hex=f"{int(val) & 0xFF:02x}")
        CT.wait_game_frames(e, 20)
        f = LT.Fighters(e)
        p1 = f.by_player(0)
        cpu = f.by_player(1)
        if p1 is None or cpu is None:
            raise SkillDrillsUiError("fighters not found after match start")
        tmdh = find_tmdh(e, match_pos)
        self.tmpd = find_tmpd(e, match_pos)
        return p1, cpu, tmdh


def sc_boot_menu(t):
    e = t.e
    t.ensure_skill_drills_open()
    e.advance(10)

    # Walk all 8 Tech Practice rows (0..7) and back to 0 (L-Cancel).
    DT.go_to_row(e, 7)
    t.check(DT.last_row(e) == 7, "boot-tech-practice-8-rows", f"reached row {DT.last_row(e)} on Tech Practice")
    DT.go_to_row(e, 0)
    e.advance(10)
    t.snap("boot-skill-drills-16x9")


def sc_ledgedash_hud(t):
    e = t.e
    # ---- 16:9 Ledgedash HUD (Row 2, DrillId 1) + Active Auto-Snap + CPU Standby ----
    p1, _cpu, tmdh = t.arm_skill_drill_and_enter(2, 1, "mario", "fox", aspect="16:9")
    e.h.call("pause")
    e.h.call("frame_advance", n=2)

    d0 = read_tmdh(e, tmdh)
    p0 = read_tmpd(e, t.tmpd)
    t.check(
        d0["magic"] == TMDH_MAGIC and d0["version"] == TMDH_VERSION and d0["activeDrill"] == 1
        and d0["repsDone"] == 0 and d0["modalOpen"] == 0 and d0["drawCalls"] > 0,
        "ledgedash-initial-tmdh",
        f"magic={d0['magic']:#x} drill={d0['activeDrill']} reps={d0['repsDone']} draws={d0['drawCalls']}",
    )
    t.check(
        p0["magic"] == TMPD_MAGIC and p0["activeDrill"] == 1 and p0["cpuInStandby"] == 1
        and p0["ledgeSnapCount"] >= 1,
        "ledgedash-standby-and-autosnap",
        f"standby={p0['cpuInStandby']} snaps={p0['ledgeSnapCount']} side={p0['ledgeActiveSide']}",
    )

    perform_fd_ledgedash(e, p1["fighter"], t.tmpd)
    p1_snap = read_tmpd(e, t.tmpd)
    t.check(
        p1_snap["ledgeActiveSide"] == 1 and p1_snap["ledgeSnapCount"] >= 2,
        "ledgedash-dpad-right-snap",
        f"side={p1_snap['ledgeActiveSide']} snaps={p1_snap['ledgeSnapCount']}",
    )
    d1 = read_tmdh(e, tmdh)
    t.check(
        d1["repsDone"] >= 1 and d1["repsHit"] >= 1 and d1["bestMetric"] > 0 and d1["bestScore"] >= 1,
        "ledgedash-live-rep-16x9",
        f"reps={d1['repsDone']} hit={d1['repsHit']} galint=+{d1['bestMetric']}f best={d1['bestScore']} "
        f"streak={d1['streak']}",
    )
    t.snap_live_hud("hud-ledgedash-16x9")

    # Trigger End-Screen Summary Modal via TMDH forceComplete (16:9).
    e.h.call("resume")
    e.neutral(0)
    trigger_force_complete(e, tmdh)
    e.wait_for("end-screen modal open (16:9)", lambda: read_tmdh(e, tmdh)["modalOpen"] == 1, 120, every=2)
    e.advance(8)
    dm = read_tmdh(e, tmdh)
    t.check(
        dm["modalOpen"] == 1 and dm["repsDone"] == 10 and dm["bestScore"] >= 1,
        "ledgedash-endscreen-modal-16x9",
        f"modal={dm['modalOpen']} reps={dm['repsDone']} hit={dm['repsHit']} newBest={dm['newBest']} "
        f"best={dm['bestScore']}",
    )
    t.snap("endscreen-retry-16x9")

    # Press real GC A -> Retry resets set, closes modal, increments retryCount, preserves bestScore.
    e.press(0, ["A"], frames=4, after=8)
    e.wait_for("retry closes modal", lambda: read_tmdh(e, tmdh)["modalOpen"] == 0, 120, every=2)
    dr = read_tmdh(e, tmdh)
    t.check(
        dr["modalOpen"] == 0 and dr["retryCount"] == 1 and dr["repsDone"] == 0 and dr["repsHit"] == 0
        and dr["bestScore"] >= 1 and dr["activeDrill"] == 1,
        "endscreen-retry-a",
        f"modal={dr['modalOpen']} retries={dr['retryCount']} reps={dr['repsDone']} best={dr['bestScore']}",
    )

    # Set 4:3 BEFORE returning to CSS so menu::attach loads in genuine 4:3 (xs = 1.0)
    # and shows the persisted Best Streak on the Tech Practice Details Card!
    LT.set_aspect(e, "4:3", lambda _m: None)
    css_pos = len(e.log_text())
    trigger_force_complete(e, tmdh)
    e.wait_for("modal reopened for B exit", lambda: read_tmdh(e, tmdh)["modalOpen"] == 1, 120, every=2)
    e.advance(8)
    e.press(0, ["B"], frames=4, after=8)
    e.wait_for("Training CSS after B return", lambda: nav.scene(e)[0] == "scSelctCharacter", 60 * 25, every=5)
    e.wait_for("menu attached after B return", lambda: "] menu: attached" in e.log_text()[css_pos:], 240, every=4)
    e.wait_for("drill list auto-open after drill", lambda: nav.drill_list_state(e) == "open", 180, every=4)
    e.advance(10)
    tail = e.log_text()[css_pos:]
    t.check(
        "drills: list opened (auto)" in tail and "Ledgedash" in tail,
        "endscreen-return-css-autoopen",
        "returned cleanly to CSS in 4:3 and auto-opened Tech Practice on Ledgedash",
    )
    t.snap("boot-skill-drills-4x3")

    # ---- True 4:3 Ledgedash HUD + End-Screen Modal ----
    p1_43, _cpu_43, tmdh_43 = t.arm_skill_drill_and_enter(2, 1, "mario", "fox", aspect="4:3")
    e.h.call("pause")
    e.h.call("frame_advance", n=2)
    d43_0 = read_tmdh(e, tmdh_43)
    t.check(
        d43_0["activeDrill"] == 1 and d43_0["bestScore"] >= 1,
        "ledgedash-best-persisted-across-css",
        f"re-entered Ledgedash with persisted bestScore={d43_0['bestScore']}",
    )
    perform_fd_ledgedash(e, p1_43["fighter"], t.tmpd)
    d43_1 = read_tmdh(e, tmdh_43)
    t.check(
        d43_1["repsDone"] >= 1 and d43_1["repsHit"] >= 1 and d43_1["bestMetric"] > 0,
        "ledgedash-live-rep-4x3",
        f"reps={d43_1['repsDone']} hit={d43_1['repsHit']} galint=+{d43_1['bestMetric']}f",
    )
    t.snap_live_hud("hud-ledgedash-4x3")

    e.h.call("resume")
    e.neutral(0)
    trigger_force_complete(e, tmdh_43)
    e.wait_for("end-screen modal open (4:3)", lambda: read_tmdh(e, tmdh_43)["modalOpen"] == 1, 120, every=2)
    e.advance(8)
    t.snap("endscreen-retry-4x3")

    # Switch aspect back to 16:9 before returning to CSS for Wavedash & L-Cancel.
    LT.set_aspect(e, "16:9", lambda _m: None)
    e.press(0, ["B"], frames=4, after=8)
    e.wait_for("Training CSS after 4:3 B return", lambda: nav.scene(e)[0] == "scSelctCharacter", 60 * 25, every=5)
    e.advance(10)


def sc_wavedash_hud(t):
    e = t.e
    # Row 1 on Tech Practice is Wavedash / Waveland (DrillId 2).
    _p1, _cpu, tmdh = t.arm_skill_drill_and_enter(1, 2, "fox", "mario", aspect="16:9")
    e.h.call("pause")
    e.h.call("frame_advance", n=2)

    d0 = read_tmdh(e, tmdh)
    p0 = read_tmpd(e, t.tmpd)
    t.check(
        d0["activeDrill"] == 2 and d0["repsDone"] == 0 and p0["cpuInStandby"] == 1,
        "wavedash-initial-tmdh",
        f"drill={d0['activeDrill']} reps={d0['repsDone']} standby={p0['cpuInStandby']}",
    )

    inp = PI.PluginInput(e)
    wd_frames = {
        1: (PI.B_JUMP, 0.0, 0.0),
        4: (PI.B_GUARD, 0.96, -0.28),
        5: (PI.B_GUARD, 0.96, -0.28),
    }
    inp.run({0: PI.frames_to_steps(wd_frames, 8)}, extra=20)
    d1 = read_tmdh(e, tmdh)
    t.check(
        d1["repsDone"] >= 1 and d1["repsHit"] >= 1 and d1["bestMetric"] >= 1 and d1["bestScore"] >= 1,
        "wavedash-live-rep",
        f"reps={d1['repsDone']} hit={d1['repsHit']} f1Perfect={d1['bestMetric']} best={d1['bestScore']}",
    )
    t.snap_live_hud("hud-wavedash-16x9")

    e.h.call("resume")
    nav.finish_training(e)


def sc_lcancel_lab(t):
    e = t.e
    # Row 0 on Tech Practice is L-Cancel (DrillId 3), default Stationary CPU target.
    p1, cpu, tmdh = t.arm_skill_drill_and_enter(0, 3, "mario", "fox", aspect="16:9")
    e.h.call("pause")
    e.h.call("frame_advance", n=2)

    p0 = read_tmpd(e, t.tmpd)
    t.check(
        p0["activeDrill"] == 3 and p0["targetModeActive"] == 1 and p0["cpuInStandby"] == 0,
        "lcancel-default-stationary-target",
        f"drill={p0['activeDrill']} target={p0['targetModeActive']} standby={p0['cpuInStandby']}",
    )

    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]
    posture_p1 = S4.read_fighter_full(e, p1_ptr)["posture"]
    posture_cpu = S4.read_fighter_full(e, cpu_ptr)["posture"]

    def step(n=1):
        e.h.call("frame_advance", n=n)

    S4.put_fighter(e, posture_p1, -40.0, 0.0)
    S4.put_fighter(e, posture_cpu, 40.0, 0.0)
    step(4)

    # Short-hop Nair manual L-cancel on whiff (exact sequence from stage4a_core_tests).
    e.hold(0, ["X"])
    step(2)
    e.neutral(0)
    for _ in range(8):
        step(1)
        if S4.read_fighter_full(e, p1_ptr)["air"]:
            break
    e.hold(0, ["A"])
    step(2)
    e.neutral(0)
    pressed = False
    land_frame = None
    prev_y = -100.0
    for i in range(60):
        cur = S4.read_fighter_full(e, p1_ptr)
        falling = cur["y"] < prev_y
        prev_y = cur["y"]
        if not pressed and cur["air"] and falling and cur["y"] < 4.5:
            e.hold(0, ["R"])
            step(2)
            e.neutral(0)
            pressed = True
            continue
        if cur["status"] == S4.ST_LANDING_ATTACK_AIR and land_frame is None:
            land_frame = i
        if land_frame is not None and i - land_frame >= 6:
            break
        step(1)
    e.neutral(0)
    for _ in range(30):
        step(1)
        cur = S4.read_fighter_full(e, p1_ptr)
        if not cur["air"] and cur["status"] == S4.ST_WAIT:
            break
    step(2)

    d1 = read_tmdh(e, tmdh)
    t.check(
        d1["repsDone"] >= 1 and d1["repsHit"] >= 1 and 1 <= d1["bestMetric"] <= 7,
        "lcancel-live-rep",
        f"reps={d1['repsDone']} hit={d1['repsHit']} pressFrame={d1['bestMetric']}/7 best={d1['bestScore']}",
    )
    t.snap_live_hud("hud-lcancel-16x9")

    # Open End-Screen Modal and test Start -> Exit Drill directly into live Training Lab.
    e.h.call("resume")
    e.neutral(0)
    trigger_force_complete(e, tmdh)
    e.wait_for("lcancel modal open", lambda: read_tmdh(e, tmdh)["modalOpen"] == 1, 120, every=2)
    e.advance(8)
    e.press(0, ["START"], frames=4, after=8)
    e.wait_for(
        "Start exits drill into live Lab",
        lambda: read_tmdh(e, tmdh)["modalOpen"] == 0 and read_tmdh(e, tmdh)["activeDrill"] == 0,
        120,
        every=2,
    )
    dl = read_tmdh(e, tmdh)
    t.check(
        dl["modalOpen"] == 0 and dl["activeDrill"] == 0 and nav.scene(e)[0] == "scMelee",
        "endscreen-start-to-lab",
        f"modal={dl['modalOpen']} activeDrill={dl['activeDrill']} scene={nav.scene(e)[0]}",
    )

    # Exit cleanly to CSS and verify match + extra modules unload back to baseline.
    unload_pos = len(e.log_text())
    nav.finish_training(e)
    e.advance(20)
    utail = e.log_text()[unload_pos:]
    t.check(
        "back to baseline: yes" in utail and "back to baseline: NO" not in utail,
        "clean-module-unload",
        "match and extra unloaded cleanly back to baseline",
    )


def sc_powershield_cpu(t):
    e = t.e
    # Row 3 on Tech Practice is Powershield (DrillId 5). Default mode is
    # Projectile Reflection; Fox CPU fires short-hop / grounded Neutral B lasers.
    p1, cpu, tmdh = t.arm_skill_drill_and_enter(3, 5, "mario", "fox", aspect="16:9", freeze_cpu=False)
    e.h.call("pause")
    e.h.call("frame_advance", n=2)

    d0 = read_tmdh(e, tmdh)
    p0 = read_tmpd(e, t.tmpd)
    t.check(
        d0["magic"] == TMDH_MAGIC and d0["activeDrill"] == 5 and d0["modalOpen"] == 0
        and p0["psResolvedMode"] == 0 and p0["cpuInStandby"] == 0,
        "powershield-initial-tmdh",
        f"magic={d0['magic']:#x} drill={d0['activeDrill']} psMode={p0['psResolvedMode']} draws={d0['drawCalls']}",
    )

    cpu_ptr = cpu["fighter"]
    laser_entries = 0
    prev_special_n = False

    # Step 240 game frames: Fox CPU must fire Neutral B lasers (status 0x112
    # or fighter-specific SpecialN sub-states >= 0x112) at least twice.
    for _ in range(240):
        e.h.call("frame_advance", n=1)
        cp = S4.read_fighter_full(e, cpu_ptr)
        in_special_n = cp["status"] >= 0x112
        if in_special_n and not prev_special_n:
            laser_entries += 1
        prev_special_n = in_special_n

    t.check(
        laser_entries >= 2,
        "powershield-cpu-fires-lasers-live",
        f"foxLaserEntries={laser_entries}",
    )

    e.h.call("resume")
    unload_pos = len(e.log_text())
    nav.finish_training(e)
    e.advance(20)
    utail = e.log_text()[unload_pos:]
    t.check(
        "back to baseline: yes" in utail and "back to baseline: NO" not in utail,
        "powershield-clean-unload",
        "match and extra unloaded cleanly back to baseline after P+ Powershield",
    )


DISPATCH = {
    "boot-menu": sc_boot_menu,
    "ledgedash-hud": sc_ledgedash_hud,
    "wavedash-hud": sc_wavedash_hud,
    "lcancel-lab": sc_lcancel_lab,
    "powershield-cpu": sc_powershield_cpu,
}


def cmd_test_skill_drills_ui(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-skill-drills-ui")
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

    label = "test-skill-drills-ui"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, log_path = "", ""
    t = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            t = Runner(emu, keep_going=opts.keep_going)
            try:
                for name in selected:
                    t.step_name = name
                    DISPATCH[name](t)
            except (Timeout, nav.NavError, HarnessError, SkillDrillsUiError, DO.DisplayError, LT.LabError,
                    S4.Stage4aCoreError, struct.error, KeyError, TypeError, IndexError, ValueError) as exc:
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
