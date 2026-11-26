#include "extra/scenario_presets.h"

#include <cstdio>
#include <GX/GXTypes.h>
#include <ft/fighter.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/color/so_color_blend_module_impl.h>
#include <so/damage/so_damage.h>
#include <so/damage/so_damage_module_impl.h>
#include <so/effect/so_effect_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/combo_trainer.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_hud.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/net_alloc.h"
#include "extra/pplus_recovery.h"
#include "extra/scenario_presets_core.h"
#include "extra/stage_geom.h"
#include "main/cpu/options.h"
#include "main/extra.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/tech_frames.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/services.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/ui/pages/scenarios.h"
#include "shared/drill_session.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/log.h"
#include "shared/version.h"

namespace tm {
namespace extra {
    enum {
        kScenarioDebugMagic = 0x544D5343, // "TMSC"
        kStatusPassId = 0x72,             // Fighter::Status::Pass (ft/fighter.h:134)
        kStatusDamageFlyId = 0x45,
        kStatusDamageFallId = 0x49,
        kStatusDownWaitId = 0x4D,
        kHitstunBasicId = 0x10000038,     // LA-Basic[0x38] hitstun
        kLaBitRebirthDesc = 0x12000001,   // LA-Bit[1]: Rebirth descending
        kLaBitRebirthNoInv = 0x12000023,  // LA-Bit[35]: skip Rebirth lift & exit invuln
        kRaBasicRebirthWait = 0x20000002, // RA-Basic[2]: Rebirth wait timer
        kRaBasicRebirthEf = 0x20000003,   // RA-Basic[3]: EfID 0x62 revival platform handle
        kRaBitRebirthDone = 0x22000010,   // RA-Bit[16]: Rebirth descent finished
        kKineticListOff = 0x14,           // soKineticModule -> energy list (cpu/view.cpp)
        kDamageEnergySlot = 0x4C,         // damage kinetic energy slot (cpu/view.cpp)
        kEnergySpeedOff = 0x08            // Vec2f speed inside energy
    };

    // Compact RAM-inspectable telemetry block for live Dolphin tests.
    struct ScenarioDebugBlock {
        u32 magic;                // +0x00: kScenarioDebugMagic
        u32 frame;                // +0x04
        u32 scratchAddr;          // +0x08: ScenarioMenuState in ui::menuScratch()
        u32 resetCount;           // +0x0C
        u32 completionCount;      // +0x10
        u32 sdiHitCount;          // +0x14
        u32 sdiHitlagFrames;      // +0x18
        u32 grabEscapeFrames;     // +0x1C
        u32 shieldHitCount;       // +0x20
        float hmnSpawnX;          // +0x24
        float hmnSpawnY;          // +0x28
        float cpuSpawnX;          // +0x2C
        float cpuSpawnY;          // +0x30
        float sdiDispX;           // +0x34
        float sdiDispY;           // +0x38
        u8 activePreset;          // +0x3C
        u8 hasPlatform;           // +0x3D
        u8 snapshotActive;        // +0x3E
        u8 sdiEscaped;            // +0x3F
        u8 lastCorrectWritten[2]; // +0x40..0x41 (0=HMN, 1=CPU)
        u8 observedCorrect[2];    // +0x42..0x43 (0=HMN, 1=CPU)
        u8 restorePending[2];     // +0x44..0x45
        u8 sawShieldDrop;         // +0x46
        u8 sawWallTech;           // +0x47
        u8 sawSlideOff;           // +0x48
        u8 sawGrabEscape;         // +0x49
        u8 autoReset;             // +0x4A
        u8 side;                  // +0x4B
        u8 multiHitMove;          // +0x4C
        u8 resolvedMultiHit;      // +0x4D
        u8 resolvedPlatformSlot;  // +0x4E
        u8 resolvedLedgetechAtk;  // +0x4F
    };
    TM_STATIC_ASSERT(sizeof(ScenarioDebugBlock) == 0x50, tm_scenario_debug_block_size);

    struct ScenarioNet {
        ScenarioState state;
        ScenarioCpuState cpuState;
        ScenarioDebugBlock debug;
        bool revivalActive;
        s8 revivalPlayer;
        bool cpuMotionFrozen;
        bool slidePassDisabled;
        bool dropIntangActive;
        bool ledgetechWallWarned;
        u8 techFlashFrames;
        s8 spacingOffset;
    };

    static ScenarioNet* s_net = NULL;
    static s8 s_placedPlayer[2] = {-1, -1};
    static bool s_wasPaused = false;
    static bool s_resumeFromPause = false;

    TM_STATIC_ASSERT(sizeof(ui::pages::ScenarioMenuState) == 33, tm_scenario_menu_state_size);

#pragma dont_inline on
    static void readMenuConfigInto(ScenarioConfig& out)
    {
        const ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        ScenarioConfig raw;
        memcpy(&raw, m, sizeof(*m));
        raw.spacingOffset = s_net != NULL ? s_net->spacingOffset : 0;
        sanitizeScenarioConfigInto(raw, out);
    }

    static void initScratchDefaults()
    {
        static const ui::pages::ScenarioMenuState kDef = {
            kScenarioOff,        // preset
            1,                   // autoReset
            kScenarioSideRight,  // side
            kMultiHitAuto,       // multiHitMove
            kOosGrab,            // oosAction
            kLtSpawnCloseLow,    // ledgetechSpawn
            kLtDjAvailable,      // ledgetechDj
            kLtAtkDsmash,        // ledgetechAttack
            kSdPlatLeft,         // shieldDropPlat
            kSdSpaceRandom,      // shieldDropSpacing
            kSdFaceToward,       // shieldDropFacing
            kGrabActHold,        // grabAction
            kGrabPctRandom,      // grabPctMode
            kTechSetupTumbleDrop,// techSetup
            0,                   // techPosition
            kTechEscIntangFlash, // techEscape
            0, 0, 0, 0, 0, 0,    // techInvisMode..techShowReaction
            kEgSetupLaunch,      // edgeguardSetup
            kEgStrNormal,        // edgeguardStrength
            kEgDistMid,          // edgeguardDist
            kEgDjAvail,          // edgeguardDj
            main::kRecMixupMixed,// edgeguardMixup
            kLtStartRevival,     // ledgetechStart
            kLtTimeHoldActive,   // ledgetechTiming
            kShieldRoleOffense,  // shieldRole
            kShieldDefAerial,    // shieldDefAttack
            kSdAtkShAerial,      // shieldDropAttack
            kSlideAtkUnderUair   // slideOffAttack
        };
        memcpy(ui::pages::scenarioMenuState(), &kDef, sizeof(kDef));
    }

