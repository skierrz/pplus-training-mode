"""Live Dolphin verification for Break the Targets!, shared Solo CPU Standby,
P1 shield refill in solo Wavedash / L-Cancel, Wavedash floor target zone polish,
and 5x Ice Climbers vs Olimar Fountain of Dreams heap-leak regression.

Run via Python with emu.py environment variables set (DOLPHIN, DOLPHIN_USER,
LAUNCHER_DOL, BRAWL_ISO, SD_FOLDER, SD_IMAGE, SCREENSHOT_DIR):

  py -3.10 -I scripts/stream3_break_targets_tests.py build/TrainingMode.rel
      [--only NAME ...] [--evidence-dir DIR] [--aspect 16:9|4:3|both]
"""
import argparse
import os
import re
import shutil
import struct
import sys
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import display_options_tests as DO  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import skill_drills_ui_tests as SD  # noqa: E402
import stage4a_core_tests as S4  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

TMBR_MAGIC = 0x544D4252  # "TMBR"
TMBR_SIZE = 0x5C
TMBR_CMD_OFF = 0x09

TMSC_MAGIC = 0x544D5343  # "TMSC"
TMSC_RE = re.compile(r"scenarios: debug block at ([0-9a-f]{8})")
RUSH_BLOCK_RE = re.compile(r"combo_break_rush: debug block at ([0-9a-f]{8})")

ST_SHIELD_BREAK_FLY = 0x56
ST_DEAD = 0xBD
ST_REBIRTH = 0xBE
ST_STANDBY = 0x10B

SCENARIOS = [
    "break-targets",
    "wavedash-zone-shield",
    "lcancel-shield",
    "ledgedash-standby",
    "fod-5x-leak",
]


class Stream3Error(RuntimeError):
    pass


def step(emu, n=1):
    emu.h.call("frame_advance", n=n)


def read_tmbr(emu, addr):
    raw = emu.read(addr, TMBR_SIZE)
    hdr = struct.unpack_from(">IIBBBBBBBBIIHHHHII", raw, 0)
    tx = struct.unpack_from(">4f", raw, 0x28)
    ty = struct.unpack_from(">4f", raw, 0x38)
    tr = struct.unpack_from(">4f", raw, 0x48)
    tex_loaded, cpu_hidden, sfx_ok, vfx_ok = struct.unpack_from(">BBBB", raw, 0x58)
    return {
        "magic": hdr[0],
        "version": hdr[1],
        "mode": hdr[2],
        "cmdMode": hdr[3],
        "active": hdr[4],
        "finished": hdr[5],
        "multiplier": hdr[6],
        "activeTargets": hdr[7],
        "stagePlatforms": hdr[8],
        "shapeOk": hdr[9],
        "score": hdr[10],
        "bestScore": hdr[11],
        "streak": hdr[12],
        "breaks": hdr[13],
        "chainFrames": hdr[14],
        "timerFrames": hdr[15],
        "draw3dCalls": hdr[16],
        "draw2dCalls": hdr[17],
        "targetX": tx,
        "targetY": ty,
        "targetR": tr,
        "texLoaded": tex_loaded,
        "cpuHidden": cpu_hidden,
        "lastSfxOk": sfx_ok,
        "lastVfxOk": vfx_ok,
    }


def read_shield_health(emu, fighter_ptr):
    """Reads LA-Float[3] (shield health, 0..60) via soWorkManageModule."""
    acc = emu.ptr(fighter_ptr + 0x60)
    mods = emu.ptr(acc + 0xD8)
    work = emu.ptr(mods + 0x64)
    la = emu.ptr(work + 0x20)
    flts = emu.ptr(la + 0x14)
    return struct.unpack(">f", emu.read(flts + 3 * 4, 4))[0]


def find_block(emu, rx, since_pos, label):
    hits = rx.findall(emu.log_text()[since_pos:]) or rx.findall(emu.log_text())
    if not hits:
        raise Stream3Error(f"{label} debug block address not found in log")
    return int(hits[-1], 16)


