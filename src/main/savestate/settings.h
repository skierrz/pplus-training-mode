#pragma once

#include <types.h>

// Choices of the savestate feature's settings (main::Settings: controls
// for the D-pad bindings, recording for the random percent).
namespace tm {
namespace savestate {
    // What D-pad left / right do in a running match (Controls page).
    enum DpadLeft {
        kLeftLoadState = 0,
        kLeftNone,
        kLeftCount
    };
    enum DpadRight {
        kRightSaveState = 0,
        kRightNone,
        kRightCount
    };

    enum { kMaxRandomPercent = 200 };
}
}
