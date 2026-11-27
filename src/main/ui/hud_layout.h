#pragma once

#include "main/gfx/draw.h"

// Screen layout geometry for P+ Training Mode HUD pieces in Brawl's 640x480 2D
// space across 4:3 and 16:9 modes. Drawing code pulls positions from here and
// host tests verify non-overlap and 5% CRT safe-area containment.
namespace tm {
namespace ui {
namespace layout {
    struct Box {
        float x0, y0, x1, y1;
    };

    inline Box box(float x, float y, float width, float height)
    {
        Box b = {x, y, x + width, y + height};
        return b;
    }

    inline float xsOf(bool wide)
    {
        return wide ? 0.75f : 1.0f;
    }

    inline float leftX(float x, bool wide)
    {
        return x * xsOf(wide);
    }

    inline float rightX(float x, bool wide)
    {
        return 640.0f - (640.0f - x) * xsOf(wide);
    }

    inline float centreX(float x, bool wide)
    {
        return 320.0f + (x - 320.0f) * xsOf(wide);
    }

    // ---- 4:3 and 16:9 anamorphic CRT overscan safe area ----
    // 5% margin on each edge (SMPTE RP 27.3 / Nintendo Wii 90% action-safe
    // frame: 32 px on 640, 24 px on 480). Real Wii 16:9 is anamorphic 640x480
    // stretched horizontally across widescreen CRTs/TVs, so X in [32, 608]
    // applies to both 4:3 and 16:9.
    static const float kSafeMarginX = 32.0f;
    static const float kSafeMarginY = 24.0f;

    inline Box safeArea(bool wide)
    {
        (void)wide;
        Box b = {kSafeMarginX, kSafeMarginY, 640.0f - kSafeMarginX, 480.0f - kSafeMarginY};
        return b;
    }

    // ---- Info Display / Frame-Step Inspector panels (ui/info_display.cpp) ----
    // Aligned to the SMPTE 5% safe edge (x0 = 32, y0 = 24) with max 4:3 width <= 218 px
    // (x = 32..250, clearing Brawl's 4:3 match timer at x = 258 by 8 px = F6).
    static const float kInfoLeft = kSafeMarginX;
    static const float kInfoTop = kSafeMarginY;
    static const float kInfoGap = 0.0f; // flush Panel B stacking with 2px inset divider (matches info_display.cpp)
    static const float kInfoSizeScale[3] = {0.84f, 0.92f, 1.0f};
    static const float kInfoWidth43[3] = {206.0f, 214.0f, 218.0f};
    static const float kInfoPitch[3] = {12.5f, 13.0f, 14.5f};

    inline float infoScale(int size)
    {
        return kInfoSizeScale[size >= 0 && size < 3 ? size : 1];
    }

    inline float infoLeft(bool wide)
    {
        (void)wide;
        return kInfoLeft;
    }

    inline float infoWidth(int size, bool wide)
    {
        int idx = size >= 0 && size < 3 ? size : 1;
        return kInfoWidth43[idx] * xsOf(wide);
    }

    inline float infoHeight(int rows, int size)
    {
        int idx = size >= 0 && size < 3 ? size : 1;
        return rows > 0 ? (10.0f + rows * kInfoPitch[idx]) : 0.0f;
    }

    // Top y of the CPU info panel below the human panel (or at humanTop when
    // the human panel has no rows).
    inline float infoCpuTop(float humanTop, float humanBottom)
    {
        return humanBottom > humanTop ? humanBottom + kInfoGap : humanTop;
    }

    // ---- Ledgedash / Unified Drill Timing Bar top band (ui/ledgedash_hud.cpp) ----
    // Calm white 3-zone header (Early / Drill Title / Late) or legend directly above
    // bar (y 31..41), pulled gently off the ceiling for natural eye gravitation
    // (08-calm-wavedash), chunky dark-outlined cell bar at y 48..64 (16 px tall),
    // and upward pointer arrow at y 65.5..72.5.
    // Max width 450 px on 4:3 (x 32..482) clears the Top-Right Option A GC
    // controller (x 490..608) by 8 px (F6) even at 64 cells.
    static const float kBarTop = 43.0f;
    static const float kBarBottom = 59.0f;
    static const float kBarMaxW = 450.0f;
    static const float kKeyY = 25.0f;
    static const float kKeyLabelTop = 29.0f;
    static const float kLegendTop = 60.5f;
    static const float kLegendCap = 9.0f;

