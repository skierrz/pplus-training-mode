#pragma once

#include <types.h>

#include "main/game/fighters.h"
#include "main/gfx/draw.h"

// Shared 2D HUD chrome and circle-math helpers for the extra module, avoiding
// duplicated GX call sites and kept match-module relocations across overlays.
namespace tm {
namespace extra {
    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale);

    // Double-framed dark-teal P+ overlay card. If `sheen != 0`, draws a 2px top
    // highlight across the accent stripe.
    void drawHudCard(float x, float y, float w, float h, float xs, gfx::Rgba accent, gfx::Rgba sheen);

    // Crisp 2.0px integer CRT-safe horizontal divider inside a HUD card.
    void drawHudDivider(float x, float y, float bw, float xs);

    // Filled unit-circle dial with axes and 16-segment rim (shared by DI
    // compass, custom DI picker, and stick gates).
    void drawUnitDial(float cx, float cy, float rx, float ry, gfx::Rgba fillCol, gfx::Rgba rimCol);

    // Truncates `buf` with ".." until its rendered width fits `maxW`.
    void clipTextToWidth(char* buf, float scale, float maxW);

    // Counts active Input Viewer slots (0..2) so stacked HUD cards clear it.
    int countInputViewerSlots(const game::FighterRef* refs, int n);

    // 16-segment unit circle vertex (step 22.5 deg; stride 2 gives an octagon).
    void unitPoint16(int idx, float& ox, float& oy);
}
}
