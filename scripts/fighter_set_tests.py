"""Emulator test of the fighter set guard (run through emu.py; settings come
from the environment the same way).

  test-fighter-sets REL
      Project+'s Code Menu can give each Training CPU its own character
      ("P1..P4 Character Select"). Every different character takes one of the
      game's four fighter heap sets, and the fourth set loads its character's
      code into OverlayFighter4, where the extra module lives; that load fails
      and the match freezes. The stub refuses only the change that would take
      that set and keeps the CPU on a character already loaded.

      Lab match, P1 Mario against a CPU Fox, No. of CPUs 3, then:
        1. P3 -> Luigi through the Code Menu: a third set, allowed (Luigi's
           code goes into OverlayFighter3).
        2. P4 -> Jigglypuff: would take the fourth set: refused, P4 stays Fox,
           one log line says so, the match goes on.
        3. The Code Menu opened and closed again: the refused choice is not
           applied again.
        4. Finish, then a second match: P3 -> Luigi and P4 -> Jigglypuff in
           one menu close: one is loaded, the other refused.
      In every step the game's frame counter must keep running, OverlayFighter4
      must hold only the extra module, and the log must have no problem lines.

      Then a third match (swap safety): Mario (P1) against a Fox CPU with the
      tracker, OSDs, color overlays, info display, CPU control and Character
      RNG (Peach Stitch Face + Frying Pan) on. Saves a state, swaps P2 Fox ->
      Falco -> Fox and P1 Mario -> Peach -> Mario -> Peach -> Mario through the
      Code Menu, exercises savestate loads before/after swaps and Peach turnip
      pulls on both Peach appearances, and finishes the match. Checks zero
      "gone while observed" / dead-manager observers, Character RNG re-finding
      Peach's live file on swap back and never writing through a stale odds
      pointer at detach, and clean module/heap unload.

  test-fighter-sets REL --without-extra BROKEN
      The same match with BROKEN installed (its extra module is rejected at
      boot): nothing is refused, and P3 Luigi and P4 Jigglypuff both load,
      Jigglypuff's code into the empty OverlayFighter4.
"""
import argparse
import re
import struct
import time
import sys

sys.dont_write_bytecode = True

import display_options_tests as DO  # noqa: E402
import emu as E  # noqa: E402
import extra_tests as X  # noqa: E402
import lab_tests as LT  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
import rng_tests as RNG  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

# P+ 3.2.1's Code Menu (built from QuickLava's generator): its "P1..P4
# Character Select" lines and its open flag (4 while the menu is open).
CM_LINES = {1: 0x804E0F34, 2: 0x804E1410, 3: 0x804E1674, 4: 0x804E18D8}
CM_FLAG = 0x804E0034
CM_OPEN = 4
CM_ACTIVATION_VAL = 0x804E0C2C  # 0 Default, 1 PM 3.6, 2 OFF
# Controls > P+ Code Menu Shortcut (src/main/ui/pages/controls.cpp, row 8, settings +32).
CONTROLS_CODE_MENU = 8
SET_ALLOW_CODE_MENU = 32
# Character kinds as the match setup holds them (byte 0 of a player entry).
MARIO, FOX, LUIGI, PEACH, FALCO, JIGGLYPUFF = 0x00, 0x07, 0x09, 0x0D, 0x15, 0x27
NAMES = {MARIO: "Mario", FOX: "Fox", LUIGI: "Luigi", PEACH: "Peach", FALCO: "Falco", JIGGLYPUFF: "Jigglypuff"}
REFUSED = r"\[{name}\] fighter sets: player (\d)'s change to character (\d+) refused: .*; keeps character (\d+)"

