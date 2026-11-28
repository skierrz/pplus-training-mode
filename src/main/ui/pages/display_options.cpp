#include "main/display/display.h"
#include "main/extra.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_BEGIN(kWho) TM_L("Off") TM_L("P1") TM_L("CPU") TM_L("Both") TM_LABELS_END
    TM_LABELS_BEGIN(kPsaTargets) TM_L("Off") TM_L("P1") TM_L("CPU") TM_LABELS_END
    TM_LABELS_BEGIN(kFrameMeterModes) TM_L("Off") TM_L("P1 Top") TM_L("CPU Top") TM_LABELS_END
    TM_LABELS_BEGIN(kHitboxColors) TM_L("Default") TM_L("Property") TM_L("Priority") TM_LABELS_END
    TM_LABELS_BEGIN(kDiVectors)
        TM_L("Live Stick") TM_L("No DI") TM_L("In") TM_L("Out") TM_L("Up-In")
        TM_L("Up-Out") TM_L("Down-In") TM_L("Down-Out") TM_L("Survival")
    TM_LABELS_END
    TM_LABELS_ASSERT(kWho, 4, tm_disp_who_match);
    TM_LABELS_ASSERT(kPsaTargets, 3, tm_disp_psa_match);
    TM_LABELS_ASSERT(kFrameMeterModes, 3, tm_disp_fm_match);
    TM_LABELS_ASSERT(kHitboxColors, 3, tm_disp_hbcol_match);
    TM_LABELS_ASSERT(kDiVectors, 9, tm_disp_divec_match);
#endif

    static bool dispActive(bool on, u8 val)
    {
        return on && val != 0;
    }

    static bool hitboxColorsEnabled()
    {
        return dispActive(main::extraLoaded(), main::settings().display.hitboxes);
    }

    static bool heatmapChildEnabled()
    {
        return dispActive(main::extraLoaded(), main::settings().display.heatmap);
    }

    static bool diVectorEnabled()
    {
        return dispActive(main::extraDiRow(), main::settings().general.diDisplay);
    }

#ifdef __MWERKS__
    extern Page kDisplayOptions = {"Display", NULL, 0, {0, 0, 0}};
    extern Page kCollision = {"Boxes & Heatmap", NULL, 0, {0, 0, 0}};
    extern Page kHudOverlays = {"Overlays & Meters", NULL, 0, {0, 0, 0}};
    extern Page kDebug = {"Lab Debug", NULL, 0, {0, 0, 0}};

    void initDisplayPages(Option* buf)
    {
        static const u16 kStrs[7] = {
            kPs_disp_kFrameMeterModes, kPs_disp_kWho, kPs_disp_kHitboxColors,
            kPs_disp_kDiVectors, kPs_fmt_pct, kPs_fmt_df, kPs_disp_kPsaTargets
        };
        static bool (*const kDispGates[6])() = {
            NULL, main::extraLoaded, hitboxColorsEnabled, main::extraDiRow, diVectorEnabled, heatmapChildEnabled
        };
        const u8 kH = kFlagHideWhenDisabled;
        static const RowSpec kSpecs[29] = {
            // P_DISPLAY (0..9)
            {kKindChoice, 0, TM_FIELD(general.actionLog), TM_CHOICE_LO, 0, 2, 0, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(display.inputViewer), TM_CHOICE_LO, 0, 3, 1, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(display.hitboxes), TM_CHOICE_LO, 0, 3, 1, 0, 0, 0},
            {kKindChoice, kH, TM_FIELD(display.hitboxColorMode), TM_CHOICE_LO, 0, 2, 2, 0, 2, 0},
            {kKindToggle, 0, TM_FIELD(general.diDisplay), 0, 0, 1, 0xFF, 0, 3, 0},
            {kKindChoice, kH, TM_FIELD(display.diVectorMode), TM_CHOICE_LO, 0, 8, 3, 0, 4, 0},
            {kKindMenu,   0, 0, 0, 0, P_COLLISION, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_HUD_OVERLAYS, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_THEME, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_DEBUG, 0xFF, 0, 0, 0},
            // P_COLLISION (10..15)
            {kKindChoice, 0, TM_FIELD(display.heatmap), TM_CHOICE_LO, 0, 3, 1, 0, 1, 0},
            {kKindToggle, kH, TM_FIELD(display.heatmapBounds), 0, 0, 1, 0xFF, 0, 5, 0},
            {kKindInt,    kH, TM_FIELD(display.heatmapOpacity), 20, 60, 100, 4, 0, 5, 0},
            {kKindInt,    kH, TM_FIELD(display.heatmapColorFrames), 1, 5, 30, 5, 0, 5, 0},
            {kKindChoice, 0, TM_FIELD(display.grabVisualizer), TM_CHOICE_LO, 0, 3, 1, 0, 1, 0},
            {kKindToggle, 0, TM_FIELD(display.envCollision), 0, 0, 1, 0xFF, 0, 0, 0},
            // P_HUD_OVERLAYS (16..20)
            {kKindMenu,   0, 0, 0, 0, P_HMN_OVERLAYS, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_OVERLAYS, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_COMBO_METERS, 0xFF, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_LEDGEDASH_HUD, 0xFF, 0, 0, 0},
            {kKindToggle, 0, TM_FIELD(general.offlineVersus), 0, 0, 1, 0xFF, 0, 0, 0},
            // P_DEBUG (21..28)
            {kKindMenu,   0, 0, 0, 0, P_HMN_INFO, 0xFF, 0, 0, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_INFO, 0xFF, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(display.psaInspector), TM_CHOICE_LO, 0, 2, 6, 0, 1, 0},
            {kKindToggle, 0, TM_FIELD(display.debugOverlay), 0, 0, 1, 0xFF, 0, 0, 0},
            {kKindToggle, 0, TM_FIELD(display.hud), 0, 1, 1, 0xFF, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(display.models), TM_CHOICE_LO, 3, 3, 1, 0, 0, 0},
            {kKindToggle, 0, TM_FIELD(display.stageModel), 0, 1, 1, 0xFF, 0, 0, 0},
            {kKindToggle, 0, TM_FIELD(display.effects), 0, 1, 1, 0xFF, 0, 0, 0}
        };
        fillPageRows(kDisplayOptions, buf, 29, kSpecs, kPs_disp_names, kStrs, NULL, NULL, kDispGates);
        kDisplayOptions.count = 10;
        kCollision.options = buf + 10;
        kCollision.count = 6;
        kHudOverlays.options = buf + 16;
        kHudOverlays.count = 5;
        kDebug.options = buf + 21;
        kDebug.count = 8;
    }
