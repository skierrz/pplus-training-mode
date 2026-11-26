#pragma once

#include <types.h>

#include "main/game/match.h"

// Break the Targets!: stage-aware 3D Brawl target challenge.
// Runtime state and streamed Brawl target texture live in Heaps::Network
// (~0 B .bss, 0 B leak on deactivate or detach).
namespace tm {
namespace extra {
    void comboBreakRushAttach();
    void comboBreakRushDetach();
    void comboBreakRushReset();
    void comboBreakRushFrame();
    void comboBreakRushDraw(const game::MatchState& match);
    void comboBreakRushDraw3d(const game::MatchState& match);

    // Clean external API (wired into Challenges boot menu and L+Up mode cycle):
    // mode: 0 = Off, 1 = Timed 30s, 2 = Timed 60s, 3 = Free Practice.
    void comboBreakRushSetActive(u8 mode);
    void comboBreakRushCycleMode();
    bool comboBreakRushActive();
}
}
