#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/cpu/view.h"

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
    void scenarioPresetsFrame();
    void scenarioPresetsCall(int call);
    bool scenarioPresetsCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out,
                                u32 lastButtons);
    void scenarioPresetsSavedView(main::Settings& out);
}
}
