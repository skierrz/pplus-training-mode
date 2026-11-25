#pragma once

#include <types.h>

// P+ 3.2.1 Stage Options backend: dynamic stage/module/Ground detection,
// CHR0 moving-platform freeze and positioning, Fountain of Dreams platform
// heights, and live Hazards ON/OFF transitions for _ht stage modules.
namespace tm {
namespace extra {
    void stageOptionsAttach();
    // main::StageOptionsCall dispatcher.
    bool stageOptions(int call);
    void stageOptionsDetach();
}
}
