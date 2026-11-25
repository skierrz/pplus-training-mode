"""Project+ 3.2.1 menu knowledge for scripted runs: where the game keeps the
scene, character-select and stage-select state, and recipes that drive them
with controller input only (memory is read, never written).

The character-select and stage-select offsets and the player-state values
come from Brawl Online's harness library (harness/ppharness/brawl.py in its
repository), which credits several of them to Orca. They and the other
offsets here are read live in this project's runs (RSBE01 rev 2, P+ 3.2.1).
The idea of steering a menu cursor with the stick while reading its position
is also that library's; the steering rule here (speed tiers, settling, probing
around a point, one retry) is this project's own.
"""
import math
import re
import struct

from harness import Timeout

# gfSceneManager instance pointer; +0x4 current scene, +0x10 current sequence,
# each an object whose first word points at its name.
SCENE_MANAGER = 0x805A0060
GAME_GLOBAL = 0x805A00E0

# Character select (scSelctCharacter): scene+0x400 -> its task; task+0x44 +
# 4*n -> player area n (area 0 is P1; in Training area 1 is the CPU).
CSS_TASK = 0x400
CSS_AREAS = 0x44
AREA_HAND = 0x1A8       # -> hand
AREA_KIND = 0x1B4       # 0 empty, 1 human, 2 CPU
AREA_CHARACTER = 0x1B8  # CSS icon id of the token (or under the held token)
AREA_COSTUME = 0x1BC    # costume index
AREA_TOKEN_HELD = 0x1F8  # u8
HAND_TARGET = 0x80      # what the hand points at, see HAND_*
HAND_X = 0x90           # float, y up
HAND_Y = 0x94
HAND_GRID_HOLDING = 7   # over the icon grid with a token in hand

# Stage select (scSelStage): scene+0x3AC -> task.
SSS_TASK = 0x3AC
SSS_HAZARD_OFF = 0x45   # u8: P+ StageFiles.asm:1538, 0 hazards ON, 1 hazards OFF
SSS_CURSOR = 0x200      # -> cursor; +0x3C x, +0x40 y (y up)
SSS_STATE = 0x224       # 0 while choosing
SSS_HOVERED = 0x244     # page position + 2 under the cursor, 0 for none
SSS_TAKEN_KIND = 0x258

# gmGlobalModeMelee (GameGlobal+0x8): byte 0x8 holds the game mode in its top
# six bits, +0x1A the stage kind, +0x29 bit 0x20 P+'s m_isHazardOff
# (Random.asm:1024-1031), players at +0x98 (0x5C each: +0 character kind,
# +1 0 human / 1 CPU / 3 none, +5 costume file number).
MODE_TRAINING = 0xD
MODE_VERSUS = 0x0
MELEE_STAGE = 0x1A
MELEE_HAZARD_FLAGS = 0x29
MELEE_HAZARD_OFF_BIT = 0x20
MELEE_PLAYERS = 0x98
MELEE_PLAYER_SIZE = 0x5C

# g_GameFrame.frameCounter (GameGlobal heap): 1 on a match's first simulated
# frame, +1 per simulated frame; it stops while Training is paused.
GAME_FRAME_COUNTER = 0x901812A4

# scMelee+0x54 -> stOperatorRuleMelee; +0xDF is set once the match has started.
# scMelee+0x68 -> the info operator, in Training a StOperatorInfoTraining
# whose +0x128 is 1 while the pause menu is open.
MELEE_RULE = 0x54
RULE_STARTED = 0xDF
MELEE_INFO = 0x68
INFO_TRAINING_MENU_OPEN = 0x128

# Character select icon grid on P+ 3.2.1, top row first: (name, icon id).
# Odd rows hold 9 icons, even rows 8, offset by half an icon. The id is what
# AREA_CHARACTER reads with the hand (or token) on that icon.
CSS_GRID = [
    [("mario", 0x00), ("luigi", 0x09), ("peach", 0x0D), ("wario", 0x15), ("yoshi", 0x05),
     ("bowser", 0x0C), ("donkey_kong", 0x01), ("diddy_kong", 0x1A), ("captain_falcon", 0x0A)],
    [("fox", 0x07), ("falco", 0x13), ("wolf", 0x25), ("link", 0x02), ("toon_link", 0x24),
     ("zelda", 0x0E), ("sheik", 0x0F), ("ganondorf", 0x14)],
    [("pikachu", 0x08), ("jigglypuff", 0x23), ("mewtwo", 0x2E), ("squirtle", 0x2B),
     ("ivysaur", 0x2C), ("charizard", 0x2A), ("lucario", 0x20), ("samus", 0x03),
     ("zero_suit_samus", 0x04)],
    [("ness", 0x0B), ("lucas", 0x19), ("kirby", 0x06), ("meta_knight", 0x16),
     ("king_dedede", 0x1F), ("marth", 0x11), ("roy", 0x2D), ("ike", 0x21)],
    [("game_and_watch", 0x12), ("rob", 0x22), ("ice_climbers", 0x10), ("pit", 0x17),
     ("random", 0x29), ("olimar", 0x18), ("snake", 0x26), ("sonic", 0x27), ("knuckles", 0x30)],
]
# Hand coordinates of the icon centres: rows from the top, columns 6.35 apart.
CSS_ROW_Y = [13.0, 10.0, 7.0, 4.0, 1.3]
CSS_COL_X0 = (-25.4, -22.2)  # first icon of odd / even rows
CSS_COL_STEP = 6.35
CSS_ID = {}
CSS_POS = {}
for _r, _row in enumerate(CSS_GRID):
    for _c, (_name, _id) in enumerate(_row):
        CSS_ID[_name] = _id
        CSS_POS[_name] = (CSS_COL_X0[_r % 2] + CSS_COL_STEP * _c, CSS_ROW_Y[_r])
CSS_NAME = {v: k for k, v in CSS_ID.items()}
# A drill's select may keep only some icons (the plugin's menu/game/select.cpp):
# the task's icon list (+0x70, a word each, count at +0x138) holds them, icon n
# in the layout's slot n, the slots in CSS_GRID's order.
CSS_ROSTER = 0x70
CSS_ROSTER_COUNT = 0x138
CSS_SLOT_POS = [CSS_POS[_name] for _row in CSS_GRID for _name, _ in _row]

