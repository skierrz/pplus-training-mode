#include "extra/hud_draw.h"

#include <GX/GXTypes.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "extra/combo_break_rush.h"
#include "extra/di_cstick_core.h"
#include "extra/drill_hud.h"
#include "extra/drill_practice_driver.h"
#include "extra/net_alloc.h"
#include "extra/scenario_presets.h"
#include "main/cpu/cpu_settings.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/game/match.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/match_hud.h"
#include "main/ui/pages/scenarios.h"
#include "main/ui/style.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"

namespace tm {
namespace extra {
    static const float kCosQ[5] = {1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f};

    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale)
    {
        gfx::text(x + 0.8f, y + 0.8f, text, 0x000000C8u, scale);
        gfx::text(x, y, text, color, scale);
    }

    void drawShadowCapText(float x, float capY, const char* text, gfx::Rgba color, float cap, bool rightAlign)
    {
        float sc = ui::style::scaleForCap(cap);
        float dx = rightAlign ? (x - gfx::textWidth(text, sc)) : x;
        drawShadowText(dx, ui::style::textYForCap(capY, sc), text, color, sc);
    }

    void drawShadowCapCenter(float cx, float capY, const char* text, gfx::Rgba color, float cap, float maxW)
    {
        float sc = ui::style::scaleForCap(cap);
        float tw = gfx::textWidth(text, sc);
        float sx = sc;
        if (maxW > 0.0f && tw > maxW && tw > 0.0f)
        {
            sx = sc * maxW / tw;
            tw = maxW;
        }
        float dx = cx - tw * 0.5f;
        float dy = ui::style::textYForCap(capY, sc);
        gfx::text(dx + gfx::xScale(), dy + 1.0f, text, 0x000000D0u, sx, sc);
        gfx::text(dx, dy, text, color, sx, sc);
    }

    void drawSmokedPlate(float x, float y, float w, float h, gfx::Rgba bg)
    {
        if (w > 0.0f && h > 0.0f)
        {
            gfx::fillRect(x, y, w, h, bg);
            if (h > 4.0f)
            {
                gfx::fillRect(x, y, w, 2.0f, kSmokedPlateSheen);
            }
        }
    }

    void drawSmokedCard(float x, float y, float w, float h, float xs, gfx::Rgba bg)
    {
        if (w <= 0.0f || h <= 0.0f)
        {
            return;
        }
        float ex = 2.0f * xs;
        float ey = 2.0f;
        gfx::fillRect(x, y, w, h, kSmokedPlateEdge);
        if (w > 2.0f * ex && h > 2.0f * ey)
        {
            gfx::fillRect(x + ex, y + ey, w - 2.0f * ex, h - 2.0f * ey, bg);
            if (h > 6.0f)
            {
                gfx::fillRect(x + ex, y + ey, w - 2.0f * ex, 2.0f, kSmokedPlateSheen);
            }
        }
    }

    void drawPlateDivider(float x, float y, float w, float xs)
    {
        float inset = 8.0f * xs;
        if (w > 2.0f * inset)
        {
            gfx::fillRect(x + inset, y, w - 2.0f * inset, 2.0f, kSmokedDivider);
        }
    }

    void drawOutlinedText(float x, float y, const char* text, gfx::Rgba color, float sx, float sy)
    {
        float dx = gfx::xScale();
        const gfx::Rgba edge = kHudOutlineEdge;
        gfx::text(x - dx, y, text, edge, sx, sy);
        gfx::text(x + dx, y, text, edge, sx, sy);
        gfx::text(x, y - 1.0f, text, edge, sx, sy);
        gfx::text(x, y + 1.0f, text, edge, sx, sy);
        gfx::text(x, y, text, color, sx, sy);
    }

