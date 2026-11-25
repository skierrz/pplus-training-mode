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
]

# ---- longest values ------------------------------------------------------------

# Numbers that are not "three digits": (source's base name, format) -> one
# value per conversion (None: the default).
NUMBERS = {
    ("lab.cpp", "L-Cancel (%d%%)\nMissed"): ["100"],
    ("lab.cpp", "L-Cancel (%d%%)\nFrame %d/7"): ["100", "7"],
    ("lab.cpp", "%s\nRate: %d%%"): [None, "100"],
    ("pplus_tech_tracker.cpp", "%s\nCount: %u"): [None, "999"],
    ("lab.cpp", "Wavedash: f%d\nAngle: %2.1f\n%s (%df)"): ["99", "90.0", None, "99"],
    ("lab.cpp", "Grab Escape\nTimer: %d\n%.1f / frame"): ["999", "9.9"],
    ("ledgedash_hud.cpp", "GALINT: %d   Ledge: %d   Angle: %.1f"): ["-24", "99", "90.0"],
    ("ledgedash_hud.cpp", "GALINT: %d   Ledge: %d"): ["-24", "99"],
    ("ledgedash_hud.cpp", "GALINT: None   Ledge: %d"): ["99"],
    ("ledgedash_hud.cpp", "GALINT: None (%d/5)   Ledge: %d"): ["99", "99"],
    ("wall_info_core.h", "Can Walljump: CD %df"): ["99"],
    ("psa_inspector_core.h", "PSA P%d"): ["4"],
    ("psa_inspector_core.h", "FSM x%.2f"): ["99.99"],
    ("psa_inspector_core.h", "A:%03X S:%03X f%.1f 2x%.2f"): ["FFF", "FFF", "999.9", "99.99"],
    ("psa_inspector_core.h", "A:%03X S:%03X f%.1f"): ["FFF", "FFF", "999.9"],
    ("psa_inspector_core.h", "f%.1f"): ["999.9"],
    ("psa_inspector_core.h", "w%.1f"): ["99.9"],
    ("psa_inspector_core.h", "a%.1f"): ["99.9"],
    ("psa_inspector_core.h", "%02X:%02X"): ["FF", "FF"],
    ("savestate.cpp", "%u:%02u ago"): ["99", "59"],
    ("recording_file.cpp", "%s-%d%s"): [None, "99", None],
    ("info_display.cpp", "%s: %.0f/%.0f"): [None, "999", "999"],
    ("info_display.cpp", "Coll: G:%d C:%d L:%d R:%d"): ["999", "999", "999", "999"],
    ("info_display.cpp", "Subaction: %.20s %d/%d"): [None, "999", "999"],
    ("info_display.cpp", "Last Hit: P%d%s, %d f ago"): ["4", None, "99999"],
    ("info_display.cpp", "Actionable: true, %d f"): ["99999"],
    ("info_display.cpp", "Actionable: false, %d f"): ["99999"],
    ("info_display.cpp", "Frame: %d"): ["99999"],
    ("info_display.cpp", "Frame: %d/%d"): ["99999", "99999"],
    ("info_display.cpp", "State: 0x%X"): ["FFF"],
    ("info_display.cpp", "Subaction: 0x%X %d/%d"): ["FFF", "999", "999"],
    ("status_names.cpp", "0x%X"): ["FFF"],
    # Players 1 to 4, frames of the match, state numbers up to 0xFFF.
    ("debug_overlay.cpp", "adv P%d>P%d %s %+d: hit %d, A %d %X<%X, D %d %X<%X, x%d"):
        ["4", "4", None, "-99", "99999", "99999", "FFF", "FFF", "99999", "FFF", "FFF", "99"],
}

# What a %s argument can be, when it is neither a choice of literals in the
# call nor an index into a table of the same source: (source's base name,
# the argument's text) -> the words, or a callable (sources) -> words.


def status_names(sources):
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


# The recording's file name as fileName() builds it: date, time, a counter.
RECORDING_FILE = "991231-235959-99.tmr"

widest_status = widest(status_names)

