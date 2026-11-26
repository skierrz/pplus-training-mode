#include "main/ui/style.h"

namespace tm {
namespace ui {
namespace style {
    void drawBevelBar(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol,
                      gfx::Rgba sheenCol, gfx::Rgba edgeCol)
    {
        gfx::fillGradV(x, y, width, height, topCol, botCol);
        gfx::fillRect(x, y, width, 2.0f, sheenCol);
        gfx::fillRect(x, y + height - 2.0f, width, 2.0f, edgeCol);
    }

    void drawCursorTri(float x, float cy, float size)
    {
        float xs = gfx::xScale();
        float half = size * 0.58f;
        float sw = size * xs;
        float pad = 1.2f * xs;
        gfx::fillTri(x - pad, cy - half - 1.5f, x - pad, cy + half + 1.5f, x + sw + 1.8f * xs, cy, 0x000000FF);
        gfx::fillTri(x, cy - half, x, cy + half, x + sw, cy, 0xEE2218FF);
        gfx::fillTri(x + 0.8f * xs, cy - half * 0.72f, x + 0.8f * xs, cy - 0.5f, x + sw * 0.68f, cy - 0.8f,
                     0xFF7A66FF);
    }

    void fillStadium(float x, float y, float width, float height, gfx::Rgba color)
    {
        float ry = height * 0.5f;
        float rx = ry * gfx::xScale();
        float cy = y + ry;
        if (width <= 2.0f * rx + 0.5f)
        {
            gfx::fillEllipse(x + width * 0.5f, cy, width * 0.5f, ry, color);
            return;
        }
        gfx::fillEllipse(x + rx, cy, rx, ry, color);
        gfx::fillEllipse(x + width - rx, cy, rx, ry, color);
        gfx::fillRect(x + rx, y, width - 2.0f * rx, height, color);
    }

    void drawGlossyPillBadge(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol)
    {
        float xs = gfx::xScale();
        float bx = 1.2f * xs;
        fillStadium(x - bx, y - 1.0f, width + 2.0f * bx, height + 2.0f, 0x05080CFF);
        fillStadium(x, y, width, height, botCol);
        fillStadium(x + 0.8f * xs, y + 0.5f, width - 1.6f * xs, height * 0.72f, topCol);
        fillStadium(x + width * 0.22f, y + height * 0.14f, width * 0.56f, height * 0.22f, 0xFFFFFF70);
    }

    void drawMenuPanel(bool withFooter)
    {
        float left = cx(kPanelLeft);
        float right = cx(kPanelRight);
        float bx = w(kBorderX);
        float width = right - left;
        float innerH = kPanelBottom - kPanelTop - 2.0f * kBorderY;
        gfx::fillRect(left, kPanelTop, width, kBorderY, kBorder);
        gfx::fillRect(left, kPanelBottom - kBorderY, width, kBorderY, kBorder);
        gfx::fillRect(left, kPanelTop + kBorderY, bx, innerH, kBorder);
        gfx::fillRect(right - bx, kPanelTop + kBorderY, bx, innerH, kBorder);
        float x0 = left + bx;
        float iw = width - 2.0f * bx;
        float top = kPanelTop + kBorderY;
        float bottom = kPanelBottom - kBorderY;
        gfx::fillRect(x0, top, iw, kTitleBandBottom - top, kTitleBand);
        if (withFooter)
        {
            gfx::fillRect(x0, kTitleBandBottom, iw, kRowsBandBottom - kTitleBandBottom, kRowsBand);
            gfx::fillRect(x0, kRowsBandBottom, iw, bottom - kRowsBandBottom, kFooterBand);
        }
        else
        {
            gfx::fillRect(x0, kTitleBandBottom, iw, bottom - kTitleBandBottom, kRowsBand);
        }
    }

    void drawScrollBar(int count, int scroll, int visible, float height)
    {
        if (count <= visible)
        {
            return;
        }
        float left = cx(kScrollLeft);
        float width = w(kScrollW);
        gfx::fillRect(left, kScrollTop, width, height, kLabDivider);
        float inner = height - 4.0f;
        float length = inner * visible / count;
        if (length < 14.0f)
        {
            length = 14.0f;
        }
        float top = kScrollTop + 2.0f + (inner - length) * scroll / (count - visible);
        gfx::fillGradV(left + w(1.0f), top, width - w(2.0f), length, kLabTealTop, kLabTealBot);
    }
}
}
}
