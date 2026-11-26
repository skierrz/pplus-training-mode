"""Emulator tests of the stub and its main module loader (run through
emu.py; settings come from the environment the same way).

  test-match REL [--p1 NAME] [--p1-costume N] [--cpu NAME] [--stage NAME]
                 [--cpus N] [--without-plugin] [--install FILE] [--shot-frame N]
      One Training match: the main module is loaded when the match scene
      starts, draws nothing while everything is at its default, and is
      unloaded when the scene exits, with its heap back to where it was. The running-match screenshot is taken
      with emulation paused at a fixed game frame of the match, with one CPU
      thread and a fixed clock, so a run without the plugin
      (--without-plugin) can be compared pixel for pixel (compare-shots).
      --install plays with another plugin file under REL's name (make
      test-big-file: one padded far past the heaps it was once read whole
      into) and also checks the menu module's load on the character select.

  test-stress REL [--cycles N] [--cpus N] [--items] [--versus N] [--characters A,B,..]
      Training matches back to back from one boot, a different pair of
      characters each time; every load and unload is checked. --versus N
      plays one Versus match with N human players instead (needs a build
      that loads the main module outside Training: EXPERIMENTS=1 ANY_MATCH=1),
      through the results screen back to the character select.

  test-reject BROKEN_REL KIND --restore REL
      Installs a broken plugin file (pack-rel.py --break KIND) under the
      plugin's name, plays a Training match and expects the stub to reject
      the main module with a log line and the game to carry on; then puts
      REL back.

  test-boot REL [--only NAME ...]
      Four boots: with no button held the game comes up on Training's
      character select with the drill list open and the menu on the
      controller (in port 1, then alone in port 2); with X held on Versus';
      with L held on Training's through P+'s own boot code, which the plugin
      leaves alone. Also checks 4-port drill list hand-over and a regular
      (non-Training) offline Versus match + select screen with the cursor
      hook installed and no match module loaded.

  compare-shots A.png B.png
      Counts differing pixels outside the overlay's rectangle.
"""
import argparse
import os
import re
import shutil
import struct
import sys
import time

sys.dont_write_bytecode = True

import emu as E  # noqa: E402
import nav  # noqa: E402
import shots  # noqa: E402
from harness import HarnessError, Timeout  # noqa: E402

NAME = None  # the plugin's log tag, escaped for regexes


def tag():
    global NAME
    if NAME is None:
        ours = E.expected_lines()[0]
        NAME = re.escape(ours[1:ours.index("]")])
    return NAME


def rx(text):
    return re.compile(text.format(name=tag()))


LOADED = r"\[{name}\] module MTCH: loaded v\S+ at ([0-9a-f]{{8}}) in (\w+); free (\w+) largest (\w+) blocks (\d+) -> free (\w+) largest (\w+)"
UNLOADED = r"\[{name}\] module MTCH: unloaded at scene exit; free (\w+) largest (\w+) blocks (\d+), back to baseline: (yes|NO)"
# The extra module, loaded right after the match module and unloaded first.
EXTRA_LOADED = LOADED.replace("MTCH", "XTRA") + r"; (\d+) us"
EXTRA_UNLOADED = UNLOADED.replace("MTCH", "XTRA")
# Its one log line per match: what it read and called in the match module.
EXTRA_PROOF = r"\[{name}\] extra: match v\S+, frame (\d+), slot 0 status (-?\d+), trace (\d+) \(render call (\d+)\)"
# Lines that mean something went wrong, whatever the test.
PROBLEMS = [
    r"\[{name}\] modules?( \w+)?: (not loaded|refused|still loaded|heap has|temp heap|scene hooks)",
    r"\[{name}\] hook [\w ]+: (stub check: unrecognised|.*not installing|removed again)",
    r"Can't Alloc|has allocated block|OSPanic|Invalid read|Invalid write",
]


def problems(log):
    found = []
    for pattern in PROBLEMS:
        found += [m.group(0) for m in rx(pattern).finditer(log)]
    return found


def check_sequence(log, steps):
    """steps: [(label, regex)]; each searched after the previous match.
    Returns [(label, matched text or None)] and whether all were found."""
    out, pos, ok = [], 0, True
    for label, pattern in steps:
        m = rx(pattern).search(log, pos)
        if m is None:
            out.append((label, None))
            ok = False
            continue
        out.append((label, m.group(0)))
        pos = m.end()
    return out, ok


def report(log_path, why, label):
    print(f"{label}: FAIL ({why})")
    log = E.read_log(log_path) if log_path else ""
    print(f"--- last 40 lines of {log_path} ---")
    for line in log.splitlines()[-40:]:
        print(line)
    return 1


def plugin_lines(log):
    return [line for line in log.splitlines()
            if re.search(rf"\[{tag()}\] (main|modules?|match|scene|overlay|padding|modload|extra)", line)]


# ---- test-match ----------------------------------------------------------------

MATCH_STEPS = [
    ("attached", r"\[{name}\] main: attached in scMelee"),
    ("lab ready", r"\[{name}\] lab: screen (4:3|16:9); frame advance off, overlay off"),
    ("main module loaded", LOADED),
    ("extra registered", r"\[{name}\] main: extra module registered"),
    ("extra loaded", EXTRA_LOADED),
    ("match started", r"\[{name}\] match: started \(Training\) at render call (\d+)"),
    ("extra drew and logged", EXTRA_PROOF),
    ("paused", r"\[{name}\] match: paused at render call (\d+)"),
    ("resumed", r"\[{name}\] match: resumed at render call (\d+)"),
    ("paused again", r"\[{name}\] match: paused at render call (\d+)"),
    ("extra gone", r"\[{name}\] main: extra module gone"),
    ("extra unloaded first", EXTRA_UNLOADED.replace("(yes|NO)", "yes")),
    ("detaching", r"\[{name}\] main: detaching"),
    ("unloaded, heap at baseline", UNLOADED.replace("(yes|NO)", "yes")),
    ("back on CSS", r"\[{name}\] scene: scSelctCharacter / sqTraining at render call (\d+)"),
]
# The running-match screenshot: this many simulated frames into the match.
SHOT_FRAME = 180
SETTLE_FRAMES = 30


def match_args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-match")
    ap.add_argument("rel")
    ap.add_argument("--p1", default="mario", choices=sorted(nav.CSS_POS))
    ap.add_argument("--p1-costume", type=int, default=0)
    ap.add_argument("--cpu", default="fox", choices=sorted(nav.CSS_POS))
    ap.add_argument("--stage", default="final_destination", choices=sorted(nav.STAGES))
    ap.add_argument("--cpus", type=int, default=1, choices=(1, 2, 3))
    ap.add_argument("--shot-frame", type=int, default=SHOT_FRAME)
    ap.add_argument("--without-plugin", action="store_true",
                    help="take the plugin off the SD card for this run (baseline screenshots)")
    ap.add_argument("--install", default="",
                    help="play with this plugin file installed under REL's name (pack-rel.py --pad), "
                         "then put REL back")
    return ap.parse_args(rest)


# With --install: the file's size at boot and the menu module's load on
# Training's character select, before the match.
INSTALL_STEPS = [
    ("file read at boot", r"\[{name}\] modules: \S+: ok; (\d+)-byte file, \d+ module\(s\)"),
    ("menu module loaded", LOADED.replace("MTCH", "MENU")),
    ("menu module unloaded", UNLOADED.replace("MTCH", "MENU").replace("(yes|NO)", "yes")),
]


