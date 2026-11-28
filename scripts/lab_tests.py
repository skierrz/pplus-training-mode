"""Emulator test of the lab menu and the features it drives (run through
emu.py; settings come from the environment the same way).

  test-lab REL [--presses N] [--aspect 16:9|4:3]
      One boot, Mario (P1) against a Fox CPU on Final Destination:
        1. Lab Menu Button starts at Start; Start opens the menu and its
           row is set to Z in Pause, for steps 1 to 4; pause, open the lab
           menu with Z, check Training's own pause menu does not move while
           ours is open;
        2. set Player Percent and CPU Percent by scripted input (taps, R for
           fast key repeat; repeat timings and clamping checked),
           lock the CPU's, turn the debug overlay on, then choose info
           display presets and sizes;
        3. close, resume; read both fighters' damage from the game's memory;
           P1 hits the CPU and the CPU's damage must come back to the locked
           value; hitstun and shield stun are recorded frame by frame; with
           the lock off, a CPU Percent set through Training's pause and
           damage dealt survive the pause closing (the damage HUD showing
           it), and Training's own CPU Damage changed in its pause still
           applies; Infinite Shields
           Health runs 0-60 (default 60) and holds the CPU's shield; the
           cursor skips rows not built yet;
        4. turn frame advance on: the game must stand still, then advance by
           exactly one frame per press of L (the default), over N
           presses; a light analog L press must not; Start must still open
           Training's pause menu;
        5. Lab Menu Button = Start: Start opens the menu over the held game,
           where it was left, with the damage HUD hidden (layer and
           pixels) and back on close; its screenshots are compared with
           the menu geometry (shots.check_menu); B does not close it,
           the Y shortcuts work, Start closes it; the match timer shows,
           counts, hides with HUD Off, and stops with
           frame advance; the Training Menu row, and Z in the menu, open
           Training's pause, Start closes it and the next Start opens the
           lab menu again, Y + Z staying Re-Save Positions; Main Menu > Exit
           (A through the pad) leaves for
           Training's character select with Training's pause never open on
           the way (its flag polled every frame, and no "match: paused"
           line); the match module unloads back to its heap baseline and
           the tracker took off every observer itself (none lost);
        6. a second match: the settings must have been kept (Lab Menu
           Button still Start); frame advance on (Y+A); Z to Training's
           pause and back with frame advance holding the game; then Exit again:
           the same checks with frame advance holding the game;
        7. a third match: Lab Menu Button back to Z, Training's pause, the
           lab menu over it with Z, Exit: the pause closes as Finish
           closes it and the match ends the same way;
        8. a fourth match left with Training's pause menu Finish (Brawl's
           own way still works); every match module unload of the run left
           the same heap.
      Screenshots of the menu pages, the info panels and frame advance are
      saved in the screenshot folder. --aspect sets the game's screen
      setting on the character select first.

Fighter data is read straight from game memory here, independently of the
plugin, at offsets documented in src/main/game/fighters.cpp.
"""
import argparse
import os
import re
import math
import struct
import time

import emu as E
import loader_tests as L
import nav
import shots
from harness import HarnessError, Timeout

# ftEntryManager (P+ hard-codes this address; read live in our runs):
# +0 -> the ftEntry array (0x244 each), +4 entry count.
ENTRY_MANAGER = 0x80624780
ENTRY_SIZE = 0x244
# StOperatorInfoTraining options: IfMinigameTraining+0x24 is the CPU
# behaviour (P+ option 6 makes the CPU hold shield).
CPU_BEHAVIOUR = 0x24
CPU_SHIELD = 6
STATUS_GUARD_DAMAGE = 0x1D

# Lab menu categories and Draft C rail utility indices (src/main/ui/pages/):
# Rail items 0..6 are the 7 top-level categories; 7..9 are pinned utilities.
MAIN_GENERAL, MAIN_SCENARIOS, MAIN_CPU, MAIN_RECORDING, MAIN_DISPLAY, MAIN_OSD, MAIN_STAGE_CAST = range(7)
MAIN_RESET, MAIN_TRAINING, MAIN_EXIT = 7, 8, 9
MAIN_DISABLED, MAIN_ROWS = (), 10
GEN_FRAME_ADVANCE, GEN_PLAYER_PERCENT, GEN_LOCK_PLAYER, GEN_PLAYER_RAND = 0, 1, 2, 3
GEN_PLAYER_SHIELDS, GEN_GAME_SPEED, GEN_CAMERA, GEN_STALING, GEN_CONTROLS = 4, 5, 6, 7, 8
GEN_RAIL_ROWS, GEN_TABS_ROWS = 9, 12
GEN_ROWS = GEN_RAIL_ROWS
CPU_PERCENT_ROW, CPU_LOCK_ROW = 0, 1
CPU_DEFENSE_ROW, CPU_SHIELD_HEALTH_ROW = 8, 1
INFO_PRESET, INFO_SIZE = 0, 1
CONTROLS_MENU_BUTTON, CONTROLS_CODE_MENU, CONTROLS_MENU_STYLE = 7, 8, 9
OSD_POWERSHIELD = 8   # visible row in OSDs > Individual OSDs at default
OSD_ROWS = 21         # visible Individual OSDs rows at default (Wavedash .. P+ Tech OSD)
DISPLAY_DEBUG = 7     # visible Display row at default (Lab Debug is last visible row)
DISPLAY_ROWS = 8      # visible Display rows at default (Frame Meter .. Lab Debug; Hitbox Colors & DI Vector hidden until enabled)
DEBUG_HMN_INFO, DEBUG_CPU_INFO, DEBUG_OVERLAY_ROW = 0, 1, 3  # Display > Lab Debug: P1 Info, CPU Info, PSA, Debug Overlay
PRESETS = {"none": 0, "state": 1, "ledge": 2, "damage": 3, "debug": 4}
SIZES = {"small": 0, "medium": 1, "large": 2}
# main::Settings offsets (src/main/settings.h; the module checks them at build time).
SET_PLAYER_PERCENT, SET_LOCK_PLAYER, SET_FRAME_ADVANCE, SET_PLAYER_SHIELDS = 8, 10, 11, 20
SET_ADVANCE_BUTTON, SET_ADVANCE_DELAY, SET_START_OPENS, SET_MENU_STYLE = 24, 25, 26, 33
SET_OVERLAY = 40
SET_HMN_INFO, SET_CPU_INFO = 56, 72  # preset, size, rows[8]
SET_OSD_POSITION, SET_ADVANTAGE, SET_ADVANTAGE_HIT, SET_TOLERANCE = 88, 89, 90, 91
SET_CPU_PERCENT, SET_LOCK_CPU = 120, 122
# cpu.options (CpuSettings +3): control, behavior, tdi, ..., counterGround +5,
# counterDelay +8, infiniteShields +9, shieldHealth +10.
SET_CPU_CONTROL, SET_CPU_BEHAVIOR, SET_CPU_TDI = 123, 124, 125
SET_CPU_COUNTER_GROUND, SET_CPU_COUNTER_DELAY = 128, 131
SET_INFINITE_SHIELDS, SET_SHIELD_HEALTH = 132, 133
SET_HUD = 41                # display.hud
SET_STAGE_MODEL = 45        # display.stageModel
SET_OSD_POWERSHIELD = 104   # osd.powershield
ADVANCE_L = 0
STATUS_GUARD = range(0x1A, 0x1E)  # GuardOn .. GuardDamage

PLAYER_PERCENT = 123
CPU_PERCENT = 45

UP, DOWN, LEFT, RIGHT = (128, 228), (128, 28), (28, 128), (228, 128)

# The plugin's line when the lab menu opens (src/main/lab.cpp openMenu).
MENU_OPENED = (r"lab: menu opened by port 1 \((?P<how>Start|Z in pause)\) on page (?P<page>\d+) "
               r"\((?P<title>[^)\r\n]*)\) row (?P<row>\d+), depth (?P<depth>\d+), main row (?P<main>\d+)"
               r"(?:, rail (?P<rail>\d+))?")
MENU_PTRS = r"lab: menu pointers def ([0-9a-f]{8}) state ([0-9a-f]{8})"
KIND_MENU, KIND_CHOICE, KIND_INT, KIND_TOGGLE, KIND_FUNC, KIND_INFO = range(6)


class LabError(RuntimeError):
    pass


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


class Fighters:
    """Reads the match's fighters from memory (independent of the plugin)."""

    def __init__(self, emu):
        self.emu = emu

    def entries(self):
        e = self.emu
        arr = e.ptr(ENTRY_MANAGER)
        if arr is None:
            return []
        out = []
        for i in range(min(e.u32(ENTRY_MANAGER + 4), 9)):
            raw = e.read(arr + i * ENTRY_SIZE, 0x60)
            entry_id, active = s32(raw, 4), raw[0xA]
            if entry_id < 0 or active > 3:
                continue
            fighter = u32(raw, 0x34 + 8 * active)
            owner = u32(raw, 0x28)
            if not (e.is_ram(fighter) and e.is_ram(owner)):
                continue
            out.append({"entry": entry_id, "player": s32(raw, 0x58), "fighter": fighter, "owner": owner})
        return out

    def by_player(self, player):
        return next((f for f in self.entries() if f["player"] == player), None)

    def damage(self, f):
        return self.emu.f32(self.emu.ptr(f["owner"]) + 0x24)

    def hud_damage(self, f):
        """The percent the damage HUD shows for `f`: IfMngr+0x68+player is
        its HUD's index, IfMngr+0x4C+4*index its IfPlayer, whose +0x18 the
        game's setter (0x800E14A4) writes and draws the digits from (read
        in the game's code, as Training's pause and a hit call it)."""
        e = self.emu
        manager = e.ptr(nav.IF_MANAGER)
        hud = e.ptr(manager + 0x4C + 4 * e.u8(manager + 0x68 + f["player"]))
        return struct.unpack(">i", e.read(hud + 0x18, 4))[0]

    def check_hud(self, f, what):
        """The damage HUD shows the fighter's damage (whole percent)."""
        shown, real = self.hud_damage(f), self.damage(f)
        if shown != int(real):
            raise LabError(f"{what}: the damage HUD shows {shown}% while the fighter's damage is {real:g}")
        return shown

    def modules(self, f):
        acc = self.emu.ptr(f["fighter"] + 0x60)
        return self.emu.read(self.emu.ptr(acc + 0xD8), 0xD0)

    def shield(self, f):
        """Shield health: instance work float 3 (src/main/cpu/view.cpp)."""
        mods = self.modules(f)
        work = self.emu.ptr(u32(mods, 0x64) + 0x20)
        return self.emu.f32(self.emu.ptr(work + 0x14) + 4 * 3)

    def state(self, f):
        e = self.emu
        mods = self.modules(f)
        posture, motion, work, status = u32(mods, 0xC), u32(mods, 0x8), u32(mods, 0x64), u32(mods, 0x70)
        p = e.read(posture, 0x24)

        def work_int(which, index):
            g = e.ptr(work + which)
            ints, n = e.u32(g + 0xC), e.u32(g + 0x10)
            return struct.unpack(">i", e.read(ints + 4 * index, 4))[0] if index < n else None
        return {"x": f32(p, 0xC), "y": f32(p, 0x10), "status": e.u32(status + 0x34),
                "anim": e.f32(motion + 0x40), "hitstun": work_int(0x20, 0x38), "ra5": work_int(0x24, 5),
                "la78": work_int(0x20, 0x4E), "damage": self.damage(f)}