# Stage select page 0: 3 rows of 7 icons. Cursor coordinates of the icon
# centres; an icon's page position is row * 7 + column.
SSS_ROW_Y = [-4.0, -8.3, -14.3]
SSS_COL_X0 = -17.5
SSS_COL_STEP = 6.55
SSS_HOVER_BASE = 2      # SSS_HOVERED reads page position + 2
# srStageKind and page-0 position of the stages the recipes can pick (read
# by taking every position once).
STAGES = {
    "final_destination": (0x02, 10),
    "battlefield": (0x01, 17),
    "smashville": (0x21, 16),
    "pokemon_stadium_2": (0x2E, 18),
    "yoshis_story": (0x2A, 0),
    "dream_land": (0x2D, 12),
}
# Stages for the menu module's test-only direct match start (srStageKind,
# src/menu/names.cpp): Final Destination, Battlefield, and Green Hill Zone
# (moving platform).
QUICK_STAGES = {
    "final_destination": 0x02,
    "battlefield": 0x01,
    "green_hill_zone": 0x23,
}
MENU_TEST_MAGIC = 0x544D4D4E  # "TMMN" (src/menu/drill_list.cpp)


class NavError(RuntimeError):
    pass


# ---- readers ---------------------------------------------------------------

def scene(emu):
    """(scene name, sequence name, scene object); names are None when unknown."""
    manager = emu.ptr(SCENE_MANAGER)
    if manager is None:
        return None, None, None
    obj = emu.ptr(manager + 0x4)
    seq = emu.ptr(manager + 0x10)
    name = emu.cstr(emu.ptr(obj)) if obj else None
    seq_name = emu.cstr(emu.ptr(seq)) if seq else None
    return name, seq_name, obj


def css_area(emu, index):
    """dict of one CSS player area, or None outside the character select."""
    name, _, obj = scene(emu)
    if name != "scSelctCharacter":
        return None
    task = emu.ptr(obj + CSS_TASK)
    area = emu.ptr(task + CSS_AREAS + 4 * index) if task else None
    if area is None:
        return None
    raw = emu.read(area + AREA_KIND, 0x48)
    out = {
        "area": area,
        "kind": int.from_bytes(raw[0:4], "big"),
        "character": int.from_bytes(raw[4:8], "big"),
        "costume": int.from_bytes(raw[8:12], "big"),
        "held": raw[AREA_TOKEN_HELD - AREA_KIND] != 0,
        "target": None, "x": None, "y": None,
    }
    hand = emu.ptr(area + AREA_HAND)
    if hand:
        hraw = emu.read(hand + HAND_TARGET, 0x18)
        out["target"] = struct.unpack_from(">I", hraw, 0)[0]
        out["x"], out["y"] = struct.unpack_from(">ff", hraw, HAND_X - HAND_TARGET)
    return out


def css_grid(emu):
    """The icon ids of Training's character select grid, slot by slot."""
    name, _, obj = scene(emu)
    if name != "scSelctCharacter":
        return None
    task = emu.ptr(obj + CSS_TASK)
    n = min(emu.u32(task + CSS_ROSTER_COUNT), 50)
    return list(struct.unpack(f">{n}I", emu.read(task + CSS_ROSTER, 4 * n))) if n else []


def icon_pos(emu, character):
    """Hand coordinates of `character`'s icon in the grid shown."""
    icons = css_grid(emu)
    if icons is None or CSS_ID[character] not in icons:
        raise NavError(f"{character} is not on the character select's grid")
    return CSS_SLOT_POS[icons.index(CSS_ID[character])]


def sss(emu):
    """dict of the stage select's cursor and state, or None outside it."""
    name, _, obj = scene(emu)
    if name != "scSelStage":
        return None
    task = emu.ptr(obj + SSS_TASK)
    cursor = emu.ptr(task + SSS_CURSOR) if task else None
    if cursor is None:
        return None
    cxy = emu.read(cursor + 0x3C, 8)
    traw = emu.read(task + SSS_STATE, SSS_TAKEN_KIND - SSS_STATE + 4)
    return {
        "x": struct.unpack_from(">f", cxy, 0)[0], "y": struct.unpack_from(">f", cxy, 4)[0],
        "state": struct.unpack_from(">I", traw, 0)[0],
        "hovered": struct.unpack_from(">I", traw, SSS_HOVERED - SSS_STATE)[0],
        "taken": struct.unpack_from(">I", traw, SSS_TAKEN_KIND - SSS_STATE)[0],
        "hazard_off": emu.u8(task + SSS_HAZARD_OFF),
    }


def melee_setup(emu):
    """What the menus handed to the match: game mode, stage, players."""
    gg = emu.ptr(GAME_GLOBAL)
    mode = emu.ptr(gg + 0x8) if gg else None
    if mode is None:
        return None
    hdr = emu.read(mode + 0x8, MELEE_HAZARD_FLAGS - 0x8 + 1)
    raw = emu.read(mode + MELEE_PLAYERS, 4 * MELEE_PLAYER_SIZE)
    players = []
    for i in range(4):
        p = raw[i * MELEE_PLAYER_SIZE:(i + 1) * MELEE_PLAYER_SIZE]
        players.append({"kind": p[0], "state": p[1], "costume_file": p[5]})
    return {
        "mode": hdr[0] >> 2,
        "stage": struct.unpack_from(">H", hdr, MELEE_STAGE - 0x8)[0],
        "hazard_off": 1 if (hdr[MELEE_HAZARD_FLAGS - 0x8] & MELEE_HAZARD_OFF_BIT) else 0,
        "players": players,
    }


def match_started(emu):
    name, _, obj = scene(emu)
    if name != "scMelee":
        return False
    rule = emu.ptr(obj + MELEE_RULE)
    return bool(rule) and emu.u8(rule + RULE_STARTED) != 0


# ---- steering ----------------------------------------------------------------

# Stick tilts by distance to the target, in cursor units: fast while far, a
# slower approach, then the smallest tilt the menus still move a cursor with.
STEER_TIERS = ((4.0, 100.0), (1.2, 60.0), (0.0, 30.0))
STEER_NEAR = 0.3      # closer than this counts as on the target point
STEER_SETTLE = 12     # frames a cursor at rest is given to report its target
DROP_NEAR = 0.6       # how close to an icon's centre a token is let go
# Where to try next when the target point itself is not on the item: small
# steps around it.
STEER_PROBES = ((0.0, 0.0), (0.5, 0.0), (-0.5, 0.0), (0.0, 0.5), (0.0, -0.5))


