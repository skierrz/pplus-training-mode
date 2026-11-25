#pragma once

#include <types.h>

#include "main/game/match.h"
#include "main/settings.h"

// Pure Live DI Vector Picker solver and 2D unit-circle DI compass helpers.
// Header-only and free of double-quoted string literals so both di_display.cpp
// and PC host tests share the exact same vector math.
namespace tm {
namespace extra {
    struct DiVectorResult {
        float dx;
        float dy;
        bool overridden;
    };

    inline float diPickerSqrt(float v)
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

    inline void diNormalizeUnit(float& x, float& y)
    {
        float sq = x * x + y * y;
        if (sq > 1.0f)
        {
            float inv = 1.0f / diPickerSqrt(sq);
            x *= inv;
            y *= inv;
        }
    }

    // Resolves the DI preview vector (dx, dy) in [-1, 1] for `mode` (main::DiVectorMode)
    // given launch velocity (kx, ky), fighter X position, stage center X, facing (+1/-1),
    // and live stick (liveX, liveY).
    inline DiVectorResult resolveDiVector(u8 mode, float kx, float ky, float posX, float stageCenterX, float facing,
                                          float liveX, float liveY)
    {
        static const float kDiag = 0.70710678f;
        // Fixed (awayMul, yVal) table for modes 1..7 (None, In, Out, UpIn, UpOut, DownIn, DownOut).
        static const s8 kAxis[7][2] = {
            {0, 0}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}, {-1, -1}, {1, -1}
        };
        DiVectorResult r = {liveX, liveY, false};
        if (mode == main::kDiVectorLive || mode >= main::kDiVectorModeCount)
        {
            diNormalizeUnit(r.dx, r.dy);
            return r;
        }

        r.overridden = true;
        float away = (kx > 0.01f) ? 1.0f : (kx < -0.01f) ? -1.0f
                   : (posX != stageCenterX) ? (posX > stageCenterX ? 1.0f : -1.0f)
                   : (facing >= 0.0f ? 1.0f : -1.0f);

        if (mode <= main::kDiVectorDownOut)
        {
            int ax = kAxis[mode - 1][0];
            int ay = kAxis[mode - 1][1];
            float s = (ax != 0 && ay != 0) ? kDiag : 1.0f;
            r.dx = away * static_cast<float>(ax) * s;
            r.dy = static_cast<float>(ay) * s;
        }
        else
        {
            // Survival DI: exact perpendicular turning the launch upward and inward.
            float len = diPickerSqrt(kx * kx + ky * ky);
            if (len < 0.0001f)
            {
                r.dx = -away * kDiag;
                r.dy = kDiag;
            }
            else if (kx > 0.001f)
            {
                r.dx = -ky / len;
                r.dy = kx / len;
            }
            else if (kx < -0.001f)
            {
                r.dx = ky / len;
                r.dy = -kx / len;
            }
            else
            {
                r.dx = -away;
                r.dy = 0.0f;
            }
        }
        diNormalizeUnit(r.dx, r.dy);
        return r;
    }

    // Compact readout for the 2D DI Compass header without string literals.
    inline void diVectorShortLabel(u8 mode, char out[16])
    {
        static const char kNames[9][7] = {
            {'L', 'i', 'v', 'e', 0, 0, 0},
            {'N', 'o', 'n', 'e', 0, 0, 0},
            {'I', 'n', 0, 0, 0, 0, 0},
            {'O', 'u', 't', 0, 0, 0, 0},
            {'U', 'p', '-', 'I', 'n', 0, 0},
            {'U', 'p', '-', 'O', 'u', 't', 0},
            {'D', 'n', '-', 'I', 'n', 0, 0},
            {'D', 'n', '-', 'O', 'u', 't', 0},
            {'S', 'u', 'r', 'v', 0, 0, 0}
        };
        out[0] = 'D';
        out[1] = 'I';
        out[2] = ':';
        out[3] = ' ';
        const char* src = kNames[mode < main::kDiVectorModeCount ? mode : 0];
        for (int i = 0; i < 7; i++)
        {
            out[4 + i] = src[i];
        }
    }

    // 2D unit-circle DI Compass rendered inside lab.cpp's 2D pass (implemented in di_display.cpp).
    void diCompassDraw(const game::MatchState& match, bool menuOpen);
    bool diCompassVisible(const game::MatchState& match, bool menuOpen);
}
}