class Lab:
    def __init__(self, emu, log_steps):
        self.emu = emu
        self.f = Fighters(emu)
        self.settings = None
        self.menu_def = None
        self.menu_state = None
        self.log_steps = log_steps

    # ---- plugin state ----------------------------------------------------

    def find_settings(self):
        m = None
        for m in re.finditer(r"main: attached in scMelee \S+ \S+; settings v\d+ at ([0-9a-f]{8})",
                             self.emu.synced_log_text()):
            pass
        if m is None:
            raise LabError("no attach line with the settings address")
        self.settings = int(m.group(1), 16)
        return self.settings

    def find_menu_ptrs(self):
        m = None
        for m in re.finditer(MENU_PTRS, self.emu.synced_log_text()):
            pass
        if m is None:
            raise LabError("no attach line with the menu pointers")
        self.menu_def = int(m.group(1), 16)
        self.menu_state = int(m.group(2), 16)
        return self.menu_def, self.menu_state

    def read_menu_state(self):
        """Read ui::MenuState live from Dolphin RAM (src/main/ui/menu.h MWCC layout)."""
        k_max_pages = 48
        k_depth = 6
        c0 = 1 + k_depth
        c1 = c0 + k_max_pages
        s1 = c1 + k_max_pages
        if self.menu_state is None:
            self.find_menu_ptrs()
        raw = self.emu.read(self.menu_state, s1 + 6)
        return {
            "depth": raw[0],
            "stack": list(raw[1:c0]),
            "cursor": list(raw[c0:c1]),
            "scroll": list(raw[c1:s1]),
            "category": raw[s1],
            "rail_cursor": raw[s1 + 1],
            "rail_focus": bool(raw[s1 + 2]),
            "open_start": bool(raw[s1 + 3]),
            "controller_type": raw[s1 + 4],
            "reset_confirm_page": raw[s1 + 5],
        }

    def read_menu_def(self):
        """Read ui::MenuDef tables live from Dolphin RAM (src/main/ui/menu.h MWCC layout)."""
        if self.menu_def is None:
            self.find_menu_ptrs()
        e = self.emu
        raw = e.read(self.menu_def, 32)
        pages_ptr = u32(raw, 0x00)
        page_count = raw[0x04]
        cat_count = raw[0x0C]
        cat_pages_ptr = u32(raw, 0x10)
        tab_labels_ptr = u32(raw, 0x14)
        util_count = raw[0x18]
        utils_ptr = u32(raw, 0x1C)

        page_ptrs = []
        if page_count and e.is_ram(pages_ptr):
            pb = e.read(pages_ptr, page_count * 4)
            page_ptrs = [u32(pb, i * 4) for i in range(page_count)]

        cat_pages = list(e.read(cat_pages_ptr, cat_count)) if (cat_count and e.is_ram(cat_pages_ptr)) else []

        tab_labels = []
        if cat_count and e.is_ram(tab_labels_ptr):
            tb = e.read(tab_labels_ptr, cat_count * 4)
            for i in range(cat_count):
                p = u32(tb, i * 4)
                tab_labels.append(e.cstr(p, 64) if e.is_ram(p) else None)

        utilities = []
        if util_count and e.is_ram(utils_ptr):
            ub = e.read(utils_ptr, util_count * 12)
            for i in range(util_count):
                np = u32(ub, i * 12)
                utilities.append({"name": e.cstr(np, 64) if e.is_ram(np) else None})

        return {
            "page_count": page_count,
            "page_ptrs": page_ptrs,
            "cat_count": cat_count,
            "cat_pages": cat_pages,
            "tab_labels": tab_labels,
            "util_count": util_count,
            "utilities": utilities,
        }

    def read_page(self, page_id, menu_def=None):
        """Read ui::Page and its 24-byte MWCC ui::Option array live from RAM."""
        mdef = menu_def or self.read_menu_def()
        if not (0 <= page_id < len(mdef["page_ptrs"])):
            raise LabError(f"page_id {page_id} out of range (pageCount={mdef['page_count']})")
        e = self.emu
        pptr = mdef["page_ptrs"][page_id]
        praw = e.read(pptr, 12)
        title_ptr = u32(praw, 0)
        opts_ptr = u32(praw, 4)
        count = praw[8]
        title = (e.cstr(title_ptr, 64) if e.is_ram(title_ptr) else None) or ""
        options = []
        if count and e.is_ram(opts_ptr):
            oraw = e.read(opts_ptr, count * 24)
            for i in range(count):
                o = i * 24
                name_ptr = u32(oraw, o + 8)
                options.append({
                    "idx": i,
                    "kind": oraw[o],
                    "flags": oraw[o + 1],
                    "offset": struct.unpack_from(">H", oraw, o + 2)[0],
                    "min": oraw[o + 4],
                    "def": struct.unpack_from(">b", oraw, o + 5)[0],
                    "max": struct.unpack_from(">h", oraw, o + 6)[0],
                    "name": e.cstr(name_ptr, 64) if e.is_ram(name_ptr) else None,
                })
        return {"id": page_id, "title": title, "count": count, "options": options}

    def menu_select_row(self, label, menu_def=None):
        """Move the cursor on the current live page to the option named `label`,
        verifying MenuState.cursor[pageId] after each step."""
        want = label.strip()
        mdef = menu_def or self.read_menu_def()
        st = self.read_menu_state()
        page_id = st["stack"][st["depth"]]
        page = self.read_page(page_id, mdef)
        targets = [o["idx"] for o in page["options"] if o["name"] == want]
        if not targets:
            wl = want.lower()
            targets = [o["idx"] for o in page["options"] if o["name"] and o["name"].lower() == wl]
        if not targets:
            avail = [o["name"] for o in page["options"] if o["name"]]
            raise LabError(f"row {want!r} not found on page {page['title']!r} (page {page_id}); rows: {avail}")

        cur = st["cursor"][page_id]
        if cur in targets:
            return {"page_id": page_id, "row": cur, "option": page["options"][cur], "state": st}

        if len(targets) == 1:
            t = targets[0]
            step_dir = 1 if t > cur else -1
            stick = DOWN if step_dir > 0 else UP
            for _ in range(page["count"] + 2):
                self.tap(stick=stick)
                st = self.read_menu_state()
                new_cur = st["cursor"][page_id]
                if new_cur == t:
                    return {"page_id": page_id, "row": new_cur, "option": page["options"][new_cur], "state": st}
                if new_cur == cur:
                    cur_name = page["options"][cur]["name"] if cur < page["count"] else None
                    raise LabError(f"row {want!r} (index {t}) on page {page['title']!r} is disabled, hidden, or "
                                   f"unreachable (cursor stopped at index {cur} {cur_name!r})")
                if (step_dir > 0 and new_cur > t) or (step_dir < 0 and new_cur < t):
                    raise LabError(f"row {want!r} (index {t}) on page {page['title']!r} was skipped by cursor "
                                   f"({cur} -> {new_cur}), likely disabled or hidden")
                cur = new_cur
            raise LabError(f"row {want!r} (index {t}) on page {page['title']!r} not reached after {page['count']} steps")

        # Multiple rows share the same name (e.g. gated trigger groups): scan downward then upward.
        for stick in (DOWN, UP):
            for _ in range(page["count"] + 2):
                self.tap(stick=stick)
                st = self.read_menu_state()
                new_cur = st["cursor"][page_id]
                if new_cur in targets:
                    return {"page_id": page_id, "row": new_cur, "option": page["options"][new_cur], "state": st}
                if new_cur == cur:
                    break
                cur = new_cur
        raise LabError(f"no enabled row named {want!r} (indices {targets}) reachable on page {page['title']!r}")

    def menu_nav_to(self, path):
        """Navigate the open lab menu by live RAM labels (e.g. 'CPU > Counters & Reversals > Counter Mode'
        or a sequence of labels). Handles both Category Rail and Top Tabs layouts."""
        parts = [p.strip() for p in path.split(">")] if isinstance(path, str) else [str(p).strip() for p in path]
        parts = [p for p in parts if p]
        if not parts:
            raise LabError("menu_nav_to: empty path")
        if self.settings is None:
            self.find_settings()
        mdef = self.read_menu_def()
        st = self.read_menu_state()

        if st["reset_confirm_page"] != 0:
            self.tap(["B"])
            st = self.read_menu_state()

        # Rewind any open subpages back to depth 0.
        for _ in range(8):
            if st["depth"] == 0:
                break
            prev = st["depth"]
            self.tap(["B"])
            st = self.read_menu_state()
            if st["depth"] >= prev:
                raise LabError(f"menu_nav_to({path!r}): B did not leave subpage (depth stayed {st['depth']})")
        if st["depth"] != 0:
            raise LabError(f"menu_nav_to({path!r}): could not rewind to depth 0 (depth={st['depth']})")

        # Resolve top-level category or left-rail utility.
        top = parts[0]
        top_l = top.lower()
        cat_titles = [self.read_page(pid, mdef)["title"] for pid in mdef["cat_pages"]]
        cat_idx = None
        for i in range(mdef["cat_count"]):
            tl = mdef["tab_labels"][i] if i < len(mdef["tab_labels"]) else None
            if top == tl or top == cat_titles[i]:
                cat_idx = i
                break
        if cat_idx is None:
            for i in range(mdef["cat_count"]):
                tl = mdef["tab_labels"][i] if i < len(mdef["tab_labels"]) else None
                if (tl and tl.lower() == top_l) or cat_titles[i].lower() == top_l:
                    cat_idx = i
                    break

        util_idx = None
        if cat_idx is None:
            for u, info in enumerate(mdef["utilities"]):
                if info["name"] and (info["name"] == top or info["name"].lower() == top_l):
                    util_idx = u
                    break

        if cat_idx is None and util_idx is None:
            avail_cats = [f"{cat_titles[i]} ({mdef['tab_labels'][i]})" for i in range(mdef["cat_count"])]
            avail_utils = [u["name"] for u in mdef["utilities"] if u["name"]]
            raise LabError(f"menu_nav_to({path!r}): unknown top category/utility {top!r}; "
                           f"categories={avail_cats}, utilities={avail_utils}")

        tabs_mode = mdef["cat_count"] > 0 and self.setting(SET_MENU_STYLE) == 1
        if util_idx is not None:
            if len(parts) > 1:
                raise LabError(f"menu_nav_to({path!r}): rail utility {top!r} cannot have sub-items")
            if tabs_mode:
                raise LabError(f"menu_nav_to({path!r}): rail utility {top!r} is unavailable in Top Tabs mode")
            if not st["rail_focus"]:
                self.tap(["B"])
                st = self.read_menu_state()
                if not st["rail_focus"]:
                    raise LabError(f"menu_nav_to({path!r}): B did not focus left rail")
            target_rail = mdef["cat_count"] + util_idx
            for _ in range(mdef["cat_count"] + mdef["util_count"] + 2):
                if st["rail_cursor"] == target_rail:
                    return {"page_id": st["stack"][0], "rail_cursor": st["rail_cursor"], "rail_focus": True, "state": st}
                prev = st["rail_cursor"]
                self.tap(stick=DOWN if target_rail > prev else UP)
                st = self.read_menu_state()
                if st["rail_cursor"] == prev:
                    raise LabError(f"menu_nav_to({path!r}): rail cursor stuck at {prev}, wanted {target_rail}")
            raise LabError(f"menu_nav_to({path!r}): could not reach rail utility {top!r}")

        if tabs_mode:
            n_cats = mdef["cat_count"]
            for _ in range(n_cats + 1):
                if st["category"] == cat_idx:
                    break
                cur_cat = st["category"]
                fwd = (cat_idx - cur_cat) % n_cats
                bwd = (cur_cat - cat_idx) % n_cats
                self.tap(["R"] if fwd <= bwd else ["L"])
                st = self.read_menu_state()
                if st["category"] == cur_cat:
                    raise LabError(f"menu_nav_to({path!r}): L/R did not move category from {cur_cat}")
        else:
            if st["category"] != cat_idx or st["rail_focus"]:
                if not st["rail_focus"]:
                    self.tap(["B"])
                    st = self.read_menu_state()
                    if not st["rail_focus"]:
                        raise LabError(f"menu_nav_to({path!r}): B did not focus left rail")
                for _ in range(mdef["cat_count"] + mdef["util_count"] + 2):
                    if st["rail_cursor"] == cat_idx:
                        break
                    prev = st["rail_cursor"]
                    self.tap(stick=DOWN if cat_idx > prev else UP)
                    st = self.read_menu_state()
                    if st["rail_cursor"] == prev:
                        raise LabError(f"menu_nav_to({path!r}): rail cursor stuck at {prev}, wanted {cat_idx}")
                if st["rail_cursor"] != cat_idx:
                    raise LabError(f"menu_nav_to({path!r}): failed to reach category {top!r} on rail")
                if len(parts) > 1 or st["rail_focus"]:
                    self.tap(["A"])
                    st = self.read_menu_state()
                    if st["rail_focus"] or st["stack"][0] != mdef["cat_pages"][cat_idx]:
                        raise LabError(f"menu_nav_to({path!r}): A did not enter category {top!r} "
                                       f"(railFocus={st['rail_focus']}, stack[0]={st['stack'][0]})")

        if len(parts) == 1:
            pid = st["stack"][0]
            return {"page_id": pid, "row": st["cursor"][pid], "state": st}

        res = None
        for idx, comp in enumerate(parts[1:], start=1):
            res = self.menu_select_row(comp, mdef)
            if idx < len(parts) - 1:
                if res["option"]["kind"] != KIND_MENU:
                    raise LabError(f"menu_nav_to({path!r}): {comp!r} on page {res['page_id']} is not a submenu "
                                   f"(kind={res['option']['kind']})")
                prev_depth = res["state"]["depth"]
                self.tap(["A"])
                st = self.read_menu_state()
                if st["depth"] != prev_depth + 1:
                    raise LabError(f"menu_nav_to({path!r}): A on submenu {comp!r} did not advance depth "
                                   f"(stayed {st['depth']})")
        return res

    def setting(self, offset, half=False):
        return self.emu.u16(self.settings + offset) if half else self.emu.u8(self.settings + offset)

    def wait_log(self, what, regex, after, frames=120):
        rx = re.compile(regex)

        def found():
            return rx.search(self.emu.log_text(), after)
        return self.emu.wait_for(what, found, frames, every=2)

    def log_pos(self):
        return len(self.emu.synced_log_text())

    # ---- input -------------------------------------------------------------

    def tap(self, buttons=(), stick=(128, 128), frames=2, after=4):
        self.emu.press(0, list(buttons), frames=frames, after=after, stick=stick)

    def down(self, n=1):
        for _ in range(n):
            self.tap(stick=DOWN)

    def up(self, n=1):
        for _ in range(n):
            self.tap(stick=UP)

    def open_menu(self, keep=False):
        """In the match, Start (Lab Menu Button = Start, the default); in
        Training's pause, Z. With Z in Pause set, Start opens Training's
        pause instead, and Z then opens the menu over it. The menu opens
        where it was left; unless `keep`, B and up then bring the
        cursor to Main Menu's first row, where the tests' row counts start.
        Returns the open line's match (page id, title, row, depth, main row)."""
        pos = self.log_pos()
        if not nav.training_paused(self.emu):
            self.tap(["START"])
            rx = re.compile(MENU_OPENED)
            self.emu.wait_for("the lab menu or Training's pause to open",
                              lambda: rx.search(self.emu.log_text(), pos) or nav.training_paused(self.emu), 120,
                              every=2)
        if not re.search(MENU_OPENED, self.emu.log_text()[pos:]):
            self.tap(["Z"])
        m = self.wait_log("the lab menu to open", MENU_OPENED, pos)
        if not keep:
            self.to_main_top(m)
        return m

    def to_main_top(self, m):
        """From where the open line `m` says the menu opened: B once per
        subpage, once more to focus the Draft C left rail if in an options
        pane, then up to rail index 0 (Lab)."""
        for _ in range(int(m.group("depth"))):
            self.tap(["B"])
        if int(m.group("rail") or 0) == 0:
            self.tap(["B"])
        self.up(int(m.group("main")))

    def go_controls(self):
        """Enter Lab > Controls via live RAM label lookup."""
        self.menu_nav_to("Lab > Controls")
        self.tap(["A"])

    def close_menu(self):
        """The button that opened it: Start, or Z over Training's pause (Z in
        a menu opened with Start would open Training's pause)."""
        opened = re.findall(MENU_OPENED, self.emu.synced_log_text())
        pos = self.log_pos()
        self.tap(["START"] if opened and opened[-1][0] == "Start" else ["Z"])
        self.wait_log("the lab menu to close", r"lab: menu closed", pos)

    def set_int(self, offset, target, limit=80):
        """Left/Right to `target`: held with R (fast repeat, a step every 2
        frames) while far, then single taps."""
        for _ in range(limit):
            v = self.setting(offset, half=True)
            if v == target:
                return v
            diff = target - v
            stick = RIGHT if diff > 0 else LEFT
            if abs(diff) > 8:
                self.emu.press(0, ["R"], frames=2 * (abs(diff) - 4), after=4, stick=stick)
            else:
                self.tap(stick=stick)
        raise LabError(f"value at +{offset:#x} stuck at {self.setting(offset, True)}, wanted {target}")

    def set_choice(self, offset, target, limit=40):
        for _ in range(limit):
            v = self.setting(offset)
            if v == target:
                return
            self.tap(stick=RIGHT if target > v else LEFT)
        raise LabError(f"choice at +{offset:#x} stuck at {self.setting(offset)}, wanted {target}")

    def toggle_on(self, offset):
        if self.setting(offset) == 0:
            self.tap(["A"])
        if self.setting(offset) != 1:
            raise LabError(f"toggle at +{offset:#x} did not turn on")

    def toggle_off(self, offset):
        if self.setting(offset) == 1:
            self.tap(["A"])
        if self.setting(offset) != 0:
            raise LabError(f"toggle at +{offset:#x} did not turn off")

    def main_to(self, row, start=0):
        """On Main Menu, from row `start` down to `row` (disabled rows skipped)."""
        self.down(sum(1 for r in range(start + 1, row + 1) if r not in MAIN_DISABLED))

    def back_to_main(self, depth=1):
        for _ in range(depth):
            self.tap(["B"])


def toggle_box(note, path, xscale, row, want_on):
    """The value box of visible row `row` is green exactly when `want_on`."""
    on, rg = shots.box_on(path, row, xscale)
    note(f"toggle box, row {row}: {'green' if on else 'not green'} (red, green {rg}) in {os.path.basename(path)}")
    if on != want_on:
        raise LabError(f"row {row}'s value box is {'green' if on else 'not green'} {rg}, the value is "
                       f"{'On' if want_on else 'Off'}")


# gfSceneRoot (pointer at 0x8059FF80): layer 0 (stage & sky) is at +0x399,
# layer 8 (the HUD: damage, player tags, Training's pause menu) is at
# +0x399 + 0x18 * 8; each is shown while bit 0x80 is set
# (src/main/display/display.cpp).
SCENE_ROOT, STAGE_LAYER_BYTE, HUD_LAYER_BYTE = 0x8059FF80, 0x399, 0x399 + 0x18 * 8
# A game-space strip just under the lab menu's panel (its bottom edge is at
# 432) on the left, where the bottom of the damage numbers (y 397..445) shows.
BELOW_PANEL = (60.0, 434.0, 400.0, 446.0)


def stage_layer_shown(emu):
    return bool(emu.u8(emu.ptr(SCENE_ROOT) + STAGE_LAYER_BYTE) & 0x80)


def hud_layer_shown(emu):
    return bool(emu.u8(emu.ptr(SCENE_ROOT) + HUD_LAYER_BYTE) & 0x80)


def region_diff(path_a, path_b, xscale, rect):
    """Mean absolute difference per channel over `rect` (game space)."""
    a, b = shots.Shot(path_a, xscale), shots.Shot(path_b, xscale)
    total, n = 0, 0
    for x, y in a.area(*rect):
        pa, pb = a.px(x, y), b.px(x, y)
        total += sum(abs(pa[c] - pb[c]) for c in range(3))
        n += 3
    return total / max(n, 1)


def menu_check(note, closed, opened, xscale, label, **kw):
    """Screenshot geometry of the lab menu (shots.check_menu)."""
    results, bad = shots.check_menu(closed, opened, xscale, **kw)
    for name, expected, measured, ok in results:
        note(f"  {label}: {'ok ' if ok else 'BAD'} {name}: expected {expected}, got {measured}")
    if bad:
        raise LabError(f"{label}: {len(bad)} geometry checks failed: {[b[0] for b in bad]}")


# Final Destination's edges are at about x = +-68; P1 never walks past this.
SAFE_X = 56.0
# Grounded statuses from which a walk tilt walks without triggering a roll
# (lib/BrawlHeaders ft/fighter.h: Wait, Walk, Walk_Brake, Dash, Turn).
STATUS_WALK_OK = (0x0, 0x1, 0x2, 0x3, 0x6)
# Knockdown wait / ledge hang: neutral Up stands in place or climbs cleanly
# (horizontal stick in Down_Wait triggers Down_Stand_Fb 0x52, rolling away).
STATUS_GETUP_UP = (0x4D, 0x4E, 0x75)