def cmd_test_match(rest):
    opts = match_args(rest)
    label = "test-match"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    kind = "noplugin" if opts.without_plugin else "plugin"
    installed = os.path.join(E.plugins_dir(os.path.abspath(E.env("SD_FOLDER"))), os.path.basename(opts.rel))
    if opts.without_plugin and os.path.exists(installed):
        os.remove(installed)
    if opts.install:
        shutil.copyfile(opts.install, installed)
        print(f"installed {opts.install} ({os.path.getsize(opts.install)} bytes) as {installed}")
    t0 = time.monotonic()
    taken, why, step, log_path = [], "", "boot", ""
    try:
        with E.harness_run(backend, E.DETERMINISTIC_ARGS,
                           css_cache=not (opts.without_plugin or opts.install)) as (proc, emu):
            log_path = emu.log_path
            try:
                step = "into the match"
                nav.enter_training_match(emu, opts.p1, opts.cpu, opts.stage, opts.p1_costume,
                                         seed_rng=True)
                if opts.cpus != 1:
                    step = f"setting {opts.cpus} CPUs"
                    nav.set_training_cpus(emu, opts.cpus)
                step = f"screenshot at game frame {opts.shot_frame}"
                shot, check = nav.screenshot_at_game_frame(emu, f"match-{kind}", opts.shot_frame)
                taken.append(shot)
                still = differing_pixels(shot[0], check[0])[0]
                print(f"match screenshot at game frame {opts.shot_frame}; "
                      f"{still} pixels changed while held still")
                step = "pausing"
                nav.pause_training(emu)
                emu.advance(SETTLE_FRAMES)
                taken.append(emu.screenshot(f"paused-{kind}"))
                step = "resuming"
                nav.resume_training(emu)
                emu.advance(SETTLE_FRAMES)
                step = "finishing"
                nav.finish_training(emu)
                emu.advance(2 * SETTLE_FRAMES)
            except (Timeout, nav.NavError, HarnessError) as e:
                why = f"{step}: {e}"
                try:
                    emu.h.call("resume")
                    taken.append(emu.screenshot(f"fail-{kind}"))
                    print("state at failure:", nav.scene(emu)[:2])
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    finally:
        if opts.without_plugin or opts.install:
            shutil.copyfile(opts.rel, installed)
            if opts.install:
                print(f"put {opts.rel} back")
    log = E.read_log(log_path) if log_path else ""

    print()
    for line in plugin_lines(log):
        print("  " + line)
    print()
    if not opts.without_plugin:
        found, ok = check_sequence(log, (INSTALL_STEPS if opts.install else []) + MATCH_STEPS)
        if opts.install and found[0][1] is not None:
            size = int(rx(INSTALL_STEPS[0][1]).search(log).group(1))
            if size != os.path.getsize(opts.install):
                ok = False
                print(f"the stub read a {size}-byte file at boot, not the installed "
                      f"{os.path.getsize(opts.install)} bytes")
        for step_label, text in found:
            print(f"{step_label:>27}: {'yes' if text else 'NO'}")
        bad = problems(log)
        for line in bad:
            print(f"                    problem: {line}")
        if not why and not ok:
            why = "log lines missing or out of order"
        if not why and bad:
            why = "problem lines in the log"
    for path, w, h in taken:
        print(f"                 screenshot: {path} ({w}x{h})")
    if taken and not why:
        # The debug overlay is off by default (test-lab turns it on and checks
        # it), so nothing of ours is on the running-match screenshot.
        score = shots.overlay_score(taken[0][0])
        visible = score >= shots.OVERLAY_MIN_SCORE
        print(f"            overlay visible: {'yes' if visible else 'no'} (score {score:.3f})")
        if visible:
            why = "overlay visible although it is off by default"
        # The version watermark is the one thing the plugin draws by default
        # without the plugin it must not be there.
        white, dark = shots.watermark_score(taken[0][0])
        mark = white >= shots.WATERMARK_MIN_SCORE and dark >= shots.WATERMARK_MIN_SCORE
        print(f"          watermark visible: {'yes' if mark else 'no'} (white {white:.3f}, outline {dark:.3f})")
        if not why and mark == opts.without_plugin:
            why = "watermark missing" if not mark else "watermark visible without the plugin"
    print(f"time: {time.monotonic() - t0:.1f}s")
    if not why and len(taken) < 2:
        why = "screenshots missing"
    if why:
        return report(log_path, why, label)
    print(f"{label}: PASS")
    return 0


# ---- test-stress ---------------------------------------------------------------

# (P1, CPU) per cycle: fighter modules and resources of different sizes,
# including transforming and two-fighter characters.
ROTATION = [
    ("mario", "fox"), ("samus", "ice_climbers"), ("pikachu", "kirby"), ("king_dedede", "olimar"),
    ("snake", "zelda"), ("charizard", "game_and_watch"), ("ike", "sonic"), ("wario", "lucario"),
]


