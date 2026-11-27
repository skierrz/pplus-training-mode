#pragma once

#include <types.h>

#include "main/gfx/draw.h"
#include "main/settings.h"

// Pure OSD theme color, opacity, progress-track darkening, and low-opacity
// drop-shadow helpers shared by osd.cpp and host tests.
namespace tm {
namespace osd {
    enum {
        kShadowAlphaStepMax = main::kOsdAlphaMax - 1
    };

    struct ThemeColors {
        gfx::Rgba bodyColor;
        gfx::Rgba trackColor;
        gfx::Rgba shadowColor;
    };

    // 24-bit RGB for each OsdBgColor preset (main/settings.h).
    static const u32 kBgPalette[main::kOsdBgColorCount] = {
        0x0F242Cu, // P+ Teal
        0x141A24u, // Dark Slate
        0x101E3Au, // Brawl Navy
        0x1A1A1Eu, // Charcoal
        0x080A0Eu, // Black
        0x281018u, // Crimson
        0x1D122Cu  // Violet
    };

    static inline u8 clampBgColor(u8 raw)
    {
        return raw >= main::kOsdBgColorCount ? static_cast<u8>(main::kOsdBgColorCount - 1) : raw;
    }

    static inline u8 clampBgAlpha(u8 raw)
    {
        return raw > main::kOsdAlphaMax ? static_cast<u8>(main::kOsdAlphaMax) : raw;
    }

    static inline u32 bgRgb(u8 bgColor)
    {
        return kBgPalette[clampBgColor(bgColor)];
    }

    // Exact round(pct * 255 / 100) for pct = step * 10 (0..10): 255/10 == 51/2.
    static inline u8 bodyAlphaByte(u8 bgAlpha)
    {
        u8 step = clampBgAlpha(bgAlpha);
        return static_cast<u8>((static_cast<u32>(step) * 51u + 1u) >> 1);
    }

    // Darkened progress-track tint (50% of each RGB channel).
    static inline u32 darkenedRgb(u32 rgb)
    {
        return (rgb >> 1) & 0x7F7F7Fu;
    }

    // Progress-track alpha scaled cleanly from bgAlpha (0 when transparent).
    static inline u8 trackAlphaByte(u8 bgAlpha)
    {
        u8 body = bodyAlphaByte(bgAlpha);
        return body == 0 ? 0 : static_cast<u8>(body + ((255u - body) >> 4));
    }

    // Always-on 1px dark drop-shadow whenever opacity < 100% (steps 0..9:
    // 0x000000D0 at 0% tapering by 0x10 per 10% step to 0x00000040 at 90%;
    // 0 at 100% opaque).
    static inline gfx::Rgba dropShadowColor(u8 bgAlpha)
    {
        u8 step = clampBgAlpha(bgAlpha);
        return step <= kShadowAlphaStepMax ? static_cast<gfx::Rgba>(0xD0u - static_cast<u32>(step) * 0x10u) : 0;
    }

    static inline void resolveThemeTo(u8 bgColor, u8 bgAlpha, ThemeColors& out)
    {
        u8 step = bgAlpha > main::kOsdAlphaMax ? static_cast<u8>(main::kOsdAlphaMax) : bgAlpha;
        u8 a = static_cast<u8>((static_cast<u32>(step) * 51u + 1u) >> 1);
        gfx::Rgba body = 0;
        gfx::Rgba track = 0;
        if (a != 0)
        {
            u32 rgb = kBgPalette[bgColor >= main::kOsdBgColorCount ? main::kOsdBgColorCount - 1 : bgColor];
            body = (rgb << 8) | a;
            track = ((rgb << 7) & 0x7F7F7F00u) | (a + ((255u - a) >> 4));
        }
        out.bodyColor = body;
        out.trackColor = track;
        out.shadowColor = step <= kShadowAlphaStepMax ? static_cast<gfx::Rgba>(0xD0u - static_cast<u32>(step) * 0x10u) : 0;
    }

    static inline ThemeColors resolveTheme(u8 bgColor, u8 bgAlpha)
    {
        ThemeColors out;
        resolveThemeTo(bgColor, bgAlpha, out);
        return out;
    }
}
}
