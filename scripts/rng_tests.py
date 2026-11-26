"""Emulator test of the lab menu's Character RNG page
(src/extra/character_rng.cpp), run through emu.py; settings come from the
environment the same way.

  test-rng REL [--only NAME ...] [--keep-going] [--dump PREFIX]

One boot: a Training match of Peach (P1) against a Peach CPU on Final
Destination, the CPU standing still under the lab's CPU control. Both are
driven through the plugin's script source, so every input lands on its game
frame, and both do every move at the same time; what came out is read from
the game's memory for each of them.

Forward smash (smash-default, smash-golf, smash-pan, smash-racket,
smash-after): smashes in a row with no reload in between, one every 56
frames. The weapon of a smash is read twice: from the hitboxes the game
placed (damage and angle: the golf club's are 13, 15 and 17 at 361, the
frying pan's 18 at 85, the tennis racket's 13 at 29) and from the three
flags Project+'s script keeps (which one is set while the smash is out).
With Default, six smashes must be club, pan, racket, club, pan, racket:
Project+'s fixed order. With each other value, 20 smashes must all be the
chosen weapon, whatever the smash before the first one was. smash-after:
back on Default after 20 tennis rackets, the order goes on from there
(club, pan, racket).

Turnip (turnip-default, then turnip-smile, turnip-t-eyes, turnip-line-eyes,
turnip-circle-eyes, turnip-arched-eyes, turnip-winky, turnip-dot,
turnip-stitch, turnip-saturn): 20 pulls, each from the same saved frame
(an emulator state) with another random seed written after the load (such
a load also puts the lab's settings back, so the row's value is written
again as well). After a pull the item in hand is read (its kind: 0x56 the
turnip, 0x13 Mr. Saturn) and, for a turnip, its face three ways: the
turnip's instance int 0, the frame its "Face" texture animation stands on,
and its instance int 1 (the damage it does when thrown: 2 for faces 0 to
4, then 6, 12, 22). With Default the 40 pulls must show at least three
faces, the three readings must agree on every turnip, and Peach's one-in-N
parameter must read 80. With a turnip value every pull must give a turnip
with that face in all three readings (the parameter reads -1); the first
pull is also read after each of its first six frames: the turnip is made
in the first, and must have the chosen face from frame FROM_FRAME (1) on. With
Mr. Saturn every pull must give one (the parameter reads 1).

turnip-again: Stitch Face with no reload: pull, throw, pull, three times.
Every pull must be a stitch face, and the hitbox of every thrown turnip
must carry the stitch face's damage.

state-turnip, state-smash: the lab's own savestate (through its test
block) saved on the idle frame with a face or weapon chosen, loaded four
times (another seed after each): every pull or smash must give the choice.
On Default the same loads must show at least two faces. The row changed
between the save and the load: the row's value at the load counts, as for
every lab setting (a load keeps the lab's settings).

record-turnip, record-smash: Save Positions, HMN Record of a pull (Stitch
Face) or a forward smash (Golf Club) into slot 1, then two playbacks with
P1 neutral: with the same value, then with another (Dot Eyes, Tennis
Racket); the played-back move must give the row's value at playback, and
the playback must not be taken over or end before it.

menu: the Stage & Cast page through the lab menu itself (three downs below
Peach Float must not move the cursor: Luigi Misfire and G&W Judge are hidden
when inactive without a Luigi or a Game & Watch): open the lab menu, down to
Stage & Cast, A, down past Fighter Swap and Show Inactive to Peach Turnip
(visible row 2 on FD), set Stitch Face Turnip and Peach F-Smash to Frying Pan
with the stick; then a pull and a smash must give them. Screenshot
`rng-menu.png`. Last, the match is left: the log must show the parameter was
found and checked ("character rng: Peach's item odds ... ok") and no problem
line.

Then a second boot: Game & Watch (P1) against a Game & Watch CPU, both
doing side specials (the Judge) every JUDGE_FRAMES frames. The number of
a Judge is read from the game: the status int 0 the start sets (motion
464 + number - 1; 1, 5 and 9 matched to the sign on screen when this was
built). judge-default: 12 Judges each; Project+ never repeats either of
the two numbers before, and at least four numbers must show. judge-1 to
judge-9: the row on that number, 4 Judges each, all that number. Last,
the row back on Default and Training left.

Then a third boot: Luigi (P1) against a Luigi CPU, each doing a side
special away from the other. Project+ decides a misfire from a count in
Luigi's instance int 91 (src/extra/character_rng.cpp), which his Wait
draws again every frame: so every side special starts from the same saved
frame, and the count is written on its third frame (in the start, status
0x113), 0 to 7 and again (16 side specials by each Luigi). A misfire is
read two ways, which must agree: the status flag the launch (status 0x118)
sets for one (status flag 18) and its subaction (0x1D3, else 0x1D2); the
launch must place a hitbox. misfire-default: the row on Default; a misfire
exactly when the count was 7, the count as written at the launch, and 1
added by every side special that did not misfire. misfire-always, misfire-never: every side special misfires, or
none does. Neither Luigi may be in a damage status during any of them.
The counts and the misfire damage at the match's start are printed.

--dump PREFIX writes PREFIX.mem1 and PREFIX.mem2 (scripts/memdump.py) with
both Peaches standing, before the scenarios: for reading the game's code.
"""
import argparse
import os
import re
import struct
import time

import display_options_tests as DO
import emu as E
import lab_tests as LT
import loader_tests as L
import memdump
import nav
import plugin_input as PI
import record_tests as RT
from harness import HarnessError, Timeout
from plugin_input import B_ATTACK, B_SMASH, B_SPECIAL, frames_to_steps

# main::Settings (src/main/settings.h): characterRng.
S_TURNIP, S_SMASH, S_HAMMER = 292, 293, 294
TURNIP_VALUES = ["Default", "Smile Turnip", "T Eyes Turnip", "Line Eyes Turnip", "Circle Eyes Turnip",
                 "Arch Eyes Turnip", "Winky Turnip", "Dot Eyes Turnip", "Stitch Face Turnip", "Mr. Saturn"]
SMASH_VALUES = ["Default", "Golf Club", "Frying Pan", "Tennis Racket"]
# Visible row of Peach Turnip on Stage & Cast on Final Destination (rows 0..1
# are Fighter Swap and Show Inactive; Stage Options rows 2..5 are hidden on FD).
STAGE_CAST_PEACH_TURNIP = 2

