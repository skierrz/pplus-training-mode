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
    void fillPageRows(Page& page, Option* buf, int count, const RowSpec* specs, u16 namesOff,
                      const u16* strOffs, const void* const* extraData,
                      void (*const* cbTbl)(), bool (*const* gateTbl)())
    {
        const char* nm = pageStr(namesOff);
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
            if (sp.dataIdx == 0xFF)
            {
                o.data = NULL;
            }
            else if (sp.dataIdx >= 0xF0)
            {
                o.data = extraData != NULL ? extraData[sp.dataIdx - 0xF0] : NULL;
            }
            else
            {
                o.data = strOffs != NULL ? pageStr(strOffs[sp.dataIdx]) : NULL;
            }
            o.onChange = (sp.cbIdx == 0 || cbTbl == NULL) ? NULL : cbTbl[sp.cbIdx];
            o.enabled = (sp.gateIdx == 0 || gateTbl == NULL) ? NULL : gateTbl[sp.gateIdx];
            while (*nm++ != '\0') {}
        }
        page.options = buf;
        page.count = static_cast<u8>(count);
    }

    enum {
        kCpuAllRowCount = 14 + 6 + 12 + 7 + 15 + 126, // main, di, counters, defense, reversals, pool
        kOverlayRowCount = 34,                        // 17 P1 + 17 CPU
        kInfoRowCount = 20,                           // 10 P1 + 10 CPU
        kStageCastRowCount = 34,
        kScenariosRowCount = 55,
        kOsdMenuRowCount = 26,
        kComboTrainerRowCount = 23,                  // 16 + 7 meters
        kTechOptionsRowCount = 17,
        kRecordingRowCount = 19,
        kGeneralRowCount = 13,
        kControlsRowCount = 10,
        kThemeRowCount = 5,
        kAlterInputsRowCount = 16,
        kSlotManagementRowCount = 6,
        kSlotChancesRowCount = 14,                   // 7 P1 + 7 CPU
        kAdvCounterRowCount = 8,
        kLedgedashHudRowCount = 4,
        kOlimarRowCount = 22,
        kDynRowTotal = kCpuAllRowCount + kOverlayRowCount + kInfoRowCount + kStageCastRowCount
                       + kScenariosRowCount + kOsdMenuRowCount + kComboTrainerRowCount + kTechOptionsRowCount
                       + kRecordingRowCount + kGeneralRowCount + kControlsRowCount + kThemeRowCount
                       + kAlterInputsRowCount + kSlotManagementRowCount + kSlotChancesRowCount
                       + kAdvCounterRowCount + kLedgedashHudRowCount + kOlimarRowCount
    };

    static Option* s_dynRows = NULL;

    void pagesAttach()
    {
        pageStringsAttach();
        if (!pageStringsReady())
        {
            pagesDetach();
            return;
        }
        if (s_dynRows == NULL)
        {
            s_dynRows = static_cast<Option*>(game::allocNetworkZero(static_cast<u32>(sizeof(Option) * kDynRowTotal)));
        }
        if (s_dynRows == NULL)
        {
            pagesDetach();
            return;
        }
        Option* p = s_dynRows;
        initCpuPages(p); p += kCpuAllRowCount;
        initAdvancedCounterPage(p); p += kAdvCounterRowCount;
        initOverlayPages(p); p += kOverlayRowCount;
        initInfoPages(p); p += kInfoRowCount;
        initStageCastPage(p); p += kStageCastRowCount;
        initScenariosPage(p); p += kScenariosRowCount;
        initOsdMenuPage(p); p += kOsdMenuRowCount;
        initComboTrainerPages(p); p += kComboTrainerRowCount;
        initTechOptionsPage(p); p += kTechOptionsRowCount;
        initRecordingPage(p); p += kRecordingRowCount;
        initGeneralPage(p); p += kGeneralRowCount;
        initControlsPage(p); p += kControlsRowCount;
        initThemePage(p); p += kThemeRowCount;
        initAlterInputsPage(p); p += kAlterInputsRowCount;
        initSlotManagementPage(p); p += kSlotManagementRowCount;
        initSlotChancesPages(p); p += kSlotChancesRowCount;
        initLedgedashHudPage(p); p += kLedgedashHudRowCount;
        initOlimarPage(p);
    }

    void pagesDetach()
    {
        Page* const dyn[] = {&kCpuOptions, &kCpuDi, &kCpuCounters, &kCpuDefense, &kReversalSlots, &kCounterPool,
                             &kAdvancedCounter, &kHmnOverlays, &kCpuOverlays, &kHmnInfo, &kCpuInfo, &kStageCast,
                             &kScenarios, &kOsdMenu, &kComboTrainer, &kComboMeters, &kTechOptions, &kRecording,
                             &kGeneral, &kControls, &kTheme, &kAlterInputs, &kSlotManagement, &kHmnChances,
                             &kCpuChances, &kLedgedashHud, &kOlimar};
        for (int i = 0; i < static_cast<int>(sizeof(dyn) / sizeof(dyn[0])); i++)
        {
            dyn[i]->options = NULL;
            dyn[i]->count = 0;
        }
        detachCpuPages();
        detachStageCastPage();
        detachRecordingPage();
        detachAlterInputsPage();
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
        &kReversalSlots, &kCpuDi,       &kCpuCounters,    &kCpuDefense,  &kCounterPool, &kOlimar
    };
}
}
}