def wait_frozen(emu, want=True, frames=90):
    """Wait until the scheduler's stop bit (frame advance holding the game)
    matches `want`. Falls through on timeout so caller assertions report."""
    try:
        emu.wait_for(f"scheduler stop bit {'set' if want else 'clear'}",
                     lambda: bool(emu.u8(emu.ptr(SCHEDULER) + 0xB) & 0x8) == want, frames, every=1)
    except Timeout:
        pass


def hold_steps(emu, steps):
    """Run [(pad_kwargs, frames), ...] while paused, stepping exact game
    frames so host load cannot stretch or drop any hold."""
    emu.h.call("pause")
    try:
        for pad, n in steps:
            if pad:
                emu.hold(0, **pad)
            else:
                emu.neutral(0)
            if n > 0:
                emu.h.call("frame_advance", n=n)
    finally:
        emu.neutral(0)
        emu.h.call("resume")


def walk_to(emu, lab, me, target, close=11.0, frames=900):
    """Walk P1 toward `target` (a fighter, or an x) until within `close` and
    standing (Wait), never past SAFE_X. Steps frame by frame while paused so
    host load cannot overshoot, and waits out any non-walkable state (such as
    knockdown or air) instead of triggering a roll."""
    emu.h.call("pause")
    try:
        for _ in range(frames):
            sm = lab.f.state(me)
            a, st = sm["x"], sm["status"]
            b = target if isinstance(target, float) else lab.f.state(target)["x"]
            if st in STATUS_WALK_OK and (abs(a - b) < close or (b > a and a > SAFE_X) or (b < a and a < -SAFE_X)):
                break
            if st in STATUS_GETUP_UP:
                emu.hold(0, stick=(128, 228))
            elif st in STATUS_WALK_OK:
                emu.hold(0, stick=(178, 128) if b > a else (78, 128))
            else:
                emu.neutral(0)
            emu.h.call("frame_advance", n=1)
        emu.neutral(0)
        for _ in range(60):
            emu.h.call("frame_advance", n=1)
            if lab.f.state(me)["status"] == 0:
                break
    finally:
        emu.neutral(0)
        emu.h.call("resume")


def set_cpu_behaviour(emu, value):
    """Training's pause menu, CPU row: step right until the option is `value`.
    The lab's CPU Control goes to Training first: on Lab, its default, the
    lab drives the CPU and Training's option does nothing."""
    emu.h.call("write_mem", addr=Lab(emu, []).find_settings() + SET_CPU_CONTROL, hex="00")
    nav.pause_training(emu)
    nav.menu_select(emu, 3)
    tr = emu.ptr(emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU)
    for _ in range(20):
        if emu.u32(tr + CPU_BEHAVIOUR) == value:
            break
        hold_steps(emu, [({"stick": RIGHT}, 3), ({}, 8)])
    if emu.u32(tr + CPU_BEHAVIOUR) != value:
        raise LabError(f"Training's CPU option stuck at {emu.u32(tr + CPU_BEHAVIOUR)}, wanted {value}")
    nav.resume_training(emu)


def face(emu, lab, me, target):
    """Turn P1 toward `target`: a few frames of walk tilt toward it while
    paused, then still until Wait. walk_to() stops at once when P1 is already
    close, whichever way it faces, and a jab thrown with its back turned misses."""
    dx = lab.f.state(target)["x"] - lab.f.state(me)["x"]
    if lab.f.state(me)["status"] == 0 and dx * facing(lab, me) > 0:
        return
    stick = (178, 128) if dx > 0 else (78, 128)
    emu.h.call("pause")
    try:
        emu.hold(0, stick=stick)
        for _ in range(4):
            emu.h.call("frame_advance", n=1)
        emu.neutral(0)
        for _ in range(30):
            emu.h.call("frame_advance", n=1)
            if lab.f.state(me)["status"] == 0:
                break
    finally:
        emu.neutral(0)
        emu.h.call("resume")


def smash_toward(emu, lab, me, target):
    """Forward smash toward `target` (stick and A together, exact 3 frames)."""
    stick = RIGHT if lab.f.state(target)["x"] > lab.f.state(me)["x"] else LEFT
    hold_steps(emu, [({"buttons": ["A"], "stick": stick}, 3)])


def wait_standing(emu, lab, f, what, frames=600):
    """Until `f` stands (Wait) on the stage for 10 frames in a row: back
    from a launch, not in the air, on a ledge or getting up. A fighter that
    stands still out by an edge (back by a ledge getup; P1 does not walk
    past SAFE_X to reach it) is put back on the floor at x +-30, as the DI
    test places fighters (its posture position written)."""
    run = [0]
    edge = [0]
    is_p1 = f["player"] == 0

    def standing():
        s = lab.f.state(f)
        if is_p1:
            if s["status"] in STATUS_GETUP_UP:
                emu.hold(0, stick=(128, 228))
            else:
                emu.neutral(0)
        run[0] = run[0] + 1 if s["status"] == 0 and abs(s["x"]) <= SAFE_X + 10 else 0
        edge[0] = edge[0] + 1 if s["status"] == 0 and abs(s["x"]) > SAFE_X + 10 else 0
        if edge[0] >= 10:
            edge[0] = 0
            posture = u32(lab.f.modules(f), 0xC)
            pos = struct.pack(">ff", 30.0 if s["x"] > 0 else -30.0, 0.01).hex()
            emu.h.call("write_mem", addr=posture + 0xC, hex=pos)
        return run[0] >= 10
    try:
        emu.wait_for(f"{what} to stand on the stage", standing, frames)
    except Timeout:
        s = lab.f.state(f)
        raise LabError(f"{what} did not stand on the stage in {frames} frames: at ({s['x']:.1f}, {s['y']:.1f}), "
                       f"status {s['status']:#x}, entry {f['entry']:x}; fighters now "
                       f"{[(g['entry'], g['player']) for g in lab.f.entries()]}")
    finally:
        if is_p1:
            emu.neutral(0)


def positions(lab, a, b):
    sa, sb = lab.f.state(a), lab.f.state(b)
    return (f"P1 at ({sa['x']:.1f}, {sa['y']:.1f}) status {sa['status']:#x}, the CPU at ({sb['x']:.1f}, "
            f"{sb['y']:.1f}) status {sb['status']:#x}")


TRAINING_CPU_DAMAGE = 0x28  # IfMinigameTraining+0x28: Training's CPU Damage (0..999)
TRAINING_CPU_DAMAGE_ROW = 4
KEEP_PERCENT = 37


def training_cpu_damage(emu):
    return emu.u32(emu.ptr(emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU) + TRAINING_CPU_DAMAGE)


def pause_keeps_damage(emu, lab, human, cpu, note):
    """Brawl's Training writes its own CPU Damage to every CPU when its pause
    closes; the lab takes that write out unless the player changed CPU
    Damage in that pause. With the CPU's percent not locked: a CPU Percent
    set in the lab menu (reached through Training's pause, the default Z
    mode) is still there after the resume; damage dealt survives a pause
    with no lab menu; a CPU Damage changed in Training's pause still
    applies, as in Brawl. Leaves CPU Percent at CPU_PERCENT, locked, and
    Training's CPU Damage as it found it."""
    def cpu_page():
        nav.pause_training(emu)
        lab.open_menu(keep=True)
        lab.menu_nav_to("CPU > CPU Damage")

    def leave_page():
        lab.back_to_main()
        lab.close_menu()
        nav.resume_training(emu)
        emu.advance(30)

    # The step before launched the CPU (whose recovery can also knock down P1):
    # both fighters must be back and standing before P1 walks to the CPU.
    wait_standing(emu, lab, cpu, "the CPU")
    wait_standing(emu, lab, human, "P1")
    trained = training_cpu_damage(emu)
    cpu_page()
    lab.menu_select_row("Lock CPU Damage")
    lab.toggle_off(SET_LOCK_CPU)
    lab.menu_select_row("CPU Damage")
    pos = lab.log_pos()
    lab.set_int(SET_CPU_PERCENT, KEEP_PERCENT)
    lab.wait_log("the CPU percent to apply", rf"lab: CPU percent {KEEP_PERCENT} applied to entry {cpu['entry']:x}",
                 pos)
    try:
        emu.wait_for("CPU HUD damage to update", lambda: lab.f.hud_damage(cpu) == KEEP_PERCENT, 20, every=1)
    except Timeout:
        pass
    in_menu = lab.f.hud_damage(cpu)
    leave_page()
    got = lab.f.damage(cpu)
    if got != KEEP_PERCENT:
        raise LabError(f"CPU Percent {KEEP_PERCENT} set in the lab menu, unlocked: the CPU is at {got} after "
                       f"Training's pause closed (Training's CPU Damage is {trained})")
    lab.f.check_hud(cpu, f"CPU Percent {KEEP_PERCENT} set through Training's pause, after the resume")
    note(f"CPU Percent {KEEP_PERCENT} (unlocked) set through Training's pause: kept after the resume, and the "
         f"damage HUD shows it (it showed {in_menu}% with the menu still open; Training's CPU Damage {trained})")

    wait_standing(emu, lab, cpu, "the CPU")
    wait_standing(emu, lab, human, "P1")
    walk_to(emu, lab, human, cpu)
    if abs(lab.f.state(human)["x"] - lab.f.state(cpu)["x"]) > 12:
        raise LabError(f"P1 could not reach the CPU safely: {positions(lab, human, cpu)}")
    face(emu, lab, human, cpu)
    before = positions(lab, human, cpu)
    smash_toward(emu, lab, human, cpu)
    try:
        emu.wait_for("P1's forward smash to damage the CPU", lambda: lab.f.damage(cpu) > KEEP_PERCENT, 90, every=1)
    except Timeout:
        pass
    emu.advance(15)
    dealt = lab.f.damage(cpu)
    if dealt <= KEEP_PERCENT:
        raise LabError(f"P1's forward smash did not damage the CPU (damage {dealt}); before the smash {before}, "
                       f"after {positions(lab, human, cpu)}")
    nav.pause_training(emu)
    emu.advance(10)
    nav.resume_training(emu)
    emu.advance(10)
    got = lab.f.damage(cpu)
    if got != dealt:
        raise LabError(f"the CPU's damage went {dealt} -> {got} over a Training pause with no lab menu")
    lab.f.check_hud(cpu, "damage dealt, after a Training pause with no lab menu")
    note(f"damage dealt ({KEEP_PERCENT} -> {dealt}) kept over a Training pause")

    nav.pause_training(emu)
    nav.menu_select(emu, TRAINING_CPU_DAMAGE_ROW)
    for _ in range(3):
        emu.press(0, stick=RIGHT, frames=3, after=8)
    changed = training_cpu_damage(emu)
    if changed != (trained + 3) % 1000:
        raise LabError(f"Training's CPU Damage {trained} -> {changed} after three presses right")
    nav.resume_training(emu)
    emu.advance(10)
    got = lab.f.damage(cpu)
    if got != changed:
        raise LabError(f"Training's CPU Damage changed to {changed} in its pause: the CPU is at {got}")
    lab.f.check_hud(cpu, f"Training's CPU Damage changed to {changed} in its pause")
    note(f"Training's CPU Damage changed in its pause ({trained} -> {changed}) still sets the CPU's damage: {got}")

    nav.pause_training(emu)
    nav.menu_select(emu, TRAINING_CPU_DAMAGE_ROW)
    for _ in range(3):
        emu.press(0, stick=LEFT, frames=3, after=8)
    if training_cpu_damage(emu) != trained:
        raise LabError(f"Training's CPU Damage not back to {trained}: {training_cpu_damage(emu)}")
    nav.resume_training(emu)
    emu.advance(10)
    cpu_page()
    pos = lab.log_pos()
    lab.set_int(SET_CPU_PERCENT, CPU_PERCENT)
    lab.wait_log("the CPU percent to apply", rf"lab: CPU percent {CPU_PERCENT} applied to entry {cpu['entry']:x}",
                 pos)
    lab.menu_select_row("Lock CPU Damage")
    lab.toggle_on(SET_LOCK_CPU)
    leave_page()
    if lab.f.damage(cpu) != CPU_PERCENT:
        raise LabError(f"CPU damage {lab.f.damage(cpu)} after putting CPU Percent back to {CPU_PERCENT}, locked")
    lab.f.check_hud(cpu, f"CPU Percent back to {CPU_PERCENT} through Training's pause")


def record(emu, lab, f, frames, each=None):
    """Per game frame (emulation stepped one frame at a time): state of `f`.
    `each(i)`, if given, sets pad state before frame `i`."""
    rows = []
    emu.h.call("pause")
    try:
        for i in range(frames):
            if each:
                each(i)
            s = lab.f.state(f)
            s["gf"] = nav.game_frame(emu)
            rows.append(s)
            emu.h.call("frame_advance", n=1)
    finally:
        if each:
            emu.neutral(0)
        emu.h.call("resume")
    return rows


def check_hitstun(rows):
    """Hitstun (LA int 0x38) from the hit on: constant through hitlag, then
    down by exactly one per game frame to 0. Returns a summary or raises."""
    start = next((i for i, r in enumerate(rows) if (r["hitstun"] or 0) > 0), None)
    if start is None:
        raise LabError("the CPU was never in hitstun")
    seq = [(r["gf"], r["hitstun"]) for r in rows[start:]]
    status = [r["status"] for r in rows[start:]]
    first = seq[0][1]
    falling = False
    for i, ((g0, a), (g1, b)) in enumerate(zip(seq, seq[1:])):
        if g1 != g0 + 1:
            continue
        if b == a and not falling:
            continue
        if b == 0 and a <= 1:
            return f"hitstun {first} at the hit, down by 1 per frame to 0"
        if b == 0 and status[i + 1] != status[i]:
            # The count is dropped when the fighter leaves its damage status
            # early (a landing or a tech).
            return (f"hitstun {first} at the hit, down by 1 per frame, cleared at {a} when the status "
                    f"went {status[i]:#x} -> {status[i + 1]:#x}")
        if b != a - 1:
            raise LabError(f"hitstun went {a} -> {b} at game frame {g1} "
                           f"(status {status[i]:#x} -> {status[i + 1]:#x})")
        falling = True
    raise LabError(f"hitstun did not reach 0 ({seq[-1]})")


def check_shield_stun(rows):
    """While GuardDamage (0x1D): RA int 5 counts down by one per frame."""
    guard = [(r["gf"], r["ra5"]) for r in rows if r["status"] == STATUS_GUARD_DAMAGE]
    if not guard:
        raise LabError("the CPU never entered GuardDamage")
    vals = [v for _, v in guard]
    if vals[0] <= 0:
        raise LabError(f"shield stun did not start positive: {vals}")
    for a, b in zip(vals, vals[1:]):
        if b not in (a, a - 1):
            raise LabError(f"shield stun went {a} -> {b}: {vals}")
    return f"shield stun {vals[0]} on entering GuardDamage, down to {vals[-1]} over {len(vals)} frames"


def sim_clock(lab, f):
    """Counters that only move when the game simulates a frame: the
    fighter's animation frame and its instance-work int 0x4E (observed to
    count up by one per simulated frame in Wait, and to stop under the
    scheduler's stop bit and Training's pause). The match frame counter
    (nav.game_frame) is no use here: it keeps counting while the game's
    tasks are stopped."""
    s = lab.f.state(f)
    return s["anim"], s["la78"]


def frame_advance_presses(emu, lab, f, presses, button="L"):
    """Per press of `button` (the Frame Advance Button, L by default as in
    the game): (animation frames, int 0x4E) the fighter moved on. Stepped while
    paused so host load cannot stretch a 2-frame tap into the hold delay."""
    results = []
    emu.h.call("pause")
    try:
        for _ in range(presses):
            a0, c0 = sim_clock(lab, f)
            emu.hold(0, [button])
            emu.h.call("frame_advance", n=2)
            emu.neutral(0)
            emu.h.call("frame_advance", n=6)
            a1, c1 = sim_clock(lab, f)
            results.append((round(a1 - a0, 3), c1 - c0))
    finally:
        emu.neutral(0)
        emu.h.call("resume")
    return results