# Game addresses for swap-safety checks (sora_melee RSBE01 rev 2 / P+ 3.2.1).
FT_COMMON_DATA = 0x80B87FD0  # g_ftCommonDataAccesser table (12 bytes per ftKind)
FT_PEACH = 0x0C              # Fighter_Peach in ftKind
DATA_ODDS = 0x84             # offset of one-in-N odds pointer in Peach's fighter data
SO_EVENT_SYSTEM = 0x80B86970 # soEventSystem singleton (+28 count word, +32 entries of {mgr, id})

# Settings offsets (src/main/settings.h).
S_OVERLAY_CPU = DO.S_OVERLAY_HMN + 20
S_OSD_WAVEDASH = LT.SET_OSD_POSITION + 4
OVERLAY_GREEN, OVERLAY_YELLOW = 2, 3
SAVESTATE_SAVED = re.compile(r"savestate: saved frame (\d+)")
SAVESTATE_LOADED = re.compile(r"savestate: loaded frame (\d+)")
SAVESTATE_REFUSED = re.compile(r"savestate: (save|load) refused: ([^\r\n]+)")
DETACH_ODDS = re.compile(r"character rng: detach item odds ([0-9a-f]{8}) \(was (-?\d+), put back (-?\d+), live data ([0-9a-f]{8})\)")
GONE_OBSERVED = re.compile(r"tracker: P\d(?: partner)? entry [0-9a-f]+ gone while observed \([^)]*\)")
OBSERVERS_ON = re.compile(r"tracker: P(\d)( partner)? entry ([0-9a-f]+) observers on: .* manage (\d+)")
TRACKER_SUMMARY = re.compile(r"tracker: (\d+) registrations, (\d+) removals, (\d+) lost, (\d+) moved")


class GuardError(RuntimeError):
    pass


def line_info(emu, player):
    """A Character Select line: its text, value (an index into its list) and
    the list of character kinds it offers."""
    a = CM_LINES[player]
    raw = emu.read(a, 0x20)
    text = emu.cstr(a + struct.unpack_from(">H", raw, 5)[0], 40)
    value, top, src = struct.unpack_from(">I", raw, 8)[0], struct.unpack_from(">I", raw, 20)[0], \
        struct.unpack_from(">I", raw, 24)[0]
    ids = list(emu.read(src + 28, 4 * (top + 1))[3::4]) if emu.is_ram(src) and top < 80 else []
    return text or "", value, ids


def code_menu(emu, choices):
    """Opens the Code Menu with the pad (L + R + D-pad down), sets each
    player's Character Select line to a character ({player: kind}; the value
    is written in memory instead of scrolled to), closes it with B (which
    applies the lines) and lets the match run on."""
    for player, kind in choices.items():
        text, _, ids = line_info(emu, player)
        if f"P{player}" not in text or "Character" not in text or kind not in ids:
            raise GuardError(f"Code Menu line {player} is {text!r} with {len(ids)} characters")
    emu.hold(0, buttons=["L", "R", "DDOWN"], l=255, r=255)
    emu.advance(6)
    emu.hold(0)
    emu.advance(20)
    if emu.u32(CM_FLAG) != CM_OPEN:
        raise GuardError(f"the Code Menu did not open (flag {emu.u32(CM_FLAG)})")
    for player, kind in choices.items():
        _, _, ids = line_info(emu, player)
        emu.h.call("write_mem", addr=CM_LINES[player] + 8, hex="%08x" % ids.index(kind))
    for _ in range(8):
        emu.press(0, ["B"], frames=3, after=12)
        if emu.u32(CM_FLAG) != CM_OPEN:
            break
    if emu.u32(CM_FLAG) == CM_OPEN:
        raise GuardError("the Code Menu did not close")


def kinds(emu):
    return [p["kind"] for p in nav.melee_setup(emu)["players"]]


