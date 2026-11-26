#include "extra/drill_hud.h"

#include <cstdio>
#include <gf/gf_camera.h>
#include <gf/gf_pad_status.h>
#include <string.h>

#ifndef TM_STAGE4C
#define TM_STAGE4C 0
#endif
#if TM_STAGE4C
#include "extra/challenge_backends.h"
#endif
#include "extra/combo_break_rush.h"
#include "extra/combo_break_rush_core.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_float_djc_core.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_hud_core.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/drill_practice_driver.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"
#include "extra/scenario_presets.h"
#include "extra/ui_assets.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/pad_inmatch.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/services.h"
#include "main/ui/hud_layout.h"
#include "main/ui/menu.h"
#include "main/ui/pages/scenarios.h"
#include "shared/game/mem.h"
#include "shared/drill_session.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "shared/ui_icons.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kPorts = iface::kPadCount,
            // label() flags: bits 0..1 align (0 left, 1 right, 2 center),
            // bits 2..4 size/font preset (0=8pt, 1=10pt, 2=disp 8.5pt, 3=disp 13pt, 4=disp 20pt),
            // bit 5 = gold display bottom, bit 6 = red display bottom, bit 7 = clamp to 168*xs.
            kAlignRight = 1,
            kAlignCenter = 2,
            kSz10 = 1 << 2,
            kSzDisp85 = 2 << 2,
            kSzDisp13 = 3 << 2,
            kSzDisp20 = 4 << 2,
            kDispGold = 1 << 5,
            kDispRed = 1 << 6,
            kClamp168 = 1 << 7
        };

        const u32 kGradeCols[5] = {0xA0BCC4FFu, 0xFFD300FFu, 0x4CE688FFu, 0xFF6658FFu, 0xFFB848FFu};
        const u32 kPipCols[5] = {0x20343EFFu, 0xFFD300FFu, 0x4CE688FFu, 0xFF6658FFu, 0xFFB848FFu};
        const float kScales[5] = {8.0f / 15.0f, 10.0f / 15.0f, 8.5f / 24.0f, 13.0f / 24.0f, 20.0f / 24.0f};
        const u8 kPromptIcons[3][4] = {
            {ui::kIconGcA, ui::kIconClA, ui::kIconWii2, ui::kIconWiiA},
            {ui::kIconGcStart, ui::kIconWiiPlus, ui::kIconWiiPlus, ui::kIconWiiPlus},
            {ui::kIconGcB, ui::kIconClB, ui::kIconWii1, ui::kIconWiiB}
        };
        const char kPromptLabels[] = "Retry\0Lab\0Return to CSS";
        const char kPromptGlyphs[] = "A\0+\0B";

        // Single consolidated runtime block avoids per-static PowerPC -sdata 0 address loads.
        struct Runtime {
            DrillHudDebugBlock debug;
            DrillHudState st;
            drills::Session* sess;
            u32 prevHeld[kPorts];
            u32 releaseMask[kPorts];
            float xs;
            u8 waitRelease;
            u8 lastPadType;
            u8 fresh;
            u8 synced;
            u8 pausedByUs;
        };

        static Runtime* s_rtPtr = NULL;

        void unpauseIfNeeded(Runtime& s_rt)
        {
            if (s_rt.pausedByUs)
            {
                game::setGameplayPaused(false);
                s_rt.pausedByUs = 0;
            }
        }

        void syncDebug()
        {
            if (s_rtPtr == NULL)
            {
                return;
            }
            Runtime& s_rt = *s_rtPtr;
            s_rt.debug.magic = kDrillHudDebugMagic;
            s_rt.debug.version = kDrillHudDebugVersion;
            memcpy(&s_rt.debug.activeDrill, &s_rt.st.activeDrill, 14);
            s_rt.debug.alcNotice = s_rt.st.alcNotice;
        }

        // Double-framed Brawl/P+ panel: dark outer frame, 2px accent border, dark gradient body.
        void drawPanel(float ox, float oy, float w, float h, u32 accent, u32 top, u32 bot)
        {
            float xs = s_rtPtr->xs;
            float bw = w * xs;
            gfx::fillRect(ox, oy, bw, h, 0x060D12F2u);
            gfx::fillRect(ox + 2.0f * xs, oy + 2.0f, bw - 4.0f * xs, h - 4.0f, accent);
            gfx::fillGradV(ox + 4.0f * xs, oy + 4.0f, bw - 8.0f * xs, h - 8.0f, top, bot);
        }

        // Compact 5-argument text renderer supporting system and Brawl Display fonts.
        void label(float x, float y, const char* s, u32 col, u8 fl)
        {
            int sz = (fl >> 2) & 7;
            bool disp = sz >= 2;
            float sc = kScales[sz];
            float tw = disp ? gfx::displayTextWidth(s, sc) : gfx::textWidth(s, sc);
            float sx = sc;
            if (fl & kClamp168)
            {
                float mw = 168.0f * s_rtPtr->xs;
                if (tw > mw && tw > 0.0f)
                {
                    sx = sc * mw / tw;
                    tw = mw;
                }
            }
            u8 al = fl & 3;
            float dx = al == kAlignCenter ? (x - tw * 0.5f) : (al == kAlignRight ? (x - tw) : x);
            if (disp)
            {
                u32 bot = (fl & kDispGold) ? 0xE89B00FFu : ((fl & kDispRed) ? 0xD8483EFFu : 0xB8E0EEFFu);
                gfx::displayText(dx, y - 2.5f * sc, s, col, bot, sx, sc, 1.2f, 0x050C12FFu);
            }
            else
            {
                gfx::text(dx, y - 5.0f * sc, s, col, sx, sc);
            }
        }

        void drawPips(float x, float y, float totalW, float h, int count)
        {
            Runtime& s_rt = *s_rtPtr;
            if (count <= 0)
            {
                count = kDrillFreePips;
            }
            float step = totalW / static_cast<float>(count);
            float gap = (count > 10 ? 1.0f : 2.0f) * s_rt.xs;
            float pw = step - gap;
            if (pw < 1.0f)
            {
                pw = 1.0f;
            }
            for (int k = 0; k < count && k < kDrillMaxSetReps; k++, x += step)
            {
                u8 p = s_rt.st.repPips[k];
                gfx::fillRect(x, y, pw, h, kPipCols[p < 5 ? p : 0]);
            }
        }

        int primaryTrackedSlot()
        {
            int slot = -1;
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t && !t->partner && (!t->cpu || slot < 0))
                {
                    slot = i;
                    if (!t->cpu)
                    {
                        break;
                    }
                }
            }
            return slot;
        }

        void drawLiveHud()
        {
            Runtime& s_rt = *s_rtPtr;
            DrillHudState& st = s_rt.st;
            bool wide = gfx::widescreen();
            float xs = s_rt.xs;
            u8 d = st.activeDrill;
            int slot = primaryTrackedSlot();
            u8 g = st.lastGrade < 5 ? st.lastGrade : 0;
            u8 target = drillModeTargetReps(st.mode);

            // Format calm right-margin Panel B Stat Plate rows (3 rows, centered).
            char val0[28];
            char val1[24];
            char val2[24];
            const char* lbl0 = "Result";
            const char* lbl1 = "Streak";
            const char* lbl2 = "Rate";
            gfx::Rgba col0 = kGradeCols[g];

            const char* fb = st.lastFeedback;
            bool wdTargetCue = (d == drills::kDrillWavedash && drillPracticeConfig().wdTargetZone != 0u);
            if (wdTargetCue && strcmp(fb, "READY") == 0)
            {
                fb = "INTO TARGET";
            }
            if (st.alcNotice)
            {
                snprintf(val0, sizeof(val0), d == drills::kDrillLCancel ? "Auto LC On" : "Check Cast");
            }
            else if (strlen(fb) <= 13)
            {
                snprintf(val0, sizeof(val0), "%s", fb);
            }
            else
            {
                snprintf(val0, sizeof(val0), "%.12s", fb);
            }
            if (st.sessionBest)
            {
                snprintf(val1, sizeof(val1), "%u (Best %u)", static_cast<unsigned>(st.streakFull),
                         static_cast<unsigned>(st.sessionBest));
            }
            else
            {
                snprintf(val1, sizeof(val1), "%u", static_cast<unsigned>(st.streakFull));
            }
            if (st.mode == kDrillModeTimed60)
            {
                lbl2 = "Time / Hit";
                unsigned secs = static_cast<unsigned>((st.timerFrames + 59u) / 60u);
                snprintf(val2, sizeof(val2), "%02us / %u", secs, static_cast<unsigned>(st.totalHit));
            }
            else if (target > 0)
            {
                lbl2 = "Set";
                snprintf(val2, sizeof(val2), "%u/%u (%u)", static_cast<unsigned>(st.repsDone),
                         static_cast<unsigned>(target), static_cast<unsigned>(st.repsHit));
            }
            else
            {
                unsigned pct = st.totalAtt > 0u ? (static_cast<unsigned>(st.totalHit) * 100u) / st.totalAtt : 0u;
                snprintf(val2, sizeof(val2), "%u%% (%u/%u)", pct, static_cast<unsigned>(st.totalHit),
                         static_cast<unsigned>(st.totalAtt));
            }

            // Zone 1: Top-Center Pure Timing / State Bar (calm white legend + crisp cells + white ^ pointer).
            gfx::Rgba cells[64];
            int cellCount = 15;
            int ptrCell = -1;
            TimingLegendItem legend[6];
            int legendCount = 0;
            char caption[40];
            caption[0] = '\0';

            if (d == drills::kDrillWavedash)
            {
                // 15-cell Wavedash bar: 3 Early (red) | 1 Perfect f1 (mint green) | 11 Late (lime -> gold -> orange).
                static const gfx::Rgba kWdCells[15] = {
                    0xDC4444E8u, 0xDC4444E8u, 0xDC4444E8u,
                    0x3CE890FFu,
                    0x7CE068E8u, 0xA8DC50E8u, 0xD4D440E8u, 0xF0C038E8u,
                    0xF0A234E8u, 0xEC8634E8u, 0xE66E36E8u, 0xE05A38E8u,
                    0xDC4C3CE8u, 0xD84440E8u, 0xD43E44E8u
                };
                memcpy(cells, kWdCells, sizeof(kWdCells));
                cellCount = 15;
                legend[0].swatch = 0xDC4444FFu; legend[0].label = "Early";
                legend[1].swatch = 0x3CE890FFu; legend[1].label = "Frame 1";
                legend[2].swatch = 0xF0B838FFu; legend[2].label = "Late";
                legendCount = 3;
                lbl0 = "Timing";
                lbl1 = "Angle";
                const WdCoreState* wd = slot >= 0 ? coreDrillWavedashState(slot) : NULL;
                if (wd != NULL && wd->seq > 0u)
                {
                    const WdResult& r = wd->last;
                    if (r.outcome == kWdOutcomeSuccess)
                    {
                        int f = (r.kind == kWdKindWaveland) ? r.airdodgeAirFrames : r.jumpsquatToDodge;
                        if (f < 1) f = 1;
                        ptrCell = 3 + (f - 1);
                        if (ptrCell >= cellCount) ptrCell = cellCount - 1;
                        if (f == 1)
                        {
                            snprintf(val0, sizeof(val0), "Perfect f1");
                            col0 = 0x4CE688FFu;
                        }
                        else
                        {
                            snprintf(val0, sizeof(val0), "%df Late", f - 1);
                            col0 = f <= 3 ? 0xFFE24AFFu : 0xFFB848FFu;
                        }
                    }
                    else if (r.outcome == kWdMissNoAirdodge)
                    {
                        ptrCell = 1;
                        snprintf(val0, sizeof(val0), "Early");
                        col0 = 0xFF6658FFu;
                    }
                    else
                    {
                        ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Miss");
                        col0 = 0xFF6658FFu;
                    }
                    if (r.angleDeg > 0.0f)
                    {
                        snprintf(val1, sizeof(val1), "%.1f deg", static_cast<double>(r.angleDeg));
                    }
                    else
                    {
                        snprintf(val1, sizeof(val1), "--");
                    }
                }
                else
                {
                    snprintf(val0, sizeof(val0), "Ready");
                    snprintf(val1, sizeof(val1), "--");
                }
            }
            else if (d == drills::kDrillLCancel)
            {
                // 15-cell L-Cancel bar: 3 Early (red) | 7f P+ window (green) + cyan hitlag ext | Late (red).
                const LcCoreState* lc = slot >= 0 ? coreDrillLCancelState(slot) : NULL;
                int hitlagExt = (lc != NULL && lc->seq > 0u && lc->last.hitlagFrames > 0) ? lc->last.hitlagFrames : 0;
                if (hitlagExt > 6) hitlagExt = 6;
                cellCount = 15 + hitlagExt;
                for (int i = 0; i < cellCount; i++)
                {
                    if (i < 3) cells[i] = 0xDC4444E8u;
                    else if (i < 10) cells[i] = 0x3CE890FFu;
                    else if (i < 10 + hitlagExt) cells[i] = 0x36DCE8FFu;
                    else cells[i] = 0xDC4444E8u;
                }
                legend[0].swatch = 0xDC4444FFu; legend[0].label = "Early";
                legend[1].swatch = 0x3CE890FFu; legend[1].label = "7f Window";
                if (hitlagExt > 0)
                {
                    legend[2].swatch = 0x36DCE8FFu; legend[2].label = "Hitlag";
                    legend[3].swatch = 0xDC4444FFu; legend[3].label = "Late";
                    legendCount = 4;
                }
                else
                {
                    legend[2].swatch = 0xDC4444FFu; legend[2].label = "Late";
                    legendCount = 3;
                }
                lbl0 = "Timing";
                lbl1 = "Hitlag";
                if (lc != NULL && lc->seq > 0u)
                {
                    const LcResult& r = lc->last;
                    if (r.outcome == kLcSuccessManual)
                    {
                        int pf = r.pressFrame < 1 ? 1 : (r.pressFrame > 7 ? 7 : r.pressFrame);
                        ptrCell = 3 + (pf - 1);
                        snprintf(val0, sizeof(val0), "%df / 7f", pf);
                        col0 = 0x4CE688FFu;
                    }
                    else if (r.outcome == kLcMissEarly)
                    {
                        int ef = r.earlyFrames < 1 ? 1 : r.earlyFrames;
                        ptrCell = 3 - ef;
                        if (ptrCell < 0) ptrCell = 0;
                        snprintf(val0, sizeof(val0), "Early -%df", ef);
                        col0 = 0xFF6658FFu;
                    }
                    else if (r.outcome == kLcMissLate)
                    {
                        int lf = r.lateFrames < 1 ? 1 : r.lateFrames;
                        ptrCell = 10 + hitlagExt + (lf - 1);
                        if (ptrCell >= cellCount) ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Late +%df", lf);
                        col0 = 0xFF6658FFu;
                    }
                    else if (r.outcome == kLcSuccessAuto)
                    {
                        ptrCell = 6;
                        snprintf(val0, sizeof(val0), "Auto ON");
                        col0 = 0xFFB848FFu;
                    }
                    else if (r.outcome == kLcAutoCanceled)
                    {
                        snprintf(val0, sizeof(val0), "Autocancel");
                        col0 = 0xFFE24AFFu;
                    }
                    else
                    {
                        ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Missed");
                        col0 = 0xFF6658FFu;
                    }
                    if (r.hitlagFrames > 0)
                    {
                        snprintf(val1, sizeof(val1), "+%df", static_cast<int>(r.hitlagFrames));
                    }
                    else
                    {
                        snprintf(val1, sizeof(val1), "0f");
                    }
                }
                else
                {
                    snprintf(val0, sizeof(val0), "Ready");
                    snprintf(val1, sizeof(val1), "0f");
                }
            }
            else if (d == drills::kDrillPowershield)
            {
                // 12-cell Powershield bar: 3 Early (red) | 2f P+ PS (cyan-emerald) | 4 Normal (gold) | 3 Hit (red).
                static const gfx::Rgba kPsCells[12] = {
                    0xDC4444E8u, 0xDC4444E8u, 0xDC4444E8u,
                    0x2EE8C8FFu, 0x2EE8C8FFu,
                    0xF0BE38E8u, 0xF0BE38E8u, 0xF0BE38E8u, 0xF0BE38E8u,
                    0xDC4444E8u, 0xDC4444E8u, 0xDC4444E8u
                };
                memcpy(cells, kPsCells, sizeof(kPsCells));
                cellCount = 12;
                legend[0].swatch = 0xDC4444FFu; legend[0].label = "Early";
                legend[1].swatch = 0x2EE8C8FFu; legend[1].label = "2f PS";
                legend[2].swatch = 0xF0BE38FFu; legend[2].label = "Shield";
                legend[3].swatch = 0xDC4444FFu; legend[3].label = "Hit";
                legendCount = 4;
                lbl0 = "Timing";
                lbl1 = "Type";
                const PsSlot* ps = slot >= 0 ? hoverPsPowershieldSlot(slot) : NULL;
                if (ps != NULL && ps->stats.attempts > 0u)
                {
                    const PsResult& r = ps->last;
                    if (r.outcome == kPsPhysical || r.outcome == kPsReflect)
                    {
                        int wf = r.windowFrame < 1 ? 1 : (r.windowFrame > 2 ? 2 : r.windowFrame);
                        ptrCell = 3 + (wf - 1);
                        snprintf(val0, sizeof(val0), "PS f%d", wf);
                        col0 = 0x4CE688FFu;
                        snprintf(val1, sizeof(val1), r.psDropUsed ? "PS-Drop" : (r.outcome == kPsReflect ? "Reflect" : "Physical"));
                    }
                    else if (r.outcome == kPsMissEarly)
                    {
                        int off = r.offsetFrames < 0 ? -r.offsetFrames : r.offsetFrames;
                        ptrCell = 3 - (off < 1 ? 1 : off);
                        if (ptrCell < 0) ptrCell = 0;
                        snprintf(val0, sizeof(val0), "Early -%df", off);
                        col0 = 0xFF6658FFu;
                        snprintf(val1, sizeof(val1), "Shield");
                    }
                    else
                    {
                        int off = r.offsetFrames < 1 ? 1 : r.offsetFrames;
                        ptrCell = 5 + (off - 1);
                        if (ptrCell >= cellCount) ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Late +%df", off);
                        col0 = 0xFFB848FFu;
                        snprintf(val1, sizeof(val1), "Shield");
                    }
                }
                else
                {
                    snprintf(val0, sizeof(val0), "Ready");
                    snprintf(val1, sizeof(val1), "--");
                }
            }
            else if (d == drills::kDrillLedgedash)
            {
                static const gfx::Rgba kLedgeCols[8] = {
                    0x8894A0E0u, 0xE0C040E0u, 0x50C878E0u, 0x4890E0E0u,
                    0xE8EEF4E0u, 0xE05050E0u, 0xA070D0E0u, 0x40D8CCE0u
                };
                legend[0].swatch = 0x8894A0FFu; legend[0].label = "Ledge";
                legend[1].swatch = 0xE0C040FFu; legend[1].label = "Drop";
                legend[2].swatch = 0x50C878FFu; legend[2].label = "Jump";
                legend[3].swatch = 0x4890E0FFu; legend[3].label = "Dodge";
                legend[4].swatch = 0xE8EEF4FFu; legend[4].label = "Land";
                legend[5].swatch = 0x40D8CCFFu; legend[5].label = "GALINT";
                legendCount = 6;
                lbl0 = "GALINT";
                lbl1 = "On Ledge";
                lbl2 = "Angle";
                const LedgeCoreState* lg = slot >= 0 ? coreDrillLedgeState(slot) : NULL;
                if (lg != NULL && lg->seq > 0u && lg->last.cellCount > 0u)
                {
                    const LedgeResult& r = lg->last;
                    int gal = r.galint > 0 ? r.galint : 0;
                    int frames = r.cellCount;
                    int maxC = 54;
                    int first = 0;
                    if (frames + gal > maxC)
                    {
                        int over = frames + gal - maxC;
                        first = over < frames ? over : frames;
                        frames -= first;
                        if (frames + gal > maxC) gal = maxC - frames;
                    }
                    cellCount = frames + gal;
                    if (cellCount < 1) cellCount = 1;
                    for (int i = 0; i < cellCount; i++)
                    {
                        u8 ty = i < frames ? r.cells[first + i] : static_cast<u8>(7);
                        cells[i] = kLedgeCols[ty < 8 ? ty : 6];
                    }
                    ptrCell = frames < cellCount ? frames : (cellCount - 1);
                    if (r.noXlu)
                    {
                        snprintf(val0, sizeof(val0), "None");
                        col0 = 0xFFB848FFu;
                    }
                    else
                    {
                        snprintf(val0, sizeof(val0), r.galint > 0 ? "+%df" : "%df", static_cast<int>(r.galint));
                        col0 = r.galint > 0 ? 0x4CE688FFu : 0xFF6658FFu;
                    }
                    snprintf(val1, sizeof(val1), "%df", static_cast<int>(r.cliffWaitFrames));
                    if (r.airdodgeAngle > 0.0f)
                    {
                        snprintf(val2, sizeof(val2), "%.1f deg", static_cast<double>(r.airdodgeAngle));
                    }
                    else
                    {
                        snprintf(val2, sizeof(val2), "--");
                    }
                }
                else
                {
                    cellCount = 16;
                    for (int i = 0; i < cellCount; i++) cells[i] = 0x1E2C38C0u;
                    snprintf(val0, sizeof(val0), "Ready");
                    snprintf(val1, sizeof(val1), "--");
                    snprintf(val2, sizeof(val2), "--");
                }
            }
            else
            {
                // Unified calm white-label rep/progress timing bar for all other drills & challenges.
                int pips = target > 0 ? static_cast<int>(target) : kDrillFreePips;
                if (pips > kDrillMaxSetReps) pips = kDrillMaxSetReps;
                cellCount = pips;
                for (int i = 0; i < cellCount; i++)
                {
                    u8 p = st.repPips[i];
                    cells[i] = p == 0 ? 0x1E2C38C8u : kPipCols[p < 5 ? p : 0];
                }
                int activePip = target > 0 ? (static_cast<int>(st.repsDone) - 1) : (static_cast<int>(st.freePipCount) - 1);
                if (activePip >= 0 && activePip < cellCount)
                {
                    ptrCell = activePip;
                }
                snprintf(caption, sizeof(caption), "%s  -  %s", drillHudTitle(d), drillModeLabel(st.mode));
            }

            drawTimingBar(cells, cellCount, ptrCell, legendCount > 0 ? legend : NULL, legendCount,
                          legendCount == 0 ? caption : NULL, wide);

            // Zone 2: Panel B Right-Margin Quiet Smoked Stat Plate stacked cleanly under Option A GC controller.
            StatPlateRow rows[3];
            rows[0].label = lbl0; rows[0].value = val0; rows[0].valueColor = col0;
            rows[1].label = lbl1; rows[1].value = val1; rows[1].valueColor = 0xF4F8FCFFu;
            rows[2].label = lbl2; rows[2].value = val2; rows[2].valueColor = 0xF4F8FCFFu;
            drawStatPlate(rows, 3, hudViewerColumnBottom(wide), wide);

            // Floating 2D TARGET label above the Wavedash floor target zone.
            if (wdTargetCue)
            {
                const WdZoneState* wz = drillPracticeWdZone();
                gfCameraManager* camMgr = gfCameraManager::getManager();
                if (wz != NULL && wz->active != 0u && game::isRam(camMgr))
                {
                    Vec3f wp(0.5f * (wz->minX + wz->maxX), wz->y + 10.5f, 0.0f);
                    Vec2f sp(0.0f, 0.0f);
                    camMgr->m_cameras[0].calcProjection3Dto2D(&wp, &sp);
                    float sx = sp.m_x;
                    float sy = sp.m_y;
                    if (sx >= 48.0f && sx <= 592.0f && sy >= 74.0f && sy <= 376.0f)
                    {
                        const char* tag = "TARGET";
                        float tw = gfx::textWidth(tag, kScales[0]);
                        float pad = 5.0f * xs;
                        float bw = tw + 2.0f * pad;
                        drawSmokedPlate(sx - bw * 0.5f, sy - 7.0f, bw, 14.0f);
                        label(sx, sy - 4.0f, tag, 0xFFD300FFu, kAlignCenter);
                    }
                }
            }
        }

        void drawEndModal()
        {
            Runtime& s_rt = *s_rtPtr;
            DrillHudState& st = s_rt.st;
            float xs = s_rt.xs;
            float ox = 320.0f - 192.0f * xs;
            float ix = ox + 4.0f * xs;
            float iw = 376.0f * xs;
            bool timed = st.mode == kDrillModeTimed60;
            u8 target = drillModeTargetReps(st.mode);
            if (!timed && target == 0)
            {
                target = kDrillSetReps10;
            }

            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0x050C10B0u);
            drawPanel(ox, 104.0f, 384.0f, 272.0f, st.newBest ? 0xFFD300FFu : 0x35B8C8FFu, 0x10242EF4u, 0x09151CF4u);

            gfx::fillGradV(ix, 108.0f, iw, 44.0f, 0x3AD0E0FAu, 0x14505CFAu);
            gfx::fillRect(ix, 150.0f, iw, 2.0f, 0x12404AFFu);

            char buf[28];
            snprintf(buf, sizeof(buf), "%s PRACTICE", drillHudTitle(st.activeDrill));
            label(320.0f, 115.0f, buf, 0xFFFFFFFFu, kAlignCenter | kSzDisp13);
            const char* subTitle = "60s RUN COMPLETE";
            if (!timed)
            {
                snprintf(buf, sizeof(buf), "%u-REP SET COMPLETE", static_cast<unsigned>(target));
                subTitle = buf;
            }
            label(320.0f, 136.0f, subTitle, 0xFFF5B8FFu, kAlignCenter);

            unsigned denom = timed ? (st.totalAtt > 0u ? static_cast<unsigned>(st.totalAtt) : 1u)
                                   : static_cast<unsigned>(target);
            unsigned hits = timed ? static_cast<unsigned>(st.totalHit)
                                  : (st.totalHit < target ? static_cast<unsigned>(st.totalHit) : static_cast<unsigned>(target));
            unsigned pct = (timed && st.totalAtt == 0u) ? 0u : (hits * 100u) / denom;
            snprintf(buf, sizeof(buf), "%u / %u  (%u%%)", hits, timed ? static_cast<unsigned>(st.totalAtt) : denom, pct);
            u8 scFl = kAlignCenter | kSzDisp20 | (pct >= 80u ? kDispGold : (pct < 50u ? kDispRed : 0));
            label(320.0f, 166.0f, buf, pct >= 80u ? 0xFFE866FFu : (pct >= 50u ? 0xFFFFFFFFu : 0xFF9E92FFu), scFl);

            if (st.newBest)
            {
                float pw = 92.0f * xs;
                gfx::fillGradV(320.0f - pw * 0.5f, 196.0f, pw, 18.0f, 0xFFE866FFu, 0xE89B00FFu);
                label(320.0f, 200.0f, "NEW BEST!", 0x1A1000FFu, kAlignCenter);
            }
            else
            {
                float pw = 220.0f * xs;
                drawPips(320.0f - pw * 0.5f, 202.0f, pw, 6.0f, timed ? kDrillFreePips : static_cast<int>(target));
            }

            float rowX = ox + 20.0f * xs;
            float rowW = 344.0f * xs;
            float lx = ox + 30.0f * xs;
            float rx = ox + 354.0f * xs;
            float ry = 226.0f;
            for (int r = 0; r < 3; r++, ry += 28.0f)
            {
                gfx::fillGradV(rowX, ry, rowW, 24.0f, 0x132834E8u, 0x0B1820E8u);
                gfx::fillRect(rowX, ry + 22.0f, rowW, 2.0f, 0x060D12C0u);
                const char* lbl = r == 0 ? "Personal Best" : (r == 1 ? "Best Streak" : drillHighlightLabel(st.activeDrill));
                const char* val = buf;
                u32 vc = 0xF0F4F8FFu;
                if (r == 0)
                {
                    if (st.sessionBest)
                    {
                        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(st.sessionBest));
                    }
                    else
                    {
                        val = "--";
                    }
                    if (st.newBest)
                    {
                        vc = 0xFFD300FFu;
                    }
                }
                else if (r == 1)
                {
                    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(st.bestStreakFull));
                }
                else
                {
                    drillHudFormatHighlight(st, buf, sizeof(buf));
                    vc = 0x3AD0E0FAu;
                }
                label(lx, ry + 7.0f, lbl, kGradeCols[0], kSz10);
                label(rx, ry + 7.0f, val, vc, kSz10 | kAlignRight);
            }

            gfx::fillRect(ix, 322.0f, iw, 2.0f, 0x2A5866E0u);
            gfx::fillGradV(ix, 324.0f, iw, 48.0f, 0x122630F2u, 0x08131AF2u);

            u8 ct = s_rt.lastPadType < 4 ? s_rt.lastPadType : 0;
            float bw = 16.0f * xs;
            float px = ox + 28.0f * xs;
            float step = 100.0f * xs;
            for (int p = 0; p < 3; p++, px += step)
            {
                if (!uiAssetsDrawIcon(kPromptIcons[p][ct], px, 340.0f, bw, 16.0f, 0xFFFFFFFFu))
                {
                    gfx::fillRect(px, 340.0f, bw, 16.0f, 0x35B8C8FFu);
                    label(px + bw * 0.5f, 344.0f, drillNthStr(kPromptGlyphs, p), 0xF0F4F8FFu, kAlignCenter);
                }
                label(px + bw + 5.0f * xs, 343.0f, drillNthStr(kPromptLabels, p), 0xF0F4F8FFu, kSz10);
            }
        }
    }

    void drillHudAttach()
    {
        freeNetPtr(s_rtPtr);
        s_rtPtr = allocNetArray<Runtime>(1);
        if (s_rtPtr == NULL)
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        s_rt.fresh = 1;
        s_rt.sess = drills::sessionIn(main::services().settings());

        u8 id = (s_rt.sess && isValidSkillDrill(s_rt.sess->drill)) ? s_rt.sess->drill
                                                                   : static_cast<u8>(drills::kDrillNone);
        drillHudInit(s_rt.st, id, id ? s_rt.sess->best[id] : 0u);
        drillPracticeSetActiveDrill(id);
        ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
        pm->activeDrill = id < kPracticeDrillCount ? id : static_cast<u8>(drills::kDrillNone);
        pm->mode = s_rt.st.mode < 3u ? s_rt.st.mode : 0u;
        if (id)
        {
            s_rt.sess->drillRan = 1;
            ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
        }
        syncDebug();
        TM_LOG("drillhud: debug block at %08x\n", &s_rt.debug);
    }

    void drillHudDetach()
    {
        if (s_rtPtr == NULL)
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        unpauseIfNeeded(s_rt);
        s_rt.st.modalOpen = 0;
        syncDebug();
        freeNetPtr(s_rtPtr);
    }

    void drillHudSavestateReset()
    {
        if (s_rtPtr != NULL)
        {
            s_rtPtr->synced = 0;
        }
    }

    void drillHudFrame()
    {
        if (s_rtPtr == NULL)
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        DrillHudState& st = s_rt.st;
        bool saveBest = false;
        if (s_rt.debug.forceComplete)
        {
            s_rt.debug.forceComplete = 0;
            if (isValidSkillDrill(st.activeDrill) && !st.modalOpen)
            {
                drillHudComplete(st);
                s_rt.waitRelease = 1;
                saveBest = true;
            }
        }
        u8 d = st.activeDrill;
        if (!isValidSkillDrill(d))
        {
            syncDebug();
            return;
        }
        int slot = -1;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t && !t->partner && (!t->cpu || slot < 0))
            {
                slot = i;
                if (!t->cpu)
                {
                    break;
                }
            }
        }
        if (slot >= 0)
        {
            DrillEngineFeed feed;
            memset(&feed, 0, sizeof(feed));
            const game::Tracked* tr = game::tracked(slot);
            switch (d)
            {
            case drills::kDrillLedgedash:
                feed.ledge = coreDrillLedgeState(slot);
                break;
            case drills::kDrillWavedash:
                feed.wd = coreDrillWavedashState(slot);
                break;
            case drills::kDrillLCancel:
                feed.lc = coreDrillLCancelState(slot);
                feed.autoLCancel = coreDrillAutoLCancelActive();
                break;
            case drills::kDrillHoverFloat:
                feed.fd = hoverPsFloatDjcSlot(slot);
                if (tr != NULL)
                {
                    int ftKind = game::fighterKind(tr->entryId, tr->partner);
                    feed.ineligibleFighter = !isFloatCapable(ftKind) && !isDjcCapable(ftKind);
                }
                break;
            case drills::kDrillPowershield:
                feed.ps = hoverPsPowershieldSlot(slot);
                break;
            case drills::kDrillRar:
                feed.rar = pplusTechRar(slot);
                break;
            case drills::kDrillDacus:
                feed.dacus = pplusTechDacus(slot);
                break;
            case drills::kDrillBReverse:
                feed.brev = pplusTechBReverse(slot);
                break;
#if TM_STAGE4C
            case drills::kDrillCancelSprint:
                feed.cs = challengeCancelSprint(slot);
                if (tr != NULL)
                {
                    feed.ineligibleFighter = cancelSprintSpec(game::fighterKind(tr->entryId, tr->partner)) == NULL;
                }
                break;
            case drills::kDrillFlowCourse:
                feed.fc = challengeFlowCourse(slot);
                break;
            case drills::kDrillReactionLab:
            case drills::kDrillTechchaseEscape:
                feed.rt = challengeReactionTechchase(slot);
                break;
            case drills::kDrillRecoverySweetspot:
            case drills::kDrillFloorhug:
                feed.rf = challengeRecoveryFloorhug(slot);
                break;
            case drills::kDrillSnakeRecovery:
                feed.snk = challengeSnakeRecovery(slot);
                if (tr != NULL)
                {
                    feed.ineligibleFighter = game::fighterKind(tr->entryId, tr->partner) != kSnkFtSnake;
                }
                break;
#endif
            default:
                break;
            }
            bool sync = !s_rt.synced;
            if (sync)
            {
                s_rt.synced = 1;
            }
            bool modalTriggered = false;
            if ((sync || !st.modalOpen) && drillHudPoll(st, feed, sync) && !sync)
            {
                saveBest = true;
                modalTriggered = st.modalOpen != 0;
            }
            if (!st.modalOpen && drillHudTickTimer(st))
            {
                saveBest = true;
                modalTriggered = true;
            }
            if (modalTriggered)
            {
                s_rt.waitRelease = 1;
                ui::menuSound(game::kSoundConfirm);
            }
        }
        if (saveBest && s_rt.sess)
        {
            u16 m = drills::betterBest(d, s_rt.sess->best[d], st.sessionBest);
            s_rt.sess->best[d] = m;
            st.sessionBest = m;
        }
        syncDebug();
    }

    u32 drillHudHook(u32 phase, gfPadStatus* pads)
    {
        if (s_rtPtr == NULL)
        {
            return 0u;
        }
        Runtime& s_rt = *s_rtPtr;
        DrillHudState& st = s_rt.st;
        if (pads == NULL)
        {
            bool active = isValidSkillDrill(st.activeDrill);
            bool tc = scenarioPresetsWantsDraw(game::matchState());
            bool topHud = active || tc;
            if (phase == 2u && topHud)
            {
                bool wide = gfx::widescreen();
                ui::layout::Box b = ui::layout::drillHudBox(wide);
                if (active && st.activeDrill == drills::kDrillLedgedash)
                {
                    int cells = 16;
                    int slot = primaryTrackedSlot();
                    const LedgeCoreState* lg = slot >= 0 ? coreDrillLedgeState(slot) : NULL;
                    if (lg != NULL && lg->seq > 0u && lg->last.cellCount > 0u)
                    {
                        int gal = lg->last.galint > 0 ? lg->last.galint : 0;
                        cells = static_cast<int>(lg->last.cellCount) + gal;
                        if (cells > 54)
                        {
                            cells = 54;
                        }
                        if (cells < 1)
                        {
                            cells = 1;
                        }
                    }
                    b = ui::layout::drillLedgedashBand(cells, wide);
                }
                ui::layout::setLabBand(b);
            }
            const WdZoneState* wz = drillPracticeWdZone();
            bool wdZone = active && st.activeDrill == drills::kDrillWavedash && drillPracticeConfig().wdTargetZone != 0u
                          && wz != NULL && wz->active != 0u;
#if TM_STAGE4C
            bool recGuide = active && st.activeDrill == drills::kDrillRecoverySweetspot;
#else
            bool recGuide = false;
#endif
            return (topHud ? 1u : 0u) | (st.modalOpen ? 2u : 0u) | (drillPracticeClaimsDpadLR() ? 4u : 0u)
                   | ((wdZone || recGuide) ? 8u : 0u);
        }
        if (phase == iface::kPadsSystem)
        {
            bool anyHeld = false;
            bool sawConn = false;
            int act = 0;
            for (int i = 0; i < kPorts; i++)
            {
                gfPadStatus& p = pads[i];
                bool conn = game::padConnectedInMatch(i, p);
                u32 held = conn ? p.m_buttonsCurrentFrame.bits : 0u;
                u32 pr = (s_rt.fresh || !conn) ? 0u : (held & ~s_rt.prevHeld[i]);
                s_rt.prevHeld[i] = held;
                s_rt.releaseMask[i] &= held;
                if (conn && held)
                {
                    anyHeld = true;
                }
                if (conn && !sawConn)
                {
                    sawConn = true;
                    s_rt.lastPadType = static_cast<u8>(p.m_controllerType);
                }
                if (st.modalOpen && !s_rt.waitRelease && !act && pr)
                {
                    act = (pr & game::kPadA) ? 1 : (pr & game::kPadStart) ? 2 : (pr & game::kPadB) ? 3 : 0;
                    if (act)
                    {
                        s_rt.lastPadType = static_cast<u8>(p.m_controllerType);
                    }
                }
                else if (!st.modalOpen && (isValidSkillDrill(st.activeDrill) || comboBreakRushActive())
                         && (pr & game::kPadDUp) != 0 && (held & (game::kPadDDown | game::kPadR)) == 0)
                {
                    u32 ct = static_cast<u32>(p.m_controllerType);
                    u32 modBit = ct == gfPadType::WIIMOTE ? game::kPadWiiMinus : game::inMatchPad(ct).prevCat;
                    if (modBit != 0 && (held & modBit) != 0)
                    {
                        if (comboBreakRushActive())
                        {
                            comboBreakRushCycleMode();
                            s_rt.releaseMask[i] |= game::kPadDUp;
                        }
                        else
                        {
                            drillHudCycleMode(st);
                            ui::pages::practiceMenuState()->mode = st.mode < 3u ? st.mode : 0u;
                            s_rt.synced = 0;
                            s_rt.releaseMask[i] |= game::kPadDUp;
#if TM_STAGE4C
                            if (st.activeDrill == drills::kDrillFloorhug)
                            {
                                challengeBackendsInterruptTaunts();
                            }
#endif
                            ui::menuSound(game::kSoundMove);
                            syncDebug();
                        }
                    }
                }
            }
            s_rt.fresh = 0;

            if (st.modalOpen)
            {
                if (s_rt.waitRelease && !anyHeld)
                {
                    s_rt.waitRelease = 0;
                }
                else if (act)
                {
                    memcpy(s_rt.releaseMask, s_rt.prevHeld, sizeof(s_rt.releaseMask));
                    unpauseIfNeeded(s_rt);
                    if (act == 1)
                    {
                        drillHudRetry(st);
                        drillPracticeReset();
#if TM_STAGE4C
                        if (isChallengeDrill(st.activeDrill))
                        {
                            challengeBackendsReset();
                        }
#endif
                        s_rt.synced = 0;
                    }
                    else
                    {
                        st.modalOpen = 0;
                        if (act == 2)
                        {
                            st.activeDrill = drills::kDrillNone;
                            ui::pages::practiceMenuState()->activeDrill = drills::kDrillNone;
                            drillPracticeSetActiveDrill(drills::kDrillNone);
                            if (s_rt.sess)
                            {
                                s_rt.sess->drill = drills::kDrillNone;
                                s_rt.sess->drillRan = 0;
                            }
                        }
                        else
                        {
                            if (s_rt.sess)
                            {
                                s_rt.sess->drillRan = 1;
                            }
                            lab::exitMatch();
                        }
                    }
                    ui::menuSound(act == 3 ? game::kSoundBack : game::kSoundConfirm);
                }
                syncDebug();
            }
        }
        else if (phase == iface::kPadsGame)
        {
            bool m = st.modalOpen != 0;
            if (m != (s_rt.pausedByUs != 0))
            {
                game::setGameplayPaused(m);
                s_rt.pausedByUs = m ? 1 : 0;
            }
            if (m)
            {
                game::trackerStepRuns(false);
            }
        }

        bool modal = st.modalOpen != 0;
        if (!modal)
        {
            drillPracticeOnPad(phase, pads);
        }
        for (int i = 0; i < kPorts; i++)
        {
            u32 mask = modal ? 0xFFFFFFFFu : s_rt.releaseMask[i];
            if (mask)
            {
                gfPadStatus& p = pads[i];
                game::clearPadBits(p, mask);
                if (modal)
                {
                    p.m_lTriggerAnalog = p.m_rTriggerAnalog = 0;
                    p.m_stickX = p.m_stickY = 0;
                    p.m_subStickX = p.m_subStickY = 0;
                }
            }
        }
        return modal ? 1u : 0u;
    }

    void drillHudDraw(const game::MatchState& match)
    {
        if (s_rtPtr == NULL || !match.running || match.paused || lab::menuOpen()
            || !isValidSkillDrill(s_rtPtr->st.activeDrill) || gfx::lineHeight(0.5f) <= 0.0f)
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        s_rt.xs = ui::layout::xsOf(gfx::widescreen());
        if (s_rt.st.modalOpen)
        {
            drawEndModal();
        }
        else
        {
            drawLiveHud();
        }
        s_rt.debug.drawCalls++;
    }

    u8 drillHudActiveId()
    {
        return s_rtPtr != NULL ? s_rtPtr->st.activeDrill : static_cast<u8>(drills::kDrillNone);
    }

    bool drillHudModalOpen()
    {
        return s_rtPtr != NULL && s_rtPtr->st.modalOpen != 0;
    }

    void drillHudSetMode(u8 mode)
    {
        if (s_rtPtr == NULL || !isValidSkillDrill(s_rtPtr->st.activeDrill))
        {
            return;
        }
        u8 clean = mode < kDrillModeCount ? mode : static_cast<u8>(kDrillModeFree);
        Runtime& s_rt = *s_rtPtr;
        ui::pages::practiceMenuState()->mode = clean < 3u ? clean : 0u;
        if (clean != s_rt.st.mode)
        {
            drillHudInitMode(s_rt.st, s_rt.st.activeDrill, s_rt.st.sessionBest, clean);
            ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
            s_rt.synced = 0;
            syncDebug();
        }
    }

    void drillHudDisable()
    {
        comboBreakRushSetActive(kRushOff);
        ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
        if (s_rtPtr == NULL)
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        unpauseIfNeeded(s_rt);
        drillHudInit(s_rt.st, static_cast<u8>(drills::kDrillNone), 0u);
        drillPracticeSetActiveDrill(kPracticeDrillNone);
        ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
        pm->activeDrill = static_cast<u8>(drills::kDrillNone);
        pm->mode = static_cast<u8>(kDrillModeFree);
        syncDebug();
    }
}
}