# The game (src/extra/character_rng.cpp says where each was read).
RNG_SEED = 0x805A00BC
ST_WAIT, ST_SMASH_START, ST_SMASH_HOLD, ST_SMASH, ST_PULL = 0x0, 0x2A, 0x2B, 0x2C, 0x115
MOD_MOTION, MOD_ATTACK, MOD_WORK, MOD_STATUS, MOD_ITEM = 0x8, 0x1C, 0x64, 0x70, 0xA8
STATUS_KIND = 0x34
ATTACK_RENDER_DEBUG, SLOT_RENDER_DEBUG = 0x8074BDB0, 0x120  # soCollisionAttackModuleImpl::renderDebug vtable check
WORK_INSTANCE, WORK_STATUS, WORK_BITS = 0x20, 0x24, 0x1C
HAVE_ITEMS, HAVE_KIND = 0x38, 0xC
ATTACK_PARTS, ATTACK_GROUPS, ARRAY_ITEMS, PART_SIZE, PART_DAMAGE, PART_ANGLE = 0x30, 0x70, 0xC, 0x90, 0x4, 0x18
OTHER_ANIMS, ANIM_FRAME, ANIM_KIND, ANIM_FACE = 0x90, 0x14, 0x28, 7
PEACH_DATA, DATA_ODDS = 0x1870C, 0x84
IT_TURNIP, IT_SATURN = 0x56, 0x13
ODDS_FILE, ODDS_NEVER, ODDS_ALWAYS = 80, -1, 1
# Weapon by its hitboxes (damage, angle) and by the script's flag word
# (flags 104, 105, 106 are bits 8, 9, 10 of the fourth flag word).
WEAPONS = {frozenset({(13, 361), (15, 361), (17, 361)}): 1, frozenset({(18, 85)}): 2, frozenset({(13, 29)}): 3}
FLAG_BITS = {0x100: 1, 0x200: 2, 0x400: 3}
FACE = {v: v - 1 for v in range(1, 9)}  # turnip value -> face
POWER = {0: 2, 1: 2, 2: 2, 3: 2, 4: 2, 5: 6, 6: 12, 7: 22}  # face -> thrown damage
# The savestate feature's test block (src/main/savestate/trace.h): +5 asks
# for a save (1) or load (2) at the next top of the main loop.
SS_BLOCK = re.compile(r"savestate: test block at ([0-9a-f]{8})")
SS_SAVED = re.compile(r"savestate: saved frame (\d+)")
SS_LOADED = re.compile(r"savestate: loaded frame (\d+)")
SS_REFUSED = re.compile(r"savestate: (save|load) refused: ([^\r\n]+)")
LOADS = 4
# The random seed at the match's first frame. During the countdown the game
# preloads an assist trophy (and Poke Ball Pokemon) chosen at random; with
# the seed nav.py uses by default, this match (Peach against Peach on Final
# Destination) gets Mr. Resetti, whose two files the game loads into
# FighterTechqniq (the log's file reads), the heap the Recording page's
# storage needs, so recording is refused for the whole match. With this
# seed it gets Shadow, and the storage is there.
MATCH_SEED = 0x2BADB002
SMASH_FRAMES, PULL_FRAMES, PULLS, SMASHES = 56, 32, 20, 20
ODDS_LINE = re.compile(r"character rng: Peach's item odds at ([0-9a-f]{8}) \(1 in (-?\d+)\), table at [0-9a-f]{8} "
                       r"\((\d+)\): (ok|not as expected)")


class RngError(RuntimeError):
    pass


ST_JUDGE, JUDGE_MOTION, JUDGE_FRAMES, JUDGES = 0x113, 464, 70, 4


def judges(t, count):
    """`count` Judges in a row by both Game & Watches: the number of each."""
    e, r = t.emu, t.r
    who = [r.m, r.c]
    mods = [r.f.modules(f) for f in who]
    status = [u32(m, MOD_STATUS) + STATUS_KIND for m in mods]

    def ra0_and_la68(m):
        wm = u32(m, MOD_WORK)
        gs = e.ptr(wm + WORK_STATUS)
        gi = e.ptr(wm + WORK_INSTANCE)
        ra0_val = s32(e.read(e.u32(gs + 0xC), 4), 0)
        la68_val = s32(e.read(e.u32(gi + 0xC) + 68 * 4, 4), 0)
        return ra0_val, la68_val
    frames = {}
    for n in range(count):
        frames.update(press(1 + n * JUDGE_FRAMES, B_SPECIAL, 1.0, 0.0))
    rows = t.both(frames_to_steps(frames, count * JUDGE_FRAMES),
                  read=lambda i: [(e.u32(a), *ra0_and_la68(m)) for a, m in zip(status, mods)])
    out = []
    for k in range(2):
        got = []
        for n in range(count):
            judge_frames = [row[k] for row in rows[n * JUDGE_FRAMES:(n + 1) * JUDGE_FRAMES]
                            if row[k][0] == ST_JUDGE]
            seen = {v for _, v, _ in judge_frames}
            if len(seen) != 1 or not 0 <= next(iter(seen)) - JUDGE_MOTION < 9:
                raise RngError(f"G&W {k} Judge {n + 1}: status int 0 read {sorted(seen)} during the Judge")
            num = next(iter(seen)) - JUDGE_MOTION
            vis = {b for _, _, b in judge_frames}
            if vis != {num}:
                raise RngError(
                    f"G&W {k} Judge {n + 1}: LA-Basic[68] (sign digit) read {sorted(vis)} != {num} during status 0x113"
                )
            got.append(num + 1)
        out.append(got)
    return out


def judge_part(t, only):
    """The second boot: Game & Watch against Game & Watch."""
    e, r = t.emu, t.r
    t.step = "the Game & Watch match"
    nav.boot_to_training_css(e)
    nav.start_training_match(e, "game_and_watch", "game_and_watch", seed_rng=MATCH_SEED)
    nav.pause_at_game_frame(e, DO.BASE_FRAME)
    r.lab.find_settings()
    r.m, r.c = r.f.by_player(0), r.f.by_player(1)
    r.inp = PI.PluginInput(e)
    for offset, value in DO.CPU_STILL.items():
        r.set(offset, value)
    e.h.call("frame_advance", n=30)
    if not only or "judge-default" in only:
        t.step = "judge-default"
        r.set(S_HAMMER, 0)
        e.h.call("frame_advance", n=2)
        for name, got in zip(("HMN", "CPU"), judges(t, 12)):
            repeats = [i for i in range(2, len(got)) if got[i] in got[i - 2:i]]
            t.check(not repeats and len(set(got)) >= 4, f"judge-default {name}",
                    f"Default, 12 Judges: {got} (a number that repeats one of the two before: at {repeats})")
    for value in range(1, 10):
        name = f"judge-{value}"
        if only and name not in only:
            continue
        t.step = name
        r.set(S_HAMMER, value)
        e.h.call("frame_advance", n=2)
        for who, got in zip(("HMN", "CPU"), judges(t, JUDGES)):
            t.check(got == [value] * JUDGES, f"{name} {who}", f"{JUDGES} Judges: {got}")
    r.set(S_HAMMER, 0)
    e.h.call("frame_advance", n=2)
    r.inp.stop()
    e.h.call("resume")
    nav.finish_training(e)


