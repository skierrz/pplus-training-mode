#pragma once

#include <string.h>
#include <types.h>

#include "extra/challenge_common_core.h"
#include "extra/stage_geom_math.h"

// Pure host-testable Project+ 3.2.1 Snake Infinite Recovery backend core.
// Verified P+ 3.2.1 Snake facts (FitSnake.pac, ItmCommonParam.pac, VariableSet.asm):
//  - ftKind = 0x2E (Fighter_Snake).
//  - Cypher: 0x114 Start -> 0x117 Hang (10.0 armor) -> 0x118 Cut.
//    LA-Bit[61] (0x1200003D) cleared on self-hit; LA-Float[52] (0x11000034)
//    0..6 diminishing height stacks reset on landing.
//  - C4: 0x11D Air drop, 0x119 Detonate (15 dmg self-hit).
//  - Grenade: 0x112 Start / 0x12D throw (90f fuse, 12 dmg self-hit).
namespace tm {
namespace extra {
    enum SnakeSelfHitKind {
        kSnakeSelfHitNone = 0,
        kSnakeSelfHitC4 = 1,
        kSnakeSelfHitGrenade = 2,
        kSnakeSelfHitOther = 3
    };

    enum SnakeRecoveryConst {
        kSnkFtSnake = 0x2E,
        kSnkMaxCypherStacks = 6,
        kSnkDetonateWindow = 45,
        kSnkGrenadeWindow = 180,

        kSnkStDamageAir = 0x44,
        kSnkStDamageFly = 0x45,
        kSnkStDamageFlyRoll = 0x46,
        kSnkStCliffCatchMove = 0x73,
        kSnkStCliffWait = 0x75,
        kSnkStDead = 0xBD,
        kSnkStSpecialN = 0x112,
        kSnkStCypherStart = 0x114,
        kSnkStCypherHang = 0x117,
        kSnkStCypherCut = 0x118,
        kSnkStC4Detonate = 0x119,
        kSnkStC4AirDrop = 0x11D,
        kSnkStGrenadeThrow = 0x12D
    };

    // 32-byte naturally aligned telemetry block mirrored directly into "TM4X".
    struct SnakeRecoveryStats {
        u32 refreshCount;
        u32 completedRecoveries;
        u16 loopChain;
        u16 bestLoopChain;
        u8 cypherStacks;
        u8 cypherLockout;
        u8 lastSelfHitKind;
        u8 offstage;
        float totalSelfDamage;
        float altitudeRecovered;
        float lastSelfHitDamage;
        u32 totalCyphers;
    };

    struct SnakeRecoveryState {
        SnakeRecoveryStats out;
        u8 prevLockout;
        u8 hasPrevDamage;
        u8 refreshedThisFrame;
        float prevDamage;
        float selfHitStartY;
        s32 lastC4DetonateFrame;
        s32 lastGrenadeFrame;
    };

    inline u8 snkClampStacks(float raw)
    {
        if (raw <= 0.0f)
        {
            return 0;
        }
        int v = static_cast<int>(raw + 0.5f);
        return v > kSnkMaxCypherStacks ? static_cast<u8>(kSnkMaxCypherStacks) : static_cast<u8>(v);
    }

    inline void snakeRecoveryResetTransient(SnakeRecoveryState& st)
    {
        st.out.offstage = 0;
        st.out.cypherStacks = 0;
        st.out.cypherLockout = 0;
        st.out.loopChain = 0;
        st.prevLockout = 0;
        st.hasPrevDamage = 0;
        st.refreshedThisFrame = 0;
        st.prevDamage = 0.0f;
        st.selfHitStartY = 0.0f;
        st.lastC4DetonateFrame = kChallengeNoFrame;
        st.lastGrenadeFrame = kChallengeNoFrame;
    }

    inline void snakeRecoveryResetAll(SnakeRecoveryState& st)
    {
        memset(&st, 0, sizeof(st));
        snakeRecoveryResetTransient(st);
    }

