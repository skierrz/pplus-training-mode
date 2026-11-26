#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    extern const char* const kTechInvisLabels[4] = {"Off", "CPU", "All", "Flash"};
    extern const char* const kPreTechDriftLabels[6] = {
        "Off", "Random", "In", "Out", "Toward Tech", "Away From Tech"
    };
    static const char* const kTech[] = {"Random", "In Place", "Away", "Toward", "Miss"};
    static const char* const kGetUp[] = {"Random", "Stand", "Roll Away", "Roll Toward", "Attack"};
    static const char* const kTechTrap[] = {"Off", "Early", "Late"};
    static const char* const kLockout[] = {"Early", "Late"};

    static bool techInvisActive()
    {
        return main::settings().techInvis.mode != 0;
    }

    static bool techNotMiss()
    {
        return cpuLabControl() && main::settings().cpu.options.tech != cpu::kTechNone;
    }

    static bool techRandom()
    {
        return cpuLabControl() && main::settings().cpu.options.tech == cpu::kTechRandom;
    }

    static bool getupRandom()
    {
        return cpuLabControl() && main::settings().cpu.options.getup == cpu::kGetupRandom;
    }

    // Rebalances four integer weights so their total remains 100.
    static void rebalance(u8* chance, int changed)
    {
        int left = 100 - chance[changed];
        int others = 0;
        for (int i = 0; i < 4; i++)
        {
            others += i == changed ? 0 : chance[i];
        }
        int given = 0;
        int last = -1;
        for (int i = 0; i < 4; i++)
        {
            if (i == changed)
            {
                continue;
            }
            int share = others > 0 ? chance[i] * left / others : left / 3;
            chance[i] = static_cast<u8>(share);
            given += share;
            last = i;
        }
        chance[last] = static_cast<u8>(chance[last] + left - given);
    }

#define TM_REBALANCE(fn, group, i)                       \
    static void fn()                                     \
    {                                                    \
        rebalance(main::settings().cpu.options.group, i); \
    }
    TM_REBALANCE(tech0, techChance, 0)
    TM_REBALANCE(tech1, techChance, 1)
    TM_REBALANCE(tech2, techChance, 2)
    TM_REBALANCE(tech3, techChance, 3)
    TM_REBALANCE(getup0, getupChance, 0)
    TM_REBALANCE(getup1, getupChance, 1)
    TM_REBALANCE(getup2, getupChance, 2)
    TM_REBALANCE(getup3, getupChance, 3)
#undef TM_REBALANCE

#define TM_CHANCE(name, f, onChange, enabled) \
    TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(f), 0, 100, 25, name, "%d%%", onChange, enabled)

    static const Option kRows[] = {
        TM_CPU_CHOICE("Tech", cpu.options.tech, kTech, 0),
        TM_CPU_CHOICE("Getup", cpu.options.getup, kGetUp, 0),
        TM_CHOICE("Tech Invisibility", techInvis.mode, kTechInvisLabels, 0, NULL),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(techInvis.delay), 0, 15, 0,
               "Invis Delay", "%df", NULL, techInvisActive),
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.techSound), 0, 1, 0, "Tech Sound Cue", NULL, NULL,
               techNotMiss),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.techTrap), 0, TM_COUNT(kTechTrap) - 1, 0, "Tech Trap",
               kTechTrap, NULL, techNotMiss),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.techLockout), 0, TM_COUNT(kLockout) - 1, 0,
               "Tech Timing", kLockout, NULL, techNotMiss),
        TM_CPU_CHOICE("Pre-Tech Drift", techInvis.preTechDrift, kPreTechDriftLabels, 0),
        TM_CHANCE("Tech In Place %", cpu.options.techChance[0], tech0, techRandom),
        TM_CHANCE("Tech Away %", cpu.options.techChance[1], tech1, techRandom),
        TM_CHANCE("Tech Toward %", cpu.options.techChance[2], tech2, techRandom),
        TM_CHANCE("Miss Tech %", cpu.options.techChance[3], tech3, techRandom),
        TM_CPU_INT("Down Wait %", cpu.options.missTechWait, 0, 100, 0, "%d%%"),
        TM_CHANCE("Stand %", cpu.options.getupChance[0], getup0, getupRandom),
        TM_CHANCE("Roll Away %", cpu.options.getupChance[1], getup1, getupRandom),
        TM_CHANCE("Roll Toward %", cpu.options.getupChance[2], getup2, getupRandom),
        TM_CHANCE("Getup Attack %", cpu.options.getupChance[3], getup3, getupRandom)
    };
#undef TM_CHANCE

    extern const Page kTechOptions = TM_PAGE("Tech & Getup", kRows);
}
}
}
