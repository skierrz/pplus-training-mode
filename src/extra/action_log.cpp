#include "extra/action_log.h"

#include <cstdio>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <string.h>

#include "extra/action_log_core.h"
#include "main/game/motion.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        struct ActionLogNet {
            ActionLogState st;
            ActionLogDebugBlock debug;
        };

        static ActionLogNet* s_net = NULL;

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        void syncDebug(u8 mode, u8 slot)
        {
            ActionLogDebugBlock& d = s_net->debug;
            const ActionLogState& st = s_net->st;
            d.magic = kActionLogDebugMagic;
            d.version = kActionLogDebugVersion;
            d.targetMode = mode;
            d.trackedSlot = slot;
            d.count = st.count;
            d.totalTransitions = st.totalTransitions;
            d.lastFrame = st.lastFrame;
            for (int i = 0; i < kActionLogCap; i++)
            {
                const ActionLogEntry* e = actionLogAt(st, i);
                if (e != NULL)
                {
                    d.entries[i] = *e;
                }
                else
                {
                    d.entries[i].frames = 0;
                    d.entries[i].label[0] = '\0';
                }
            }
        }

        void drawPill(float x, float y, float w, float h, float xs, u32 border, u32 top, u32 bot, const char* txt,
                      u32 tc, float txOff)
        {
            gfx::fillRect(x, y, w, h, border);
            gfx::fillGradV(x + xs, y + 1.0f, w - 2.0f * xs, h - 2.0f, top, bot);
            gfx::text(x + txOff, y + (h > 12.0f ? 0.5f : -1.5f), txt, tc, 0.5f);
        }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
    }

