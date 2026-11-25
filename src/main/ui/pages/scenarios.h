#pragma once

#include <types.h>

#include "main/ui/menu.h"
#include "main/ui/pages/alter_inputs.h"

// Session-only Scenario Preset menu values, stored in ui::menuScratch() after
// AlterCopy so they are never persisted to the SD card and never auto-warp
// fighters on boot.
namespace tm {
namespace ui {
namespace pages {
    enum {
        kScenarioScratchOffset = 24
    };

    struct ScenarioMenuState {
        u8 preset;       // extra::ScenarioId (0 = Off)
        u8 autoReset;    // 0 = Off, 1 = On
        u8 side;         // 0 = Right, 1 = Left
        u8 multiHitMove; // 0 = Up Air, 1 = Side B, 2 = Rapid Jab, 3 = Down Air
    };

    TM_STATIC_ASSERT(kScenarioScratchOffset >= sizeof(AlterCopy), tm_scenario_scratch_after_alter_copy);
    TM_STATIC_ASSERT(kScenarioScratchOffset + sizeof(ScenarioMenuState) <= kScratchBytes,
                     tm_scenario_scratch_fits_menu_scratch);

    inline ScenarioMenuState* scenarioMenuState()
    {
        return reinterpret_cast<ScenarioMenuState*>(menuScratch() + kScenarioScratchOffset);
    }
}
}
}
