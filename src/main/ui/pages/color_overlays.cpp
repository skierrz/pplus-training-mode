#include "main/game/overlays.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

#include <string.h>

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_BEGIN(kPreset) TM_L("Custom") TM_L("Standard (PPunch)") TM_LABELS_END
    // In game::OverlayChoice order.
    TM_LABELS_BEGIN(kColor)
        TM_L("None") TM_L("Red") TM_L("Green") TM_L("Blue")
        TM_L("Yellow") TM_L("White") TM_L("Black") TM_L("Cyan")
        TM_L("Orange") TM_L("Remove") TM_L("Show Collision") TM_L("Invisible")
        TM_L("Play Sound")
    TM_LABELS_END
    TM_LABELS_ASSERT(kPreset, 2, tm_overlay_preset_match);
    TM_LABELS_ASSERT(kColor, game::kOverlayChoiceCount, tm_overlay_choices_match);
#endif

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

#ifdef __MWERKS__
    extern Page kHmnOverlays = {"P1 Overlays", NULL, 0, {0, 0, 0}};
    extern Page kCpuOverlays = {"CPU Overlays", NULL, 0, {0, 0, 0}};

    void initOverlayPages(Option* buf)
    {
        static const u8 kGroups[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16};
        const char* nm = pageStr(kPs_ov_names);
        const char* preset = pageStr(kPs_ov_kPreset);
        const char* col = pageStr(kPs_ov_kColor);
        const u16 base0 = TM_FIELD(overlay[0]);
        const u16 delta = static_cast<u16>(TM_FIELD(overlay[1]) - base0);
        for (int i = 0; i < 17; i++)
        {
            Option& h = buf[i];
            h.kind = kKindChoice;
            h.flags = 0;
            h.offset = i == 0 ? TM_FIELD(overlay[0].preset) : static_cast<u16>(TM_FIELD(overlay[0].groups[0]) + kGroups[i - 1]);
            h.min = TM_CHOICE_LO;
            h.def = 0;
            h.max = i == 0 ? 1 : (game::kOverlayChoiceCount - 1);
            h.name = nm;
            h.data = i == 0 ? preset : col;
            h.onChange = i == 0 ? hmnPreset : hmnGroup;
            h.enabled = NULL;
            Option& c = buf[17 + i];
            c = h;
            c.offset = static_cast<u16>(h.offset + delta);
            c.onChange = i == 0 ? cpuPreset : cpuGroup;
            while (*nm++ != '\0') {}
        }
        kHmnOverlays.options = buf;
        kHmnOverlays.count = 17;
        kCpuOverlays.options = buf + 17;
        kCpuOverlays.count = 17;
    }
#else
#define TM_GROUP(side, g, name, onGroup) TM_CHOICE_P(name, overlay[side].groups[g], kColor, game::kOverlayChoiceCount, 0, onGroup)
#define TM_OVERLAY_PAGE(side, onPreset, onGroup)                                                                     \
    TM_CHOICE_P("Preset", overlay[side].preset, kPreset, 2, 0, onPreset),                                            \
        TM_GROUP(side, 0, "Actionable", onGroup), TM_GROUP(side, 1, "Hitstun", onGroup),                             \
        TM_GROUP(side, 2, "Invincible", onGroup), TM_GROUP(side, 3, "Ledge Actionable", onGroup),                    \
        TM_GROUP(side, 4, "Missed L-Cancel", onGroup), TM_GROUP(side, 5, "Can Fastfall", onGroup),                   \
        TM_GROUP(side, 6, "Autocancel", onGroup), TM_GROUP(side, 7, "Crouch", onGroup),                              \
        TM_GROUP(side, 8, "Wait", onGroup), TM_GROUP(side, 9, "Walk", onGroup), TM_GROUP(side, 10, "Dash", onGroup), \
        TM_GROUP(side, 11, "Run", onGroup), TM_GROUP(side, 13, "Full Hop", onGroup),                                 \
        TM_GROUP(side, 14, "Short Hop", onGroup), TM_GROUP(side, 15, "IASA", onGroup),                               \
        TM_GROUP(side, 16, "Shieldstun", onGroup)

    static const Option kHmnRows[] = {TM_OVERLAY_PAGE(0, hmnPreset, hmnGroup)};
    static const Option kCpuRows[] = {TM_OVERLAY_PAGE(1, cpuPreset, cpuGroup)};

    extern const Page kHmnOverlays = TM_PAGE("P1 Overlays", kHmnRows);
    extern const Page kCpuOverlays = TM_PAGE("CPU Overlays", kCpuRows);
#endif
#undef TM_OVERLAY_PAGE
#undef TM_GROUP
}
}
}
