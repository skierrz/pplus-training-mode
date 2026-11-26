#pragma once

namespace tm {
namespace game {
    struct MatchState {
        bool running;  // in scMelee and the match has started
        bool training; // running, and the game mode is Training
        bool paused;   // training, and Training's pause menu is open
    };

    // Reads the state from the game each call; a few loads, no allocation.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    MatchState x_matchState();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline MatchState matchState() { return x_matchState(); }
#else
    MatchState matchState();
#endif
#else
    MatchState matchState();
#endif

    // Ends the running Training match as Training's Finish does, back to its
    // character select (docs/internals.md, "How Training's Finish ends a
    // match"): kEndByWait never shows the pause, kEndFromPause closes it
    // first. The game's next steps end the match and must run. Refused
    // outside a running Training match or once its operator is ending it.
    enum EndResult { kEndRefused, kEndFromPause, kEndByWait };
    EndResult endTrainingMatch();
    // Training's operator has left its running state: Finish is ending the match.
    bool trainingEnding();

    // Row of Training's pause menu the cursor is on (0 Speed ... 7 Finish),
    // or -1 if the menu is not readable.
    enum { kTrainingMenuFinish = 7 };
    int trainingMenuCursor();

    // True if the game renders for 16:9 (the game's own screen setting).
    bool widescreen();
}
}
