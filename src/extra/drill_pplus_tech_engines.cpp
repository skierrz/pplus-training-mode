#include "extra/drill_pplus_tech_engines.h"

#include <ft/fighter.h>
#include <so/controller/so_controller_module_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <string.h>

#include "extra/net_alloc.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Live P+ 3.2.1 runtime adapter feeding all five pure detection backends from
// tracker frames, controller/posture/motion modules, and LA/RA work variables.
// Strictly read-only: never mutates fighter state, work variables, or inputs.
namespace tm {
namespace extra {
    namespace {
        enum {
            kPosturePos = 0x0C,
            kPosturePrevPos = 0x18,
            kPostureLr = 0x40,
            kWorkInstance = 0x20,
            kWorkStatus = 0x24,
            kWorkInts = 0x0C,
            kWorkIntCount = 0x10,
            kWorkFloats = 0x14,
            kWorkFloatCount = 0x18,
            kWorkFlags = 0x1C,
            kWorkFlagCount = 0x20,
            kLaStartTurnFrame = 55,  // LA-Basic[55] (0x10000037): B-Reverse countdown
            kLaAgtChainCount = 81,   // LA-Basic[81] (0x10000051): consecutive mid-air AGT count
            kLaTauntLockout = 82,    // LA-Basic[82] (0x10000052): frames since prior taunt press
            kLaCStickRelX = 34,      // LA-Float[34] (0x88): C-Stick relative X
            kLaCStickY = 35,         // LA-Float[35] (0x8C): C-Stick Y
            kRaDacusFrame = 1,       // RA-Basic[1] (0x20000001): Attack_Dash cancel counter
            kParamLandingFrame = 3074 // IC-Basic[3074]: Customize_Param_Float_Landing_Frame
        };

        struct SlotEngine {
            bool active;
            s32 entryId;
            bool partner;
            s8 playerNo;
            const Fighter* fighter;
            u32 taskId;
            RarState rar;
            DacusState dacus;
            BReverseState brev;
            FootstoolAgtState fa;
            FloorhugState fh;
        };

        struct SlotSnap {
            bool valid;
            const game::Tracked* t;
            float x, y;
            float vx, vy;
            float lr;
            float stickX, stickY;
            float cStickX, cStickY;
            float motionFrame, motionRate;
            float landingFrame;
            u32 buttons;
            u32 triggerButtons;
            u32 raFlag0;
            s16 motionKind;
            s32 laBasic55;
            s32 laBasic81;
            s32 laBasic82;
            s32 raBasic1;
            u8 outOfPlay;
            u8 hitDealt;
            s8 hitDealtTarget;
            s8 attackerSlot;
        };

        SlotEngine* s_engines = NULL;
        PPlusTechDebugBlock s_debug;
        u32 s_resetCount = 0;
        s32 s_prevFrame = game::kNoFrame;

        float floatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        const void* workBank(const soModuleEnumeration* m, u32 which)
        {
            if (m == NULL || !game::isRam(m->m_workManageModule))
            {
                return NULL;
            }
            const void* w = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, which));
            return game::isRam(w) ? w : NULL;
        }

        s32 statusInt(const soModuleEnumeration* m, u32 index)
        {
            const void* w = workBank(m, kWorkStatus);
            if (w == NULL || index >= game::wordAt(w, kWorkIntCount))
            {
                return 0;
            }
            const s32* ints = reinterpret_cast<const s32*>(game::wordAt(w, kWorkInts));
            return game::isRam(ints) ? ints[index] : 0;
        }

        float instanceFloat(const soModuleEnumeration* m, u32 index)
        {
            const void* w = workBank(m, kWorkInstance);
            if (w == NULL || index >= game::wordAt(w, kWorkFloatCount))
            {
                return 0.0f;
            }
            const float* floats = reinterpret_cast<const float*>(game::wordAt(w, kWorkFloats));
            if (!game::isRam(floats))
            {
                return 0.0f;
            }
            float v = floats[index];
            return (v >= -1.5f && v <= 1.5f) ? v : 0.0f;
        }

        u32 statusFlagWord(const soModuleEnumeration* m, u32 wordIndex)
        {
            const void* w = workBank(m, kWorkStatus);
            if (w == NULL || wordIndex >= game::wordAt(w, kWorkFlagCount))
            {
                return 0;
            }
            const u32* flags = reinterpret_cast<const u32*>(game::wordAt(w, kWorkFlags));
            return game::isRam(flags) ? flags[wordIndex] : 0;
        }

