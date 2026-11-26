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
        kPresetComboLab = 1,
        kPresetTechchase = 2,
        kPresetEdgeguard = 3,
        kPresetShieldPressure = 4,
        kPresetShieldDrop = 5,
        kPresetLedgetech = 6,
        kPresetSlideOff = 7,
        kPresetGrabEscape = 8,
        kPresetMultiHitSdi = 9
    };

    struct ScenarioMenuState {
        u8 preset;            // extra::ScenarioId (0 = Off, 1..9 = presets)
        u8 autoReset;         // 0 = Off, 1 = On
        u8 side;              // 0 = Right, 1 = Left, 2 = Random
        u8 multiHitMove;      // 0 = Auto (Best), 1..8 explicit moves
        u8 oosAction;         // 0 = Grab .. 6 = Random, 7 = Custom Pool
        u8 ledgetechSpawn;    // 0 = Close Low, 1 = Deep Low, 2 = Far Out, 3 = High, 4 = Random
        u8 ledgetechDj;       // 0 = Available, 1 = Spent
        u8 ledgetechAttack;   // 0 = Dtilt, 1 = Ftilt, 2 = Fsmash, 3 = Down Smash [def], 4 = Dair, 5 = Neutral B, 6 = Side B, 7 = Random
        u8 shieldDropPlat;    // 0 = Left, 1 = Right, 2 = Top, 3 = Random
        u8 shieldDropSpacing; // 0 = Random, 1 = Close, 2 = Under, 3 = Far
        u8 shieldDropFacing;  // 0 = Toward, 1 = Away, 2 = Random
        u8 grabAction;        // 0 = Hold (Mash Out), 1 = Random Throw, 2..5 = F/B/U/D Throw, 6 = Stand (P1 Grabs)
        u8 grabPctMode;       // 0 = Random 0-79%, 1 = Current P1 %
        // Techchase
        u8 techSetup;         // 0 = Tumble Drop [def], 1 = Free Hit
        u8 techPosition;      // 0 = Ground [def], 1..4 = Platform 1..4
        u8 techEscape;        // 0 = Intang Flash [def], 1 = Spotdodge, 2 = Jab, 3 = Jump, 4 = Shield, 5 = Roll, 6 = None
        u8 techInvisMode;     // 0 = Off, 1 = CPU, 2 = All, 3 = Flash
        u8 techInvisDelay;    // 0..15f
        u8 techSound;         // 0/1
        u8 techPreDrift;      // 0..5 (main::PreTechDrift)
        u8 techTdi;           // 0 = Random, 1 = None, 2 = Away, 3 = Toward, 4 = Survival
        u8 techShowReaction;  // 0/1
        // Edgeguard
        u8 edgeguardSetup;    // 0 = Knockback Launch [def], 1 = Offstage Spawn
        u8 edgeguardStrength; // 0 = Weak, 1 = Normal [def], 2 = Strong, 3 = Random
        u8 edgeguardDist;     // 0 = Mid [def], 1 = Deep, 2 = High, 3 = Random
        u8 edgeguardDj;       // 0 = Available [def], 1 = Spent, 2 = Random
        u8 edgeguardMixup;    // 0 = Mixed [def], 1 = Low, 2 = High, 3 = Direct
        // Ledgetech
        u8 ledgetechStart;    // 0 = Revival Platform [def], 1 = Instant Drop
        u8 ledgetechTiming;   // 0 = Hold Active [def], 1 = Timed Swing
        // Shield Pressure / OoS Role
        u8 shieldRole;        // 0 = Offense [def], 1 = Defense
        u8 shieldDefAttack;   // 0 = Aerial [def], 1 = Tilt, 2 = Smash, 3 = Dash Attack, 4 = Getup Attack, 5 = Random
        // Active Shield Drop & Under-Platform Slide-Off
        u8 shieldDropAttack;  // 0 = SH Aerial [def], 1 = Tilt, 2 = Down Smash, 3 = Off
        u8 slideOffAttack;    // 0 = Under Up Air [def], 1 = Under Fair, 2 = Under Nair, 3 = On-Plat Ftilt
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