#else
    static const Option kRows[] = {
        TM_ROW_CHOICE_P(0, TM_FIELD(general.actionLog), 3, 0, "Frame Meter", kFrameMeterModes, NULL, main::extraLoaded),
        TM_ROW_CHOICE_P(0, TM_FIELD(display.inputViewer), 4, 0, "Input Viewer", kWho, NULL, main::extraLoaded),
        TM_CHOICE_P("Hitboxes", display.hitboxes, kWho, 4, 0, NULL),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.hitboxColorMode), 3, 0,
                        "Hitbox Colors", kHitboxColors, NULL, hitboxColorsEnabled),
        TM_ROW(::tm::ui::kKindToggle, TM_HALF(general.diDisplay), TM_FIELD(general.diDisplay), 0, 1, 0, "DI Display",
               NULL, NULL, main::extraDiRow),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.diVectorMode), 9, 0,
                        "DI Vector", kDiVectors, NULL, diVectorEnabled),
        TM_MENU("Boxes & Heatmap", P_COLLISION),
        TM_MENU("Overlays & Meters", P_HUD_OVERLAYS),
        TM_MENU("Theme", P_THEME),
        TM_MENU("Lab Debug", P_DEBUG)
    };

    static const Option kCollisionRows[] = {
        TM_ROW_CHOICE_P(0, TM_FIELD(display.heatmap), 4, 0, "Hitbox Heatmap", kWho, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapBounds), 0, 1, 0,
               "Heatmap Bounds", NULL, NULL, heatmapChildEnabled),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapOpacity), 20, 100, 60,
               "Heatmap Opacity", "%d%%", NULL, heatmapChildEnabled),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapColorFrames), 1, 30, 5,
               "Heatmap Color Step", "%df", NULL, heatmapChildEnabled),
        TM_ROW_CHOICE_P(0, TM_FIELD(display.grabVisualizer), 4, 0, "Grab / Tether Boxes", kWho, NULL,
                        main::extraLoaded),
        TM_TOGGLE("Stage Collision", display.envCollision, 0, NULL)
    };

    static const Option kHudOverlaysRows[] = {
        TM_MENU("P1 Overlays", P_HMN_OVERLAYS),
        TM_MENU("CPU Overlays", P_CPU_OVERLAYS),
        TM_MENU_IF("Floating Meters", P_COMBO_METERS, main::extraLoaded),
        TM_MENU("Ledgedash HUD", P_LEDGEDASH_HUD),
        TM_TOGGLE("Offline VS Displays", general.offlineVersus, 0, NULL)
    };

    static const Option kDebugRows[] = {
        TM_MENU("P1 Info Display", P_HMN_INFO),
        TM_MENU("CPU Info Display", P_CPU_INFO),
        TM_ROW_CHOICE_P(0, TM_FIELD(display.psaInspector), 3, 0, "PSA Debugger", kPsaTargets, NULL, main::extraLoaded),
        TM_TOGGLE("Debug Overlay", display.debugOverlay, 0, NULL),
        TM_TOGGLE("Match HUD", display.hud, 1, NULL),
        TM_CHOICE_P("Fighter Models", display.models, kWho, 4, 3, NULL),
        TM_TOGGLE("Stage Model", display.stageModel, 1, NULL),
        TM_TOGGLE("Effects", display.effects, 1, NULL)
    };

    extern const Page kDisplayOptions = TM_PAGE("Display", kRows);
    extern const Page kCollision = TM_PAGE("Boxes & Heatmap", kCollisionRows);
    extern const Page kHudOverlays = TM_PAGE("Overlays & Meters", kHudOverlaysRows);
    extern const Page kDebug = TM_PAGE("Lab Debug", kDebugRows);
#endif
}
}
}