def copy_evidence(shot_path, evidence_dir, filename):
    if not evidence_dir or not shot_path:
        return
    os.makedirs(evidence_dir, exist_ok=True)
    shutil.copy2(shot_path, os.path.join(evidence_dir, filename))


class Runner:
    def __init__(self, emu, evidence_dir=None, keep_going=False):
        self.e = emu
        self.evidence_dir = evidence_dir
        self.keep_going = keep_going
        self.results = []
        self.failures = []
        self.shots = []
        self.settings = None
        self.tmbr = None
        self.tmpd = None
        self.tmsc = None
        self.tmdh = None

    def check(self, ok, name, detail):
        if ok:
            self.results.append((name, detail))
            print(f"  PASS {name}: {detail}")
        else:
            self.failures.append(f"{name}: {detail}")
            print(f"  FAIL {name}: {detail}")
            if not self.keep_going:
                raise Stream3Error(f"{name}: {detail}")

    def rush_cmd(self, val):
        self.e.h.call("write_mem", addr=self.tmbr + TMBR_CMD_OFF, hex=f"{int(val) & 0xFF:02x}")

    def activate_rush(self, mode, max_frames=60):
        """Writes TMBR cmdMode and steps until Break the Targets! reaches the desired mode."""
        want_active = 1 if mode != 0 else 0
        self.rush_cmd(mode)
        for _ in range(max_frames):
            step(self.e, 1)
            st = read_tmbr(self.e, self.tmbr)
            if st["active"] == want_active and st["mode"] == mode:
                step(self.e, 4)
                return read_tmbr(self.e, self.tmbr)
        return read_tmbr(self.e, self.tmbr)

    def set_u8(self, offset, val):
        self.e.h.call("write_mem", addr=self.settings + offset, hex=f"{int(val) & 0xFF:02x}")

    def snap_paused(self, name, out_filename=None):
        """Captures a crisp D3D frame while keeping gameplay frozen via Frame Advance."""
        e = self.e
        self.set_u8(LT.SET_FRAME_ADVANCE, 1)
        step(e, 1)
        e.h.call("resume")
        e.advance(3)
        shot_path, sw, sh = e.screenshot(name)
        self.shots.append((shot_path, sw, sh))
        print(f"  SHOT {out_filename or name}: {shot_path} ({sw}x{sh})")
        if out_filename:
            copy_evidence(shot_path, self.evidence_dir, out_filename)
        e.h.call("pause")
        self.set_u8(LT.SET_FRAME_ADVANCE, 0)
        step(e, 1)
        return shot_path

    def back_to_css(self):
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

    def start_quick(self, stage="battlefield", p1=(0x00, 0x00), cpu=(0x07, 0x07), aspect="16:9"):
        e = self.e
        self.back_to_css()
        LT.set_aspect(e, aspect, lambda _m: None)
        pos = len(e.log_text())
        nav.start_quick_match(e, stage=stage, p1=p1, cpu=cpu, seed_rng=True)
        e.neutral(0)
        lab = LT.Lab(e, [])
        self.settings = lab.find_settings()
        # Keep display overlays clean for crisp visual captures.
        self.set_u8(LT.SET_FRAME_ADVANCE, 0)
        self.set_u8(22, 0)  # actionLog = Off
        self.set_u8(52, 0)  # inputViewer = Off
        self.set_u8(55, 0)  # psaInspector = Off
        self.set_u8(19, 0)  # diDisplay = Off
        for off, val in DO.CPU_STILL.items():
            self.set_u8(off, val)
        e.h.call("pause")
        step(e, 18)
        self.tmbr = find_block(e, RUSH_BLOCK_RE, pos, "TMBR")
        self.tmpd = SD.find_tmpd(e, pos)
        self.tmdh = SD.find_tmdh(e, pos)
        f = LT.Fighters(e)
        p1_ref = f.by_player(0)
        cpu_ref = f.by_player(1)
        if p1_ref is None or cpu_ref is None:
            raise Stream3Error("fighters not found after match start")
        return p1_ref, cpu_ref

    def open_and_apply_scenario(self, preset_id=1):
        """Activates a Scenario Preset through the real Lab menu controller path."""
        e = self.e
        e.h.call("resume")
        e.neutral(0)
        lab = LT.Lab(e, [])
        lab.open_menu()
        lab.main_to(LT.MAIN_SCENARIOS)
        lab.tap(["A"])
        for _ in range(max(1, int(preset_id))):
            lab.tap(stick=LT.RIGHT)
        lab.close_menu()
        e.advance(14)
        e.h.call("pause")
        step(e, 4)

    def toggle_wd_target_zone_via_menu(self):
        """Enables Wavedash Target Zone through the real Lab menu controller path."""
        e = self.e
        e.h.call("resume")
        e.neutral(0)
        lab = LT.Lab(e, [])
        lab.open_menu()
        lab.main_to(LT.MAIN_SCENARIOS)
        lab.tap(["A"])
        # In Wavedash drill, row 0 is Practice Mode, row 1 is Target Zone (Off -> On).
        lab.down(1)
        lab.tap(stick=LT.RIGHT)
        lab.close_menu()
        e.advance(10)
        e.h.call("pause")
        step(e, 6)