SCHEDULER = 0x805A0068
# g_GameGlobal (pointer at nav.GAME_GLOBAL) +0x24 -> gmGlobalRecord, whose
# byte +0x838 holds the screen setting in its top bit (the load the module's
# game::widescreen() compiles to).
GLOBAL_RECORD = 0x24
WIDESCREEN_BYTE = 0x838


def set_aspect(emu, aspect, note):
    """Sets the game's screen setting (16:9 or 4:3) from the character select."""
    record = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + GLOBAL_RECORD)
    if record is None:
        raise LabError("the game's record is not readable")
    addr = record + WIDESCREEN_BYTE
    old = emu.u8(addr)
    new = (old | 0x80) if aspect == "16:9" else (old & 0x7F)
    emu.h.call("write_mem", addr=addr, hex=f"{new:02x}")
    note(f"screen setting byte {old:#04x} -> {new:#04x} ({aspect})")


def key_repeat(emu, lab, note):
    """On CPU Percent (at 0): held Right moves it on the press, after 15
    frames, then every 5; with R held, every 2."""
    hold_steps(emu, [({"stick": RIGHT}, 40), ({}, 6)])
    slow = lab.setting(SET_CPU_PERCENT, half=True)
    hold_steps(emu, [({"buttons": ["R"], "stick": RIGHT}, 40), ({}, 6)])
    fast = lab.setting(SET_CPU_PERCENT, half=True) - slow
    note(f"key repeat: Right held 40 frames moved CPU Percent {slow} steps (expected 6), "
         f"with R {fast} (expected 20)")
    if not 5 <= slow <= 7 or not 18 <= fast <= 21:
        raise LabError(f"key repeat moved {slow} steps in 40 frames ({fast} with R)")


# Controls page (src/main/ui/pages/controls.cpp): Frame Decrement Button,
# DPad Up and DPad Down rows, and their settings (main::Settings +29..31).
CONTROLS_DPAD_UP, CONTROLS_DPAD_DOWN = 3, 4
SET_DECREMENT, SET_DPAD_UP, SET_DPAD_DOWN = 29, 30, 31
UP_TAUNT, UP_NONE = 0, 1
DOWN_NONE, DOWN_FRAME_ADVANCE = 0, 1
STATUS_APPEAL = 0x10C  # every taunt (lib/BrawlHeaders ft/fighter.h)


def facing(lab, f):
    return lab.emu.f32(u32(lab.f.modules(f), 0xC) + 0x40)


def set_dpad(emu, lab, up, down):
    """DPad Up and DPad Down through the Controls page, from the match."""
    nav.pause_training(emu)
    lab.open_menu(keep=True)
    lab.menu_nav_to("Lab > Controls > D-Pad Up")
    lab.set_choice(SET_DPAD_UP, up)
    lab.menu_select_row("D-Pad Down")
    lab.set_choice(SET_DPAD_DOWN, down)
    lab.close_menu()
    nav.resume_training(emu)
    emu.advance(30)


def taunts(emu, lab, f, button):
    """Whether a press of `button` puts the fighter in a taunt within 40
    frames; then waits for it to stand again."""
    emu.press(0, [button], frames=3, after=0)
    seen = False
    for _ in range(20):
        if lab.f.state(f)["status"] == STATUS_APPEAL:
            seen = True
            break
        emu.advance(2)
    emu.wait_for("P1 to stand again", lambda: lab.f.state(f)["status"] == 0, 300, every=5)
    return seen


def dpad_bindings(emu, lab, human, cpu, note):
    """Choices for D-pad up (Taunt, None) and down (None, Frame Advance),
    each tried in the match."""
    defaults = [lab.setting(o) for o in (SET_DECREMENT, SET_DPAD_UP, SET_DPAD_DOWN)]
    if defaults != [0, UP_TAUNT, DOWN_NONE]:
        raise LabError(f"Frame Rewind, D-Pad Up, D-Pad Down start at {defaults}, not None, Taunt, None")
    wait_standing(emu, lab, cpu, "the CPU")
    wait_standing(emu, lab, human, "P1")
    if not taunts(emu, lab, human, "DUP"):
        raise LabError("DPad Up = Taunt: D-pad up did not taunt")
    set_dpad(emu, lab, UP_NONE, DOWN_NONE)
    up, down = taunts(emu, lab, human, "DUP"), taunts(emu, lab, human, "DDOWN")
    note(f"DPad Up = Taunt: taunt; DPad Up = None: taunt {up}; DPad Down = None: taunt {down}")
    if up or not down:
        raise LabError(f"None: D-pad up taunted {up} (want False), D-pad down taunted {down} (want True)")
    set_dpad(emu, lab, UP_TAUNT, DOWN_FRAME_ADVANCE)
    hold_steps(emu, [({"buttons": ["DDOWN"]}, 2), ({}, 10)])
    on = lab.setting(SET_FRAME_ADVANCE)
    if on == 1:
        wait_frozen(emu, True)
    c0 = sim_clock(lab, human)
    emu.advance(30)
    held = sim_clock(lab, human) == c0
    hold_steps(emu, [({"buttons": ["DDOWN"]}, 2), ({}, 10)])
    off = lab.setting(SET_FRAME_ADVANCE)
    if off == 0:
        wait_frozen(emu, False)
    note(f"DPad Down = Frame Advance: on {on}, game held {held}, off again {off == 0}")
    if on != 1 or not held or off != 0 or lab.f.state(human)["status"] == STATUS_APPEAL:
        raise LabError("DPad Down = Frame Advance did not turn frame advance on and off")
    set_dpad(emu, lab, UP_TAUNT, DOWN_NONE)


def write_u8(emu, lab, offset, value):
    emu.h.call("write_mem", addr=lab.settings + offset, hex=f"{value:02x}")


def infinite_shields(emu, lab, cpu, note, shot):
    """CPU Options > Infinite Shields Health: range 0-60 and default
    60 (P+'s full shield is 60). Through the menu: Right at 60 stays, Left
    gives 59, and with Infinite Shields On the CPU's shield holds 59."""
    if lab.setting(SET_SHIELD_HEALTH) != 60:
        raise LabError(f"Infinite Shields Health starts at {lab.setting(SET_SHIELD_HEALTH)}, not 60")
    control, shields = lab.setting(SET_CPU_CONTROL), lab.setting(SET_INFINITE_SHIELDS)
    write_u8(emu, lab, SET_CPU_CONTROL, 1)      # Lab: the CPU rows count
    write_u8(emu, lab, SET_INFINITE_SHIELDS, 2)  # On
    nav.pause_training(emu)
    lab.open_menu(keep=True)
    lab.menu_nav_to("CPU > Shield & Grab > Shield Health")
    emu.advance(4)
    shot("lab-cpu-shields-60")
    lab.tap(stick=RIGHT)
    top = lab.setting(SET_SHIELD_HEALTH)
    lab.tap(stick=LEFT)
    one_less = lab.setting(SET_SHIELD_HEALTH)
    lab.close_menu()
    nav.resume_training(emu)
    try:
        emu.wait_for("CPU shield health to hold 59", lambda: abs(lab.f.shield(cpu) - 59.0) <= 0.01, 60, every=1)
    except Timeout:
        pass
    held = lab.f.shield(cpu)
    note(f"Infinite Shields Health: default 60, Right at 60 -> {top}, Left -> {one_less}; the CPU's shield "
         f"with Infinite Shields On: {held:.2f}")
    write_u8(emu, lab, SET_SHIELD_HEALTH, 60)
    write_u8(emu, lab, SET_CPU_CONTROL, control)
    write_u8(emu, lab, SET_INFINITE_SHIELDS, shields)
    emu.advance(10)
    if top != 60 or one_less != 59 or abs(held - 59.0) > 0.01:
        raise LabError(f"Infinite Shields Health: Right at 60 gave {top}, Left {one_less}, CPU shield {held}")


def player_infinite_shields(emu, lab, human, cpu, note, shot):
    """General > Player Infinite Shields (Off / On Until Hit / On, default On
    Until Hit): Off drains passively; On Until Hit holds 60.0 passively (even
    when CPU Infinite Shields Health is lowered), drains on a CPU hit in
    GuardDamage, and refills to 60.0 after dropping shield; On holds 60.0 even
    through CPU hits."""
    if lab.setting(SET_PLAYER_SHIELDS) != 1:
        raise LabError(f"Player Infinite Shields starts at {lab.setting(SET_PLAYER_SHIELDS)}, not 1 (On Until Hit)")

    saved = {off: lab.setting(off) for off in (
        SET_CPU_CONTROL, SET_CPU_BEHAVIOR, SET_CPU_TDI,
        SET_CPU_COUNTER_GROUND, SET_CPU_COUNTER_DELAY, SET_SHIELD_HEALTH,
    )}

    def set_player_shields_menu(mode):
        nav.pause_training(emu)
        lab.open_menu(keep=True)
        lab.menu_nav_to("Lab > P1 Infinite Shield")
        lab.set_choice(SET_PLAYER_SHIELDS, mode)
        lab.close_menu()
        nav.resume_training(emu)

    def cpu_jab_p1_shield():
        """P1 jabs the standing CPU to trigger Counter Action (Ground) = Jab
        after Counter Delay = 20 frames, then holds R so the CPU's counter-jab
        hits P1's shield. Returns (saw_guard_damage, min_shield, shield_after)."""
        wait_standing(emu, lab, cpu, "the CPU")
        wait_standing(emu, lab, human, "P1")
        walk_to(emu, lab, human, cpu, close=8.5)
        face(emu, lab, human, cpu)
        saw_gd = False
        min_sh = 60.0
        post_gd = 0
        try:
            emu.hold(0, ["A"])
            emu.advance(2)
            emu.neutral(0)
            emu.advance(1)
            emu.hold(0, ["R"])
            for _ in range(90):
                emu.advance(1)
                st = lab.f.state(human)["status"]
                sh = lab.f.shield(human)
                if st == STATUS_GUARD_DAMAGE:
                    saw_gd = True
                if saw_gd:
                    if sh < min_sh:
                        min_sh = sh
                    post_gd += 1
                    if post_gd >= 20:
                        break
            after = lab.f.shield(human)
            return saw_gd, min_sh, after
        finally:
            emu.neutral(0)

    try:
        # Configure CPU counter-jab (behavior Stand, TDI None, counterGround
        # Jab=13, counterDelay=20), and lower CPU Infinite Shields Health to 30
        # to verify it never shrinks HMN shield.
        write_u8(emu, lab, SET_CPU_CONTROL, 1)
        write_u8(emu, lab, SET_CPU_BEHAVIOR, 0)
        write_u8(emu, lab, SET_CPU_TDI, 4)
        write_u8(emu, lab, SET_CPU_COUNTER_GROUND, 13)
        write_u8(emu, lab, SET_CPU_COUNTER_DELAY, 20)
        write_u8(emu, lab, SET_SHIELD_HEALTH, 30)
        wait_standing(emu, lab, cpu, "the CPU")
        wait_standing(emu, lab, human, "P1")

        # 1. Off: passive hold drains shield (~0.28/frame -> ~47 after 45 frames).
        set_player_shields_menu(0)
        emu.hold(0, ["R"])
        try:
            emu.wait_for("P1 to hold guard (Off)", lambda: lab.f.state(human)["status"] == 0x1B, 60, every=1)
            emu.advance(45)
            off_passive = lab.f.shield(human)
        finally:
            emu.neutral(0)
        emu.wait_for("P1 to stand after Off", lambda: lab.f.state(human)["status"] == 0, 60, every=1)
        if off_passive >= 55.0:
            raise LabError(f"Player Infinite Shields Off did not drain passively (shield {off_passive:.2f})")

        # 2. On Until Hit: holds full shield passively (60.0 written before the
        #    fighter step's single-frame 0.28 decay -> steady 59.72, not shrunk
        #    to CPU health 30), drains on CPU hit, and refills to 60.0 on drop.
        set_player_shields_menu(1)
        emu.hold(0, ["R"])
        try:
            emu.wait_for("P1 to hold guard (Until Hit)", lambda: lab.f.state(human)["status"] == 0x1B, 60, every=1)
            emu.advance(5)
            until_early = lab.f.shield(human)
            emu.advance(40)
            until_passive = lab.f.shield(human)
        finally:
            emu.neutral(0)
        emu.wait_for("P1 to stand before Until Hit jab", lambda: lab.f.state(human)["status"] == 0, 60, every=1)
        if until_passive < 59.7 or abs(until_passive - until_early) > 0.01:
            raise LabError(f"On Until Hit passive shield drifted or shrank: early {until_early:.2f}, "
                           f"after 40f {until_passive:.2f} (CPU health=30)")

        saw_gd_u, min_u, after_u = cpu_jab_p1_shield()
        emu.wait_for("P1 to drop shield after Until Hit jab", lambda: lab.f.state(human)["status"] == 0, 60, every=1)
        emu.advance(5)
        refilled = lab.f.shield(human)
        if not saw_gd_u:
            raise LabError("CPU counter-jab did not put P1 into GuardDamage (On Until Hit)")
        if after_u >= 58.0 or min_u >= 58.0:
            raise LabError(f"On Until Hit did not drain on hit (min {min_u:.2f}, after {after_u:.2f})")
        if abs(refilled - 60.0) > 0.01:
            raise LabError(f"On Until Hit did not refill to 60.0 after dropping shield (got {refilled:.2f})")

        # 3. On: holds full shield even through a CPU hit in GuardDamage.
        set_player_shields_menu(2)
        saw_gd_on, min_on, after_on = cpu_jab_p1_shield()
        emu.wait_for("P1 to drop shield after On jab", lambda: lab.f.state(human)["status"] == 0, 60, every=1)
        if not saw_gd_on:
            raise LabError("CPU counter-jab did not put P1 into GuardDamage (On)")
        if after_on < 59.7:
            raise LabError(f"Player Infinite Shields On did not hold full shield through hit "
                           f"(min {min_on:.2f}, after {after_on:.2f})")

        note(f"Player Infinite Shields: Off passive {off_passive:.2f}; On Until Hit passive {until_early:.2f}->{until_passive:.2f} "
             f"(CPU health=30), hit -> {after_u:.2f}, drop -> {refilled:.2f}; On hit -> {after_on:.2f}")
    finally:
        emu.neutral(0)
        write_u8(emu, lab, SET_PLAYER_SHIELDS, 1)
        for off, val in saved.items():
            write_u8(emu, lab, off, val)
        emu.advance(10)