def stick_toward(x, y, tx, ty):
    """Main-stick value moving a cursor at (x, y) toward (tx, ty) (y up), by
    the tiers above; neutral when on the point."""
    dx, dy = tx - x, ty - y
    dist = math.hypot(dx, dy)
    if dist < 0.05:
        return 128, 128
    tilt = next(t for limit, t in STEER_TIERS if dist >= limit)
    return int(round(128 + dx / dist * tilt)), int(round(128 + dy / dist * tilt))


def _settled(emu, done, port):
    """Lets go of the stick and waits for done() to hold with the cursor at
    rest. The game sees a stick change about two frames late and a menu can
    take a few more to report what its cursor is on."""
    emu.neutral(port)
    for _ in range(STEER_SETTLE):
        emu.advance(1)
        if done():
            emu.advance(4)
            if done():
                return True
    return False


def _approach(emu, read_xy, tx, ty, done, port, frames):
    """Steers toward (tx, ty) for up to `frames` polls. True once done()
    holds at rest; False when the point is reached without it or time is up."""
    start = emu.polls()
    while emu.polls() - start < frames:
        if done() and _settled(emu, done, port):
            return True
        x, y = read_xy()
        if math.hypot(tx - x, ty - y) < STEER_NEAR:
            return _settled(emu, done, port)
        emu.hold(port, stick=stick_toward(x, y, tx, ty))
        emu.advance(1)
    emu.neutral(port)
    return False


def steer(emu, what, read_xy, tx, ty, done, port=0, timeout_frames=600):
    """Move a menu cursor toward (tx, ty) until done() holds with the stick
    released and the cursor at rest. read_xy() returns the cursor position.
    If the point is reached and done() does not hold, points just around it
    are tried; if the cursor does not get there at all (a menu that was not
    taking input yet), the whole approach is tried once more after a pause."""
    for attempt in range(2):
        for ox, oy in STEER_PROBES:
            if _approach(emu, read_xy, tx + ox, ty + oy, done, port, timeout_frames):
                return
            x, y = read_xy()
            if math.hypot(tx + ox - x, ty + oy - y) >= STEER_NEAR:
                break  # never arrived: probing around the point cannot help
        if attempt == 0:
            emu.neutral(port)
            emu.advance(30)
    emu.neutral(port)
    raise Timeout(f"{what} (cursor at {read_xy()}, aimed at ({tx:.1f}, {ty:.1f}))")


# ---- the plugin's drill list ------------------------------------------------------
# With the plugin's menu module in the file, Training's character select opens
# with the drill list over it, which takes every controller until it closes.
# Its state is read from the plugin's log lines.

MENU_MODULE_OK = re.compile(r"\] module MENU: id \d+, .*: ok")
DRILL_EVENT = re.compile(r"\] drills: (list opened|list closed|list stays closed|starting)")


def drill_list_state(emu):
    """The drill list on the current visit to Training's character select:
    "open" or "closed", "absent" without the menu module, None until the
    module's lines for this visit are in the log."""
    log = emu.log_text()
    if not MENU_MODULE_OK.search(log):
        return "absent"
    attached = log.rfind("] menu: attached")
    if attached < 0 or log.count("] menu: attached") != log.count("] menu: detaching") + 1:
        return None
    events = DRILL_EVENT.findall(log, attached)
    if not events:
        return None
    return "open" if events[-1] == "list opened" else "closed"


# A drill with character rules builds the select for them (menu/game/select.cpp):
# one area without a CPU, preset characters, locked coins (bit 0 P1, bit 1 CPU).
SELECT_BUILT = re.compile(r"\] select: built for drill (\d+): (\d) area\(s\)[^,]*, P1 icon (\d+), CPU icon (\d+), "
                          r"locked ([0-9a-f]+)")


def select_rule(emu):
    """How the plugin built the character select on this visit, from its log:
    None for the normal select, else {"drill", "areas", "p1", "cpu", "locked"}."""
    log = emu.log_text()
    attached = log.rfind("] menu: attached")
    if attached < 0:
        return None
    found = list(SELECT_BUILT.finditer(log, attached))
    if not found:
        return None
    g = found[-1].groups()
    return {"drill": int(g[0]), "areas": int(g[1]), "p1": int(g[2]), "cpu": int(g[3]), "locked": int(g[4], 16)}


def close_drill_list(emu, timeout_frames=300, port=0):
    """On Training's character select: close the drill list with B on `port`
    if it is open. Returns True if it was open."""
    state = emu.wait_for("the drill list's state in the log", lambda: drill_list_state(emu), timeout_frames,
                         every=2)
    if state != "open":
        return False
    press_until(emu, "the drill list to close", ["B"], lambda: drill_list_state(emu) == "closed", 300,
                every=20, port=port)
    emu.advance(5)
    return True


# ---- recipes -------------------------------------------------------------------

def _css_panels_ready(emu):
    p1, cpu = css_area(emu, 0), css_area(emu, 1)
    return bool(p1 and cpu and p1["kind"] == 1 and cpu["kind"] == 2 and p1["held"])


def _try_load_css_cache(emu, cache, drill_list):
    """Restore Training CSS from `cache` once Brawl (RSBE01) has started.
    Returns True on a verified restore, False to fall back to cold boot."""
    import time
    t0 = time.monotonic()
    key, sav, meta = cache.load_meta()
    if not key:
        return False
    try:
        deadline = time.monotonic() + 30.0
        while emu.h.call("status").get("game_id") != "RSBE01":
            if time.monotonic() > deadline:
                print("css cache: timed out waiting for RSBE01; falling back to cold boot")
                return False
            time.sleep(0.05)
        emu.h.call("pause")
        emu.h.call("load_state", path=sav)
        emu.neutral(0)
        emu.h.call("frame_advance", n=1)
        emu.seed_boot_log(meta["boot_log"])
        emu.h.call("resume")
        name, seq, _ = scene(emu)
        if (name, seq) != ("scSelctCharacter", "sqTraining") or not _css_panels_ready(emu):
            raise NavError(f"unexpected state after load_state ({name} / {seq})")
        state = drill_list_state(emu)
        if state not in ("open", "absent"):
            raise NavError(f"unexpected drill list state after load_state: {state!r}")
        cache.touch(key)
        print(f"css cache: hit ({key[:8]}, {cache.backend}, {time.monotonic() - t0:.2f}s)")
        if drill_list != "keep":
            close_drill_list(emu)
        return True
    except Exception as exc:
        print(f"css cache: restore failed ({exc}); purging cache and falling back to cold boot")
        cache.invalidate(key)
        emu.clear_boot_log()
        try:
            emu.h.call("resume")
        except Exception:
            pass
        emu.hold(0, buttons=["L"])
        return False


