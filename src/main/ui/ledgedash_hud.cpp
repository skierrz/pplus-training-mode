#include "main/ui/ledgedash_hud.h"

#include <cstdio>

#include "main/game/techniques.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"

// Minimal neutral Ledgedash HUD: displays a per-frame bar for the last
// ledgedash (kept for 300 frames) and a compact top summary line with GALINT,
// frames on ledge, and airdodge angle.
namespace tm {
namespace ui {
    using main::settings;

    enum { kLifetime = 300, kMaxCells = 64 };

    // Neutral high-contrast per-phase cell colors.
    static const gfx::Rgba kCellColors[game::kCellTypes] = {
        0x888888D0, // ledge
        0xE0C040D0, // fall
        0x50C878D0, // jump
        0x4890E0D0, // airdodge
        0xE8E8E8D0, // landing
        0xE05050D0, // attack
        0xA070D0D0, // other
        0x40D0C8D0  // GALINT
    };

    bool ledgedashHudVisible()
    {
        const game::Ledgedash& l = game::lastLedgedash();
        s32 now = game::trackerFrame();
        return settings().general.ledgeHud && l.done != game::kNoFrame && now >= l.done
               && now - l.done < kLifetime && l.count > 0;
    }

    int ledgedashCells()
    {
        int max = settings().general.ledgeMaxFrames < 30 ? 30 : settings().general.ledgeMaxFrames;
        return max > kMaxCells ? kMaxCells : max;
    }

    void drawLedgedashHud()
    {
        const game::Ledgedash& l = game::lastLedgedash();
        const main::GeneralSettings& g = settings().general;
        int galint = g.ledgeGalint && l.galint > 0 ? l.galint : 0;
        int max = ledgedashCells();
        int frames = l.count;
        int first = 0;
        if (frames + galint > max)
        {
            // Keep the end of the ledgedash: the oldest cells (on the ledge) go.
            int over = frames + galint - max;
            first = over < frames ? over : frames;
            frames -= first;
            if (frames + galint > max)
            {
                galint = max - frames;
            }
        }
        int n = frames + galint;
        if (n <= 0)
        {
            return;
        }
        bool wide = gfx::widescreen();
        layout::Box bar = layout::frameBarBox(n, wide);
        gfx::fillRect(bar.x0, bar.y0, bar.x1 - bar.x0, bar.y1 - bar.y0, 0x10141AD8);
        float gap = layout::kCellGap * layout::xsOf(wide);
        float cellW = ((bar.x1 - bar.x0) - (n + 1) * gap) / n;
        float cellH = (bar.y1 - bar.y0) - 2.0f * layout::kCellGap;
        for (int k = 0; k < n; k++)
        {
            u8 type = static_cast<u8>(k < frames ? l.cells[first + k] : game::kCellGalint);
            gfx::Rgba col = type < game::kCellTypes ? kCellColors[type] : kCellColors[game::kCellOther];
            gfx::fillRect(bar.x0 + gap + k * (cellW + gap), bar.y0 + layout::kCellGap, cellW, cellH, col);
        }
        if (g.ledgePanel)
        {
            char summary[64];
            if (l.noXlu)
            {
                if (l.grabCount > 5)
                {
                    snprintf(summary, sizeof(summary), "GALINT: None (%d/5)   Ledge: %d", l.grabCount, l.ledgeFrames);
                }
                else
                {
                    snprintf(summary, sizeof(summary), "GALINT: None   Ledge: %d", l.ledgeFrames);
                }
            }
            else if (l.hasAngle)
            {
                snprintf(summary, sizeof(summary), "GALINT: %d   Ledge: %d   Angle: %.1f", l.galint, l.ledgeFrames,
                         l.angle);
            }
            else
            {
                snprintf(summary, sizeof(summary), "GALINT: %d   Ledge: %d", l.galint, l.ledgeFrames);
            }
            float scale = style::scaleForCap(layout::kKeyCap);
            float tw = gfx::textWidth(summary, scale);
            float padX = 6.0f * layout::xsOf(wide);
            float x = 320.0f - tw * 0.5f;
            float y = style::textYForCap(layout::kKeyLabelTop, scale);
            gfx::fillRect(x - padX, y - 2.0f, tw + 2.0f * padX, gfx::lineHeight(scale) + 4.0f, 0x10141AC8);
            gfx::text(x, y, summary, 0xFFFFFFFF, scale);
        }
    }
}
}