# Luigi (FitLuigi.pac, Project+ 3.2.1): the misfire count (instance int 91),
# the misfire's damage (int 92), the side special's start and launch
# statuses, the launch's misfire flag (status flag 18) and its two
# subactions. Luigi's Wait subaction draws the count again every frame
# (FitLuigi.pac 0x8958), so it is written once the side special has started.
S_MISFIRE = 295
MISFIRE_VALUES = ["Default", "Always Misfire", "Never Misfire"]
MISFIRE_SCENARIOS = ["misfire-default", "misfire-always", "misfire-never"]
WORK_INTS = 0xC
MOTION_KIND = 0x58  # the motion module's subaction (scripts/cpu_tests.py reads it the same way)
LA_COUNT, LA_DAMAGE, RA_MISFIRE = 91, 92, 18
ST_SIDE_START, ST_SIDE_LAUNCH = 0x113, 0x118
SUB_NORMAL, SUB_MISFIRE = 0x1D2, 0x1D3
ST_DAMAGE = range(0x43, 0x4A)  # Damage to Damage_Fall (ft/fighter.h)
MISFIRE_FRAMES, WRITE_AT = 40, 3
MISFIRE_COUNTS = list(range(8)) * 2


class Luigi:
    """One Luigi's misfire count and side special, read from the game."""

    def __init__(self, emu, fighters, ft, name):
        self.emu, self.name = emu, name
        self.p = Peach(emu, fighters, ft, name)
        work = u32(self.p.mods(), MOD_WORK)
        self.ints = emu.u32(emu.ptr(work + WORK_INSTANCE) + WORK_INTS)
        self.bits = emu.u32(emu.ptr(work + WORK_STATUS) + WORK_BITS)
        self.motion = u32(self.p.mods(), MOD_MOTION)

    def la(self, n):
        return s32(self.emu.read(self.ints + 4 * n, 4), 0)

    def set_count(self, value):
        self.emu.h.call("write_mem", addr=self.ints + 4 * LA_COUNT, hex=struct.pack(">i", value).hex())

    def row(self):
        """(status, misfire flag, subaction, count, hitbox damages) after a
        frame."""
        flag = bool(self.emu.u32(self.bits) & (1 << RA_MISFIRE))
        return (self.p.status(), flag, self.emu.u32(self.motion + MOTION_KIND), self.la(LA_COUNT),
                tuple(sorted({d for d, _ in self.p.hitboxes()})))


def misfires(t, luigis, idle, value, counts):
    """One side special per count in `counts` by both Luigis, each from the
    saved frame with the row on `value`, the count written on the side
    special's start. Per Luigi, per side special: (count written, count on
    the last start frame, misfired, count on the first launch frame, hitbox
    damages)."""
    e, r = t.emu, t.r
    out = [[], []]
    for c in counts:
        e.h.call("load_state", path=idle)
        e.h.call("pause")
        e.h.call("frame_advance", n=1)
        r.set(S_MISFIRE, value)
        e.h.call("frame_advance", n=1)

        def each(i, c=c):
            if i == WRITE_AT:
                for g in luigis:
                    g.set_count(c)
        # Away from each other: P1 stands on the left, the CPU on the right.
        scripts = {0: frames_to_steps(press(1, B_SPECIAL, -1.0, 0.0), MISFIRE_FRAMES),
                   1: frames_to_steps(press(1, B_SPECIAL, 1.0, 0.0), MISFIRE_FRAMES)}
        rows = r.inp.run(scripts, read=lambda i: [g.row() for g in luigis], each=each)
        r.inp.stop()
        for k, g in enumerate(luigis):
            mine = [row[k] for row in rows]
            statuses = [x[0] for x in mine]
            if any(s in ST_DAMAGE for s in statuses):
                raise RngError(f"{g.name}, count {c}: was hit (statuses {statuses})")
            at = next((i for i, x in enumerate(mine) if x[0] == ST_SIDE_LAUNCH), None)
            if at is None or at < WRITE_AT or any(x[0] != ST_SIDE_START for x in mine[WRITE_AT - 1:at]):
                raise RngError(f"{g.name}, count {c}: not in the side special's start from frame {WRITE_AT} to "
                               f"its launch (status {ST_SIDE_LAUNCH:#x}): statuses {statuses}")
            launch = [x for x in mine[at:] if x[0] == ST_SIDE_LAUNCH]
            flags, subs = {x[1] for x in launch}, {x[2] for x in launch}
            if len(flags) != 1 or subs not in ({SUB_NORMAL}, {SUB_MISFIRE}):
                raise RngError(f"{g.name}, count {c}: misfire flag {flags}, subactions {sorted(subs)} in the launch")
            flag = flags.pop()
            if flag != (subs == {SUB_MISFIRE}):
                raise RngError(f"{g.name}, count {c}: misfire flag {flag} but subaction {sorted(subs)}")
            damages = tuple(sorted({d for x in launch for d in x[4]}))
            if not damages:
                raise RngError(f"{g.name}, count {c}: no hitbox during the launch")
            out[k].append((c, mine[at - 1][3], flag, mine[at][3], damages))
    return out


def misfire_part(t, only):
    """The third boot: Luigi against Luigi."""
    e, r = t.emu, t.r
    t.step = "the Luigi match"
    nav.boot_to_training_css(e)
    nav.start_training_match(e, "luigi", "luigi", seed_rng=MATCH_SEED)
    nav.pause_at_game_frame(e, DO.BASE_FRAME)
    r.lab.find_settings()
    r.m, r.c = r.f.by_player(0), r.f.by_player(1)
    r.inp = PI.PluginInput(e)
    for offset, value in DO.CPU_STILL.items():
        r.set(offset, value)
    luigis = [Luigi(e, r.f, r.m, "HMN"), Luigi(e, r.f, r.c, "CPU")]
    t.ok("misfire start", "at the match's start: " + "; ".join(
        f"{g.name} count {g.la(LA_COUNT)}, misfire damage {g.la(LA_DAMAGE)}" for g in luigis))
    r.set(S_MISFIRE, 0)
    e.h.call("frame_advance", n=30)
    idle = os.path.join(os.path.dirname(t.idle), "rng-luigi.sav")
    e.h.call("save_state", path=idle)
    for value, name in enumerate(MISFIRE_SCENARIOS):
        if only and name not in only:
            continue
        t.step = name
        for g, uses in zip(luigis, misfires(t, luigis, idle, value, MISFIRE_COUNTS)):
            got = [u[2] for u in uses]
            text = ", ".join(f"{u[0]}>{u[1]}{'M' if u[2] else ''}" for u in uses)
            damage = sorted({d for u in uses if u[2] for d in u[4]})
            detail = (f"{len(uses)} side specials (count written > count at the launch, M: misfired): {text}; "
                      f"misfire hitbox damage {damage}")
            if value == 0:
                as_written = all(u[1] == u[0] for u in uses)
                adds = all(u[3] == u[0] + 1 for u in uses if not u[2])
                t.check(got == [c >= 7 for c in MISFIRE_COUNTS] and as_written and adds, f"{name} {g.name}",
                        f"{detail}; count left as written: {as_written}; +1 by each that did not misfire: {adds}")
            else:
                t.check(got == [value == 1] * len(uses), f"{name} {g.name}", f"{MISFIRE_VALUES[value]}, {detail}")
    e.h.call("load_state", path=idle)
    e.h.call("pause")
    r.set(S_MISFIRE, 0)
    e.h.call("frame_advance", n=2)
    r.inp.stop()
    e.h.call("resume")
    nav.finish_training(e)


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def s32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def press(frame, buttons, x=0.0, y=0.0, hold=2):
    return {frame + k: (buttons, x, y) for k in range(hold)}


