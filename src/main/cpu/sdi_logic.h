#pragma once

#include <types.h>

// Pure host-testable CPU Dual-Stick SDI vector and per-hitlag-frame cadence
// helper. Because fn_8004897C is human-only and ButtonPresses.asm:310-341
// zeroes LA-Float[34..35] on AI, CPU Dual-Stick SDI drives aiInput's main stick
// on every consecutive hitlag frame (hitlag > 1) by alternating primary and
// secondary vectors that cross Brawl's +-0.35f flick threshold (0x80764FC0,
// 0x80AD7528) in both directions while staying >= 0.7f (IC-Basic[3173]).
namespace tm {
namespace cpu {
    const float kSdiThreshold = 0.7f;
    const float kSdiFlickThreshold = 0.35f;
    // Unit-vector components for alternating across +-0.35f cleanly:
    //  - cardinal primary: (0.30, 0.9539392) has 0.30 < 0.35 and len == 1.0
    //  - diagonal primary: (0.9165151, -0.40) has |-0.40| > 0.35 and len == 1.0
    const float kDualBelowFlick = 0.30f;
    const float kDualMajor = 0.9539392f;
    const float kDualFlipAxis = 0.40f;
    const float kDualKeepAxis = 0.9165151f;

    inline float sdiAbs(float v)
    {
        return v < 0.0f ? -v : v;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline float sdiSqrt(float v)
    {
        if (v <= 0.0f)
        {
            return 0.0f;
        }
        float r = v > 1.0f ? v * 0.5f : 1.0f;
        for (int i = 0; i < 20; i++)
        {
            r = 0.5f * (r + v / r);
        }
        return r;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // One of 8 directions (0 for the centre); within 22.5 degrees of an axis
    // counts as on it (sin(pi/8) = 0.38268f).
    inline int sdiSector8(float x, float y)
    {
        float len = sdiSqrt(x * x + y * y);
        if (len < 0.001f)
        {
            return 0;
        }
        float k = 0.38268f * len;
        return (x > k ? 1 : (x < -k ? 2 : 0)) + (y > k ? 3 : (y < -k ? 6 : 0));
    }

    inline bool sdiFlicksInGame(float prevX, float prevY, float curX, float curY)
    {
        bool fx = (curX >= kSdiFlickThreshold && prevX < kSdiFlickThreshold)
                  || (curX <= -kSdiFlickThreshold && prevX > -kSdiFlickThreshold);
        bool fy = (curY >= kSdiFlickThreshold && prevY < kSdiFlickThreshold)
                  || (curY <= -kSdiFlickThreshold && prevY > -kSdiFlickThreshold);
        return (fx || fy) && (curX * curX + curY * curY >= kSdiThreshold * kSdiThreshold);
    }

    // Computes a unit-length secondary vector for Dual-Stick SDI that:
    //  1) has magnitude 1.0 (>= 0.7 SDI threshold),
    //  2) lies in a distinct 8-way sector from primary,
    //  3) crosses Brawl's +-0.35f axis flick threshold in BOTH transitions
    //     (primary -> secondary and secondary -> primary),
    //  4) avoids downward Y when grounded and primary is horizontal or upward.
    inline void dualStickSecondaryVector(float primaryX, float primaryY, bool grounded, float awayDir, float& outX,
                                         float& outY)
    {
        float len = sdiSqrt(primaryX * primaryX + primaryY * primaryY);
        if (len < 0.01f)
        {
            outX = 0.0f;
            outY = 0.0f;
            return;
        }
        float px = primaryX / len;
        float py = primaryY / len;
        float ax = sdiAbs(px);
        float ay = sdiAbs(py);
        float sx = px < 0.0f ? -1.0f : 1.0f;
        float sy = py < 0.0f ? -1.0f : 1.0f;

        if (ay < kSdiFlickThreshold)
        {
            // Horizontal primary: primary has |px| >= 0.35 and |py| < 0.35.
            // Secondary puts |x| = 0.30 (< 0.35) and |y| = 0.954 (>= 0.35),
            // avoiding downward Y when grounded.
            float ySign = (grounded || py >= 0.0f) ? 1.0f : -1.0f;
            outX = sx * kDualBelowFlick;
            outY = ySign * kDualMajor;
        }
        else if (ax < kSdiFlickThreshold)
        {
            // Vertical primary: primary has |py| >= 0.35 and |px| < 0.35.
            // Secondary puts |x| = 0.954 (>= 0.35) and |y| = 0.30 (< 0.35).
            float xSign = (awayDir < 0.0f) ? -1.0f : 1.0f;
            if (ax > 0.01f)
            {
                xSign = sx;
            }
            outX = xSign * kDualMajor;
            outY = sy * kDualBelowFlick;
        }
        else
        {
            // Diagonal primary: both |px| >= 0.35 and |py| >= 0.35.
            // Flip one axis past +-0.35 (to +-0.40) so both transitions flick.
            // Flip X when |py| > |px|, or when grounded with upward Y and balanced axes.
            bool flipX = (ay > ax) || (grounded && py > 0.0f && ax <= 0.75f);
            if (flipX)
            {
                outX = -sx * kDualFlipAxis;
                outY = sy * kDualKeepAxis;
            }
            else
            {
                outX = sx * kDualKeepAxis;
                outY = -sy * kDualFlipAxis;
            }
        }
    }

    struct HitlagSdiInput {
        bool dualStick;
        bool grounded;
        int hitlag;
        int hitlagFrame;
        int sdiDone;
        int sdiAmount;
        float sdiX, sdiY;
        bool haveAsdi;
        float asdiX, asdiY;
        float tdiX, tdiY;
        float awayDir;
    };

    struct HitlagSdiOutput {
        float x, y;
        bool holdStill;
        bool consumedSdi;
    };

    inline HitlagSdiOutput stepHitlagSdi(const HitlagSdiInput& in)
    {
        HitlagSdiOutput out = {0.0f, 0.0f, false, false};
        if (in.hitlag <= 1)
        {
            out.x = in.haveAsdi ? in.asdiX : in.tdiX;
            out.y = in.haveAsdi ? in.asdiY : in.tdiY;
            out.holdStill = true;
            return out;
        }
        if (in.sdiDone >= in.sdiAmount)
        {
            return out;
        }
        if (!in.dualStick)
        {
            if ((in.hitlagFrame & 1) == 0)
            {
                out.x = in.sdiX;
                out.y = in.sdiY;
                out.consumedSdi = true;
            }
            return out;
        }
        float len = sdiSqrt(in.sdiX * in.sdiX + in.sdiY * in.sdiY);
        if (len < 0.01f)
        {
            return out;
        }
        if ((in.hitlagFrame & 1) == 0)
        {
            out.x = in.sdiX / len;
            out.y = in.sdiY / len;
        }
        else
        {
            dualStickSecondaryVector(in.sdiX, in.sdiY, in.grounded, in.awayDir, out.x, out.y);
        }
        out.consumedSdi = true;
        return out;
    }
}
}
