"""Emulator tests of per-fighter mechanics overrides
(src/extra/fighter_mechanics.cpp, run through emu.py).

  test-fighter-mechanics REL [--only NAME ...] [--keep-going]

One boot on Final Destination, switching fighters via nav.start_quick_match.
Verifies live P+ 3.2.1 behavior and work-variable state across all supported
fighters plus Peach Float and G&W Oil Panic, plus King Dedede's .pac weight
table detach restoration.
"""
import argparse
import os
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import charge_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# (gmCharacterKind, CSS icon id) from src/menu/names.cpp and scripts/nav.py.
CHARS = {
    "fox": (0x07, 0x07),
    "peach": (0x0D, 0x0D),
    "game_and_watch": (0x14, 0x12),
    "samus": (0x03, 0x03),
    "mewtwo": (0x33, 0x2E),
    "lucario": (0x24, 0x20),
    "wario": (0x17, 0x15),
    "donkey_kong": (0x01, 0x01),
    "ivysaur": (0x22, 0x2C),
    "rob": (0x26, 0x22),
    "lucas": (0x1B, 0x19),
    "squirtle": (0x20, 0x2B),
    "sheik": (0x0F, 0x0F),
    "king_dedede": (0x23, 0x1F),
    "snake": (0x2A, 0x26),
    "ganondorf": (0x16, 0x14),
    "olimar": (0x1A, 0x18),
}

# main::Settings offsets for CharacterRngSettings (292) & FighterMechanicsSettings (432).
S_GNW_HAMMER = 294
S_FM = 432
S_PEACH_FLOAT = S_FM + 0
S_GNW_BUCKET = S_FM + 1
S_SAMUS_CHARGE = S_FM + 2
S_SAMUS_BEAM = S_FM + 3
S_SAMUS_AIR_BOMBS = S_FM + 4
S_MEWTWO_BALL = S_FM + 5
S_MEWTWO_HOVER = S_FM + 6
S_LUCARIO_AURA = S_FM + 7
S_LUCARIO_SPHERE = S_FM + 8
S_WARIO_WAFT = S_FM + 9
S_DK_PUNCH = S_FM + 10
S_IVYSAUR_SOLAR = S_FM + 11
S_ROB_BOOSTS = S_FM + 12
S_ROB_GYRO = S_FM + 13
S_ROB_BEAM = S_FM + 14
S_LUCAS_OFFENSE = S_FM + 15
S_SQUIRTLE_GUN = S_FM + 16
S_SHEIK_NEEDLES = S_FM + 17
S_DEDEDE_WADDLE = S_FM + 18
S_SNAKE_TRANQ = S_FM + 19
S_SNAKE_CYPHER = S_FM + 20
S_GANON_FLOAT = S_FM + 21
S_NANA_THROW = S_FM + 22
S_OLIMAR_PIKMIN = S_FM + 23

RIGHT, LEFT, UP, DOWN, NEUTRAL = CT.RIGHT, CT.LEFT, CT.UP, CT.DOWN, CT.NEUTRAL

DEDEDE_FOUND_RE = re.compile(
    r"fighter mech: Dedede Waddle weights at ([0-9a-f]{8}) "
    r"\(35\.0/10\.0/4\.0/0\.0, const ([\d.]+)/([\d.]+)/([\d.]+)\): ok"
)
DEDEDE_DETACH_RE = re.compile(
    r"fighter mech: detach Dedede Waddle weights ([0-9a-f]{8}) \(put back 35\.0/10\.0/4\.0/0\.0,"
)
OLIMAR_FOUND_RE = re.compile(
    r"fighter mech: Olimar Pikmin weights at ([0-9a-f]{8}) "
    r"\(gid=0 spd=1\.0 1\.0/1\.0/1\.0/1\.0/1\.0\): ok"
)
OLIMAR_DETACH_RE = re.compile(
    r"fighter mech: detach Olimar Pikmin weights ([0-9a-f]{8}) \(put back 1\.0/1\.0/1\.0/1\.0/1\.0,"
)


def set_u8(emu, settings_addr, offset, value):
    emu.h.call("write_mem", addr=settings_addr + offset, hex=f"{value:02x}")
    CT.wait_game_frames(emu, 3)


def clear_fm(emu, settings_addr):
    emu.h.call("write_mem", addr=settings_addr + S_FM, hex="00" * 32)
    emu.h.call("write_mem", addr=settings_addr + S_GNW_HAMMER, hex="00")
    CT.wait_game_frames(emu, 2)


def state_path(name):
    p = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states", f"fm-{name}.sav")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def prepare(emu, char_name, cpu_name="fox"):
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
    if nav.scene(emu)[0] != "scSelctCharacter":
        nav.boot_to_training_css(emu)
    emu.mark(f"fighter mechanics: {char_name} vs {cpu_name}")
    nav.start_quick_match(emu, stage="final_destination", p1=CHARS[char_name], cpu=CHARS[cpu_name], seed_rng=True)
    emu.neutral(0)
    lab = LT.Lab(emu, [])
    settings = lab.find_settings()
    emu.h.call("write_mem", addr=settings + LT.SET_CPU_PERCENT, hex="000000")
    emu.h.call("write_mem", addr=settings + CT.SET_CPU_OPTIONS, hex="00")
    emu.h.call("write_mem", addr=settings + LT.SET_FRAME_ADVANCE, hex="00")
    clear_fm(emu, settings)
    CT.wait_game_frames(emu, 30)
    p1 = CT.FighterView(emu, 0)
    cpu = CT.FighterView(emu, 1)
    CT.wait_idle(emu, p1, 120)
    return settings, p1, cpu


