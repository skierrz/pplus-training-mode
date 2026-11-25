#include "main/display/display.h"
#include "main/extra.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kWho[] = {"Off", "P1", "CPU", "Both"};
    static const char* const kPsaTargets[] = {"Off", "P1", "CPU"};
    static const char* const kHitboxColors[] = {"Default", "Property", "Priority"};
    static const char* const kDiVectors[] = {
        "Live Stick", "No DI", "In", "Out", "Up-In", "Up-Out", "Down-In", "Down-Out", "Survival"
    };

    static bool hitboxColorsEnabled()
    {
        return main::extraLoaded() && main::settings().display.hitboxes != 0;
    }

    static bool heatmapChildEnabled()
    {
        return main::extraLoaded() && main::settings().display.heatmap != 0;
    }

    static bool diVectorEnabled()
    {
        return main::extraDiRow() && main::settings().general.diDisplay != 0;
    }

    static const Option kRows[] = {
        TM_TOGGLE("Match HUD", display.hud, 1, NULL),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.inputViewer), 0, TM_COUNT(kWho) - 1, 0,
               "Input Viewer", kWho, NULL, main::extraLoaded),
        TM_MENU("Theme", P_THEME),
        TM_CHOICE("Hitboxes", display.hitboxes, kWho, 0, NULL),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.hitboxColorMode), 0,
               TM_COUNT(kHitboxColors) - 1, 0, "Hitbox Colors", kHitboxColors, NULL, hitboxColorsEnabled),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.heatmap), 0, TM_COUNT(kWho) - 1, 0,
               "Hitbox Heatmap", kWho, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapBounds), 0, 1, 0,
               "Heatmap Bounds", NULL, NULL, heatmapChildEnabled),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapOpacity), 20, 100, 60,
               "Heatmap Opacity", "%d%%", NULL, heatmapChildEnabled),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.heatmapColorFrames), 1, 30, 5,
               "Heatmap Color Step", "%df", NULL, heatmapChildEnabled),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.grabVisualizer), 0, TM_COUNT(kWho) - 1, 0,
               "Grab / Tether Boxes", kWho, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, TM_HALF(general.diDisplay), TM_FIELD(general.diDisplay), 0, 1, 0, "DI Display",
               NULL, NULL, main::extraDiRow),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(display.diVectorMode), 0,
               TM_COUNT(kDiVectors) - 1, 0, "DI Vector", kDiVectors, NULL, diVectorEnabled),
        TM_TOGGLE("Stage Collision", display.envCollision, 0, NULL),
        TM_CHOICE("Fighter Models", display.models, kWho, 3, NULL),
        TM_TOGGLE("Stage Model", display.stageModel, 1, NULL),
        TM_TOGGLE("Effects", display.effects, 1, NULL),
        TM_MENU("P1 Overlays", P_HMN_OVERLAYS),
        TM_MENU("CPU Overlays", P_CPU_OVERLAYS),
        TM_MENU("P1 Info Display", P_HMN_INFO),
        TM_MENU("CPU Info Display", P_CPU_INFO),
        TM_MENU("Ledgedash HUD", P_LEDGEDASH_HUD),
        TM_MENU("Advanced / Debug", P_DEBUG)
    };

    extern const Page kDisplayOptions = TM_PAGE("Display", kRows);

    static const Option kDebugRows[] = {
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.psaInspector), 0, TM_COUNT(kPsaTargets) - 1, 0,
               "PSA Inspector", kPsaTargets, NULL, main::extraLoaded),
        TM_TOGGLE("Debug Overlay", display.debugOverlay, 0, NULL)
    };

    extern const Page kDebug = TM_PAGE("Advanced / Debug", kDebugRows);
}
}
}