def lab_timer(emu, lab, note, shot, xscale):
    """Count-up match timer, as Brawl's own at the top centre
    (main/ui/match_hud.cpp; read in the centre HUD: its bit and its model in
    the HUD's scene group, the time last set): shown and counting while the
    match runs, hidden with Display Options > Match HUD Off, and held by
    frame advance."""
    aspect = "16:9" if xscale < 1.0 else "4:3"

    def settle(want_timer, frozen=False, frames=60):
        """Wait until the centre HUD's timer state (and frame-advance stop
        bit, if requested) matches, then two frames so the rendered picture
        has drawn that state (matchHudApply runs at the end of onRender)."""
        def ready():
            ok = nav.centre_hud(emu).get("timer") == want_timer
            if frozen:
                ok = ok and bool(emu.u8(emu.ptr(SCHEDULER) + 0xB) & 0x8)
            return ok
        try:
            emu.wait_for(f"match timer {'shown' if want_timer else 'hidden'}", ready, frames, every=1)
        except Timeout:
            pass
        emu.advance(2)

    def state(name, want_timer, frozen=False):
        settle(want_timer, frozen=frozen)
        h = nav.centre_hud(emu)
        h["pixels"] = shots.match_hud_pixels(shot(name), aspect, "timer")
        return h

    # Hide the stage layer (Final Destination's animated sky flashes white
    # right behind the top-centre timer around 45-55s, putting hundreds of
    # near-white background pixels into the timer box even with the HUD off).
    stage_was = lab.setting(SET_STAGE_MODEL)
    try:
        write_u8(emu, lab, SET_STAGE_MODEL, 0)
        emu.wait_for("stage layer to hide", lambda: not stage_layer_shown(emu), 30, every=1)
        emu.advance(2)
        a = state("lab-timer", True)
        # Step 30 exact game frames while paused so host load cannot drift t1 - t0.
        emu.h.call("pause")
        try:
            t0 = nav.centre_hud(emu).get("time", 0)
            emu.h.call("frame_advance", n=30)
            t1 = nav.centre_hud(emu).get("time", 0)
        finally:
            emu.h.call("resume")
        b = state("lab-timer-later", True)
        write_u8(emu, lab, SET_HUD, 0)
        off = state("lab-timer-hud-off", False)
        write_u8(emu, lab, SET_HUD, 1)
        write_u8(emu, lab, SET_FRAME_ADVANCE, 1)
        h1 = state("lab-timer-frame-advance", True, frozen=True)
        emu.advance(30)
        h2 = state("lab-timer-frame-advance-later", True, frozen=True)
    finally:
        write_u8(emu, lab, SET_HUD, 1)
        write_u8(emu, lab, SET_FRAME_ADVANCE, 0)
        write_u8(emu, lab, SET_STAGE_MODEL, stage_was)
        if stage_was:
            try:
                emu.wait_for("stage layer to return", lambda: stage_layer_shown(emu), 30, every=1)
            except Timeout:
                pass
        emu.advance(20)
    note(f"timer: running {a.get('timer')}, {b.get('timer')}; {t0} -> {t1} over 30 frames; HUD Off {off.get('timer')}; frame advance "
         f"{h1.get('timer')} at {h1.get('time')}, {h2.get('time')} 30 frames later; near-white pixels in its "
         f"place {[x['pixels'] for x in (a, b, off, h1)]}")
    if any(x["pixels"] is not None and x["pixels"] < 15 for x in (a, b, h1)) \
            or (off["pixels"] is not None and off["pixels"] > 3):
        raise LabError("the match timer's picture does not follow its state")
    if not (a.get("timer") and b.get("timer") and h1.get("timer")) or not 28 <= t1 - t0 <= 32:
        raise LabError(f"the match timer is not shown, or does not count: {nav.centre_hud_text(b)}")
    if off.get("timer"):
        raise LabError("the match timer still shows with HUD Off")
    if h1["time"] != h2["time"]:
        raise LabError(f"the match timer moved while frame advance held the game ({h1['time']} -> {h2['time']})")


# ---- info display values read straight from the game -------------------

# InfoValue ids (src/main/ui/info_display.h) of the values compared here.
IV_SELF, IV_KB, IV_ENG_L, IV_SYS_L, IV_ENG_C, IV_SYS_C, IV_ENG_TRIG, IV_SYS_TRIG = 4, 5, 7, 8, 9, 10, 11, 12
IV_LEDGE, IV_INTANG, IV_SHIELD, IV_GRIP, IV_ECB, IV_JUMPS, IV_BLAST_LR, IV_BLAST_UD = 13, 14, 17, 19, 20, 22, 27, 28
IV_ECB_BOTTOM, IV_WALLJUMPS, IV_LINES, IV_STATE = 21, 23, 26, 2
# g_gfPadSystem; the game pads (gfPadStatus, 0x40 each) at +0x444: sticks
# at +0x30 (s8 x, y, C-stick x, y), analog triggers at +0x34 (u8 L, R).
PAD_SYSTEM, GAME_PADS, PAD_SIZE = 0x805A0040, 0x444, 0x40
# CameraController::getInstance() returns the pointer at r13 - 0x41A8;
# soGroundUtil::checkDeadArea compares with its floats at +0x158..+0x164.
CAMERA, DEAD_RANGE = 0x805A0278, 0x158
INFO_LINE = re.compile(r"info P(\d): ([^\n]*)")
CAPTURED = range(0x3D, 0x40)   # CapturePulled..CaptureDamage
HOLDING = range(0x35, 0x3B)    # CatchPull..CatchAttack
CLIFF_CATCH, CLIFF_WAIT, OTTOTO = 0x74, 0x75, 0x7C
WALL_JUMP = 0x67
# The ground module (modules +0x10) keeps its shapes at +0x28; the first
# shape's grCollStatus is at +0x10 of that array (its saved position follows
# it: read on Mario at -60). The collision status keeps its current ECB shape
# at +0x60 (the bottom point's y at +0x18) and the touch bits at +0x75:
# 0x80 ground, 0x40 ceiling, 0x20 left wall, 0x10 right wall (the bitfield
# order of BrawlHeaders' grCollStatus; the rhombus copy at 0x801341d8).
GROUND_SHAPES, SHAPE_COLL, COLL_SHAPE, SHAPE_DOWN_Y, COLL_TOUCH = 0x28, 0x10, 0x60, 0x18, 0x75
TOUCH_BITS = (("G", 0x80), ("C", 0x40), ("L", 0x20), ("R", 0x10))
# Dead and Rebirth move the fighter without its energies (respawn).
NO_ENERGY = (0xBD, 0xBE)


def start_menu_to_exit(emu, lab, then_exit=True):
    """Lab Menu Button = Start: Start opens the lab menu; the cursor to Main
    Menu's first row, then (unless not `then_exit`) down to Exit. Returns
    the open line's match."""
    pos = lab.log_pos()
    emu.press(0, ["START"], frames=3, after=10)
    m = lab.wait_log("the lab menu to open with Start", MENU_OPENED, pos)
    if nav.training_paused(emu):
        raise LabError("Start opened Training's pause as well as the lab menu")
    if then_exit:
        lab.menu_nav_to("Return to CSS")
    return m


def z_to_training(emu, lab, f, note, label, advancing=False):
    """Lab Menu Button = Start: Start opens the lab menu; Z closes it and
    opens Training's pause, as Main Menu > Training Menu does; Start closes
    that and the game goes on (held again by frame advance when `advancing`,
    after the catch-up), and the next Start opens the lab menu again.
    `f`: a fighter whose clock shows the game running."""
    start_menu_to_exit(emu, lab, then_exit=False)
    pos = lab.log_pos()
    emu.press(0, ["Z"], frames=3, after=0)
    emu.wait_for(f"{label}: Z in the lab menu to open Training's pause", lambda: nav.training_paused(emu), 90)
    emu.advance(20)
    text = emu.synced_log_text()[pos:]
    if "lab: menu closed" not in text or "lab: menu opened" in text or not nav.training_paused(emu):
        raise LabError(f"{label}: Z did not leave the lab menu for Training's pause alone (menu closed "
                       f"{'lab: menu closed' in text}, opened again {'lab: menu opened' in text})")
    pos = lab.log_pos()
    emu.press(0, ["START"], frames=3, after=0)
    emu.wait_for(f"{label}: Start to close Training's pause", lambda: not nav.training_paused(emu), 60)
    if advancing:
        wait_frozen(emu, True, frames=90)
        emu.advance(10)
    else:
        emu.advance(70)
    if "lab: menu opened" in emu.synced_log_text()[pos:]:
        raise LabError(f"{label}: Start closing Training's pause opened the lab menu")
    c0 = sim_clock(lab, f)[1]
    emu.advance(30)
    moved = sim_clock(lab, f)[1] - c0
    if (moved != 0) if advancing else (moved < 25):
        raise LabError(f"{label}: the game moved {moved} frames in 30 after Training's pause closed")
    m = start_menu_to_exit(emu, lab, then_exit=False)
    if m.group("how") != "Start":
        raise LabError(f"{label}: the next Start opened the lab menu by {m.group('how')!r}")
    lab.close_menu()
    emu.advance(10)
    note(f"Start mode, {label}: Z opened Training's pause in the lab menu's place, Start closed it (game moved "
         f"{moved} frames in 30 after), Start opened the lab menu again")


# The lab's line when Exit is chosen (src/main/lab.cpp exitMatch): the end
# Training's Finish starts, with its pause closed first if it was open.
EXIT_NO_PAUSE = "Training's end of match started, no pause"
EXIT_FROM_PAUSE = "Training's end of match started, its pause closed"
MTCH_UNLOADED = (r"module MTCH: unloaded at scene exit; free (\w+) largest (\w+) blocks (\d+), "
                 r"back to baseline: (yes|NO)")
TRACKER_SUMMARY = r"tracker: (\d+) registrations, (\d+) removals, (\d+) lost"


def check_match_end(text, label):
    """The log of one match's end (`text`, from before the end to after the
    character select): the match module unloaded with its heap back to
    where it was, every observer the tracker put on taken off by the
    tracker (none lost: none still on when the game removed the fighters)."""
    unloads = re.findall(MTCH_UNLOADED, text)
    if len(unloads) != 1 or unloads[0][3] != "yes":
        raise LabError(f"{label}: match module unload lines {unloads}")
    summary = re.findall(TRACKER_SUMMARY, text)
    if len(summary) != 1 or summary[0][0] != summary[0][1] or summary[0][2] != "0":
        raise LabError(f"{label}: tracker summary {summary} (registrations, removals, lost)")
    if "gone while observed" in text:
        raise LabError(f"{label}: a fighter left the match while observed")
    return unloads[0], summary[0]


def exit_to_css(emu, lab, note, label, expect):
    """The cursor is on Main Menu > Exit: A, then every frame until Training's
    character select, checking Training's pause menu flag
    (StOperatorInfoTraining+0x128) on each. With `expect` EXIT_NO_PAUSE the
    pause must never have been open, in memory or in the plugin's
    "match: paused" line (logged from the render callback, so for every frame
    drawn). Then the match end is checked (check_match_end). What this does
    not see: a pause opened and closed between two polls without a frame
    drawn."""
    pos = lab.log_pos()
    lab.tap(["A"], after=0)
    seen = {"polls": 0, "paused": 0}

    def at_css():
        name = nav.scene(emu)[0]
        seen["polls"] += 1
        if name == "scMelee" and nav.training_paused(emu):
            seen["paused"] += 1
        return name == "scSelctCharacter"
    emu.wait_for(f"{label}: Training's character select after Exit", at_css, 300, every=1)
    try:
        lab.wait_log(f"{label}: match module unload", MTCH_UNLOADED, pos, frames=60)
    except Timeout:
        pass
    emu.advance(10)
    text = emu.synced_log_text()[pos:]
    line = re.search(r"lab: exit: ([^\r\n]*)", text)
    if line is None or line.group(1) != expect:
        raise LabError(f"{label}: Exit's line is {line.group(0) if line else None!r}, wanted {expect!r}")
    paused_lines = re.findall(r"match: paused at render call \d+", text)
    if expect == EXIT_NO_PAUSE and (seen["paused"] or paused_lines):
        raise LabError(f"{label}: Training's pause was shown on the way out ({seen['paused']} of "
                       f"{seen['polls']} polls, log {paused_lines})")
    unload, summary = check_match_end(text, label)
    saves = re.findall(r"storage: settings [^\r\n]*", text)
    note(f"{label}: Exit -> Training's character select in {seen['polls']} polls, Training's pause open on "
         f"{seen['paused']} of them; heap back to baseline (free {unload[0]}); tracker {summary[0]} on, "
         f"{summary[1]} off, 0 lost; settings: {saves}")


def check_heap_kept(log, note):
    """Every match module unload of the run left the heap the same."""
    unloads = re.findall(MTCH_UNLOADED, log)
    if len(unloads) < 4 or any(u != unloads[0] for u in unloads) or unloads[0][3] != "yes":
        raise LabError(f"match module unloads differ or are missing: {unloads}")
    note(f"{len(unloads)} match module unloads, each free {unloads[0][0]} largest {unloads[0][1]} blocks "
         f"{unloads[0][2]}, back to baseline")


def f32r(x):
    """`x` rounded to single precision, as the plugin computes it."""
    return struct.unpack(">f", struct.pack(">f", x))[0]


