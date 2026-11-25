#pragma once

#include <types.h>

#include "main/game/match.h"

// General > DI Display: in hitlag, the path a fighter will fly for the stick
// held now (trajectory DI and ASDI); grey: no DI; yellow and orange: smash
// DI made in hitlag (main fighters only). Once launched, the real flight to
// the end of hitstun or the stage (white bar: landing; yellow cross: wall or
// ceiling), then dashed for no tech (slide, or bounce off an upright wall or
// level ceiling). Red past a blast zone. Reached via main/extra.h's hooks.
namespace tm {
namespace extra {
    // Checks the game data the display reads (once per attach).
    void diAttach();
    // From the render hook while a match runs, in the main camera.
    void diDraw(const game::MatchState& m);
    // Savestate loaded: the smash DI trails of the left timeline go.
    void diReset();
    // Each game step with whether the DI Display is shown, and false when
    // the module detaches: Project+'s own Draw DI is switched off while it
    // is, and its Code Menu value is put back as it was when it is not.
    void diHideDrawDi(bool hide);

    // Writing a fighter's player number + 1 here (the harness does) logs the
    // predicted points of that fighter's paths every time they are drawn.
    extern u8 g_diTrace;
}
}
