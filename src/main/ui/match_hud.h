#pragma once

#include <types.h>

#include "main/game/match.h"
#include "shared/game/mem.h"

// Native Brawl match timer HUD piece: the centre HUD's match timer (top
// centre, "MM:SS ff", frames 00-59). Active in Training matches only. A caller
// requests the timer on every render call it should show; otherwise it is
// hidden automatically. Changes the centre HUD's flags (byte 0: 0x80, 0x40)
// and its scene group.
namespace tm {
namespace ui {
    // Brawl's match timer showing `frames` (game frames, 60 a second).
    void showMatchTimer(s32 frames);
    // Once per render call, after everything that asks: shows, sets or hides
    // the timer.
    void matchHudApply(const game::MatchState& m);

    // True while Brawl's native right-side Training Info/Combo HUD is visible:
    // IfMngr (0x805A02D0) +0x04 == 5 (Training) -> IfTraining* (+0x78) -> u8 +0x30.
    // Verified against RSBE01 rev 2 (0x800DC1EC / 0x80104F64).
    inline bool matchHudCounterShown()
    {
#ifdef __MWERKS__
        u8* manager = *reinterpret_cast<u8**>(0x805A02D0u);
        if (!game::isRam(manager) || game::wordAt(manager, 0x4) != 5u)
        {
            return false;
        }
        u8* training = reinterpret_cast<u8*>(game::wordAt(manager, 0x78));
        return game::isRam(training) && training[0x30] != 0u;
#else
        return false;
#endif
    }
}
}
