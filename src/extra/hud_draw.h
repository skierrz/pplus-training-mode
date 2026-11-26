#pragma once

#include <types.h>

#include "main/game/fighters.h"
#include "main/game/match.h"
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

    // Quiet Panel B borderless smoked-dark plate (0x0E161EA6u) with zero border
    // and zero colored header tab, plus faint 2px inset horizontal divider.
    static const gfx::Rgba kSmokedPlateBg = 0x0E161EA6u;
    static const gfx::Rgba kSmokedDivider = 0xDCE8F028u;

    void drawSmokedPlate(float x, float y, float w, float h, gfx::Rgba bg = kSmokedPlateBg);
    void drawPlateDivider(float x, float y, float w, float xs);

    // Draws centered shadowed text sized to `cap` pixels around horizontal `cx`.
    void drawShadowCapCenter(float cx, float capY, const char* text, gfx::Rgba color, float cap, float maxW = 0.0f);

    // One key item for the calm white legend row above a timing/state bar.
    struct TimingLegendItem {
        gfx::Rgba swatch; // 0 = caption text only (no color swatch)
        const char* label;
    };

    // Unified Zone 1 Top-Center Timing / State Bar:
    //  - calm white Rodin caption/legend row directly above bar (>= 6px item gap)
    //  - crisp color cells inside a quiet smoked trough (dynamic width clamping)
    //  - clean white upward pointer arrow (^) at `pointerCell` (-1 = none)
    // Returns the total occupied bounding box for top-band registration.
    ui::layout::Box drawTimingBar(const gfx::Rgba* cells, int count, int pointerCell, const TimingLegendItem* legend,
                                  int legendCount, const char* caption, bool wide);

    // One metric row for a Panel B right-margin quiet smoked stat plate.
    struct StatPlateRow {
        const char* label;
        const char* value;
        gfx::Rgba valueColor;
    };

    // Unified Zone 2 Panel B Right-Margin Stat Plate:
    //  - single quiet borderless smoked plate flush to x1 = 608 under Option A GC Controller
    //  - faint 2px inset horizontal dividers between rows
    //  - dead-center muted-white labels above bold centered semantic values (>= 10.5 scanlines)
    void drawStatPlate(const StatPlateRow* rows, int count, float aboveBottom, bool wide);

    // Compact portrait-docked `CPU: <State>` tag (10.5 scanlines, y = 382..395)
    // drawn above the CPU portrait in Training Lab (hidden in Versus).
    void drawCpuStatusTag(const game::MatchState& match, bool menuOpen);

    // Bottom y of active top-left Info Display panels (for PSA stacking).
    float hudInfoStackBottom(bool wide);

    // Bottom y of active Option A GC Controller slots (for right-margin Panel B plates).
    float hudViewerColumnBottom(bool wide);

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
