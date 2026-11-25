#pragma once

#include <types.h>

#include "main/display/input_display.h"
#include "shared/game/pad_bits.h"
#include "shared/ui_icons.h"

// Pure multi-controller Input Viewer layout tables, stick/trigger normalization,
// pressed-state tinting, and AI-input-to-pad synthesis. Header-only and free of
// double-quoted string literals so host tests exercise the exact same code and
// text_rules.py stays clean.
namespace tm {
namespace extra {
    enum ViewerLayoutKind {
        kViewerGcc = 0,
        kViewerClassic = 1,
        kViewerWiimote = 2,
        kViewerNunchuk = 3
    };

    // Compact 8-byte button descriptor (nxPct/nyPct in 0..100, h2 in half-px).
    struct ViewerButtonSpec {
        u32 mask;
        u8 iconId; // ui::UiIconId
        u8 nxPct;
        u8 nyPct;
        u8 h2;     // height = h2 * 0.5f; bit 7 (0x80) = isDpad? Kept separate via iconId check.
    };

    struct ViewerStickSpec {
        u8 present;
        u8 isCStick;
        u8 octagonal; // 1: octagonal gate (GCC/Nunchuk), 0: circular (Classic)
        u8 nxPct;
        u8 nyPct;
        u8 r2;        // radius = r2 * 0.5f
    };

    struct ViewerTriggerSpec {
        u32 digitalMask;
        u8 present;
        u8 isRight;
        u8 nxPct;
        u8 nyPct;
        u8 nwPct;
        u8 h;
    };

    struct ViewerLayout {
        u8 kind;
        u8 buttonOffset;
        u8 buttonCount;
        ViewerStickSpec sticks[2];
        ViewerTriggerSpec triggers[2];
    };

    enum {
        kViewerColorIdle = 0xAAB8C4A8u,
        kViewerColorPressed = 0xFFFFFFFFu,
        kViewerColorGlow = 0xFFD30088u,
        kViewerColorTrigFill = 0x36CFC2FFu,
        kViewerColorTrigClick = 0xFFD300FFu
    };

    inline const ViewerButtonSpec* viewerAllButtons()
    {
        static const ViewerButtonSpec kButtons[] = {
            // 0..8: GameCube (9 buttons)
            {game::kPadL, ui::kIconGcL, 19, 28, 17},
            {game::kPadStart, ui::kIconGcStart, 44, 34, 14},
            {game::kPadZ, ui::kIconGcZ, 59, 28, 13},
            {game::kPadR, ui::kIconGcR, 81, 28, 17},
            {game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp, ui::kIconGcDpad, 37, 74, 25},
            {game::kPadY, ui::kIconGcY, 75, 46, 17},
            {game::kPadA, ui::kIconGcA, 75, 66, 26},
            {game::kPadB, ui::kIconGcB, 62, 79, 19},
            {game::kPadX, ui::kIconGcX, 89, 62, 23},
            // 9..19: Classic Controller (11 buttons)
            {game::kPadL, ui::kIconClL, 16, 27, 16},
            {game::kPadClassicZL, ui::kIconClZL, 36, 27, 16},
            {game::kPadClassicZR, ui::kIconClZR, 64, 27, 16},
            {game::kPadR, ui::kIconClR, 84, 27, 16},
            {game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp, ui::kIconWiiDpad, 14, 60, 28},
            {game::kPadWiiMinus, ui::kIconWiiMinus, 45, 46, 15},
            {game::kPadStart | game::kPadClassicPlus, ui::kIconWiiPlus, 55, 46, 15},
            {game::kPadX, ui::kIconClX, 81, 48, 17},
            {game::kPadY, ui::kIconClY, 71, 64, 17},
            {game::kPadA, ui::kIconClA, 91, 64, 17},
            {game::kPadB, ui::kIconClB, 81, 80, 17},
            // 20..26: Sideways Wii Remote (7 buttons)
            {game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp, ui::kIconWiiDpad, 18, 56, 36},
            {game::kPadWiiA, ui::kIconWiiA, 38, 36, 23},
            {game::kPadWiiB, ui::kIconWiiB, 38, 74, 25},
            {game::kPadWiiMinus, ui::kIconWiiMinus, 54, 36, 17},
            {game::kPadStart, ui::kIconWiiPlus, 54, 72, 17},
            {game::kPadWii1, ui::kIconWii1, 72, 56, 27},
            {game::kPadWii2, ui::kIconWii2, 88, 56, 27},
            // 27..35: Nunchuk (9 buttons)
            {game::kPadNunchukC, ui::kIconNunC, 14, 25, 18},
            {game::kPadZ, ui::kIconNunZ, 30, 25, 18},
            {game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp, ui::kIconWiiDpad, 45, 58, 30},
            {game::kPadWiiA | game::kPadA, ui::kIconWiiA, 64, 35, 21},
            {game::kPadWiiB | game::kPadB, ui::kIconWiiB, 64, 72, 24},
            {game::kPadWiiMinus, ui::kIconWiiMinus, 79, 26, 15},
            {game::kPadStart, ui::kIconWiiPlus, 90, 26, 15},
            {game::kPadWii1, ui::kIconWii1, 80, 68, 21},
            {game::kPadWii2, ui::kIconWii2, 91, 68, 21}
        };
        return kButtons;
    }