    static void syncDebugBlock(Fighter* hmn, Fighter* cpu)
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioDebugBlock& s_debug = s_net->debug;
        ScenarioConfig cfg;
        readMenuConfigInto(cfg);
        s_debug.magic = kScenarioDebugMagic;
        s_debug.frame = game::frameCounter();
        s_debug.scratchAddr = reinterpret_cast<u32>(ui::pages::scenarioMenuState());
        s_debug.resetCount = s_state.resetCount;
        s_debug.completionCount = s_state.completionCount;
        s_debug.sdiHitCount = s_state.sdiHitCount;
        s_debug.sdiHitlagFrames = s_state.sdiHitlagFrames;
        s_debug.grabEscapeFrames = s_state.grabEscapeFrames;
        s_debug.shieldHitCount = s_state.shieldHitCount;
        s_debug.hmnSpawnX = s_state.hmnSpawnX;
        s_debug.hmnSpawnY = s_state.hmnSpawnY;
        s_debug.cpuSpawnX = s_state.cpuSpawnX;
        s_debug.cpuSpawnY = s_state.cpuSpawnY;
        s_debug.sdiDispX = s_state.sdiDispX;
        s_debug.sdiDispY = s_state.sdiDispY;
        s_debug.activePreset = s_state.activePreset;
        s_debug.hasPlatform = s_state.hasPlatform ? 1 : 0;
        s_debug.snapshotActive = s_state.snapshotActive ? 1 : 0;
        s_debug.sdiEscaped = s_state.sdiEscaped ? 1 : 0;
        s_debug.lastCorrectWritten[0] = s_state.lastCorrectWritten[0];
        s_debug.lastCorrectWritten[1] = s_state.lastCorrectWritten[1];
        int hObs = hmn != NULL ? getFighterCorrect(hmn) : -1;
        int cObs = cpu != NULL ? getFighterCorrect(cpu) : -1;
        s_debug.observedCorrect[0] = hObs >= 0 ? static_cast<u8>(hObs) : 0xFF;
        s_debug.observedCorrect[1] = cObs >= 0 ? static_cast<u8>(cObs) : 0xFF;
        s_debug.restorePending[0] = s_state.restoreCorrect[0] ? 1 : 0;
        s_debug.restorePending[1] = s_state.restoreCorrect[1] ? 1 : 0;
        s_debug.sawShieldDrop = s_state.sawShieldDrop ? 1 : 0;
        s_debug.sawWallTech = s_state.sawWallTech ? 1 : 0;
        s_debug.sawSlideOff = s_state.sawSlideOff ? 1 : 0;
        s_debug.sawGrabEscape = s_state.sawGrabEscape ? 1 : 0;
        s_debug.autoReset = cfg.autoReset;
        s_debug.side = cfg.side;
        s_debug.multiHitMove = cfg.multiHitMove;
        s_debug.resolvedMultiHit = s_state.resolvedMultiHit;
        s_debug.resolvedPlatformSlot = s_state.resolvedPlatformSlot;
        s_debug.resolvedLedgetechAtk = s_state.resolvedLedgetechAttack;
    }

    static bool sawIntraFrameStatus(int playerNo, int targetStatus)
    {
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || t->playerNo != playerNo)
            {
                continue;
            }
            if (t->cur.status == targetStatus)
            {
                return true;
            }
            for (int k = 0; k < t->eventCount; k++)
            {
                if (t->events[k].status == targetStatus)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool findPlayerRef(const game::FighterRef* refs, int n, int playerNo, game::FighterRef& out)
    {
        for (int i = 0; i < n; i++)
        {
            if (refs[i].playerNo == playerNo)
            {
                out = refs[i];
                return true;
            }
        }
        return false;
    }

    static void applyPercentsIfChanged(u16 beforeHmn, u16 beforeCpu, const main::Settings& s)
    {
        if (s.general.playerPercent != beforeHmn)
        {
            lab::applyPercent(main::kSideHuman);
        }
        if (s.cpu.percent != beforeCpu)
        {
            lab::applyPercent(main::kSideCpu);
        }
    }
#pragma dont_inline reset

    void scenarioPresetsAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<ScenarioNet>(1);
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
        initScratchDefaults();
        if (s_net == NULL)
        {
            return;
        }
        scenarioStateInit(s_net->state);
        scenarioCpuStateReset(s_net->cpuState);
        s_net->revivalActive = false;
        s_net->revivalPlayer = -1;
        s_net->cpuMotionFrozen = false;
        s_net->slidePassDisabled = false;
        s_net->dropIntangActive = false;
        s_net->ledgetechWallWarned = false;
        s_net->techFlashFrames = 0;
        s_net->spacingOffset = 0;
        drills::Session* sess = drills::sessionIn(main::services().settings());
        if (sess != NULL)
        {
            s_net->state.techBest = sess->best[kTechBestSlot];
            if (sess->pendingScenario >= kScenarioComboLab && sess->pendingScenario < kScenarioCount)
            {
                u8 p = sess->pendingScenario;
                sess->pendingScenario = 0;
                ui::pages::scenarioMenuState()->preset = p;
                TM_LOG("scenarios: arm %u\n", p);
            }
        }
        memset(&s_net->debug, 0, sizeof(s_net->debug));
        syncDebugBlock(NULL, NULL);
        TM_LOG("scenarios: debug block %08x\n", &s_net->debug);
    }

#pragma dont_inline on
    // Clears lingering respawn or ledge intangibility/invincibility when a
    // scenario warps a fighter into a fresh rep (soCollisionHitModule).
    void clearWarpIntang(const game::FighterRef& ref)
    {
        Fighter* pair[2] = {ref.fighter, (ref.partner != NULL && ref.partner != ref.fighter) ? ref.partner : NULL};
        for (int i = 0; i < 2; i++)
        {
            if (pair[i] == NULL)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(pair[i]);
            if (m != NULL && game::isRam(m->m_collisionHitModule))
            {
                m->m_collisionHitModule->setXluFrameGlobal(0, 0);
                m->m_collisionHitModule->setInvincibleFrameGlobal(0, true, 0);
                m->m_collisionHitModule->resetStatusGlobal(0);
            }
        }
    }

    static void killRevivalPlatform(Fighter* f)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return;
        }
        int handle = m->m_workManageModule->getInt(kRaBasicRebirthEf);
        if (handle != 0 && game::isRam(m->m_effectModule))
        {
            m->m_effectModule->kill(handle, true, true);
            m->m_workManageModule->setInt(0, kRaBasicRebirthEf);
        }
    }

    static void forceStatus(Fighter* f, int status)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m != NULL && game::isRam(m->m_statusModule) && game::isRam(f->m_moduleAccesser))
        {
            m->m_statusModule->changeStatusForce(status, f->m_moduleAccesser);
        }
    }

    static void warpAirClean(const game::FighterRef& ref, float x, float y, bool noclip, float dmg, float lr,
                             bool refreshAir, bool spendDj)
    {
        placeFalling(ref, x, y, noclip, dmg, lr);
        if (refreshAir)
        {
            refreshAirResources(ref, spendDj);
        }
        clearWarpIntang(ref);
    }

    static void setTechFlashTint(Fighter* f, bool on)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m != NULL && game::isRam(m->m_colorBlendModule))
        {
            static const GXColor kPale = {220, 238, 255, 165};
            static const GXColor kZero = {0, 0, 0, 0};
            m->m_colorBlendModule->setSubColor(on ? kPale : kZero, on ? 1 : 0);
        }
    }
