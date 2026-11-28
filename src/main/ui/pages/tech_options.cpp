#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_EXTERN_BEGIN(kTechInvisLabels, 4) TM_L("Off") TM_L("CPU") TM_L("All") TM_L("Flash") TM_LABELS_END
    TM_LABELS_EXTERN_BEGIN(kPreTechDriftLabels, 6)
        TM_L("Off") TM_L("Random") TM_L("In") TM_L("Out") TM_L("Toward Tech") TM_L("Away From Tech")
    TM_LABELS_END
    TM_LABELS_BEGIN(kTech) TM_L("Random") TM_L("In Place") TM_L("Away") TM_L("Toward") TM_L("Miss") TM_LABELS_END
    TM_LABELS_BEGIN(kGetUp) TM_L("Random") TM_L("Stand") TM_L("Roll Away") TM_L("Roll Toward") TM_L("Attack") TM_LABELS_END
    TM_LABELS_BEGIN(kTechTrap) TM_L("Off") TM_L("Early") TM_L("Late") TM_LABELS_END
    TM_LABELS_BEGIN(kLockout) TM_L("Early") TM_L("Late") TM_LABELS_END
    TM_LABELS_ASSERT(kTechInvisLabels, 4, tm_tech_invis_match);
    TM_LABELS_ASSERT(kPreTechDriftLabels, 6, tm_pre_drift_match);
    TM_LABELS_ASSERT(kTech, 5, tm_tech_match);
    TM_LABELS_ASSERT(kGetUp, 5, tm_getup_match);
    TM_LABELS_ASSERT(kTechTrap, 3, tm_tech_trap_match);
    TM_LABELS_ASSERT(kLockout, 2, tm_lockout_match);
#endif

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

    static void rebalanceRow()
    {
        int r = ui::activeRow();
        if (r >= 0 && r < 4)
        {
            main::rebalanceWeights(main::settings().cpu.options.techChance, 4, 0xFu, r);
        }
        else if (r >= 5 && r < 9)
        {
            main::rebalanceWeights(main::settings().cpu.options.getupChance, 4, 0xFu, r - 5);
        }
    }

#ifdef __MWERKS__
    extern Page kTechOptions = {"Tech & Getup", NULL, 0, {0, 0, 0}};
    extern Page kTechWeights = {"Mixup Weights", NULL, 0, {0, 0, 0}};

    void initTechOptionsPage(Option* buf)
    {
        static const u16 kStrs[8] = {
            kPs_tech_kTech, kPs_tech_kGetUp, kPs_tech_kTechInvisLabels, kPs_fmt_df,
            kPs_tech_kTechTrap, kPs_tech_kLockout, kPs_tech_kPreTechDriftLabels, kPs_fmt_pct
        };
        static bool (*const kGates[6])() = {
            NULL, cpuLabControl, techInvisActive, techNotMiss, techRandom, getupRandom
        };
        static void (*const kCbs[2])() = {NULL, rebalanceRow};
        static const RowSpec kTs[18] = {
            {kKindChoice, 0, TM_FIELD(cpu.options.tech), TM_CHOICE_LO, 0, 4, 0, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.getup), TM_CHOICE_LO, 0, 4, 1, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(techInvis.mode), TM_CHOICE_LO, 0, 3, 2, 0, 0, 0},
            {kKindInt,    kFlagHideWhenDisabled, TM_FIELD(techInvis.delay), 0, 0, 15, 3, 0, 2, 0},
            {kKindToggle, 0, TM_FIELD(cpu.options.techSound), 0, 0, 1, 0xFF, 0, 3, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.techTrap), TM_CHOICE_LO, 0, 2, 4, 0, 3, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.techLockout), TM_CHOICE_LO, 0, 1, 5, 0, 3, 0},
            {kKindChoice, 0, TM_FIELD(techInvis.preTechDrift), TM_CHOICE_LO, 0, 5, 6, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_TECH_WEIGHTS, 0xFF, 0, 1, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.techChance[0]), 0, 25, 100, 7, 1, 4, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.techChance[1]), 0, 25, 100, 7, 1, 4, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.techChance[2]), 0, 25, 100, 7, 1, 4, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.techChance[3]), 0, 25, 100, 7, 1, 4, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.missTechWait), 0, 0, 100, 7, 0, 1, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.getupChance[0]), 0, 25, 100, 7, 1, 5, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.getupChance[1]), 0, 25, 100, 7, 1, 5, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.getupChance[2]), 0, 25, 100, 7, 1, 5, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.getupChance[3]), 0, 25, 100, 7, 1, 5, 0}
        };
        fillPageRows(kTechOptions, buf, 18, kTs, kPs_tech_names, kStrs, NULL, kCbs, kGates);
        kTechOptions.count = 9;
        kTechWeights.options = buf + 9;
        kTechWeights.count = 9;
    }
#else
#define TM_CHANCE(name, f, onChange, enabled) \
    TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(f), 0, 100, 25, name, "%d%%", onChange, enabled)

    static const Option kRows[] = {
        TM_CPU_CHOICE_P("Tech", cpu.options.tech, kTech, 5, 0),
        TM_CPU_CHOICE_P("Getup", cpu.options.getup, kGetUp, 5, 0),
        TM_CHOICE_P("Tech Invisibility", techInvis.mode, kTechInvisLabels, 4, 0, NULL),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(techInvis.delay), 0, 15, 0,
               "Invis Delay", "%df", NULL, techInvisActive),
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.techSound), 0, 1, 0, "Tech Sound Cue", NULL, NULL,
               techNotMiss),
        TM_ROW_CHOICE_P(0, TM_FIELD(cpu.options.techTrap), 3, 0, "Tech Trap", kTechTrap, NULL, techNotMiss),
        TM_ROW_CHOICE_P(0, TM_FIELD(cpu.options.techLockout), 2, 0, "Tech Timing", kLockout, NULL, techNotMiss),
        TM_CPU_CHOICE_P("Pre-Tech Drift", techInvis.preTechDrift, kPreTechDriftLabels, 6, 0),
        TM_MENU_IF("Mixup Weights", P_TECH_WEIGHTS, cpuLabControl)
    };

    static const Option kWeightRows[] = {
        TM_CHANCE("Tech In Place %", cpu.options.techChance[0], rebalanceRow, techRandom),
        TM_CHANCE("Tech Away %", cpu.options.techChance[1], rebalanceRow, techRandom),
        TM_CHANCE("Tech Toward %", cpu.options.techChance[2], rebalanceRow, techRandom),
        TM_CHANCE("Miss Tech %", cpu.options.techChance[3], rebalanceRow, techRandom),
        TM_CPU_INT("Down Wait %", cpu.options.missTechWait, 0, 100, 0, "%d%%"),
        TM_CHANCE("Stand %", cpu.options.getupChance[0], rebalanceRow, getupRandom),
        TM_CHANCE("Roll Away %", cpu.options.getupChance[1], rebalanceRow, getupRandom),
        TM_CHANCE("Roll Toward %", cpu.options.getupChance[2], rebalanceRow, getupRandom),
        TM_CHANCE("Getup Attack %", cpu.options.getupChance[3], rebalanceRow, getupRandom)
    };
#undef TM_CHANCE

    extern const Page kTechOptions = TM_PAGE("Tech & Getup", kRows);
    extern const Page kTechWeights = TM_PAGE("Mixup Weights", kWeightRows);
#endif
}
}
}
