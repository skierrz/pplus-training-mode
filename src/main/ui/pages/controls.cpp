#include "main/game/code_menu.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    TM_LABELS_BEGIN(kAdvanceButton) TM_L("L") TM_L("Z") TM_L("X") TM_L("Y") TM_L("R") TM_LABELS_END
    TM_LABELS_BEGIN(kDecrementButton) TM_L("None") TM_L("Z") TM_L("L") TM_L("R") TM_L("X") TM_L("Y") TM_LABELS_END
    TM_LABELS_BEGIN(kDpadUp) TM_L("Taunt") TM_L("None") TM_LABELS_END
    TM_LABELS_BEGIN(kDpadDown) TM_L("None") TM_L("Frame Advance") TM_LABELS_END
    TM_LABELS_BEGIN(kDpadLeft) TM_L("Load State") TM_L("None") TM_LABELS_END
    TM_LABELS_BEGIN(kDpadRight) TM_L("Save State") TM_L("None") TM_LABELS_END
    TM_LABELS_BEGIN(kMenuButton) TM_L("Z in Pause") TM_L("Start") TM_LABELS_END
    TM_LABELS_BEGIN(kMenuStyle) TM_L("Category Rail") TM_L("Top Tabs") TM_LABELS_END
    TM_LABELS_ASSERT(kAdvanceButton, main::kAdvanceButtonCount, tm_adv_btn_labels_match);
    TM_LABELS_ASSERT(kDecrementButton, main::kDecrementButtonCount, tm_dec_btn_labels_match);
    TM_LABELS_ASSERT(kMenuStyle, main::kMenuStyleCount, tm_menu_style_labels_match);

    static const Option kRows[] = {
        TM_CHOICE_P("Advance Button", controls.advanceButton, kAdvanceButton, main::kAdvanceButtonCount, main::kAdvanceL, NULL),
        TM_INT("Hold Repeat Delay", controls.advanceDelay, 5, 60, 30, "%df", NULL),
        TM_CHOICE_P("Rewind Button", controls.decrementButton, kDecrementButton, main::kDecrementButtonCount, main::kDecrementNone, NULL),
        TM_CHOICE_P("D-Pad Up", controls.dpadUp, kDpadUp, 2, main::kUpTaunt, NULL),
        TM_CHOICE_P("D-Pad Down", controls.dpadDown, kDpadDown, 2, main::kDownNone, NULL),
        TM_CHOICE_P("D-Pad Left", controls.dpadLeft, kDpadLeft, 2, savestate::kLeftLoadState, NULL),
        TM_CHOICE_P("D-Pad Right", controls.dpadRight, kDpadRight, 2, savestate::kRightSaveState, NULL),
        TM_CHOICE_P("Menu Open", controls.startOpensMenu, kMenuButton, 2, main::kDefaultStartOpensMenu, NULL),
        TM_TOGGLE("P+ Code Menu Shortcut", controls.allowCodeMenu, 0, game::codeMenuSync),
        TM_CHOICE_P("Menu Style", controls.menuStyle, kMenuStyle, main::kMenuStyleCount, main::kMenuStyleRail, NULL)
    };

    extern const Page kControls = TM_PAGE("Controls", kRows);
}
}
}