    ui::layout::Box drawTimingBar(const gfx::Rgba* cells, int count, int pointerCell, const TimingLegendItem* legend,
                                  int legendCount, const char* caption, bool wide, const char* leftLabel,
                                  const char* rightLabel, const TimingBarFx* fx)
    {
        using namespace ui::layout;
        float xs = xsOf(wide);
        float barScale = wide ? 0.90f : 1.0f;
        int n = count > 0 ? count : 1;
        Box bar = frameBarBox(n, wide);
        float bw = bar.x1 - bar.x0;
        float bh = bar.y1 - bar.y0;
        float barCx = (bar.x0 + bar.x1) * 0.5f;
        float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
        bool repStrip = (fx != NULL && fx->isRepStrip);

        // Precompute header / legend layout so all quads (scrim pills, swatches, cells, pointer)
        // batch cleanly before any text draws.
        float capSize = kKeyCap;
        float sc = ui::style::scaleForCap(capSize);
        float ty = ui::style::textYForCap(kKeyLabelTop, sc);
        bool has3Zone = (leftLabel != NULL && leftLabel[0] != '\0') || (rightLabel != NULL && rightLabel[0] != '\0');
        bool hasRepHdr = !has3Zone && repStrip && fx->repTag != NULL && fx->repTag[0] != '\0'
                         && caption != NULL && caption[0] != '\0';

        int lCount = (!has3Zone && !hasRepHdr && legend != NULL) ? (legendCount < 8 ? legendCount : 8) : 0;
        float swW = kKeySwatch * xs;
        float swGap = 4.0f * xs;
        float itemGap = 9.0f * xs;
        float widths[8];
        float hdrSx = sc;
        float hdrRatio = 1.0f;
        float lw = 0.0f, cw = 0.0f, rw = 0.0f;
        float lx0 = bar.x0;
        float drawTotalW = bw;
        float pillX0 = bar.x0;
        float pillX1 = bar.x1;
        float pillY0 = kKeyY;
        float pillH = kBarTop - 2.0f - kKeyY; // 16 px (y 25..41)
        bool hasHeader = false;

        if (has3Zone)
        {
            hasHeader = true;
            lw = (leftLabel != NULL && leftLabel[0] != '\0') ? gfx::textWidth(leftLabel, sc) : 0.0f;
            cw = (caption != NULL && caption[0] != '\0') ? gfx::textWidth(caption, sc) : 0.0f;
            rw = (rightLabel != NULL && rightLabel[0] != '\0') ? gfx::textWidth(rightLabel, sc) : 0.0f;
            float padX = 6.0f * xs;
            float innerW = bw - 2.0f * padX;
            float minGap = 8.0f * xs;
            float need = lw + cw + rw + (lw > 0.0f ? minGap : 0.0f) + (rw > 0.0f ? minGap : 0.0f);
            hdrSx = (need > innerW && need > 0.0f) ? (sc * innerW / need) : sc;
            hdrRatio = sc > 0.0f ? (hdrSx / sc) : 1.0f;
            pillX0 = bar.x0;
            pillX1 = bar.x1;
        }
        else if (hasRepHdr)
        {
            hasHeader = true;
            lw = gfx::textWidth(caption, sc);
            rw = gfx::textWidth(fx->repTag, sc);
            float padX = 6.0f * xs;
            float minGap = 10.0f * xs;
            float need = lw + rw + minGap;
            float pillMinW = bw;
            if (need + 2.0f * padX > pillMinW)
            {
                pillMinW = need + 2.0f * padX;
            }
            float avail = maxRight - kSafeMarginX;
            if (pillMinW > avail)
            {
                pillMinW = avail;
            }
            pillX0 = barCx - pillMinW * 0.5f;
            pillX1 = barCx + pillMinW * 0.5f;
            if (pillX1 > maxRight)
            {
                pillX0 -= (pillX1 - maxRight);
                pillX1 = maxRight;
            }
            if (pillX0 < kSafeMarginX)
            {
                pillX0 = kSafeMarginX;
            }
            float innerW = (pillX1 - pillX0) - 2.0f * padX;
            hdrSx = (need > innerW && need > 0.0f) ? (sc * innerW / need) : sc;
            hdrRatio = sc > 0.0f ? (hdrSx / sc) : 1.0f;
        }
        else if (lCount > 0)
        {
            hasHeader = true;
            float totalW = 0.0f;
            for (int i = 0; i < lCount; i++)
            {
                widths[i] = gfx::textWidth(legend[i].label, sc);
                totalW += (legend[i].swatch != 0u ? (swW + swGap) : 0.0f) + widths[i]
                          + (i + 1 < lCount ? itemGap : 0.0f);
            }
            float padX = 6.0f * xs;
            float availW = (maxRight - kSafeMarginX) - 2.0f * padX;
            hdrSx = (totalW > availW && totalW > 0.0f) ? (sc * availW / totalW) : sc;
            drawTotalW = totalW > availW ? availW : totalW;
            hdrRatio = (sc > 0.0f) ? (hdrSx / sc) : 1.0f;
            lx0 = barCx - drawTotalW * 0.5f;
            if (lx0 + drawTotalW + padX > maxRight)
            {
                lx0 = maxRight - padX - drawTotalW;
            }
            if (lx0 - padX < kSafeMarginX)
            {
                lx0 = kSafeMarginX + padX;
            }
            pillX0 = lx0 - padX;
            pillX1 = lx0 + drawTotalW + padX;
            if (pillX0 > bar.x0) pillX0 = bar.x0;
            if (pillX1 < bar.x1) pillX1 = bar.x1;
        }
        else if (caption != NULL && caption[0] != '\0')
        {
            hasHeader = true;
            cw = gfx::textWidth(caption, sc);
            float padX = 8.0f * xs;
            float maxCapW = bw + 60.0f * xs;
            if (maxCapW > maxRight - kSafeMarginX - 2.0f * padX)
            {
                maxCapW = maxRight - kSafeMarginX - 2.0f * padX;
            }
            hdrSx = (cw > maxCapW && cw > 0.0f) ? (sc * maxCapW / cw) : sc;
            drawTotalW = cw > maxCapW ? maxCapW : cw;
            pillX0 = barCx - drawTotalW * 0.5f - padX;
            pillX1 = barCx + drawTotalW * 0.5f + padX;
            if (pillX0 > bar.x0) pillX0 = bar.x0;
            if (pillX1 < bar.x1) pillX1 = bar.x1;
            if (pillX1 > maxRight)
            {
                pillX0 -= (pillX1 - maxRight);
                pillX1 = maxRight;
            }
            if (pillX0 < kSafeMarginX)
            {
                pillX0 = kSafeMarginX;
            }
        }

        // Cell geometry shared across cells, hit flash, pointer, and end ticks.
        float gap = (repStrip ? 3.0f : kCellGap) * barScale;
        float innerPadX = (repStrip ? 4.0f : 2.0f) * barScale;
        float cellW = (bw - 2.0f * innerPadX - i2f(n - 1) * gap) / i2f(n);
        if (cellW < 1.5f)
        {
            gap = 1.0f * xs;
            cellW = (bw - 2.0f * innerPadX - i2f(n - 1) * gap) / i2f(n);
            if (cellW < 1.2f)
            {
                cellW = 1.2f;
            }
        }
        float cellTop = repStrip ? (bar.y0 + 3.0f) : (bar.y0 + 2.0f);
        float cellH = repStrip ? (bh - 6.0f) : (bh - 4.0f);

        // Determine pointer position (continuous glide when fx->pointerPos >= 0, else integer pointerCell).
        bool drawPtr = false;
        float ptrIdx = -1.0f;
        if (!repStrip)
        {
            if (fx != NULL && fx->pointerPos >= 0.0f)
            {
                ptrIdx = fx->pointerPos;
                if (ptrIdx > i2f(n - 1)) ptrIdx = i2f(n - 1);
                drawPtr = true;
            }
            else if (pointerCell >= 0 && pointerCell < n)
            {
                ptrIdx = i2f(pointerCell);
                drawPtr = true;
            }
        }
        float ptrPx = 0.0f;
        if (drawPtr)
        {
            float halfBase = 4.4f * xs;
            float outPad = 1.4f * xs;
            ptrPx = bar.x0 + innerPadX + ptrIdx * (cellW + gap) + cellW * 0.5f;
            if (ptrPx - halfBase - outPad < bar.x0)
            {
                ptrPx = bar.x0 + halfBase + outPad;
            }
            if (ptrPx + halfBase + outPad > bar.x1)
            {
                ptrPx = bar.x1 - halfBase - outPad;
            }
            ptrPx = i2f(f2i(ptrPx + 0.5f));
        }

        // Precompute optional L-Cancel / window end-tick markers ("7f" / "1f", >= 8.0 px cap).
        bool hasTicks = !repStrip && fx != NULL && fx->tickStartLabel != NULL && fx->tickEndLabel != NULL
                        && fx->tickStartCell >= 0 && fx->tickEndCell >= fx->tickStartCell && fx->tickEndCell < n;
        float tickSc = ui::style::scaleForCap(ui::style::kTypeMicro);
        float tick0X = 0.0f, tick1X = 0.0f, tick0W = 0.0f, tick1W = 0.0f;
        float tickEdge0 = 0.0f, tickEdge1 = 0.0f;
        if (hasTicks)
        {
            tick0W = gfx::textWidth(fx->tickStartLabel, tickSc);
            tick1W = gfx::textWidth(fx->tickEndLabel, tickSc);
            tickEdge0 = bar.x0 + innerPadX + i2f(fx->tickStartCell) * (cellW + gap);
            tickEdge1 = bar.x0 + innerPadX + i2f(fx->tickEndCell) * (cellW + gap) + cellW;
            // Default aligned inside the green zone edges; nudge outside if pointer sits right on that edge.
            tick0X = tickEdge0;
            tick1X = tickEdge1 - tick1W;
            if (drawPtr && ptrPx >= tick0X - 5.0f * xs && ptrPx <= tick0X + tick0W + 5.0f * xs)
            {
                tick0X = tickEdge0 - tick0W - 3.0f * xs;
            }
            if (drawPtr && ptrPx >= tick1X - 5.0f * xs && ptrPx <= tick1X + tick1W + 5.0f * xs)
            {
                tick1X = tickEdge1 + 3.0f * xs;
            }
            float minTickX = bar.x0 + 3.0f * xs;
            float maxTickX = bar.x1 - 3.0f * xs;
            if (tick0X < minTickX) tick0X = minTickX;
            if (tick0X + tick0W > maxTickX) tick0X = maxTickX - tick0W;
            if (tick1X < minTickX) tick1X = minTickX;
            if (tick1X + tick1W > maxTickX) tick1X = maxTickX - tick1W;
            tick0X = i2f(f2i(tick0X + 0.5f));
            tick1X = i2f(f2i(tick1X + 0.5f));
        }

        // ---- PASS 1: ALL QUADS & TRIANGLES (zero shape<->text GX flushes) ----
        if (hasHeader)
        {
            drawSmokedCard(pillX0, pillY0, pillX1 - pillX0, pillH, xs);
        }
        if (lCount > 0)
        {
            float curX = lx0;
            float swY = kKeyLabelTop + (capSize - kKeySwatch) * 0.5f;
            for (int i = 0; i < lCount; i++)
            {
                if (legend[i].swatch != 0u)
                {
                    gfx::fillRect(curX - 1.2f * xs, swY - 1.2f, swW + 2.4f * xs, kKeySwatch + 2.4f, kHudTroughBg);
                    gfx::fillRect(curX, swY, swW, kKeySwatch, legend[i].swatch | 0xFFu);
                    curX += (swW + swGap) * hdrRatio;
                }
                curX += (widths[i] + itemGap) * hdrRatio;
            }
        }

        // Trough & cells (discrete compact streak pips for rep strips, chunky timing cells for timing bars).
        if (repStrip)
        {
            drawSmokedCard(bar.x0, bar.y0, bw, bh, xs);
        }
        else
        {
            gfx::fillRect(bar.x0, bar.y0, bw, bh, kHudTroughBg);
        }
        for (int k = 0; k < n; k++)
        {
            gfx::Rgba col = (cells != NULL && k < count) ? (cells[k] | 0xFFu) : 0x243442FFu;
            float cx = bar.x0 + innerPadX + i2f(k) * (cellW + gap);
            if (repStrip)
            {
                gfx::fillRect(cx - 1.0f * xs, cellTop - 1.0f, cellW + 2.0f * xs, cellH + 2.0f, kHudTroughBg);
                gfx::fillRect(cx, cellTop, cellW, cellH, col);
                if (cells != NULL && k < count && (cells[k] & 0xFFFFFF00u) != (kHudCellEmpty & 0xFFFFFF00u))
                {
                    gfx::fillRect(cx, cellTop, cellW, 2.0f, kHudCellSheen);
                }
            }
            else
            {
                gfx::fillRect(cx, cellTop, cellW, cellH, col);
                gfx::fillRect(cx, cellTop, cellW, 2.0f, kHudCellSheen);
                if (fx != NULL && fx->flashCell == k && fx->flashAlpha > 0.01f)
                {
                    u32 fa = static_cast<u32>(fx->flashAlpha * 185.0f);
                    if (fa > 220u) fa = 220u;
                    gfx::fillRect(cx, cellTop, cellW, cellH, 0xFFF29A00u | fa);
                }
            }
        }

        // Scrim pills + crisp boundary ticks for green-window end markers ("7f" ... "1f").
        if (hasTicks)
        {
            float tPad = 3.0f * xs;
            float tPillY = bar.y1 + 1.0f;
            float tPillH = 10.0f;
            gfx::fillRect(tickEdge0, bar.y1 - 2.0f, 2.0f * xs, 3.0f, kHudGreen);
            gfx::fillRect(tickEdge1 - 2.0f * xs, bar.y1 - 2.0f, 2.0f * xs, 3.0f, kHudGreen);
            drawSmokedPlate(tick0X - tPad, tPillY, tick0W + 2.0f * tPad, tPillH);
            drawSmokedPlate(tick1X - tPad, tPillY, tick1W + 2.0f * tPad, tPillH);
        }

        // Bold black-outlined white upward pointer arrow (^) centered under `ptrPx`.
        if (drawPtr)
        {
            float halfBase = 4.4f * xs;
            float outPad = 1.4f * xs;
            float tipY = bar.y1 + 1.5f;
            float baseY = bar.y1 + 7.5f;
            gfx::fillTri(ptrPx, tipY - 0.5f, ptrPx - halfBase - outPad, baseY + 1.0f, ptrPx + halfBase + outPad,
                         baseY + 1.0f, kHudTroughBg);
            gfx::fillTri(ptrPx, tipY, ptrPx - halfBase, baseY, ptrPx + halfBase, baseY, 0xFFFFFFFFu);
        }

        // ---- PASS 2: ALL TEXT (single uninterrupted font pass) ----
        if (has3Zone)
        {
            float padX = 6.0f * xs;
            if (lw > 0.0f)
            {
                drawOutlinedText(bar.x0 + padX, ty, leftLabel, kHudTextPrimary, hdrSx, sc);
            }
            if (cw > 0.0f)
            {
                drawOutlinedText(barCx - cw * hdrRatio * 0.5f, ty, caption, kHudTextPrimary, hdrSx, sc);
            }
            if (rw > 0.0f)
            {
                drawOutlinedText(bar.x1 - padX - rw * hdrRatio, ty, rightLabel, kHudTextPrimary, hdrSx, sc);
            }
        }
        else if (hasRepHdr)
        {
            float padX = 6.0f * xs;
            drawOutlinedText(pillX0 + padX, ty, caption, kHudTextPrimary, hdrSx, sc);
            drawOutlinedText(pillX1 - padX - rw * hdrRatio, ty, fx->repTag, 0x8CE8D8FFu, hdrSx, sc);
        }
        else if (lCount > 0)
        {
            float curX = lx0;
            for (int i = 0; i < lCount; i++)
            {
                if (legend[i].swatch != 0u)
                {
                    curX += (swW + swGap) * hdrRatio;
                }
                drawOutlinedText(curX, ty, legend[i].label, kHudTextPrimary, hdrSx, sc);
                curX += (widths[i] + itemGap) * hdrRatio;
            }
        }
        else if (hasHeader)
        {
            drawOutlinedText(barCx - drawTotalW * 0.5f, ty, caption, kHudTextPrimary, hdrSx, sc);
        }

        if (hasTicks)
        {
            float tickCapY = bar.y1 + 2.0f;
            float tickTy = ui::style::textYForCap(tickCapY, tickSc);
            drawShadowText(tick0X, tickTy, fx->tickStartLabel, kHudTextPrimary, tickSc);
            drawShadowText(tick1X, tickTy, fx->tickEndLabel, kHudTextPrimary, tickSc);
        }

        Box out = {bar.x0, kKeyY, bar.x1, hasTicks ? (bar.y1 + 11.0f) : (bar.y1 + 9.0f)};
        return out;
    }

