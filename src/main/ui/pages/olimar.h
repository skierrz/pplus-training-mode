#pragma once

#include <types.h>

#include "main/ui/menu.h"

// Unpacked menu scratch mirror for P_OLIMAR (persisted bit-packed inside
// main::FighterMechanicsSettings bytes 455..463).
namespace tm {
namespace ui {
namespace pages {
    struct OlimarMenuState {
        u8 dpadMode;      // 0..2 (main::OlimarDpadMode)
        u8 whistleColor;  // 0..5 (Default, R, Y, B, W, P)
        u8 allMaturity;   // 0..3 (Default, Leaf, Bud, Flower)
        u8 colorMat[5];   // 0..3 per color (R, Y, B, W, P)
        u8 startSlot[4];  // 0..6 per slot (Default, R, Y, B, W, P, Empty)
        u8 dpadPreset[4]; // 0..15 per D-pad dir (Up, Down, Left, Right)
    };

    TM_STATIC_ASSERT(kOlimarScratchOffset > kCounterModeScratchOffset,
                     tm_olimar_scratch_after_counter_mode);
    TM_STATIC_ASSERT(kOlimarScratchOffset + sizeof(OlimarMenuState) <= kScratchBytes,
                     tm_olimar_scratch_fits_menu_scratch);

    inline OlimarMenuState* olimarMenuState()
    {
        return reinterpret_cast<OlimarMenuState*>(menuScratch() + kOlimarScratchOffset);
    }

    // Copies bit-packed main::settings().fighterMechanics Olimar fields into
    // OlimarMenuState scratch bytes (called when entering P_OLIMAR).
    void olimarSyncFromSettings();
}
}
}