def run_break_targets(t, aspect):
    e = t.e
    tag = "4x3" if aspect == "4:3" else "16x9"
    p1, cpu = t.start_quick(stage="battlefield", aspect=aspect)
    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]

    # Activate Break the Targets! Free Practice (mode = 3).
    s1 = t.activate_rush(3)
    p1_tmpd = SD.read_tmpd(e, t.tmpd)
    cpu_st = S4.read_fighter_full(e, cpu_ptr)
    t.check(
        s1["magic"] == TMBR_MAGIC
        and s1["active"] == 1
        and s1["mode"] == 3
        and s1["activeTargets"] == 3
        and s1["stagePlatforms"] >= 3
        and s1["texLoaded"] == 1
        and s1["cpuHidden"] == 1
        and p1_tmpd["cpuInStandby"] == 1
        and cpu_st["y"] > 120.0
        and cpu_st["status"] not in (ST_DEAD, ST_REBIRTH, ST_STANDBY),
        f"btt-active-{tag}",
        f"tex={s1['texLoaded']} hidden={s1['cpuHidden']}/{p1_tmpd['cpuInStandby']} "
        f"cpuY={cpu_st['y']:.1f} st={cpu_st['status']:#x} targets={s1['activeTargets']}",
    )

    # Verify ZERO blastzone KO loop over 180 frames while CPU is parked in shared standby.
    ko_seen = False
    for _ in range(180):
        step(e, 1)
        cs = S4.read_fighter_full(e, cpu_ptr)
        if cs["status"] in (ST_DEAD, ST_REBIRTH, ST_STANDBY):
            ko_seen = True
            break
    t.check(not ko_seen, f"btt-no-ko-loop-{tag}", f"180f standby cpuY={cs['y']:.1f} st={cs['status']:#x}")

    # Capture active Break the Targets! screenshot (Brawl CMPR bullseye + Panel B HUD + CPU hidden).
    t.snap_paused(f"stream3-01-btt-active-{tag}", f"01-break-the-targets-active-{tag}.png")

    # Snap target 0 right in front of Mario and hit it with real Jab; break twice for x2 chain!
    e.neutral(0)
    step(e, 4)
    t.rush_cmd(0xFE)
    step(e, 2)
    e.hold(0, ["A"])
    step(e, 3)
    e.neutral(0)
    step(e, 18)

    # Second break in chain window, capture mid-shatter (3 frames after impact)!
    t.rush_cmd(0xFE)
    step(e, 2)
    e.hold(0, ["A"])
    shatter_captured = False
    for _ in range(12):
        step(e, 1)
        cur = read_tmbr(e, t.tmbr)
        if cur["breaks"] >= 2:
            e.neutral(0)
            step(e, 3)
            t.snap_paused(f"stream3-02-btt-shatter-{tag}", f"02-break-the-targets-shatter-{tag}.png")
            shatter_captured = True
            break
    e.neutral(0)
    step(e, 10)
    s2 = read_tmbr(e, t.tmbr)
    t.check(
        shatter_captured
        and s2["breaks"] >= 2
        and s2["score"] >= 450
        and s2["multiplier"] >= 2
        and s2["chainFrames"] > 0
        and s2["lastSfxOk"] == 1
        and s2["lastVfxOk"] == 1,
        f"btt-hit-shatter-{tag}",
        f"breaks={s2['breaks']} score={s2['score']} mult=x{s2['multiplier']} "
        f"sfx={s2['lastSfxOk']} vfx={s2['lastVfxOk']} shot={shatter_captured}",
    )

    if aspect == "16:9":
        # Verify real controller L+Up mode cycle (Free -> Timed 30s -> Timed 60s -> Free).
        modes = [read_tmbr(e, t.tmbr)["mode"]]
        for _ in range(3):
            e.hold(0, ["L", "DUP"])
            step(e, 3)
            e.neutral(0)
            step(e, 6)
            modes.append(read_tmbr(e, t.tmbr)["mode"])
        t.check(
            modes == [3, 1, 2, 3],
            "btt-l-up-mode-cycle",
            f"modes={modes}",
        )

        # Verify repeated mid-match OFF->ON toggle reloads 8 KiB CMPR tex cleanly with 0 B heap drift!
        tbl_before = L.heap_table(e)
        net_used_before = heap_free_used(tbl_before, "Network")[1]
        s_t0 = t.activate_rush(0)
        s_t1 = t.activate_rush(3)
        tbl_after = L.heap_table(e)
        net_used_after = heap_free_used(tbl_after, "Network")[1]
        t.check(
            s_t0["active"] == 0 and s_t0["texLoaded"] == 0
            and s_t1["active"] == 1 and s_t1["texLoaded"] == 1
            and net_used_after == net_used_before,
            "btt-midmatch-toggle-zero-burn",
            f"tex={s_t0['texLoaded']}->{s_t1['texLoaded']} netUsed={net_used_before}->{net_used_after}B",
        )

        # Switch from Break the Targets! to a Scenario Preset and verify clean CPU restore + tex free!
        t.open_and_apply_scenario(1)
        s_off = read_tmbr(e, t.tmbr)
        p_off = SD.read_tmpd(e, t.tmpd)
        cpu_restored = S4.read_fighter_full(e, cpu_ptr)
        t.check(
            s_off["active"] == 0
            and s_off["texLoaded"] == 0
            and s_off["cpuHidden"] == 0
            and p_off["cpuInStandby"] == 0
            and abs(cpu_restored["y"]) < 60.0,
            "btt-restore-on-scenario",
            f"active={s_off['active']} tex={s_off['texLoaded']} standby={p_off['cpuInStandby']} "
            f"cpu=({cpu_restored['x']:.1f},{cpu_restored['y']:.1f})",
        )
        t.snap_paused("stream3-03-btt-restore-scenario-16x9", "03-break-the-targets-restore-scenario-16x9.png")


