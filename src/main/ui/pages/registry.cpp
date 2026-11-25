#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    bool never()
    {
        return false;
    }

    // Indexed by PageId.
    extern const Page* const kPages[P_COUNT] = {
        &kGeneral,      &kScenarios,    &kCpuOptions,     &kRecording,   &kDisplayOptions, &kOsdMenu,
        &kStageCast,    &kControls,     &kDebug,          &kComboTrainer, &kComboMeters,   &kTechOptions,
        &kAdvancedCounter, &kSlotManagement, &kAlterInputs, &kHmnChances, &kCpuChances,    &kHmnOverlays,
        &kCpuOverlays,  &kHmnInfo,      &kCpuInfo,        &kLedgedashHud, &kFighterSwap, &kTheme,
        &kReversalSlots, &kCpuDi,       &kCpuCounters,    &kCpuDefense
    };
}
}
}
