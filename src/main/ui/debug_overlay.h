#pragma once

#include <types.h>

#include "main/game/match.h"

namespace tm {
namespace ui {
    // Draws the debug overlay (version, frame counter, scene and match state)
    // in the top-left corner. `call` counts render-hook calls from 1.
    void drawDebugOverlay(u32 call, const game::MatchState& match);
}
}
