#pragma once

#include <types.h>

class Fighter;
class soModuleEnumeration;

// Per-fighter mechanics backend (P+ 3.2.1 fighters): applies stored
// charge, resource, float, and Dedede Waddle Toss overrides across every
// matching fighter in the match, and restores patched .pac bytes on detach.
namespace tm {
namespace extra {
    void fighterMechanicsAttach();
    void fighterMechanicsSync();
    void fighterMechanicsApplyFighter(Fighter* fighter, const soModuleEnumeration* m, int ftKind, int status,
                                      int playerNo = 0, bool isCpu = false);
    // Mid-match fighter swap: restores patched FitDedede.pac Waddle weights
    // while resident and clears the cache before stLoaderPlayer frees files.
    void fighterMechanicsPrepareSwap();
    void fighterMechanicsDetach();
}
}
