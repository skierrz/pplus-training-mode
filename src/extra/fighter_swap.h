#pragma once

#include <types.h>

#include "main/game/match.h"

// Mid-session fighter swap runtime (src/extra/): runs a complete pre-swap
// teardown barrier, writes gmPlayerInitData + gmSelCharData (and clears P+
// AltZ/AltR), and lets Brawl's stLoaderPlayer + stOperatorFighterChange
// execute the async remove/load/spawn pipeline.
namespace tm {
namespace extra {
    void fighterSwapAttach();
    void fighterSwapDetach();
    void fighterSwapTick(const game::MatchState& match);

    // ExtraHooks callbacks (main/extra.h).
    bool fighterSwapRow();
    void fighterSwapChanged();
    void fighterSwapApply();
    bool fighterSwapBusy();
}
}
