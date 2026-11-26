#pragma once

#include <types.h>

// Training's character select and the way into a match from it, for the
// training menu. Every address and offset is from RSBE01 rev 2 with Project+
// 3.2.1 and is checked before use (see training.cpp).
namespace tm {
namespace menu {
namespace training {
    // A Training match a drill starts: P1 (human) against one CPU, both in
    // their first costume.
    struct MatchSetup {
        u8 p1Kind; // character kind (gmCharacterKind)
        u8 p1Icon; // the same character's icon id on the character select
        u8 cpuKind;
        u8 cpuIcon;
        u16 stage; // srStageKind
    };

    // True while the game renders 16:9.
    bool widescreen();

    // The controller port (0-3 GameCube, 4-7 Wii) Training's menus follow:
    // gmSetRule+0x24 if connected, else the first connected port (GC 0-3
    // before Wii 4-7). Returns -1 when no controller is connected.
    int controllingPort();

    // True while Training's character select runs normally: not starting,
    // not leaving.
    bool selectRunning();

    // The exit code the character select is leaving with (gfSceneManager
    // +0x284): 1 to the stage select (or a drill's match), 0 back to the
    // main menu. Only meaningful while it exits.
    int exitCode();

    // Checks the game code the direct start relies on. NULL if it is as
    // expected, else why not; logged once by the caller.
    const char* checkStartPath();

    // Costumes (gmPlayerInitData +5, +6), Project+'s alternate-stage button
    // word, and stage hazard state (0 hazards ON, 1 hazards OFF) for a match
    // an imported recording starts.
    struct Looks {
        u8 p1ColorFile;
        u8 p1Color;
        u8 cpuColorFile;
        u8 cpuColor;
        u16 altStage;
        u8 hazardOff;
    };

    // Starts a Training match with `setup`, skipping the stage select, as if
    // the stage select had just been left with a stage chosen: writes the
    // players and stage into the selection data and lets the character
    // select end. P1 uses controller `port`. NULL on success, else why not
    // (nothing was written). Without `looks`, first costumes and the stage's
    // normal layout.
    const char* startMatch(const MatchSetup& setup, int port, const Looks* looks = NULL);

    // While Training's character select is running, exits cleanly back to
    // Brawl's Solo menu (exit code 0 in sqTraining state 4 -> state 11).
    // NULL on success, else why not (nothing was changed).
    const char* exitToSolo();

    // While Training's character select is exiting toward the stage select
    // (exit code 1, sqTraining in state 4): writes `stage` into the selection
    // data and moves sqTraining to state 6 so the stage select is skipped and
    // sqTraining::setupTraining builds the match from the players the select
    // just wrote (keeping costumes, tags, ports and solo/CPU rules). NULL on
    // success, else why not (nothing was written).
    const char* skipStageSelect(u16 stage);
}
}
}
