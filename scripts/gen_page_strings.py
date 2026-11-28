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

# Load encode/decode helpers from gen_menu_descriptions.py (shared Huff+LZSS format).
_spec = importlib.util.spec_from_file_location("gen_menu_descriptions", ROOT / "scripts" / "gen_menu_descriptions.py")
_gmd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_gmd)

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
            "kEdgeSetups", "kEdgeStrengths", "kEdgeDists", "kEdgeDjs", "kEdgeMixups", "kEdgeResets", "kShieldRoles",
            "kOosActions", "kShieldDefAttacks", "kLtStarts", "kLtTimings", "kLedgetechSpawns",
            "kLedgetechAttacks", "kShieldDropPlats", "kShieldDropAttacks", "kShieldDropSpacing",
            "kShieldDropFacing", "kSlideOffStarts", "kSlideOffAttacks", "kGrabActions", "kGrabPctModes", "kMultiHitMoves",
            "kPracticeModes", "kPsAtkModes", "kPsHeights", "kPsCadences", "kLedgeSides", "kLedgeInvulns",
            "kPracticeTargets",
        ],
    )
    entries.append(("scen_names", _extract_host_option_names(scen, "kRows")))

    # 2. stage_cast.cpp: 24 label tables + 32 row names (skip first 2 head rows)
    sc = PAGES_DIR / "stage_cast.cpp"
    entries += _parse_labels_from_cpp(
        sc,
        "cast",
        [
            "kHazards", "kFodHeight", "kPlatDefault", "kPlatGeneric", "kPlatLuigisMansion",
            "kPlatDelfino", "kPlatHorizontal", "kPlatDeadLine", "kPlatRandall",
            "kTurnip", "kPeachSmash", "kMisfire", "kHammer", "kUnlimited", "kCharge2", "kCharge3",
            "kGnwBucket", "kSamusBeam", "kLucarioAura", "kRobBeam", "kLucasOffense",
            "kSheikNeedles", "kDededeWaddle", "kNanaThrow",
        ],
    )
    all_cast_names = _extract_host_option_names(sc, "kRows").split(b"\x00")[:-1]
    entries.append(("cast_names", b"".join(x + b"\x00" for x in all_cast_names[2:])))

    # 2b. olimar.cpp: 5 label tables + 22 row names
    oli = PAGES_DIR / "olimar.cpp"
    entries += _parse_labels_from_cpp(
        oli,
        "oli",
        ["kOlimarDpadMode", "kOlimarColor", "kOlimarMaturity", "kOlimarStartSlot", "kOlimarPresets"],
    )
    entries.append(("oli_names", _extract_host_option_names(oli, "kRows")))

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
            "kShfflTiming", "kShieldTiming", "kCounterModes",
            "kRevTriggers", "kRevGetup", "kRevSlot1Getup", "kRevLedge",
        ],
    )
    entries.append(("cpu_main_names", _extract_host_option_names(cpu, "kRows")))
    entries.append(("cpu_di_names", _extract_host_option_names(cpu, "kDiRows")))
    entries.append(("cpu_ctr_names", _extract_host_option_names(cpu, "kCounterRows")))
    entries.append(("cpu_def_names", _extract_host_option_names(cpu, "kDefenseRows")))

    # 4. osd_menu.cpp: 5 label tables + 8+21 row names
    osd = PAGES_DIR / "osd_menu.cpp"
    entries += _parse_labels_from_cpp(
        osd, "osd", ["kPosition", "kTimingMeter", "kOsdPresets", "kOsdSaveTargets", "kTechSparkle"]
    )
    entries.append(
        ("osd_names", _extract_host_option_names(osd, "kRows") + _extract_host_option_names(osd, "kIndivRows"))
    )

    # 4b. display_options.cpp: 5 label tables + 10+6+4+8 row names
    disp = PAGES_DIR / "display_options.cpp"
    entries += _parse_labels_from_cpp(
        disp, "disp", ["kWho", "kPsaTargets", "kFrameMeterModes", "kHitboxColors", "kDiVectors"]
    )
    entries.append((
        "disp_names",
        _extract_host_option_names(disp, "kRows")
        + _extract_host_option_names(disp, "kCollisionRows")
        + _extract_host_option_names(disp, "kHudOverlaysRows")
        + _extract_host_option_names(disp, "kDebugRows"),
    ))

    # 5. combo_trainer.cpp: 4 label tables + 15+7 row names
    ct = PAGES_DIR / "combo_trainer.cpp"
    entries += _parse_labels_from_cpp(ct, "ct", ["kResetDamageMode", "kResetDelays", "kPostEscapes", "kRecMixups"])
    entries.append(("ct_names", _extract_host_option_names(ct, "kRows") + _extract_host_option_names(ct, "kMeterRows")))

    # 6. tech_options.cpp: 6 label tables + 9+9 row names
    tech = PAGES_DIR / "tech_options.cpp"
    entries += _parse_labels_from_cpp(
        tech, "tech", ["kTechInvisLabels", "kPreTechDriftLabels", "kTech", "kGetUp", "kTechTrap", "kLockout"]
    )
    entries.append(
        ("tech_names", _extract_host_option_names(tech, "kRows") + _extract_host_option_names(tech, "kWeightRows"))
    )

    # 7. info_display.cpp (pages): kValues + kPresets + kSizes + 10 row names
    info = PAGES_DIR / "info_display.cpp"
    entries += _parse_labels_from_cpp(info, "info", ["kValues", "kPresets", "kSizes"])
    entries.append(("info_names", b"".join(x.encode("ascii") + b"\x00" for x in [
        "Preset", "Size", "Row 1", "Row 2", "Row 3", "Row 4", "Row 5", "Row 6", "Row 7", "Row 8"
    ])))

    # 8. color_overlays.cpp: kPreset + kColor + 17 row names
    ov = PAGES_DIR / "color_overlays.cpp"
    entries += _parse_labels_from_cpp(ov, "ov", ["kPreset", "kColor"])
    entries.append(("ov_names", b"".join(x.encode("ascii") + b"\x00" for x in [
        "Preset", "Actionable", "Hitstun", "Invincible", "Ledge Actionable", "Missed L-Cancel",
        "Can Fastfall", "Autocancel", "Crouch", "Wait", "Walk", "Dash", "Run", "Full Hop",
        "Short Hop", "IASA", "Shieldstun"
    ])))

    # 9. recording.cpp: 7 label tables + 9+9 row names
    rec = PAGES_DIR / "recording.cpp"
    entries += _parse_labels_from_cpp(
        rec, "rec", ["kHmnMode", "kCpuMode", "kSlot", "kMirrored", "kCpuCounter", "kAutoRestore", "kTakeover"]
    )
    entries.append(
        ("rec_names", _extract_host_option_names(rec, "kRows") + _extract_host_option_names(rec, "kManageRows"))
    )

    # 10. general.cpp: 3 label tables + 13 row names
    gen = PAGES_DIR / "general.cpp"
    entries += _parse_labels_from_cpp(gen, "gen", ["kInfiniteShields", "kCamera", "kGameSpeed"])
    entries.append(("gen_names", _extract_host_option_names(gen, "kRows")))

    # 11. controls.cpp: 8 label tables + 10 row names
    ctrl = PAGES_DIR / "controls.cpp"
    entries += _parse_labels_from_cpp(
        ctrl, "ctrl",
        ["kAdvanceButton", "kDecrementButton", "kDpadUp", "kDpadDown", "kDpadLeft", "kDpadRight", "kMenuButton", "kMenuStyle"]
    )
    entries.append(("ctrl_names", _extract_host_option_names(ctrl, "kRows")))

    # 12. theme.cpp: 6 label tables + 6 row names
    thm = PAGES_DIR / "theme.cpp"
    entries += _parse_labels_from_cpp(
        thm, "thm",
        ["kOsdBgLabels", "kOsdAlphaLabels", "kMenuAccentLabels", "kInfoBgLabels", "kInfoAlphaLabels", "kColorblindLabels"]
    )
    entries.append(("thm_names", _extract_host_option_names(thm, "kRows")))

    # 13. alter_inputs.cpp: kPressed + 16 row names
    alt = PAGES_DIR / "alter_inputs.cpp"
    entries += _parse_labels_from_cpp(alt, "alt", ["kPressed"])
    entries.append(("alt_names", _extract_host_option_names(alt, "kRows")))

    # 14. slot_management.cpp: kPlayer + kSlot + 6 row names
    sm = PAGES_DIR / "slot_management.cpp"
    entries += _parse_labels_from_cpp(sm, "sm", ["kPlayer", "kSlot"])
    entries.append(("sm_names", _extract_host_option_names(sm, "kRows")))

    # 15. slot_chances.cpp: 7 shared row names
    entries.append(("sc_names", b"".join(x.encode("ascii") + b"\x00" for x in [
        "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Random Percent"
    ])))

    # 16. advanced_counter.cpp: kLogic + 8 row names
    ac = PAGES_DIR / "advanced_counter.cpp"
    entries += _parse_labels_from_cpp(ac, "ac", ["kLogic"])
    entries.append(("ac_names", _extract_host_option_names(ac, "kRows")))

    # 17. ledgedash_hud.cpp: 4 row names
    ld = PAGES_DIR / "ledgedash_hud.cpp"
    entries.append(("ld_names", _extract_host_option_names(ld, "kRows")))

    # 18. Shared format strings and dynamic labels used by Wii builders
    entries += [
        ("fmt_d", b"%d\x00"),
        ("fmt_pd", b"%+d\x00"),
        ("fmt_df", b"%df\x00"),
        ("fmt_pdf", b"%+df\x00"),
        ("fmt_pct", b"%d%%\x00"),
        ("rev_act", b"Slot 2 Action\x00"),
        ("rev_dly", b"Slot 2 Delay\x00"),
        ("rev_wgt", b"Slot 2 Chance\x00"),
        ("rec_save", b"Save Positions\x00"),
        ("rec_restore", b"Restore Positions\x00"),
        ("pool_nothing", b"Do Nothing\x00"),
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
    comp = _gmd.encode_huff_lzss(raw)
    if _gmd.decode_huff_lzss(comp, len(raw)) != raw:
        raise RuntimeError("Huff+LZSS round-trip verification failed")

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
        "    bool pageStringsReady();",
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
        "    static const u8 kPageStringsComp[kPageStringsCompBytes] = {",
        _gmd.format_u8_array(comp),
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
        "        if (!extra::decompressHuffLzss(kPageStringsComp, s_pageStrBuf, kPageStringsRawBytes))",
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
        "    bool pageStringsReady()",
        "    {",
        "        return s_pageStrBuf != NULL;",
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
