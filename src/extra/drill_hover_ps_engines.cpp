#include "extra/drill_hover_ps_engines.h"

#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/net_alloc.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    struct HoverPsState {
        FloatDjcSlot floatDjc[kHoverPsMaxSlots];
        PsSlot ps[kHoverPsMaxSlots];
    };

    static HoverPsState* s_state = NULL;
    static HoverPsDebugBlock s_debug;
    static s32 s_lastFrame = game::kNoFrame;
    static const Fighter* s_lastFighter[kHoverPsMaxSlots];
    static int s_lastEntryId[kHoverPsMaxSlots];
    static u32 s_lastTaskId[kHoverPsMaxSlots];

    static void clearSlotIdentity(int i)
    {
        s_lastFighter[i] = NULL;
        s_lastEntryId[i] = -1;
        s_lastTaskId[i] = 0;
    }

    static void syncDebugSlot(int i, const game::Tracked* t, int ftKind, s16 motionKind)
    {
        if (s_state == NULL)
        {
            return;
        }
        HoverPsDebugSlot& d = s_debug.slots[i];
        const FloatDjcSlot& fd = s_state->floatDjc[i];
        const PsSlot& ps = s_state->ps[i];

        if (t != NULL && t->used)
        {
            d.used = 1;
            d.partner = t->partner ? 1 : 0;
            d.playerNo = t->playerNo;
            d.ftKind = ftKind >= 0 ? static_cast<u8>(ftKind) : 0xFF;
            d.curStatus = t->cur.status;
            d.curMotion = motionKind;
        }
        else
        {
            d.used = 0;
            d.partner = 0;
            d.playerNo = -1;
            d.ftKind = 0xFF;
            d.curStatus = -1;
            d.curMotion = -1;
        }

        d.fcAttempts = fd.stats.fcAttempts;
        d.fcSuccesses = fd.stats.fcSuccesses;
        d.fcStreak = fd.stats.fcStreak;
        d.fcBestStreak = fd.stats.fcBestStreak;
        d.lastFcOutcome = fd.last.fcOutcome;
        d.lastFcIsHover = fd.last.fcIsHover;
        d.inFloat = fd.inFloat ? 1 : 0;
        d.floatAerialActive = (fd.floatAerialActive || fd.floatAerialVia33 || fd.droppedFloatAerial) ? 1 : 0;
        d.lastFcDuration = fd.last.fcDurationFrames;
        d.lastFcAerialFrame = fd.last.fcAerialFrame;
        d.lastFcActFrame = fd.last.fcActFrame;
        d.bestActFrame = fd.stats.bestActFrame;
        d.lastFcMotion = fd.last.fcMotionKind;
        d.lastFcLandingLCancel = fd.last.fcLandingLCancelled;
        d.padFc = 0;
        d.lastFcFrame = fd.last.fcEventFrame;
        d.lastFcEntryY = fd.last.fcEntryY;
        d.lastFcMinY = fd.last.fcMinY;

        d.djcAttempts = fd.stats.djcAttempts;
        d.djcInstant = fd.stats.djcInstant;
        d.djcStreak = fd.stats.djcStreak;
        d.djcBestStreak = fd.stats.djcBestStreak;
        d.lastDjcGrade = fd.last.djcGrade;
        d.lastDjcLandOutcome = fd.last.djcLandOutcome;
        d.lastDjcLandedClean = fd.last.djcLandedClean;
        d.lastDjcHadBit33 = fd.last.djcHadBit33;
        d.lastDjcFrame = fd.last.djcFrame;
        d.lastDjcMotion = fd.last.djcMotionKind;
        d.lastDjcEventFrame = fd.last.djcEventFrame;
        d.lastDjcVyBefore = fd.last.djcVyBefore;
        d.lastDjcVyAfter = fd.last.djcVyAfter;
        d.lastDjcHeight = fd.last.djcHeight;

        d.psAttempts = ps.stats.attempts;
        d.psPhysicalHits = ps.stats.physicalHits;
        d.psReflectHits = ps.stats.reflectHits;
        d.psRePsHits = ps.stats.rePsHits;
        d.psLightToHardHits = ps.stats.lightToHardHits;
        d.psEarlyMisses = ps.stats.earlyMisses;
        d.psLateMisses = ps.stats.lateMisses;
        d.psBufferedMisses = ps.stats.bufferedMisses;
        d.psDropCancels = ps.stats.psDropCancels;
        d.psStreak = ps.stats.streak;
        d.psBestStreak = ps.stats.bestStreak;
        d.lastPsOutcome = ps.last.outcome;
        d.lastPsRePs = ps.last.rePs;
        d.lastPsLightToHard = ps.last.lightToHard;
        d.lastPsDropUsed = ps.last.psDropUsed;
        d.lastPsDropChainedRePs = ps.last.psDropChainedRePs;
        d.guardArmed = ps.guardArmed ? 1 : 0;
        d.lastPsWindowFrame = ps.last.windowFrame;
        d.lastPsOffsetFrames = ps.last.offsetFrames;
        d.lastPsDropActFrame = ps.last.psDropActFrame;
        d.lastPsEventFrame = ps.last.eventFrame;
    }

    void hoverPsResetAll()
    {
        s_lastFrame = game::kNoFrame;
        for (int i = 0; i < kHoverPsMaxSlots; i++)
        {
            if (s_state != NULL)
            {
                floatDjcResetAll(s_state->floatDjc[i]);
                psResetAll(s_state->ps[i]);
            }
            clearSlotIdentity(i);
        }
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kHoverPsDebugMagic;
        s_debug.version = kHoverPsDebugVersion;
        for (int i = 0; i < kHoverPsMaxSlots; i++)
        {
            syncDebugSlot(i, NULL, -1, -1);
        }
    }

    void hoverPsResetTransient()
    {
        s_lastFrame = game::kNoFrame;
        for (int i = 0; i < kHoverPsMaxSlots; i++)
        {
            if (s_state != NULL)
            {
                floatDjcResetTransient(s_state->floatDjc[i]);
                psResetTransient(s_state->ps[i]);
            }
            s_debug.slots[i].inFloat = 0;
            s_debug.slots[i].floatAerialActive = 0;
            s_debug.slots[i].guardArmed = 0;
        }
        s_debug.resetCount++;
    }

    void hoverPsAttach()
    {
        freeNetPtr(s_state);
        s_state = allocNetArray<HoverPsState>(1);
        hoverPsResetAll();
        TM_LOG("hover_ps: debug block at %08x\n", &s_debug);
    }

    void hoverPsDetach()
    {
        hoverPsResetAll();
        freeNetPtr(s_state);
    }

    void hoverPsFrame()
    {
        if (s_state == NULL)
        {
            return;
        }
        const s32 frame = game::trackerFrame();
        if (s_lastFrame != game::kNoFrame && frame <= s_lastFrame)
        {
            hoverPsResetTransient();
        }
        s_lastFrame = frame;
        s_debug.frame = static_cast<u32>(frame >= 0 ? frame : 0);

        const int limit = game::kMaxTracked < kHoverPsMaxSlots ? game::kMaxTracked : kHoverPsMaxSlots;
        for (int i = 0; i < limit; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || !t->used || t->cur.frame != frame || !game::isRam(t->fighter))
            {
                if (s_debug.slots[i].used != 0 || s_lastFighter[i] != NULL)
                {
                    floatDjcResetTransient(s_state->floatDjc[i]);
                    psResetTransient(s_state->ps[i]);
                    clearSlotIdentity(i);
                    syncDebugSlot(i, NULL, -1, -1);
                }
                continue;
            }

            if (s_lastFighter[i] != t->fighter || s_lastEntryId[i] != t->entryId || s_lastTaskId[i] != t->taskId)
            {
                floatDjcResetTransient(s_state->floatDjc[i]);
                psResetTransient(s_state->ps[i]);
                s_lastFighter[i] = t->fighter;
                s_lastEntryId[i] = t->entryId;
                s_lastTaskId[i] = t->taskId;
            }

            const int ftKind = game::fighterKind(t->entryId, t->partner);
            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            game::Body body;
            const bool haveBody = game::readBody(t->fighter, body);
            soWorkManageModule* work = (m != NULL && game::isRam(m->m_workManageModule)) ? m->m_workManageModule : NULL;

            // --- Feed Engine 1: Float / Hover Cancel & DJC ---
            FloatDjcInput fdIn;
            memset(&fdIn, 0, sizeof(fdIn));
            fdIn.frame = frame;
            fdIn.ftKind = ftKind;
            fdIn.status = t->cur.status;
            fdIn.motionKind = haveBody ? static_cast<s16>(body.motionKind) : -1;
            fdIn.hitlag = t->cur.hitlag;
            fdIn.hitstun = t->cur.hitstun;
            fdIn.airborne = t->cur.airborne;
            fdIn.actionable = t->cur.actionable;
            fdIn.y = haveBody ? body.y : 0.0f;
            fdIn.vy = haveBody ? body.vy : 0.0f;

            if (work != NULL)
            {
                if (ftKind == kDrillFtPeach)
                {
                    fdIn.floatUsed = work->isFlag(kFdVarPeachFloatUsed) ? 1 : 0;
                    if (t->cur.status == kFdStPeachFloat)
                    {
                        fdIn.floatRem = work->getInt(kFdVarFloatFrames);
                    }
                }
                else if (ftKind == kDrillFtMewtwo)
                {
                    fdIn.hoverActive = work->isFlag(kFdVarMewtwoHoverActive) ? 1 : 0;
                    fdIn.floatUsed = work->isFlag(kFdVarMewtwoHoverUsed) ? 1 : 0;
                    if (t->cur.status == kFdStMewtwoHover)
                    {
                        fdIn.floatRem = work->getInt(kFdVarFloatFrames);
                    }
                }
                if (isDjcCapable(ftKind) && (t->cur.status == kFdStJumpAerial || t->cur.status == kFdStAttackAir))
                {
                    fdIn.djcBit = work->isFlag(kFdVarDjcBit) ? 1 : 0;
                }
            }

            const u8 nEv = t->eventCount <= kFdMaxEvents ? t->eventCount : kFdMaxEvents;
            fdIn.eventCount = nEv;
            for (u8 k = 0; k < nEv; k++)
            {
                fdIn.events[k].status = t->events[k].status;
                fdIn.events[k].prev = t->events[k].prev;
                fdIn.events[k].motionKind = t->events[k].motionKind;
                fdIn.events[k].lCancel = t->events[k].lCancel;
                fdIn.events[k].airborne = t->events[k].airborne;
            }
            stepFloatDjc(fdIn, s_state->floatDjc[i]);

            // --- Feed Engine 2: P+ Powershield & PS-Drop ---
            PsInput psIn;
            memset(&psIn, 0, sizeof(psIn));
            psIn.frame = frame;
            psIn.status = t->cur.status;
            psIn.curHitlag = t->cur.hitlag;
            psIn.prevHitlag = t->prev.hitlag;
            psIn.laBit96 = haveBody ? body.powershield : 0;
            psIn.guardButton = (haveBody && (body.buttons & game::kButtonGuard) != 0) ? 1 : 0;
            psIn.shieldReceived = t->cur.shieldReceived;

            if (work != NULL)
            {
                const int st = t->cur.status;
                if (st == kPsStGuardOn || st == kPsStGuard || st == kPsStGuardOff || st == kPsStGuardDamage)
                {
                    psIn.raBasic1 = work->getInt(kPsVarRaActive);
                    psIn.raBasic3 = work->getInt(kPsVarRaDrop);
                    psIn.raBit10 = work->isFlag(kPsVarRaReflect) ? 1 : 0;
                }
                psIn.laBasic77 = game::instanceInt(m, 77);
            }

            const u8 psEv = t->eventCount <= kPsMaxEvents ? t->eventCount : kPsMaxEvents;
            psIn.eventCount = psEv;
            for (u8 k = 0; k < psEv; k++)
            {
                psIn.events[k].status = t->events[k].status;
                psIn.events[k].prev = t->events[k].prev;
            }
            stepPowershield(psIn, s_state->ps[i]);

            syncDebugSlot(i, t, ftKind, fdIn.motionKind);
        }
    }

    const FloatDjcSlot* hoverPsFloatDjcSlot(int trackedIndex)
    {
        if (s_state == NULL || trackedIndex < 0 || trackedIndex >= kHoverPsMaxSlots)
        {
            return NULL;
        }
        return &s_state->floatDjc[trackedIndex];
    }

    const PsSlot* hoverPsPowershieldSlot(int trackedIndex)
    {
        if (s_state == NULL || trackedIndex < 0 || trackedIndex >= kHoverPsMaxSlots)
        {
            return NULL;
        }
        return &s_state->ps[trackedIndex];
    }
}
}