def boot_to_training_css(emu, timeout_frames=60 * 120, drill_list="close"):
    """From power-on to Training's character select. L is held through the
    boot: P+ then opens Training's with or without the plugin (which opens it
    with no button held; test-boot covers that). The plugin's drill list is
    closed unless `drill_list` is "keep". When `emu.css_cache` is attached and
    holds a matching savestate, restores straight to Training CSS instead of
    cold-booting."""
    # Hold L from the start so a fallback to cold boot still lands in Training.
    emu.hold(0, buttons=["L"])
    cache = getattr(emu, "css_cache", None)
    if cache is not None and cache.can_load():
        if _try_load_css_cache(emu, cache, drill_list):
            return
    emu.wait_for("the character select screen after boot",
                 lambda: scene(emu)[0] == "scSelctCharacter", timeout_frames, every=5)
    emu.neutral(0)
    name, seq, _ = scene(emu)
    if seq != "sqTraining":
        raise NavError(f"booted to {name} / {seq}, not Training")
    if cache is not None:
        emu.wait_for("the drill list's state in the log", lambda: drill_list_state(emu), 300, every=2)
        emu.wait_for("the P1 and CPU panels", lambda: _css_panels_ready(emu), 600, every=2)
        emu.advance(20)
        try:
            boot_log = emu.synced_log_text()
            cache.save(emu, boot_log)
        except Exception as exc:
            print(f"css cache: save skipped ({exc})")
        if drill_list != "keep":
            close_drill_list(emu)
        return
    if drill_list != "keep":
        close_drill_list(emu)
    emu.wait_for("the P1 and CPU panels", lambda: _css_panels_ready(emu), 600, every=2)
    emu.advance(20)


def _hand_xy(emu):
    a = css_area(emu, 0)
    if a is None:
        raise NavError(f"left the character select ({scene(emu)[:2]})")
    return a["x"], a["y"]


def _hand_over(emu, character, holding, target_xy=None):
    """The held token is over `character`, with the hand near the icon's
    centre. A token lies where it was let go, and _pick_up_token looks for
    it around the centre; stopping at the icon's edge let tokens go up to
    1.5 off it."""
    a = css_area(emu, 0)
    if a is None or a["target"] != HAND_GRID_HOLDING:
        return False
    tx, ty = icon_pos(emu, character) if target_xy is None else target_xy
    char_id = a["character"] if holding != "cpu" else css_area(emu, 1)["character"]
    return char_id == CSS_ID[character] and math.hypot(a["x"] - tx, a["y"] - ty) < DROP_NEAR


def _drop_p1_token(emu, character):
    """Carry the token in P1's hand onto `character` and drop it."""
    tx, ty = icon_pos(emu, character)
    steer(emu, f"P1's token onto {character}", lambda: _hand_xy(emu), tx, ty,
          lambda: _hand_over(emu, character, "p1", (tx, ty)))
    emu.press(0, ["A"])
    emu.wait_for(f"P1's token to drop on {character}", lambda: not css_area(emu, 0)["held"], 30)
    if css_area(emu, 0)["character"] != CSS_ID[character]:
        raise NavError(f"P1's token landed on {css_area(emu, 0)['character']:#x}, not {character}")


def _pick_up_token(emu, character, area):
    """With P1's hand free, pick up the token lying on `character` that
    belongs to CSS area `area` (0 P1, 1 CPU). Over a token the hand's target
    reads 3; only one token may lie on that icon."""
    tx, ty = icon_pos(emu, character)
    steer(emu, f"P1's hand onto the token on {character}", lambda: _hand_xy(emu), tx, ty,
          lambda: (a := css_area(emu, 0)) is not None and a["target"] == 3
                  and math.hypot(a["x"] - tx, a["y"] - ty) < 2.0)
    emu.press(0, ["A"])
    emu.wait_for(f"the token on {character} to be picked up", lambda: css_area(emu, area)["held"], 30)


def _drop_cpu_token(emu, character):
    tx, ty = icon_pos(emu, character)
    steer(emu, f"the CPU token onto {character}", lambda: _hand_xy(emu), tx, ty,
          lambda: _hand_over(emu, character, "cpu", (tx, ty)))
    emu.press(0, ["A"])
    emu.wait_for(f"the CPU token to drop on {character}", lambda: not css_area(emu, 1)["held"], 30)
    if css_area(emu, 1)["character"] != CSS_ID[character]:
        raise NavError(f"CPU token landed on {css_area(emu, 1)['character']:#x}, not {character}")


def pick_characters(emu, p1, cpu, p1_costume=0):
    """From the start of Training's character select (P1's token in P1's
    hand, the CPU token on some icon): P1 on `p1` in costume index
    `p1_costume` (stepped with X), the CPU on `cpu` (None: left as it is,
    for a select without a CPU or with its character locked)."""
    if cpu is None:
        if p1 is not None:
            _drop_p1_token(emu, p1)
        step_costume(emu, p1_costume)
        return
    cpu_now = CSS_NAME.get(css_area(emu, 1)["character"])
    if cpu_now is None:
        raise NavError(f"CPU token on unknown icon {css_area(emu, 1)['character']:#x}")
    # Never leave P1's token on the icon the CPU token has to be taken from.
    park = None
    if cpu_now == p1 and cpu != p1:
        park = next(n for n in ("mario", "luigi", "peach") if n not in (cpu_now, cpu))
    _drop_p1_token(emu, park or p1)
    if cpu_now != cpu:
        _pick_up_token(emu, cpu_now, 1)
        _drop_cpu_token(emu, cpu)
    if park:
        _pick_up_token(emu, park, 0)
        _drop_p1_token(emu, p1)
    step_costume(emu, p1_costume)


def step_costume(emu, p1_costume):
    """P1's costume index to `p1_costume`, stepped with X."""
    for _ in range(40):
        if css_area(emu, 0)["costume"] == p1_costume:
            return
        emu.press(0, ["X"], frames=3, after=6)
    raise NavError(f"P1's costume stuck at {css_area(emu, 0)['costume']}, wanted {p1_costume}")


def press_until(emu, what, buttons, until, timeout_frames, every=60, port=0):
    """Tap `buttons` on `port` every `every` frames until until() holds."""
    start = emu.polls()
    while emu.polls() - start < timeout_frames:
        emu.press(port, buttons)
        try:
            return emu.wait_for(what, until, every, every=2)
        except Timeout:
            continue
    raise Timeout(f"{what} (gave up after {timeout_frames} frames)")


