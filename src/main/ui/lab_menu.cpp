#include "main/ui/lab_menu.h"

#include "main/lab.h"
#include "main/record/record.h"
#include "main/settings.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
    using namespace pages;

    static u8* values()
    {
        return reinterpret_cast<u8*>(&main::settings());
    }

    static const u8 kCategoryPages[kCategoryCount] = {
        P_GENERAL, P_SCENARIOS, P_CPU, P_RECORDING, P_DISPLAY, P_OSD_MENU, P_STAGE_CAST
    };

    static const char* const kTabLabels[kCategoryCount] = {
        "Lab", "Scenarios", "CPU", "Recording", "Display", "OSDs", "Stage/Cast"
    };

    static const MenuAction kRailUtilities[] = {
        {"Reset Settings", lab::g_resetLabel, lab::resetAllPressed},
        {"Brawl Training", NULL, lab::openTrainingMenu},
        {"Return to CSS", NULL, lab::exitMatch}
    };

    static const MenuDef kLabMenu = {
        kPages, P_COUNT, values,
        kCategoryCount, kCategoryPages, kTabLabels,
        static_cast<u8>(sizeof(kRailUtilities) / sizeof(kRailUtilities[0])), kRailUtilities
    };

    const MenuDef& labMenu()
    {
        return kLabMenu;
    }

    void clampSettings()
    {
        clampAll(kLabMenu);
    }

    void applyMenuDefaults()
    {
        defaultAll(kLabMenu);
    }

    bool applyMenuDefaultOption(int pageId, int row)
    {
        if (static_cast<u32>(pageId) < P_COUNT)
        {
            const Page& p = *kPages[pageId];
            if (static_cast<u32>(row) < p.count)
            {
                return defaultOption(kLabMenu, p.options[row]);
            }
        }
        return false;
    }

    bool applyMenuDefaultPage(int pageId)
    {
        return defaultPage(kLabMenu, pageId);
    }

    void applyMenuDefaultRange(u16 offset, u16 size)
    {
        defaultRange(kLabMenu, offset, size);
    }

    void retireDroppedValues()
    {
        retireDropped(kLabMenu);
        // The ten Advanced Counter pages keep their actions outside the
        // rows (the rows show one page's copy): the same rule, with the
        // rows' defaults (cpu/lab_data.cpp defaultHit).
        static const int kSizes[3] = {kCounterGroundSize, kCounterAirSize, kCounterShieldSize};
        static const u8 kDefaults[3] = {13, 1, 1};
        cpu::CpuTables& t = main::settings().cpuTables;
        for (int h = 0; h < cpu::kAdvancedHits; h++)
        {
            for (int k = 0; k < 3; k++)
            {
                u8& a = t.hits[h].action[k];
                if (a < kSizes[k])
                {
#ifdef __MWERKS__
                    const char* tbl = (k == 1) ? kCounterAir : (k == 0 ? kCounterGround : kCounterShield);
                    const char* lbl = tbl != NULL ? pages::packedLabelAt(tbl, a) : NULL;
#else
                    const char* lbl = (k == 1) ? pages::packedLabelAt(kCounterAir, a)
                                               : (k == 0 ? pages::packedLabelAt(kCounterGround, a)
                                                         : pages::packedLabelAt(kCounterShield, a));
#endif
                    if (droppedLabel(lbl))
                    {
                        a = kDefaults[k];
                    }
                }
            }
        }
    }
}
}