        float readLandingFrame(Fighter* fighter)
        {
            const float kDefaultLandingFrame = 4.0f;
            if (!game::isRam(fighter))
            {
                return kDefaultLandingFrame;
            }
            soModuleAccesser* accesser = fighter->m_moduleAccesser;
            if (!game::isRam(accesser))
            {
                return kDefaultLandingFrame;
            }
            float lf = soValueAccesser::getConstantFloat(accesser, kParamLandingFrame, 0);
            return (lf >= 0.0f && lf <= 30.0f) ? lf : kDefaultLandingFrame;
        }

        void resetSlotEngine(SlotEngine& e)
        {
            e.active = false;
            e.entryId = -1;
            e.partner = false;
            e.playerNo = -1;
            e.fighter = NULL;
            e.taskId = 0;
            rarReset(e.rar);
            dacusReset(e.dacus);
            breverseReset(e.brev);
            footstoolAgtReset(e.fa);
            floorhugReset(e.fh);
        }

        void clearDebugSlot(int playerNo)
        {
            if (playerNo >= 0 && playerNo < kPPlusTechDebugSlots)
            {
                memset(&s_debug.slots[playerNo], 0, sizeof(s_debug.slots[playerNo]));
            }
        }

        void clearAllTransient(bool countReset)
        {
            if (s_engines != NULL)
            {
                for (int i = 0; i < game::kMaxTracked; i++)
                {
                    resetSlotEngine(s_engines[i]);
                }
            }
            if (countReset)
            {
                s_resetCount++;
            }
            memset(&s_debug, 0, sizeof(s_debug));
            s_debug.magic = kPPlusTechDebugMagic;
            s_debug.version = kPPlusTechDebugVersion;
            s_debug.resetCount = s_resetCount;
        }

        bool readSlotSnap(int i, s32 f, SlotSnap& out)
        {
            memset(&out, 0, sizeof(out));
            out.hitDealtTarget = -1;
            out.attackerSlot = -1;
            out.motionKind = -1;
            out.landingFrame = 4.0f;

            const game::Tracked* t = game::tracked(i);
            if (t == NULL || !t->used || t->cur.frame != f || !game::isRam(t->fighter))
            {
                return false;
            }
            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            if (m == NULL || !game::isRam(m->m_postureModule))
            {
                return false;
            }

            const void* posture = m->m_postureModule;
            out.x = floatAt(posture, kPosturePos);
            out.y = floatAt(posture, kPosturePos + 4);
            float prevX = floatAt(posture, kPosturePrevPos);
            float prevY = floatAt(posture, kPosturePrevPos + 4);
            out.vx = out.x - prevX;
            out.vy = out.y - prevY;
            float rawLr = floatAt(posture, kPostureLr);
            out.lr = rawLr < 0.0f ? -1.0f : 1.0f;

            game::Body body;
            if (game::readBody(t->fighter, body))
            {
                out.stickX = body.stickX;
                out.stickY = body.stickY;
                out.buttons = body.buttons;
                out.motionKind = static_cast<s16>(body.motionKind);
            }

            if (game::isRam(m->m_controllerModule))
            {
                out.triggerButtons = m->m_controllerModule->getTrigger().m_mask;
            }
            if (game::isRam(m->m_motionModule))
            {
                out.motionFrame = m->m_motionModule->getFrame();
                out.motionRate = m->m_motionModule->getRate();
            }

            out.cStickX = instanceFloat(m, kLaCStickRelX);
            out.cStickY = instanceFloat(m, kLaCStickY);
            out.laBasic55 = game::instanceInt(m, kLaStartTurnFrame);
            out.laBasic81 = game::instanceInt(m, kLaAgtChainCount);
            out.laBasic82 = game::instanceInt(m, kLaTauntLockout);
            out.raBasic1 = (t->cur.status == kDacusStatusAttackDash) ? statusInt(m, kRaDacusFrame) : 0;
            out.raFlag0 = statusFlagWord(m, 0);
            out.landingFrame = readLandingFrame(t->fighter);
            out.outOfPlay = game::outOfPlay(t->cur.status, true) ? 1 : 0;

            if (t->lastDealt.frame == f && !t->lastDealt.shield)
            {
                out.hitDealt = 1;
                out.hitDealtTarget = t->lastDealt.other;
            }
            if (t->lastReceived.frame == f && !t->lastReceived.shield)
            {
                out.attackerSlot = t->lastReceived.other;
            }

            out.t = t;
            out.valid = true;
            return true;
        }