    static bool hudTrainingSidebarShown()
    {
        return game::trainingGameMode() && main::settings().display.hud != 0 && ui::matchHudCounterShown();
    }

    void drawStatPlate(const StatPlateRow* rows, int count, float aboveBottom, bool wide)
    {
        if (rows == NULL || count <= 0)
        {
            return;
        }
        using namespace ui::layout;
        float xs = xsOf(wide);
        Box b = drillStatPlateBox(count, aboveBottom, wide, hudTrainingSidebarShown());
        float bw = b.x1 - b.x0;
        float bh = b.y1 - b.y0;
        float cx = (b.x0 + b.x1) * 0.5f;
        float maxW = bw - 2.0f * ui::style::kFibTight * xs;

        // Pass 1: all quads (plate + row dividers).
        drawSmokedPlate(b.x0, b.y0, bw, bh);
        for (int i = 1; i < count; i++)
        {
            float ry = b.y0 + 4.0f + i2f(i) * kDrillPlateRowH;
            drawPlateDivider(b.x0, ry - 1.5f, bw, xs);
        }
        // Pass 2: all text.
        for (int i = 0; i < count; i++)
        {
            float ry = b.y0 + 4.0f + i2f(i) * kDrillPlateRowH;
            if (rows[i].label != NULL && rows[i].label[0] != '\0')
            {
                drawShadowCapCenter(cx, ry + 4.0f, rows[i].label, kHudTextLabel, 8.5f, maxW);
            }
            if (rows[i].value != NULL && rows[i].value[0] != '\0')
            {
                gfx::Rgba vc = rows[i].valueColor != 0u ? rows[i].valueColor : kHudTextValue;
                drawShadowCapCenter(cx, ry + 15.0f, rows[i].value, vc, 12.5f, maxW);
            }
        }
    }