    inline const ViewerLayout& viewerLayoutForType(u8 controllerType)
    {
        static const ViewerLayout kLayouts[4] = {
            // 0: GameCube
            {kViewerGcc, 0, 9,
             {{1, 0, 1, 18, 62, 24}, {1, 1, 1, 50, 74, 15}},
             {{game::kPadL, 1, 0, 6, 10, 29, 4}, {game::kPadR, 1, 1, 65, 10, 29, 4}}},
            // 1: Classic Controller
            {kViewerClassic, 9, 11,
             {{1, 0, 0, 34, 72, 19}, {1, 1, 0, 56, 72, 19}},
             {{game::kPadL, 1, 0, 6, 10, 26, 4}, {game::kPadR, 1, 1, 68, 10, 26, 4}}},
            // 2: Sideways Wii Remote
            {kViewerWiimote, 20, 7,
             {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
             {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}},
            // 3: Nunchuk
            {kViewerNunchuk, 27, 9,
             {{1, 0, 1, 20, 66, 23}, {0, 0, 0, 0, 0, 0}},
             {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}}
        };
        return kLayouts[controllerType < 4 ? controllerType : 0];
    }

    inline const ViewerButtonSpec* viewerLayoutButtons(const ViewerLayout& layout)
    {
        return viewerAllButtons() + layout.buttonOffset;
    }

    inline bool viewerIsDpadIcon(u8 iconId)
    {
        return iconId == ui::kIconGcDpad || iconId == ui::kIconWiiDpad;
    }

    inline bool viewerButtonPressed(u32 buttons, const ViewerButtonSpec& btn)
    {
        return (buttons & btn.mask) != 0;
    }

    inline u32 viewerIconTint(bool pressed)
    {
        return pressed ? kViewerColorPressed : kViewerColorIdle;
    }

    inline float viewerTriggerFill(u8 analog, bool digitalPressed)
    {
        if (digitalPressed)
        {
            return 1.0f;
        }
        float v = static_cast<float>(analog) * (1.0f / 150.0f);
        return v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v);
    }

    inline float viewerSqrt(float v)
    {
        if (v <= 0.0f)
        {
            return 0.0f;
        }
        float x = v > 1.0f ? v : 1.0f;
        for (int i = 0; i < 5; i++)
        {
            x = 0.5f * (x + v / x);
        }
        return x;
    }

    inline void viewerStickNorm(s8 rawX, s8 rawY, float& outX, float& outY)
    {
        float x = static_cast<float>(rawX) * (1.0f / 80.0f);
        float y = static_cast<float>(rawY) * (1.0f / 80.0f);
        float sq = x * x + y * y;
        if (sq > 1.0f)
        {
            float inv = 1.0f / viewerSqrt(sq);
            x *= inv;
            y *= inv;
        }
        outX = x;
        outY = y;
    }

    inline void viewerDpadDir(u32 buttons, int& outDx, int& outDy)
    {
        outDx = ((buttons & game::kPadDRight) ? 1 : 0) - ((buttons & game::kPadDLeft) ? 1 : 0);
        outDy = ((buttons & game::kPadDUp) ? 1 : 0) - ((buttons & game::kPadDDown) ? 1 : 0);
    }

    inline s8 viewerFloatToStick(float v)
    {
        if (v > 1.0f)
        {
            v = 1.0f;
        }
        else if (v < -1.0f)
        {
            v = -1.0f;
        }
        int iv = static_cast<int>(v >= 0.0f ? (v * 80.0f + 0.5f) : (v * 80.0f - 0.5f));
        return static_cast<s8>(iv > 80 ? 80 : (iv < -80 ? -80 : iv));
    }

    inline void viewerAiInputToPad(u32 aiButtons, float sx, float sy, float lTrig, float rTrig,
                                   display::ViewerPad& out)
    {
        out.buttons = 0;
        out.sx = viewerFloatToStick(sx);
        out.sy = viewerFloatToStick(sy);
        out.cx = 0;
        out.cy = 0;
        out.lTrig = 0;
        out.rTrig = 0;
        out.controllerType = kViewerGcc;
        out.connected = 1;

        bool atk = (aiButtons & 0x1u) != 0;
        bool grd = (aiButtons & 0x8u) != 0;
        if ((aiButtons & 0x100u) != 0)
        {
            out.cx = out.sx;
            out.cy = out.sy;
        }
        else if (atk && grd)
        {
            out.buttons |= game::kPadZ;
        }
        else
        {
            if (atk)
            {
                out.buttons |= game::kPadA;
            }
            if (grd)
            {
                out.buttons |= game::kPadL;
                out.lTrig = 150;
            }
        }
        if ((aiButtons & 0x2u) != 0)
        {
            out.buttons |= game::kPadB;
        }
        if ((aiButtons & 0x4u) != 0)
        {
            out.buttons |= game::kPadX;
        }
        if ((aiButtons & 0x400u) != 0)
        {
            out.buttons |= game::kPadDUp;
        }
        if ((aiButtons & 0x800u) != 0)
        {
            out.buttons |= game::kPadDRight;
        }
        if ((aiButtons & 0x1000u) != 0)
        {
            out.buttons |= game::kPadDDown;
        }
        if (lTrig > 0.02f)
        {
            int lv = static_cast<int>(lTrig * 150.0f);
            out.lTrig = static_cast<u8>(lv > 150 ? 150 : lv);
        }
        if (rTrig > 0.02f)
        {
            int rv = static_cast<int>(rTrig * 150.0f);
            out.rTrig = static_cast<u8>(rv > 150 ? 150 : rv);
        }
    }

    inline void viewerTagText(int playerNo, bool cpu, char out[8])
    {
        if (cpu)
        {
            out[0] = 'C';
            out[1] = 'P';
            out[2] = 'U';
            out[3] = '\0';
        }
        else
        {
            int p = (playerNo >= 0 && playerNo < 4) ? (playerNo + 1) : 1;
            out[0] = 'P';
            out[1] = static_cast<char>('0' + p);
            out[2] = '\0';
        }
    }
}
}