def pick_stage(emu, stage, hazard_off=False):
    """`stage` is a name in STAGES, or a page-0 icon position (any stage
    kind accepted). When `hazard_off` is True, toggles P+'s stage-select
    hazard switch with Z before confirming."""
    kind, position = STAGES[stage] if isinstance(stage, str) else (None, stage)
    emu.wait_for("the stage select cursor", lambda: sss(emu), 600, every=2)
    emu.advance(20)
    row, col = divmod(position, 7)
    tx, ty = SSS_COL_X0 + SSS_COL_STEP * col, SSS_ROW_Y[row]

    def cursor():
        s = sss(emu)
        if s is None:
            raise NavError(f"left the stage select ({scene(emu)[:2]})")
        return s["x"], s["y"]
    steer(emu, f"the stage cursor onto {stage}", cursor, tx, ty,
          lambda: sss(emu)["hovered"] == position + SSS_HOVER_BASE)
    want_haz = 1 if hazard_off else 0
    if sss(emu)["hazard_off"] != want_haz:
        press_until(emu, f"stage hazards {'off' if hazard_off else 'on'}", ["Z"],
                    lambda: (sss(emu) or {}).get("hazard_off") == want_haz, 120, every=15)
        emu.advance(4)
    emu.press(0, ["A"])
    taken = emu.wait_for(f"the stage select to take {stage}",
                         lambda: (sss(emu) or {}).get("taken") or scene(emu)[0] != "scSelStage", 120)
    if kind is not None and taken is not True and taken != kind:
        raise NavError(f"the stage select took stage kind {taken:#x}, not {stage}")


def choose_on_css(emu, p1, cpu, p1_costume=0):
    """On Training's character select, from the boot state (P1's token in
    P1's hand) or the state a finished match leaves (both tokens on icons).
    On a select a drill's rule built (select_rule): no CPU to pick without
    its area, and a locked character must be the one asked for."""
    close_drill_list(emu)
    rule = select_rule(emu)
    solo = rule is not None and rule["areas"] == 1

    def ready():
        p, c = css_area(emu, 0), css_area(emu, 1)
        return p and p["kind"] == 1 and (solo or (c and c["kind"] == 2))
    emu.wait_for("the P1 and CPU panels", ready, 600, every=2)
    emu.advance(20)
    locked = rule["locked"] if rule else 0
    if locked & 2:
        if CSS_ID[cpu] != css_area(emu, 1)["character"]:
            raise NavError(f"the CPU is locked on {CSS_NAME.get(css_area(emu, 1)['character'])}, not {cpu}")
        cpu = None
    if solo:
        cpu = None
    area = css_area(emu, 0)
    if locked & 1:
        if CSS_ID[p1] != area["character"]:
            raise NavError(f"P1 is locked on {CSS_NAME.get(area['character'])}, not {p1}")
        pick_characters(emu, None, None, p1_costume)
        return
    if not area["held"]:
        on = CSS_NAME.get(area["character"])
        if on is None:
            raise NavError(f"P1's token on unknown icon {area['character']:#x}")
        _pick_up_token(emu, on, 0)
    pick_characters(emu, p1, cpu, p1_costume)


def wait_match_start(emu, timeout_frames=60 * 60):
    emu.wait_for("the match scene", lambda: scene(emu)[0] == "scMelee", timeout_frames, every=5)
    emu.wait_for("the match to start", lambda: match_started(emu), 60 * 30, every=1)


def confirm_css_skip_stage(emu, timeout_frames=600, port=0):
    """Confirm Training's character select with Start for a drill that fixes
    its stage: waits for the select to exit straight into the match load and
    fails if scSelStage appears on any polled frame."""
    start = emu.polls()
    pressed = -999
    while emu.polls() - start < timeout_frames:
        name = scene(emu)[0]
        if name == "scSelStage":
            raise NavError("entered scSelStage (expected stage select to be skipped)")
        if name != "scSelctCharacter":
            return name
        if emu.polls() - pressed >= 60:
            emu.press(port, ["START"])
            pressed = emu.polls()
        else:
            emu.advance(1)
    raise Timeout(f"the character select to exit into the match (still in {scene(emu)[0]})")


def start_training_match(emu, p1, cpu, stage="final_destination", p1_costume=0, seed_rng=False, during=None,
                         skip_stage_select=False, hazard_off=False):
    """From Training's character select to a running Training match
    (with seed_rng, see seed_match_rng; an int other than True is the seed
    the two generators get). `during(emu)`, if given, runs right after the
    stage is picked (or after confirming the character select when
    `skip_stage_select` is True) to watch the match load and start frame by
    frame."""
    pos = len(emu.log_text())
    choose_on_css(emu, p1, cpu, p1_costume)
    if skip_stage_select:
        confirm_css_skip_stage(emu)
    else:
        press_until(emu, "the stage select", ["START"], lambda: scene(emu)[0] == "scSelStage", 600)
        pick_stage(emu, stage, hazard_off=hazard_off)
    if seed_rng is True:
        seed_match_rng(emu)
    elif seed_rng:
        seed_match_rng(emu, seed=seed_rng)
    if during is not None:
        during(emu)
    wait_match_start(emu)
    if skip_stage_select and "scene: scSelStage" in emu.log_text()[pos:]:
        raise NavError("scSelStage ran when the drill should skip the stage select")
    setup = melee_setup(emu)
    if setup["mode"] != MODE_TRAINING or (isinstance(stage, str) and setup["stage"] != STAGES[stage][0]):
        raise NavError(f"match setup is mode {setup['mode']:#x}, stage {setup['stage']:#x}")
    if [p["state"] for p in setup["players"][:2]] != [0, 1]:
        raise NavError(f"match players are {setup['players'][:2]}, wanted a human and a CPU")
    return setup


