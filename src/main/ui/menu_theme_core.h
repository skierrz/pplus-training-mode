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
            // 0: P+ Teal (exact baseline colors from ui/style.h)
            {
                0x5A96A890u, 0x040B0EFFu, 0x1A3946F2u, 0x0E212AF2u, 0x0E1D25F5u, 0x081117F5u,
                0x102029EEu, 0x091319EEu, 0x0F222AF5u, 0x081318F5u, 0x24424CFFu, 0x36CFC2FFu,
                0x1A8C83FFu, 0x8EF5EA90u, 0x0D4D48FFu, 0x041B1AFFu, 0x1D3D4AFFu, 0x11252EFFu,
                0xB8CDD6FFu, 0x9FB4BDFFu, 0xC6D8E0FFu
            },
            // 1: Brawl Navy (deep cobalt steel chrome)
            {
                0x648EC290u, 0x040814FFu, 0x1B3156F2u, 0x0E1B34F2u, 0x0E182DF5u, 0x080D1BF5u,
                0x101B32EEu, 0x09101FEEu, 0x0F1C34F5u, 0x08101FF5u, 0x263E68FFu, 0x4AA4F2FFu,
                0x2266B8FFu, 0xA6D8FF90u, 0x103668FFu, 0x041224FFu, 0x1E355CFFu, 0x11203AFFu,
                0xBCCBE0FFu, 0x9EB2CCFFu, 0xC8D8ECFFu
            },
            // 2: Subspace Violet (royal indigo/plum chrome)
            {
                0x9672B890u, 0x0A0512FFu, 0x321D4AF2u, 0x1D102DF2u, 0x1B1028F5u, 0x100918F5u,
                0x1E122DEEu, 0x110A1BEEu, 0x1F122FF5u, 0x110A1BF5u, 0x462A64FFu, 0xB27CF2FFu,
                0x723EC2FFu, 0xDEC2FF90u, 0x3A1B6BFFu, 0x140624FFu, 0x35204EFFu, 0x1F1230FFu,
                0xD0C2E0FFu, 0xB4A2C8FFu, 0xDCD0ECFFu
            },
            // 3: Crimson Steel (gunmetal crimson chrome)
            {
                0xB86A7490u, 0x120508FFu, 0x461C24F2u, 0x291016F2u, 0x241015F5u, 0x15090CF5u,
                0x271217EEu, 0x160A0DEEu, 0x281218F5u, 0x160A0DF5u, 0x5E2834FFu, 0xEA6474FFu,
                0xB02A3CFFu, 0xFFB2BC90u, 0x5C121EFFu, 0x24040AFFu, 0x4A1F28FFu, 0x2C1118FFu,
                0xE0C2C8FFu, 0xC6A2AAFFu, 0xECD0D6FFu
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
