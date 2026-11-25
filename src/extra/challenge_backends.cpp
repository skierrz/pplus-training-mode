#include "extra/challenge_backends.h"

#include <so/controller/so_controller_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/drill_pplus_tech_engines.h"
#include "extra/net_alloc.h"
#include "extra/stage_geom.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Live read-only P+ 3.2.1 runtime adapter for all five Stage 4C challenge
// backends. All slot buffers, Reaction Lab sample rings, and the "TM4X" debug
// block are allocated from Heaps::Network so extra .bss stays near-zero.
namespace tm {
namespace extra {
    namespace {
        enum {
            kPosturePrevPos = 0x18,
            kPostureLr = 0x40,
            kLaBasicLedgeGrabs = 79,           // LA-Basic[79] (Ledge.asm:495-513)
            kVarSnakeCypherUses = 0x11000034u, // LA-Float[52] (FitSnake.pac 0x2CBF8)
            kVarSnakeCypherUsed = 0x1200003Du  // LA-Bit[61]   (FitSnake.pac 0x2CC18)
        };

        struct ChallengeSlot {
            bool active;
            s32 entryId;
            bool partner;
            s8 playerNo;
            int ftKind;
            const Fighter* fighter;
            u32 taskId;
            CancelSprintState cs;
            FlowCourseState fc;
            ReactionTechchaseState rt;
            RecoveryFloorhugState rf;
            SnakeRecoveryState snk;
        };

        struct NetState {
            ChallengeDebugBlock debug;
            ChallengeSlot slots[game::kMaxTracked];
            ChallengeFrame frames[game::kMaxTracked];
            u16 reactionRings[game::kMaxTracked * kReactionRingCap];
        };

        NetState* s_net = NULL;
        u32 s_resetCount = 0;
        s32 s_prevFrame = game::kNoFrame;

        inline float readFloatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        // Single out-of-line RAM check keeps readFrame() compact on MWCC.
        bool ramOk(const void* p)
        {
            return game::isRam(p);
        }

        ChallengeDebugSlot* debugSlotFor(const ChallengeSlot& slot)
        {
            return (!slot.partner && slot.playerNo >= 0 && slot.playerNo < kChallengeDebugSlots)
                       ? &s_net->debug.slots[slot.playerNo]
                       : NULL;
        }

        void mirrorSlot(const ChallengeSlot& slot, s32 f)
        {
            ChallengeDebugSlot* dp = debugSlotFor(slot);
            if (dp == NULL)
            {
                return;
            }
            dp->used = 1;
            dp->playerNo = slot.playerNo;
            dp->ftKind = slot.ftKind >= 0 ? static_cast<u8>(slot.ftKind) : 0xFFu;
            dp->pad0 = 0;
            dp->frame = static_cast<u32>(f >= 0 ? f : 0);
            memcpy(&dp->cs, &slot.cs.out, sizeof(dp->cs));
            memcpy(&dp->fc, &slot.fc.out, sizeof(dp->fc));
            memcpy(&dp->rt, &slot.rt.out, sizeof(dp->rt));
            memcpy(&dp->rf, &slot.rf.out, sizeof(dp->rf));
            memcpy(&dp->snk, &slot.snk.out, sizeof(dp->snk));
        }

        void resetSlot(int idx, bool full)
        {
            ChallengeSlot& slot = s_net->slots[idx];
            if (full)
            {
                ChallengeDebugSlot* d = slot.active ? debugSlotFor(slot) : NULL;
                if (d != NULL)
                {
                    memset(d, 0, sizeof(*d));
                    d->playerNo = -1;
                }
                memset(&slot, 0, sizeof(slot));
                slot.entryId = -1;
                slot.playerNo = -1;
                slot.ftKind = -1;
                slot.cs.lastFtKind = -1;
                slot.rt.ring = s_net->reactionRings + idx * kReactionRingCap;
                slot.rt.ringCap = kReactionRingCap;
            }
            cancelSprintResetTransient(slot.cs);
            flowCourseResetTransient(slot.fc);
            reactionTechchaseResetTransient(slot.rt);
            recoveryFloorhugResetTransient(slot.rf);
            snakeRecoveryResetTransient(slot.snk);
        }

        const game::Tracked* readFrame(int i, s32 f, const game::FighterRef* refs, int refCount, ChallengeFrame& out)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || !t->used || t->cur.frame != f || !ramOk(t->fighter))
            {
                return NULL;
            }
            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            game::Body body;
            if (m == NULL || !ramOk(m->m_postureModule) || !game::readBody(t->fighter, body))
            {
                return NULL;
            }