class InfoProbe:
    """The info panels' text (the plugin logs each drawn row while the
    tracker trace byte is set) against the same values read from memory."""

    def __init__(self, emu, lab):
        self.emu, self.lab = emu, lab
        m = re.findall(r"tracker: trace byte at ([0-9a-f]{8})", emu.synced_log_text())
        if not m:
            raise LabError("no trace byte line in the log")
        self.trace = int(m[-1], 16)
        self.pos = len(emu.log_text())
        self.compared, self.bad, self.seen = 0, [], {}
        self.shown_last = {}

    def rows(self, offset, values):
        self.emu.h.call("write_mem", addr=self.lab.settings + offset + 2, hex=bytes((values + [0] * 8)[:8]).hex())

    def tracing(self, on):
        self.emu.h.call("write_mem", addr=self.trace, hex="01" if on else "00")
        self.pos = len(self.emu.synced_log_text())

    def shown(self):
        """{player: {label: text}} drawn since the last call."""
        text = self.emu.log_text()
        out = {}
        for p, line in INFO_LINE.findall(text[self.pos:]):
            line = line.rstrip()
            out.setdefault(int(p), {})[line.split(":")[0]] = line
        self.pos = len(text)
        return out

    # ---- memory ---------------------------------------------------------

    def la(self, mods, index, floats=False):
        e = self.emu
        g = e.ptr(u32(mods, 0x64) + 0x20)
        base, count = (0x14, 0x18) if floats else (0xC, 0x10)
        if g is None or index >= e.u32(g + count):
            return None
        raw = e.read(e.u32(g + base) + 4 * index, 4)
        return f32(raw, 0) if floats else s32(raw, 0)

    def body_group(self, mods, x):
        """Intangible frames, invincible frames, any whole-body state (as
        game/body.cpp reads them: the hit group whose x follows the fighter)."""
        e = self.emu
        arr = e.ptr(u32(mods, 0x20) + 0x58)
        if arr is None:
            return 0, 0, False
        g = e.read(arr + 0xC, 0x2C)
        if abs(f32(g, 8) - x) >= 0.5:
            return 0, 0, False
        return s32(g, 0x24), min(u32(g, 0x28), 255), u32(g, 0x1C) != 0 or u32(g, 0x20) != 0

    def clatter(self, mods):
        c = self.emu.ptr(u32(mods, 0x5C) + 0x10)
        return self.emu.f32(c + 0xC) if c else 0.0

    def expected(self, f, others):
        """{label: text or a checking function} for fighter `f` now."""
        e, mods = self.emu, self.lab.f.modules(f)
        st = self.lab.f.state(f)
        out = {}
        pads = e.ptr(PAD_SYSTEM)
        if pads is not None and 0 <= f["player"] < 4:
            p = e.read(pads + GAME_PADS + PAD_SIZE * f["player"] + 0x30, 6)
            sx, sy, cx, cy = struct.unpack(">bbbb", p[:4])
            k = f32r(0.0125)
            # The main stick at P+'s scale (src/shared/game/stick.h): capped at
            # 100 units, times 0.010325, clamped; the C-stick times 0.011775, clamped.
            px, py = sx, sy
            if px * px + py * py > 10000:
                r = 100.0 / math.sqrt(px * px + py * py)
                px, py = int(px * r), int(py * r)
            ks = f32r(0.010325)
            lx, ly = (max(-1.0, min(1.0, f32r(v * ks))) for v in (px, py))
            out["Stick (Raw)"] = f"Stick (Raw): ({lx:+.4f},{ly:+.4f})"
            kc = f32r(0.011775)
            cxs, cys = (max(-1.0, min(1.0, f32r(v * kc))) for v in (cx, cy))
            out["C-Stick (Raw)"] = f"C-Stick (Raw): ({cxs:+.4f},{cys:+.4f})"
            out["Trig (Raw)"] = f"Trig (Raw): ({f32r(p[4] / 255.0):+.2f},{f32r(p[5] / 255.0):+.2f})"
        impl = e.ptr(u32(mods, 0x5C) + 0x18)
        if impl is not None:
            c = e.read(impl, 0x3C)
            x, y = f32(c, 8), f32(c, 0xC)
            if abs(x) > 1.5 or abs(y) > 1.5:
                x = y = 0.0
            out["Stick (Game)"] = f"Stick (Game): ({x:+.4f},{y:+.4f})"
            out["Trig (Game)"] = f"Trig (Game): ({f32(c, 0x38):+.3f})"
        out["Ledgegrab Timer"] = f"Ledgegrab Timer: {self.la(mods, 2)}"
        xlu, inv, whole = self.body_group(mods, st["x"])
        left = max(xlu, inv)
        out["Intangibility"] = f"Intangibility: {left if left > 0 or not whole else 'move'}"
        out["Shield Health"] = f"Shield Health: {self.la(mods, 3, floats=True):.3f}"
        jumps = self.la(mods, 1)
        out["Jumps"] = (lambda t: re.fullmatch(rf"Jumps: {jumps}/2", t), f"{jumps}/2")
        # Project+'s extra block: its pointer is at LA ints + 0x2D0, past
        # the work's int count (src/main/game/body.cpp).
        g = e.ptr(u32(mods, 0x64) + 0x20)
        ais = e.ptr(e.u32(g + 0xC) + 0x2D0) if g is not None else None
        if ais is not None:
            raw = e.read(ais, 0x1C)
            air, drop = s32(raw, 0x10), s32(raw, 0x18)
            out["ECB Lock"] = f"ECB Lock: {max(10 - air if 1 <= air <= 9 else 0, drop)}"
        grip = None
        if st["status"] in CAPTURED:
            grip = self.clatter(mods)
        elif st["status"] in HOLDING:
            grip = next((self.clatter(self.lab.f.modules(o)) for o in others
                         if self.lab.f.state(o)["status"] in CAPTURED), None)
        g = grip or 0.0
        out["Grab Release"] = (lambda t: abs(float(t.split(": ")[1]) - g) <= 0.51, f"{g:.2f}")
        out["Walljumps"] = f"Walljumps: {self.la(mods, 3)}"
        coll = self.coll_status(mods)
        if coll is not None:
            shape = e.ptr(coll + COLL_SHAPE)
            if shape is not None:
                out["ECB Bottom"] = f"ECB Bottom: {f32r(e.f32(shape + SHAPE_DOWN_Y) - st['y']):.3f}"
            touch = e.u8(coll + COLL_TOUCH)
            want = {k: bool(touch & bit) for k, bit in TOUCH_BITS}

            def lines(t, want=want):
                got = dict(re.findall(r"([GCLR]):(-?\d+)", t))
                return len(got) == 4 and all((got[k] != "-1") == want[k] for k in want)
            out["Coll"] = (lines, f"touching {[k for k in want if want[k]]}")
        cam = e.ptr(CAMERA)
        if cam is not None:
            d = struct.unpack(">4f", e.read(cam + DEAD_RANGE, 16))
            out["Blast L/R"] = f"Blast L/R: ({d[0]:+.3f},{d[1]:+.3f})"
            out["Blast U/D"] = f"Blast U/D: ({d[2]:.2f},{d[3]:.2f})"
            if not d[2] > 0 > d[3]:
                self.bad.append(f"blast zones +0x160/+0x164 are not top, bottom: {d}")
        return out

    def coll_status(self, mods):
        arr = self.emu.ptr(u32(mods, 0x10) + GROUND_SHAPES)
        return self.emu.ptr(arr + SHAPE_COLL) if arr is not None else None

    def position(self, f):
        p = self.emu.read(u32(self.lab.f.modules(f), 0xC), 0x24)
        return f32(p, 0xC), f32(p, 0x10), f32(p, 0x18), f32(p, 0x1C)

    def velocity(self, pos, shown):
        """Self + KB velocity against the position change of the frame,
        in the air (no ground to correct the move): None if not checkable."""
        if pos is None or "Vel (Self)" not in shown or "Vel (KB)" not in shown:
            return None
        x, y, px, py = pos
        nums = [float(v) for t in (shown["Vel (Self)"], shown["Vel (KB)"]) for v in re.findall(r"[-+][\d.]+", t)]
        if y < 2.0 or py < 2.0:
            return None
        sx, sy = nums[0] + nums[2], nums[1] + nums[3]
        return abs(sx - (x - px)) < 0.01 and abs(sy - (y - py)) < 0.01, (sx, sy, x - px, y - py)

    def frame(self, fighters):
        """One game frame (the game is paused); compares every row drawn.
        A frame advance draws the panels before it runs the game step (the
        drawn text shows the state the previous advance left), so memory is
        read before the advance."""
        wants = [(self.expected(f, [o for o in fighters if o is not f]),
                  None if self.lab.f.state(f)["status"] in NO_ENERGY else self.position(f)) for f in fighters]
        self.emu.h.call("frame_advance", n=1)
        shown = self.shown_last = self.shown()
        for f, (want, pos) in zip(fighters, wants):
            rows = shown.get(f["player"] + 1, {})
            for label, text in rows.items():
                self.seen.setdefault(label, set()).add(text)
                w = want.get(label)
                if w is None:
                    continue
                ok = w[0](text) if isinstance(w, tuple) else text == w
                self.compared += 1
                if not ok:
                    self.bad.append(f"P{f['player'] + 1} {label}: shown {text!r}, memory {w[1] if isinstance(w, tuple) else w!r}")
            v = self.velocity(pos, rows)
            if v is not None:
                self.compared += 1
                self.seen.setdefault("velocity sum = position change (air)", set()).add(v[0])
                if not v[0]:
                    self.bad.append(f"P{f['player'] + 1} SelfVel+KBVel {v[1][:2]} vs position change {v[1][2:]}")
        return shown

    def run(self, fighters, frames, each=None):
        """`frames` frames; each(i) sets the pads before frame i and may
        return True to stop early."""
        for i in range(frames):
            if each and each(i):
                break
            self.frame(fighters)


def wall_jump(emu, lab, human, both, probe, note):
    """ECB Bottom, Walljumps and Line Info by a wall: Mario is put in the
    air beside the wall under Final Destination's right edge (moved in steps
    that stay clear of the stage, so the game sees no line crossed), drifts
    into it and jumps off with a flick away; then he is put back above the
    stage and lands, which clears the count."""
    probe.rows(SET_HMN_INFO, [IV_ECB_BOTTOM, IV_WALLJUMPS, IV_LINES, IV_JUMPS, IV_STATE])
    st = lambda: lab.f.state(human)  # noqa: E731
    posture = u32(lab.f.modules(human), 0xC)

    def put(points):
        for x, y in points:
            for off in (0xC, 0x18):  # position and previous position
                emu.h.call("write_mem", addr=posture + off, hex=struct.pack(">ff", x, y).hex())
            probe.frame(both)
    probe.run(both, 6, lambda i: emu.hold(0, ["X"]) if i < 3 else emu.neutral(0))
    probe.run(both, 6, lambda i: emu.neutral(0))
    put([(100.0, 15.0), (100.0, -10.0), (73.0, -36.0)])
    seen = {}

    def to_wall(i):
        rows = probe.shown_last.get(human["player"] + 1, {})
        line = rows.get("Coll", "")
        if "jumped" not in seen and "L:" in line and "L:-1" not in line:
            seen.setdefault("touch", i)
        if "touch" in seen and "jumped" not in seen:
            emu.hold(0, stick=(255, 128))  # a flick away from the wall
            if st()["status"] == WALL_JUMP:
                seen["jumped"] = i
        else:
            emu.hold(0, stick=(28, 128) if "jumped" not in seen else (128, 128))
        return "jumped" in seen and i > seen["jumped"] + 10
    probe.run(both, 120, to_wall)
    walljumps = probe.la(lab.f.modules(human), 3)
    note(f"wall: touched at frame {seen.get('touch')}, wall jump at {seen.get('jumped')}, count {walljumps}")
    if "jumped" not in seen or walljumps != 1:
        raise LabError(f"no wall jump off Final Destination's wall ({seen}, count {walljumps})")
    put([(100.0, -10.0), (100.0, 15.0), (0.0, 15.0)])
    probe.run(both, 90, lambda i: emu.neutral(0))
    walljumps = probe.la(lab.f.modules(human), 3)
    if walljumps != 0:
        raise LabError(f"the wall jump count is {walljumps} after landing")


def info_values(emu, lab, human, cpu, note, shot):
    """Every info display value read from the game, in situations that
    change it, compared frame by frame with memory (see InfoProbe)."""
    probe = InfoProbe(emu, lab)

    def still(name):
        # Screenshots need the emulation running: a moment of it.
        emu.h.call("resume")
        shot(name)
        emu.h.call("pause")
        probe.shown()
    probe.rows(SET_HMN_INFO, [IV_SYS_L, IV_ENG_L, IV_SYS_C, IV_SYS_TRIG, IV_ENG_TRIG, IV_SELF, IV_KB, IV_SHIELD])
    probe.rows(SET_CPU_INFO, [IV_BLAST_LR, IV_BLAST_UD, IV_INTANG, IV_LEDGE, IV_JUMPS, IV_ECB, IV_GRIP, IV_SHIELD])
    both = [human, cpu]
    emu.h.call("pause")
    probe.tracing(True)
    try:
        # Sticks and triggers: walk, C-stick, analog trigger, then shield.
        pads = [dict(stick=(178, 128))] * 8 + [dict(cstick=(128, 40))] * 3 + [{}] * 30 \
            + [dict(r=120)] * 4 + [dict(buttons=["R"])] * 20 + [{}] * 20
        probe.run(both, len(pads), lambda i: emu.hold(0, **pads[i]))
        still("info-values-sticks")
        # Jumps and the ECB lock: a jump, a double jump, landing.
        probe.rows(SET_HMN_INFO, [IV_JUMPS, IV_ECB, IV_SELF, IV_KB, IV_INTANG, IV_LEDGE, IV_GRIP, IV_SYS_L])
        jump = {0: ["X"], 1: ["X"], 2: ["X"], 14: ["X"], 15: ["X"], 16: ["X"]}
        probe.run(both, 90, lambda i: emu.hold(0, jump.get(i, [])))
        # A spot dodge: intangibility the move's script sets.
        probe.run(both, 30, lambda i: emu.hold(0, ["R"], stick=(128, 28)) if i < 3 else emu.neutral(0))
        # The ledge: run right, turn, jump drifting right past the edge and
        # fall onto the ledge facing the stage; then drop off it.
        st = lambda: lab.f.state(human)  # noqa: E731
        probe.run(both, 400, lambda i: emu.hold(0, stick=(228, 128)) or st()["x"] > 50.0)
        probe.run(both, 20, lambda i: emu.neutral(0))
        # Walk to the edge: the walk stops (Ottoto, or no more progress).
        edge = []

        def to_edge(i):
            edge.append(st()["x"])
            emu.hold(0, stick=(170, 128))
            return st()["status"] == OTTOTO or (len(edge) > 6 and edge[-1] - edge[-6] < 0.05)
        probe.run(both, 300, to_edge)
        edge_x = st()["x"]
        probe.run(both, 20, lambda i: emu.hold(0, stick=(80, 128)) if i < 2 else emu.neutral(0))
        phase = {}

        path = []

        def to_ledge(i):
            s = st()
            path.append((i, round(s["x"], 1), round(s["y"], 1), hex(s["status"])))
            if s["status"] in (CLIFF_CATCH, CLIFF_WAIT):
                phase.setdefault("caught", i)
            if "caught" in phase:
                return i > phase["caught"] + 40
            # Steer to stop a few units past the edge, facing the stage:
            # aim with where the drift would take the fighter.
            x, _, px, _ = probe.position(human)
            ahead = x + 8.0 * (x - px)
            buttons = ["X"] if i < 3 else []
            emu.hold(0, buttons, stick=(228, 128) if ahead < edge_x + 5.0 else (28, 128))
            return s["y"] < -40.0
        probe.run(both, 150, to_ledge)
        note(f"ledge: caught at frame {phase.get('caught')} of the attempt (status now {st()['status']:#x})")
        note(f"ledge: edge at x {edge_x:.1f}; path {path[::4]}")
        still("info-values-ledge")
        # Drop, then jump back toward the stage.
        drop = lambda i: emu.hold(0, stick=(128, 28)) if i < 2 else (  # noqa: E731
            emu.hold(0, ["X"], stick=(28, 128)) if 6 <= i < 9 else emu.hold(0, stick=(28, 128)))
        probe.run(both, 120, drop)
        emu.neutral(0)
        probe.run(both, 200, lambda i: lab.f.state(human)["y"] > -1.0 and abs(lab.f.state(human)["y"]) < 0.01
                  and lab.f.state(human)["status"] == 0)
        # A grab: walk to the CPU and hold it until the grab ends.
        probe.tracing(False)
        emu.h.call("resume")
        wait_standing(emu, lab, cpu, "the CPU")
        walk_to(emu, lab, human, cpu, close=8.0)
        face(emu, lab, human, cpu)
        emu.h.call("pause")
        probe.tracing(True)
        probe.rows(SET_CPU_INFO, [IV_GRIP, IV_SELF, IV_KB, IV_ECB, IV_INTANG, IV_JUMPS, IV_BLAST_LR, IV_BLAST_UD])
        # Throw toward the middle of the stage (a forward or back throw), so
        # the later steps find the CPU within walking range.
        toward = (28, 128) if lab.f.state(cpu)["x"] > 0 else (228, 128)
        held = {}

        def grab(i):
            if i < 2:
                emu.hold(0, ["Z"])
            elif i == 30 and held:
                still("info-values-grab")
                emu.hold(0, stick=toward)  # a throw: knockback for the CPU
            elif i == 33:
                emu.neutral(0)
            if lab.f.state(cpu)["status"] in CAPTURED:
                held.setdefault("at", i)
        probe.run(both, 150, grab)
        note(f"grab: the CPU was held from frame {held.get('at')} of the attempt")
        wall_jump(emu, lab, human, both, probe, note)
    finally:
        probe.tracing(False)
        emu.neutral(0)
        emu.h.call("resume")
        probe.rows(SET_HMN_INFO, [])
        probe.rows(SET_CPU_INFO, [])
    for label in sorted(probe.seen):
        vals = sorted(map(str, probe.seen[label]))
        note(f"  {label}: {len(vals)} distinct, e.g. {vals[:3]}{' ... ' + vals[-1] if len(vals) > 3 else ''}")
    note(f"info values: {probe.compared} row-frames compared with memory, {len(probe.bad)} differed")
    if probe.bad:
        raise LabError(f"info values differ from memory: {probe.bad[:8]}")
    still = [k for k in INFO_CHANGING if len(probe.seen.get(k, ())) < 2]
    if still:
        raise LabError(f"info values that never changed: {still}")
    return probe


class InfoOnly(Exception):
    """--info-only: the info display values were checked; skip the rest."""


# Values the scenario must change (the velocity check counts as one).
INFO_CHANGING = ["Stick (Raw)", "Stick (Game)", "C-Stick (Raw)", "Trig (Raw)", "Trig (Game)", "Vel (Self)", "Vel (KB)",
                 "Shield Health", "Jumps", "ECB Lock", "Intangibility", "Ledgegrab Timer", "Grab Release",
                 "ECB Bottom", "Walljumps", "Coll"]