#pragma dont_inline reset

#pragma dont_inline on
    static void restoreRuntimeMods()
    {
        if (s_net == NULL)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m == NULL)
            {
                continue;
            }
            if (!refs[i].cpu && s_net->revivalActive)
            {
                killRevivalPlatform(refs[i].fighter);
            }
            if (refs[i].cpu)
            {
                if (s_net->cpuMotionFrozen && game::isRam(m->m_motionModule))
                {
                    m->m_motionModule->setRate(1.0f);
                }
                if (s_net->slidePassDisabled && game::isRam(m->m_groundModule))
                {
                    m->m_groundModule->setPassableCheck(true, 0);
                }
                if (s_net->techFlashFrames > 0)
                {
                    setTechFlashTint(refs[i].fighter, false);
                }
                if (s_net->dropIntangActive)
                {
                    clearWarpIntang(refs[i]);
                }
            }
        }
        s_net->revivalActive = false;
        s_net->revivalPlayer = -1;
        s_net->cpuMotionFrozen = false;
        s_net->slidePassDisabled = false;
        s_net->dropIntangActive = false;
        s_net->techFlashFrames = 0;
    }
#pragma dont_inline reset

#pragma dont_inline on
    static bool fighterHasActiveHitbox(Fighter* f)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
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
#pragma dont_inline reset

#pragma dont_inline on
    static void placeOnRevivalPlatform(const game::FighterRef& ref, float x, float y, float lr, float dmg, bool spendDj)
    {
        killRevivalPlatform(ref.fighter);
        placeFalling(ref, x, y, true, dmg, lr);
        refreshAirResources(ref, spendDj);
        const soModuleEnumeration* m = game::fighterModules(ref.fighter);
        if (m == NULL || !game::isRam(m->m_statusModule) || !game::isRam(m->m_workManageModule)
            || !game::isRam(ref.fighter->m_moduleAccesser))
        {
            return;
        }
        // Clear LA-Bit[35] before entering Rebirth so Brawl spawns the revival
        // platform effect (EfID 0x62), then immediately pin position, cancel
        // descent, hold wait timer, and set LA-Bit[35] to suppress exit invuln.
        m->m_workManageModule->offFlag(kLaBitRebirthNoInv);
        forceStatus(ref.fighter, game::kStatusRebirth);
        m->m_workManageModule->onFlag(kRaBitRebirthDone);
        m->m_workManageModule->offFlag(kLaBitRebirthDesc);
        m->m_workManageModule->onFlag(kLaBitRebirthNoInv);
        m->m_workManageModule->setInt(0, kRaBasicRebirthWait);
        if (game::isRam(m->m_postureModule))
        {
            Vec3f pos(x, y, 0.0f);
            m->m_postureModule->setPos(&pos);
            m->m_postureModule->setLr(lr);
        }
        if (game::isRam(m->m_groundModule))
        {
            m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_Air, 0);
        }
        clearWarpIntang(ref);
        if (s_net != NULL)
        {
            s_net->revivalActive = true;
            s_net->revivalPlayer = static_cast<s8>(ref.playerNo);
        }
    }
#pragma dont_inline reset

#pragma dont_inline on
    static void applyEdgeguardLaunch(const game::FighterRef& ref, const StageGeometry& geom, u8 strengthMode,
                                     int sideChoice, u32& rng, bool setNoclip, float damage)
    {
        bool leftSide = sideChoice == kScenarioSideLeft
                            ? true
                            : (sideChoice == kScenarioSideRight ? false : ((geomNextRng(rng) & 1u) != 0));
        const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
        float outward = leftSide ? -1.0f : 1.0f;
        EdgeguardLaunchParams lp;
        computeEdgeguardLaunch(strengthMode, ledge, outward, rng, lp);

        warpAirClean(ref, lp.startX, lp.startY, setNoclip, damage, -outward, true, false);

        const soModuleEnumeration* m = game::fighterModules(ref.fighter);
        if (m != NULL && game::isRam(m->m_statusModule) && game::isRam(m->m_workManageModule)
            && game::isRam(ref.fighter->m_moduleAccesser))
        {
            if (game::isRam(m->m_damageModule))
            {
                soDamageLog* log = m->m_damageModule->getDamageLog();
                if (game::isRam(log))
                {
                    log->m_reaction = lp.speed * 30.0f;
                    log->m_level = soDamage::Level_Fly;
                    log->m_speed.m_x = lp.kbX;
                    log->m_speed.m_y = lp.kbY;
                    log->m_angle = lp.angleDeg;
                    log->m_lr = -outward;
                    log->m_frame = static_cast<float>(lp.hitstun);
                    log->m_hitStopFrame = static_cast<u32>(lp.hitlag);
                }
            }
            m->m_workManageModule->setInt(lp.hitstun, kHitstunBasicId);
            forceStatus(ref.fighter, kStatusDamageFlyId);
            // Write knockback speed directly into Brawl's damage kinetic energy.
            if (game::isRam(m->m_kineticModule))
            {
                u32 list = game::wordAt(m->m_kineticModule, kKineticListOff);
                if (game::isRam(reinterpret_cast<void*>(list)))
                {
                    u32 energy = game::wordAt(reinterpret_cast<void*>(list), kDamageEnergySlot);
                    if (game::isRam(reinterpret_cast<void*>(energy)))
                    {
                        float* spd = reinterpret_cast<float*>(energy + kEnergySpeedOff);
                        spd[0] = lp.kbX;
                        spd[1] = lp.kbY;
                    }
                }
            }
            if (game::isRam(m->m_stopModule))
            {
                m->m_stopModule->setHitStopFrame(lp.hitlag, false);
            }
        }
        // Reset CPU Brain AFTER launch so havePrev == false suppresses launch-hitlag DI,
        // while P1 follow-ups still get full Survival DI.
        cpu::optionsResetSlot(ref.playerNo);
    }
#pragma dont_inline reset