# The frame of a pull (1: the frame the turnip is made in) from which a
# chosen face must show in all three readings: the plugin sets the weights
# of the turnip script's own draw, so the face is chosen when it is made.
FROM_FRAME = 1
# The plugin's log about the draw (src/extra/character_rng.cpp) and the
# draw's weights in Project+'s file.
DRAW_LINE = re.compile(r"character rng: turnip face draw at ([0-9a-f]{8})( not found)?")
DRAW_BACK = re.compile(r"character rng: turnip face draw at ([0-9a-f]{8}) put back")
FILE_WEIGHTS = [34, 6, 5, 3, 3, 4, 2, 1]


def late_frames(early, face):
    """Frames of the pull (1-based) at or after FROM_FRAME whose readings
    are not the chosen face's."""
    want = (face, float(face), POWER[face])
    return [i + 1 for i, t in enumerate(early) if i + 1 >= FROM_FRAME and t != want]


def pull_is(g, value):
    """Whether one Peach's pull_now() result (kind, turnip readings, the
    one-in-N parameter, the readings frame by frame) is `value`'s."""
    kind, t, odds, early = g
    if value == TURNIP_VALUES.index("Mr. Saturn"):
        return kind == IT_SATURN and odds == ODDS_ALWAYS
    face = FACE[value]
    return (kind == IT_TURNIP and t == (face, float(face), POWER[face]) and odds == ODDS_NEVER and bool(early)
            and early[0] is not None and not late_frames(early, face))


def describe(got):
    """Short text of pull_now() results."""
    out = []
    for kind, t, odds, early in got:
        what = f"face {t[0]}" if t else ("Mr. Saturn" if kind == IT_SATURN else f"kind {kind}")
        if t and early:
            late = late_frames(early, t[0])
            what += f" (made with {early[0][0] if early[0] else None}{f', late {late}' if late else ''})"
        out.append(f"{what}, parameter {odds}")
    return "; ".join(out)


class Peach:
    """One Peach's values, read from the game's memory. The addresses of
    what is read every frame are looked up once: they do not change during
    the match (a loaded state is the same match)."""

    def __init__(self, emu, fighters, ft, name):
        self.emu, self.f, self.ft, self.name = emu, fighters, ft, name
        self._mods = self._status = self._flags = self._parts = None

    def mods(self):
        if self._mods is None:
            self._mods = self.f.modules(self.ft)
        return self._mods

    def status(self):
        if self._status is None:
            self._status = u32(self.mods(), MOD_STATUS) + STATUS_KIND
        return self.emu.u32(self._status)

    def flag_word(self):
        """The word the script's three weapon flags are in."""
        if self._flags is None:
            g = self.emu.ptr(u32(self.mods(), MOD_WORK) + WORK_STATUS)
            self._flags = self.emu.u32(g + WORK_BITS) + 12
        return self.emu.u32(self._flags)

    def parts(self, mods):
        """(address, count) of an attack module's hitbox parts."""
        attack = self.emu.read(u32(mods, MOD_ATTACK), 0x80)
        # The parts are an array object followed in memory by the groups'.
        parts, groups = u32(attack, ATTACK_PARTS), u32(attack, ATTACK_GROUPS)
        count = (groups - parts - ARRAY_ITEMS) // PART_SIZE
        if not 0 < count <= 16:
            raise RngError(f"{self.name}: attack parts at {parts:#x}, groups at {groups:#x}: not laid out as expected")
        return parts + ARRAY_ITEMS, count

    def hitboxes(self, at=None):
        """(damage, angle) of every hitbox placed: Peach's own, or those of
        the parts at `at` (from parts())."""
        if at is None:
            if self._parts is None:
                self._parts = self.parts(self.mods())
            at = self._parts
        raw = self.emu.read(at[0], at[1] * PART_SIZE)
        return [(s32(raw, i * PART_SIZE + PART_DAMAGE), s32(raw, i * PART_SIZE + PART_ANGLE))
                for i in range(at[1]) if u32(raw, i * PART_SIZE)]

    def held(self):
        """(item kind, item address) of the item in hand, or (None, 0)."""
        info = self.emu.u32(u32(self.mods(), MOD_ITEM) + HAVE_ITEMS)
        raw = self.emu.read(info + HAVE_KIND, 8)
        kind, item = s32(raw, 0), u32(raw, 4)
        return (kind, item) if self.emu.is_ram(item) else (None, 0)

    def item_mods(self, item):
        return self.emu.read(self.emu.u32(self.emu.u32(item + 0x60) + 0xD8), 0xD0)

    def item_hitboxes(self, item):
        """(damage, angle) of an item's placed hitboxes; none while its
        attack module is not the kind Project+'s hitbox display draws."""
        mods = self.item_mods(item)
        vtable = self.emu.u32(u32(mods, MOD_ATTACK))
        if not self.emu.is_ram(vtable) or self.emu.u32(vtable + SLOT_RENDER_DEBUG) != ATTACK_RENDER_DEBUG:
            return []
        return self.hitboxes(self.parts(mods))

    def turnip(self, item):
        """(face int, face animation frame, thrown damage int) of a turnip."""
        mods = self.item_mods(item)
        # Items keep their ints in an array object: the values from +0xC.
        g = self.emu.u32(u32(mods, MOD_WORK) + WORK_INSTANCE)
        raw = self.emu.read(self.emu.u32(g + 0xC) + 0xC, 8)
        anim = self.emu.read(self.emu.u32(u32(mods, MOD_MOTION) + OTHER_ANIMS) + 0xC, 0x2C)
        frame = struct.unpack_from(">f", anim, ANIM_FRAME)[0] if u32(anim, ANIM_KIND) == ANIM_FACE else None
        return s32(raw, 0), frame, s32(raw, 4)

    def odds(self):
        data = self.emu.u32(self.ft["fighter"] + PEACH_DATA)
        return struct.unpack(">i", self.emu.read(self.emu.u32(data + DATA_ODDS), 4))[0]


