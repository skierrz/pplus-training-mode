#include "extra/hud_draw.h"

#include <GX/GXTypes.h>
#include <string.h>

#include "main/cpu/cpu_settings.h"
#include "main/display/display.h"
#include "main/gfx/text.h"
#include "main/settings.h"

namespace tm {
namespace extra {
    static const float kCosQ[5] = {1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f};

    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale)
    {
        gfx::text(x + 0.8f, y + 0.8f, text, 0x000000C8u, scale);
        gfx::text(x, y, text, color, scale);
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

    ui::layout::Box hudRightCardBox(int kind, bool wide, int viewerSlotsOverride, int hasCompOverride)
    {
        const main::Settings& s = main::settings();
        ui::layout::Box band = ui::layout::topBand();
        int vSlots = viewerSlotsOverride >= 0 ? viewerSlotsOverride : ui::layout::inputViewerSlotCount(s.display.inputViewer);
        bool hasComp = hasCompOverride >= 0
                           ? (hasCompOverride != 0)
                           : (s.general.diDisplay != 0 && s.display.diVectorMode != main::kDiVectorLive);
        bool hasPsa = s.display.psaInspector != main::kPsaOff;
        bool hasAdapt = s.cpu.options.control == cpu::kControlLab && s.cpu.options.adaptiveMode != cpu::kAdaptiveOff;
        if (kind == kHudCardDiCompass)
        {
            return ui::layout::diCompassBox(band, vSlots, wide);
        }
        if (kind == kHudCardPsa)
        {
            return ui::layout::psaInspectorBox(band, vSlots, hasComp, wide);
        }
        if (kind == kHudCardAdaptive)
        {
            return ui::layout::adaptiveHudBox(band, vSlots, hasComp, hasPsa, wide);
        }
        return ui::layout::techchaseHudBox(band, vSlots, hasComp, hasPsa, wide, hasAdapt);
    }
}
}
