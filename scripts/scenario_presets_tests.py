"""Emulator tests for Unified P+ Lab Scenario Presets (src/extra/scenario_presets.*).

  test-scenarios REL [--only NAME ...] [--list]

Runs across Final Destination and Battlefield (Mario vs Fox, plus Mario vs Ness
for P+ Multi-Hit SDI PK Fire):
  - Off default (no warps / overrides) + Off -> Preset -> Preset -> Off exact
    byte-for-byte restoration of cpu.options and comboTrainer, plus opening
    General > Scenarios (row 12) via real controller input.
  - All 10 presets: Combo Lab, Techchase, Edgeguard, Shield Pressure,
    OoS / Reversal, Shield Drop (Battlefield live platform + Guard->Pass
    detection + FD platless fallback), Ledgetech (1-frame noclip Correct_None
    -> Correct_Air + auto-reset), Slide-Off (FD lip + Battlefield platform),
    Grab Escape (live capture frame count + escape detection), and
    P+ Multi-Hit SDI (live Ness PK Fire hitlag SDI displacement + escape).
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import cpu_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import plugin_input as PI  # noqa: E402
import recovery_combo_tests as RC  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

CHARS = {
    "mario": (0x00, 0x00),
    "fox": (0x07, 0x07),
    "pikachu": (0x08, 0x08),
    "ness": (0x0B, 0x0B),
    "wario": (0x17, 0x15),
    "meta_knight": (0x18, 0x16),
    "ivysaur": (0x22, 0x2C),
    "jigglypuff": (0x27, 0x23),
    "snake": (0x2A, 0x26),
}

# ScenarioDebugBlock layout (src/extra/scenario_presets.cpp, 0x50 bytes).
SCENARIO_MAGIC = 0x544D5343  # "TMSC"
SCENARIO_LOG = re.compile(r"scenarios: debug block ([0-9a-f]{8})")
SCENARIO_FMT = ">9I6f20B"
SCENARIO_SIZE = 0x50

# Settings offsets (src/main/settings.h).
SET_PLAYER_INF_SHIELD = 20
SET_CPU_OPTIONS = 123
CPU_OPTIONS_SIZE = 61
SET_COMBO = 388
COMBO_SIZE = 16

# ScenarioId (src/extra/scenario_presets_core.h).
SCEN_OFF = 0
SCEN_COMBO_LAB = 1
SCEN_TECHCHASE = 2
SCEN_EDGEGUARD = 3
SCEN_SHIELD_PRESSURE = 4
SCEN_SHIELD_DROP = 5
SCEN_LEDGETECH = 6
SCEN_SLIDE_OFF = 7
SCEN_GRAB_ESCAPE = 8
SCEN_MULTI_HIT_SDI = 9

SIDE_RIGHT, SIDE_LEFT, SIDE_RANDOM = 0, 1, 2
MH_AUTO, MH_NAIR, MH_FAIR, MH_UP_AIR, MH_DOWN_AIR = 0, 1, 2, 3, 4
MH_RAPID_JAB, MH_UP_B, MH_SIDE_B, MH_DOWN_B = 5, 6, 7, 8
OOS_GRAB, OOS_UP_B, OOS_SPOTDODGE, OOS_ROLL_AWAY, OOS_SH_NAIR, OOS_WAVEDASH, OOS_RANDOM = 0, 1, 2, 3, 4, 5, 6
LT_SPAWN_CLOSE_LOW, LT_SPAWN_DEEP_LOW, LT_SPAWN_FAR_OUT, LT_SPAWN_HIGH, LT_SPAWN_RANDOM = 0, 1, 2, 3, 4
LT_DJ_AVAIL, LT_DJ_SPENT = 0, 1
LT_ATK_DTILT, LT_ATK_FTILT, LT_ATK_FSMASH, LT_ATK_DSMASH, LT_ATK_DAIR, LT_ATK_NEUTRAL_B, LT_ATK_SIDE_B, LT_ATK_RANDOM = (
    0, 1, 2, 3, 4, 5, 6, 7
)
SD_PLAT_LEFT, SD_PLAT_RIGHT, SD_PLAT_TOP, SD_PLAT_RANDOM = 0, 1, 2, 3
SD_SPACE_RANDOM, SD_SPACE_CLOSE, SD_SPACE_UNDER, SD_SPACE_FAR = 0, 1, 2, 3
SD_FACE_TOWARD, SD_FACE_AWAY, SD_FACE_RANDOM = 0, 1, 2

CORRECT_NONE, CORRECT_GROUND_CLIFF_STOP, CORRECT_AIR = 0, 2, 5
STATUS_PASS = 0x72
STATUS_GUARD_DAMAGE = 0x1D
STATUS_CATCH = 0x34
STATUS_CAPTURE_PULLED = 0x3D
STATUS_CAPTURE_WAIT = 0x3E
STATUS_CAPTURE_CUT = 0x40
STATUS_CAPTURE_JUMP = 0x41
STATUS_CLIFF_CATCH = 0x74
STATUS_CLIFF_WAIT = 0x75


class TestError(RuntimeError):
    pass


class ScenarioBlock:
    """Reads extra::ScenarioDebugBlock and writes ScenarioMenuState in menuScratch()."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None
        self.scratch_addr = None

    def find(self):
        found = SCENARIO_LOG.findall(self.emu.log_text())
        if not found:
            raise TestError("no scenarios debug block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != SCENARIO_MAGIC:
            raise TestError(f"bad scenario debug magic at {self.addr:#x}")
        self.scratch_addr = self.emu.u32(self.addr + 8)
        if not self.emu.is_ram(self.scratch_addr):
            raise TestError(f"bad scenario scratch addr {self.scratch_addr:#x}")

    def read(self):
        raw = self.emu.read(self.addr, SCENARIO_SIZE)
        v = struct.unpack(SCENARIO_FMT, raw)
        return {
            "magic": v[0],
            "frame": v[1],
            "scratchAddr": v[2],
            "resetCount": v[3],
            "completionCount": v[4],
            "sdiHitCount": v[5],
            "sdiHitlagFrames": v[6],
            "grabEscapeFrames": v[7],
            "shieldHitCount": v[8],
            "hmnSpawnX": v[9],
            "hmnSpawnY": v[10],
            "cpuSpawnX": v[11],
            "cpuSpawnY": v[12],
            "sdiDispX": v[13],
            "sdiDispY": v[14],
            "activePreset": v[15],
            "hasPlatform": v[16],
            "snapshotActive": v[17],
            "sdiEscaped": v[18],
            "lastCorrectWritten": (v[19], v[20]),
            "observedCorrect": (v[21], v[22]),
            "restorePending": (v[23], v[24]),
            "sawShieldDrop": v[25],
            "sawWallTech": v[26],
            "sawSlideOff": v[27],
            "sawGrabEscape": v[28],
            "autoReset": v[29],
            "side": v[30],
            "multiHitMove": v[31],
            "resolvedMultiHit": v[32],
            "resolvedPlatformSlot": v[33],
            "resolvedLedgetechAtk": v[34],
        }

    def set_menu(self, preset, auto_reset=1, side=SIDE_RIGHT, move=MH_AUTO,
                 oos_action=OOS_GRAB, lt_spawn=LT_SPAWN_CLOSE_LOW, lt_dj=LT_DJ_AVAIL,
                 lt_atk=LT_ATK_DTILT, sd_plat=SD_PLAT_LEFT, sd_space=SD_SPACE_RANDOM,
                 sd_face=SD_FACE_TOWARD):
        payload = bytes([
            preset, auto_reset, side, move,
            oos_action, lt_spawn, lt_dj, lt_atk,
            sd_plat, sd_space, sd_face,
        ])
        self.emu.h.call("write_mem", addr=self.scratch_addr, hex=payload.hex())


def read_cpu_opts(emu, base):
    return emu.read(base + SET_CPU_OPTIONS, CPU_OPTIONS_SIZE)


def read_combo_cfg(emu, base):
    return emu.read(base + SET_COMBO, COMBO_SIZE)


def set_player_percent(emu, base, percent, lock=True):
    payload = struct.pack(">HB", int(percent), 1 if lock else 0)
    emu.h.call("write_mem", addr=base + LT.SET_PLAYER_PERCENT, hex=payload.hex())


def read_player_percent(emu, base):
    return struct.unpack(">HB", emu.read(base + LT.SET_PLAYER_PERCENT, 3))


def wait_settled_grounded(r, max_frames=120):
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    for _ in range(max_frames):
        hf = r.g.fighter(h_en)
        cf = r.g.fighter(c_en)
        if not hf["air"] and not cf["air"] and hf["status"] not in CT.GONE and cf["status"] not in CT.GONE:
            return hf, cf
        r.emu.advance(1)
    return r.g.fighter(h_en), r.g.fighter(c_en)


def scenario_off_snapshot_restore(r, sb):
    """Verify Off default, General > Scenarios menu entry (row 12), and exact snapshot restore."""
    res = CT.Result("off-snapshot-restore")
    sb.set_menu(SCEN_OFF, auto_reset=1, side=SIDE_RIGHT, move=MH_UP_AIR)
    r.emu.advance(6)
    d0 = sb.read()
    res.row("Off default activePreset=0 snapshot=0", "preset=0 snap=0",
            f"preset={d0['activePreset']} snap={d0['snapshotActive']}",
            d0["activePreset"] == SCEN_OFF and d0["snapshotActive"] == 0)

    # Verify closing the Lab menu while a preset is active does NOT treat the
    # preset's temporary cpu.options / comboTrainer overrides as persisted changes
    # (no save attempt / no write-protect save error on the locked card).
    log_before_menu = r.emu.log_text()
    r.emu.press(0, ["START"], frames=2, after=6)
    r.emu.press(0, ["B"], frames=2, after=4)               # Focus Draft C rail (0 = Lab)
    r.emu.press(0, [], stick=LT.DOWN, frames=2, after=4)   # Rail 1 = Scenarios
    r.emu.press(0, ["A"], frames=2, after=6)               # Scenarios page (row 0 = Preset)
    r.emu.press(0, [], stick=LT.RIGHT, frames=2, after=6)  # Combo Lab
    d_menu_combo = sb.read()
    r.emu.press(0, ["START"], frames=2, after=10)  # Close menu while Combo Lab is active
    d_closed_combo = sb.read()
    combo_fix_live = read_combo_cfg(r.emu, r.opt.base)[2]
    menu_close_log = r.emu.log_text()[len(log_before_menu):]
    leaked_save = ("storage: settings saved" in menu_close_log
                   or "Lab settings not saved" in menu_close_log
                   or "settings save to" in menu_close_log)
    res.row("Menu close with active preset does not save", "active & no save",
            f"preset={d_closed_combo['activePreset']} fix={combo_fix_live} leaked={leaked_save}",
            d_closed_combo["activePreset"] == SCEN_COMBO_LAB and combo_fix_live == 1 and not leaked_save)

    # Re-open menu (Lab menu remembers the Scenarios page and row 0 cursor),
    # switch to Techchase, then back to Off and close.
    r.emu.press(0, ["START"], frames=2, after=6)
    r.emu.press(0, [], stick=LT.RIGHT, frames=2, after=6)  # Techchase
    d_menu_tech = sb.read()
    r.emu.press(0, [], stick=LT.LEFT, frames=2, after=4)
    r.emu.press(0, [], stick=LT.LEFT, frames=2, after=6)   # Back to Off
    r.emu.press(0, ["START"], frames=2, after=6)           # Close menu

    res.row("Menu row 12 entered Scenarios & switched", "combo=1 tech=2",
            f"combo={d_menu_combo['activePreset']} tech={d_menu_tech['activePreset']}",
            d_menu_combo["activePreset"] == SCEN_COMBO_LAB and d_menu_tech["activePreset"] == SCEN_TECHCHASE)

    # Set distinctive non-default cpu.options & comboTrainer values before activating presets.
    r.opt.set(control=CT.LAB, behavior=CT.BEHAVIOR["Crouch"], tdi=CT.TDI["Outwards"],
              sdiAmount=2, counterGround=9, counterAir=11, counterShield=4,
              tech=CT.TECH["In Place"], getup=CT.GETUP["Attack"], grabEscape=1)
    RC.write_combo_settings(r.emu, r.opt.base, enabled=0, noclip=0, fix_pos=0, rand_pos=1,
                            rand_ground=1, rand_dmg=1, recovery_trainer=0, timer_adj=7,
                            fixed_dmg=55, enabled_meters=RC.METER_HITSTUN | RC.METER_DI)
    r.emu.advance(4)
    orig_cpu = read_cpu_opts(r.emu, r.opt.base)
    orig_combo = read_combo_cfg(r.emu, r.opt.base)
    resets_before = sb.read()["resetCount"]

    # Also test live transitions Off -> Combo Lab -> Edgeguard -> Shield Pressure -> Off.
    sb.set_menu(SCEN_COMBO_LAB)
    r.emu.advance(3)
    sb.set_menu(SCEN_EDGEGUARD)
    r.emu.advance(3)
    sb.set_menu(SCEN_SHIELD_PRESSURE)
    r.emu.advance(3)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)

    d_after = sb.read()
    after_cpu = read_cpu_opts(r.emu, r.opt.base)
    after_combo = read_combo_cfg(r.emu, r.opt.base)
    res.row("Off restores cpu.options byte-for-byte", "exact match",
            "match" if after_cpu == orig_cpu else "DIFF",
            after_cpu == orig_cpu)
    res.row("Off restores comboTrainer byte-for-byte", "exact match",
            "match" if after_combo == orig_combo else "DIFF",
            after_combo == orig_combo)
    res.row("snapshotActive cleared on Off", "0", str(d_after["snapshotActive"]),
            d_after["snapshotActive"] == 0 and d_after["resetCount"] > resets_before)
    return res