class Tester:
    def __init__(self, emu, keep_going, states):
        self.emu = emu
        self.keep_going = keep_going
        self.r = DO.Runner(emu, [], [])
        self.results, self.failures, self.shots = [], [], []
        self.step = "boot"
        self.idle = os.path.join(states, "rng-idle.sav")
        self.peaches = []

    def ok(self, name, detail):
        self.results.append((name, detail))
        print(f"ok   {name}: {detail}")

    def fail(self, name, detail):
        if not self.keep_going:
            raise RngError(f"{name}: {detail}")
        self.failures.append(f"{name}: {detail}")
        print(f"FAIL {name}: {detail}")

    def check(self, cond, name, detail):
        (self.ok if cond else self.fail)(name, detail)

    def start(self, dump):
        e, r = self.emu, self.r
        nav.boot_to_training_css(e)
        nav.start_training_match(e, "peach", "peach", seed_rng=MATCH_SEED)
        nav.pause_at_game_frame(e, DO.BASE_FRAME)
        # The recording scenarios need the Recording page's storage.
        if not re.search(r"record: storage [0-9a-f]{8}", e.log_text()):
            raise RngError("the recording storage was not allocated at the match start (the game may have "
                           "preloaded an assist trophy into FighterTechqniq: see MATCH_SEED)")
        r.lab.find_settings()
        r.m, r.c = r.f.by_player(0), r.f.by_player(1)
        r.inp = PI.PluginInput(e)
        for offset, value in DO.CPU_STILL.items():
            r.set(offset, value)
        e.h.call("frame_advance", n=30)
        self.peaches = [Peach(e, r.f, r.m, "HMN"), Peach(e, r.f, r.c, "CPU")]
        found = SS_BLOCK.findall(e.log_text())
        if not found:
            raise RngError("no savestate test block line in the log")
        self.ss_block = int(found[-1], 16)
        self.rec = RT.Rec(e)
        self.rec.find()
        if dump:
            memdump.capture(e, dump)
            print(f"written: {dump}.mem1, {dump}.mem2")
        e.h.call("save_state", path=self.idle)

    def load(self, seed, turnip=0, smash=0):
        """The saved frame again, with another random seed and the two rows'
        values (the load put the saved settings back)."""
        e = self.emu
        e.h.call("load_state", path=self.idle)
        e.h.call("pause")
        e.h.call("frame_advance", n=1)
        self.r.set(S_TURNIP, turnip)
        self.r.set(S_SMASH, smash)
        self.reseed(seed)

    def reseed(self, seed):
        """Another random seed, then one game frame (for the lab to apply
        the rows)."""
        self.emu.h.call("write_mem", addr=RNG_SEED, hex=f"{seed:08x}")
        self.emu.h.call("frame_advance", n=1)

    def lab_state(self, what):
        """The lab's own savestate (the D-pad's slot) saves or loads at the
        next top of the main loop: one game frame, or a few if the request
        waits. Returns the frame counter it saved or loaded."""
        e = self.emu
        pos = len(e.log_text())
        e.h.call("write_mem", addr=self.ss_block + 5, hex="01" if what == "save" else "02")
        done = SS_SAVED if what == "save" else SS_LOADED
        for _ in range(5):
            e.h.call("frame_advance", n=1)
            text = e.log_text()[pos:]
            m, refused = done.search(text), SS_REFUSED.search(text)
            if m:
                return int(m.group(1))
            if refused:
                raise RngError(f"lab savestate {what} refused: {refused.group(2)}")
        raise RngError(f"lab savestate {what}: no answer in the log")

    def rec_request(self, name):
        """One request to the Recording page (its test block), carried out
        in the next game frame."""
        before = self.rec.status()["result"]
        self.rec.request(name)
        self.emu.h.call("frame_advance", n=1)
        st = self.rec.status()
        if st["result"] != before + 1:
            raise RngError(f"recording request {name} not carried out ({st})")
        return st

    def both(self, steps, **kw):
        rows = self.r.inp.run({0: steps, 1: steps}, **kw)
        self.r.inp.stop()
        return rows

    def one(self, k, steps, **kw):
        rows = self.r.inp.run({k: steps}, **kw)
        self.r.inp.stop()
        return rows

    # ---- forward smash ----------------------------------------------------------

    def smashes(self, count):
        """`count` smashes in a row by both: per Peach, the weapon of each
        by its hitboxes and by the script's flags."""
        frames = {}
        for n in range(count):
            frames.update(press(1 + n * SMASH_FRAMES, B_ATTACK | B_SMASH, 1.0, 0.0))
        steps = frames_to_steps(frames, count * SMASH_FRAMES)
        rows = self.both(steps, read=lambda i: [self.smash_row(p) for p in self.peaches])
        out = []
        for k, p in enumerate(self.peaches):
            out.append([self.weapon([row[k] for row in rows[n * SMASH_FRAMES:(n + 1) * SMASH_FRAMES]],
                                    f"{p.name} smash {n + 1}") for n in range(count)])
        return out

    @staticmethod
    def smash_row(p):
        return p.status(), p.flag_word(), p.hitboxes()

    @staticmethod
    def weapon(part, what):
        """The weapon of the one smash in `part` (smash_row()s), by its
        hitboxes and by the script's flags, which must agree."""
        out_frames = [x for x in part if x[0] == ST_SMASH]
        boxes = frozenset(b for x in out_frames for b in x[2])
        flags = {x[1] & 0x700 for x in out_frames}
        by_box = WEAPONS.get(boxes)
        by_flag = FLAG_BITS.get(next(iter(flags))) if len(flags) == 1 else None
        if by_box is None or by_box != by_flag:
            raise RngError(f"{what}: hitboxes {sorted(boxes)} and flags {sorted(hex(f) for f in flags)} do not "
                           f"name one weapon ({len(out_frames)} frames of the smash seen)")
        return by_box

    def sc_smash(self, name, value, count, expect, what):
        self.r.set(S_SMASH, value)
        self.emu.h.call("frame_advance", n=2)
        for p, weapons in zip(self.peaches, self.smashes(count)):
            names = [SMASH_VALUES[w] for w in weapons]
            self.check(weapons == expect, f"{name} {p.name}",
                       f"{what}: {', '.join(names) if len(set(names)) > 1 else f'{len(names)} x {names[0]}'}")

    # ---- turnip -----------------------------------------------------------------

    def pull(self, seed, value, watch=False):
        """One pull by both from the saved frame: per Peach (kind, turnip
        readings or None, the parameter). With `watch`, a fourth value: the
        turnip's readings after each of the pull's first six frames."""
        self.load(seed, turnip=value)
        return self.pull_now(watch)

    def pull_now(self, watch=False):
        """pull() from where the game is."""
        seen = [[] for _ in self.peaches]

        def read(i):
            for k, p in enumerate(self.peaches):
                kind, item = p.held()
                seen[k].append(p.turnip(item) if kind == IT_TURNIP else None)
        self.both(frames_to_steps(press(1, B_SPECIAL, 0.0, -1.0), PULL_FRAMES),
                  read=read if watch else None, until=(lambda i, rows: i >= 6) if watch else None)
        if watch:
            self.both(frames_to_steps({}, PULL_FRAMES - 6))
        out = []
        for k, p in enumerate(self.peaches):
            kind, item = p.held()
            out.append((kind, p.turnip(item) if kind == IT_TURNIP else None, p.odds(), seen[k]))
        return out

    def sc_turnip(self, name, value):
        pulls = [self.pull(0x2000 + 77 * n, value, watch=n == 0) for n in range(PULLS)]
        for k, p in enumerate(self.peaches):
            got = [row[k] for row in pulls]
            kinds = [g[0] for g in got]
            odds = sorted({g[2] for g in got})
            turnips = [g[1] for g in got if g[1] is not None]
            # The three readings of a turnip must agree with each other.
            mixed = [t for t in turnips if t[1] != float(t[0]) or POWER.get(t[0]) != t[2]]
            faces = [t[0] for t in turnips]
            if mixed:
                self.fail(f"{name} {p.name}", f"face int, face animation frame and thrown damage disagree: {mixed[:4]}")
            elif value == 0:
                self.check(all(k in (IT_TURNIP, IT_SATURN) for k in kinds) and len(set(faces)) >= 3
                           and odds == [ODDS_FILE], f"{name} {p.name}",
                           f"{len(got)} pulls: faces {faces}, {kinds.count(IT_SATURN)} Mr. Saturn; one-in-N "
                           f"parameter {odds}")
            elif value == TURNIP_VALUES.index("Mr. Saturn"):
                self.check(kinds == [IT_SATURN] * PULLS and odds == [ODDS_ALWAYS], f"{name} {p.name}",
                           f"{kinds.count(IT_SATURN)} of {len(got)} pulls a Mr. Saturn (kinds "
                           f"{sorted(set(hex(k) if k is not None else 'none' for k in kinds))}); parameter {odds}")
            else:
                face = FACE[value]
                # The first pull, frame by frame: the turnip is made in the
                # pull's first frame, and the chosen face must be in from
                # frame FROM_FRAME on.
                early = got[0][3]
                late = late_frames(early, face)
                self.check(kinds == [IT_TURNIP] * PULLS and faces == [face] * PULLS and odds == [ODDS_NEVER]
                           and early[0] is not None and not late, f"{name} {p.name}",
                           f"{faces.count(face)} of {len(got)} pulls a turnip with face {face} (int, animation frame "
                           f"{face}.0, thrown damage {POWER[face]}); kinds "
                           f"{sorted(set(hex(k) if k is not None else 'none' for k in kinds))}, faces "
                           f"{sorted(set(faces))}; parameter {odds}; first pull: made in frame 1 with face "
                           f"{early[0][0] if early[0] else None}, the chosen one from frame {FROM_FRAME}"
                           f"{'' if not late else f' except frames {late}: {early}'}")

    # ---- savestates and recordings ------------------------------------------------

    def forced_pulls(self, name, pulls, value, what):
        """Checks pull_now(watch=True) results: every pull by both Peaches
        gives `value` (a face, or Mr. Saturn), the face in all three
        readings and from frame FROM_FRAME of the pull."""
        for k, p in enumerate(self.peaches):
            got = [row[k] for row in pulls]
            self.check(all(pull_is(g, value) for g in got), f"{name} {p.name}",
                       f"{what}: {len(got)} pulls, {describe(got)}; want {TURNIP_VALUES[value]}")

    def sc_state_turnip(self):
        """The lab's savestate with a face chosen: saved before a pull,
        loaded LOADS times with another seed each time; then the same loads
        on Default (the faces must vary, or the seeds do not reach the
        draw); then the row changed between the save and the load."""
        stitch, dot = TURNIP_VALUES.index("Stitch Face Turnip"), TURNIP_VALUES.index("Dot Eyes Turnip")
        saturn = TURNIP_VALUES.index("Mr. Saturn")
        seeds = [0x3200 + 97 * n for n in range(LOADS)]
        self.load(0x3100, turnip=stitch)
        saved = self.lab_state("save")
        pulls = []
        for seed in seeds:
            self.lab_state("load")
            self.reseed(seed)
            pulls.append(self.pull_now(watch=True))
        self.forced_pulls("state-turnip", pulls, stitch, f"saved at frame {saved}, {LOADS} loads")
        self.r.set(S_TURNIP, 0)
        faces = set()
        for seed in seeds:
            self.lab_state("load")
            self.reseed(seed)
            faces |= {g[1][0] for g in self.pull_now() if g[1] is not None}
        self.check(len(faces) >= 2, "state-turnip Default", f"the same {LOADS} loads and seeds on Default: faces "
                   f"{sorted(faces)} (at least two)")
        # The row changed after the save: the row's value when the state is
        # loaded is the one that counts, as for every lab setting (a load
        # keeps the lab's settings).
        changes = [(stitch, dot), (stitch, saturn), (saturn, stitch)]
        for n, (at_save, at_load) in enumerate(changes):
            # Saved from the idle frame (both Peaches empty-handed).
            self.load(0x3300 + n, turnip=at_save)
            self.lab_state("save")
            self.r.set(S_TURNIP, at_load)
            self.lab_state("load")
            self.reseed(0x3400 + n)
            self.forced_pulls(f"state-turnip changed {n + 1}", [self.pull_now(watch=True)], at_load,
                              f"saved with {TURNIP_VALUES[at_save]}, loaded with {TURNIP_VALUES[at_load]}")

    def sc_state_smash(self):
        """The lab's savestate with a forward smash weapon chosen: saved,
        loaded LOADS times, a smash by both after each load; then the row
        changed between the save and the load."""
        pan, racket = SMASH_VALUES.index("Frying Pan"), SMASH_VALUES.index("Tennis Racket")
        self.load(0x3500, smash=pan)
        saved = self.lab_state("save")
        got = []
        for n in range(LOADS):
            self.lab_state("load")
            self.reseed(0x3600 + n)
            got.append(self.smashes(1))
        for k, p in enumerate(self.peaches):
            weapons = [w[k][0] for w in got]
            self.check(weapons == [pan] * LOADS, f"state-smash {p.name}",
                       f"saved at frame {saved}, {LOADS} loads: {[SMASH_VALUES[w] for w in weapons]}")
        self.r.set(S_SMASH, racket)
        self.lab_state("load")
        self.reseed(0x3700)
        for p, w in zip(self.peaches, self.smashes(1)):
            self.check(w == [racket], f"state-smash changed {p.name}",
                       f"saved with Frying Pan, loaded with Tennis Racket: {SMASH_VALUES[w[0]]}")

    def record(self, steps, value, row):
        """From the idle frame with `row` set to `value`: Save Positions,
        then HMN Record into slot 1 while P1 plays `steps`; recording off."""
        rec = self.rec
        self.load(0x3800, **{row: value})
        st = self.rec_request("save")
        if not st["flags"] & RT.FLAG_EXACT:
            raise RngError(f"Save Positions did not take a savestate ({st})")
        rec.set(hmnMode=RT.HMN_MODE["Record"], hmnSlot=1, cpuMode=0)
        self.rec_request("hmn-mode")
        # The restore starts the recording (the savestate is loaded at the
        # next top of the main loop); P1's script once it is done.
        self.rec_request("restore")
        self.emu.h.call("frame_advance", n=2)
        self.one(0, steps)
        rec.set(hmnMode=0)
        self.rec_request("hmn-mode")
        end = rec.slot_end(0, 0)
        if end[1] < sum(s[3] for s in steps):
            raise RngError(f"slot 1 holds frames {end[0]} to {end[1]}, shorter than the {sum(s[3] for s in steps)} "
                           f"played")
        return end

    def playback(self, frames, seed, read):
        """HMN Playback of slot 1 from its restore, a new seed written right
        after the restore, `frames` game frames with P1 neutral (no live
        input to take over). Returns (read() after each frame, the test
        block before and after)."""
        e, rec = self.emu, self.rec
        e.neutral(0)
        self.r.inp.stop()
        rec.set(hmnMode=RT.HMN_MODE["Playback"], hmnSlot=1, cpuMode=0, loop=0, autoRestore=0, startPaused=0)
        before = rec.status()
        self.rec_request("hmn-mode")
        rows, self.cancelled = [], []
        for i in range(frames):
            # Before each of the first frames, while the restore may still
            # be on its way (it brings the saved seed back); the pull starts
            # later.
            if i < 3:
                e.h.call("write_mem", addr=RNG_SEED, hex=f"{seed:08x}")
            e.h.call("frame_advance", n=1)
            rows.append(read())
            self.cancelled.append(bool(rec.status()["flags"] & RT.FLAG_HMN_CANCELLED))
        after = rec.status()
        rec.set(hmnMode=0)
        self.rec_request("hmn-mode")
        return rows, before, after

    def played_back(self, before, after, frames):
        """Playback started from a restore, was not taken over, and was not
        over before `frames` frames of the window (it ends with the
        recording, a few frames before the window does)."""
        return (after["restores"] > before["restores"] and after["takeover"][0] == before["takeover"][0]
                and not any(self.cancelled[:frames]))

    def sc_record_turnip(self):
        """A pull recorded in a slot with a face chosen, played back: the
        played-back pull gives the chosen face; played back again after the
        row is changed, it gives the row's new face."""
        stitch, dot = TURNIP_VALUES.index("Stitch Face Turnip"), TURNIP_VALUES.index("Dot Eyes Turnip")
        lead = 5
        frames = lead + PULL_FRAMES
        p = self.peaches[0]
        end = self.record(frames_to_steps(press(lead, B_SPECIAL, 0.0, -1.0), frames), stitch, "turnip")

        def read():
            kind, item = p.held()
            return kind, p.turnip(item) if kind == IT_TURNIP else None
        for n, value in enumerate([stitch, dot]):
            self.r.set(S_TURNIP, value)
            rows, before, after = self.playback(frames + 4, 0x3900 + 31 * n, read)
            held = [t for kind, t in rows if kind == IT_TURNIP]
            kinds = sorted({hex(kind) for kind, _ in rows if kind is not None})
            got = (rows[-1][0], rows[-1][1], ODDS_NEVER, held[:6])
            ran = self.played_back(before, after, frames - 6)
            self.check(ran and pull_is(got, value), f"record-turnip {n + 1}",
                       f"recorded with Stitch Face Turnip (slot frames {end[0]}-{end[1]}), played back with "
                       f"{TURNIP_VALUES[value]}: {describe([got])}; kinds held {kinds}; playback "
                       f"{'ran' if ran else 'not started, over early or taken over'}")

    def sc_record_smash(self):
        """A forward smash recorded with Golf Club chosen, played back with
        Golf Club, then with Tennis Racket."""
        golf, racket = SMASH_VALUES.index("Golf Club"), SMASH_VALUES.index("Tennis Racket")
        lead = 5
        frames = lead + SMASH_FRAMES
        p = self.peaches[0]
        end = self.record(frames_to_steps(press(lead, B_ATTACK | B_SMASH, 1.0, 0.0), frames), golf, "smash")
        for n, value in enumerate([golf, racket]):
            self.r.set(S_SMASH, value)
            rows, before, after = self.playback(frames + 4, 0x3A00 + n, lambda: self.smash_row(p))
            w = self.weapon(rows, f"record-smash {n + 1}")
            ran = self.played_back(before, after, frames - 6)
            self.check(ran and w == value, f"record-smash {n + 1}",
                       f"recorded with Golf Club (slot frames {end[0]}-{end[1]}), played back with "
                       f"{SMASH_VALUES[value]}: {SMASH_VALUES[w]}; playback "
                       f"{'ran' if ran else 'not started, over early or taken over'}")

    def sc_again(self):
        value = TURNIP_VALUES.index("Stitch Face Turnip")
        face = FACE[value]
        for k, p in enumerate(self.peaches):
            # One Peach at a time (the other stands, and may be hit by the
            # throws), each from the saved frame.
            self.load(0x5151 + k, turnip=value)
            rows = []
            for n in range(3):
                self.one(k, frames_to_steps(press(1, B_SPECIAL, 0.0, -1.0), PULL_FRAMES))
                kind, item = p.held()
                t = p.turnip(item) if kind == IT_TURNIP else None
                damage = set()
                if t:
                    # Throw it forward (A with an item in hand) and watch its hitbox.
                    self.one(k, frames_to_steps(press(1, B_ATTACK, 0.0, 0.0), 44),
                             read=lambda i: damage.update(d for d, _ in p.item_hitboxes(item)))
                rows.append((kind, t, damage))
            good = all(kind == IT_TURNIP and t == (face, float(face), POWER[face]) and dmg == {POWER[face]}
                       for kind, t, dmg in rows)
            self.check(good, f"turnip-again {p.name}",
                       f"3 pulls with a throw between, no reload: faces {[t[0] if t else kind for kind, t, _ in rows]}, "
                       f"thrown hitbox damage {[sorted(d) for _, _, d in rows]}")

    # ---- the menu ----------------------------------------------------------------

    def sc_menu(self):
        e, r, lab = self.emu, self.r, self.r.lab
        turnip, smash = TURNIP_VALUES.index("Stitch Face Turnip"), SMASH_VALUES.index("Frying Pan")
        self.load(0x7777)
        e.h.call("resume")
        lab.open_menu()
        # Stage & Cast on Final Destination (Peach vs Peach): visible rows are
        # Fighter Swap (0), Show Inactive (1), Peach Turnip (2), Peach F-Smash (3),
        # Peach Float (4); inactive stage and fighter rows are compacted out.
        lab.main_to(LT.MAIN_STAGE_CAST)
        lab.tap(["A"])
        lab.down(STAGE_CAST_PEACH_TURNIP)
        lab.set_choice(S_TURNIP, turnip)
        lab.down(1)
        lab.set_choice(S_SMASH, smash)
        # One more Peach row (Peach Float) sits directly below Peach F-Smash;
        # every non-Peach row below it is hidden without those fighters.
        lab.down(1)
        pos = lab.log_pos()
        lab.down(3)
        moves = len(re.findall(r"\] ui: sound 0 ", e.log_text()[pos:]))
        self.check(moves == 0, "menu skips", f"3 downs from Peach Float moved the cursor {moves} rows "
                   f"(the inactive non-Peach rows below it hidden)")
        e.advance(10)
        self.shots.append(e.screenshot("rng-menu"))
        lab.close_menu()
        e.advance(30)
        e.h.call("pause")
        weapons = self.smashes(1)
        self.both(frames_to_steps(press(1, B_SPECIAL, 0.0, -1.0), PULL_FRAMES))
        for p, w in zip(self.peaches, weapons):
            kind, item = p.held()
            t = p.turnip(item) if kind == IT_TURNIP else None
            self.check(w == [smash] and t == (FACE[turnip], float(FACE[turnip]), POWER[FACE[turnip]]),
                       f"menu {p.name}",
                       f"set through the lab menu ({TURNIP_VALUES[turnip]}, {SMASH_VALUES[smash]}): smash "
                       f"{SMASH_VALUES[w[0]]}, pull {'face ' + str(t[0]) if t else kind}")


