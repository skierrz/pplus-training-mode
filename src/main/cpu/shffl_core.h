#pragma once

#include <stddef.h>
#include <types.h>

#include "main/cpu/actions.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/fighter_input.h"
#include "main/game/status_ids.h"

// Pure host-testable CPU Auto L-Cancel eligibility and SHFFL / FFL timing
// script builder.
//
// P+ 3.2.1 L-Cancel rules (Source/Project+/L-Cancel.asm:39-196):
//  - In AttackAir (0x33), a fresh Guard press (bit 0x8 in held & pressed) sets
//    LA-Basic[90] (0x1000005A) to 7 (or 8 during hitstop in execStop).
//  - execStatus decrements LA-Basic[90] each non-hitlag frame (it goes <= 0
//    when expired or never pressed).
//  - On landing into LandingAttackAir (0x18), LA-Basic[90] > 0 halves landing
//    lag and triggers the white flash for all characters including tethers.
//  - Guard (0x8) must never fire during IASA (or the frame IASA opens) or
//    outside AttackAir so the CPU never airdodges (EscapeAir 0x21) or Zairs
//    (AttackAirCatch 0x7F).
namespace tm {
namespace cpu {
    enum {
        // Guard injected up to 5 frames before predicted landing (2f margin inside
        // P+'s 7-frame LA-Basic[90] window).
        kAutoLCancelLead = 5,
        // Interrupt groups that would let a Guard press trigger EscapeAir or Zair.
        kAutoLCancelUnsafeGroups = (1u << game::kGroupAirEscape) | (1u << game::kGroupAirLasso)
                                   | (1u << game::kGroupAirAttack),
        // Maximum Step entries emitted by buildShfflScript() including TM_END.
        kMaxShfflSteps = 20,
        // Number of alternating down/neutral fastfall flick pairs.
        kShfflFlickPairs = 5,
        // Aerial / fastfall frame delays per timing preset.
        kShfflEarlyAerialDelay = 2,
        kShfflEarlyFfDelay = 1,
        kShfflNormalAerialDelay = 4,
        kShfflNormalFfDelay = 2,
        // Post-apex delay for Late FFL when starting in tumble (cannot fastfall
        // in tumble before the aerial leaves Damage_Fall).
        kShfflLateTumbleDelay = 4
    };

    const float kAutoLCancelNearHeight = 4.0f;

    struct AutoLCancelInput {
        bool enabled;
        int status;
        bool air;
        bool canActNow;
        int actInFrames;       // -1 unknown, 0 actionable now, 1 opens this coming frame
        int hitlag;
        int lCancelTimer;      // LA-Basic[90] (0x1000005A): > 0 means active window
        bool prevGuardHeld;    // (lastButtons & kGuard) != 0
        bool attackPressed;    // (out.buttons & kAttack) != 0
        float vy;
        int framesToLand;      // 0..kAutoLCancelLead if landing soon, else -1
        bool hasGround;
        float heightAboveGround;
    };

    inline bool shouldAutoLCancel(const AutoLCancelInput& in)
    {
        if (!in.enabled || in.status != game::kStatusAttackAirFirst || !in.air)
        {
            return false;
        }
        if (in.canActNow || in.actInFrames == 0 || in.actInFrames == 1)
        {
            return false;
        }
        if (in.hitlag > 1 || in.lCancelTimer > 0 || in.prevGuardHeld || in.attackPressed)
        {
            return false;
        }
        if (in.framesToLand >= 0 && in.framesToLand <= kAutoLCancelLead)
        {
            return true;
        }
        return (in.vy <= 0.0f || in.hitlag == 1) && in.hasGround && in.heightAboveGround >= -1.0f
               && in.heightAboveGround <= kAutoLCancelNearHeight;
    }

    // Resolves kShfflRandom (4) to 0..3 via `roll`; invalid values fall back to
    // kShfflFramePerfect (0).
    inline u8 resolveShfflTiming(u8 setting, u32 roll)
    {
        if (setting == kShfflRandom)
        {
            return static_cast<u8>(roll % 4u);
        }
        return setting < kShfflRandom ? setting : static_cast<u8>(kShfflFramePerfect);
    }

