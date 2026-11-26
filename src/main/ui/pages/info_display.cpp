#include "main/extra.h"
#include "main/ui/info_display.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

#include <string.h>

namespace tm {
namespace ui {
namespace pages {
    // In InfoValue order. Entries prefixed with NA are reserved placeholders.
#define NA TM_UNAVAILABLE_LABEL
#ifndef __MWERKS__
    static const char* const kValues[kInfoValueCount] = {
        "None", "Position", "State Name", "State Frame", "Velocity - Self", "Velocity - KB", "Velocity - Total",
        "Engine LStick", "System LStick", NA "Engine CStick", "System CStick", "Engine Trigger", "System Trigger",
        "Ledgegrab Timer", "Intangibility", "Hitlag", "Hitstun", "Shield Health", "Shield Stun", "Grip Strength",
        "ECB Lock", "ECB Bottom", "Jumps", "Walljumps", "Can Walljump", NA "Jab Counter", "Line Info",
        "Blastzone X", "Blastzone Y", "Damage", "Subaction", "Cancel Flags", "Actionable", "Airborne", "Last Hit",
        "Actionable In", "Wall Cling"
    };
#endif
#undef NA

#ifndef __MWERKS__
    TM_LABELS_BEGIN(kPresets) TM_L("None") TM_L("Basic") TM_L("Ledge") TM_L("Combat") TM_L("Frame Data") TM_LABELS_END
    TM_LABELS_BEGIN(kSizes) TM_L("Small") TM_L("Medium") TM_L("Large") TM_LABELS_END
    TM_LABELS_ASSERT(kPresets, kInfoPresetCount, tm_info_presets_match);
    TM_LABELS_ASSERT(kSizes, main::kInfoSizeCount, tm_info_sizes_match);
#endif

    static void hmnPreset()
    {
        main::extraInfoApplyPreset(main::kSideHuman);
    }

    static void cpuPreset()
    {
        main::extraInfoApplyPreset(main::kSideCpu);
    }

    static void hmnRowEdited()
    {
        main::settings().info[main::kSideHuman].preset = kInfoPresetNone;
    }

    static void cpuRowEdited()
    {
        main::settings().info[main::kSideCpu].preset = kInfoPresetNone;
    }

    static bool infoHasRows(int side)
    {
        const u8* rows = main::settings().info[side].rows;
        for (int i = 0; i < main::kInfoRows; i++)
        {
            if (rows[i] != kInfoNone)
            {
                return true;
            }
        }
        return false;
    }

    static bool hmnHasRows()
    {
        return infoHasRows(main::kSideHuman);
    }

    static bool cpuHasRows()
    {
        return infoHasRows(main::kSideCpu);
    }

#ifdef __MWERKS__
#define TM_INFO_ROW(side, n, name, onEdit) TM_CHOICE_P(name, info[side].rows[n], NULL, kInfoValueCount, kInfoNone, onEdit)
#define TM_INFO_PAGE(side, onPreset, onEdit, hasRows)                                                         \
    TM_CHOICE_P("Preset", info[side].preset, NULL, kInfoPresetCount, kInfoPresetNone, onPreset),              \
        TM_ROW_CHOICE_P(0, TM_FIELD(info[side].size), main::kInfoSizeCount, main::kInfoMedium,                \
                        "Size", NULL, NULL, hasRows),                                                         \
        TM_INFO_ROW(side, 0, "Row 1", onEdit), TM_INFO_ROW(side, 1, "Row 2", onEdit),                         \
        TM_INFO_ROW(side, 2, "Row 3", onEdit), TM_INFO_ROW(side, 3, "Row 4", onEdit),                         \
        TM_INFO_ROW(side, 4, "Row 5", onEdit), TM_INFO_ROW(side, 5, "Row 6", onEdit),                         \
        TM_INFO_ROW(side, 6, "Row 7", onEdit), TM_INFO_ROW(side, 7, "Row 8", onEdit)

    static Option kHmnRows[] = {TM_INFO_PAGE(0, hmnPreset, hmnRowEdited, hmnHasRows)};