def fire_two_tap(emu, p1, cpu, hold_statuses=(0x117, 0x118), direct_start_shoot=False, first_stick=NEUTRAL):
    before = cpu.damage()
    if direct_start_shoot:
        emu.hold(0, buttons=["B"], stick=first_stick)
        CT.wait_cond(emu, "SpecialNStart 0x112", lambda: p1.status() == 0x112, timeout=30, step=1)
        emu.neutral(0)
        CT.wait_game_frames(emu, 2)
        emu.hold(0, buttons=["B"])
        CT.wait_game_frames(emu, 3)
        emu.neutral(0)
    else:
        emu.hold(0, buttons=["B"], stick=first_stick)
        CT.wait_game_frames(emu, 3)
        emu.neutral(0)
        CT.wait_game_frames(emu, 2)
        CT.wait_cond(emu, "charge hold", lambda: p1.status() in hold_statuses, timeout=45, step=1)
        emu.hold(0, buttons=["B"])
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
    t0 = nav.game_frame(emu)
    while nav.game_frame(emu) - t0 < 70:
        if cpu.damage() > before + 0.01:
            CT.wait_game_frames(emu, 10)
            break
        emu.advance(2)
    CT.wait_idle(emu, p1, 120)
    return round(cpu.damage() - before, 2)


# ---- scenarios --------------------------------------------------------------


def sc_peach(emu, notes):
    settings, p1, _ = prepare(emu, "peach")
    fails = []
    set_u8(emu, settings, S_PEACH_FLOAT, 1)

    # Full-hop, then enter float (hold X + Down), hold past normal cap? Check
    # LA-Bit[62] stays 0 and RA-Basic[2] stays refilled near 150, then release
    # and re-enter float a SECOND time in the same airtime without landing!
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 14)
    emu.hold(0, buttons=["X"], stick=DOWN)
    CT.wait_cond(emu, "Peach first float (0x11E)", lambda: p1.status() == 0x11E, timeout=40, step=1)
    CT.wait_game_frames(emu, 30)
    st1, used1, rem1 = p1.status(), p1.la_bit(62), p1.ra_int(2)
    # Release jump briefly to fall out of float, then re-enter float midair!
    emu.neutral(0)
    CT.wait_cond(emu, "Peach exit float", lambda: p1.status() != 0x11E, timeout=30, step=1)
    CT.wait_game_frames(emu, 4)
    emu.hold(0, buttons=["X"], stick=DOWN)
    CT.wait_cond(emu, "Peach second midair float (0x11E)", lambda: p1.status() == 0x11E, timeout=40, step=1)
    st2, used2, rem2 = p1.status(), p1.la_bit(62), p1.ra_int(2)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 120)

    if st1 != 0x11E or used1 != 0 or rem1 < 145:
        fails.append(f"first float: status={st1:#x} used={used1} rem={rem1}")
    if st2 != 0x11E or used2 != 0 or rem2 < 145:
        fails.append(f"second midair float: status={st2:#x} used={used2} rem={rem2}")
    notes.append(f"peach float: 1st(st={st1:#x}, used={used1}, rem={rem1}) 2nd(st={st2:#x}, used={used2}, rem={rem2})")
    return fails


def sc_gnw(emu, notes):
    settings, p1, cpu = prepare(emu, "game_and_watch")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=10.0)

    readings = []
    for val in (1, 2, 3, 4):
        set_u8(emu, settings, S_GNW_BUCKET, val)
        readings.append((round(p1.la_float(19), 1), round(p1.la_float(20), 1), p1.la_bit(61)))
    if readings != [(0.0, 0.0, 0), (1.0, 12.0, 0), (2.0, 24.0, 0), (3.0, 36.0, 1)]:
        fails.append(f"bucket readings: {readings}")

    # Spill full bucket on CPU via Down-B and verify real damage + auto-refill.
    dmg = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=4, wait_f=70)
    after_cnt = round(p1.la_float(19), 1)
    if dmg < 15.0 or after_cnt != 3.0:
        fails.append(f"bucket spill: dmg={dmg} after_cnt={after_cnt}")

    # Savestate preservation: Default preserves saved full bucket after spill;
    # locked Bucket=2 and Judge=9 re-apply immediately on quick savestate load.
    set_u8(emu, settings, S_GNW_BUCKET, 0)
    CT.dpad_save(emu)
    CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=4, wait_f=60)
    ss_spent = round(p1.la_float(19), 1)
    CT.dpad_load(emu)
    ss_def = round(p1.la_float(19), 1)
    set_u8(emu, settings, S_GNW_BUCKET, 3)   # 2
    set_u8(emu, settings, S_GNW_HAMMER, 9)   # Judge 9 -> LA-Basic[67] == 8
    CT.dpad_load(emu)
    ss_lock = (round(p1.la_float(19), 1), p1.la_int(67))
    if ss_spent != 0.0 or ss_def != 3.0 or ss_lock != (2.0, 8):
        fails.append(f"gnw savestate: spent={ss_spent} def_load={ss_def} lock_load={ss_lock}")
    notes.append(
        f"gnw bucket: readings={readings} spill_dmg={dmg} refill={after_cnt} "
        f"ss(def={ss_def}, lock={ss_lock})"
    )
    return fails