WORDS = {
    ("lab.cpp", "kFrom[t.b]"): array("main/lab.cpp", "kFrom"),
    ("lab.cpp", "kText[t.a]"): array("main/lab.cpp", "kText"),
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
    ("recording_file.cpp", "s_name"): [RECORDING_FILE],
    ("recording_file.cpp", "name"): [RECORDING_FILE],
    ("settings_file.cpp", "storage::resultText(r)"): file_results,
    ("settings_file.cpp", "storage::resultText(s_job.result())"): file_results,
    ("settings_file.cpp", "resultText(r)"): file_results,
    ("settings_file.cpp", "resultText(s_job.result())"): file_results,
    ("pplus_tech_tracker.cpp", "label"): array("extra/pplus_tech_tracker_core.h", "kTable"),
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


guard("main/ui/info_display.cpp", "drawSide", "45d9714b")


def info_display(c, src):
    # drawSide(): a row is squeezed to the panel's width less its two
    # insets, (236 - 2 x 8.0) x k at capitals kCap, for the sizes k = 0.72,
    # 0.86, 1.0 with kCap = 6.2, 7.6, 8.8; bold. Medium leaves the least room
    # per cap height, so it is the one checked. A row's buffer has 64 bytes.
    def row(lit, text):
        c.item("squeeze", lit, text, scale_for_cap(7.6), 1, (236.0 - 2 * 8.0) * 0.86, 0.0)
        c.buffer(lit, text, 64.0)

    formats = [l for l in src.literals if l.func == "gameText" and re.search(r"format =$", l.stmt)]
    # gameText(): a switch over the row's value; a case sets `format` (or
    # keeps the first one, "%s: %.0f") and the label is panelLabel(value):
    # its own short names, else the name the menu shows for the value. Each
    # format goes with the labels of the cases above it; a case with no
    # short name stands for the widest of the menu's names.
    short = dict(re.findall(r'case (kInfo\w+):\s*return "([^"]*)";', src.text))
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


guard("main/ui/ledgedash_hud.cpp", "drawLedgedashHud", "120d84c6")


def ledgedash_hud(c, src):
    # drawLedgedashHud(): summary strip centred over the bar (520 wide, 64-byte buffer).
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "snprintf" and lit.arg == 2:
            for t in c.expand(lit, 2):
                c.item("fit", lit, t, scale_for_cap(8.0), 0, 520.0, 0.0)
                c.buffer(lit, t, 64.0)
        else:
            continue
        c.take(lit)


guard("main/ui/debug_overlay.cpp", "drawDebugOverlay", "38b42bd4")


def debug_overlay(c, src):
    # drawDebugOverlay() (Display Options > Debug Overlay): up to four lines
    # from x 20 at scale 0.5 in a box that follows the widest; the frame line
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
        c.item("box", lit, text, 0.5, 20.0, 0.0)
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
    # Pause-menu hint texts drawn by lab.cpp (gfx::textBox at x 520, scale 0.5).
    for lit in src.literals:
        if c.taken(lit):
            continue
        c.item("box", lit, lit.text, 0.5, 520.0, 0.0)
        c.take(lit)


def lab_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "gfx::textBox":
            # The hint beside the pause menu: a box from x 520, scale 0.5.
            c.item("box", lit, lit.text, 0.5, 520.0, 0.0)
        elif lit.var in ("kFrom", "kText"):
            pass  # words of the messages above
        elif lit.name == "strcpy" and "g_resetLabel" in lit.first:
            # The Reset All Settings row's value on the Main Menu (12 bytes).
            c.item("menu-value", lit, lit.text)
            c.buffer(lit, lit.text, 12.0)
        else:
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
        if not c.taken(lit) and (lit.func in ("fileName", "applyImport")):
            c.take(lit)  # the file's name; reasons (words of "Import failed: %s")


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
        else:
            continue
        c.take(lit)


def psa_inspector_cpp(c, src):
    for lit in src.literals:
        if c.taken(lit):
            continue
        if lit.name == "drawShadowText":
            c.item("fit", lit, lit.text, scale_for_cap(6.0), 0, 170.0, 0.0)
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
    ("extra/psa_inspector.cpp", psa_inspector_cpp),
    ("main/savestate/savestate.cpp", savestate),
    ("main/game/pad_inmatch.cpp", pad_inmatch_cpp),
    ("main/lab.cpp", lab_cpp),
    ("main/ui/watermark.cpp", watermark),
    ("main/ui/match_hud.cpp", match_hud),
    ("main/cpu/options.cpp", cpu_options),
    ("main/game/status_names.cpp", status_names_cpp),
    ("main/storage/file_io.cpp", file_io),
    ("main/storage/recording_file.cpp", recording_file),
    ("main/storage/settings_file.cpp", settings_file),
    ("menu/bests_file.cpp", settings_file),  # its file names, never drawn
    ("menu/browser.cpp", browser),
    ("menu/drill_list.cpp", drill_list),
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
