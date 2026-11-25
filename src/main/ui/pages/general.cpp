#include "main/extra.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static void humanPercent()
    {
        lab::applyPercent(main::kSideHuman);
    }

    static const char* const kInfiniteShields[] = {"Off", "Until Hit", "On"};
    static const char* const kCamera[] = {"Normal", "Zoom", "Fixed"};
    static const char* const kGameSpeed[] = {"1x", "5/6x", "2/3x", "1/2x", "1/4x"};

    static const Option kRows[] = {
        TM_TOGGLE("Frame Advance", general.frameAdvance, 0, lab::frameAdvanceChanged),
        TM_INT("P1 Damage", general.playerPercent, 0, 999, 0, "%d%%", humanPercent),
        TM_TOGGLE("Lock P1 Damage", general.lockPlayerPercent, 0, humanPercent),
        TM_INT("P1 Load Random %", recording.randomPercent[0], 0, savestate::kMaxRandomPercent, 0, "%d%%", NULL),
        TM_CHOICE("P1 Infinite Shield", general.playerInfiniteShields, kInfiniteShields, cpu::kShieldsUntilHit, NULL),
        TM_CHOICE("Game Speed", general.gameSpeed, kGameSpeed, 0, NULL),
        TM_CHOICE("Camera", general.cameraMode, kCamera, 0, NULL),
        TM_TOGGLE("Move Staling", general.moveStaling, 1, NULL),
        TM_TOGGLE("Offline VS Displays", general.offlineVersus, 0, NULL),
        TM_MENU("Controls", P_CONTROLS),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Reset Settings", lab::g_resetLabel,
               lab::resetAllPressed, NULL),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Brawl Training Menu", NULL,
               lab::openTrainingMenu, NULL),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Return to CSS", NULL, lab::exitMatch, NULL)
    };

    extern const Page kGeneral = TM_PAGE("Lab", kRows);
}
}
}
