#pragma once

#include <types.h>

// Native Brawl / Project+ UI emblem IDs shared between match (menu.cpp) and
// extra (ui_assets.cpp). Actual BRRES lookup, UV tables, and GX CI8/TLUT
// rendering live in extra so match headroom stays protected.
namespace tm {
namespace ui {
    enum UiIconId {
        // InfPauseButton00 #01 red cursor triangle rotated 90 deg CW to point right.
        kIconCursorRight = 0,
        // GameCube controller (InfPauseButton00, 120x132 CI8).
        kIconGcA,
        kIconGcB,
        kIconGcX,
        kIconGcY,
        kIconGcL,
        kIconGcR,
        kIconGcZ,
        kIconGcStart,
        kIconGcDpad,
        // Classic Controller (InfButton04ClBtn01, 64x168 CI8).
        kIconClA,
        kIconClB,
        kIconClX,
        kIconClY,
        kIconClL,
        kIconClR,
        kIconClZL,
        kIconClZR,
        // Wiimote & Nunchuk (InfButton04NnBtn01, 112x96 CI8).
        kIconWiiA,
        kIconWiiB,
        kIconWii1,
        kIconWii2,
        kIconWiiPlus,
        kIconWiiMinus,
        kIconWiiDpad,
        kIconNunC,
        kIconNunZ,
        kIconCount
    };

    // Natural width/height aspect ratio of each emblem (accounting for 90 deg
    // CW UV rotation on Classic L/R and Nunchuk Z).
    inline float uiIconAspect(u8 id)
    {
        switch (id)
        {
        case kIconGcX:
            return 22.0f / 33.0f;
        case kIconGcY:
            return 33.0f / 22.0f;
        case kIconGcL:
            return 47.0f / 31.0f;
        case kIconGcR:
            return 48.0f / 30.0f;
        case kIconGcZ:
            return 52.0f / 17.0f;
        case kIconClL:
        case kIconClR:
            return 52.0f / 23.0f;
        case kIconClZL:
        case kIconClZR:
            return 32.0f / 26.0f;
        case kIconWiiB:
            return 33.0f / 47.0f;
        case kIconWiiPlus:
            return 16.0f / 15.0f;
        case kIconNunC:
            return 34.0f / 30.0f;
        case kIconNunZ:
            return 42.0f / 31.0f;
        default:
            return 1.0f;
        }
    }
}
}
