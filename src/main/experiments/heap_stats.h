#pragma once

#include <types.h>

#include "main/game/match.h"

// Heap measurements, built only with `make EXPERIMENTS=1`. While the main
// module is loaded (a Training match), logs the whole heap table at fixed
// points and, when it is detached, the least free space every heap had.
namespace tm {
namespace experiments {
    // Called once per render callback.
    void heapStatsOnRender(u32 call, const game::MatchState& match);

    // Called from detach, before the stub frees the module.
    void heapStatsOnDetach();
}
}
