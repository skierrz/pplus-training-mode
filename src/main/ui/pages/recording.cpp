#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/storage/recording_file.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

// Conditional availability: post-save rows hide until positions are saved
// (kFlagHideWhenDisabled); CPU rows require CPU Control: Lab; Loop and Auto
// Restore disable while recording; Mirrored Playback locks both modes.
#define TM_REC_CHOICE(name, f, labels, count, def, onChange, enabled)                                       \
    TM_ROW_CHOICE_P(TM_HALF(f) | ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(f), count, def, name, labels,    \
                    onChange, enabled)
#define TM_REC_TOGGLE(name, f, def, enabled)                                                                \
    TM_ROW(::tm::ui::kKindToggle, TM_HALF(f) | ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(f), 0, 1, def,     \
           name, NULL, NULL, enabled)
#define TM_REC_FUNC(name, onSelect, enabled) \
    TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, name, NULL, onSelect, enabled)
#define TM_REC_MENU(name, page, enabled) \
    TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, ::tm::ui::pages::page, 0, name, NULL, NULL,  \
           enabled)

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_BEGIN(kHmnMode) TM_L("Off") TM_L("Record") TM_L("Playback") TM_L("Re-Record") TM_LABELS_END
    TM_LABELS_BEGIN(kCpuMode) TM_L("Off") TM_L("Control") TM_L("Record") TM_L("Playback") TM_L("Re-Record") TM_LABELS_END
    TM_LABELS_BEGIN(kSlot) TM_L("Random") TM_L("Slot 1") TM_L("Slot 2") TM_L("Slot 3") TM_L("Slot 4") TM_L("Slot 5") TM_L("Slot 6") TM_LABELS_END
    TM_LABELS_BEGIN(kMirrored) TM_L("Off") TM_L("On") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kCpuCounter) TM_L("Off") TM_L("After Playback") TM_L("On CPU Hit") TM_L("On P1 Hit") TM_L("On Any Hit") TM_LABELS_END
    TM_LABELS_BEGIN(kAutoRestore) TM_L("Off") TM_L("Playback Ends") TM_L("CPU Counters") TM_LABELS_END
    TM_LABELS_BEGIN(kTakeover) TM_L("P1") TM_L("CPU") TM_L("None") TM_LABELS_END
    TM_LABELS_ASSERT(kHmnMode, 4, tm_rec_hmn_mode_match);
    TM_LABELS_ASSERT(kCpuMode, 5, tm_rec_cpu_mode_match);
    TM_LABELS_ASSERT(kSlot, 7, tm_rec_slot_match);
    TM_LABELS_ASSERT(kMirrored, 3, tm_rec_mirror_match);
    TM_LABELS_ASSERT(kCpuCounter, 5, tm_rec_ctr_match);
    TM_LABELS_ASSERT(kAutoRestore, 3, tm_rec_auto_match);
    TM_LABELS_ASSERT(kTakeover, 3, tm_rec_take_match);
#endif

    static bool saveRow();

