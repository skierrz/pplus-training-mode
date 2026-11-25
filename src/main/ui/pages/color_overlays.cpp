#include "main/game/overlays.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static const char* const kPreset[] = {"Custom", "ProjectPunch"};
    // In game::OverlayChoice order.
    static const char* const kColor[] = {"None",      "Red",      "Green",          "Blue",
                                         "Yellow",    "White",    "Black",          "Cyan",
                                         "Orange",    "Remove",   "Show Collision", "Invisible",
                                         "Play Sound"};
    TM_STATIC_ASSERT(TM_COUNT(kColor) == game::kOverlayChoiceCount, tm_overlay_choices_match);

    // ProjectPunch's overlays as a preset (color_overlay.cpp, MIT; see
    // THIRD_PARTY_NOTICES.md): waiting green, dash cyan, run blue, IASA orange.
    static void applyPreset(int side)
    {
        main::OverlaySettings& o = main::settings().overlay[side];
        if (o.preset != 1)
        {
            return;
        }
        for (int g = 0; g < main::kOverlayGroups; g++)
        {
            o.groups[g] = game::kOverlayNone;
        }
        o.groups[game::kGroupWait] = game::kOverlayGreen;
        o.groups[game::kGroupDash] = game::kOverlayCyan;
        o.groups[game::kGroupRun] = game::kOverlayBlue;
        o.groups[game::kGroupIasa] = game::kOverlayOrange;
    }

    static void hmnPreset()
    {
        applyPreset(main::kSideHuman);
    }

    static void cpuPreset()
    {
        applyPreset(main::kSideCpu);
    }

    // Changing a group by hand switches the preset back to Custom.
    static void hmnGroup()
    {
        main::settings().overlay[main::kSideHuman].preset = 0;
    }

    static void cpuGroup()
    {
        main::settings().overlay[main::kSideCpu].preset = 0;
    }

#define TM_GROUP(side, g, name, onGroup) TM_CHOICE(name, overlay[side].groups[g], kColor, 0, onGroup)
#define TM_OVERLAY_PAGE(side, onPreset, onGroup)                                                                     \
    TM_CHOICE("Preset", overlay[side].preset, kPreset, 0, onPreset),                                                 \
        TM_GROUP(side, 0, "Actionable", onGroup), TM_GROUP(side, 1, "Hitstun", onGroup),                             \
        TM_GROUP(side, 2, "Invincible", onGroup), TM_GROUP(side, 3, "Ledge Actionable", onGroup),                    \
        TM_GROUP(side, 4, "Missed L-Cancel", onGroup), TM_GROUP(side, 5, "Can Fastfall", onGroup),                   \
        TM_GROUP(side, 6, "Autocancel", onGroup), TM_GROUP(side, 7, "Crouch", onGroup),                              \
        TM_GROUP(side, 8, "Wait", onGroup), TM_GROUP(side, 9, "Walk", onGroup), TM_GROUP(side, 10, "Dash", onGroup), \
        TM_GROUP(side, 11, "Run", onGroup), TM_GROUP(side, 13, "Full Hop", onGroup),                                 \
        TM_GROUP(side, 14, "Short Hop", onGroup), TM_GROUP(side, 15, "IASA", onGroup),                               \
        TM_GROUP(side, 16, "Shield Stun", onGroup)

    static const Option kHmnRows[] = {TM_OVERLAY_PAGE(0, hmnPreset, hmnGroup)};
    static const Option kCpuRows[] = {TM_OVERLAY_PAGE(1, cpuPreset, cpuGroup)};
#undef TM_OVERLAY_PAGE
#undef TM_GROUP

    extern const Page kHmnOverlays = TM_PAGE("P1 Overlays", kHmnRows);
    extern const Page kCpuOverlays = TM_PAGE("CPU Overlays", kCpuRows);
}
}
}