def run_wavedash_zone_and_shield(t, aspect):
    e = t.e
    tag = "4x3" if aspect == "4:3" else "16x9"
    sd = SD.Runner(e)
    sd.ensure_page_open("Tech Practice")
    # Row 1 = Wavedash (DrillId 2), Fox vs Mario on Battlefield (or FD via quick arm).
    p1, cpu, _tmdh = sd.arm_skill_drill_and_enter(1, 2, "fox", "mario", aspect=aspect)
    t.settings = sd.settings
    t.tmpd = sd.tmpd
    e.h.call("pause")
    step(e, 6)

    # Enable Wavedash Target Zone via the real Lab Menu (Scenarios/Practice page row 1).
    t.toggle_wd_target_zone_via_menu()
    p0 = SD.read_tmpd(e, t.tmpd)

    p1_ptr = p1["fighter"]
    cpu_ptr = cpu["fighter"]
    cpu_st = S4.read_fighter_full(e, cpu_ptr)
    t.check(
        p0["activeDrill"] == 2 and p0["cpuInStandby"] == 1 and cpu_st["y"] > 120.0
        and p0["wdZoneActive"] == 1 and p0["drawCalls"] > 0,
        f"wd-standby-zone-{tag}",
        f"drill={p0['activeDrill']} standby={p0['cpuInStandby']} cpuY={cpu_st['y']:.1f} "
        f"wdZone={p0['wdZoneActive']} draws={p0['drawCalls']}",
    )

    t.snap_paused(f"stream3-04-wd-zone-{tag}", f"04-wavedash-floor-zone-hidden-cpu-{tag}.png")

    if aspect == "16:9":
        # Disable global P1 Infinite Shield (offset 20 -> 0 = Off) so ONLY the Wavedash
        # drill refill in drill_practice_driver.cpp is keeping P1 shield full!
        t.set_u8(20, 0)
        # Hold L continuously for 240 frames (shield normally breaks around ~170f!).
        broke = False
        min_sh = 999.0
        e.hold(0, ["L"])
        for _ in range(240):
            step(e, 1)
            sh = read_shield_health(e, p1_ptr)
            if sh < min_sh:
                min_sh = sh
            if S4.read_fighter_full(e, p1_ptr)["status"] == ST_SHIELD_BREAK_FLY:
                broke = True
                break
        e.neutral(0)
        step(e, 6)
        t.check(
            not broke and min_sh >= 58.0,
            "wd-p1-shield-full-240f",
            f"broke={broke} minShield={min_sh:.2f}",
        )