    void initInfoPages(Option* buf)
    {
        const u8 n = static_cast<u8>(TM_COUNT(kHmnRows));
        const char* vals = pageStr(kPs_info_kValues);
        kHmnRows[0].data = pageStr(kPs_info_kPresets);
        kHmnRows[1].data = pageStr(kPs_info_kSizes);
        for (int i = 2; i < n; i++)
        {
            kHmnRows[i].data = vals;
        }
        memcpy(buf, kHmnRows, sizeof(kHmnRows));
        const u16 delta = static_cast<u16>(TM_FIELD(info[1]) - TM_FIELD(info[0]));
        for (int i = 0; i < n; i++)
        {
            buf[i].offset = static_cast<u16>(buf[i].offset + delta);
            buf[i].onChange = i == 0 ? cpuPreset : (i >= 2 ? cpuRowEdited : NULL);
            if (i == 1)
            {
                buf[i].enabled = cpuHasRows;
            }
        }
        kCpuInfo.options = buf;
        kCpuInfo.count = n;
    }

    extern const Page kHmnInfo = TM_PAGE("P1 Info Display", kHmnRows);
    extern Page kCpuInfo = {"CPU Info Display", NULL, 0, {0, 0, 0}};
#else
#define TM_INFO_ROW(side, n, name, onEdit) TM_CHOICE_P(name, info[side].rows[n], kValues, kInfoValueCount, kInfoNone, onEdit)
#define TM_INFO_PAGE(side, onPreset, onEdit, hasRows)                                                         \
    TM_CHOICE_P("Preset", info[side].preset, kPresets, kInfoPresetCount, kInfoPresetNone, onPreset),          \
        TM_ROW_CHOICE_P(0, TM_FIELD(info[side].size), main::kInfoSizeCount, main::kInfoMedium,                \
                        "Size", kSizes, NULL, hasRows),                                                       \
        TM_INFO_ROW(side, 0, "Row 1", onEdit), TM_INFO_ROW(side, 1, "Row 2", onEdit),                         \
        TM_INFO_ROW(side, 2, "Row 3", onEdit), TM_INFO_ROW(side, 3, "Row 4", onEdit),                         \
        TM_INFO_ROW(side, 4, "Row 5", onEdit), TM_INFO_ROW(side, 5, "Row 6", onEdit),                         \
        TM_INFO_ROW(side, 6, "Row 7", onEdit), TM_INFO_ROW(side, 7, "Row 8", onEdit)

    static const Option kHmnRows[] = {TM_INFO_PAGE(0, hmnPreset, hmnRowEdited, hmnHasRows)};
    static const Option kCpuRows[] = {TM_INFO_PAGE(1, cpuPreset, cpuRowEdited, cpuHasRows)};

    extern const Page kHmnInfo = TM_PAGE("P1 Info Display", kHmnRows);
    extern const Page kCpuInfo = TM_PAGE("CPU Info Display", kCpuRows);
#endif
#undef TM_INFO_PAGE
#undef TM_INFO_ROW
}

    // Linked in extra (main/ui/info_display.cpp); keep exported from match.
#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    const char* infoValueLabel(int value)
    {
        if (value < 0 || value >= kInfoValueCount)
        {
            return "?";
        }
#ifdef __MWERKS__
        const char* s = pages::packedLabelAt(pages::pageStr(pages::kPs_info_kValues), value);
#else
        const char* s = pages::packedLabelAt(pages::kValues, value);
#endif
        return s[0] == TM_UNAVAILABLE_LABEL[0] ? s + 1 : s;
    }

    bool infoValueAvailable(int value)
    {
        return value >= 0 && value < kInfoValueCount
#ifdef __MWERKS__
               && pages::packedLabelAt(pages::pageStr(pages::kPs_info_kValues), value)[0] != TM_UNAVAILABLE_LABEL[0];
#else
               && pages::packedLabelAt(pages::kValues, value)[0] != TM_UNAVAILABLE_LABEL[0];
#endif
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif
}
}
