#include "main/record/record.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    static const char* const kPlayer[] = {"P1", "CPU"};
    static const char* const kSlot[] = {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6"};
#endif

    // Alter Inputs / Delete / Copy require a slot that holds recorded inputs.
    static bool hasInputs()
    {
        const main::RecordingSettings& r = main::settings().recording;
        return record::slotHasInputs(r.manageSide, r.manageSlot);
    }

    static bool canCopySlot()
    {
        const main::RecordingSettings& r = main::settings().recording;
        return hasInputs() && r.manageCopyTo != r.manageSlot;
    }

#ifdef __MWERKS__
    extern Page kSlotManagement = {"Slot Management", NULL, 0, {0, 0, 0}};

    void initSlotManagementPage(Option* buf)
    {
        static const u16 kStrs[2] = {kPs_sm_kPlayer, kPs_sm_kSlot};
        static const void* const kExtra[1] = {record::g_deleteSlotLabel};
        static void (*const kCbs[4])() = {
            NULL, record::clearDeleteConfirm, record::deleteManagedSlotPressed, record::copyManagedSlot
        };
        static bool (*const kGates[4])() = {NULL, hasInputs, canCopySlot, record::positionsSaved};
        static const RowSpec kSpecs[8] = {
            {kKindChoice, 0, TM_FIELD(recording.manageSide), TM_CHOICE_LO, 0, 1, 0, 1, 0, 0},
            {kKindChoice, 0, TM_FIELD(recording.manageSlot), TM_CHOICE_LO, 0, 5, 1, 1, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_ALTER_INPUTS, 0xFF, 0, 1, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xF0, 2, 1, 0},
            {kKindChoice, 0, TM_FIELD(recording.manageCopyTo), TM_CHOICE_LO, 0, 5, 1, 0, 0, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xFF, 3, 2, 0},
            {kKindMenu,   0, 0, 0, 0, P_HMN_CHANCES, 0xFF, 0, 3, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_CHANCES, 0xFF, 0, 3, 0}
        };
        fillPageRows(kSlotManagement, buf, 8, kSpecs, kPs_sm_names, kStrs, kExtra, kCbs, kGates);
    }
#else
    static const Option kRows[] = {
        TM_CHOICE("Player", recording.manageSide, kPlayer, 0, record::clearDeleteConfirm),
        TM_CHOICE("Slot", recording.manageSlot, kSlot, 0, record::clearDeleteConfirm),
        TM_MENU_IF("Alter Inputs", P_ALTER_INPUTS, hasInputs),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Delete Slot", record::g_deleteSlotLabel,
               record::deleteManagedSlotPressed, hasInputs),
        TM_CHOICE("Copy Slot To", recording.manageCopyTo, kSlot, 0, NULL),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Copy Slot", NULL, record::copyManagedSlot, canCopySlot),
        TM_MENU_IF("P1 Slot Chances", P_HMN_CHANCES, record::positionsSaved),
        TM_MENU_IF("CPU Slot Chances", P_CPU_CHANCES, record::positionsSaved)
    };

    extern const Page kSlotManagement = TM_PAGE("Slot Management", kRows);
#endif
}
}
}
