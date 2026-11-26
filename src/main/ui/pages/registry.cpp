#include "main/ui/pages/pages.h"
#ifdef __MWERKS__
#include "main/ui/pages/page_strings.h"
#include "shared/game/heap.h"
#endif

namespace tm {
namespace ui {
namespace pages {
    bool never()
    {
        return false;
    }

#ifdef __MWERKS__
    void fillPageRows(Page& page, Option* buf, int count, const RowSpec* specs, const char* names,
                      const void* const* dataTbl, void (*const* cbTbl)(), bool (*const* gateTbl)())
    {
        const char* nm = names;
        for (int i = 0; i < count; i++)
        {
            const RowSpec& sp = specs[i];
            Option& o = buf[i];
            o.kind = sp.kind;
            o.flags = sp.flags;
            o.offset = sp.off;
            o.min = static_cast<u8>(sp.lo);
            o.def = sp.def;
            o.max = sp.hi;
            o.name = nm;
            o.data = (sp.dataIdx == 0xFF || dataTbl == NULL) ? NULL : dataTbl[sp.dataIdx];
            o.onChange = (sp.cbIdx == 0 || cbTbl == NULL) ? NULL : cbTbl[sp.cbIdx];
            o.enabled = (sp.gateIdx == 0 || gateTbl == NULL) ? NULL : gateTbl[sp.gateIdx];
            while (*nm++ != '\0') {}
        }
        page.options = buf;
        page.count = static_cast<u8>(count);
    }

    enum {
        kCpuAllRowCount = 14 + 8 + 11 + 7 + 15 + 65, // main, di, counters, defense, reversals, pool
        kCpuOverlayRowCount = 17,
        kCpuInfoRowCount = 10,
        kStageCastRowCount = 34,
        kScenariosRowCount = 45,
        kOsdMenuRowCount = 26,
        kComboTrainerRowCount = 23, // 16 + 7 meters
        kTechOptionsRowCount = 17,
        kDynRowTotal = kCpuAllRowCount + kCpuOverlayRowCount + kCpuInfoRowCount + kStageCastRowCount
                       + kScenariosRowCount + kOsdMenuRowCount + kComboTrainerRowCount + kTechOptionsRowCount
    };

    static Option* s_dynRows = NULL;

    void pagesAttach()
    {
        pageStringsAttach();
        if (s_dynRows == NULL)
        {
            s_dynRows = static_cast<Option*>(game::allocNetworkZero(static_cast<u32>(sizeof(Option) * kDynRowTotal)));
        }
        if (s_dynRows != NULL)
        {
            Option* p = s_dynRows;
            initCpuPages(p); p += kCpuAllRowCount;
            initAdvancedCounterPage();
            initOverlayPages(p); p += kCpuOverlayRowCount;
            initInfoPages(p); p += kCpuInfoRowCount;
            initStageCastPage(p); p += kStageCastRowCount;
            initScenariosPage(p); p += kScenariosRowCount;
            initOsdMenuPage(p); p += kOsdMenuRowCount;
            initComboTrainerPages(p); p += kComboTrainerRowCount;
            initTechOptionsPage(p);
        }
    }

    void pagesDetach()
    {
        Page* const dyn[] = {&kCpuOptions, &kCpuDi, &kCpuCounters, &kCpuDefense, &kReversalSlots, &kCounterPool,
                             &kCpuOverlays, &kCpuInfo, &kStageCast, &kScenarios, &kOsdMenu, &kComboTrainer,
                             &kComboMeters, &kTechOptions};
        for (int i = 0; i < static_cast<int>(sizeof(dyn) / sizeof(dyn[0])); i++)
        {
            dyn[i]->options = NULL;
            dyn[i]->count = 0;
        }
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_dynRows));
        pageStringsDetach();
    }
#endif

    // Indexed by PageId.
    extern const Page* const kPages[P_COUNT] = {
        &kGeneral,      &kScenarios,    &kCpuOptions,     &kRecording,   &kDisplayOptions, &kOsdMenu,
        &kStageCast,    &kControls,     &kDebug,          &kComboTrainer, &kComboMeters,   &kTechOptions,
        &kAdvancedCounter, &kSlotManagement, &kAlterInputs, &kHmnChances, &kCpuChances,    &kHmnOverlays,
        &kCpuOverlays,  &kHmnInfo,      &kCpuInfo,        &kLedgedashHud, &kFighterSwap, &kTheme,
        &kReversalSlots, &kCpuDi,       &kCpuCounters,    &kCpuDefense,  &kCounterPool
    };
}
}
}