def scenario_combo_lab(r, sb, cb):
    """Verify Combo Lab settings, center spawn + 1-frame noclip (0->2), and hitstun auto-reset."""
    res = CT.Result("combo-lab")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    r.opt.cpu_percent(0, False)

    sb.set_menu(SCEN_COMBO_LAB, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(1)
    d_warp = sb.read()
    r.emu.advance(1)
    d_next = sb.read()

    res.row("Combo Lab sets comboTrainer & CPU options", "en=1 fix=1 nc=1 rec=1",
            f"en={read_combo_cfg(r.emu, r.opt.base)[0]} fix={read_combo_cfg(r.emu, r.opt.base)[2]} "
            f"nc={read_combo_cfg(r.emu, r.opt.base)[1]} rec={read_cpu_opts(r.emu, r.opt.base)[39]}",
            read_combo_cfg(r.emu, r.opt.base)[0] == 1 and read_combo_cfg(r.emu, r.opt.base)[2] == 1
            and read_combo_cfg(r.emu, r.opt.base)[1] == 1 and read_cpu_opts(r.emu, r.opt.base)[39] == 1)
    res.row("grounded 1-frame noclip 0->2 on warp", "warp=(0,0) next=(2,2)",
            f"warp={d_warp['lastCorrectWritten']} next={d_next['lastCorrectWritten']} obs={d_next['observedCorrect']}",
            d_warp["lastCorrectWritten"] == (CORRECT_NONE, CORRECT_NONE)
            and d_next["lastCorrectWritten"] == (CORRECT_GROUND_CLIFF_STOP, CORRECT_GROUND_CLIFF_STOP)
            and d_next["observedCorrect"] == (CORRECT_GROUND_CLIFF_STOP, CORRECT_GROUND_CLIFF_STOP))

    r.emu.advance(8)
    base_combo_resets = cb.slot(CT.CPU)["resetCount"]
    hit_ok = RC.move_p1_to_cpu_and_hit(r, CT.FSMASH)
    combo_reset_fired = False
    for _ in range(150):
        r.emu.advance(1)
        if cb.slot(CT.CPU)["resetCount"] > base_combo_resets:
            combo_reset_fired = True
            break
    res.row("Combo Lab hit triggers Combo Trainer reset", "hit & reset",
            f"hit={hit_ok} reset={combo_reset_fired}", hit_ok and combo_reset_fired)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_techchase(r, sb):
    """Verify Techchase preset knockdown/tech completion detection and auto-reset."""
    res = CT.Result("techchase")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_TECHCHASE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(6)
    r.opt.cpu_percent(85, True)
    r.emu.advance(2)
    r.opt.cpu_percent(85, False)
    d0 = sb.read()

    # Hit CPU with fsmash at 85% to force knockdown/tech.
    hit_ok = RC.move_p1_to_cpu_and_hit(r, CT.FSMASH)
    auto_reset_ok = False
    for _ in range(260):
        r.emu.advance(1)
        d = sb.read()
        if d["resetCount"] > d0["resetCount"]:
            auto_reset_ok = True
            break
    d1 = sb.read()
    res.row("Techchase hit & auto-reset after tech/getup", "hit & resetCount+1",
            f"hit={hit_ok} resets={d0['resetCount']}->{d1['resetCount']} comp={d1['completionCount']}",
            hit_ok and auto_reset_ok)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_edgeguard(r, sb, cb):
    """Verify Edgeguard HMN ledge placement + CPU offstage PPlusAI recovery loop."""
    res = CT.Result("edgeguard")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_EDGEGUARD, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(4)

    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    hf = r.g.fighter(h_en)
    cf = r.g.fighter(c_en)
    res.row("HMN placed near right ledge lip", "x in (60,85.5), !air",
            f"x={hf['x']:.1f} y={hf['y']:.1f} air={hf['air']}",
            60.0 < hf["x"] < 85.5 and not hf["air"])
    res.row("CPU spawned offstage right", "x>88 & air",
            f"x={cf['x']:.1f} y={cf['y']:.1f} air={cf['air']}",
            cf["x"] > 88.0 and cf["air"])

    saw_rec_loop = False
    saw_special = False
    for _ in range(360):
        r.emu.advance(1)
        cf = r.g.fighter(c_en)
        if cf["status"] >= 0x112 or cf["status"] == CT.STATUS["Jump_Aerial"]:
            saw_special = True
        if cb.slot(CT.CPU)["recoveryResets"] >= 1:
            saw_rec_loop = True
            break
    res.row("Edgeguard CPU recovers & loops offstage", "special or recResets>=1",
            f"special={saw_special} recLoop={saw_rec_loop} resets={cb.slot(CT.CPU)['recoveryResets']}",
            saw_special or saw_rec_loop)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_shield_pressure(r, sb):
    """Verify Shield Pressure close spacing, CPU infinite shield, and OoS Grab counter."""
    res = CT.Result("shield-pressure")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_SHIELD_PRESSURE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(12)

    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    hf = r.g.fighter(h_en)
    cf = r.g.fighter(c_en)
    spacing = abs(cf["x"] - hf["x"])
    res.row("Shield Pressure spacing ~12 & CPU shields", "dx~12 & guard",
            f"dx={spacing:.1f} cpuSt={cf['status']:#x}",
            9.0 <= spacing <= 16.0 and cf["status"] in (0x1A, 0x1B))

    # P1 hits CPU shield with jab/fsmash and watches shieldHitCount + Grab counter.
    r.emu.press(0, ["A"], stick=(128, 128), frames=3, after=0)
    saw_hit = False
    saw_grab = False
    for _ in range(60):
        r.emu.advance(1)
        d = sb.read()
        cf = r.g.fighter(c_en)
        if d["shieldHitCount"] >= 1 or cf["status"] == STATUS_GUARD_DAMAGE:
            saw_hit = True
        if cf["status"] in (STATUS_CATCH, 0x35, 0x36, 0x39):
            saw_grab = True
            break
    res.row("CPU took shield hit & countered with Grab", "hit & grab",
            f"hit={saw_hit} grab={saw_grab} shieldHits={sb.read()['shieldHitCount']}",
            saw_hit and saw_grab)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_oos_reversal(r, sb):
    """Verify unified Shield Pressure / OoS with CPU OoS = Up B counters & auto-resets."""
    res = CT.Result("oos-reversal")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_SHIELD_PRESSURE, auto_reset=1, side=SIDE_RIGHT, oos_action=OOS_UP_B)
    r.emu.advance(12)
    d0 = sb.read()

    c_en = r.g.entry(CT.CPU)
    r.emu.press(0, ["A"], stick=(255, 128), frames=3, after=0)
    saw_upb = False
    saw_reset = False
    for _ in range(340):
        r.emu.advance(1)
        cf = r.g.fighter(c_en)
        if cf["status"] >= 0x112:
            saw_upb = True
        if sb.read()["resetCount"] > d0["resetCount"]:
            saw_reset = True
            break
    res.row("CPU countered OoS with UpB & auto-reset", "upb & reset",
            f"upb={saw_upb} reset={saw_reset}", saw_upb and saw_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_shield_drop_fd(r, sb):
    """Verify Shield Drop on platless FD sets hasPlatform=0 and falls back safely to center."""
    res = CT.Result("shield-drop-fd-fallback")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_SHIELD_DROP, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(6)

    d = sb.read()
    hf = r.g.fighter(r.g.entry(CT.P1))
    cf = r.g.fighter(r.g.entry(CT.CPU))
    res.row("FD platless fallback hasPlatform=0", "hasPlat=0 on floor",
            f"hasPlat={d['hasPlatform']} hmn=({hf['x']:.1f},{hf['y']:.1f}) cpu=({cf['x']:.1f},{cf['y']:.1f})",
            d["hasPlatform"] == 0 and abs(hf["y"]) < 2.0 and abs(cf["y"]) < 2.0 and abs(hf["x"]) < 30.0)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_ledgetech(r, sb):
    """Verify Ledgetech airborne wall placement, 1-frame Correct_Air restore, and auto-reset."""
    res = CT.Result("ledgetech")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    sb.set_menu(SCEN_LEDGETECH, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(1)
    d_warp = sb.read()
    r.emu.advance(1)
    d_next = sb.read()

    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    hf = r.g.fighter(h_en)
    cf = r.g.fighter(c_en)
    res.row("Ledgetech 1-frame noclip (HMN 0->5, CPU 0->2)", "warp=(0,0) next=(5,2)",
            f"warp={d_warp['lastCorrectWritten']} next={d_next['lastCorrectWritten']}",
            d_warp["lastCorrectWritten"] == (CORRECT_NONE, CORRECT_NONE)
            and d_next["lastCorrectWritten"] == (CORRECT_AIR, CORRECT_GROUND_CLIFF_STOP))
    res.row("HMN near right ledge wall & CPU on lip", "hmnX>85 cpuX in (60,85)",
            f"hmn=({hf['x']:.1f},{hf['y']:.1f}) cpu=({cf['x']:.1f},{cf['y']:.1f})",
            hf["x"] > 85.0 and 60.0 < cf["x"] < 85.5)

    # Drift left / Up-B into ledge to trigger ledge grab / landing / KO auto-reset.
    r.emu.hold(0, stick=(28, 228))
    r.emu.advance(8)
    r.emu.press(0, ["B"], stick=(60, 255), frames=3, after=0)
    r.emu.neutral(0)
    auto_reset = False
    for _ in range(180):
        r.emu.advance(1)
        if sb.read()["resetCount"] > d_warp["resetCount"]:
            auto_reset = True
            break
    res.row("Ledgetech auto-resets cleanly", "resetCount+1",
            f"resets={d_warp['resetCount']}->{sb.read()['resetCount']}", auto_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_slide_off_fd(r, sb):
    """Verify Slide-Off on FD places HMN near lip and detects airborne tumble slide-off."""
    res = CT.Result("slide-off-fd")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    set_player_percent(r.emu, r.opt.base, 65, True)
    sb.set_menu(SCEN_SLIDE_OFF, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(4)
    d0 = sb.read()
    hf = r.g.fighter(r.g.entry(CT.P1))
    cf = r.g.fighter(r.g.entry(CT.CPU))
    res.row("Slide-Off FD lip placement", "hasPlat=0 hmnX>76 cpuX<hmnX",
            f"hasPlat={d0['hasPlatform']} hmnX={hf['x']:.1f} cpuX={cf['x']:.1f}",
            d0["hasPlatform"] == 0 and hf["x"] > 76.0 and cf["x"] < hf["x"])

    # Hold DI right/down off the lip while CPU throws ftilt across the edge.
    r.emu.hold(0, stick=(235, 80))
    saw_slide = False
    saw_reset = False
    for _ in range(220):
        r.emu.advance(1)
        d = sb.read()
        if d["sawSlideOff"]:
            saw_slide = True
        if d["resetCount"] > d0["resetCount"]:
            saw_reset = True
            break
    r.emu.neutral(0)
    res.row("Slide-Off detected tumble & auto-reset", "sawSlideOff & reset",
            f"sawSlide={saw_slide} reset={saw_reset}", saw_slide and saw_reset)
    set_player_percent(r.emu, r.opt.base, 0, False)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_grab_escape(r, sb):
    """Verify Grab Escape capture frame counting, CPU mash escape detection, and auto-reset."""
    res = CT.Result("grab-escape")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    r.opt.cpu_percent(0, False)
    sb.set_menu(SCEN_GRAB_ESCAPE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(8)
    d0 = sb.read()

    # P1 grabs CPU with Z and holds neutral so CPU (GrabEscape=High) mashes out!
    r.emu.press(0, ["Z"], frames=3, after=0)
    saw_grab_frames = 0
    saw_escape = False
    saw_reset = False
    for _ in range(220):
        r.emu.advance(1)
        d = sb.read()
        if d["grabEscapeFrames"] > saw_grab_frames:
            saw_grab_frames = d["grabEscapeFrames"]
        if d["sawGrabEscape"]:
            saw_escape = True
        if d["resetCount"] > d0["resetCount"]:
            saw_reset = True
            break
    res.row("Grab Escape tracked frames & escape reset", "frames>0 & escape & reset",
            f"frames={saw_grab_frames} escape={saw_escape} reset={saw_reset}",
            saw_grab_frames > 0 and saw_escape and saw_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_multi_hit_sdi(r, sb):
    """Verify P+ Multi-Hit SDI live hitlag SDI displacement & escape on Ness PK Fire."""
    res = CT.Result("multi-hit-sdi")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    set_player_percent(r.emu, r.opt.base, 20, True)
    sb.set_menu(SCEN_MULTI_HIT_SDI, auto_reset=1, side=SIDE_RIGHT, move=MH_SIDE_B)
    r.emu.advance(4)
    d0 = sb.read()

    # Wait neutrally until PK Fire hits P1 and connects a 2nd link, then
    # rapidly alternate stick right / up-right during hitlag so P1 SDIs out!
    h_en = r.g.entry(CT.P1)
    max_hits = 0
    max_lag = 0
    max_disp = 0.0
    saw_escaped = False
    saw_reset = False
    for i in range(280):
        hf = r.g.fighter(h_en)
        d = sb.read()
        if d["sdiHitCount"] >= 2 and (hf["hitlag"] > 0 or hf["hitstun"] > 0):
            # Alternate between up-right and right every frame to trigger P+ SDI shifts.
            stick = (255, 228) if (i % 2 == 0) else (255, 128)
            r.emu.hold(0, stick=stick)
        else:
            r.emu.neutral(0)
        r.emu.advance(1)
        d = sb.read()
        if d["sdiHitCount"] > max_hits:
            max_hits = d["sdiHitCount"]
        if d["sdiHitlagFrames"] > max_lag:
            max_lag = d["sdiHitlagFrames"]
        disp = abs(d["sdiDispX"]) + abs(d["sdiDispY"])
        if disp > max_disp:
            max_disp = disp
        if d["sdiEscaped"]:
            saw_escaped = True
        if d["resetCount"] > d0["resetCount"]:
            saw_reset = True
            break
    r.emu.neutral(0)
    set_player_percent(r.emu, r.opt.base, 0, False)
    res.row("PK Fire multi-hit links & hitlag counted", "hits>=2 lag>=2",
            f"hits={max_hits} lag={max_lag}", max_hits >= 2 and max_lag >= 2)
    res.row("live SDI displacement & escape detected", "disp>0.5 & escaped & reset",
            f"disp={max_disp:.2f} escaped={saw_escaped} reset={saw_reset}",
            max_disp > 0.5 and saw_escaped and saw_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_battlefield_platforms(r, sb):
    """Verify Battlefield Shield Drop platform cycling (D-Pad Up), P1 Infinite Shield,
    Guard->Pass detection, and Slide-Off."""
    res = CT.Result("battlefield-platforms")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)

    # 1. Shield Drop on Battlefield: Left default + P1 Infinite Shield On + D-Pad Up cycles Left -> Right -> Top.
    sb.set_menu(SCEN_SHIELD_DROP, auto_reset=1, side=SIDE_RIGHT,
                sd_plat=SD_PLAT_LEFT, sd_space=SD_SPACE_UNDER, sd_face=SD_FACE_TOWARD)
    r.emu.advance(6)
    d_left = sb.read()
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    hf_l = r.g.fighter(h_en)
    inf_sh = r.emu.u8(r.opt.base + SET_PLAYER_INF_SHIELD)
    res.row("Shield Drop Left plat & P1 Infinite Shield On", "slot=0 x<-20 y~27 inf=2",
            f"slot={d_left['resolvedPlatformSlot']} hmn=({hf_l['x']:.1f},{hf_l['y']:.1f}) inf={inf_sh}",
            d_left["hasPlatform"] == 1 and d_left["resolvedPlatformSlot"] == SD_PLAT_LEFT
            and -58.0 < hf_l["x"] < -20.0 and 24.0 < hf_l["y"] < 31.0 and inf_sh == 2)

    # Press D-Pad Up once -> cycles to Right platform; press again -> Top platform; press again -> Left, then Right.
    r.emu.press(0, ["DUP"], frames=2, after=6)
    d_right = sb.read()
    hf_r = r.g.fighter(h_en)
    r.emu.press(0, ["DUP"], frames=2, after=6)
    d_top = sb.read()
    hf_t = r.g.fighter(h_en)
    res.row("D-Pad Up cycles Left -> Right -> Top platforms", "R(slot=1 x>20) T(slot=2 y>45)",
            f"R=(slot={d_right['resolvedPlatformSlot']},x={hf_r['x']:.1f},y={hf_r['y']:.1f}) "
            f"T=(slot={d_top['resolvedPlatformSlot']},x={hf_t['x']:.1f},y={hf_t['y']:.1f})",
            d_right["resolvedPlatformSlot"] == SD_PLAT_RIGHT and 20.0 < hf_r["x"] < 58.0 and 24.0 < hf_r["y"] < 31.0
            and d_top["resolvedPlatformSlot"] == SD_PLAT_TOP and abs(hf_t["x"]) < 20.0 and hf_t["y"] > 45.0)

    # Select Right platform explicitly for the Guard->Pass shield drop check.
    sb.set_menu(SCEN_SHIELD_DROP, auto_reset=1, side=SIDE_RIGHT,
                sd_plat=SD_PLAT_RIGHT, sd_space=SD_SPACE_CLOSE, sd_face=SD_FACE_TOWARD)
    r.emu.advance(6)
    d0 = sb.read()
    hf = r.g.fighter(h_en)
    cf = r.g.fighter(c_en)
    res.row("Battlefield Shield Drop spawns on Right plat", "hasPlat=1 hmnY~27 cpuY~0",
            f"hasPlat={d0['hasPlatform']} hmn=({hf['x']:.1f},{hf['y']:.1f}) cpu=({cf['x']:.1f},{cf['y']:.1f})",
            d0["hasPlatform"] == 1 and 20.0 < hf["x"] < 58.0 and 24.0 < hf["y"] < 31.0 and abs(cf["y"]) < 2.0)

    # Hold L to enter Guard on platform, then tilt stick down-right (~65% down) for clean P+ shield drop.
    r.emu.hold(0, ["L"], stick=(128, 128))
    r.emu.advance(8)
    # Tilt stick to straight-down / axe-notch to drop through platform without spotdodging.
    r.emu.hold(0, ["L"], stick=(196, 62))
    saw_drop = False
    saw_reset = False
    for i in range(160):
        r.emu.advance(1)
        if i == 10:
            r.emu.neutral(0)
        d = sb.read()
        if d["sawShieldDrop"]:
            saw_drop = True
        if d["resetCount"] > d0["resetCount"]:
            saw_reset = True
            break
    r.emu.neutral(0)
    res.row("Shield Drop Guard->Pass (0x72) & auto-reset", "sawDrop & reset",
            f"sawDrop={saw_drop} reset={saw_reset} comp={sb.read()['completionCount']}",
            saw_drop and saw_reset)

    # 2. Slide-Off on Battlefield right platform.
    set_player_percent(r.emu, r.opt.base, 65, True)
    sb.set_menu(SCEN_SLIDE_OFF, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(5)
    d1 = sb.read()
    hf2 = r.g.fighter(h_en)
    cf2 = r.g.fighter(c_en)
    res.row("Battlefield Slide-Off spawns on platform edge", "hasPlat=1 both Y~27",
            f"hasPlat={d1['hasPlatform']} hmn=({hf2['x']:.1f},{hf2['y']:.1f}) cpu=({cf2['x']:.1f},{cf2['y']:.1f})",
            d1["hasPlatform"] == 1 and 24.0 < hf2["y"] < 31.0 and 24.0 < cf2["y"] < 31.0)

    r.emu.hold(0, stick=(20, 80))
    saw_slide = False
    saw_slide_reset = False
    for _ in range(220):
        r.emu.advance(1)
        d = sb.read()
        if d["sawSlideOff"]:
            saw_slide = True
        if d["resetCount"] > d1["resetCount"]:
            saw_slide_reset = True
            break
    r.emu.neutral(0)
    set_player_percent(r.emu, r.opt.base, 0, False)
    res.row("Battlefield Slide-Off detected & auto-reset", "sawSlideOff & reset",
            f"sawSlide={saw_slide} reset={saw_slide_reset}", saw_slide and saw_slide_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_slide_off_zero_and_offstage_switch(r, sb):
    """Verify Slide-Off from default 0% tumbles & restores P1 percent cleanly, and offstage switch has no ghost KO."""
    res = CT.Result("slide-off-zero-and-offstage-switch")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    # 1. Activate Slide-Off directly from default 0% (unlocked) and confirm tumble + slide-off.
    set_player_percent(r.emu, r.opt.base, 0, True)
    r.emu.advance(2)
    set_player_percent(r.emu, r.opt.base, 0, False)
    r.emu.advance(2)
    log_pos = len(r.emu.log_text())
    sb.set_menu(SCEN_SLIDE_OFF, auto_reset=0, side=SIDE_RIGHT)
    r.emu.advance(4)
    live_pct, live_lock = read_player_percent(r.emu, r.opt.base)
    hf0 = r.g.fighter(h_en)
    saw_tumble = False
    saw_slide = False
    r.emu.hold(0, stick=(235, 80))
    for _ in range(90):
        r.emu.advance(1)
        hf = r.g.fighter(h_en)
        if 0x45 <= hf["status"] <= 0x49:
            saw_tumble = True
        if sb.read()["sawSlideOff"]:
            saw_slide = True
            break
    r.emu.neutral(0)
    res.row("Slide-Off at default 0% raises dmg & tumbles", "pct>=75 tumble slide",
            f"pct={live_pct} dmg={hf0['damage']:.0f} tumb={saw_tumble} slide={saw_slide}",
            live_pct >= 75 and hf0["damage"] >= 75.0 and saw_tumble and saw_slide)

    # 2. Switch Slide-Off -> Grab Escape -> Off: P1 percent & lock restore cleanly with 0 SD saves.
    sb.set_menu(SCEN_GRAB_ESCAPE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(4)
    mid_pct, mid_lock = read_player_percent(r.emu, r.opt.base)
    mid_dmg = r.g.fighter(h_en)["damage"]
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    end_pct, end_lock = read_player_percent(r.emu, r.opt.base)
    end_dmg = r.g.fighter(h_en)["damage"]
    log_tail = r.emu.log_text()[log_pos:]
    leaked_save = ("storage: settings saved" in log_tail or "Lab settings not saved" in log_tail)
    res.row("Slide-Off switch restores P1 percent & 0 SD save", "0/0 & dmg=0 & no save",
            f"mid=({mid_pct},{mid_lock},{mid_dmg:.0f}) end=({end_pct},{end_lock},{end_dmg:.0f}) save={leaked_save}",
            mid_pct == 0 and mid_lock == 0 and mid_dmg < 1.0
            and end_pct == 0 and end_lock == 0 and end_dmg < 1.0 and not leaked_save)

    # 3. Idle 45f in Ledgetech then switch to Grab Escape (45f): zero GONE/Rebirth frames.
    sb.set_menu(SCEN_LEDGETECH, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(4)
    lt_died = False
    for _ in range(45):
        r.emu.advance(1)
        if r.g.fighter(h_en)["status"] in CT.GONE or r.g.fighter(c_en)["status"] in CT.GONE:
            lt_died = True
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    sb.set_menu(SCEN_GRAB_ESCAPE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(4)
    ge_died = False
    for _ in range(45):
        r.emu.advance(1)
        if r.g.fighter(h_en)["status"] in CT.GONE or r.g.fighter(c_en)["status"] in CT.GONE:
            ge_died = True
    res.row("Ledgetech idle -> Grab Escape has zero KO/Rebirth", "ltDied=0 geDied=0",
            f"ltDied={lt_died} geDied={ge_died}", not lt_died and not ge_died)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_pikachu_grab_and_multihit_aerials(r, sb):
    """Verify short-arm Pikachu Grab Escape, Fox Up/Down Air Multi-Hit SDI, and Ledgetech dtilt."""
    res = CT.Result("pikachu-grab-and-multihit-aerials")
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)

    # 1. Grab Escape: Pikachu standing grab connects immediately after reset and Fox CPU escapes.
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    before_rst = sb.read()["resetCount"]
    sb.set_menu(SCEN_GRAB_ESCAPE, auto_reset=0, side=SIDE_RIGHT)
    for _ in range(20):
        r.emu.advance(1)
        if sb.read()["resetCount"] > before_rst:
            break
    d_ge = sb.read()
    grab_dx = abs(d_ge["cpuSpawnX"] - d_ge["hmnSpawnX"])
    r.emu.advance(3)
    r.emu.hold(0, ["Z"])
    r.emu.advance(3)
    r.emu.neutral(0)
    grabbed = False
    escaped = False
    for _ in range(70):
        r.emu.advance(1)
        if r.g.fighter(c_en)["status"] in (STATUS_CAPTURE_PULLED, STATUS_CAPTURE_WAIT):
            grabbed = True
        if sb.read()["sawGrabEscape"]:
            escaped = True
            break
    res.row("Pikachu standing grab connects in Grab Escape", "dx<=7 & grab & escape",
            f"dx={grab_dx:.1f} grab={grabbed} escape={escaped}",
            grab_dx <= 7.0 and grabbed and escaped)

    # 2. P+ Multi-Hit SDI: Fox CPU Up Air and Down Air both deliver >= 2 hits on Pikachu.
    for mv, label in ((MH_UP_AIR, "Up Air"), (MH_DOWN_AIR, "Down Air")):
        sb.set_menu(SCEN_OFF)
        r.emu.advance(4)
        set_player_percent(r.emu, r.opt.base, 30, True)
        sb.set_menu(SCEN_MULTI_HIT_SDI, auto_reset=0, side=SIDE_RIGHT, move=mv)
        r.emu.advance(4)
        max_hits = 0
        for _ in range(110):
            r.emu.advance(1)
            d = sb.read()
            if d["sdiHitCount"] > max_hits:
                max_hits = d["sdiHitCount"]
        set_player_percent(r.emu, r.opt.base, 0, False)
        res.row(f"Fox CPU {label} multi-hits short Pikachu", "sdiHitCount>=2",
                f"hits={max_hits}", max_hits >= 2)

    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_fox_ledgetech_and_offstage_switch(r, sb):
    """Verify Fox vs Fox Ledgetech dtilt connects and fastfaller offstage switch has zero KO/Rebirth."""
    res = CT.Result("fox-ledgetech-and-offstage-switch")
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)

    # 1. Ledgetech: Fox CPU ledge-lip dtilt connects on recovering Fox P1.
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    set_player_percent(r.emu, r.opt.base, 80, True)
    before_rst = sb.read()["resetCount"]
    sb.set_menu(SCEN_LEDGETECH, auto_reset=0, side=SIDE_RIGHT)
    for _ in range(20):
        r.emu.advance(1)
        if sb.read()["resetCount"] > before_rst:
            break
    r.emu.advance(3)
    saw_lt_hit = False
    for f in range(90):
        if f < 4:
            r.emu.hold(0, ["X"], stick=LT.LEFT)
        elif f < 10:
            r.emu.hold(0, [], stick=LT.LEFT)
        elif f < 14:
            r.emu.hold(0, ["B"], stick=(70, 228))
        else:
            r.emu.hold(0, [], stick=LT.LEFT)
        r.emu.advance(1)
        hf = r.g.fighter(h_en)
        if hf["hitlag"] > 0 or hf["hitstun"] > 0:
            saw_lt_hit = True
            break
    r.emu.neutral(0)
    set_player_percent(r.emu, r.opt.base, 0, False)
    res.row("Fox Ledgetech CPU dtilt hits recovering Fox", "sawHit=True",
            f"sawHit={saw_lt_hit}", saw_lt_hit)

    # 2. Idle 45f in Ledgetech on FD (fastfaller Fox reaches y=-139.1 without proactive reset),
    # then switch Off -> Slide-Off (45f) -> Grab Escape (45f): zero GONE/Rebirth frames!
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    before_rst = sb.read()["resetCount"]
    sb.set_menu(SCEN_LEDGETECH, auto_reset=1, side=SIDE_RIGHT)
    for _ in range(20):
        r.emu.advance(1)
        if sb.read()["resetCount"] > before_rst:
            break
    r.emu.advance(3)
    lt_died = False
    for _ in range(45):
        r.emu.advance(1)
        if r.g.fighter(h_en)["status"] in CT.GONE or r.g.fighter(c_en)["status"] in CT.GONE:
            lt_died = True
    follow_died = False
    for nxt in (SCEN_SLIDE_OFF, SCEN_GRAB_ESCAPE):
        sb.set_menu(SCEN_OFF)
        r.emu.advance(4)
        before_rst = sb.read()["resetCount"]
        sb.set_menu(nxt, auto_reset=1, side=SIDE_RIGHT)
        for _ in range(20):
            r.emu.advance(1)
            if sb.read()["resetCount"] > before_rst:
                break
        r.emu.advance(3)
        for _ in range(45):
            r.emu.advance(1)
            if r.g.fighter(h_en)["status"] in CT.GONE or r.g.fighter(c_en)["status"] in CT.GONE:
                follow_died = True
    res.row("Fox Ledgetech idle -> Slide-Off -> Grab Escape 0 KO", "ltDied=0 follow=0",
            f"ltDied={lt_died} followDied={follow_died}", not lt_died and not follow_died)

    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_ledgetech_cypher_dj_and_pause(r, sb):
    """Verify Snake Ledgetech Cypher refresh across repeated resets, 60% auto-seed,
    P1 Double Jump = Spent, and Brawl Training Pause preservation."""
    res = CT.Result("ledgetech-cypher-dj-and-pause")
    h_en = r.g.entry(CT.P1)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    set_player_percent(r.emu, r.opt.base, 0, False)
    r.emu.advance(3)

    # 1. Activate Ledgetech from 0% -> auto-seeds P1 to 60%, and Up-B Cypher works on rep 1 AND rep 2!
    sb.set_menu(SCEN_LEDGETECH, auto_reset=0, side=SIDE_RIGHT,
                lt_spawn=LT_SPAWN_CLOSE_LOW, lt_dj=LT_DJ_AVAIL, lt_atk=LT_ATK_DTILT)
    r.emu.advance(4)
    pct_live, _ = read_player_percent(r.emu, r.opt.base)
    dmg_live = r.g.fighter(h_en)["damage"]
    res.row("Ledgetech auto-seeds P1 to 60% from 0%", "pct=60 dmg>=59",
            f"pct={pct_live} dmg={dmg_live:.1f}", pct_live == 60 and dmg_live >= 59.0)

    cypher_reps = []
    for rep in range(2):
        if rep > 0:
            # Force a fresh rep via menu reset (same path as auto-reset / Reset Now).
            sb.set_menu(SCEN_OFF)
            r.emu.advance(2)
            sb.set_menu(SCEN_LEDGETECH, auto_reset=0, side=SIDE_RIGHT,
                        lt_spawn=LT_SPAWN_CLOSE_LOW, lt_dj=LT_DJ_AVAIL, lt_atk=LT_ATK_DTILT)
            r.emu.advance(3)
        saw_upb = False
        r.emu.press(0, ["B"], stick=(128, 255), frames=4, after=0)
        for _ in range(24):
            r.emu.advance(1)
            if r.g.fighter(h_en)["status"] >= 0x112:
                saw_upb = True
                break
        cypher_reps.append(saw_upb)
    res.row("Snake Cypher (Up-B) works across repeated resets", "[True, True]",
            str(cypher_reps), cypher_reps == [True, True])

    # 2. P1 Double Jump = Spent: pressing X in air must NOT enter Jump_Aerial (0x0C).
    sb.set_menu(SCEN_LEDGETECH, auto_reset=0, side=SIDE_RIGHT,
                lt_spawn=LT_SPAWN_CLOSE_LOW, lt_dj=LT_DJ_SPENT, lt_atk=LT_ATK_DTILT)
    r.emu.advance(4)
    saw_dj = False
    r.emu.press(0, ["X"], frames=4, after=0)
    for _ in range(16):
        r.emu.advance(1)
        if r.g.fighter(h_en)["status"] == CT.STATUS["Jump_Aerial"]:
            saw_dj = True
            break
    res.row("Snake P1 Double Jump Spent blocks midair jump", "sawDj=False",
            f"sawDj={saw_dj}", not saw_dj)

    # 3. Brawl Training Pause -> unpause preserves active Ledgetech & triggers reset.
    resets_pre_pause = sb.read()["resetCount"]
    nav.pause_training(r.emu)
    r.emu.advance(12)
    nav.press_until(r.emu, "unpause Brawl Training pause", ["B"],
                    lambda: not nav.training_paused(r.emu), 120, every=10)
    r.emu.advance(8)
    d_post_pause = sb.read()
    res.row("Brawl Training Pause preserves & resets Ledgetech", "preset=6 resets+",
            f"preset={d_post_pause['activePreset']} resets={resets_pre_pause}->{d_post_pause['resetCount']}",
            d_post_pause["activePreset"] == SCEN_LEDGETECH and d_post_pause["resetCount"] > resets_pre_pause)

    # 4. Off restores P1 percent cleanly to 0% unlocked.
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    pct_end, lock_end = read_player_percent(r.emu, r.opt.base)
    res.row("Ledgetech Off restores P1 0% unlocked", "(0, 0)",
            f"({pct_end}, {lock_end})", pct_end == 0 and lock_end == 0)
    return res


def scenario_jigglypuff_dj_spent(r, sb):
    """Verify P1 Double Jump = Spent exhausts all 5 air jumps on multi-jump Jigglypuff."""
    res = CT.Result("jigglypuff-dj-spent")
    h_en = r.g.entry(CT.P1)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    sb.set_menu(SCEN_LEDGETECH, auto_reset=0, side=SIDE_RIGHT,
                lt_spawn=LT_SPAWN_FAR_OUT, lt_dj=LT_DJ_SPENT, lt_atk=LT_ATK_DTILT)
    r.emu.advance(4)
    saw_air_jump = False
    r.emu.press(0, ["X"], frames=4, after=0)
    for _ in range(18):
        r.emu.advance(1)
        st = r.g.fighter(h_en)["status"]
        if st in (CT.STATUS["Jump_Aerial"], 0x0D):  # Jump_Aerial or Fly
            saw_air_jump = True
            break
    res.row("Jigglypuff DJ Spent blocks all multi-jumps", "sawAirJump=False",
            f"sawAirJump={saw_air_jump}", not saw_air_jump)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_techchase_air_and_edgeguard_sides(r, sb):
    """Verify Edgeguard Left/Right side spawns + 35% CPU seed, and Techchase air escape auto-reset."""
    res = CT.Result("techchase-air-and-edgeguard-sides")
    h_en = r.g.entry(CT.P1)
    c_en = r.g.entry(CT.CPU)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    r.opt.cpu_percent(0, False)
    r.emu.advance(3)

    # 1. Edgeguard Left spawns left + auto-seeds CPU to 35%; Right spawns right; Off restores 0%.
    sb.set_menu(SCEN_EDGEGUARD, auto_reset=1, side=SIDE_LEFT)
    r.emu.advance(5)
    hf_l, cf_l = r.g.fighter(h_en), r.g.fighter(c_en)
    cpu_pct_live = r.emu.u16(r.opt.base + LT.SET_CPU_PERCENT)
    sb.set_menu(SCEN_EDGEGUARD, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(5)
    hf_r, cf_r = r.g.fighter(h_en), r.g.fighter(c_en)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    cpu_pct_end = r.emu.u16(r.opt.base + LT.SET_CPU_PERCENT)
    res.row("Edgeguard Left/Right sides & 35% CPU seed/restore", "L<0 R>0 pct=35->0",
            f"L=({hf_l['x']:.1f},{cf_l['x']:.1f}) R=({hf_r['x']:.1f},{cf_r['x']:.1f}) pct={cpu_pct_live}->{cpu_pct_end}",
            hf_l["x"] < -60.0 and cf_l["x"] < -88.0 and hf_r["x"] > 60.0 and cf_r["x"] > 88.0
            and cpu_pct_live == 35 and cpu_pct_end == 0)

    # 2. Techchase airborne escape: launch Fox high at 115% with Up Smash so he recovers/escapes in air -> auto-reset!
    sb.set_menu(SCEN_TECHCHASE, auto_reset=1, side=SIDE_RIGHT)
    r.emu.advance(6)
    r.opt.cpu_percent(115, True)
    r.emu.advance(2)
    r.opt.cpu_percent(115, False)
    d0 = sb.read()
    hit_ok = RC.move_p1_to_cpu_and_hit(r, CT.USMASH)
    auto_reset = False
    for _ in range(260):
        r.emu.advance(1)
        if sb.read()["resetCount"] > d0["resetCount"]:
            auto_reset = True
            break
    res.row("Techchase airborne escape triggers auto-reset", "hit & resetCount+",
            f"hit={hit_ok} resets={d0['resetCount']}->{sb.read()['resetCount']}", hit_ok and auto_reset)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    return res


def scenario_multihit_sdi_auto(r, sb, cpu_label, want_move):
    """Verify Multi-Hit SDI Auto (Best) selects expected move and delivers >= 2 hits."""
    res = CT.Result(f"multihit-sdi-auto-{cpu_label}")
    sb.set_menu(SCEN_OFF)
    r.emu.advance(4)
    set_player_percent(r.emu, r.opt.base, 25, True)
    sb.set_menu(SCEN_MULTI_HIT_SDI, auto_reset=0, side=SIDE_RIGHT, move=MH_AUTO)
    r.emu.advance(5)
    d0 = sb.read()
    max_hits = 0
    for _ in range(130):
        r.emu.advance(1)
        d = sb.read()
        if d["sdiHitCount"] > max_hits:
            max_hits = d["sdiHitCount"]
            if max_hits >= 2:
                break
    set_player_percent(r.emu, r.opt.base, 0, False)
    sb.set_menu(SCEN_OFF)
    r.emu.advance(3)
    res.row(f"Auto (Best) {cpu_label} move={want_move} & hits>=2",
            f"move={want_move} hits>=2",
            f"move={d0['resolvedMultiHit']} hits={max_hits}",
            d0["resolvedMultiHit"] == want_move and max_hits >= 2)
    return res


ALL_SCENARIOS = [
    "off-snapshot-restore",
    "combo-lab",
    "techchase",
    "edgeguard",
    "shield-pressure",
    "oos-reversal",
    "shield-drop-fd-fallback",
    "ledgetech",
    "slide-off-fd",
    "grab-escape",
    "multi-hit-sdi",
    "battlefield-platforms",
    "slide-off-zero-and-offstage-switch",
    "pikachu-grab-and-multihit-aerials",
    "fox-ledgetech-and-offstage-switch",
    "techchase-air-and-edgeguard-sides",
    "ledgetech-cypher-dj-and-pause",
    "jigglypuff-dj-spent",
    "multihit-sdi-auto-ivysaur",
    "multihit-sdi-auto-wario",
    "multihit-sdi-auto-meta-knight",
    "multihit-sdi-auto-fox",
]


def cmd_test_scenarios(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-scenarios")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args(rest)

    wanted = [s for s in ALL_SCENARIOS if not opts.only or s in opts.only]
    if opts.list:
        for s in wanted:
            print(s)
        return 0
    if not wanted:
        E.die(f"no scenarios matched --only {opts.only!r}")

    label = "test-scenarios"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, results, times = "", "boot", "", [], []

    def run_one(name, fn):
        t1 = time.monotonic()
        try:
            res = fn()
        except (KeyError, IndexError, TypeError, ValueError, AttributeError,
                ZeroDivisionError, CT.CpuError, TestError) as exc:
            import traceback
            res = CT.Result(name)
            res.check(f"{name} stopped: {exc!r}", False)
            for line in traceback.format_exc().splitlines()[-6:]:
                res.note(line)
        res.key = name
        print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}", flush=True)
        for line in res.lines:
            print("  " + line, flush=True)
        results.append(res)
        times.append((name, time.monotonic() - t1))

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            r = CT.Runner(emu)
            sb = ScenarioBlock(emu)
            cb = RC.ComboBlock(emu)
            current_match = None

            def ensure_match(stage_name, cpu_name, p1_name="mario"):
                nonlocal current_match
                key = (stage_name, p1_name, cpu_name)
                emu.h.call("resume")
                if current_match != key:
                    if nav.scene(emu)[0] == "scMelee":
                        nav.finish_training(emu)
                    nav.start_quick_match(emu, stage_name, p1=CHARS[p1_name], cpu=CHARS[cpu_name])
                    emu.advance(45)
                    current_match = key
                r.refresh()
                sb.find()
                cb.find()

            try:
                step = "boot"
                nav.boot_to_training_css(emu)

                fd_fox_map = {
                    "off-snapshot-restore": lambda: scenario_off_snapshot_restore(r, sb),
                    "combo-lab": lambda: scenario_combo_lab(r, sb, cb),
                    "techchase": lambda: scenario_techchase(r, sb),
                    "edgeguard": lambda: scenario_edgeguard(r, sb, cb),
                    "shield-pressure": lambda: scenario_shield_pressure(r, sb),
                    "oos-reversal": lambda: scenario_oos_reversal(r, sb),
                    "shield-drop-fd-fallback": lambda: scenario_shield_drop_fd(r, sb),
                    "ledgetech": lambda: scenario_ledgetech(r, sb),
                    "slide-off-fd": lambda: scenario_slide_off_fd(r, sb),
                    "grab-escape": lambda: scenario_grab_escape(r, sb),
                    "slide-off-zero-and-offstage-switch": lambda: scenario_slide_off_zero_and_offstage_switch(r, sb),
                    "techchase-air-and-edgeguard-sides": lambda: scenario_techchase_air_and_edgeguard_sides(r, sb),
                    "multihit-sdi-auto-fox": lambda: scenario_multihit_sdi_auto(r, sb, "fox", MH_DOWN_AIR),
                }
                for name in wanted:
                    if name in fd_fox_map:
                        step = name
                        ensure_match("final_destination", "fox")
                        run_one(name, fd_fox_map[name])

                if "pikachu-grab-and-multihit-aerials" in wanted:
                    step = "pikachu-grab-and-multihit-aerials"
                    ensure_match("final_destination", "fox", p1_name="pikachu")
                    run_one("pikachu-grab-and-multihit-aerials",
                            lambda: scenario_pikachu_grab_and_multihit_aerials(r, sb))

                if "fox-ledgetech-and-offstage-switch" in wanted:
                    step = "fox-ledgetech-and-offstage-switch"
                    ensure_match("final_destination", "fox", p1_name="fox")
                    run_one("fox-ledgetech-and-offstage-switch",
                            lambda: scenario_fox_ledgetech_and_offstage_switch(r, sb))

                if "ledgetech-cypher-dj-and-pause" in wanted:
                    step = "ledgetech-cypher-dj-and-pause"
                    ensure_match("final_destination", "fox", p1_name="snake")
                    run_one("ledgetech-cypher-dj-and-pause",
                            lambda: scenario_ledgetech_cypher_dj_and_pause(r, sb))

                if "jigglypuff-dj-spent" in wanted:
                    step = "jigglypuff-dj-spent"
                    ensure_match("final_destination", "fox", p1_name="jigglypuff")
                    run_one("jigglypuff-dj-spent",
                            lambda: scenario_jigglypuff_dj_spent(r, sb))

                for sc_name, cpu_key, label_key, want_mv in (
                    ("multihit-sdi-auto-ivysaur", "ivysaur", "ivysaur", MH_NAIR),
                    ("multihit-sdi-auto-wario", "wario", "wario", MH_UP_B),
                    ("multihit-sdi-auto-meta-knight", "meta_knight", "meta-knight", MH_SIDE_B),
                ):
                    if sc_name in wanted:
                        step = sc_name
                        ensure_match("final_destination", cpu_key)
                        run_one(sc_name, lambda lk=label_key, wm=want_mv: scenario_multihit_sdi_auto(r, sb, lk, wm))

                if "multi-hit-sdi" in wanted:
                    step = "multi-hit-sdi"
                    ensure_match("final_destination", "ness")
                    run_one("multi-hit-sdi", lambda: scenario_multi_hit_sdi(r, sb))

                if "battlefield-platforms" in wanted:
                    step = "battlefield-platforms"
                    ensure_match("battlefield", "fox")
                    run_one("battlefield-platforms", lambda: scenario_battlefield_platforms(r, sb))

                step = "finish"
                emu.h.call("resume")
                if nav.scene(emu)[0] == "scMelee":
                    nav.finish_training(emu)
                emu.advance(40)
            except (Timeout, nav.NavError, HarnessError, CT.CpuError, LT.LabError, PI.InputError, TestError) as exc:
                why = f"{step}: {exc}"
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{step}: {exc}"

    log = E.read_log(log_path) if log_path else ""
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why:
        why = CT.cpu_report(log)
    print()
    print(f"{'scenario':<26} {'check':<42} {'expected':<24} observed")
    for res in results:
        for name, what, exp, obs, ok in res.table:
            print(f"{'' if ok else '!'}{name:<25} {what[:42]:<42} {exp[:24]:<24} {obs}")
    if times:
        print("seconds per scenario: " + ", ".join(f"{name} {t:.0f}" for name, t in times))
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and not all(res.ok for res in results):
        print("failed --only: " + " ".join(res.key for res in results if not res.ok))
        why = "a scenario failed: " + ", ".join(res.name for res in results if not res.ok)
    if not why and not results:
        why = "no scenario ran"
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