def start_quick_match(emu, stage="final_destination", p1=(0x00, 0x00), cpu=(0x07, 0x07), port=0, seed_rng=False):
    """Start a Training match directly from Training's character select via
    the menu module's test block (src/menu/drill_list.cpp). `stage` is a name
    in QUICK_STAGES / STAGES or an integer srStageKind; `p1` and `cpu` are
    (gmCharacterKind, CSS icon id) pairs (default Mario vs Fox CPU)."""
    import struct
    found = re.findall(r"\] drills: test block at ([0-9a-f]{8})", emu.log_text())
    if not found:
        raise NavError("no drills test block line in the log")
    addr = int(found[-1], 16)
    if emu.u32(addr) != MENU_TEST_MAGIC:
        raise NavError(f"no drills test block at {addr:#x}")
    if isinstance(stage, str):
        if stage in QUICK_STAGES:
            stage_kind = QUICK_STAGES[stage]
        elif stage in STAGES:
            stage_kind = STAGES[stage][0]
        else:
            raise NavError(f"unknown quick match stage {stage!r}")
    else:
        stage_kind = int(stage)
    p1_kind, p1_icon = p1
    cpu_kind, cpu_icon = cpu
    payload = struct.pack(">BBBBHBB", p1_kind, p1_icon, cpu_kind, cpu_icon, stage_kind, port, 0)
    pos = len(emu.log_text())
    emu.h.call("write_mem", addr=addr + 0xC, hex=payload.hex())
    emu.h.call("write_mem", addr=addr + 4, hex="00000001")
    rx = re.compile(r"\] drills: (starting test match|test start refused: [^\r\n]*)")
    m = emu.wait_for("the test quick match to start", lambda: rx.search(emu.log_text(), pos), 120, every=2)
    if not m.group(1).startswith("starting"):
        raise NavError(f"quick match {m.group(1)}")
    if seed_rng is True:
        seed_match_rng(emu)
    elif seed_rng:
        seed_match_rng(emu, seed=seed_rng)
    wait_match_start(emu)
    return melee_setup(emu)


def enter_training_match(emu, p1="mario", cpu="fox", stage="final_destination", p1_costume=0,
                         seed_rng=False):
    """Power-on to a running Training match: P1 `p1` (costume index
    `p1_costume`) against a CPU `cpu` on `stage`. Returns the match setup."""
    boot_to_training_css(emu)
    return start_training_match(emu, p1, cpu, stage, p1_costume, seed_rng)


def game_frame(emu):
    return emu.u32(GAME_FRAME_COUNTER)


def pause_at_game_frame(emu, target):
    """Pause emulation exactly when the match's frame counter reads `target`
    (run to just before it, then step frame by frame). Returns with
    emulation paused; the caller resumes it with emu.h.call("resume")."""
    while game_frame(emu) < target - 20:
        emu.advance(5)
    emu.h.call("pause")
    w = emu.watch(lambda: game_frame(emu))
    now = w.read()
    for _ in range(200):
        if now >= target:
            if now != target:
                raise NavError(f"stepped past game frame {target} (at {now})")
            return now
        now = emu.step(w)
    raise NavError(f"game frame {target} not reached by stepping (at {now})")


# g_taskScheduler (RSBE01_02 symbol) points at the gfTaskScheduler; bit 0x8
# of its byte 0xB stops game tasks from running while drawing goes on (the
# pause ProjectPunch uses). Tests only: the harness writes it so a frame can
# be held still for a screenshot, which the harness takes only while
# emulation runs.
TASK_SCHEDULER = 0x805A0068
SCHEDULER_FLAGS = 0xB
SCHEDULER_STOP_TASKS = 0x8


def hold_game_still(emu, still):
    sched = emu.ptr(TASK_SCHEDULER)
    if sched is None:
        raise NavError("no task scheduler")
    flags = emu.u8(sched + SCHEDULER_FLAGS)
    flags = flags | SCHEDULER_STOP_TASKS if still else flags & ~SCHEDULER_STOP_TASKS
    emu.h.call("write_mem", addr=sched + SCHEDULER_FLAGS, hex=f"{flags:02x}")


# Random number state (RSBE01_02 symbols): g_mtRand and a second mtRand
# object at 0x805A0420 ({vtable, seed}), and the C library's rand state.
RNG_SEEDS = [(0x805A00B8 + 4, "1badb002"), (0x805A0420 + 4, "1badb002"),
             (0x8059FF58, "0000000000000001")]


def seed_match_rng(emu, max_steps=3000, seed=None):
    """Stop emulation on the match's first simulated frame (frame counter
    1) and give every random number generator the same state, so two runs
    whose menus took different numbers of frames play the match the same
    way. `seed`: another value for the two game generators. Tests only.
    Resumes before returning."""
    emu.wait_for("the match scene", lambda: scene(emu)[0] == "scMelee", 60 * 60, every=1)
    emu.h.call("pause")
    w = emu.watch(lambda: game_frame(emu))
    try:
        now = w.read()
        for _ in range(max_steps):
            if now == 1:
                for addr, value in RNG_SEEDS:
                    if seed is not None and len(value) == 8:
                        value = f"{seed:08x}"
                    emu.h.call("write_mem", addr=addr, hex=value)
                return
            now = emu.step(w)
        raise NavError(f"the match's first frame was not reached (counter {now})")
    finally:
        emu.h.call("resume")


def screenshot_at_game_frame(emu, name, target, settle_s=0.5):
    """Screenshot of the match as it is at game frame `target`: emulation is
    stopped exactly there, the game's tasks are held still, emulation
    resumes for the capture (the frame counter itself keeps counting), and
    a second capture `name`-check shows whether the picture really held.
    Returns ((path, width, height), (check path, width, height))."""
    pause_at_game_frame(emu, target)
    hold_game_still(emu, True)
    emu.h.call("resume")
    import time
    time.sleep(settle_s)
    shot = emu.screenshot(name)
    time.sleep(settle_s)
    check = emu.screenshot(name + "-check")
    hold_game_still(emu, False)
    return shot, check


# ---- Versus (sqVsMelee, where the plugin boots with X or Y held) ------------

def boot_to_versus_css(emu, players, timeout_frames=60 * 120):
    """Power-on to the Versus character select with `players` human players,
    ports 1..n, each joined with A and holding its token. Ports past 1 need
    a controller (emu.FOUR_PADS_ARGS)."""
    emu.hold(0, buttons=["X"])
    emu.wait_for("the character select screen after boot",
                 lambda: scene(emu)[0] == "scSelctCharacter", timeout_frames, every=5)
    emu.neutral(0)
    if scene(emu)[1] != "sqVsMelee":
        raise NavError(f"booted to {scene(emu)[:2]}, not Versus")
    emu.advance(30)
    for port in range(players):
        emu.press(port, ["A"])
        emu.wait_for(f"player {port + 1} to join", lambda: css_area(emu, port)["kind"] == 1, 120, every=2)
    emu.advance(10)