    inline bool snakeRecoveryStep(SnakeRecoveryState& st, const StageGeometry& g, const ChallengeFrame& in)
    {
        st.refreshedThisFrame = 0;
        if (in.ftKind != kSnkFtSnake || in.outOfPlay || in.status == kSnkStDead)
        {
            snakeRecoveryResetTransient(st);
            return false;
        }

        st.out.cypherStacks = snkClampStacks(in.cypherStacks);
        st.out.cypherLockout = in.cypherLockout ? 1 : 0;

        const int evN = in.eventCount < kChallengeMaxEvents ? static_cast<int>(in.eventCount) : kChallengeMaxEvents;
        bool enteredCypher = false;
        bool enteredAirDmg = false;
        bool grabbed = chInRange(in.status, kSnkStCliffCatchMove, kSnkStCliffWait);

        for (int k = 0; k < evN; k++)
        {
            const s16 s = in.events[k].status;
            if (s == kSnkStCypherStart && in.events[k].prev != kSnkStCypherStart)
            {
                enteredCypher = true;
            }
            else if (s == kSnkStC4Detonate)
            {
                st.lastC4DetonateFrame = in.frame;
            }
            else if (s == kSnkStSpecialN || s == kSnkStGrenadeThrow)
            {
                st.lastGrenadeFrame = in.frame;
            }
            else if (chInRange(s, kSnkStDamageAir, kSnkStDamageFlyRoll))
            {
                enteredAirDmg = true;
            }
            else if (chInRange(s, kSnkStCliffCatchMove, kSnkStCliffWait))
            {
                grabbed = true;
            }
        }
        if (enteredCypher || (in.status == kSnkStCypherStart && !st.prevLockout && st.out.cypherLockout))
        {
            st.out.totalCyphers++;
        }

        if (in.airborne && !grabbed && (!g.floor.valid || in.x < g.floor.leftX || in.x > g.floor.rightX || in.y < g.floor.centerY - 2.0f))
        {
            st.out.offstage = 1;
        }
        float alt = in.y - st.selfHitStartY;
        if (st.out.loopChain && (in.status == kSnkStCypherStart || chInRange(in.status, kSnkStCypherHang, kSnkStCypherCut))
            && alt > st.out.altitudeRecovered)
        {
            st.out.altitudeRecovered = alt;
        }

        float dmgDelta = st.hasPrevDamage ? (in.damage - st.prevDamage) : 0.0f;
        int dm = dmgDelta > 0.0f ? static_cast<int>(dmgDelta) : 0;
        if (in.airborne && !grabbed && !in.opponentDealtHit
            && (dm >= 1 || (st.prevLockout && !st.out.cypherLockout && enteredAirDmg))
            && (st.prevLockout || st.out.cypherStacks || st.out.offstage))
        {
            u8 kind = kSnakeSelfHitOther;
            if ((st.lastC4DetonateFrame != kChallengeNoFrame && in.frame - st.lastC4DetonateFrame <= kSnkDetonateWindow)
                || chInRange(static_cast<s16>(dm), 14, 17))
            {
                kind = kSnakeSelfHitC4;
            }
            else if ((st.lastGrenadeFrame != kChallengeNoFrame && in.frame - st.lastGrenadeFrame <= kSnkGrenadeWindow)
                     || chInRange(static_cast<s16>(dm), 10, 13))
            {
                kind = kSnakeSelfHitGrenade;
            }
            st.out.refreshCount++;
            chBumpStreak(st.out.loopChain, st.out.bestLoopChain);
            st.out.lastSelfHitKind = kind;
            st.out.lastSelfHitDamage = dm > 0 ? dmgDelta : 0.0f;
            st.out.totalSelfDamage += st.out.lastSelfHitDamage;
            st.selfHitStartY = in.y;
            st.refreshedThisFrame = 1;
        }

        if (grabbed || (!in.airborne && in.hitstun == 0 && !chInRange(in.status, kSnkStDamageAir, kSnkStDamageFlyRoll)))
        {
            if (st.out.loopChain)
            {
                st.out.completedRecoveries++;
            }
            st.out.loopChain = 0;
            st.out.offstage = 0;
        }

        st.prevLockout = st.out.cypherLockout;
        st.prevDamage = in.damage;
        st.hasPrevDamage = 1;
        return st.refreshedThisFrame != 0;
    }
}
}
