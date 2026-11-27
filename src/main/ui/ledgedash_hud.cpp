#include "main/ui/ledgedash_hud.h"

#include <cstdio>

#include "main/game/techniques.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/match_hud.h"
#include "main/ui/menu.h"
#include "main/ui/style.h"
#include "shared/game/scene.h"

// Minimal neutral Ledgedash HUD: displays a per-frame bar for the last
// ledgedash (kept for 300 frames), a standardized top summary strip (GALINT,
// hang frames, and airdodge angle), and a compact 1-line color legend.
namespace tm {
namespace ui {
    using main::settings;

    enum { kLifetime = 300, kMaxCells = 64 };

    static u8 s_seenDismissGen = 0;
    static s32 s_dismissedDone = game::kNoFrame;

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
        u8 gen = menuScratch()[kLedgeDismissScratchOffset];
        if (gen != s_seenDismissGen)
        {
            s_seenDismissGen = gen;
            s_dismissedDone = l.done;
        }
        if (l.done == s_dismissedDone)
        {
            return false;
        }
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
        float xs = layout::xsOf(wide);
        layout::Box bar = layout::frameBarBox(n, wide);
        float barW = bar.x1 - bar.x0;
        float barH = bar.y1 - bar.y0;
        float barCx = (bar.x0 + bar.x1) * 0.5f;

        // Zone 1: Bold calm-white swatch-legend row directly above bar (09-calm-ledgedash).
        if (g.ledgePanel)
        {
            static const char* const kLegNames[6] = {"Ledge", "Drop", "Jump", "Dodge", "Land", "GALINT"};
            static const u8 kLegCells[6] = {
                game::kCellLedge, game::kCellFall, game::kCellJump,
                game::kCellAirdodge, game::kCellLanding, game::kCellGalint
            };
            float lScale = style::scaleForCap(layout::kKeyCap);
            float swW = layout::kKeySwatch * xs;
            float swGap = 3.5f * xs;
            float itemGap = 7.0f * xs;
            float lWidths[6];
            float totalW = 0.0f;
            for (int i = 0; i < 6; i++)
            {
                lWidths[i] = gfx::textWidth(kLegNames[i], lScale);
                totalW += swW + swGap + lWidths[i] + (i < 5 ? itemGap : 0.0f);
            }
            layout::Box hdrBox = layout::ledgeHeaderBox(wide);
            float availW = (hdrBox.x1 - hdrBox.x0) - 4.0f * xs;
            float sx = (totalW > availW && totalW > 0.0f) ? (lScale * availW / totalW) : lScale;
            float drawTotalW = totalW > availW ? availW : totalW;
            float lx0 = barCx - drawTotalW * 0.5f;
            if (lx0 + drawTotalW > hdrBox.x1 - 2.0f * xs)
            {
                lx0 = hdrBox.x1 - 2.0f * xs - drawTotalW;
            }
            if (lx0 < hdrBox.x0 + 2.0f * xs)
            {
                lx0 = hdrBox.x0 + 2.0f * xs;
            }
            float ratio = lScale > 0.0f ? (sx / lScale) : 1.0f;
            float swY = layout::kKeyLabelTop + (layout::kKeyCap - layout::kKeySwatch) * 0.5f;
            float curX = lx0;
            for (int i = 0; i < 6; i++)
            {
                gfx::fillRect(curX - 1.0f * xs, swY - 1.0f, swW + 2.0f * xs, layout::kKeySwatch + 2.0f, 0x05090FF2u);
                gfx::fillRect(curX, swY, swW, layout::kKeySwatch, kCellColors[kLegCells[i]] | 0xFFu);
                curX += (swW + swGap + lWidths[i] + itemGap) * ratio;
            }
            curX = lx0;
            float ly = style::textYForCap(layout::kKeyLabelTop, lScale);
            for (int i = 0; i < 6; i++)
            {
                float tx = curX + (swW + swGap) * ratio;
                gfx::text(tx - xs, ly, kLegNames[i], 0x04070CE8u, sx, lScale);
                gfx::text(tx + xs, ly, kLegNames[i], 0x04070CE8u, sx, lScale);
                gfx::text(tx, ly - 1.0f, kLegNames[i], 0x04070CE8u, sx, lScale);
                gfx::text(tx, ly + 1.0f, kLegNames[i], 0x04070CE8u, sx, lScale);
                gfx::text(tx, ly, kLegNames[i], 0xF8FCFFFFu, sx, lScale);
                curX += (swW + swGap + lWidths[i] + itemGap) * ratio;
            }
        }

