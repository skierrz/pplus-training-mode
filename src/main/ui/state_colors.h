#pragma once

#include <types.h>

#include "main/gfx/draw.h"
#include "main/settings.h"

// Single source-of-truth semantic color tables for Project+ Training Mode.
// Strictly separates:
//   - Layer 1 (Timing / Grading feedback): Good / Okay / Bad / Neutral / Muted
//     used by OSDs, Drill Results, Streak Pips, and Frame Advantage readouts.
//   - Layer 2 (Action-State valence palette): coordinated across Frame Meter,
//     Ledgedash HUD, Drill HUD, and Floating Overhead Meters, with Flat Matte
//     Dead Ash-Slate (zero sheen) for unactionable lag/recovery/landing and
//     Hot Amber-Orange for stun/hitlag.
// Both layers route cleanly through Display > Theme > Colorblind Mode
// (Off / Deuteran / Protan / Tritan) with zero extra draw calls.
namespace tm {
namespace ui {
    enum GradeKind {
        kGradeGood = 0,
        kGradeOkay = 1,
        kGradeBad = 2,
        kGradeNeutral = 3,
        kGradeMuted = 4,
        kGradeCount
    };

    enum ActionStateColor {
        kStateColorEmpty = 0,      // Idle / empty recessed trough
        kStateColorActionable = 1, // Actionable / Control / Jump / Advantage Gap
        kStateColorGalint = 2,     // Intangible / Invincible / GALINT / XLU cap
        kStateColorStartup = 3,    // Attack/Grab startup, Jumpsquat
        kStateColorStrike = 4,     // Active strike hitbox
        kStateColorGrab = 5,       // Active grabbox / tether
        kStateColorLag = 6,        // Flat Matte Dead Ash-Slate (zero sheen): Recovery / Endlag / Land
        kStateColorStun = 7,       // Hot Amber-Orange: Hitstun / Shieldstun / Grabbed / Hitlag
        kStateColorLedge = 8,      // Ledge Hang
        kStateColorDrop = 9,       // Ledge Drop / Fall off ledge
        kStateColorDodge = 10,     // Airdodge / Evade
        kStateColorCount
    };

    static inline u8 clampColorblindMode(u8 mode)
    {
        return mode < main::kColorblindCount ? mode : static_cast<u8>(main::kColorblindOff);
    }

    static inline gfx::Rgba gradeColor(u8 grade, u8 mode)
    {
        static const gfx::Rgba kGrade[main::kColorblindCount][kGradeCount] = {
            // Off (Standard locked palette; exact match to osd::kGreen/kYellow/kRed/kWhite)
            {0x4ADE80FFu, 0xFACC15FFu, 0xFF948AFFu, 0xF1F5F9FFu, 0x8C96A4FFu},
            // Deuteran (M-cone / green-weak)
            {0x33C4FFFFu, 0xFFE044FFu, 0xE82A74FFu, 0xF1F5F9FFu, 0x8C96A4FFu},
            // Protan (L-cone / red-weak)
            {0x38D0FFFFu, 0xFFF05AFFu, 0xE07418FFu, 0xF1F5F9FFu, 0x8C96A4FFu},
            // Tritan (S-cone / blue-yellow weak)
            {0x14E6DCFFu, 0xFF9EC8FFu, 0xF22E3EFFu, 0xF1F5F9FFu, 0x8C96A4FFu}
        };
        u8 m = clampColorblindMode(mode);
        u8 g = grade < kGradeCount ? grade : static_cast<u8>(kGradeNeutral);
        return kGrade[m][g];
    }

    // Remaps standard OSD/HUD grading colors (kGreen/kYellow/kRed/kHudGreen/kHudWarn/kHudRed)
    // to the active Colorblind Mode while passing Off and neutral colors through unchanged.
    static inline gfx::Rgba remapGradeColor(gfx::Rgba c, u8 mode)
    {
        u8 m = clampColorblindMode(mode);
        if (m == main::kColorblindOff)
        {
            return c;
        }
        static const u32 kSrc[13] = {
            0x4ADE8000u, 0x4CE68800u, 0x3CE89000u, 0x22C55E00u,
            0xFACC1500u, 0xFFE24A00u, 0xFFD30000u, 0xFFB84800u, 0xF59E0B00u,
            0xFF948A00u, 0xFF665800u, 0xEF444400u, 0xDC444400u
        };
        u32 rgb = c & 0xFFFFFF00u;
        for (int i = 0; i < 13; i++)
        {
            if (rgb == kSrc[i])
            {
                u8 g = static_cast<u8>(i < 4 ? kGradeGood : (i < 9 ? kGradeOkay : kGradeBad));
                return (gradeColor(g, m) & 0xFFFFFF00u) | (c & 0xFFu);
            }
        }
        return c;
    }

    static inline gfx::Rgba stateColor(u8 state, u8 mode)
    {
        static const gfx::Rgba kState[main::kColorblindCount][kStateColorCount] = {
            // Off (Standard locked action-state valence palette)
            {
                0x18202ADCu, // Empty / Idle trough
                0x3CE072FFu, // Actionable / Control / Jump (Emerald Green)
                0x6EF8F2FFu, // Intangible / GALINT (Bright Ice-Aqua)
                0x3B8BFFFFu, // Startup (Cobalt Sky-Blue)
                0xF23848FFu, // Active Strike (Vivid Crimson Red)
                0xCC4CF5FFu, // Active Grab / Tether (Vivid Magenta-Purple)
                0x4C505EFFu, // Lag / Recovery / Land (Flat Matte Dead Ash-Slate)
                0xF59824FFu, // Stun / Hitlag (Hot Amber-Orange)
                0x8C96A4FFu, // Ledge Hang (Neutral Stone Gray)
                0xDFC460FFu, // Ledge Drop (Warm Sand)
                0x6E78FFFFu  // Airdodge / Evade (Indigo-Violet)
            },
            // Deuteran (M-cone / green-weak)
            {
                0x18202ADCu, 0x33C4FFFFu, 0xC4FFFFFFu, 0x2563EBFFu, 0xE82A74FFu,
                0xFF99F0FFu, 0x464A56FFu, 0xFFE044FFu, 0x8C96A4FFu, 0xF5E8B0FFu, 0x4F46E5FFu
            },
            // Protan (L-cone / red-weak)
            {
                0x18202ADCu, 0x38D0FFFFu, 0xDCFFFFFFu, 0x245CE6FFu, 0xFF801CFFu,
                0xE8D8FFFFu, 0x424652FFu, 0xFFF05AFFu, 0x8C96A4FFu, 0xE8DC9CFFu, 0x5B82FFFFu
            },
            // Tritan (S-cone / blue-yellow weak)
            {
                0x18202ADCu, 0x14E6DCFFu, 0xF4FAFFFFu, 0x0E8FA8FFu, 0xF22E3EFFu,
                0xFF8CC8FFu, 0x464A56FFu, 0xFF7868FFu, 0x8C96A4FFu, 0xC2DCE0FFu, 0x1A627AFFu
            }
        };
        u8 m = clampColorblindMode(mode);
        u8 s = state < kStateColorCount ? state : static_cast<u8>(kStateColorEmpty);
        return kState[m][s];
    }

    // Active Colorblind Mode from live settings (0 when uninitialized).
    static inline u8 activeColorblindMode()
    {
        const main::Settings& s = main::settings();
        return s.size ? clampColorblindMode(s.osd.colorblindMode) : static_cast<u8>(main::kColorblindOff);
    }
}
}
