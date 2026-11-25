#include "main/ui/pages/scenarios.h"

#include <stddef.h>

#include "main/extra.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

// Unified P+ Lab Scenario Presets and Combo Trainer entry.
// Preset values live in ui::menuScratch() at kScenarioScratchOffset (session-only).
namespace tm {
namespace ui {
namespace pages {
    static bool comboTrainerAvailable()
    {
        return cpuLabControl() && main::extraLoaded();
    }

    static bool scenarioActive()
    {
        return main::extraLoaded() && scenarioMenuState()->preset != kPresetOff;
    }

    static bool scenarioSideActive()
    {
        u8 p = scenarioMenuState()->preset;
        return main::extraLoaded() && p != kPresetOff && p != kPresetShieldDrop;
    }

#pragma dont_inline on
    static bool presetIs(u8 id)
    {
        return main::extraLoaded() && scenarioMenuState()->preset == id;
    }
#pragma dont_inline reset

    static bool scenarioShieldPressureActive()
    {
        return presetIs(kPresetShieldPressure);
    }

    static bool scenarioShieldDropActive()
    {
        return presetIs(kPresetShieldDrop);
    }

    static bool scenarioLedgetechActive()
    {
        return presetIs(kPresetLedgetech);
    }

    static bool scenarioMultiHitActive()
    {
        return presetIs(kPresetMultiHitSdi);
    }

    static const char* const kPresets[] = {
        "Off", "Combo Lab", "Techchase", "Edgeguard", "Shield Pressure / OoS",
        "Shield Drop", "Ledgetech", "Slide-Off", "Grab Escape", "P+ Multi-Hit SDI"
    };
    static const char* const kSides[] = {"Right", "Left", "Random"};
    static const char* const kOosActions[] = {
        "Grab", "Up B", "Spotdodge", "Roll Away", "SH Nair", "Wavedash", "Random"
    };
    static const char* const kLedgetechSpawns[] = {
        "Close Low", "Deep Low", "Far Out", "High", "Random"
    };
    static const char* const kLedgetechDj[] = {"Available", "Spent"};
    static const char* const kLedgetechAttacks[] = {
        "Down Tilt", "Fwd Tilt", "F-Smash", "D-Smash", "Down Air", "Neutral B", "Side B", "Random"
    };
    static const char* const kShieldDropPlats[] = {"Left", "Right", "Top", "Random"};
    static const char* const kShieldDropSpacing[] = {"Random", "Close", "Under", "Far"};
    static const char* const kShieldDropFacing[] = {"Toward", "Away", "Random"};
    static const char* const kMultiHitMoves[] = {
        "Auto (Best)", "Nair", "Fair", "Up Air", "Down Air", "Rapid Jab", "Up B", "Side B", "Down B"
    };

#define TM_SCEN_OFF(f) static_cast<u16>(kScenarioScratchOffset + offsetof(ScenarioMenuState, f))

    static const Option kRows[] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SCEN_OFF(preset), 0, TM_COUNT(kPresets) - 1, 0,
               "Preset", kPresets, main::extraScenarioChanged, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(autoReset),
               0, 1, 1, "Auto-Reset", NULL, main::extraScenarioChanged, scenarioActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(side), 0,
               TM_COUNT(kSides) - 1, 0, "Side", kSides, main::extraScenarioChanged, scenarioSideActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(oosAction),
               0, TM_COUNT(kOosActions) - 1, 0, "CPU OoS", kOosActions, main::extraScenarioChanged,
               scenarioShieldPressureActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(ledgetechSpawn), 0, TM_COUNT(kLedgetechSpawns) - 1, 0, "Spawn Position", kLedgetechSpawns,
               main::extraScenarioChanged, scenarioLedgetechActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(ledgetechDj), 0, TM_COUNT(kLedgetechDj) - 1, 0, "P1 Double Jump", kLedgetechDj,
               main::extraScenarioChanged, scenarioLedgetechActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(ledgetechAttack), 0, TM_COUNT(kLedgetechAttacks) - 1, 0, "CPU Edgeguard", kLedgetechAttacks,
               main::extraScenarioChanged, scenarioLedgetechActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(shieldDropPlat), 0, TM_COUNT(kShieldDropPlats) - 1, 0, "Platform", kShieldDropPlats,
               main::extraScenarioChanged, scenarioShieldDropActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(shieldDropSpacing), 0, TM_COUNT(kShieldDropSpacing) - 1, 0, "CPU Spacing", kShieldDropSpacing,
               main::extraScenarioChanged, scenarioShieldDropActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(shieldDropFacing), 0, TM_COUNT(kShieldDropFacing) - 1, 0, "CPU Facing", kShieldDropFacing,
               main::extraScenarioChanged, scenarioShieldDropActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(multiHitMove), 0, TM_COUNT(kMultiHitMoves) - 1, 0, "Multi-Hit Move", kMultiHitMoves,
               main::extraScenarioChanged, scenarioMultiHitActive),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Reset Now", NULL,
               main::extraScenarioResetNow, scenarioActive),
        TM_MENU_IF("Combo Trainer", P_COMBO_TRAINER, comboTrainerAvailable)
    };
#undef TM_SCEN_OFF

    extern const Page kScenarios = TM_PAGE("Scenarios", kRows);
}
}
}
