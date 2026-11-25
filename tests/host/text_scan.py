"""Reads the text the mod can draw out of its sources, for the PC test of
whether every text fits where it is drawn (tests/host/tests/test_text_fit.cpp).

Most on-screen text is written where it is used: a tip in a call of tip(),
a HUD value in an snprintf into the HUD's buffer, a message in a call of
osd::post(). A test cannot see such a string, so this file reads the
sources: scan() lists every string literal of a source with where it
stands (the call and argument it is in, or the table it initialises), and
text_rules.py says for every one of them what it is: text drawn in a
certain place (then with its longest values filled in, for a printf-style
text), or something that is never drawn (a log line's reason, a file name).
A literal no rule knows fails the test: new text cannot go unchecked.

Nothing here knows the mod; the knowledge is in text_rules.py.
"""
import re
import zlib

KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "static_cast", "reinterpret_cast",
            "const_cast"}
ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "r": "\r", "a": "\a", "?": "?"}
TOKEN = re.compile(
    r'''(?P<ws>\s+)|(?P<lc>//[^\n]*)|(?P<bc>/\*.*?\*/)|(?P<pp>^[ \t]*\#(?:[^\n\\]|\\.)*)'''
    r'''|(?P<str>"(?:[^"\\\n]|\\.)*")|(?P<chr>'(?:[^'\\\n]|\\.)*')|(?P<id>[A-Za-z_]\w*)'''
    r'''|(?P<num>\.?\d[\w.]*)|(?P<op>::|->|.)''', re.S | re.M)
# Macros that stand for a string literal.
STRING_MACROS = {"TM_UNAVAILABLE_LABEL": "\x01", "TM_PLUGIN_NAME": "TrainingMode", "TM_VERSION_STRING": "0.0.0",
                 "TM_VERSION_MARK": "P+TM v99.99.99-pre99"}


def unescape(body):
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        c = body[i]
        if c in "01234567":
            j = i
            while j < len(body) and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(chr(int(body[i:j], 8)))
            i = j
        elif c == "x":
            j = i + 1
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i + 1:j], 16)))
            i = j
        else:
            out.append(ESCAPES[c])
            i += 1
    return "".join(out)


class Literal(object):
    """One string literal (adjacent literals are one)."""

    def __init__(self):
        self.file = ""    # path under src/
        self.line = 0
        self.text = ""
        self.call = None  # the innermost call or macro use it is an argument of (a Call), or None
        self.arg = 0      # which argument of that call, or which element of the initialiser
        self.var = ""     # the variable whose initialiser it is in, when in no call
        self.func = ""    # the function it is in
        self.stmt = ""    # the statement's text before it
        self.calls = []   # names of every enclosing call, innermost first

    @property
    def where(self):
        return "%s:%d" % (self.file, self.line)

    @property
    def name(self):
        return self.call.name if self.call else ""

    @property
    def first(self):
        """The text of the call's first argument ("" outside a call)."""
        return self.call.args[0] if self.call and self.call.args else ""


class Call(object):
    def __init__(self, name, line):
        self.name = name
        self.line = line
        self.args = []      # source text of each argument
        self.literals = []  # the literals directly in it


class Source(object):
    def __init__(self, rel, text):
        self.file = rel
        self.text = text
        self.literals = []
        self.functions = {}  # name -> checksum of its tokens (the first definition of that name)
        self.buffers = {}    # char NAME[N]: name -> N
        self.numbers = {}    # NAME = number (enumerators and constants)
        self.arrays = {}     # NAME -> the literals initialising it, in order


