#pragma once

#include "extra/hitbox_heatmap_core.h"
#include "main/game/match.h"

namespace tm {
namespace extra {
    void heatmapAttach();
    void heatmapDetach();
    void heatmapReset();
    void heatmapTick();
    void heatmapDraw(const game::MatchState& match);
    const HitboxBounds* heatmapBounds(int playerNo);
}
}
