#!/usr/bin/env python3
"""Generates compressed Wii page label and row-name strings (LZ77 ASCII).

Single source of truth: parses the host-side TM_LABELS_* tables and row-name
lists directly from src/main/ui/pages/*.cpp so Wii compressed strings and host
test tables can never drift. Run with --check in check-host.
"""
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGES_DIR = ROOT / "src" / "main" / "ui" / "pages"
OUT_H = PAGES_DIR / "page_strings.h"
OUT_CPP = PAGES_DIR / "page_strings.cpp"

# Load encode/decode helpers from gen_status_names.py (shared bit-7 LZ77 format).
_spec = importlib.util.spec_from_file_location("gen_status_names", ROOT / "scripts" / "gen_status_names.py")
_gsn = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_gsn)

# Macro substitutions used inside TM_L(...) on Wii (__MWERKS__).
WII_MACROS = {
    "NA": "\x01",
    "TM_UNAVAILABLE_LABEL": "\x01",
    "DROPPED": "\x02",
    "TM_DROPPED_LABEL": "\x02",
    "DJC_M": "\x03",
    "TM_DJC_LABEL": "\x03",
    "FLT_M": "\x04",
    "TM_FLOAT_LABEL": "\x04",
}

CPU_EXPANSIONS = {
    "TM_PLAY_SLOT_LABELS_P": (
        'TM_L("Slot 1") TM_L("Slot 2") TM_L("Slot 3") TM_L("Slot 4") '
        'TM_L("Slot 5") TM_L("Slot 6") TM_L("Random Slot")'
    ),
    "TM_DJC_LABELS_P": (
        'TM_L(DJC_M "DJC Nair") TM_L(DJC_M "DJC Fair") TM_L(DJC_M "DJC Bair") '
        'TM_L(DJC_M "DJC Uair") TM_L(DJC_M "DJC Dair")'
    ),
    "TM_FLOAT_LABELS_P": (
        'TM_L(FLT_M "Float Nair") TM_L(FLT_M "Float Fair") TM_L(FLT_M "Float Bair") '
        'TM_L(FLT_M "Float Uair") TM_L(FLT_M "Float Dair")'
    ),
    "TM_SHFFL_LABELS_P": (
        'TM_L("SHFFL Nair") TM_L("SHFFL Fair") TM_L("SHFFL Bair") '
        'TM_L("SHFFL Uair") TM_L("SHFFL Dair")'
    ),
    "TM_FFL_LABELS_P": (
        'TM_L("FFL Nair") TM_L("FFL Fair") TM_L("FFL Bair") '
        'TM_L("FFL Uair") TM_L("FFL Dair")'
    ),
}


def _eval_tm_l_arg(expr: str) -> str:
    """Evaluate a C string-concat expression inside TM_L(...)."""
    out = []
    pos = 0
    n = len(expr)
    while pos < n:
        if expr[pos].isspace():
            pos += 1
            continue
        if expr[pos] == '"':
            end = pos + 1
            while end < n and expr[end] != '"':
                if expr[end] == "\\" and end + 1 < n:
                    end += 2
                else:
                    end += 1
            raw = expr[pos + 1 : end]
            raw = raw.replace(r"\"", '"').replace(r"\\", "\\")
            out.append(raw)
            pos = end + 1
            continue
        m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", expr[pos:])
        if m:
            tok = m.group(0)
            if tok not in WII_MACROS:
                raise ValueError(f"Unknown macro token {tok!r} in TM_L({expr!r})")
            out.append(WII_MACROS[tok])
            pos += len(tok)
            continue
        raise ValueError(f"Unexpected char {expr[pos]!r} in TM_L({expr!r})")
    return "".join(out)


def _extract_tm_l_block(body: str) -> bytes:
    for k, v in CPU_EXPANSIONS.items():
        body = body.replace(k, v)
    parts = [_eval_tm_l_arg(m.group(1)) for m in re.finditer(r"TM_L\(([^)]+)\)", body)]
    if not parts:
        raise ValueError("Empty TM_L block")
    return b"".join(p.encode("ascii") + b"\x00" for p in parts)


def _extract_c_strings(body: str) -> bytes:
    """Extract a list of C string literals (with optional NA prefix) from an array body."""
    items = []
    for chunk in body.split(","):
        c = chunk.strip()
        if not c:
            continue
        items.append(_eval_tm_l_arg(c))
    if not items:
        raise ValueError("Empty C string array")
    return b"".join(p.encode("ascii") + b"\x00" for p in items)


