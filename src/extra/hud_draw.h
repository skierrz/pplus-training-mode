#pragma once

#include <types.h>

#include "main/game/fighters.h"
#include "main/game/match.h"
#include "main/gfx/draw.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"

// Shared 2D HUD chrome and circle-math helpers for the extra module, avoiding
// duplicated GX call sites and kept match-module relocations across overlays.
namespace tm {
namespace extra {
    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale);

    // Draws shadowed text sized to `cap` pixels; when `rightAlign` is true, `x`
    // is the right edge.
    void drawShadowCapText(float x, float capY, const char* text, gfx::Rgba color, float cap, bool rightAlign = false);

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
        kHudCardPsa = 1,
        kHudCardAdaptive = 2,
        kHudCardTechchase = 3
    };

    // Shared semantic HUD scrim and feedback tokens for live extra/ overlays
    // (~77% dark smoked slate for >= 4.5:1 text contrast over bright stages).
    static const gfx::Rgba kSmokedPlateBg = ui::style::kSmokedPlateBg;
    static const gfx::Rgba kSmokedPlateEdge = ui::style::kSmokedPlateEdge;
    static const gfx::Rgba kSmokedPlateSheen = ui::style::kSmokedPlateSheen;
    static const gfx::Rgba kSmokedDivider = ui::style::kSmokedDivider;
    static const gfx::Rgba kHudOutlineEdge = ui::style::kHudOutlineEdge;
    static const gfx::Rgba kHudShadow = ui::style::kHudShadow;
    static const gfx::Rgba kHudTextPrimary = ui::style::kHudTextPrimary;
    static const gfx::Rgba kHudTextLabel = ui::style::kHudTextLabel;
    static const gfx::Rgba kHudTextValue = ui::style::kHudTextValue;
    static const gfx::Rgba kHudTroughBg = ui::style::kHudTroughBg;
    static const gfx::Rgba kHudCellEmpty = ui::style::kHudCellEmpty;
    static const gfx::Rgba kHudCellSheen = ui::style::kHudCellSheen;
    static const gfx::Rgba kHudGold = ui::style::kHudGold;
    static const gfx::Rgba kHudYellow = ui::style::kHudYellow;
    static const gfx::Rgba kHudGreen = ui::style::kHudGreen;
    static const gfx::Rgba kHudGreenHi = 0x3CE890FFu;
    static const gfx::Rgba kHudWarn = ui::style::kHudWarn;
    static const gfx::Rgba kHudRed = ui::style::kHudRed;
    static const gfx::Rgba kHudSteelBorder = 0x3A4656FFu;
    static const gfx::Rgba kHudCardGlassTop = 0x121822F4u;
    static const gfx::Rgba kHudCardGlassBot = 0x0A0E15F4u;
    static const gfx::Rgba kHudDialBg = 0x080C12E8u;
    static const gfx::Rgba kHudAxisLine = 0x3E4958C0u;

    void drawSmokedPlate(float x, float y, float w, float h, gfx::Rgba bg = kSmokedPlateBg);
    // Smoked glass scrim card strictly within [x..x+w, y..y+h] with crisp 2px dark edge and subtle top sheen.
    void drawSmokedCard(float x, float y, float w, float h, float xs, gfx::Rgba bg = kSmokedPlateBg);
    void drawPlateDivider(float x, float y, float w, float xs);

    // Draws centered shadowed text sized to `cap` pixels around horizontal `cx`.
    void drawShadowCapCenter(float cx, float capY, const char* text, gfx::Rgba color, float cap, float maxW = 0.0f);

    // Draws crisp 4-way dark-outlined floating HUD text (zero muddy diagonal double-shadow).
    void drawOutlinedText(float x, float y, const char* text, gfx::Rgba color, float sx, float sy);

    // One key item for the calm white legend row above a timing/state bar.
    struct TimingLegendItem {
        gfx::Rgba swatch; // 0 = caption text only (no color swatch)
        const char* label;
    };

    // Optional visual extras for drawTimingBar (pointer glide, hit flash, green-window end ticks, rep-pip mode).
    struct TimingBarFx {
        float pointerPos; // >= 0.0f: continuous cell index for smooth pointer glide (overrides pointerCell)
        int flashCell;    // >= 0: cell index receiving landed hit flash
        float flashAlpha; // 0..1 intensity for landed hit flash
        int tickStartCell;
        int tickEndCell;
        const char* tickStartLabel;
        const char* tickEndLabel;
        const char* repTag; // non-NULL: right-side micro-label for rep-history strips ("Streak" / "Reps")
        bool isRepStrip;    // true: render spaced discrete streak/rep pips instead of timing bar cells
    };

    // Unified Zone 1 Top-Center Timing / State Bar:
    //  - when `leftLabel`/`rightLabel` are set, draws the calm 3-zone header
    //    (Early left | Drill Title center | Late right) on a smoked scrim strip
    //  - otherwise draws the calm white swatch-legend or centered caption row
    //  - crisp color cells inside a dark-outlined trough (widescreen expanded)
    //  - clean white upward pointer arrow (^) at `pointerCell` (-1 = none)
    // Returns the total occupied bounding box for top-band registration.
    ui::layout::Box drawTimingBar(const gfx::Rgba* cells, int count, int pointerCell, const TimingLegendItem* legend,
                                  int legendCount, const char* caption, bool wide, const char* leftLabel = NULL,
                                  const char* rightLabel = NULL, const TimingBarFx* fx = NULL);

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