#pragma dont_inline on
    void finishStandingWarp(const game::FighterRef& ref, bool setNoclip, bool fallIfNoGround)
    {
        Fighter* pair[2] = {ref.fighter, (ref.partner != NULL && ref.partner != ref.fighter) ? ref.partner : NULL};
        for (int i = 0; i < 2; i++)
        {
            if (pair[i] == NULL)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(pair[i]);
            if (m == NULL || !game::isRam(m->m_groundModule) || !game::isRam(m->m_statusModule)
                || !game::isRam(pair[i]->m_moduleAccesser))
            {
                continue;
            }
            m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_Ground_Cliff_Stop, 0);
            bool attached = m->m_groundModule->attachGround(0);
            if (attached)
            {
                m->m_groundModule->setStGroundForce(0);
            }
            bool useFall = fallIfNoGround && !attached;
            m->m_statusModule->changeStatusForce(useFall ? game::kStatusFall : game::kStatusWait,
                                                 pair[i]->m_moduleAccesser);
            m->m_groundModule->setCorrect(
                setNoclip ? soGroundShapeImpl::Correct_None
                          : (useFall ? soGroundShapeImpl::Correct_Air : soGroundShapeImpl::Correct_Ground_Cliff_Stop),
                0);
        }
    }

    static void setXlu(const soModuleEnumeration* m, int frames)
    {
        if (game::isRam(m->m_collisionHitModule))
        {
            m->m_collisionHitModule->setXluFrameGlobal(frames, 0);
        }
    }

    void warpGroundClean(const game::FighterRef& ref, float x, float y, bool noclip, float dmg, float lr,
                         bool fallIfNoGround)
    {
        placeStanding(ref, x, y, noclip, dmg, lr);
        finishStandingWarp(ref, noclip, fallIfNoGround);
        clearWarpIntang(ref);
    }

    static void setMotionFrozen(const soModuleEnumeration* cm, bool freeze)
    {
        if (s_net->cpuMotionFrozen != freeze && game::isRam(cm->m_motionModule))
        {
            cm->m_motionModule->setRate(freeze ? 0.0f : 1.0f);
            s_net->cpuMotionFrozen = freeze;
        }
    }

    static void setSlideNoPass(const soModuleEnumeration* cm, bool noPass)
    {
        if (s_net->slidePassDisabled != noPass && game::isRam(cm->m_groundModule))
        {
            cm->m_groundModule->setPassableCheck(!noPass, 0);
            s_net->slidePassDisabled = noPass;
        }
    }
#pragma dont_inline reset

    void scenarioPresetsDetach()
    {
        if (s_net != NULL)
        {
            restoreRuntimeMods();
            main::Settings& s = main::settings();
            u16 prevHmn = s.general.playerPercent;
            u16 prevCpu = s.cpu.percent;
            restoreScenarioSnapshot(s_net->state, s.cpu.options, s.comboTrainer, &s.general, &s.cpu, &s.osd,
                                    &s.techInvis);
            applyPercentsIfChanged(prevHmn, prevCpu, s);
            s_net->state.activePreset = kScenarioOff;
            freeNetPtr(s_net);
        }
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
    }

#pragma dont_inline on
    static void applyConfigAndPercents(ScenarioState& st, const ScenarioConfig* optCfg)
    {
        ScenarioConfig localCfg;
        const ScenarioConfig* cfg = optCfg;
        if (cfg == NULL)
        {
            readMenuConfigInto(localCfg);
            cfg = &localCfg;
        }
        main::Settings& s = main::settings();
        u16 prevHmn = s.general.playerPercent;
        u16 prevCpu = s.cpu.percent;
        applyScenarioConfig(st, *cfg, s.cpu.options, s.comboTrainer, &s.general, &s.cpu, &s.osd, &s.techInvis);
        applyPercentsIfChanged(prevHmn, prevCpu, s);
    }
