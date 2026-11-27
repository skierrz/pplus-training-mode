#include "main/ui/hud_layout.h"

namespace tm {
namespace ui {
namespace layout {
    static Box s_labBand = {0.0f, 0.0f, 0.0f, 0.0f};

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void setLabBand(const Box& band)
    {
        s_labBand = band;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    Box frameBarBox(int cells, bool wide)
    {
        float xs = xsOf(wide);
        float barScale = wide ? 0.90f : 1.0f;
        float cell = kCellW;
        if (cells * (kCellW + kCellGap) + kCellGap > kBarMaxW)
        {
            cell = (kBarMaxW - kCellGap) / cells - kCellGap;
        }
        float total = cells * (cell + kCellGap) * barScale + kCellGap * barScale;
        float x0 = 320.0f - total * 0.5f;
        float x1 = 320.0f + total * 0.5f;
        float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
        if (x1 > maxRight)
        {
            float shift = x1 - maxRight;
            x0 -= shift;
            x1 -= shift;
        }
        if (x0 < kSafeMarginX)
        {
            x0 = kSafeMarginX;
        }
        Box b = {x0, kBarTop, x1, kBarBottom};
        return b;
    }

    Box ledgeHeaderBox(bool wide)
    {
        float xs = xsOf(wide);
        float barScale = wide ? 0.90f : 1.0f;
        float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
        float half = 206.0f * barScale;
        float x0 = 320.0f - half;
        float x1 = 320.0f + half;
        if (x1 > maxRight)
        {
            float shift = x1 - maxRight;
            x0 -= shift;
            x1 -= shift;
        }
        if (x0 < kSafeMarginX)
        {
            x0 = kSafeMarginX;
        }
        Box b = {x0, kKeyY, x1, kBarTop - 2.0f};
        return b;
    }

    Box ledgeLegendBox(bool wide)
    {
        float xs = xsOf(wide);
        float barScale = wide ? 0.90f : 1.0f;
        float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
        float half = 206.0f * barScale;
        float x0 = 320.0f - half;
        float x1 = 320.0f + half;
        if (x1 > maxRight)
        {
            float shift = x1 - maxRight;
            x0 -= shift;
            x1 -= shift;
        }
        if (x0 < kSafeMarginX)
        {
            x0 = kSafeMarginX;
        }
        Box b = {x0, kBarBottom + 1.0f, x1, kBarBottom + 9.0f};
        return b;
    }

    Box timerRoom(bool wide)
    {
        Box b16 = {275.0f, 40.0f, 367.0f, 65.0f};
        Box b43 = {258.0f, 40.0f, 380.0f, 65.0f};
        return wide ? b16 : b43;
    }

    Box counterRoom(bool wide)
    {
        Box b16 = {106.0f, 47.0f, 183.0f, 77.0f};
        Box b43 = {31.0f, 47.0f, 130.0f, 77.0f};
        return wide ? b16 : b43;
    }

    Box unite(const Box& a, const Box& b)
    {
        if (empty(a))
        {
            return b;
        }
        if (empty(b))
        {
            return a;
        }
        Box u = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                 a.y1 > b.y1 ? a.y1 : b.y1};
        return u;
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    float infoTop(const Box& band, float width, bool wide)
    {
        Box at = {infoLeft(wide), kInfoTop, infoLeft(wide) + width, kInfoTop + 1.0f};
        return meetsX(at, band) && band.y1 > kInfoTop ? band.y1 + kStepGap : kInfoTop;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    Box labBand(bool timer, int ledgeCells, bool wide)
    {
        Box band = timer ? timerRoom(wide) : none();
        if (ledgeCells > 0)
        {
            band = unite(band, unite(frameBarBox(ledgeCells, wide),
                                     unite(ledgeHeaderBox(wide), ledgeLegendBox(wide))));
        }
        return band;
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    Box topBand()
    {
        return s_labBand;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif
}
}
}
