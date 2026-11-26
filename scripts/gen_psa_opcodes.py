#!/usr/bin/env python3
"""Generate LZ77-compressed PSA opcode/module name tables (src/extra/psa_opcodes.cpp).

Opcode and module names are derived from fudgepop01's Brawl/P+ AnimCmd
specification (credited to fudgepop01; see CREDITS.md).

Usage:
  python scripts/gen_psa_opcodes.py          # regenerate src/extra/psa_opcodes.cpp
  python scripts/gen_psa_opcodes.py --check  # verify generated file is up to date
"""
import importlib.util
import pathlib
import sys

_gmd_spec = importlib.util.spec_from_file_location(
    "gen_menu_descriptions", pathlib.Path(__file__).resolve().parent / "gen_menu_descriptions.py"
)
_gmd = importlib.util.module_from_spec(_gmd_spec)
_gmd_spec.loader.exec_module(_gmd)

# All 41 module prefixes from fudgepop01's AnimCmd spec (0x00..0x23, 0x64..0x6A).
# Short readable prefixes used as fallback "<prefix>:<code:02X>" when a command
# code is not in the named opcode table.
MODULES = [
    (0x00, "flow"),
    (0x01, "loop"),
    (0x02, "status"),
    (0x03, "model"),
    (0x04, "motion"),
    (0x05, "posture"),
    (0x06, "coll"),
    (0x07, "ctrl"),
    (0x08, "ground"),
    (0x09, "sit"),
    (0x0A, "sfx"),
    (0x0B, "vis"),
    (0x0C, "ft"),
    (0x0D, "acmd"),
    (0x0E, "kin"),
    (0x0F, "link"),
    (0x10, "art"),
    (0x11, "gfx"),
    (0x12, "work"),
    (0x13, "combo"),
    (0x14, "area"),
    (0x15, "terr"),
    (0x16, "srch"),
    (0x17, "phys"),
    (0x18, "slope"),
    (0x19, "shdw"),
    (0x1A, "cam"),
    (0x1B, "proc"),
    (0x1C, "stop"),
    (0x1D, "shake"),
    (0x1E, "dmg"),
    (0x1F, "item"),
    (0x20, "turn"),
    (0x21, "blend"),
    (0x22, "team"),
    (0x23, "slow"),
    (0x64, "cancel"),
    (0x65, "sse65"),
    (0x66, "sse66"),
    (0x69, "sse69"),
    (0x6A, "sse6a"),
]