    static const float kCellW = 11.5f;
    static const float kCellGap = 1.5f;
    static const float kKeySwatch = 7.5f;
    static const float kKeyCap = 9.0f;

    // The Ledgedash HUD's bar of `cells` cells, centred and dynamically narrowed
    // to clear the Top-Right Option A GC controller within the 5% CRT safe area.
    Box frameBarBox(int cells, bool wide);

    // The Ledgedash HUD's calm white legend/caption strip directly above the bar.
    Box ledgeHeaderBox(bool wide);

    // The Ledgedash HUD's upward pointer arrow zone directly below the bar.
    Box ledgeLegendBox(bool wide);

    // ---- Pause-menu hint (lab.cpp) ----
    // textBox pads 3 px above kHintY and 6 px right of text, so right-aligning
    // against kHintRight = 602.0 keeps the right edge at x = 608.0 (5% safe).
    static const float kHintX = 520.0f;
    static const float kHintRight = 602.0f;
    static const float kHintY = 27.0f;

    // ---- Recording HUD and storage notice (record/hud.cpp, storage/notice.cpp) ----
    // Stacked cleanly above the portrait-docked CPU status tag (y 382..395) and
    // damage row (y >= 397) with clear headroom in both 4:3 and 16:9.
    static const float kRecMessageY = 311.0f;
    static const float kNoticeY = 338.0f;
    static const float kRecBarCentreY = 366.0f;
    static const float kRecBarHeight = 20.0f;

    // ---- Portrait-docked CPU status tag (extra/hud_draw.cpp) ----
    // Compact 13-scanline tag docked above the CPU portrait (y 382..395),
    // clearing both kPositionHud OSDs and recBar (y <= 376) by 6 px.
    static const float kCpuTagTop = 382.0f;
    static const float kCpuTagHeight = 13.0f;
    static const float kCpuTagWidth = 108.0f;

    inline Box cpuStatusTagBox(int cpuPlayerNo, bool wide)
    {
        static const float kCenter16[4] = {107.0f, 253.0f, 395.0f, 537.0f};
        static const float kCenter43[4] = {100.0f, 248.0f, 395.0f, 541.5f};
        int p = (cpuPlayerNo >= 0 && cpuPlayerNo < 4) ? cpuPlayerNo : 1;
        float w = kCpuTagWidth * xsOf(wide);
        float cx = wide ? kCenter16[p] : kCenter43[p];
        float x0 = cx - w * 0.5f;
        if (x0 < kSafeMarginX)
        {
            x0 = kSafeMarginX;
        }
        if (x0 + w > 640.0f - kSafeMarginX)
        {
            x0 = 640.0f - kSafeMarginX - w;
        }
        return box(x0, kCpuTagTop, w, kCpuTagHeight);
    }

    // Storage notice y while a menu is open: centres a textBox of `lineHeight`
    // in the strip between `panelBottom` and the bottom of the 5% safe area.
    inline float menuNoticeY(float panelBottom, float lineHeight)
    {
        return (panelBottom + (480.0f - kSafeMarginY) - lineHeight) * 0.5f;
    }

    // ---- Training character select reopen hint (menu/drill_list.cpp) ----
    // In the dark teal band (y 56..88) between the TRAINING banner and the
    // fighter icon grid (y >= 89), inside the 5% CRT safe area.
    static const float kCssPendingCapTop = 62.0f;
    static const float kCssPendingRight = 608.0f;

    // ---- Watermark (ui/watermark.cpp) ----
    // Bottom-right inside the 5% CRT safe area (both 4:3 and 16:9).
    static const float kWatermarkRight = 608.0f;
    static const float kWatermarkCapTop = 446.0f;

    // ---- Brawl's native top-band HUD pieces (ui/match_hud.cpp) ----
    Box timerRoom(bool wide);
    Box counterRoom(bool wide);

    // Brawl's stock right-side Training Help sidebar (Speed / Enemy / Damage /
    // Consecutive Hits / Total Damage, IfTraining[0x30] != 0), live-measured in
    // 640x480 2D space on both 16:9 and 4:3 Dolphin D3D captures.
    inline Box trainingSidebarRoom(bool wide)
    {
        Box b16 = {445.0f, 35.0f, 582.0f, 316.0f};
        Box b43 = {492.0f, 35.0f, 608.0f, 316.0f};
        return wide ? b16 : b43;
    }

