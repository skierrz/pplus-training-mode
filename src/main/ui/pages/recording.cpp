#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/storage/recording_file.h"
#include "main/ui/pages/pages.h"

// Conditional availability: most rows require saved positions; CPU rows
// require CPU Control: Lab; Loop and Auto Restore disable while recording;
// Mirrored Playback requires playback and locks both modes.
#define TM_REC_ROW(kind, f, max, labels, def, name, onChange, enabled)                                      \
    TM_ROW(::tm::ui::kind, TM_HALF(f), TM_FIELD(f), 0, max, def, name, labels, onChange, enabled)
#define TM_REC_CHOICE(name, f, labels, def, onChange, enabled) \
    TM_REC_ROW(kKindChoice, f, TM_COUNT(labels) - 1, labels, def, name, onChange, enabled)
#define TM_REC_TOGGLE(name, f, def, enabled) \
    TM_REC_ROW(kKindToggle, f, 1, NULL, def, name, NULL, enabled)
#define TM_REC_FUNC(name, onSelect, enabled) \
    TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, name, NULL, onSelect, enabled)

namespace tm {
namespace ui {
namespace pages {
    static const char* const kHmnMode[] = {"Off", "Record", "Playback", "Re-Record"};
    static const char* const kCpuMode[] = {"Off", "Control", "Record", "Playback", "Re-Record"};
    static const char* const kSlot[] = {"Random", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6"};
    static const char* const kMirrored[] = {"Off", "On", "Random"};
    static const char* const kCpuCounter[] = {"Off", "After Playback", "On CPU Hit", "On HMN Hit", "On Any Hit"};
    static const char* const kAutoRestore[] = {"Off", "Playback Ends", "CPU Counters"};
    static const char* const kTakeover[] = {"HMN", "CPU", "None"};

    static bool saveRow();

    // Non-const so row 0 switches between Save Positions and Restore Positions.
    static Option kRows[] = {
        TM_REC_FUNC("Save Positions", record::saveOrRestore, saveRow),
        TM_REC_CHOICE("HMN Mode", recording.hmnMode, kHmnMode, 0, record::hmnModeChanged, record::modeRowsAvailable),
        TM_REC_CHOICE("HMN Record Slot", recording.hmnSlot, kSlot, 1, record::slotChanged, record::modeRowsAvailable),
        TM_REC_CHOICE("CPU Mode", recording.cpuMode, kCpuMode, 0, record::cpuModeChanged, record::cpuRowsAvailable),
        TM_REC_CHOICE("CPU Record Slot", recording.cpuSlot, kSlot, 1, record::slotChanged, record::cpuRowsAvailable),
        TM_REC_CHOICE("Mirrored Playback", recording.mirrored, kMirrored, 0, record::mirroredChanged,
                      record::mirrorRowAvailable),
        TM_REC_CHOICE("CPU Counter", recording.cpuCounter, kCpuCounter, 1, NULL, record::cpuRowsAvailable),
        TM_REC_TOGGLE("Loop Playback", recording.loop, 0, record::loopRowsAvailable),
        TM_REC_CHOICE("Auto Restore", recording.autoRestore, kAutoRestore, 0, NULL, record::loopRowsAvailable),
        TM_REC_TOGGLE("Start Paused", recording.startPaused, 0, record::positionsSaved),
        TM_REC_CHOICE("Playback Takeover", recording.takeover, kTakeover, 0, NULL, record::positionsSaved),
        TM_REC_TOGGLE("Start On Input", recording.startOnFirstInput, 0, record::positionsSaved),
        TM_REC_FUNC("Re-Save Positions", record::resavePositions, record::positionsSaved),
        TM_REC_FUNC("Prune Positions", record::prunePositions, record::positionsSaved),
        TM_REC_FUNC("Delete Positions", record::deletePositions, record::positionsSaved),
        TM_MENU_IF("Slot Management", P_SLOTS, record::positionsSaved),
        TM_MENU_IF("HMN Slot Chances", P_HMN_CHANCES, record::positionsSaved),
        TM_MENU_IF("CPU Slot Chances", P_CPU_CHANCES, record::positionsSaved),
        TM_REC_FUNC("Export Recording", storage::exportRecording, storage::exportAvailable)
    };

    static bool saveRow()
    {
        kRows[0].name = record::positionsSaved() ? "Restore Positions" : "Save Positions";
        return true;
    }

    extern const Page kRecording = TM_PAGE("Recording", kRows);
}
}
}

#undef TM_REC_ROW
#undef TM_REC_CHOICE
#undef TM_REC_TOGGLE
#undef TM_REC_FUNC