#pragma dont_inline reset

    void scenarioPresetsPrepareSwap()
    {
        if (s_net == NULL)
        {
            return;
        }
        restoreRuntimeMods();
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        bool wasActive = s_state.activePreset != kScenarioOff || m->preset != kScenarioOff || s_state.snapshotActive;
        if (s_state.restoreCorrect[0] || s_state.restoreCorrect[1])
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int slot = 0; slot < 2; slot++)
            {
                if (!s_state.restoreCorrect[slot])
                {
                    continue;
                }
                game::FighterRef target;
                if (s_placedPlayer[slot] >= 0 && findPlayerRef(refs, n, s_placedPlayer[slot], target))
                {
                    setFighterCorrect(target, s_state.restoreCorrectKind[slot]);
                }
            }
        }
        m->preset = kScenarioOff;
        applyConfigAndPercents(s_state, NULL);
        scenarioTransientReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
        syncDebugBlock(NULL, NULL);
        if (wasActive)
        {
            TM_LOG("scenarios: swap off\n");
        }
    }

    void scenarioPresetsRestoreAfterSwap(const ui::pages::ScenarioMenuState& saved)
    {
        if (s_net == NULL || saved.preset == kScenarioOff || saved.preset >= kScenarioCount)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        memcpy(ui::pages::scenarioMenuState(), &saved, sizeof(saved));
        applyConfigAndPercents(s_state, NULL);
        requestScenarioManualReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        syncDebugBlock(NULL, NULL);
        TM_LOG("scenarios: swap %u\n", saved.preset);
    }

    void scenarioPresetsOnRender(const game::MatchState& match)
    {
        if (s_net == NULL || !match.running || game::trainingEnding())
        {
            s_wasPaused = false;
            s_resumeFromPause = false;
            return;
        }
        if (s_wasPaused && !match.paused && s_net->state.activePreset != kScenarioOff)
        {
            s_resumeFromPause = true;
        }
        s_wasPaused = match.paused;
    }

    bool scenarioPresetsWantsDraw(const game::MatchState& match)
    {
        return s_net != NULL && match.running && !match.paused && s_net->state.activePreset == kScenarioTechchase;
    }

    void scenarioPresetsDraw(const game::MatchState& match)
    {
        if (!scenarioPresetsWantsDraw(match))
        {
            return;
        }
        const ScenarioState& st = s_net->state;
        const ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        // Compact Brawl/P+ Techchase HUD card (top-right, zero left-accent bars).
        static const float kRects[6][4] = {
            {426.0f, 26.0f, 188.0f, 54.0f},
            {426.0f, 26.0f, 188.0f, 1.0f},
            {426.0f, 79.0f, 188.0f, 1.0f},
            {426.0f, 26.0f, 1.0f, 54.0f},
            {613.0f, 26.0f, 1.0f, 54.0f},
            {432.0f, 43.0f, 176.0f, 1.0f}
        };
        static const gfx::Rgba kCols[6] = {
            0x0E1520D8, 0x8EA8C4B0, 0x8EA8C4B0, 0x8EA8C4B0, 0x8EA8C4B0, 0xFFFFFF24
        };
        for (int i = 0; i < 6; i++)
        {
            gfx::fillRect(kRects[i][0], kRects[i][1], kRects[i][2], kRects[i][3], kCols[i]);
        }

        gfx::text(433.0f, 31.0f, "TECHCHASE", 0xDCE6F2FF, 0.48f);
        char buf[32];
        const char* posStr = "GROUND";
        if (st.resolvedTechPos != 0)
        {
            sprintf(buf, "PLAT %u", static_cast<unsigned>(st.resolvedTechPos));
            posStr = buf;
        }
        gfx::text(550.0f, 31.0f, posStr, 0x9BB8D8FF, 0.48f);

        const char* midStr = "READY";
        gfx::Rgba midCol = 0xB8C8DCFF;
        if (st.techResult == kTechResCover)
        {
            midCol = 0x4CE088FF;
            if (m->techShowReaction != 0 && st.techReactionFrames >= 0)
            {
                sprintf(buf, "COVER  REACT %df", static_cast<int>(st.techReactionFrames));
                midStr = buf;
            }
            else
            {
                midStr = "COVER";
            }
        }
        else if (st.techResult == kTechResEarly || st.techResult == kTechResLate)
        {
            bool early = st.techResult == kTechResEarly;
            midCol = early ? 0xF2C24CFF : 0xF05E68FF;
            sprintf(buf, early ? "%df EARLY" : "%df LATE", static_cast<int>(st.techDeltaFrames));
            midStr = buf;
        }
        gfx::text(433.0f, 48.0f, midStr, midCol, 0.52f);

        sprintf(buf, "STREAK %u  BEST %u", static_cast<unsigned>(st.techStreak),
                static_cast<unsigned>(st.techBest));
        gfx::text(433.0f, 65.0f, buf, 0xE6EEF8FF, 0.46f);
    }

    static void stepTechchasePosition(int delta)
    {
        if (s_net == NULL)
        {
            return;
        }
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        if (s_net->state.activePreset != kScenarioTechchase && m->preset != kScenarioTechchase)
        {
            return;
        }
        StageGeometry geom;
        liveGeometry(geom, true, true);
        StagePlatform sortedPlats[kMaxStagePlatforms];
        int nPlats = sortPlatformsLowestLeftToRight(geom, sortedPlats);
        u8 nextPos = stepTechPosition(m->techPosition, nPlats, delta);
        m->techPosition = nextPos;
        s_net->state.lastCfg.techPosition = nextPos;
        requestScenarioManualReset(s_net->state);
        scenarioCpuStateReset(s_net->cpuState);
        if (nextPos == 0)
        {
            storage::notice(storage::kNoticeOk, "Techchase: Ground");
        }
        else
        {
            storage::notice(storage::kNoticeOk, "Techchase: Platform %u", static_cast<unsigned>(nextPos));
        }
    }

    static void adjustScenarioSpacing(int delta)
    {
        if (s_net == NULL)
        {
            return;
        }
        int next = static_cast<int>(s_net->spacingOffset) + delta;
        if (next < -8) next = -8;
        else if (next > 12) next = 12;
        s_net->spacingOffset = static_cast<s8>(next);
        requestScenarioManualReset(s_net->state);
        scenarioCpuStateReset(s_net->cpuState);
        storage::notice(storage::kNoticeOk, "Spacing: %+d", next);
    }

    static void cycleShieldDropPlatform()
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        if (s_state.activePreset != kScenarioShieldDrop && m->preset != kScenarioShieldDrop)
        {
            return;
        }
        StageGeometry geom;
        liveGeometry(geom, true, true);
        int slotIdx[3];
        classifyPlatforms(geom, slotIdx);
        u8 avail[3];
        int count = 0;
        for (int s = 0; s < 3; s++)
        {
            if (slotIdx[s] >= 0)
            {
                avail[count++] = static_cast<u8>(s);
            }
        }
        if (count == 0)
        {
            storage::notice(storage::kNoticeWarn, "No platforms on this stage");
            return;
        }
        u8 cur = m->shieldDropPlat < 3 ? m->shieldDropPlat : s_state.resolvedPlatformSlot;
        u8 nextSlot = avail[0];
        for (int i = 0; i < count; i++)
        {
            if (avail[i] > cur)
            {
                nextSlot = avail[i];
                break;
            }
        }
        m->shieldDropPlat = nextSlot;
        s_state.lastCfg.shieldDropPlat = nextSlot;
        requestScenarioManualReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        static const char* const kSlotNames[3] = {"Left", "Right", "Top"};
        storage::notice(storage::kNoticeOk, "Platform: %s", kSlotNames[nextSlot]);
        TM_LOG("scenarios: sd %s\n", kSlotNames[nextSlot]);
    }

    void scenarioPresetsCall(int call)
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        switch (call)
        {
        case main::kScenarioChanged:
            restoreRuntimeMods();
            s_net->spacingOffset = 0;
            s_net->ledgetechWallWarned = false;
            applyConfigAndPercents(s_state, NULL);
            scenarioCpuStateReset(s_cpuState);
            break;
        case main::kScenarioResetNow:
            restoreRuntimeMods();
            requestScenarioManualReset(s_state);
            scenarioCpuStateReset(s_cpuState);
            break;
        case main::kScenarioSavestateLoaded:
            restoreRuntimeMods();
            scenarioTransientReset(s_state);
            scenarioCpuStateReset(s_cpuState);
            stageGeomReset();
            break;
        case main::kScenarioSettingsReset:
            restoreRuntimeMods();
            s_net->spacingOffset = 0;
            s_net->ledgetechWallWarned = false;
            discardScenarioSnapshot(s_state);
            scenarioCpuStateReset(s_cpuState);
            initScratchDefaults();
            break;
        case main::kScenarioCyclePlatform:
            cycleShieldDropPlatform();
            break;
        case main::kScenarioComboReposition:
            comboTrainerRepositionCpu();
            break;
        case main::kScenarioStepPosUp:
            stepTechchasePosition(1);
            break;
        case main::kScenarioStepPosDown:
            stepTechchasePosition(-1);
            break;
        case main::kScenarioSpacingCloser:
            adjustScenarioSpacing(-2);
            break;
        case main::kScenarioSpacingFarther:
            adjustScenarioSpacing(2);
            break;
        default:
            break;
        }
        syncDebugBlock(NULL, NULL);
    }

    int scenarioPresetsEdgeguardSide()
    {
        if (s_net == NULL || s_net->state.activePreset != kScenarioEdgeguard)
        {
            return kScenarioSideRandom;
        }
        ScenarioConfig cfg;
        readMenuConfigInto(cfg);
        return cfg.side < kScenarioSideCount ? static_cast<int>(cfg.side) : static_cast<int>(kScenarioSideRandom);
    }

#pragma dont_inline on
    int readFighterHitlag(const soModuleEnumeration* m)
    {
        int hl = game::isRam(m->m_stopModule) ? m->m_stopModule->getHitStopRealFrame() : 0;
        return hl > 0 ? hl : 0;
    }

    bool isFighterAir(const soModuleEnumeration* m)
    {
        return game::isRam(m->m_situationModule) && m->m_situationModule->getKind() == Situation_Air;
    }

    static bool readCanAct(Fighter* f, int hitlag, int hitstun)
    {
        cpu::View vw;
        return cpu::readView(f, vw) ? cpu::canAct(vw) : (hitlag == 0 && hitstun == 0);
    }

    bool findHmnAndCpu(const game::FighterRef* refs, int n, game::FighterRef& hmnRef, game::FighterRef& cpuRef,
                       bool& haveHmn, bool& haveCpu)
    {
        haveHmn = false;
        haveCpu = false;
        for (int i = 0; i < n; i++)
        {
            if (!refs[i].cpu && !haveHmn)
            {
                hmnRef = refs[i];
                haveHmn = true;
            }
            else if (refs[i].cpu && !haveCpu)
            {
                cpuRef = refs[i];
                haveCpu = true;
            }
        }
        return haveHmn && haveCpu;
    }

    static void cancelLandingLag(const game::FighterRef& target)
    {
        Fighter* pair[2] = {target.fighter,
                            (target.partner != NULL && target.partner != target.fighter) ? target.partner : NULL};
        for (int p = 0; p < 2; p++)
        {
            if (pair[p] == NULL)
            {
                continue;
            }
            const soModuleEnumeration* tm = game::fighterModules(pair[p]);
            int st = tm != NULL ? game::fighterStatus(tm) : -1;
            if (st >= game::kStatusLanding && st <= game::kStatusLandingFallSpecial)
            {
                forceStatus(pair[p], game::kStatusWait);
            }
        }
    }