    // ---- Stepping aside ----
    static const float kStepGap = 8.0f; // F6 unified HUD vertical and horizontal gap

    // Highest bottom edge for lower-screen OSD stacks while the recording seek
    // bar is shown: keeps floor at 350.0 (6 px clear of recBar top 356.0, keeping
    // 3-card Sides OSDs below Adaptive CPU y=205 and 6-card HUD OSDs below timer).
    inline float recBarOsdFloor()
    {
        return kRecBarCentreY - kRecBarHeight * 0.5f - 6.0f;
    }

    inline bool empty(const Box& b)
    {
        return b.x1 <= b.x0 || b.y1 <= b.y0;
    }

    inline Box none()
    {
        Box b = {0.0f, 0.0f, 0.0f, 0.0f};
        return b;
    }

    Box unite(const Box& a, const Box& b);

    inline bool meetsX(const Box& a, const Box& b)
    {
        return !empty(a) && !empty(b) && a.x0 < b.x1 && b.x0 < a.x1;
    }

    inline bool meets(const Box& a, const Box& b)
    {
        return meetsX(a, b) && a.y0 < b.y1 && b.y0 < a.y1;
    }

    // Top y of the first info panel: kInfoTop, or stepped below `band` (and
    // optionally Brawl's native top-left combo counter `counterRoom`) when a
    // panel of `width` would overlap horizontally.
    float infoTop(const Box& band, float width, bool wide);

    inline float infoTopWithCounter(const Box& band, float width, bool wide, bool counter)
    {
        Box combined = counter ? unite(band, counterRoom(wide)) : band;
        return infoTop(combined, width, wide);
    }

    // Top band occupied by the lab this frame: match timer room (when shown)
    // united with the Ledgedash HUD bar and header (when `ledgeCells > 0`).
    Box labBand(bool timer, int ledgeCells, bool wide);

    // ---- Live Tech Practice HUD & End-Screen Modal (extra/drill_hud.cpp) ----
    // Centered top-band pure timing/state bar strictly inside the 5% CRT safe
    // area on both 4:3 and 16:9. Height 40 (y 24..64) stays above Brawl's timer
    // bottom (65) and clears the Top-Right Option A GC controller.
    static const float kDrillHudWidth = 316.0f;
    static const float kDrillHudHeight = 40.0f;
    static const float kDrillHudTop = 24.0f;

    inline Box drillHudBox(bool wide)
    {
        float w = kDrillHudWidth * xsOf(wide);
        return box(320.0f - w * 0.5f, kDrillHudTop, w, kDrillHudHeight);
    }

    // Unified right-column width shared by Option A GC Controller, Panel B Stat
    // Plates, and Adaptive CPU (118 px on 4:3, 88.5 ~ F11 = 89 px on 16:9).
    static const float kRightColWidth = 118.0f;

    // Dynamic top-band box for Ledgedash Practice Drill, spanning the
    // dynamic-width timing bar (`frameBarBox`), its calm white swatch legend
    // (`206 * barScale` via `ledgeHeaderBox`), and pointer zone (`ledgeLegendBox`)
    // so Info Display steps below it cleanly on both 16:9 and 4:3.
    inline Box drillLedgedashBand(int cells, bool wide)
    {
        return unite(frameBarBox(cells, wide), unite(ledgeHeaderBox(wide), ledgeLegendBox(wide)));
    }

    // ---- Combo-Break Rush top HUD (extra/combo_break_rush.cpp) ----
    // Shares the centered top-band safe slot (y 24..64) so side panels step
    // cleanly below labBand on both 4:3 and 16:9.
    inline Box comboBreakHudBox(bool wide)
    {
        return drillHudBox(wide);
    }

    // ---- SF6-Style Aspect-Adaptive Dual Frame Strip (extra/action_log.cpp) ----
    // 2P: bottom-right strip in y 398..438 (16:9: 32 cells @ 6px = 274px, x 334..608;
    // 4:3: 24 cells @ 8px = 286px, x 322..608, clearing P2 % <= 307 by >= 15px).
    // 3P/4P: auto-relocates to top-center (y 24..62, or y 71..109 if timer visible)
    // so P3/P4 portraits (y 396..450) are never touched.
    static const float kActionLogRight = 608.0f;
    static const float kActionLogTop = 398.0f;
    static const float kActionLogWidth16 = 274.0f;
    static const float kActionLogWidth43 = 286.0f;
    static const float kActionLogHeight = 40.0f;
    static const float kActionLogTopHeight = 38.0f;