def _parse_labels_from_cpp(path: pathlib.Path, prefix: str, names: list[str]) -> list[tuple[str, bytes]]:
    src = path.read_text(encoding="utf-8")
    out = []
    for name in names:
        # Match TM_LABELS_BEGIN(name) ... TM_LABELS_END or TM_LABELS_EXTERN_BEGIN(name, ...) ... TM_LABELS_END
        pat = (
            r"TM_LABELS_(?:EXTERN_)?BEGIN\s*\(\s*"
            + re.escape(name)
            + r"(?:\s*,[^)]+)?\s*\)(.*?)TM_LABELS_END"
        )
        m = re.search(pat, src, re.S)
        if m:
            out.append((f"{prefix}_{name}", _extract_tm_l_block(m.group(1))))
            continue
        # Match extern const char* name[...] = { ... }; or static const char* const name[...] = { ... };
        pat2 = r"(?:extern|static)\s+const\s+char\s*\*(?:\s*const)?\s+" + re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\};"
        m2 = re.search(pat2, src, re.S)
        if m2:
            body = m2.group(1)
            if "TM_L(" in body:
                out.append((f"{prefix}_{name}", _extract_tm_l_block(body)))
            else:
                out.append((f"{prefix}_{name}", _extract_c_strings(body)))
            continue
        # Match static const char name[] = TM_L(...) ... ;
        pat3 = r"static\s+const\s+char\s+" + re.escape(name) + r"\s*\[\s*\]\s*=\s*(.*?);"
        m3 = re.search(pat3, src, re.S)
        if m3:
            out.append((f"{prefix}_{name}", _extract_tm_l_block(m3.group(1))))
            continue
        raise ValueError(f"Could not find label table {name} in {path}")
    return out


def _extract_host_option_names(path: pathlib.Path, array_name: str) -> bytes:
    """Extract row names from the host static const Option array_name[] = { ... };."""
    src = path.read_text(encoding="utf-8")
    m = re.search(
        r"static\s+(?:const\s+)?Option\s+" + re.escape(array_name) + r"\s*\[\s*\]\s*=\s*\{(.*?)\n    \};",
        src,
        re.S,
    )
    if not m:
        raise ValueError(f"Could not find Option {array_name}[] in {path}")
    body = m.group(1)
    # Strip line comments
    body = re.sub(r"//[^\n]*", "", body)
    names = []
    # Match macro calls and extract the row display name string literal
    # Patterns:
    #  - TM_ROW*(..., "Name", ...) -> first string literal that is NOT a printf format ("%...")
    #  - TM_MENU_IF("Name", ...), TM_INT("Name", ...), TM_TOGGLE("Name", ...), TM_CHOICE_P("Name", ...)
    #  - TM_CPU_*("Name", ...), TM_SCEN_CHOICE("Name", ...), TM_PRAC_*("Name", ...)
    #  - TM_SROW_P(..., "Name", ...), TM_FROW(..., "Name", ...)
    #  - TM_COMBO_*("Name", ...), TM_CHANCE("Name", ...), TM_ADV_*("Name", ...)
    #  - TM_COUNTER_CHOICE("Name", ...), TM_REV_*(...)
    # Split top-level entries by scanning balanced parens at depth 0
    entries = []
    cur = []
    depth = 0
    in_str = False
    i = 0
    while i < len(body):
        ch = body[i]
        if in_str:
            cur.append(ch)
            if ch == "\\" and i + 1 < len(body):
                i += 1
                cur.append(body[i])
            elif ch == '"':
                in_str = False
        else:
            if ch == '"':
                in_str = True
                cur.append(ch)
            elif ch == "(":
                depth += 1
                cur.append(ch)
            elif ch == ")":
                depth -= 1
                cur.append(ch)
            elif ch == "," and depth == 0:
                s = "".join(cur).strip()
                if s:
                    entries.append(s)
                cur = []
            else:
                cur.append(ch)
        i += 1
    s = "".join(cur).strip()
    if s:
        entries.append(s)

    for e in entries:
        lits = re.findall(r'"([^"]*)"', e)
        if not lits:
            raise ValueError(f"No string literal in Option entry {e!r} in {path}")
        names.append(lits[0])
    return b"".join(nm.encode("ascii") + b"\x00" for nm in names)