def sc_samus(emu, notes):
    settings, p1, cpu = prepare(emu, "samus")
    fails = []

    # 1. Beam Mode: Plasma (1) -> LA-Basic[99]=0 vs Ice (2) -> LA-Basic[99]=1.
    # Run first at 0% and save state so both Up-Smashes hit Fox identically.
    CT.place_cpu(emu, p1, cpu, close_dist=7.0)
    beam_sav = state_path("samus-beam")
    emu.h.call("save_state", path=beam_sav)
    set_u8(emu, settings, S_SAMUS_BEAM, 1)
    beam_p = p1.la_int(99)
    dmg_p = CT.fire_and_measure_damage(emu, p1, cpu, buttons=(), c=UP, hold_f=3, wait_f=60)
    emu.h.call("load_state", path=beam_sav)
    CT.wait_game_frames(emu, 2)
    set_u8(emu, settings, S_SAMUS_BEAM, 2)
    beam_i = p1.la_int(99)
    dmg_i = CT.fire_and_measure_damage(emu, p1, cpu, buttons=(), c=UP, hold_f=3, wait_f=60)
    if (beam_p, beam_i) != (0, 1) or dmg_p <= 0.0 or dmg_i <= 0.0 or abs(dmg_p - dmg_i) < 0.5:
        fails.append(f"beam: vars={(beam_p, beam_i)} usmash_dmg={(dmg_p, dmg_i)}")

    # 2. Charge Shot: Empty (1) -> 0, Half (2) -> 4, Full (3) -> 7.
    emu.h.call("load_state", path=beam_sav)
    CT.wait_game_frames(emu, 2)
    set_u8(emu, settings, S_SAMUS_BEAM, 0)
    CT.place_cpu(emu, p1, cpu, close_dist=16.0)
    set_u8(emu, settings, S_SAMUS_CHARGE, 1)
    lvl_e = p1.la_int(65)
    dmg_e = fire_two_tap(emu, p1, cpu, hold_statuses=(0x118,))
    set_u8(emu, settings, S_SAMUS_CHARGE, 2)
    lvl_h = p1.la_int(65)
    dmg_h = fire_two_tap(emu, p1, cpu, hold_statuses=(0x118,))
    set_u8(emu, settings, S_SAMUS_CHARGE, 3)
    lvl_f = p1.la_int(65)
    dmg_f = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=70)
    if (lvl_e, lvl_h, lvl_f) != (0, 4, 7) or not (dmg_e < dmg_h < dmg_f):
        fails.append(f"charge: lvls={(lvl_e, lvl_h, lvl_f)} dmgs={(dmg_e, dmg_h, dmg_f)}")

    # 3. Air Bombs: Unlimited keeps LA-Basic[67] == 0 across 3 midair Down-B drops.
    set_u8(emu, settings, S_SAMUS_AIR_BOMBS, 1)
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 16)
    emu.neutral(0)
    bomb_counts = []
    for _ in range(3):
        emu.hold(0, buttons=["B"], stick=DOWN)
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
        CT.wait_game_frames(emu, 18)
        bomb_counts.append(p1.la_int(67))
    CT.wait_idle(emu, p1, 120)
    if any(c != 0 for c in bomb_counts):
        fails.append(f"air bombs: LA-Basic[67]={bomb_counts}")

    # 4. Savestate preservation: Default preserves saved Full charge after firing;
    # locked Half re-applies on quick savestate load.
    set_u8(emu, settings, S_SAMUS_AIR_BOMBS, 0)
    set_u8(emu, settings, S_SAMUS_CHARGE, 3)  # Full (7)
    set_u8(emu, settings, S_SAMUS_CHARGE, 0)  # Default (leave 7 intact)
    CT.dpad_save(emu)
    CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=50)
    ss_spent = p1.la_int(65)
    CT.dpad_load(emu)
    ss_def = p1.la_int(65)
    set_u8(emu, settings, S_SAMUS_CHARGE, 2)  # Half (4)
    CT.dpad_load(emu)
    ss_lock = p1.la_int(65)
    if ss_spent != 0 or ss_def != 7 or ss_lock != 4:
        fails.append(f"samus savestate: spent={ss_spent} def={ss_def} lock={ss_lock}")

    notes.append(
        f"samus: charge lvls={(lvl_e, lvl_h, lvl_f)} dmgs={(dmg_e, dmg_h, dmg_f)}; "
        f"beam vars={(beam_p, beam_i)} usmash={(dmg_p, dmg_i)}; air_bombs={bomb_counts}; "
        f"ss(def={ss_def}, lock={ss_lock})"
    )
    return fails


def sc_mewtwo(emu, notes):
    settings, p1, cpu = prepare(emu, "mewtwo")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=16.0)

    # 1. Shadow Ball: Empty (-1), Half (3), Full (6).
    set_u8(emu, settings, S_MEWTWO_BALL, 1)
    lvl_e = (p1.la_int(64), p1.la_int(65), p1.la_int(66))
    dmg_e = fire_two_tap(emu, p1, cpu)
    set_u8(emu, settings, S_MEWTWO_BALL, 2)
    lvl_h = (p1.la_int(64), p1.la_int(65), p1.la_int(66))
    dmg_h = fire_two_tap(emu, p1, cpu)
    set_u8(emu, settings, S_MEWTWO_BALL, 3)
    lvl_f = (p1.la_int(64), p1.la_int(65), p1.la_int(66))
    dmg_f = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=70)
    if (lvl_e[1], lvl_h[1], lvl_f[1]) != (-1, 3, 6) or not (dmg_e < dmg_h < dmg_f):
        fails.append(f"shadow ball: lvls={(lvl_e, lvl_h, lvl_f)} dmgs={(dmg_e, dmg_h, dmg_f)}")

    # 2. Hover: full-hop for height, enter 0x120 (X + Down), return stick to
    # neutral and hold past 80 frames (normal cap is 60f), release Jump out of
    # hover (0x11E / Fall), then re-enter 0x120 a SECOND time midair without
    # landing (proving LA-Bit[115] HasUsedHover is cleared).
    set_u8(emu, settings, S_MEWTWO_HOVER, 1)
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 14)
    emu.neutral(0)
    CT.wait_game_frames(emu, 2)
    emu.hold(0, buttons=["X"], stick=DOWN)
    CT.wait_cond(emu, "Mewtwo 1st hover 0x120", lambda: p1.status() == 0x120, timeout=25, step=1)
    emu.hold(0, buttons=["X"], stick=NEUTRAL)
    CT.wait_game_frames(emu, 80)
    st_hold, flag_hold, rem_hold, used_hold = p1.status(), p1.la_bit(97), p1.ra_int(2), p1.la_bit(115)
    emu.neutral(0)
    CT.wait_cond(emu, "Mewtwo exit 1st hover", lambda: p1.status() != 0x120, timeout=30, step=1)
    st_exit = p1.status()
    CT.wait_game_frames(emu, 4)
    # Re-enter hover midair without touching ground!
    emu.hold(0, buttons=["X"], stick=DOWN)
    CT.wait_cond(emu, "Mewtwo 2nd midair hover 0x120", lambda: p1.status() == 0x120, timeout=30, step=1)
    emu.hold(0, buttons=["X"], stick=NEUTRAL)
    CT.wait_game_frames(emu, 15)
    st2, flag2, rem2, used2 = p1.status(), p1.la_bit(97), p1.ra_int(2), p1.la_bit(115)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 140)
    if st_hold != 0x120 or flag_hold != 1 or rem_hold < 55 or used_hold != 0:
        fails.append(f"hover 1st@80f: st={st_hold:#x} flag={flag_hold} rem={rem_hold} used={used_hold}")
    if st_exit not in (0x11E, 0x0E, 0x16):
        fails.append(f"hover exit: st={st_exit:#x}")
    if st2 != 0x120 or flag2 != 1 or rem2 < 55 or used2 != 0:
        fails.append(f"hover 2nd midair: st={st2:#x} flag={flag2} rem={rem2} used={used2}")

    # 3. Savestate preservation: Default preserves saved Full Shadow Ball; locked Half enforces 3.
    set_u8(emu, settings, S_MEWTWO_HOVER, 0)
    set_u8(emu, settings, S_MEWTWO_BALL, 3)
    set_u8(emu, settings, S_MEWTWO_BALL, 0)
    CT.dpad_save(emu)
    CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=50)
    ss_spent = p1.la_int(65)
    CT.dpad_load(emu)
    ss_def = p1.la_int(65)
    set_u8(emu, settings, S_MEWTWO_BALL, 2)
    CT.dpad_load(emu)
    ss_lock = p1.la_int(65)
    if ss_spent != -1 or ss_def != 6 or ss_lock != 3:
        fails.append(f"mewtwo savestate: spent={ss_spent} def={ss_def} lock={ss_lock}")

    notes.append(
        f"mewtwo: ball lvls={(lvl_e, lvl_h, lvl_f)} dmgs={(dmg_e, dmg_h, dmg_f)}; "
        f"hover 1st@80f(st={st_hold:#x}, bit97={flag_hold}, rem={rem_hold}, bit115={used_hold}, exit={st_exit:#x}) "
        f"2nd(st={st2:#x}, bit97={flag2}, rem={rem2}, bit115={used2}); ss(def={ss_def}, lock={ss_lock})"
    )
    return fails