    inline Box actionLogBox(bool wide, int playerCount = 2, bool timer = false)
    {
        float w = wide ? kActionLogWidth16 : kActionLogWidth43;
        if (playerCount > 2)
        {
            float y0 = timer ? (timerRoom(wide).y1 + 6.0f) : kSafeMarginY;
            return box(320.0f - w * 0.5f, y0, w, kActionLogTopHeight);
        }
        return box(kActionLogRight - w, kActionLogTop, w, kActionLogHeight);
    }

    // Centered End-Screen Summary Modal (5% CRT safe on 4:3 and 16:9; F14 width 377).
    static const float kDrillModalWidth = 377.0f;
    static const float kDrillModalTop = 113.0f;
    static const float kDrillModalHeight = 254.0f;

    inline Box drillModalBox(bool wide)
    {
        float w = kDrillModalWidth * xsOf(wide);
        return box(320.0f - w * 0.5f, kDrillModalTop, w, kDrillModalHeight);
    }

    void setLabBand(const Box& band);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    Box x_topBand();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline Box topBand() { return x_topBand(); }
#else
    Box topBand();
#endif
#else
    Box topBand();
#endif

    // ---- Option A Bare Frameless GC Controller (extra/input_viewer.cpp) ----
    // Unified top baseline y = 24 (SMPTE 5% safe), height F10 = 55 px, gap F6 = 8 px.
    // Dual HMN+CPU: 16:9 side-by-side (w = 88.5, gap 8, x = 423..608, falling back to
    // vertical stack if a wide top-center band reaches x > 423); 4:3 stacked vertically
    // (y = 24..79, 87..142). When Brawl's stock Training Help sidebar is visible, slot 0
    // docks cleanly below the sidebar (y = 324..379) and slot 1 docks left of the sidebar
    // (y = 261..316, clearing both the sidebar and recording seek bar).
    static const float kViewerRight = 608.0f;
    static const float kViewerTop = kSafeMarginY;
    static const float kViewerWidth = kRightColWidth;
    static const float kViewerHeight = 55.0f;
    static const float kViewerGap = kStepGap;

    inline int inputViewerSlotCount(u8 who)
    {
        if (who == 0)
        {
            return 0;
        }
        return who == 3 ? 2 : 1;
    }

    inline Box inputViewerCardBox(int slot, const Box& band, bool wide, bool trainingSidebar = false)
    {
        float w = kViewerWidth * xsOf(wide);
        float x1 = kViewerRight;
        float x0 = x1 - w;
        if (trainingSidebar)
        {
            Box sb = trainingSidebarRoom(wide);
            if (slot <= 0)
            {
                return box(x0, sb.y1 + kStepGap, w, kViewerHeight);
            }
            float sx1 = sb.x0 - kStepGap;
            return box(sx1 - w, sb.y1 - kViewerHeight, w, kViewerHeight);
        }
        if (slot <= 0)
        {
            return box(x0, kViewerTop, w, kViewerHeight);
        }
        if (wide)
        {
            float sx1 = x0 - kViewerGap;
            float sx0 = sx1 - w;
            Box sideProbe = box(sx0, kViewerTop, w, kViewerHeight);
            if (!meets(sideProbe, band))
            {
                return sideProbe;
            }
        }
        float y0 = kViewerTop + static_cast<float>(slot) * (kViewerHeight + kViewerGap);
        return box(x0, y0, w, kViewerHeight);
    }

    // ---- Panel B Right-Margin Drill Stat Plate (extra/hud_draw.cpp, ui/ledgedash_hud.cpp) ----
    // Quiet smoked-dark borderless plate flush on the unified 118 px right column
    // (row pitch F9 = 34 px, gap F6 = 8 px). Anchored vertically in middle-right
    // eye-gravitation real estate (y = 124..234 for 3 rows, centered ~179), staying
    // rock-steady whether 0, 1, or 2 Option A GC controllers are visible above it.
    static const float kDrillPlateWidth = kRightColWidth;
    static const float kDrillPlateGap = kStepGap;
    static const float kDrillPlateRowH = 34.0f;
    static const float kDrillPlateMidY = 124.0f;

