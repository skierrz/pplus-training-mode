#pragma once

#include <types.h>

#include "main/game/match.h"

struct gfPadStatus;

// Native Brawl / Project+ in-match Skill Drill HUD, End-Screen Summary Modal,
// and RAM telemetry block ("TMDH") in the extra module.
namespace tm {
namespace extra {
    enum {
        kDrillHudDebugMagic = 0x544D4448u, // "TMDH"
        kDrillHudDebugVersion = 1u
    };

    struct DrillHudDebugBlock {
        u32 magic;        // 0x00: kDrillHudDebugMagic ("TMDH")
        u32 version;      // 0x04: kDrillHudDebugVersion (1)
        u8 activeDrill;   // 0x08: drills::DrillId (0..6)
        u8 repsDone;      // 0x09: 0..10
        u8 repsHit;       // 0x0A: 0..10
        u8 streak;        // 0x0B: current hit streak
        u8 bestStreak;    // 0x0C: best streak in this set
        u8 newBest;       // 0x0D: 1 if session best improved
        u8 modalOpen;     // 0x0E: 1 while End-Screen Modal is open
        u8 lastGrade;     // 0x0F: DrillGrade (0..4)
        u16 bestScore;    // 0x10: persisted session best for activeDrill
        u16 retryCount;   // 0x12: increments each time Retry resets the set
        s16 bestMetric;   // 0x14: drill-specific highlight metric
        u8 forceComplete; // 0x16: write 1 in test harness to open End-Screen
        u8 alcNotice;     // 0x17: 1 when Auto L-Cancel rule is active
        u32 drawCalls;    // 0x18: increments when Live HUD or Modal draws
    };

    void drillHudAttach();
    void drillHudDetach();
    void drillHudFrame();
    void drillHudSavestateReset();
    void drillHudDraw(const game::MatchState& match);
    u8 drillHudActiveId();
    bool drillHudModalOpen();

    // Unified hook for match module: pads != NULL handles pad phase (returns 1
    // if consumed); pads == NULL returns bit 0 = active, bit 1 = modal open.
    u32 drillHudHook(u32 phase, gfPadStatus* pads);
}
}