def cmd_test_lab(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-lab")
    ap.add_argument("rel")
    ap.add_argument("--presses", type=int, default=20)
    ap.add_argument("--aspect", choices=("16:9", "4:3"), help="set the game's screen setting first")
    ap.add_argument("--info-only", action="store_true", help="check only the info display values")
    opts = ap.parse_args(rest)
    label = "test-lab"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    t0 = time.monotonic()
    why, step, log_path, taken, notes = "", "boot", "", [], []

    def note(text):
        notes.append(text)
        print("  " + text)

    def shot(name):
        taken.append(emu.screenshot(name + ("-4x3" if opts.aspect == "4:3" else "")))
        return taken[-1][0]

    aspect_args = []
    if opts.aspect == "4:3":
        aspect_args = ["-C", "GFX.Settings.AspectRatio=2", "-C", "GFX.Settings.InternalResolution=1"]
    elif opts.aspect == "16:9":
        aspect_args = ["-C", "GFX.Settings.AspectRatio=1", "-C", "GFX.Settings.InternalResolution=1"]
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS + aspect_args,
                           css_cache=(opts.aspect != "4:3")) as (proc, emu):
            log_path = emu.log_path
            lab = Lab(emu, [])
            try:
                step = "into the match"
                nav.boot_to_training_css(emu)
                if opts.aspect:
                    set_aspect(emu, opts.aspect, note)
                setup = nav.start_training_match(emu, "mario", "fox")
                emu.advance(60)
                lab.find_settings()
                log = emu.synced_log_text()
                m = re.findall(r"lab: screen (16:9|4:3)", log)
                xscale = 0.75 if m and m[-1] == "16:9" else 1.0
                note(f"screen {m[-1] if m else '?'}: layout squeeze {xscale}")
                human, cpu = lab.f.by_player(0), lab.f.by_player(1)
                if human is None or cpu is None:
                    raise LabError(f"fighters not found: {lab.f.entries()}")
                note(f"fighters: human entry {human['entry']:#x}, CPU entry {cpu['entry']:#x}; "
                     f"setup human/CPU bytes {[p['state'] for p in setup['players'][:2]]}")
                for f, kind in ((human, "human"), (cpu, "cpu")):
                    rx = rf"fighters: entry {f['entry']:x} player {f['player'] + 1} port (-?\d+) {kind} fighter {f['fighter']:08x}"
                    if not re.search(rx, log):
                        raise LabError(f"no fighters line for the {kind} matching memory ({rx})")
                note("plugin's fighter list matches memory (human/CPU, player, fighter pointer)")

                step = "info display values"
                info_values(emu, lab, human, cpu, note, shot)
                if opts.info_only:
                    raise InfoOnly()
                if lab.setting(SET_START_OPENS) != 1:
                    raise LabError("Lab Menu Button is not Start by default")

                step = "Lab Menu Button: Z in Pause"
                # Start opens the menu by default; the steps up to "Lab Menu
                # Button: Start" run with Z in Pause, chosen on its row.
                m = start_menu_to_exit(emu, lab, then_exit=False)
                lab.go_controls()
                lab.menu_select_row("Menu Open")
                lab.set_choice(SET_START_OPENS, 0)
                lab.close_menu()
                emu.advance(10)
                if nav.training_paused(emu):
                    raise LabError("Start closing the lab menu opened Training's pause")
                note("Lab Menu Button starts at Start; set to Z in Pause through its row")

                step = "pause and open the lab menu"
                nav.pause_training(emu)
                emu.advance(20)
                cursor0 = nav.menu_cursor(emu)
                behaviour0 = emu.u32(emu.ptr(emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU) + CPU_BEHAVIOUR)
                shot("lab-pause-hint")
                lab.open_menu()
                emu.advance(10)
                shot("lab-menu-top")

                step = "General: Player Percent"
                lab.menu_nav_to("Lab > P1 Damage")
                pos = lab.log_pos()
                lab.set_int(SET_PLAYER_PERCENT, PLAYER_PERCENT)
                lab.wait_log("the player percent to apply",
                             rf"lab: Player percent {PLAYER_PERCENT} applied to entry {human['entry']:x} \(P1\), damage now {PLAYER_PERCENT}\.0",
                             pos)
                shot("lab-menu-general")
                lab.menu_nav_to("Display > Lab Debug > Debug Overlay")
                lab.toggle_on(SET_OVERLAY)

                step = "CPU Options: CPU Percent, key repeat, lock"
                lab.menu_nav_to("CPU > CPU Damage")
                # Opening the menu with Lock CPU Damage off syncs CPU Damage to live match %;
                # 1-tap A resets it cleanly to 0% before checking clamp at 0.
                synced_cpu = lab.setting(SET_CPU_PERCENT, half=True)
                live_cpu = int(lab.f.damage(cpu))
                if synced_cpu != live_cpu:
                    raise LabError(f"CPU Percent did not sync to live damage on open ({synced_cpu} != {live_cpu})")
                if synced_cpu != 0:
                    lab.tap(["A"])
                    if lab.setting(SET_CPU_PERCENT, half=True) != 0 or lab.f.damage(cpu) != 0.0:
                        raise LabError(f"1-tap A reset on CPU Percent failed (setting {lab.setting(SET_CPU_PERCENT, half=True)}, live {lab.f.damage(cpu)})")
                    note(f"CPU Percent synced to live {live_cpu}% on open and 1-tap A reset it to 0%")
                # Values clamp at their ends (no wrap): Left at 0 stays 0.
                lab.tap(stick=LEFT)
                if lab.setting(SET_CPU_PERCENT, half=True) != 0:
                    raise LabError("CPU Percent moved below 0")
                key_repeat(emu, lab, note)
                pos = lab.log_pos()
                lab.set_int(SET_CPU_PERCENT, CPU_PERCENT)
                lab.wait_log("the CPU percent to apply",
                             rf"lab: CPU percent {CPU_PERCENT} applied to entry {cpu['entry']:x} \(P2\), damage now {CPU_PERCENT}\.0",
                             pos)
                lab.menu_select_row("Lock CPU Damage")
                # A toggle row: its value box is green while On and
                # follows the value at once, with the cursor on it or not.
                emu.advance(3)
                toggle_box(note, shot("lab-menu-cpu-lock-off-cursor"), xscale, CPU_LOCK_ROW, False)
                lab.toggle_on(SET_LOCK_CPU)
                emu.advance(3)
                toggle_box(note, shot("lab-menu-cpu-lock-on-cursor"), xscale, CPU_LOCK_ROW, True)
                lab.menu_select_row("CPU Damage")
                emu.advance(3)
                toggle_box(note, shot("lab-menu-cpu-lock-on-cursor-above"), xscale, CPU_LOCK_ROW, True)
                lab.close_menu()
                emu.advance(10)

                step = "native pause menu untouched"
                cursor1 = nav.menu_cursor(emu)
                behaviour1 = emu.u32(emu.ptr(emu.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU) + CPU_BEHAVIOUR)
                if (cursor0, behaviour0) != (cursor1, behaviour1) or not nav.training_paused(emu):
                    raise LabError(f"Training's pause menu moved: cursor {cursor0} -> {cursor1}, "
                                   f"CPU option {behaviour0} -> {behaviour1}, paused {nav.training_paused(emu)}")
                note(f"Training's pause menu stayed on row {cursor1} (CPU option {behaviour1}) "
                     f"through the inputs in the lab menu")
                nav.resume_training(emu)
                emu.advance(30)

                step = "damage applied"
                dh, dc = lab.f.damage(human), lab.f.damage(cpu)
                if (dh, dc) != (PLAYER_PERCENT, CPU_PERCENT):
                    raise LabError(f"damage in memory is P1 {dh}, CPU {dc}")
                note(f"damage in memory: P1 {dh}, CPU {dc}")
                path = shot("lab-debug-overlay")
                score = shots.overlay_score(path)
                if score < shots.OVERLAY_MIN_SCORE:
                    raise LabError(f"debug overlay turned on but not visible (score {score:.3f})")
                note(f"debug overlay visible after turning it on (score {score:.3f})")

                step = "Info displays: presets and sizes"
                nav.pause_training(emu)
                lab.open_menu(keep=True)
                lab.menu_nav_to("Display > Lab Debug > P1 Info Display > Preset")
                lab.set_choice(SET_HMN_INFO, PRESETS["state"])
                lab.menu_select_row("Size")
                lab.set_choice(SET_HMN_INFO + 1, SIZES["small"])
                rows = list(emu.read(lab.settings + SET_HMN_INFO + 2, 8))
                if rows[:4] != [2, 3, 32, 35]:
                    raise LabError(f"Basic preset set the rows to {rows}")
                shot("lab-menu-player-info")
                lab.tap(["B"])  # back to Lab Debug
                lab.menu_select_row("CPU Info Display")
                lab.tap(["A"])
                lab.menu_select_row("Preset")
                lab.set_choice(SET_CPU_INFO, PRESETS["damage"])
                lab.menu_select_row("Size")
                lab.set_choice(SET_CPU_INFO + 1, SIZES["large"])
                # Row 1: unavailable choices (Engine CStick = 9) are skipped.
                lab.menu_select_row("Row 1")
                row1 = lab.setting(SET_CPU_INFO + 2)
                lab.set_choice(SET_CPU_INFO + 2, IV_SYS_L)
                lab.tap(stick=RIGHT)
                if lab.setting(SET_CPU_INFO + 2) != IV_SYS_C:
                    raise LabError(f"CPU Info Display row 1 right from {IV_SYS_L} landed on "
                                   f"{lab.setting(SET_CPU_INFO + 2)}, want {IV_SYS_C} (skipping {IV_ENG_C})")
                lab.set_choice(SET_CPU_INFO + 2, row1)
                note(f"CPU Info Display row 1: right from {IV_SYS_L} skipped unavailable {IV_ENG_C} to {IV_SYS_C}; back to {row1}")
                note(f"info presets: HMN rows {rows}, CPU rows {list(emu.read(lab.settings + SET_CPU_INFO + 2, 8))}")
                lab.tap(["B"])  # back to Lab Debug
                lab.menu_select_row("Debug Overlay")
                lab.toggle_off(SET_OVERLAY)
                lab.close_menu()
                nav.resume_training(emu)
                emu.advance(30)
                shot("lab-info-display")
                shot("restyle-infodisplay")

                step = "shield stun"
                wait_standing(emu, lab, cpu, "the CPU")
                wait_standing(emu, lab, human, "P1")
                walk_to(emu, lab, human, cpu, close=9.0)
                if abs(lab.f.state(human)["x"] - lab.f.state(cpu)["x"]) > 12:
                    raise LabError(f"P1 could not reach the CPU safely (P1 x {lab.f.state(human)['x']:.1f}, "
                                   f"CPU x {lab.f.state(cpu)['x']:.1f})")
                # Shield only now: a shield held for long shrinks and breaks.
                set_cpu_behaviour(emu, CPU_SHIELD)
                emu.wait_for("the CPU to shield", lambda: lab.f.state(cpu)["status"] == 0x1B, 120, every=1)
                emu.advance(5)
                # A jab, facing the CPU: weak enough not to break the shield.
                face(emu, lab, human, cpu)
                rows = record(emu, lab, cpu, 60,
                              each=lambda i: emu.hold(0, ["A"]) if i < 2 else emu.neutral(0))
                note(check_shield_stun(rows))
                emu.advance(30)
                set_cpu_behaviour(emu, behaviour0)
                wait_standing(emu, lab, cpu, "the CPU")

                step = "lock holds after a hit"
                wait_standing(emu, lab, cpu, "the CPU")
                wait_standing(emu, lab, human, "P1")
                walk_to(emu, lab, human, cpu)
                face(emu, lab, human, cpu)
                stick = RIGHT if lab.f.state(cpu)["x"] > lab.f.state(human)["x"] else LEFT
                rows = record(emu, lab, cpu, 90,
                              each=lambda i: emu.hold(0, ["A"], stick=stick) if i < 3 else emu.neutral(0))
                hit = [r for r in rows if (r["hitstun"] or 0) > 0]
                if not hit:
                    raise LabError("P1's forward smash did not hit the CPU")
                over = [r["damage"] for r in rows if r["damage"] != CPU_PERCENT]
                note(f"CPU hit (status {hit[0]['status']:#x}); damage values seen over 90 frames: "
                     f"{sorted(set(r['damage'] for r in rows))}")
                if len(over) > 1:
                    raise LabError(f"the CPU's damage left {CPU_PERCENT} for {len(over)} frames: {over[:10]}")
                note(check_hitstun(rows))
                emu.advance(60)
                if lab.f.damage(cpu) != CPU_PERCENT:
                    raise LabError(f"CPU damage {lab.f.damage(cpu)} after the hit, lock is {CPU_PERCENT}")
                # P1's percent is not locked here, and the launched CPU can hit
                # P1 while it recovers, so P1's damage may rise. It must never
                # drop or jump to the CPU's locked value: that would mean the
                # lock wrote to the wrong fighter.
                p1_damage = lab.f.damage(human)
                if p1_damage < PLAYER_PERCENT or p1_damage == CPU_PERCENT:
                    raise LabError(f"P1 damage changed to {p1_damage}")
                if p1_damage != PLAYER_PERCENT:
                    note(f"P1 took damage from the recovering CPU: {PLAYER_PERCENT} -> {p1_damage}")
                shot("lab-info-display-hit")

                step = "Training's pause keeps the CPU's damage"
                pause_keeps_damage(emu, lab, human, cpu, note)

                step = "D-pad up and down bindings"
                dpad_bindings(emu, lab, human, cpu, note)

                step = "Infinite Shields Health 0-60"
                infinite_shields(emu, lab, cpu, note, shot)

                step = "Player Infinite Shields"
                player_infinite_shields(emu, lab, human, cpu, note, shot)

                # Rows not built yet are skipped: checked on the Stage & Cast
                # page by test-rng ("menu").
                step = "the cursor stops on the last row"
                nav.pause_training(emu)
                lab.open_menu()
                lab.main_to(MAIN_DISPLAY)
                lab.tap(["A"])
                pos = lab.log_pos()
                lab.down(DISPLAY_ROWS + 2)
                moves = len(re.findall(r"\] ui: sound 0 ", emu.synced_log_text()[pos:]))
                shot("lab-display-last-row")
                note(f"Display Options: {DISPLAY_ROWS + 2} downs from the top moved the cursor {moves} rows "
                     f"(to Lab Debug, the last)")
                if moves != DISPLAY_ROWS - 1:
                    raise LabError(f"down moved {moves} rows on Display Options, want {DISPLAY_ROWS - 1} "
                                   "(stopping on the last row)")
                lab.back_to_main()
                lab.close_menu()
                nav.resume_training(emu)
                # Let earlier Hit/Shield Adv. 150f OSDs finish expiring before Frame Advance freezes game::frozen().
                emu.advance(115)

                step = "frame advance on"
                nav.pause_training(emu)
                lab.open_menu()
                lab.tap(["A"])  # General; the menu opens on Main Menu, row 0
                lab.toggle_on(SET_FRAME_ADVANCE)
                if lab.setting(SET_ADVANCE_BUTTON) != ADVANCE_L or lab.setting(SET_ADVANCE_DELAY) != 30:
                    raise LabError("Frame Advance Button is not L (the default), or the hold delay not 30")
                lab.close_menu()
                cr = sim_clock(lab, human)[1]
                nav.resume_training(emu)
                # The plugin pauses only the gameplay tasks for 45 steps while
                # Training's menu closes, then stops everything.
                wait_frozen(emu, True, frames=90)
                emu.advance(5)
                note(f"resuming into frame advance let P1 int 0x4E move {sim_clock(lab, human)[1] - cr} frames")
                c0, x0 = sim_clock(lab, human) + sim_clock(lab, cpu), lab.f.state(human)["x"]
                emu.advance(60)
                c1, x1 = sim_clock(lab, human) + sim_clock(lab, cpu), lab.f.state(human)["x"]
                stop = emu.u8(emu.ptr(SCHEDULER) + 0xB)
                if (c1, x1) != (c0, x0) or not stop & 0x8:
                    raise LabError(f"frame advance on, but the game ran: (anim, int 0x4E) of P1 and CPU {c0} -> {c1}, "
                                   f"P1 x {x0} -> {x1}, stop byte {stop:#x}")
                note(f"frame advance on: P1 and CPU (animation frame, int 0x4E) {c0} and P1 x {x0:.2f} held "
                     f"for 60 frames; scheduler stop byte {stop:#x}")
                shot("lab-frame-advance")
                named = {os.path.splitext(os.path.basename(t[0]))[0]: t[0] for t in taken}
                suffix = "-4x3" if opts.aspect == "4:3" else ""
                scores = [shots.pause_menu_score(named["lab-pause-hint" + suffix]),
                          shots.pause_menu_score(named["lab-menu-top" + suffix], shots.pause_menu_behind_panel(xscale)),
                          shots.pause_menu_score(taken[-1][0])]
                note(f"Training's pause menu on screen (score): paused {scores[0]:.3f}, behind the open lab "
                     f"menu's panel {scores[1]:.3f}, frame advance {scores[2]:.3f}")
                # At 4:3 the info display's white text (on in this step)
                # reaches into the box: a quarter of the paused score is
                # allowed for the closed pause menu then.
                gone = max(shots.PAUSE_MENU_GONE, scores[0] / 4 if xscale == 1.0 else 0.0)
                if scores[0] < shots.PAUSE_MENU_SHOWN or scores[1] > shots.PAUSE_MENU_GONE or scores[2] > gone:
                    raise LabError("Training's pause menu is visible where it should be covered or closed")

                step = f"{opts.presses} frame-advance presses"
                res = frame_advance_presses(emu, lab, human, opts.presses)
                # The animation may wrap back to frame 0 at the end of its loop.
                bad = [i for i, (a, c) in enumerate(res) if c != 1 or (a != 1.0 and a > 0)]
                note(f"per press (P1 animation frames, int 0x4E): {res}")
                if bad:
                    raise LabError(f"presses {bad} did not advance exactly one frame: {res}")
                note(f"{opts.presses} presses advanced P1's int 0x4E by {sum(c for _, c in res)}, one per press")

                step = "a light analog L press does not advance"
                # Reads the button's digital press; a light
                # press of the analog trigger (raw 80, no click) is no press,
                # and the fighter must not shield with it either.
                c0 = sim_clock(lab, human) + sim_clock(lab, cpu)
                emu.h.call("pause")
                try:
                    emu.hold(0, l=80)
                    emu.h.call("frame_advance", n=40)
                    status = lab.f.state(human)["status"]
                    emu.neutral(0)
                    emu.h.call("frame_advance", n=10)
                    c1 = sim_clock(lab, human) + sim_clock(lab, cpu)
                finally:
                    emu.neutral(0)
                    emu.h.call("resume")
                note(f"analog L at raw 80 for 40 frames (no digital L): P1 and CPU clocks {c0} -> {c1}, "
                     f"P1 status {status:#x}")
                if c1 != c0 or status in STATUS_GUARD:
                    raise LabError(f"a light analog L moved the game ({c0} -> {c1}) or shielded (status {status:#x})")

                step = "Start while frame advance is on"
                cs = sim_clock(lab, human)[1]
                emu.press(0, ["START"], frames=3, after=0)
                emu.wait_for("Training's pause to open from frame advance", lambda: nav.training_paused(emu), 60)
                note(f"Start opened Training's pause from frame advance; P1 int 0x4E moved "
                     f"{sim_clock(lab, human)[1] - cs} frames on the way")

                step = "Lab Menu Button: Start"
                lab.open_menu(keep=True)
                lab.menu_nav_to("Lab > Controls > Menu Open")
                lab.set_choice(SET_START_OPENS, 1)
                lab.close_menu()
                nav.resume_training(emu)
                wait_frozen(emu, True, frames=90)  # Start let go, catch-up done: frame advance holds the game
                try:
                    emu.wait_for("HUD layer shown before menu", lambda: hud_layer_shown(emu), 30, every=1)
                except Timeout:
                    pass
                emu.advance(2)
                closed_hud = shot("restyle-closed")
                if not hud_layer_shown(emu):
                    raise LabError("the HUD layer is hidden before the menu opened")
                # The geometry checks compare the open menu with the scene
                # behind it, and the open menu hides the HUD: that scene is
                # taken with the HUD layer off too (its bit cleared by hand;
                # the plugin only ever shows a layer it hid itself).
                hud_byte = emu.ptr(SCENE_ROOT) + HUD_LAYER_BYTE
                hud_value = emu.u8(hud_byte)
                emu.h.call("write_mem", addr=hud_byte, hex=f"{hud_value & 0x7F:02x}")
                try:
                    emu.wait_for("HUD layer hidden", lambda: not hud_layer_shown(emu), 30, every=1)
                except Timeout:
                    pass
                emu.advance(2)
                closed = shot("restyle-closed-nohud")
                emu.h.call("write_mem", addr=hud_byte, hex=f"{hud_value:02x}")
                try:
                    emu.wait_for("HUD layer restored", lambda: hud_layer_shown(emu), 30, every=1)
                except Timeout:
                    pass
                emu.advance(2)

                step = "Start mode: menu geometry"
                pos = lab.log_pos()
                emu.press(0, ["START"], frames=3, after=10)
                m = lab.wait_log("the lab menu to open with Start", MENU_OPENED, pos)
                if nav.training_paused(emu):
                    raise LabError("Start opened Training's pause as well as the lab menu")
                # The menu opens again where it was left: here the
                # Controls page, on Lab Menu Button, where Z closed it.
                emu.advance(4)
                shot("lab-reopen-controls")
                note(f"reopened on page {m.group('title')!r} row {m.group('row')} (depth {m.group('depth')}), "
                     f"where it was closed")
                if m.group("title") != "Controls" or int(m.group("row")) != CONTROLS_MENU_BUTTON:
                    raise LabError(f"the menu reopened on {m.group('title')!r} row {m.group('row')}, not where it "
                                   f"was left (Controls, row {CONTROLS_MENU_BUTTON})")
                lab.to_main_top(m)
                try:
                    emu.wait_for("HUD layer hidden while menu open", lambda: not hud_layer_shown(emu), 30, every=1)
                except Timeout:
                    pass
                emu.advance(4)
                opened = shot("restyle-main")
                # Hides the damage HUD and the player tags while its
                # menu is open (menu.c): the HUD layer is off, and the strip
                # under the panel no longer shows the damage HUD.
                hud_diff = region_diff(closed_hud, opened, xscale, BELOW_PANEL)
                note(f"menu open: HUD layer shown {hud_layer_shown(emu)}; under the panel the picture changed "
                     f"by {hud_diff:.1f} (mean per channel) against the closed match")
                if hud_layer_shown(emu) or hud_diff < 5.0:
                    raise LabError(f"the damage HUD still shows under the open menu (layer shown "
                                   f"{hud_layer_shown(emu)}, difference {hud_diff:.1f})")
                lab.tap(["A"])  # Lab: Frame Advance On, highlighted
                opened = shot("restyle-general")
                shot("restyle-toggle-on")
                menu_check(note, closed, opened, xscale, "Lab (Frame Advance On, highlighted)", cursor_row=0,
                           count=GEN_RAIL_ROWS, on_box_row=0, off_box_row=GEN_LOCK_PLAYER)
                lab.down(GEN_LOCK_PLAYER)
                opened = shot("restyle-toggle-off")
                menu_check(note, closed, opened, xscale, "Lab (Lock P1 Damage Off, highlighted)",
                           cursor_row=GEN_LOCK_PLAYER, count=GEN_RAIL_ROWS, on_box_row=0, off_box_row=GEN_LOCK_PLAYER)
                lab.up(GEN_LOCK_PLAYER)
                lab.tap(["A"])  # Frame Advance back Off via Lab menu
                if lab.setting(SET_FRAME_ADVANCE) != 0:
                    raise LabError("A on Lab > Frame Advance did not turn frame advance off")
                n_close = len(re.findall(r"lab: menu closed", emu.synced_log_text()))
                lab.tap(["B"])  # focus Draft C left rail
                if len(re.findall(r"lab: menu closed", emu.synced_log_text())) != n_close:
                    raise LabError("first B in options pane closed the lab menu instead of focusing left rail")
                lab.tap(["B"])  # B on rail closes the lab menu cleanly
                if len(re.findall(r"lab: menu closed", emu.synced_log_text())) != n_close + 1:
                    raise LabError("B on left rail did not close the lab menu")
                note("Start mode: B focuses left rail, second B on rail closes the menu")
                lab.open_menu()  # reopens with Draft C left rail still focused

                step = "Start mode: OSD Menu scroll and toggle"
                lab.menu_nav_to("OSDs > Individual OSDs")
                shot("restyle-osd-menu")
                ps_row = lab.menu_select_row("Powershield")["row"]
                lab.tap(["A"])
                shot("restyle-unavailable-skipped")
                on = lab.setting(SET_OSD_POWERSHIELD)
                lab.tap(["A"])
                if on != 1 or lab.setting(SET_OSD_POWERSHIELD) != 0:
                    raise LabError(
                        f"menu_nav_to OSDs > Individual OSDs > Powershield landed on row {ps_row}, "
                        f"A set Powershield to {on}")
                note(f"OSDs: live RAM nav landed on Powershield (row {ps_row})")
                # Scroll to the last row of Individual OSDs (OSD_ROWS = 21 > VISIBLE_ROWS = 10).
                # Visible row 0 (Act OoHitstun) has no descenders for glyph measurement.
                lab.menu_select_row("P+ Tech OSD")
                opened = shot("restyle-scrolled")
                menu_check(note, closed, opened, xscale, "OSDs scrolled to bottom",
                           cursor_row=shots.VISIBLE_ROWS - 1, count=OSD_ROWS,
                           scroll=OSD_ROWS - shots.VISIBLE_ROWS, cursor_box=True, name_row=0,
                           subpage=True)
                c0 = sim_clock(lab, human) + sim_clock(lab, cpu)
                emu.advance(40)
                c1 = sim_clock(lab, human) + sim_clock(lab, cpu)
                if c1 != c0:
                    raise LabError(f"the game ran while the Start lab menu was open: {c0} -> {c1}")
                note(f"Start mode: the game held while the menu was open (frame advance off): {c0}")

                step = "Start mode: Start closes, Exit opens Training's pause"
                pos = lab.log_pos()
                emu.press(0, ["START"], frames=3, after=10)
                lab.wait_log("Start to close the lab menu", r"lab: menu closed", pos)
                emu.advance(30)
                if nav.training_paused(emu):
                    raise LabError("closing the lab menu with Start opened Training's pause")
                c0 = sim_clock(lab, human)[1]
                emu.advance(30)
                if sim_clock(lab, human)[1] - c0 < 25:
                    raise LabError("the game did not run after the Start lab menu closed")
                try:
                    emu.wait_for("HUD layer back after menu close", lambda: hud_layer_shown(emu), 30, every=1)
                except Timeout:
                    pass
                emu.advance(2)
                again = shot("lab-hud-back")
                back_diff = region_diff(closed_hud, again, xscale, BELOW_PANEL)
                note(f"menu closed: HUD layer shown {hud_layer_shown(emu)}; under the panel {back_diff:.1f} from "
                     f"the match before the menu opened ({hud_diff:.1f} while it was open)")
                if not hud_layer_shown(emu) or back_diff > hud_diff / 2:
                    raise LabError("the damage HUD did not come back when the menu closed")
                shown = [lab.f.check_hud(f, "Start mode, the menu closed") for f in (human, cpu)]
                note(f"Start mode: the damage HUD shows the fighters' damage after the menu closed: {shown}")

                step = "lab match timer"
                lab_timer(emu, lab, note, shot, xscale)

                step = "Start mode: the Training Menu row opens Training's pause, Start closes it"
                start_menu_to_exit(emu, lab, then_exit=False)
                lab.menu_nav_to("Brawl Training")
                lab.tap(["A"])
                emu.wait_for("the Training Menu row to open Training's pause", lambda: nav.training_paused(emu), 60)
                nav.resume_training(emu)
                c0 = sim_clock(lab, human)[1]
                emu.advance(30)
                if sim_clock(lab, human)[1] - c0 < 25:
                    raise LabError("the game did not run after Training's pause was closed with Start")
                note("Start mode: the Training Menu row opened Training's pause; Start closed it and the game ran")

                step = "Start mode: Z in the menu opens Training's pause"
                z_to_training(emu, lab, human, note, "frame advance off")

                step = "Start mode: Exit leaves for the character select"
                start_menu_to_exit(emu, lab)
                exit_to_css(emu, lab, note, "first match, Start mode", EXIT_NO_PAUSE)

                step = "second match: settings kept"
                nav.start_training_match(emu, "mario", "fox")
                emu.advance(60)
                log = emu.synced_log_text()
                attaches = re.findall(r"main: attached in scMelee \S+ \S+; settings v\d+ at [0-9a-f]{8} ([a-z ]+)", log)
                if attaches[-2:] != ["set to defaults", "kept"]:
                    raise LabError(f"settings on attach: {attaches}")
                cpu2 = lab.f.by_player(1)
                if lab.f.damage(cpu2) != CPU_PERCENT:
                    raise LabError(f"second match: CPU damage {lab.f.damage(cpu2)}, lock should hold {CPU_PERCENT}")
                if lab.setting(SET_START_OPENS) != 1:
                    raise LabError("second match: Lab Menu Button is no longer Start")
                note(f"second match: settings kept, CPU damage locked at {lab.f.damage(cpu2)}, "
                     f"Lab Menu Button still Start")

                step = "second match: a savestate, then Exit with frame advance holding the game"
                pos = lab.log_pos()
                emu.h.call("pause")
                try:
                    emu.hold(0, ["DRIGHT"])
                    for i in range(60):
                        emu.h.call("frame_advance", n=1)
                        if i >= 10 and re.search(r"savestate: saved frame \d+", emu.synced_log_text()[pos:]):
                            break
                    emu.neutral(0)
                    emu.h.call("frame_advance", n=4)
                finally:
                    emu.neutral(0)
                    emu.h.call("resume")
                if not re.search(r"savestate: saved frame \d+", emu.synced_log_text()[pos:]):
                    raise LabError("holding D-pad right did not save a state")
                exit_pos = lab.log_pos()
                m = start_menu_to_exit(emu, lab, then_exit=False)
                lab.menu_nav_to("Lab > Frame Advance")
                lab.tap(["A"])  # Frame Advance On
                if lab.setting(SET_FRAME_ADVANCE) != 1:
                    raise LabError("General > Frame Advance did not turn frame advance on")
                step = "second match: Z in the menu with frame advance on"
                pos = lab.log_pos()
                emu.press(0, ["START"], frames=3, after=10)
                lab.wait_log("the lab menu to close", r"lab: menu closed", pos)
                wait_frozen(emu, True, frames=90)
                emu.advance(10)
                z_to_training(emu, lab, cpu2, note, "frame advance on", advancing=True)
                m = start_menu_to_exit(emu, lab, then_exit=False)
                lab.menu_nav_to("Return to CSS")
                exit_to_css(emu, lab, note, "second match, Start mode, frame advance on", EXIT_NO_PAUSE)
                freed = re.findall(r"savestate: \d+ saves, \d+ loads, \d+ refused; slot (\w+[\w ]*)",
                                   emu.synced_log_text()[exit_pos:])
                if freed != ["freed"]:
                    raise LabError(f"the savestate slot after Exit: {freed}")
                note("second match: the savestate slot was freed when the match module detached after Exit")

                step = "third match: Exit from the lab menu over Training's pause"
                nav.start_training_match(emu, "mario", "fox")
                emu.advance(60)
                m = start_menu_to_exit(emu, lab, then_exit=False)
                lab.menu_nav_to("Lab > Controls > Menu Open")
                lab.set_choice(SET_START_OPENS, 0)
                pos = lab.log_pos()
                emu.press(0, ["START"], frames=3, after=10)
                lab.wait_log("Start to close the lab menu", r"lab: menu closed", pos)
                emu.advance(10)
                # Frame advance is still on: Start goes to Training's pause
                # by the lab's own Start hand-over.
                nav.pause_training(emu)
                lab.open_menu(keep=True)
                lab.menu_nav_to("Return to CSS")
                exit_to_css(emu, lab, note, "third match, Z over Training's pause", EXIT_FROM_PAUSE)

                step = "fourth match: Training's own Finish"
                nav.start_training_match(emu, "mario", "fox")
                emu.advance(60)
                pos = lab.log_pos()
                nav.finish_training(emu)
                try:
                    lab.wait_log("fourth match: match module unload", MTCH_UNLOADED, pos, frames=120)
                except Timeout:
                    pass
                emu.advance(10)
                full_log = emu.synced_log_text()
                if "lab: exit:" in full_log[pos:]:
                    raise LabError("Training's Finish went through the lab's Exit")
                check_match_end(full_log[pos:], "fourth match, Training's Finish")
                note("fourth match: Training's pause menu Finish still leaves for the character select")
                check_heap_kept(full_log, note)
            except InfoOnly:
                pass
            except (Timeout, nav.NavError, HarnessError, LabError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    taken.append(emu.screenshot("fail-lab"))
                    print("state at failure:", nav.scene(emu)[:2])
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(rf"\[{L.tag()}\] (main|module|lab|fighters|hook pads)", line):
            print("  " + line)
    print()
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    if not why and bad:
        why = "problem lines in the log"
    if not why and not opts.info_only:
        m = re.findall(r"lab: pad callbacks: system (\d+), game (\d+) \((\d+) while stopped\), frame-advance steps (\d+)", log)
        if not m:
            why = "no pad-callback summary in the log"
        else:
            print(f"pad callbacks per match (system, game, game while stopped, advance steps): {m}")
            if int(m[0][3]) != opts.presses:
                why = f"the plugin counted {m[0][3]} frame-advance steps for {opts.presses} presses"
    for path, w, h in taken:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
