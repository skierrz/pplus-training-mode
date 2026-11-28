#include "main/extra.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static void humanPercent()
    {
        lab::applyPercent(main::kSideHuman);
    }

#ifndef __MWERKS__
    TM_LABELS_BEGIN(kInfiniteShields) TM_L("Off") TM_L("Until Hit") TM_L("On") TM_LABELS_END
    TM_LABELS_BEGIN(kCamera) TM_L("Normal") TM_L("Zoom") TM_L("Fixed") TM_LABELS_END
    TM_LABELS_BEGIN(kGameSpeed) TM_L("1x") TM_L("5/6x") TM_L("2/3x") TM_L("1/2x") TM_L("1/4x") TM_LABELS_END
    TM_LABELS_ASSERT(kInfiniteShields, 3, tm_gen_shields_match);
    TM_LABELS_ASSERT(kCamera, 3, tm_gen_camera_match);
    TM_LABELS_ASSERT(kGameSpeed, 5, tm_gen_speed_match);
#endif

#ifdef __MWERKS__
    extern Page kGeneral = {"Lab", NULL, 0, {0, 0, 0}};

    void initGeneralPage(Option* buf)
    {
        static const u16 kStrs[4] = {
            kPs_fmt_pct, kPs_gen_kInfiniteShields, kPs_gen_kGameSpeed, kPs_gen_kCamera
        };
        static const void* const kExtra[1] = {lab::g_resetLabel};
        static void (*const kCbs[6])() = {
            NULL, lab::frameAdvanceChanged, humanPercent, lab::resetAllPressed,
            lab::openTrainingMenu, lab::exitMatch
        };
        static const RowSpec kSpecs[12] = {
            {kKindToggle, 0, TM_FIELD(general.frameAdvance), 0, 0, 1, 0xFF, 1, 0, 0},
            {kKindInt,    TM_HALF(general.playerPercent), TM_FIELD(general.playerPercent), 0, 0, 999, 0, 2, 0, 0},
            {kKindToggle, 0, TM_FIELD(general.lockPlayerPercent), 0, 0, 1, 0xFF, 2, 0, 0},
            {kKindInt,    0, TM_FIELD(recording.randomPercent[0]), 0, 0, savestate::kMaxRandomPercent, 0, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(general.playerInfiniteShields), TM_CHOICE_LO, cpu::kShieldsUntilHit, 2, 1, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(general.gameSpeed), TM_CHOICE_LO, 0, 4, 2, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(general.cameraMode), TM_CHOICE_LO, 0, 2, 3, 0, 0, 0},
            {kKindToggle, 0, TM_FIELD(general.moveStaling), 0, 1, 1, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_CONTROLS, 0xFF, 0, 0, 0},
            {kKindFunc,   kFlagTabsOnly, 0, 0, 0, 0, 0xF0, 3, 0, 0},
            {kKindFunc,   kFlagTabsOnly, 0, 0, 0, 0, 0xFF, 4, 0, 0},
            {kKindFunc,   kFlagTabsOnly, 0, 0, 0, 0, 0xFF, 5, 0, 0}
        };
        fillPageRows(kGeneral, buf, 12, kSpecs, kPs_gen_names, kStrs, kExtra, kCbs, NULL);
    }
#else
    static const Option kRows[] = {
        TM_TOGGLE("Frame Advance", general.frameAdvance, 0, lab::frameAdvanceChanged),
        TM_INT("P1 Damage", general.playerPercent, 0, 999, 0, "%d%%", humanPercent),
        TM_TOGGLE("Lock P1 Damage", general.lockPlayerPercent, 0, humanPercent),
        TM_INT("P1 Load Random %", recording.randomPercent[0], 0, savestate::kMaxRandomPercent, 0, "%d%%", NULL),
        TM_CHOICE_P("P1 Infinite Shield", general.playerInfiniteShields, kInfiniteShields, 3, cpu::kShieldsUntilHit, NULL),
        TM_CHOICE_P("Game Speed", general.gameSpeed, kGameSpeed, 5, 0, NULL),
        TM_CHOICE_P("Camera", general.cameraMode, kCamera, 3, 0, NULL),
        TM_TOGGLE("Move Staling", general.moveStaling, 1, NULL),
        TM_MENU("Controls", P_CONTROLS),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Reset Settings", lab::g_resetLabel,
               lab::resetAllPressed, NULL),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Brawl Training Menu", NULL,
               lab::openTrainingMenu, NULL),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagTabsOnly, 0, 0, 0, 0, "Return to CSS", NULL, lab::exitMatch, NULL)
    };

    extern const Page kGeneral = TM_PAGE("Lab", kRows);
#endif
}
}
}