def sc_lucario(emu, notes):
    settings, p1, cpu = prepare(emu, "lucario")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=16.0)

    # 1. Aura Charges: 0 / 1 / 2 -> LA-Float[27] 0 / 60 / 120.
    auras = []
    for val in (1, 2, 3):
        set_u8(emu, settings, S_LUCARIO_AURA, val)
        auras.append(round(p1.la_float(27), 1))
    if auras != [0.0, 60.0, 120.0]:
        fails.append(f"aura: {auras}")

    # 2. Aura Sphere: Empty (-1) vs Full (4).
    set_u8(emu, settings, S_LUCARIO_SPHERE, 1)
    lvl_e = (p1.la_int(64), p1.la_int(65), p1.la_int(66))
    dmg_e = fire_two_tap(emu, p1, cpu, direct_start_shoot=True)
    set_u8(emu, settings, S_LUCARIO_SPHERE, 2)
    lvl_f = (p1.la_int(64), p1.la_int(65), p1.la_int(66))
    dmg_f = fire_two_tap(emu, p1, cpu, direct_start_shoot=True)
    if lvl_e != (0, -1, -1) or lvl_f != (16, 4, 4) or not (dmg_f > dmg_e + 5.0):
        fails.append(f"sphere: lvls={(lvl_e, lvl_f)} dmgs={(dmg_e, dmg_f)}")

    # 3. Savestate preservation: Default restores saved Aura=120 + Full Sphere; locked overrides re-apply.
    set_u8(emu, settings, S_LUCARIO_AURA, 3)    # 120.0
    set_u8(emu, settings, S_LUCARIO_SPHERE, 2)  # Full (4)
    set_u8(emu, settings, S_LUCARIO_AURA, 0)
    set_u8(emu, settings, S_LUCARIO_SPHERE, 0)
    CT.dpad_save(emu)
    fire_two_tap(emu, p1, cpu, direct_start_shoot=True)
    CT.dpad_load(emu)
    ss_def = (round(p1.la_float(27), 1), p1.la_int(65))
    set_u8(emu, settings, S_LUCARIO_AURA, 2)    # 60.0
    set_u8(emu, settings, S_LUCARIO_SPHERE, 1)  # Empty (-1)
    CT.dpad_load(emu)
    ss_lock = (round(p1.la_float(27), 1), p1.la_int(65))
    if ss_def != (120.0, 4) or ss_lock != (60.0, -1):
        fails.append(f"lucario savestate: def={ss_def} lock={ss_lock}")

    notes.append(f"lucario: aura={auras}; sphere lvls={(lvl_e, lvl_f)} dmgs={(dmg_e, dmg_f)}; ss(def={ss_def}, lock={ss_lock})")
    return fails


def sc_wario(emu, notes):
    settings, p1, cpu = prepare(emu, "wario")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=10.0)

    readings = []
    dmgs = []
    for val in (1, 2, 3):
        set_u8(emu, settings, S_WARIO_WAFT, val)
        readings.append((p1.la_int(65), p1.la_int(66), p1.la_bit(63)))
        CT.place_cpu(emu, p1, cpu, close_dist=10.0)
        dmgs.append(CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70))
    # ft_wario ticks LA-Basic[65] by +1 on the frame after apply before Python reads.
    ok_vars = (
        0 <= readings[0][0] <= 2 and readings[0][1:] == (0, 0)
        and 1800 <= readings[1][0] <= 1802 and readings[1][1:] == (2, 0)
        and readings[2] == (3600, 3, 1)
    )
    if not ok_vars:
        fails.append(f"waft vars: {readings}")
    if not (dmgs[0] < dmgs[1] < dmgs[2]):
        fails.append(f"waft dmgs: {dmgs}")

    # Savestate preservation: Default preserves saved Half waft after spending; Full re-applies on load.
    set_u8(emu, settings, S_WARIO_WAFT, 2)  # Half (stage 2)
    set_u8(emu, settings, S_WARIO_WAFT, 0)  # Default
    CT.dpad_save(emu)
    CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=60)
    ss_spent = p1.la_int(66)
    CT.dpad_load(emu)
    ss_def = p1.la_int(66)
    set_u8(emu, settings, S_WARIO_WAFT, 3)  # Full (stage 3)
    CT.dpad_load(emu)
    ss_lock = (p1.la_int(66), p1.la_bit(63))
    if ss_spent != 0 or ss_def != 2 or ss_lock != (3, 1):
        fails.append(f"wario savestate: spent={ss_spent} def={ss_def} lock={ss_lock}")
    notes.append(f"wario waft: vars={readings} dmgs={dmgs} ss(def={ss_def}, lock={ss_lock})")
    return fails