# High-value PSA opcodes keyed by (module, code) from fudgepop01's spec.
# Names are concise PascalCase to fit the PSA Debugger card cleanly while
# remaining immediately recognizable to Brawl/P+ PSA modders.
OPCODES = [
    # 0x00: control_flow
    (0x00, 0x01, "SyncWait"),
    (0x00, 0x02, "AsyncWait"),
    (0x00, 0x04, "SetLoop"),
    (0x00, 0x05, "ExecLoop"),
    (0x00, 0x06, "LoopBreak"),
    (0x00, 0x07, "SubRout"),
    (0x00, 0x08, "Return"),
    (0x00, 0x09, "Goto"),
    (0x00, 0x0A, "If"),
    (0x00, 0x0B, "And"),
    (0x00, 0x0C, "Or"),
    (0x00, 0x0D, "ElseIf"),
    (0x00, 0x0E, "Else"),
    (0x00, 0x0F, "EndIf"),
    (0x00, 0x10, "Switch"),
    (0x00, 0x11, "Case"),
    (0x00, 0x12, "Default"),
    (0x00, 0x13, "EndSwitch"),
    (0x00, 0x18, "Break"),
    # 0x01: control_flow_loop
    (0x01, 0x00, "LoopRest1"),
    (0x01, 0x01, "LoadRest"),
    # 0x02: status
    (0x02, 0x00, "ChgActSt"),
    (0x02, 0x01, "ChgAct"),
    (0x02, 0x04, "AddReq"),
    (0x02, 0x05, "AddIntReq"),
    (0x02, 0x06, "EnActSt"),
    (0x02, 0x08, "DisActSt"),
    (0x02, 0x0A, "AllowSpec"),
    (0x02, 0x0B, "PrevSpec"),
    (0x02, 0x0C, "ClrPrevInt"),
    # 0x04: motion
    (0x04, 0x00, "ChgSubAct"),
    (0x04, 0x02, "SetReq"),
    (0x04, 0x03, "SetExReq"),
    (0x04, 0x06, "SetAnimF"),
    (0x04, 0x07, "SetFSM"),
    (0x04, 0x08, "BoneMot"),
    (0x04, 0x0C, "RevSubAct"),
    (0x04, 0x14, "SetTimerF"),
    # 0x05: posture
    (0x05, 0x00, "RevDir"),
    (0x05, 0x01, "FaceLeft"),
    (0x05, 0x02, "FaceRight"),
    (0x05, 0x03, "SetDir"),
    (0x05, 0x04, "DecideDir"),
    # 0x06: collision
    (0x06, 0x00, "Hitbox"),
    (0x06, 0x01, "HitDmg"),
    (0x06, 0x02, "HitSize"),
    (0x06, 0x03, "DelHitbox"),
    (0x06, 0x04, "TermHits"),
    (0x06, 0x05, "BodyColl"),
    (0x06, 0x06, "UndoBoneC"),
    (0x06, 0x08, "BoneColl"),
    (0x06, 0x0A, "CatchBox"),
    (0x06, 0x0C, "DelCatch"),
    (0x06, 0x0D, "TermCatch"),
    (0x06, 0x0E, "ThrowAtk"),
    (0x06, 0x0F, "ThrowColl"),
    (0x06, 0x10, "InertBox"),
    (0x06, 0x14, "AddHitDmg"),
    (0x06, 0x15, "SpecHit"),
    (0x06, 0x17, "DefColl"),
    (0x06, 0x18, "DelDefC"),
    (0x06, 0x1B, "MoveHit"),
    (0x06, 0x2B, "ThrownHit"),
    (0x06, 0x2C, "CollatHit"),
    # 0x07: controller
    (0x07, 0x00, "RstFlkX"),
    (0x07, 0x01, "RstFlkY"),
    (0x07, 0x02, "RstBtn"),
    (0x07, 0x07, "Rumble"),
    (0x07, 0x0C, "ClrBuf"),
    # 0x08: ground
    (0x08, 0x00, "SetAirGnd"),
    (0x08, 0x01, "CliffChk"),
    (0x08, 0x04, "PassChk"),
    (0x08, 0x07, "PassFloor"),
    (0x08, 0x08, "AttachGnd"),
    (0x08, 0x0A, "GndColl"),
    (0x08, 0x16, "InitECB"),
    # 0x0A: sound
    (0x0A, 0x00, "Sfx"),
    (0x0A, 0x01, "StepSfx"),
    (0x0A, 0x02, "TransSfx"),
    (0x0A, 0x03, "StopSfx"),
    (0x0A, 0x09, "LandSfx"),
    (0x0A, 0x0A, "ImpactSfx"),
    # 0x0B: visibility
    (0x0B, 0x00, "ModelChg"),
    (0x0B, 0x01, "ModelPerm"),
    (0x0B, 0x02, "SetVis"),
    # 0x0C: ft_specific
    (0x0C, 0x04, "SetAerial"),
    (0x0C, 0x05, "TermArt"),
    (0x0C, 0x06, "EnterFS"),
    (0x0C, 0x09, "LedgeGrab"),
    (0x0C, 0x19, "DmgSfx"),
    (0x0C, 0x1C, "StaleId"),
    (0x0C, 0x23, "SlowEnemy"),
    (0x0C, 0x2B, "RstCombo"),
    # 0x0D: anim_cmd
    (0x0D, 0x00, "ConcLoop"),
    (0x0D, 0x01, "EndConc"),
    # 0x0E: kinetic
    (0x0E, 0x00, "KinAirGnd"),
    (0x0E, 0x01, "AddMom"),
    (0x0E, 0x02, "ResetMom"),
    (0x0E, 0x04, "EnMove"),
    (0x0E, 0x05, "DisMove"),
    (0x0E, 0x06, "DenyMove"),
    (0x0E, 0x07, "AllowMove"),
    (0x0E, 0x08, "SetMom"),
    # 0x10: generate_article_manage
    (0x10, 0x00, "GenArt"),
    (0x10, 0x03, "RemArt"),
    (0x10, 0x04, "ArtAct"),
    (0x10, 0x05, "ArtVis"),
    (0x10, 0x0A, "LinkArt"),
    (0x10, 0x0C, "ArtFSM"),
    # 0x11: effect
    (0x11, 0x00, "GfxEff"),
    (0x11, 0x01, "GfxAttach"),
    (0x11, 0x03, "SwordGlow"),
    (0x11, 0x05, "EndGlow"),
    (0x11, 0x14, "TermAllGfx"),
    (0x11, 0x15, "TermGfx"),
    (0x11, 0x1A, "StepGfx"),
    (0x11, 0x1B, "LandGfx"),
    (0x11, 0x1E, "FlashOn"),
    (0x11, 0x1F, "FlashOff"),
    # 0x12: work_manage
    (0x12, 0x00, "IntSet"),
    (0x12, 0x01, "IntAdd"),
    (0x12, 0x02, "IntSub"),
    (0x12, 0x03, "IntInc"),
    (0x12, 0x04, "IntDec"),
    (0x12, 0x05, "IntRand"),
    (0x12, 0x06, "FltSet"),
    (0x12, 0x07, "FltAdd"),
    (0x12, 0x08, "FltSub"),
    (0x12, 0x0A, "BitSet"),
    (0x12, 0x0B, "BitClear"),
    (0x12, 0x0D, "IntMul"),
    (0x12, 0x0F, "FltMul"),
    # 0x1A: camera
    (0x1A, 0x00, "ScrShake"),
    # 0x1C: ft_stop
    (0x1C, 0x00, "SetHitlag"),
    (0x1C, 0x01, "ForceLag"),
    # 0x1E: damage
    (0x1E, 0x00, "Armor"),
    (0x1E, 0x03, "AddDmg"),
    # 0x1F: item_manage
    (0x1F, 0x01, "ThrowItem"),
    (0x1F, 0x03, "UseItem"),
    (0x1F, 0x06, "FireProj"),
    (0x1F, 0x08, "GenItem"),
    (0x1F, 0x0A, "DelItem"),
    # 0x21: color_blend
    (0x21, 0x00, "EndFlash"),
    (0x21, 0x01, "FlashOv"),
    (0x21, 0x02, "FlashCol"),
    # 0x64: ft_cancel
    (0x64, 0x00, "AllowInt"),
    (0x64, 0x01, "DenyInt"),
]