    static bool cpuIsPassiveDefault(const cpu::CpuOptions& o)
    {
        // Untouched Lab defaults (cpu_options.cpp): Lab control, Stand behavior, Random/No DI,
        // no SDI/intangible/mash/adaptive/multi-reversal, shield not forced Always On,
        // and default counter slots (Ground=13 Jab, Air=1 Airdodge, Shield=1 Grab, or None=0).
        bool defCounters = (o.counterGround == 0 || o.counterGround == 13)
                           && (o.counterAir == 0 || o.counterAir == 1)
                           && (o.counterShield == 0 || o.counterShield == 1);
        return o.control == cpu::kControlLab && o.behavior == cpu::kBehaviorStand
               && (o.tdi == cpu::kTdiRandom || o.tdi == cpu::kTdiNone)
               && o.adaptiveMode == cpu::kAdaptiveOff && o.sdiAmount == 0 && o.intangible == cpu::kIntangibleOff
               && o.infiniteShields != cpu::kShieldsOn && o.grabEscape == cpu::kMashNone
               && o.clatterEscape == cpu::kMashNone && o.controlledBy == 0 && o.multiReversal == 0 && defCounters;
    }

    static const char* cpuStatusLabel(const cpu::CpuOptions& o)
    {
        if (cpuIsPassiveDefault(o))
        {
            return NULL;
        }
        if (o.control == cpu::kControlTraining)
        {
            return "CPU: Game AI";
        }
        if (o.controlledBy != 0)
        {
            static const char* const kPortLabels[4] = {"CPU: Port 1", "CPU: Port 2", "CPU: Port 3", "CPU: Port 4"};
            u8 p = o.controlledBy <= 4 ? static_cast<u8>(o.controlledBy - 1) : 0u;
            return kPortLabels[p];
        }
        if (o.adaptiveMode == cpu::kAdaptiveActive)
        {
            return "CPU: Adaptive";
        }
        if (o.adaptiveMode == cpu::kAdaptiveTrack)
        {
            return "CPU: Tracking";
        }
        if (o.behavior == cpu::kBehaviorShield)
        {
            return o.counterShield != 0 ? "CPU: Shield/OoS" : "CPU: Shield";
        }
        if (o.behavior == cpu::kBehaviorPerfectShield)
        {
            return "CPU: P-Shield";
        }
        if (o.behavior == cpu::kBehaviorCrouch)
        {
            return "CPU: Crouch";
        }
        if (o.behavior == cpu::kBehaviorJump)
        {
            return "CPU: Jump";
        }
        if (o.intangible != cpu::kIntangibleOff)
        {
            return o.intangible == cpu::kIntangiblePassThrough ? "CPU: Intang" : "CPU: No KB";
        }
        if (o.tdi == cpu::kTdiSurvival)
        {
            return "CPU: Surv DI";
        }
        if (o.tdi == cpu::kTdiCombo)
        {
            return "CPU: Combo DI";
        }
        if (o.tdi == cpu::kTdiSlightRandom)
        {
            return "CPU: Rand DI";
        }
        if (o.tdi == cpu::kTdiInwards)
        {
            return "CPU: DI In";
        }
        if (o.tdi == cpu::kTdiOutwards || o.tdi == cpu::kTdiSlightAway || o.tdi == cpu::kTdiDownAway)
        {
            return "CPU: DI Out";
        }
        if (o.tdi == cpu::kTdiCustom)
        {
            return "CPU: Custom DI";
        }
        if ((o.counterGround != 0 && o.counterGround != 13) || (o.counterAir != 0 && o.counterAir != 1)
            || (o.counterShield != 0 && o.counterShield != 1) || o.multiReversal != 0)
        {
            return "CPU: Counter";
        }
        if (o.sdiAmount != 0)
        {
            return "CPU: SDI";
        }
        if (o.grabEscape != cpu::kMashNone || o.clatterEscape != cpu::kMashNone)
        {
            return "CPU: Mash";
        }
        if (o.infiniteShields == cpu::kShieldsOn)
        {
            return "CPU: Inf Shield";
        }
        if (o.tdi == cpu::kTdiRandom)
        {
            return "CPU: Rand DI";
        }
        return "CPU: Stand";
    }