def sc_dk(emu, notes):
    settings, p1, cpu = prepare(emu, "donkey_kong")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=12.0)

    set_u8(emu, settings, S_DK_PUNCH, 1)
    lvl_e = p1.la_int(64)
    # Tap B to start windup, then tap B to throw uncharged punch.
    dmg_e = fire_two_tap(emu, p1, cpu, hold_statuses=(0x112, 0x117))
    CT.place_cpu(emu, p1, cpu, close_dist=12.0)
    set_u8(emu, settings, S_DK_PUNCH, 2)
    lvl_f = p1.la_int(64)
    dmg_f = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=60)
    if (lvl_e, lvl_f) != (0, 10) or not (dmg_f > dmg_e + 5.0):
        fails.append(f"dk punch: lvls={(lvl_e, lvl_f)} dmgs={(dmg_e, dmg_f)}")
    notes.append(f"donkey_kong: lvls={(lvl_e, lvl_f)} dmgs={(dmg_e, dmg_f)}")
    return fails


def sc_ivysaur(emu, notes):
    settings, p1, cpu = prepare(emu, "ivysaur")
    fails = []
    vals = []
    for v in (1, 2, 3):
        set_u8(emu, settings, S_IVYSAUR_SOLAR, v)
        vals.append(round(p1.la_float(27), 1))
    CT.place_cpu(emu, p1, cpu, close_dist=8.0)
    dmg = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=4, wait_f=80)
    if vals != [0.0, 13.0, 26.0] or dmg < 10.0:
        fails.append(f"solarbeam: vals={vals} dmg={dmg}")
    notes.append(f"ivysaur: vals={vals} full_dmg={dmg}")
    return fails


def sc_rob(emu, notes):
    settings, p1, cpu = prepare(emu, "rob")
    fails = []

    # 1. Gyro Charge: Empty (0.0) vs Full (90.0 = FitRobot IC-Float[4047]).
    # Save state BEFORE spawning any Gyro so both shots (and Robo Beam after)
    # start with a clean stage and no lingering Gyro item.
    CT.place_cpu(emu, p1, cpu, close_dist=20.0)
    gyro_sav = state_path("rob-gyro")
    emu.h.call("save_state", path=gyro_sav)
    set_u8(emu, settings, S_ROB_GYRO, 1)
    g_e = round(p1.la_float(20), 1)
    # Empty Gyro enters SpecialLwHold (0x11b); a second B tap fires it uncharged.
    dmg_e = fire_two_tap(emu, p1, cpu, hold_statuses=(0x11B,), first_stick=DOWN)

    emu.h.call("load_state", path=gyro_sav)
    CT.wait_game_frames(emu, 2)
    set_u8(emu, settings, S_ROB_GYRO, 2)
    g_f = round(p1.la_float(20), 1)
    # Full Gyro (>= 90.0) transitions directly from 0x115 to SpecialLwShoot (0x11d) on one Down-B tap.
    dmg_f = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=DOWN, hold_f=3, wait_f=70)
    if not (0.0 <= g_e <= 2.0 and 89.0 <= g_f <= 91.0) or not (dmg_f > dmg_e + 0.5):
        fails.append(f"rob gyro: vars={(g_e, g_f)} dmgs={(dmg_e, dmg_f)}")

    # 2. Robo Beam: Uncharged (0.0) / Normal (300.0) / Super (900.0).
    # Reload clean state before each shot so CPU never gets knocked off FD ledge.
    beam_vars = []
    beam_dmgs = []
    for v in (1, 2, 3):
        emu.h.call("load_state", path=gyro_sav)
        CT.wait_game_frames(emu, 2)
        set_u8(emu, settings, S_ROB_GYRO, 0)
        set_u8(emu, settings, S_ROB_BEAM, v)
        CT.wait_game_frames(emu, 4)
        beam_vars.append((round(p1.la_float(19), 1), p1.la_int(70)))
        beam_dmgs.append(CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=55))
    ok_beam = (
        0.0 <= beam_vars[0][0] <= 2.0
        and 300.0 <= beam_vars[1][0] <= 302.0
        and 899.0 <= beam_vars[2][0] <= 901.0
    )
    if not ok_beam or not (beam_dmgs[0] < beam_dmgs[1] < beam_dmgs[2]):
        fails.append(f"rob beam: vars={beam_vars} dmgs={beam_dmgs}")

    # 3. Air Boosts: Unlimited keeps LA-Basic[75]=3 and LA-Bit[111]=0 across 4 air boosts.
    set_u8(emu, settings, S_ROB_BOOSTS, 1)
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 10)
    emu.neutral(0)
    boosts = []
    for _ in range(4):
        emu.hold(0, buttons=["B"], stick=UP)
        CT.wait_game_frames(emu, 5)
        emu.neutral(0)
        CT.wait_game_frames(emu, 14)
        boosts.append((p1.la_int(75), p1.la_bit(111)))
    CT.wait_idle(emu, p1, 140)
    if any(b != (3, 0) for b in boosts):
        fails.append(f"rob boosts: {boosts}")

    # 4. Savestate preservation: Default preserves saved Super Beam & Full Gyro after firing;
    # locked AirBoosts + Normal Beam re-apply on quick savestate load.
    set_u8(emu, settings, S_ROB_BOOSTS, 0)
    set_u8(emu, settings, S_ROB_GYRO, 2)
    set_u8(emu, settings, S_ROB_BEAM, 3)
    set_u8(emu, settings, S_ROB_GYRO, 0)
    set_u8(emu, settings, S_ROB_BEAM, 0)
    CT.dpad_save(emu)
    CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=45)
    ss_spent = round(p1.la_float(19), 1)
    CT.dpad_load(emu)
    ss_def = (round(p1.la_float(19), 1), round(p1.la_float(20), 1))
    set_u8(emu, settings, S_ROB_BOOSTS, 1)
    set_u8(emu, settings, S_ROB_BEAM, 2)
    CT.dpad_load(emu)
    ss_lock = (p1.la_int(75), round(p1.la_float(19), 1))
    if ss_spent > 100.0 or ss_def[0] < 890.0 or ss_def[1] < 88.0 or ss_lock[0] != 3 or not (298.0 <= ss_lock[1] <= 305.0):
        fails.append(f"rob savestate: spent={ss_spent} def={ss_def} lock={ss_lock}")

    notes.append(
        f"rob: boosts={boosts}; gyro={(g_e, g_f)}/{(dmg_e, dmg_f)}; beam vars={beam_vars} dmgs={beam_dmgs}; "
        f"ss(def={ss_def}, lock={ss_lock})"
    )
    return fails


