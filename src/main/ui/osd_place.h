#pragma once

#include "main/ui/hud_layout.h"

// Native 640x480 screen-space placement for each player's OSD message stack.
// Pure arithmetic shared between osd.cpp and host layout tests.
namespace tm {
namespace osd {
    enum Position {
        kPositionHud = 0,      // above each player's Brawl damage readout
        kPositionSides = 1,    // four lower-screen columns inside safe area
        kPositionTopLeft = 2,  // one row per player from top-left, growing right
        kPositionTopRight = 3, // mirrored from top-right, growing left
        kPositionCount
    };

    // Newest message box centre (x, y) and per-slot step (dx, dy) toward older.
    struct StackPlace {
        float x;
        float y;
        float dx;
        float dy;
    };

    // Right edge of each player's "%" readout in 640x480 2D space, derived from
    // Brawl's info_training.pac (InfoWide SCN0 / Info SCN0 camera + InfPos_TopN).
    static const float kHudRight[4] = {144.0f, 290.0f, 436.0f, 575.0f};
    static const float kHudRight43[4] = {155.0f, 301.0f, 448.0f, 595.0f};
    static const float kDigitsTop = 415.0f;
    static const float kHudGap = 16.0f;

    static const float kBoxWidth = 124.0f;
    static const float kBoxHeight = 46.0f;
    static const float kBoxGap = 4.0f;
    static const float kPitch = kBoxHeight + kBoxGap; // 50.0 px vertical stack pitch

    static const float kSidesX[4] = {96.0f, 245.0f, 395.0f, 544.0f};
    static const float kSidesY = 366.0f;
    static const float kTopRowGap = 10.0f;
    static const float kTopColGap = 6.0f;

    // `xs`: gfx::xScale() (0.75 on 16:9, 1.0 on 4:3). `floorY`: when positive,
    // highest allowed bottom edge for lower-screen stacks (HUD / Sides) so they
    // clear elements like the recording seek bar. Unknown positions fall back to HUD.
    inline StackPlace stackPlace(int position, int player, float xs, float floorY = 0.0f)
    {
        StackPlace s;
        s.dx = 0.0f;
        s.dy = -kPitch;
        if (player < 0 || player > 3)
        {
            player = 0;
        }
        float halfW = kBoxWidth * xs * 0.5f;
        float halfH = kBoxHeight * 0.5f;
        if (position == kPositionSides)
        {
            s.x = kSidesX[player];
            s.y = kSidesY;
        }
        else if (position == kPositionTopLeft || position == kPositionTopRight)
        {
            float marginX = xs >= 1.0f ? ui::layout::kSafeMarginX : 12.0f;
            float rowPitch = kBoxHeight + kTopRowGap;
            s.y = ui::layout::kSafeMarginY + halfH + player * rowPitch;
            if (position == kPositionTopLeft)
            {
                s.x = marginX + halfW;
                s.dx = (kBoxWidth + kTopColGap) * xs;
            }
            else
            {
                s.x = 640.0f - marginX - halfW;
                s.dx = -(kBoxWidth + kTopColGap) * xs;
            }
            s.dy = 0.0f;
        }
        else
        {
            const float* right = xs < 1.0f ? kHudRight : kHudRight43;
            s.x = right[player] - halfW;
            s.y = kDigitsTop - kHudGap - halfH;
        }
        // Keep the newest box inside the 5% CRT safe area horizontally on 4:3.
        if (xs >= 1.0f)
        {
            float minX = ui::layout::kSafeMarginX + halfW;
            float maxX = 640.0f - ui::layout::kSafeMarginX - halfW;
            if (s.x < minX)
            {
                s.x = minX;
            }
            else if (s.x > maxX)
            {
                s.x = maxX;
            }
        }
        if (floorY > 0.0f && position != kPositionTopLeft && position != kPositionTopRight)
        {
            float maxY = floorY - halfH;
            if (s.y > maxY)
            {
                s.y = maxY;
            }
        }
        return s;
    }
}
}
