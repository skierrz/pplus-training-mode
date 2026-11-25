#pragma once

#include <types.h>

#include "main/gfx/draw.h"

// Shared 2D HUD chrome and circle-math helpers for the extra module, avoiding
// duplicated GX call sites and kept match-module relocations across overlays.
namespace tm {
namespace extra {
    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale);

    // Double-framed dark-teal P+ overlay card. If `sheen != 0`, draws a 1px top
    // highlight across the accent stripe.
    void drawHudCard(float x, float y, float w, float h, float xs, gfx::Rgba accent, gfx::Rgba sheen);

    // 16-segment unit circle vertex (step 22.5 deg; stride 2 gives an octagon).
    void unitPoint16(int idx, float& ox, float& oy);
}
}