            const void* posture = m->m_postureModule;
            out.frame = f;
            out.entryId = t->entryId;
            out.status = t->cur.status;
            out.hitstun = t->cur.hitstun;
            out.airborne = t->cur.airborne;
            out.actionable = t->cur.actionable;
            out.groups = t->cur.groups;
            out.x = body.x;
            out.y = body.y;
            out.vx = body.x - readFloatAt(posture, kPosturePrevPos);
            out.lr = readFloatAt(posture, kPostureLr);
            out.motionKind = static_cast<s16>(body.motionKind);
            out.xluFrames = static_cast<s16>(body.xluFrames);
            if (ramOk(m->m_controllerModule))
            {
                out.triggerButtons = m->m_controllerModule->getTrigger().m_mask;
            }
            s32 grabs = game::instanceInt(m, kLaBasicLedgeGrabs);
            out.ledgeGrabs = static_cast<u8>(grabs > 0 ? (grabs > 255 ? 255 : grabs) : 0);
            out.ftKind = game::fighterKind(t->entryId, t->partner);
            for (int k = 0; k < refCount; k++)
            {
                if (refs[k].entryId == t->entryId)
                {
                    out.damage = game::damage(refs[k]);
                    break;
                }
            }
            out.outOfPlay = game::outOfPlay(t->cur.status, true) ? 1 : 0;
            // Only treat as opponent hit when an opponent's attack actually connected
            // (kByInflict); kByElimination would misattribute C4/Grenade self-hits.
            out.opponentDealtHit = (t->lastReceived.frame == f && t->lastReceived.how == game::kByInflict
                                    && !t->lastReceived.shield && t->lastReceived.other != i)
                                       ? 1
                                       : 0;
            if (out.ftKind == kSnkFtSnake && ramOk(m->m_workManageModule))
            {
                out.cypherLockout = m->m_workManageModule->isFlag(kVarSnakeCypherUsed) ? 1 : 0;
                out.cypherStacks = m->m_workManageModule->getFloat(kVarSnakeCypherUses);
            }
            u8 evN = t->eventCount < kChallengeMaxEvents ? t->eventCount : static_cast<u8>(kChallengeMaxEvents);
            out.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                out.events[k].status = t->events[k].status;
                out.events[k].prev = t->events[k].prev;
                out.events[k].motionKind = t->events[k].motionKind;
            }
            out.valid = 1;
            return t;
        }
    }

    void challengeBackendsAttach()
    {
        challengeBackendsDetach();
        s_net = static_cast<NetState*>(allocNetZero(static_cast<u32>(sizeof(NetState))));
        s_resetCount = 0;
        if (s_net == NULL)
        {
            return;
        }
        s_net->debug.magic = kChallengeDebugMagic;
        s_net->debug.version = kChallengeDebugVersion;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            if (i < kChallengeDebugSlots)
            {
                s_net->debug.slots[i].playerNo = -1;
            }
            resetSlot(i, true);
        }
        TM_LOG("challenge4c: debug block at %08x\n", &s_net->debug);
    }

    void challengeBackendsDetach()
    {
        s_prevFrame = game::kNoFrame;
        if (s_net != NULL)
        {
            freeNetRaw(s_net);
            s_net = NULL;
        }
    }

    void challengeBackendsReset()
    {
        s_prevFrame = game::kNoFrame;
        s_resetCount++;
        if (s_net == NULL)
        {
            return;
        }
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            resetSlot(i, false);
            if (s_net->slots[i].active)
            {
                mirrorSlot(s_net->slots[i], 0);
            }
        }
        s_net->debug.resetCount = s_resetCount;
    }

    void challengeBackendsFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        const s32 f = game::trackerFrame();
        if (s_prevFrame != game::kNoFrame && f <= s_prevFrame)
        {
            challengeBackendsReset();
        }
        s_prevFrame = f;
        s_net->debug.frame = static_cast<u32>(f >= 0 ? f : 0);

        game::FighterRef refs[game::kMaxFighters];
        int refCount = game::listFighters(refs, game::kMaxFighters);

        StageGeometry geom;
        memset(&geom, 0, sizeof(geom));
        bool haveGeom = queryStageGeometry(geom, false, true);

        ChallengeFrame* frames = s_net->frames;
        memset(frames, 0, sizeof(s_net->frames));

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            ChallengeSlot& slot = s_net->slots[i];
            const game::Tracked* t = readFrame(i, f, refs, refCount, frames[i]);
            if (t == NULL)
            {
                if (slot.active)
                {
                    resetSlot(i, true);
                }
                continue;
            }
            if (!slot.active || slot.entryId != t->entryId || slot.partner != t->partner
                || slot.fighter != t->fighter || slot.taskId != t->taskId || slot.playerNo != t->playerNo)
            {
                resetSlot(i, true);
                slot.active = true;
                slot.entryId = t->entryId;
                slot.partner = t->partner;
                slot.playerNo = t->playerNo;
                slot.fighter = t->fighter;
                slot.taskId = t->taskId;
            }
            slot.ftKind = frames[i].ftKind;
        }

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            if (!frames[i].valid)
            {
                continue;
            }
            ChallengeSlot& slot = s_net->slots[i];
            const ChallengeFrame& cf = frames[i];

            cancelSprintStep(slot.cs, cf);
            if (haveGeom)
            {
                flowCourseSetGeometry(slot.fc, geom);
                flowCourseStep(slot.fc, cf);
                recoverySweetspotStep(slot.rf, geom, cf);
            }
            reactionTechchaseStep(slot.rt, frames, game::kMaxTracked, i);

            const FloorhugState* fhState = pplusTechFloorhug(i);
            if (fhState != NULL)
            {
                floorhugChallengeObserve(slot.rf, *fhState);
            }
            if (cf.ftKind == kSnkFtSnake)
            {
                snakeRecoveryStep(slot.snk, geom, cf);
            }

            mirrorSlot(slot, f);
        }
    }

    const ChallengeDebugBlock* challengeDebugBlock()
    {
        return s_net != NULL ? &s_net->debug : NULL;
    }
}
}
