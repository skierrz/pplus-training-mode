#pragma once

#include <types.h>

// Native Brawl / Project+ UI emblem and stock-icon renderer in the extra
// module. Binds resident CI8/CI4 textures directly in-place from MEM1
// (info_training.pac BRRES) and MEM2 (StockFaceTex.brres) with zero extra
// heap allocations.
namespace tm {
namespace extra {
    void uiAssetsAttach();
    void uiAssetsDetach();

    // Draws one native Brawl/P+ controller or cursor emblem from the resident
    // info_training BRRES. Returns false when unavailable so the caller can
    // fall back to geometric shapes or text pills.
    bool uiAssetsDrawIcon(u8 iconId, float x, float y, float w, float h, u32 rgba);

    // Draws page-specific native overlays inside the lab menu (currently the
    // live P1 & CPU 32x32 stock icon preview on Stage & Cast > Fighter Swap).
    void uiAssetsDrawPageOverlay(u8 pageId, bool railMode);
}
}
