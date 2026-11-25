#include "main/game/code_menu.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kAdvanceButton[main::kAdvanceButtonCount] = {"L", "Z", "X", "Y", "R"};
    static const char* const kDecrementButton[main::kDecrementButtonCount] = {"None", "Z", "L", "R", "X", "Y"};
    static const char* const kDpadUp[] = {"Taunt", "None"};
    static const char* const kDpadDown[] = {"None", "Frame Advance"};
    static const char* const kDpadLeft[] = {"Load State", "None"};
    static const char* const kDpadRight[] = {"Save State", "None"};
    static const char* const kMenuButton[] = {"Z in Pause", "Start"};
    static const char* const kMenuStyle[] = {"Category Rail", "Top Tabs"};
    TM_STATIC_ASSERT(TM_COUNT(kMenuStyle) == main::kMenuStyleCount, tm_menu_style_labels_match);

    static const Option kRows[] = {
        TM_CHOICE("Frame Advance", controls.advanceButton, kAdvanceButton, main::kAdvanceL, NULL),
        TM_INT("Advance Hold", controls.advanceDelay, 5, 60, 30, "%df", NULL),
        TM_CHOICE("Frame Rewind", controls.decrementButton, kDecrementButton, main::kDecrementNone, NULL),
        TM_CHOICE("D-Pad Up", controls.dpadUp, kDpadUp, main::kUpTaunt, NULL),
        TM_CHOICE("D-Pad Down", controls.dpadDown, kDpadDown, main::kDownNone, NULL),
        TM_CHOICE("D-Pad Left", controls.dpadLeft, kDpadLeft, savestate::kLeftLoadState, NULL),
        TM_CHOICE("D-Pad Right", controls.dpadRight, kDpadRight, savestate::kRightSaveState, NULL),
        TM_CHOICE("Menu Open", controls.startOpensMenu, kMenuButton, main::kDefaultStartOpensMenu, NULL),
        TM_TOGGLE("P+ Code Menu Shortcut", controls.allowCodeMenu, 0, game::codeMenuSync),
        TM_CHOICE("Menu Style", controls.menuStyle, kMenuStyle, main::kMenuStyleRail, NULL)
    };

    extern const Page kControls = TM_PAGE("Controls", kRows);
}
}
}
