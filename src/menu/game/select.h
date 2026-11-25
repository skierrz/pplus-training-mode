#pragma once

#include <types.h>

// Training's character select shaped by the chosen drill's rule
// (shared/select_rules.h): its characters preset in the selection data, one
// player area for a drill without a CPU, the coins of preset characters
// locked, the grid cut to the icons a coin
// can take, and the icons the held coin can't take dark and dead. The select reads all of this when it is built, so a choice that
// changes the rule rebuilds it (reopen). Every change to the game's code is
// checked before it is made and undone at detach.
namespace tm {
namespace menu {
namespace select {
    // At the module's attach, before the select's task is built: shapes the
    // select for `drill` (0: none, the normal select).
    void attach(int drill);

    // At the module's detach. `toStageSelect`: the player left with Start
    // for the stage select (not a direct start or a reopen); a drill without
    // a CPU then gets one in the selection data (see select.cpp).
    void detach(bool toStageSelect);

    // Every frame while the select runs: darkens the icons the held coin
    // can't be dropped on.
    void update();

    // The select was built for another rule than `drill`'s.
    bool needsRebuild(int drill);

    // Leaves the select and builds it again (sqTraining's state 3, which
    // opened it). NULL on success, else why not (nothing was changed).
    const char* reopen();
}
}
}
