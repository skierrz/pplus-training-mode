"""Emulator test of the extra module's heap (run through emu.py; settings
come from the environment the same way).

  test-extra-heaps REL [--stages K,K,K] [--victims A,B,C] [--items N]
      Training matches with the extra module loaded in OverlayFighter4, one
      per stage kind (written into the match setup after the stage select
      takes its first icon), with P1 Kirby against a CPU victim each time.
      In every match: Kirby inhales and swallows the CPU (copies it), an
      Assist Trophy, a Poke Ball and a Smash Ball are spawned from the Items
      row and P1 runs through them, N entries of Training's item list are
      spawned and P1 runs through them pressing A, Kirby presses B and Up+B
      (a final smash if a Smash Ball was broken), and No. of CPUs goes to 3
      and back to 1. After each step the game's heap table is read: the test
      fails if OverlayFighter3 ever holds a block or OverlayFighter4 holds
      anything but the extra module. Build with EXPERIMENTS=1 for the
      module-load log (every REL the game loads, with its heap).
"""
import argparse
import re
import struct
import sys
import time

sys.dont_write_bytecode = True

import cpu_tests as C  # noqa: E402
import emu as E  # noqa: E402
import loader_tests as L  # noqa: E402
import nav  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

KIRBY_HEAPS = ("FighterKirbyResource1", "FighterKirbyResource2", "FighterKirbyResource3")
WATCHED = ("OverlayFighter1", "OverlayFighter2", "OverlayFighter3", "OverlayFighter4", "OverlayStage",
           "Fighter3Instance", "Fighter4Instance", "AssistFigureResource", "PokemonResource",
           "FighterTechqniq") + KIRBY_HEAPS
MODULE_LIST = 0x800030C8
SORA_MELEE = 27
SLOT_MANAGER_BSS = 0x2E90   # g_ftSlotManager in sora_melee's .bss (decomp symbol map)
SLOT_SIZE = 0x4D4           # ftSlot, from the slot manager's constructor (mulli 1236)
MODULE_MANAGER = 0x805A0038  # g_gfModuleManager
MODULE_ENTRY, MODULE_ENTRIES = 0x3C, 16


def watched(table):
    return {name: table.get(name) for name in WATCHED}


def bss_of(emu, module_id):
    m = emu.u32(MODULE_LIST)
    for _ in range(64):
        if not emu.is_ram(m):
            return None
        if emu.u32(m) == module_id:
            secinfo = emu.u32(m + 0x10)
            return emu.u32(secinfo + 8 * 6) & ~1
        m = emu.u32(m + 4)
    return None


def slot_heaps(emu):
    """The fighter slot manager's slots (+0x164 instance heap, +0x168 resource
    heap, +0x16C, +0x170 heap-set key, +0x174 slot number) and its heap sets
    (a ring of 4 records of 24 bytes at +0x14: +4 module heap, +0xC key)."""
    bss = bss_of(emu, SORA_MELEE)
    if bss is None:
        return "sora_melee not found"
    mgr = emu.ptr(bss + SLOT_MANAGER_BSS)
    if mgr is None:
        return "no slot manager"
    arr, count = emu.u32(mgr), emu.u32(mgr + 4)
    slots = []
    for i in range(min(count, 9)):
        raw = emu.read(arr + i * SLOT_SIZE + 0x164, 0x14)
        slots.append(tuple(struct.unpack(">5i", raw)))
    sets = [tuple(struct.unpack(">6i", emu.read(mgr + 0x14 + 24 * i, 24))) for i in range(4)]
    head = emu.read(mgr + 8, 12).hex()
    return {"manager": hex(mgr), "slots": slots, "sets": sets, "ring": head}


def loaded_modules(emu, table):
    """[(name, heap name)] of gfModuleManager's entries in use."""
    mm = emu.ptr(MODULE_MANAGER)
    if mm is None:
        return []
    pools = {}
    import memdump as M
    raw = emu.read(M.HEAP_INFOS, 16 * M.HEAP_COUNT)
    for hid in range(M.HEAP_COUNT):
        name_p, pool, size, arena = struct.unpack_from(">4I", raw, 16 * hid)
        if M.is_ram(pool):
            pools[pool] = emu.cstr(name_p) or f"heap{hid}"
    out = []
    for i in range(MODULE_ENTRIES):
        e = mm + 8 + MODULE_ENTRY * i
        raw = emu.read(e, MODULE_ENTRY)
        if not raw[0x19] & 0x10:  # in use (the flag loadModuleRequest tests)
            continue
        name = raw[0x1A:0x3A].split(b"\0")[0].decode("ascii", "replace")
        heap = struct.unpack_from(">I", raw, 0x10)[0]
        out.append((name, pools.get(heap, hex(heap)), raw[0x18]))
    return out


