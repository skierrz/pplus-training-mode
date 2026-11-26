#pragma once

#include <types.h>

class Fighter;
class soModuleEnumeration;

// Olimar / Pikmin Lab Kit runtime backend (src/extra/olimar_lab.cpp):
//  1. Pull Pool Filter (5-bit R/Y/B/W/P + legacy migration + all-0 clamp)
//  2. Per-Color Maturity Locks + Global Set All (+ setDefaultAll + isActiveArticle)
//  3. Starting Lineup (zero-flicker Entry interception + post-Entry/Rebirth swap)
//  4. Live Neutral-B + D-Pad Pluck (pre-step weight override on frame 0)
//  5. Lead Pikmin / Down-B Whistle Color (+0x1FE88 seed + Move Lead Now)
//  6. Instant D-Pad Lineup Presets (native despawn+respawn + safe gates + 15f latch)
namespace tm {
namespace extra {
    void olimarLabAttach();
    void olimarLabSync();
    void olimarLabApplyFighter(Fighter* fighter, const soModuleEnumeration* m, int status, int playerNo, bool isCpu);
    void olimarLabNoOlimarFrame();
    void olimarLabAction(u32 action, u32 dpadHeld, u32 dpadPressed);
    void olimarLabPrepareSwap();
    void olimarLabDetach();
}
}
