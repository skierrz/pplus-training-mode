#include "main/ui/pages/pages.h"
#ifdef __MWERKS__
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
    enum {
        kPoolRowCount = 65,
        kCpuOverlayRowCount = 17,
        kCpuInfoRowCount = 10,
        kDynRowTotal = kPoolRowCount + kCpuOverlayRowCount + kCpuInfoRowCount
    };

    static Option* s_dynRows = NULL;

    void pagesAttach()
    {
        if (s_dynRows == NULL)
        {
            s_dynRows = static_cast<Option*>(game::allocNetworkZero(static_cast<u32>(sizeof(Option) * kDynRowTotal)));
        }
        if (s_dynRows != NULL)
        {
            initPoolPage(s_dynRows);
            initCpuOverlaysPage(s_dynRows + kPoolRowCount);
            initCpuInfoPage(s_dynRows + kPoolRowCount + kCpuOverlayRowCount);
        }
    }

    void pagesDetach()
    {
        kCounterPool.options = NULL;
        kCounterPool.count = 0;
        kCpuOverlays.options = NULL;
        kCpuOverlays.count = 0;
        kCpuInfo.options = NULL;
        kCpuInfo.count = 0;
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_dynRows));
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