def stress_args(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-stress")
    ap.add_argument("rel")
    ap.add_argument("--cycles", type=int, default=5)
    ap.add_argument("--cpus", type=int, default=1, choices=(1, 2, 3))
    ap.add_argument("--items", action="store_true", help="spawn items from the pause menu each cycle")
    ap.add_argument("--versus", type=int, default=0, choices=(0, 2, 3, 4),
                    help="one Versus match with this many human players instead")
    ap.add_argument("--characters", default="",
                    help="comma-separated characters for --versus (default: from ROTATION)")
    ap.add_argument("--pair", default="",
                    help="P1,CPU for every Training match instead of the ROTATION pairs")
    return ap.parse_args(rest)


def cycle_report(log, cycles):
    """Per load/unload pair: (loaded match, unloaded match). Checks they pair
    up, every unload is back to baseline, and every load started from the
    same free space."""
    loads = list(rx(LOADED).finditer(log))
    unloads = list(rx(UNLOADED).finditer(log))
    print(f"{'cycle':>5} {'at':>8} {'heap':>12} {'free before':>11} {'largest':>8} {'free after':>10} "
          f"{'free unloaded':>13} {'baseline':>8}")
    for i, (a, b) in enumerate(zip(loads, unloads)):
        print(f"{i + 1:>5} {a.group(1):>8} {a.group(2):>12} {a.group(3):>11} {a.group(4):>8} {a.group(6):>10} "
              f"{b.group(1):>13} {b.group(4):>8}")
    why = ""
    if len(loads) != cycles or len(unloads) != cycles:
        why = f"{len(loads)} loads and {len(unloads)} unloads for {cycles} matches"
    elif any(b.group(4) != "yes" for b in unloads):
        why = "a heap did not return to its baseline"
    elif len({(a.group(3), a.group(4), a.group(5)) for a in loads}) != 1:
        why = "the heap's free space before loading changed between matches"
    elif any(a.group(1) != loads[0].group(1) for a in loads):
        why = "the module did not load at the same address every time"
    return why or extra_report(log, cycles)


def extra_report(log, cycles):
    """The extra module: loaded after the match module and unloaded before it
    in every match, back to its heap's baseline, always from the same free
    space at the same address, and its proof line logged once per match."""
    loads = list(rx(EXTRA_LOADED).finditer(log))
    unloads = list(rx(EXTRA_UNLOADED).finditer(log))
    proofs = rx(EXTRA_PROOF).findall(log)
    times = [int(a.group(8)) for a in loads]
    print(f"extra module: {len(loads)} loads, {len(unloads)} unloads, {len(proofs)} proof lines; "
          f"load times (us) {times}")
    for i, (a, b) in enumerate(zip(loads, unloads)):
        print(f"{i + 1:>5} {a.group(1):>8} {a.group(2):>16} free {a.group(3)} largest {a.group(4)} "
              f"blocks {a.group(5)} -> free {a.group(6)}; unloaded: free {b.group(1)} blocks {b.group(3)} "
              f"baseline {b.group(4)}")
    if len(loads) != cycles or len(unloads) != cycles or len(proofs) != cycles:
        return f"extra module: {len(loads)} loads, {len(unloads)} unloads, {len(proofs)} proof lines for {cycles} matches"
    if any(b.group(4) != "yes" for b in unloads):
        return "the extra module's heap did not return to its baseline"
    if len({(a.group(1), a.group(3), a.group(4), a.group(5)) for a in loads}) != 1:
        return "the extra module did not load at the same address from the same free space every time"
    # Unloaded before the match module, every time.
    order = re.findall(rf"\[{tag()}\] module (XTRA|MTCH): unloaded", log)
    if order != ["XTRA", "MTCH"] * cycles:
        return f"unload order {order}"
    return ""


# ---- every heap of the game, read through the harness ---------------------------

def heap_table(emu):
    """{heap name: (start, end, allocated blocks, bytes used)} for every heap
    that exists now, from g_HeapInfos and each pool's own counters (see
    memdump.py for the fields)."""
    import memdump as M
    out = {}
    raw = emu.read(M.HEAP_INFOS, 16 * M.HEAP_COUNT)
    for hid in range(M.HEAP_COUNT):
        name_p, pool, size, arena = struct.unpack_from(">4I", raw, 16 * hid)
        if not M.is_ram(pool):
            continue
        p = emu.read(pool, 0x20)
        start, end = struct.unpack_from(">2I", p, M.POOL_START)
        (allocated,) = struct.unpack_from(">H", p, M.POOL_ALLOCATED)
        (used,) = struct.unpack_from(">I", p, M.POOL_USED)
        out[emu.cstr(name_p) or f"heap{hid}"] = (start, end, allocated, used)
    return out


def heap_report(snapshots, pairs):
    """snapshots[i]: the heap table on the character select after match i.
    Each is compared with the last one taken after a match with the same
    pair of characters (the character select keeps the chosen characters'
    files loaded, so only those are comparable)."""
    compared, changed = 0, []
    for i, snap in enumerate(snapshots):
        twins = [j for j in range(i) if pairs[j] == pairs[i]]
        if not twins:
            continue
        before = snapshots[twins[-1]]
        compared += 1
        for name in sorted(set(before) | set(snap)):
            if before.get(name) != snap.get(name):
                changed.append(f"after match {i + 1} vs {twins[-1] + 1}: {name} {before.get(name)} -> {snap.get(name)}")
    print(f"heap table on the character select: {compared} comparisons of {len(snapshots[0]) if snapshots else 0} heaps, "
          f"{len(changed)} differences")
    for line in changed[:20]:
        print("  " + line)
    if changed:
        return f"{len(changed)} heap differences between matches with the same characters"
    return ""


def settings_report(log, matches):
    """The lab's settings block is the stub's, so it must be set to defaults
    on the first match of a boot and kept on every later one; and the lab
    must find a human and a CPU fighter in every match."""
    attaches = rx(r"\[{name}\] main: attached in scMelee \S+ \S+; settings v\d+ at ([0-9a-f]{{8}}) ([a-z ]+)").findall(log)
    print(f"settings on attach: {[b for _, b in attaches]}")
    if len(attaches) != matches:
        return f"{len(attaches)} attach lines with settings for {matches} matches"
    if matches and ([b for _, b in attaches] != ["set to defaults"] + ["kept"] * (matches - 1)
                    or len({a for a, _ in attaches}) != 1):
        return "the settings were not kept across matches"
    chunks = log.split("main: attached in scMelee")[1:]
    for i, chunk in enumerate(chunks):
        kinds = set(re.findall(r"fighters: entry \w+ player \d port -?\d+ (human|cpu)", chunk.split("main: detaching")[0]))
        if kinds != {"human", "cpu"}:
            return f"match {i + 1}: the lab listed fighters {sorted(kinds)}"
    return ""


def tracker_report(log, matches, leaving=()):
    """Every match ends with the fighters' observers taken off: as many
    removals as registrations (at least one per fighter), none lost with
    its fighter, and every re-registration in the list slot the first one
    got (a slot still held would push it to another)."""
    sums = re.findall(r"tracker: (\d+) registrations, (\d+) removals, (\d+) lost, (\d+) moved", log)
    print(f"tracker per match (registrations, removals, lost, moved): {sums}")
    if len(sums) != matches:
        return f"{len(sums)} tracker summaries for {matches} matches"
    for i, (reg, rem, lost, moved) in enumerate(sums):
        if int(reg) < 2 or reg != rem or lost != "0" or moved != "0":
            return f"match {i + 1}: tracker {reg} registrations, {rem} removals, {lost} lost, {moved} moved"
    if leaving:
        # No. of CPUs went up and down in every match: the players it
        # dropped were observed, then left the match unobserved.
        chunks = log.split("main: attached in scMelee")[1:]
        for i, chunk in enumerate(chunks[:matches]):
            for p in leaving:
                if not re.search(rf"tracker: P{p} entry \w+ observers on", chunk):
                    return f"match {i + 1}: P{p} was never observed after No. of CPUs went up"
                if not re.search(rf"tracker: P{p} entry \w+ leaving the match", chunk):
                    return f"match {i + 1}: P{p} left without a 'leaving the match' line"
    return ""


def cmd_test_stress(rest):
    opts = stress_args(rest)
    label = "test-stress"
    backend = E.headless_backend()
    extra = E.FOUR_PADS_ARGS if opts.versus else []
    t0 = time.monotonic()
    why, step, log_path, done, taken = "", "boot", "", 0, []
    snapshots, pairs = [], []
    try:
        with E.harness_run(backend, extra, allow_null=True) as (proc, emu):
            log_path = emu.log_path
            try:
                if opts.versus:
                    chars = (opts.characters.split(",") if opts.characters
                             else [c for pair in ROTATION for c in pair][:opts.versus])
                    step = f"into a Versus match with {chars}"
                    nav.enter_versus_match(emu, chars)
                    emu.advance(240)
                    taken.append(emu.screenshot(f"stress-versus{opts.versus}"))
                    step = "playing the Versus match out"
                    seen = nav.finish_versus_by_falling(emu, len(chars))
                    print(f"scenes after the match: {' -> '.join(seen)}")
                    emu.advance(60)
                    done = 1
                else:
                    nav.boot_to_training_css(emu)
                    for i in range(opts.cycles):
                        p1, cpu = (opts.pair.split(",") if opts.pair else ROTATION[i % len(ROTATION)])
                        step = f"cycle {i + 1}: into a match, {p1} vs {cpu}"
                        emu.mark(step)
                        nav.start_training_match(emu, p1, cpu)
                        if opts.cpus != 1:
                            step = f"cycle {i + 1}: {opts.cpus} CPUs"
                            nav.set_training_cpus(emu, opts.cpus)
                        emu.advance(60)
                        # No. of CPUs up and down mid-match: Training adds
                        # and removes fighters after its pause closes.
                        for count in ((1, opts.cpus) if opts.cpus == 3 else (3, opts.cpus)):
                            step = f"cycle {i + 1}: No. of CPUs to {count}"
                            nav.set_training_cpus(emu, count)
                            emu.advance(90)
                        if opts.items:
                            step = f"cycle {i + 1}: items"
                            nav.spawn_items(emu)
                            nav.roam(emu)
                            taken.append(emu.screenshot(f"stress-items{i + 1}"))
                        step = f"cycle {i + 1}: pause and resume"
                        nav.pause_training(emu)
                        emu.advance(20)
                        nav.resume_training(emu)
                        emu.advance(30)
                        step = f"cycle {i + 1}: finish"
                        nav.finish_training(emu)
                        emu.advance(60)
                        done += 1
                        step = f"cycle {i + 1}: heap table"
                        snapshots.append(heap_table(emu))
                        pairs.append((p1, cpu))
            except (Timeout, nav.NavError, HarnessError) as e:
                why = f"{step}: {e}"
                try:
                    taken.append(emu.screenshot("fail-stress"))
                    print("state at failure:", nav.scene(emu)[:2])
                except (OSError, HarnessError, ValueError):
                    pass
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in plugin_lines(log):
        if "modload" in line or "module" in line or "scene:" in line or "padding" in line:
            print("  " + line)
    print()
    why = why or cycle_report(log, done)
    why = why or ("" if opts.versus else heap_report(snapshots, pairs))
    why = why or settings_report(log, done)
    why = why or ("" if opts.versus else tracker_report(log, done, leaving=(4,) if opts.cpus == 2 else (3, 4)))
    import cpu_tests  # the fighter-input hook and CPU control (imports this module)
    why = why or cpu_tests.cpu_report(log)
    bad = problems(log)
    for line in bad:
        print(f"problem: {line}")
    why = why or ("problem lines in the log" if bad else "")
    for path, w, h in taken:
        print(f"screenshot: {path} ({w}x{h})")
    print(f"matches: {done}; time: {time.monotonic() - t0:.1f}s")
    if why:
        return report(log_path, why, label)
    print(f"{label}: PASS")
    return 0


# ---- test-reject -----------------------------------------------------------------

REJECT_LINES = {
    "badversion": [
        ("checked at boot", r"\[{name}\] module MTCH: id \d+, .*: ok"),
        ("refused at load",
         r"\[{name}\] module MTCH: refused \(interface version mismatch; stub interface v\d+\), unloaded"),
    ],
    "corrupt": [
        ("rejected at boot", r"\[{name}\] module MTCH: id \d+, .*: checksum mismatch"),
        ("not loaded", r"\[{name}\] module MTCH: not loaded: rejected at boot"),
    ],
    "truncated": [
        ("rejected at boot", r"\[{name}\] modules: \S*pf/plugins/{name}\.rel: no trailer"),
        ("not loaded", r"\[{name}\] module MTCH: not loaded: none in file"),
    ],
}


def cmd_test_reject(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-reject")
    ap.add_argument("broken")
    ap.add_argument("kind", choices=sorted(REJECT_LINES))
    ap.add_argument("--restore", required=True)
    opts = ap.parse_args(rest)
    label = f"test-reject {opts.kind}"
    backend = E.env("VIDEO_BACKEND", required=False) or "D3D"
    installed = os.path.join(E.plugins_dir(os.path.abspath(E.env("SD_FOLDER"))), os.path.basename(opts.restore))
    shutil.copyfile(opts.broken, installed)
    print(f"installed {opts.broken} as {installed}")
    why, step, log_path, taken = "", "boot", "", []
    try:
        with E.harness_run(backend, css_cache=False) as (proc, emu):
            log_path = emu.log_path
            try:
                step = "into the match"
                nav.enter_training_match(emu)
                emu.advance(120)
                taken.append(emu.screenshot(f"reject-{opts.kind}"))
                step = "finishing"
                nav.finish_training(emu)
                emu.advance(60)
            except (Timeout, nav.NavError, HarnessError) as e:
                why = f"{step}: {e}"
    except (OSError, HarnessError, Timeout, ValueError) as e:
        why = why or f"{step}: {e}"
    finally:
        shutil.copyfile(opts.restore, installed)
        print(f"put {opts.restore} back")
    log = E.read_log(log_path) if log_path else ""
    print()
    for line in plugin_lines(log):
        if "main" in line or "module" in line:
            print("  " + line)
    found, ok = check_sequence(log, REJECT_LINES[opts.kind])
    for step_label, text in found:
        print(f"{step_label:>17}: {'yes' if text else 'NO'}")
    loaded = rx(LOADED).search(log) or rx(r"\[{name}\] main: attached").search(log)
    print(f"{'module attached':>17}: {'YES' if loaded else 'no'}")
    if not why and not ok:
        why = "rejection lines missing"
    if not why and loaded:
        why = "the broken main module was attached"
    if not why and taken and shots.overlay_visible(taken[0][0]):
        why = "the overlay is on screen"
    if why:
        return report(log_path, why, label)
    print(f"{label}: PASS")
    return 0


# ---- test-boot -------------------------------------------------------------------

GAME_GLOBAL = 0x805A00E0  # pointer; +0x1C gmSetRule, whose +0x24 is the menu's pad
BOOT_CASES = [
    # (the controller's port, held, sequence, the stub's boot line or None for none)
    (0, [], "sqTraining", "boot: Training, menu on pad 0"),
    (1, [], "sqTraining", "boot: Training, menu on pad 1"),
    (0, ["X"], "sqVsMelee", "boot: X/Y held on pad 0, Versus"),
    (0, ["L"], "sqTraining", None),
]


def boot_case(backend, port, held, want_seq, want_line):
    """None if the boot went as expected, else why not."""
    with E.harness_run(backend, E.PORT2_ONLY_ARGS if port == 1 else (), allow_null=True) as (proc, emu):
        emu.hold(port, buttons=held)
        emu.wait_for("the character select screen after boot",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
        emu.neutral(port)
        seq = nav.scene(emu)[1]
        menu_pad = emu.u32(emu.u32(emu.u32(GAME_GLOBAL) + 0x1C) + 0x24)
        log = E.read_log(emu.log_path)
        if want_seq == "sqTraining":
            emu.wait_for("the drill list to open",
                         lambda: re.search(r"\] drills: list opened \(auto\)", E.read_log(emu.log_path)), 900,
                         every=5)
        lines = re.findall(r"\] (boot: [^\r\n]*)", log)
        print(f"  held {held or 'nothing'}: {seq}, menu pad {menu_pad}, stub lines {lines}")
        if seq != want_seq:
            return f"held {held}: booted to {seq}, not {want_seq}"
        if lines != ([want_line] if want_line else []):
            return f"held {held}: stub lines {lines}"
        if want_seq == "sqTraining" and menu_pad != port:
            return f"held {held}: menu on pad {menu_pad}, not {port}"
        if want_seq == "sqTraining":
            # The controller moves the player's hand.
            nav.close_drill_list(emu, port=port)
            emu.wait_for("the player's hand", lambda: (nav.css_area(emu, 0) or {}).get("x") is not None, 600,
                         every=2)
            emu.advance(20)
            x0 = nav.css_area(emu, 0)["x"]
            emu.hold(port, stick=(228, 128))
            emu.advance(30)
            emu.neutral(port)
            moved = nav.css_area(emu, 0)["x"] - x0
            print(f"    port {port + 1}'s stick moved the hand {moved:+.1f}")
            if moved <= 1.0:
                return f"port {port + 1}'s stick did not move the hand ({moved:+.1f})"
    return None


def boot_multi_port_drill_list(backend):
    """All four GC ports connected: menu boots on port 1 with the drill list
    open. An active owner cannot be stolen by another port; once idle, ports
    2, 3 and 4 can each take ownership (first press claims without acting)
    and operate/close the list."""
    with E.harness_run(backend, E.FOUR_PADS_ARGS, allow_null=True) as (proc, emu):
        for p in range(4):
            emu.neutral(p)
        emu.wait_for("the character select screen after boot",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter", 60 * 120, every=5)
        emu.wait_for("the drill list to open",
                     lambda: re.search(r"\] drills: list opened \(auto\) for port 1", E.read_log(emu.log_path)),
                     900, every=5)
        emu.advance(10)
        # While port 1 is active (holding L+stick up or D-pad up on row 0),
        # port 2 pressing B must NOT steal ownership or close the list.
        emu.hold(0, stick=(128, 228))
        emu.advance(4)
        emu.press(1, ["B"], frames=3, after=6)
        emu.neutral(0)
        emu.advance(6)
        log = E.read_log(emu.log_path)
        if re.search(r"\] drills: list (handed to port 2|closed)", log):
            return "port 2 stole ownership or closed the list while port 1 was active"
        # Port 1 is now idle: port 2's first press (B) claims ownership without closing,
        # then port 2 moves down to row 1.
        emu.press(1, ["B"], frames=3, after=6)
        emu.wait_for("ownership hand-over to port 2",
                     lambda: re.search(r"\] drills: list handed to port 2", E.read_log(emu.log_path)), 120, every=2)
        if re.search(r"\] drills: list closed", E.read_log(emu.log_path)):
            return "port 2's takeover press of B closed the list on the hand-over frame"
        emu.press(1, ["DDOWN"], frames=3, after=6)
        emu.wait_for("port 2 to move to row 1",
                     lambda: re.search(r"\] drills: row 1 ", E.read_log(emu.log_path)), 120, every=2)
        # Port 3 takes over from idle port 2, then moves down to row 2.
        emu.press(2, ["DDOWN"], frames=3, after=6)
        emu.wait_for("ownership hand-over to port 3",
                     lambda: re.search(r"\] drills: list handed to port 3", E.read_log(emu.log_path)), 120, every=2)
        emu.press(2, ["DDOWN"], frames=3, after=6)
        emu.wait_for("port 3 to move to row 2",
                     lambda: re.search(r"\] drills: row 2 ", E.read_log(emu.log_path)), 120, every=2)
        # Port 4 takes over from idle port 3 without closing on the takeover frame, then closes with B.
        emu.press(3, ["B"], frames=3, after=6)
        emu.wait_for("ownership hand-over to port 4",
                     lambda: re.search(r"\] drills: list handed to port 4", E.read_log(emu.log_path)), 120, every=2)
        if re.search(r"\] drills: list closed", E.read_log(emu.log_path)):
            return "port 4's takeover press of B closed the list on the hand-over frame"
        emu.press(3, ["B"], frames=3, after=6)
        emu.wait_for("port 4 to close the drill list",
                     lambda: re.search(r"\] drills: list closed \(B\)", E.read_log(emu.log_path)), 120, every=2)
        print("  4-port boot: active owner protected; ports 2, 3 and 4 took over, navigated and closed the drill list")
    return None


ENTRY_MANAGER = 0x80624780
ENTRY_SIZE = 0x244
STATUS_DEAD = 0xBD
STATUS_REBIRTH = 0xBE
STATUS_STANDBY = 0x10B


def _versus_fighters(emu):
    """{player (0-based): {fighter, status, vis, cursor, name_cursor}} read
    straight from ftEntryManager (independent of the plugin)."""
    arr = emu.ptr(ENTRY_MANAGER)
    count = min(emu.u32(ENTRY_MANAGER + 4), 9) if arr else 0
    out = {}
    for i in range(count):
        raw = emu.read(arr + i * ENTRY_SIZE, 0x60)
        entry_id = struct.unpack_from(">i", raw, 4)[0]
        active = raw[0xA]
        if entry_id < 0 or active > 3:
            continue
        fighter = struct.unpack_from(">I", raw, 0x34 + 8 * active)[0]
        player = struct.unpack_from(">i", raw, 0x58)[0]
        if not emu.is_ram(fighter):
            continue
        acc = emu.u32(fighter + 0x60)
        enum = emu.u32(acc + 0xD8)
        status = emu.u32(emu.u32(enum + 0x70) + 0x34)
        vis = emu.u8(emu.u32(enum + 0x58) + 0xE)
        cur = emu.u8(fighter + 0x138)
        out[player] = {
            "fighter": fighter,
            "status": status,
            "vis": vis,
            "cursor": (cur >> 2) & 1,
            "name_cursor": (cur >> 1) & 1,
        }
    return out


def boot_versus_match(backend):
    """Regular (non-Training) offline Versus match and select screen with the
    fighter cursor hook installed and no callback (no match/menu module loaded):
    no crash, tags behave as the game's across ~240 frames, a KO and respawn,
    and returning to the Versus select screen."""
    with E.harness_run(backend, E.FOUR_PADS_ARGS, allow_null=True) as (proc, emu):
        for p in range(4):
            emu.neutral(p)
        nav.enter_versus_match(emu, ["mario", "fox"])
        log = E.read_log(emu.log_path)
        if not rx(r"\[{name}\] hook fighter cursor: installed at [0-9a-f]{{8}}").search(log):
            return "fighter cursor hook was not installed in Versus"
        if rx(r"\[{name}\] (module (MTCH|MENU): loaded|main: attached)").search(log):
            return "a match or menu module loaded in Versus (callback should be NULL)"

        f0 = nav.game_frame(emu)
        # ~240 frames of normal play with both fighters on stage.
        for step_i in range(48):
            stick = (200, 128) if (step_i // 8) % 2 == 0 else (56, 128)
            emu.hold(0, stick=stick)
            emu.advance(5)
            fs = _versus_fighters(emu)
            if 0 not in fs or 1 not in fs:
                return f"missing fighter during play at step {step_i}: {sorted(fs)}"
            for p in (0, 1):
                if fs[p]["vis"] != 1 or fs[p]["name_cursor"] != 1:
                    return f"P{p + 1} tag/vis wrong during normal play at step {step_i}: {fs[p]}"
        emu.neutral(0)
        f1 = nav.game_frame(emu)
        if f1 - f0 < 200:
            return f"game frame barely advanced ({f0} -> {f1})"

        # Walk P2 off the left ledge of Final Destination to trigger a KO.
        emu.hold(1, stick=(28, 128))
        emu.wait_for("P2 KO (Dead/Standby)",
                     lambda: _versus_fighters(emu).get(1, {}).get("status") in (STATUS_DEAD, STATUS_STANDBY),
                     480, every=2)
        emu.neutral(1)
        emu.advance(4)
        fs_ko = _versus_fighters(emu)
        p1_ko, p2_ko = fs_ko.get(0), fs_ko.get(1)
        if p2_ko is None or p2_ko["status"] not in (STATUS_DEAD, STATUS_STANDBY):
            return f"P2 did not stay in Dead/Standby after KO: {p2_ko}"
        if p2_ko["vis"] != 0 or p2_ko["name_cursor"] != 0 or p2_ko["cursor"] != 0:
            return f"P2 tag/vis still on while KO'd: {p2_ko}"
        if p1_ko is None or p1_ko["vis"] != 1 or p1_ko["name_cursor"] != 1:
            return f"P1 tag/vis disturbed during P2 KO: {p1_ko}"

        # Wait for P2 to respawn on the revival platform: vis returns on Rebirth,
        # and name_cursor returns once the descent onto the platform finishes.
        emu.wait_for("P2 Rebirth after KO",
                     lambda: _versus_fighters(emu).get(1, {}).get("status") == STATUS_REBIRTH,
                     360, every=2)
        emu.wait_for("P2 name cursor back after Rebirth descent",
                     lambda: _versus_fighters(emu).get(1, {}).get("name_cursor") == 1,
                     180, every=2)
        p2_rb = _versus_fighters(emu).get(1)
        if p2_rb is None or p2_rb["vis"] != 1 or p2_rb["name_cursor"] != 1:
            return f"P2 tag/vis not restored after Rebirth: {p2_rb}"
        # Drop off the revival platform (if still on it) and run 60 more frames.
        emu.press(1, stick=(128, 28), frames=4, after=10)
        emu.wait_for("P2 to leave Rebirth",
                     lambda: _versus_fighters(emu).get(1, {}).get("status") != STATUS_REBIRTH,
                     240, every=2)
        for step_i in range(12):
            emu.advance(5)
            fs_post = _versus_fighters(emu)
            for p in (0, 1):
                if fs_post[p]["vis"] != 1 or fs_post[p]["name_cursor"] != 1:
                    return f"P{p + 1} tag/vis wrong after respawn at step {step_i}: {fs_post[p]}"

        # Play the rest of the match out through the results screen back to
        # the Versus select screen, and confirm the select screen runs cleanly.
        seen = nav.finish_versus_by_falling(emu, 2)
        if nav.scene(emu)[1] != "sqVsMelee":
            return f"returned to {nav.scene(emu)[:2]} (scenes {seen}), not Versus CSS"
        emu.advance(60)
        log = E.read_log(emu.log_path)
        if rx(r"\[{name}\] (module (MTCH|MENU): loaded|main: attached)").search(log):
            return "a match or menu module loaded during Versus run"
        bad = problems(log)
        if bad:
            return f"problem lines in Versus log: {bad[:3]}"
        print(f"  Versus offline match (no callback): {f1 - f0}f play, KO (vis/cur/ncur=0), "
              f"Rebirth (cur={p2_rb['cursor']}, ncur={p2_rb['name_cursor']}), post-respawn 60f, "
              f"scenes {' -> '.join(seen)} ok")
    return None


def boot_offline_vs_readonly(backend):
    """Opt-in Offline VS Displays: enable general.offlineVersus and osd.wavedash
    through the real Lab Menu in Training, exit to a 2P local offline Versus
    match, verify match + extra attach in read-only mode (zero CPU/savestate/
    record/code-menu/display mutations, active tracker + Wavedash OSD, Start
    opens normal Versus pause instead of Lab Menu), exit cleanly, and verify
    zero heap leaks on unload."""
    with E.harness_run(backend, E.FOUR_PADS_ARGS, allow_null=True) as (proc, emu):
        for p in range(4):
            emu.neutral(p)
        nav.enter_training_match(emu, p1="mario", cpu="fox", stage="final_destination")
        emu.advance(15)
        log = E.read_log(emu.log_path)
        m_set = rx(r"\[{name}\] main: attached in scMelee / sqTraining; settings v\d+ at ([0-9a-f]{{8}})").search(log)
        if m_set is None:
            return "could not read settings block address from Training attach log"
        settings_addr = int(m_set.group(1), 16)
        if emu.u8(settings_addr + 21) != 0:
            return f"general.offlineVersus was not 0 by default ({emu.u8(settings_addr + 21)})"

        # Open Lab Menu via Start, navigate to Offline VS Displays (row 8,
        # 8 Downs from row 0 in Rail mode), and toggle it ON.
        pos_menu = len(log)
        emu.press(0, ["START"], frames=3, after=8)
        emu.wait_for("Lab Menu open in Training",
                     lambda: "lab: menu opened by port 1 (Start)" in E.read_log(emu.log_path)[pos_menu:],
                     180, every=2)
        for _ in range(8):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["DRIGHT"], frames=2, after=6)
        if emu.u8(settings_addr + 21) != 1:
            return f"Offline VS Displays toggle did not set byte +21 to 1 (got {emu.u8(settings_addr + 21)})"

        # Enable OSD Menu > Wavedash (rail 5 -> row 4) through the real menu.
        emu.press(0, ["B"], frames=2, after=6)
        for _ in range(5):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["A"], frames=2, after=6)
        for _ in range(4):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["DRIGHT"], frames=2, after=6)
        if emu.u8(settings_addr + 92) != 1:
            return f"Wavedash OSD toggle did not set byte +92 to 1 (got {emu.u8(settings_addr + 92)})"

        # Exit Training cleanly via Lab Menu left rail > Return to CSS (rail 9).
        emu.press(0, ["B"], frames=2, after=6)
        for _ in range(4):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["A"], frames=2, after=6)
        emu.wait_for("Training CSS after Return to CSS",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
        emu.advance(25)

        # Back out of Training CSS -> Main Menu -> Versus -> Brawl (sqVsMelee).
        nav.close_drill_list(emu)
        emu.advance(15)
        emu.hold(0, buttons=["B"])
        emu.wait_for("Main Menu from Training CSS",
                     lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
        emu.neutral(0)
        emu.advance(25)
        emu.press(0, ["B"], frames=3, after=25)
        emu.press(0, stick=(28, 128), frames=3, after=15)
        emu.press(0, ["A"], frames=3, after=25)
        emu.press(0, ["A"], frames=3, after=25)
        emu.wait_for("Versus CSS (sqVsMelee)",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter" and nav.scene(emu)[1] == "sqVsMelee",
                     600, every=5)
        emu.advance(30)
        for port in (0, 1):
            a = nav.css_area(emu, port)
            if not a or a["kind"] == 0:
                emu.press(port, ["A"])
                emu.wait_for(f"Versus P{port + 1} join",
                             lambda p=port: (nav.css_area(emu, p) or {}).get("kind") in (1, 2), 120, every=2)
        emu.advance(10)
        if nav.css_area(emu, 0)["held"]:
            nav.drop_own_token(emu, 0, "mario")
        if nav.css_area(emu, 1)["held"]:
            nav.drop_own_token(emu, 1, "fox")

        pos_vs = len(E.read_log(emu.log_path))
        nav.press_until(emu, "Versus stage select", ["START"], lambda: nav.scene(emu)[0] == "scSelStage", 600)
        nav.pick_stage(emu, "final_destination")
        nav.wait_match_start(emu)
        emu.advance(20)

        vs_log = E.read_log(emu.log_path)[pos_vs:]
        if not rx(LOADED).search(vs_log):
            return "match module did not load in opted-in 2P offline Versus"
        if not rx(EXTRA_LOADED).search(vs_log):
            return "extra module did not load in opted-in 2P offline Versus"
        if not rx(r"\[{name}\] main: read-only offline Versus displays attached").search(vs_log):
            return "missing read-only offline Versus attach log line"
        if not rx(r"\[{name}\] extra: attached \(read-only VS\)").search(vs_log):
            return "missing extra read-only VS attach log line"
        for forbidden in (r"code menu:", r"savestate:", r"cpu:", r"record:", r"display: layers"):
            if rx(rf"\[{{name}}\] {forbidden}").search(vs_log):
                return f"mutating subsystem logged in read-only Versus: {forbidden}"

        # Perform a wavedash on P1 (Mario: 4f jumpsquat) and confirm the tracker + OSD fires.
        emu.hold(0, buttons=["X"])
        emu.advance(2)
        emu.neutral(0)
        emu.advance(3)
        emu.hold(0, buttons=["L"], stick=(210, 72))
        emu.advance(6)
        emu.neutral(0)
        emu.advance(30)
        vs_log = E.read_log(emu.log_path)[pos_vs:]
        m_osd = rx(r"\[{name}\] osd: P1 id 0: (Wavedash:[^\r\n]*)").search(vs_log)
        if m_osd is None:
            return "Wavedash OSD did not post in read-only offline Versus"

        # Start must open normal Versus pause (freezing game_frame), NOT Lab Menu.
        emu.press(0, ["START"], frames=3, after=15)
        f_pause_1 = nav.game_frame(emu)
        emu.advance(20)
        f_pause_2 = nav.game_frame(emu)
        vs_log = E.read_log(emu.log_path)[pos_vs:]
        if rx(r"\[{name}\] lab: menu opened").search(vs_log):
            return "Start opened the Lab Menu in read-only offline Versus"
        if f_pause_2 != f_pause_1:
            return f"Start did not open normal Versus pause (game_frame {f_pause_1} -> {f_pause_2})"
        emu.press(0, ["START"], frames=3, after=15)
        emu.advance(20)
        f_resume = nav.game_frame(emu)
        if f_resume <= f_pause_2:
            return f"Versus did not unpause on second Start (game_frame stayed {f_resume})"

        # Finish Versus cleanly and verify zero heap leaks on unload.
        seen = nav.finish_versus_by_falling(emu, 2)
        if nav.scene(emu)[1] != "sqVsMelee":
            return f"returned to {nav.scene(emu)[:2]} (scenes {seen}), not Versus CSS"
        emu.advance(30)
        vs_log = E.read_log(emu.log_path)[pos_vs:]
        unload_steps = [
            ("extra unloaded at baseline", EXTRA_UNLOADED.replace("(yes|NO)", "yes")),
            ("main detaching", r"\[{name}\] main: detaching"),
            ("match unloaded at baseline", UNLOADED.replace("(yes|NO)", "yes")),
        ]
        found, ok = check_sequence(vs_log, unload_steps)
        if not ok:
            missing = [lbl for lbl, hit in found if hit is None]
            return f"clean Versus unload checks failed: missing {missing}"
        bad = problems(vs_log)
        if bad:
            return f"problem lines in read-only Versus log: {bad[:3]}"
        print(f"  Offline VS read-only: MTCH+XTRA loaded, OSD '{m_osd.group(1)}', "
              f"Start paused ({f_pause_1}=={f_pause_2} -> {f_resume}), clean unload back to baseline ok")
    return None


def boot_offline_vs_4p(backend):
    """Opt-in 4P local offline Versus with 4 distinct characters:
      1. Assist Trophies OFF (default competitive P+ ruleset): MTCH loads in
         OverlayMenu and XTRA loads in AssistFigureResource (MEM2), all four
         OverlayFighter1..4 heaps stay free for the four fighters, clean unload.
      2. Assist Trophies ON (m_itemFrequency > 0 and m_assistTrophy bit set):
         MTCH loads in OverlayMenu, XTRA is skipped (match only), leaving
         AssistFigureResource free for Brawl's Assist Trophy preloading."""
    with E.harness_run(backend, E.FOUR_PADS_ARGS, allow_null=True) as (proc, emu):
        for p in range(4):
            emu.neutral(p)
        nav.enter_training_match(emu, p1="mario", cpu="fox", stage="final_destination")
        emu.advance(15)
        log = E.read_log(emu.log_path)
        m_set = rx(r"\[{name}\] main: attached in scMelee / sqTraining; settings v\d+ at ([0-9a-f]{{8}})").search(log)
        if m_set is None:
            return "could not read settings block address from Training attach log"
        settings_addr = int(m_set.group(1), 16)

        # Open Lab Menu via Start, enable Offline VS Displays (General row 8).
        pos_menu = len(log)
        emu.press(0, ["START"], frames=3, after=8)
        emu.wait_for("Lab Menu open in Training",
                     lambda: "lab: menu opened by port 1 (Start)" in E.read_log(emu.log_path)[pos_menu:],
                     180, every=2)
        for _ in range(8):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["DRIGHT"], frames=2, after=6)
        if emu.u8(settings_addr + 21) != 1:
            return f"Offline VS Displays toggle did not set byte +21 to 1 (got {emu.u8(settings_addr + 21)})"

        # Return to Training CSS -> Main Menu -> Versus -> Brawl (sqVsMelee).
        emu.press(0, ["B"], frames=2, after=6)
        for _ in range(9):
            emu.press(0, ["DDOWN"], frames=2, after=5)
        emu.press(0, ["A"], frames=2, after=6)
        emu.wait_for("Training CSS after Return to CSS",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter", 600, every=5)
        emu.advance(25)
        nav.close_drill_list(emu)
        emu.advance(15)
        emu.hold(0, buttons=["B"])
        emu.wait_for("Main Menu from Training CSS",
                     lambda: nav.scene(emu)[0] == "muMenuMain", 600, every=5)
        emu.neutral(0)
        emu.advance(25)
        emu.press(0, ["B"], frames=3, after=25)
        emu.press(0, stick=(28, 128), frames=3, after=15)
        emu.press(0, ["A"], frames=3, after=25)
        emu.press(0, ["A"], frames=3, after=25)
        emu.wait_for("Versus CSS (sqVsMelee)",
                     lambda: nav.scene(emu)[0] == "scSelctCharacter" and nav.scene(emu)[1] == "sqVsMelee",
                     600, every=5)
        emu.advance(30)

        chars = ("mario", "fox", "luigi", "jigglypuff")
        for port, char in enumerate(chars):
            a = nav.css_area(emu, port)
            if not a or a["kind"] == 0:
                emu.press(port, ["A"])
                emu.wait_for(f"Versus P{port + 1} join",
                             lambda p=port: (nav.css_area(emu, p) or {}).get("kind") in (1, 2), 120, every=2)
            emu.advance(8)
            if nav.css_area(emu, port)["held"]:
                nav.drop_own_token(emu, port, char)
        emu.advance(15)

        # ---- Case 1: 4P Offline VS with Assist Trophies OFF ----
        pos_off = len(E.read_log(emu.log_path))
        nav.press_until(emu, "Versus stage select (4P assist OFF)", ["START"],
                        lambda: nav.scene(emu)[0] == "scSelStage", 600)
        nav.pick_stage(emu, "final_destination")
        nav.wait_match_start(emu)
        emu.advance(30)

        off_log = E.read_log(emu.log_path)[pos_off:]
        m_mtch = rx(LOADED).search(off_log)
        m_xtra = rx(EXTRA_LOADED).search(off_log)
        if not m_mtch:
            return "4P VS (assist OFF): match module did not load"
        if not m_xtra:
            return "4P VS (assist OFF): extra module did not load"
        if m_xtra.group(2) != "AssistFigureResource":
            return f"4P VS (assist OFF): extra loaded in {m_xtra.group(2)}, expected AssistFigureResource"
        if not m_xtra.group(1).startswith(("92", "93")):
            return f"4P VS (assist OFF): extra loaded at {m_xtra.group(1)}, expected MEM2"
        table = heap_table(emu)
        for idx in range(1, 5):
            of = table.get(f"OverlayFighter{idx}")
            if of is None or of[2] != 1:
                return f"4P VS (assist OFF): OverlayFighter{idx} expected 1 block, got {of}"
        assist = table.get("AssistFigureResource")
        if assist is None or assist[2] != 1:
            return f"4P VS (assist OFF): AssistFigureResource expected 1 block (extra), got {assist}"

        seen = nav.finish_versus_by_falling(emu, 4)
        if nav.scene(emu)[1] != "sqVsMelee":
            return f"4P VS (assist OFF): returned to {nav.scene(emu)[:2]} ({seen}), not Versus CSS"
        emu.advance(30)
        off_log = E.read_log(emu.log_path)[pos_off:]
        found, ok = check_sequence(off_log, [
            ("extra unloaded at baseline", EXTRA_UNLOADED.replace("(yes|NO)", "yes")),
            ("match unloaded at baseline", UNLOADED.replace("(yes|NO)", "yes")),
        ])
        if not ok:
            return f"4P VS (assist OFF): unload checks failed ({found})"
        bad = problems(off_log)
        if bad:
            return f"4P VS (assist OFF): problem lines {bad[:3]}"

        # ---- Case 2: 4P Offline VS with Assist Trophies ON ----
        pos_on = len(E.read_log(emu.log_path))
        nav.press_until(emu, "Versus stage select (4P assist ON)", ["START"],
                        lambda: nav.scene(emu)[0] == "scSelStage", 600)
        nav.pick_stage(emu, "final_destination")
        emu.wait_for("stage select exit before scMelee",
                     lambda: nav.scene(emu)[0] != "scSelStage", 600, every=1)
        # Enable Medium item frequency (+0x16 = 2) and Assist Trophy bit (+0x37 |= 0x02)
        # on gmGlobalModeMelee before scMelee::start() runs.
        mode = emu.ptr(emu.ptr(nav.GAME_GLOBAL) + 0x8)
        emu.h.call("write_mem", addr=mode + 0x16, hex="02")
        cur_sw = emu.u8(mode + 0x37)
        emu.h.call("write_mem", addr=mode + 0x37, hex=f"{(cur_sw | 0x02) & 0xFF:02x}")
        nav.wait_match_start(emu)
        emu.advance(30)

        on_log = E.read_log(emu.log_path)[pos_on:]
        if not rx(LOADED).search(on_log):
            return "4P VS (assist ON): match module did not load"
        if rx(EXTRA_LOADED).search(on_log):
            return "4P VS (assist ON): extra module loaded when Assist Trophies were ON in 4P"
        if not rx(r"\[{name}\] main: read-only offline Versus displays attached").search(on_log):
            return "4P VS (assist ON): missing read-only offline Versus attach log line"

        seen2 = nav.finish_versus_by_falling(emu, 4)
        if nav.scene(emu)[1] != "sqVsMelee":
            return f"4P VS (assist ON): returned to {nav.scene(emu)[:2]} ({seen2}), not Versus CSS"
        emu.advance(30)
        on_log = E.read_log(emu.log_path)[pos_on:]
        if not rx(UNLOADED.replace("(yes|NO)", "yes")).search(on_log):
            return "4P VS (assist ON): match module did not unload to baseline"
        bad2 = problems(on_log)
        if bad2:
            return f"4P VS (assist ON): problem lines {bad2[:3]}"
        print(f"  Offline VS 4P: assist OFF loaded XTRA at {m_xtra.group(1)} in AssistFigureResource (OF1..4=1); "
              f"assist ON loaded MTCH only (XTRA skipped); both unloaded to baseline ok")
    return None


def cmd_test_boot(rest):
    ap = argparse.ArgumentParser(prog="emu.py test-boot")
    ap.add_argument("rel")
    ap.add_argument("--only", nargs="+", choices=("cases", "multi", "versus", "offline-vs-readonly", "offline-vs-4p"))
    opts = ap.parse_args(rest)
    want = set(opts.only) if opts.only else {"cases", "multi", "versus", "offline-vs-readonly", "offline-vs-4p"}
    backend = E.headless_backend()
    if "cases" in want:
        for port, held, seq, line in BOOT_CASES:
            try:
                why = boot_case(backend, port, held, seq, line)
            except (OSError, HarnessError, Timeout, ValueError, nav.NavError) as e:
                why = f"held {held}: {e}"
            if why:
                print(f"test-boot: FAIL ({why})")
                return 1
    if "multi" in want:
        try:
            why = boot_multi_port_drill_list(backend)
        except (OSError, HarnessError, Timeout, ValueError, nav.NavError) as e:
            why = f"4-port drill list: {e}"
        if why:
            print(f"test-boot: FAIL ({why})")
            return 1
    if "versus" in want:
        try:
            why = boot_versus_match(backend)
        except (OSError, HarnessError, Timeout, ValueError, nav.NavError) as e:
            why = f"versus match: {e}"
        if why:
            print(f"test-boot: FAIL ({why})")
            return 1
    if "offline-vs-readonly" in want:
        try:
            why = boot_offline_vs_readonly(backend)
        except (OSError, HarnessError, Timeout, ValueError, nav.NavError) as e:
            why = f"offline-vs-readonly: {e}"
        if why:
            print(f"test-boot: FAIL ({why})")
            return 1
    if "offline-vs-4p" in want:
        try:
            why = boot_offline_vs_4p(backend)
        except (OSError, HarnessError, Timeout, ValueError, nav.NavError) as e:
            why = f"offline-vs-4p: {e}"
        if why:
            print(f"test-boot: FAIL ({why})")
            return 1
    print("test-boot: PASS")
    return 0


# ---- compare-shots ---------------------------------------------------------------

# The overlay's box in the game's 640x480 2D space, with a margin.
OVERLAY_RECT = (0, 0, 320 / 640, 80 / 480)


def differing_pixels(path_a, path_b, tolerance=0):
    """(count, total, bounding box or None, overlay corner) of pixels that
    differ by more than `tolerance` in a channel, outside OVERLAY_RECT and
    the watermark's rectangle (shots.WATERMARK_RECT; test-match checks the
    watermark is there with the plugin and absent without it)."""
    wa, ha, ca, ra = shots.read_png(path_a)
    wb, hb, cb, rb = shots.read_png(path_b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"sizes differ ({wa}x{ha} vs {wb}x{hb})")
    x1, y1 = int(OVERLAY_RECT[2] * wa), int(OVERLAY_RECT[3] * ha)
    wx0, wy0 = int(shots.WATERMARK_RECT[0] * wa), int(shots.WATERMARK_RECT[1] * ha)
    wx1, wy1 = int(shots.WATERMARK_RECT[2] * wa), int(shots.WATERMARK_RECT[3] * ha)
    skipped = (wx1 - wx0) * (wy1 - wy0)
    diff, box = 0, [wa, ha, -1, -1]
    for y in range(ha):
        row_a, row_b = ra[y], rb[y]
        for x in range(x1 if y < y1 else 0, wa):
            if wy0 <= y < wy1 and wx0 <= x < wx1:
                continue
            pa, pb = x * ca, x * cb
            if (abs(row_a[pa] - row_b[pb]) > tolerance or abs(row_a[pa + 1] - row_b[pb + 1]) > tolerance
                    or abs(row_a[pa + 2] - row_b[pb + 2]) > tolerance):
                diff += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
    return diff, wa * ha - x1 * y1 - skipped, (box if diff else None), (x1, y1)


def cmd_compare_shots(rest):
    ap = argparse.ArgumentParser(prog="emu.py compare-shots")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tolerance", type=int, default=0, help="per-channel difference ignored")
    opts = ap.parse_args(rest)
    diff, total, box, corner = differing_pixels(opts.a, opts.b, opts.tolerance)
    print(f"compare-shots: {diff} of {total} pixels differ outside the overlay rectangle "
          f"(0,0)-{corner} and the watermark's" + (f"; differing area {tuple(box)}" if box else ""))
    return 0 if diff == 0 else 1


COMMANDS = {
    "test-match": cmd_test_match,
    "test-stress": cmd_test_stress,
    "test-reject": cmd_test_reject,
    "test-boot": cmd_test_boot,
    "compare-shots": cmd_compare_shots,
}
