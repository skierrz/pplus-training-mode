#pragma once

#include <types.h>

// Character RNG control: forces deterministic outcomes for Peach's turnip
// pull and forward smash, Luigi's side-special misfire, and Game & Watch's
// Judge number across every matching fighter in the match.
namespace tm {
namespace extra {
    // Forgets what the last match found (once per attach).
    void characterRngAttach();
    // main::CharacterRngCall: apply the page's settings (frame ended,
    // savestate loaded, row changed), or say whether such a fighter is in
    // the match.
    bool characterRng(int call);
    // Mid-match fighter swap: restores patched FitPeach.pac odds and Dedede
    // FitDedede.pac Waddle weights while resident and drops caches before
    // stLoaderPlayer frees the outgoing fighter's .pac files.
    void characterRngPrepareSwap();
    // Puts back what was changed in the game's data.
    void characterRngDetach();
}
}
