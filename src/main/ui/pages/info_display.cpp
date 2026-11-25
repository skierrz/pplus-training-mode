#include "main/ui/info_display.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // In InfoValue order. Entries prefixed with NA are reserved placeholders.
#define NA TM_UNAVAILABLE_LABEL
    static const char* const kValues[kInfoValueCount] = {
        "None",
        "Position",
        "State Name",
        "State Frame",
        "Velocity - Self",
        "Velocity - KB",
        "Velocity - Total",
        "Engine LStick",
        "System LStick",
        NA "Engine CStick",
        "System CStick",
        "Engine Trigger",
        "System Trigger",
        "Ledgegrab Timer",
        "Intangibility",
        "Hitlag",
        "Hitstun",
        "Shield Health",
        "Shield Stun",
        "Grip Strength",
        "ECB Lock",
        "ECB Bottom",
        "Jumps",
        "Walljumps",
        "Can Walljump",
        NA "Jab Counter",
        "Line Info",
        "Blastzone X",
        "Blastzone Y",
        "Damage",
        "Subaction",
        "Cancel Flags",
        "Actionable",
        "Airborne",
        "Last Hit",
        "Actionable In",
        "Wall Cling"
    };
#undef NA

    static const char* const kPresets[kInfoPresetCount] = {"None", "Basic", "Ledge", "Combat", "Frame Data"};
    static const char* const kSizes[main::kInfoSizeCount] = {"Small", "Medium", "Large"};

    static void hmnPreset()
    {
        applyInfoPreset(main::kSideHuman);
    }

    static void cpuPreset()
    {
        applyInfoPreset(main::kSideCpu);
    }

#define TM_INFO_ROW(side, n, name) TM_CHOICE(name, info[side].rows[n], kValues, kInfoNone, NULL)
#define TM_INFO_PAGE(side, onPreset)                                                                          \
    TM_CHOICE("Preset", info[side].preset, kPresets, kInfoPresetNone, onPreset),                              \
        TM_CHOICE("Size", info[side].size, kSizes, main::kInfoMedium, NULL),                                  \
        TM_INFO_ROW(side, 0, "Row 1"), TM_INFO_ROW(side, 1, "Row 2"), TM_INFO_ROW(side, 2, "Row 3"),          \
        TM_INFO_ROW(side, 3, "Row 4"), TM_INFO_ROW(side, 4, "Row 5"), TM_INFO_ROW(side, 5, "Row 6"),          \
        TM_INFO_ROW(side, 6, "Row 7"), TM_INFO_ROW(side, 7, "Row 8")

    static const Option kHmnRows[] = {TM_INFO_PAGE(0, hmnPreset)};
    static const Option kCpuRows[] = {TM_INFO_PAGE(1, cpuPreset)};
#undef TM_INFO_PAGE
#undef TM_INFO_ROW

    extern const Page kHmnInfo = TM_PAGE("P1 Info Display", kHmnRows);
    extern const Page kCpuInfo = TM_PAGE("CPU Info Display", kCpuRows);
}

    const char* infoValueLabel(int value)
    {
        if (value < 0 || value >= kInfoValueCount)
        {
            return "?";
        }
        const char* s = pages::kValues[value];
        return s[0] == TM_UNAVAILABLE_LABEL[0] ? s + 1 : s;
    }

    bool infoValueAvailable(int value)
    {
        return value >= 0 && value < kInfoValueCount && pages::kValues[value][0] != TM_UNAVAILABLE_LABEL[0];
    }
}
}
