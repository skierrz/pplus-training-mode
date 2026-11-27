#include "main/ui/style.h"

#ifdef __MWERKS__
#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#endif

namespace tm {
namespace ui {
namespace style {
    const gfx::Rgba kBtnTheme[4][3] = {
        {kLabBtnATop, kLabBtnABot, kLabBtnDarkText},
        {kLabBtnBTop, kLabBtnBBot, kLabWhiteText},
        {kLabBtnLightTop, kLabBtnLightBot, kLabBtnDarkText},
        {kLabBtnZTop, kLabBtnZBot, kLabWhiteText}
    };

    static inline float mapX(float x43, float ms)
    {
        return 320.0f + (x43 - 320.0f) * ms;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
#ifndef TM_MODULE_MENU
    void drawBevelBar(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol,
                      gfx::Rgba sheenCol, gfx::Rgba edgeCol)
    {
        gfx::fillGradV(x, y, width, height, topCol, botCol);
        gfx::fillRect(x, y, width, 2.0f, sheenCol);
        gfx::fillRect(x, y + height - 2.0f, width, 2.0f, edgeCol);
    }
#endif

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
        float ms = menuScale();
        float slant = height * kStyleSlantTan;
        float x0Top = mapX(flushLeft ? x043 : (x043 + slant), ms);
        float x0Bot = mapX(x043, ms);
        float x1Top = mapX(x143, ms);
        float x1Bot = mapX(x143 - slant, ms);
        if (edgeCol != 0)
        {
            drawSlantGradV(x0Top, x0Bot, x1Top, x1Bot, y, y + height, edgeCol, edgeCol);
            float padX = 2.0f * ms;
            float dySlant = (2.0f * kStyleSlantTan) * ms;
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
        float ms = menuScale();
        float left = mapX(left43, ms);
        float panelW = (right43 - left43) * ms;
        float bx = border * ms;
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

    void drawGoldBar(float x043, float x143, float y, float h, float cursorSize, bool flushLeft, float stripeRight43)
    {
        float ms = menuScale();
        float slant = h * kStyleSlantTan;
        drawSlantBar(x043, x143, y, h, flushLeft, kLabGoldTop, kLabGoldBot, kLabGoldEdge);
        if (stripeRight43 > 0.0f)
        {
            float inH = h - 4.0f;
            drawDiagStripes(mapX(stripeRight43, ms), y + 2.0f, y + h - 2.0f, (inH * kStyleSlantTan) * ms, 4.0f * ms,
                            3.0f * ms, kLabStripeCol);
        }
        float triX43 = flushLeft ? (x043 + 4.0f) : (x043 + slant * 0.55f + 2.5f);
        drawCursorTri(mapX(triX43 + 1.0f, ms), y + h * 0.5f, cursorSize);
    }

    void drawHeaderRibbon(float inL43, float right43, float topY, float height, gfx::Rgba topCol, gfx::Rgba botCol)
    {
        float ms = menuScale();
        drawSlantBar(inL43, right43, topY, height, true, topCol, botCol, 0);
        drawDiagStripes(mapX(right43 - 6.0f, ms), topY, topY + height, (height * kStyleSlantTan) * ms, 4.5f * ms,
                        3.5f * ms, kLabStripeCol);
    }

    void drawModalFrame(const ModalFrame& f, const char* title, float dy, float alpha)
    {
        if (f.backdrop != 0)
        {
            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(f.backdrop, alpha));
        }
        float topY = f.topY + dy;
        float botY = f.botY + dy;
        float hdrBot = f.hdrBot + dy;
        float descTop = f.descTop > 0.0f ? (f.descTop + dy) : 0.0f;
        float footTop = f.footTop + dy;
        drawFrameBorder(f.left43, f.right43, topY, botY, kLabBorder, kLabBorderColor);
        float ms = menuScale();
        float inL43 = f.left43 + kLabBorder;
        float inTop = topY + kLabBorder;
        float inBot = botY - kLabBorder;
        float bodyX = mapX(inL43, ms);
        float bodyW = (f.right43 - f.left43 - 2.0f * kLabBorder) * ms;
        float bodyTop = hdrBot + kFibHair;
        float bodyEnd = descTop > 0.0f ? descTop : footTop;

        gfx::fillGradV(bodyX, inTop, bodyW, hdrBot - inTop, kLabHeaderTop, kLabHeaderBot);
        gfx::fillRect(bodyX, hdrBot, bodyW, kFibHair, kLabDivider);
        gfx::fillGradV(bodyX, bodyTop, bodyW, bodyEnd - bodyTop, kLabBaseTop, kLabBaseBot);
#if defined(TM_MODULE_MENU) || !defined(__MWERKS__)
        // Menu modals supply descTop > 0.0f; MAIN modals currently pass descTop = 0.0f.
        if (descTop > 0.0f)
        {
            gfx::fillRect(bodyX, descTop, bodyW, kFibHair, kLabDivider);
            gfx::fillGradV(bodyX, descTop + kFibHair, bodyW, footTop - descTop - kFibHair, kLabDescTopCol,
                           kLabDescBotCol);
        }
#endif
        gfx::fillRect(bodyX, footTop, bodyW, kFibHair, kLabDivider);
        gfx::fillGradV(bodyX, footTop + kFibHair, bodyW, inBot - footTop - kFibHair, kLabFooterTopCol,
                       kLabFooterBotCol);
        if (f.ribbonRight43 > 0.0f)
        {
            drawHeaderRibbon(inL43, f.ribbonRight43, inTop, hdrBot - inTop, kLabTealTop, kLabTealBot);
        }
        if (title != NULL)
        {
            float titleScale = displayScaleForCap(kTypeHeading);
            float hdrMid = (inTop + hdrBot) * 0.5f;
            gfx::displayText(mapX(inL43 + kFibPad, ms), displayYForCap(hdrMid - kTypeHeading * 0.5f, titleScale),
                             title, kLabActiveTabText, kLabActiveTabText, titleScale, titleScale, 0.0f, 0);
        }
        gfx::fillRect(bodyX, inBot - kFibHair, bodyW, kFibHair, kLabBevelDark);
    }

    void drawValueArrows(float pillLeft43, float pillRight43, float rowY, float rowH, gfx::Rgba col)
    {
        float ms = menuScale();
        float xs = gfx::xScale();
        float midY = rowY + rowH * 0.5f;
        float midSlant = rowH * kStyleSlantTan * 0.5f;
        float l = mapX(pillLeft43 + midSlant + 4.0f, ms);
        float r = mapX(pillRight43 - midSlant - 4.0f, ms);
        float dx2 = 2.0f * xs;
        float dx8 = 8.0f * xs;
        gfx::fillTri(l + dx2, midY, l + dx8, midY - 4.0f, l + dx8, midY + 4.0f, col);
        gfx::fillTri(r - dx2, midY, r - dx8, midY - 4.0f, r - dx8, midY + 4.0f, col);
    }

    float drawFooterBadge(float x, float cy, float badgeH, const char* glyph, const char* label, u8 theme, u8 flags,
                          gfx::Rgba labelCol)
    {
        float xs = gfx::xScale();
        bool dense = (flags & kBadgeDense) != 0;
        float cap = dense ? kTypeMicro : kTypeMeta;
        float scale = scaleForCap(cap);
        float glyphW = gfx::textWidth(glyph, scale);
        bool round = (flags & kBadgeRound) != 0;
        float pillPad = (dense ? 6.0f : 10.0f) * xs;
        float bw = round ? (badgeH * xs) : (glyphW + pillPad);
        float minW = (round ? badgeH : (badgeH + 3.0f)) * xs;
        if (bw < minW)
        {
            bw = minW;
        }
        const gfx::Rgba* tc = kBtnTheme[theme < 4 ? theme : 0];
        drawGlossyPillBadge(x, cy - badgeH * 0.5f, bw, badgeH, tc[0], tc[1]);
        float textY = textYForCap(cy - cap * 0.5f, scale);
        gfx::text(x + (bw - glyphW) * 0.5f, textY, glyph, tc[2], scale);
        float gap = (dense ? 3.5f : ((flags & kBadgeTightGap) ? kFibTight : kFibGap)) * xs;
        float pad = (dense ? 5.5f : ((flags & kBadgeWidePad) ? (kFibPad + kFibGap) : kFibPad)) * xs;
        float endX = gfx::text(x + bw + gap, textY, label, labelCol, scale);
        return endX + pad;
    }

    void drawFitText(float x, float y, const char* str, gfx::Rgba color, float scale, float maxW, bool centre)
    {
        float width = gfx::textWidth(str, scale);
        float sx = scale;
        if (width > maxW && width > 0.0f)
        {
            sx = scale * maxW / width;
            width = maxW;
        }
        gfx::text(centre ? x - width * 0.5f : x, y, str, color, sx, scale);
    }

    void drawScrollBarAt(float left43, float topY, float height, int count, int scroll, int visible,
                         gfx::Rgba trackCol, gfx::Rgba thumbTop, gfx::Rgba thumbBot)
    {
        if (count <= visible)
        {
            return;
        }
        float ms = menuScale();
        float left = mapX(left43, ms);
        float width = kScrollW * ms;
        gfx::fillRect(left, topY, width, height, trackCol);
        float inner = height - 4.0f;
        float length = inner * visible / count;
        if (length < kFibRowH)
        {
            length = kFibRowH;
        }
        int maxScroll = count - visible;
        if (scroll > maxScroll)
        {
            scroll = maxScroll;
        }
        float thumbY = topY + 2.0f + (inner - length) * scroll / maxScroll;
        gfx::fillGradV(left + ms, thumbY, width - 2.0f * ms, length, thumbTop, thumbBot);
    }

#if defined(TM_MODULE_MENU) || !defined(__MWERKS__)
    void drawScrollBar(int count, int scroll, int visible, float height)
    {
        drawScrollBarAt(kScrollLeft, kScrollTop, height, count, scroll, visible, kLabDivider, kLabTealTop, kLabTealBot);
    }
#endif

    float easeOutCubic(float t)
    {
        float u = t <= 0.0f ? 1.0f : (t >= 1.0f ? 0.0f : 1.0f - t);
        return 1.0f - u * u * u;
    }

    float easeOutQuart(float t)
    {
        float u = t <= 0.0f ? 1.0f : (t >= 1.0f ? 0.0f : 1.0f - t);
        float u2 = u * u;
        return 1.0f - u2 * u2;
    }

    float chase60(float cur, float target, float alpha)
    {
        float d = target - cur;
        return (d > -0.25f && d < 0.25f) ? target : (cur + d * alpha);
    }

    float snapPx(float v)
    {
        return static_cast<float>(static_cast<int>(v + 1024.5f) - 1024);
    }

    gfx::Rgba scaleAlpha(gfx::Rgba c, float a)
    {
        int m = static_cast<int>(a * 256.0f + 0.5f);
        if (m >= 256)
        {
            return c;
        }
        return (c & 0xFFFFFF00u) | (m <= 0 ? 0u : (((c & 0xFFu) * static_cast<u32>(m)) >> 8));
    }

    float modalAnimStep(ModalAnim& a, float& alphaOut)
    {
        if (a.enter > 0)
        {
            float t = easeOutCubic(static_cast<float>(9 - a.enter--) / 8.0f);
            alphaOut = 0.25f + 0.75f * t;
            return static_cast<float>(-static_cast<int>((1.0f - t) * 6.0f + 0.5f));
        }
        if (a.exit > 0)
        {
            float t = easeOutQuart(static_cast<float>(5 - a.exit--) / 4.0f);
            alphaOut = 1.0f - t;
            return static_cast<float>(static_cast<int>(t * 4.0f + 0.5f));
        }
        alphaOut = 1.0f;
        return 0.0f;
    }

    float modalAnimCursor(ModalAnim& a, float targetY)
    {
        a.cursorY = a.hasCursor ? chase60(a.cursorY, targetY, 0.44f) : targetY;
        a.hasCursor = 1;
        return a.cursorY;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
}
}
}