def collect_entries() -> list[tuple[str, bytes]]:
    entries: list[tuple[str, bytes]] = []

    # 1. scenarios.cpp: 33 local label tables + 45 row names
    scen = PAGES_DIR / "scenarios.cpp"
    entries += _parse_labels_from_cpp(
        scen,
        "scen",
        [
            "kPresets", "kSides", "kTechSetups", "kTechPositions", "kTechEscapes", "kTechTdis",
            "kEdgeSetups", "kEdgeStrengths", "kEdgeDists", "kEdgeDjs", "kEdgeMixups", "kShieldRoles",
            "kOosActions", "kShieldDefAttacks", "kLtStarts", "kLtTimings", "kLedgetechSpawns",
            "kLedgetechAttacks", "kShieldDropPlats", "kShieldDropAttacks", "kShieldDropSpacing",
            "kShieldDropFacing", "kSlideOffAttacks", "kGrabActions", "kGrabPctModes", "kMultiHitMoves",
            "kPracticeModes", "kPsAtkModes", "kPsHeights", "kPsCadences", "kLedgeSides", "kLedgeInvulns",
            "kPracticeTargets",
        ],
    )
    entries.append(("scen_names", _extract_host_option_names(scen, "kRows")))

    # 2. stage_cast.cpp: 25 label tables + 34 row names (skip first 2 head rows for kCastNames)
    sc = PAGES_DIR / "stage_cast.cpp"
    entries += _parse_labels_from_cpp(
        sc,
        "cast",
        [
            "kHazards", "kFodHeight", "kPlatDefault", "kPlatGeneric", "kPlatLuigisMansion",
            "kPlatDelfino", "kPlatHorizontal", "kPlatDeadLine", "kPlatRandall",
            "kTurnip", "kPeachSmash", "kMisfire", "kHammer", "kUnlimited", "kCharge2", "kCharge3",
            "kGnwBucket", "kSamusBeam", "kLucarioAura", "kRobBeam", "kLucasOffense",
            "kSheikNeedles", "kDededeWaddle", "kNanaThrow", "kOlimarPikmin",
        ],
    )
    all_cast_names = _extract_host_option_names(sc, "kRows").split(b"\x00")[:-1]
    entries.append(("cast_names", b"".join(x + b"\x00" for x in all_cast_names[2:])))

    # 3. cpu_options.cpp: 21 label tables
    cpu = PAGES_DIR / "cpu_options.cpp"
    entries += _parse_labels_from_cpp(
        cpu,
        "cpu",
        [
            "kControl", "kTdiLabels", "kSdiDirection", "kAsdi", "kBehavior",
            "kCounterGround", "kCounterAir", "kCounterShield",
            "kInfiniteShields", "kShieldAngle", "kGrabEscape", "kGrabRelease",
            "kControlledBy", "kRecovery", "kLedgeOption", "kAdaptiveMode",
            "kShfflTiming", "kShieldTiming",
            "kRevTriggers", "kRevGetup", "kRevSlot1Getup", "kRevLedge", "kPoolCats",
        ],
    )
    entries.append(("cpu_main_names", _extract_host_option_names(cpu, "kRows")))
    entries.append(("cpu_di_names", _extract_host_option_names(cpu, "kDiRows")))
    entries.append(("cpu_ctr_names", _extract_host_option_names(cpu, "kCounterRows")))
    entries.append(("cpu_def_names", _extract_host_option_names(cpu, "kDefenseRows")))

    # 4. osd_menu.cpp: 4 label tables + 19 toggle names
    osd = PAGES_DIR / "osd_menu.cpp"
    entries += _parse_labels_from_cpp(osd, "osd", ["kPosition", "kTimingMeter", "kOsdPresets", "kTechSparkle"])
    osd_names = _extract_host_option_names(osd, "kRows").split(b"\x00")[:-1]
    # 19 toggles = rows 6..11 (6) + rows 13..25 (13) (skipping row 12 Advantage Tolerance)
    osd_toggles = osd_names[6:12] + osd_names[13:26]
    entries.append(("osd_toggle_names", b"".join(x + b"\x00" for x in osd_toggles)))

    # 5. combo_trainer.cpp: 4 label tables + 16+7 row names
    ct = PAGES_DIR / "combo_trainer.cpp"
    entries += _parse_labels_from_cpp(ct, "ct", ["kResetDamageMode", "kResetDelays", "kPostEscapes", "kRecMixups"])
    entries.append(("ct_names", _extract_host_option_names(ct, "kRows") + _extract_host_option_names(ct, "kMeterRows")))

    # 6. tech_options.cpp: 6 label tables + 17 row names
    tech = PAGES_DIR / "tech_options.cpp"
    entries += _parse_labels_from_cpp(
        tech, "tech", ["kTechInvisLabels", "kPreTechDriftLabels", "kTech", "kGetUp", "kTechTrap", "kLockout"]
    )
    entries.append(("tech_names", _extract_host_option_names(tech, "kRows")))

    # 7. info_display.cpp (pages): kValues + kPresets + kSizes
    info = PAGES_DIR / "info_display.cpp"
    entries += _parse_labels_from_cpp(info, "info", ["kValues", "kPresets", "kSizes"])

    # 8. color_overlays.cpp: kPreset + kColor
    ov = PAGES_DIR / "color_overlays.cpp"
    entries += _parse_labels_from_cpp(ov, "ov", ["kPreset", "kColor"])

    # 9. Shared format strings and Reversal Slot 2 labels used by Wii dynamic builders
    entries += [
        ("fmt_df", b"%df\x00"),
        ("fmt_pct", b"%d%%\x00"),
        ("rev_act", b"Slot 2 Action\x00"),
        ("rev_dly", b"Slot 2 Delay\x00"),
        ("rev_wgt", b"Slot 2 Chance\x00"),
    ]

    return entries