def sc_lucas(emu, notes):
    settings, p1, cpu = prepare(emu, "lucas")
    fails = []
    # Match start has LA-Basic[53] == 0: verify Lucas Offense Up = Charged bumps
    # LA-Basic[53] from 0 -> 1 so Wait1 does not clear RA-Bit[107]/LA-Float[61],
    # and verify Off (16.0) vs Charged (20.0) F-Smash right from match start.
    CT.place_cpu(emu, p1, cpu, close_dist=10.0)
    CT.dpad_save(emu)

    set_u8(emu, settings, S_LUCAS_OFFENSE, 1)
    off_state = (p1.ra_bit(107), round(p1.la_float(61), 1))
    toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    dmg_off = CT.fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=60)

    CT.dpad_load(emu)
    serial_before = p1.la_int(53)
    set_u8(emu, settings, S_LUCAS_OFFENSE, 2)
    serial_after = p1.la_int(53)
    on_state = (p1.ra_bit(107), round(p1.la_float(61), 1))
    toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    dmg_on = CT.fire_and_measure_damage(emu, p1, cpu, buttons=(), c=toward, hold_f=3, wait_f=60)

    if off_state != (0, 0.0) or serial_after < 1 or on_state != (1, 120.0) or not (dmg_on > dmg_off + 1.0):
        fails.append(
            f"lucas offense: serial={serial_before}->{serial_after} "
            f"off={off_state}({dmg_off}) on={on_state}({dmg_on})"
        )

    # Savestate preservation under Default + Off re-apply: save while Charged,
    # switch to Default, spend Offense Up by whiffing F-Smash away from CPU
    # (Subr@0x28544 refunds RA-Bit[107] on hit, whereas a whiff consumes it to 0),
    # then quick-load -> Default preserves (1, 120.0) and Off re-applies (0, 0.0).
    CT.dpad_load(emu)
    set_u8(emu, settings, S_LUCAS_OFFENSE, 2)
    set_u8(emu, settings, S_LUCAS_OFFENSE, 0)
    CT.dpad_save(emu)
    away = LEFT if cpu.posture()["x"] >= p1.posture()["x"] else RIGHT
    CT.run_pad_script(emu, [CT._pad_step(cstick=away, hold=4)])
    CT.wait_cond(emu, "Lucas AttackS4 whiff", lambda: p1.status() in (0x2A, 0x2B, 0x2C), timeout=30, step=1)
    CT.wait_idle(emu, p1, 120)
    ss_spent = p1.ra_bit(107)
    CT.dpad_load(emu)
    ss_def = (p1.ra_bit(107), round(p1.la_float(61), 1))
    set_u8(emu, settings, S_LUCAS_OFFENSE, 1)  # Off
    CT.dpad_load(emu)
    ss_off = (p1.ra_bit(107), round(p1.la_float(61), 1))
    if ss_spent != 0 or ss_def != (1, 120.0) or ss_off != (0, 0.0):
        fails.append(f"lucas savestate: spent={ss_spent} def={ss_def} off={ss_off}")

    notes.append(
        f"lucas: serial={serial_before}->{serial_after} "
        f"off={off_state} fsmash={dmg_off}; charged={on_state} fsmash={dmg_on}; ss(def={ss_def}, off={ss_off})"
    )
    return fails


def sc_squirtle(emu, notes):
    settings, p1, cpu = prepare(emu, "squirtle")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=16.0)

    set_u8(emu, settings, S_SQUIRTLE_GUN, 1)
    v_e = p1.la_int(64)
    set_u8(emu, settings, S_SQUIRTLE_GUN, 2)
    v_f = p1.la_int(64)
    x0 = cpu.posture()["x"]
    emu.hold(0, buttons=["B"])
    CT.wait_game_frames(emu, 4)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 120)
    dx = round(abs(cpu.posture()["x"] - x0), 2)
    if (v_e, v_f) != (0, 60) or dx < 10.0:
        fails.append(f"squirtle: vars={(v_e, v_f)} push_dx={dx}")
    notes.append(f"squirtle: vars={(v_e, v_f)} push_dx={dx}")
    return fails


def sc_sheik(emu, notes):
    settings, p1, cpu = prepare(emu, "sheik")
    fails = []
    CT.place_cpu(emu, p1, cpu, close_dist=16.0)

    set_u8(emu, settings, S_SHEIK_NEEDLES, 1)
    s0 = (p1.la_int(64), p1.la_bit(61))
    set_u8(emu, settings, S_SHEIK_NEEDLES, 2)
    s6 = (p1.la_int(64), p1.la_bit(61))
    dmg = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), hold_f=3, wait_f=60)
    if s0 != (0, 0) or s6 != (6, 1) or dmg < 10.0:
        fails.append(f"sheik: s0={s0} s6={s6} dmg={dmg}")
    notes.append(f"sheik: s0={s0} s6={s6} 6-needle_dmg={dmg}")
    return fails