    inline Box drillStatPlateBox(int rows, float aboveBottom, bool wide, bool trainingSidebar = false)
    {
        float w = kDrillPlateWidth * xsOf(wide);
        float h = rows > 0 ? (8.0f + static_cast<float>(rows) * kDrillPlateRowH) : 0.0f;
        if (trainingSidebar)
        {
            Box sb = trainingSidebarRoom(wide);
            float y0 = aboveBottom > sb.y1 ? (aboveBottom + kDrillPlateGap) : (sb.y1 + kStepGap);
            if (y0 + h > 390.0f)
            {
                float x1 = sb.x0 - kStepGap;
                return box(x1 - w, 65.0f + kStepGap, w, h);
            }
            return box(kViewerRight - w, y0, w, h);
        }
        float minTop = aboveBottom > 0.0f ? (aboveBottom + kDrillPlateGap) : kDrillPlateMidY;
        float y0 = minTop > kDrillPlateMidY ? minTop : kDrillPlateMidY;
        return box(kViewerRight - w, y0, w, h);
    }

    // ---- Legacy DI Compass geometry (retired from live HUD; kept for host API stability) ----
    static const float kCompassRight = 608.0f;
    static const float kCompassWidth = 82.0f;
    static const float kCompassHeight = 72.0f;

    inline Box diCompassBox(const Box& band, int viewerSlots, bool wide)
    {
        float w = kCompassWidth * xsOf(wide);
        float x1 = kCompassRight;
        float x0 = x1 - w;
        float y0 = kViewerTop;
        if (viewerSlots > 0)
        {
            Box last = inputViewerCardBox(viewerSlots - 1, band, wide);
            y0 = last.y1 + kViewerGap;
            Box first = inputViewerCardBox(0, band, wide);
            if (first.y1 + kViewerGap > y0)
            {
                y0 = first.y1 + kViewerGap;
            }
        }
        else
        {
            Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
            if (meetsX(probe, band) && band.y1 > kViewerTop)
            {
                y0 = band.y1 + kStepGap;
            }
        }
        return box(x0, y0, w, kCompassHeight);
    }

    // ---- Panel B Quiet Smoked-Plate PSA Frame-Step Inspector (extra/psa_inspector.cpp) ----
    // Docks on Top-Left (x0 = 32, width <= 218 px on 4:3 clearing Brawl's 4:3 timer
    // at x = 258 by 8 px), stacking flush below Info Display with a faint 2px inset
    // divider when both fit above y = 348, or falling back cleanly to the right column
    // under any active Option A GC Controller when an extreme 8+8 row Info stack is up.
    static const float kPsaWidth = 218.0f;
    static const float kPsaHeight = 108.0f;
    static const float kAdaptiveHudWidth = kRightColWidth;
    static const float kAdaptiveHudHeight = 55.0f;

    inline Box psaInspectorBox(const Box& band, int viewerSlots, bool hasCompass, bool wide, float infoBottom = 0.0f)
    {
        float xs = xsOf(wide);
        float w = kPsaWidth * xs;
        if (infoBottom <= 0.0f)
        {
            float y0 = infoTop(band, w, wide);
            return box(kInfoLeft, y0, w, kPsaHeight);
        }
        if (infoBottom + kPsaHeight <= 348.0f)
        {
            return box(kInfoLeft, infoBottom, w, kPsaHeight);
        }
        // Extreme 8+8 row Info Display fallback: dock left of the right column
        // (clearing both Info Display x <= 250 and right-column overlays).
        float x1 = kViewerRight;
        if (viewerSlots > 0 || hasCompass)
        {
            float rightW = kRightColWidth * xs;
            x1 = kViewerRight - rightW - kStepGap;
        }
        float x0 = x1 - w;
        float y0 = kViewerTop;
        Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
        if (meetsX(probe, band) && band.y1 + kStepGap > y0)
        {
            y0 = band.y1 + kStepGap;
        }
        for (int s = 0; s < viewerSlots; s++)
        {
            Box vb = inputViewerCardBox(s, band, wide);
            if (meetsX(probe, vb) && vb.y1 + kViewerGap > y0)
            {
                y0 = vb.y1 + kViewerGap;
            }
        }
        return box(x0, y0, w, kPsaHeight);
    }