    void drawCpuStatusTag(const game::MatchState& match, bool menuOpen)
    {
        if (!match.running || !match.training || match.paused || menuOpen || !display::hudShown()
            || drillHudActiveId() != 0 || comboBreakRushActive() || drillPracticeCpuInStandby()
            || ui::pages::scenarioMenuState()->preset != ui::pages::kPresetOff || scenarioPresetsWantsDraw(match)
            || gfx::lineHeight(0.5f) <= 0.0f)
        {
            return;
        }
        const char* label = cpuStatusLabel(main::settings().cpu.options);
        if (label == NULL)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        int cpuSlot = -1;
        for (int i = 0; i < n; i++)
        {
            if (display::cpuPlayer(refs[i]))
            {
                cpuSlot = refs[i].playerNo;
                break;
            }
        }
        if (cpuSlot < 0)
        {
            return;
        }
        bool wide = gfx::widescreen();
        float xs = ui::layout::xsOf(wide);
        ui::layout::Box b = ui::layout::cpuStatusTagBox(cpuSlot, wide);
        float bw = b.x1 - b.x0;
        float bh = b.y1 - b.y0;
        float cap = 9.0f;
        float sc = ui::style::scaleForCap(cap);
        float padX = 6.0f * xs;
        float maxW = bw - 2.0f * padX;
        float tw = gfx::textWidth(label, sc);
        float sx = (tw > maxW && tw > 0.0f) ? (sc * maxW / tw) : sc;
        float drawW = tw > maxW ? maxW : tw;
        float pillW = drawW + 2.0f * padX;
        if (pillW > bw) pillW = bw;
        float pillX = (b.x0 + b.x1) * 0.5f - pillW * 0.5f;
        drawSmokedCard(pillX, b.y0, pillW, bh, xs);
        drawOutlinedText((b.x0 + b.x1) * 0.5f - drawW * 0.5f, ui::style::textYForCap(b.y0 + (bh - cap) * 0.5f, sc),
                         label, kHudTextPrimary, sx, sc);
    }

