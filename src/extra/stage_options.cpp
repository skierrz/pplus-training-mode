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

#include "extra/net_alloc.h"
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

        static LiveStage* s_live = NULL;

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
            if (s_live == NULL)
            {
                return;
            }
            LiveStage& live = *s_live;
            if (shouldRestoreGlobalHazardOff(live.initialized, live.preset, live.initialHazardOff,
                                             live.appliedHazardOff))
            {
                writeGameHazardOff(live.initialHazardOff);
                live.appliedHazardOff = live.initialHazardOff;
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

        static bool groundHasChrAnim(Ground* g, u32 minFrames)
        {
            return groundChrFrameCount(g) >= minFrames;
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
            LiveStage& live = *s_live;
            if (!game::isRam(g) || live.movingCount >= kMaxMovingGrounds)
            {
                return;
            }
            for (int i = 0; i < live.movingCount; i++)
            {
                if (live.moving[i] == g)
                {
                    return;
                }
            }
            live.moving[live.movingCount++] = g;
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

#pragma dont_inline on
        static void jumpGround(Ground* g, float frame, u32 minFrames = 0)
        {
            if (!game::isRam(g) || (minFrames != 0 && !game::isRam(g->m_modelAnims)))
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
                if (minFrames != 0)
                {
                    gfModelAnimation* anim = g->m_modelAnims[i];
                    if (!game::isRam(anim) || !game::isRam(anim->m_anmObjChrRes) || anim->getFrameCount() < minFrames)
                    {
                        continue;
                    }
                }
                g->setMotionFrame(frame, i);
            }
            clearCarryVelocity(g);
        }

        static void applyPlatformTarget(Ground* g, Ground* aux, float target, u32 trackIdx, u32 minFrames, u8 platMode,
                                        bool force)
        {
            const LiveStage& live = *s_live;
            bool hasAux = game::isRam(aux);
            if (target >= 0.0f)
            {
                if (force || live.appliedPlatformMode != platMode
                    || fabsf(g->getMotionFrame(trackIdx) - target) > 0.05f)
                {
                    jumpGround(g, target, minFrames);
                    if (hasAux) jumpGround(aux, target, 0);
                }
                else
                {
                    setPauseAnim(g, true);
                    if (hasAux) setPauseAnim(aux, true);
                }
            }
            else
            {
                bool pause = (platMode == main::kStagePlatFreeze);
                setPauseAnim(g, pause);
                if (hasAux)
                {
                    setPauseAnim(aux, pause);
                    if (force || live.appliedPlatformMode != platMode)
                    {
                        aux->setMotionFrame(g->getMotionFrame(trackIdx), 0);
                    }
                }
            }
        }
#pragma dont_inline reset

        static void* fetchStageStdt(int miscIndex)
        {
            Stage* stage = extraStage();
            if (!game::isRam(stage))
            {
                return NULL;
            }
            stCommonGimmick* st = static_cast<stCommonGimmick*>(stage);
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
            LiveStage& live = *s_live;
            Ground* spout = live.spout[side];
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
                if (force || switchedAnim || live.appliedFod[side] != heightChoice || fabsf(cur - wantedFrame) > 0.05f)
                {
                    setFrame(spout, wantedFrame);
                    clearCarryVelocity(spout);
                }
                live.appliedFod[side] = heightChoice;
                return;
            }

            // Default height: follow active hazard mode.
            bool changedFromOverride = live.appliedFod[side] != main::kFodDefault;
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
            live.appliedFod[side] = main::kFodDefault;
        }

        static void applyDreamLandPositions(bool hazardOff)
        {
            Stage* stage = extraStage();
            if (!game::isRam(stage) || !game::isRam(stage->m_stagePositions))
            {
                return;
            }
            stCommonGimmick* st = static_cast<stCommonGimmick*>(stage);
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
            stage->m_stagePositions->loadPositionData(&res);
            stage->updateStagePositions();
        }

        static void applyDreamLand(bool hazardOff, bool hazardChanged, bool force)
        {
            Ground* whispy = s_live->whispy;
            Stage* stage = extraStage();
            if (!game::isRam(whispy) || !game::isRam(stage))
            {
                return;
            }
            void* desiredStdt = fetchStageStdt(hazardOff ? 21 : 20);
            bool stdtMismatch = desiredStdt != NULL && (stage->m_stageData != desiredStdt || whispy->m_stageData != desiredStdt);
            if (desiredStdt != NULL && stdtMismatch)
            {
                stage->m_stageData = desiredStdt;
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

        static bool swapGroundAnim(Ground* g, u8 wantAnim)
        {
            VSetAnim4 setAnim = reinterpret_cast<VSetAnim4>(vtableSlot(g, 0x1E0));
            if (setAnim == NULL)
            {
                return false;
            }
            setAnim(g, wantAnim, 1, 1, 0);
            return true;
        }

        static void applySmashville(bool hazardOff, u8 platMode, bool force)
        {
            const LiveStage& live = *s_live;
            Ground* plat = live.ashiba[0];
            Ground* shadow = live.ashiba[1];
            if (!game::isRam(plat))
            {
                return;
            }
            u8 wantAnim = hazardOff ? 1 : 0;
            bool hasShad = game::isRam(shadow);
            bool needAnimSwap = (*(reinterpret_cast<u8*>(plat) + kSvCurAnimOffset) != wantAnim)
                                || (hasShad && *(reinterpret_cast<u8*>(shadow) + kSvCurAnimOffset) != wantAnim);

            if (needAnimSwap)
            {
                float savedFrame = plat->getMotionFrame(0);
                Ground* gs[2] = {plat, hasShad ? shadow : NULL};
                for (int i = 0; i < 2; i++)
                {
                    if (gs[i] != NULL && swapGroundAnim(gs[i], wantAnim))
                    {
                        gs[i]->setMotionFrame(savedFrame, 0);
                    }
                }
                clearCarryVelocity(plat);
                force = true;
            }
            applyPlatformTarget(plat, shadow, platformTargetFrame(kPresetSmashville, platMode, hazardOff), 0, 0,
                                platMode, force);
        }

        static void applyGreenHillOrDeadLine(int preset, bool hazardOff, u8 platMode, bool force)
        {
            Ground* plat = s_live->ghzPlat;
            if (!game::isRam(plat))
            {
                return;
            }
            u8 wantAnim = hazardOff ? 1 : 0;
            if (*(reinterpret_cast<u8*>(plat) + kGhzCurAnimOffset) != wantAnim && swapGroundAnim(plat, wantAnim))
            {
                clearCarryVelocity(plat);
                force = true;
            }
            applyPlatformTarget(plat, NULL, platformTargetFrame(preset, platMode, hazardOff), 0, 0, platMode, force);
        }

        // Yoshi's Island Brawl (st_crayon_noshyguy_ht.rel): ModelData[80] owns the
        // 2880f support clouds / timedGhosts (anims 2..6, COLL[3] joints AKumoAMKasure01/r)
        // and ModelData[81] owns the synced BlarggwichIndicator (anim 0).
        // Under Hazards OFF, stCrayon::createObj omits both grounds and COLL[3], so live
        // Hazards OFF parks both underground at f0, disables collision, and hides them
        // with zero StageInstance allocations.
        static void applyYoshisIsland(bool hazardOff, u8 platMode, bool force)
        {
            const LiveStage& live = *s_live;
            Ground* clouds = live.crayonClouds[0];
            Ground* ind = live.crayonClouds[1];
            if (!game::isRam(clouds))
            {
                return;
            }
            u32 trackIdx = clouds->m_modelAnimNum > 3 ? 3 : 0;
            clouds->setVisibility(hazardOff ? 0 : 1);
            clouds->setEnableCollisionStatus(!hazardOff);
            if (game::isRam(ind))
            {
                ind->setVisibility(hazardOff ? 0 : 1);
            }
            float target = hazardOff ? 0.0f : platformTargetFrame(kPresetYoshisIsland, platMode, false);
            applyPlatformTarget(clouds, ind, target, trackIdx, 1000, hazardOff ? live.appliedPlatformMode : platMode,
                                force);
        }

        static void applyGenericPlatforms(int preset, u8 platMode, bool hazardOff, bool force)
        {
            const LiveStage& live = *s_live;
            float target = platformTargetFrame(preset, platMode, hazardOff);
            for (int i = 0; i < live.movingCount; i++)
            {
                Ground* g = live.moving[i];
                if (game::isRam(g))
                {
                    applyPlatformTarget(g, NULL, target, 0, 0, platMode, force);
                }
            }
        }

        // ---- Detection -----------------------------------------------------

        static void detectStage()
        {
            LiveStage& live = *s_live;
            memset(&live, 0, sizeof(live));
            Stage* stage = extraStage();
            if (!game::isRam(stage))
            {
                main::setStagePlatformRowPreset(kPresetNone);
                return;
            }
            live.stage = stage;
            live.initialHazardOff = readGameHazardOff();
            live.appliedHazardOff = live.initialHazardOff;

            StageDetectInput in;
            memset(&in, 0, sizeof(in));
            static const char* const kModPrefixes[5] = {
                "st_dxfountain_ht", "st_dreamland64_ht", "st_smashville_ht",
                "st_greenhill_ht",  "st_crayon_noshyguy_ht"
            };
            bool* const modFlags[5] = {
                &in.modFod, &in.modDreamLand, &in.modSmashville, &in.modGreenHill, &in.modCrayon
            };
            for (int m = 0; m < 5; m++)
            {
                *modFlags[m] = moduleLoadedPrefix(kModPrefixes[m]);
            }

            Ground* platLeftGround = NULL;
            Ground* randallGround = NULL;

            Ground* g = stage->m_headGround;
            for (int n = 0; n < kMaxGroundWalk && game::isRam(g); n++, g = g->m_nextGround)
            {
                u16 mdl = g->m_mdlIndex;
                if (in.modFod)
                {
                    if (mdl == 80) { live.spout[0] = g; in.hasSpout80 = true; }
                    if (mdl == 81) { live.spout[1] = g; in.hasSpout81 = true; }
                }
                if (in.modDreamLand && mdl == 2)
                {
                    live.whispy = g;
                    in.hasWhispy2 = true;
                }
                if (in.modSmashville)
                {
                    if (mdl == 80) { live.ashiba[0] = g; in.hasAshiba80 = true; }
                    if (mdl == 81) { live.ashiba[1] = g; in.hasAshiba81 = true; }
                }
                if (in.modGreenHill && mdl == 80)
                {
                    live.ghzPlat = g;
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
                        live.crayonClouds[0] = g;
                        in.hasCrayonClouds80 = true;
                    }
                    if (mdl == 81)
                    {
                        live.crayonClouds[1] = g;
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
                in.hasGenericMovingColl = live.movingCount > 0;
            }

            live.preset = classifyStagePreset(in);
            live.initialized = true;
            main::setStagePlatformRowPreset(live.preset);
            if (presetHasPlatforms(live.preset))
            {
                main::StageOptionsSettings& so = main::settings().stageOptions;
                so.platformMode = static_cast<u8>(clampPlatformMode(live.preset, so.platformMode));
            }
            TM_LOG("stage_options: detected preset %d (stage %08x kind %x initHazOff %d moving %d)\n", live.preset,
                   stage, static_cast<int>(stage->m_stageKind), live.initialHazardOff ? 1 : 0,
                   live.movingCount);
        }

        static void ensureDetected()
        {
            LiveStage& live = *s_live;
            Stage* stage = extraStage();
            if (!game::isRam(stage))
            {
                if (live.initialized || live.stage != NULL)
                {
                    restoreGlobalHazardOff();
                    memset(&live, 0, sizeof(live));
                    main::setStagePlatformRowPreset(kPresetNone);
                }
                return;
            }
            if (!live.initialized || live.stage != stage)
            {
                restoreGlobalHazardOff();
                detectStage();
            }
        }

        static void applyAll(bool force)
        {
            LiveStage& live = *s_live;
            ensureDetected();
            if (!live.initialized || live.preset == kPresetNone)
            {
                return;
            }

            main::StageOptionsSettings& so = main::settings().stageOptions;
            bool forceNow = force || live.forceReapply;
            live.forceReapply = false;

            u8 platMode = static_cast<u8>(clampPlatformMode(live.preset, so.platformMode));
            if (presetHasPlatforms(live.preset) && so.platformMode != platMode)
            {
                so.platformMode = platMode;
            }

            bool wantHazardOff = live.initialHazardOff;
            bool hazardChanged = false;
            if (presetHasHazards(live.preset))
            {
                wantHazardOff = resolveHazardOff(so.hazards, live.initialHazardOff);
                hazardChanged = (wantHazardOff != live.appliedHazardOff);
                writeGameHazardOff(wantHazardOff);
            }

            switch (live.preset)
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
                applyGreenHillOrDeadLine(live.preset, wantHazardOff, platMode, forceNow || hazardChanged);
                break;
            case kPresetYoshisIsland:
                applyYoshisIsland(wantHazardOff, platMode, forceNow || hazardChanged);
                break;
            case kPresetLuigisMansion:
            case kPresetDelfinoSecret:
            case kPresetYoshisStory:
            case kPresetGenericMoving:
                applyGenericPlatforms(live.preset, platMode, wantHazardOff, forceNow);
                break;
            default:
                break;
            }

            live.appliedHazardOff = wantHazardOff;
            live.appliedPlatformMode = platMode;
        }
    }

    void stageOptionsAttach()
    {
        freeNetPtr(s_live);
        s_live = allocNetArray<LiveStage>(1);
        main::setStagePlatformRowPreset(kPresetNone);
    }

    void stageOptionsDetach()
    {
        restoreGlobalHazardOff();
        freeNetPtr(s_live);
        main::setStagePlatformRowPreset(kPresetNone);
    }

    bool stageOptions(int call)
    {
        if (s_live == NULL)
        {
            return false;
        }
        LiveStage& live = *s_live;
        if (call == main::kStageTick || call == main::kStageChanged || call == main::kStageSavestateLoaded)
        {
            if (call == main::kStageSavestateLoaded)
            {
                // Savestate load restores StageInstance memory: re-apply fixed
                // presets and hazard state on this frame, while Freeze preserves
                // the savestate's frozen frame.
                live.forceReapply = true;
            }
            applyAll(call != main::kStageTick);
            return live.preset != kPresetNone;
        }
        ensureDetected();
        switch (call)
        {
        case main::kStagePage:
            return presetAnyOption(live.preset);
        case main::kStageHazardsRow:
            return presetHasHazards(live.preset);
        case main::kStagePlatformsRow:
            return presetHasPlatforms(live.preset);
        case main::kStageFodRow:
            return presetHasFod(live.preset);
        default:
            return false;
        }
    }
}
}
