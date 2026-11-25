#pragma once

#include "main/game/match.h"

// Live read-only PSA AnimCmd 11-Thread & FSM Inspector rendered inside the 2D
// HUD pass (src/extra/psa_inspector.cpp).
namespace tm {
namespace extra {
    void psaInspectorAttach();
    void psaInspectorDetach();
    void psaInspectorDraw(const game::MatchState& match, bool menuOpen);
}
}