def scenarios(t):
    return [
        ("smash-default", lambda: t.sc_smash("smash-default", 0, 6, [1, 2, 3, 1, 2, 3],
                                             "Default, 6 smashes in a row")),
        ("smash-golf", lambda: t.sc_smash("smash-golf", 1, SMASHES, [1] * SMASHES, "Golf Club")),
        ("smash-pan", lambda: t.sc_smash("smash-pan", 2, SMASHES, [2] * SMASHES, "Frying Pan")),
        ("smash-racket", lambda: t.sc_smash("smash-racket", 3, SMASHES, [3] * SMASHES, "Tennis Racket")),
        ("smash-after", lambda: t.sc_smash("smash-after", 0, 3, [1, 2, 3],
                                           "Default again after the tennis rackets")),
        ("turnip-default", lambda: t.sc_turnip("turnip-default", 0)),
    ] + [(name, (lambda name=name, value=value: t.sc_turnip(name, value))) for name, value in TURNIP_SCENARIOS] + [
        ("turnip-again", t.sc_again),
        ("state-turnip", t.sc_state_turnip),
        ("state-smash", t.sc_state_smash),
        ("record-turnip", t.sc_record_turnip),
        ("record-smash", t.sc_record_smash),
        ("menu", t.sc_menu),
    ]


