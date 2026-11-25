#pragma once

#include <types.h>

#include "main/ui/menu.h"
#include "main/ui/pages/scenarios.h"

// Session-only Fighter Swap menu values, stored in ui::menuScratch() after
// ScenarioMenuState so they are never persisted to the SD card and never touch
// main::Settings.
namespace tm {
namespace ui {
namespace pages {
    enum {
        kFighterSwapScratchOffset = 28
    };

    struct FighterSwapMenuState {
        u8 p1Fighter;  // 0..kFighterSwapCount-1
        u8 p1Costume;  // 0..numColors-1
        u8 cpuFighter; // 0..kFighterSwapCount-1
        u8 cpuCostume; // 0..numColors-1
    };

    TM_STATIC_ASSERT(kFighterSwapScratchOffset >= kScenarioScratchOffset + sizeof(ScenarioMenuState),
                     tm_fighter_swap_scratch_after_scenarios);
    TM_STATIC_ASSERT(kFighterSwapScratchOffset + sizeof(FighterSwapMenuState) <= kScratchBytes,
                     tm_fighter_swap_scratch_fits_menu_scratch);

    inline FighterSwapMenuState* fighterSwapMenuState()
    {
        return reinterpret_cast<FighterSwapMenuState*>(menuScratch() + kFighterSwapScratchOffset);
    }
}
}
}
