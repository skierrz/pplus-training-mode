#include "main/cpu/options.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_BEGIN(kLogic) TM_L("Default") TM_L("Off") TM_L("Custom") TM_LABELS_END
    enum { kLogicCount = 3 };
    TM_LABELS_ASSERT(kLogic, kLogicCount, tm_adv_logic_labels_match);
#endif

    static void hitChanged()
    {
        cpu::advancedHitChanged();
    }

    static void valueChanged()
    {
        cpu::advancedValueChanged();
    }

    static bool custom()
    {
        return counterLabels() && main::settings().cpu.options.advLogic == cpu::kLogicCustom;
    }

#ifdef __MWERKS__
    extern Page kAdvancedCounter = {"Per-Hit Counters", NULL, 0, {0, 0, 0}};

    void initAdvancedCounterPage(Option* buf)
    {
        static const u16 kStrs[6] = {
            kPs_fmt_d, kPs_ac_kLogic, kPs_cpu_kCounterGround, kPs_cpu_kCounterAir,
            kPs_cpu_kCounterShield, kPs_fmt_df
        };
        static void (*const kCbs[3])() = {NULL, hitChanged, valueChanged};
        static bool (*const kGates[3])() = {NULL, cpuLabControl, custom};
        static const RowSpec kSpecs[8] = {
            {kKindInt,    0, TM_FIELD(cpu.options.advHit), 1, 1, cpu::kAdvancedHits, 0, 1, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.advLogic), TM_CHOICE_LO, 0, 2, 1, 2, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.advGround), TM_CHOICE_LO, 13, kCounterGroundSize - 1, 2, 2, 2, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.advAir), TM_CHOICE_LO, 1, kCounterAirSize - 1, 3, 2, 2, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.advShield), TM_CHOICE_LO, 1, kCounterShieldSize - 1, 4, 2, 2, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.advDelayGround), 0, 0, 99, 5, 2, 2, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.advDelayAir), 0, 0, 99, 5, 2, 2, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.advDelayShield), 0, 0, 99, 5, 2, 2, 0}
        };
        fillPageRows(kAdvancedCounter, buf, 8, kSpecs, kPs_ac_names, kStrs, NULL, kCbs, kGates);
    }
#else
#define TM_ADV_CHOICE(name, f, labels, count, def) \
    TM_ROW_CHOICE_P(0, TM_FIELD(f), count, def, name, labels, valueChanged, custom)
#define TM_ADV_DELAY(name, f) \
    TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(f), 0, 99, 0, name, "%df", valueChanged, custom)

    static const Option kRows[] = {
        TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(cpu.options.advHit), 1, cpu::kAdvancedHits, 1, "Hit Index", "%d",
               hitChanged, cpuLabControl),
        TM_ROW_CHOICE_P(0, TM_FIELD(cpu.options.advLogic), kLogicCount, 0, "Hit Rule",
                        kLogic, valueChanged, cpuLabControl),
        TM_ADV_CHOICE("Ground Action", cpu.options.advGround, kCounterGround, kCounterGroundSize, 13),
        TM_ROW_CHOICE_P(0, TM_FIELD(cpu.options.advAir), kCounterAirSize, 1, "Air Action",
                        kCounterAir, valueChanged, custom),
        TM_ADV_CHOICE("Shield Action", cpu.options.advShield, kCounterShield, kCounterShieldSize, 1),
        TM_ADV_DELAY("Ground Delay", cpu.options.advDelayGround),
        TM_ADV_DELAY("Air Delay", cpu.options.advDelayAir),
        TM_ADV_DELAY("Shield Delay", cpu.options.advDelayShield)
    };
#undef TM_ADV_CHOICE
#undef TM_ADV_DELAY

    extern const Page kAdvancedCounter = TM_PAGE("Per-Hit Counters", kRows);
#endif
}
}
}
