#!/usr/bin/env python3
"""Generate LZ77-compressed src/main/game/status_names.cpp from readable names.

Usage:
  python scripts/gen_status_names.py          # regenerate status_names.cpp
  python scripts/gen_status_names.py --check  # verify file is up to date
"""
import importlib.util
import pathlib
import sys

_gmd_spec = importlib.util.spec_from_file_location(
    "gen_menu_descriptions", pathlib.Path(__file__).resolve().parent / "gen_menu_descriptions.py"
)
_gmd = importlib.util.module_from_spec(_gmd_spec)
_gmd_spec.loader.exec_module(_gmd)

# Common status kinds 0x00-0x111 in the order of Fighter::Status::Kind
# (lib/BrawlHeaders ft/fighter.h) with underscores dropped.
COMMON_STATUS_NAMES = [
    "Wait", "Walk", "WalkBrake", "Dash", "Run", "RunBrake", "Turn", "TurnDash", "TurnRun", "TurnRunBrake",
    "JumpSquat", "Jump", "JumpAerial", "Fly", "Fall", "FallAerial", "FallSpecial", "Squat", "SquatWait",
    "SquatF", "SquatB", "SquatRv", "Landing", "LandingLight", "LandingAttackAir", "LandingFallSpecial",
    "GuardOn", "Guard", "GuardOff", "GuardDamage", "Escape", "EscapeF", "EscapeB", "EscapeAir",
    "ReboundStop", "Rebound", "Attack", "Attack100", "AttackDash", "AttackS3", "AttackHi3", "AttackLw3",
    "AttackS4Start", "AttackS4Hold", "AttackS4", "AttackLw4Start", "AttackLw4Hold", "AttackLw4",
    "AttackHi4Start", "AttackHi4Hold", "AttackHi4", "AttackAir", "Catch", "CatchPull", "CatchDash",
    "CatchDashPull", "CatchTurn", "CatchWait", "CatchAttack", "CatchCut", "Throw", "CapturePulled",
    "CaptureWait", "CaptureDamage", "CaptureCut", "CaptureJump", "CaptureThrown", "Damage", "DamageAir",
    "DamageFly", "DamageFlyRoll", "DamageFlyReflectLr", "DamageFlyReflectU", "DamageFall", "Down",
    "DownSpot", "DownContinue", "DownWait", "DownWaitContinue", "DownEat", "LayDown", "DownStand",
    "DownStandFb", "DownStandAttack", "DownDamage", "DownReflectLr", "ShieldBreakFly", "ShieldBreakFall",
    "ShieldBreakDown", "FuraFuraStand", "FuraFura", "FuraFuraEnd", "DamageSongStart", "DamageSong",
    "DamageSongEnd", "Bind", "Passive", "PassiveFb", "PassiveWall", "PassiveWallJump", "PassiveCeil",
    "StopWall", "StopCeil", "WallJump", "AttachWall", "AttachWallWait", "DetachWall", "DetachWallFall",
    "DetachWallJump", "TreadJump", "TreadDamage", "TreadDamageRv", "TreadDamageAir", "TreadFall", "Pass",
    "CliffCatchMove", "CliffCatch", "CliffWait", "CliffAttack", "CliffClimb", "CliffEscape", "CliffJump1",
    "CliffJump2", "CliffJump3", "Ottoto", "OttotoWait", "MissFoot", "AirLasso", "AirLassoReach",
    "AirLassoHang", "AirLassoRewind", "AirLassoFailure", "GlideStart", "Glide", "GlideLanding",
    "GlideAttack", "GlideEnd", "Slip", "SlipDown", "SlipTurn", "SlipDash", "SlipWait", "SlipStand",
    "SlipStandAttack", "SlipStandF", "SlipStandB", "LadderCatch", "LadderCatchEnd", "LadderWait",
    "LadderEnd", "ItemPickupLight", "ItemPickupHeavy", "ItemLiftWait", "ItemLiftTurn", "ItemLiftWalk",
    "ItemThrow", "ItemThrowDash", "ItemThrowHeavy", "ItemSwing", "ItemSwingS3", "ItemSwingS4Start",
    "ItemSwingS4", "ItemSwingS4Hold", "ItemSwingDash", "ItemShootWait", "ItemShootWalkF",
    "ItemShootWalkBrakeF", "ItemShootWalkB", "ItemShootWalkBrakeB", "ItemShootDashF", "ItemShootDashB",
    "ItemShootJumpSquat", "ItemShootJump", "ItemShootAir", "ItemShootLanding", "ItemScrewJump",
    "ItemScrewJumpAerial", "ItemScrewFall", "ItemAssistHoist", "SwimDive", "SwimRise", "SwimUp",
    "SwimWait", "Swim", "SwimEnd", "SwimTurn", "SwimJump", "SwimDrown", "SwimDrownOut", "Dead", "Rebirth",
    "GimmickDoor", "GimmickBarrel", "GimmickCatapult", "GimmickTruck", "GimmickSpring", "GimmickSpringJump",
    "GimmickSpringBack", "GimmickEaten", "Bury", "BuryWait", "BuryJump", "Ice", "IceJump",
    "BittenWarioStart", "BittenWario", "BittenWarioEnd", "ClungCaptain", "Swallowed", "SwallowedDrink",
    "SwallowedCapture", "SwallowedThrownStar", "SwallowedThrown", "SwallowedCanceled", "ClungDiddy",
    "ClungDamageDiddy", "ClungThrownDiddy", "ShoulderedDonkeyStart", "ShoulderedDonkey",
    "ShoulderedDonkeyThrown", "Warpstar", "WarpstarJump", "DragoonRide", "HammerWait", "HammerWalk",
    "HammerTurn", "HammerJumpSquat", "HammerJump", "HammerFall", "HammerLanding", "ClungGanon",
    "CatchedGanon", "CatchedAirGanon", "CatchedAirFallGanon", "CatchedAirEndGanon", "CaptureYoshi",
    "YoshiEgg", "LinkFinal", "KoopaDived", "MetaknightFinal", "PitFall", "ThrowKirby", "Big", "Small",
    "HeartSwap", "HeartSwapEnd", "AdvFollow", "IkeFinalDamageFly", "IkeFinalDamage", "IkeFinalDamageFall",
    "CaptureMasterhand", "FinalVisualAttackOther", "CaptainFinalStart", "CaptainFinalFuraFura",
    "CaptainFinalClash", "CaptainFinalEnd", "KirbyFinalDamageFly", "KirbyFinalDamageWait",
    "CaptureGokulin", "CaptureCutGokulin", "CaptureTaboo", "CaptureBucyulus", "CaptureCutBucyulus",
    "CapturePulledYoshi", "CaptureWaitYoshi", "CaptureDamageYoshi", "CaptureCutYoshi", "Standby", "Appeal",
    "SmashAppeal", "Entry", "Win", "Lose", "TestMotion",
]