    // ---- Adaptive CPU Habit Readout Plate (extra/adaptive_cpu.cpp) ----
    // Quiet Panel B smoked plate flush on the unified 118 px right column (F10 = 55 px).

    inline Box adaptiveHudBox(const Box& band, int viewerSlots, bool hasCompass, bool hasPsa, bool wide,
                              float infoBottom = 0.0f, bool trainingSidebar = false)
    {
        float xs = xsOf(wide);
        float w = kAdaptiveHudWidth * xs;
        float x1 = kViewerRight;
        float x0 = x1 - w;
        if (trainingSidebar)
        {
            Box sb = trainingSidebarRoom(wide);
            float y0 = sb.y1 + kStepGap;
            for (int s = 0; s < viewerSlots; s++)
            {
                Box vb = inputViewerCardBox(s, band, wide, true);
                if (meetsX(box(x0, vb.y0, w, 1.0f), vb) && vb.y1 + kViewerGap > y0)
                {
                    y0 = vb.y1 + kViewerGap;
                }
            }
            if (y0 + kAdaptiveHudHeight > 390.0f)
            {
                x1 = sb.x0 - kStepGap;
                x0 = x1 - w;
                y0 = 65.0f + kStepGap;
                if (!empty(band) && meetsX(box(x0, y0, w, 1.0f), band) && band.y1 + kStepGap > y0)
                {
                    y0 = band.y1 + kStepGap;
                }
                if (hasPsa)
                {
                    Box psa = psaInspectorBox(band, viewerSlots, hasCompass, wide, infoBottom);
                    Box cur = box(x0, y0, w, kAdaptiveHudHeight);
                    if (meets(cur, psa))
                    {
                        y0 = psa.y1 + kViewerGap;
                    }
                }
            }
            return box(x0, y0, w, kAdaptiveHudHeight);
        }
        float y0 = kViewerTop;
        if (viewerSlots > 0)
        {
            for (int s = 0; s < viewerSlots; s++)
            {
                Box vb = inputViewerCardBox(s, band, wide);
                if (meetsX(box(x0, vb.y0, w, 1.0f), vb) && vb.y1 + kViewerGap > y0)
                {
                    y0 = vb.y1 + kViewerGap;
                }
            }
        }
        if (hasCompass)
        {
            Box comp = diCompassBox(band, viewerSlots, wide);
            if (comp.y1 + kViewerGap > y0)
            {
                y0 = comp.y1 + kViewerGap;
            }
        }
        else if (viewerSlots == 0)
        {
            Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
            if (meetsX(probe, band) && band.y1 > kViewerTop)
            {
                y0 = band.y1 + kStepGap;
            }
        }
        if (hasPsa)
        {
            Box psa = psaInspectorBox(band, viewerSlots, hasCompass, wide, infoBottom);
            Box cur = box(x0, y0, w, kAdaptiveHudHeight);
            if (meets(cur, psa))
            {
                y0 = psa.y1 + kViewerGap;
            }
        }
        if (y0 + kAdaptiveHudHeight > 255.0f && (viewerSlots > 0 || hasCompass))
        {
            float rightW = (viewerSlots > 0 ? kRightColWidth : kCompassWidth) * xs;
            x1 = kViewerRight - rightW - kStepGap;
            x0 = x1 - w;
            y0 = kViewerTop;
            Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
            if (meetsX(probe, band) && band.y1 + kStepGap > y0)
            {
                y0 = band.y1 + kStepGap;
            }
            for (int s = 0; s < viewerSlots; s++)
            {
                Box vb = inputViewerCardBox(s, band, wide);
                if (meetsX(probe, vb) && vb.y1 + kViewerGap > y0)
                {
                    y0 = vb.y1 + kViewerGap;
                }
            }
            if (hasPsa)
            {
                Box psa = psaInspectorBox(band, viewerSlots, hasCompass, wide, infoBottom);
                Box cur = box(x0, y0, w, kAdaptiveHudHeight);
                if (meets(cur, psa))
                {
                    y0 = psa.y1 + kViewerGap;
                }
            }
        }
        return box(x0, y0, w, kAdaptiveHudHeight);
    }