    void drawUnitDial(float cx, float cy, float rx, float ry, gfx::Rgba fillCol, gfx::Rgba rimCol)
    {
        gfx::fillEllipse(cx, cy, rx, ry, fillCol);
        gfx::lineBegin(GX_LINES, 4, 12);
        gfx::vertex(cx - rx, cy, 0x3A586860u);
        gfx::vertex(cx + rx, cy, 0x3A586860u);
        gfx::vertex(cx, cy - ry, 0x3A586860u);
        gfx::vertex(cx, cy + ry, 0x3A586860u);
        gfx::lineBegin(GX_LINESTRIP, 17, 12);
        for (int i = 0; i < 17; i++)
        {
            float ux, uy;
            unitPoint16(i, ux, uy);
            gfx::vertex(cx + ux * rx, cy - uy * ry, rimCol);
        }
    }

    void drawDialRayDot(float cx, float cy, float rx, float ry, float vx, float vy, float rayScale, float dotR,
                        float xs, gfx::Rgba rayCol, gfx::Rgba dotCol, u8 lineW, bool drawRay)
    {
        float tx = cx + vx * rx * rayScale;
        float ty = cy - vy * ry * rayScale;
        if (drawRay)
        {
            gfx::lineBegin(GX_LINES, 2, lineW);
            gfx::vertex(cx, cy, rayCol);
            gfx::vertex(tx, ty, rayCol);
        }
        gfx::fillEllipse(tx, ty, (dotR + 0.9f) * xs, dotR + 0.9f, 0x050C12E8u);
        gfx::fillEllipse(tx, ty, dotR * xs, dotR, dotCol);
    }

