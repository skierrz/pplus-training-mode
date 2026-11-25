#pragma once

#include <types.h>

// Runtime adaptive CPU backend in the extra module.
// Tracks fighter movement and RPS habits when cpu.options.adaptiveMode != Off,
// exposes RAM-inspectable AdaptiveDebugBlock ("TMAC"), and provides counter
// action biasing when adaptiveMode == Adaptive.
namespace tm {
namespace extra {
    void adaptiveCpuAttach();
    void adaptiveCpuDetach();
    void adaptiveCpuReset();
    void adaptiveCpuFrame();
    // Returns a cpu/actions.cpp index for `list` (kListGround or kListShield)
    // if adaptive biasing applies, or -1 otherwise. When `commit` is false,
    // peeks without mutating mixup counters, cooldown, or biasTriggers.
    int adaptiveCpuBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit);
}
}