def encode_lz77_ascii(raw):
    from collections import defaultdict
    for b in raw:
        if b >= 0x80:
            raise ValueError(f"Non-ASCII byte {b:#x} in status names")
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


def format_u8_array(values, indent="        ", per_line=16):
    rows = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        rows.append(indent + ", ".join(f"0x{v:02X}" for v in chunk) + ",")
    if rows:
        rows[-1] = rows[-1].rstrip(",")
    return "\n".join(rows)


def generate_cpp():
    assert len(COMMON_STATUS_NAMES) == 0x112
    for s in COMMON_STATUS_NAMES:
        for ch in s:
            b = ord(ch)
            if b < 0x20 or b >= 0x7F:
                raise ValueError(f"Non-printable ASCII byte {b:#x} in {s!r}")
    raw = b"".join(s.encode("ascii") + b"\x00" for s in COMMON_STATUS_NAMES)
    comp = _gmd.encode_huff_lzss(raw)
    if _gmd.decode_huff_lzss(comp, len(raw)) != raw:
        raise AssertionError("Huff+LZSS round-trip mismatch in status names")

    return f"""// Generated by scripts/gen_status_names.py - edit status names there.
#include "main/game/status_names.h"

#include <cstdio>

#include "extra/lz77_ascii.h"
#ifdef __MWERKS__
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#endif
#include "main/game/status_ids.h"

// Names of the common status kinds 0x00-0x111, in the order of
// Fighter::Status::Kind (lib/BrawlHeaders ft/fighter.h) with underscores
// dropped. Stored as Canonical Huffman + LZSS and decompressed once into Heaps::Network.
namespace tm {{
#ifdef __MWERKS__
namespace extra {{
#pragma force_active on
    bool decompressHuffLzss(const u8* src, char* dst, u32 rawLen)
    {{
        return decompressHuffLzssImpl(src, dst, rawLen);
    }}

    bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count)
    {{
        return indexPackedStringsImpl(text, len, offsets, count);
    }}
#pragma force_active reset
}}
#endif
namespace game {{
    enum {{
        kRawBytes = {len(raw)},
        kCompBytes = {len(comp)}
    }};

    struct StatusNamesNet {{
        char text[kRawBytes];
        u16 offsets[kCommonStatusCount];
    }};

    // Compressed NUL-terminated status names ({len(comp)} B vs {len(raw)} B raw + 548 B offsets).
    static const u8 kNamesComp[kCompBytes] = {{
{format_u8_array(comp)}
    }};

    // Statuses from 0x112 are each fighter's own; the first five are shared
    // by every fighter (ProjectPunch's actions.h, MIT; see
    // THIRD_PARTY_NOTICES.md).
    static const char* const kSpecialNames[] = {{"SpecialN", "SpecialS", "SpecialHi", "SpecialLw", "Final"}};

#ifdef __MWERKS__
    static StatusNamesNet* s_net = NULL;
#else
    static StatusNamesNet s_hostNet;
    static StatusNamesNet* s_net = NULL;
#endif

    static bool populate(StatusNamesNet* net)
    {{
        if (net == NULL)
        {{
            return false;
        }}
        return extra::decompressHuffLzss(kNamesComp, net->text, kRawBytes)
               && extra::indexPackedStrings(net->text, kRawBytes, net->offsets, kCommonStatusCount);
    }}

#ifdef __MWERKS__
#pragma force_active on
#endif
    void statusNamesAttach()
    {{
#ifdef __MWERKS__
        statusNamesDetach();
        s_net = static_cast<StatusNamesNet*>(allocNetworkZero(static_cast<u32>(sizeof(StatusNamesNet))));
        if (!populate(s_net))
        {{
            statusNamesDetach();
        }}
#else
        if (s_net == NULL && populate(&s_hostNet))
        {{
            s_net = &s_hostNet;
        }}
#endif
    }}

    void statusNamesDetach()
    {{
#ifdef __MWERKS__
        freeNetworkSlot(reinterpret_cast<void**>(&s_net));
#else
        s_net = NULL;
#endif
    }}

    const char* statusName(int status)
    {{
        if (status >= 0 && status < kCommonStatusCount)
        {{
#ifndef __MWERKS__
            if (s_net == NULL)
            {{
                statusNamesAttach();
            }}
#endif
            return s_net != NULL ? (s_net->text + s_net->offsets[status]) : NULL;
        }}
        if (status >= kStatusSpecialN && status <= kStatusFinal)
        {{
            return kSpecialNames[status - kStatusSpecialN];
        }}
        return NULL;
    }}

    void statusText(int status, char* buf, u32 size)
    {{
        const char* name = statusName(status);
        if (name != NULL)
        {{
            snprintf(buf, size, "%s", name);
        }}
        else
        {{
            snprintf(buf, size, "0x%X", status);
        }}
    }}
#ifdef __MWERKS__
#pragma force_active reset
#endif
}}
}}
"""


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out_path = root / "src" / "main" / "game" / "status_names.cpp"
    cpp = generate_cpp()
    if "--check" in sys.argv[1:]:
        current = out_path.read_bytes().decode("ascii")
        if current != cpp:
            sys.stderr.write("error: src/main/game/status_names.cpp is out of date; run scripts/gen_status_names.py\n")
            return 1
        print("status_names: up to date")
        return 0
    out_path.write_bytes(cpp.encode("ascii"))
    print(f"wrote {out_path} ({len(cpp)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