# A scenario per Peach Turnip value after Default.
TURNIP_SCENARIOS = [("turnip-smile", 1), ("turnip-t-eyes", 2), ("turnip-line-eyes", 3), ("turnip-circle-eyes", 4),
                    ("turnip-arched-eyes", 5), ("turnip-winky", 6), ("turnip-dot", 7), ("turnip-stitch", 8),
                    ("turnip-saturn", 9)]


class JudgeOnly(Exception):
    """--only names Judge scenarios alone: the Peach boot is skipped."""


JUDGE_SCENARIOS = ["judge-default"] + [f"judge-{v}" for v in range(1, 10)]


def cmd_test_rng(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-rng")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--keep-going", action="store_true")
    ap.add_argument("--dump", default=None)
    opts = ap.parse_args(rest)
    label = "test-rng"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, log_path = "", ""
    states = os.path.join(os.path.dirname(os.path.abspath(E.env("SD_IMAGE"))), "states")
    os.makedirs(states, exist_ok=True)
    t = None
    logs = []
    judge_only = bool(opts.only) and all(n in JUDGE_SCENARIOS + MISFIRE_SCENARIOS for n in opts.only)
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            logs.append(log_path)
            t = Tester(emu, opts.keep_going, states)
            try:
                if judge_only:
                    raise JudgeOnly()
                t.step = "the match"
                t.start(opts.dump)
                names = [name for name, _ in scenarios(t)] + JUDGE_SCENARIOS + MISFIRE_SCENARIOS
                unknown = [n for n in opts.only or [] if n not in names]
                if unknown:
                    raise RngError(f"no scenario named {unknown}; there are {names}")
                for name, fn in scenarios(t):
                    if not opts.only or name in opts.only:
                        t.step = name
                        fn()
                t.step = "the end of the match"
                t.r.inp.stop()
                emu.h.call("resume")
                found = ODDS_LINE.findall(emu.log_text())
                t.check(bool(found) and found[-1][1:] == (str(ODDS_FILE), "3", "ok"), "the parameter",
                        f"found and checked by the plugin: {found[-1] if found else 'no line in the log'}")
                nav.finish_training(emu)
                # The turnip's draw is in item files that stay loaded after
                # Training: its weights must be the file's again.
                draws = DRAW_LINE.findall(emu.log_text())
                back = DRAW_BACK.findall(emu.log_text())
                weights = None
                if draws and draws[-1][0] != "00000000":
                    raw = emu.read(int(draws[-1][0], 16), 8 * 9)
                    weights = [s32(raw, 8 * i + 4) // 60000 for i in range(1, 9)]
                t.check(bool(draws) and not draws[-1][1] and bool(back) and weights == FILE_WEIGHTS,
                        "the turnip's draw", f"found by the plugin at {draws[-1][0] if draws else 'no line'}, put back "
                        f"when Training was left ({'logged' if back else 'not logged'}); weights now {weights}")
            except JudgeOnly:
                pass
            except (Timeout, nav.NavError, HarnessError, RngError, DO.DisplayError, LT.LabError, PI.InputError,
                    struct.error, KeyError, StopIteration, TypeError, IndexError, ValueError) as e:
                why = f"{t.step}: {type(e).__name__}: {e}"
                try:
                    emu.h.call("resume")
                    t.shots.append(emu.screenshot("fail-rng"))
                except (OSError, HarnessError, ValueError):
                    pass
        for part, names in ((judge_part, JUDGE_SCENARIOS), (misfire_part, MISFIRE_SCENARIOS)):
            if why or (opts.only and not any(n in names for n in opts.only)):
                continue
            with E.harness_run(backend, E.DETERMINISTIC_ARGS, allow_null=True) as (proc, emu):
                logs.append(emu.log_path)
                t.emu, t.r = emu, DO.Runner(emu, [], [])
                try:
                    part(t, opts.only)
                except (Timeout, nav.NavError, HarnessError, RngError, DO.DisplayError, PI.InputError,
                        struct.error, KeyError, StopIteration, TypeError, IndexError, ValueError) as e:
                    why = f"{t.step}: {type(e).__name__}: {e}"
                    try:
                        emu.h.call("resume")
                        t.shots.append(emu.screenshot("fail-rng"))
                    except (OSError, HarnessError, ValueError):
                        pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{t.step if t else 'boot'}: {e}"
    print()
    for name, detail in (t.results if t else []):
        print(f"ok  {name}: {detail}")
    for line in (t.failures if t else []):
        print(f"FAIL {line}")
    for shot in (t.shots if t else []):
        print(f"screenshot: {shot[0]} ({shot[1]}x{shot[2]})")
    bad = [line for path in logs for line in L.problems(E.read_log(path))]
    for line in bad:
        print(f"problem: {line}")
    if not why and t and t.failures:
        why = f"{len(t.failures)} check(s) failed"
    if not why and bad:
        why = "problem lines in the log"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(logs[-1] if logs else log_path, why, label)
    print(f"{label}: PASS")
    return 0
