#include "extra/drill_hud.h"

#include <cstdio>
#include <gf/gf_pad_status.h>
#include <string.h>

#include "extra/drill_core_engines.h"
#include "extra/drill_float_djc_core.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_hud_core.h"
#include "extra/drill_pplus_tech_engines.h"
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
            // bit 5 = gold display bottom, bit 6 = red display bottom, bit 7 = clamp to 152*xs.
            kAlignRight = 1,
            kAlignCenter = 2,
            kSz10 = 1 << 2,
            kSzDisp85 = 2 << 2,
            kSzDisp13 = 3 << 2,
            kSzDisp20 = 4 << 2,
            kDispGold = 1 << 5,
            kDispRed = 1 << 6,
            kClamp152 = 1 << 7
        };

        const u32 kGradeCols[5] = {0xA0BCC4FFu, 0xFFD300FFu, 0x4CE688FFu, 0xFF6658FFu, 0xFFB848FFu};
        const u32 kPipCols[5] = {0x20343EFFu, 0xFFD300FFu, 0x4CE688FFu, 0xFF6658FFu, 0xFFB848FFu};
        const float kScales[5] = {8.0f / 15.0f, 10.0f / 15.0f, 8.5f / 24.0f, 13.0f / 24.0f, 20.0f / 24.0f};
        const u8 kPromptIcons[3][4] = {
            {ui::kIconGcA, ui::kIconClA, ui::kIconWii2, ui::kIconWiiA},
            {ui::kIconGcStart, ui::kIconWiiPlus, ui::kIconWiiPlus, ui::kIconWiiPlus},
            {ui::kIconGcB, ui::kIconClB, ui::kIconWii1, ui::kIconWiiB}
        };
        const char* const kPromptLabels[3] = {"Retry", "Lab", "Return to CSS"};
        const char* const kPromptGlyphs[3] = {"A", "+", "B"};

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

        static Runtime s_rt;

        void syncDebug()
        {
            s_rt.debug.magic = kDrillHudDebugMagic;
            s_rt.debug.version = kDrillHudDebugVersion;
            memcpy(&s_rt.debug.activeDrill, &s_rt.st.activeDrill, 14);
            s_rt.debug.alcNotice = s_rt.st.alcNotice;
        }

        // Double-framed Brawl/P+ panel: dark outer frame, 2px accent border, dark gradient body.
        void drawPanel(float ox, float oy, float w, float h, u32 accent, u32 top, u32 bot)
        {
            float xs = s_rt.xs;
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
            if (fl & kClamp152)
            {
                float mw = 152.0f * s_rt.xs;
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

        void drawPips(float x, float y, float totalW, float h)
        {
            float step = totalW * 0.1f;
            float pw = step - 2.0f * s_rt.xs;
            for (int k = 0; k < kDrillSetReps; k++, x += step)
            {
                u8 p = s_rt.st.repPips[k];
                gfx::fillRect(x, y, pw, h, kPipCols[p < 5 ? p : 0]);
            }
        }

        void drawLiveHud()
        {
            DrillHudState& st = s_rt.st;
            float xs = s_rt.xs;
            float ox = 320.0f - 148.0f * xs;
            drawPanel(ox, 24.0f, 296.0f, 40.0f, st.alcNotice ? kGradeCols[4] : 0x35B8C8FFu, 0x0D1E26ECu, 0x071117ECu);

            float ix = ox + 4.0f * xs;
            float iw = 288.0f * xs;
            gfx::fillGradV(ix, 28.0f, iw, 15.0f, 0x226874F0u, 0x123C46F0u);
            gfx::fillRect(ix, 43.0f, iw, 2.0f, 0x2A5866E0u);

            float lx = ox + 10.0f * xs;
            float rx = ox + 286.0f * xs;
            label(lx, 31.0f, drillHudTitle(st.activeDrill), 0xFFFFFFFFu, kSzDisp85);

            char buf[24];
            snprintf(buf, sizeof(buf), "REP %u/10  HIT %u", static_cast<unsigned>(st.repsDone),
                     static_cast<unsigned>(st.repsHit));
            label(rx, 31.5f, buf, 0xFFD300FFu, kAlignRight);

            u8 g = st.lastGrade < 5 ? st.lastGrade : 0;
            label(lx, 46.0f, st.lastFeedback, kGradeCols[g], kClamp152);

            if (st.sessionBest)
            {
                snprintf(buf, sizeof(buf), "STK %u  BEST %u", static_cast<unsigned>(st.streak),
                         static_cast<unsigned>(st.sessionBest));
            }
            else
            {
                snprintf(buf, sizeof(buf), "STK %u  BEST --", static_cast<unsigned>(st.streak));
            }
            label(rx, 46.0f, buf, kGradeCols[0], kAlignRight);
            drawPips(lx, 56.0f, 276.0f * xs, 3.0f);
        }

        void drawEndModal()
        {
            DrillHudState& st = s_rt.st;
            float xs = s_rt.xs;
            float ox = 320.0f - 192.0f * xs;
            float ix = ox + 4.0f * xs;
            float iw = 376.0f * xs;

            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0x050C10B0u);
            drawPanel(ox, 104.0f, 384.0f, 272.0f, st.newBest ? 0xFFD300FFu : 0x35B8C8FFu, 0x10242EF4u, 0x09151CF4u);

            gfx::fillGradV(ix, 108.0f, iw, 44.0f, 0x3AD0E0FAu, 0x14505CFAu);
            gfx::fillRect(ix, 150.0f, iw, 2.0f, 0x12404AFFu);

            char buf[28];
            snprintf(buf, sizeof(buf), "%s DRILL", drillHudTitle(st.activeDrill));
            label(320.0f, 115.0f, buf, 0xFFFFFFFFu, kAlignCenter | kSzDisp13);
            label(320.0f, 136.0f, "10-REP SET COMPLETE", 0xFFF5B8FFu, kAlignCenter);

            unsigned pct = static_cast<unsigned>(st.repsHit) * 10u;
            snprintf(buf, sizeof(buf), "%u / 10  (%u%%)", static_cast<unsigned>(st.repsHit), pct);
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
                drawPips(320.0f - pw * 0.5f, 202.0f, pw, 6.0f);
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
                        snprintf(buf, sizeof(buf), "%u / 10", static_cast<unsigned>(st.sessionBest));
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
                    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(st.bestStreak));
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
                    label(px + bw * 0.5f, 344.0f, kPromptGlyphs[p], 0xF0F4F8FFu, kAlignCenter);
                }
                label(px + bw + 5.0f * xs, 343.0f, kPromptLabels[p], 0xF0F4F8FFu, kSz10);
            }
        }
    }

    void drillHudAttach()
    {
        memset(&s_rt, 0, sizeof(s_rt));
        s_rt.fresh = 1;
        s_rt.sess = drills::sessionIn(main::services().settings());

        u8 id = (s_rt.sess && isValidSkillDrill(s_rt.sess->drill)) ? s_rt.sess->drill
                                                                   : static_cast<u8>(drills::kDrillNone);
        drillHudInit(s_rt.st, id, id ? s_rt.sess->best[id] : 0u);
        if (id)
        {
            s_rt.sess->drillRan = 1;
        }
        syncDebug();
        TM_LOG("drillhud: debug block at %08x\n", &s_rt.debug);
    }

    void drillHudDetach()
    {
        if (s_rt.pausedByUs)
        {
            game::setGameplayPaused(false);
            s_rt.pausedByUs = 0;
        }
        s_rt.st.modalOpen = 0;
        syncDebug();
    }

    void drillHudSavestateReset()
    {
        s_rt.synced = 0;
    }

    void drillHudFrame()
    {
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
            if (d == drills::kDrillLedgedash)
            {
                feed.ledge = coreDrillLedgeState(slot);
            }
            else if (d == drills::kDrillWavedash)
            {
                feed.wd = coreDrillWavedashState(slot);
            }
            else if (d == drills::kDrillLCancel)
            {
                feed.lc = coreDrillLCancelState(slot);
                feed.autoLCancel = coreDrillAutoLCancelActive();
            }
            else if (d == drills::kDrillHoverFloat)
            {
                feed.fd = hoverPsFloatDjcSlot(slot);
                const game::Tracked* tr = game::tracked(slot);
                if (tr != NULL)
                {
                    int ftKind = game::fighterKind(tr->entryId, tr->partner);
                    feed.ineligibleFighter = !isFloatCapable(ftKind) && !isDjcCapable(ftKind);
                }
            }
            else if (d == drills::kDrillPowershield)
            {
                feed.ps = hoverPsPowershieldSlot(slot);
            }
            else if (d == drills::kDrillPPlusTech)
            {
                feed.rar = pplusTechRar(slot);
                feed.dacus = pplusTechDacus(slot);
                feed.brev = pplusTechBReverse(slot);
            }
            if (!s_rt.synced)
            {
                drillHudPoll(st, feed, true);
                s_rt.synced = 1;
            }
            else if (!st.modalOpen && drillHudPoll(st, feed, false))
            {
                saveBest = true;
                if (st.modalOpen)
                {
                    s_rt.waitRelease = 1;
                    ui::menuSound(game::kSoundConfirm);
                }
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
        DrillHudState& st = s_rt.st;
        if (pads == NULL)
        {
            bool active = isValidSkillDrill(st.activeDrill);
            if (phase == 2u && active)
            {
                float hw = 148.0f * ui::layout::xsOf(gfx::widescreen());
                ui::layout::Box b = {320.0f - hw, 24.0f, 320.0f + hw, 64.0f};
                ui::layout::setLabBand(b);
            }
            return (active ? 1u : 0u) | (st.modalOpen ? 2u : 0u);
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
                    if (s_rt.pausedByUs)
                    {
                        game::setGameplayPaused(false);
                        s_rt.pausedByUs = 0;
                    }
                    if (act == 1)
                    {
                        drillHudRetry(st);
                        s_rt.synced = 0;
                    }
                    else
                    {
                        st.modalOpen = 0;
                        if (act == 2)
                        {
                            st.activeDrill = drills::kDrillNone;
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
        if (!match.running || match.paused || lab::menuOpen() || !isValidSkillDrill(s_rt.st.activeDrill)
            || gfx::lineHeight(0.5f) <= 0.0f)
        {
            return;
        }
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
        return s_rt.st.activeDrill;
    }

    bool drillHudModalOpen()
    {
        return s_rt.st.modalOpen != 0;
    }
}
}