def _own_hand_xy(emu, port):
    a = css_area(emu, port)
    if a is None:
        raise NavError(f"left the character select ({scene(emu)[:2]})")
    return a["x"], a["y"]


def drop_own_token(emu, port, character):
    """Carry the token in player `port`'s hand onto `character` and drop it."""
    tx, ty = icon_pos(emu, character)

    def over():
        a = css_area(emu, port)
        return a["target"] == HAND_GRID_HOLDING and a["character"] == CSS_ID[character]
    steer(emu, f"P{port + 1}'s token onto {character}", lambda: _own_hand_xy(emu, port), tx, ty,
          over, port=port)
    emu.press(port, ["A"])
    emu.wait_for(f"P{port + 1}'s token to drop", lambda: not css_area(emu, port)["held"], 30)
    if css_area(emu, port)["character"] != CSS_ID[character]:
        raise NavError(f"P{port + 1}'s token landed on {css_area(emu, port)['character']:#x}")


def enter_versus_match(emu, characters, stage="final_destination"):
    """Power-on to a running Versus match, one human player per entry of
    `characters` (ports 1..n)."""
    boot_to_versus_css(emu, len(characters))
    for port, character in enumerate(characters):
        drop_own_token(emu, port, character)
    press_until(emu, "the stage select", ["START"], lambda: scene(emu)[0] == "scSelStage", 600)
    pick_stage(emu, stage)
    wait_match_start(emu)
    setup = melee_setup(emu)
    if setup["mode"] != MODE_VERSUS:
        raise NavError(f"match setup is mode {setup['mode']:#x}, not Versus")
    return setup


def finish_versus_by_falling(emu, players, timeout_frames=60 * 240):
    """Ports 2..n run off the stage until they are out of stocks; the match
    ends with P1 winning. Then from the results screen back to the
    character select with A on every port. Returns the scenes seen on the way."""
    for port in range(1, players):
        emu.hold(port, stick=(28, 128))
    seen = []
    start = emu.polls()
    while scene(emu)[0] != "scSelctCharacter":
        name = scene(emu)[0]
        if not seen or seen[-1] != name:
            seen.append(name)
        if emu.polls() - start > timeout_frames:
            raise Timeout(f"the match to end and the character select to return (scenes {seen})")
        if name not in ("scMelee", None):
            # The results screen waits for every player to press A.
            for port in range(players):
                emu.press(port, ["A"], frames=3, after=5)
            emu.advance(20)
        else:
            emu.advance(10)
    for port in range(1, players):
        emu.neutral(port)
    return seen + ["scSelctCharacter"]


def training_paused(emu):
    """True while Training's pause menu is open (StOperatorInfoTraining+0x128)."""
    name, _, obj = scene(emu)
    if name != "scMelee":
        return False
    info = emu.ptr(obj + MELEE_INFO)
    return bool(info) and emu.u8(info + INFO_TRAINING_MENU_OPEN) != 0


# In a drill Start opens the drill's own menu
# and that menu's last row, Exit, leaves the match for Training's character
# select; Training's pause cannot be reached. A drill runs from its
# "drills: active" log line to its "drills: <name> ends:" line.
DRILL_ACTIVE = re.compile(r"\] drills: active ")
DRILL_ENDS = re.compile(r"\] drills: .* ends: ")
DRILL_MENU_ROWS = 18  # more than any drill page has


def drill_running(emu):
    text = emu.log_text()
    active = [m.end() for m in DRILL_ACTIVE.finditer(text)]
    ends = [m.end() for m in DRILL_ENDS.finditer(text)]
    return bool(active) and (not ends or active[-1] > ends[-1])


def drill_menu_exit(emu, timeout_frames=60 * 30):
    """In a drill: Start (the drill's menu opens on its page), down to the
    last row, Exit, A: the match ends to Training's character select, with
    Training's pause never open on the way (polled every frame) and the
    drill's "ends:" line logged."""
    pos = len(emu.log_text())
    press_until(emu, "the drill's menu", ["START"],
                lambda: "lab: menu opened by port 1 (Start)" in emu.log_text()[pos:], 300, every=30)
    emu.advance(4)
    for _ in range(DRILL_MENU_ROWS):
        emu.press(0, stick=(128, 28), frames=2, after=4)
    emu.press(0, ["A"], after=0)
    paused = []

    def at_css():
        if training_paused(emu):
            paused.append(True)
        return scene(emu)[0] == "scSelctCharacter"
    emu.wait_for("Training's character select after the drill menu's Exit", at_css, timeout_frames, every=1)
    text = emu.log_text()[pos:]
    if paused or "match: paused" in text:
        raise NavError("Training's pause was open on the way out of the drill's Exit")
    if "lab: exit: Training's end of match started, no pause" not in text:
        raise NavError("the drill menu's last row did not end the match through the lab's Exit")
    if not DRILL_ENDS.search(text):
        raise NavError("no drill end line after the drill menu's Exit")


# The lab's line when Start opens its menu (src/main/lab.cpp openMenu).
LAB_OPENED_START = "lab: menu opened by port 1 (Start)"


def lab_menu_to_training(emu):
    """With the lab menu open (opened with Start): Z, once, opens Training's
    pause in its place. One press only: Z in Training's pause would open the
    lab menu over it."""
    emu.press(0, ["Z"])
    emu.wait_for("Training's pause from the lab menu's Z", lambda: training_paused(emu), 60, every=2)


def pause_training(emu):
    """Training's pause menu, whichever way Lab Menu Button is set: with
    Z in Pause Start opens it; with Start (the default) Start opens the lab
    menu and Z in that opens Training's pause."""
    if training_paused(emu):
        return
    if drill_running(emu):
        raise NavError("Training's pause cannot be opened in a drill (its menu's Exit leaves the match)")
    pos = len(emu.log_text())
    press_until(emu, "Training's pause menu or the lab menu", ["START"],
                lambda: training_paused(emu) or LAB_OPENED_START in emu.log_text()[pos:], 300, every=30)
    if not training_paused(emu):
        lab_menu_to_training(emu)


def resume_training(emu):
    """Start closes Training's pause; a press more would open the lab menu
    (Lab Menu Button = Start), so none may have."""
    if not training_paused(emu):
        return
    pos = len(emu.log_text())
    press_until(emu, "Training to resume", ["START"], lambda: not training_paused(emu), 300, every=30)
    if "lab: menu opened" in emu.log_text()[pos:]:
        raise NavError("resuming Training also opened the lab menu")


