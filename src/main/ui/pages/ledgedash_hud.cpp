#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static bool ledgeHudEnabled()
    {
        return main::settings().general.ledgeHud != 0;
    }

    // ProjectPunch's ledgedash frame bar settings (range 30..64, default 50).
    static const Option kRows[] = {
        TM_TOGGLE("Enable", general.ledgeHud, 1, NULL),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(general.ledgeMaxFrames), 30, 64, 50,
               "Max Frames", "%df", NULL, ledgeHudEnabled),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(general.ledgeGalint), 0, 1, 1,
               "Show GALINT", NULL, NULL, ledgeHudEnabled),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(general.ledgePanel), 0, 1, 1,
               "Show Summary", NULL, NULL, ledgeHudEnabled)
    };

    extern const Page kLedgedashHud = TM_PAGE("Ledgedash HUD", kRows);
}
}
}
