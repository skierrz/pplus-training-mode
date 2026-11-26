"""Emulator test of Project+'s Code Menu interaction (run through emu.py;
settings come from the environment the same way).

  test-code-menu REL [--only NAME ...] [--aspect 16:9|4:3] [--list]
      Scenarios:
        hide-hud        Drives Controls > P+ Code Menu Shortcut through the
                        real lab menu (default Off -> On -> Off -> On -> Reset
                        All), verifies HUD/OSD/watermark/hitbox hiding
                        (diff_ref=0 against a clean Versus capture), closing an
                        open lab menu, blocking shortcuts under Code Menu,
                        Frame Advance held, recording in progress, and exact
                        restoration of P+'s Code Menu Activation (both
                        Default=0 and PM 3.6=1) and stored data.cmnu bytes
                        across Training exit paths (Lab Exit, Training Finish,
                        Reset All).
        block-training  Default (Off) behaviour: L+R+D-pad Down leaves Code
                        Menu closed and the game unpaused in Training Lab with
                        our own shortcuts working; stored Code Menu values are
                        byte-identical after Training; Main Menu (Versus > Codes)
                        still opens it; and regular offline Versus still opens
                        it (also providing the clean reference capture).
"""
import argparse
import os
import re
import time

import drill_tests as DT
import emu as E
import lab_tests as LT
import loader_tests as L
import nav
import shots as SH
from harness import HarnessError, Timeout

# P+ 3.2.1 Code Menu memory layout (Source/CodeMenu/CodeMenu.asm, pf/menu3/data.cmnu).
CM_DATA = 0x804E0000
CM_ROOT_PTR = 0x804E0004
CM_ROOT = 0x804E07D8
CM_END = 0x804E2510
CM_FLAG = 0x804E0034
CM_OPEN = 4
CM_ACTIVATION_VAL = 0x804E0C2C  # 0 Default, 1 PM 3.6, 2 OFF
# P+'s own runtime state inside data.cmnu (0x804E07D8..0x804E2510):
# - P1..P4 Character Select (+0x8 value, +0x10 default), synced by
#   CodeMenu.asm:3618-3645 on fighter load;
# - root page scroll index (CM_ROOT + 0x0C, 4 bytes), updated by
#   Print Code Menu (CodeMenu.asm:140-154) the first time the menu opens.
# Every other byte of data.cmnu is static unless changed by the player or mod.
CM_RUNTIME_SYNC_BYTES = frozenset(
    [CM_ROOT + 0x0C + b for b in range(4)]
    + [
        addr + off + b
        for addr in (0x804E0F34, 0x804E1410, 0x804E1674, 0x804E18D8)
        for off in (0x08, 0x10)
        for b in range(4)
    ]
)

# Settings offsets (src/main/settings.h) and Controls page row index.
SET_ALLOW_CODE_MENU = 32
CONTROLS_CODE_MENU = 8  # Controls page row index of "P+ Code Menu Shortcut"

# Log lines from stub/render_hook.cpp and main/record/record.cpp.
RX_DRAW_SKIP = re.compile(r"render hook: code menu open, draw skipped")
RX_DRAW_RESUME = re.compile(r"render hook: code menu closed, (\d+) draw\(s\) skipped")
RX_REC_BLOCK = re.compile(r"record: test block at ([0-9a-fA-F]+)")


class CodeMenuError(RuntimeError):
    pass


def open_code_menu(emu):
    """Press L + R + D-pad Down through the real controller path."""
    emu.hold(0, buttons=["L", "R", "DDOWN"], l=255, r=255)
    emu.advance(6)
    emu.neutral(0)
    emu.advance(20)
    return emu.u32(CM_FLAG)


def close_code_menu(emu, button="B"):
    """Close an open Code Menu with B or Start."""
    for _ in range(8):
        emu.press(0, [button], frames=3, after=12)
        if emu.u32(CM_FLAG) != CM_OPEN:
            break
    if emu.u32(CM_FLAG) == CM_OPEN:
        raise CodeMenuError(f"Code Menu did not close with {button}")