        // Chunky individually dark-outlined chronological frame cells (09-calm-ledgedash).
        float barScale = wide ? 0.90f : 1.0f;
        gfx::fillRect(bar.x0, bar.y0, barW, barH, 0x05090FF4u);
        float gap = layout::kCellGap * barScale;
        float innerPadX = 2.0f * barScale;
        float cellW = (barW - 2.0f * innerPadX - (n - 1) * gap) / n;
        if (cellW < 1.5f)
        {
            gap = 1.0f * xs;
            cellW = (barW - 2.0f * innerPadX - (n - 1) * gap) / n;
        }
        float cellH = barH - 4.0f;
        for (int k = 0; k < n; k++)
        {
            u8 type = static_cast<u8>(k < frames ? l.cells[first + k] : game::kCellGalint);
            gfx::Rgba col = (type < game::kCellTypes ? kCellColors[type] : kCellColors[game::kCellOther]) | 0xFFu;
            float cx = bar.x0 + innerPadX + k * (cellW + gap);
            gfx::fillRect(cx, bar.y0 + 2.0f, cellW, cellH, col);
            gfx::fillRect(cx, bar.y0 + 2.0f, cellW, 2.0f, 0xFFFFFF34u);
        }

        // Bold black-outlined white upward pointer arrow (^) at first actionable frame.
        {
            int ptrCell = frames < n ? frames : (n - 1);
            float halfBase = 4.4f * xs;
            float outPad = 1.4f * xs;
            float px = bar.x0 + innerPadX + ptrCell * (cellW + gap) + cellW * 0.5f;
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

        // Zone 2: Panel B single quiet smoked plate on right margin under Option A GC controller.
        if (g.ledgePanel)
        {
            char galintBuf[20];
            char ledgeBuf[16];
            char angleBuf[20];
            gfx::Rgba galintCol = 0xFF6658FFu;
            if (l.noXlu)
            {
                galintCol = 0xFFB848FFu;
                if (l.grabCount > 5)
                {
                    snprintf(galintBuf, sizeof(galintBuf), "None (%d/5)", l.grabCount);
                }
                else
                {
                    snprintf(galintBuf, sizeof(galintBuf), "None");
                }
            }
            else
            {
                galintCol = l.galint > 0 ? 0x4CE688FFu : 0xFF6658FFu;
                snprintf(galintBuf, sizeof(galintBuf), l.galint > 0 ? "+%df" : "%df", l.galint);
            }
            snprintf(ledgeBuf, sizeof(ledgeBuf), "%df", l.ledgeFrames);
            if (l.hasAngle)
            {
                snprintf(angleBuf, sizeof(angleBuf), "%.1f deg", l.angle);
            }
            else
            {
                snprintf(angleBuf, sizeof(angleBuf), "--");
            }

            int vSlots = layout::inputViewerSlotCount(settings().display.inputViewer);
            bool sidebar = game::trainingGameMode() && settings().display.hud != 0 && ui::matchHudCounterShown();
            float aboveBot = sidebar ? layout::trainingSidebarRoom(wide).y1 : 0.0f;
            float sbLeft = sidebar ? layout::trainingSidebarRoom(wide).x0 : 0.0f;
            if (vSlots > 0)
            {
                layout::Box band = layout::topBand();
                for (int s = 0; s < vSlots; s++)
                {
                    layout::Box vb = layout::inputViewerCardBox(s, band, wide, sidebar);
                    if ((!sidebar || vb.x1 > sbLeft) && vb.y1 > aboveBot)
                    {
                        aboveBot = vb.y1;
                    }
                }
            }
            layout::Box plate = layout::drillStatPlateBox(3, aboveBot, wide, sidebar);
            float pw = plate.x1 - plate.x0;
            float ph = plate.y1 - plate.y0;
            float pcx = (plate.x0 + plate.x1) * 0.5f;
            float maxW = pw - 2.0f * style::kFibTight * xs;
            gfx::fillRect(plate.x0, plate.y0, pw, ph, 0x111720A8u);

            static const char* const kRowLabels[3] = {"GALINT", "On Ledge", "Angle"};
            const char* rowVals[3] = {galintBuf, ledgeBuf, angleBuf};
            const gfx::Rgba rowCols[3] = {galintCol, 0xF4F8FCFFu, 0xF4F8FCFFu};
            float lblSc = style::scaleForCap(style::kTypeMeta);
            float valSc = style::scaleForCap(style::kTypeHeading);
            for (int r = 0; r < 3; r++)
            {
                float ry = plate.y0 + 4.0f + r * layout::kDrillPlateRowH;
                if (r > 0)
                {
                    gfx::fillRect(plate.x0 + 8.0f * xs, ry - 1.0f, pw - 16.0f * xs, 2.0f, 0xFFFFFF38u);
                }
                float lw = gfx::textWidth(kRowLabels[r], lblSc);
                float lx = pcx - lw * 0.5f;
                float ly = style::textYForCap(ry + 3.0f, lblSc);
                gfx::text(lx + xs, ly + 1.0f, kRowLabels[r], 0x000000C8u, lblSc);
                gfx::text(lx, ly, kRowLabels[r], 0xC6D4E0FFu, lblSc);

                float vw = gfx::textWidth(rowVals[r], valSc);
                float sx = (vw > maxW && vw > 0.0f) ? (valSc * maxW / vw) : valSc;
                float vx = pcx - (vw > maxW ? maxW : vw) * 0.5f;
                float vy = style::textYForCap(ry + 16.0f, valSc);
                gfx::text(vx + xs, vy + 1.0f, rowVals[r], 0x000000D0u, sx, valSc);
                gfx::text(vx, vy, rowVals[r], rowCols[r], sx, valSc);
            }
        }
    }
}
}
