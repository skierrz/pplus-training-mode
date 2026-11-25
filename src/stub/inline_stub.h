#pragma once

#include <types.h>

// Knowledge of the stub sy_core (Syriinge 0.6.0) builds for an inline hook,
// and the fix for the restore-order defect in the sy_core.rel that ships with
// Project+ 3.2.1.
namespace tm {
namespace inline_stub {
    enum Result {
        kCorrect,  // restores r0 and LR correctly; left alone
        kFixed,    // had the swapped restore order; repaired in place
        kUnknown   // not a layout we recognise; nothing was changed
    };

    // `stub` is where the hooked site branches to; `site` is the hooked
    // address. Logs the stub's words and what was found.
    Result checkAndFix(u32* stub, u32 site);

    // True if `stub` looks like the stub for `site` whose first word is `original`.
    bool belongsTo(const u32* stub, u32 site, u32 original);

    const char* describe(Result result);
}
}
