#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static bool ledgeHudEnabled()
    {
        return main::settings().general.ledgeHud != 0;
    }

#ifdef __MWERKS__
    extern Page kLedgedashHud = {"Ledgedash HUD", NULL, 0, {0, 0, 0}};

    void initLedgedashHudPage(Option* buf)
    {
        static const u16 kStrs[1] = {kPs_fmt_df};
        static bool (*const kGates[2])() = {NULL, ledgeHudEnabled};
        const u8 kH = kFlagHideWhenDisabled;
        static const RowSpec kSpecs[4] = {
            {kKindToggle, 0,  TM_FIELD(general.ledgeHud), 0, 1, 1, 0xFF, 0, 0, 0},
            {kKindInt,    kH, TM_FIELD(general.ledgeMaxFrames), 30, 50, 64, 0, 0, 1, 0},
            {kKindToggle, kH, TM_FIELD(general.ledgeGalint), 0, 1, 1, 0xFF, 0, 1, 0},
            {kKindToggle, kH, TM_FIELD(general.ledgePanel), 0, 1, 1, 0xFF, 0, 1, 0}
        };
        fillPageRows(kLedgedashHud, buf, 4, kSpecs, kPs_ld_names, kStrs, NULL, NULL, kGates);
    }
#else
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
#endif
}
}
}
