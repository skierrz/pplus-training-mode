#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/cpu/view.h"
#include "main/game/match.h"
#include "main/ui/pages/scenarios.h"

class Fighter;

// Unified P+ Lab Scenario Presets runtime backend (src/extra/).
namespace tm {
namespace main {
    struct Settings;
}
namespace extra {
    void scenarioPresetsAttach();
    void scenarioPresetsDetach();
    void scenarioPresetsPrepareSwap();
    void scenarioPresetsRestoreAfterSwap(const ui::pages::ScenarioMenuState& saved);
    void scenarioPresetsOnRender(const game::MatchState& match);
    void scenarioPresetsFrame();
    void scenarioPresetsCall(int call);
    bool scenarioPresetsCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out,
                                u32 lastButtons);
    void scenarioPresetsSavedView(main::Settings& out);
    // Returns 0 (Right), 1 (Left), or 2 (Random) for Combo Trainer's
    // Recovery Trainer loop when Edgeguard scenario is active (2 otherwise).
    int scenarioPresetsEdgeguardSide();
}
}
