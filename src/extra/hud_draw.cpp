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
        const gfx::Rgba edge = 0x04070CE8u;
        gfx::text(x - dx, y, text, edge, sx, sy);
        gfx::text(x + dx, y, text, edge, sx, sy);
        gfx::text(x, y - 1.0f, text, edge, sx, sy);
        gfx::text(x, y + 1.0f, text, edge, sx, sy);
        gfx::text(x, y, text, color, sx, sy);
    }

    ui::layout::Box drawTimingBar(const gfx::Rgba* cells, int count, int pointerCell, const TimingLegendItem* legend,
                                  int legendCount, const char* caption, bool wide, const char* leftLabel,
                                  const char* rightLabel)
    {
        using namespace ui::layout;
        float xs = xsOf(wide);
        float barScale = wide ? 0.90f : 1.0f;
        int n = count > 0 ? count : 1;
        Box bar = frameBarBox(n, wide);
        float bw = bar.x1 - bar.x0;
        float bh = bar.y1 - bar.y0;
        float barCx = (bar.x0 + bar.x1) * 0.5f;

        float capSize = kKeyCap;
        float sc = ui::style::scaleForCap(capSize);
        float ty = ui::style::textYForCap(kKeyLabelTop, sc);
        bool has3Zone = (leftLabel != NULL && leftLabel[0] != '\0') || (rightLabel != NULL && rightLabel[0] != '\0');
        if (has3Zone)
        {
            // Calm 3-zone symmetric header (Early left | Drill Title center | Late right, matching 08-calm-wavedash).
            float lw = (leftLabel != NULL && leftLabel[0] != '\0') ? gfx::textWidth(leftLabel, sc) : 0.0f;
            float cw = (caption != NULL && caption[0] != '\0') ? gfx::textWidth(caption, sc) : 0.0f;
            float rw = (rightLabel != NULL && rightLabel[0] != '\0') ? gfx::textWidth(rightLabel, sc) : 0.0f;
            float minGap = 8.0f * xs;
            float need = lw + cw + rw + (lw > 0.0f ? minGap : 0.0f) + (rw > 0.0f ? minGap : 0.0f);
            float sx = (need > bw && need > 0.0f) ? (sc * bw / need) : sc;
            float ratio = sc > 0.0f ? (sx / sc) : 1.0f;
            if (lw > 0.0f)
            {
                drawOutlinedText(bar.x0, ty, leftLabel, 0xF8FCFFFFu, sx, sc);
            }
            if (cw > 0.0f)
            {
                drawOutlinedText(barCx - cw * ratio * 0.5f, ty, caption, 0xF8FCFFFFu, sx, sc);
            }
            if (rw > 0.0f)
            {
                drawOutlinedText(bar.x1 - rw * ratio, ty, rightLabel, 0xF8FCFFFFu, sx, sc);
            }
        }
        else
        {
            // Bold calm-white Rodin caption / swatch legend row directly above bar (09-calm-ledgedash).
            float swW = kKeySwatch * xs;
            float swGap = 4.0f * xs;
            float itemGap = 9.0f * xs;
            float totalW = 0.0f;
            float widths[8];
            int lCount = legend != NULL ? (legendCount < 8 ? legendCount : 8) : 0;
            if (lCount > 0)
            {
                for (int i = 0; i < lCount; i++)
                {
                    widths[i] = gfx::textWidth(legend[i].label, sc);
                    totalW += (legend[i].swatch != 0u ? (swW + swGap) : 0.0f) + widths[i]
                              + (i + 1 < lCount ? itemGap : 0.0f);
                }
                float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
                float availW = maxRight - kSafeMarginX;
                float sx = (totalW > availW && totalW > 0.0f) ? (sc * availW / totalW) : sc;
                float drawTotalW = totalW > availW ? availW : totalW;
                float lx0 = barCx - drawTotalW * 0.5f;
                if (lx0 + drawTotalW > maxRight)
                {
                    lx0 = maxRight - drawTotalW;
                }
                if (lx0 < kSafeMarginX)
                {
                    lx0 = kSafeMarginX;
                }
                float ratio = (sc > 0.0f) ? (sx / sc) : 1.0f;
                float curX = lx0;
                float swY = kKeyLabelTop + (capSize - kKeySwatch) * 0.5f;
                for (int i = 0; i < lCount; i++)
                {
                    if (legend[i].swatch != 0u)
                    {
                        gfx::fillRect(curX - 1.2f * xs, swY - 1.2f, swW + 2.4f * xs, kKeySwatch + 2.4f, 0x05090FF2u);
                        gfx::fillRect(curX, swY, swW, kKeySwatch, legend[i].swatch | 0xFFu);
                        curX += (swW + swGap) * ratio;
                    }
                    curX += (widths[i] + itemGap) * ratio;
                }
                curX = lx0;
                for (int i = 0; i < lCount; i++)
                {
                    if (legend[i].swatch != 0u)
                    {
                        curX += (swW + swGap) * ratio;
                    }
                    drawOutlinedText(curX, ty, legend[i].label, 0xF8FCFFFFu, sx, sc);
                    curX += (widths[i] + itemGap) * ratio;
                }
            }
            else if (caption != NULL && caption[0] != '\0')
            {
                float cw = gfx::textWidth(caption, sc);
                float maxCapW = bw + 60.0f * xs;
                float sx = (cw > maxCapW && cw > 0.0f) ? (sc * maxCapW / cw) : sc;
                float drawW = cw > maxCapW ? maxCapW : cw;
                drawOutlinedText(barCx - drawW * 0.5f, ty, caption, 0xF8FCFFFFu, sx, sc);
            }
        }

        // Chunky individually dark-outlined frame cells (matching 08-calm-wavedash & 09-calm-ledgedash).
        gfx::fillRect(bar.x0, bar.y0, bw, bh, 0x05090FF4u);
        float gap = kCellGap * barScale;
        float innerPadX = 2.0f * barScale;
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
        float cellH = bh - 4.0f;
        for (int k = 0; k < n; k++)
        {
            gfx::Rgba col = (cells != NULL && k < count) ? (cells[k] | 0xFFu) : 0x243442FFu;
            float cx = bar.x0 + innerPadX + i2f(k) * (cellW + gap);
            gfx::fillRect(cx, bar.y0 + 2.0f, cellW, cellH, col);
            gfx::fillRect(cx, bar.y0 + 2.0f, cellW, 2.0f, 0xFFFFFF34u);
        }

        // Bold black-outlined white upward pointer arrow (^) centered under `pointerCell`.
        if (pointerCell >= 0 && pointerCell < n)
        {
            float halfBase = 4.4f * xs;
            float outPad = 1.4f * xs;
            float px = bar.x0 + innerPadX + i2f(pointerCell) * (cellW + gap) + cellW * 0.5f;
            if (px - halfBase - outPad < bar.x0)
            {
                px = bar.x0 + halfBase + outPad;
            }
            if (px + halfBase + outPad > bar.x1)
            {
                px = bar.x1 - halfBase - outPad;
            }
            float tipY = bar.y1 + 1.5f;
            float baseY = bar.y1 + 7.5f;
            gfx::fillTri(px, tipY - 0.5f, px - halfBase - outPad, baseY + 1.0f, px + halfBase + outPad,
                         baseY + 1.0f, 0x04070CF8u);
            gfx::fillTri(px, tipY, px - halfBase, baseY, px + halfBase, baseY, 0xFFFFFFFFu);
        }

        Box out = {bar.x0, kKeyY, bar.x1, bar.y1 + 9.0f};
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

        drawSmokedPlate(b.x0, b.y0, bw, bh);
        for (int i = 0; i < count; i++)
        {
            float ry = b.y0 + 4.0f + i2f(i) * kDrillPlateRowH;
            if (i > 0)
            {
                drawPlateDivider(b.x0, ry - 1.5f, bw, xs);
            }
            if (rows[i].label != NULL && rows[i].label[0] != '\0')
            {
                drawShadowCapCenter(cx, ry + 4.0f, rows[i].label, 0xC6D4E0FFu, 8.5f, maxW);
            }
            if (rows[i].value != NULL && rows[i].value[0] != '\0')
            {
                gfx::Rgba vc = rows[i].valueColor != 0u ? rows[i].valueColor : 0xF4F8FCFFu;
                drawShadowCapCenter(cx, ry + 15.0f, rows[i].value, vc, 12.5f, maxW);
            }
        }
    }

    static const char* cpuStatusLabel(const cpu::CpuOptions& o)
    {
        if (o.control == cpu::kControlTraining)
        {
            return "CPU: Game AI";
        }
        if (o.adaptiveMode == cpu::kAdaptiveActive)
        {
            return "CPU: Adaptive";
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
        if (o.tdi == cpu::kTdiSurvival)
        {
            return "CPU: Surv DI";
        }
        if (o.tdi == cpu::kTdiCombo)
        {
            return "CPU: Combo DI";
        }
        if (o.tdi == cpu::kTdiRandom || o.tdi == cpu::kTdiSlightRandom)
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
        if (o.counterGround != 0 || o.counterAir != 0)
        {
            return "CPU: Counter";
        }
        return "CPU: Stand";
    }

    void drawCpuStatusTag(const game::MatchState& match, bool menuOpen)
    {
        if (!match.running || !match.training || match.paused || menuOpen || !display::hudShown()
            || drillHudActiveId() != 0 || comboBreakRushActive() || drillPracticeCpuInStandby()
            || gfx::lineHeight(0.5f) <= 0.0f)
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
        const char* label = cpuStatusLabel(main::settings().cpu.options);
        // Frameless dark-outlined portrait tag matching 01-final-everyday & 02-final-sf6.
        ui::layout::Box b = ui::layout::cpuStatusTagBox(cpuSlot, wide);
        float bw = b.x1 - b.x0;
        float bh = b.y1 - b.y0;
        float cap = 10.0f;
        float sc = ui::style::scaleForCap(cap);
        float maxW = bw + 12.0f * xs;
        float tw = gfx::textWidth(label, sc);
        float sx = (tw > maxW && tw > 0.0f) ? (sc * maxW / tw) : sc;
        float drawW = tw > maxW ? maxW : tw;
        drawOutlinedText((b.x0 + b.x1) * 0.5f - drawW * 0.5f, ui::style::textYForCap(b.y0 + (bh - cap) * 0.5f, sc),
                         label, 0xF8FCFFFFu, sx, sc);
    }

    void drawHudCard(float x, float y, float w, float h, float xs, gfx::Rgba accent, gfx::Rgba sheen)
    {
        float px = 2.0f * xs;
        float innerW = w - 2.0f * px;
        gfx::fillRect(x, y, w, h, 0x050C12E4u);
        gfx::fillRect(x + px, y + 2.0f, innerW, h - 4.0f, 0x3A5868D8u);
        gfx::fillGradV(x + 2.0f * px, y + 4.0f, w - 4.0f * px, h - 8.0f, 0x132632DCu, 0x09141CE0u);
        gfx::fillRect(x + px, y + 2.0f, innerW, 2.0f, accent);
        if (sheen != 0u)
        {
            gfx::fillRect(x + px, y + 2.0f, innerW, 2.0f, sheen);
        }
    }

    void drawHudBanner(float ox, float oy, float bw, float bh, float xs, gfx::Rgba accent)
    {
        float ix = ox + 4.0f * xs;
        float iw = bw - 8.0f * xs;
        gfx::fillRect(ox, oy, bw, bh, 0x060D12F2u);
        gfx::fillRect(ox + 2.0f * xs, oy + 2.0f, bw - 4.0f * xs, bh - 4.0f, accent);
        gfx::fillGradV(ix, oy + 4.0f, iw, bh - 8.0f, 0x0D1E26ECu, 0x071117ECu);
        gfx::fillGradV(ix, oy + 4.0f, iw, 15.0f, 0x226874F0u, 0x123C46F0u);
        gfx::fillRect(ix, oy + 19.0f, iw, 2.0f, 0x2A5866E0u);
    }

    void drawHudBannerTitle(float ox, float oy, float bw, float bh, float xs, gfx::Rgba accent, const char* title)
    {
        drawHudBanner(ox, oy, bw, bh, xs, accent);
        float sc = ui::style::displayScaleForCap(ui::style::kTypeMeta);
        gfx::displayText(ox + 10.0f * xs, ui::style::displayYForCap(oy + 7.0f, sc), title, 0xFFFFFFFFu, 0xB8E0EEFFu,
                         sc, sc, 1.2f, 0x050C12FFu);
    }

    void drawHudPill(float x, float y, float w, float xs, bool gold)
    {
        gfx::fillRect(x, y, w, 13.0f, 0x07141AE8u);
        gfx::fillGradV(x + 2.0f * xs, y + 2.0f, w - 4.0f * xs, 9.0f,
                       gold ? 0xB88200FFu : 0x1B6B76FFu, gold ? 0x7A5000FFu : 0x0E3D45FFu);
    }

    void drawHudDivider(float x, float y, float bw, float xs)
    {
        gfx::fillRect(x + 4.0f * xs, y, bw - 8.0f * xs, 2.0f, 0x2A4C58C0u);
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
