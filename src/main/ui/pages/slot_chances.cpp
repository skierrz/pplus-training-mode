#include "main/record/record.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/page_strings.h"
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

#ifdef __MWERKS__
    extern Page kHmnChances = {"P1 Slot Chances", NULL, 0, {0, 0, 0}};
    extern Page kCpuChances = {"CPU Slot Chances", NULL, 0, {0, 0, 0}};

    static void fillChancePage(Page& page, Option* buf, u16 baseOff, u16 rndOff,
                               void (*const* cbs)(), bool (*const* gates)())
    {
        const char* nm = pageStr(kPs_sc_names);
        const char* pct = pageStr(kPs_fmt_pct);
        for (int i = 0; i < 7; i++)
        {
            Option& o = buf[i];
            o.kind = kKindInt;
            o.flags = 0;
            o.offset = i < 6 ? static_cast<u16>(baseOff + i) : rndOff;
            o.min = 0;
            o.def = 0;
            o.max = i < 6 ? 100 : savestate::kMaxRandomPercent;
            o.name = nm;
            o.data = pct;
            o.onChange = i < 6 ? cbs[i] : NULL;
            o.enabled = i < 6 ? gates[i] : NULL;
            while (*nm++ != '\0') {}
        }
        page.options = buf;
        page.count = 7;
    }

    void initSlotChancesPages(Option* buf)
    {
        static void (*const kHmnCbs[6])() = {chance00, chance01, chance02, chance03, chance04, chance05};
        static bool (*const kHmnGates[6])() = {has00, has01, has02, has03, has04, has05};
        static void (*const kCpuCbs[6])() = {chance10, chance11, chance12, chance13, chance14, chance15};
        static bool (*const kCpuGates[6])() = {has10, has11, has12, has13, has14, has15};
        fillChancePage(kHmnChances, buf, TM_FIELD(recording.hmnChance[0]),
                       TM_FIELD(recording.randomPercent[0]), kHmnCbs, kHmnGates);
        fillChancePage(kCpuChances, buf + 7, TM_FIELD(recording.cpuChance[0]),
                       TM_FIELD(recording.randomPercent[1]), kCpuCbs, kCpuGates);
    }
#else
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
#endif
}
}
}