#ifdef __MWERKS__
#pragma force_active on
#endif
    void actionLogAttach()
    {
        if (s_net == NULL)
        {
            s_net = static_cast<ActionLogNet*>(game::allocNetworkZero(static_cast<u32>(sizeof(ActionLogNet))));
        }
        if (s_net == NULL)
        {
            return;
        }
        actionLogResetState(s_net->st);
        syncDebug(main::settings().general.actionLog, 0xFF);
        TM_LOG("action_log: debug block at %08x\n", reinterpret_cast<u32>(&s_net->debug));
    }

    void actionLogDetach()
    {
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_net));
    }

    void actionLogReset()
    {
        if (s_net != NULL)
        {
            actionLogResetState(s_net->st);
            syncDebug(main::settings().general.actionLog, 0xFF);
        }
    }

    void actionLogFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        u8 mode = main::settings().general.actionLog;
        bool changed = (mode != s_net->st.activeTarget);
        if (changed)
        {
            actionLogResetState(s_net->st);
            s_net->st.activeTarget = mode;
        }
        if (mode == main::kActionLogOff)
        {
            if (changed)
            {
                syncDebug(mode, 0xFF);
            }
            return;
        }
        u8 slot = 0xFF;
        bool wantCpu = (mode == main::kActionLogCpu);
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t != NULL && t->used && !t->partner && t->cpu == wantCpu)
            {
                slot = static_cast<u8>(i);
                int status = static_cast<int>(t->cur.status);
                game::MotionInfo mi;
                bool gotM = (t->fighter != NULL && game::readMotion(t->fighter, mi));
                actionLogPushFrame(s_net->st, game::trackerFrame(), status, gotM ? mi.kind : -1,
                                   t->cur.airborne != 0, NULL, gotM ? mi.name : NULL);
                break;
            }
        }
        syncDebug(mode, slot);
    }

    void actionLogDraw(const game::MatchState& match)
    {
        u8 mode = main::settings().general.actionLog;
        if (s_net == NULL || mode == main::kActionLogOff || !match.running || match.paused
            || gfx::lineHeight(0.5f) <= 0.0f)
        {
            return;
        }

        const bool wide = gfx::widescreen();
        const float xs = ui::layout::xsOf(wide);
        const ui::layout::Box b = ui::layout::actionLogBox(wide);
        const float ox = b.x0, oy = b.y0, bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        const float ix = ox + 4.0f * xs, iw = bw - 8.0f * xs;

        // Double-framed translucent Brawl/P+ panel (2.0px borders, zero left stripe).
        gfx::fillRect(ox, oy, bw, bh, 0x050C12ECu);
        gfx::fillRect(ox + 2.0f * xs, oy + 2.0f, bw - 4.0f * xs, bh - 4.0f, 0x359CB0E8u);
        gfx::fillGradV(ix, oy + 4.0f, iw, bh - 8.0f, 0x0D1E28E6u, 0x061016ECu);
        gfx::fillGradV(ix, oy + 4.0f, iw, 13.0f, 0x205A68EEu, 0x10323CEEu);
        gfx::fillRect(ix, oy + 17.0f, iw, 2.0f, 0x2A5A68E4u);

        gfx::text(ix + 5.0f * xs, oy + 3.5f, "ACTION LOG", 0xE8F8FFFFu, 0.5f);
        const char* who = (mode == main::kActionLogCpu) ? "CPU" : "P1";
        drawPill(ix + 10.0f * xs + gfx::textWidth("ACTION LOG", 0.5f), oy + 5.0f,
                 gfx::textWidth(who, 0.5f) + 6.0f * xs, 11.0f, xs, 0x050C12E0u,
                 mode == main::kActionLogCpu ? 0x9C6814FFu : 0x1C7282FFu,
                 mode == main::kActionLogCpu ? 0x663E0AFFu : 0x0E444EFFu, who, 0xFFFFFFFFu, 3.0f * xs);

        const ActionLogState& st = s_net->st;
        char buf[20];
        if (st.count > 0)
        {
            actionLogFormatPill(*actionLogAt(st, static_cast<int>(st.count) - 1), buf, sizeof(buf));
        }
        else
        {
            strcpy(buf, "IDLE");
        }
        gfx::text(ix + iw - 5.0f * xs - gfx::textWidth(buf, 0.5f), oy + 3.5f, buf, 0xFFD300FFu, 0.5f);

        int n = static_cast<int>(st.count);
        if (n > 0)
        {
            const float padX = 5.0f * xs;
            const float chevW = gfx::textWidth(">", 0.5f) + 3.0f * xs;
            float widths[kActionLogCap];
            for (int i = 0; i < n; i++)
            {
                actionLogFormatPill(*actionLogAt(st, i), buf, sizeof(buf));
                widths[i] = gfx::textWidth(buf, 0.5f) + 2.0f * padX;
            }
            int first = n - 1;
            float usedW = widths[first];
            const float availW = iw - 8.0f * xs;
            while (first > 0 && usedW + widths[first - 1] + chevW <= availW)
            {
                usedW += widths[--first] + chevW;
            }
            float px = ix + 4.0f * xs;
            for (int i = first; i < n; i++)
            {
                if (i > first)
                {
                    gfx::text(px + 1.5f * xs, oy + 21.5f, ">", 0x90BCC8D8u, 0.5f);
                    px += chevW;
                }
                const ActionLogEntry* e = actionLogAt(st, i);
                const ActionPillPalette& pal = actionPillPalette(e->stateClass);
                float pw = widths[i];
                float maxW = ix + iw - 4.0f * xs - px;
                if (pw > maxW)
                {
                    pw = maxW;
                }
                if (pw < 8.0f * xs)
                {
                    break;
                }
                actionLogFormatPill(*e, buf, sizeof(buf));
                drawPill(px, oy + 21.0f, pw, 14.0f, xs, pal.border, pal.fillTop, pal.fillBot, buf, pal.text, padX);
                px += pw;
            }
        }
        s_net->debug.drawCalls++;
    }
#ifdef __MWERKS__
#pragma force_active reset
#endif
}
}