def pick_drill(emu, row, reopen=True, page_shift=0):
    """On Training's character select: open the drill list if needed, shift
    page with L/R (`page_shift` < 0 presses L, > 0 presses R), pick `row`,
    and confirm with A."""
    if reopen:
        state = nav.drill_list_state(emu)
        if state == "closed":
            emu.press(0, ["L", "R"], frames=4, after=15)
            emu.wait_for("drill list reopen", lambda: nav.drill_list_state(emu) == "open", 120, every=2)
    else:
        emu.wait_for("drill list open", lambda: nav.drill_list_state(emu) == "open", 300, every=2)
    emu.advance(10)
    for _ in range(abs(page_shift)):
        emu.press(0, ["L" if page_shift < 0 else "R"], frames=3, after=12)
    # Return to row 0 on the page first (reopening restores the last row).
    for _ in range(20):
        emu.press(0, stick=(128, 228), frames=2, after=4)
    for _ in range(row):
        emu.press(0, stick=(128, 28), frames=2, after=5)
    emu.press(0, ["A"], frames=3, after=20)
    emu.wait_for("drill list close", lambda: nav.drill_list_state(emu) == "closed", 180, every=2)


def leave_css_to_versus_menu(emu):
    """From Training CSS (boot menu open or closed): exit through the Boot
    Menu (B) to Brawl's SOLO submenu, then step to VERSUS."""
    if nav.drill_list_state(emu) == "closed":
        emu.press(0, ["L", "R"], frames=4, after=12)
        emu.wait_for("boot menu open before Solo exit", lambda: nav.drill_list_state(emu) == "open", 120, every=2)
    emu.advance(12)
    emu.press(0, ["B"], frames=4, after=10)
    emu.wait_for("Main Menu from Training Boot Menu", lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
    emu.neutral(0)
    emu.advance(25)
    # Back out of SOLO submenu to SELECT MODE, then move LEFT to VERSUS and press A.
    emu.press(0, ["B"], frames=3, after=25)
    emu.press(0, stick=(28, 128), frames=3, after=15)
    emu.press(0, ["A"], frames=3, after=25)


def set_allow_code_menu(lab, want_on, shot_name=None, shots=None):
    """Toggle Lab > Controls > P+ Code Menu Shortcut through the real lab menu."""
    lab.open_menu()
    lab.go_controls()
    lab.down(CONTROLS_CODE_MENU)
    if shot_name is not None and shots is not None:
        shots.append(lab.emu.screenshot(shot_name))
    if want_on:
        lab.toggle_on(SET_ALLOW_CODE_MENU)
    else:
        lab.toggle_off(SET_ALLOW_CODE_MENU)
    lab.close_menu()


def reset_all_settings(lab):
    """Run Reset Settings (double A on Draft C rail) through the real menu."""
    lab.open_menu()
    lab.main_to(LT.MAIN_RESET)
    lab.tap(["A"])
    lab.tap(["A"])
    lab.close_menu()


def exit_via_menu(lab, in_drill=False):
    """Leave the match via Return to CSS (bottom Draft C rail utility)."""
    emu = lab.emu
    lab.open_menu()
    lab.main_to(LT.MAIN_EXIT)
    lab.tap(["A"])
    emu.wait_for("Training CSS after menu Exit", lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
    emu.advance(30)


def check_cm_unchanged(emu, cm_before, expected_act, context):
    """Assert P+'s Code Menu Activation equals `expected_act` (never stuck at
    OFF) and every non-character-sync byte of data.cmnu matches `cm_before`."""
    act_after = emu.u32(CM_ACTIVATION_VAL)
    cm_after = emu.read(CM_ROOT, CM_END - CM_ROOT)
    diffs = [i for i in range(len(cm_before))
             if cm_before[i] != cm_after[i] and (CM_ROOT + i) not in CM_RUNTIME_SYNC_BYTES]
    if act_after != expected_act or diffs:
        first = [hex(CM_ROOT + i) for i in diffs[:8]]
        raise CodeMenuError(f"{context}: Code Menu data changed (act {expected_act}->{act_after}, diffs at {first})")


def check_clean_code_menu_shot(path, ref_path=None):
    """Verify a screenshot with the Code Menu open shows only P+'s Code Menu:
    black background, green/yellow/white menu text, zero bleed from our timing
    bar, side panel, OSD, watermark, or 3D Wavedash Target column."""
    w, h, ch, rows = SH.read_png(path)

    def px(x, y):
        i = x * ch
        r = rows[y]
        return r[i], r[i + 1], r[i + 2]

    def rect_px(x0, y0, x1, y1):
        return (int(x0 * w / 640), int(y0 * h / 480), int(x1 * w / 640), int(y1 * h / 480))

    problems = []

    # 1. Wavedash timing bar region (top band): Code Menu only draws green
    #    header text starting around y=48; the timing bar has red/orange/yellow
    #    cells and white text at y=20..92.
    x0, y0, x1, y1 = rect_px(180, 18, 460, 44)
    bar_bad = sum(1 for y in range(y0, y1) for x in range(x0, x1) if max(px(x, y)) > 15)
    if bar_bad > 0:
        problems.append(f"timing bar pixels on Code Menu: {bar_bad}")

    # 2. Right side overlay band: pure black in Code Menu in both 16:9 and
    #    native 4:3 (in 4:3 the "Endless Friendlies Stage Selection: Random"
    #    line reaches x~620 around y=220..248, while y=40..185 and y=265..440
    #    stay pure black).
    x0, y0, x1, y1 = rect_px(555, 40, 635, 185)
    x2, y2, x3, y3 = rect_px(555, 265, 635, 440)
    side_bad = (sum(1 for y in range(y0, y1) for x in range(x0, x1) if max(px(x, y)) > 15)
                + sum(1 for y in range(y2, y3) for x in range(x2, x3) if max(px(x, y)) > 15))
    if side_bad > 0:
        problems.append(f"side panel / target pixels on Code Menu: {side_bad}")

    # 3. Left OSD band (x=5..46, y=40..360): pure black in Code Menu in both
    #    16:9 and native 4:3 (4:3 Code Menu text starts at x=49.5; our left OSD
    #    starts at x=12).
    x0, y0, x1, y1 = rect_px(5, 40, 46, 360)
    osd_bad = sum(1 for y in range(y0, y1) for x in range(x0, x1) if max(px(x, y)) > 15)
    if osd_bad > 0:
        problems.append(f"OSD pixels on Code Menu: {osd_bad}")

    # 4. Watermark corner (bottom-right, x=480..635, y=450..478): pure black.
    x0, y0, x1, y1 = rect_px(480, 450, 635, 478)
    wm_bad = sum(1 for y in range(y0, y1) for x in range(x0, x1) if max(px(x, y)) > 15)
    if wm_bad > 0:
        problems.append(f"watermark pixels on Code Menu: {wm_bad}")

    # 5. Code Menu text must actually be present (green header + yellow row).
    gx0, gy0, gx1, gy1 = rect_px(110, 45, 500, 175)
    green_px = sum(1 for y in range(gy0, gy1) for x in range(gx0, gx1)
                   if px(x, y)[1] > 150 and px(x, y)[0] < 60 and px(x, y)[2] < 60)
    yx0, yy0, yx1, yy1 = rect_px(110, 180, 400, 230)
    yellow_px = sum(1 for y in range(yy0, yy1) for x in range(yx0, yx1)
                    if px(x, y)[0] > 180 and px(x, y)[1] > 180 and px(x, y)[2] < 60)
    if green_px < 1000 or yellow_px < 200:
        problems.append(f"Code Menu text missing or clipped (green={green_px}, yellow={yellow_px})")

    # 6. Full-frame match against clean reference capture (no plugin draw).
    diff_px = None
    if ref_path is not None:
        rw, rh, rch, rrows = SH.read_png(ref_path)
        if (rw, rh) != (w, h):
            problems.append(f"size mismatch vs ref ({w}x{h} vs {rw}x{rh})")
        else:
            diff_px = 0
            for y in range(h):
                ra, rb = rows[y], rrows[y]
                for x in range(w):
                    ia, ib = x * ch, x * rch
                    if ra[ia:ia + 3] != rb[ib:ib + 3]:
                        diff_px += 1
            if diff_px > 0:
                problems.append(f"{diff_px} pixel(s) differ from clean Code Menu reference")

    return problems, {"bar_bad": bar_bad, "side_bad": side_bad, "osd_bad": osd_bad, "wm_bad": wm_bad,
                      "green": green_px, "yellow": yellow_px, "diff_ref": diff_px}


def scheduler_stop_byte(emu):
    """gfTaskScheduler +0xB (0x805B8A0B): 1 while Code Menu pauses, 0x8 for lab freeze."""
    sched = emu.ptr(nav.TASK_SCHEDULER)
    return emu.u8(sched + nav.SCHEDULER_FLAGS) if sched else None


def scenario_hide_hud(emu, aspect, notes, shots, ref_holder):
    """Exercises Controls > P+ Code Menu Shortcut through the real lab menu,
    verifying HUD/OSD/watermark/3D hitbox hiding, shortcut blocking, menu
    overlap, Frame Advance held, recording pause/resume, Reset All Settings,
    exact restoration across every exit route, and non-Training modes."""
    tag = aspect.replace(":", "x")
    if emu.u32(CM_ROOT_PTR) != CM_ROOT:
        raise CodeMenuError(f"Code Menu not loaded at CSS (root ptr {emu.u32(CM_ROOT_PTR):#x})")
    act_before = emu.u32(CM_ACTIVATION_VAL)
    cm_before = emu.read(CM_ROOT, CM_END - CM_ROOT)

    # ---- Part 1: Training Lab (Controls row, OSD + 3D hitbox hide, overlap,
    #      Frame Advance, Recording, toggle Off, Reset All, Lab Exit, Finish) ----
    pick_drill(emu, 0, reopen=(nav.drill_list_state(emu) == "closed"))
    nav.start_training_match(emu, "mario", "fox", stage="final_destination")
    emu.advance(30)
    lab = LT.Lab(emu, [])
    lab.find_settings()
    p1 = lab.f.by_player(0)
    cpu = lab.f.by_player(1)

    # Default Off: combo blocked.
    if lab.setting(SET_ALLOW_CODE_MENU) != 0 or emu.u32(CM_ACTIVATION_VAL) != 2:
        raise CodeMenuError(f"expected default blocked (row={lab.setting(SET_ALLOW_CODE_MENU)}, "
                            f"act={emu.u32(CM_ACTIVATION_VAL)})")
    if open_code_menu(emu) != 0:
        raise CodeMenuError("Code Menu opened with Controls > P+ Code Menu Shortcut = Off")

    # Turn Controls > P+ Code Menu Shortcut On through the real lab menu
    # (capturing the Controls page with the cursor on the row for text-fit check).
    set_allow_code_menu(lab, True, shot_name=f"code-menu-controls-lab-{tag}", shots=shots)
    if emu.u32(CM_ACTIVATION_VAL) != act_before:
        raise CodeMenuError(f"toggling row On did not restore Activation ({emu.u32(CM_ACTIVATION_VAL)} != {act_before})")

    # Turn on 3D Hitboxes (Display > Hitboxes) and Wavedash OSD so both 2D
    # HUD/OSD/watermark and 3D GX overlays are actively drawn right before
    # opening Code Menu.
    lab.open_menu()
    lab.main_to(LT.MAIN_DISPLAY)
    lab.tap(["A"])
    lab.down(1)  # Hitboxes: Off -> P1
    lab.tap(stick=LT.RIGHT)
    lab.back_to_main(1)
    lab.close_menu()
    emu.h.call("write_mem", addr=lab.settings + LT.SET_OSD_POSITION + 4, hex="01")
    emu.advance(5)
    pos = len(emu.log_text())
    emu.hold(0, buttons=["Y"])
    emu.advance(2)
    emu.neutral(0)
    emu.advance(4)
    emu.hold(0, buttons=["L"], l=255, stick=(200, 60))
    emu.advance(6)
    emu.neutral(0)
    emu.advance(15)
    if "osd: P1 id " not in emu.log_text()[pos:]:
        raise CodeMenuError("Wavedash OSD did not post in plain Lab")
    before_shot = emu.screenshot(f"code-menu-lab-before-{tag}")
    shots.append(before_shot)
    if not SH.watermark_visible(before_shot[0]):
        raise CodeMenuError("watermark not visible before Code Menu in Lab")

    pos = len(emu.log_text())
    if open_code_menu(emu) != CM_OPEN:
        raise CodeMenuError(f"Code Menu did not open in Lab with row On (flag {emu.u32(CM_FLAG)})")
    if not RX_DRAW_SKIP.search(emu.log_text()[pos:]):
        raise CodeMenuError("render hook did not log skipping draw in plain Lab")
    lab_shot = emu.screenshot(f"code-menu-lab-open-{tag}")
    shots.append(lab_shot)
    problems, stats = check_clean_code_menu_shot(lab_shot[0], ref_holder.get("path"))
    notes.append(f"hide-hud lab+osd ({aspect}): stats={stats}")
    if problems:
        raise CodeMenuError(f"plain Lab + OSD with Code Menu open ({aspect}): {'; '.join(problems)}")

    # Savestate and Place CPU shortcuts must not fire while Code Menu is open.
    cpu_x_before = lab.f.state(cpu)["x"]
    pos_sc = len(emu.log_text())
    emu.hold(0, buttons=["DRIGHT"])
    emu.advance(15)
    emu.neutral(0)
    emu.advance(5)
    emu.press(0, ["DLEFT"], frames=3, after=8)
    emu.hold(0, buttons=["DDOWN"])
    emu.advance(12)
    emu.neutral(0)
    emu.advance(5)
    emu.press(0, ["DUP"], frames=3, after=8)  # restore cursor to row 0
    sc_log = emu.log_text()[pos_sc:]
    if "savestate: saved" in sc_log or "savestate: loaded" in sc_log:
        raise CodeMenuError("savestate shortcut fired while Code Menu was open")
    if "lab: menu opened" in sc_log:
        raise CodeMenuError("lab menu opened while Code Menu was open")
    pos_cl = len(emu.log_text())
    close_code_menu(emu, button="B")
    log_close = emu.log_text()[pos_cl:]
    m = RX_DRAW_RESUME.search(log_close)
    if not m or int(m.group(1)) < 10:
        raise CodeMenuError(f"missing/low skipped-draw resume line on Lab Code Menu close: {log_close!r}")
    emu.advance(12)
    cpu_x_after = lab.f.state(cpu)["x"]
    if abs(cpu_x_after - cpu_x_before) > 2.0:
        raise CodeMenuError(f"CPU moved under Code Menu ({cpu_x_before:.2f} -> {cpu_x_after:.2f})")
    after_shot = emu.screenshot(f"code-menu-lab-after-{tag}")
    shots.append(after_shot)
    if not SH.watermark_visible(after_shot[0]):
        raise CodeMenuError("watermark did not return after closing Code Menu in Lab")
    notes.append(f"hide-hud lab ({aspect}): closed cleanly after {m.group(1)} skipped render(s)")

    # Open lab menu with Start, then open Code Menu over it: lab menu closes;
    # closing Code Menu with Start must not re-open lab menu.
    pos_ov = len(emu.log_text())
    emu.press(0, ["START"], frames=3, after=12)
    if "lab: menu opened" not in emu.log_text()[pos_ov:]:
        raise CodeMenuError("lab menu did not open with Start for overlap check")
    pos_ov2 = len(emu.log_text())
    if open_code_menu(emu) != CM_OPEN:
        raise CodeMenuError("Code Menu did not open over lab menu")
    if "lab: menu closed" not in emu.log_text()[pos_ov2:]:
        raise CodeMenuError("lab menu stayed open when Code Menu opened over it")
    pos_ov3 = len(emu.log_text())
    close_code_menu(emu, button="START")
    if "lab: menu opened" in emu.log_text()[pos_ov3:]:
        raise CodeMenuError("Start closing Code Menu also opened our lab menu")
    emu.advance(12)

    # Frame Advance held across Code Menu open/close stays frozen on same frame.
    lab.open_menu()
    lab.main_to(LT.MAIN_GENERAL)
    lab.tap(["A"])
    lab.tap(["A"])  # General > Frame Advance: Off -> On
    lab.back_to_main(1)
    lab.close_menu()
    if lab.setting(LT.SET_FRAME_ADVANCE) != 1:
        raise CodeMenuError("Frame Advance did not turn on via General > Frame Advance")
    emu.advance(6)
    fa_anim0 = lab.f.state(p1)["anim"]
    emu.advance(10)
    if lab.f.state(p1)["anim"] != fa_anim0:
        raise CodeMenuError("Frame Advance did not hold fighter before Code Menu")
    if open_code_menu(emu) != CM_OPEN:
        raise CodeMenuError("Code Menu did not open with Frame Advance held")
    emu.hold(0, buttons=["L"], l=255)  # must not step under Code Menu
    emu.advance(3)
    emu.neutral(0)
    emu.advance(10)
    close_code_menu(emu, button="B")
    emu.advance(10)
    fa_anim1 = lab.f.state(p1)["anim"]
    fa_stop = scheduler_stop_byte(emu)
    if fa_anim1 != fa_anim0 or (fa_stop & 0x8) == 0:
        raise CodeMenuError(f"Frame Advance state lost across Code Menu (anim {fa_anim0}->{fa_anim1}, stop={fa_stop:#x})")
    lab.open_menu()
    lab.main_to(LT.MAIN_GENERAL)
    lab.tap(["A"])
    lab.tap(["A"])  # General > Frame Advance: On -> Off
    lab.back_to_main(1)
    lab.close_menu()
    emu.advance(10)
    notes.append(f"hide-hud lab ({aspect}): Frame Advance stayed frozen across Code Menu (anim={fa_anim0:.1f})")

    # Recording in progress pauses cleanly while Code Menu is open and resumes after.
    rec_matches = RX_REC_BLOCK.findall(emu.log_text())
    if not rec_matches:
        raise CodeMenuError("record test block address not found in log")
    rec_addr = int(rec_matches[-1], 16)
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.tap(["A"])  # Save Positions
    lab.close_menu()
    emu.advance(20)
    lab.open_menu()
    lab.main_to(LT.MAIN_RECORDING)
    lab.tap(["A"])
    lab.down(1)  # HMN Mode -> Record
    lab.tap(stick=LT.RIGHT)
    lab.close_menu()
    emu.advance(20)
    rec_t0 = emu.u32(rec_addr + 0x14)
    if rec_t0 < 5:
        raise CodeMenuError(f"recording did not start advancing before Code Menu (t={rec_t0})")
    if open_code_menu(emu) != CM_OPEN:
        raise CodeMenuError("Code Menu did not open during active recording")
    rec_t_open0 = emu.u32(rec_addr + 0x14)
    emu.advance(30)
    rec_t_open1 = emu.u32(rec_addr + 0x14)
    if rec_t_open1 != rec_t_open0:
        raise CodeMenuError(f"recording ticked while Code Menu was open ({rec_t_open0} -> {rec_t_open1})")
    close_code_menu(emu, button="B")
    emu.advance(20)
    rec_t_after = emu.u32(rec_addr + 0x14)
    if rec_t_after <= rec_t_open1 + 5:
        raise CodeMenuError(f"recording did not resume after Code Menu ({rec_t_open1} -> {rec_t_after})")
    notes.append(f"hide-hud lab ({aspect}): recording paused at t={rec_t_open0}..{rec_t_open1}, resumed to {rec_t_after}")

    # Switch row back Off -> blocked again immediately.
    set_allow_code_menu(lab, False)
    if emu.u32(CM_ACTIVATION_VAL) != 2 or open_code_menu(emu) != 0:
        raise CodeMenuError("switching row back Off did not re-block Code Menu")

    # Switch On, then Reset All Settings -> back Off and blocked again.
    set_allow_code_menu(lab, True)
    if emu.u32(CM_ACTIVATION_VAL) != act_before:
        raise CodeMenuError("switching row On before Reset All did not restore Activation")
    reset_all_settings(lab)
    if lab.setting(SET_ALLOW_CODE_MENU) != 0 or emu.u32(CM_ACTIVATION_VAL) != 2 or open_code_menu(emu) != 0:
        raise CodeMenuError("Reset All Settings did not re-block Code Menu")
    notes.append(f"hide-hud lab ({aspect}): row Off and Reset All Settings both re-blocked cleanly")

    # Exit route 1: Lab Menu > Exit (after Reset All) restores Activation & data.cmnu.
    exit_via_menu(lab, in_drill=False)
    check_cm_unchanged(emu, cm_before, act_before, "after Lab Menu Exit + Reset All")

    # Exit route 2: non-default player Activation = 1 (PM 3.6) restored after Brawl Finish.
    emu.h.call("write_mem", addr=CM_ACTIVATION_VAL, hex="00000001")
    pick_drill(emu, 0, reopen=True)
    nav.start_training_match(emu, "mario", "fox", stage="final_destination")
    emu.advance(30)
    lab = LT.Lab(emu, [])
    lab.find_settings()
    if emu.u32(CM_ACTIVATION_VAL) != 2 or open_code_menu(emu) != 0:
        raise CodeMenuError("PM 3.6 (1) was not blocked in Training Lab")
    nav.finish_training(emu)
    emu.advance(40)
    if emu.u32(CM_ACTIVATION_VAL) != 1:
        raise CodeMenuError(f"Training Finish did not restore player PM 3.6 (1): got {emu.u32(CM_ACTIVATION_VAL)}")
    emu.h.call("write_mem", addr=CM_ACTIVATION_VAL, hex="%08x" % act_before)
    check_cm_unchanged(emu, cm_before, act_before, "after Lab Finish (PM 3.6 restore)")
    notes.append(f"hide-hud ({aspect}): PM 3.6 (1) blocked in Lab and restored on Training Finish")

    return lab_shot[0]


def scenario_block_training(emu, aspect, notes, shots, ref_holder):
    """Default (Off) in-match block:
    1. Training Lab: L+R+DDOWN keeps Code Menu closed, game unpaused, shortcuts work.
    2. Code Menu stored values byte-identical before and after Training.
    3. Main Menu (Versus > Codes): Code Menu still opens.
    4. Regular offline Versus match: L+R+DDOWN still opens Code Menu (clean ref shot).
    """
    tag = aspect.replace(":", "x")
    if emu.u32(CM_ROOT_PTR) != CM_ROOT:
        raise CodeMenuError(f"Code Menu not loaded at CSS (root ptr {emu.u32(CM_ROOT_PTR):#x})")
    cm_before = emu.read(CM_ROOT, CM_END - CM_ROOT)
    act_before = emu.u32(CM_ACTIVATION_VAL)
    notes.append(f"block-training: CSS baseline Code Menu Activation={act_before}, snapshot={len(cm_before)} bytes")

    # ---- 1. Training Lab ----
    pick_drill(emu, 0, reopen=(nav.drill_list_state(emu) == "closed"))
    nav.start_training_match(emu, "mario", "fox", stage="final_destination")
    emu.advance(30)
    lab = LT.Lab(emu, [])
    p1 = lab.f.by_player(0)
    flag = open_code_menu(emu)
    stop = scheduler_stop_byte(emu)
    anim0 = lab.f.state(p1)["anim"]
    emu.advance(15)
    anim1 = lab.f.state(p1)["anim"]
    notes.append(f"block-training lab: after L+R+DDOWN flag={flag}, stop={stop}, anim {anim0:.1f} -> {anim1:.1f}")
    if flag != 0:
        raise CodeMenuError(f"Code Menu opened in Training Lab with default settings (flag={flag})")
    if stop != 0 or anim1 == anim0:
        raise CodeMenuError(f"game paused on L+R+DDOWN in Training Lab (stop={stop}, anim {anim0:.1f}->{anim1:.1f})")
    pos = len(emu.log_text())
    emu.press(0, ["START"], frames=3, after=12)
    if "lab: menu opened" not in emu.log_text()[pos:]:
        raise CodeMenuError("lab menu did not open with Start after blocked combo in Lab")
    emu.press(0, ["START"], frames=3, after=12)
    if "lab: menu closed" not in emu.log_text()[pos:]:
        raise CodeMenuError("lab menu did not close with Start in Lab")
    pos_sv = len(emu.log_text())
    emu.hold(0, buttons=["DRIGHT"])
    emu.advance(15)
    emu.neutral(0)
    emu.advance(10)
    if "savestate: saved" not in emu.log_text()[pos_sv:]:
        raise CodeMenuError("savestate save did not work after blocked combo in Lab")
    nav.finish_training(emu)
    emu.advance(40)
    check_cm_unchanged(emu, cm_before, act_before, "block-training after Lab Finish")
    notes.append(f"block-training after Training: Activation={emu.u32(CM_ACTIVATION_VAL)}, differing option bytes=0")

    # ---- 3. Main Menu > Versus > Codes still opens Code Menu ----
    # Restore 16:9 before entering muMenuMain so Brawl's NAND autosave
    # (/autosv0.bin) never leaves 4:3 behind for subsequent tests.
    if aspect != "16:9":
        LT.set_aspect(emu, "16:9", lambda s: notes.append(f"restore before Main Menu: {s}"))
    leave_css_to_versus_menu(emu)
    emu.press(0, stick=(128, 28), frames=3, after=15)
    emu.press(0, stick=(228, 128), frames=3, after=15)
    emu.press(0, ["A"], frames=3, after=25)
    mm_flag = emu.u32(CM_FLAG)
    notes.append(f"block-training Main Menu > Versus > Codes: flag={mm_flag}")
    if mm_flag != CM_OPEN:
        raise CodeMenuError(f"Main Menu Code Menu did not open after Training (flag={mm_flag})")
    shots.append(emu.screenshot(f"code-menu-mainmenu-{tag}"))
    close_code_menu(emu, button="B")

    # ---- 5. Regular offline Versus match still opens Code Menu ----
    emu.press(0, stick=(28, 128), frames=3, after=12)
    emu.press(0, stick=(128, 228), frames=3, after=12)
    emu.press(0, ["A"], frames=3, after=20)
    emu.wait_for("Versus CSS", lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
    if nav.scene(emu)[1] != "sqVsMelee":
        raise CodeMenuError(f"expected sqVsMelee, got {nav.scene(emu)[:2]}")
    emu.advance(30)
    for port in (0, 1):
        a = nav.css_area(emu, port)
        if not a or a["kind"] == 0:
            emu.press(port, ["A"])
            emu.wait_for(f"Versus P{port + 1} join", lambda p=port: nav.css_area(emu, p)["kind"] in (1, 2), 120, every=2)
    emu.advance(10)
    if nav.css_area(emu, 0)["held"]:
        nav.drop_own_token(emu, 0, "mario")
    if nav.css_area(emu, 1)["held"]:
        nav.drop_own_token(emu, 1, "fox")
    pos_vs = len(emu.log_text())
    nav.press_until(emu, "Versus stage select", ["START"], lambda: nav.scene(emu)[0] == "scSelStage", 600)
    nav.pick_stage(emu, "final_destination")
    nav.wait_match_start(emu)
    emu.advance(30)
    if "main: attached" in emu.log_text()[pos_vs:]:
        raise CodeMenuError("main module unexpectedly attached in regular offline Versus match")
    vs_flag = open_code_menu(emu)
    notes.append(f"block-training Versus match: after L+R+DDOWN flag={vs_flag}")
    if vs_flag != CM_OPEN:
        raise CodeMenuError(f"Code Menu did not open in regular offline Versus match (flag={vs_flag})")
    ref_shot = emu.screenshot(f"code-menu-vs-ref-{tag}")
    shots.append(ref_shot)
    ref_holder["path"] = ref_shot[0]
    close_code_menu(emu, button="B")


SCENARIOS = [
    ("hide-hud", scenario_hide_hud),
    ("block-training", scenario_block_training),
]


def cmd_test_code_menu(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-code-menu")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--aspect", choices=("16:9", "4:3"), default="16:9")
    ap.add_argument("--list", action="store_true")
    opts = ap.parse_args(rest)
    names = [n for n, _ in SCENARIOS]
    if opts.list:
        for n in names:
            print(n)
        return 0
    if opts.only:
        bad = [n for n in opts.only if n not in names]
        if bad:
            E.die(f"unknown scenario(s): {', '.join(bad)}; known: {', '.join(names)}")
    selected = [(n, fn) for n, fn in SCENARIOS if not opts.only or n in opts.only]
    label = "test-code-menu"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    notes, shots = [], []
    failed = []
    log_path = ""
    state = {"step": "boot"}
    ref_holder = {}
    hide_shots = None
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + E.FOUR_PADS_ARGS) as (proc, emu):
            log_path = emu.log_path
            nav.boot_to_training_css(emu, drill_list="keep")
            LT.set_aspect(emu, opts.aspect, lambda s: notes.append(s))
            for name, fn in selected:
                state["step"] = name
                emu.mark(name)
                try:
                    res = fn(emu, opts.aspect, notes, shots, ref_holder)
                    if name == "hide-hud":
                        hide_shots = res
                    notes.append(f"PASS {name}")
                except (Timeout, nav.NavError, HarnessError, LT.LabError, CodeMenuError) as e:
                    failed.append((name, str(e)))
                    notes.append(f"FAIL {name}: {e}")
                    try:
                        shots.append(emu.screenshot(f"fail-code-menu-{name}"))
                    except (OSError, HarnessError, ValueError):
                        pass
                    break
            # Cross-check hide-hud capture pixel-for-pixel against the clean
            # Versus reference capture when both scenarios ran.
            if not failed and hide_shots is not None and ref_holder.get("path"):
                probs, st = check_clean_code_menu_shot(hide_shots, ref_holder["path"])
                notes.append(f"ref-compare lab+osd ({opts.aspect}): diff_ref={st['diff_ref']}")
                if probs:
                    failed.append(("hide-hud", f"lab+osd vs clean reference: {'; '.join(probs)}"))
    except (OSError, HarnessError, Timeout, ValueError) as e:
        failed.append((state["step"], str(e)))
    log = E.read_log(log_path) if log_path else ""
    print()
    for n in notes:
        print("  " + n)
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    for path, w, h in shots:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if failed:
        keys = [k for k, _ in failed]
        print("failed --only: " + " ".join(keys))
        return L.report(log_path, "; ".join(f"{k}: {m}" for k, m in failed), label)
    if bad:
        return L.report(log_path, "problem lines in the log", label)
    print(f"{label}: PASS ({opts.aspect})")
    return 0