def generate() -> tuple[str, str, int, int]:
    entries = collect_entries()
    blob = bytearray()
    offsets: list[tuple[str, int]] = []
    for sym, data in entries:
        offsets.append((sym, len(blob)))
        blob.extend(data)
    raw = bytes(blob)
    comp = _gsn.encode_lz77_ascii(raw)
    if _gsn.decode_lz77_ascii(comp) != raw:
        raise RuntimeError("LZ77 ASCII round-trip verification failed")

    # Header
    h_lines = [
        "// Generated by scripts/gen_page_strings.py - do not edit by hand.",
        "#pragma once",
        "",
        "#include <types.h>",
        "",
        "#ifdef __MWERKS__",
        "namespace tm {",
        "namespace ui {",
        "namespace pages {",
        "    enum {",
        f"        kPageStringsRawBytes = {len(raw)},",
        f"        kPageStringsCompBytes = {len(comp)}",
        "    };",
        "",
        "    enum PageStrOff {",
    ]
    for i, (sym, off) in enumerate(offsets):
        comma = "," if i + 1 < len(offsets) else ""
        h_lines.append(f"        kPs_{sym} = {off}{comma}")
    h_lines += [
        "    };",
        "",
        "    void pageStringsAttach();",
        "    void pageStringsDetach();",
        "    const char* pageStr(u16 offset);",
        "}",
        "}",
        "}",
        "#endif",
        "",
    ]

    # Source
    cpp_lines = [
        "// Generated by scripts/gen_page_strings.py - do not edit by hand.",
        '#include "main/ui/pages/page_strings.h"',
        "",
        "#ifdef __MWERKS__",
        '#include "extra/lz77_ascii.h"',
        '#include "shared/game/heap.h"',
        "",
        "namespace tm {",
        "namespace ui {",
        "namespace pages {",
        f"    // Compressed lab menu page strings ({len(comp)} B vs {len(raw)} B raw).",
        "    static const u8 kPageStringsLz77[kPageStringsCompBytes] = {",
        _gsn.format_u8_array(comp),
        "    };",
        "",
        "    static char* s_pageStrBuf = NULL;",
        "",
        "    void pageStringsAttach()",
        "    {",
        "        if (s_pageStrBuf != NULL)",
        "        {",
        "            return;",
        "        }",
        "        s_pageStrBuf = static_cast<char*>(game::allocNetworkZero(kPageStringsRawBytes));",
        "        if (s_pageStrBuf == NULL)",
        "        {",
        "            return;",
        "        }",
        "        u32 n = extra::decompressLz77Ascii(kPageStringsLz77, kPageStringsCompBytes,",
        "                                           s_pageStrBuf, kPageStringsRawBytes);",
        "        if (n != kPageStringsRawBytes)",
        "        {",
        "            pageStringsDetach();",
        "        }",
        "    }",
        "",
        "    void pageStringsDetach()",
        "    {",
        "        game::freeNetworkSlot(reinterpret_cast<void**>(&s_pageStrBuf));",
        "    }",
        "",
        "    const char* pageStr(u16 offset)",
        "    {",
        "        static const char kEmpty[1] = {0};",
        "        return s_pageStrBuf != NULL ? (s_pageStrBuf + offset) : kEmpty;",
        "    }",
        "}",
        "}",
        "}",
        "#endif",
        "",
    ]
    return "\n".join(h_lines), "\n".join(cpp_lines), len(raw), len(comp)


def main() -> int:
    check = "--check" in sys.argv[1:]
    h_text, cpp_text, raw_n, comp_n = generate()
    if check:
        ok = (
            OUT_H.exists()
            and OUT_CPP.exists()
            and OUT_H.read_text(encoding="utf-8") == h_text
            and OUT_CPP.read_text(encoding="utf-8") == cpp_text
        )
        if not ok:
            print("page_strings: OUT OF DATE - run py -3.10 scripts/gen_page_strings.py", file=sys.stderr)
            return 1
        print(f"page_strings: up to date ({comp_n} B compressed / {raw_n} B raw)")
        return 0
    OUT_H.write_text(h_text, encoding="utf-8", newline="\n")
    OUT_CPP.write_text(cpp_text, encoding="utf-8", newline="\n")
    print(f"page_strings: wrote {OUT_H.name} & {OUT_CPP.name} ({comp_n} B compressed / {raw_n} B raw, saves {raw_n - comp_n} B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
