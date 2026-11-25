#pragma once

#include "extra/grab_visualizer_core.h"
#include "main/game/match.h"

namespace tm {
namespace extra {
    void grabVizAttach();
    void grabVizDetach();
    void grabVizDraw(const game::MatchState& match);
}
}
