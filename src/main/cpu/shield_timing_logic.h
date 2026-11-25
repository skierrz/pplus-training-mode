#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/game/status_ids.h"

namespace tm {
namespace cpu {
    enum {
        // Safety cap (in post-shieldstun hold frames) so an infinite loop move
        // such as Attack100 never locks the CPU in shield indefinitely.
        kFullMoveMaxHold = 60
    };

    static const float kFullMoveMaxDistSq = 45.0f * 45.0f;

    // Statuses from which a fighter can deliver an active melee / special hit
    // on shield (used to avoid latching onto an unrelated status on projectile
    // block).
    inline bool isAttackerMoveStatus(int st)
    {
        return (st >= game::kStatusAttack && st <= game::kStatusAttackAirFirst)
               || (st >= game::kStatusSpecialN && st < game::kStatusFinal);
    }

    struct ShieldHoldInput {
        u8 shieldTiming;
        bool attackerReadable;
        int latchedStatus;
        int attackerStatus;
        int attackerHitstun;
        bool attackerDamageHitlag;
        bool attackerCanAct;
        bool attackerHasMoreHits;
        float distSq;
        u8 holdFrames;
    };

    // Decides whether a shielding CPU that just became actionable out of
    // shieldstun should keep holding shield for the next hit of an ongoing
    // multi-hit move instead of firing its OoS counter in the first gap.
    inline bool shouldHoldFullMoveShield(const ShieldHoldInput& in)
    {
        if (in.shieldTiming != kShieldTimingFullMove || !in.attackerReadable || in.latchedStatus < 0)
        {
            return false;
        }
        if (in.holdFrames >= kFullMoveMaxHold || in.distSq > kFullMoveMaxDistSq)
        {
            return false;
        }
        if (in.attackerStatus != in.latchedStatus || !isAttackerMoveStatus(in.attackerStatus))
        {
            return false;
        }
        if (in.attackerHitstun > 0 || in.attackerDamageHitlag || in.attackerCanAct)
        {
            return false;
        }
        return in.attackerHasMoreHits;
    }
}
}