def sc_dedede(emu, notes):
    pos = len(emu.log_text())
    settings, p1, cpu = prepare(emu, "king_dedede")
    fails = []

    # Check plugin found & validated FitDedede.pac Waddle weights.
    set_u8(emu, settings, S_DEDEDE_WADDLE, 3)  # Gordo
    found = DEDEDE_FOUND_RE.findall(emu.log_text()[pos:])
    if not found:
        fails.append("Dedede weights table not validated in log")
        notes.append("dedede: weights table NOT found")
        return fails
    w_addr = int(found[-1][0], 16)

    def read_weights():
        return [round(v, 1) for v in struct.unpack(">4f", emu.read(w_addr, 16))]

    w_gordo = read_weights()
    cd_gordo = p1.la_int(65)
    # Throw Side-B Gordo toward CPU and verify real Gordo damage (>= 15).
    CT.place_cpu(emu, p1, cpu, close_dist=20.0)
    toward = RIGHT if cpu.posture()["x"] >= p1.posture()["x"] else LEFT
    dmg_gordo = CT.fire_and_measure_damage(emu, p1, cpu, buttons=("B",), stick=toward, hold_f=4, wait_f=70)
    cd_after = p1.la_int(65)

    set_u8(emu, settings, S_DEDEDE_WADDLE, 2)  # Waddle Doo
    w_doo = read_weights()
    set_u8(emu, settings, S_DEDEDE_WADDLE, 1)  # Waddle Dee
    w_dee = read_weights()
    set_u8(emu, settings, S_DEDEDE_WADDLE, 3)  # Leave patched on Gordo before savestate + exit!
    CT.dpad_save(emu)
    CT.dpad_load(emu)
    w_ss = read_weights()
    cd_ss = p1.la_int(65)

    # Exit match and verify detach restores original bytes (35, 10, 4, 0) in log.
    nav.finish_training(emu)
    detached = DEDEDE_DETACH_RE.findall(emu.log_text()[pos:])
    if w_gordo != [0.0, 0.0, 100.0, 0.0] or w_doo != [0.0, 100.0, 0.0, 0.0] or w_dee != [100.0, 0.0, 0.0, 0.0]:
        fails.append(f"weights: dee={w_dee} doo={w_doo} gordo={w_gordo}")
    if w_ss != [0.0, 0.0, 100.0, 0.0] or cd_ss != 0:
        fails.append(f"dedede savestate: weights={w_ss} cd={cd_ss}")
    if cd_gordo != 0 or cd_after != 0 or dmg_gordo < 12.0:
        fails.append(f"gordo throw: cd=({cd_gordo},{cd_after}) dmg={dmg_gordo}")
    if not detached:
        fails.append("detach did not log restoring Dedede weights")
    notes.append(
        f"dedede: addr={w_addr:#x} dee={w_dee} doo={w_doo} gordo={w_gordo} "
        f"gordo_dmg={dmg_gordo} cd={cd_after} ss_ok={w_ss == [0.0, 0.0, 100.0, 0.0]} detach_restored={bool(detached)}"
    )
    return fails


def sc_snake(emu, notes):
    settings, p1, _ = prepare(emu, "snake")
    fails = []

    # 1. Tranq Ammo: Unlimited keeps LA-Basic[93] == 0 across 4 Neutral-B shots.
    set_u8(emu, settings, S_SNAKE_TRANQ, 1)
    tranq = []
    for _ in range(4):
        emu.hold(0, buttons=["B"])
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 100)
        tranq.append(p1.la_int(93))
    if any(t != 0 for t in tranq):
        fails.append(f"snake tranq: {tranq}")

    # 2. Cypher: Unlimited keeps LA-Float[52] == 0.0 and LA-Bit[61] == 0 across 2 air Cyphers.
    set_u8(emu, settings, S_SNAKE_CYPHER, 1)
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 12)
    emu.neutral(0)
    cypher = []
    for _ in range(2):
        emu.hold(0, buttons=["B"], stick=UP)
        CT.wait_game_frames(emu, 14)
        emu.neutral(0)
        CT.wait_game_frames(emu, 4)
        # Cancel Cypher with A (aerial) or shield/fall.
        emu.hold(0, buttons=["A"])
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
        CT.wait_game_frames(emu, 18)
        cypher.append((round(p1.la_float(52), 1), p1.la_bit(61)))
    CT.wait_idle(emu, p1, 160)
    if any(c != (0.0, 0) for c in cypher):
        fails.append(f"snake cypher: {cypher}")

    notes.append(f"snake: tranq={tranq}; cypher={cypher}")
    return fails


def sc_ganondorf(emu, notes):
    settings, p1, _ = prepare(emu, "ganondorf")
    fails = []
    set_u8(emu, settings, S_GANON_FLOAT, 1)

    # Full-hop, enter Dead Man's Float (Neutral-B in air -> status 0x112),
    # hold for 55 frames (past normal timer cap with LA-Basic[98] pinned at 0),
    # cancel float with double-jump (X), then re-enter Dead Man's Float a SECOND
    # time midair without landing!
    emu.hold(0, buttons=["X"])
    CT.wait_game_frames(emu, 14)
    emu.neutral(0)
    CT.wait_game_frames(emu, 2)
    emu.hold(0, buttons=["B"])
    CT.wait_cond(emu, "Ganon 1st float (0x112)", lambda: p1.status() == 0x112, timeout=40, step=1)
    emu.neutral(0)
    CT.wait_game_frames(emu, 55)
    st1, cnt1 = p1.status(), p1.la_int(98)
    # Double-jump cancels Dead Man's Float out of 0x112 into JumpAerial; then
    # press B midair to re-enter Dead Man's Float without ever touching ground.
    emu.hold(0, buttons=["X"])
    CT.wait_cond(emu, "Ganon exit float via double-jump", lambda: p1.status() != 0x112, timeout=30, step=1)
    emu.neutral(0)
    CT.wait_game_frames(emu, 6)
    emu.hold(0, buttons=["B"])
    CT.wait_cond(emu, "Ganon 2nd midair float (0x112)", lambda: p1.status() == 0x112, timeout=40, step=1)
    st2, cnt2 = p1.status(), p1.la_int(98)
    # Exit float with Nair (A) so Ganon lands cleanly back to Wait.
    emu.hold(0, buttons=["A"])
    CT.wait_game_frames(emu, 4)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 140)

    if st1 != 0x112 or cnt1 > 1 or st2 != 0x112 or cnt2 > 1:
        fails.append(f"ganon float: 1st=({st1:#x},{cnt1}) 2nd=({st2:#x},{cnt2})")
    notes.append(f"ganondorf float: 1st@55f(st={st1:#x}, la98={cnt1}) 2nd(st={st2:#x}, la98={cnt2})")
    return fails


