#pragma once

#include <types.h>

// Stage ray measurements, built only with `make EXPERIMENTS=1` (scripts/
// di_tests.py --measure-ray). The harness writes rays into g_rayProbe; the
// next drawing of the DI Display casts them with the game's stRayCheck and
// writes the answers back. With `ecb` set it also logs every fighter's ECB
// points against its position, once per drawing.
namespace tm {
namespace experiments {
    struct RayProbe {
        u32 n;     // rays to cast at the next drawing (cleared when done)
        u32 done;  // drawings that cast rays
        u32 ticks; // OS ticks the last batch took
        u32 ecb;   // 1: log the fighters' ECB points; 2: the DI Display's time per fighter
        struct Ray {
            float pos[3];
            float dir[3];
            u8 kind; // 0: stRayCheck with hit point and normal, 1: with line id too, 2: without outputs
            u8 b1, b2, mask;
            float hit[3];
            float normal[3];
            s32 ret;
            s32 line;
        } rays[16];
    };
    extern RayProbe g_rayProbe;
    // stRayCheck calls the DI Display made for the fighter being drawn
    // (logged with its drawing time while bit 2 of `ecb` is set).
    extern u32 g_rayCount;

    // From the DI Display's drawing.
    void rayProbeRun();
}
}
