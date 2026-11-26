#include "extra/drill_core_engines.h"

#include <so/controller/so_controller_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <stddef.h>
#include <string.h>

#include "extra/net_alloc.h"
#include "extra/stage_geom.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            // Project+ 3.2.1 work-vars and addresses:
            //  - LA-Basic[77] (0x1000004D): new analog L/R press >= 0.3 (ButtonPresses.asm:454)
            //  - LA-Basic[79] (0x1000004F): consecutive ledge-grab counter (Ledge.asm:495-513)
            //  - RA-Bit[19]   (0x22000013): Cliff_Wait neutral-stick latch (mem1 0x8087AA70)
            //  - 0x805A00E0 -> +0x08 -> +0xE5: ALC (bit 0x01) & 3f buffer (bit 0x02) (L-Cancel.asm:235-240, 315-319)
            kLaBasicAnalogLR = 77,
            kLaBasicLedgeGrabs = 79,
            kRaBitCliffNeutral = 0x22000013,
            kGameGlobalPtrAddr = 0x805A00E0u,
            kModeMeleeOffset = 0x08u,
            kRuleFlagsOffset = 0xE5u,
            kButtonAttack = 1 << 0
        };

        struct CoreDrillSlot {
            bool active;
            Fighter* fighter;
            int entryId;
            u32 taskId;
            u32 prevButtons;
            bool hasPrevButtons;
            LedgeCoreState ledge;
            WdCoreState wd;
            LcCoreState lc;
        };

        struct CoreDrillNet {
            CoreDrillSlot slots[game::kMaxTracked];
            CoreDrillDebugBlock debug;
        };

        static CoreDrillNet* s_net = NULL;
        static s32 s_prevFrame = game::kNoFrame;
        static bool s_autoLCancel = false;
        static bool s_inputBuffer3f = false;

        // Reads P+ 3.2.1 Auto L-Cancel (bit 0x01) and 3-frame input buffer
        // (bit 0x02) flags via isRam()-guarded dereference (L-Cancel.asm:235-240).
        static void readPPlusRuleFlags(bool& autoLCancel, bool& inputBuffer3f)
        {
            autoLCancel = false;
            inputBuffer3f = false;
            const void* global = reinterpret_cast<const void*>(*reinterpret_cast<const u32*>(kGameGlobalPtrAddr));
            if (!game::isRam(global))
            {
                return;
            }
            const void* modeMelee = reinterpret_cast<const void*>(game::wordAt(global, kModeMeleeOffset));
            if (!game::isRam(modeMelee))
            {
                return;
            }
            u8 flags = game::byteAt(modeMelee, kRuleFlagsOffset);
            autoLCancel = (flags & 0x01u) != 0;
            inputBuffer3f = (flags & 0x02u) != 0;
        }

        static float entryDamage(int entryId, const game::FighterRef* refs, int n)
        {
            for (int k = 0; k < n; k++)
            {
                if (refs[k].entryId == entryId)
                {
                    return game::damage(refs[k]);
                }
            }
            return 0.0f;
        }

        static bool isLandingOnPlatform(float x, float y, bool haveGeom, const StageGeometry& geom)
        {
            if (haveGeom)
            {
                int np = geom.platformCount < kMaxStagePlatforms ? geom.platformCount : kMaxStagePlatforms;
                for (int k = 0; k < np; k++)
                {
                    const StagePlatform& p = geom.platforms[k];
                    if (!p.valid)
                    {
                        continue;
                    }
                    float dy = y - p.y;
                    if (dy < 0.0f)
                    {
                        dy = -dy;
                    }
                    if (x >= p.leftX - 3.0f && x <= p.rightX + 3.0f && dy <= 3.5f)
                    {
                        return true;
                    }
                }
                if (geom.floor.valid)
                {
                    return y > geom.floor.centerY + 2.0f;
                }
            }
            return y > 1.0f;
        }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        static void resetSlotAll(CoreDrillSlot& slot)
        {
            slot.active = false;
            slot.fighter = NULL;
            slot.entryId = -1;
            slot.taskId = 0;
            slot.prevButtons = 0;
            slot.hasPrevButtons = false;
            ledgeCoreResetAll(slot.ledge);
            wdCoreResetAll(slot.wd);
            lcCoreResetAll(slot.lc);
        }

        static void resetSlotTransient(CoreDrillSlot& slot)
        {
            slot.prevButtons = 0;
            slot.hasPrevButtons = false;
            ledgeCoreResetTransient(slot.ledge);
            wdCoreResetTransient(slot.wd);
            lcCoreResetTransient(slot.lc);
        }

