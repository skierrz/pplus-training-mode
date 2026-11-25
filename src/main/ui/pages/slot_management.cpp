#include "main/record/record.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kPlayer[] = {"P1", "CPU"};
    static const char* const kSlot[] = {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6"};

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

    static const Option kRows[] = {
        TM_CHOICE("Player", recording.manageSide, kPlayer, 0, record::clearDeleteConfirm),
        TM_CHOICE("Slot", recording.manageSlot, kSlot, 0, record::clearDeleteConfirm),
        TM_MENU_IF("Alter Inputs", P_ALTER_INPUTS, hasInputs),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Delete Slot", record::g_deleteSlotLabel,
               record::deleteManagedSlotPressed, hasInputs),
        TM_CHOICE("Copy Slot To", recording.manageCopyTo, kSlot, 0, NULL),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Copy Slot", NULL, record::copyManagedSlot, canCopySlot)
    };

    extern const Page kSlotManagement = TM_PAGE("Slot Management", kRows);
}
}
}