    int extraHumanPort(const game::FighterRef& ref)
    {
        const u8* cfg = reinterpret_cast<const u8*>(ipPadConfig::getInstance());
        if (game::isRam(cfg) && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
        {
            int c = cfg[kPadConfigPorts + ref.playerNo] & 0xF;
            if (c >= 0 && c < display::kPortPads)
            {
                return c;
            }
        }
        return ref.port;
    }

    void clipTextToWidth(char* buf, float scale, float maxW)
    {
        if (gfx::textWidth(buf, scale) <= maxW)
        {
            return;
        }
        int len = static_cast<int>(strlen(buf));
        while (len > 3)
        {
            len--;
            buf[len - 2] = '.';
            buf[len - 1] = '.';
            buf[len] = '\0';
            if (gfx::textWidth(buf, scale) <= maxW)
            {
                return;
            }
        }
    }

    int countInputViewerSlots(const game::FighterRef* refs, int n)
    {
        u8 who = main::settings().display.inputViewer;
        if (who == display::kWhoNone)
        {
            return 0;
        }
        int slots = 0;
        for (int i = 0; i < n && slots < 2; i++)
        {
            bool cpu = display::cpuPlayer(refs[i]);
            if ((who & (cpu ? display::kWhoCpu : display::kWhoHuman)) != 0)
            {
                slots++;
            }
        }
        return slots;
    }

    void unitPoint16(int idx, float& ox, float& oy)
    {
        int i = idx & 15;
        int q = i >> 2;
        int k = i & 3;
        float a = kCosQ[k];
        float b = kCosQ[4 - k];
        if (q == 0)
        {
            ox = a;
            oy = b;
        }
        else if (q == 1)
        {
            ox = -b;
            oy = a;
        }
        else if (q == 2)
        {
            ox = -a;
            oy = -b;
        }
        else
        {
            ox = b;
            oy = -a;
        }
    }

    static float viewerColumnBottomFor(int vSlots, const ui::layout::Box& band, bool wide, bool sidebar)
    {
        float bottom = sidebar ? ui::layout::trainingSidebarRoom(wide).y1 : 0.0f;
        float sbLeft = sidebar ? ui::layout::trainingSidebarRoom(wide).x0 : 0.0f;
        for (int s = 0; s < vSlots; s++)
        {
            ui::layout::Box vb = ui::layout::inputViewerCardBox(s, band, wide, sidebar);
            if ((!sidebar || vb.x1 > sbLeft) && vb.y1 > bottom)
            {
                bottom = vb.y1;
            }
        }
        return bottom;
    }

    static int hudInfoRowCount(const main::InfoSettings& info)
    {
        int n = 0;
        for (int r = 0; r < main::kInfoRows; r++)
        {
            if (info.rows[r] != 0)
            {
                n = r + 1;
            }
        }
        return n;
    }

    float hudInfoStackBottom(bool wide)
    {
        using namespace ui::layout;
        const main::Settings& s = main::settings();
        int hr = hudInfoRowCount(s.info[0]);
        int cr = hudInfoRowCount(s.info[1]);
        if (hr <= 0 && cr <= 0)
        {
            return 0.0f;
        }
        float wH = hr > 0 ? infoWidth(s.info[0].size, wide) : 0.0f;
        float wC = cr > 0 ? infoWidth(s.info[1].size, wide) : 0.0f;
        float w = wH > wC ? wH : wC;
        bool counter = game::trainingGameMode() && s.display.hud != 0 && ui::matchHudCounterShown();
        Box band = topBand();
        float top = infoTop(band, w, wide);
        if (counter)
        {
            float cX0 = wide ? 106.0f : 31.0f;
            float cX1 = wide ? 183.0f : 130.0f;
            float cY1 = 77.0f;
            if (kInfoLeft < cX1 && cX0 < kInfoLeft + w && cY1 + kStepGap > top)
            {
                top = cY1 + kStepGap;
            }
        }
        float bot = hr > 0 ? top + infoHeight(hr, s.info[0].size) : top;
        if (cr > 0)
        {
            bot = infoCpuTop(top, bot) + infoHeight(cr, s.info[1].size);
        }
        return bot;
    }

    float hudViewerColumnBottom(bool wide)
    {
        if (!game::trainingGameMode())
        {
            return 0.0f;
        }
        const main::Settings& s = main::settings();
        int vSlots = ui::layout::inputViewerSlotCount(s.display.inputViewer);
        return viewerColumnBottomFor(vSlots, ui::layout::topBand(), wide, hudTrainingSidebarShown());
    }

    ui::layout::Box hudRightCardBox(int kind, bool wide, int viewerSlotsOverride, int hasCompOverride)
    {
        (void)hasCompOverride;
        using namespace ui::layout;
        const main::Settings& s = main::settings();
        Box band = topBand();
        int vSlots = viewerSlotsOverride >= 0 ? viewerSlotsOverride : inputViewerSlotCount(s.display.inputViewer);
        bool sidebar = hudTrainingSidebarShown();
        if (kind == kHudCardPsa)
        {
            float infoBot = hudInfoStackBottom(wide);
            Box b = psaInspectorBox(band, vSlots, false, wide, infoBot);
            if (infoBot <= 0.0f && sidebar)
            {
                float cX0 = wide ? 106.0f : 31.0f;
                float cX1 = wide ? 183.0f : 130.0f;
                float cY1 = 77.0f;
                if (b.x0 < cX1 && cX0 < b.x1 && cY1 + kStepGap > b.y0)
                {
                    float h = b.y1 - b.y0;
                    b.y0 = cY1 + kStepGap;
                    b.y1 = b.y0 + h;
                }
            }
            return b;
        }
        if (kind == kHudCardAdaptive)
        {
            return adaptiveHudBox(band, vSlots, false, false, wide, 0.0f, sidebar);
        }
        return drillHudBox(wide);
    }

    ui::layout::Box hudViewerCardBox(int slot, bool wide)
    {
        return ui::layout::inputViewerCardBox(slot, ui::layout::topBand(), wide, hudTrainingSidebarShown());
    }

    float hudOsdTopStartY(bool isTopRight, bool vsReadOnly)
    {
        using namespace ui::layout;
        const main::Settings& s = main::settings();
        bool wide = gfx::widescreen();
        Box band = topBand();
        float sideBot = 0.0f;
        if (!isTopRight)
        {
            sideBot = hudInfoStackBottom(wide);
            if (s.display.psaInspector != main::kPsaOff)
            {
                Box psa = hudRightCardBox(kHudCardPsa, wide);
                if (psa.x0 < 320.0f && psa.y1 > sideBot)
                {
                    sideBot = psa.y1;
                }
            }
        }
        else
        {
            bool training = game::trainingGameMode();
            int vSlots = training ? inputViewerSlotCount(s.display.inputViewer) : 0;
            bool hasAdaptive = training && s.cpu.options.control == cpu::kControlLab
                               && s.cpu.options.adaptiveMode != cpu::kAdaptiveOff;
            int plateRows = 0;
            if (!vsReadOnly && training)
            {
                plateRows = drillHudActiveId() != 0 ? 3 : scenarioPresetsPlateRows(game::matchState());
            }
            sideBot = rightColumnBottom(band, vSlots, false, false, wide, hasAdaptive, plateRows,
                                        hudTrainingSidebarShown());
            if (s.display.psaInspector != main::kPsaOff)
            {
                Box psa = hudRightCardBox(kHudCardPsa, wide);
                if (psa.x1 > 320.0f && psa.y1 > sideBot)
                {
                    sideBot = psa.y1;
                }
            }
        }
        float y = kSafeMarginY;
        if (band.x1 > band.x0 && band.y1 > band.y0 && band.y1 + kStepGap > y)
        {
            y = band.y1 + kStepGap;
        }
        if (sideBot > 0.0f && sideBot + kStepGap > y)
        {
            y = sideBot + kStepGap;
        }
        return y > 345.0f ? 345.0f : y;
    }
}
}