#ifndef __MWERKS__
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, ledgeAirdodgeAngle) - offsetof(CoreDrillDebugSlot, ledgeGalint)
                             == offsetof(LedgeResult, airdodgeAngle) - offsetof(LedgeResult, galint),
                         tm_cd_ledge_span);
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, ledgeBestGalint) - offsetof(CoreDrillDebugSlot, ledgeAttempts)
                             == offsetof(LedgeStats, bestGalint) - offsetof(LedgeStats, attempts),
                         tm_cd_ledge_stats_span);
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, wdAirdodgeAirFrames) - offsetof(CoreDrillDebugSlot, wdKind)
                             == offsetof(WdResult, airdodgeAirFrames) - offsetof(WdResult, kind),
                         tm_cd_wd_span);
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, wlBestStreak) - offsetof(CoreDrillDebugSlot, wdAttempts)
                             == sizeof(WdStats) - sizeof(u16),
                         tm_cd_wd_stats_span);
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, lcLateFrames) - offsetof(CoreDrillDebugSlot, lcPressFrame)
                             == offsetof(LcResult, lateFrames) - offsetof(LcResult, pressFrame),
                         tm_cd_lc_span);
        TM_STATIC_ASSERT(offsetof(CoreDrillDebugSlot, lcBestStreak) - offsetof(CoreDrillDebugSlot, lcTotal)
                             == sizeof(LcStats) - sizeof(u16),
                         tm_cd_lc_stats_span);