def sc_olimar(emu, notes):
    pos = len(emu.log_text())
    settings, p1, _ = prepare(emu, "olimar")
    fails = []

    # Validate FitPikmin.pac pullOutParam row-0 discovery.
    set_u8(emu, settings, S_OLIMAR_PIKMIN, 5)  # Purple
    found = OLIMAR_FOUND_RE.findall(emu.log_text()[pos:])
    if not found:
        fails.append("Olimar Pikmin weights table not validated in log")
        notes.append("olimar: weights table NOT found")
        return fails
    w_addr = int(found[-1], 16)

    def read_weights():
        return [round(v, 1) for v in struct.unpack(">5f", emu.read(w_addr, 20))]

    # Save quick lab savestate while Olimar has 3 Pikmin (1 free pluck slot), then
    # reload and pluck all 5 colors (Red=0, Yellow=1, Blue=2, White=3, Purple=4).
    set_u8(emu, settings, S_OLIMAR_PIKMIN, 0)
    w_def = read_weights()
    CT.dpad_save(emu)

    plucked = []
    for choice in (1, 2, 3, 4, 5):
        CT.dpad_load(emu)
        set_u8(emu, settings, S_OLIMAR_PIKMIN, choice)
        emu.hold(0, buttons=["B"])
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 80)
        plucked.append(p1.la_int(64))

    # Consecutive identical plucks: Olimar starts with 3 Pikmin (P+ cap is 4).
    # Walk to the left ledge, throw 1 Pikmin offstage so it dies (leaving 2 Pikmin
    # and 2 open pluck slots), then pluck Purple twice in a row. This verifies
    # 0x80a6b9a0's f30==0 fallback to LA-Basic[64] keeps consecutive plucks locked.
    CT.dpad_load(emu)
    set_u8(emu, settings, S_OLIMAR_PIKMIN, 5)  # Purple (4)
    w_purple = read_weights()
    emu.hold(0, stick=LEFT)
    CT.wait_cond(emu, "Olimar near left ledge", lambda: p1.posture()["x"] < -70.0, timeout=120, step=2)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 60)
    art0 = p1.active_articles()
    emu.hold(0, buttons=["B"], stick=LEFT)
    CT.wait_game_frames(emu, 4)
    emu.neutral(0)
    CT.wait_idle(emu, p1, 60)
    CT.wait_cond(emu, "thrown Pikmin dies offstage", lambda: p1.active_articles() < art0, timeout=240, step=2)
    consec = []
    for _ in range(2):
        emu.hold(0, buttons=["B"])
        CT.wait_game_frames(emu, 4)
        emu.neutral(0)
        CT.wait_idle(emu, p1, 80)
        consec.append((p1.la_int(64), p1.la_int(65)))

    # Leave patched on Purple and exit Training: detach must restore 1.0/1.0/1.0/1.0/1.0.
    nav.finish_training(emu)
    detached = OLIMAR_DETACH_RE.findall(emu.log_text()[pos:])

    if w_def != [1.0, 1.0, 1.0, 1.0, 1.0] or w_purple != [0.0, 0.0, 0.0, 0.0, 1.0]:
        fails.append(f"olimar weights: def={w_def} purple={w_purple}")
    if plucked != [0, 1, 2, 3, 4]:
        fails.append(f"olimar 5-color pluck: got LA-Basic[64]={plucked}, want [0,1,2,3,4]")
    if consec[-1] != (4, 4):
        fails.append(f"olimar consecutive purple plucks: {consec} (want last (4, 4))")
    if not detached:
        fails.append("detach did not log restoring Olimar Pikmin weights")
    notes.append(
        f"olimar: addr={w_addr:#x} def={w_def} plucked={plucked} consec={consec} detach_restored={bool(detached)}"
    )
    return fails


SCENARIOS = [
    ("peach", sc_peach),
    ("gnw", sc_gnw),
    ("samus", sc_samus),
    ("mewtwo", sc_mewtwo),
    ("lucario", sc_lucario),
    ("wario", sc_wario),
    ("donkey_kong", sc_dk),
    ("ivysaur", sc_ivysaur),
    ("rob", sc_rob),
    ("lucas", sc_lucas),
    ("squirtle", sc_squirtle),
    ("sheik", sc_sheik),
    ("dedede", sc_dedede),
    ("snake", sc_snake),
    ("ganondorf", sc_ganondorf),
    ("olimar", sc_olimar),
]


def cmd_test_fighter_mechanics(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-fighter-mechanics")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--keep-going", action="store_true")
    opts = ap.parse_args(rest)
    label = "test-fighter-mechanics"
    known = [name for name, _ in SCENARIOS]
    if opts.only:
        unknown = [n for n in opts.only if n not in known]
        if unknown:
            E.die(f"no scenario named {unknown}; choose from {', '.join(known)}")
    selected = [(n, fn) for n, fn in SCENARIOS if not opts.only or n in opts.only]

    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    notes = []
    failed_scenarios = []

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            for name, fn in selected:
                s0 = time.monotonic()
                try:
                    fails = fn(emu, notes)
                except (Timeout, nav.NavError, HarnessError, CT.ChargeError, LT.LabError, ValueError) as exc:
                    fails = [f"{type(exc).__name__}: {exc}"]
                dt = time.monotonic() - s0
                if fails:
                    failed_scenarios.append((name, fails))
                    print(f"FAIL {name} ({dt:.1f}s): {'; '.join(fails)}")
                    if not opts.keep_going:
                        why = f"{name}: {fails[0]}"
                        break
                else:
                    print(f"ok   {name} ({dt:.1f}s)")
            if nav.scene(emu)[0] == "scMelee":
                try:
                    nav.finish_training(emu)
                except (Timeout, nav.NavError, HarnessError):
                    pass
    except (OSError, HarnessError, Timeout, nav.NavError) as exc:
        why = f"boot: {type(exc).__name__}: {exc}"

    print("\n--- fighter mechanics measurements ---")
    for line in notes:
        print(" ", line)
    bad = L.problems(E.read_log(log_path)) if log_path else []
    for line in bad:
        print(f"problem: {line}")
    if not why and failed_scenarios:
        why = f"{len(failed_scenarios)} scenario(s) failed: {', '.join(n for n, _ in failed_scenarios)}"
        print("failed --only:", " ".join(n for n, _ in failed_scenarios))
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
