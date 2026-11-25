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
    enum {
        kPresetOff = 0,
        kPresetMultiHitSdi = 10
    };

    static bool comboTrainerAvailable()
    {
        return cpuLabControl() && main::extraLoaded();
    }

    static bool scenarioActive()
    {
        return main::extraLoaded() && scenarioMenuState()->preset != kPresetOff;
    }

    static bool scenarioMultiHitActive()
    {
        return main::extraLoaded() && scenarioMenuState()->preset == kPresetMultiHitSdi;
    }

    static const char* const kPresets[] = {
        "Off",          "Combo Lab",  "Techchase", "Edgeguard", "Shield Pressure", "OoS / Reversal",
        "Shield Drop",  "Ledgetech",  "Slide-Off", "Grab Escape", "P+ Multi-Hit SDI"
    };
    static const char* const kSides[] = {"Right", "Left"};
    static const char* const kMultiHitMoves[] = {"Up Air", "Side B", "Rapid Jab", "Down Air"};

#define TM_SCEN_OFF(f) static_cast<u16>(kScenarioScratchOffset + offsetof(ScenarioMenuState, f))

    static const Option kRows[] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SCEN_OFF(preset), 0, TM_COUNT(kPresets) - 1, 0,
               "Preset", kPresets, main::extraScenarioChanged, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(autoReset),
               0, 1, 1, "Auto-Reset", NULL, main::extraScenarioChanged, scenarioActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(side), 0,
               TM_COUNT(kSides) - 1, 0, "Side", kSides, main::extraScenarioChanged, scenarioActive),
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