#endif

        static void syncDebugSlot(CoreDrillNet& net, int idx, const game::Tracked* t, const CoreDrillSlot& slot)
        {
            CoreDrillDebugSlot& d = net.debug.slots[idx];
            if (t == NULL || !slot.active)
            {
                memset(&d, 0, sizeof(d));
                d.playerNo = -1;
                return;
            }

            d.used = 1;
            d.playerNo = t->playerNo;
            d.partner = t->partner ? 1 : 0;
            d.cpu = t->cpu ? 1 : 0;
            d.frame = static_cast<u32>(t->cur.frame >= 0 ? t->cur.frame : 0);

            // Engine 1: Ledgedash
            const LedgeCoreState& l = slot.ledge;
            d.ledgeSeq = l.seq;
            d.ledgeOutcome = l.last.outcome;
            d.ledgeFailFlags = l.last.failFlags;
            d.ledgeGrabCount = l.last.grabCount;
            d.ledgeFromTether = l.last.fromTether;
            d.ledgeUsedFly = l.last.usedFly;
            d.ledgeTourneySlow = l.last.tourneyWinnerSlow;
            d.ledgeJumpXluStripped = l.last.ledgeJumpXluStripped;
            d.ledgeCellCount = l.last.cellCount;
            memcpy(&d.ledgeGalint, &l.last.galint,
                   offsetof(LedgeResult, airdodgeAngle) - offsetof(LedgeResult, galint) + sizeof(float));
            d.ledgeActivePhase = l.phase;
            d.ledgeActiveGrabs = l.grabCount;
            d.ledgeActiveNoInvuln = l.noInvulnLimit;
            d.ledgeActiveFromTether = l.fromTether;
            memcpy(&d.ledgeAttempts, &l.stats.attempts,
                   offsetof(LedgeStats, bestGalint) - offsetof(LedgeStats, attempts) + sizeof(s16));
            d.ledgeStreak = l.stats.streak;
            d.ledgeBestStreak = l.stats.bestStreak;
            d.ledgeHasBestGalint = l.stats.hasBestGalint;
            d.ledgePad[0] = d.ledgePad[1] = d.ledgePad[2] = 0;
            memcpy(d.ledgeCells, l.last.cells, kLedgeCoreMaxCells);

            // Engine 2: Wavedash & Platform Waveland
            const WdCoreState& w = slot.wd;
            d.wdSeq = w.seq;
            memcpy(&d.wdKind, &w.last.kind,
                   offsetof(WdResult, airdodgeAirFrames) - offsetof(WdResult, kind) + sizeof(s16));
            d.wdPad = 0;
            d.wdAngleDeg = w.last.angleDeg;
            d.wdSlideDistance = w.last.slideDistance;
            d.wdExpectedSpeed = w.last.expectedSpeed;
            memcpy(&d.wdAttempts, &w.stats, sizeof(w.stats));

            // Engine 3: L-Cancel
            const LcCoreState& c = slot.lc;
            d.lcSeq = c.seq;
            d.lcOutcome = c.last.outcome;
            d.lcContext = c.last.context;
            d.lcInputKind = c.last.inputKind;
            d.lcAutoLCancelActive = c.last.autoLCancelActive;
            d.lcBuffer3fActive = c.last.inputBuffer3fActive;
            d.lcPad = 0;
            memcpy(&d.lcPressFrame, &c.last.pressFrame, 3 * sizeof(s16));
            memcpy(&d.lcTotal, &c.stats, sizeof(c.stats));
        }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

        static const CoreDrillSlot* activeSlot(int tracked)
        {
            return (s_net != NULL && tracked >= 0 && tracked < game::kMaxTracked && s_net->slots[tracked].active)
                       ? &s_net->slots[tracked]
                       : NULL;
        }
    }

    void coreDrillEnginesAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<CoreDrillNet>(1);
        s_prevFrame = game::kNoFrame;
        s_autoLCancel = false;
        s_inputBuffer3f = false;
        if (s_net != NULL)
        {
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                resetSlotAll(s_net->slots[i]);
            }
            memset(&s_net->debug, 0, sizeof(s_net->debug));
            s_net->debug.magic = kCoreDrillDebugMagic;
            s_net->debug.version = kCoreDrillDebugVersion;
            s_net->debug.slotSize = static_cast<u32>(sizeof(CoreDrillDebugSlot));
            s_net->debug.maxSlots = static_cast<u8>(game::kMaxTracked);
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                s_net->debug.slots[i].playerNo = -1;
            }
            TM_LOG("drill4c: debug block at %08x\n", &s_net->debug);
        }
    }

    void coreDrillEnginesDetach()
    {
        freeNetPtr(s_net);
        s_prevFrame = game::kNoFrame;
        s_autoLCancel = false;
        s_inputBuffer3f = false;
    }

    void coreDrillEnginesSavestateReset()
    {
        s_prevFrame = game::kNoFrame;
        if (s_net == NULL)
        {
            return;
        }
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            resetSlotTransient(s_net->slots[i]);
        }
        s_net->debug.savestateResets++;
    }

    void coreDrillEnginesFrame()
    {
        CoreDrillNet* net = s_net;
        if (net == NULL)
        {
            return;
        }
        s32 f = game::trackerFrame();
        if (s_prevFrame != game::kNoFrame && f <= s_prevFrame)
        {
            coreDrillEnginesSavestateReset();
        }
        s_prevFrame = f;

        readPPlusRuleFlags(s_autoLCancel, s_inputBuffer3f);
        net->debug.frame = static_cast<u32>(f >= 0 ? f : 0);
        net->debug.autoLCancelActive = s_autoLCancel ? 1 : 0;
        net->debug.inputBuffer3fActive = s_inputBuffer3f ? 1 : 0;

        game::FighterRef refs[game::kMaxFighters];
        int refCount = game::listFighters(refs, game::kMaxFighters);

        StageGeometry geom;
        bool geomQueried = false;
        bool haveGeom = false;

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            CoreDrillSlot& slot = net->slots[i];

            if (t == NULL)
            {
                if (slot.active)
                {
                    resetSlotAll(slot);
                }
                syncDebugSlot(*net, i, NULL, slot);
                continue;
            }
            if (t->cur.frame != f)
            {
                if (slot.active)
                {
                    resetSlotTransient(slot);
                }
                syncDebugSlot(*net, i, t, slot);
                continue;
            }

            // Fighter instance, entryId, or taskId changed (transform, fighter swap, etc.).
            if (!slot.active || slot.fighter != t->fighter || slot.entryId != t->entryId || slot.taskId != t->taskId)
            {
                resetSlotTransient(slot);
                slot.active = true;
                slot.fighter = t->fighter;
                slot.entryId = t->entryId;
                slot.taskId = t->taskId;
            }

            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            game::Body body;
            if (m == NULL || !game::readBody(t->fighter, body))
            {
                syncDebugSlot(*net, i, t, slot);
                continue;
            }

            s32 ledgeGrabsRaw = game::instanceInt(m, kLaBasicLedgeGrabs);
            u8 ledgeGrabs = 0;
            if (ledgeGrabsRaw > 0)
            {
                ledgeGrabs = static_cast<u8>(ledgeGrabsRaw > 255 ? 255 : ledgeGrabsRaw);
            }
            s32 analogLR = game::instanceInt(m, kLaBasicAnalogLR);
            bool neutralLatch = game::isRam(m->m_workManageModule)
                                && m->m_workManageModule->isFlag(kRaBitCliffNeutral);

            u32 trigMask = 0;
            if (game::isRam(m->m_controllerModule))
            {
                trigMask = m->m_controllerModule->getTrigger().m_mask;
            }
            bool digitalGuardEdge = ((body.buttons & game::kButtonGuard) != 0
                                     && ((slot.hasPrevButtons && (slot.prevButtons & game::kButtonGuard) == 0)
                                         || (trigMask & game::kButtonGuard) != 0));
            bool digitalAttackEdge = ((body.buttons & kButtonAttack) != 0
                                      && ((slot.hasPrevButtons && (slot.prevButtons & kButtonAttack) == 0)
                                          || (trigMask & kButtonAttack) != 0));
            slot.prevButtons = body.buttons;
            slot.hasPrevButtons = true;

            // Lazy platform geometry query: only invoked on frames where a
            // fighter actually enters Landing_Fall_Special (0x19) or Landing
            // (0x16/0x17) so idle frames incur zero raycast overhead.
            bool checkPlatform = (t->cur.status == game::kStatusLandingFallSpecial);
            int evCount = t->eventCount < game::kMaxEvents ? static_cast<int>(t->eventCount) : game::kMaxEvents;
            for (int k = 0; k < evCount && !checkPlatform; k++)
            {
                int es = t->events[k].status;
                if (es == game::kStatusLandingFallSpecial || es == game::kStatusLanding
                    || es == game::kStatusLandingLight)
                {
                    checkPlatform = true;
                }
            }
            if (checkPlatform && !geomQueried)
            {
                geomQueried = true;
                haveGeom = queryStageGeometry(geom, false, true);
            }
            bool onPlatform = checkPlatform ? isLandingOnPlatform(body.x, body.y, haveGeom, geom) : false;

            // 1. Step Ledgedash Engine.
            LedgeFrameInput li;
            memset(&li, 0, sizeof(li));
            li.frame = f;
            li.status = t->cur.status;
            li.prevStatus = t->prev.status;
            li.motionKind = static_cast<s16>(body.motionKind);
            li.hitstun = t->cur.hitstun;
            li.xluFrames = body.xluFrames;
            li.ledgeGrabCount = ledgeGrabs;
            li.airborne = t->cur.airborne;
            li.actionable = t->cur.actionable;
            li.neutralLatch = neutralLatch ? 1 : 0;
            li.damage = entryDamage(t->entryId, refs, refCount);
            li.stickX = body.stickX;
            li.stickY = body.stickY;
            li.eventCount = static_cast<u8>(evCount < kLedgeCoreMaxEvents ? evCount : kLedgeCoreMaxEvents);
            for (int k = 0; k < li.eventCount; k++)
            {
                li.events[k].status = t->events[k].status;
                li.events[k].prev = t->events[k].prev;
                li.events[k].motionKind = t->events[k].motionKind;
                li.events[k].airborne = t->events[k].airborne;
            }
            ledgeCoreStep(slot.ledge, li);

            // 2. Step Wavedash / Platform Waveland Engine.
            WdFrameInput wi;
            memset(&wi, 0, sizeof(wi));
            wi.frame = f;
            wi.status = t->cur.status;
            wi.prevStatus = t->prev.status;
            wi.hitstun = t->cur.hitstun;
            wi.airborne = t->cur.airborne;
            wi.actionable = t->cur.actionable;
            wi.onPlatform = onPlatform ? 1 : 0;
            wi.guardEdge = (digitalGuardEdge || analogLR == 1) ? 1 : 0;
            wi.buttons = body.buttons;
            wi.x = body.x;
            wi.y = body.y;
            wi.stickX = body.stickX;
            wi.stickY = body.stickY;
            wi.eventCount = static_cast<u8>(evCount < kWdCoreMaxEvents ? evCount : kWdCoreMaxEvents);
            for (int k = 0; k < wi.eventCount; k++)
            {
                wi.events[k].status = t->events[k].status;
                wi.events[k].prev = t->events[k].prev;
                wi.events[k].flags = t->events[k].flags;
                wi.events[k].airborne = t->events[k].airborne;
            }
            wdCoreStep(slot.wd, wi);

            // 3. Step L-Cancel Engine.
            LcFrameInput ci;
            memset(&ci, 0, sizeof(ci));
            ci.frame = f;
            ci.status = t->cur.status;
            ci.prevStatus = t->prev.status;
            ci.motionKind = static_cast<s16>(body.motionKind);
            ci.hitlag = t->cur.hitlag;
            ci.lCancelCounter = body.lCancel;
            ci.analogLRState = analogLR;
            ci.digitalGuardEdge = digitalGuardEdge ? 1 : 0;
            ci.digitalAttackEdge = digitalAttackEdge ? 1 : 0;
            ci.attackActive = t->cur.attackActive;
            ci.dealtHit = (t->lastDealt.frame == f && t->lastDealt.shield == 0) ? 1 : 0;
            ci.dealtShield = (t->lastDealt.frame == f && t->lastDealt.shield != 0) ? 1 : 0;
            ci.autoLCancelActive = s_autoLCancel ? 1 : 0;
            ci.inputBuffer3fActive = s_inputBuffer3f ? 1 : 0;
            ci.eventCount = static_cast<u8>(evCount < kLcCoreMaxEvents ? evCount : kLcCoreMaxEvents);
            for (int k = 0; k < ci.eventCount; k++)
            {
                ci.events[k].status = t->events[k].status;
                ci.events[k].prev = t->events[k].prev;
                ci.events[k].motionKind = t->events[k].motionKind;
                ci.events[k].lCancel = t->events[k].lCancel;
            }
            lcCoreStep(slot.lc, ci);

            syncDebugSlot(*net, i, t, slot);
        }
    }

    const LedgeCoreState* coreDrillLedgeState(int tracked)
    {
        const CoreDrillSlot* s = activeSlot(tracked);
        return s != NULL ? &s->ledge : NULL;
    }

    const WdCoreState* coreDrillWavedashState(int tracked)
    {
        const CoreDrillSlot* s = activeSlot(tracked);
        return s != NULL ? &s->wd : NULL;
    }

    const LcCoreState* coreDrillLCancelState(int tracked)
    {
        const CoreDrillSlot* s = activeSlot(tracked);
        return s != NULL ? &s->lc : NULL;
    }

    bool coreDrillAutoLCancelActive()
    {
        return s_autoLCancel;
    }

    bool coreDrillInputBuffer3fActive()
    {
        return s_inputBuffer3f;
    }
}
}
