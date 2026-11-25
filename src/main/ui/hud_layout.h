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

    // ---- 4:3 CRT overscan safe area ----
    // 5% margin on each edge (SMPTE RP 27.3 / Nintendo Wii 90% action-safe
    // frame: 32 px on 640, 24 px on 480). Brawl's own 4:3 HUD in
    // info_training.pac stays inside this margin; on 16:9 the same vertical
    // margin applies across the full 640 width.
    static const float kSafeMarginX = 32.0f;
    static const float kSafeMarginY = 24.0f;

    inline Box safeArea(bool wide)
    {
        Box b = {wide ? 0.0f : kSafeMarginX, kSafeMarginY, wide ? 640.0f : 640.0f - kSafeMarginX,
                 480.0f - kSafeMarginY};
        return b;
    }

    // ---- Info Display panels (ui/info_display.cpp) ----
    static const float kInfoLeft = 34.0f;
    static const float kInfoTop = 26.0f;
    static const float kInfoGap = 6.0f;
    static const float kInfoSizeScale[3] = {0.72f, 0.86f, 1.0f};

    inline float infoScale(int size)
    {
        return kInfoSizeScale[size >= 0 && size < 3 ? size : 1];
    }

    inline float infoLeft(bool wide)
    {
        return leftX(kInfoLeft, wide);
    }

    inline float infoWidth(int size, bool wide)
    {
        return 236.0f * infoScale(size) * xsOf(wide);
    }

    inline float infoHeight(int rows, int size)
    {
        return rows > 0 ? (12.0f + rows * 13.0f) * infoScale(size) : 0.0f;
    }

    // Top y of the CPU info panel below the human panel (or at humanTop when
    // the human panel has no rows).
    inline float infoCpuTop(float humanTop, float humanBottom)
    {
        return humanBottom > humanTop ? humanBottom + kInfoGap : humanTop;
    }

    // ---- Ledgedash HUD top band (ui/ledgedash_hud.cpp) ----
    static const float kBarTop = 79.8f;
    static const float kBarBottom = 91.7f;
    // Spans 32..608 at most on 4:3 (inside the 5% CRT safe area).
    static const float kBarMaxW = 576.0f;
    static const float kKeyY = 42.0f;
    static const float kKeyLabelTop = 58.5f;

    static const float kCellW = 10.8f;
    static const float kCellGap = 1.0f;
    static const float kKeySwatch = 11.0f;
    static const float kKeyCap = 8.0f;

    // The Ledgedash HUD's bar of `cells` cells, centred and narrowed to kBarMaxW.
    Box frameBarBox(int cells, bool wide);

    // The Ledgedash HUD's summary strip above the bar.
    Box ledgeHeaderBox(bool wide);

    // ---- Pause-menu hint (lab.cpp) ----
    // textBox pads 3 px above kHintY, so 27.0 puts the box top at y = 24.0.
    static const float kHintX = 520.0f;
    static const float kHintY = 27.0f;

    // ---- Recording HUD and storage notice (record/hud.cpp, storage/notice.cpp) ----
    // Stacked cleanly above the damage row (y >= 397) with clear portrait
    // headroom in both 4:3 and 16:9 inside the 5% CRT safe area (y 24..456).
    static const float kRecMessageY = 320.0f;
    static const float kNoticeY = 347.0f;
    static const float kRecBarCentreY = 375.0f;
    static const float kRecBarHeight = 20.0f;

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
    // Bottom-right inside the 5% CRT safe area.
    static const float kWatermarkRight = 606.0f;
    static const float kWatermarkCapTop = 443.5f;

    // ---- Brawl's native top-band HUD pieces (ui/match_hud.cpp) ----
    Box timerRoom(bool wide);
    Box counterRoom(bool wide);

    // ---- Stepping aside ----
    static const float kStepGap = 6.0f;

    // Highest bottom edge for lower-screen OSD stacks while the recording seek
    // bar is shown: one step gap above the bar's top edge.
    inline float recBarOsdFloor()
    {
        return kRecBarCentreY - kRecBarHeight * 0.5f - kStepGap;
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

    // Top y of the first info panel: kInfoTop, or stepped below `band` when a
    // panel of `width` would overlap it horizontally.
    float infoTop(const Box& band, float width, bool wide);

    // Top band occupied by the lab this frame: match timer room (when shown)
    // united with the Ledgedash HUD bar and header (when `ledgeCells > 0`).
    Box labBand(bool timer, int ledgeCells, bool wide);

    // ---- Live Skill Drill HUD & End-Screen Modal (extra/drill_hud.cpp) ----
    // Centered top-band header bar strictly inside the 5% CRT safe area on
    // both 4:3 and 16:9. Width 296 clears 4:3 Input Viewer (470..608) and
    // 16:9 Large Info Display (25.5..202.5) without stepping them down, and
    // height 40 (y 24..64) stays above Brawl's timer bottom (65).
    static const float kDrillHudWidth = 296.0f;
    static const float kDrillHudHeight = 40.0f;
    static const float kDrillHudTop = 24.0f;

    inline Box drillHudBox(bool wide)
    {
        float w = kDrillHudWidth * xsOf(wide);
        return box(320.0f - w * 0.5f, kDrillHudTop, w, kDrillHudHeight);
    }

    // Centered End-Screen Summary Modal (5% CRT safe on 4:3 and 16:9).
    static const float kDrillModalWidth = 384.0f;
    static const float kDrillModalTop = 104.0f;
    static const float kDrillModalHeight = 272.0f;

    inline Box drillModalBox(bool wide)
    {
        float w = kDrillModalWidth * xsOf(wide);
        return box(320.0f - w * 0.5f, kDrillModalTop, w, kDrillModalHeight);
    }

    void setLabBand(const Box& band);
    Box topBand();

    // ---- Multi-controller Input Viewer (extra/input_viewer.cpp) ----
    // Right-aligned column strictly inside the 5% CRT safe area (x <= 608,
    // y >= 24) on both 4:3 and 16:9, stepping below `band` when the Ledgedash
    // HUD is active.
    static const float kViewerRight = 608.0f;
    static const float kViewerTop = 26.0f;
    static const float kViewerWidth = 138.0f;
    static const float kViewerHeight = 54.0f;
    static const float kViewerGap = 4.0f;

    inline int inputViewerSlotCount(u8 who)
    {
        if (who == 0)
        {
            return 0;
        }
        return who == 3 ? 2 : 1;
    }

    inline Box inputViewerCardBox(int slot, const Box& band, bool wide)
    {
        float w = kViewerWidth * xsOf(wide);
        float x1 = kViewerRight;
        float x0 = x1 - w;
        Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
        float top = (meetsX(probe, band) && band.y1 > kViewerTop) ? (band.y1 + kStepGap) : kViewerTop;
        float y0 = top + static_cast<float>(slot > 0 ? slot : 0) * (kViewerHeight + kViewerGap);
        return box(x0, y0, w, kViewerHeight);
    }

    // ---- Live DI Vector Picker 2D unit-circle compass (extra/di_display.cpp) ----
    // Right-aligned below any active Input Viewer cards (or below `band`).
    static const float kCompassRight = 608.0f;
    static const float kCompassWidth = 82.0f;
    static const float kCompassHeight = 78.0f;

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

    // ---- PSA AnimCmd 11-Thread & FSM Inspector (extra/psa_inspector.cpp) ----
    // Right-aligned at kViewerRight (608) when Input Viewer and DI Compass are
    // off, or shifted left of the active right column with a kStepGap gutter so
    // it clears Large Info Display (x1 <= 270) and the right-hand overlays in
    // both 4:3 and 16:9.
    static const float kPsaWidth = 182.0f;
    static const float kPsaHeight = 104.0f;

    inline Box psaInspectorBox(const Box& band, int viewerSlots, bool hasCompass, bool wide)
    {
        float xs = xsOf(wide);
        float w = kPsaWidth * xs;
        float x1 = kViewerRight;
        if (viewerSlots > 0 || hasCompass)
        {
            float rightW = viewerSlots > 0 ? (kViewerWidth * xs) : (kCompassWidth * xs);
            x1 = kViewerRight - rightW - kStepGap;
        }
        float x0 = x1 - w;
        Box probe = {x0, kViewerTop, x1, kViewerTop + 1.0f};
        float y0 = (meetsX(probe, band) && band.y1 > kViewerTop) ? (band.y1 + kStepGap) : kViewerTop;
        return box(x0, y0, w, kPsaHeight);
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

    // Bottom y of active right-hand Lab overlays (Input Viewer, DI Compass, PSA card).
    inline float rightColumnBottom(const Box& band, int viewerSlots, bool hasCompass, bool hasPsa, bool wide)
    {
        float bottom = 0.0f;
        if (viewerSlots > 0)
        {
            Box last = inputViewerCardBox(viewerSlots - 1, band, wide);
            if (last.y1 > bottom)
            {
                bottom = last.y1;
            }
        }
        if (hasCompass)
        {
            Box comp = diCompassBox(band, viewerSlots, wide);
            if (comp.y1 > bottom)
            {
                bottom = comp.y1;
            }
        }
        if (hasPsa)
        {
            Box psa = psaInspectorBox(band, viewerSlots, hasCompass, wide);
            if (psa.y1 > bottom)
            {
                bottom = psa.y1;
            }
        }
        return bottom;
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
