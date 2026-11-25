#include "extra/stage_options.h"

#include <math.h>
#include <string.h>
#include <gf/gf_archive.h>
#include <gf/gf_model.h>
#include <gf/gf_module.h>
#include <gm/gm_global.h>
#include <gr/collision/gr_collision.h>
#include <gr/collision/gr_collision_joint.h>
#include <gr/ground.h>
#include <nw4r/g3d/g3d_resfile.h>
#include <st/st_common_gimmick.h>
#include <st/st_positions.h>
#include <st/st_trigger.h>
#include <st/stage.h>

#include "extra/stage_options_logic.h"
#include "main/extra.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kMaxGroundWalk = 64,
            kMaxCollWalk = 64,
            kMaxMovingGrounds = 8,
            // g_grCollisionList (0x8049E57C, sora_melee grCollision update 0x8011269C):
            // +0 count, +4 sentinel LinkListNode; node is at grCollision + 0x30 (48).
            kGrCollisionListAddr = 0x8049E57C,
            kGrCollisionNodeOffset = 0x30,
            // g_ecMgr (0x805A0148), ecMgr::endEffect(u32) (0x80060C54), and
            // cmRemoveQuake(int) (0x8009DD40, RSBE01.lst).
            kEcMgrPtrAddr = 0x805A0148,
            kEcEndEffectAddr = 0x80060C54,
            kCmRemoveQuakeAddr = 0x8009DD40,
            // g_GameGlobal->m_modeMelee + 0x29 bit 0x20: P+ m_isHazardOff (Random.asm:1024-1031).
            kModeMeleeHazardOffset = 0x29,
            kModeMeleeHazardOffBit = 0x20,
            // Ground + 0x6D bit 0x10: Ground::m_isPauseAnim (Ground::processAnim 0x8096A658).
            kGroundPauseAnimOffset = 0x6D,
            kGroundPauseAnimBit = 0x10,
            // grCollisionJoint + 0x54 bit 0x80: cleared to reset prev=cur vertices
            // and suppress phantom carry velocity on instant teleports (0x8011375C).
            kJointCarryOffset = 0x54,
            kJointCarryBit = 0x80,
            // STDT header magic ("STDT") and payload offset into stDataContainerData (testStageDataInit 0x8092DDB4).
            kStdtMagic = 0x53544454,
            kStdtPayloadOffset = 0x14,
            // grIzumiSpout offsets (st_dxfountain_ht.rel .text + 0x7F8..0x1274).
            kSpoutStateOffset = 0x160,
            kSpoutTimerOffset = 0x164,
            kSpoutCurChrOffset = 0x1A8,
            kSpoutTargetFrameOffset = 0x1B8,
            // grGreenhillBreak (st_greenhill_ht.rel +0xB748) & grVillageAshiba (st_smashville_ht.rel +0x2764).
            kGhzCurAnimOffset = 0x174,
            kSvCurAnimOffset = 0x168,
            // grDxGreensWhispy offsets (st_dreamland64_ht.rel .text + 0xAEA0..0xBB50).
            kWhispySubStateOffset = 0x150,
            kWhispyTimerOffset = 0x154,
            kWhispyEfIdOffset = 0x158,
            kWhispyStateOffset = 0x15C,
            kWhispyAnimWorkOffset = 0x164,
            kWhispyTriggerOffset = 0x16C,
            // Quiet NaN bit pattern for Whispy's cooldown timer (matches STDT[21] -inf - -inf).
            kQuietNaN = 0x7FC00000
        };

        typedef void (*VSetAnim4)(Ground*, u32, u32, u32, u32);
        typedef void (*VSetFrame1)(Ground*, float);
        typedef void (*VWhispyAnim)(Ground*, u32, u32, u32, void*);
        typedef void (*EcEndEffectFn)(void*, u32);
        typedef void (*CmRemoveQuakeFn)(int);

        struct LiveStage {
            Stage* stage;
            int preset;
            bool initialHazardOff;
            bool appliedHazardOff;
            u8 appliedPlatformMode;
            u8 appliedFod[2];
            bool initialized;
            bool forceReapply;

            Ground* spout[2]; // FoD 80, 81
            Ground* whispy;   // Dream Land 2
            Ground* ashiba[2]; // Smashville 80, 81
            Ground* ghzPlat;  // GHZ / Dead Line 80
            Ground* crayonClouds[2]; // Yoshi's Island Brawl 80 (support clouds), 81 (indicator)
            Ground* moving[kMaxMovingGrounds];
            int movingCount;
        };

        static LiveStage s_live;

        static bool prefixMatchCI(const char* str, const char* prefix)
        {
            if (str == NULL || prefix == NULL)
            {
                return false;
            }
            while (*prefix != '\0')
            {
                char a = *str++;
                char b = *prefix++;
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        // Checks g_gfModuleManager->m_moduleInfos[0..15] for a loaded module
        // whose name starts with `prefix` (gfModuleManager::isLoaded 0x80027300).
        static bool moduleLoadedPrefix(const char* prefix)
        {
            if (!game::isRam(g_gfModuleManager))
            {
                return false;
            }
            const u8* base = reinterpret_cast<const u8*>(g_gfModuleManager) + 8;
            for (int i = 0; i < 16; i++)
            {
                const u8* info = base + i * 60;
                u8 flags = info[25];
                if ((flags & 0x10) == 0)
                {
                    continue;
                }
                const char* name = reinterpret_cast<const char*>(info + 26);
                if (prefixMatchCI(name, prefix))
                {
                    return true;
                }
            }
            return false;
        }

        static bool readGameHazardOff()
        {
            if (!game::isRam(g_GameGlobal) || !game::isRam(g_GameGlobal->m_modeMelee))
            {
                return false;
            }
            const u8* mode = reinterpret_cast<const u8*>(g_GameGlobal->m_modeMelee);
            return (mode[kModeMeleeHazardOffset] & kModeMeleeHazardOffBit) != 0;
        }

        static void writeGameHazardOff(bool off)
        {
            if (!game::isRam(g_GameGlobal) || !game::isRam(g_GameGlobal->m_modeMelee))
            {
                return;
            }
            u8* mode = reinterpret_cast<u8*>(g_GameGlobal->m_modeMelee);
            if (off)
            {
                mode[kModeMeleeHazardOffset] |= kModeMeleeHazardOffBit;
            }
            else
            {
                mode[kModeMeleeHazardOffset] &= static_cast<u8>(~kModeMeleeHazardOffBit);
            }
        }

        static void restoreGlobalHazardOff()
        {
            if (shouldRestoreGlobalHazardOff(s_live.initialized, s_live.preset, s_live.initialHazardOff,
                                             s_live.appliedHazardOff))
            {
                writeGameHazardOff(s_live.initialHazardOff);
                s_live.appliedHazardOff = s_live.initialHazardOff;
            }
        }

        static void* vtableSlot(const void* obj, u32 offset)
        {
            u32 vt = game::wordAt(obj, 0x3C); // gfTask/Ground vtable pointer is at +0x3C
            if (!game::isRam(reinterpret_cast<void*>(vt)))
            {
                return NULL;
            }
            u32 fn = game::wordAt(reinterpret_cast<void*>(vt), offset);
            return game::isRam(reinterpret_cast<void*>(fn)) ? reinterpret_cast<void*>(fn) : NULL;
        }

        static bool groundHasChrAnim(Ground* g, u32 minFrames)
        {
            if (!game::isRam(g) || g->m_modelAnimNum == 0 || !game::isRam(g->m_modelAnims))
            {
                return false;
            }
            gfModelAnimation* anim = g->m_modelAnims[0];
            if (!game::isRam(anim) || !game::isRam(anim->m_anmObjChrRes))
            {
                return false;
            }
            return anim->getFrameCount() >= minFrames;
        }

        static u32 groundChrFrameCount(Ground* g)
        {
            if (!game::isRam(g) || g->m_modelAnimNum == 0 || !game::isRam(g->m_modelAnims))
            {
                return 0;
            }
            gfModelAnimation* anim = g->m_modelAnims[0];
            if (!game::isRam(anim) || !game::isRam(anim->m_anmObjChrRes))
            {
                return 0;
            }
            return anim->getFrameCount();
        }

        static bool groundHasNode(Ground* g, const char* nodeName)
        {
            if (!game::isRam(g) || g->m_modelAnimNum == 0 || !game::isRam(g->m_sceneModels))
            {
                return false;
            }
            u32 idx = 0;
            u32 count = g->m_modelAnimNum;
            if (count > 8)
            {
                count = 8;
            }
            for (u32 m = 0; m < count; m++)
            {
                if (game::isRam(g->m_sceneModels[m]) && g->getNodeIndex(&idx, m, nodeName))
                {
                    return true;
                }
            }
            return false;
        }

        static void addMovingGround(Ground* g)
        {
            if (!game::isRam(g) || s_live.movingCount >= kMaxMovingGrounds)
            {
                return;
            }
            for (int i = 0; i < s_live.movingCount; i++)
            {
                if (s_live.moving[i] == g)
                {
                    return;
                }
            }
            s_live.moving[s_live.movingCount++] = g;
        }

        // Walks g_grCollisionList and invokes `clearCarry` (when target != NULL)
        // or collects animated COLL-bound moving grounds (when target == NULL).
        static void scanCollisionJoints(Ground* clearTarget)
        {
            u32 listAddr = kGrCollisionListAddr;
            u32 sentinel = listAddr + 4;
            u32 node = game::wordAt(reinterpret_cast<void*>(sentinel), 0);
            for (int step = 0; step < kMaxCollWalk && node != sentinel && game::isRam(reinterpret_cast<void*>(node));
                 step++)
            {
                grCollision* coll = reinterpret_cast<grCollision*>(node - kGrCollisionNodeOffset);
                if (game::isRam(coll) && coll->m_jointLen > 0 && coll->m_jointLen <= 128
                    && game::isRam(coll->m_joints))
                {
                    for (int j = 0; j < coll->m_jointLen; j++)
                    {
                        grCollisionJoint* joint = &coll->m_joints[j];
                        Ground* owner = joint->m_ground;
                        if (clearTarget != NULL)
                        {
                            if (owner == clearTarget)
                            {
                                u8* flags = reinterpret_cast<u8*>(joint) + kJointCarryOffset;
                                *flags &= static_cast<u8>(~kJointCarryBit);
                            }
                        }
                        else if (game::isRam(owner) && joint->m_nodeIndex != 0xFFFF && game::isRam(joint->m_matrix)
                                 && groundHasChrAnim(owner, 2))
                        {
                            addMovingGround(owner);
                        }
                    }
                }
                node = game::wordAt(reinterpret_cast<void*>(node), 0);
            }
        }

        static void clearCarryVelocity(Ground* g)
        {
            if (game::isRam(g))
            {
                scanCollisionJoints(g);
            }
        }

        static void setPauseAnim(Ground* g, bool pause)
        {
            if (!game::isRam(g))
            {
                return;
            }
            u8* flags = reinterpret_cast<u8*>(g) + kGroundPauseAnimOffset;
            if (pause)
            {
                *flags |= kGroundPauseAnimBit;
            }
            else
            {
                *flags &= static_cast<u8>(~kGroundPauseAnimBit);
            }
        }

        static void jumpGroundToFrame(Ground* g, float frame)
        {
            if (!game::isRam(g))
            {
                return;
            }
            setPauseAnim(g, true);
            u32 n = g->m_modelAnimNum;
            if (n > 16)
            {
                n = 16;
            }
            for (u32 i = 0; i < n; i++)
            {
                g->setMotionFrame(frame, i);
            }
            clearCarryVelocity(g);
        }

        static void jumpGroundLongAnimsToFrame(Ground* g, float frame, u32 minFrames)
        {
            if (!game::isRam(g) || !game::isRam(g->m_modelAnims))
            {
                return;
            }
            setPauseAnim(g, true);
            u32 n = g->m_modelAnimNum;
            if (n > 16)
            {
                n = 16;
            }
            for (u32 i = 0; i < n; i++)
            {
                gfModelAnimation* anim = g->m_modelAnims[i];
                if (game::isRam(anim) && game::isRam(anim->m_anmObjChrRes) && anim->getFrameCount() >= minFrames)
                {
                    g->setMotionFrame(frame, i);
                }
            }
            clearCarryVelocity(g);
        }

        static void* fetchStageStdt(int miscIndex)
        {
            if (!game::isRam(g_Stage))
            {
                return NULL;
            }
            stCommonGimmick* st = static_cast<stCommonGimmick*>(g_Stage);
            if (!game::isRam(st->m_fileData))
            {
                return NULL;
            }
            void* raw = st->m_fileData->getData(Data_Type_Misc, miscIndex, 0xFFFE);
            if (!game::isRam(raw) || game::wordAt(raw, 0) != kStdtMagic)
            {
                return NULL;
            }
            return reinterpret_cast<u8*>(raw) + kStdtPayloadOffset;
        }

        // ---- Per-stage apply routines --------------------------------------

        static void applyFodSpout(int side, u8 heightChoice, bool hazardOff, bool force)
        {
            Ground* spout = s_live.spout[side];
            if (!game::isRam(spout))
            {
                return;
            }
            VSetAnim4 setAnim = reinterpret_cast<VSetAnim4>(vtableSlot(spout, 0x1E8));
            VSetFrame1 setFrame = reinterpret_cast<VSetFrame1>(vtableSlot(spout, 0x1E4));
            if (setAnim == NULL || setFrame == NULL)
            {
                return;
            }
            u32* state = reinterpret_cast<u32*>(reinterpret_cast<u8*>(spout) + kSpoutStateOffset);
            float* timer = reinterpret_cast<float*>(reinterpret_cast<u8*>(spout) + kSpoutTimerOffset);
            u32* curChr = reinterpret_cast<u32*>(reinterpret_cast<u8*>(spout) + kSpoutCurChrOffset);
            float* targetFrame = reinterpret_cast<float*>(reinterpret_cast<u8*>(spout) + kSpoutTargetFrameOffset);

            float wantedFrame = fodFrameForChoice(heightChoice);
            if (wantedFrame >= 0.0f)
            {
                bool switchedAnim = false;
                if (*curChr != 2)
                {
                    setAnim(spout, 2, 3, 2, 0);
                    switchedAnim = true;
                }
                *state = 0;
                *targetFrame = wantedFrame;
                setPauseAnim(spout, false);
                float cur = spout->getMotionFrame(0);
                if (force || switchedAnim || s_live.appliedFod[side] != heightChoice || fabsf(cur - wantedFrame) > 0.05f)
                {
                    setFrame(spout, wantedFrame);
                    clearCarryVelocity(spout);
                }
                s_live.appliedFod[side] = heightChoice;
                return;
            }

            // Default height: follow active hazard mode.
            bool changedFromOverride = s_live.appliedFod[side] != main::kFodDefault;
            bool wantChr0 = hazardOff;
            if (wantChr0)
            {
                if (*curChr != 0)
                {
                    setAnim(spout, 0, 0, 0, 0);
                    clearCarryVelocity(spout);
                }
                *state = 0;
                setPauseAnim(spout, false);
            }
            else
            {
                bool switched = false;
                if (*curChr != 2)
                {
                    setAnim(spout, 2, 3, 2, 0);
                    switched = true;
                }
                if (switched || changedFromOverride || *state == 0)
                {
                    float defFrame = side == 0 ? 43.0f : 64.0f;
                    *targetFrame = defFrame;
                    if (switched)
                    {
                        setFrame(spout, defFrame);
                        clearCarryVelocity(spout);
                    }
                    *timer = 60.0f;
                    *state = 1;
                    setPauseAnim(spout, false);
                }
            }
            s_live.appliedFod[side] = main::kFodDefault;
        }

        static void applyDreamLandPositions(bool hazardOff)
        {
            if (!game::isRam(g_Stage) || !game::isRam(g_Stage->m_stagePositions))
            {
                return;
            }
            stCommonGimmick* st = static_cast<stCommonGimmick*>(g_Stage);
            if (!game::isRam(st->m_fileData))
            {
                return;
            }
            void* posData = st->m_fileData->getData(Data_Type_Model, hazardOff ? 180 : 100, 0xFFFE);
            if (!game::isRam(posData))
            {
                return;
            }
            nw4r::g3d::ResFile res(posData);
            g_Stage->m_stagePositions->loadPositionData(&res);
            g_Stage->updateStagePositions();
        }

        static void applyDreamLand(bool hazardOff, bool hazardChanged, bool force)
        {
            Ground* whispy = s_live.whispy;
            if (!game::isRam(whispy) || !game::isRam(g_Stage))
            {
                return;
            }
            void* desiredStdt = fetchStageStdt(hazardOff ? 21 : 20);
            bool stdtMismatch = desiredStdt != NULL && (g_Stage->m_stageData != desiredStdt || whispy->m_stageData != desiredStdt);
            if (desiredStdt != NULL && stdtMismatch)
            {
                g_Stage->m_stageData = desiredStdt;
                whispy->m_stageData = desiredStdt;
            }

            if (hazardChanged || force || stdtMismatch)
            {
                applyDreamLandPositions(hazardOff);
            }

            u8* wb = reinterpret_cast<u8*>(whispy);
            u32* timerBits = reinterpret_cast<u32*>(wb + kWhispyTimerOffset);
            u8* state = wb + kWhispyStateOffset;
            u8* subState = wb + kWhispySubStateOffset;

            if (hazardOff)
            {
                // Hold Whispy's wind cooldown at quiet NaN (identical to STDT[21]).
                *timerBits = kQuietNaN;
                // Blow states 4 and 5 own active wind trigger, quake(1), and effect.
                if (*state == 4 || *state == 5)
                {
                    stTrigger* trig = *reinterpret_cast<stTrigger**>(wb + kWhispyTriggerOffset);
                    if (game::isRam(trig))
                    {
                        trig->setAreaSleep(true);
                    }
                    CmRemoveQuakeFn removeQuake = reinterpret_cast<CmRemoveQuakeFn>(kCmRemoveQuakeAddr);
                    removeQuake(1);
                    u32* efIdPtr = reinterpret_cast<u32*>(wb + kWhispyEfIdOffset);
                    u32 efId = *efIdPtr;
                    void* ecMgr = reinterpret_cast<void*>(game::wordAt(reinterpret_cast<void*>(kEcMgrPtrAddr), 0));
                    if (efId != 0 && game::isRam(ecMgr))
                    {
                        EcEndEffectFn endEf = reinterpret_cast<EcEndEffectFn>(kEcEndEffectAddr);
                        endEf(ecMgr, efId);
                    }
                    *efIdPtr = 0;
                }
                // Abort wind prep/blow/end/shake states (2..8) back to idle anim 0.
                if (*state >= 2 && *state <= 8)
                {
                    VWhispyAnim setAnim = reinterpret_cast<VWhispyAnim>(vtableSlot(whispy, 0x1D0));
                    if (setAnim != NULL)
                    {
                        setAnim(whispy, 0, 0, 1, wb + kWhispyAnimWorkOffset);
                    }
                    *subState = 0;
                    *state = 0;
                }
            }
            else
            {
                // Re-enable wind: if timer is NaN (from HazOff) or transitioning OFF -> ON,
                // set state = 11 so grDxGreensWhispy::update cleanly re-seeds [800, 1800].
                bool isNaN = ((*timerBits & 0x7F800000u) == 0x7F800000u) && ((*timerBits & 0x007FFFFFu) != 0);
                if (hazardChanged || isNaN)
                {
                    *state = 11;
                    *subState = 0;
                }
            }
        }

        static void applySmashville(bool hazardOff, u8 platMode, bool force)
        {
            Ground* plat = s_live.ashiba[0];
            Ground* shadow = s_live.ashiba[1];
            if (!game::isRam(plat))
            {
                return;
            }
            u8 wantAnim = hazardOff ? 1 : 0;
            u8 curAnim = *(reinterpret_cast<u8*>(plat) + kSvCurAnimOffset);
            bool needAnimSwap = (curAnim != wantAnim);
            if (game::isRam(shadow) && *(reinterpret_cast<u8*>(shadow) + kSvCurAnimOffset) != wantAnim)
            {
                needAnimSwap = true;
            }

            if (needAnimSwap)
            {
                float savedFrame = plat->getMotionFrame(0);
                VSetAnim4 setAnimPlat = reinterpret_cast<VSetAnim4>(vtableSlot(plat, 0x1E0));
                if (setAnimPlat != NULL)
                {
                    setAnimPlat(plat, wantAnim, 1, 1, 0);
                    plat->setMotionFrame(savedFrame, 0);
                }
                if (game::isRam(shadow))
                {
                    VSetAnim4 setAnimShad = reinterpret_cast<VSetAnim4>(vtableSlot(shadow, 0x1E0));
                    if (setAnimShad != NULL)
                    {
                        setAnimShad(shadow, wantAnim, 1, 1, 0);
                        shadow->setMotionFrame(savedFrame, 0);
                    }
                }
                clearCarryVelocity(plat);
                force = true;
            }

            float target = platformTargetFrame(kPresetSmashville, platMode, hazardOff);
            if (target >= 0.0f)
            {
                if (force || s_live.appliedPlatformMode != platMode
                    || fabsf(plat->getMotionFrame(0) - target) > 0.05f)
                {
                    jumpGroundToFrame(plat, target);
                    if (game::isRam(shadow))
                    {
                        jumpGroundToFrame(shadow, target);
                    }
                }
                else
                {
                    setPauseAnim(plat, true);
                    if (game::isRam(shadow))
                    {
                        setPauseAnim(shadow, true);
                    }
                }
            }
            else
            {
                bool pause = (platMode == main::kStagePlatFreeze);
                setPauseAnim(plat, pause);
                if (game::isRam(shadow))
                {
                    setPauseAnim(shadow, pause);
                    // Keep shadow locked in phase with platform when paused/resumed.
                    if (force || s_live.appliedPlatformMode != platMode)
                    {
                        shadow->setMotionFrame(plat->getMotionFrame(0), 0);
                    }
                }
            }
        }

        static void applyGreenHillOrDeadLine(int preset, bool hazardOff, u8 platMode, bool force)
        {
            Ground* plat = s_live.ghzPlat;
            if (!game::isRam(plat))
            {
                return;
            }
            u8 wantAnim = hazardOff ? 1 : 0;
            u8 curAnim = *(reinterpret_cast<u8*>(plat) + kGhzCurAnimOffset);
            if (curAnim != wantAnim)
            {
                VSetAnim4 setAnim = reinterpret_cast<VSetAnim4>(vtableSlot(plat, 0x1E0));
                if (setAnim != NULL)
                {
                    setAnim(plat, wantAnim, 1, 1, 0);
                    clearCarryVelocity(plat);
                    force = true;
                }
            }

            float target = platformTargetFrame(preset, platMode, hazardOff);
            if (target >= 0.0f)
            {
                if (force || s_live.appliedPlatformMode != platMode
                    || fabsf(plat->getMotionFrame(0) - target) > 0.05f)
                {
                    jumpGroundToFrame(plat, target);
                }
                else
                {
                    setPauseAnim(plat, true);
                }
            }
            else
            {
                setPauseAnim(plat, platMode == main::kStagePlatFreeze);
            }
        }

        // Yoshi's Island Brawl (st_crayon_noshyguy_ht.rel): ModelData[80] owns the
        // 2880f support clouds / timedGhosts (anims 2..6, COLL[3] joints AKumoAMKasure01/r)
        // and ModelData[81] owns the synced BlarggwichIndicator (anim 0).
        // Under Hazards OFF, stCrayon::createObj omits both grounds and COLL[3], so live
        // Hazards OFF parks both underground at f0, disables collision, and hides them
        // with zero StageInstance allocations.
        static void applyYoshisIsland(bool hazardOff, u8 platMode, bool force)
        {
            Ground* clouds = s_live.crayonClouds[0];
            Ground* ind = s_live.crayonClouds[1];
            if (!game::isRam(clouds))
            {
                return;
            }
            u32 trackIdx = clouds->m_modelAnimNum > 3 ? 3 : 0;
            if (hazardOff)
            {
                clouds->setVisibility(0);
                clouds->setEnableCollisionStatus(false);
                if (game::isRam(ind))
                {
                    ind->setVisibility(0);
                }
                if (force || fabsf(clouds->getMotionFrame(trackIdx) - 0.0f) > 0.05f)
                {
                    jumpGroundLongAnimsToFrame(clouds, 0.0f, 1000);
                    if (game::isRam(ind))
                    {
                        jumpGroundToFrame(ind, 0.0f);
                    }
                }
                else
                {
                    setPauseAnim(clouds, true);
                    if (game::isRam(ind))
                    {
                        setPauseAnim(ind, true);
                    }
                }
                return;
            }

            clouds->setVisibility(1);
            clouds->setEnableCollisionStatus(true);
            if (game::isRam(ind))
            {
                ind->setVisibility(1);
            }

            float target = platformTargetFrame(kPresetYoshisIsland, platMode, false);
            if (target >= 0.0f)
            {
                if (force || s_live.appliedPlatformMode != platMode
                    || fabsf(clouds->getMotionFrame(trackIdx) - target) > 0.05f)
                {
                    jumpGroundLongAnimsToFrame(clouds, target, 1000);
                    if (game::isRam(ind))
                    {
                        jumpGroundToFrame(ind, target);
                    }
                }
                else
                {
                    setPauseAnim(clouds, true);
                    if (game::isRam(ind))
                    {
                        setPauseAnim(ind, true);
                    }
                }
            }
            else
            {
                bool pause = (platMode == main::kStagePlatFreeze);
                setPauseAnim(clouds, pause);
                if (game::isRam(ind))
                {
                    setPauseAnim(ind, pause);
                    if (force || s_live.appliedPlatformMode != platMode)
                    {
                        ind->setMotionFrame(clouds->getMotionFrame(trackIdx), 0);
                    }
                }
            }
        }

        static void applyGenericPlatforms(int preset, u8 platMode, bool hazardOff, bool force)
        {
            float target = platformTargetFrame(preset, platMode, hazardOff);
            for (int i = 0; i < s_live.movingCount; i++)
            {
                Ground* g = s_live.moving[i];
                if (!game::isRam(g))
                {
                    continue;
                }
                if (target >= 0.0f)
                {
                    if (force || s_live.appliedPlatformMode != platMode
                        || fabsf(g->getMotionFrame(0) - target) > 0.05f)
                    {
                        jumpGroundToFrame(g, target);
                    }
                    else
                    {
                        setPauseAnim(g, true);
                    }
                }
                else
                {
                    setPauseAnim(g, platMode == main::kStagePlatFreeze);
                }
            }
        }

        // ---- Detection -----------------------------------------------------

        static void detectStage()
        {
            memset(&s_live, 0, sizeof(s_live));
            if (!game::isRam(g_Stage))
            {
                main::setStagePlatformRowPreset(kPresetNone);
                return;
            }
            s_live.stage = g_Stage;
            s_live.initialHazardOff = readGameHazardOff();
            s_live.appliedHazardOff = s_live.initialHazardOff;

            StageDetectInput in;
            memset(&in, 0, sizeof(in));
            in.modFod = moduleLoadedPrefix("st_dxfountain_ht");
            in.modDreamLand = moduleLoadedPrefix("st_dreamland64_ht");
            in.modSmashville = moduleLoadedPrefix("st_smashville_ht");
            in.modGreenHill = moduleLoadedPrefix("st_greenhill_ht");
            in.modCrayon = moduleLoadedPrefix("st_crayon_noshyguy_ht");

            Ground* platLeftGround = NULL;
            Ground* randallGround = NULL;

            Ground* g = g_Stage->m_headGround;
            for (int n = 0; n < kMaxGroundWalk && game::isRam(g); n++, g = g->m_nextGround)
            {
                u16 mdl = g->m_mdlIndex;
                if (in.modFod)
                {
                    if (mdl == 80) { s_live.spout[0] = g; in.hasSpout80 = true; }
                    if (mdl == 81) { s_live.spout[1] = g; in.hasSpout81 = true; }
                }
                if (in.modDreamLand && mdl == 2)
                {
                    s_live.whispy = g;
                    in.hasWhispy2 = true;
                }
                if (in.modSmashville)
                {
                    if (mdl == 80) { s_live.ashiba[0] = g; in.hasAshiba80 = true; }
                    if (mdl == 81) { s_live.ashiba[1] = g; in.hasAshiba81 = true; }
                }
                if (in.modGreenHill && mdl == 80)
                {
                    s_live.ghzPlat = g;
                    if (groundHasNode(g, "furiko3") || groundHasNode(g, "dai"))
                    {
                        in.hasGhzPendulum = true;
                    }
                    else if (groundHasNode(g, "pCube2"))
                    {
                        in.hasDeadLinePlats = true;
                    }
                }
                if (in.modCrayon)
                {
                    if (mdl == 80 && groundHasNode(g, "AKumoAMKasure01") && groundHasNode(g, "AKumoAMKasure01r"))
                    {
                        s_live.crayonClouds[0] = g;
                        in.hasCrayonClouds80 = true;
                    }
                    if (mdl == 81)
                    {
                        s_live.crayonClouds[1] = g;
                    }
                }
                if (platLeftGround == NULL && groundChrFrameCount(g) == 3600 && groundHasNode(g, "PlatformLeft"))
                {
                    platLeftGround = g;
                    in.hasPlatformLeft = true;
                    if (groundHasNode(g, "PlatformCenter"))
                    {
                        in.hasPlatformCenter = true;
                    }
                }
                if (randallGround == NULL && groundChrFrameCount(g) >= 1000 && groundHasNode(g, "Randall"))
                {
                    randallGround = g;
                    in.hasRandall = true;
                }
            }

            if (in.hasPlatformLeft)
            {
                addMovingGround(platLeftGround);
            }
            else if (in.hasRandall)
            {
                addMovingGround(randallGround);
            }
            else if (!in.modFod && !in.modDreamLand && !in.modSmashville && !in.modGreenHill && !in.modCrayon)
            {
                scanCollisionJoints(NULL);
                in.hasGenericMovingColl = s_live.movingCount > 0;
            }

            s_live.preset = classifyStagePreset(in);
            s_live.initialized = true;
            main::setStagePlatformRowPreset(s_live.preset);
            if (presetHasPlatforms(s_live.preset))
            {
                main::StageOptionsSettings& so = main::settings().stageOptions;
                so.platformMode = static_cast<u8>(clampPlatformMode(s_live.preset, so.platformMode));
            }
            TM_LOG("stage_options: detected preset %d (stage %08x kind %x initHazOff %d moving %d)\n", s_live.preset,
                   g_Stage, static_cast<int>(g_Stage->m_stageKind), s_live.initialHazardOff ? 1 : 0,
                   s_live.movingCount);
        }

        static void ensureDetected()
        {
            if (!game::isRam(g_Stage))
            {
                if (s_live.initialized || s_live.stage != NULL)
                {
                    restoreGlobalHazardOff();
                    memset(&s_live, 0, sizeof(s_live));
                    main::setStagePlatformRowPreset(kPresetNone);
                }
                return;
            }
            if (!s_live.initialized || s_live.stage != g_Stage)
            {
                restoreGlobalHazardOff();
                detectStage();
            }
        }

        static void applyAll(bool force)
        {
            ensureDetected();
            if (!s_live.initialized || s_live.preset == kPresetNone)
            {
                return;
            }

            main::StageOptionsSettings& so = main::settings().stageOptions;
            bool forceNow = force || s_live.forceReapply;
            s_live.forceReapply = false;

            u8 platMode = static_cast<u8>(clampPlatformMode(s_live.preset, so.platformMode));
            if (presetHasPlatforms(s_live.preset) && so.platformMode != platMode)
            {
                so.platformMode = platMode;
            }

            bool wantHazardOff = s_live.initialHazardOff;
            bool hazardChanged = false;
            if (presetHasHazards(s_live.preset))
            {
                wantHazardOff = resolveHazardOff(so.hazards, s_live.initialHazardOff);
                hazardChanged = (wantHazardOff != s_live.appliedHazardOff);
                writeGameHazardOff(wantHazardOff);
            }

            switch (s_live.preset)
            {
            case kPresetFod:
                applyFodSpout(0, so.fodLeft, wantHazardOff, forceNow || hazardChanged);
                applyFodSpout(1, so.fodRight, wantHazardOff, forceNow || hazardChanged);
                break;
            case kPresetDreamLand:
                applyDreamLand(wantHazardOff, hazardChanged, forceNow);
                break;
            case kPresetSmashville:
                applySmashville(wantHazardOff, platMode, forceNow || hazardChanged);
                break;
            case kPresetGreenHill:
            case kPresetDeadLine:
                applyGreenHillOrDeadLine(s_live.preset, wantHazardOff, platMode, forceNow || hazardChanged);
                break;
            case kPresetYoshisIsland:
                applyYoshisIsland(wantHazardOff, platMode, forceNow || hazardChanged);
                break;
            case kPresetLuigisMansion:
            case kPresetDelfinoSecret:
            case kPresetYoshisStory:
            case kPresetGenericMoving:
                applyGenericPlatforms(s_live.preset, platMode, wantHazardOff, forceNow);
                break;
            default:
                break;
            }

            s_live.appliedHazardOff = wantHazardOff;
            s_live.appliedPlatformMode = platMode;
        }
    }

    void stageOptionsAttach()
    {
        memset(&s_live, 0, sizeof(s_live));
        main::setStagePlatformRowPreset(kPresetNone);
    }

    void stageOptionsDetach()
    {
        restoreGlobalHazardOff();
        memset(&s_live, 0, sizeof(s_live));
        main::setStagePlatformRowPreset(kPresetNone);
    }

    bool stageOptions(int call)
    {
        switch (call)
        {
        case main::kStageTick:
            applyAll(false);
            return s_live.preset != kPresetNone;
        case main::kStageChanged:
            applyAll(true);
            return s_live.preset != kPresetNone;
        case main::kStageSavestateLoaded:
            // Savestate load restores StageInstance memory: re-apply fixed
            // presets and hazard state on this frame, while Freeze preserves
            // the savestate's frozen frame.
            s_live.forceReapply = true;
            applyAll(true);
            return s_live.preset != kPresetNone;
        case main::kStagePage:
            ensureDetected();
            return presetAnyOption(s_live.preset);
        case main::kStageHazardsRow:
            ensureDetected();
            return presetHasHazards(s_live.preset);
        case main::kStagePlatformsRow:
            ensureDetected();
            return presetHasPlatforms(s_live.preset);
        case main::kStageFodRow:
            ensureDetected();
            return presetHasFod(s_live.preset);
        default:
            return false;
        }
    }
}
}
