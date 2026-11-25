#pragma once

#include <types.h>

#include "main/game/match.h"

// Runtime adaptive CPU backend in the extra module.
// Tracks fighter movement and RPS habits when cpu.options.adaptiveMode != Off,
// exposes RAM-inspectable AdaptiveDebugBlock ("TMAC"), draws a compact native
// Brawl/P+ Habit Readout HUD card, and provides counter action biasing when
// adaptiveMode == Adaptive.
namespace tm {
namespace extra {
    void adaptiveCpuAttach();
    void adaptiveCpuDetach();
    void adaptiveCpuReset();
    void adaptiveCpuFrame();
    void adaptiveCpuDraw(const game::MatchState& match);
    // Returns a cpu/actions.cpp index for `list` (kListGround or kListShield)
    // if adaptive biasing applies, or -1 otherwise. When `commit` is false,
    // peeks without mutating mixup counters, cooldown, or biasTriggers.
    int adaptiveCpuBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit);
}
}
