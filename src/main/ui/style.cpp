#include "main/ui/style.h"

#ifdef __MWERKS__
#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#endif

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

    void drawSlantGradV(float x0Top, float x0Bot, float x1Top, float x1Bot, float y0, float y1, gfx::Rgba topCol,
                        gfx::Rgba botCol)
    {
#ifdef __MWERKS__
        gfx::prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        gfx::vertex(x0Top, y0, topCol);
        gfx::vertex(x1Top, y0, topCol);
        gfx::vertex(x1Bot, y1, botCol);
        gfx::vertex(x0Bot, y1, botCol);
#else
        float minX = x0Top < x0Bot ? x0Top : x0Bot;
        float maxX = x1Top > x1Bot ? x1Top : x1Bot;
        gfx::fillGradV(minX, y0, maxX - minX, y1 - y0, topCol, botCol);
#endif
    }

    static const float kStyleSlantTan = 0.445f; // ~24 deg stock P+ slant

    void drawSlantBar(float x043, float x143, float y, float height, bool flushLeft, gfx::Rgba topCol,
                      gfx::Rgba botCol, gfx::Rgba edgeCol)
    {
        float slant = height * kStyleSlantTan;
        float x0Top = cx(flushLeft ? x043 : (x043 + slant));
        float x0Bot = cx(x043);
        float x1Top = cx(x143);
        float x1Bot = cx(x143 - slant);
        if (edgeCol != 0)
        {
            drawSlantGradV(x0Top, x0Bot, x1Top, x1Bot, y, y + height, edgeCol, edgeCol);
            float padX = w(2.0f);
            float dySlant = w(2.0f * kStyleSlantTan);
            float ix0Top = flushLeft ? x0Top : (x0Top - dySlant + padX);
            float ix0Bot = flushLeft ? x0Bot : (x0Bot + dySlant + padX);
            float ix1Top = x1Top - dySlant - padX;
            float ix1Bot = x1Bot + dySlant - padX;
            drawSlantGradV(ix0Top, ix0Bot, ix1Top, ix1Bot, y + 2.0f, y + height - 2.0f, topCol, botCol);
        }
        else
        {
            drawSlantGradV(x0Top, x0Bot, x1Top, x1Bot, y, y + height, topCol, botCol);
        }
    }

    void drawValuePill(float left43, float right43, float y, float rowH, bool isOn, bool on, bool selected)
    {
        gfx::Rgba topCol, botCol;
        if (isOn)
        {
            topCol = on ? kLabPillOnTop : kLabPillOnMutedTop;
            botCol = on ? kLabPillOnBot : kLabPillOnMutedBot;
        }
        else if (selected && on)
        {
            topCol = kLabPillFocusTop;
            botCol = kLabPillFocusBot;
        }
        else
        {
            topCol = on ? kLabPillOffTop : kLabPillOffMutedTop;
            botCol = on ? kLabPillOffBot : kLabPillOffMutedBot;
        }
        // Seamless flush-height compartment sharing the exact 2 px dark border
        // and slant of drawSlantBar (zero yellow overhang, zero vertical spill).
        drawSlantBar(left43, right43, y, rowH, false, topCol, botCol, kLabPillBorder);
    }

    void drawDiagStripes(float rightTopX, float y0, float y1, float slantPx, float stripeW, float gap,
                         gfx::Rgba color)
    {
        float x1 = rightTopX;
        float x0 = x1 - stripeW;
        drawSlantGradV(x0, x0 - slantPx, x1, x1 - slantPx, y0, y1, color, color);
        x1 = x0 - gap;
        x0 = x1 - stripeW;
        drawSlantGradV(x0, x0 - slantPx, x1, x1 - slantPx, y0, y1, color, color);
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

    void drawFrameBorder(float left43, float right43, float topY, float botY, float border, gfx::Rgba color)
    {
        float left = cx(left43);
        float panelW = w(right43 - left43);
        float bx = w(border);
        float inTop = topY + border;
        float inH = botY - topY - 2.0f * border;
        const float r[4][4] = {
            {left, topY, panelW, border},
            {left, botY - border, panelW, border},
            {left, inTop, bx, inH},
            {left + panelW - bx, inTop, bx, inH}
        };
        for (int i = 0; i < 4; i++)
        {
            gfx::fillRect(r[i][0], r[i][1], r[i][2], r[i][3], color);
        }
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
        if (length < kFibRowH)
        {
            length = kFibRowH;
        }
        float top = kScrollTop + 2.0f + (inner - length) * scroll / (count - visible);
        gfx::fillGradV(left + w(1.0f), top, width - w(2.0f), length, kLabTealTop, kLabTealBot);
    }
}
}
}
