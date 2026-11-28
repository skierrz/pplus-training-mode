"""What each string literal in the mod's sources is: the knowledge behind the
PC test of whether every on-screen text fits (see text_scan.py, and
tests/host/tests/test_text_fit.cpp for what is checked in each place).

collect() goes through every literal text_scan found and gives each one of:

  an item      the text is drawn: where (the kind of place) and, for a
               printf-style text, with its longest values filled in;
  not drawn    a log line, a log line's reason, a file name, a scene name;
  walked       text of a table the test reads from the compiled source itself
               (the lab menu's pages, the drill list, the character names);
  a problem    nothing here knows the literal, or a printf-style text has an
               argument whose longest value nothing here knows, or a drawing
               function whose layout is restated below was edited. A problem
               fails the test and says what to add here.

The longest values: a number is taken to have at most three digits before
the point (a zero-padded field: its width) unless NUMBERS says otherwise; a
%s takes each of the words it can be, found in the call itself (a choice
of literals), in the table the argument indexes, or in WORDS.

Layouts restated here (for places whose drawing code cannot be compiled on
the PC) are tied to the source by GUARDS: a checksum of the function's
tokens. When the function is edited, the test fails until the numbers here
were looked at again and the checksum was updated.
"""
import itertools
import os
import re

from text_scan import conversions, render, widest_number

# ---- the font and the menu's look (src/main/ui/style.h) -----------------------

CAP = 15.0      # style::kCapHeight: capitals at scale 1



