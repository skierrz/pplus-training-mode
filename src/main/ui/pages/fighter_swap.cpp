#include "main/ui/pages/fighter_swap.h"

#include <stddef.h>

#include "main/extra.h"
#include "main/ui/pages/pages.h"

// Minimal neutral baseline UI page for mid-session Fighter Swap.
// Values live strictly in ui::menuScratch() (100% session-only). The roster
// label table and swap logic live in the extra module to keep match lean.
namespace tm {
namespace ui {
namespace pages {
    static const char* const kFallbackRoster[] = {"-"};

#define TM_SWAP_OFF(f) static_cast<u16>(kFighterSwapScratchOffset + offsetof(FighterSwapMenuState, f))

    // Non-const so extra/fighter_swap.cpp binds its roster labels on attach
    // and updates costume upper bounds per chosen fighter.
    static Option kRows[] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SWAP_OFF(p1Fighter), 0, 0, 0, "P1 Fighter",
               kFallbackRoster, main::extraFighterSwapChanged, main::extraFighterSwapRow),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch, TM_SWAP_OFF(p1Costume), 0, 0, 0, "P1 Costume", "%d",
               main::extraFighterSwapChanged, main::extraFighterSwapRow),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SWAP_OFF(cpuFighter), 0, 0, 0, "CPU Fighter",
               kFallbackRoster, main::extraFighterSwapChanged, main::extraFighterSwapRow),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch, TM_SWAP_OFF(cpuCostume), 0, 0, 0, "CPU Costume", "%d",
               main::extraFighterSwapChanged, main::extraFighterSwapRow),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Apply Swap", NULL, main::extraFighterSwapApply,
               main::extraFighterSwapRow)
    };
#undef TM_SWAP_OFF

    extern const Page kFighterSwap = TM_PAGE("Fighter Swap", kRows);
}
}

namespace main {
    // Called from extra/fighter_swap.cpp; keep from linker dead-stripping.
#pragma force_active on
    void setFighterSwapLabels(const char* const* names, int maxIndex)
    {
        using namespace ui::pages;
        if (names == NULL || maxIndex < 0)
        {
            kRows[0].data = kFallbackRoster;
            kRows[0].max = 0;
            kRows[2].data = kFallbackRoster;
            kRows[2].max = 0;
            return;
        }
        kRows[0].data = names;
        kRows[0].max = static_cast<s16>(maxIndex);
        kRows[2].data = names;
        kRows[2].max = static_cast<s16>(maxIndex);
    }

    void setFighterSwapCostumeMax(int side, int maxIndex)
    {
        using namespace ui::pages;
        int rowIdx = side == 0 ? 1 : 3;
        int hi = maxIndex < 0 ? 0 : (maxIndex > 31 ? 31 : maxIndex);
        kRows[rowIdx].max = static_cast<s16>(hi);
    }
#pragma force_active reset
}
}
