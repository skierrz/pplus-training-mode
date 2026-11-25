#pragma once

#include <types.h>

#include "main/ui/menu.h"

// Session-only Scenario Preset menu values, stored in ui::menuScratch() past
// kShowInactiveScratchOffset / kRevTriggerScratchOffset so they are never
// persisted to the SD card and never auto-warp fighters on boot.
namespace tm {
namespace ui {
namespace pages {
    enum {
        kScenarioScratchOffset = 36,
        kPresetOff = 0,
        kPresetShieldPressure = 4,
        kPresetShieldDrop = 5,
        kPresetLedgetech = 6,
        kPresetGrabEscape = 8,
        kPresetMultiHitSdi = 9
    };

    struct ScenarioMenuState {
        u8 preset;            // extra::ScenarioId (0 = Off, 1..9 = presets)
        u8 autoReset;         // 0 = Off, 1 = On
        u8 side;              // 0 = Right, 1 = Left, 2 = Random
        u8 multiHitMove;      // 0 = Auto (Best), 1..8 explicit moves
        u8 oosAction;         // 0 = Grab, 1 = Up B, 2 = Spotdodge, 3 = Roll Away, 4 = SH Nair, 5 = Wavedash, 6 = Random
        u8 ledgetechSpawn;    // 0 = Close Low, 1 = Deep Low, 2 = Far Out, 3 = High, 4 = Random
        u8 ledgetechDj;       // 0 = Available, 1 = Spent
        u8 ledgetechAttack;   // 0 = Down Tilt .. 6 = Side B, 7 = Random
        u8 shieldDropPlat;    // 0 = Left, 1 = Right, 2 = Top, 3 = Random
        u8 shieldDropSpacing; // 0 = Random, 1 = Close, 2 = Under, 3 = Far
        u8 shieldDropFacing;  // 0 = Toward, 1 = Away, 2 = Random
        u8 grabAction;        // 0 = Hold (Mash Out), 1 = Random Throw, 2..5 = F/B/U/D Throw, 6 = Stand (P1 Grabs)
        u8 grabPctMode;       // 0 = Random 0-79%, 1 = Current P1 %
    };

    TM_STATIC_ASSERT(kScenarioScratchOffset > kShowInactiveScratchOffset + 2,
                     tm_scenario_scratch_after_flags);
    TM_STATIC_ASSERT(kScenarioScratchOffset + sizeof(ScenarioMenuState) <= kScratchBytes,
                     tm_scenario_scratch_fits_menu_scratch);

    inline ScenarioMenuState* scenarioMenuState()
    {
        return reinterpret_cast<ScenarioMenuState*>(menuScratch() + kScenarioScratchOffset);
    }
}
}
}
