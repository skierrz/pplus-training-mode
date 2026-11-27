#include "extra/action_log.h"

#include <cstdio>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/action_log_core.h"
#include "main/display/display.h"
#include "main/game/motion.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        struct ActionLogNet {
            ActionLogState st;
            Sf6StripState sf6;
            ActionLogDebugBlock debug;
            u8 topIsCpu;
            u8 botIsCpu;
        };

        static ActionLogNet* s_net = NULL;

        bool trackedHasActiveHitbox(const game::Tracked* t)
        {
            if (t == NULL)
            {
                return false;
            }
            if (t->cur.attackActive != 0)
            {
                return true;
            }
            if (t->fighter == NULL)
            {
                return false;
            }
            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            if (m == NULL || !game::isRam(m->m_collisionAttackModule))
            {
                return false;
            }
            u32 parts = m->m_collisionAttackModule->getPartSize();
            if (parts > 16)
            {
                parts = 16;
            }
            for (u32 i = 0; i < parts; i++)
            {
                if (m->m_collisionAttackModule->isAttack(i, false))
                {
                    return true;
                }
            }
            return false;
        }

        int activePlayerCount()
        {
            int maxP = 2;
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t != NULL && t->used && !t->partner && t->playerNo + 1 > maxP)
                {
                    maxP = t->playerNo + 1;
                }
            }
            return maxP;
        }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        void syncDebug(u8 mode, u8 slot)
        {
            ActionLogDebugBlock& d = s_net->debug;
            const ActionLogState& st = s_net->st;
            const Sf6StripState& sf = s_net->sf6;
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
            d.sf6Advantage = sf.advantage;
            d.sf6HasAdvantage = sf.hasAdvantage;
            d.sf6Filled = sf.filled;
            for (int r = 0; r < 2; r++)
            {
                for (int k = 0; k < kSf6MaxCells; k++)
                {
                    d.sf6Cells[r][k] = k < static_cast<int>(sf.filled) ? sf6CellAt(sf, r, k) : 0u;
                }
            }
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
        sf6ResetState(s_net->sf6);
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
            sf6ResetState(s_net->sf6);
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
            sf6ResetState(s_net->sf6);
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
        const game::Tracked* p1 = NULL;
        const game::Tracked* p2 = NULL;
        u8 slot = 0xFF;
        bool wantCpu = (mode == main::kActionLogCpu);
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || !t->used || t->partner)
            {
                continue;
            }
            if (p1 == NULL && !t->cpu)
            {
                p1 = t;
            }
            else if (p2 == NULL && (t->cpu || t != p1))
            {
                p2 = t;
            }
            if (slot == 0xFF && t->cpu == wantCpu)
            {
                slot = static_cast<u8>(i);
                int status = static_cast<int>(t->cur.status);
                game::MotionInfo mi;
                bool gotM = (t->fighter != NULL && game::readMotion(t->fighter, mi));
                actionLogPushFrame(s_net->st, game::trackerFrame(), status, gotM ? mi.kind : -1,
                                   t->cur.airborne != 0, NULL, gotM ? mi.name : NULL);
            }
        }
        if (p1 == NULL && p2 != NULL)
        {
            p1 = p2;
            p2 = NULL;
        }
        const game::Tracked* topRow = wantCpu && p2 != NULL ? p2 : p1;
        const game::Tracked* botRow = wantCpu && p2 != NULL ? p1 : p2;
        s_net->topIsCpu = (topRow != NULL && topRow->cpu) ? 1u : 0u;
        s_net->botIsCpu = (botRow != NULL && botRow->cpu) ? 1u : 0u;
        int st0 = topRow != NULL ? static_cast<int>(topRow->cur.status) : 0;
        bool act0 = topRow == NULL || topRow->cur.actionable != 0;
        bool hit0 = trackedHasActiveHitbox(topRow);
        bool stun0 = topRow != NULL
                     && sf6IsDefenderStun(st0, topRow->cur.hitstun, topRow->cur.shieldStun, topRow->cur.hitReceived,
                                          topRow->cur.shieldReceived);
        int st1 = botRow != NULL ? static_cast<int>(botRow->cur.status) : 0;
        bool act1 = botRow == NULL || botRow->cur.actionable != 0;
        bool hit1 = trackedHasActiveHitbox(botRow);
        bool stun1 = botRow != NULL
                     && sf6IsDefenderStun(st1, botRow->cur.hitstun, botRow->cur.shieldStun, botRow->cur.hitReceived,
                                          botRow->cur.shieldReceived);
        sf6PushDualFrame(s_net->sf6, game::trackerFrame(), st0, act0, hit0, stun0, st1, act1, hit1, stun1);
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
        const int players = activePlayerCount();
        const bool timerOn = match.training && display::hudShown();
        const ui::layout::Box b = ui::layout::actionLogBox(wide, players, timerOn);
        const float ox = b.x0, oy = b.y0, bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        const int visibleCells = wide ? kSf6Cells16 : kSf6Cells43;
        const float cellPitch = wide ? 6.0f : 8.0f;
        const float cellFillW = wide ? 4.8f : 6.4f;

        // Crisp dark grounded SF6 pill strip (matching 02-final-sf6-16x9-4x3).
        gfx::fillRect(ox, oy, bw, bh, 0x091018D8u);
        gfx::fillRect(ox, oy, bw, 2.0f, 0x04080EF0u);
        gfx::fillRect(ox, oy + bh - 2.0f, bw, 2.0f, 0x04080EF0u);

        const float tagRight = ox + (wide ? 22.0f : 28.0f);
        const float cellsX = tagRight + 4.0f * xs;
        const float rowH = 10.4f;
        const float rowY[2] = {oy + 6.5f, oy + bh - 6.5f - rowH};
        const bool swapCpu = (mode == main::kActionLogCpu);
        const char* topTag = s_net->topIsCpu ? "CPU" : (swapCpu ? "P2" : "P1");
        const char* botTag = s_net->botIsCpu ? "CPU" : (swapCpu ? "P1" : "P2");
        const char* const rowTags[2] = {topTag, botTag};
        const gfx::Rgba tagCols[2] = {s_net->topIsCpu ? 0x86DCEEFFu : 0xF8FCFFFFu,
                                      s_net->botIsCpu ? 0x86DCEEFFu : 0xF8FCFFFFu};

        const Sf6StripState& sf = s_net->sf6;
        int filled = static_cast<int>(sf.filled);
        int startIdx = filled > visibleCells ? (filled - visibleCells) : 0;
        const float tagSc = ui::style::scaleForCap(9.0f);
        const float trackW = static_cast<float>(visibleCells) * cellPitch + (cellPitch - cellFillW);

        for (int r = 0; r < 2; r++)
        {
            float tw = gfx::textWidth(rowTags[r], tagSc);
            float tx = tagRight - tw;
            if (tx < ox + 3.0f * xs)
            {
                tx = ox + 3.0f * xs;
            }
            float ty = ui::style::textYForCap(rowY[r] + (rowH - 9.0f) * 0.5f, tagSc);
            gfx::text(tx + xs, ty + 1.0f, rowTags[r], 0x000000E0u, tagSc);
            gfx::text(tx, ty, rowTags[r], tagCols[r], tagSc);

            // Crisp dark cell trough so individual frame slots read sharply.
            gfx::fillRect(cellsX - 1.0f * xs, rowY[r] - 1.0f, trackW + 1.0f * xs, rowH + 2.0f, 0x04070CECu);
            for (int c = 0; c < visibleCells; c++)
            {
                int src = startIdx + c;
                u8 kind = (src < filled) ? sf6CellAt(sf, r, src) : static_cast<u8>(kSf6Empty);
                float cx = cellsX + static_cast<float>(c) * cellPitch;
                gfx::fillRect(cx, rowY[r], cellFillW, rowH, sf6CellColor(kind));
                if (kind != kSf6Empty && kind != kSf6Gap)
                {
                    gfx::fillRect(cx, rowY[r], cellFillW, 2.0f, 0xFFFFFF38u);
                }
                if (kind == kSf6Gap)
                {
                    // Crisp 3.5-scanline bright yellow gap marker centered in the cell.
                    gfx::fillRect(cx, rowY[r] + (rowH - 3.5f) * 0.5f, cellFillW, 3.5f, 0xFFD300FFu);
                }
            }
        }

        // Bold Brawl Display Font (0x80497DA4, 20 scanlines) frame advantage readout on right.
        char advBuf[12];
        gfx::Rgba topCol = 0xD8E6F0FFu;
        gfx::Rgba botCol = 0x90A8B8FFu;
        if (sf.hasAdvantage)
        {
            int adv = static_cast<int>(sf.advantage);
            if (adv > 0)
            {
                snprintf(advBuf, sizeof(advBuf), "+%df", adv);
                topCol = 0x6BFFB0FFu;
                botCol = 0x26B868FFu;
            }
            else if (adv < 0)
            {
                snprintf(advBuf, sizeof(advBuf), "%df", adv);
                topCol = 0xFF847CFFu;
                botCol = 0xD83232FFu;
            }
            else
            {
                strcpy(advBuf, "0f");
                topCol = 0xFFE866FFu;
                botCol = 0xD89B00FFu;
            }
        }
        else
        {
            strcpy(advBuf, "--");
        }
        float advSc = 20.0f / 24.0f;
        float advAreaX = cellsX + static_cast<float>(visibleCells) * cellPitch + 4.0f;
        float advAreaW = (ox + bw - 5.0f) - advAreaX;
        float advW = gfx::displayTextWidth(advBuf, advSc);
        float advSx = (advW > advAreaW && advW > 0.0f) ? (advSc * advAreaW / advW) : advSc;
        float drawW = advW > advAreaW ? advAreaW : advW;
        float advX = advAreaX + (advAreaW - drawW) * 0.5f;
        float advY = oy + (bh - 20.0f) * 0.5f - 2.0f * advSc;
        gfx::displayText(advX, advY, advBuf, topCol, botCol, advSx, advSc, 1.4f, 0x050C12FFu);

        s_net->debug.sf6VisibleCells = static_cast<u8>(visibleCells);
        s_net->debug.sf6RelocatedTop = players > 2 ? 1u : 0u;
        s_net->debug.drawCalls++;
    }
#ifdef __MWERKS__
#pragma force_active reset
#endif
}
}
