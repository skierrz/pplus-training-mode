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
#include "main/ui/style.h"
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

        const u32 kGradeCols[5] = {0xA0BCC4FFu, kHudGold, kHudGreen, kHudRed, kHudWarn};
        const u32 kPipCols[5] = {0x20343EFFu, kHudGold, kHudGreen, kHudRed, kHudWarn};
        const float kScales[5] = {
            ui::style::kTypeMicro / ui::style::kCapHeight,
            ui::style::kTypeBody / ui::style::kCapHeight,
            ui::style::kTypeMeta / ui::style::kDisplayCapHeight,
            ui::style::kTypeHeading / ui::style::kDisplayCapHeight,
            ui::style::kTypeHero / ui::style::kDisplayCapHeight
        };
        const char kPromptLabels[] = "Retry\0Lab\0Return to CSS";

        // Single consolidated runtime block avoids per-static PowerPC -sdata 0 address loads.
        // Motion state (ptrVisual, hitFlashTimer, modalAnim, modalNewBestPop, prevMotionDrill)
        // is strictly 8 bytes total with zero heap allocations and zero idle pulsing.
        struct Runtime {
            DrillHudDebugBlock debug;
            DrillHudState st;
            DrillHudState modalSnap; // Last completed summary kept for 4f Retry fade-out after st resets.
            drills::Session* sess;
            u32 prevHeld[kPorts];
            u32 releaseMask[kPorts];
            float xs;
            float ptrVisual;    // 4 B: smoothed timing-bar pointer cell (< 0 = hidden/unset)
            u8 hitFlashTimer;   // 1 B: 5..0f landed-cell brightness/gold flash on hit
            u8 modalAnim;       // 1 B: 0..8f modal reveal (steps -2/f on exit -> 4f exit)
            u8 modalNewBestPop; // 1 B: 5..0f focal pop strictly on New Best! badge
            u8 prevMotionDrill; // 1 B: active drill id to snap pointer on drill switch
            u8 waitRelease;
            u8 lastPadType;
            u8 fresh;
            u8 synced;
            u8 pausedByUs;
        };

        static Runtime* s_rtPtr = NULL;

        inline gfx::Rgba scaleAlpha(gfx::Rgba col, float a)
        {
            if (a >= 0.995f)
            {
                return col;
            }
            if (a <= 0.0f)
            {
                return col & 0xFFFFFF00u;
            }
            u32 al = static_cast<u32>(f2i(i2f(col & 0xFFu) * a + 0.5f));
            if (al > 255u) al = 255u;
            return (col & 0xFFFFFF00u) | al;
        }

        inline float easeOutCubic(float t)
        {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            float u = 1.0f - t;
            return 1.0f - u * u * u;
        }

        void resetMotionState(Runtime& s_rt, bool clearModal)
        {
            s_rt.ptrVisual = -1.0f;
            s_rt.hitFlashTimer = 0u;
            if (clearModal)
            {
                s_rt.modalAnim = 0u;
                s_rt.modalNewBestPop = 0u;
                s_rt.modalSnap.activeDrill = 0u;
            }
        }

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

        // Unified 3 px (F4) outer modal frame + dark gradient body matching style.h.
        void drawPanel(float ox, float oy, float w, float h, u32 top, u32 bot, float alpha = 1.0f)
        {
            float xs = s_rtPtr->xs;
            float bw = w * xs;
            float b = ui::style::kLabBorder;
            float bx = b * xs;
            gfx::fillRect(ox, oy, bw, h, scaleAlpha(ui::style::kLabBorderColor, alpha));
            gfx::fillGradV(ox + bx, oy + b, bw - 2.0f * bx, h - 2.0f * b, scaleAlpha(top, alpha),
                           scaleAlpha(bot, alpha));
        }

        // Compact text renderer supporting system and Brawl Display fonts with optional fade alpha.
        void label(float x, float y, const char* s, u32 col, u8 fl, float alpha = 1.0f)
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
                gfx::displayText(dx, ui::style::displayYForCap(y, sc), s, scaleAlpha(col, alpha),
                                 scaleAlpha(bot, alpha), sx, sc, 1.2f, scaleAlpha(0x050C12FFu, alpha));
            }
            else
            {
                gfx::text(dx, ui::style::textYForCap(y, sc), s, scaleAlpha(col, alpha), sx, sc);
            }
        }

        void drawPips(float x, float y, float totalW, float h, int count, const u8* pips, float alpha = 1.0f)
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
                u8 p = pips[k];
                gfx::fillRect(x, y, pw, h, scaleAlpha(kPipCols[p < 5 ? p : 0], alpha));
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

        void formatPanelBFeedback(const char* fb, char* out, size_t outSize)
        {
            static const struct {
                const char* from;
                const char* to;
            } kMap[] = {
                {"CC / ASDI-DN ", "CC "},      {"PERFECT CANCEL ", "Perfect "},
                {"GATLING DACUS ", "Gatling "}, {"SWEETSPOT ", "Sweet "},
                {"INSTANT DJC ", "DJC "},       {"C4 REFRESH ", "C4 "},
                {"NADE REFRESH ", "Nade "},     {"PERFECT ESC ", "Perfect "},
                {"RECOVERED ", "Clear "},       {"COURSE ", "Clear "},
                {"HIGH OVERSHOOT", "Overshoot"}, {"MISSED ASDI-DN", "Missed"},
                {"BROKEN TUMBLE", "Tumble"},    {"SD / MISSED LEDGE", "SD / Miss"},
                {"ASDI-DOWN TECH", "ASDI Tech"}, {"TRUE PUNISH!", "Punish!"},
                {"DROPPED FLOAT", "Dropped"},   {"FLOAT CANCEL", "Float Ok"},
                {"HOVER CANCEL", "Hover Ok"},   {"TURNAROUND B", "Turn-B"},
                {"CANCEL MISS", "Missed"},      {"DACUS MISS", "Missed"},
                {"RAR MISS", "Missed"},         {"INTO TARGET", "Target"},
                {"READY", "Ready"}
            };
            for (u32 i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++)
            {
                size_t len = strlen(kMap[i].from);
                if (strncmp(fb, kMap[i].from, len) == 0)
                {
                    snprintf(out, outSize, "%s%s", kMap[i].to, fb + len);
                    return;
                }
            }
            if (strncmp(fb, "GATE ", 5) == 0)
            {
                snprintf(out, outSize, "Gate %.3s", fb + 5);
                return;
            }
            snprintf(out, outSize, "%s", fb);
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
            else
            {
                formatPanelBFeedback(fb, val0, sizeof(val0));
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

            // Zone 1: Top-Center Timing / State Bar (symmetric Early / Title / Late) or Rep-History Strip.
            gfx::Rgba cells[64];
            int cellCount = 15;
            int ptrCell = -1;
            TimingLegendItem legend[6];
            int legendCount = 0;
            const char* leftLbl = NULL;
            const char* rightLbl = NULL;
            char caption[40];
            caption[0] = '\0';
            TimingBarFx fx;
            memset(&fx, 0, sizeof(fx));
            fx.pointerPos = -1.0f;
            fx.flashCell = -1;
            fx.tickStartCell = -1;
            fx.tickEndCell = -1;

            if (d == drills::kDrillWavedash)
            {
                // Symmetric 15-cell Wavedash bar: 7 Early (red->coral) | center cell 7 = Perfect f1 | 7 Late (lime->gold->red).
                static const gfx::Rgba kWdCells[15] = {
                    0xD43E44FFu, 0xD84440FFu, 0xDC4C3CFFu, 0xE05A38FFu, 0xE66E36FFu, 0xEC8434FFu, 0xF09C36FFu,
                    0x3CE890FFu,
                    0x88E260FFu, 0xBCE048FFu, 0xE8D43CFFu, 0xF2B236FFu, 0xEC8434FFu, 0xE25E3AFFu, 0xD43E44FFu
                };
                memcpy(cells, kWdCells, sizeof(kWdCells));
                cellCount = 15;
                leftLbl = "Early";
                rightLbl = "Late";
                snprintf(caption, sizeof(caption), "Wavedash");
                lbl0 = "Timing";
                lbl1 = "Angle";
                const WdCoreState* wd = slot >= 0 ? coreDrillWavedashState(slot) : NULL;
                if (wd != NULL && wd->seq > 0u)
                {
                    const WdResult& r = wd->last;
                    ptrCell = wdTimingBarPointerCell(r, cellCount);
                    if (r.outcome == kWdOutcomeSuccess)
                    {
                        int f = (r.kind == kWdKindWaveland) ? r.airdodgeAirFrames : r.jumpsquatToDodge;
                        if (f < 1) f = 1;
                        if (f == 1)
                        {
                            snprintf(val0, sizeof(val0), "Perfect f1");
                            col0 = kHudGreen;
                        }
                        else
                        {
                            snprintf(val0, sizeof(val0), "%df Late", f - 1);
                            col0 = f <= 3 ? kHudYellow : kHudWarn;
                        }
                    }
                    else if (r.outcome == kWdMissNoAirdodge)
                    {
                        if (r.earlyGuardFrames > 0)
                        {
                            snprintf(val0, sizeof(val0), "Early -%df", static_cast<int>(r.earlyGuardFrames));
                        }
                        else if (r.earlyGuardInSquat != 0)
                        {
                            snprintf(val0, sizeof(val0), "Early");
                        }
                        else
                        {
                            snprintf(val0, sizeof(val0), "No Dodge");
                        }
                        col0 = kHudRed;
                    }
                    else if (r.outcome == kWdMissLateAirdodge)
                    {
                        snprintf(val0, sizeof(val0), "Late Dodge");
                        col0 = kHudRed;
                    }
                    else if (r.outcome == kWdMissUpwardAirdodge)
                    {
                        snprintf(val0, sizeof(val0), "Air Dodge");
                        col0 = kHudRed;
                    }
                    else if (r.outcome == kWdMissZairTether)
                    {
                        snprintf(val0, sizeof(val0), "Zair (L/R)");
                        col0 = kHudWarn;
                    }
                    else
                    {
                        snprintf(val0, sizeof(val0), "Miss");
                        col0 = kHudRed;
                    }
                    if (r.angleDeg > 0.0f)
                    {
                        float ang = static_cast<int>(r.angleDeg * 10.0f) * 0.1f;
                        snprintf(val1, sizeof(val1), "%.1f deg", static_cast<double>(ang));
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
                // Chronological L-Cancel bar: 4 Early (red) | cyan hitlag freeze ext | 7f P+ window (green, 7f->1f) | 4 Late (red).
                const LcCoreState* lc = slot >= 0 ? coreDrillLCancelState(slot) : NULL;
                int hitlagExt = (lc != NULL && lc->seq > 0u && lc->last.hitlagFrames > 0) ? lc->last.hitlagFrames : 0;
                if (hitlagExt > 6) hitlagExt = 6;
                cellCount = 15 + hitlagExt;
                for (int i = 0; i < cellCount; i++)
                {
                    if (i < 4) cells[i] = 0xDC4444FFu;
                    else if (i < 4 + hitlagExt) cells[i] = 0x36DCE8FFu;
                    else if (i < 11 + hitlagExt) cells[i] = 0x3CE890FFu;
                    else cells[i] = 0xDC4444FFu;
                }
                leftLbl = "Early";
                rightLbl = "Late";
                snprintf(caption, sizeof(caption), hitlagExt > 0 ? "L-Cancel +HL" : "L-Cancel");
                fx.tickStartCell = 4 + hitlagExt;
                fx.tickEndCell = 10 + hitlagExt;
                fx.tickStartLabel = "7f";
                fx.tickEndLabel = "1f";
                lbl0 = "Timing";
                lbl1 = "Hitlag";
                if (lc != NULL && lc->seq > 0u)
                {
                    const LcResult& r = lc->last;
                    if (r.outcome == kLcSuccessManual)
                    {
                        int pf = r.pressFrame < 1 ? 1 : (r.pressFrame > 7 ? 7 : r.pressFrame);
                        // pf=7 is 7f before landing (earliest valid, leftmost green);
                        // pf=1 is 1f before landing (latest valid, rightmost green next to Late +1f).
                        ptrCell = 4 + hitlagExt + (7 - pf);
                        snprintf(val0, sizeof(val0), "Frame %d/7", pf);
                        col0 = kHudGreen;
                    }
                    else if (r.outcome == kLcMissEarly)
                    {
                        int ef = r.earlyFrames < 1 ? 1 : r.earlyFrames;
                        ptrCell = 4 - ef;
                        if (ptrCell < 0) ptrCell = 0;
                        snprintf(val0, sizeof(val0), "Early -%df", ef);
                        col0 = kHudRed;
                    }
                    else if (r.outcome == kLcMissLate)
                    {
                        int lf = r.lateFrames < 1 ? 1 : r.lateFrames;
                        ptrCell = 11 + hitlagExt + (lf - 1);
                        if (ptrCell >= cellCount) ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Late +%df", lf);
                        col0 = kHudRed;
                    }
                    else if (r.outcome == kLcSuccessAuto)
                    {
                        ptrCell = 4 + hitlagExt + 3;
                        snprintf(val0, sizeof(val0), "Auto ON");
                        col0 = kHudWarn;
                    }
                    else if (r.outcome == kLcAutoCanceled)
                    {
                        snprintf(val0, sizeof(val0), "Autocancel");
                        col0 = kHudYellow;
                    }
                    else
                    {
                        ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Missed");
                        col0 = kHudRed;
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
                // Chronological 14-cell Powershield bar:
                // 6 Early Shield (amber/gold) | centered 2f P+ PS (cyan-emerald, f2/f3 left, f1 right) | 6 Late Hit (crimson).
                static const gfx::Rgba kPsCells[14] = {
                    0xD88228FFu, 0xDF8E2CFFu, 0xE59B30FFu, 0xEAA834FFu, 0xEEB436FFu, 0xF0BE38FFu,
                    0x2EE8C8FFu, 0x2EE8C8FFu,
                    0xEC624CFFu, 0xE85446FFu, 0xE24A42FFu, 0xDC4444FFu, 0xD43E44FFu, 0xC83640FFu
                };
                memcpy(cells, kPsCells, sizeof(kPsCells));
                cellCount = 14;
                leftLbl = "Early";
                rightLbl = "Late";
                snprintf(caption, sizeof(caption), "Powershield");
                lbl0 = "Timing";
                lbl1 = "Type";
                const PsSlot* ps = slot >= 0 ? hoverPsPowershieldSlot(slot) : NULL;
                if (ps != NULL && ps->stats.attempts > 0u)
                {
                    const PsResult& r = ps->last;
                    if (r.outcome == kPsPhysical || r.outcome == kPsReflect)
                    {
                        int wf = r.windowFrame < 1 ? 1 : (r.windowFrame > 3 ? 3 : r.windowFrame);
                        // wf=2/3 is earlier inside the PS window (next to Early -1f); wf=1 is latest (next to Late +1f).
                        ptrCell = (wf <= 1) ? 7 : 6;
                        snprintf(val0, sizeof(val0), "PS f%d", wf);
                        col0 = kHudGreen;
                        const char* typeStr = r.psDropUsed ? "PS-Drop"
                                                           : (r.outcome == kPsReflect ? "Reflect"
                                                                                      : (r.lightToHard ? "Light-Hard"
                                                                                                       : "Physical"));
                        snprintf(val1, sizeof(val1), "%s", typeStr);
                    }
                    else if (r.outcome == kPsMissBuffered)
                    {
                        ptrCell = 5;
                        snprintf(val0, sizeof(val0), "Buffered");
                        col0 = kHudWarn;
                        snprintf(val1, sizeof(val1), "No Hard");
                    }
                    else if (r.outcome == kPsMissEarly)
                    {
                        int off = r.offsetFrames < 0 ? -r.offsetFrames : r.offsetFrames;
                        ptrCell = 6 - (off < 1 ? 1 : off);
                        if (ptrCell < 0) ptrCell = 0;
                        snprintf(val0, sizeof(val0), "Early -%df", off);
                        col0 = kHudWarn;
                        snprintf(val1, sizeof(val1), "Shield");
                    }
                    else
                    {
                        int off = r.offsetFrames < 1 ? 1 : r.offsetFrames;
                        ptrCell = 8 + (off - 1);
                        if (ptrCell >= cellCount) ptrCell = cellCount - 1;
                        snprintf(val0, sizeof(val0), "Late +%df", off);
                        col0 = kHudRed;
                        snprintf(val1, sizeof(val1), "Hit");
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
                        col0 = kHudWarn;
                    }
                    else
                    {
                        snprintf(val0, sizeof(val0), r.galint > 0 ? "+%df" : "%df", static_cast<int>(r.galint));
                        col0 = r.galint > 0 ? kHudGreen : kHudRed;
                    }
                    snprintf(val1, sizeof(val1), "%df", static_cast<int>(r.cliffWaitFrames));
                    if (r.airdodgeAngle > 0.0f)
                    {
                        float ang = static_cast<int>(r.airdodgeAngle * 10.0f) * 0.1f;
                        snprintf(val2, sizeof(val2), "%.1f deg", static_cast<double>(ang));
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
                // Distinct discrete streak/rep pip strip (no timing pointer, no Early/Late framing).
                int pips = target > 0 ? static_cast<int>(target) : kDrillFreePips;
                if (pips > kDrillMaxSetReps) pips = kDrillMaxSetReps;
                cellCount = pips;
                for (int i = 0; i < cellCount; i++)
                {
                    u8 p = st.repPips[i];
                    cells[i] = p == 0 ? kHudCellEmpty : kPipCols[p < 5 ? p : 0];
                }
                fx.isRepStrip = true;
                fx.repTag = target > 0 ? "Reps" : "Streak";
                if (st.mode == kDrillModeFree)
                {
                    snprintf(caption, sizeof(caption), "%s", drillHudTitle(d));
                }
                else
                {
                    snprintf(caption, sizeof(caption), "%s  -  %s", drillHudTitle(d), drillModeLabel(st.mode));
                }
            }

            // 60fps timing bar pointer glide (4-5f, alpha=0.45) + 5f landed-cell hit flash.
            // Snaps instantly when game::frozen() (Frame Advance) or switching drills.
            if (s_rt.prevMotionDrill != d)
            {
                s_rt.prevMotionDrill = d;
                s_rt.ptrVisual = -1.0f;
                s_rt.hitFlashTimer = 0u;
            }
            if (!fx.isRepStrip && ptrCell >= 0)
            {
                float targetPtr = i2f(ptrCell);
                if (s_rt.ptrVisual < 0.0f || game::frozen())
                {
                    s_rt.ptrVisual = targetPtr;
                }
                else
                {
                    float dp = targetPtr - s_rt.ptrVisual;
                    if (dp > -0.04f && dp < 0.04f)
                    {
                        s_rt.ptrVisual = targetPtr;
                    }
                    else
                    {
                        s_rt.ptrVisual += dp * 0.45f;
                    }
                }
                fx.pointerPos = s_rt.ptrVisual;
                if (game::frozen())
                {
                    s_rt.hitFlashTimer = 0u;
                }
                else if (s_rt.hitFlashTimer > 0u)
                {
                    fx.flashCell = ptrCell;
                    fx.flashAlpha = i2f(s_rt.hitFlashTimer) * 0.2f;
                    s_rt.hitFlashTimer--;
                }
            }
            else
            {
                s_rt.ptrVisual = -1.0f;
                s_rt.hitFlashTimer = 0u;
            }

            drawTimingBar(cells, cellCount, ptrCell, legendCount > 0 ? legend : NULL, legendCount,
                          legendCount == 0 ? caption : NULL, wide, leftLbl, rightLbl, &fx);

            // Zone 2: Panel B Right-Margin Quiet Smoked Stat Plate stacked cleanly under Option A GC controller.
            StatPlateRow rows[3];
            rows[0].label = lbl0; rows[0].value = val0; rows[0].valueColor = col0;
            rows[1].label = lbl1; rows[1].value = val1; rows[1].valueColor = kHudTextValue;
            rows[2].label = lbl2; rows[2].value = val2; rows[2].valueColor = kHudTextValue;
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

        void drawEndModal(float animT, const DrillHudState& st, bool exiting)
        {
            using namespace ui::style;
            Runtime& s_rt = *s_rtPtr;
            float xs = s_rt.xs;
            float eased = exiting ? (1.0f - easeOutCubic(1.0f - animT)) : easeOutCubic(animT);
            float alpha = (animT >= 0.99f) ? 1.0f : (exiting ? eased : (0.15f + 0.85f * eased));
            float yOff = (animT >= 0.99f) ? 0.0f : i2f(f2i(6.0f * (1.0f - eased) + 0.5f));
            float modalW = ui::layout::kDrillModalWidth; // 377.0f (F14)
            float modalTop = ui::layout::kDrillModalTop + yOff; // 113.0f at rest
            float modalH = ui::layout::kDrillModalHeight; // 254.0f
            float ox = 320.0f - (modalW * 0.5f) * xs;
            float ix = ox + kLabBorder * xs;
            float iw = (modalW - 2.0f * kLabBorder) * xs;
            float inTop = modalTop + kLabBorder;
            float inBot = modalTop + modalH - kLabBorder;
            bool timed = st.mode == kDrillModeTimed60;
            u8 target = drillModeTargetReps(st.mode);
            if (!timed && target == 0)
            {
                target = kDrillSetReps10;
            }

            // Pass 1: batch modal chrome, badge/pips, stat row slabs, and footer badges before text.
            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(0x070B12B8u, eased));
            drawPanel(ox, modalTop, modalW, modalH, kLabBaseTop, kLabBaseBot, alpha);

            const float hdrH = kFibBandH + kFibHair;
            u32 accent = st.newBest ? kLabFocusGold : kLabTealTop;
            gfx::fillGradV(ix, inTop, iw, hdrH, scaleAlpha(kLabHeaderTop, alpha), scaleAlpha(kLabHeaderBot, alpha));
            gfx::fillRect(ix, inTop, iw, kFibHair, scaleAlpha(accent, alpha));
            gfx::fillRect(ix, inTop + hdrH, iw, kFibHair, scaleAlpha(kLabDivider, alpha));

            float bodyY = inTop + hdrH + kFibHair;
            float badgeY = bodyY + 11.0f + kTypeHero + 11.0f;
            if (st.newBest)
            {
                float pop = 0.0f;
                if (s_rt.st.modalOpen && s_rt.modalNewBestPop > 0u)
                {
                    float u = i2f(s_rt.modalNewBestPop) / 6.0f;
                    pop = 4.0f * u * (1.0f - u);
                    s_rt.modalNewBestPop--;
                }
                float pw = 104.0f * (1.0f + 0.06f * pop) * xs;
                float extraH = pop > 0.01f ? i2f(f2i(2.0f * pop + 0.5f)) : 0.0f;
                float topShift = i2f(f2i(extraH * 0.5f));
                gfx::fillGradV(320.0f - pw * 0.5f, badgeY - topShift, pw, kFibRowH + extraH,
                               scaleAlpha(kLabGoldTop, alpha), scaleAlpha(kLabGoldBot, alpha));
            }
            else
            {
                float pw = 220.0f * xs;
                drawPips(320.0f - pw * 0.5f, badgeY + 7.0f, pw, 6.0f, timed ? kDrillFreePips : static_cast<int>(target),
                         st.repPips, alpha);
            }

            float rowPad = kFibPad + kLabBorder;
            float rowX = ox + rowPad * xs;
            float rowW = (modalW - 2.0f * rowPad) * xs;
            float lx = rowX + kFibPad * xs;
            float rx = rowX + rowW - kFibPad * xs;
            float ry0 = badgeY + kFibRowH + 11.0f;
            for (int r = 0; r < 3; r++)
            {
                float ry = ry0 + i2f(r) * (24.0f + kFibTight);
                gfx::fillGradV(rowX, ry, rowW, 24.0f, scaleAlpha(kLabRowSlateTop, alpha),
                               scaleAlpha(kLabRowSlateBot, alpha));
                gfx::fillRect(rowX, ry + 22.0f, rowW, kFibHair, scaleAlpha(kLabRowSlateEdge, alpha));
            }

            float footTop = inBot - kFibBandH - kFibHair;
            gfx::fillRect(ix, footTop, iw, kFibHair, scaleAlpha(kLabDivider, alpha));
            gfx::fillGradV(ix, footTop + kFibHair, iw, kFibBandH, scaleAlpha(kLabFooterTopCol, alpha),
                           scaleAlpha(kLabFooterBotCol, alpha));
            gfx::fillRect(ix, inBot - kFibHair, iw, kFibHair, scaleAlpha(kLabBevelDark, alpha));

            u8 ct = s_rt.lastPadType < 4 ? s_rt.lastPadType : 0;
            bool sideways = (ct == gfPadType::WIIMOTE);
            const char* glyphs[3] = {
                sideways ? "2" : "A",
                ct == gfPadType::GCC ? "START" : "+",
                sideways ? "1" : "B"
            };
            static const gfx::Rgba kBadgeCols[3][3] = {
                {kLabBtnATop,     kLabBtnABot,     kLabActiveTabText},
                {kLabBtnLightTop, kLabBtnLightBot, kLabBtnDarkText},
                {kLabBtnBTop,     kLabBtnBBot,     kLabWhiteText}
            };
            float footMid = footTop + kFibHair + kFibBandH * 0.5f;
            float bh = 17.0f;
            float gScale = scaleForCap(kTypeMicro);
            float gCapY = footMid - kTypeMicro * 0.5f;
            float lScale = scaleForCap(kTypeBody);
            float lCapY = footMid - kTypeBody * 0.5f;
            float itemGap = 20.0f * xs;
            float badgeW[3];
            float itemW[3];
            float totalW = 0.0f;
            for (int p = 0; p < 3; p++)
            {
                bool round = (p != 1 || ct != gfPadType::GCC);
                float gw = gfx::textWidth(glyphs[p], gScale);
                float bw = round ? bh * xs : (gw + 10.0f * xs);
                if (bw < bh * xs) bw = bh * xs;
                badgeW[p] = bw;
                float lw = gfx::textWidth(drillNthStr(kPromptLabels, p), lScale);
                itemW[p] = bw + kFibTight * xs + lw;
                totalW += itemW[p];
            }
            totalW += 2.0f * itemGap;
            float footStartX = 320.0f - totalW * 0.5f;
            float px = footStartX;
            for (int p = 0; p < 3; p++)
            {
                drawGlossyPillBadge(px, footMid - bh * 0.5f, badgeW[p], bh, scaleAlpha(kBadgeCols[p][0], alpha),
                                    scaleAlpha(kBadgeCols[p][1], alpha));
                px += itemW[p] + itemGap;
            }

            // Pass 2: batched text (Title Case header/subtitle, hero score, badge, 3 stat rows, footer labels).
            char buf[28];
            snprintf(buf, sizeof(buf), "%s Practice", drillHudTitle(st.activeDrill));
            label(320.0f, inTop + 6.0f, buf, 0xFFFFFFFFu, kAlignCenter | kSzDisp13, alpha);
            const char* subTitle = "60s Run Complete";
            if (!timed)
            {
                snprintf(buf, sizeof(buf), "%u-Rep Set Complete", static_cast<unsigned>(target));
                subTitle = buf;
            }
            label(320.0f, inTop + 23.0f, subTitle, 0xFFF5B8FFu, kAlignCenter, alpha);

            unsigned denom = timed ? (st.totalAtt > 0u ? static_cast<unsigned>(st.totalAtt) : 1u)
                                   : static_cast<unsigned>(target);
            unsigned hits = timed ? static_cast<unsigned>(st.totalHit)
                                  : (st.totalHit < target ? static_cast<unsigned>(st.totalHit) : static_cast<unsigned>(target));
            unsigned pct = (timed && st.totalAtt == 0u) ? 0u : (hits * 100u) / denom;
            snprintf(buf, sizeof(buf), "%u / %u  (%u%%)", hits, timed ? static_cast<unsigned>(st.totalAtt) : denom, pct);
            u8 scFl = kAlignCenter | kSzDisp20 | (pct >= 80u ? kDispGold : (pct < 50u ? kDispRed : 0));
            label(320.0f, bodyY + 11.0f, buf, pct >= 80u ? 0xFFE866FFu : (pct >= 50u ? 0xFFFFFFFFu : 0xFF9E92FFu),
                  scFl, alpha);

            if (st.newBest)
            {
                label(320.0f, badgeY + (kFibRowH - kTypeMicro) * 0.5f, "New Best!", kLabGoldInk, kAlignCenter, alpha);
            }

            for (int r = 0; r < 3; r++)
            {
                float ry = ry0 + i2f(r) * (24.0f + kFibTight);
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
                        vc = kLabFocusGold;
                    }
                }
                else if (r == 1)
                {
                    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(st.bestStreakFull));
                }
                else
                {
                    drillHudFormatHighlight(st, buf, sizeof(buf));
                    vc = kLabTealTop;
                }
                label(lx, ry + (24.0f - kTypeBody) * 0.5f, lbl, kGradeCols[0], kSz10, alpha);
                label(rx, ry + (24.0f - kTypeBody) * 0.5f, val, vc, kSz10 | kAlignRight, alpha);
            }

            px = footStartX;
            for (int p = 0; p < 3; p++)
            {
                float bw = badgeW[p];
                float gw = gfx::textWidth(glyphs[p], gScale);
                gfx::text(px + (bw - gw) * 0.5f, textYForCap(gCapY, gScale), glyphs[p],
                          scaleAlpha(kBadgeCols[p][2], alpha), gScale);
                gfx::text(px + bw + kFibTight * xs, textYForCap(lCapY, lScale), drillNthStr(kPromptLabels, p),
                          scaleAlpha(0xF0F4F8FFu, alpha), lScale);
                px += itemW[p] + itemGap;
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
        resetMotionState(s_rt, true);
        s_rt.sess = drills::sessionIn(main::services().settings());

        u8 id = (s_rt.sess && isValidSkillDrill(s_rt.sess->drill)) ? s_rt.sess->drill
                                                                   : static_cast<u8>(drills::kDrillNone);
        drillHudInit(s_rt.st, id, id ? s_rt.sess->best[id] : 0u);
        drillPracticeSetActiveDrill(id);
        ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
        pm->activeDrill = id;
        pm->mode = s_rt.st.mode < 3u ? s_rt.st.mode : 0u;
        if (id)
        {
            s_rt.sess->drillRan = 1;
            ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
        }
        syncDebug();
        TM_LOG("drillhud: debug block at %08x\n", &s_rt.debug);
    }

    void drillHudStartDrill(u8 drillId)
    {
        if (s_rtPtr == NULL || !isValidSkillDrill(drillId))
        {
            return;
        }
        Runtime& s_rt = *s_rtPtr;
        unpauseIfNeeded(s_rt);
        comboBreakRushSetActive(kRushOff);
        resetMotionState(s_rt, true);
        u16 best = (s_rt.sess != NULL && drillId < drills::kBestSlots) ? s_rt.sess->best[drillId] : 0u;
        drillHudInit(s_rt.st, drillId, best);
        drillPracticeSetActiveDrill(drillId);
        ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
        pm->activeDrill = drillId;
        pm->mode = s_rt.st.mode < 3u ? s_rt.st.mode : 0u;
        if (s_rt.sess != NULL)
        {
            s_rt.sess->drill = drillId;
            s_rt.sess->drillRan = 1;
        }
#if TM_STAGE4C
        if (isChallengeDrill(drillId))
        {
            challengeBackendsReset();
        }
#endif
        s_rt.synced = 0;
        ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
        syncDebug();
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
            resetMotionState(*s_rtPtr, false);
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
            u16 prevAtt = st.totalAtt;
            if ((sync || !st.modalOpen) && drillHudPoll(st, feed, sync) && !sync)
            {
                saveBest = true;
                modalTriggered = st.modalOpen != 0;
                if (!game::frozen() && st.totalAtt != prevAtt
                    && (st.lastGrade == kDrillGradeGood || st.lastGrade == kDrillGradePerfect))
                {
                    s_rt.hitFlashTimer = 5u;
                }
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
            bool tc = scenarioPresetsWantsTopBand(game::matchState());
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
                   | ((wdZone || recGuide) ? 8u : 0u) | (drillPracticeClaimsDpadDown() ? 16u : 0u);
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
                        s_rt.modalSnap = st;
                        drillHudRetry(st);
                        drillPracticeReset();
#if TM_STAGE4C
                        if (isChallengeDrill(st.activeDrill))
                        {
                            challengeBackendsReset();
                        }
#endif
                        resetMotionState(s_rt, false);
                        s_rt.synced = 0;
                    }
                    else
                    {
                        st.modalOpen = 0;
                        resetMotionState(s_rt, true);
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
            // 8f smooth modal reveal; trigger 5f New Best! focal pop once on open.
            if (s_rt.modalAnim == 0u && s_rt.st.newBest != 0u)
            {
                s_rt.modalNewBestPop = 5u;
            }
            if (s_rt.modalAnim < 8u)
            {
                s_rt.modalAnim++;
            }
            drawEndModal(i2f(s_rt.modalAnim) * 0.125f, s_rt.st, false);
        }
        else
        {
            drawLiveHud();
            // 4f fast modal exit fade (<= 0.65 * 8f) over live gameplay without blocking inputs.
            if (s_rt.modalAnim > 0u)
            {
                s_rt.modalAnim = s_rt.modalAnim > 2u ? static_cast<u8>(s_rt.modalAnim - 2u) : 0u;
                if (s_rt.modalAnim > 0u && s_rt.modalSnap.activeDrill != 0u)
                {
                    drawEndModal(i2f(s_rt.modalAnim) * 0.125f, s_rt.modalSnap, true);
                }
                else if (s_rt.modalAnim == 0u)
                {
                    s_rt.modalSnap.activeDrill = 0u;
                }
            }
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
            resetMotionState(s_rt, true);
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
        resetMotionState(s_rt, true);
        drillHudInit(s_rt.st, static_cast<u8>(drills::kDrillNone), 0u);
        drillPracticeSetActiveDrill(kPracticeDrillNone);
        ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
        pm->activeDrill = static_cast<u8>(drills::kDrillNone);
        pm->mode = static_cast<u8>(kDrillModeFree);
        syncDebug();
    }
}
}