def font_advances():
    """The advance of each character at scale 1 (tests/host/data/font_advances.h)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "font_advances.h")
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    rows = re.search(r"kAdvance\[[^\]]*\] = \{(.*?)\};", text, re.S).group(1)
    numbers = [int(n) for n in re.findall(r"-?\d+", re.sub(r"//[^\n]*", "", rows))]
    return dict((chr(0x20 + i), n) for i, n in enumerate(numbers))


ADVANCE = font_advances()


def width(text):
    """How wide `text` is at scale 1, to pick the widest of many words."""
    return sum(ADVANCE.get(ch, 19) for ch in text)


def widest(words):
    """Of a long list of names only the widest is drawn by the test: the
    others fit if it does."""
    return lambda sources: [max(words(sources), key=width)]


# ---- literals that are never drawn --------------------------------------------

# Calls whose string arguments only reach the log (each was read: the
# function logs its `why` and draws nothing).
LOG_CALLS = {
    "TM_LOG", "OSReport",
    "section",                            # #pragma section names in rel.cpp
    "sameString", "game::sameString",     # comparisons
    "sceneIs", "game::sceneIs",           # scene names
    "makePath", "start",                  # file names (storage/file_io.cpp)
    "release", "cpu::release", "unsilence", "unregisterSlot", "cancel", "requestRestore", "restarted",
    "dumpTable", "notLoaded", "unload", "open", "close",
    "perf::announce",                     # PERF=1 builds' block line (shared/perf.cpp)
}
# The same, for one source only: (file, call).
LOG_CALLS_IN = {}
# Whole sources that draw no text of their own, with the reason.
NOT_DRAWN_FILES = [
    (r"stub/", "the stub draws nothing"),
    (r".*/rel\.cpp$", "module entry"),
    (r".*/experiments/", "measurement code, never in a normal build"),
    (r"main/savestate/snapshot\.cpp$", "heap names and results for the log"),
    (r"main/savestate/trace\.cpp$", "log lines"),
    (r"main/record/start_state\.cpp$", "reasons for the log"),
    (r"main/cpu/driver\.cpp$", "reasons for the log"),
    (r"main/game/code_menu\.cpp$", "P+ Code Menu option label matched in RAM"),
    (r"main/game/tracker\.cpp$", "reasons for the log"),
    (r"main/game/match\.cpp$", "scene names"),
    (r"main/storage/format\.h$", "file names"),
    (r"menu/game/training\.cpp$", "reasons for the log; browser.cpp shows them, see browser"),
    (r"menu/game/select\.cpp$", "patch names and reasons for the log"),
    (r"extra/frame_data\.cpp$", None),  # its osd::post calls are handled; the rest are log reasons
    (r"shared/", "names for the log"),
    # The proof line of `make EXTRA_PROOF=1`.
    (r"extra/module\.cpp$", "only drawn in an EXTRA_PROOF build"),
    (r"extra/di_display\.cpp$", "a text of the game it looks for"),
    (r"extra/stage_options\.cpp$", "stage module and bone names matched in RAM"),
    (r"extra/fighter_swap\.cpp$", "swap refusal reasons for the log"),
    (r"extra/ui_assets\.cpp$", "BRRES texture and palette names matched in RAM"),
    (r"extra/net_alloc\.cpp$", "logging format wrapper"),
    (r"main/module\.cpp$", "logging format wrapper"),
]

# ---- longest values ------------------------------------------------------------

# Numbers that are not "three digits": (source's base name, format) -> one
# value per conversion (None: the default).
NUMBERS = {
    ("lab.cpp", "L-Cancel (%d%%)\nMissed"): ["100"],
    ("lab.cpp", "L-Cancel (%d%%)\nAuto (Rule On)"): ["100"],
    ("lab.cpp", "L-Cancel (%d%%)\nFrame %d/7"): ["100", "7"],
    ("lab.cpp", "DACUS\nFailed (+%df)"): ["11"],
    ("lab.cpp", "%s\nRate: %d%%"): [None, "100"],
    ("pplus_tech_tracker.cpp", "%s\nCount: %u"): [None, "999"],
    ("lab.cpp", "Wavedash: f%d\nAngle: %2.1f\n%s (%df)"): ["99", "90.0", None, "99"],
    ("lab.cpp", "Grab Escape\nTimer: %d\n%.1f / frame"): ["999", "9.9"],
    ("lab.cpp", "Grab Escape\nFrames %d / %d\n%.1f / frame"): ["999", "99", "17.0"],
    ("lab.cpp", "CPU Damage: %d%%"): ["999"],
    ("ledgedash_hud.cpp", "None (%d/5)"): ["99"],
    ("ledgedash_hud.cpp", "+%df"): ["31"],
    ("ledgedash_hud.cpp", "%df"): ["99"],
    ("ledgedash_hud.cpp", "%.1f deg"): ["90.0"],
    ("wall_info_core.h", "Can Walljump: CD %df"): ["99"],
    ("psa_inspector_core.h", "PSA P%d"): ["4"],
    ("psa_inspector_core.h", "FSM x%.2f"): ["99.99"],
    ("psa_inspector_core.h", "A:%03X S:%03X f%.1f 2x%.2f"): ["FFF", "FFF", "999.9", "99.99"],
    ("psa_inspector_core.h", "A:%03X S:%03X f%.1f"): ["FFF", "FFF", "999.9"],
    ("psa_inspector_core.h", "f%.1f"): ["999.9"],
    ("psa_inspector_core.h", "w%.1f"): ["99.9"],
    ("psa_inspector_core.h", "a%.1f"): ["99.9"],
    ("psa_inspector_core.h", "%02X:%02X"): ["FF", "FF"],
    ("drill_hud_core.h", "%+df"): ["+31"],
    ("drill_hud_core.h", "%d / %u"): ["20", "20"],
    ("drill_hud_core.h", "Frame %d/7"): ["7"],
    ("drill_hud_core.h", "Frame %d"): ["10"],
    ("drill_hud_core.h", "%d clean"): ["20"],
    ("drill_hud_core.h", "%d"): ["99"],
    ("drill_hud_core.h", "GALINT +%df INVULN"): ["31"],
    ("drill_hud_core.h", "GRAB %d/5 (NO INVULN)"): ["9"],
    ("drill_hud_core.h", "GALINT %df (VULN)"): ["-24"],
    ("drill_hud_core.h", "WAVELAND f%d"): ["99"],
    ("drill_hud_core.h", "WAVEDASH f%d"): ["99"],
    ("drill_hud_core.h", "PERFECT f1 %.1f"): ["90.0"],
    ("drill_hud_core.h", "WAVEDASH f%d %.1f"): ["10", "90.0"],
    ("drill_hud_core.h", "LATE +%df"): ["10"],
    ("drill_hud_core.h", "L-CANCEL f%d/7"): ["7"],
    ("drill_hud_core.h", "EARLY -%df"): ["30"],
    ("drill_hud_core.h", "INSTANT DJC f%d"): ["2"],
    ("drill_hud_core.h", "LOW DJC f%d"): ["6"],
    ("drill_hud_core.h", "LATE DJC f%d"): ["99"],
    ("drill_hud_core.h", "POWERSHIELD f%d"): ["3"],
    ("drill_hud_core.h", "PS REFLECT f%d"): ["3"],
    ("drill_hud_core.h", "EARLY PS %df"): ["-10"],
    ("drill_hud_core.h", "LATE PS +%df"): ["10"],
    ("drill_hud_core.h", "PS-DROP f%d"): ["5"],
    ("drill_hud_core.h", "RAR BAIR f%d"): ["10"],
    ("drill_hud_core.h", "GATLING DACUS f%d"): ["7"],
    ("drill_hud_core.h", "DACUS f%d"): ["3"],
    ("drill_hud.cpp", "%u (Best %u)"): ["99", "999"],
    ("drill_hud.cpp", "%02us / %u"): ["60", "99"],
    ("drill_hud.cpp", "%u/%u (%u)"): ["20", "20", "20"],
    ("drill_hud.cpp", "%u%% (%u/%u)"): ["100", "99", "99"],
    ("drill_hud.cpp", "%df Late"): ["11"],
    ("drill_hud.cpp", "%.1f deg"): ["90.0"],
    ("drill_hud.cpp", "%df / 7f"): ["7"],
    ("drill_hud.cpp", "Early -%df"): ["30"],
    ("drill_hud.cpp", "Late +%df"): ["10"],
    ("drill_hud.cpp", "+%df"): ["31"],
    ("drill_hud.cpp", "%df"): ["99"],
    ("drill_hud.cpp", "PS f%d"): ["3"],
    ("drill_hud.cpp", "%s  -  %s"): ["CANCEL ENDURANCE", "10-REP"],
    ("drill_hud.cpp", "%s%s"): ["Perfect ", "f10"],
    ("drill_hud.cpp", "Gate %.3s"): ["4/4"],
    ("drill_hud.cpp", "%s"): ["Perfect f1"],
    ("action_log_core.h", "0x%02X"): ["FFFF"],
    ("action_log_core.h", "%s %uf"): [None, "999"],
    ("action_log.cpp", "+%df"): ["59"],
    ("action_log.cpp", "%df"): ["-59"],
    ("combo_break_rush.cpp", "%u:%02u"): ["1", "00"],
    ("combo_break_rush.cpp", "%u"): ["65535"],
    ("combo_break_rush.cpp", "x%u"): ["8"],
    ("combo_break_rush.cpp", "Best %u"): ["65535"],
    ("combo_break_rush.cpp", "Chain %u"): ["99"],
    ("drill_hud.cpp", "STREAK %u  BEST --"): ["999"],
    ("drill_hud.cpp", "%s PRACTICE"): ["FLOAT / HOVER"],
    ("drill_hud.cpp", "%u-REP SET COMPLETE"): ["20"],
    ("drill_hud.cpp", "%u / %u  (%u%%)"): ["20", "20", "100"],
    ("drill_hud.cpp", "%u"): ["999"],
    ("savestate.cpp", "%u:%02u ago"): ["99", "59"],
    ("recording_file.cpp", "%s-%d%s"): [None, "99", None],
    ("info_display.cpp", "%s: %.0f/%.0f"): [None, "999", "999"],
    ("info_display.cpp", "Coll: G:%d C:%d L:%d R:%d"): ["999", "999", "999", "999"],
    ("info_display.cpp", "Sub: %.14s %d/%d"): [None, "999", "999"],
    ("info_display.cpp", "Hit: P%d%s %df ago"): ["4", None, "99999"],
    ("info_display.cpp", "Actionable: true, %d f"): ["99999"],
    ("info_display.cpp", "Actionable: false, %d f"): ["99999"],
    ("info_display.cpp", "Frame: %d"): ["99999"],
    ("info_display.cpp", "Frame: %d/%d"): ["99999", "99999"],
    ("info_display.cpp", "State: 0x%X"): ["FFF"],
    ("info_display.cpp", "Sub: 0x%X %d/%d"): ["FFF", "999", "999"],
    ("status_names.cpp", "0x%X"): ["FFF"],
    ("adaptive_cpu.cpp", "%u%%"): ["100"],
    ("scenario_presets.cpp", "Plat %u"): ["4"],
    ("scenario_presets.cpp", "%df React"): ["99"],
    ("scenario_presets.cpp", "%df Early"): ["99"],
    ("scenario_presets.cpp", "%df Late"): ["99"],
    ("scenario_presets.cpp", "%u (Best %u)"): ["99", "999"],
    ("scenario_presets.cpp", "%u"): ["999"],
    ("scenario_presets.cpp", "Techchase: Platform %u"): ["4"],
    ("scenario_presets.cpp", "Spacing: %+d"): ["+12"],
    ("scenario_presets_core.h", "%u / %u"): ["999", "999"],
    ("scenario_presets_core.h", "%u"): ["999"],
    ("scenario_presets_core.h", "Plat %u"): ["4"],
    ("scenario_presets_core.h", "%uf Out"): ["999"],
    ("scenario_presets_core.h", "%uf Held"): ["999"],
    ("scenario_presets_core.h", "%u Hits"): ["999"],
    ("scenario_presets_core.h", "%u.%u u"): ["99", "9"],
    # Players 1 to 4, frames of the match, state numbers up to 0xFFF.
    ("debug_overlay.cpp", "adv P%d>P%d %s %+d: hit %d, A %d %X<%X, D %d %X<%X, x%d"):
        ["4", "4", None, "-99", "99999", "99999", "FFF", "FFF", "99999", "FFF", "FFF", "99"],
}

# What a %s argument can be, when it is neither a choice of literals in the
# call nor an index into a table of the same source: (source's base name,
# the argument's text) -> the words, or a callable (sources) -> words.


def _decode_lz77_ascii(comp):
    out = bytearray()
    i = 0
    n = len(comp)
    while i < n:
        b = comp[i]
        i += 1
        if (b & 0x80) == 0:
            out.append(b)
        elif (b & 0x40) == 0:
            ln = ((b >> 3) & 7) + 3
            dist = (((b & 7) << 8) | comp[i]) + 1
            i += 1
            for _ in range(ln):
                out.append(out[-dist])
        else:
            ln = (b & 0x3F) + 3
            dist = ((comp[i] << 8) | comp[i + 1]) + 1
            i += 2
            for _ in range(ln):
                out.append(out[-dist])
    return bytes(out)


def status_names(sources):
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    src_path = os.path.join(repo_root, "src", "main", "game", "status_names.cpp")
    text = open(src_path, "r", encoding="utf-8").read()
    m_comp = re.search(r"kNamesComp\[[^\]]*\]\s*=\s*\{([^}]+)\}", text)
    m_raw = re.search(r"kRawBytes\s*=\s*(\d+)", text)
    m_lz = re.search(r"kNamesLz77\[[^\]]*\]\s*=\s*\{([^}]+)\}", text)
    if m_comp and m_raw:
        import importlib.util
        gmd_path = os.path.join(repo_root, "scripts", "gen_menu_descriptions.py")
        spec = importlib.util.spec_from_file_location("gen_menu_descriptions", gmd_path)
        gmd = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gmd)
        comp = bytes(int(tok, 0) for tok in re.findall(r"0x[0-9A-Fa-f]+|\d+", m_comp.group(1)))
        raw = gmd.decode_huff_lzss(comp, int(m_raw.group(1))).decode("ascii")
        names = [n for n in raw.split("\0") if n]
    elif m_lz:
        comp = bytes(int(tok, 0) for tok in re.findall(r"0x[0-9A-Fa-f]+|\d+", m_lz.group(1)))
        raw = _decode_lz77_ascii(comp).decode("ascii")
        names = [n for n in raw.split("\0") if n]
    else:
        blob = [l for l in sources["main/game/status_names.cpp"].literals if "kNames" in l.stmt]
        names = [n for n in blob[0].text.split("\0") if n]
    names += [l.text for l in sources["main/game/status_names.cpp"].arrays.get("kSpecialNames", [])]
    return names + ["0xFFF"]


def array(path, name):
    return lambda sources: [l.text for l in sources[path].arrays[name] if l.text]


def file_results(sources):
    return [l.text for l in sources["main/storage/file_io.cpp"].arrays["kTexts"]] + ["?"]


def returned(path, func):
    """The literals a function returns or assigns."""
    return lambda sources: [l.text for l in sources[path].literals
                            if l.func == func and not l.calls and l.text]


def import_reasons(sources):
    return file_results(sources) + returned("main/storage/recording_file.cpp", "applyImport")(sources)


def fail_notice_reasons(sources):
    literals = [l.text for l in sources["main/storage/recording_file.cpp"].literals
                if l.name == "failNotice" and l.arg == 1 and l.text]
    return import_reasons(sources) + literals


# The recording's file name as fileName() builds it: date, time, a counter.
RECORDING_FILE = "991231-235959-99.tmr"

widest_status = widest(status_names)

WORDS = {
    ("lab.cpp", "kFrom[t.b]"): array("main/lab.cpp", "kFrom"),
    ("lab.cpp", "kText[t.a]"): array("main/lab.cpp", "kText"),
    ("lab.cpp", "title"): lambda sources: [l.text for l in sources["main/lab.cpp"].literals if l.name == "postTechFrame"],
    ("info_display.cpp", "name"): widest_status,
    ("info_display.cpp", "state"): widest_status,
    # A subaction's name is the game's; the row shows 20 characters of it.
    ("info_display.cpp", "mi.name"): ["LandingAttackAirHiLwN"],
    ("info_display.cpp", "kGroupNames[i]"): array("main/ui/info_display.cpp", "kGroupNames"),
    ("info_display.cpp", "label"): returned("main/ui/info_display.cpp", "panelLabel"),
    ("info_display.cpp", "panelLabel(value)"): returned("main/ui/info_display.cpp", "panelLabel"),
    ("status_names.cpp", "name"): widest_status,
    ("savestate.cpp", "text"): lambda sources: [l.text for l in sources["main/savestate/savestate.cpp"].literals
                                                if l.name == "refusal"],
    ("recording_file.cpp", "storage::resultText(r)"): file_results,
    ("recording_file.cpp", "storage::resultText(s_job.result())"): file_results,
    ("recording_file.cpp", "storage::resultText(result)"): file_results,
    ("recording_file.cpp", "why"): import_reasons,
    ("recording_file.cpp", "msg"): fail_notice_reasons,
    ("recording_file.cpp", "s_name"): [RECORDING_FILE],
    ("recording_file.cpp", "name"): [RECORDING_FILE],
    ("settings_file.cpp", "storage::resultText(r)"): file_results,
    ("settings_file.cpp", "storage::resultText(s_job.result())"): file_results,
    ("settings_file.cpp", "resultText(r)"): file_results,
    ("settings_file.cpp", "resultText(s_job.result())"): file_results,
    ("pplus_tech_tracker.cpp", "label"): array("extra/pplus_tech_tracker_core.h", "kTable"),
    ("fighter_swap.cpp", "refuseNoticeText(code)"): returned("extra/fighter_swap.cpp", "refuseNoticeText"),
    ("adaptive_cpu.cpp", "adaptiveBiasLabel(v.biasAction)"): returned("extra/adaptive_cpu_core.h", "adaptiveBiasLabel"),
    ("adaptive_cpu.cpp", "adaptiveRpsLabel(v.dominantRps)"): returned("extra/adaptive_cpu_core.h", "adaptiveRpsLabel"),
    ("ledgedash_hud.cpp", "gText"): ["+31f INVULN", "-24f (VULN)", "None (99/5)", "None"],
    ("ledgedash_hud.cpp", "angleBuf"): ["", "  Angle: 90.0 deg"],
}

# ---- restated layouts ----------------------------------------------------------

# function -> the checksum its tokens had when its layout was restated here.
GUARDS = {}


def guard(path, func, checksum):
    GUARDS[(path, func)] = checksum


def scale_for_cap(cap):
    return cap / CAP


# ---- collecting -----------------------------------------------------------------

class Collector(object):
    def __init__(self, sources):
        self.sources = sources
        self.items = []
        self.problems = []
        self.done = set()

    # -- results --

    def item(self, kind, lit, text, a=0.0, b=0.0, c=0.0, d=0.0, where=None):
        self.items.append({"kind": kind, "where": where or lit.where, "text": text, "n": (a, b, c, d)})

    def problem(self, where, text):
        self.problems.append((where, text))

    def take(self, lit):
        self.done.add(id(lit))

    def taken(self, lit):
        return id(lit) in self.done

    # -- printf-style --

    def words(self, lit, expr):
        """The words a %s argument `expr` of the call `lit` is in can be:
        the literals in it, the words of the tables it indexes, and what
        WORDS says. None (and a problem) if a part of it is not known."""
        src = self.sources[lit.file]
        base = os.path.basename(lit.file)
        index = lit.call.args.index(expr) if expr in lit.call.args else -1
        literal = [l for l in lit.call.literals if l is not lit and l.arg == index]
        for l in literal:
            self.take(l)
        found = [l.text for l in literal]
        known = WORDS.get((base, expr))
        if known is None and "resultText(" in expr and base in ("recording_file.cpp", "settings_file.cpp"):
            known = file_results
        if known is not None:
            return found + list(known(self.sources) if callable(known) else known)
        # The values of a choice (a ? b : c): every part that is not a
        # condition must be a literal or an index into a table.
        plain = re.sub(r'"(?:[^"\\]|\\.)*"', '""', expr).replace("::", "\0")
        parts = re.split(r"([?:])", plain)
        for k in range(0, len(parts), 2):
            if k + 1 < len(parts) and parts[k + 1] == "?":
                continue  # a condition
            value = parts[k].strip().strip("()").strip()
            m = re.match(r"^(k\w+)\[", value)
            if value == '""':
                continue
            if m and m.group(1) in src.arrays:
                for l in src.arrays[m.group(1)]:
                    self.take(l)
                found += [l.text for l in src.arrays[m.group(1)]]
                continue
            self.problem(lit.where, 'no longest value is known for the %%s argument "%s" of "%s": add it to WORDS '
                         "in tests/host/text_rules.py" % (expr, lit.text.replace("\n", "\\n")))
            return None
        return found

    def expand(self, lit, fmt_arg, digits=3):
        """Every text the format `lit` (argument `fmt_arg` of its call) can
        give with its longest values."""
        base = os.path.basename(lit.file)
        convs = conversions(lit.text)
        if not convs:
            return [render(lit.text, [])]
        fixed = NUMBERS.get((base, lit.text)) or NUMBERS.get(("*", lit.text)) or [None] * len(convs)
        choices = []
        for k, m in enumerate(convs):
            if fixed[k] is not None:
                choices.append([fixed[k]])
            elif m.group("type") == "s":
                if fmt_arg + 1 + k >= len(lit.call.args):
                    self.problem(lit.where, 'the format "%s" has more conversions than arguments' % lit.text)
                    return []
                found = self.words(lit, lit.call.args[fmt_arg + 1 + k])
                if found is None:
                    return []
                choices.append(found or [""])
            else:
                choices.append([widest_number(m, digits)])
        total = 1
        for c in choices:
            total *= len(c)
        if total > 96:
            # Too many to draw each: every word of each argument once, with
            # the longest words (by letters) for the others.
            longest = [max(c, key=len) for c in choices]
            out = []
            for k, c in enumerate(choices):
                for word in c:
                    out.append(render(lit.text, longest[:k] + [word] + longest[k + 1:]))
            return sorted(set(out))
        return sorted(set(render(lit.text, list(v)) for v in itertools.product(*choices)))

    def buffer(self, lit, text, size, where=None):
        """A text written into a char array of `size` bytes: an item only
        if it does not fit (the test then reports it)."""
        if size and len(text) + 1 > size:
            self.item("buffer", lit, text, float(size), where=where)

    def capacity(self, lit, dest):
        """The size of the char array a text is written to, 0 if not known."""
        field = re.sub(r"\[[^\]]*\]$", "", dest).split(".")[-1].split(">")[-1].strip()
        size = self.sources[lit.file].buffers.get(field, 0)
        return float(size)


def collect(sources):
    c = Collector(sources)
    for path in sorted(sources):
        src = sources[path]
        for lit in src.literals:
            if "TM_LOG" in lit.calls or lit.stmt.endswith("extern"):
                c.take(lit)
            elif any(n in LOG_CALLS or (path, n) in LOG_CALLS_IN for n in lit.calls):
                c.take(lit)
    # Places first (they also take the words their formats use), then the
    # sources that draw nothing.
    for path in sorted(sources):
        src = sources[path]
        if path.startswith("main/ui/pages/"):
            lab_page(c, src)
        for handler_path, handler in HANDLERS:
            if path == handler_path:
                handler(c, src)
        messages(c, src)
    for path in sorted(sources):
        for pattern, reason in NOT_DRAWN_FILES:
            if re.match(pattern, path):
                for lit in sources[path].literals:
                    c.take(lit)
    for (path, func), checksum in sorted(GUARDS.items()):
        now = sources[path].functions.get(func) if path in sources else None
        if now != checksum:
            c.problem("%s (%s)" % (path, func),
                      "this function's layout is restated in tests/host/text_rules.py and the function was edited "
                      "(checksum %s, was %s): compare the numbers there with it, then update the checksum"
                      % (now, checksum))
    for path in sorted(sources):
        for lit in sources[path].literals:
            if not c.taken(lit):
                context = lit.name and ("argument %d of %s()" % (lit.arg, lit.name)) or (
                    lit.var and "in the table %s" % lit.var) or ("after `%s`" % lit.stmt[-40:])
                c.problem(lit.where, 'the text "%s" (%s) is not known to tests/host/text_rules.py: say there where '
                          "it is drawn, or that it is not" % (lit.text.replace("\n", "\\n")[:60], context))
    return c.items, c.problems


# ---- the lab menu's pages (src/main/ui/pages) ----------------------------------

def lab_page(c, src):
    """The tables are walked by the test through the compiled pages; only
    what a page writes into a row while the match runs is listed here."""
    base = os.path.basename(src.file)
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name.startswith("TM_") or lit.var:
            pass  # walked
        elif re.search(r"\.name = ", lit.stmt):
            c.item("menu-name", lit, lit.text)
        elif base == "osd_menu.cpp" and (lit.func == "osdSyncSaveLabel" or "s_osdSaveLabel" in lit.stmt):
            if "%d" in lit.text:
                c.item("menu-value", lit, "20 OSDs")
                c.buffer(lit, "20 OSDs", 12.0)
            else:
                c.item("menu-value", lit, lit.text)
                c.buffer(lit, lit.text, 12.0)
        elif base == "info_display.cpp" and lit.func == "infoValueLabel":
            pass  # "?" for a value that is not in the table
        else:
            continue
        c.take(lit)


# ---- messages every module posts ------------------------------------------------

def messages(c, src):
    """osd::post and storage::notice, wherever they are called."""
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "osd::post" and lit.arg == 3:
            for t in c.expand(lit, 3, digits=2):
                c.item("osd", lit, t)
        elif lit.name in ("notice", "storage::notice") and lit.arg == 1:
            for t in c.expand(lit, 1):
                c.item("notice", lit, t)
        else:
            continue
        c.take(lit)


# ---- one source each ------------------------------------------------------------

def menu_cpp(c, src):
    # menu.cpp draws the rows, rail/tab chrome, breadcrumbs, and controller
    # footer strip; test_text_fit.cpp exercises every mode and controller type.
    for lit in src.literals:
        c.take(lit)


def menu_descriptions(c, src):
    # Every clean-room 1-line lab menu description in extra/menu_descriptions.cpp.
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.item("lab-desc", lit, lit.text)
        c.take(lit)


guard("main/ui/info_display.cpp", "drawSide", "ecc870b7")


def info_display(c, src):
    # drawSide(): a row is squeezed to the Panel B smoked plate's width less its two
    # insets, (214 - 2 * 7.0) = 200 px at capitals kCap = 10.5 (Medium), normal weight
    # with 1px drop shadow. Small/Medium/Large widths are 206/214/218 px on 4:3
    # (<= 218 px clears 4:3 timer). A row's buffer has 64 bytes.
    def row(lit, text):
        c.item("squeeze", lit, text, scale_for_cap(10.5), 0, 214.0 - 2 * 7.0, 0.0)
        c.buffer(lit, text, 64.0)

    formats = [l for l in src.literals if l.func == "gameText" and re.search(r"format =$", l.stmt)]
    # gameText(): a switch over the row's value; a case sets `format` (or
    # keeps the first one, "%s: %.0f") and the label is panelLabel(value):
    # its own short names, else the name the menu shows for the value. Each
    # format goes with the labels of the cases above it; a case with no
    # short name stands for the widest of the menu's names.
    short = dict(re.findall(r'\{(kInfo\w+),\s*"([^"]*)"\}', src.text))
    menu_names = [l.text.lstrip("\x01") for l in c.sources["main/ui/pages/info_display.cpp"].arrays.get("kValues", [])
                  if l.text]
    longest_menu_name = max(menu_names, key=width)
    body = src.text[src.text.index("static bool gameText("):]
    cases_of = {}
    cases = []
    fresh = True
    for line in body.split("\n"):
        m = re.match(r"\s*case (kInfo\w+):", line)
        if m:
            cases = [m.group(1)] if fresh else cases + [m.group(1)]
            fresh = False
            continue
        fresh = True
        m = re.search(r'format = "((?:[^"\\]|\\.)*)"', line)
        if m:
            cases_of.setdefault(m.group(1), set()).update(cases)
        if re.match(r"    }$", line):
            break
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit in formats:
            # snprintf(buf, size, format, label, values...). A stick's or a
            # trigger's value (four decimals) is between -1 and 1.
            convs = conversions(lit.text)
            numbers = ["-1.0000" if m.group("precision") == "4" else widest_number(m) for m in convs[1:]]
            mine = cases_of.get(lit.text, set())
            labels = set(short.get(case, longest_menu_name) for case in mine) or set([longest_menu_name])
            if lit.text == "%s: %.0f":
                labels = set([longest_menu_name])
            for label in sorted(labels):
                row(lit, render(lit.text, [label] + numbers))
        elif lit.func == "rowText" and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                row(lit, t)
        elif lit.func == "groupText":
            if lit.text == "IASA:":
                # groupText(): "IASA:" and a name for every group that is
                # open, or " -", then "  Cancel: true". Taken as the longest:
                # the eight ground groups together, or the eight air groups.
                # All eighteen at once would not fit the row's 64 bytes (the
                # code stops at the buffer's end) and is taken not to happen.
                names = [l.text for l in src.arrays["kGroupNames"] if l.text]
                row(lit, "IASA:" + "".join(" " + n for n in names[:8]) + "  Cancel: true")
                row(lit, "IASA:" + "".join(" " + n for n in names[10:]) + "  Cancel: true")
        elif lit.func == "panelLabel" or lit.var == "kGroupNames":
            pass
        else:
            continue
        c.take(lit)


guard("main/ui/ledgedash_hud.cpp", "drawLedgedashHud", "5fa280e4")


def ledgedash_hud(c, src):
    # drawLedgedashHud(): bold calm white swatch-legend row directly above bar plus
    # Panel B right-margin smoked stat plate (118 px wide, 108 px inner text room).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            cap = 16.0 if lit.text == "%df" else 20.0
            for t in c.expand(lit, 2):
                c.item("squeeze", lit, t, scale_for_cap(13.0), 0, 108.0, 0.0)
                c.buffer(lit, t, cap)
        elif lit.var == "kLegNames":
            c.item("fit", lit, lit.text, scale_for_cap(9.0), 0, 80.0, 0.0)
        elif lit.var == "kRowLabels":
            c.item("fit", lit, lit.text, scale_for_cap(9.0), 0, 108.0, 0.0)
        else:
            continue
        c.take(lit)


guard("main/ui/debug_overlay.cpp", "drawDebugOverlay", "38b42bd4")


def debug_overlay(c, src):
    # drawDebugOverlay() (Display Options > Debug Overlay): up to four lines
    # from x 36 at kTypeMicro (8.0 px cap) in a box that follows the widest; the frame line
    # has 24 bytes, the scene line 80, the advantage line 96. The scene and
    # sequence names are the game's: the longest the sources and the
    # decompilation name stand for them.
    scene = "scSelctCharacter / sqTraining - Training match, paused"
    texts = [(src.literals[0], "P+TM v99.99.99-pre99", 0), (src.literals[0], scene, 80)]
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "snprintf" and lit.arg == 2 and lit.text == "frame %u":
            texts.append((lit, "frame 4294967295", 24))
        elif lit.name == "snprintf" and lit.arg == 2 and lit.text.startswith("adv "):
            texts += [(lit, t, 96) for t in c.expand(lit, 2)]
        elif lit.name == "snprintf" and lit.arg == 2 or lit.func in ("matchText", "drawDebugOverlay"):
            pass  # the scene line, above; the version
        else:
            continue
        c.take(lit)
    for lit, text, size in texts:
        c.item("box", lit, text, scale_for_cap(8.0), 36.0, 0.0)
        c.buffer(lit, text, float(size))


def record_hud(c, src):
    # hudDraw() is compiled and drawn by the test (recording_bar_and_watermark)
    # with the largest frame count; its own texts are its numbers' formats
    # and the mode's tags.
    for lit in src.literals:
        c.take(lit)


def record_cpp(c, src):
    # message(): the text is shown by hud.cpp's hudDraw in a box over the
    # bar (the test draws each through hudDraw).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "message":
            c.item("bar-message", lit, lit.text)
        elif lit.name == "strcpy" and "g_delete" in lit.first:
            # Double-press "A: Confirm" labels on Delete Positions / Delete Slot (12 bytes).
            c.item("menu-value", lit, lit.text)
            c.buffer(lit, lit.text, 12.0)
        elif lit.func in ("savePositions",) and re.search(r"why =$", lit.stmt):
            pass  # a reason for the log
        else:
            continue
        c.take(lit)


def lab_tools(c, src):
    for lit in src.literals:
        if lit.name == "record::showMessage":
            c.item("bar-message", lit, lit.text)
            c.take(lit)


def savestate(c, src):
    # A refusal is an OSD message (osd::post in refusal(), drawn by
    # messages() with the texts WORDS collects from the refusal() calls):
    # those literals are taken here. g_stateText is the value of the General page's Savestate
    # row (40 bytes, kStateTextSize).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "osd::post":
            continue
        if lit.name == "refusal" or lit.func in ("blocked", "run"):
            pass
        elif lit.func == "updateStateText" and lit.name == "snprintf":
            if lit.arg == 2:
                for t in c.expand(lit, 2):
                    c.item("menu-value", lit, t)
                    c.buffer(lit, t, 40.0)
        else:
            continue
        c.take(lit)


def pad_inmatch_cpp(c, src):
    # Pause-menu hint texts drawn by lab.cpp (gfx::textBox right-aligned at kHintRight = 602, kTypeMicro 8.0 px cap).
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.item("box", lit, lit.text, scale_for_cap(8.0), 520.0, 0.0)
        c.take(lit)


def lab_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "gfx::textBox":
            # The hint beside the pause menu: right-aligned at kHintRight = 602, kTypeMicro 8.0 px cap.
            c.item("box", lit, lit.text, scale_for_cap(8.0), 520.0, 0.0)
        elif lit.var in ("kFrom", "kText") or lit.name == "postTechFrame":
            pass  # words of the messages above
        elif lit.name == "strcpy" and "g_resetLabel" in lit.first:
            # The Reset All Settings row's value on the Main Menu (12 bytes).
            c.item("menu-value", lit, lit.text)
            c.buffer(lit, lit.text, 12.0)
        elif ((lit.name == "strcpy" and "g_diPickLabel" in lit.first) or "g_diPickLabel" in lit.stmt
              or "lbl =" in lit.stmt):
            # The Pick DI Angle row's value on CPU > DI & SDI (16 bytes).
            c.item("menu-value", lit, lit.text)
            c.buffer(lit, lit.text, 16.0)
        else:
            continue
        c.take(lit)


def settings_modal_cpp(c, src):
    # v1 -> v2 Settings Update Modal (src/main/ui/settings_modal.cpp), drawn directly by test_text_fit.cpp.
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.take(lit)


def watermark(c, src):
    # drawWatermark() is compiled and drawn by the test.
    for lit in src.literals:
        c.take(lit)


def match_hud(c, src):
    # Match timer HUD pieces (not drawn as 2D text).
    for lit in src.literals:
        c.take(lit)


def cpu_options(c, src):
    # The Move CPU and Freeze CPU rows' values (12 bytes each).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "strcpy" and re.match(r"g_\w+Label", lit.first):
            if lit.text:
                c.item("menu-value", lit, lit.text)
                c.buffer(lit, lit.text, 12.0)
            c.take(lit)
        elif re.search(r"char g_\w+Label\[\d+\] =$", lit.stmt):
            c.take(lit)


def cpu_options_page(c, src):
    # Dynamic Slot 1 (Primary) label buffer in P_REVERSALS (28 bytes) and
    # live Custom Pool category/status buffers in P_COUNTER_POOL (20 bytes).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func in ("stripMarker", "revEnabled") or "s_revSlot1Label" in lit.stmt:
            c.item("menu-value", lit, lit.text)
            c.buffer(lit, lit.text, 28.0)
            c.take(lit)
        elif lit.func == "poolPageActive" or "s_poolCatBuf" in lit.stmt or "s_poolStatusLabel" in lit.stmt:
            if lit.text == "%s (%d On)":
                for nm in ("Ground", "Air", "Shield"):
                    t = f"{nm} (53 On)"
                    c.item("menu-value", lit, t)
                    c.buffer(lit, t, 20.0)
            elif lit.text != "%s":
                c.item("menu-value", lit, lit.text)
                c.buffer(lit, lit.text, 20.0)
            c.take(lit)


def status_names_cpp(c, src):
    # Words of the info display's State row and the state messages.
    for lit in src.literals:
        c.take(lit)


def file_io(c, src):
    # Results (words of the notices) and file names.
    for lit in src.literals:
        c.take(lit)


def recording_file(c, src):
    for lit in src.literals:
        if not c.taken(lit) and (lit.func in ("fileName", "applyImport")
                                 or (lit.name == "failNotice" and lit.arg == 1)):
            c.take(lit)  # the file's name; reasons (words of "Export/Import failed: %s")


def settings_file(c, src):
    for lit in src.literals:
        if lit.var == "kNames":
            c.take(lit)


def browser(c, src):
    # The recordings browser is compiled into the test and drawn by it (test
    # recordings_browser) with recordings, file names and states it makes up.
    # Here: the messages say() writes into s_messageText (a format with one
    # word put in) and the others shown in place of the description, which
    # the test draws through the browser.
    size = float(src.buffers.get("s_messageText", 0))
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "say" and lit.arg == 0:
            if "%s" in lit.text:
                detail = lit.call.args[1]
                given = [l.text for l in lit.call.literals if l.arg == 1]
                for l in lit.call.literals:
                    c.take(l)
                if given:
                    words = given
                elif "resultText" in detail:
                    words = file_results(c.sources)
                elif detail in ("why", "problem"):
                    words = returned("menu/game/training.cpp", "startMatch")(c.sources) + returned(
                        "menu/game/training.cpp", "checkStartPath")(c.sources)
                else:
                    words = [RECORDING_FILE]
                texts = [lit.text.replace("%s", w) for w in words]
            else:
                texts = [lit.text]
            for t in texts:
                c.item("browser-message", lit, t)
                c.buffer(lit, t, size)
        elif re.search(r"s_message =$", lit.stmt):
            c.item("browser-message", lit, lit.text)
        c.take(lit)


def pad_map_h(c, src):
    # Per-controller-type reopen hints drawn by menu/drill_list.cpp (drawPending).
    # The short back and reopen names ("B", "1", "L+R", "- + B", "C+Z") only reach logs.
    for lit in src.literals:
        if c.taken(lit):
            continue
        if "Training Menu" in lit.text:
            c.item("list-pending", lit, lit.text)
        c.take(lit)


def drill_list(c, src):
    # Boot menu (src/menu/drill_list.cpp) is compiled into
    # test_text_fit.cpp and every section, row, description, chip, banner and
    # controller footer badge is walked and checked there on 4:3 and 16:9.
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.take(lit)


def walked(c, src):
    for lit in src.literals:
        c.take(lit)


def fighter_swap_core_h(c, src):
    # Every roster name shown in the P_FIGHTER_SWAP choice rows (menu-value).
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.item("menu-value", lit, lit.text)
        c.take(lit)


def wall_info_core_h(c, src):
    # Info display rows formatted by main/ui/wall_info_core.h (same width and
    # buffer rules as info_display.cpp drawSide).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("squeeze", lit, t, scale_for_cap(7.6), 1, (236.0 - 2 * 8.0) * 0.86, 0.0)
                c.buffer(lit, t, 64.0)
            c.take(lit)


def psa_inspector_core_h(c, src):
    # PSA Inspector card (kPsaWidth = 182; inner text room 170 px; column widths
    # 50/42/42/36 px for name/frame/wait/opcode; header left 84 px, FSM 84 px).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func == "psaThreadName":
            c.item("fit", lit, lit.text, scale_for_cap(6.0), 0, 50.0, 0.0)
        elif lit.func == "psaFormatTitle" and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(6.5), 0, 84.0, 0.0)
                c.buffer(lit, t, 16.0)
        elif lit.func == "psaFormatFsm" and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(6.5), 0, 84.0, 0.0)
                c.buffer(lit, t, 20.0)
        elif lit.func == "psaFormatSubHeader" and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(5.5), 0, 170.0, 0.0)
                c.buffer(lit, t, 40.0)
        elif lit.func in ("psaFormatFrame", "psaFormatWait") and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(6.0), 0, 42.0, 0.0)
                c.buffer(lit, t, 12.0)
        elif lit.func == "psaFormatOpcode" and lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(6.0), 0, 36.0, 0.0)
                c.buffer(lit, t, 12.0)
        elif lit.func == "psaFormatFocusCommand":
            # Formatted into focusText[48] and clipped via clipToWidth to 170 px.
            pass
        else:
            continue
        c.take(lit)


def psa_opcodes_cpp(c, src):
    # Opcode names and typed argument strings, clipped via clipToWidth in psa_inspector.cpp.
    for lit in src.literals:
        c.take(lit)


def psa_inspector_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name in ("drawShadowText", "drawShadowCapText"):
            c.item("fit", lit, lit.text, scale_for_cap(6.0), 0, 170.0, 0.0)
            c.take(lit)
        elif lit.name == "snprintf" and lit.arg == 2 and lit.text == "%s:%s":
            # Focus footer strip clipped via clipToWidth to 170 px (focusLine[56]).
            c.take(lit)


def adaptive_cpu_core_h(c, src):
    # Habit Readout HUD RPS & counter-bias labels (drawn via adaptive_cpu.cpp footer).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func in ("adaptiveRpsLabel", "adaptiveBiasLabel"):
            c.item("fit", lit, lit.text, scale_for_cap(6.0), 0, 127.0, 0.0)
            c.take(lit)


def adaptive_cpu_cpp(c, src):
    # Quiet Panel B Habit Readout plate (kAdaptiveHudWidth = 118, inner width 108 px).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func != "adaptiveCpuDraw":
            continue
        if lit.var == "kBarLabels":
            c.item("fit", lit, lit.text, scale_for_cap(8.5), 0, 26.0, 0.0)
        elif lit.name in ("drawHudText", "drawShadowText", "drawShadowCapText"):
            c.item("fit", lit, lit.text, scale_for_cap(8.5), 0, 64.0, 0.0)
        elif lit.name == "strcpy":
            if lit.text == "--":
                c.item("fit", lit, lit.text, scale_for_cap(8.5), 0, 40.0, 0.0)
                c.buffer(lit, lit.text, 16.0)
            else:
                c.item("squeeze", lit, lit.text, scale_for_cap(8.5), 0, 108.0, 0.0)
                c.buffer(lit, lit.text, 28.0)
        elif lit.name == "snprintf" and lit.arg == 2:
            if lit.text == "%u%%":
                for t in c.expand(lit, 2):
                    c.item("fit", lit, t, scale_for_cap(8.5), 0, 34.0, 0.0)
                    c.buffer(lit, t, 8.0)
            else:
                for t in c.expand(lit, 2):
                    c.item("squeeze", lit, t, scale_for_cap(8.5), 0, 108.0, 0.0)
                    c.buffer(lit, t, 28.0)
        else:
            continue
        c.take(lit)


def scenario_presets_core_h(c, src):
    # Panel B right-margin stat plates for the 7 non-Techchase Lab Scenarios (108 px inner).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func == "scenarioSetRow" and lit.name == "snprintf" and lit.arg == 2 and lit.text == "%s":
            c.take(lit)
            continue
        if lit.func != "formatScenarioStatPlate":
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("squeeze", lit, t, scale_for_cap(13.0), 0, 108.0, 0.0)
                c.buffer(lit, t, 24.0)
        elif lit.name == "scenarioSetRow" or lit.var or not lit.calls:
            c.item("squeeze", lit, lit.text, scale_for_cap(13.0), 0, 108.0, 0.0)
            c.buffer(lit, lit.text, 24.0)
        else:
            continue
        c.take(lit)


def scenario_presets_cpp(c, src):
    # Unified Techchase timing bar legend + Panel B right-margin stat plate (108 px inner).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func != "scenarioPresetsDraw":
            continue
        if lit.name == "sprintf" and lit.arg == 1:
            for t in c.expand(lit, 1):
                c.item("squeeze", lit, t, scale_for_cap(13.0), 0, 108.0, 0.0)
                c.buffer(lit, t, 24.0)
        elif not lit.calls or lit.name == "drawTimingBar":
            c.item("squeeze", lit, lit.text, scale_for_cap(13.0), 0, 108.0, 0.0)
        else:
            continue
        c.take(lit)


def drill_hud_core_h(c, src):
    # Live Tech Practice HUD (kDrillHudWidth = 316) and End-Screen Modal (kDrillModalWidth = 384).
    def expand_part(lit, part):
        convs = conversions(part)
        if not convs:
            return [render(part, [])]
        fixed = NUMBERS.get(("drill_hud_core.h", part)) or [None] * len(convs)
        vals = [fixed[k] if fixed[k] is not None else widest_number(m, 3) for k, m in enumerate(convs)]
        return [render(part, vals)]

    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func == "drillHudTitle":
            for part in lit.text.split("\0"):
                c.item("fit", lit, part, scale_for_cap(8.5), 0, 146.0, 0.0)
        elif lit.func == "drillModeLabel":
            for part in lit.text.split("\0"):
                c.item("fit", lit, part, scale_for_cap(7.2), 0, 56.0, 0.0)
        elif lit.func == "drillHighlightLabel":
            for part in lit.text.split("\0"):
                c.item("fit", lit, part, scale_for_cap(10.0), 0, 185.0, 0.0)
        elif lit.func == "drillHudFormatHighlight":
            if lit.name == "snprintf" and lit.arg == 2:
                for t in c.expand(lit, 2):
                    c.item("fit", lit, t, scale_for_cap(10.0), 0, 144.0, 0.0)
                    c.buffer(lit, t, 24.0)
            else:
                for part in lit.text.split("\0"):
                    for t in expand_part(lit, part):
                        c.item("fit", lit, t, scale_for_cap(10.0), 0, 144.0, 0.0)
                        c.buffer(lit, t, 24.0)
        elif lit.func in ("drillHudInit", "drillHudInitMode", "drillHudSetMode", "drillHudCycleMode",
                          "drillHudClearSet", "drillHudStep", "drillHudPoll"):
            if lit.name == "snprintf" and lit.arg == 2:
                pass
            else:
                for part in lit.text.split("\0"):
                    for t in expand_part(lit, part):
                        c.item("squeeze", lit, t, scale_for_cap(8.0), 0, 168.0, 0.0)
                        c.buffer(lit, t, 28.0)
        else:
            continue
        c.take(lit)


def drill_hud_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if (lit.var in ("kPromptLabels", "kPromptGlyphs")
                or "kPromptLabels" in lit.stmt or "kPromptGlyphs" in lit.stmt):
            for part in lit.text.split("\0"):
                c.item("fit", lit, part, scale_for_cap(9.5), 0, 110.0, 0.0)
        elif lit.func == "formatPanelBFeedback":
            if lit.name == "strncmp" or (lit.var == "kMap" and lit.text.isupper()):
                c.take(lit)
                continue
            if lit.name == "snprintf" and lit.arg == 2:
                for t in c.expand(lit, 2):
                    c.item("squeeze", lit, t, scale_for_cap(13.0), 0, 108.0, 0.0)
                    c.buffer(lit, t, 28.0)
            else:
                c.item("squeeze", lit, lit.text, scale_for_cap(13.0), 0, 108.0, 0.0)
        elif lit.func == "drawLiveHud":
            if lit.name == "snprintf" and lit.arg == 2:
                is_cap = "caption" in lit.stmt
                cap = 9.0 if is_cap else 13.0
                room = 320.0 if is_cap else 108.0
                buf = 40.0 if is_cap else 24.0
                if lit.text == "%s":
                    buf = 28.0
                for t in c.expand(lit, 2):
                    c.item("squeeze", lit, t, scale_for_cap(cap), 0, room, 0.0)
                    c.buffer(lit, t, buf)
            else:
                # Panel B stat plate labels/values and top timing bar legend labels.
                c.item("squeeze", lit, lit.text, scale_for_cap(13.0), 0, 108.0, 0.0)
        elif lit.func == "drawEndModal":
            if lit.name == "snprintf" and lit.arg == 2:
                is_score = "%%" in lit.text
                is_title = "PRACTICE" in lit.text or "Practice" in lit.text or "SET COMPLETE" in lit.text or "Set Complete" in lit.text
                cap = 21.0 if is_score else (13.0 if is_title else 10.5)
                room = 351.0 if (is_score or is_title) else 160.0
                buf = 28.0 if (is_score or is_title) else 24.0
                expanded = (["%s Practice" % t for t in (
                    "Ledgedash", "Wavedash", "L-Cancel", "Float / Hover", "Powershield",
                    "RAR", "DACUS", "B-Reverse", "Cancel Endurance", "Movement Flow",
                    "Reaction Lab", "Techchase Escape", "Recovery Sweetspot",
                    "CC / ASDI-Down", "Snake C4 Recovery", "Break the Targets!")]
                            if lit.text == "%s Practice"
                            else (["20-Rep Set Complete"] if lit.text == "%u-Rep Set Complete" else c.expand(lit, 2)))
                for t in expanded:
                    c.item("fit", lit, t, scale_for_cap(cap), 0, room, 0.0)
                    c.buffer(lit, t, buf)
            else:
                c.item("fit", lit, lit.text, scale_for_cap(10.5), 0, 185.0, 0.0)
        else:
            continue
        c.take(lit)


def hud_draw_cpp(c, src):
    # Portrait-docked CPU status tag (104 px wide, 98 px inner text room at 10.5 cap, squeezed cleanly).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func == "cpuStatusLabel":
            c.item("squeeze", lit, lit.text, scale_for_cap(10.5), 0, 98.0, 0.0)
            c.take(lit)


def osd_quick(c, src):
    # Boot menu Y: OSDs quick screen (src/menu/osd_quick.cpp), drawn directly by test_text_fit.cpp.
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.take(lit)


def action_log_core_h(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.func == "actionLogShortLabel" and lit.text == "AttackAir":
            c.take(lit)  # prefix comparison against motionName, not drawn
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            if lit.text == "%s %uf":
                c.item("fit", lit, "JumpSq 999f", scale_for_cap(7.5), 0, 96.0, 0.0)
                c.buffer(lit, "JumpSq 999f", 20.0)
            else:
                for t in c.expand(lit, 2):
                    c.item("fit", lit, t, scale_for_cap(7.5), 0, 72.0, 0.0)
                    c.buffer(lit, t, 10.0)
        else:
            c.item("fit", lit, lit.text, scale_for_cap(7.5), 0, 72.0, 0.0)
        c.take(lit)


def action_log_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.item("fit", lit, lit.text, scale_for_cap(7.5), 0, 120.0, 0.0)
        c.take(lit)


def combo_break_rush_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.text.startswith("/stage/"):
            c.take(lit)
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(9.0), 0, 150.0, 0.0)
                c.buffer(lit, t, 32.0)
        elif lit.text == "Break the Targets!":
            c.item("fit", lit, lit.text, scale_for_cap(9.0), 0, 190.0, 0.0)
        else:
            c.item("fit", lit, lit.text, scale_for_cap(9.0), 0, 90.0, 0.0)
        c.take(lit)


def read_sources(root):
    """Every source under `root` (the mod's src/), scanned."""
    from text_scan import scan
    sources = {}
    for folder, _, names in os.walk(root):
        for name in sorted(names):
            if name.endswith((".cpp", ".h")):
                path = os.path.join(folder, name)
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                with open(path, "r", encoding="utf-8") as f:
                    sources[rel] = scan(rel, f.read())
    return sources


def quoted(text):
    out = []
    for ch in text:
        if ch == "\n":
            out.append("\\n")
        elif ch in '"\\':
            out.append("\\" + ch)
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            out.append('\\%03o' % ord(ch))
    return '"' + "".join(out) + '"'


def table(root):
    """The C++ source of the table the test reads (host/text_items.h)."""
    items, problems = collect(read_sources(root))
    seen = set()
    lines = ["// Written by tests/host/build.py from the mod's sources (tests/host/text_rules.py).",
             '#include "host/text_items.h"', "", "namespace host {", "namespace text {",
             "    extern const Item kItems[] = {"]
    for item in items:
        if "\0" in item["text"]:
            # C would see the text up to the NUL only: a rule took a label
            # with its description (or two words at once) as one text.
            problems.append((item["where"], 'the text "%s" holds a NUL: a rule in tests/host/text_rules.py took more '
                             "than one text" % item["text"].replace("\0", "\\0")[:60]))
            continue
        key = (item["kind"], item["text"], item["n"], item["where"].split(":")[0])
        if key in seen:
            continue
        seen.add(key)
        lines.append("        {%s, %s, %s, {%s}}," % (quoted(item["kind"]), quoted(item["where"]), quoted(item["text"]),
                                                    ", ".join("%rf" % float(v) for v in item["n"])))
    for where, text in problems:
        lines.append("        {\"problem\", %s, %s, {0.0f, 0.0f, 0.0f, 0.0f}}," % (quoted(where), quoted(text)))
    lines += ["        {NULL, NULL, NULL, {0.0f, 0.0f, 0.0f, 0.0f}}", "    };", "}", "}", ""]
    return "\n".join(lines), items, problems


HANDLERS = [
    ("main/ui/menu.cpp", menu_cpp),
    ("main/ui/lab_menu.cpp", walked),
    ("extra/menu_descriptions.cpp", menu_descriptions),
    ("extra/menu_summaries.cpp", walked),
    ("main/ui/info_display.cpp", info_display),
    ("main/ui/wall_info_core.h", wall_info_core_h),
    ("main/ui/ledgedash_hud.cpp", ledgedash_hud),
    ("main/ui/debug_overlay.cpp", debug_overlay),
    ("main/record/hud.cpp", record_hud),
    ("main/record/record.cpp", record_cpp),
    ("extra/lab_tools.cpp", lab_tools),
    ("extra/fighter_swap_core.h", fighter_swap_core_h),
    ("extra/pplus_tech_tracker_core.h", walked),
    ("extra/psa_inspector_core.h", psa_inspector_core_h),
    ("extra/psa_opcodes.cpp", psa_opcodes_cpp),
    ("extra/psa_inspector.cpp", psa_inspector_cpp),
    ("extra/adaptive_cpu_core.h", adaptive_cpu_core_h),
    ("extra/adaptive_cpu.cpp", adaptive_cpu_cpp),
    ("extra/scenario_presets_core.h", scenario_presets_core_h),
    ("extra/scenario_presets.cpp", scenario_presets_cpp),
    ("extra/drill_hud_core.h", drill_hud_core_h),
    ("extra/drill_hud.cpp", drill_hud_cpp),
    ("extra/hud_draw.cpp", hud_draw_cpp),
    ("extra/action_log_core.h", action_log_core_h),
    ("extra/action_log.cpp", action_log_cpp),
    ("extra/input_viewer.cpp", walked),
    ("extra/combo_break_rush.cpp", combo_break_rush_cpp),
    ("main/savestate/savestate.cpp", savestate),
    ("main/game/pad_inmatch.cpp", pad_inmatch_cpp),
    ("main/lab.cpp", lab_cpp),
    ("main/ui/settings_modal.cpp", settings_modal_cpp),
    ("main/ui/watermark.cpp", watermark),
    ("main/ui/match_hud.cpp", match_hud),
    ("main/cpu/options.cpp", cpu_options),
    ("main/ui/pages/cpu_options.cpp", cpu_options_page),
    ("main/game/status_names.cpp", status_names_cpp),
    ("main/storage/file_io.cpp", file_io),
    ("main/storage/recording_file.cpp", recording_file),
    ("main/storage/settings_file.cpp", settings_file),
    ("menu/bests_file.cpp", settings_file),  # its file names, never drawn
    ("menu/browser.cpp", browser),
    ("menu/drill_list.cpp", drill_list),
    ("menu/osd_quick.cpp", osd_quick),
    ("menu/names.cpp", walked),   # words of the browser's lines
    ("shared/game/pad_map.h", pad_map_h),
]


if __name__ == "__main__":
    import collections
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    _, found, wrong = table(os.path.join(os.path.dirname(os.path.dirname(here)), "src"))
    for kind, count in sorted(collections.Counter(i["kind"] for i in found).items()):
        print("%5d %s" % (count, kind))
    for where, text in wrong:
        print("%s: %s" % (where, text))
    if "--list" in sys.argv:
        for i in found:
            print("%s|%s|%r|%s" % (i["kind"], i["where"], i["text"], i["n"]))
    sys.exit(1 if wrong else 0)
