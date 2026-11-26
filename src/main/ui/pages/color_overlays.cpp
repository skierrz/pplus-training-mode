#include "main/game/overlays.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

#include <string.h>

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_BEGIN(kPreset) TM_L("Custom") TM_L("ProjectPunch") TM_LABELS_END
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
#define TM_GROUP(side, g, name, onGroup) TM_CHOICE_P(name, overlay[side].groups[g], NULL, game::kOverlayChoiceCount, 0, onGroup)
#define TM_OVERLAY_PAGE(side, onPreset, onGroup)                                                                     \
    TM_CHOICE_P("Preset", overlay[side].preset, NULL, 2, 0, onPreset),                                               \
        TM_GROUP(side, 0, "Actionable", onGroup), TM_GROUP(side, 1, "Hitstun", onGroup),                             \
        TM_GROUP(side, 2, "Invincible", onGroup), TM_GROUP(side, 3, "Ledge Actionable", onGroup),                    \
        TM_GROUP(side, 4, "Missed L-Cancel", onGroup), TM_GROUP(side, 5, "Can Fastfall", onGroup),                   \
        TM_GROUP(side, 6, "Autocancel", onGroup), TM_GROUP(side, 7, "Crouch", onGroup),                              \
        TM_GROUP(side, 8, "Wait", onGroup), TM_GROUP(side, 9, "Walk", onGroup), TM_GROUP(side, 10, "Dash", onGroup), \
        TM_GROUP(side, 11, "Run", onGroup), TM_GROUP(side, 13, "Full Hop", onGroup),                                 \
        TM_GROUP(side, 14, "Short Hop", onGroup), TM_GROUP(side, 15, "IASA", onGroup),                               \
        TM_GROUP(side, 16, "Shield Stun", onGroup)

    static Option kHmnRows[] = {TM_OVERLAY_PAGE(0, hmnPreset, hmnGroup)};

    void initOverlayPages(Option* buf)
    {
        const u8 n = static_cast<u8>(TM_COUNT(kHmnRows));
        const char* col = pageStr(kPs_ov_kColor);
        kHmnRows[0].data = pageStr(kPs_ov_kPreset);
        for (int i = 1; i < n; i++)
        {
            kHmnRows[i].data = col;
        }
        memcpy(buf, kHmnRows, sizeof(kHmnRows));
        const u16 delta = static_cast<u16>(TM_FIELD(overlay[1]) - TM_FIELD(overlay[0]));
        for (int i = 0; i < n; i++)
        {
            buf[i].offset = static_cast<u16>(buf[i].offset + delta);
            buf[i].onChange = i == 0 ? cpuPreset : cpuGroup;
        }
        kCpuOverlays.options = buf;
        kCpuOverlays.count = n;
    }

    extern const Page kHmnOverlays = TM_PAGE("P1 Overlays", kHmnRows);
    extern Page kCpuOverlays = {"CPU Overlays", NULL, 0, {0, 0, 0}};
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
        TM_GROUP(side, 16, "Shield Stun", onGroup)

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