class Run:
    def __init__(self, emu):
        self.emu = emu
        self.game = C.Game(emu)
        self.steps = []
        self.bad = []
        self.shots = []

    def check(self, label):
        e = self.emu
        table = L.heap_table(e)
        w = watched(table)
        mods = loaded_modules(e, table)
        self.steps.append((label, w, mods))
        of3, of4 = w["OverlayFighter3"], w["OverlayFighter4"]
        if of3 is None or of3[2] != 0:
            self.bad.append(f"{label}: OverlayFighter3 {of3}")
        if of4 is None or of4[2] != 1:
            self.bad.append(f"{label}: OverlayFighter4 {of4} (the extra module is its one block)")
        k, c = self.fighters() if e.ptr(C.ENTRY_MANAGER) else ({}, {})
        print(f"  [{label}] OF3 {of3[2] if of3 else '-'} blocks, OF4 {of4[2] if of4 else '-'} blocks; "
              f"OverlayStage {w['OverlayStage'][3] if w['OverlayStage'] else '-'} B used; "
              f"blocks/used: assist {w['AssistFigureResource'][2:] if w['AssistFigureResource'] else '-'}, "
              f"pokemon {w['PokemonResource'][2:] if w['PokemonResource'] else '-'}, "
              f"techniq {w['FighterTechqniq'][2:] if w['FighterTechqniq'] else '-'}; "
              f"Kirby {[w[n][2] if w[n] else '-' for n in KIRBY_HEAPS]}; damage P1 {k.get('damage')} CPU {c.get('damage')}; "
              f"fighter modules {[(n, h) for n, h, _ in mods if n.startswith('ft_') or n.startswith('st_')]}")
        return w

    def fighters(self):
        g = self.game
        g.mods = {}
        p1, cpu = g.entry(0), g.entry(1)
        return g.fighter(p1), g.fighter(cpu)

    def approach(self, gap=14.0, timeout=360):
        e = self.emu
        for _ in range(timeout // 4):
            k, c = self.fighters()
            dx = c["x"] - k["x"]
            if abs(dx) < gap and not k["air"]:
                e.neutral(0)
                e.advance(6)
                return True
            e.hold(0, stick=(228 if dx > 0 else 28, 128))
            e.advance(4)
        e.neutral(0)
        return False

    def kirby_copy(self, tries=6):
        """Inhale with B (jumping first if the CPU stands higher), swallow
        with Down. Returns the try that hurt the CPU (it takes damage when
        swallowed; nothing else hits it at this point), or 0."""
        e = self.emu
        for t in range(tries):
            self.approach()
            k, c = self.fighters()
            if c["y"] - k["y"] > 8:
                e.press(0, ["X"], frames=3, after=10)
            e.press(0, ["B"], frames=45, after=4)
            e.press(0, stick=(128, 28), frames=4, after=60)
            k, c = self.fighters()
            if c["damage"] > 0:
                return t + 1
            e.advance(60)
        return 0

    def item_field(self):
        """Offset of the byte in Training's pause menu that steps by one with
        each Right on the Items row (the shown item), found by watching it."""
        e = self.emu
        menu = e.ptr(e.ptr(nav.IF_MANAGER) + nav.IF_TRAINING_MENU)
        snaps = [e.read(menu, 0x200)]
        for _ in range(2):
            e.press(0, stick=(228, 128), frames=3, after=8)
            snaps.append(e.read(menu, 0x200))
        found = [o for o in range(0x200)
                 if (snaps[1][o] - snaps[0][o]) % 256 == 1 and (snaps[2][o] - snaps[1][o]) % 256 == 1]
        print(f"  items row: bytes stepping by one at {[hex(o) for o in found]}")
        return menu, found

    def spawn_kinds(self, kinds, each=2):
        """Spawns items whose kind is in `kinds` from the Items row, if the
        shown item's kind can be found in the menu; returns what was spawned."""
        e = self.emu
        nav.pause_training(e)
        nav.menu_select(e, nav.MENU_ITEMS_ROW)
        menu, found = self.item_field()
        # Project+'s ItemEx keeps the list in IfMinigameTraining: +0xEC the
        # table (one word per entry, the item kind in its low half, as
        # stOperatorInfoTraining reads it), +0x2EC the number of entries.
        table, count = e.u32(menu + 0xEC), e.u32(menu + 0x2EC)
        words = struct.unpack(f">{count}I", e.read(table, 4 * count)) if 0 < count < 200 else ()
        print(f"  item list: {count} entries, kinds {[hex(w & 0xFFFF) for w in words]}")
        spawned = []
        off = found[0] if found else None
        for kind in kinds:
            index = next((i for i, w in enumerate(words) if w & 0xFFFF == kind), None)
            if off is None or index is None:
                continue
            for _ in range(count + 1):
                if e.u8(menu + off) == index:
                    break
                e.press(0, stick=(228, 128), frames=3, after=6)
            if e.u8(menu + off) == index:
                self.shots.append(e.screenshot(f"extra-heaps-item-{kind:02x}"))
                for _ in range(each):
                    e.press(0, ["A"], frames=3, after=10)
                spawned.append((index, hex(kind)))
        nav.resume_training(e)
        return spawned

    def chase_final(self, frames=1800):
        """Kirby jumps and attacks up and forward, drifting left and right,
        and presses B when it glows; returns the frame Kirby's status was
        Final (0x116), or None."""
        e = self.emu
        g = self.game
        for i in range(frames // 40):
            stick = (228, 128) if (i // 3) % 2 == 0 else (28, 128)
            e.press(0, ["X"], frames=3, after=2, stick=stick)
            e.press(0, ["A"], frames=3, after=12, stick=(128, 228))
            e.press(0, ["B"], frames=3, after=20)
            g.mods = {}
            if g.fighter(g.entry(0))["status"] == 0x116:
                return nav.game_frame(e)
        return None


def cmd_test_extra_heaps(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-extra-heaps")
    ap.add_argument("rel")
    # PictoChat (137 KB stage module, its drawings change the stage), Port
    # Town Aero Dive (109 KB, moving track), Norfair (122 KB, lava).
    ap.add_argument("--stages", default="0x24,0x16,0x0B", help="stage kinds")
    ap.add_argument("--victims", default="ice_climbers,pikachu,samus")
    ap.add_argument("--items", type=int, default=30)
    opts = ap.parse_args(rest)
    stages = [int(s, 0) for s in opts.stages.split(",")]
    victims = opts.victims.split(",")
    label = "test-extra-heaps"
    backend = E.headless_backend()
    t0 = time.monotonic()
    why, step, log_path, taken, run, results = "", "boot", "", [], None, []
    try:
        with E.harness_run(backend, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            run = Run(emu)
            try:
                nav.boot_to_training_css(emu)
                for i, want in enumerate(stages):
                    victim = victims[i % len(victims)]
                    step = f"match {i + 1}: kirby vs {victim} on stage kind {want:#x}"
                    emu.mark(step)
                    print(step)
                    # The first stage icon is taken, then the match setup's
                    # stage kind is replaced before the match scene loads it.
                    try:
                        nav.choose_on_css(emu, "kirby", victim)
                    except Timeout as e:
                        # The token sometimes stops just off an icon's edge.
                        print(f"  character select: {e}; trying again")
                        nav.choose_on_css(emu, "kirby", victim)
                    nav.press_until(emu, "the stage select", ["START"],
                                    lambda: nav.scene(emu)[0] == "scSelStage", 600)
                    nav.pick_stage(emu, 0)
                    # The stage select writes its choice as it exits; the
                    # match scene reads it when it starts.
                    emu.wait_for("the stage select to exit", lambda: nav.scene(emu)[0] != "scSelStage", 600)
                    mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
                    emu.h.call("write_mem", addr=mode + nav.MELEE_STAGE, hex=struct.pack(">H", want).hex())
                    print(f"  stage kind written in {nav.scene(emu)[0]}")
                    nav.wait_match_start(emu)
                    kind = nav.melee_setup(emu)["stage"]
                    emu.advance(60)
                    taken.append(emu.screenshot(f"extra-heaps-m{i + 1}-stage"))
                    run.check(f"m{i + 1} start, stage kind {kind:#x}")
                    C.set_training_option(emu, "Stop")
                    step = f"match {i + 1}: Kirby copies {victim}"
                    emu.mark(step)
                    copied = run.kirby_copy()
                    run.check(f"m{i + 1} after copying {victim} (try {copied})")
                    taken.append(emu.screenshot(f"extra-heaps-m{i + 1}-copy"))
                    step = f"match {i + 1}: Assist Trophy, Poke Ball, Smash Ball"
                    emu.mark(step)
                    # Assist Trophy and Poke Ball, one at a time: P1 picks
                    # the item up with A where it lands and uses it (the
                    # trophy) or throws it (the ball) with A.
                    for kind, name in ((0x00, "assist"), (0x2A, "pokeball")):
                        spawned = run.spawn_kinds([kind], each=1)
                        emu.mark(f"spawned {name} {spawned}")
                        emu.advance(40)
                        for j in range(8):
                            emu.press(0, ["A"], frames=3, after=8,
                                      stick=(160, 128) if j % 2 else (96, 128))
                        emu.press(0, ["A"], frames=3, after=4, stick=(228, 128))
                        emu.advance(150)
                        taken.append(emu.screenshot(f"extra-heaps-m{i + 1}-{name}"))
                        emu.advance(150)
                        run.check(f"m{i + 1} after {name} {spawned}")
                    step = f"match {i + 1}: Smash Ball"
                    spawned = run.spawn_kinds([0x37], each=3)
                    emu.mark(f"spawned {spawned}")
                    final = run.chase_final()
                    emu.mark(f"final smash at game frame {final}")
                    print(f"  Smash Ball {spawned}; Kirby's final smash status seen at game frame {final}")
                    emu.advance(240)
                    taken.append(emu.screenshot(f"extra-heaps-m{i + 1}-smash-ball"))
                    run.check(f"m{i + 1} after Smash Ball (final at {final})")
                    step = f"match {i + 1}: items"
                    emu.mark(step)
                    nav.spawn_items(emu, kinds=opts.items, each=1)
                    nav.roam(emu, frames=900)
                    taken.append(emu.screenshot(f"extra-heaps-m{i + 1}-items"))
                    run.check(f"m{i + 1} after {opts.items} items")
                    step = f"match {i + 1}: specials"
                    emu.mark(step)
                    for _ in range(3):
                        emu.press(0, ["B"], frames=4, after=90)
                        emu.press(0, ["B"], frames=4, after=90, stick=(128, 228))
                    run.check(f"m{i + 1} after specials")
                    step = f"match {i + 1}: 3 CPUs"
                    emu.mark(step)
                    nav.set_training_cpus(emu, 3)
                    emu.advance(120)
                    run.check(f"m{i + 1} with 3 CPUs")
                    results.append((want, kind, victim, copied, slot_heaps(emu)))
                    nav.set_training_cpus(emu, 1)
                    emu.advance(60)
                    step = f"match {i + 1}: finish"
                    nav.finish_training(emu)
                    emu.advance(60)
            except (Timeout, nav.NavError, HarnessError, C.CpuError) as e:
                why = f"{step}: {e}"
                try:
                    taken.append(emu.screenshot("fail-extra-heaps"))
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in log.splitlines():
        if re.search(r"modload|module (MTCH|XTRA)|heap has|HARNESS\]|extra:|File Read.*(Kirby|Final|Spy|ItmAssist|Monsterball|st_)|spawned",
                     line, re.I):
            print("  " + line.split("OSREPORT]: ")[-1].split("HARNESS]: ")[-1][:200])
    print()
    for want, kind, victim, copied, slots in results:
        print(f"stage kind asked {want:#x}, in the match {kind:#x}; Kirby swallowed {victim}: {'yes, try ' + str(copied) if copied else 'NO'}")
        print(f"  slot manager with 4 fighters: {slots}")
    if run is not None:
        for line in run.bad:
            print(f"problem: {line}")
        why = why or ("OverlayFighter3/4 held a game block" if run.bad else "")
    bad = L.problems(log)
    for line in bad:
        print(f"problem: {line}")
    why = why or ("problem lines in the log" if bad else "")
    for path, w, h in taken + (run.shots if run is not None else []):
        print(f"screenshot: {path} ({w}x{h})")
    print(f"time: {time.monotonic() - t0:.1f}s")
    if why:
        return L.report(log_path, why, label)
    print(f"{label}: PASS")
    return 0