def encode_lz77_ascii(raw):
    from collections import defaultdict
    for b in raw:
        if b >= 0x80:
            raise ValueError(f"Non-ASCII byte {b:#x} in PSA opcode table")
    n = len(raw)
    trigrams = defaultdict(list)
    matches = [[] for _ in range(n)]
    for i in range(n - 2):
        tg = raw[i:i + 3]
        lst = trigrams[tg]
        best_short = {}
        best_long = {}
        limit = min(66, n - i)
        for p in reversed(lst):
            dist = i - p
            if dist > 65536:
                break
            l = 3
            while l < limit and raw[p + l] == raw[i + l]:
                l += 1
            if dist <= 2048:
                l2 = min(l, 10)
                if l2 not in best_short:
                    best_short[l2] = dist
            if l not in best_long:
                best_long[l] = dist
        for l, d in best_short.items():
            matches[i].append((2, l, d))
        for l, d in best_long.items():
            matches[i].append((3, l, d))
        lst.append(i)
    dp = [10**9] * (n + 1)
    choice = [None] * n
    dp[n] = 0
    for i in range(n - 1, -1, -1):
        dp[i] = 1 + dp[i + 1]
        choice[i] = (1, 1, raw[i])
        for cost, l, d in matches[i]:
            max_ll = 10 if cost == 2 else l
            for ll in range(3, min(l, max_ll) + 1):
                c = cost + dp[i + ll]
                if c < dp[i]:
                    dp[i] = c
                    choice[i] = (cost, ll, d)
    out = bytearray()
    i = 0
    while i < n:
        cost, l, d = choice[i]
        if cost == 1:
            out.append(d)
            i += 1
        elif cost == 2:
            dm = d - 1
            out.append(0x80 | ((l - 3) << 3) | (dm >> 8))
            out.append(dm & 0xFF)
            i += l
        else:
            dm = d - 1
            out.append(0xC0 | (l - 3))
            out.append((dm >> 8) & 0xFF)
            out.append(dm & 0xFF)
            i += l
    return bytes(out)