class Run:
    def __init__(self, emu, extra=True):
        self.emu = emu
        self.extra = extra
        self.shots = []
        self.results = []

    def settle(self, label, frames=300):
        """Lets the match run; fails if its frame counter stops (the freeze
        the guard prevents) or, with the extra module, OverlayFighter4 holds
        more than it. Returns the heap table."""
        e = self.emu
        before = nav.game_frame(e)
        for _ in range(frames // 30):
            e.advance(30)
        after = nav.game_frame(e)
        table = L.heap_table(e)
        of3, of4 = table.get("OverlayFighter3"), table.get("OverlayFighter4")
        mods = [n for n, h, _ in X.loaded_modules(e, table) if n.startswith("ft_")]
        line = (f"  [{label}] game frame {before} -> {after}; players {[NAMES.get(k, hex(k)) for k in kinds(e)]}; "
                f"OverlayFighter3 {of3[2] if of3 else '-'} blocks, OverlayFighter4 {of4[2] if of4 else '-'} blocks; "
                f"fighter modules {mods}")
        print(line)
        self.results.append(line)
        if after - before < frames // 2:
            raise GuardError(f"{label}: the match froze (game frame {before} -> {after})")
        if self.extra and (of4 is None or of4[2] != 1):
            raise GuardError(f"{label}: OverlayFighter4 holds {of4[2] if of4 else '-'} blocks, not just the extra module")
        self.shots.append(e.screenshot("fighter-sets-" + re.sub(r"\W+", "-", label).strip("-")))
        return table


def refusals(emu, since):
    return [(int(a), int(b), int(c)) for a, b, c in L.rx(REFUSED).findall(emu.log_text()[since:])]


def three_cpus(emu, run, match):
    nav.start_training_match(emu, "mario", "fox")
    emu.advance(60)
    nav.set_training_cpus(emu, 3)
    run.settle(f"match {match} 3 CPUs", 120)
    if kinds(emu) != [0x00, FOX, FOX, FOX]:
        raise GuardError(f"players are {kinds(emu)}, wanted Mario and three Fox")


def enable_code_menu(emu):
    """Turn Lab > Controls > P+ Code Menu Shortcut On through the real lab menu
    (blocked by default in Training Lab)."""
    lab = LT.Lab(emu, [])
    lab.find_settings()
    lab.open_menu()
    lab.go_controls()
    lab.down(CONTROLS_CODE_MENU)
    lab.toggle_on(SET_ALLOW_CODE_MENU)
    lab.close_menu()


def peach_common_data(emu):
    return emu.u32(FT_COMMON_DATA + 12 * FT_PEACH)


def live_event_managers(emu):
    """Set of live soEventManager ids currently registered in soEventSystem."""
    word = emu.u32(SO_EVENT_SYSTEM + 28)
    count = word & 0x3FF
    if count > 500:
        count = 500
    raw = emu.read(SO_EVENT_SYSTEM + 32, 8 * count)
    out = {}
    for i in range(count):
        mgr, mid = struct.unpack_from(">2I", raw, 8 * i)
        out[mid] = mgr
    return out


def write_u8(emu, addr, val):
    emu.h.call("write_mem", addr=addr, hex=f"{val & 0xFF:02x}")


def dpad_save(emu, tries=3):
    """Hold D-pad right through the controller path to save the quick slot."""
    for _ in range(tries):
        pos = len(emu.log_text())
        emu.hold(0, buttons=["DRIGHT"])
        emu.advance(14)
        emu.hold(0)
        emu.advance(36)
        chunk = emu.log_text()[pos:]
        m = SAVESTATE_SAVED.search(chunk)
        if m:
            return int(m.group(1)), None
        ref = SAVESTATE_REFUSED.search(chunk)
        if ref and "loading" in ref.group(2):
            emu.advance(60)
            continue
        return None, ref.group(2) if ref else "no savestate log line"
    return None, "files kept loading"


def dpad_load(emu):
    """Tap D-pad left through the controller path to load the quick slot."""
    pos = len(emu.log_text())
    emu.press(0, ["DLEFT"], frames=3, after=30)
    chunk = emu.log_text()[pos:]
    m = SAVESTATE_LOADED.search(chunk)
    if m:
        return int(m.group(1)), None
    ref = SAVESTATE_REFUSED.search(chunk)
    return None, ref.group(2) if ref else "no savestate log line"


def pull_stitch_face(emu, fighters):
    """Drive P1 Peach to pull a turnip via real controller (down + B) and read it."""
    emu.press(0, ["B"], stick=(128, 28), frames=4, after=32)
    f = fighters.by_player(0)
    if f is None:
        return None, None, None
    p = RNG.Peach(emu, fighters, f, "P1")
    kind, item = p.held()
    turnip = p.turnip(item) if kind == RNG.IT_TURNIP else None
    return kind, turnip, p.odds()


def heap_containing(table, addr):
    for name, (start, end, blocks, used) in table.items():
        if start <= addr < end:
            return name, start, end
    return None, 0, 0


def fighter_heaps_snapshot(emu):
    return {k: v for k, v in L.heap_table(emu).items()
            if k.startswith(("Fighter", "OverlayFighter"))}


def swap_safety(emu, run, checks, mark):
    """Mid-match Code Menu swaps with tracker, OSDs, overlays, info display,
    CPU control and Character RNG active, plus savestates and clean detach."""
    mark("swap: start Mario vs Fox")
    match_pos = len(emu.log_text())
    nav.start_training_match(emu, "mario", "fox", "final_destination", seed_rng=RNG.MATCH_SEED)
    emu.advance(140)
    lab = LT.Lab(emu, [])
    base = lab.find_settings()
    if emu.u8(base + SET_ALLOW_CODE_MENU) == 0:
        enable_code_menu(emu)
    fighters = LT.Fighters(emu)

    # Enable OSDs, HMN/CPU overlays, HMN/CPU info display, CPU control (stand),
    # and Character RNG (Peach Stitch Face + Frying Pan).
    for off in range(LT.SET_ADVANTAGE, LT.SET_ADVANTAGE + 12):
        if off != LT.SET_TOLERANCE:
            write_u8(emu, base + off, 1)
    write_u8(emu, base + DO.S_OVERLAY_HMN + 1 + DO.GROUP_WAIT, OVERLAY_GREEN)
    write_u8(emu, base + S_OVERLAY_CPU + 1 + DO.GROUP_WAIT, OVERLAY_YELLOW)
    write_u8(emu, base + LT.SET_HMN_INFO, 1)      # State preset
    write_u8(emu, base + LT.SET_HMN_INFO + 1, 0)  # Small
    write_u8(emu, base + LT.SET_CPU_INFO, 1)      # State preset
    write_u8(emu, base + LT.SET_CPU_INFO + 1, 0)  # Small
    for off, val in DO.CPU_STILL.items():
        write_u8(emu, base + off, val)
    write_u8(emu, base + RNG.S_TURNIP, RNG.TURNIP_VALUES.index("Stitch Face Turnip"))
    write_u8(emu, base + RNG.S_SMASH, RNG.SMASH_VALUES.index("Frying Pan"))
    emu.advance(30)

    # Play some frames with P1 before saving.
    emu.press(0, ["Y"], frames=2, after=25)
    emu.press(0, ["A"], frames=2, after=25)

    mark("swap: save pre-swap savestate")
    saved_pre, save_err = dpad_save(emu)
    print(f"  [swap] pre-swap savestate: frame={saved_pre} err={save_err}")
    checks.append(("pre-swap savestate saved", saved_pre is not None))

    # Record initial P2 manageId from the tracker's observers-on line.
    obs_before = OBSERVERS_ON.findall(emu.log_text()[match_pos:])
    p2_init_mid = int(next((m[3] for m in reversed(obs_before) if m[0] == "2" and not m[1]), "-1"))

    mark("swap: P2 Fox -> Falco")
    code_menu(emu, {2: FALCO})
    run.settle("swap P2 Fox -> Falco", 120)
    emu.press(0, ["A"], frames=2, after=20)
    checks.append(("swap P2 is Falco", kinds(emu)[:2] == [MARIO, FALCO]))
    live_mids = live_event_managers(emu)
    checks.append(("old P2 Fox manager gone from soEventSystem", p2_init_mid >= 0 and p2_init_mid not in live_mids))

    # Loading the pre-swap savestate while P2 is Falco must refuse cleanly.
    loaded_falco, ref_falco = dpad_load(emu)
    print(f"  [swap] load pre-swap while Falco: loaded={loaded_falco} refused={ref_falco!r}")
    checks.append(("pre-swap load refused while Falco", loaded_falco is None and ref_falco == "saved in another match"))

    mark("swap: P2 Falco -> Fox")
    code_menu(emu, {2: FOX})
    run.settle("swap P2 Falco -> Fox", 120)
    emu.press(0, ["A"], frames=2, after=20)
    checks.append(("swap P2 back to Fox", kinds(emu)[:2] == [MARIO, FOX]))
    loaded_fox, ref_fox = dpad_load(emu)
    print(f"  [swap] load pre-swap after Falco -> Fox: loaded={loaded_fox} refused={ref_fox!r}")
    run.settle("after pre-swap load attempt", 60)
    checks.append(("pre-swap load after Fox return safe", loaded_fox is not None or ref_fox is not None))

    mark("swap: P1 Mario -> Peach (1st)")
    pos_p1 = len(emu.log_text())
    code_menu(emu, {1: PEACH})
    table_p1 = run.settle("swap P1 Mario -> Peach 1", 120)
    checks.append(("swap P1 is Peach (1st)", kinds(emu)[:2] == [PEACH, FOX]))
    data_1 = peach_common_data(emu)
    odds_ptr_1 = emu.u32(data_1 + DATA_ODDS) if emu.is_ram(data_1) else 0
    odds_val_1 = struct.unpack(">i", emu.read(odds_ptr_1, 4))[0] if emu.is_ram(odds_ptr_1) else None
    odds_logs_1 = RNG.ODDS_LINE.findall(emu.log_text()[pos_p1:])
    kind_1, turnip_1, _ = pull_stitch_face(emu, fighters)
    odds_heap_1 = heap_containing(table_p1, odds_ptr_1)
    print(f"  [swap] Peach #1: data={data_1:#x} odds_ptr={odds_ptr_1:#x} val={odds_val_1} "
          f"pull=({kind_1}, {turnip_1}) logs={odds_logs_1} heap={odds_heap_1[0]}({odds_heap_1[1]:#x}..{odds_heap_1[2]:#x})")
    checks.append(("Peach #1 odds found and Stitch Face active",
                   bool(odds_logs_1) and int(odds_logs_1[-1][0], 16) == odds_ptr_1
                   and odds_logs_1[-1][3] == "ok" and odds_val_1 == RNG.ODDS_NEVER))
    checks.append(("Peach #1 pulled Stitch Face turnip",
                   kind_1 == RNG.IT_TURNIP and turnip_1 == (7, 7.0, 22)))

    mark("swap: P1 Peach -> Mario")
    code_menu(emu, {1: MARIO})
    table_m1 = run.settle("swap P1 Peach -> Mario", 120)
    emu.press(0, ["A"], frames=2, after=20)
    data_while_mario = peach_common_data(emu)
    val_at_old_odds = emu.u32(odds_ptr_1) if emu.is_ram(odds_ptr_1) else None
    odds_heap_m1 = heap_containing(table_m1, odds_ptr_1)
    print(f"  [swap] Mario after Peach: peach_data={data_while_mario:#x} old_odds_ptr={odds_ptr_1:#x} "
          f"holds={val_at_old_odds:#x} heap={odds_heap_m1[0]}({odds_heap_m1[1]:#x}..{odds_heap_m1[2]:#x})")
    checks.append(("Peach data cleared when swapped to Mario", data_while_mario == 0))

    mark("swap: P1 Mario -> Peach (2nd)")
    pos_p2 = len(emu.log_text())
    code_menu(emu, {1: PEACH})
    run.settle("swap P1 Mario -> Peach 2", 120)
    checks.append(("swap P1 is Peach (2nd)", kinds(emu)[:2] == [PEACH, FOX]))
    data_2 = peach_common_data(emu)
    odds_ptr_2 = emu.u32(data_2 + DATA_ODDS) if emu.is_ram(data_2) else 0
    odds_val_2 = struct.unpack(">i", emu.read(odds_ptr_2, 4))[0] if emu.is_ram(odds_ptr_2) else None
    odds_logs_2 = RNG.ODDS_LINE.findall(emu.log_text()[pos_p2:])
    kind_2, turnip_2, _ = pull_stitch_face(emu, fighters)
    print(f"  [swap] Peach #2: data={data_2:#x} odds_ptr={odds_ptr_2:#x} val={odds_val_2} "
          f"pull=({kind_2}, {turnip_2}) logs={odds_logs_2}")
    checks.append(("Peach #2 odds re-found in live FitPeach.pac",
                   bool(odds_logs_2) and int(odds_logs_2[-1][0], 16) == odds_ptr_2
                   and odds_logs_2[-1][3] == "ok" and odds_val_2 == RNG.ODDS_NEVER))
    checks.append(("Peach #2 pulled Stitch Face turnip",
                   kind_2 == RNG.IT_TURNIP and turnip_2 == (7, 7.0, 22)))

    # Take and load a savestate after all the swaps.
    mark("swap: save and load post-swap savestate")
    saved_post, save_post_err = dpad_save(emu)
    emu.advance(20)
    loaded_post, load_post_err = dpad_load(emu)
    print(f"  [swap] post-swap savestate: saved={saved_post} ({save_post_err}) loaded={loaded_post} ({load_post_err})")
    checks.append(("post-swap savestate saved and loaded", saved_post is not None and loaded_post == saved_post))

    # Swap Peach out one final time so FitPeach.pac is unloaded at match detach.
    mark("swap: P1 Peach -> Mario before finish")
    code_menu(emu, {1: MARIO})
    table_end = run.settle("swap P1 Peach -> Mario final", 120)
    data_before_finish = peach_common_data(emu)
    odds_heap_end = heap_containing(table_end, odds_ptr_1)
    word_before_finish = emu.u32(odds_ptr_1) if emu.is_ram(odds_ptr_1) else 0
    print(f"  [swap] before finish: peach_data={data_before_finish:#x} odds_ptr_1={odds_ptr_1:#x} "
          f"(heap={odds_heap_end[0]} {odds_heap_end[1]:#x}..{odds_heap_end[2]:#x}, word={word_before_finish:#010x})")

    mark("swap: finish")
    nav.finish_training(emu)
    emu.advance(90)

    chunk = emu.log_text()[match_pos:]
    gone = GONE_OBSERVED.findall(chunk)
    tsums = TRACKER_SUMMARY.findall(chunk)
    detach_odds = DETACH_ODDS.findall(chunk)
    print(f"  [swap] tracker summary={tsums} gone_while_observed={len(gone)}")
    for g in gone:
        print(f"    ! {g}")
    for d in detach_odds:
        print(f"  [swap] detach odds line: addr=0x{d[0]} was={d[1]} put_back={d[2]} live_data=0x{d[3]}")

    checks.append(("zero 'gone while observed' during swaps", len(gone) == 0))
    if tsums:
        reg, rem, lost, moved = (int(x) for x in tsums[-1])
        checks.append(("tracker lost == 0, moved == 0, reg == rem",
                       lost == 0 and moved == 0 and reg == rem and reg >= 8))
    else:
        checks.append(("tracker summary logged for swap match", False))

    stale_detach = [d for d in detach_odds if d[3] == "00000000"]
    checks.append(("no stale s_odds write on detach when Peach gone", len(stale_detach) == 0))

    # Check fighter instance/code/resource2 heaps after match end on CSS:
    # Brawl keeps 2 CSS portrait blocks in Fighter*Resoruce, while fighter
    # instance, resource2 and overlay heaps must hold 0 blocks.
    css_heaps = fighter_heaps_snapshot(emu)
    leaked = {k: v for k, v in css_heaps.items()
              if not re.fullmatch(r"Fighter[1-4]Resoruce", k) and v[2] != 0}
    print(f"  [swap] non-empty fighter instance/res2/overlay heaps on CSS: {leaked}")
    checks.append(("fighter instance/res2/overlay heaps empty on CSS", len(leaked) == 0))


def guarded(emu, run, checks, mark, only=None):
    """The extra module loaded: the fourth set is refused, the third is not;
    then mid-match swap safety checks."""
    want = set(only) if only else {"sets", "swap"}
    matches = 0
    if "sets" in want:
        mark("match 1: three CPUs")
        three_cpus(emu, run, 1)
        enable_code_menu(emu)

        mark("match 1: P3 -> Luigi (third set)")
        pos = len(emu.log_text())
        code_menu(emu, {3: LUIGI})
        table = run.settle("P3 -> Luigi")
        of3 = table.get("OverlayFighter3")
        checks.append(("P3 is Luigi", kinds(emu)[2] == LUIGI))
        checks.append(("Luigi's code in OverlayFighter3", of3 is not None and of3[2] == 1))
        checks.append(("nothing refused", refusals(emu, pos) == []))

        mark("match 1: P4 -> Jigglypuff (fourth set)")
        pos = len(emu.log_text())
        code_menu(emu, {4: JIGGLYPUFF})
        run.settle("P4 -> Jigglypuff")
        checks.append(("P4's change refused once, Fox kept", refusals(emu, pos) == [(4, JIGGLYPUFF, FOX)]))
        checks.append(("P4 is Fox, P3 still Luigi", kinds(emu)[2:] == [LUIGI, FOX]))

        mark("match 1: the Code Menu again")
        pos = len(emu.log_text())
        code_menu(emu, {})
        run.settle("Code Menu closed again")
        _, value, ids = line_info(emu, 4)
        checks.append(("not refused again", refusals(emu, pos) == []))
        checks.append(("P4's line shows Fox", ids[value] == FOX))
        checks.append(("players unchanged", kinds(emu)[2:] == [LUIGI, FOX]))

        mark("match 1: finish")
        nav.finish_training(emu)
        emu.advance(60)
        matches += 1

        mark("match 2: three CPUs")
        three_cpus(emu, run, 2)
        mark("match 2: P3 -> Luigi and P4 -> Jigglypuff in one close")
        pos = len(emu.log_text())
        code_menu(emu, {3: LUIGI, 4: JIGGLYPUFF})
        run.settle("P3 and P4 in one close")
        found = refusals(emu, pos)
        now = kinds(emu)[2:]
        checks.append(("one of the two refused", len(found) == 1 and found[0][2] == FOX))
        checks.append(("one loaded, the other kept Fox",
                       sorted(now) in (sorted([LUIGI, FOX]), sorted([JIGGLYPUFF, FOX]))))
        mark("match 2: finish")
        nav.finish_training(emu)
        emu.advance(120)
        matches += 1

    if "swap" in want:
        swap_safety(emu, run, checks, mark)
        matches += 1
    return matches


def unguarded(emu, run, checks, mark):
    """The extra module not loaded (rejected at boot): nothing is refused and
    the fourth character loads into the empty OverlayFighter4."""
    mark("three CPUs")
    three_cpus(emu, run, 1)
    enable_code_menu(emu)
    mark("P3 -> Luigi, then P4 -> Jigglypuff")
    pos = len(emu.log_text())
    code_menu(emu, {3: LUIGI})
    run.settle("no extra P3 -> Luigi")
    code_menu(emu, {4: JIGGLYPUFF})
    table = run.settle("no extra P4 -> Jigglypuff")
    of4 = table.get("OverlayFighter4")
    checks.append(("nothing refused", refusals(emu, pos) == []))
    checks.append(("P3 Luigi, P4 Jigglypuff", kinds(emu)[2:] == [LUIGI, JIGGLYPUFF]))
    checks.append(("Jigglypuff's code in OverlayFighter4", of4 is not None and of4[2] == 1))
    mark("finish")
    nav.finish_training(emu)
    emu.advance(60)
    return 1


def cmd_test_fighter_sets(rest):
    import os
    import shutil
    ap = argparse.ArgumentParser(prog="emu.py test-fighter-sets")
    ap.add_argument("rel")
    ap.add_argument("--without-extra", metavar="BROKEN",
                    help="install this build (its extra module rejected at boot) for the run, then REL again")
    ap.add_argument("--only", nargs="+", choices=("sets", "swap"),
                    help="run only the named scenario(s); default runs all")
    opts = ap.parse_args(rest)
    label = "test-fighter-sets" + (" (no extra module)" if opts.without_extra else "")
    backend = E.headless_backend()
    installed = None
    if opts.without_extra:
        installed = os.path.join(E.plugins_dir(os.path.abspath(E.env("SD_FOLDER"))), os.path.basename(opts.rel))
        shutil.copyfile(opts.without_extra, installed)
        print(f"installed {opts.without_extra} as {installed}")
    t0 = time.monotonic()
    state = {"step": "boot"}
    why, log_path, checks = "", "", []
    run = None
    expected_matches = 2

    def mark(text):
        state["step"] = text
        emu.mark(text)

    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS, allow_null=True,
                           css_cache=not opts.without_extra) as (proc, emu):
            log_path = emu.log_path
            run = Run(emu, extra=not opts.without_extra)
            try:
                nav.boot_to_training_css(emu)
                if opts.without_extra:
                    expected_matches = unguarded(emu, run, checks, mark)
                else:
                    expected_matches = guarded(emu, run, checks, mark, opts.only)
            except (Timeout, nav.NavError, HarnessError, GuardError, LT.LabError) as e:
                why = f"{state['step']}: {e}"
                try:
                    run.shots.append(emu.screenshot("fail-fighter-sets"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{state['step']}: {e}"
    finally:
        if installed:
            shutil.copyfile(opts.rel, installed)
            print(f"put {opts.rel} back")
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(r"fighter sets|fighter set guard|Can't Alloc|module XTRA|HARNESS\]|gone while observed|"
                     r"tracker: \d+ reg|character rng:|savestate: (saved|loaded|load refused)", line):
            print("  " + line.split("OSREPORT]: ")[-1].split("HARNESS]: ")[-1][:200])
    print()
    for name, ok in checks:
        print(f"{name:>45}: {'yes' if ok else 'NO'}")
    if not why and any(not ok for _, ok in checks):
        why = "checks failed"
    if not opts.without_extra:
        unloads = L.rx(L.EXTRA_UNLOADED).findall(log)
        print(f"{'extra module unloads, back to baseline':>45}: {[u[-1] for u in unloads]}")
        if not why and (len(unloads) != expected_matches or any(u[-1] != "yes" for u in unloads)):
            why = "the extra module did not unload to its baseline after each match"
    bad = [p for p in L.problems(log) if not (opts.without_extra and "module XTRA: not loaded" in p)]
    for line in bad:
        print(f"problem: {line}")
    why = why or ("problem lines in the log" if bad else "")
    for path, w, h in (run.shots if run is not None else []):
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
