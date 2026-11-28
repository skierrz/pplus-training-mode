"""Live Dolphin verification of per-mode CSS filtering, locking, and solo 1P
select rules (src/shared/select_rules.h, src/menu/game/select.cpp):

  test-select REL

Runs headless (Null video backend) by default; checks through real controller
inputs:
  1. L-Cancel stays 2P unrestricted (43 icons, no CSS rebuild).
  2. Float / Hover & DJC (drill 4, Minigames & Tech row 1): 1P solo, grid cut
     to 6 icons (Peach, Mewtwo, Ganondorf, Yoshi, Ness, Lucas), Peach preset,
     Ganondorf & Mewtwo selectable, Start spawns Target CPU cleanly.
  3. Cancel Sprint (drill 9): verified gated behind TM_BOOT_CANCEL_ENDURANCE=0
     (or tested live when present).
  4. Snake C4 Recovery (drill 15, Minigames & Tech row 2): 1P solo, grid cut
     to 1 icon (Snake), P1 coin locked, costume cycling via X still works,
     single B reopens Boot Menu cleanly, Start enters match with addCpu.
  5. Ledgedash (drill 1): strictly solo 1P with full 43-icon grid, then
     switching back to Training Lab restores 2P CSS with valid CPU panel.
  6. High-costume reset: finishing a match on Mario alt 7 then entering a
     restricted drill (Snake) resets costume to 0 cleanly.
"""
import argparse
import sys
import time

sys.dont_write_bytecode = True

import drill_tests as DT  # noqa: E402
import emu as E  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

FLOAT_DJC_GRID = [0x0D, 0x05, 0x14, 0x2E, 0x0B, 0x19]
CANCEL_SPRINT_GRID = [0x07, 0x13, 0x25, 0x2E, 0x20, 0x0B, 0x19]


class SelectRulesError(RuntimeError):
    pass


def _arm_row(emu, page, row, expect_rebuild):
    """Navigate open Boot Menu to (`page`, `row`), press A, and wait for CSS."""
    DT.go_to_page(emu, page, "R")
    DT.go_to_row(emu, row)
    emu.advance(6)
    pos = len(emu.log_text())
    pre_attaches = emu.log_text().count("] menu: attached")
    emu.press(0, ["A"], frames=4, after=8)
    emu.wait_for(f"{page} row {row} list close", lambda: nav.drill_list_state(emu) == "closed", 120, every=2)
    tail = emu.log_text()[pos:]
    rebuilt = "drills: character select rebuilt" in tail
    if expect_rebuild and not rebuilt:
        raise SelectRulesError(f"{page} row {row}: expected CSS rebuild, none logged")
    if not expect_rebuild and rebuilt:
        raise SelectRulesError(f"{page} row {row}: unexpected CSS rebuild")
    if rebuilt:
        emu.wait_for(
            f"{page} row {row} CSS rebuild",
            lambda: emu.log_text().count("] menu: attached") > pre_attaches
            and nav.drill_list_state(emu) == "closed",
            300,
            every=2,
        )
    emu.advance(20)


def _check(cond, results, failures, name, detail):
    if cond:
        results.append((name, detail))
    else:
        failures.append(f"{name}: {detail}")
        raise SelectRulesError(f"{name}: {detail}")


