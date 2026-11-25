#pragma once

#include <types.h>

#include "extra/challenge_cancel_sprint_core.h"
#include "extra/challenge_flow_course_core.h"
#include "extra/challenge_reaction_techchase_core.h"
#include "extra/challenge_recovery_floorhug_core.h"
#include "extra/challenge_snake_recovery_core.h"

// Live Project+ 3.2.1 runtime adapter and "TM4X" RAM debug telemetry block for
// the five Stage 4C Track D novel P+ challenge backends. Strictly read-only
// observation across all tracked fighters (game::kMaxTracked). Working slot
// engines, Reaction Lab sample rings, and the "TM4X" block live in
// Heaps::Network so extra .bss cost stays near-zero.
namespace tm {
namespace extra {
    enum {
        kChallengeDebugMagic = 0x544D3458u, // "TM4X"
        kChallengeDebugVersion = 1u,
        kChallengeDebugSlots = 4
    };

    // Naturally aligned 200-byte (0xC8) per-player snapshot for live RAM reads.
    struct ChallengeDebugSlot {
        // Header (0x00 .. 0x07, 8 bytes)
        u8 used;
        s8 playerNo;
        u8 ftKind;
        u8 pad0;
        u32 frame;

        CancelSprintStats cs;      // 0x08 .. 0x23 (28 bytes)
        FlowCourseStats fc;        // 0x24 .. 0x47 (36 bytes)
        ReactionTechchaseStats rt; // 0x48 .. 0x77 (48 bytes)
        RecoveryFloorhugStats rf;  // 0x78 .. 0xA7 (48 bytes)
        SnakeRecoveryStats snk;    // 0xA8 .. 0xC7 (32 bytes)
    };

    struct ChallengeDebugBlock {
        u32 magic;      // 0x00: kChallengeDebugMagic ("TM4X")
        u32 version;    // 0x04: kChallengeDebugVersion
        u32 frame;      // 0x08: trackerFrame()
        u32 resetCount; // 0x0C: incremented on savestate / explicit reset
        ChallengeDebugSlot slots[kChallengeDebugSlots];
    };

    void challengeBackendsAttach();
    void challengeBackendsDetach();
    void challengeBackendsReset();
    void challengeBackendsFrame();

    const ChallengeDebugBlock* challengeDebugBlock();
}
}
