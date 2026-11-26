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
        float cell = kCellW;
        if (cells * (kCellW + kCellGap) + kCellGap > kBarMaxW)
        {
            cell = (kBarMaxW - kCellGap) / cells - kCellGap;
        }
        float total = cells * (cell + kCellGap) * xsOf(wide) + kCellGap * xsOf(wide);
        Box b = {320.0f - total * 0.5f, kBarTop, 320.0f + total * 0.5f, kBarBottom};
        return b;
    }

    Box ledgeHeaderBox(bool wide)
    {
        float half = 260.0f * xsOf(wide);
        // Matches drawLedgedashHud(): scale = kKeyCap / 15, textYForCap(kKeyLabelTop, scale)
        // with 2 px vertical rect padding above and below lineHeight(scale).
        float scale = kKeyCap / 15.0f;
        float y = kKeyLabelTop - 5.0f * scale;
        Box b = {320.0f - half, y - 2.0f, 320.0f + half, y + 24.0f * scale + 2.0f};
        return b;
    }

    Box ledgeLegendBox(bool wide)
    {
        float half = 240.0f * xsOf(wide);
        Box b = {320.0f - half, 68.0f, 320.0f + half, 84.0f};
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
