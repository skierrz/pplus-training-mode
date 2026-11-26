#pragma once

#include <types.h>

#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/ui/hud_layout.h"

// Shared 2D HUD chrome and circle-math helpers for the extra module, avoiding
// duplicated GX call sites and kept match-module relocations across overlays.
namespace tm {
namespace extra {
    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale);

    // Draws shadowed text sized to `cap` pixels; when `rightAlign` is true, `x`
    // is the right edge.
    void drawShadowCapText(float x, float capY, const char* text, gfx::Rgba color, float cap, bool rightAlign = false);

    // Double-framed dark-teal P+ overlay card. If `sheen != 0`, draws a 2px top
    // highlight across the accent stripe.
    void drawHudCard(float x, float y, float w, float h, float xs, gfx::Rgba accent, gfx::Rgba sheen);

    // Unified top-center Brawl/P+ double-framed banner with header band and divider.
    void drawHudBanner(float ox, float oy, float bw, float bh, float xs, gfx::Rgba accent);

    // Draws drawHudBanner plus the standard 8.5pt Brawl Display title on the left.
    void drawHudBannerTitle(float ox, float oy, float bw, float bh, float xs, gfx::Rgba accent, const char* title);

    // Compact Brawl-styled mode/status pill inside a top HUD banner.
    void drawHudPill(float x, float y, float w, float xs, bool gold);

    // Crisp 2.0px integer CRT-safe horizontal divider inside a HUD card.
    void drawHudDivider(float x, float y, float bw, float xs);

    // Filled unit-circle dial with axes and 16-segment rim (shared by DI
    // compass, custom DI picker, and stick gates).
    void drawUnitDial(float cx, float cy, float rx, float ry, gfx::Rgba fillCol, gfx::Rgba rimCol);

    // Draws a dial vector ray (when `drawRay`) plus a dark-rimmed tip dot.
    void drawDialRayDot(float cx, float cy, float rx, float ry, float vx, float vy, float rayScale, float dotR,
                        float xs, gfx::Rgba rayCol, gfx::Rgba dotCol, u8 lineW, bool drawRay);

    // Hardware pad port (0-3 GC, 4-7 Wii) for a human fighter from ipPadConfig.
    int extraHumanPort(const game::FighterRef& ref);

    // Truncates `buf` with ".." until its rendered width fits `maxW`.
    void clipTextToWidth(char* buf, float scale, float maxW);

    // Counts active Input Viewer slots (0..2) so stacked HUD cards clear it.
    int countInputViewerSlots(const game::FighterRef* refs, int n);

    // 16-segment unit circle vertex (step 22.5 deg; stride 2 gives an octagon).
    void unitPoint16(int idx, float& ox, float& oy);

    enum HudRightCard {
        kHudCardDiCompass = 0,
        kHudCardPsa = 1,
        kHudCardAdaptive = 2,
        kHudCardTechchase = 3
    };

    // Out-of-line wrapper around ui::layout right-column card boxes so
    // rightColumnBottom() is compiled once in hud_draw.o instead of inlined
    // into every overlay translation unit.
    ui::layout::Box hudRightCardBox(int kind, bool wide, int viewerSlotsOverride = -1, int hasCompOverride = -1);

    // Out-of-line wrapper around ui::layout::inputViewerCardBox.
    ui::layout::Box hudViewerCardBox(int slot, bool wide);

    // Out-of-line OSD top-start Y hook shared by extra/module.cpp so
    // ui::layout right-column geometry is compiled once in hud_draw.o.
    float hudOsdTopStartY(bool isTopRight, bool vsReadOnly);
}
}