    inline Step makeStep(u16 buttons, s8 x, s8 y, u8 ref, u8 frames, u8 until)
    {
        Step s = {buttons, x, y, ref, frames, until, 0};
        return s;
    }

    // Populates `out[0..n-1]` (terminated with a 0-frame sentinel) for a SHFFL
    // (`shortHop == true`) or FFL (`shortHop == false`) aerial under
    // `timingSetting`. Returns the number of steps written including the
    // terminator (0 if `out == NULL` or `cap < 2`).
    inline int buildShfflScript(Step* out, int cap, const Step& aerial, bool shortHop, u8 timingSetting, u32 roll,
                                u8* resolvedTimingOut = NULL)
    {
        u8 timing = resolveShfflTiming(timingSetting, roll);
        if (resolvedTimingOut != NULL)
        {
            *resolvedTimingOut = timing;
        }
        if (out == NULL || cap < 2)
        {
            return 0;
        }
        int n = 0;
        const int limit = cap - 1;
        const u8 fallWait = shortHop ? 60 : 240;

        if (shortHop)
        {
            if (n < limit)
            {
                out[n++] = makeStep(button::kJump, 0, 0, kRefAbs, 1, kUntilNone);
            }
            if (n < limit)
            {
                out[n++] = makeStep(0, 0, 0, kRefAbs, 30, kUntilTakeoff);
            }
        }

        if (timing == kShfflLate)
        {
            if (shortHop && n < limit)
            {
                // Leave Jump_Squat cleanly on the takeoff frame before checking kUntilFalling.
                out[n++] = makeStep(0, 0, 0, kRefAbs, 1, kUntilNone);
            }
            if (n < limit)
            {
                out[n++] = makeStep(0, 0, 0, kRefAbs, fallWait, kUntilFalling);
            }
            if (!shortHop && n < limit)
            {
                // Post-apex delay for Late FFL so the aerial lands later than Frame-Perfect/Normal.
                out[n++] = makeStep(0, 0, 0, kRefAbs, kShfflLateTumbleDelay, kUntilNone);
            }
            // Late shield-pressure aerial at apex, followed immediately by fastfall.
            if (n < limit)
            {
                out[n++] = makeStep(aerial.buttons, aerial.x, aerial.y, aerial.ref, 1, kUntilNone);
            }
            for (int i = 0; i < kShfflFlickPairs && n + 1 < limit; i++)
            {
                out[n++] = makeStep(0, 0, -100, kRefAbs, 1, kUntilFastfall);
                out[n++] = makeStep(0, 0, 0, kRefAbs, 1, kUntilFastfall);
            }
        }
        else
        {
            u8 aerialDelay = 0;
            u8 ffDelay = 0;
            if (timing == kShfflEarly)
            {
                aerialDelay = kShfflEarlyAerialDelay;
                ffDelay = kShfflEarlyFfDelay;
            }
            else if (timing == kShfflNormal)
            {
                aerialDelay = kShfflNormalAerialDelay;
                ffDelay = kShfflNormalFfDelay;
            }
            if (aerialDelay > 0 && n < limit)
            {
                out[n++] = makeStep(0, 0, 0, kRefAbs, aerialDelay, kUntilNone);
            }
            if (n < limit)
            {
                out[n++] = makeStep(aerial.buttons, aerial.x, aerial.y, aerial.ref, 1, kUntilNone);
            }
            if (n < limit)
            {
                out[n++] = makeStep(0, 0, 0, kRefAbs, fallWait, kUntilFalling);
            }
            if (ffDelay > 0 && n < limit)
            {
                out[n++] = makeStep(0, 0, 0, kRefAbs, ffDelay, kUntilNone);
            }
            for (int i = 0; i < kShfflFlickPairs && n + 1 < limit; i++)
            {
                out[n++] = makeStep(0, 0, -100, kRefAbs, 1, kUntilFastfall);
                out[n++] = makeStep(0, 0, 0, kRefAbs, 1, kUntilFastfall);
            }
        }

        out[n++] = makeStep(0, 0, 0, kRefAbs, 0, kUntilNone);
        return n;
    }
}
}
