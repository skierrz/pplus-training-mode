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

    // Pure snapshot of Training CSS coin & dropdown state used to gate B-back
    // to the Boot Menu without interfering with coin recall, CPU coin dragging,
    // or tag/CPU dropdowns.
    struct CssBackState {
        bool selectRunning;
        bool areaValid;
        int playerKind;       // muSelCharPlayerArea::m_playerKind (1 = Human)
        int controllerPort;   // muSelCharPlayerArea::m_controllerNo (0..7, -1 = none)
        bool handHoldingAny;  // hand->m_selCharCoin != NULL
        bool handHoldingOwn;  // hand->m_selCharCoin == area->m_selCharCoin
        bool ownCoinPlaced;   // coin->m_selCharHand == NULL (B recalls P1's coin)
        bool anyDropdownOpen; // any player area's hand->m_mode != 0
    };

    inline bool canBackFromCss(const CssBackState& st, int port)
    {
        if (!st.selectRunning || !st.areaValid || st.playerKind != 1 || st.anyDropdownOpen)
        {
            return false;
        }
        if (st.controllerPort >= 0 && st.controllerPort != port)
        {
            return false;
        }
        if (st.ownCoinPlaced)
        {
            return false;
        }
        if (st.handHoldingAny && !st.handHoldingOwn)
        {
            return false;
        }
        return true;
    }

    // True when pressing B on `port` in Training CSS has no placed P1 coin to
    // recall, is not dragging CPU's coin, and no tag/CPU dropdown is open.
    bool canBackToBootMenu(int port);

    // Zeroes muSelCharHand::m_bButtonCnt across CSS areas when B steps back to
    // the Boot Menu so Brawl's hold-B-to-Solo gauge never fires underneath.
    void clearBHoldCounter();
}
}
}
