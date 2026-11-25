#pragma once

#include "main/game/match.h"

// Multi-controller Input Viewer rendered inside the 2D HUD pass using resident
// info_training.pac button sheets via uiAssetsDrawIcon() and pure GX stick gates
// and analog trigger bars.
namespace tm {
namespace extra {
    void inputViewerAttach();
    void inputViewerDetach();
    void inputViewerDraw(const game::MatchState& match, bool menuOpen);
}
}
