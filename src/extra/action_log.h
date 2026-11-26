#pragma once

#include "main/game/match.h"

// Brawl/P+ Live State Strip (Display > Action Log): tracks the last 8 fighter
// state transitions and exact frame durations in Heaps::Network and renders a
// CRT-safe double-framed pill strip.
namespace tm {
namespace extra {
    void actionLogAttach();
    void actionLogDetach();
    void actionLogReset();
    void actionLogFrame();
    void actionLogDraw(const game::MatchState& match);
}
}