def run_lcancel_shield(t):
    e = t.e
    sd = SD.Runner(e)
    sd.ensure_page_open("Tech Practice")
    # Row 0 = L-Cancel (DrillId 3).
    p1, _cpu, tmdh = sd.arm_skill_drill_and_enter(0, 3, "mario", "fox", aspect="16:9")
    t.settings = sd.settings
    t.tmpd = sd.tmpd
    e.h.call("pause")
    step(e, 6)

    # Disable global P1 Infinite Shield (offset 20 -> 0 = Off) so ONLY the L-Cancel
    # drill refill keeps P1 shield full, and exiting the drill proves normal shield drains.
    t.set_u8(20, 0)
    p1_ptr = p1["fighter"]
    broke = False
    min_sh = 999.0
    e.hold(0, ["L"])
    for _ in range(220):
        step(e, 1)
        sh = read_shield_health(e, p1_ptr)
        if sh < min_sh:
            min_sh = sh
        if S4.read_fighter_full(e, p1_ptr)["status"] == ST_SHIELD_BREAK_FLY:
            broke = True
            break
    e.neutral(0)
    step(e, 6)
    t.check(
        not broke and min_sh >= 58.0,
        "lcancel-p1-shield-full-220f",
        f"broke={broke} minShield={min_sh:.2f}",
    )

    # Exit L-Cancel drill via End-Screen Start (Exit Drill -> live Free Lab), then
    # verify normal Lab shield DOES drain when holding L!
    e.h.call("resume")
    e.neutral(0)
    SD.trigger_force_complete(e, tmdh)
    e.wait_for("lcancel modal open", lambda: SD.read_tmdh(e, tmdh)["modalOpen"] == 1, 120, every=2)
    e.advance(6)
    e.press(0, ["START"], frames=4, after=8)
    e.wait_for("exit drill to live lab", lambda: SD.read_tmdh(e, tmdh)["activeDrill"] == 0, 120, every=2)
    e.h.call("pause")
    step(e, 6)
    p0_after = SD.read_tmpd(e, t.tmpd)
    sh_before = read_shield_health(e, p1_ptr)
    e.hold(0, ["L"])
    step(e, 70)
    sh_after = read_shield_health(e, p1_ptr)
    e.neutral(0)
    step(e, 4)
    t.check(
        p0_after["activeDrill"] == 0 and sh_before >= 58.0 and sh_after < sh_before - 10.0,
        "normal-lab-shield-drains",
        f"drill={p0_after['activeDrill']} before={sh_before:.2f} after70f={sh_after:.2f}",
    )


