#pragma once

// Training-match control of Project+'s in-match Code Menu combo.
namespace tm {
namespace game {
    // True when P+'s in-match Code Menu combo (L+R+D-pad Down) should be
    // blocked during Training matches: always true in a drill, else follows
    // Controls > P+ Code Menu Shortcut (Off = blocked by default).
    bool codeMenuBlockedInTraining();

    // Applies or restores P+'s own "Code Menu Activation" option in RAM to
    // match codeMenuBlockedInTraining(). Safe to call mid-match when the
    // Controls row changes, on Reset All Settings, or when a drill swaps
    // settings aside or back.
    void codeMenuSync();

    // Called from main::attach(): enters a Training match and runs
    // codeMenuSync(). Leaves Main Menu access (VERSUS > CODES) and all other
    // stored option values untouched.
    void codeMenuMatchAttach();

    // Called from main::detach(): restores the player's saved "Code Menu
    // Activation" value in RAM.
    void codeMenuMatchDetach();
}
}
