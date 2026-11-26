#include "extra/hud_draw.h"

#include <GX/GXTypes.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "extra/di_cstick_core.h"
#include "extra/drill_hud.h"
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

    ui::layout::Box drawTimingBar(const gfx::Rgba* cells, int count, int pointerCell, const TimingLegendItem* legend,
                                  int legendCount, const char* caption, bool wide)
    {
        using namespace ui::layout;
        float xs = xsOf(wide);
        int n = count > 0 ? count : 1;
        Box bar = frameBarBox(n, wide);
        float bw = bar.x1 - bar.x0;
        float bh = bar.y1 - bar.y0;
        float barCx = (bar.x0 + bar.x1) * 0.5f;

        // Calm white Rodin caption / swatch legend row directly above the bar (y = 27..38).
        float capSize = 8.5f;
        float sc = ui::style::scaleForCap(capSize);
        float swW = kKeySwatch * xs;
        float swGap = 3.0f * xs;
        float itemGap = 8.0f * xs;
        float totalW = 0.0f;
        float widths[8];
        int lCount = legend != NULL ? (legendCount < 8 ? legendCount : 8) : 0;
        if (lCount > 0)
        {
            for (int i = 0; i < lCount; i++)
            {
                widths[i] = gfx::textWidth(legend[i].label, sc);
                totalW += (legend[i].swatch != 0u ? (swW + swGap) : 0.0f) + widths[i] + (i + 1 < lCount ? itemGap : 0.0f);
            }
            float maxRight = kViewerRight - kViewerWidth * xs - kStepGap;
            float lx0 = barCx - totalW * 0.5f;
            if (lx0 + totalW > maxRight)
            {
                lx0 = maxRight - totalW;
            }
            if (lx0 < kSafeMarginX)
            {
                lx0 = kSafeMarginX;
            }
            float curX = lx0;
            for (int i = 0; i < lCount; i++)
            {
                if (legend[i].swatch != 0u)
                {
                    gfx::fillRect(curX, 27.5f, swW, 7.0f, legend[i].swatch);
                    curX += swW + swGap;
                }
                curX += widths[i] + itemGap;
            }
            curX = lx0;
            float ty = ui::style::textYForCap(27.0f, sc);
            for (int i = 0; i < lCount; i++)
            {
                if (legend[i].swatch != 0u)
                {
                    curX += swW + swGap;
                }
                drawShadowText(curX, ty, legend[i].label, 0xF1F5F9FFu, sc);
                curX += widths[i] + itemGap;
            }
        }
        else if (caption != NULL && caption[0] != '\0')
        {
            drawShadowCapCenter(barCx, 27.0f, caption, 0xF1F5F9FFu, capSize, bw + 60.0f * xs);
        }

        // Crisp dark smoked trough + high-contrast semantic color cells.
        gfx::fillRect(bar.x0, bar.y0, bw, bh, 0x0A121ADCu);
        float gap = kCellGap * xs;
        float cellW = (bw - i2f(n + 1) * gap) / i2f(n);
        if (cellW < 1.5f)
        {
            cellW = 1.5f;
        }
        float cellH = bh - 4.0f;
        for (int k = 0; k < n; k++)
        {
            gfx::Rgba col = (cells != NULL && k < count) ? cells[k] : 0x263644CCu;
            gfx::fillRect(bar.x0 + gap + i2f(k) * (cellW + gap), bar.y0 + 2.0f, cellW, cellH, col);
        }

        // Clean white upward pointer arrow (^) centered under `pointerCell`.
        if (pointerCell >= 0 && pointerCell < n)
        {
            float halfBase = 4.2f * xs;
            float px = bar.x0 + gap + i2f(pointerCell) * (cellW + gap) + cellW * 0.5f;
            if (px - halfBase - xs < bar.x0)
            {
                px = bar.x0 + halfBase + xs;
            }
            if (px + halfBase + xs > bar.x1)
            {
                px = bar.x1 - halfBase - xs;
            }
            float tipY = bar.y1 + 1.5f;
            float baseY = bar.y1 + 7.5f;
            gfx::fillTri(px, tipY - 0.5f, px - halfBase - xs, baseY + 1.0f, px + halfBase + xs, baseY + 1.0f,
                         0x060C12E0u);
            gfx::fillTri(px, tipY, px - halfBase, baseY, px + halfBase, baseY, 0xFFFFFFFFu);
        }

        Box out = {bar.x0, kKeyY, bar.x1, bar.y1 + 9.0f};
        return out;
    }

    void drawStatPlate(const StatPlateRow* rows, int count, float aboveBottom, bool wide)
    {
        if (rows == NULL || count <= 0)
        {
            return;
        }
        using namespace ui::layout;
        float xs = xsOf(wide);
        Box b = drillStatPlateBox(count, aboveBottom, wide);
        float bw = b.x1 - b.x0;
        float bh = b.y1 - b.y0;
        float cx = (b.x0 + b.x1) * 0.5f;
        float maxW = bw - 10.0f * xs;

        drawSmokedPlate(b.x0, b.y0, bw, bh);
        for (int i = 0; i < count; i++)
        {
            float ry = b.y0 + 4.0f + i2f(i) * kDrillPlateRowH;
            if (i > 0)
            {
                drawPlateDivider(b.x0, ry - 1.0f, bw, xs);
            }
            if (rows[i].label != NULL && rows[i].label[0] != '\0')
            {
                drawShadowCapCenter(cx, ry + 3.0f, rows[i].label, 0xC6D4E0FFu, 8.2f, maxW);
            }
            if (rows[i].value != NULL && rows[i].value[0] != '\0')
            {
                gfx::Rgba vc = rows[i].valueColor != 0u ? rows[i].valueColor : 0xF4F8FCFFu;
                drawShadowCapCenter(cx, ry + 15.0f, rows[i].value, vc, 11.5f, maxW);
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
            || drillHudActiveId() != 0 || gfx::lineHeight(0.5f) <= 0.0f)
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
        ui::layout::Box b = ui::layout::cpuStatusTagBox(cpuSlot, wide);
        float bw = b.x1 - b.x0;
        float bh = b.y1 - b.y0;
        drawSmokedPlate(b.x0, b.y0, bw, bh, 0x0E161E98u);
        drawShadowCapCenter((b.x0 + b.x1) * 0.5f, b.y0 + 1.2f, label, 0xE6F0F8FFu, 10.5f, bw - 6.0f * xs);
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
        float sc = 8.5f / 24.0f;
        gfx::displayText(ox + 10.0f * xs, oy + 7.0f - 2.5f * sc, title, 0xFFFFFFFFu, 0xB8E0EEFFu, sc, sc, 1.2f,
                         0x050C12FFu);
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

    static float viewerColumnBottomFor(int vSlots, const ui::layout::Box& band, bool wide)
    {
        float bottom = 0.0f;
        for (int s = 0; s < vSlots; s++)
        {
            ui::layout::Box vb = ui::layout::inputViewerCardBox(s, band, wide);
            if (vb.y1 > bottom)
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
        return viewerColumnBottomFor(vSlots, ui::layout::topBand(), wide);
    }

    ui::layout::Box hudRightCardBox(int kind, bool wide, int viewerSlotsOverride, int hasCompOverride)
    {
        (void)hasCompOverride;
        using namespace ui::layout;
        const main::Settings& s = main::settings();
        Box band = topBand();
        int vSlots = viewerSlotsOverride >= 0 ? viewerSlotsOverride : inputViewerSlotCount(s.display.inputViewer);
        if (kind == kHudCardPsa)
        {
            float infoBot = hudInfoStackBottom(wide);
            Box b = psaInspectorBox(band, vSlots, false, wide, infoBot);
            if (infoBot <= 0.0f && game::trainingGameMode() && s.display.hud != 0 && ui::matchHudCounterShown())
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
            return adaptiveHudBox(band, vSlots, false, false, wide);
        }
        return drillHudBox(wide);
    }

    ui::layout::Box hudViewerCardBox(int slot, bool wide)
    {
        return ui::layout::inputViewerCardBox(slot, ui::layout::topBand(), wide);
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
            bool hasDrillPlate = !vsReadOnly && training
                                 && (drillHudActiveId() != 0 || scenarioPresetsWantsDraw(game::matchState()));
            sideBot = rightColumnBottom(band, vSlots, false, false, wide, hasAdaptive, hasDrillPlate ? 3 : 0);
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