# Training's pause menu: IfMngr (pointer at IF_MANAGER) +0x78 -> the menu,
# whose byte +0x17 is the cursor: 0 Speed, 1 Items, 2 No. of CPUs, 3 CPU,
# 4 CPU Damage, 5 Camera, 6 Help, 7 Finish. Up/Down wrap; the cursor keeps
# its place between pauses.
IF_MANAGER = 0x805A02D0
IF_TRAINING_MENU = 0x78
MENU_CURSOR = 0x17
MENU_ITEMS_ROW, MENU_CPUS, MENU_FINISH, MENU_ITEMS = 1, 2, 7, 8


# The centre HUD (IfMngr +0x44), which holds Brawl's match timer and KO
# counter (src/main/ui/match_hud.cpp): IfMngr +0x4 is the HUD's kind (5 in
# Training); the centre's byte 0 has the timer's bit 0x80, +0x17C the time
# last set; +0x154 and +0x160 the timer's and counter's models, each in the
# HUD's scene group (+0x8) while its +4 (the parent) is non-zero. A
# group (nw4r ScnGroup) holds its members' array at +0xDC and their number
# at +0xE4.
CENTRE_HUD_TIMER_ON = 0x80
CENTRE_HUD_MATCH_TRAINING = 5
MATCH_HUD_COUNTER = re.compile(r"match hud: counter (\d+)\s*$", re.M)


def centre_hud(emu):
    mngr = emu.u32(IF_MANAGER)
    out = {"kind": emu.u32(mngr + 4) if emu.is_ram(mngr) else None}
    c = emu.u32(mngr + 0x44) if emu.is_ram(mngr) else 0
    out["centre"] = c
    if not emu.is_ram(c):
        return out
    out["flags"] = emu.u8(c)
    t = emu.u32(c + 0x17C)
    out["time"] = t - (1 << 32) if t & 0x80000000 else t
    tm = emu.u32(c + 0x154)
    out["timer_mdl"] = tm
    out["timer_in"] = emu.is_ram(tm) and emu.u32(tm + 4) != 0
    cm = emu.u32(c + 0x160)
    out["counter_mdl"] = cm
    out["counter_in"] = emu.is_ram(cm) and emu.u32(cm + 4) != 0
    group = emu.u32(c + 0x8)
    members = []
    if emu.is_ram(group):
        arr, n = emu.u32(group + 0xDC), min(emu.u32(group + 0xE4), 64)
        members = list(struct.unpack(f">{n}I", emu.read(arr, 4 * n))) if n and emu.is_ram(arr) else []
    out["group"] = group
    out["members"] = members
    # every member's parent is the group (nothing left that was freed)
    out["members_ok"] = all(emu.is_ram(m) and emu.u32(m + 4) == group for m in members)
    out["timer"] = bool(out["flags"] & CENTRE_HUD_TIMER_ON) and out["timer_in"]
    out["counter"] = out["counter_in"]
    return out


def centre_hud_text(h):
    if "flags" not in h:
        return f"kind {h['kind']}, no centre"
    return (f"kind {h['kind']}, centre {h['centre']:#x}, flags {h['flags']:#04x}, time {h['time']}, timer model "
            f"{h['timer_mdl']:#x} in group {h['timer_in']}, counter model {h['counter_mdl']:#x} in group "
            f"{h['counter_in']}; HUD group {h['group']:#x}: {len(h['members'])} members, "
            f"timer {h['members'].count(h['timer_mdl'])}x, counter {h['members'].count(h['counter_mdl'])}x, "
            f"parents {'all the group' if h['members_ok'] else 'NOT all the group'}")


def menu_cursor(emu):
    ifm = emu.ptr(IF_MANAGER)
    menu = emu.ptr(ifm + IF_TRAINING_MENU) if ifm else None
    return emu.u8(menu + MENU_CURSOR) if menu else None


def menu_select(emu, index):
    """Move the open pause menu's cursor to item `index`."""
    for _ in range(MENU_ITEMS):
        now = menu_cursor(emu)
        if now == index:
            return
        if now is None:
            raise NavError("Training's pause menu is not readable")
        up = (now - index) % MENU_ITEMS <= MENU_ITEMS // 2
        emu.press(0, stick=(128, 228 if up else 28), frames=3, after=8)
    raise NavError(f"the pause menu cursor did not reach item {index} (at {menu_cursor(emu)})")


def cpu_count(emu):
    setup = melee_setup(emu)
    return sum(1 for p in setup["players"][:4] if p["state"] == 1)


def set_training_cpus(emu, count):
    """Set No. of CPUs (1-3; the extra CPUs copy the first) and resume;
    returns once the match setup lists `count` CPUs."""
    if count == cpu_count(emu):
        return
    pause_training(emu)
    menu_select(emu, MENU_CPUS)
    # Right steps the count 0 -> 1 -> 2 -> 3 -> 0.
    for _ in range((count - cpu_count(emu)) % 4):
        emu.press(0, stick=(228, 128), frames=3, after=8)
    resume_training(emu)
    emu.wait_for(f"{count} CPUs in the match", lambda: cpu_count(emu) == count, 600, every=5)


def spawn_items(emu, kinds=3, each=2):
    """From a running Training match: spawn `each` of the first `kinds`
    entries of the pause menu's Items list (A on the Items row spawns the
    shown item; Right shows the next), then resume."""
    pause_training(emu)
    menu_select(emu, MENU_ITEMS_ROW)
    for k in range(kinds):
        for _ in range(each):
            emu.press(0, ["A"], frames=3, after=10)
        emu.press(0, stick=(228, 128), frames=3, after=8)
    resume_training(emu)


def roam(emu, frames=240):
    """P1 runs right and left pressing A, picking up and using items."""
    for i in range(frames // 30):
        stick = (228, 128) if (i // 2) % 2 == 0 else (28, 128)
        emu.press(0, ["A"], frames=2, after=0, stick=stick)
        emu.hold(0, stick=stick)
        emu.advance(28)
    emu.neutral(0)


def finish_training(emu, timeout_frames=60 * 30):
    """Leave a Training match for Training's character select through the
    pause menu's Finish item; in a drill, through its menu's Exit."""
    if not training_paused(emu) and drill_running(emu):
        drill_menu_exit(emu, timeout_frames)
        return
    if not training_paused(emu):
        pause_training(emu)
    menu_select(emu, MENU_FINISH)
    emu.press(0, ["A"])
    emu.wait_for("Training's character select after Finish",
                 lambda: scene(emu)[0] == "scSelctCharacter", timeout_frames, every=5)
