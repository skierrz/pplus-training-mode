#pragma once

#include <types.h>

#include "main/game/match.h"

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
}
}