def decode_lz77_ascii(comp):
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


def format_array(values, fmt, indent="        ", per_line=16):
    rows = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        rows.append(indent + ", ".join(fmt.format(v) for v in chunk) + ",")
    if rows:
        rows[-1] = rows[-1].rstrip(",")
    return "\n".join(rows)


def generate_cpp():
    mods = sorted(MODULES, key=lambda x: x[0])
    ops = sorted(OPCODES, key=lambda x: (x[0] << 8) | x[1])
    seen_mods = set()
    for mid, name in mods:
        if mid in seen_mods:
            raise ValueError(f"Duplicate module id {mid:#x}")
        seen_mods.add(mid)
    seen_ops = set()
    for mid, code, name in ops:
        key = (mid << 8) | code
        if key in seen_ops:
            raise ValueError(f"Duplicate opcode {mid:#04x}:{code:#04x}")
        seen_ops.add(key)

    strings = [name for _, name in mods] + [name for _, _, name in ops]
    for s in strings:
        for ch in s:
            if ord(ch) < 0x20 or ord(ch) >= 0x7F:
                raise ValueError(f"Non-printable ASCII in {s!r}")

    # Expect contiguous 0x00..0x23 followed by the 5 high SSE/cancel module IDs.
    low_count = sum(1 for mid, _ in mods if mid <= 0x23)
    if [mid for mid, _ in mods[:low_count]] != list(range(low_count)):
        raise ValueError("Expected contiguous low module IDs 0x00..0x23")
    hi_mods = [mid for mid, _ in mods[low_count:]]

    # Pack (mod, count) spans + u8 opcode codes right into the LZ77 stream
    # (every byte is < 0x80) before the NUL-terminated names.
    op_spans = []
    op_codes = []
    for mid, code, _ in ops:
        op_codes.append(code)
        if not op_spans or op_spans[-1][0] != mid:
            op_spans.append([mid, 1])
        else:
            op_spans[-1][1] += 1
    span_bytes = bytes(b for pair in op_spans for b in pair)
    code_bytes = bytes(op_codes)
    str_raw = b"".join(s.encode("ascii") + b"\x00" for s in strings)
    raw = span_bytes + code_bytes + str_raw
    comp = _gmd.encode_huff_lzss(raw)
    if _gmd.decode_huff_lzss(comp, len(raw)) != raw:
        raise AssertionError("Huff+LZSS round-trip mismatch in PSA opcode table")

    cpp = f"""// Generated by scripts/gen_psa_opcodes.py - edit opcode tables there.
// PSA AnimCmd module prefixes and opcode names adapted from fudgepop01's
// Brawl/P+ AnimCmd specification (used with permission; see CREDITS.md).
#include "extra/psa_opcodes.h"

#include <cstdio>

#include "extra/lz77_ascii.h"
#ifdef __MWERKS__
#include "extra/net_alloc.h"
#endif

namespace tm {{
namespace extra {{
    enum {{
        kPsaLowMods = {low_count},
        kPsaHiMods = {len(hi_mods)},
        kPsaModCount = {len(mods)},
        kPsaOpCount = {len(ops)},
        kPsaSpanCount = {len(op_spans)},
        kPsaSpanBytes = kPsaSpanCount * 2,
        kPsaStrCount = kPsaModCount + kPsaOpCount,
        kPsaStrBytes = {len(str_raw)},
        kPsaRawBytes = {len(raw)},
        kPsaCompBytes = {len(comp)}
    }};

    struct PsaOpcodeNet {{
        u8 spans[kPsaSpanBytes];
        u8 codes[kPsaOpCount];
        char text[kPsaStrBytes];
        u16 offsets[kPsaStrCount];
    }};

    static const u8 kHiMods[kPsaHiMods] = {{
{format_array(hi_mods, "0x{:02X}", per_line=16)}
    }};

    // Canonical Huffman + LZSS packed spans + opcode codes + module/opcode names
    // ({len(comp)} B vs {len(raw)} B raw).
    static const u8 kPsaBlobComp[kPsaCompBytes] = {{
{format_array(comp, "0x{:02X}", per_line=16)}
    }};

#ifdef __MWERKS__
    static PsaOpcodeNet* s_net = NULL;
#else
    static PsaOpcodeNet s_hostNet;
    static PsaOpcodeNet* s_net = NULL;
#endif

    static bool populate(PsaOpcodeNet* net)
    {{
        if (net == NULL)
        {{
            return false;
        }}
        return decompressHuffLzss(kPsaBlobComp, reinterpret_cast<char*>(net->spans), kPsaRawBytes)
               && indexPackedStrings(net->text, kPsaStrBytes, net->offsets, kPsaStrCount);
    }}

    void psaOpcodesAttach()
    {{
#ifdef __MWERKS__
        freeNetPtr(s_net);
        s_net = allocNetArray<PsaOpcodeNet>(1);
        if (!populate(s_net))
        {{
            freeNetPtr(s_net);
        }}
#else
        if (s_net == NULL && populate(&s_hostNet))
        {{
            s_net = &s_hostNet;
        }}
#endif
    }}

    void psaOpcodesDetach()
    {{
#ifdef __MWERKS__
        freeNetPtr(s_net);
#else
        s_net = NULL;
#endif
    }}

    static const PsaOpcodeNet* table()
    {{
#ifndef __MWERKS__
        if (s_net == NULL)
        {{
            psaOpcodesAttach();
        }}
#endif
        return s_net;
    }}

    const char* psaModulePrefix(u8 mod)
    {{
        const PsaOpcodeNet* net = table();
        if (net == NULL)
        {{
            return NULL;
        }}
        if (mod < kPsaLowMods)
        {{
            return net->text + net->offsets[mod];
        }}
        for (int i = 0; i < kPsaHiMods; i++)
        {{
            if (kHiMods[i] == mod)
            {{
                return net->text + net->offsets[kPsaLowMods + i];
            }}
        }}
        return NULL;
    }}

    const char* psaOpcodeName(u32 cmdHeader)
    {{
        const PsaOpcodeNet* net = table();
        if (net == NULL)
        {{
            return NULL;
        }}
        u8 mod = static_cast<u8>((cmdHeader >> 24) & 0xFFu);
        u8 code = static_cast<u8>((cmdHeader >> 16) & 0xFFu);
        if (mod == 0u && code == 2u && ((cmdHeader >> 8) & 0xFFu) == 0u)
        {{
            return "NoEvent";
        }}
        int start = 0;
        for (int s = 0; s < kPsaSpanCount; s++)
        {{
            int cnt = net->spans[s * 2 + 1];
            if (net->spans[s * 2] == mod)
            {{
                for (int i = 0; i < cnt; i++)
                {{
                    if (net->codes[start + i] == code)
                    {{
                        return net->text + net->offsets[kPsaModCount + start + i];
                    }}
                }}
                return NULL;
            }}
            start += cnt;
        }}
        return NULL;
    }}

    void psaFormatOpcodeName(u32 cmdHeader, char* out, int outSize)
    {{
        if (outSize <= 0)
        {{
            return;
        }}
        size_t cap = static_cast<size_t>(outSize);
        const char* name = cmdHeader == 0u ? "-" : psaOpcodeName(cmdHeader);
        if (name != NULL)
        {{
            snprintf(out, cap, "%s", name);
            return;
        }}
        u8 mod = static_cast<u8>((cmdHeader >> 24) & 0xFFu);
        unsigned code = static_cast<unsigned>((cmdHeader >> 16) & 0xFFu);
        const char* prefix = psaModulePrefix(mod);
        if (prefix != NULL)
        {{
            snprintf(out, cap, "%s:%02X", prefix, code);
        }}
        else
        {{
            snprintf(out, cap, "%02X:%02X", static_cast<unsigned>(mod), code);
        }}
    }}

    void psaFormatArg(u32 type, u32 content, char* out, int outSize)
    {{
        if (outSize <= 0)
        {{
            return;
        }}
        size_t cap = static_cast<size_t>(outSize);
        unsigned u = static_cast<unsigned>(content);
        const char* fmt = NULL;
        switch (type)
        {{
        case 0u: // int
            fmt = "%d";
            break;
        case 1u: // scalar (fixed-point / 60000; ac_anim_cmd_impl.h:46)
        {{
            s32 raw = static_cast<s32>(content);
            bool neg = raw < 0;
            u32 mag = neg ? (static_cast<u32>(-(raw + 1)) + 1u) : static_cast<u32>(raw);
            u32 cents = (mag + 300u) / 600u;
            const char* sign = (neg && cents != 0u) ? "-" : "";
            unsigned whole = static_cast<unsigned>(cents / 100u);
            unsigned frac = static_cast<unsigned>(cents % 100u);
            snprintf(out, cap, frac == 0u ? "%s%u" : "%s%u.%02u", sign, whole, frac);
            return;
        }}
        case 2u: // offset / relocated pointer
            fmt = "@%X";
            break;
        case 3u: // bool
            fmt = content != 0u ? "true" : "false";
            break;
        case 4u: // file
            fmt = "f:%X";
            break;
        case 5u: // variable (IC/LA/RA + Basic/Float/Bit + index; so_anim_cmd.h:9-17)
        {{
            static const char kMem[] = "IC\\0LA\\0RA";
            static const char kKind[] = "Int\\0Flt\\0Bit";
            u32 mem = (content >> 28) & 0xFu;
            u32 kind = (content >> 24) & 0xFu;
            if (mem < 3u && kind < 3u)
            {{
                snprintf(out, cap, "%s-%s[%u]", kMem + mem * 3u, kKind + kind * 4u,
                         static_cast<unsigned>(content & 0x00FFFFFFu));
                return;
            }}
            fmt = "V:%X";
            break;
        }}
        case 6u: // requirement (bit 31 = inverted)
            snprintf(out, cap, "%sR:%X", (content & 0x80000000u) != 0u ? "!" : "",
                     static_cast<unsigned>(content & 0x7FFFFFFFu));
            return;
        default:
            snprintf(out, cap, "?%u:%X", static_cast<unsigned>(type), u);
            return;
        }}
        snprintf(out, cap, fmt, u);
    }}
}}
}}
"""
    return cpp, len(mods), len(ops), len(raw), len(comp)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out_path = root / "src" / "extra" / "psa_opcodes.cpp"
    cpp, n_mods, n_ops, raw_b, comp_b = generate_cpp()
    if "--check" in sys.argv[1:]:
        if not out_path.exists():
            sys.stderr.write("error: src/extra/psa_opcodes.cpp is missing; run scripts/gen_psa_opcodes.py\n")
            return 1
        current = out_path.read_bytes().decode("ascii")
        if current != cpp:
            sys.stderr.write("error: src/extra/psa_opcodes.cpp is out of date; run scripts/gen_psa_opcodes.py\n")
            return 1
        print(f"psa_opcodes: up to date ({n_mods} modules, {n_ops} opcodes, {comp_b} B compressed / {raw_b} B raw)")
        return 0
    out_path.write_bytes(cpp.encode("ascii"))
    print(f"wrote {out_path} ({n_mods} modules, {n_ops} opcodes, {comp_b} B compressed / {raw_b} B raw)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
