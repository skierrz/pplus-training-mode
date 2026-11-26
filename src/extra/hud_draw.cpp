#include "extra/hud_draw.h"

#include <GX/GXTypes.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "extra/di_cstick_core.h"
#include "extra/net_alloc.h"
#include "extra/scenario_presets.h"
#include "main/cpu/cpu_settings.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/game/match.h"
#include "main/gfx/text.h"
#include "main/settings.h"
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

    static float stepBelowBand(float x0, float x1, const ui::layout::Box& band)
    {
        using namespace ui::layout;
        bool hit = band.x1 > band.x0 && band.y1 > band.y0 && x0 < band.x1 && band.x0 < x1 && band.y1 > kViewerTop;
        return hit ? (band.y1 + kStepGap) : kViewerTop;
    }

    static float viewerCardY1(int slot, const ui::layout::Box& band, float xs)
    {
        using namespace ui::layout;
        float x0 = kViewerRight - kViewerWidth * xs;
        float top = stepBelowBand(x0, kViewerRight, band);
        float y0 = top + i2f(slot > 0 ? slot : 0) * (kViewerHeight + kViewerGap);
        return y0 + kViewerHeight;
    }

    static ui::layout::Box rightCardBox(int kind, const ui::layout::Box& band, int vSlots, bool hasComp, bool hasPsa,
                                        bool wide)
    {
        using namespace ui::layout;
        float xs = xsOf(wide);
        if (kind == kHudCardDiCompass)
        {
            float w = kCompassWidth * xs;
            float x0 = kCompassRight - w;
            float y0 = vSlots > 0 ? (viewerCardY1(vSlots - 1, band, xs) + kViewerGap)
                                  : stepBelowBand(x0, kCompassRight, band);
            return box(x0, y0, w, kCompassHeight);
        }
        if (kind == kHudCardPsa || kind == kHudCardAdaptive)
        {
            float x1 = kViewerRight;
            if (vSlots > 0 || hasComp)
            {
                float rightW = (vSlots > 0 ? kViewerWidth : kCompassWidth) * xs;
                x1 = kViewerRight - rightW - kStepGap;
            }
            if (kind == kHudCardPsa)
            {
                float w = kPsaWidth * xs;
                float x0 = x1 - w;
                return box(x0, stepBelowBand(x0, x1, band), w, kPsaHeight);
            }
            float w = kAdaptiveHudWidth * xs;
            if (hasPsa)
            {
                Box psa = rightCardBox(kHudCardPsa, band, vSlots, hasComp, false, wide);
                float ax = (vSlots == 0 && hasComp) ? psa.x0 : (psa.x1 - w);
                return box(ax, psa.y1 + kViewerGap, w, kAdaptiveHudHeight);
            }
            float x0 = x1 - w;
            return box(x0, stepBelowBand(x0, x1, band), w, kAdaptiveHudHeight);
        }
        float w = kDrillHudWidth * xs;
        return box(320.0f - w * 0.5f, kDrillHudTop, w, kDrillHudHeight);
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

    ui::layout::Box hudRightCardBox(int kind, bool wide, int viewerSlotsOverride, int hasCompOverride)
    {
        const main::Settings& s = main::settings();
        ui::layout::Box band = ui::layout::topBand();
        int vSlots = viewerSlotsOverride >= 0 ? viewerSlotsOverride : ui::layout::inputViewerSlotCount(s.display.inputViewer);
        bool hasComp = hasCompOverride >= 0
                           ? (hasCompOverride != 0)
                           : (s.general.diDisplay != 0 && s.display.diVectorMode != main::kDiVectorLive);
        bool hasPsa = s.display.psaInspector != main::kPsaOff;
        return rightCardBox(kind, band, vSlots, hasComp, hasPsa, wide);
    }

    ui::layout::Box hudViewerCardBox(int slot, bool wide)
    {
        using namespace ui::layout;
        float xs = xsOf(wide);
        float w = kViewerWidth * xs;
        float y1 = viewerCardY1(slot, topBand(), xs);
        return box(kViewerRight - w, y1 - kViewerHeight, w, kViewerHeight);
    }

    float hudOsdTopStartY(bool isTopRight, bool vsReadOnly)
    {
        using namespace ui::layout;
        const main::Settings& s = main::settings();
        bool wide = gfx::widescreen();
        float xs = xsOf(wide);
        Box band = topBand();
        float sideBot = 0.0f;
        if (!isTopRight)
        {
            int hr = hudInfoRowCount(s.info[0]);
            int cr = hudInfoRowCount(s.info[1]);
            if (hr > 0 || cr > 0)
            {
                float hSc = infoScale(s.info[0].size);
                float cSc = infoScale(s.info[1].size);
                float wH = hr > 0 ? 236.0f * hSc * xs : 0.0f;
                float wC = cr > 0 ? 236.0f * cSc * xs : 0.0f;
                float top = infoTop(band, wH > wC ? wH : wC, wide);
                float bot = hr > 0 ? top + (12.0f + i2f(hr) * 13.0f) * hSc : top;
                if (cr > 0)
                {
                    bot = (bot > top ? bot + kInfoGap : top) + (12.0f + i2f(cr) * 13.0f) * cSc;
                }
                sideBot = bot;
            }
        }
        else
        {
            bool training = game::trainingGameMode();
            int vSlots = training ? inputViewerSlotCount(s.display.inputViewer) : 0;
            bool hasComp = training && s.general.diDisplay != 0 && s.display.diVectorMode != main::kDiVectorLive;
            bool hasPsa = s.display.psaInspector != main::kPsaOff;
            bool hasAdaptive = training && s.cpu.options.control == cpu::kControlLab
                               && s.cpu.options.adaptiveMode != cpu::kAdaptiveOff;
            if (vSlots > 0)
            {
                float y1 = viewerCardY1(vSlots - 1, band, xs);
                if (y1 > sideBot) sideBot = y1;
            }
            const bool active[4] = {hasComp, hasPsa, hasAdaptive,
                                    !vsReadOnly && scenarioPresetsWantsDraw(game::matchState())};
            for (int k = 0; k < 4; k++)
            {
                if (active[k])
                {
                    float y1 = rightCardBox(k, band, vSlots, hasComp, hasPsa, wide).y1;
                    if (y1 > sideBot) sideBot = y1;
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
