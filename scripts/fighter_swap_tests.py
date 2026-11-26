"""Emulator tests for Mid-Session Fighter Swap in Lab
(src/extra/fighter_swap.cpp, run through emu.py).

  test-fighter-swap REL [--only NAME ...] [--list]

Scenarios:
  1. p1-swap-and-costume: real-controller General > Fighters (row 13) swap of
     P1 Mario -> Falco (costume 2) -> Peach -> Kirby -> Ice Climbers; checks
     gmSelCharData CSS icons, costume file mapping, lockPercents() re-apply,
     post-swap auto-save + manual D-pad savestate save & load, and tracker
     s_lost == 0.
  2. cpu-swap-mechanics-rng: CPU swap Fox -> Dedede -> Mewtwo -> Peach -> Fox
     with Dedede Waddle (Gordo), Mewtwo Hover, and Peach Turnip/F-Smash RNG
     active; verifies live application on swapped-in fighters, pre-swap
     .pac restoration before unload, and zero heap corruption on match exit.
  3. simultaneous-swap: simultaneous P1 + CPU swap (Marth c1 + Sheik c2) in a
     single menu visit; verifies both swap cleanly, CSS icons update, and
     post-swap savestate save & load pass.
  4. active-scenario-and-recording-cleanup: swap while a Scenario Preset is
     active (verifies clean deactivation & snapshot restore) and while
     Recording / CPU Control is active (verifies positions deleted and CPU
     m_controllerNo restored).
  5. stress-swaps: 12 consecutive swaps across heavy/item/sub-fighter
     characters (Kirby, Ice Climbers, Zelda, Sheik, Samus, Zero Suit Samus,
     Peach, Snake, Wario, Charizard, Roy, Mewtwo), checking heap stability,
     post-stress savestate save & load, and clean match exit.
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import charge_tests as CHG  # noqa: E402
import cpu_tests as CT  # noqa: E402
import emu as E  # noqa: E402
import fighter_mechanics_tests as FM  # noqa: E402
import fighter_set_tests as FS  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import record_tests as RT  # noqa: E402
import rng_tests as RNG  # noqa: E402
import scenario_presets_tests as SP  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# Alphabetical roster matching src/extra/fighter_swap_core.h (45 entries).
ROSTER = [
    ("Bowser",           0x0C, 0x0C),
    ("Captain Falcon",   0x0A, 0x0A),
    ("Charizard",        0x1E, 0x2A),
    ("Dedede",           0x23, 0x1F),
    ("Diddy Kong",       0x1C, 0x1A),
    ("Donkey Kong",      0x01, 0x01),
    ("Falco",            0x15, 0x13),
    ("Fox",              0x07, 0x07),
    ("Ganondorf",        0x16, 0x14),
    ("Giga Bowser",      0x2C, 0x0C),
    ("Ice Climbers",     0x10, 0x10),
    ("Ike",              0x25, 0x21),
    ("Ivysaur",          0x22, 0x2C),
    ("Jigglypuff",       0x27, 0x23),
    ("Kirby",            0x06, 0x06),
    ("Knuckles",         0x35, 0x30),
    ("Link",             0x02, 0x02),
    ("Lucario",          0x24, 0x20),
    ("Lucas",            0x1B, 0x19),
    ("Luigi",            0x09, 0x09),
    ("Mario",            0x00, 0x00),
    ("Marth",            0x13, 0x11),
    ("Meta Knight",      0x18, 0x16),
    ("Mewtwo",           0x33, 0x2E),
    ("Mr. Game & Watch", 0x14, 0x12),
    ("Ness",             0x0B, 0x0B),
    ("Olimar",           0x1A, 0x18),
    ("Peach",            0x0D, 0x0D),
    ("Pikachu",          0x08, 0x08),
    ("Pit",              0x19, 0x17),
    ("R.O.B.",           0x26, 0x22),
    ("Roy",              0x32, 0x2D),
    ("Samus",            0x03, 0x03),
    ("Sheik",            0x0F, 0x0F),
    ("Snake",            0x2A, 0x26),
    ("Sonic",            0x2B, 0x27),
    ("Sopo",             0x11, 0x10),
    ("Squirtle",         0x20, 0x2B),
    ("Toon Link",        0x28, 0x24),
    ("Wario",            0x17, 0x15),
    ("Wario-Man",        0x2D, 0x15),
    ("Wolf",             0x29, 0x25),
    ("Yoshi",            0x05, 0x05),
    ("Zelda",            0x0E, 0x0E),
    ("Zero Suit Samus",  0x04, 0x04),
]
ROSTER_IDX = {name: i for i, (name, _, _) in enumerate(ROSTER)}
ROSTER_INFO = {name: (kind, icon) for name, kind, icon in ROSTER}

# FighterSwapDebugBlock (src/extra/fighter_swap.cpp, 0x20 bytes).
SWAP_MAGIC = 0x544D4653  # "TMFS"
SWAP_BLOCK_RE = re.compile(r"swap: debug block ([0-9a-f]{8})")
SWAP_FMT = ">5I4B2b6B"
SWAP_SIZE = 0x20

# General page row 13 is Fighters (src/main/ui/pages/general.cpp).
GENERAL_FIGHTERS_ROW = 13

# gmSelCharData offsets (src/menu/game/training.cpp).
SEL_ICONS = 0x08
SEL_PLAYERS = 0xB8
PLAYER_SIZE = 0x5C

# Dedede common data (ftKind 0x20, FitDedede.pac data+0x80 -> weights at +0x18).
FT_COMMON_DATA = 0x80B87FD0
FT_DEDEDE = 0x20
DEDEDE_TABLE_OFF = 0x80
DEDEDE_WEIGHTS_OFF = 0x18

DEDEDE_FOUND_RE = FM.DEDEDE_FOUND_RE
DEDEDE_DETACH_RE = FM.DEDEDE_DETACH_RE
AUTO_SAVE_RE = re.compile(r"savestate: match-start state taken at frame (\d+)")
SWAP_COMPLETE_RE = re.compile(r"swap: complete after (\d+) frames")
SCEN_DEACT_RE = re.compile(r"scenarios: deactivated for fighter swap")
SCEN_RESTORE_RE = re.compile(r"scenarios: restored preset (\d+) after fighter swap")
PEACH_RESTORE_RE = re.compile(r"character rng: pre-swap restore item odds ([0-9a-f]{8})")


class SwapError(RuntimeError):
    pass


class SwapBlock:
    """Reads FighterSwapDebugBlock and drives FighterSwapMenuState in menuScratch()."""

    def __init__(self, emu):
        self.emu = emu
        self.addr = None
        self.scratch = None

    def find(self):
        found = SWAP_BLOCK_RE.findall(self.emu.log_text())
        if not found:
            raise SwapError("no swap debug block line in the log")
        self.addr = int(found[-1], 16)
        if self.emu.u32(self.addr) != SWAP_MAGIC:
            raise SwapError(f"bad swap debug magic at {self.addr:#x}")
        self.scratch = self.emu.u32(self.addr + 4)
        if not self.emu.is_ram(self.scratch):
            raise SwapError(f"bad swap scratch addr {self.scratch:#x}")

    def read(self):
        raw = self.emu.read(self.addr, SWAP_SIZE)
        v = struct.unpack(SWAP_FMT, raw)
        return {
            "magic": v[0],
            "scratchAddr": v[1],
            "swapCount": v[2],
            "completedCount": v[3],
            "refusalCount": v[4],
            "inProgress": v[5],
            "lastRefusal": v[6],
            "costumeHelperOk": v[7],
            "altCostumeOk": v[8],
            "p1Slot": v[9],
            "cpuSlot": v[10],
            "p1MaxCostume": v[11],
            "cpuMaxCostume": v[12],
            "lastP1Kind": v[13],
            "lastP1ColorFile": v[14],
            "lastCpuKind": v[15],
            "lastCpuColorFile": v[16],
        }

    def read_scratch(self):
        return tuple(self.emu.read(self.scratch, 4))

    def write_scratch(self, p1_idx, p1_cos, cpu_idx, cpu_cos):
        payload = bytes([p1_idx & 0xFF, p1_cos & 0xFF, cpu_idx & 0xFF, cpu_cos & 0xFF])
        self.emu.h.call("write_mem", addr=self.scratch, hex=payload.hex())


def sel_char_icon(emu, player):
    g = emu.u32(nav.GAME_GLOBAL)
    sel = emu.u32(g + 0x10)
    if not emu.is_ram(sel):
        return -1
    return (emu.u32(sel + SEL_ICONS + 4 * player) >> 8) & 0xFF


def player_init(emu, player):
    g = emu.u32(nav.GAME_GLOBAL)
    mode = emu.u32(g + 8)
    raw = emu.read(mode + nav.MELEE_PLAYERS + player * PLAYER_SIZE, 8)
    return {
        "kind": raw[0],
        "state": raw[1],
        "colorFile": raw[5],
        "colorNo": raw[6],
        "controller": raw[7],
    }


def open_fighters_page(emu, fresh=True):
    """Open Stage & Cast > Fighter Swap (row 0) through the real controller path."""
    emu.press(0, ["START"], frames=2, after=6)
    if fresh:
        emu.press(0, ["L"], frames=2, after=6)  # Wrap category 0 (Lab) -> 6 (Stage & Cast), row 0 = Fighter Swap
        emu.press(0, ["A"], frames=2, after=6)  # Fighter Swap page, row 0


def wait_swap_done(emu, sb, prev_completed, max_frames=240):
    """Advance match until FighterSwapDebugBlock reports completion."""
    for _ in range(max_frames // 4):
        emu.advance(4)
        d = sb.read()
        if d["inProgress"] == 0 and d["completedCount"] > prev_completed:
            emu.advance(16)
            return d
    raise SwapError(f"swap did not settle within {max_frames} frames: {sb.read()}")


def apply_swap_via_menu(emu, sb, p1=None, p1_cos=None, cpu=None, cpu_cos=None, fresh=True, step_via_stick=False):
    """Open General > Fighters through the real controller path, select target
    fighter(s)/costume(s), and press A on Apply Swap (row 4)."""
    d_before = sb.read()
    open_fighters_page(emu, fresh=fresh)
    cur_p1, cur_p1c, cur_cpu, cur_cpuc = sb.read_scratch()
    want_p1 = ROSTER_IDX[p1] if p1 is not None else cur_p1
    want_p1c = p1_cos if p1_cos is not None else (0 if (p1 is not None and want_p1 != cur_p1) else cur_p1c)
    want_cpu = ROSTER_IDX[cpu] if cpu is not None else cur_cpu
    want_cpuc = cpu_cos if cpu_cos is not None else (0 if (cpu is not None and want_cpu != cur_cpu) else cur_cpuc)

    if step_via_stick:
        # Drive every single row through real stick presses (row 0 -> 1 -> 2 -> 3 -> 4).
        # Row 0: P1 Fighter
        diff = want_p1 - cur_p1
        for _ in range(abs(diff)):
            emu.press(0, [], stick=LT.RIGHT if diff > 0 else LT.LEFT, frames=2, after=2)
        emu.press(0, [], stick=LT.DOWN, frames=2, after=3)  # Row 1: P1 Costume
        cur_p1c = sb.read_scratch()[1]
        diff = want_p1c - cur_p1c
        for _ in range(abs(diff)):
            emu.press(0, [], stick=LT.RIGHT if diff > 0 else LT.LEFT, frames=2, after=2)
        emu.press(0, [], stick=LT.DOWN, frames=2, after=3)  # Row 2: CPU Fighter
        diff = want_cpu - cur_cpu
        for _ in range(abs(diff)):
            emu.press(0, [], stick=LT.RIGHT if diff > 0 else LT.LEFT, frames=2, after=2)
        emu.press(0, [], stick=LT.DOWN, frames=2, after=3)  # Row 3: CPU Costume
        cur_cpuc = sb.read_scratch()[3]
        diff = want_cpuc - cur_cpuc
        for _ in range(abs(diff)):
            emu.press(0, [], stick=LT.RIGHT if diff > 0 else LT.LEFT, frames=2, after=2)
        emu.press(0, [], stick=LT.DOWN, frames=2, after=3)  # Row 4: Apply Swap
    else:
        sb.write_scratch(want_p1, want_p1c, want_cpu, want_cpuc)
        # Cursor starts on row 0 on fresh open, or on row 4 when reopening the page.
        if fresh:
            for _ in range(4):
                emu.press(0, [], stick=LT.DOWN, frames=2, after=3)

    emu.press(0, ["A"], frames=2, after=6)  # Apply Swap (also closes lab menu)
    return wait_swap_done(emu, sb, d_before["completedCount"])


def start_clean_match(emu, p1_name="Mario", cpu_name="Fox", stage="final_destination"):
    if nav.scene(emu)[0] == "scMelee":
        nav.finish_training(emu)
        emu.advance(45)
    p1_k, p1_i = ROSTER_INFO[p1_name]
    cpu_k, cpu_i = ROSTER_INFO[cpu_name]
    nav.start_quick_match(emu, stage=stage, p1=(p1_k, p1_i), cpu=(cpu_k, cpu_i), seed_rng=True)
    emu.neutral(0)
    emu.advance(45)
    sb = SwapBlock(emu)
    sb.find()
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    return sb, base


def verify_match_exit_clean(emu, match_pos, res, label):
    nav.finish_training(emu)
    emu.advance(75)
    chunk = emu.log_text()[match_pos:]
    gone = FS.GONE_OBSERVED.findall(chunk)
    tsums = FS.TRACKER_SUMMARY.findall(chunk)
    unloads = L.rx(L.EXTRA_UNLOADED).findall(chunk)
    res.row(f"{label}: zero gone-while-observed", "0", str(len(gone)), len(gone) == 0)
    if tsums:
        reg, rem, lost, moved = (int(x) for x in tsums[-1])
        res.row(f"{label}: tracker s_lost == 0", "lost=0 reg==rem",
                f"reg={reg} rem={rem} lost={lost} moved={moved}",
                lost == 0 and moved == 0 and reg == rem and reg > 0)
    else:
        res.row(f"{label}: tracker summary logged", "present", "missing", False)
    res.row(f"{label}: extra unloaded to baseline", "yes",
            str([u[-1] for u in unloads]), len(unloads) >= 1 and unloads[-1][-1] == "yes")


# ---- scenarios --------------------------------------------------------------


def sc_p1_swap_and_costume(emu):
    res = CT.Result("p1-swap-and-costume")
    match_pos = len(emu.log_text())
    sb, base = start_clean_match(emu, "Mario", "Fox")
    d0 = sb.read()
    res.row("costume & AltCostume guards ok", "cos=1 alt=1",
            f"cos={d0['costumeHelperOk']} alt={d0['altCostumeOk']}",
            d0["costumeHelperOk"] == 1 and d0["altCostumeOk"] == 1)

    # Lock P1 damage to 42% so we confirm lockPercents() re-applies after swap.
    SP.set_player_percent(emu, base, 42, lock=True)
    emu.advance(4)

    # 1. Mario -> Falco costume 2 via full real-stick navigation.
    pos = len(emu.log_text())
    apply_swap_via_menu(emu, sb, p1="Falco", p1_cos=2, fresh=True, step_via_stick=True)
    p1_info = player_init(emu, 0)
    p1_icon = sel_char_icon(emu, 0)
    fighters = LT.Fighters(emu)
    f_falco = fighters.by_player(0)
    dmg = fighters.damage(f_falco) if f_falco is not None else -1.0
    auto_ok = AUTO_SAVE_RE.search(emu.log_text()[pos:]) is not None
    res.row("P1 Falco c2 + CSS icon 0x13", "kind=0x15 cos=2 file!=0 icon=0x13",
            f"kind={p1_info['kind']:#x} cos={p1_info['colorNo']} file={p1_info['colorFile']} icon={p1_icon:#x}",
            p1_info["kind"] == 0x15 and p1_info["colorNo"] == 2 and p1_info["colorFile"] != 0 and p1_icon == 0x13)
    res.row("lockPercents re-applied 42% & auto-save fired", "dmg=42 auto=True",
            f"dmg={dmg:.1f} auto={auto_ok}", abs(dmg - 42.0) < 0.5 and auto_ok)

    saved, serr = FS.dpad_save(emu)
    emu.advance(15)
    loaded, lerr = FS.dpad_load(emu)
    res.row("post-swap Falco savestate save & load", "saved & loaded",
            f"saved={saved}({serr}) loaded={loaded}({lerr})", saved is not None and loaded == saved)

    SP.set_player_percent(emu, base, 0, lock=False)

    # 2. Falco -> Peach -> Kirby -> Ice Climbers.
    game = CT.Game(emu)
    for target, expect_partner in (("Peach", False), ("Kirby", False), ("Ice Climbers", True)):
        exp_kind, exp_icon = ROSTER_INFO[target]
        apply_swap_via_menu(emu, sb, p1=target, p1_cos=0, fresh=False)
        pi = player_init(emu, 0)
        ic = sel_char_icon(emu, 0)
        en = game.entry(0)
        has_partner = en is not None and en["fighters"][1] != 0
        s_f, s_e = FS.dpad_save(emu)
        emu.advance(12)
        l_f, l_e = FS.dpad_load(emu)
        res.row(f"P1 -> {target} spawn, icon & savestate",
                f"kind={exp_kind:#x} icon={exp_icon:#x} partner={expect_partner} ss=ok",
                f"kind={pi['kind']:#x} icon={ic:#x} partner={has_partner} ss={s_f}->{l_f}({s_e}/{l_e})",
                pi["kind"] == exp_kind and ic == exp_icon and has_partner == expect_partner
                and s_f is not None and l_f == s_f)

    verify_match_exit_clean(emu, match_pos, res, "p1-swap")
    return res


def sc_cpu_swap_mechanics_rng(emu):
    res = CT.Result("cpu-swap-mechanics-rng")
    match_pos = len(emu.log_text())
    sb, base = start_clean_match(emu, "Mario", "Fox")

    # Enable Dedede Waddle = Gordo (3), Mewtwo Shadow Ball = Full (3) & Hover = On (1),
    # Peach Turnip = Stitch Face, Peach F-Smash = Frying Pan.
    FM.set_u8(emu, base, FM.S_DEDEDE_WADDLE, 3)
    FM.set_u8(emu, base, FM.S_MEWTWO_BALL, 3)
    FM.set_u8(emu, base, FM.S_MEWTWO_HOVER, 1)
    FS.write_u8(emu, base + RNG.S_TURNIP, RNG.TURNIP_VALUES.index("Stitch Face Turnip"))
    FS.write_u8(emu, base + RNG.S_SMASH, RNG.SMASH_VALUES.index("Frying Pan"))
    emu.advance(6)

    # 1. CPU Fox -> Dedede: verify FitDedede.pac weights discovered & patched to Gordo (0/0/1/0).
    pos_ddd = len(emu.log_text())
    apply_swap_via_menu(emu, sb, cpu="Dedede", cpu_cos=0, fresh=True)
    emu.advance(10)
    ddd_logs = DEDEDE_FOUND_RE.findall(emu.log_text()[pos_ddd:])
    ddd_data = emu.u32(FT_COMMON_DATA + 12 * FT_DEDEDE)
    ddd_weights = None
    if emu.is_ram(ddd_data):
        tbl = emu.u32(ddd_data + DEDEDE_TABLE_OFF)
        if emu.is_ram(tbl):
            ddd_weights = struct.unpack(">4f", emu.read(tbl + DEDEDE_WEIGHTS_OFF, 16))
    res.row("CPU Dedede Waddle weights found & patched", "Gordo (0,0,100,0)",
            f"found={len(ddd_logs)} weights={ddd_weights}",
            bool(ddd_logs) and ddd_weights == (0.0, 0.0, 100.0, 0.0))

    # 2. CPU Dedede -> Mewtwo: verify Dedede restored pre-swap and Mewtwo mechanics apply live.
    pos_m2 = len(emu.log_text())
    apply_swap_via_menu(emu, sb, cpu="Mewtwo", cpu_cos=0, fresh=False)
    emu.advance(10)
    ddd_detached = DEDEDE_DETACH_RE.findall(emu.log_text()[pos_m2:])
    ddd_after = emu.u32(FT_COMMON_DATA + 12 * FT_DEDEDE)
    cpu_m2 = player_init(emu, 1)
    m2_view = CHG.FighterView(emu, 1)
    m2_ball = m2_view.la_int(65)
    m2_hover_used = m2_view.la_bit(115)
    res.row("Dedede weights restored pre-swap & Mewtwo mechanics live", "detach=1 data=0 ball=6 hoverUsed=0",
            f"detach={len(ddd_detached)} data={ddd_after:#x} cpu={cpu_m2['kind']:#x} ball={m2_ball} hoverUsed={m2_hover_used}",
            len(ddd_detached) >= 1 and ddd_after == 0 and cpu_m2["kind"] == 0x33
            and m2_ball == 6 and m2_hover_used == 0)

    # 3. CPU Mewtwo -> Peach: verify Peach live odds discovered & set to Never (-1).
    pos_peach = len(emu.log_text())
    apply_swap_via_menu(emu, sb, cpu="Peach", cpu_cos=0, fresh=False)
    emu.advance(10)
    peach_logs = RNG.ODDS_LINE.findall(emu.log_text()[pos_peach:])
    peach_data = FS.peach_common_data(emu)
    odds_ptr = emu.u32(peach_data + FS.DATA_ODDS) if emu.is_ram(peach_data) else 0
    odds_val = struct.unpack(">i", emu.read(odds_ptr, 4))[0] if emu.is_ram(odds_ptr) else None
    res.row("CPU Peach live odds found & Stitch Face active", "ok & odds=-1",
            f"logs={len(peach_logs)} odds={odds_val}",
            bool(peach_logs) and peach_logs[-1][3] == "ok" and odds_val == RNG.ODDS_NEVER)

    # 4. CPU Peach -> Fox: verify Peach odds restored pre-swap before FitPeach.pac freed.
    pos_fox = len(emu.log_text())
    apply_swap_via_menu(emu, sb, cpu="Fox", cpu_cos=0, fresh=False)
    emu.advance(10)
    peach_restored = PEACH_RESTORE_RE.findall(emu.log_text()[pos_fox:])
    peach_after = FS.peach_common_data(emu)
    res.row("Peach odds restored pre-swap & unloaded", "restore=1 data=0",
            f"restore={len(peach_restored)} data={peach_after:#x}",
            len(peach_restored) >= 1 and peach_after == 0)

    # 5. Exit match and verify zero stale detach writes on Peach or Dedede.
    pos_exit = len(emu.log_text())
    verify_match_exit_clean(emu, match_pos, res, "cpu-mech")
    exit_chunk = emu.log_text()[pos_exit:]
    stale_peach = [d for d in FS.DETACH_ODDS.findall(exit_chunk) if d[3] == "00000000"]
    stale_ddd = DEDEDE_DETACH_RE.findall(exit_chunk)
    res.row("zero stale .pac writes at match detach", "peach=0 ddd=0",
            f"peach={len(stale_peach)} ddd={len(stale_ddd)}",
            len(stale_peach) == 0 and len(stale_ddd) == 0)
    return res


def sc_simultaneous_swap(emu):
    res = CT.Result("simultaneous-swap")
    match_pos = len(emu.log_text())
    sb, _ = start_clean_match(emu, "Mario", "Fox")

    apply_swap_via_menu(emu, sb, p1="Marth", p1_cos=1, cpu="Sheik", cpu_cos=2, fresh=True)
    p1 = player_init(emu, 0)
    cpu = player_init(emu, 1)
    i1, i2 = sel_char_icon(emu, 0), sel_char_icon(emu, 1)
    res.row("both P1 Marth c1 & CPU Sheik c2 swapped in one close",
            "P1=0x13/c1/i0x11 CPU=0x0F/c2/i0x0F",
            f"P1={p1['kind']:#x}/c{p1['colorNo']}/i{i1:#x} CPU={cpu['kind']:#x}/c{cpu['colorNo']}/i{i2:#x}",
            p1["kind"] == 0x13 and p1["colorNo"] == 1 and i1 == 0x11
            and cpu["kind"] == 0x0F and cpu["colorNo"] == 2 and i2 == 0x0F)

    s_f, s_e = FS.dpad_save(emu)
    emu.advance(12)
    l_f, l_e = FS.dpad_load(emu)
    res.row("post-simultaneous savestate save & load", "saved & loaded",
            f"saved={s_f}({s_e}) loaded={l_f}({l_e})", s_f is not None and l_f == s_f)

    verify_match_exit_clean(emu, match_pos, res, "simultaneous")
    return res


def sc_active_scenario_and_recording_cleanup(emu):
    res = CT.Result("active-scenario-and-recording-cleanup")
    match_pos = len(emu.log_text())
    sb, base = start_clean_match(emu, "Mario", "Fox")
    scen = SP.ScenarioBlock(emu)
    scen.find()

    # Record baseline cpu.options before activating Shield Pressure scenario.
    orig_cpu_opts = SP.read_cpu_opts(emu, base)
    scen.set_menu(SP.SCEN_SHIELD_PRESSURE, auto_reset=1, side=SP.SIDE_RIGHT)
    emu.advance(8)
    sd_before = scen.read()
    res.row("Scenario Shield Pressure active before swap", "preset=4 snap=1",
            f"preset={sd_before['activePreset']} snap={sd_before['snapshotActive']}",
            sd_before["activePreset"] == SP.SCEN_SHIELD_PRESSURE and sd_before["snapshotActive"] == 1)

    # Swap P1 Mario -> Captain Falcon while scenario is active: scenario must be
    # preserved and cleanly reset for the new fighter, and turning it Off later
    # must still restore the original pre-scenario CPU options byte-for-byte.
    pos_sc = len(emu.log_text())
    apply_swap_via_menu(emu, sb, p1="Captain Falcon", p1_cos=0, fresh=True)
    emu.advance(8)
    sd_after = scen.read()
    chunk_sc = emu.log_text()[pos_sc:]
    deact_logged = SCEN_DEACT_RE.search(chunk_sc) is not None
    restore_m = SCEN_RESTORE_RE.search(chunk_sc)
    res.row("Scenario preserved & reset across fighter swap", "preset=4 snap=1 resets+ log=True",
            f"preset={sd_after['activePreset']} snap={sd_after['snapshotActive']} "
            f"resets={sd_before['resetCount']}->{sd_after['resetCount']} "
            f"deact={deact_logged} restore={restore_m.group(1) if restore_m else None}",
            sd_after["activePreset"] == SP.SCEN_SHIELD_PRESSURE and sd_after["snapshotActive"] == 1
            and sd_after["resetCount"] > sd_before["resetCount"]
            and deact_logged and restore_m is not None and int(restore_m.group(1)) == SP.SCEN_SHIELD_PRESSURE)

    scen.set_menu(SP.SCEN_OFF, auto_reset=1, side=SP.SIDE_RIGHT)
    emu.advance(6)
    sd_off = scen.read()
    restored_opts = SP.read_cpu_opts(emu, base)
    res.row("Scenario Off after swap restores original settings", "preset=0 snap=0 exact=True",
            f"preset={sd_off['activePreset']} snap={sd_off['snapshotActive']} exact={restored_opts == orig_cpu_opts}",
            sd_off["activePreset"] == SP.SCEN_OFF and sd_off["snapshotActive"] == 0
            and restored_opts == orig_cpu_opts)

    # Now save Recording positions and enter CPU Control mode (which assigns P1's pad to CPU).
    orig_cpu_ctrl = player_init(emu, 1)["controller"]
    p1_ctrl = player_init(emu, 0)["controller"]
    rec = RT.Rec(emu)
    rec.find()
    rec.cpu_control(True)
    rec.request("save")
    emu.advance(12)
    rec.set(cpuMode=RT.CPU_MODE["Control"])
    rec.request("cpu-mode")
    emu.advance(12)
    st_during = rec.status()
    cpu_ctrl_during = player_init(emu, 1)["controller"]
    saved_during = bool(st_during["flags"] & RT.FLAG_SAVED)
    swapped_during = bool(st_during["flags"] & RT.FLAG_SWAPPED)
    res.row("Recording CPU Control active before swap", f"saved=True swapped=True ctrl={p1_ctrl}",
            f"saved={saved_during} swapped={swapped_during} ctrl={cpu_ctrl_during} (orig={orig_cpu_ctrl})",
            saved_during and swapped_during and cpu_ctrl_during == p1_ctrl)

    # Swap CPU Fox -> Luigi while Recording CPU Control is active.
    apply_swap_via_menu(emu, sb, cpu="Luigi", cpu_cos=0, fresh=False)
    st_after = rec.status()
    cpu_after = player_init(emu, 1)
    saved_after = bool(st_after["flags"] & RT.FLAG_SAVED)
    swapped_after = bool(st_after["flags"] & RT.FLAG_SWAPPED)
    res.row("Recording cleared & CPU controllerNo restored", f"saved=False swapped=False ctrl={orig_cpu_ctrl} kind=0x09",
            f"saved={saved_after} swapped={swapped_after} ctrl={cpu_after['controller']} kind={cpu_after['kind']:#x}",
            (not saved_after) and (not swapped_after)
            and cpu_after["controller"] == orig_cpu_ctrl and cpu_after["kind"] == 0x09)

    s_f, s_e = FS.dpad_save(emu)
    emu.advance(12)
    l_f, l_e = FS.dpad_load(emu)
    res.row("post-cleanup savestate save & load", "saved & loaded",
            f"saved={s_f}({s_e}) loaded={l_f}({l_e})", s_f is not None and l_f == s_f)

    verify_match_exit_clean(emu, match_pos, res, "cleanup")
    return res


def sc_stress_swaps(emu):
    res = CT.Result("stress-swaps")
    match_pos = len(emu.log_text())
    sb, _ = start_clean_match(emu, "Mario", "Fox")
    heaps_before = L.heap_table(emu)

    # 12 consecutive swaps across heavy / item / sub-fighter / clone characters.
    steps = [
        {"p1": "Kirby"},
        {"cpu": "Ice Climbers"},
        {"p1": "Zelda"},
        {"cpu": "Sheik"},
        {"p1": "Samus"},
        {"cpu": "Zero Suit Samus"},
        {"p1": "Peach", "p1_cos": 1},
        {"cpu": "Snake", "cpu_cos": 1},
        {"p1": "Wario", "p1_cos": 0},
        {"cpu": "Charizard"},
        {"p1": "Roy", "cpu": "Mewtwo"},
        {"p1": "Kirby", "cpu": "Ice Climbers"},
    ]
    all_ok = True
    for i, st in enumerate(steps):
        f0 = nav.game_frame(emu)
        apply_swap_via_menu(emu, sb, p1=st.get("p1"), p1_cos=st.get("p1_cos"),
                            cpu=st.get("cpu"), cpu_cos=st.get("cpu_cos"), fresh=(i == 0))
        emu.advance(20)
        f1 = nav.game_frame(emu)
        table = L.heap_table(emu)
        assist = table.get("AssistFigureResource")
        of4 = table.get("OverlayFighter4")
        p1_now, cpu_now = player_init(emu, 0), player_init(emu, 1)
        ok_p1 = ("p1" not in st) or (p1_now["kind"] == ROSTER_INFO[st["p1"]][0])
        ok_cpu = ("cpu" not in st) or (cpu_now["kind"] == ROSTER_INFO[st["cpu"]][0])
        ok_step = (f1 > f0 + 10) and (assist is not None and assist[2] == 1) and (of4 is not None and of4[2] == 0) and ok_p1 and ok_cpu
        if not ok_step:
            all_ok = False
            res.note(f"step {i + 1} {st} failed: frames {f0}->{f1} assist={assist} of4={of4} p1={p1_now} cpu={cpu_now}")
    res.row("12 consecutive heavy/sub-fighter swaps ran cleanly", "12/12 ok",
            "12/12 ok" if all_ok else "FAILED", all_ok)

    heaps_after = L.heap_table(emu)
    assist_after = heaps_after.get("AssistFigureResource")
    of4_after = heaps_after.get("OverlayFighter4")
    # Confirm AssistFigureResource holds only extra and OverlayFighter4 stays empty after 12 swaps.
    res.row("AssistFigureResource holds only extra and OverlayFighter4 empty", "assist=1, of4=0",
            f"assist={assist_after} of4={of4_after}",
            assist_after is not None and assist_after[2] == 1 and of4_after is not None and of4_after[2] == 0)

    for hname in ("OverlayFighter1", "OverlayFighter2", "Fighter1Resoruce", "Fighter2Resoruce",
                  "Fighter1Instance", "Fighter2Instance", "IteamResource", "Effect"):
        hb = heaps_before.get(hname)
        ha = heaps_after.get(hname)
        if hb and ha:
            res.note(f"heap {hname}: blocks {hb[2]}->{ha[2]} used {hb[3]:#x}->{ha[3]:#x}")

    # Verify savestate fits & restores even on final Kirby vs Ice Climbers state!
    s_f, s_e = FS.dpad_save(emu)
    emu.advance(15)
    l_f, l_e = FS.dpad_load(emu)
    res.row("post-stress Kirby vs Ice Climbers savestate", "saved & loaded",
            f"saved={s_f}({s_e}) loaded={l_f}({l_e})", s_f is not None and l_f == s_f)

    verify_match_exit_clean(emu, match_pos, res, "stress")
    css_heaps = FS.fighter_heaps_snapshot(emu)
    leaked = {k: v for k, v in css_heaps.items()
              if not re.fullmatch(r"Fighter[1-4]Resoruce", k) and v[2] != 0}
    res.row("fighter instance/res2/overlay heaps empty on CSS", "0 leaked",
            f"{leaked}", len(leaked) == 0)
    return res


ALL_SCENARIOS = [
    ("p1-swap-and-costume", sc_p1_swap_and_costume),
    ("cpu-swap-mechanics-rng", sc_cpu_swap_mechanics_rng),
    ("simultaneous-swap", sc_simultaneous_swap),
    ("active-scenario-and-recording-cleanup", sc_active_scenario_and_recording_cleanup),
    ("stress-swaps", sc_stress_swaps),
]


def cmd_test_fighter_swap(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-fighter-swap")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args(rest)

    wanted = [(name, fn) for name, fn in ALL_SCENARIOS if not opts.only or name in opts.only]
    if opts.list:
        for name, _ in wanted:
            print(name)
        return 0
    if not wanted:
        E.die(f"no scenarios matched --only {opts.only!r}")

    label = "test-fighter-swap"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, results = "", "boot", "", []

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            try:
                nav.boot_to_training_css(emu)
                for name, fn in wanted:
                    step = name
                    emu.mark(f"fighter-swap: {name}")
                    try:
                        res = fn(emu)
                    except (SwapError, Timeout, nav.NavError, HarnessError, LT.LabError) as exc:
                        res = CT.Result(name)
                        res.check(f"{name} stopped: {exc}", False)
                    print(f"{res.name}: {'PASS' if res.ok else 'FAIL'}", flush=True)
                    for line in res.lines:
                        print("  " + line, flush=True)
                    results.append(res)
            except (Timeout, nav.NavError, HarnessError, SwapError, LT.LabError) as exc:
                why = f"{step}: {exc}"
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or f"{step}: {exc}"

    log = E.read_log(log_path) if log_path else ""
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why and any(not r.ok for r in results):
        why = "one or more scenarios failed"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