def _run_all(emu, results, failures):
    # 1. Boot to Training CSS with Boot Menu open (2P unrestricted, 43 icons).
    nav.boot_to_training_css(emu, drill_list="keep")
    DT.wait_count(emu, "boot menu open", r"\] drills: list opened \(auto\)", 1, 300)
    grid0 = nav.css_grid(emu)
    _check(len(grid0) == 43, results, failures, "boot-2p-grid", f"len={len(grid0)}")

    # 2. L-Cancel (Tech Practice row 0, drill 3): stays 2P unrestricted, NO rebuild.
    _arm_row(emu, "Tech Practice", 0, expect_rebuild=False)
    rule = nav.select_rule(emu)
    p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
    _check(
        rule is None and p1 is not None and cpu is not None and p1["kind"] == 1 and cpu["kind"] == 2
        and len(nav.css_grid(emu)) == 43,
        results, failures, "lcancel-stays-2p",
        f"rule={rule} p1={p1 and p1['kind']} cpu={cpu and cpu['kind']} grid={len(nav.css_grid(emu))}",
    )
    # B with P1 token in hand reopens Boot Menu cleanly.
    n_b = DT.count(emu, r"\] drills: list opened \(B\)")
    emu.press(0, ["B"], frames=4, after=10)
    DT.wait_count(emu, "B to reopen Boot Menu from L-Cancel CSS", r"\] drills: list opened \(B\)", n_b + 1, 120)
    emu.advance(8)

    # 3. Float / Hover & DJC (Minigames & Tech row 1, drill 4):
    #    1P solo, grid cut to 6 icons [Peach, Mewtwo, Ganondorf, Yoshi, Ness, Lucas],
    #    P1 preset Peach (0x0D), Ganondorf & Mewtwo selectable, Start spawns Target CPU.
    _arm_row(emu, "Minigames & Tech", 1, expect_rebuild=True)
    rule = nav.select_rule(emu)
    p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
    grid = nav.css_grid(emu)
    _check(
        rule is not None and rule["drill"] == 4 and rule["areas"] == 1 and rule["p1"] == 0x0D
        and rule["locked"] == 0 and grid == FLOAT_DJC_GRID and p1 is not None
        and p1["character"] == 0x0D and cpu is None,
        results, failures, "hover-float-solo-6-grid",
        f"rule={rule} grid={[hex(x) for x in grid]} p1={p1 and hex(p1['character'])} cpu={cpu}",
    )
    # First B recalls P1's placed Peach coin into hand (does NOT reopen Boot Menu).
    n_b = DT.count(emu, r"\] drills: list opened \(B\)")
    emu.press(0, ["B"], frames=4, after=12)
    emu.wait_for("B recalls P1 coin on Float/Hover", lambda: nav.css_area(emu, 0)["held"], 60, every=2)
    _check(
        DT.count(emu, r"\] drills: list opened \(B\)") == n_b,
        results, failures, "hover-float-first-b-recalls-coin",
        "first B recalled placed P1 coin without reopening Boot Menu",
    )
    # Drop P1 coin on Mewtwo (0x2E, P+ clone), then Ganondorf (0x14): both must succeed!
    nav._drop_p1_token(emu, "mewtwo")
    _check(
        not nav.css_area(emu, 0)["held"] and nav.css_area(emu, 0)["character"] == 0x2E,
        results, failures, "hover-float-mewtwo-allowed",
        f"p1={hex(nav.css_area(emu, 0)['character'])}",
    )
    emu.press(0, ["B"], frames=4, after=12)
    emu.wait_for("B recalls Mewtwo coin", lambda: nav.css_area(emu, 0)["held"], 60, every=2)
    setup = nav.start_training_match(emu, "ganondorf", None, stage="final_destination")
    _check(
        setup["players"][0]["kind"] == 22 and setup["players"][0]["state"] == 0
        and setup["players"][1]["state"] == 1,
        results, failures, "hover-float-ganondorf-and-addcpu",
        f"players={setup['players'][:2]}",
    )
    emu.advance(20)
    nav.finish_training(emu)
    emu.wait_for("Boot Menu reopened after Float/DJC Ganondorf match",
                 lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    emu.advance(15)

    # 4. Cancel Sprint (hidden by default TM_BOOT_CANCEL_ENDURANCE=0; verify gated or test live).
    DT.go_to_page(emu, "Minigames & Tech", "R")
    DT.go_to_row(emu, 2)
    emu.press(0, ["DDOWN"], frames=4, after=8)
    if DT.last_row(emu) == 3:
        _arm_row(emu, "Minigames & Tech", 3, expect_rebuild=True)
        rule = nav.select_rule(emu)
        grid = nav.css_grid(emu)
        p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
        _check(
            rule is not None and rule["drill"] == 9 and rule["areas"] == 1 and rule["locked"] == 0
            and grid == CANCEL_SPRINT_GRID and p1 is not None and p1["character"] == 0x07 and cpu is None,
            results, failures, "cancel-sprint-solo-7-grid",
            f"rule={rule} grid={[hex(x) for x in grid]} p1={p1 and hex(p1['character'])} cpu={cpu}",
        )
        emu.press(0, ["B"], frames=4, after=12)
        emu.wait_for("B recalls Fox coin on Cancel Sprint", lambda: nav.css_area(emu, 0)["held"], 60, every=2)
        nav._drop_p1_token(emu, "mewtwo")
        _check(
            not nav.css_area(emu, 0)["held"] and nav.css_area(emu, 0)["character"] == 0x2E,
            results, failures, "cancel-sprint-mewtwo-pick",
            f"p1={hex(nav.css_area(emu, 0)['character'])}",
        )
        n_lr = DT.count(emu, r"\] drills: list opened \(L\+R\)")
        emu.press(0, ["L", "R"], frames=4, after=10)
        DT.wait_count(emu, "L+R reopens Boot Menu from Cancel Sprint", r"\] drills: list opened \(L\+R\)", n_lr + 1, 120)
        emu.advance(8)
    else:
        results.append(("cancel-sprint-gated", "default TM_BOOT_CANCEL_ENDURANCE=0 clamps Minigames & Tech at 3 rows"))

    # 5. Snake C4 Recovery (Minigames & Tech row 2, drill 15):
    #    1P solo, 1-icon grid [0x26], P1 locked (locked == 1), costume X works,
    #    single B reopens Boot Menu, and Start launches match cleanly.
    _arm_row(emu, "Minigames & Tech", 2, expect_rebuild=True)
    rule = nav.select_rule(emu)
    grid = nav.css_grid(emu)
    p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
    _check(
        rule is not None and rule["drill"] == 15 and rule["areas"] == 1 and rule["p1"] == 0x26
        and rule["locked"] == 1 and grid == [0x26] and p1 is not None and p1["character"] == 0x26
        and p1["costume"] == 0 and not p1["held"] and cpu is None,
        results, failures, "snake-solo-locked-setup",
        f"rule={rule} grid={[hex(x) for x in grid]} p1={p1}",
    )
    # Costume cycling via X works while locked.
    emu.press(0, ["X"], frames=4, after=10)
    emu.press(0, ["X"], frames=4, after=10)
    _check(
        nav.css_area(emu, 0)["costume"] == 2,
        results, failures, "snake-locked-costume-cycling",
        f"costume={nav.css_area(emu, 0)['costume']}",
    )
    # Attempting to pick up Snake's locked coin with A leaves held == False.
    sx, sy = nav.icon_pos(emu, "snake")
    nav.steer(
        emu, "P1 hand onto locked Snake coin", lambda: nav._hand_xy(emu), sx, sy,
        lambda: (a := nav.css_area(emu, 0)) is not None and abs(a["x"] - sx) < 1.2 and abs(a["y"] - sy) < 1.2,
    )
    emu.press(0, ["A"], frames=4, after=10)
    _check(
        not nav.css_area(emu, 0)["held"] and nav.css_area(emu, 0)["character"] == 0x26,
        results, failures, "snake-coin-pickup-locked",
        f"held={nav.css_area(emu, 0)['held']} char={hex(nav.css_area(emu, 0)['character'])}",
    )
    # Single B on locked Snake CSS steps cleanly back to Boot Menu.
    n_b = DT.count(emu, r"\] drills: list opened \(B\)")
    emu.press(0, ["B"], frames=4, after=10)
    DT.wait_count(emu, "single B reopens Boot Menu from locked Snake CSS", r"\] drills: list opened \(B\)", n_b + 1, 120)
    emu.advance(8)
    # Re-confirm Snake (same rule -> NO rebuild) and start match -> verify addCpu fills CPU slot!
    _arm_row(emu, "Minigames & Tech", 2, expect_rebuild=False)
    setup = nav.start_training_match(emu, "snake", "snake", stage="final_destination", p1_costume=2)
    _check(
        setup["players"][0]["kind"] == 0x2A and setup["players"][0]["state"] == 0
        and setup["players"][1]["kind"] == 0x2A and setup["players"][1]["state"] == 1,
        results, failures, "snake-solo-match-launch",
        f"players={setup['players'][:2]}",
    )
    emu.advance(30)
    nav.finish_training(emu)
    emu.wait_for("Boot Menu reopened after Snake match", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    emu.advance(15)

    # 6. Strictly solo mode (Ledgedash, Tech Practice row 2, drill 1) ->
    #    1P solo with all 43 icons, then switch back to Training Lab (2P, 43 icons).
    _arm_row(emu, "Tech Practice", 2, expect_rebuild=True)
    rule = nav.select_rule(emu)
    grid = nav.css_grid(emu)
    p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
    _check(
        rule is not None and rule["drill"] == 1 and rule["areas"] == 1 and rule["locked"] == 0
        and len(grid) == 43 and p1 is not None and cpu is None,
        results, failures, "ledgedash-solo-43-grid",
        f"rule={rule} grid={len(grid)} cpu={cpu}",
    )
    n_lr = DT.count(emu, r"\] drills: list opened \(L\+R\)")
    emu.press(0, ["L", "R"], frames=4, after=10)
    DT.wait_count(emu, "L+R from Ledgedash CSS", r"\] drills: list opened \(L\+R\)", n_lr + 1, 120)
    emu.advance(8)
    _arm_row(emu, "Quick Start", 0, expect_rebuild=True)
    rule = nav.select_rule(emu)
    p1, cpu = nav.css_area(emu, 0), nav.css_area(emu, 1)
    _check(
        rule is None and len(nav.css_grid(emu)) == 43 and p1 is not None and p1["kind"] == 1
        and cpu is not None and cpu["kind"] == 2 and cpu["character"] in nav.CSS_NAME,
        results, failures, "solo-to-2p-recovery",
        f"rule={rule} grid={len(nav.css_grid(emu))} p1={p1 and hex(p1['character'])} cpu={cpu and hex(cpu['character'])}",
    )

    # 7. High-costume reset: play Mario at alt 7, finish, arm Snake -> costume resets to 0.
    setup = nav.start_training_match(emu, "mario", "fox", stage="final_destination", p1_costume=7)
    _check(
        setup["players"][0]["kind"] == 0x00 and setup["players"][0]["costume_file"] != 0,
        results, failures, "high-costume-mario-match",
        f"p1={setup['players'][0]}",
    )
    emu.advance(20)
    nav.finish_training(emu)
    emu.wait_for("Boot Menu after Mario alt 7 match", lambda: nav.drill_list_state(emu) == "open", 300, every=5)
    emu.advance(15)
    _arm_row(emu, "Minigames & Tech", 2, expect_rebuild=True)
    p1 = nav.css_area(emu, 0)
    _check(
        p1 is not None and p1["character"] == 0x26 and p1["costume"] == 0,
        results, failures, "high-costume-reset-on-preset",
        f"p1={p1}",
    )


def cmd_test_select(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-select")
    ap.add_argument("rel")
    ap.parse_args(rest)

    label = "test-select"
    backend = E.env("VIDEO_BACKEND", required=False) or "Null"
    t0 = time.monotonic()
    why, log_path = "", ""
    results, failures = [], []
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (_proc, emu):
            log_path = emu.log_path
            try:
                _run_all(emu, results, failures)
            except (Timeout, nav.NavError, HarnessError, SelectRulesError, KeyError, ValueError) as exc:
                why = f"{type(exc).__name__}: {exc}"
    except (OSError, HarnessError, Timeout, ValueError) as exc:
        why = why or str(exc)

    print()
    for name, detail in results:
        print(f"ok  {name}: {detail}")
    for line in failures:
        print(f"FAIL {line}")
    log = E.read_log(log_path) if log_path else ""
    bad = L.problems(log) if log else []
    for line in bad:
        print(f"problem: {line}")
    if not why and "back to baseline: NO" in log:
        why = "a module unload was not back to baseline"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