def run_ledgedash_standby(t):
    e = t.e
    sd = SD.Runner(e)
    sd.ensure_page_open("Tech Practice")
    # Row 2 = Ledgedash (DrillId 1).
    _p1, cpu, _tmdh = sd.arm_skill_drill_and_enter(2, 1, "fox", "mario", aspect="16:9")
    t.settings = sd.settings
    t.tmpd = sd.tmpd
    e.h.call("pause")
    step(e, 24)
    p0 = SD.read_tmpd(e, t.tmpd)
    cpu_st = S4.read_fighter_full(e, cpu["fighter"])
    t.check(
        p0["activeDrill"] == 1 and p0["cpuInStandby"] == 1 and cpu_st["y"] > 120.0,
        "ledgedash-standby-active",
        f"drill={p0['activeDrill']} standby={p0['cpuInStandby']} cpuY={cpu_st['y']:.1f}",
    )
    t.snap_paused("stream3-05-ledgedash-hidden-cpu-16x9", "05-ledgedash-hidden-cpu-16x9.png")


def heap_free_used(tbl, name):
    rec = tbl.get(name)
    if not rec:
        return (-1, -1, -1)
    start, end, blocks, used = rec
    return ((end - start) - used, used, blocks)


def run_fod_5x_leak(t):
    """5 consecutive Ice Climbers (0x10, 0x10) vs Olimar (0x1A, 0x18) matches
    on Fountain of Dreams (0x1F) with Break the Targets! activated, broken,
    deactivated, and detached -> +0 B leak."""
    e = t.e
    t.back_to_css()
    e.advance(20)
    net_used_css = []
    inmatch_net_free = []

    for cycle in range(1, 6):
        t.start_quick(stage=0x1F, p1=(0x10, 0x10), cpu=(0x1A, 0x18), aspect="16:9")
        # Activate Break the Targets! (loads 8 KiB 128x128 CMPR texture + parks CPU Olimar).
        s1 = t.activate_rush(3)
        # Measure Heaps::Network while Break the Targets! is active on FoD with ICs vs Olimar.
        tbl_on = L.heap_table(e)
        net_free, net_used, net_blk = heap_free_used(tbl_on, "Network")
        stg_free, stg_used, _ = heap_free_used(tbl_on, "StageResource")
        inmatch_net_free.append(net_free)
        # Wait for Popo to be settled on the stage, then snap target 0 right in front and Jab!
        e.neutral(0)
        step(e, 24)
        s2 = read_tmbr(e, t.tmbr)
        for _attempt in range(3):
            if s2["breaks"] >= 1:
                break
            t.rush_cmd(0xFE)
            step(e, 2)
            e.hold(0, ["A"])
            step(e, 4)
            e.neutral(0)
            step(e, 18)
            s2 = read_tmbr(e, t.tmbr)
        # Deactivate Break the Targets! (frees 8 KiB CMPR texture + restores Olimar).
        s3 = t.activate_rush(0)
        t.check(
            s1["texLoaded"] == 1 and s1["cpuHidden"] == 1 and s2["breaks"] >= 1
            and s3["active"] == 0 and s3["texLoaded"] == 0 and s3["cpuHidden"] == 0,
            f"fod-cycle-{cycle}-btt",
            f"tex={s1['texLoaded']}->{s3['texLoaded']} breaks={s2['breaks']} "
            f"netFreeOn={net_free}B netUsedOn={net_used}B({net_blk}blk) stgFreeOn={stg_free}B",
        )
        # Exit cleanly back to CSS and sample Heaps::Network at CSS!
        unload_pos = len(e.log_text())
        t.back_to_css()
        e.advance(20)
        utail = e.log_text()[unload_pos:]
        tbl_css = L.heap_table(e)
        c_free, c_used, c_blk = heap_free_used(tbl_css, "Network")
        net_used_css.append(c_used)
        t.check(
            "back to baseline: yes" in utail and "back to baseline: NO" not in utail,
            f"fod-cycle-{cycle}-unload",
            f"cssNetUsed={c_used}B cssNetFree={c_free}B blk={c_blk}",
        )

    # Cycles 2..5 must match cycle 1 exactly (+0 B leak across 5x ICs vs Olimar on FoD).
    delta = net_used_css[-1] - net_used_css[0]
    t.check(
        len(net_used_css) == 5 and all(v == net_used_css[0] for v in net_used_css),
        "fod-5x-zero-byte-leak",
        f"cssNetUsed={net_used_css} delta={delta:+d} B inMatchNetFree={inmatch_net_free}",
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rel", help="path to packed TrainingMode.rel")
    ap.add_argument("--only", nargs="*", choices=SCENARIOS, default=None)
    ap.add_argument("--evidence-dir", default=None)
    ap.add_argument("--aspect", choices=["16:9", "4:3", "both"], default="both")
    ap.add_argument("--keep-going", action="store_true")
    args = ap.parse_args()

    wanted = args.only if args.only else list(SCENARIOS)
    aspects = ["16:9", "4:3"] if args.aspect == "both" else [args.aspect]

    E.cmd_install(args.rel)
    extra_args = list(E.DETERMINISTIC_ARGS)
    gfx_ini = os.path.join(E.env("DOLPHIN_USER"), "Config", "GFX.ini")
    old_gfx = open(gfx_ini, "r", encoding="utf-8").read() if os.path.exists(gfx_ini) else None
    if args.aspect == "4:3":
        E.update_ini(gfx_ini, {"Settings": {"AspectRatio": "2"}})
    try:
        with E.harness_run("D3D", extra_args, allow_null=True) as (_proc, emu):
            t = Runner(emu, evidence_dir=args.evidence_dir, keep_going=args.keep_going)
            try:
                if "break-targets" in wanted:
                    for asp in aspects:
                        run_break_targets(t, asp)
                if "wavedash-zone-shield" in wanted:
                    for asp in aspects:
                        run_wavedash_zone_and_shield(t, asp)
                if "lcancel-shield" in wanted:
                    run_lcancel_shield(t)
                if "ledgedash-standby" in wanted:
                    run_ledgedash_standby(t)
                if "fod-5x-leak" in wanted:
                    run_fod_5x_leak(t)
            except Exception as exc:
                print(f"ERROR: {exc}")
                t.failures.append(str(exc))
            finally:
                try:
                    emu.h.call("resume")
                    emu.advance(2)
                except (OSError, HarnessError, ValueError, Timeout):
                    pass

            probs = L.problems(emu.log_text())
            if probs:
                t.failures.append(f"log problems: {probs[:5]}")

            print(f"stream3 summary: {len(t.results)} passed, {len(t.failures)} failed, {len(t.shots)} screenshots")
            for f in t.failures:
                print(f"  FAILURE: {f}")
            rc = 1 if t.failures else 0
    finally:
        if args.aspect == "4:3" and old_gfx is not None:
            with open(gfx_ini, "w", encoding="utf-8") as f:
                f.write(old_gfx)
    sys.exit(rc)


if __name__ == "__main__":
    main()
