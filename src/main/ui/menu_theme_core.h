#pragma once

#include <types.h>

#include "main/gfx/draw.h"
#include "main/settings.h"

// Pure Theme v2 palette and Info Display background/opacity helpers shared by
// menu.cpp (match), info_display.cpp (extra), and host tests.
// Preset 0 (P+ Teal) reproduces the exact baseline Lab Menu palette byte-for-byte,
// and Info Default/Default reproduces style::kInfoFill (0x121822CC) byte-for-byte.
namespace tm {
namespace ui {
    struct MenuPalette {
        gfx::Rgba bevelLight;
        gfx::Rgba bevelDark;
        gfx::Rgba headerTop;
        gfx::Rgba headerBot;
        gfx::Rgba railTop;
        gfx::Rgba railBot;
        gfx::Rgba baseTop;
        gfx::Rgba baseBot;
        gfx::Rgba descTop;
        gfx::Rgba descBot;
        gfx::Rgba divider;
        gfx::Rgba accentTop;
        gfx::Rgba accentBot;
        gfx::Rgba accentSheen;
        gfx::Rgba accentEdge;
        gfx::Rgba activeTabText;
        gfx::Rgba tabTop;
        gfx::Rgba tabBot;
        gfx::Rgba tabInactiveText;
        gfx::Rgba mutedText;
        gfx::Rgba descText;
    };

    static inline u8 clampMenuAccent(u8 raw)
    {
        return raw >= main::kMenuAccentCount ? static_cast<u8>(main::kMenuAccentTeal) : raw;
    }

    // Indexed by main::MenuAccent (main/settings.h). Preserves the metallic gold
    // focus bar, dark double borders, and vertical bevel depth across every preset.
    // Function-local static so TUs that only use InfoThemeColors omit the table.
    static inline const MenuPalette& menuPalette(u8 accent)
    {
        static const MenuPalette kMenuPalettes[main::kMenuAccentCount] = {
            // 0: P+ Teal (v1 Direction A smoked charcoal + emerald-teal, matching ui/style.h)
            {
                0x2A3544A0u, 0x05070BFFu, 0x151B24F2u, 0x0E1219F2u, 0x11161EF2u, 0x0B0E14F2u,
                0x141A22E6u, 0x0D1117E6u, 0x0F141BF5u, 0x090C11F5u, 0x344254FFu, 0x2DB3A1FFu,
                0x209485FFu, 0x7CE4D670u, 0x125B52FFu, 0x061413FFu, 0x242C38FFu, 0x181E27FFu,
                0xC0CCD6FFu, 0x9AA7B4FFu, 0xD2DCE4FFu
            },
            // 1: Brawl Navy (smoked charcoal + cobalt accent)
            {
                0x304260A0u, 0x05070EFFu, 0x141B2CF2u, 0x0C111DF2u, 0x101524F2u, 0x090C16F2u,
                0x131826E6u, 0x0C101AE6u, 0x0F1422F5u, 0x080B14F5u, 0x263552FFu, 0x4A9CEEFFu,
                0x286CB8FFu, 0x9CD0FF70u, 0x143C6EFFu, 0x051020FFu, 0x222C42FFu, 0x161D2EFFu,
                0xC2CDE0FFu, 0x9AA8C0FFu, 0xD2DCEEFFu
            },
            // 2: Subspace Violet (smoked charcoal + royal indigo accent)
            {
                0x463662A0u, 0x08060EFFu, 0x1E162BF2u, 0x130E1DF2u, 0x181224F2u, 0x0F0B18F2u,
                0x1A1426E6u, 0x110C1AE6u, 0x171122F5u, 0x0E0A16F5u, 0x3C2A54FFu, 0xA874EEFFu,
                0x7440C2FFu, 0xD8BEFF70u, 0x3A1E6AFFu, 0x120620FFu, 0x2E2242FFu, 0x1E162CFFu,
                0xD0C4E0FFu, 0xB0A0C4FFu, 0xDCD2ECFFu
            },
            // 3: Crimson Steel (smoked charcoal + crimson accent)
            {
                0x5C303AA0u, 0x0C0508FFu, 0x26151AF2u, 0x180D11F2u, 0x201216F2u, 0x130A0DF2u,
                0x221318E6u, 0x150B0FE6u, 0x1E1116F5u, 0x120A0DF5u, 0x4C2630FFu, 0xE25C6EFFu,
                0xAC2C3EFFu, 0xFFACB670u, 0x5A1620FFu, 0x20050AFFu, 0x3B2028FFu, 0x26141AFFu,
                0xE0C6CCFFu, 0xC09EA6FFu, 0xECD2D8FFu
            }
        };
        return kMenuPalettes[clampMenuAccent(accent)];
    }

    // ---- Info Display background color and opacity ----
    struct InfoThemeColors {
        gfx::Rgba fillColor;   // 0 when transparent (0% opacity)
        gfx::Rgba shadowColor; // non-zero drop-shadow at <= 20% opacity
    };

    // Indexed by main::InfoBgColor (main/settings.h).
    static const u32 kInfoBgPalette[main::kInfoBgColorCount] = {
        0x121822u, // 0: Default (exact style::kInfoFill RGB)
        0x0F242Cu, // 1: P+ Teal
        0x101E3Au, // 2: Brawl Navy
        0x1A1A1Eu, // 3: Charcoal
        0x080A0Eu, // 4: Black
        0x1D122Cu, // 5: Violet
        0x281018u  // 6: Crimson
    };

    // Indexed by main::InfoAlpha (main/settings.h): Default = 80% (0xCC).
    static const u8 kInfoAlphaBytes[main::kInfoAlphaCount] = {
        0xCCu, // 0: Default (80%)
        0x00u, // 1: 0%
        0x33u, // 2: 20%
        0x66u, // 3: 40%
        0x99u, // 4: 60%
        0xCCu, // 5: 80%
        0xFFu  // 6: 100%
    };

    static inline u8 clampInfoBgColor(u8 raw)
    {
        return raw >= main::kInfoBgColorCount ? static_cast<u8>(main::kInfoBgColorCount - 1) : raw;
    }

    static inline u8 clampInfoAlpha(u8 raw)
    {
        return raw >= main::kInfoAlphaCount ? static_cast<u8>(main::kInfoAlphaCount - 1) : raw;
    }

    static inline u32 infoBgRgb(u8 color)
    {
        return kInfoBgPalette[clampInfoBgColor(color)];
    }

    static inline u8 infoAlphaByte(u8 alpha)
    {
        return kInfoAlphaBytes[clampInfoAlpha(alpha)];
    }

    // Matches OSD Theme v1 drop-shadow strength at <= 20% opacity.
    static inline gfx::Rgba infoDropShadowColor(u8 alpha)
    {
        u8 step = clampInfoAlpha(alpha);
        if (step == main::kInfoAlpha0)
        {
            return 0x000000D0u;
        }
        if (step == main::kInfoAlpha20)
        {
            return 0x00000088u;
        }
        return 0u;
    }

    static inline InfoThemeColors resolveInfoTheme(u8 bgColor, u8 bgAlpha)
    {
        InfoThemeColors out;
        u8 a = infoAlphaByte(bgAlpha);
        out.fillColor = a != 0 ? ((infoBgRgb(bgColor) << 8) | a) : 0u;
        out.shadowColor = infoDropShadowColor(bgAlpha);
        return out;
    }
}
}
