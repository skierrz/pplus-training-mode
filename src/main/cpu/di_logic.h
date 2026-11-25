#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/game/status_ids.h"

// Pure host-testable CPU Trajectory DI calculation for all 10 Tdi modes and
// hitstun-end edge detection. Keeps existing 0..4 indices (Random, In, Out,
// Survival, None) bit-for-bit stable while adding Combo, Slight Random,
// Slight Away, Down & Away, and Custom 2D stick vectors.
namespace tm {
namespace cpu {
    // True on the exact first frame hitstun hits 0 after hitstun >= 1 (including
    // crouch-cancel / weak-hit skips from >1 straight to 0), EXCEPT while locked
    // in Landing / LandingLight lag (where canAct() uses Landing_Frame instead).
    inline bool cpuJustOut(bool havePrev, int prevHitstun, int hitstun, int hitlag, bool air, int status)
    {
        const bool landing = !air && (status == game::kStatusLanding || status == game::kStatusLandingLight);
        return havePrev && prevHitstun >= 1 && hitstun == 0 && hitlag == 0 && !landing;
    }

    inline int resolveRandomTdi(u32 roll)
    {
        static const u8 kPick[7] = {
            kTdiInwards, kTdiOutwards, kTdiSurvival, kTdiCombo, kTdiSlightAway, kTdiDownAway, kTdiNone
        };
        return kPick[roll % 7u];
    }

    // Computes stick (outX, outY) for resolved `diKind` given normalized launch
    // unit vector (ux, uy), `toward` (+1 opponent right, -1 left), precomputed
    // `survivalTurn` (+1 CCW / -1 CW), per-hit `roll` (for Slight Random), and
    // user-pinned custom vector (customX, customY) in [-1, 1].
    inline void computeTdiStick(int diKind, float ux, float uy, float toward, float survivalTurn, u32 roll,
                                float customX, float customY, float& outX, float& outY)
    {
        // Inwards: clockwise for an upward launch with opponent on the right,
        // counter-clockwise for a downward one.
        const float inTurn = (uy >= 0.0f ? -1.0f : 1.0f) * toward;
        const float away = -toward;
        switch (diKind)
        {
        case kTdiInwards:
            outX = -uy * inTurn;
            outY = ux * inTurn;
            return;
        case kTdiOutwards:
            outX = -uy * (-inTurn);
            outY = ux * (-inTurn);
            return;
        case kTdiSurvival:
            outX = -uy * survivalTurn;
            outY = ux * survivalTurn;
            return;
        case kTdiCombo:
        {
            // Pull back toward the attacker / down into combo follow-up range.
            float absUy = uy < 0.0f ? -uy : uy;
            float absUx = ux < 0.0f ? -ux : ux;
            if (absUy >= absUx * 0.25f)
            {
                outX = -uy * inTurn;
                outY = ux * inTurn;
            }
            else
            {
                outX = toward;
                outY = -0.35f;
            }
            return;
        }
        case kTdiSlightRandom:
        {
            // Subtle partial-magnitude DI (~0.42) across 8 cardinal/diagonal angles.
            static const float kMag = 0.42f;
            static const float kDiag = 0.297f;
            static const float kAngles[8][2] = {
                {kMag, 0.0f}, {-kMag, 0.0f}, {0.0f, kMag}, {0.0f, -kMag},
                {kDiag, kDiag}, {-kDiag, kDiag}, {kDiag, -kDiag}, {-kDiag, -kDiag}
            };
            u32 idx = roll & 7u;
            outX = kAngles[idx][0];
            outY = kAngles[idx][1];
            return;
        }
        case kTdiSlightAway:
            outX = (-uy * (-inTurn)) * 0.42f;
            outY = (ux * (-inTurn)) * 0.42f;
            return;
        case kTdiDownAway:
            outX = away * 0.7071f;
            outY = -0.7071f;
            return;
        case kTdiCustom:
        {
            float x = customX;
            float y = customY;
            float sq = x * x + y * y;
            if (sq > 1.0f)
            {
                float r = sq;
                for (int i = 0; i < 5; i++)
                {
                    r = 0.5f * (r + sq / r);
                }
                x /= r;
                y /= r;
            }
            outX = x;
            outY = y;
            return;
        }
        case kTdiNone:
        default:
            outX = 0.0f;
            outY = 0.0f;
            return;
        }
    }
}
}