        void mirrorToDebugSlot(int playerNo, const SlotEngine& e)
        {
            if (playerNo < 0 || playerNo >= kPPlusTechDebugSlots)
            {
                return;
            }
            PPlusTechDebugSlot& d = s_debug.slots[playerNo];

            // Engine 1: RAR
            d.rarAttempts = e.rar.attempts;
            d.rarBairCount = e.rar.successBairCount;
            d.rarOutcome = e.rar.last.outcome;
            d.rarTurnRunFrames = e.rar.last.turnRunFrames;
            d.rarBairDelay = e.rar.last.bairDelay;
            d.rarBackwardMomentum = e.rar.last.backwardMomentum;
            d.rarMomentumRetained = e.rar.last.momentumRetained;
            d.rarTakeoffVx = e.rar.last.takeoffVx;

            // Engine 2: DACUS & Gatling
            d.dacusAttempts = e.dacus.attempts;
            d.dacusSuccessCount = e.dacus.successCount;
            d.dacusGatlingCount = e.dacus.gatlingCount;
            d.dacusOutcome = e.dacus.last.outcome;
            d.dacusRawDashFrame = e.dacus.last.rawDashFrame;
            d.dacusNonHitlagFrame = e.dacus.last.nonHitlagDashFrame;
            d.dacusGatling = e.dacus.last.gatling;
            d.dacusVxCarry = e.dacus.last.vxCarry;
            d.dacusSlideDistance = e.dacus.last.slideDistance;

            // Engine 3: B-Reverse & Wavebounce
            d.brevAttempts = e.brev.attempts;
            d.brevBReverseCount = e.brev.bReverseCount;
            d.brevWavebounceCount = e.brev.wavebounceCount;
            d.brevTurnaroundCount = e.brev.turnaroundCount;
            d.brevOutcome = e.brev.last.outcome;
            d.brevEarlyFlick = e.brev.last.earlyFlickFrames;
            d.brevLateFlick = e.brev.last.lateFlickFrames;
            d.brevAirborne = e.brev.last.airborne;
            d.brevVxBefore = e.brev.last.vxBefore;
            d.brevVxAfter = e.brev.last.vxAfter;
            d.brevMomentumRatio = e.brev.last.momentumRatio;

            // Engine 4: Footstool & Lock + AGT
            d.footstoolCount = e.fa.footstoolCount;
            d.footstoolLockCount = e.fa.lockCount;
            d.agtCount = e.fa.agtCount;
            d.agtGroundedCount = e.fa.groundedGlideTossCount;
            d.footstoolOutcome = e.fa.lastFootstool.outcome;
            d.footstoolShortHop = e.fa.lastFootstool.shortHop;
            d.footstoolVictimTeched = e.fa.lastFootstool.victimTeched;
            d.footstoolLockHit = e.fa.lastFootstool.footstoolLockHit;
            d.footstoolTauntLockout = e.fa.lastFootstool.tauntLockoutFrames;
            d.agtCurrentChain = e.fa.currentAgtChain;
            d.agtIsAerial = e.fa.lastAgt.isAerial;
            d.agtThrowFrame = e.fa.lastAgt.throwFrame;
            d.agtLastChainCount = static_cast<u8>(e.fa.lastAgt.agtChainCount > 255
                                                      ? 255
                                                      : (e.fa.lastAgt.agtChainCount < 0 ? 0 : e.fa.lastAgt.agtChainCount));
            d.agtChainResetCount = static_cast<u8>(e.fa.agtChainResetCount > 255 ? 255 : e.fa.agtChainResetCount);
            d.agtVxCarry = e.fa.lastAgt.vxCarry;
            d.agtVyCarry = e.fa.lastAgt.vyCarry;
            d.footstoolFailLockoutCount = e.fa.failLockoutCount;

            // Engine 5: Floorhug / ASDI-Down & Crouch-Cancel
            d.fhHitCount = e.fh.hitCount;
            d.fhCancelCount = e.fh.cancelCount;
            d.fhTruePunishCount = e.fh.truePunishCount;
            d.fhOutcome = e.fh.last.outcome;
            d.fhWasCrouching = e.fh.last.wasCrouching;
            d.fhAsdiDown = e.fh.last.asdiDown;
            d.fhAsdiFromCStick = e.fh.last.asdiFromCStick;
            d.fhHitstunAtLanding = e.fh.last.hitstunAtLanding;
            d.fhHitstunSaved = e.fh.last.hitstunSaved;
            d.fhPunishActFrame = e.fh.last.punishActFrame;
            d.fhTrueCounterPunish = e.fh.last.trueCounterPunish;
            d.fhPad = 0;
            d.fhEffectiveAsdiX = e.fh.last.effectiveAsdiX;
            d.fhEffectiveAsdiY = e.fh.last.effectiveAsdiY;
            d.fhLandingFrame = e.fh.last.landingFrame;
        }
    }

    void pplusTechAttach()
    {
        freeNetPtr(s_engines);
        s_engines = allocNetArray<SlotEngine>(game::kMaxTracked);
        s_resetCount = 0;
        s_prevFrame = game::kNoFrame;
        clearAllTransient(false);
        TM_LOG("pplus_tech: debug block at %08x\n", &s_debug);
    }

    void pplusTechDetach()
    {
        s_prevFrame = game::kNoFrame;
        clearAllTransient(false);
        freeNetPtr(s_engines);
    }

    void pplusTechReset()
    {
        s_prevFrame = game::kNoFrame;
        clearAllTransient(true);
    }

    void pplusTechFrame()
    {
        if (s_engines == NULL)
        {
            return;
        }
        const s32 f = game::trackerFrame();
        if (s_prevFrame != game::kNoFrame && f <= s_prevFrame)
        {
            clearAllTransient(true);
        }
        s_prevFrame = f;
        s_debug.frame = static_cast<u32>(f >= 0 ? f : 0);

        SlotSnap snaps[game::kMaxTracked];
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            readSlotSnap(i, f, snaps[i]);
        }

        bool slotChanged[game::kMaxTracked];
        bool anySlotChanged = false;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            slotChanged[i] = false;
            SlotEngine& e = s_engines[i];
            const SlotSnap& sn = snaps[i];
            if (!sn.valid)
            {
                if (e.active)
                {
                    if (!e.partner)
                    {
                        clearDebugSlot(e.playerNo);
                    }
                    resetSlotEngine(e);
                    slotChanged[i] = true;
                    anySlotChanged = true;
                }
                continue;
            }

            const game::Tracked& t = *sn.t;
            if (!e.active || e.entryId != t.entryId || e.partner != t.partner
                || e.fighter != t.fighter || e.taskId != t.taskId || e.playerNo != t.playerNo)
            {
                if (e.active && !e.partner)
                {
                    clearDebugSlot(e.playerNo);
                }
                resetSlotEngine(e);
                e.active = true;
                e.entryId = t.entryId;
                e.partner = t.partner;
                e.playerNo = t.playerNo;
                e.fighter = t.fighter;
                e.taskId = t.taskId;
                if (!t.partner)
                {
                    clearDebugSlot(t.playerNo);
                }
                slotChanged[i] = true;
                anySlotChanged = true;
            }
        }

        if (anySlotChanged)
        {
            for (int k = 0; k < game::kMaxTracked; k++)
            {
                SlotEngine& e = s_engines[k];
                if (!e.active)
                {
                    continue;
                }
                if (e.fa.watchingVictim && e.fa.watchVictimSlot >= 0
                    && e.fa.watchVictimSlot < game::kMaxTracked && slotChanged[e.fa.watchVictimSlot])
                {
                    e.fa.watchingVictim = 0;
                    e.fa.watchVictimSlot = -1;
                    e.fa.victimWasDown = 0;
                }
                if (e.fh.attackerSlot >= 0 && e.fh.attackerSlot < game::kMaxTracked
                    && slotChanged[e.fh.attackerSlot])
                {
                    if (e.fh.phase == kFhPhasePunishWatch)
                    {
                        e.fh.phase = kFhPhaseIdle;
                    }
                    e.fh.attackerSlot = -1;
                }
            }
        }

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            if (!snaps[i].valid)
            {
                continue;
            }
            SlotEngine& e = s_engines[i];
            const SlotSnap& sn = snaps[i];
            const game::Tracked& t = *sn.t;

            const u8 evN = t.eventCount < game::kMaxEvents ? t.eventCount : static_cast<u8>(game::kMaxEvents);

            // 1. RAR
            RarFrameInput rarIn;
            memset(&rarIn, 0, sizeof(rarIn));
            rarIn.frame = f;
            rarIn.status = t.cur.status;
            rarIn.motionKind = sn.motionKind;
            rarIn.hitstun = t.cur.hitstun;
            rarIn.airborne = t.cur.airborne;
            rarIn.outOfPlay = sn.outOfPlay;
            rarIn.lr = sn.lr;
            rarIn.vx = sn.vx;
            rarIn.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                rarIn.events[k].status = t.events[k].status;
                rarIn.events[k].prev = t.events[k].prev;
            }
            rarStep(e.rar, rarIn);

            // 2. DACUS & Gatling
            DacusFrameInput dacusIn;
            memset(&dacusIn, 0, sizeof(dacusIn));
            dacusIn.frame = f;
            dacusIn.status = t.cur.status;
            dacusIn.hitlag = t.cur.hitlag;
            dacusIn.prevHitlag = t.prev.hitlag;
            dacusIn.hitstun = t.cur.hitstun;
            dacusIn.outOfPlay = sn.outOfPlay;
            dacusIn.hasLiveRaBasic1 = (t.cur.status == kDacusStatusAttackDash) ? 1 : 0;
            dacusIn.raBasic1 = sn.raBasic1;
            dacusIn.x = sn.x;
            dacusIn.vx = sn.vx;
            dacusIn.stickY = sn.stickY;
            dacusIn.cStickY = sn.cStickY;
            dacusIn.buttons = sn.buttons;
            dacusIn.triggerButtons = sn.triggerButtons;
            dacusIn.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                dacusIn.events[k].status = t.events[k].status;
                dacusIn.events[k].prev = t.events[k].prev;
            }
            dacusStep(e.dacus, dacusIn);

            // 3. B-Reverse & Wavebounce
            BReverseFrameInput brevIn;
            memset(&brevIn, 0, sizeof(brevIn));
            brevIn.frame = f;
            brevIn.status = t.cur.status;
            brevIn.hitstun = t.cur.hitstun;
            brevIn.airborne = t.cur.airborne;
            brevIn.outOfPlay = sn.outOfPlay;
            brevIn.laBasic55 = sn.laBasic55;
            brevIn.lr = sn.lr;
            brevIn.vx = sn.vx;
            brevIn.stickX = sn.stickX;
            brevIn.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                brevIn.events[k].status = t.events[k].status;
                brevIn.events[k].prev = t.events[k].prev;
            }
            breverseStep(e.brev, brevIn);

            // 4. Footstool & Lock + AGT
            FootstoolAgtFrameInput faIn;
            memset(&faIn, 0, sizeof(faIn));
            faIn.frame = f;
            faIn.selfSlot = static_cast<s8>(i);
            faIn.status = t.cur.status;
            faIn.hitstun = t.cur.hitstun;
            faIn.airborne = t.cur.airborne;
            faIn.outOfPlay = sn.outOfPlay;
            faIn.attackActive = t.cur.attackActive;
            faIn.hitDealt = sn.hitDealt;
            faIn.hitDealtTarget = sn.hitDealtTarget;
            faIn.x = sn.x;
            faIn.y = sn.y;
            faIn.vx = sn.vx;
            faIn.vy = sn.vy;
            faIn.buttons = sn.buttons;
            faIn.triggerButtons = sn.triggerButtons;
            faIn.statusFlags0 = sn.raFlag0;
            faIn.laBasic81 = sn.laBasic81;
            faIn.laBasic82 = sn.laBasic82;
            faIn.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                faIn.events[k].status = t.events[k].status;
                faIn.events[k].prev = t.events[k].prev;
                faIn.events[k].flags = t.events[k].flags;
            }
            for (int j = 0; j < game::kMaxTracked && faIn.opponentCount < kFaMaxOpponents; j++)
            {
                if (j == i || !snaps[j].valid || snaps[j].t->entryId == t.entryId)
                {
                    continue;
                }
                const SlotSnap& os = snaps[j];
                const game::Tracked& ot = *os.t;
                FaOpponentView& ov = faIn.opponents[faIn.opponentCount++];
                ov.slot = static_cast<s8>(j);
                ov.status = ot.cur.status;
                ov.prevStatus = ot.prev.status;
                ov.airborne = ot.cur.airborne;
                ov.outOfPlay = os.outOfPlay;
                ov.hitReceived = ot.cur.hitReceived;
                ov.shieldReceived = ot.cur.shieldReceived;
                ov.lastReceivedOther = (ot.lastReceived.frame == f) ? ot.lastReceived.other : -1;
                ov.x = os.x;
                ov.y = os.y;
                u8 oevN = ot.eventCount < kFaMaxEvents ? ot.eventCount : static_cast<u8>(kFaMaxEvents);
                ov.eventCount = oevN;
                for (int k = 0; k < oevN; k++)
                {
                    ov.events[k].status = ot.events[k].status;
                    ov.events[k].prev = ot.events[k].prev;
                    ov.events[k].flags = ot.events[k].flags;
                }
            }
            footstoolAgtStep(e.fa, faIn);

            // 5. Floorhug / ASDI-Down & Crouch-Cancel
            FloorhugFrameInput fhIn;
            memset(&fhIn, 0, sizeof(fhIn));
            fhIn.frame = f;
            fhIn.status = t.cur.status;
            fhIn.hitlag = t.cur.hitlag;
            fhIn.prevHitlag = t.prev.hitlag;
            fhIn.hitstun = t.cur.hitstun;
            fhIn.prevHitstun = t.prev.hitstun;
            fhIn.airborne = t.cur.airborne;
            fhIn.actionable = t.cur.actionable;
            fhIn.hitReceived = t.cur.hitReceived;
            fhIn.shieldReceived = t.cur.shieldReceived;
            fhIn.hitDealt = sn.hitDealt;
            fhIn.hitDealtTarget = sn.hitDealtTarget;
            fhIn.attackerSlot = sn.attackerSlot;
            fhIn.outOfPlay = sn.outOfPlay;
            fhIn.lr = sn.lr;
            fhIn.stickX = sn.stickX;
            fhIn.stickY = sn.stickY;
            fhIn.cStickX = sn.cStickX;
            fhIn.cStickY = sn.cStickY;
            fhIn.motionFrame = sn.motionFrame;
            fhIn.motionRate = sn.motionRate;
            fhIn.landingFrame = sn.landingFrame;
            fhIn.eventCount = evN;
            for (int k = 0; k < evN; k++)
            {
                fhIn.events[k].status = t.events[k].status;
                fhIn.events[k].prev = t.events[k].prev;
            }
            for (int j = 0; j < game::kMaxTracked && fhIn.attackerCount < kFhMaxAttackers; j++)
            {
                if (j == i || !snaps[j].valid || snaps[j].t->entryId == t.entryId)
                {
                    continue;
                }
                const game::Tracked& at = *snaps[j].t;
                FhAttackerView& av = fhIn.attackers[fhIn.attackerCount++];
                av.slot = static_cast<s8>(j);
                av.status = at.cur.status;
                av.hitlag = at.cur.hitlag;
                av.hitstun = at.cur.hitstun;
                av.actionable = at.cur.actionable;
                av.outOfPlay = snaps[j].outOfPlay;
            }
            floorhugStep(e.fh, fhIn);

            if (!t.partner)
            {
                mirrorToDebugSlot(t.playerNo, e);
            }
        }
    }

    const RarState* pplusTechRar(int trackedSlot)
    {
        return (s_engines != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked && s_engines[trackedSlot].active)
                   ? &s_engines[trackedSlot].rar
                   : NULL;
    }

    const DacusState* pplusTechDacus(int trackedSlot)
    {
        return (s_engines != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked && s_engines[trackedSlot].active)
                   ? &s_engines[trackedSlot].dacus
                   : NULL;
    }

    const BReverseState* pplusTechBReverse(int trackedSlot)
    {
        return (s_engines != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked && s_engines[trackedSlot].active)
                   ? &s_engines[trackedSlot].brev
                   : NULL;
    }

    const FootstoolAgtState* pplusTechFootstoolAgt(int trackedSlot)
    {
        return (s_engines != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked && s_engines[trackedSlot].active)
                   ? &s_engines[trackedSlot].fa
                   : NULL;
    }

    const FloorhugState* pplusTechFloorhug(int trackedSlot)
    {
        return (s_engines != NULL && trackedSlot >= 0 && trackedSlot < game::kMaxTracked && s_engines[trackedSlot].active)
                   ? &s_engines[trackedSlot].fh
                   : NULL;
    }

    const PPlusTechDebugBlock* pplusTechDebugBlock()
    {
        return &s_debug;
    }
}
}
