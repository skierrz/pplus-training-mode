#pragma once

#include <types.h>

// Shared per-fighter frame snapshot and compact helpers passed into all five
// Stage 4C pure challenge cores (zero Brawl headers; compiles on MWCC 4.2 and
// PC host).
namespace tm {
namespace extra {
    enum {
        kChallengeMaxEvents = 8,
        kChallengeNoFrame = -1
    };

    struct ChallengeEvent {
        s16 status;
        s16 prev;
        s16 motionKind;
    };

    struct ChallengeFrame {
        s32 frame;
        int ftKind;
        s32 entryId;
        s16 status;
        s16 motionKind;
        s16 hitstun;
        s16 xluFrames;
        u8 valid;
        u8 airborne;
        u8 actionable;
        u8 outOfPlay;
        u8 ledgeGrabs;
        u8 cypherLockout;
        u8 opponentDealtHit;
        u8 eventCount;
        u32 groups;
        u32 triggerButtons;
        float x;
        float y;
        float vx;
        float lr;
        float damage;
        float cypherStacks;
        ChallengeEvent events[kChallengeMaxEvents];
    };

    inline bool chInRange(s16 v, s16 lo, s16 hi)
    {
        return static_cast<u32>(static_cast<s32>(v) - static_cast<s32>(lo))
               <= static_cast<u32>(static_cast<s32>(hi) - static_cast<s32>(lo));
    }

    inline u16 chClampU16(s32 v, s32 lo)
    {
        if (v < lo)
        {
            v = lo;
        }
        return v > 65535 ? static_cast<u16>(65535) : static_cast<u16>(v);
    }

    inline void chBumpStreak(u16& streak, u16& best)
    {
        if (++streak > best)
        {
            best = streak;
        }
    }
}
}