    // Bottom y of the active P1 + CPU Info Display stack (0 when both hidden).
    inline float infoStackBottom(const Box& band, int hmnRows, int hmnSize, int cpuRows, int cpuSize, bool wide)
    {
        if (hmnRows <= 0 && cpuRows <= 0)
        {
            return 0.0f;
        }
        float wH = hmnRows > 0 ? infoWidth(hmnSize, wide) : 0.0f;
        float wC = cpuRows > 0 ? infoWidth(cpuSize, wide) : 0.0f;
        float w = wH > wC ? wH : wC;
        float top = infoTop(band, w, wide);
        float bottom = hmnRows > 0 ? top + infoHeight(hmnRows, hmnSize) : top;
        if (cpuRows > 0)
        {
            bottom = infoCpuTop(top, bottom) + infoHeight(cpuRows, cpuSize);
        }
        return bottom;
    }

    // Bottom y of active right-hand Lab overlays (Option A GC Controller, Adaptive Plate, Drill Stat Plate).
    inline float rightColumnBottom(const Box& band, int viewerSlots, bool hasCompass, bool hasPsa, bool wide,
                                   bool hasAdaptive = false, int drillPlateRows = 0, bool trainingSidebar = false)
    {
        (void)hasPsa;
        float bottom = trainingSidebar ? trainingSidebarRoom(wide).y1 : 0.0f;
        float sbLeft = trainingSidebar ? trainingSidebarRoom(wide).x0 : 0.0f;
        for (int s = 0; s < viewerSlots; s++)
        {
            Box vb = inputViewerCardBox(s, band, wide, trainingSidebar);
            if ((!trainingSidebar || vb.x1 > sbLeft) && vb.y1 > bottom)
            {
                bottom = vb.y1;
            }
        }
        if (hasCompass && !trainingSidebar)
        {
            Box comp = diCompassBox(band, viewerSlots, wide);
            if (comp.y1 > bottom)
            {
                bottom = comp.y1;
            }
        }
        if (hasAdaptive)
        {
            Box adap = adaptiveHudBox(band, viewerSlots, hasCompass, false, wide, 0.0f, trainingSidebar);
            if ((!trainingSidebar || adap.x1 > sbLeft) && adap.y1 > bottom)
            {
                bottom = adap.y1;
            }
        }
        if (drillPlateRows > 0)
        {
            Box plate = drillStatPlateBox(drillPlateRows, bottom, wide, trainingSidebar);
            if ((!trainingSidebar || plate.x1 > sbLeft) && plate.y1 > bottom)
            {
                bottom = plate.y1;
            }
        }
        return bottom;
    }

    // ---- Techchase Scenario HUD banner (extra/scenario_presets.cpp) ----
    // Unified onto the same centered top-band slot as drillHudBox(wide)
    // (316*xs x 40 at y = 24), cleanly inside the 5% CRT safe area on 4:3 and 16:9.
    static const float kTechchaseHudWidth = kDrillHudWidth;
    static const float kTechchaseHudHeight = kDrillHudHeight;

    inline Box techchaseHudBox(const Box& band, int viewerSlots, bool hasCompass, bool hasPsa, bool wide,
                               bool hasAdaptive = false)
    {
        (void)band;
        (void)viewerSlots;
        (void)hasCompass;
        (void)hasPsa;
        (void)hasAdaptive;
        return drillHudBox(wide);
    }

    // Starting top y for TopLeft / TopRight OSD stacks so they step cleanly
    // below the active top band and any same-side overlay column while keeping
    // P1's row inside the 5% CRT safe area above Brawl's damage HUD (y >= 397).
    inline float osdTopStartY(bool isTopRight, const Box& band, float infoBottom, float rightBottom)
    {
        float y = kSafeMarginY;
        if (!empty(band) && band.y1 + kStepGap > y)
        {
            y = band.y1 + kStepGap;
        }
        float sideBottom = isTopRight ? rightBottom : infoBottom;
        if (sideBottom > 0.0f && sideBottom + kStepGap > y)
        {
            y = sideBottom + kStepGap;
        }
        // Leave room for one 46 px OSD row cleanly above Brawl's damage row (y 397).
        const float kMaxTopStart = 345.0f;
        if (y > kMaxTopStart)
        {
            y = kMaxTopStart;
        }
        return y;
    }
}
}
}
