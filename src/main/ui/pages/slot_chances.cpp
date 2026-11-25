#include "main/record/record.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // One row per recording slot (available once it holds inputs; changing one
    // rebalances the active slots to 100%) plus Random Percent added on load.
#define TM_CHANCE_FNS(side, n)                 \
    static void chance##side##n()              \
    {                                          \
        record::chanceChanged(side, n);        \
    }                                          \
    static bool has##side##n()                 \
    {                                          \
        return record::slotHasInputs(side, n); \
    }
    TM_CHANCE_FNS(0, 0)
    TM_CHANCE_FNS(0, 1)
    TM_CHANCE_FNS(0, 2)
    TM_CHANCE_FNS(0, 3)
    TM_CHANCE_FNS(0, 4)
    TM_CHANCE_FNS(0, 5)
    TM_CHANCE_FNS(1, 0)
    TM_CHANCE_FNS(1, 1)
    TM_CHANCE_FNS(1, 2)
    TM_CHANCE_FNS(1, 3)
    TM_CHANCE_FNS(1, 4)
    TM_CHANCE_FNS(1, 5)
#undef TM_CHANCE_FNS

#define TM_CHANCE(name, side, f, n) \
    TM_ROW(::tm::ui::kKindInt, 0, TM_FIELD(f[n]), 0, 100, 0, name, "%d%%", chance##side##n, has##side##n)
#define TM_CHANCES(side, f)                                                                          \
    {TM_CHANCE("Slot 1", side, f, 0), TM_CHANCE("Slot 2", side, f, 1), TM_CHANCE("Slot 3", side, f, 2), \
     TM_CHANCE("Slot 4", side, f, 3), TM_CHANCE("Slot 5", side, f, 4), TM_CHANCE("Slot 6", side, f, 5), \
     TM_INT("Random Percent", recording.randomPercent[side], 0, savestate::kMaxRandomPercent, 0, "%d%%", NULL)}

    static const Option kHmnRows[] = TM_CHANCES(0, recording.hmnChance);
    static const Option kCpuRows[] = TM_CHANCES(1, recording.cpuChance);
#undef TM_CHANCES
#undef TM_CHANCE

    extern const Page kHmnChances = TM_PAGE("P1 Slot Chances", kHmnRows);
    extern const Page kCpuChances = TM_PAGE("CPU Slot Chances", kCpuRows);
}
}
}
