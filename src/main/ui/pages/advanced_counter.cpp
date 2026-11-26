#include "main/cpu/options.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    TM_LABELS_BEGIN(kLogic) TM_L("Default") TM_L("Off") TM_L("Custom") TM_LABELS_END
    enum { kLogicCount = 3 };
    TM_LABELS_ASSERT(kLogic, kLogicCount, tm_adv_logic_labels_match);

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

#define TM_ADV_CHOICE(name, f, labels, count, def) \
    TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(f), 0, (count) - 1, def, name, labels, valueChanged, custom)
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
}
}
}