#ifdef __MWERKS__
    extern Page kRecording = {"Recording", NULL, 0, {0, 0, 0}};
    static Option* s_recRows = NULL;

    static bool saveRow()
    {
        if (s_recRows != NULL && pageStringsReady())
        {
            s_recRows[0].name = pageStr(record::positionsSaved() ? kPs_rec_restore : kPs_rec_save);
        }
        return true;
    }

    void initRecordingPage(Option* buf)
    {
        s_recRows = buf;
        static const u16 kStrs[7] = {
            kPs_rec_kHmnMode, kPs_rec_kSlot, kPs_rec_kCpuMode,
            kPs_rec_kMirrored, kPs_rec_kCpuCounter, kPs_rec_kAutoRestore,
            kPs_rec_kTakeover
        };
        static const void* const kExtra[1] = {record::g_deletePosLabel};
        static void (*const kCbs[10])() = {
            NULL, record::saveOrRestore, record::hmnModeChanged, record::slotChanged, record::cpuModeChanged,
            record::mirroredChanged, record::resavePositions, record::prunePositions,
            record::deletePositionsPressed, storage::exportRecording
        };
        static bool (*const kGates[8])() = {
            NULL, saveRow, record::modeRowsAvailable, record::cpuRowsAvailable, record::mirrorRowAvailable,
            record::loopRowsAvailable, record::positionsSaved, storage::exportAvailable
        };
        const u8 kH = kFlagHideWhenDisabled;
        static const RowSpec kSpecs[19] = {
            {kKindFunc,   0,  0, 0, 0, 0, 0xFF, 1, 1, 0},
            {kKindChoice, kH, TM_FIELD(recording.hmnMode), TM_CHOICE_LO, 0, 3, 0, 2, 2, 0},
            {kKindChoice, kH, TM_FIELD(recording.hmnSlot), TM_CHOICE_LO, 1, 6, 1, 3, 2, 0},
            {kKindChoice, kH, TM_FIELD(recording.cpuMode), TM_CHOICE_LO, 0, 4, 2, 4, 3, 0},
            {kKindChoice, kH, TM_FIELD(recording.cpuSlot), TM_CHOICE_LO, 1, 6, 1, 3, 3, 0},
            {kKindChoice, kH, TM_FIELD(recording.mirrored), TM_CHOICE_LO, 0, 2, 3, 5, 4, 0},
            {kKindChoice, kH, TM_FIELD(recording.cpuCounter), TM_CHOICE_LO, 1, 4, 4, 0, 3, 0},
            {kKindToggle, kH, TM_FIELD(recording.loop), 0, 0, 1, 0xFF, 0, 5, 0},
            {kKindChoice, kH, TM_FIELD(recording.autoRestore), TM_CHOICE_LO, 0, 2, 5, 0, 5, 0},
            {kKindToggle, kH, TM_FIELD(recording.startPaused), 0, 0, 1, 0xFF, 0, 6, 0},
            {kKindChoice, kH, TM_FIELD(recording.takeover), TM_CHOICE_LO, 0, 2, 6, 0, 6, 0},
            {kKindToggle, kH, TM_FIELD(recording.startOnFirstInput), 0, 0, 1, 0xFF, 0, 6, 0},
            {kKindFunc,   kH, 0, 0, 0, 0, 0xFF, 6, 6, 0},
            {kKindFunc,   kH, 0, 0, 0, 0, 0xFF, 7, 6, 0},
            {kKindFunc,   kH, 0, 0, 0, 0, 0xF0, 8, 6, 0},
            {kKindMenu,   kH, 0, 0, 0, P_SLOTS, 0xFF, 0, 6, 0},
            {kKindMenu,   kH, 0, 0, 0, P_HMN_CHANCES, 0xFF, 0, 6, 0},
            {kKindMenu,   kH, 0, 0, 0, P_CPU_CHANCES, 0xFF, 0, 6, 0},
            {kKindFunc,   kH, 0, 0, 0, 0, 0xFF, 9, 7, 0}
        };
        fillPageRows(kRecording, buf, 19, kSpecs, kPs_rec_names, kStrs, kExtra, kCbs, kGates);
    }

    void detachRecordingPage()
    {
        s_recRows = NULL;
    }
#else
    // Non-const so row 0 switches between Save Positions and Restore Positions.
    static Option kRows[] = {
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Save Positions", NULL, record::saveOrRestore, saveRow),
        TM_REC_CHOICE("P1 Mode", recording.hmnMode, kHmnMode, 4, 0, record::hmnModeChanged, record::modeRowsAvailable),
        TM_REC_CHOICE("P1 Record Slot", recording.hmnSlot, kSlot, 7, 1, record::slotChanged, record::modeRowsAvailable),
        TM_REC_CHOICE("CPU Mode", recording.cpuMode, kCpuMode, 5, 0, record::cpuModeChanged, record::cpuRowsAvailable),
        TM_REC_CHOICE("CPU Record Slot", recording.cpuSlot, kSlot, 7, 1, record::slotChanged, record::cpuRowsAvailable),
        TM_REC_CHOICE("Mirrored Playback", recording.mirrored, kMirrored, 3, 0, record::mirroredChanged,
                      record::mirrorRowAvailable),
        TM_REC_CHOICE("CPU Counter", recording.cpuCounter, kCpuCounter, 5, 1, NULL, record::cpuRowsAvailable),
        TM_REC_TOGGLE("Loop Playback", recording.loop, 0, record::loopRowsAvailable),
        TM_REC_CHOICE("Auto Restore", recording.autoRestore, kAutoRestore, 3, 0, NULL, record::loopRowsAvailable),
        TM_REC_TOGGLE("Start Paused", recording.startPaused, 0, record::positionsSaved),
        TM_REC_CHOICE("Playback Takeover", recording.takeover, kTakeover, 3, 0, NULL, record::positionsSaved),
        TM_REC_TOGGLE("Start On Input", recording.startOnFirstInput, 0, record::positionsSaved),
        TM_REC_FUNC("Re-Save Positions", record::resavePositions, record::positionsSaved),
        TM_REC_FUNC("Prune Positions", record::prunePositions, record::positionsSaved),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Delete Positions",
               record::g_deletePosLabel, record::deletePositionsPressed, record::positionsSaved),
        TM_REC_MENU("Slot Management", P_SLOTS, record::positionsSaved),
        TM_REC_MENU("P1 Slot Chances", P_HMN_CHANCES, record::positionsSaved),
        TM_REC_MENU("CPU Slot Chances", P_CPU_CHANCES, record::positionsSaved),
        TM_REC_FUNC("Export Recording", storage::exportRecording, storage::exportAvailable)
    };

    static bool saveRow()
    {
        kRows[0].name = record::positionsSaved() ? "Restore Positions" : "Save Positions";
        return true;
    }

    extern const Page kRecording = TM_PAGE("Recording", kRows);
#endif
}
}
}

#undef TM_REC_CHOICE
#undef TM_REC_TOGGLE
#undef TM_REC_FUNC
#undef TM_REC_MENU
