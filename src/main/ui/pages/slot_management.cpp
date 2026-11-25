#include "main/record/record.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kPlayer[] = {"HMN", "CPU"};
    static const char* const kSlot[] = {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6"};

    // Alter Inputs edits a slot that has inputs.
    static bool hasInputs()
    {
        const main::RecordingSettings& r = main::settings().recording;
        return record::slotHasInputs(r.manageSide, r.manageSlot);
    }

    static const Option kRows[] = {
        TM_CHOICE("Player", recording.manageSide, kPlayer, 0, NULL),
        TM_CHOICE("Slot", recording.manageSlot, kSlot, 0, NULL),
        TM_MENU_IF("Modify Inputs", P_ALTER_INPUTS, hasInputs),
        TM_FUNC("Delete Slot", record::deleteManagedSlot),
        TM_CHOICE("Copy Slot To", recording.manageCopyTo, kSlot, 0, NULL),
        TM_FUNC("Copy Slot", record::copyManagedSlot)
    };

    extern const Page kSlotManagement = TM_PAGE("Slot Management", kRows);
}
}
}