#pragma dont_inline reset

    void scenarioPresetsFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ScenarioDebugBlock& s_debug = s_net->debug;
        ScenarioConfig cfg;
        readMenuConfigInto(cfg);
        // Fast path when Off: zero per-frame fighter reads, writes, or overrides.
        if (cfg.preset == kScenarioOff && s_state.activePreset == kScenarioOff && !s_state.restoreCorrect[0]
            && !s_state.restoreCorrect[1])
        {
            s_debug.frame = game::frameCounter();
            s_debug.activePreset = kScenarioOff;
            s_debug.snapshotActive = s_state.snapshotActive ? 1 : 0;
            s_debug.autoReset = cfg.autoReset;
            s_debug.side = cfg.side;
            s_debug.multiHitMove = cfg.multiHitMove;
            s_resumeFromPause = false;
            return;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef;
        game::FighterRef cpuRef;
        bool haveHmn = false;
        bool haveCpu = false;
        if (!findHmnAndCpu(refs, n, hmnRef, cpuRef, haveHmn, haveCpu))
        {
            applyConfigAndPercents(s_state, &cfg);
            syncDebugBlock(haveHmn ? hmnRef.fighter : NULL, haveCpu ? cpuRef.fighter : NULL);
            return;
        }

        // Returning from Brawl Training pause: re-apply preset overrides,
        // re-claim CPU brain, and trigger a clean scenario reset!
        if (s_resumeFromPause)
        {
            s_resumeFromPause = false;
            if (s_state.activePreset != kScenarioOff)
            {
                restoreRuntimeMods();
                main::Settings& s = main::settings();
                reapplyActiveScenarioOverrides(s_state, cfg, s.cpu.options, s.comboTrainer, &s.general, &s.cpu, &s.osd,
                                               &s.techInvis);
                lab::applyPercent(main::kSideHuman);
                lab::applyPercent(main::kSideCpu);
                requestScenarioManualReset(s_state);
                scenarioCpuStateReset(s_cpuState);
                TM_LOG("scenarios: resume %u\n", s_state.activePreset);
            }
        }

        game::FighterState hmnFs;
        game::FighterState cpuFs;
        if (!game::readState(hmnRef, hmnFs) || !game::readState(cpuRef, cpuFs))
        {
            return;
        }

        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        const soModuleEnumeration* cm = game::fighterModules(cpuRef.fighter);
        if (hm == NULL || cm == NULL)
        {
            return;
        }

        // Maintain Ledgetech Revival Platform hold (pin wait timer & clear invuln on exit).
        bool hmnOnRevivalPlat = false;
        if (s_net->revivalActive)
        {
            if (hmnFs.status == game::kStatusRebirth)
            {
                hmnOnRevivalPlat = true;
                if (game::isRam(hm->m_workManageModule))
                {
                    hm->m_workManageModule->setInt(0, kRaBasicRebirthWait);
                    hm->m_workManageModule->onFlag(kLaBitRebirthNoInv);
                }
            }
            else
            {
                s_net->revivalActive = false;
                clearWarpIntang(hmnRef);
                if (cfg.preset == kScenarioLedgetech && cfg.ledgetechDj == kLtDjSpent)
                {
                    refreshAirResources(hmnRef, true);
                }
            }
        }

        // Clear Techchase initial Tumble Drop intangibility once CPU leaves DamageFall.
        if (s_net->dropIntangActive && cpuFs.status != kStatusDamageFallId)
        {
            clearWarpIntang(cpuRef);
            s_net->dropIntangActive = false;
        }

        // Tick down Techchase Intangible Flash pale tint cleanly.
        if (s_net->techFlashFrames > 0)
        {
            s_net->techFlashFrames--;
            if (s_net->techFlashFrames == 0)
            {
                setTechFlashTint(cpuRef.fighter, false);
            }
        }

        game::Body cpuBody;
        bool haveCpuBody = game::readBody(cpuRef.fighter, cpuBody);

        ScenarioStepInput in;
        memset(&in, 0, sizeof(in));
        in.hmnStatus = hmnFs.status;
        in.hmnHitlag = readFighterHitlag(hm);
        in.hmnDamageHitlag = in.hmnHitlag > 0 && game::isRam(hm->m_stopModule) && hm->m_stopModule->isDamage();
        in.hmnHitstun = hmnFs.hitstun;
        in.hmnShieldStun = hmnFs.shieldStun;
        in.hmnX = hmnFs.x;
        in.hmnY = hmnFs.y;
        in.hmnAir = isFighterAir(hm);
        in.hmnOutOfPlay = hmnOnRevivalPlat ? false : game::outOfPlay(hmnFs.status, true);
        in.hmnActionable = readCanAct(hmnRef.fighter, in.hmnHitlag, in.hmnHitstun);
        in.hmnEnteredPass = hmnFs.status == kStatusPassId || sawIntraFrameStatus(hmnRef.playerNo, kStatusPassId);

        in.cpuStatus = cpuFs.status;
        in.cpuHitlag = readFighterHitlag(cm);
        in.cpuHitstun = cpuFs.hitstun;
        in.cpuShieldStun = cpuFs.shieldStun;
        in.cpuFtKind = game::fighterKind(cpuRef.entryId, false);
        in.cpuTechDist = game::techDistinguishable(in.cpuFtKind);
        in.cpuX = cpuFs.x;
        in.cpuY = cpuFs.y;
        in.cpuAir = isFighterAir(cm);
        in.cpuOutOfPlay = game::outOfPlay(cpuFs.status, true);
        in.cpuActionable = readCanAct(cpuRef.fighter, in.cpuHitlag, in.cpuHitstun);
        in.cpuIntangible = haveCpuBody && cpuBody.intangible != 0;

        bool needFreshGeom = s_state.pendingReset || cfg.preset != s_state.activePreset
                             || cfg.side != s_state.lastCfg.side || cfg.multiHitMove != s_state.lastCfg.multiHitMove
                             || cfg.shieldDropPlat != s_state.lastCfg.shieldDropPlat
                             || cfg.techPosition != s_state.lastCfg.techPosition;
        bool needPlatforms = cfg.preset == kScenarioShieldDrop || cfg.preset == kScenarioSlideOff
                             || cfg.preset == kScenarioTechchase || s_state.activePreset == kScenarioShieldDrop
                             || s_state.activePreset == kScenarioSlideOff
                             || s_state.activePreset == kScenarioTechchase;
        liveGeometry(in.geom, needFreshGeom, needPlatforms);

        // Stir RNG with live frame counter so random choices vary cleanly across warps.
        s_state.rng ^= game::frameCounter() * 0x9E3779B9u;
        if (s_state.rng == 0)
        {
            s_state.rng = 0x51A7C0DEu;
        }

        u16 prevBest = s_state.techBest;
        ScenarioStepAction act;
        main::Settings& s = main::settings();
        stepScenario(s_state, cfg, in, s.cpu.options, s.comboTrainer, act, &s.general, &s.cpu, &s.osd, &s.techInvis);
        if (s_state.techBest > prevBest)
        {
            drills::Session* sess = drills::sessionIn(main::services().settings());
            if (sess != NULL)
            {
                sess->best[kTechBestSlot] = s_state.techBest;
            }
        }
        if (act.applyHmnPercent)
        {
            lab::applyPercent(main::kSideHuman);
        }
        if (act.applyCpuPercent)
        {
            lab::applyPercent(main::kSideCpu);
        }

        // Techchase Intangible Flash cue: grant pale tint + 12f intangibility
        // on the first actionable escape frame so late covers visibly whiff.
        if (act.techFlashCpu)
        {
            setXlu(cm, 12);
            setTechFlashTint(cpuRef.fighter, true);
            s_net->techFlashFrames = 12;
        }

        // Shield Pressure Defense: CPU becomes intangible once actionable after
        // swinging so late OoS punishes visibly whiff.
        if (s_state.activePreset == kScenarioShieldPressure && cfg.shieldRole == kShieldRoleDefense
            && s_state.shieldDefSwung && in.cpuActionable && in.cpuHitstun == 0 && in.cpuHitlag == 0)
        {
            setXlu(cm, 2);
        }

        // Ledgetech Hold Active: freeze CPU attack on its active hitbox frame
        // until P1 gets hit, grabs ledge, or resets.
        bool wantHold = s_state.activePreset == kScenarioLedgetech && cfg.ledgetechTiming == kLtTimeHoldActive
                        && shouldHoldLedgetechActive(cfg.ledgetechTiming, fighterHasActiveHitbox(cpuRef.fighter),
                                                     in.hmnHitstun > 0 || in.hmnHitlag > 0,
                                                     hmnFs.status >= 0x73 && hmnFs.status <= 0x7B, in.hmnOutOfPlay);
        setMotionFrozen(cm, wantHold);

        // Under-Platform Slide-Off: disable CPU platform pass-through landing
        // during AttackAir (0x33) so CPU does not accidentally land on the platform.
        bool wantNoPass = s_state.activePreset == kScenarioSlideOff && s_state.hasPlatform
                          && cfg.slideOffAttack != kSlideAtkOnPlatFtilt && in.cpuAir
                          && (cpuFs.status == game::kStatusAttackAirFirst || cpuFs.status == game::kStatusJump);
        setSlideNoPass(cm, wantNoPass);

        // Apply 1-frame noclip restores and frames 2..3 spurious Landing lag cancellation.
        bool wantCancelLag = !act.warpHmn && !act.warpCpu && s_state.activePreset != kScenarioOff
                             && s_state.sinceResetFrames >= 2 && s_state.sinceResetFrames <= 3;
        for (int slot = 0; slot < 2; slot++)
        {
            bool doRestore = act.restoreCorrect[slot];
            bool doCancel = wantCancelLag && s_state.restoreCorrectKind[slot] == kCorrectGroundCliffStop;
            if (!doRestore && !doCancel)
            {
                continue;
            }
            game::FighterRef target = slot == 0 ? hmnRef : cpuRef;
            if (s_placedPlayer[slot] >= 0)
            {
                findPlayerRef(refs, n, s_placedPlayer[slot], target);
            }
            if (doRestore)
            {
                setFighterCorrect(target, act.restoreCorrectKind[slot]);
                if (act.restoreCorrectKind[slot] == kCorrectAir)
                {
                    bool spendDj = slot == 0 && cfg.preset == kScenarioLedgetech && cfg.ledgetechDj == kLtDjSpent;
                    if (!(slot == 0 && s_net->revivalActive)
                        && !(slot == 1 && s_state.activePreset == kScenarioEdgeguard
                             && cfg.edgeguardSetup == kEgSetupSpawn && cfg.edgeguardDj != kEgDjAvail))
                    {
                        refreshAirResources(target, spendDj);
                    }
                }
            }
            if (doCancel)
            {
                cancelLandingLag(target);
            }
        }

        float hmnDmg = act.useExplicitHmnDmg ? act.hmnDmg : static_cast<float>(s.general.playerPercent);
        float cpuDmg = act.useExplicitCpuDmg ? act.cpuDmg : static_cast<float>(s.cpu.percent);
        if (act.warpHmn || act.warpCpu)
        {
            restoreRuntimeMods();
        }
        if (act.warpHmn)
        {
            if (act.spawns.hmnRevivalPlat)
            {
                placeOnRevivalPlatform(hmnRef, act.spawns.hmnX, act.spawns.hmnY, act.spawns.hmnLr, hmnDmg,
                                       act.hmnSpendDj);
            }
            else if (act.spawns.hmnFalling)
            {
                warpAirClean(hmnRef, act.spawns.hmnX, act.spawns.hmnY, act.setNoclip[0], hmnDmg, act.spawns.hmnLr,
                             act.refreshHmnAir, act.hmnSpendDj);
            }
            else
            {
                warpGroundClean(hmnRef, act.spawns.hmnX, act.spawns.hmnY, act.setNoclip[0], hmnDmg, act.spawns.hmnLr);
            }
            s_placedPlayer[0] = static_cast<s8>(hmnRef.playerNo);

            // Non-intrusive notice if current stage lacks a vertical wall below ledge.
            if (s_state.activePreset == kScenarioLedgetech && !s_net->ledgetechWallWarned && in.geom.hasLedges)
            {
                bool leftSide = s_state.resolvedSide == kScenarioSideLeft;
                const LedgeInfo& ledge = leftSide ? in.geom.leftLedge : in.geom.rightLedge;
                float outward = leftSide ? -1.0f : 1.0f;
                bool hasWall = rayCheckWall(ledge.x + outward * 14.0f, ledge.y - 12.0f, -outward, 24.0f)
                               || rayCheckWall(ledge.x + outward * 14.0f, ledge.y - 22.0f, -outward, 24.0f);
                if (!hasWall)
                {
                    s_net->ledgetechWallWarned = true;
                    storage::notice(storage::kNoticeWarn, "No wall below ledge on this stage");
                }
            }
        }
        bool cpuLaunchedEdgeguard = false;
        if (act.warpCpu)
        {
            if (act.edgeguardLaunchCpu && in.geom.hasLedges)
            {
                applyEdgeguardLaunch(cpuRef, in.geom, cfg.edgeguardStrength, s_state.resolvedSide, s_state.rng,
                                     act.setNoclip[1], cpuDmg);
                cpuLaunchedEdgeguard = true;
            }
            else if (act.spawns.cpuFalling)
            {
                bool spendCpuDj = s_state.activePreset == kScenarioEdgeguard && cfg.edgeguardSetup == kEgSetupSpawn
                                  && (cfg.edgeguardDj == kEgDjSpent
                                      || (cfg.edgeguardDj == kEgDjRandom && (geomNextRng(s_state.rng) & 1u) != 0));
                warpAirClean(cpuRef, act.spawns.cpuX, act.spawns.cpuY, act.setNoclip[1], cpuDmg, act.spawns.cpuLr,
                             act.refreshCpuAir, spendCpuDj);
                if (act.spawns.cpuTumbleDrop)
                {
                    forceStatus(cpuRef.fighter, kStatusDamageFallId);
                    setXlu(cm, 28);
                    s_net->dropIntangActive = true;
                }
            }
            else
            {
                warpGroundClean(cpuRef, act.spawns.cpuX, act.spawns.cpuY, act.setNoclip[1], cpuDmg, act.spawns.cpuLr);
                if (act.cpuDownWait)
                {
                    forceStatus(cpuRef.fighter, kStatusDownWaitId);
                }
            }
            s_placedPlayer[1] = static_cast<s8>(cpuRef.playerNo);
        }
        if (act.resetComboTrainer)
        {
            comboTrainerReset();
            if (s_state.activePreset == kScenarioComboLab && act.warpHmn && act.warpCpu)
            {
                comboTrainerSetAnchors(act.spawns.hmnX, act.spawns.hmnY, act.spawns.hmnLr, act.spawns.cpuX,
                                       act.spawns.cpuY);
            }
            coreDrillEnginesSavestateReset();
            hoverPsResetTransient();
            pplusTechReset();
            if (!cpuLaunchedEdgeguard)
            {
                cpu::optionsResetAll();
            }
            for (int i = 0; i < n; i++)
            {
                if (refs[i].cpu && refs[i].playerNo >= 0 && refs[i].playerNo < game::kMaxFighters)
                {
                    pplusRecoveryReset(refs[i].playerNo);
                }
            }
            scenarioCpuStateReset(s_cpuState);
        }

        syncDebugBlock(hmnRef.fighter, cpuRef.fighter);
    }

    bool scenarioPresetsCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out,
                                u32 lastButtons)
    {
        if (s_net == NULL || !who.cpu || f == NULL)
        {
            return false;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        const bool psDrill = (s_state.activePreset == kScenarioOff)
                             && (drillHudActiveId() == drills::kDrillPowershield)
                             && !drillHudModalOpen();
        ScenarioConfig cfg;
        readMenuConfigInto(cfg);
        if (!psDrill)
        {
            bool grabActive = s_state.activePreset == kScenarioGrabEscape && cfg.grabAction != kGrabActStand;
            bool shieldDefActive =
                s_state.activePreset == kScenarioShieldPressure && cfg.shieldRole == kShieldRoleDefense;
            bool sdActive = s_state.activePreset == kScenarioShieldDrop && cfg.shieldDropAttack != kSdAtkOff;
            if (s_state.activePreset != kScenarioMultiHitSdi && s_state.activePreset != kScenarioLedgetech
                && s_state.activePreset != kScenarioSlideOff && !grabActive && !shieldDefActive && !sdActive)
            {
                return false;
            }
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef, cpuRef;
        bool haveHmn = false, haveCpu = false;
        findHmnAndCpu(refs, n, hmnRef, cpuRef, haveHmn, haveCpu);
        if (!haveHmn)
        {
            return false;
        }
        game::FighterState hmnFs;
        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        if (hm == NULL || !game::readState(hmnRef, hmnFs))
        {
            return false;
        }

        ScenarioCpuInput in;
        memset(&in, 0, sizeof(in));
        in.preset = s_state.activePreset;
        in.multiHitMove = cfg.multiHitMove;
        in.ledgetechAttack = s_state.resolvedLedgetechAttack;
        in.ledgetechTiming = cfg.ledgetechTiming;
        in.shieldRole = cfg.shieldRole;
        in.shieldDefAttack = s_state.resolvedShieldDefAtk;
        in.shieldDropAttack = cfg.shieldDropAttack;
        in.slideOffAttack = cfg.slideOffAttack;
        in.grabAction = cfg.grabAction;
        in.hasPlatform = s_state.hasPlatform;
        in.rng = geomNextRng(s_state.rng) ^ game::frameCounter();
        in.cpuFtKind = game::fighterKind(who.entryId, who.sub);
        in.cpuStatus = v.status;
        in.hmnStatus = hmnFs.status;
        in.cpuAir = v.air;
        in.cpuCanAct = cpu::canAct(v);
        in.cpuTakeoffNext = cpu::takeoffNext(v);
        in.cpuX = v.x;
        in.cpuY = v.y;
        in.cpuVx = v.vx;
        in.cpuVy = v.vy;
        in.cpuLr = v.lr;
        in.hmnX = hmnFs.x;
        in.hmnY = hmnFs.y;
        in.hmnAir = isFighterAir(hm);
        in.hmnHitstun = hmnFs.hitstun;
        in.hmnOutOfPlay = (s_net->revivalActive && hmnFs.status == game::kStatusRebirth)
                              ? false
                              : game::outOfPlay(hmnFs.status, true);
        in.cpuOutOfPlay = game::outOfPlay(v.status, true);
        in.lastButtons = lastButtons;

        ScenarioCpuOutput cpuOut;
        if (psDrill)
        {
            stepPowershieldDrillCpu(in, s_cpuState, cpuOut);
        }
        else
        {
            stepScenarioCpu(in, s_cpuState, cpuOut);
        }
        if (!cpuOut.active)
        {
            return false;
        }
        out.buttons = cpuOut.buttons;
        out.x = cpuOut.stickX;
        out.y = cpuOut.stickY;
        return true;
    }

    void scenarioPresetsSavedView(main::Settings& out)
    {
        if (s_net == NULL)
        {
            return;
        }
        projectScenarioSavedView(s_net->state, out.cpu.options, out.comboTrainer, &out.general, &out.cpu, &out.osd,
                                 &out.techInvis);
    }

    bool scenarioPresetsEdgeguardRespawn(const game::FighterRef& ref, const StageGeometry& geom, u32& rng,
                                         bool setNoclip, float damage)
    {
        if (s_net == NULL || s_net->state.activePreset != kScenarioEdgeguard || !geom.hasLedges)
        {
            return false;
        }
        ScenarioConfig cfg;
        readMenuConfigInto(cfg);
        if (cfg.edgeguardSetup == kEgSetupLaunch)
        {
            applyEdgeguardLaunch(ref, geom, cfg.edgeguardStrength, cfg.side, rng, setNoclip, damage);
            return true;
        }
        float wx = 0.0f, wy = 0.0f;
        if (!sampleEdgeguardSpawn(geom, cfg.edgeguardDist, cfg.side, rng, wx, wy))
        {
            return false;
        }
        float lr = wx >= 0.5f * (geom.floor.leftX + geom.floor.rightX) ? -1.0f : 1.0f;
        bool spendDj =
            cfg.edgeguardDj == kEgDjSpent || (cfg.edgeguardDj == kEgDjRandom && (geomNextRng(rng) & 1u) != 0);
        warpAirClean(ref, wx, wy, setNoclip, damage, lr, true, spendDj);
        cpu::optionsResetSlot(ref.playerNo);
        return true;
    }
}
}
