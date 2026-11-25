#pragma once

#include <types.h>

#include "extra/combo_trainer_core.h"
#include "main/game/match.h"

// Combo Trainer backend (src/extra/) adapted from fudgepop01's
// SuperTraining onto P+ 3.2.1 APIs.
namespace tm {
namespace extra {
    void comboTrainerAttach();
    void comboTrainerDetach();
    void comboTrainerReset();
    void comboTrainerFrame();
    void comboTrainerDraw(const game::MatchState& match);
    void comboTrainerSetAnchors(float hmnX, float hmnY, float hmnLr, float cpuX, float cpuY);
    void comboTrainerRepositionCpu();

    // Latest meter readings for playerNo (0..3), or NULL if invalid.
    const ComboMeterValues* comboTrainerMeters(int playerNo);
}
}
