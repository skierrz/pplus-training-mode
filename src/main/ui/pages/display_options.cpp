#include "main/display/display.h"
#include "main/extra.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kWho[] = {"Off", "P1", "CPU", "Both"};

    static const Option kRows[] = {
        TM_TOGGLE("Match HUD", display.hud, 1, NULL),
        TM_CHOICE("Hitboxes", display.hitboxes, kWho, 0, NULL),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.heatmap), 0, TM_COUNT(kWho) - 1, 0,
               "Hitbox Heatmap", kWho, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(display.heatmapBounds), 0, 1, 0,
               "Heatmap Bounds", NULL, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(display.heatmapOpacity), 20, 100, 60,
               "Heatmap Opacity", "%d%%", NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(display.heatmapColorFrames), 1, 30, 5,
               "Heatmap Color Step", "%df", NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(display.grabVisualizer), 0, TM_COUNT(kWho) - 1, 0,
               "Grab / Tether Boxes", kWho, NULL, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, TM_HALF(general.diDisplay), TM_FIELD(general.diDisplay), 0, 1, 0, "DI Display",
               NULL, NULL, main::extraDiRow),
        TM_CHOICE("Fighter Models", display.models, kWho, 3, NULL),
        TM_TOGGLE("Stage Collision", display.envCollision, 0, NULL),
        TM_TOGGLE("Stage Model", display.stageModel, 1, NULL),
        TM_TOGGLE("Effects", display.effects, 1, NULL),
        TM_MENU("P1 Overlays", P_HMN_OVERLAYS),
        TM_MENU("CPU Overlays", P_CPU_OVERLAYS),
        TM_MENU("P1 Info Display", P_HMN_INFO),
        TM_MENU("CPU Info Display", P_CPU_INFO),
        TM_MENU("Ledgedash HUD", P_LEDGEDASH_HUD),
        TM_MENU("Theme", P_THEME)
    };

    extern const Page kDisplayOptions = TM_PAGE("Display", kRows);

    static const Option kDebugRows[] = {
        TM_TOGGLE("Debug Overlay", display.debugOverlay, 0, NULL)
    };

    extern const Page kDebug = TM_PAGE("Debug", kDebugRows);
}
}
}