def scan(rel, text):
    src = Source(rel, text)
    toks = [(m.lastgroup, m.group(), m.start()) for m in TOKEN.finditer(text)
            if m.lastgroup not in ("ws", "lc", "bc", "pp")]
    n = len(toks)

    def line_of(pos):
        return text.count("\n", 0, pos) + 1

    for m in re.finditer(r"\bchar\s+(\w+)\s*\[\s*(\w+)\s*\]", text):
        src.buffers.setdefault(m.group(1), m.group(2))
    for m in re.finditer(r"\b(k\w+)\s*=\s*(-?\d+(?:\.\d+)?)f?\s*[,;}\n]", text):
        src.numbers.setdefault(m.group(1), float(m.group(2)))
    for name, size in list(src.buffers.items()):
        if size.isdigit():
            src.buffers[name] = int(size)
        elif size in src.numbers:
            src.buffers[name] = int(src.numbers[size])
        else:
            del src.buffers[name]

    stack = []  # open brackets: {"kind": "(" or "{", ...}
    last_closed = None
    func = None
    func_depth = None
    func_start = 0
    stmt_start = 0
    i = 0
    while i < n:
        kind, tok, pos = toks[i]
        if kind == "op" and tok == "(":
            name = ""
            j = i - 1
            if j >= 0 and toks[j][0] == "id":
                name = toks[j][1]
                while j >= 2 and toks[j - 1][1] == "::" and toks[j - 2][0] == "id":
                    name = toks[j - 2][1] + "::" + name
                    j -= 2
            stack.append({"kind": "(", "call": Call(name, line_of(pos)), "arg": 0, "start": pos + 1})
        elif kind == "op" and tok == ")":
            if stack and stack[-1]["kind"] == "(":
                e = stack.pop()
                e["call"].args.append(" ".join(text[e["start"]:pos].split()))
                last_closed = e["call"]
        elif kind == "op" and tok == "{":
            name = ""
            init = False
            j = i - 1
            if j >= 0 and toks[j][1] == "=":
                j -= 1
                while j >= 0 and toks[j][1] == "]":
                    depth = 1
                    j -= 1
                    while j >= 0 and depth:
                        depth += {"]": 1, "[": -1}.get(toks[j][1], 0)
                        j -= 1
                if j >= 0 and toks[j][0] == "id":
                    name = toks[j][1]
                init = True
            elif stack and stack[-1]["kind"] == "{" and stack[-1]["init"]:
                init = True
                name = stack[-1]["name"]
            elif (j >= 0 and (toks[j][1] == ")" or (toks[j][1] == "const" and j >= 1 and toks[j - 1][1] == ")"))
                  and last_closed is not None and last_closed.name not in KEYWORDS and func is None):
                func = last_closed.name
                func_depth = len(stack)
                func_start = i
            stack.append({"kind": "{", "name": name, "init": init, "arg": 0})
            stmt_start = i + 1
        elif kind == "op" and tok == "}":
            if stack and stack[-1]["kind"] == "{":
                stack.pop()
                if func is not None and len(stack) == func_depth:
                    body = " ".join(t[1] for t in toks[func_start:i + 1])
                    src.functions.setdefault(func, "%08x" % (zlib.crc32(body.encode("utf-8")) & 0xFFFFFFFF))
                    func = None
            stmt_start = i + 1
        elif kind == "op" and tok == ";":
            if not (stack and stack[-1]["kind"] == "("):
                stmt_start = i + 1
        elif kind == "op" and tok == ",":
            if stack:
                e = stack[-1]
                if e["kind"] == "(":
                    e["call"].args.append(" ".join(text[e["start"]:pos].split()))
                    e["start"] = pos + 1
                e["arg"] += 1
        elif kind == "str" or (kind == "id" and tok in STRING_MACROS and (
                (i + 1 < n and toks[i + 1][0] == "str") or (i >= 1 and toks[i - 1][0] == "str"))):
            lit = Literal()
            j = i
            while j < n and (toks[j][0] == "str" or (toks[j][0] == "id" and toks[j][1] in STRING_MACROS)):
                lit.text += STRING_MACROS[toks[j][1]] if toks[j][0] == "id" else unescape(toks[j][1][1:-1])
                j += 1
            lit.file = rel
            lit.line = line_of(pos)
            lit.func = func or ""
            lit.stmt = " ".join(text[toks[stmt_start][2]:pos].split()) if stmt_start < i else ""
            for e in reversed(stack):
                if e["kind"] == "(" and e["call"].name and e["call"].name not in KEYWORDS:
                    lit.calls.append(e["call"].name)
                    if lit.call is None and not lit.var:
                        lit.call = e["call"]
                        lit.arg = e["arg"]
                        e["call"].literals.append(lit)
                elif e["kind"] == "{" and e["init"] and lit.call is None and not lit.var:
                    lit.var = e["name"]
                    lit.arg = e["arg"]
                    src.arrays.setdefault(e["name"], []).append(lit)
            src.literals.append(lit)
            i = j
            continue
        i += 1
    return src


# ---- printf-style texts ------------------------------------------------------

CONVERSION = re.compile(r"%(?P<flags>[-+ 0#]*)(?P<width>\d+)?(?:\.(?P<precision>\d+))?(?P<length>[hlLzjt]*)"
                        r"(?P<type>[diuxXfFeEgGsc%])")


def conversions(fmt):
    return [m for m in CONVERSION.finditer(fmt) if m.group("type") != "%"]


def widest_number(m, digits=3):
    """The longest text a numeric conversion is taken to give: `digits`
    digits before the point (a zero-padded field: its width)."""
    flags = m.group("flags")
    kind = m.group("type")
    width = int(m.group("width") or 0)
    if kind == "c":
        return "W"
    if kind in "xX":
        body = ("f" if kind == "x" else "F") * max(digits + 1, width)
    else:
        body = "9" * (width if "0" in flags and width else digits)
        if kind in "fFeEgG":
            precision = int(m.group("precision")) if m.group("precision") is not None else 6
            body = "9" * digits + ("." + "9" * precision if precision else "")
    sign = "+" if "+" in flags else ""
    return (sign + body).rjust(width)


def render(fmt, values):
    """`fmt` with the texts in `values` put in for its conversions."""
    out = []
    at = 0
    k = 0
    for m in CONVERSION.finditer(fmt):
        out.append(fmt[at:m.start()])
        at = m.end()
        if m.group("type") == "%":
            out.append("%")
            continue
        value = values[k]
        k += 1
        if m.group("type") == "s" and m.group("precision") is not None:
            value = value[:int(m.group("precision"))]
        out.append(value)
    out.append(fmt[at:])
    return "".join(out)
