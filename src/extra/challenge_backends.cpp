#include "extra/challenge_backends.h"

#include <GX/GXGeometry.h>
#include <GX/GXTypes.h>
#include <GX/GXVert.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/controller/so_controller_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/visibility/so_visibility_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/drill_hud.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/net_alloc.h"
#include "extra/stage_geom.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "shared/drill_session.h"
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
            kCollCliffBoxOffset = 0x3C,        // grCollStatus cliff-hang box floats (0x801357F0)
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
            u8 inTaunt;
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
            StageGeometry cachedGeom;
            u32 guideDrawCalls;
            u32 tauntInterrupts;
            u32 preResetTauntMask;
            u8 tauntGuardFrames;
            u8 floorhugGuardActive;
            u8 haveGeom;
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
            slot.inTaunt = 0;
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
        ChallengeSlot* slotAt(int trackedSlot)
        {
            return (s_net != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked
                    && s_net->slots[trackedSlot].active)
                       ? &s_net->slots[trackedSlot]
                       : NULL;
        }

        u32 tauntMaskBit(s32 entryId, bool partner)
        {
            if (entryId < 0 || entryId >= 16)
            {
                return 0u;
            }
            return 1u << (static_cast<u32>(entryId) * 2u + (partner ? 1u : 0u));
        }

        void restoreTauntModules(const soModuleEnumeration* m)
        {
            if (ramOk(m->m_visibilityModule))
            {
                m->m_visibilityModule->setWhole(1);
                m->m_visibilityModule->setDefaultAll();
            }
            if (ramOk(m->m_collisionHitModule))
            {
                m->m_collisionHitModule->setWhole(0, 0);
                m->m_collisionHitModule->resetStatusAll(0);
                m->m_collisionHitModule->resetStatusGlobal(0);
            }
            if (ramOk(m->m_controllerModule))
            {
                m->m_controllerModule->resetButton();
                m->m_controllerModule->resetTrigger();
            }
        }

        void interruptFighterTaunt(Fighter* f, s32 entryId, bool partner, int ftKind, u8 guardFrames, bool onReset)
        {
            const soModuleEnumeration* m = game::fighterModules(f);
            if (m == NULL)
            {
                return;
            }
            bool floorhugActive = s_net->floorhugGuardActive != 0;
            bool wasInTaunt = (s_net->preResetTauntMask & tauntMaskBit(entryId, partner)) != 0u;
            int st = game::fighterStatus(m);
            if (floorhugNeedsTauntInterrupt(ftKind, static_cast<s16>(st), guardFrames, floorhugActive, wasInTaunt))
            {
                if (ramOk(m->m_statusModule) && ramOk(f->m_moduleAccesser))
                {
                    m->m_statusModule->changeStatusForce(game::kStatusWait, f->m_moduleAccesser);
                }
                restoreTauntModules(m);
                s_net->tauntInterrupts++;
                TM_LOG("challenge4c: interrupted sticky taunt (ft %#x, st %#x)\n", ftKind, st);
            }
            else if (floorhugNeedsMetaKnightModuleRestore(ftKind, onReset, floorhugActive, wasInTaunt))
            {
                // Savestate loads may already have restored status to Wait while
                // Meta Knight's AppealHi vanish/hurtbox flags remained dirty.
                restoreTauntModules(m);
            }
        }

        void scanInterruptTaunts(bool onReset)
        {
            if (s_net == NULL || s_net->tauntGuardFrames == 0)
            {
                return;
            }
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int i = 0; i < n; i++)
            {
                if (ramOk(refs[i].fighter))
                {
                    interruptFighterTaunt(refs[i].fighter, refs[i].entryId, false,
                                          game::fighterKind(refs[i].entryId, false), s_net->tauntGuardFrames, onReset);
                }
                if (ramOk(refs[i].partner) && refs[i].partner != refs[i].fighter)
                {
                    interruptFighterTaunt(refs[i].partner, refs[i].entryId, true,
                                          game::fighterKind(refs[i].entryId, true), s_net->tauntGuardFrames, onReset);
                }
            }
        }
    }

    void challengeBackendsInterruptTaunts()
    {
        if (s_net == NULL)
        {
            return;
        }
        s_net->floorhugGuardActive = 1;
        s_net->preResetTauntMask = 0u;
        s_net->tauntGuardFrames = kFhTauntGuardFrames;
        scanInterruptTaunts(true);
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
        u32 preResetTauntMask = 0u;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            if (s_net->slots[i].active && s_net->slots[i].inTaunt != 0)
            {
                preResetTauntMask |= tauntMaskBit(s_net->slots[i].entryId, s_net->slots[i].partner);
            }
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m != NULL && floorhugIsTauntStatus(static_cast<s16>(game::fighterStatus(m))))
            {
                preResetTauntMask |= tauntMaskBit(refs[i].entryId, false);
            }
            if (ramOk(refs[i].partner) && refs[i].partner != refs[i].fighter)
            {
                const soModuleEnumeration* mp = game::fighterModules(refs[i].partner);
                if (mp != NULL && floorhugIsTauntStatus(static_cast<s16>(game::fighterStatus(mp))))
                {
                    preResetTauntMask |= tauntMaskBit(refs[i].entryId, true);
                }
            }
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

        // Only arm the taunt/AppealHi reset guard during Floorhug or when a
        // fighter was actually in taunt right before reset, so non-taunt
        // Training Lab savestates (e.g. Meta Knight Dimensional Cape, ledge
        // invuln, or solo-standby hidden CPU) are never disturbed.
        bool floorhugActive = (drillHudActiveId() == drills::kDrillFloorhug);
        if (floorhugActive || preResetTauntMask != 0u)
        {
            s_net->preResetTauntMask = preResetTauntMask;
            s_net->floorhugGuardActive = floorhugActive ? 1 : 0;
            s_net->tauntGuardFrames = kFhTauntGuardFrames;
            scanInterruptTaunts(true);
        }
        else
        {
            s_net->tauntGuardFrames = 0;
            s_net->preResetTauntMask = 0u;
            s_net->floorhugGuardActive = 0;
        }
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

        if (s_net->tauntGuardFrames > 0)
        {
            scanInterruptTaunts(false);
            s_net->tauntGuardFrames--;
            if (s_net->tauntGuardFrames == 0)
            {
                s_net->preResetTauntMask = 0u;
                s_net->floorhugGuardActive = 0;
            }
        }

        game::FighterRef refs[game::kMaxFighters];
        int refCount = game::listFighters(refs, game::kMaxFighters);

        StageGeometry& geom = s_net->cachedGeom;
        memset(&geom, 0, sizeof(geom));
        bool haveGeom = queryStageGeometry(geom, false, true);
        s_net->haveGeom = haveGeom ? 1 : 0;

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
            slot.inTaunt = floorhugIsTauntStatus(cf.status) ? 1 : 0;

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

    // Draws 3D GX world-space ledge-grab box and sweetspot ceiling guides on
    // both ledges while Universal Recovery Sweetspot challenge is active.
    void challengeBackendsDraw3d(const game::MatchState& match)
    {
        if (s_net == NULL || !match.running || match.paused || lab::menuOpen()
            || drillHudActiveId() != drills::kDrillRecoverySweetspot || !s_net->haveGeom)
        {
            return;
        }
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!ramOk(camMgr))
        {
            return;
        }

        float offX = -4.0f, offY = 9.0f, cw = 10.0f, ch = 10.0f;
        bool overshoot = false;
        const ChallengeSlot* hmnSlot = NULL;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t != NULL && t->used && !t->partner && !t->cpu && s_net->slots[i].active)
            {
                hmnSlot = &s_net->slots[i];
                const soModuleEnumeration* m = game::fighterModules(t->fighter);
                if (m != NULL && ramOk(m->m_groundModule))
                {
                    grCollStatus* cs = m->m_groundModule->getCollStatus(0);
                    if (ramOk(cs))
                    {
                        const float* f = reinterpret_cast<const float*>(reinterpret_cast<u32>(cs) + kCollCliffBoxOffset);
                        offX = f[0];
                        offY = f[1];
                        cw = f[2];
                        ch = f[3];
                    }
                }
                break;
            }
        }
        if (hmnSlot != NULL && hmnSlot->rf.offstageActive && hmnSlot->rf.sawPeakNearLip
            && hmnSlot->rf.maxHeightAboveLip > kRecSweetspotMaxPeakAboveLip)
        {
            overshoot = true;
        }

        RecoveryGuideGeom guide;
        if (!computeRecoveryGuideGeom(s_net->cachedGeom, offX, offY, cw, ch, guide))
        {
            return;
        }

        const gfx::Rgba kBoxTop = 0x22B8CC48u;
        const gfx::Rgba kBoxBot = 0x0C48589Cu;
        const gfx::Rgba kBoxEdge = 0x4CE6F0F0u;
        const gfx::Rgba kBandFill = overshoot ? 0xFF66583Cu : 0x4CE68834u;
        const gfx::Rgba kRailCol = overshoot ? 0xFF6658F0u : 0xFFD300F0u;
        const gfx::Rgba kLipCol = 0xFFFFFFFFu;

        camMgr->m_cameras[0].setGX();
        gfx::forgetState();
        gfx::prepare();

        // Translucent ledge-grab pocket + sweetspot band on both ledges.
        GXBegin(GX_QUADS, GX_VTXFMT1, 16);
        for (int s = 0; s < 2; s++)
        {
            const RecoveryLedgeGuide& d = guide.sides[s];
            gfx::vertex(d.grabMinX, d.grabMaxY, kBoxTop);
            gfx::vertex(d.grabMaxX, d.grabMaxY, kBoxTop);
            gfx::vertex(d.grabMaxX, d.grabMinY, kBoxBot);
            gfx::vertex(d.grabMinX, d.grabMinY, kBoxBot);

            gfx::vertex(d.sweetMinX, d.sweetCeilY, kBandFill);
            gfx::vertex(d.sweetMaxX, d.sweetCeilY, kBandFill);
            gfx::vertex(d.sweetMaxX, d.lipY, kBandFill);
            gfx::vertex(d.sweetMinX, d.lipY, kBandFill);
        }
        GXEnd();

        // Ledge-grab box outline + sweetspot ceiling target line + lip tick.
        gfx::lineBegin(GX_LINES, 36, 14);
        for (int s = 0; s < 2; s++)
        {
            const RecoveryLedgeGuide& d = guide.sides[s];
            // Grab box rectangle (4 segments)
            gfx::vertex(d.grabMinX, d.grabMaxY, kBoxEdge);
            gfx::vertex(d.grabMaxX, d.grabMaxY, kBoxEdge);
            gfx::vertex(d.grabMinX, d.grabMinY, kBoxEdge);
            gfx::vertex(d.grabMaxX, d.grabMinY, kBoxEdge);
            gfx::vertex(d.grabMinX, d.grabMinY, kBoxEdge);
            gfx::vertex(d.grabMinX, d.grabMaxY, kBoxEdge);
            gfx::vertex(d.grabMaxX, d.grabMinY, kBoxEdge);
            gfx::vertex(d.grabMaxX, d.grabMaxY, kBoxEdge);
            // Sweetspot ceiling rail + end ticks (3 segments)
            gfx::vertex(d.sweetMinX, d.sweetCeilY, kRailCol);
            gfx::vertex(d.sweetMaxX, d.sweetCeilY, kRailCol);
            gfx::vertex(d.sweetMinX, d.lipY, kRailCol);
            gfx::vertex(d.sweetMinX, d.sweetCeilY, kRailCol);
            gfx::vertex(d.sweetMaxX, d.lipY, kRailCol);
            gfx::vertex(d.sweetMaxX, d.sweetCeilY, kRailCol);
            // Ledge lip horizontal + vertical crosshair (2 segments)
            gfx::vertex(d.lipX - 2.0f, d.lipY, kLipCol);
            gfx::vertex(d.lipX + 2.0f, d.lipY, kLipCol);
            gfx::vertex(d.lipX, d.lipY - 2.0f, kLipCol);
            gfx::vertex(d.lipX, d.lipY + 2.0f, kLipCol);
        }
        GXEnd();

        GXSetLineWidth(6, 0);
        gfx::forgetState();
        s_net->guideDrawCalls++;
    }

    const CancelSprintState* challengeCancelSprint(int trackedSlot)
    {
        ChallengeSlot* s = slotAt(trackedSlot);
        return s != NULL ? &s->cs : NULL;
    }

    const FlowCourseState* challengeFlowCourse(int trackedSlot)
    {
        ChallengeSlot* s = slotAt(trackedSlot);
        return s != NULL ? &s->fc : NULL;
    }

    const ReactionTechchaseState* challengeReactionTechchase(int trackedSlot)
    {
        ChallengeSlot* s = slotAt(trackedSlot);
        return s != NULL ? &s->rt : NULL;
    }

    const RecoveryFloorhugState* challengeRecoveryFloorhug(int trackedSlot)
    {
        ChallengeSlot* s = slotAt(trackedSlot);
        return s != NULL ? &s->rf : NULL;
    }

    const SnakeRecoveryState* challengeSnakeRecovery(int trackedSlot)
    {
        ChallengeSlot* s = slotAt(trackedSlot);
        return s != NULL ? &s->snk : NULL;
    }

    u32 challengeRecoveryGuideDrawCalls() { return s_net != NULL ? s_net->guideDrawCalls : 0u; }
    u32 challengeTauntInterruptCount() { return s_net != NULL ? s_net->tauntInterrupts : 0u; }

    const ChallengeDebugBlock* challengeDebugBlock()
    {
        return s_net != NULL ? &s_net->debug : NULL;
    }
}
}
