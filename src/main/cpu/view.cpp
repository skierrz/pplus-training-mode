#include "main/cpu/view.h"

#include <ft/fighter.h>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_task_scheduler.h>
#include <gm/gm_global.h>
#include <it/it_manager.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/collision/so_collision_shield_module_impl.h>
#include <so/controller/so_controller_module_impl.h>
#include <so/damage/so_damage_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/kinetic/so_kinetic_module_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/sound/so_sound_module_impl.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <st/st_utility.h>

#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/status_ids.h"
#include "shared/game/mem.h"

// What the CPU Options read on a fighter, and the little they write.
// Status, hitstun and the work layout: main/game/fighters.cpp; hitlag and
// interrupt groups: main/game/tracker.cpp. Facing, shield health and
// knockback speed: docs/internals.md, "Fighter values the CPU reads".
// Landing's interrupt frame: docs/internals.md, "Landing interrupts".
namespace tm {
namespace cpu {
    enum {
        kPosturePos = 0xC,
        kPosturePrevPos = 0x18,
        kPostureLr = 0x40,
        kAccesser = 0x60, // Fighter: its soModuleAccesser
        kStatusPassive = 0x60,
        kStatusPassiveFb = 0x61,
        kStatusSlip = 0x89,
        kStatusSlipDown = 0x8A,
        kWorkInstance = 0x20,
        kWorkFloats = 0x14,
        kWorkFloatCount = 0x18,
        kShieldFloat = 3,
        kLandingFrame = 3074, // ftValueAccesser customize parameter (float)
        kLandingLightFrame = 3073, // the same for LandingLight
        kNoLandingInterrupt = 0x12000003, // LA-Bit[3] (instance work flag 3)
        kGuardOffTimer = 0x20000005,      // RA-Basic[5] (status work int 5)
        kJumpCount = 0x10000001,          // LA-Basic[1] (jumps used; common.md:208)
        kJumpCountMax = 23003,            // IC-Basic[23003] Customize_Param_Int_Jump_Count_Max
        kKineticEnergies = 0x14,
        kDamageEnergy = 0x4C,
        kEnergySpeed = 0x8,
        kGravityEnergy = 0x58, // the kinetic module's energy list: pointer, then id
        kGravityId = 0x5C,
        kGravityAccel = 0x10,
        kGroundActionGroups = (1 << game::kGroupGroundSpecial) | (1 << game::kGroupGroundCatch)
                              | (1 << game::kGroupGroundAttack) | (1 << game::kGroupGroundEscape)
                              | (1 << game::kGroupGroundGuard) | (1 << game::kGroupGroundJump),
        kAirActionGroups = (1 << game::kGroupAirSpecial) | (1 << game::kGroupAirEscape)
                           | (1 << game::kGroupAirAttack) | (1 << game::kGroupAirJumpAerial)
    };

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    Fighter* fighterOf(int entryId, bool sub)
    {
        ftManager* manager = g_ftManager;
        ftEntryManager* entries = g_ftEntryManager;
        if (!game::isRam(manager) || !game::isRam(entries) || !manager->isValidEntryId(entryId))
        {
            return NULL;
        }
        if (sub)
        {
            Fighter* partner = manager->getSubFighter(entryId);
            return game::isRam(partner) ? partner : NULL;
        }
        ftEntry* entry = entries->getEntity(entryId);
        if (!game::isRam(entry) || entry->m_entryId != entryId)
        {
            return NULL;
        }
        int active = entry->m_activeInstanceIndex;
        if (active < 0 || active >= 4)
        {
            return NULL;
        }
        Fighter* f = entry->m_instances[active].m_fighter;
        return game::isRam(f) ? f : NULL;
    }

    int fighterKind(int entryId)
    {
        return game::fighterKind(entryId);
    }

    static gmPlayerInitData* setupOf(int playerNo)
    {
        GameGlobal* global = g_GameGlobal;
        if (!game::isRam(global) || !game::isRam(global->m_modeMelee) || playerNo < 0 || playerNo >= MAX_PLAYERS)
        {
            return NULL;
        }
        return &global->m_modeMelee->m_playersInitData[playerNo];
    }

    bool humanPlayer(int playerNo)
    {
        return game::humanPlayer(playerNo);
    }

    int controllerOf(int playerNo)
    {
        gmPlayerInitData* p = setupOf(playerNo);
        return p != NULL ? p->m_controllerNo : 0;
    }

    void setControllerOf(int playerNo, int port)
    {
        gmPlayerInitData* p = setupOf(playerNo);
        if (p != NULL)
        {
            p->m_controllerNo = static_cast<s8>(port);
        }
    }

    static bool freeStatus(int s)
    {
        switch (s)
        {
        case game::kStatusWait:
        case game::kStatusWalk:
        case game::kStatusWalkBrake:
        case game::kStatusDash:
        case game::kStatusRun:
        case game::kStatusTurn:
        case game::kStatusTurnDash:
        case game::kStatusTurnRun:
        case game::kStatusSquat:
        case game::kStatusSquatWait:
        case game::kStatusFall:
        case game::kStatusFallAerial:
        case game::kStatusGuardOn:
        case game::kStatusGuard:
            return true;
        default:
            return false;
        }
    }

    static bool escape(int status)
    {
        return status >= game::kStatusEscape && status < game::kStatusEscapeAir;
    }

    bool readView(Fighter* fighter, View& out)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_postureModule) || !game::isRam(m->m_statusModule)
            || !game::isRam(m->m_situationModule) || !game::isRam(m->m_stopModule))
        {
            return false;
        }
        const void* posture = m->m_postureModule;
        out.x = floatAt(posture, kPosturePos);
        out.y = floatAt(posture, kPosturePos + 4);
        out.vx = out.x - floatAt(posture, kPosturePrevPos);
        out.vy = out.y - floatAt(posture, kPosturePrevPos + 4);
        out.lr = floatAt(posture, kPostureLr) < 0.0f ? -1.0f : 1.0f;
        out.status = game::fighterStatus(m);
        out.hitstun = game::fighterHitstun(m);
        out.shieldStun = game::fighterShieldStun(m, out.status);
        out.guardOff = out.status == game::kStatusGuardOff && game::isRam(m->m_workManageModule)
                           ? m->m_workManageModule->getInt(kGuardOffTimer)
                           : 0;
        int stop = m->m_stopModule->getHitStopRealFrame();
        out.hitlag = stop > 0 ? stop : 0;
        // soStopModuleImpl::setHitStopFrame (0x80771EAC) sets m_isDamage (+0x1C
        // bit 0x20) on damage hits (0x8076D32C); attacker (0x8083FFC0) and
        // shield (0x80840690) hitlag pass 0.
        out.damageHitlag = stop > 0 && m->m_stopModule->isDamage();
        out.air = m->m_situationModule->getKind() == Situation_Air;
        out.motionFrame = out.motionRate = out.motionEnd = 0.0f;
        // Not the game's getEndFrame: it reads the animation resource
        // unchecked and faults when a status has none.
        bool timed = out.status == game::kStatusJumpSquat || out.status == game::kStatusGuardOff
                     || out.status == kStatusPassive || out.status == kStatusPassiveFb
                     || out.status == kStatusSlip || out.status == kStatusSlipDown
                     || out.status == game::kStatusLanding || out.status == game::kStatusLandingLight
                     || escape(out.status);
        game::MotionInfo info;
        out.interruptAt = -1.0f;
        if (timed && game::readMotion(fighter, info) && info.length > 0)
        {
            out.motionFrame = m->m_motionModule->getFrame();
            out.motionRate = m->m_motionModule->getRate();
            out.motionEnd = static_cast<float>(info.length - 1);
            soModuleAccesser* accesser =
                *reinterpret_cast<soModuleAccesser**>(reinterpret_cast<u32>(fighter) + kAccesser);
            // LA-Bit[3]: no interrupt at all (the status ends with its
            // animation). A stretched landing (LA-Float[0]) is in motionRate.
            if ((out.status == game::kStatusLanding || out.status == game::kStatusLandingLight) && game::isRam(accesser)
                && !(game::isRam(m->m_workManageModule) && m->m_workManageModule->isFlag(kNoLandingInterrupt)))
            {
                out.interruptAt = soValueAccesser::getConstantFloat(
                    accesser, out.status == game::kStatusLanding ? kLandingFrame : kLandingLightFrame, 0);
            }
        }
        out.groups = 0;
        for (int g = 1; g < game::kGroupCount; g++)
        {
            if (m->m_statusModule->isEnableTransitionTermGroup(g))
            {
                out.groups |= 1u << g;
            }
        }
        // Only an opening by a dodge's own script counts.
        int how = main::kActNone;
        out.opensNext = out.status >= game::kStatusEscape && out.status < game::kStatusEscapeAir
                        && main::extraActIn(fighter, &how) == 1 && how == main::kActScript;
        return true;
    }

    int framesUntilActionable(Fighter* fighter)
    {
        int how;
        return main::extraActIn(fighter, &how);
    }

    bool canAct(const View& v)
    {
        // Bowser's and Jigglypuff's rolls open interrupts before the
        // animation ends, during the coming frame; nothing read live shows it.
        if (v.opensNext)
        {
            return true;
        }
        // Ends with the coming frame: Wait in that frame, and a press acts
        // (observed, character not recorded: A then jabs a frame early).
        if (escape(v.status) && animationEndsNext(v))
        {
            return true;
        }
        // GuardOff counts RA-Basic[5] down from 15 and allows interrupts
        // at 0; after a powershield (RA-Basic[3] > 0), P+ opens Ground_Special,
        // Ground_Item, Ground_Attack and Guard_On immediately
        // (MoveDef/PowerShield.asm:110-124, common.md:608-610). An animation
        // ending first goes to Wait sooner (observed: Jigglypuff).
        if (v.status == game::kStatusGuardOff)
        {
            return v.guardOff <= 1 || animationEndsNext(v) || (v.groups & kGroundActionGroups) != 0;
        }
        // The values are the previous frame's: a stun showing 1 runs out in
        // the coming frame, before the controls are acted on (a held guard
        // started Guard_On on the frame hitstun read 0, 1 the frame before).
        // A crouch cancel can land the fighter in Landing with hitstun
        // counting; only the landing lag holds it then (observed, character
        // and move not recorded: a held guard shields 7 frames before
        // hitstun ends).
        const bool landing = !v.air && (v.status == game::kStatusLanding || v.status == game::kStatusLandingLight);
        if (v.hitlag > 0 || (v.hitstun > 1 && !landing)
            || (v.status == game::kStatusGuardDamage && v.shieldStun > 1))
        {
            return false;
        }
        if ((v.hitstun == 1 && !landing) || v.status == game::kStatusGuardDamage)
        {
            return true;
        }
        // docs/internals.md, "Landing interrupts".
        if (landing && v.interruptAt >= 0.0f && v.motionRate > 0.0f
            && v.motionFrame + v.motionRate > v.interruptAt + 0.01f)
        {
            return true;
        }
        u32 actions = v.air ? kAirActionGroups : kGroundActionGroups;
        return freeStatus(v.status) || (v.groups & actions) != 0;
    }

    bool animationEndsNext(const View& v)
    {
        return v.motionRate > 0.0f && v.motionFrame + v.motionRate >= v.motionEnd - 0.01f;
    }

    bool takeoffNext(const View& v)
    {
        return v.status == game::kStatusJumpSquat && animationEndsNext(v);
    }

    bool jumpLeft(Fighter* fighter)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return false;
        }
        soModuleAccesser* accesser =
            *reinterpret_cast<soModuleAccesser**>(reinterpret_cast<u32>(fighter) + kAccesser);
        if (!game::isRam(accesser))
        {
            return false;
        }
        return m->m_workManageModule->getInt(kJumpCount)
               < soValueAccesser::getConstantInt(accesser, kJumpCountMax, 0);
    }

    bool techWindowActive(Fighter* fighter)
    {
        // soControllerImpl vtable (fighter_input.cpp:44) and guard trigger
        // index 4 (+96 prev, +97 count; Fighter.pac common.md:798-799).
        enum { kControllerVtable = 0x80AE4EF0, kGuard4Prev = 88 + 2 * 4, kGuard4Count = 89 + 2 * 4 };
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_controllerModule))
        {
            return false;
        }
        soController* c = m->m_controllerModule->getController();
        if (!game::isRam(c) || game::wordAt(c, 0) != kControllerVtable)
        {
            return false;
        }
        const u8* base = reinterpret_cast<const u8*>(c);
        return base[kGuard4Prev] >= 40 && base[kGuard4Count] < 20;
    }

    void spoofWallTechTrigger(Fighter* fighter)
    {
        enum {
            kControllerVtable = 0x80AE4EF0,
            kGuard3Prev = 88 + 2 * 3,
            kGuard3Count = 89 + 2 * 3,
            kGuard4Prev = 88 + 2 * 4,
            kGuard4Count = 89 + 2 * 4
        };
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_controllerModule))
        {
            return;
        }
        soController* c = m->m_controllerModule->getController();
        if (!game::isRam(c) || game::wordAt(c, 0) != kControllerVtable)
        {
            return;
        }
        u8* base = reinterpret_cast<u8*>(c);
        base[kGuard3Prev] = 40;
        base[kGuard3Count] = 0;
        base[kGuard4Prev] = 40;
        base[kGuard4Count] = 0;
    }

    static float* workFloat(Fighter* fighter, int index)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return NULL;
        }
        const void* work = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, kWorkInstance));
        if (!game::isRam(work))
        {
            return NULL;
        }
        float* floats = reinterpret_cast<float*>(game::wordAt(work, kWorkFloats));
        if (!game::isRam(floats) || index < 0 || static_cast<u32>(index) >= game::wordAt(work, kWorkFloatCount))
        {
            return NULL;
        }
        return &floats[index];
    }

    static float* shieldSlot(Fighter* fighter)
    {
        return workFloat(fighter, kShieldFloat);
    }

    bool shieldHealth(Fighter* fighter, float& out)
    {
        float* p = shieldSlot(fighter);
        if (p == NULL)
        {
            return false;
        }
        out = *p;
        return true;
    }

    void setShieldHealth(Fighter* fighter, float value)
    {
        float* p = shieldSlot(fighter);
        if (p != NULL)
        {
            *p = value;
        }
    }

    void setIntangible(Fighter* fighter, int frames)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m != NULL && game::isRam(m->m_collisionHitModule))
        {
            m->m_collisionHitModule->setXluFrameGlobal(frames, 0);
        }
    }

    void setNoReaction(Fighter* fighter, bool on)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_damageModule) || !game::isRam(m->m_collisionHitModule))
        {
            return;
        }
        // Armour against all but an absolute hit (soDamageModuleImpl::checkNoReaction);
        // the damage lock adds no percent.
        m->m_damageModule->setNoReactionModeAlways(on);
        m->m_damageModule->setDamageLock(on);
        m->m_collisionHitModule->setCheckCatch(!on, 0);
    }

    // The damage energy's speed (x, y), or NULL if it is not readable.
    static float* knockbackSpeed(Fighter* fighter)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_kineticModule))
        {
            return NULL;
        }
        const void* energies = reinterpret_cast<const void*>(game::wordAt(m->m_kineticModule, kKineticEnergies));
        if (!game::isRam(energies))
        {
            return NULL;
        }
        void* energy = reinterpret_cast<void*>(game::wordAt(energies, kDamageEnergy));
        if (!game::isRam(energy))
        {
            return NULL;
        }
        return reinterpret_cast<float*>(reinterpret_cast<u32>(energy) + kEnergySpeed);
    }

    bool knockback(Fighter* fighter, float& x, float& y)
    {
        const float* speed = knockbackSpeed(fighter);
        if (speed == NULL)
        {
            return false;
        }
        x = speed[0];
        y = speed[1];
        return true;
    }

    void clearKnockback(Fighter* fighter)
    {
        float* speed = knockbackSpeed(fighter);
        if (speed != NULL)
        {
            speed[0] = speed[1] = 0.0f;
        }
    }

    bool fallSpeeds(Fighter* fighter, float& accel, float& maxFall, float& speedY)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_kineticModule) || game::wordAt(m->m_kineticModule, kGravityId) != 1)
        {
            return false;
        }
        const void* energy = reinterpret_cast<const void*>(game::wordAt(m->m_kineticModule, kGravityEnergy));
        if (!game::isRam(energy))
        {
            return false;
        }
        speedY = floatAt(energy, kEnergySpeed + 4);
        accel = floatAt(energy, kGravityAccel);
        maxFall = floatAt(energy, kGravityAccel + 4);
        return accel < 0.0f && accel > -2.0f && maxFall > 0.0f && maxFall < 20.0f;
    }

    bool setWorkFloat(Fighter* fighter, int index, float value)
    {
        float* p = workFloat(fighter, index);
        if (p == NULL)
        {
            return false;
        }
        *p = value;
        return true;
    }

    // Floats 36-37 and 40-41, int 77 (P+'s ButtonPresses.asm):
    // docs/internals.md, "Analog trigger variables".
    void hideAnalogTrigger(Fighter* fighter, bool right)
    {
        enum { kAnalogL = 36, kAnalogBefore = 4, kAnalogState = 77, kWorkInts = 0xC, kWorkIntCount = 0x10 };
        float* now = workFloat(fighter, kAnalogL);
        if (now == NULL || workFloat(fighter, kAnalogL + kAnalogBefore + 1) == NULL)
        {
            return;
        }
        const void* work = reinterpret_cast<const void*>(
            game::wordAt(game::fighterModules(fighter)->m_workManageModule, kWorkInstance));
        s32* ints = reinterpret_cast<s32*>(game::wordAt(work, kWorkInts));
        if (!game::isRam(ints) || game::wordAt(work, kWorkIntCount) <= kAnalogState)
        {
            return;
        }
        now[right] = 0.0f;
        now[kAnalogBefore + right] = 0.0f;
        const float kPressed = 0.3f;
        ints[kAnalogState] = now[!right] < kPressed ? 0 : (now[kAnalogBefore + !right] < kPressed ? 1 : 2);
    }

    void setMotionRate(Fighter* fighter, float rate)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m != NULL && game::isRam(m->m_motionModule))
        {
            m->m_motionModule->setRate(rate);
        }
    }

    void placeAt(Fighter* fighter, float x, float y)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_kineticModule) || !game::isRam(m->m_postureModule))
        {
            return;
        }
        m->m_kineticModule->clearSpeedAll();
        float* pos = reinterpret_cast<float*>(reinterpret_cast<u32>(m->m_postureModule) + kPosturePos);
        float* prev = reinterpret_cast<float*>(reinterpret_cast<u32>(m->m_postureModule) + kPosturePrevPos);
        pos[0] = prev[0] = x;
        pos[1] = prev[1] = y;
    }

    void requestStatus(Fighter* fighter, int status)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m != NULL && game::isRam(m->m_statusModule) && game::isRam(fighter))
        {
            soModuleAccesser* accesser =
                *reinterpret_cast<soModuleAccesser**>(reinterpret_cast<u32>(fighter) + kAccesser);
            if (game::isRam(accesser))
            {
                m->m_statusModule->changeStatusRequest(status, accesser);
            }
        }
    }

    bool groundBetween(Fighter* fighter, float x0, float y0, float dx, float dy, float& hitY)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_groundModule))
        {
            return false;
        }
        Vec3f from(x0, y0, 0.0f);
        Vec3f dir(dx, dy, 0.0f);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Vec3f normal(0.0f, 0.0f, 0.0f);
        if (!m->m_groundModule->rayCheck(&from, &dir, &hit, &normal, true))
        {
            return false;
        }
        hitY = hit.m_y;
        return true;
    }

    bool wallOrCeilBetween(float x, float y, float dx, float dy)
    {
        // stRayCheck (b1=false skips passable platforms, b2=false front-facing
        // only, mask 1 = fighter).
        Vec3f start(x, y, 0.0f);
        Vec3f dir(dx, dy, 0.0f);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Vec3f normal(0.0f, 0.0f, 0.0f);
        if (!stRayCheck(&start, &dir, &hit, &normal, false, NULL, false, 1))
        {
            return false;
        }
        return normal.m_y < 0.5f;
    }

    int attackParts(Fighter* fighter, float* x, float* y, float* size, int max)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_collisionAttackModule))
        {
            return 0;
        }
        soCollisionAttackModule* attack = m->m_collisionAttackModule;
        int n = static_cast<int>(attack->getPartSize());
        n = n < 0 ? 0 : (n > max ? max : n);
        for (int i = 0; i < n; i++)
        {
            size[i] = 0.0f;
            if (!attack->isAttack(i, false))
            {
                continue;
            }
            Vec3f c = attack->getCenterPos(i, false);
            x[i] = c.m_x;
            y[i] = c.m_y;
            size[i] = attack->getSize(i);
        }
        return n;
    }

    int playSound(Fighter* fighter, int soundId)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_soundModule))
        {
            return -1;
        }
        return m->m_soundModule->playSE(static_cast<SndID>(soundId), false, 0, 0);
    }

    namespace {
        enum {
            kReflectBit = 0x2200000A,        // RA-Bit[10]: set on powershield reflect (PowerShield.asm)
            kShapeOf = 0x80740A3C,           // soCollision::getShape(coll, id, group)
            kAttackRenderDebug = 0x8074BDB0, // soCollisionAttackModuleImpl::renderDebug
            kSlotRenderDebug = 0x120,
            kShieldModuleVtable = 0x80AE31C0, // soCollisionShieldModuleImpl (Null is 0x80AEA730)
            kHitModuleVtable = 0x80AE2B60,    // soCollisionHitModuleImpl (Null is 0x80AEA808)
            kStageObjVptr = 0x3C,             // gfTask vptr (sizeof(gfTask) == 0x40)
            kSlotProcessAnim = 0x10,
            kStageObjProcessAnim = 0x8070F444, // StageObject::processAnim
            kCapsuleVtable = 0x80ADEEE8,     // clCapsule (unswept capsule hitbox)
            kAttackParts = 0x30,
            kAttackCollision = 0x68,
            kAttackFlags = 0x9C,
            kAttackAsleep = 0x40000000,
            kPartActive = 2,
            kPartTargetCat = 0x34,
            kTargetFighter = 0x00002000,     // m_targetCategoryFighter
            kTargetGround = 0x00000400,      // m_targetSituationGround
            kPartAttr = 0x38,
            kShieldable = 0x00200000,        // m_isShieldable (0x807431C4)
            kReflectable = 0x00100000,       // m_isReflectable (0x80743214)
            kPartFlags2 = 0x3C,
            kIsCatch = 0x00000200,           // m_isCatch
            kPartNoTeam = 0x00000100,        // m_noTeam (attack-part friendly fire)
            kPartGroup = 0x88,
            kShapeRadius = 0x0C,
            kShapeActive = 0x24,
            kShapeVtable = 0x30,
            kShapeBefore = 0x34,
            kCapsuleScale = 0x70,            // clCapsule bone scale (clCapsule vs clSphere 0x8070BF78)
            kShieldParts = 0x20,             // soCollisionShieldModuleImpl::m_partArray (Reflect.asm:35)
            kShieldCollision = 0x28,         // soCollisionShieldModuleImpl::m_collision
            kHitParts = 0x30,                // soCollisionHitModuleImpl::m_partArray (Reflect.asm:75)
            kHitCollision = 0x38,            // soCollisionHitModuleImpl::m_collision
            kMaxHitParts = 24,
            kCollTeam = 0x04,                // soTeam* (so_collision.h lists int; see soCollision::check 0x80741A50)
            kCollGroups = 0x08,              // soArray<soCollisionGroup>* (soCollision::getShape 0x80740A44)
            kCollTaskId = 0x0C,
            kCollNoTeam = 0x13,
            kTeamGetNo = 0x10,               // soTeam::getNo()
            kTeamGet2nd = 0x20,              // soTeam::get2nd()
            kTeamNullVtable = 0x80AE8DE8,    // soTeamNull
            kTeamImplVtable = 0x80B25408,    // soTeamImpl (sora_melee)
            kFtTeamVtable = 0x80B09A90,      // ftTeam
            kFtTeamIndVtable = 0x80B09A30,   // ftTeamIndirect
            kLwzR3_8 = 0x80630008,           // lwz r3, 8(r3)  (soTeamImpl::getNo in fighter RELs)
            kLwzR3_12 = 0x8063000C,          // lwz r3, 12(r3) (soTeamImpl::get2nd in fighter RELs)
            kBlr = 0x4E800020,               // blr
            kShieldPartRadius = 0x18,        // soCollisionShieldPart::m_radius (0x8075125C)
            kShieldPartFlags = 0x1C,         // bits 31..23: boneId; bit 22: bone-scaled (0x80751260-64)
            kParamShieldSize = 3046,         // ftStatusUniqProcessGuardFunc::setShieldSize (0x80874C34)
            kParamShieldMax = 3233,
            kParamShieldMinScale = 3234,
            kParamShieldMult = 3247,
            kArrayAt = 0x0C,
            kArraySize = 0x14,
            kCategoryLists = 0x58,           // gfTaskScheduler category list heads (0x8002EFE8)
            kTaskNext = 0x10,
            kTaskFlags = 0x2C,
            kTaskAlive = 0x40000000,         // m_alive, cleared in gfTask::exit (0x8002DF74)
            kMaxAttackParts = 32,
            kMaxWeaponTasks = 96,
            kMaxItemUnits = 64
        };

        const float kFallbackShieldY = 8.0f;
        const float kFallbackShieldR = 12.0f;
        // P+'s powershield reflect bubble is 0.76x the shield radius (Reflect.asm:53-59).
        const float kReflectScale = 0.76f;

        typedef const void* (*ShapeOfFn)(const void* collision, int id, u32 group);
        typedef int (*ArraySizeFn)(const void* array);
        typedef const void* (*ArrayAtFn)(const void* array, int index);

        int safeArraySize(const void* arr, const void* calls)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArraySize));
            return game::isRam(fn) ? reinterpret_cast<ArraySizeFn>(fn)(arr) : 0;
        }

        const void* safeArrayAt(const void* arr, const void* calls, int index)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArrayAt));
            return game::isRam(fn) ? reinterpret_cast<ArrayAtFn>(fn)(arr, index) : NULL;
        }

        // soCollision::getShape (0x80740A3C) indexes coll+8 via atSub (0x807575AC)
        // with no bounds check on `group`; guard the group array and index first.
        const void* safeShapeOf(const void* coll, int id, u32 group)
        {
            if (!game::isRam(coll))
            {
                return NULL;
            }
            const void* groups = reinterpret_cast<const void*>(game::wordAt(coll, kCollGroups));
            const void* gVt = game::isRam(groups) ? reinterpret_cast<const void*>(game::wordAt(groups, 0)) : NULL;
            if (!game::isRam(gVt))
            {
                return NULL;
            }
            const void* atFn = reinterpret_cast<const void*>(game::wordAt(gVt, kArrayAt));
            int gCount = game::isRam(atFn) ? safeArraySize(groups, gVt) : 0;
            if (gCount <= 0 || group >= static_cast<u32>(gCount))
            {
                return NULL;
            }
            return reinterpret_cast<ShapeOfFn>(kShapeOf)(coll, id, group);
        }

        // Proves a gfTask is a StageObject before reading +0x60: gfTask's vptr
        // lies at +0x3C (within sizeof(gfTask) == 0x40), and StageObject / Weapon
        // / BaseItem vtables keep StageObject::processAnim at +0x10.
        const soModuleEnumeration* modulesOf(const void* stageObject)
        {
            if (!game::isRam(stageObject))
            {
                return NULL;
            }
            const void* vt = reinterpret_cast<const void*>(game::wordAt(stageObject, kStageObjVptr));
            if (!game::isRam(vt) || game::wordAt(vt, kSlotProcessAnim) != kStageObjProcessAnim)
            {
                return NULL;
            }
            const soModuleAccesser* accesser =
                reinterpret_cast<const soModuleAccesser*>(game::wordAt(stageObject, kAccesser));
            if (!game::isRam(accesser) || !game::isRam(accesser->m_enumerationStart))
            {
                return NULL;
            }
            return accesser->m_enumerationStart;
        }

        // Reads getNo() and get2nd() directly from known soTeam layouts without
        // indirect calls. Fighter RELs emit local COMDAT copies of soTeamImpl's
        // vtable, identified by their two-instruction getNo/get2nd bodies.
        bool readTeamIds(const void* team, int& no, int& second)
        {
            if (!game::isRam(team))
            {
                return false;
            }
            u32 vt = game::wordAt(team, 0);
            if (vt == kTeamNullVtable)
            {
                no = -1;
                second = -1;
                return true;
            }
            if (vt == kTeamImplVtable)
            {
                no = static_cast<int>(game::wordAt(team, 8));
                second = static_cast<int>(game::wordAt(team, 12));
                return true;
            }
            if (vt == kFtTeamVtable)
            {
                no = static_cast<int>(game::wordAt(team, 12));
                second = static_cast<int>(game::wordAt(team, 16));
                return true;
            }
            if (vt == kFtTeamIndVtable)
            {
                int ov = static_cast<int>(game::wordAt(team, 20));
                no = ov != -1 ? ov : static_cast<int>(game::wordAt(team, 12));
                second = static_cast<int>(game::wordAt(team, 16));
                return true;
            }
            const void* vtPtr = reinterpret_cast<const void*>(vt);
            if (!game::isRam(vtPtr))
            {
                return false;
            }
            const void* fnNo = reinterpret_cast<const void*>(game::wordAt(vtPtr, kTeamGetNo));
            const void* fn2nd = reinterpret_cast<const void*>(game::wordAt(vtPtr, kTeamGet2nd));
            if (game::isRam(fnNo) && game::isRam(fn2nd) && game::wordAt(fnNo, 0) == kLwzR3_8
                && game::wordAt(fnNo, 4) == kBlr && game::wordAt(fn2nd, 0) == kLwzR3_12
                && game::wordAt(fn2nd, 4) == kBlr)
            {
                no = static_cast<int>(game::wordAt(team, 8));
                second = static_cast<int>(game::wordAt(team, 12));
                return true;
            }
            return false;
        }

        // Mirrors soCollision::check (0x80741A2C-0x80741B00): same task never
        // hits; friendly fire (noTeam) always can; matching getNo() or get2nd()
        // team id blocks teammates and own projectiles, while a reflected
        // projectile gets the reflector's team (Weapon::reflectSettingTeam 0x808E5890).
        bool canHit(const void* atkColl, const void* defColl, bool partNoTeam)
        {
            if (game::wordAt(atkColl, kCollTaskId) == game::wordAt(defColl, kCollTaskId))
            {
                return false;
            }
            if (partNoTeam || game::byteAt(atkColl, kCollNoTeam) != 0 || game::byteAt(defColl, kCollNoTeam) != 0)
            {
                return true;
            }
            // soCollision +0x04 holds a soTeam* (BrawlHeaders lists m_indirectTeam as int).
            const void* atkTeam = reinterpret_cast<const void*>(game::wordAt(atkColl, kCollTeam));
            const void* defTeam = reinterpret_cast<const void*>(game::wordAt(defColl, kCollTeam));
            int a1 = -1, a2 = -1, d1 = -1, d2 = -1;
            if (readTeamIds(atkTeam, a1, a2) && readTeamIds(defTeam, d1, d2))
            {
                if (a1 != -1 && a1 == d1)
                {
                    return false;
                }
                if (a2 != -1 && a2 == d2)
                {
                    return false;
                }
            }
            return true;
        }

        bool segHitsCircle(float x0, float y0, float x1, float y1, float cx, float cy, float r)
        {
            float dx = x1 - x0;
            float dy = y1 - y0;
            float fx = x0 - cx;
            float fy = y0 - cy;
            float len2 = dx * dx + dy * dy;
            float t = 0.0f;
            if (len2 > 1e-6f)
            {
                t = -(fx * dx + fy * dy) / len2;
                if (t < 0.0f)
                {
                    t = 0.0f;
                }
                else if (t > 1.0f)
                {
                    t = 1.0f;
                }
            }
            float px = x0 + t * dx - cx;
            float py = y0 + t * dy - cy;
            return px * px + py * py <= r * r;
        }

        float cross2d(float ax, float ay, float bx, float by, float cx, float cy)
        {
            return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        }

        bool segHitsSeg(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1, float by1,
                        float r)
        {
            float c1 = cross2d(ax0, ay0, ax1, ay1, bx0, by0);
            float c2 = cross2d(ax0, ay0, ax1, ay1, bx1, by1);
            float c3 = cross2d(bx0, by0, bx1, by1, ax0, ay0);
            float c4 = cross2d(bx0, by0, bx1, by1, ax1, ay1);
            if (((c1 > 0.0f && c2 < 0.0f) || (c1 < 0.0f && c2 > 0.0f))
                && ((c3 > 0.0f && c4 < 0.0f) || (c3 < 0.0f && c4 > 0.0f)))
            {
                return true;
            }
            return segHitsCircle(ax0, ay0, ax1, ay1, bx0, by0, r) || segHitsCircle(ax0, ay0, ax1, ay1, bx1, by1, r)
                   || segHitsCircle(bx0, by0, bx1, by1, ax0, ay0, r) || segHitsCircle(bx0, by0, bx1, by1, ax1, ay1, r);
        }

        // True if segment [ax0,ay0]->[ax1,ay1] (radius `ar`) reaches any live
        // hurtbox of `selfMod` (guards characters whose standing hurtbox extends
        // past the 0.76x reflect bubble, e.g. Bowser or Yoshi's snout).
        bool segHitsHurtboxes(const soModuleEnumeration* selfMod, float ax0, float ay0, float ax1, float ay1, float ar)
        {
            if (selfMod == NULL || !game::isRam(selfMod->m_collisionHitModule)
                || game::wordAt(selfMod->m_collisionHitModule, 0) != kHitModuleVtable)
            {
                return false;
            }
            const void* hitMod = selfMod->m_collisionHitModule;
            const void* parts = reinterpret_cast<const void*>(game::wordAt(hitMod, kHitParts));
            const void* calls = game::isRam(parts) ? reinterpret_cast<const void*>(game::wordAt(parts, 0)) : NULL;
            if (!game::isRam(calls))
            {
                return false;
            }
            int count = safeArraySize(parts, calls);
            const void* hitColl = reinterpret_cast<const void*>(reinterpret_cast<u32>(hitMod) + kHitCollision);
            for (int h = 0; h < count && h < kMaxHitParts; h++)
            {
                const void* hShape = safeShapeOf(hitColl, h, 0);
                if (!game::isRam(hShape) || game::byteAt(hShape, kShapeActive) != 1)
                {
                    continue;
                }
                float hr = floatAt(hShape, kShapeRadius);
                if (!(hr > 0.0f))
                {
                    continue;
                }
                float hx0 = floatAt(hShape, 0);
                float hy0 = floatAt(hShape, 4);
                if (game::wordAt(hShape, kShapeVtable) == kCapsuleVtable)
                {
                    float sc = floatAt(hShape, kCapsuleScale);
                    if (sc > 0.0f && sc < 10.0f)
                    {
                        hr *= sc;
                    }
                    float hx1 = floatAt(hShape, kShapeBefore);
                    float hy1 = floatAt(hShape, kShapeBefore + 4);
                    if (segHitsSeg(ax0, ay0, ax1, ay1, hx0, hy0, hx1, hy1, ar + hr))
                    {
                        return true;
                    }
                }
                else if (segHitsCircle(ax0, ay0, ax1, ay1, hx0, hy0, ar + hr))
                {
                    return true;
                }
            }
            return false;
        }

        // Reads the fighter's shield radius from its shield module part 0
        // (soCollisionShieldPart::update 0x807511DC, soCollisionShapeUpdate
        // 0x80760288, and ftStatusUniqProcessGuardFunc::setShieldSize 0x80874C34)
        // and its centre from the fighter's ECB (soGroundModule::getCenterPos),
        // falling back to (x, y + 8, r = 12) when unreadable.
        void readShieldBubble(Fighter* fighter, const View& v, ShieldBubble& out)
        {
            out.x = v.x;
            out.y = v.y + kFallbackShieldY;
            out.radius = kFallbackShieldR;
            out.fallback = true;

            const soModuleEnumeration* m = game::fighterModules(fighter);
            if (m == NULL || !game::isRam(m->m_collisionShieldModule)
                || game::wordAt(m->m_collisionShieldModule, 0) != kShieldModuleVtable
                || !game::isRam(m->m_postureModule) || !game::isRam(m->m_groundModule))
            {
                return;
            }
            const void* parts =
                reinterpret_cast<const void*>(game::wordAt(m->m_collisionShieldModule, kShieldParts));
            const void* calls = game::isRam(parts) ? reinterpret_cast<const void*>(game::wordAt(parts, 0)) : NULL;
            if (!game::isRam(calls) || safeArraySize(parts, calls) <= 0)
            {
                return;
            }
            const void* part = safeArrayAt(parts, calls, 0);
            if (!game::isRam(part))
            {
                return;
            }
            float baseRadius = floatAt(part, kShieldPartRadius);
            u32 flags = game::wordAt(part, kShieldPartFlags);
            bool boneScaled = ((flags >> 22) & 1) != 0;
            if (!(baseRadius > 0.0f && baseRadius < 100.0f))
            {
                return;
            }
            float postureScale = m->m_postureModule->getScale();
            if (!(postureScale > 0.0f && postureScale < 10.0f))
            {
                return;
            }
            float radius = baseRadius * postureScale;
            if (boneScaled)
            {
                soModuleAccesser* accesser =
                    *reinterpret_cast<soModuleAccesser**>(reinterpret_cast<u32>(fighter) + kAccesser);
                float hp = 0.0f;
                if (!game::isRam(accesser) || !shieldHealth(fighter, hp))
                {
                    return;
                }
                float maxHp = soValueAccesser::getConstantFloat(accesser, kParamShieldMax, 0);
                float minScale = soValueAccesser::getConstantFloat(accesser, kParamShieldMinScale, 0);
                float mult = soValueAccesser::getConstantFloat(accesser, kParamShieldMult, 0);
                float shieldSize = soValueAccesser::getConstantFloat(accesser, kParamShieldSize, 0);
                if (!(maxHp > 0.0f && shieldSize > 0.0f && shieldSize < 100.0f))
                {
                    return;
                }
                if (hp < 0.0f)
                {
                    hp = 0.0f;
                }
                else if (hp > maxHp)
                {
                    hp = maxHp;
                }
                float boneScale = (minScale + (hp / maxHp) * mult * (1.0f - minScale)) * shieldSize;
                radius = baseRadius * boneScale * postureScale;
            }
            if (!(radius >= 2.0f && radius <= 40.0f))
            {
                return;
            }

            Vec2f c = m->m_groundModule->getCenterPos(0);
            float dx = c.m_x - v.x;
            float dy = c.m_y - v.y;
            if (!(dx >= -20.0f && dx <= 20.0f && dy >= 1.0f && dy <= 40.0f))
            {
                return;
            }
            out.x = c.m_x;
            out.y = c.m_y;
            out.radius = radius;
            out.fallback = false;
        }

        void readSpeed(const soModuleEnumeration* m, bool useKinetic, bool& haveSpeed, float& vx, float& vy)
        {
            if (haveSpeed)
            {
                return;
            }
            haveSpeed = true;
            if (useKinetic && game::isRam(m->m_kineticModule))
            {
                Vec2f spd = m->m_kineticModule->getSumSpeed(
                    soKineticEnergy::AttributeFlag(soKineticEnergy::ATTRIBUTE_MASK_ALL));
                vx = spd.m_x;
                vy = spd.m_y;
            }
            else if (game::isRam(m->m_postureModule))
            {
                vx = floatAt(m->m_postureModule, kPosturePos) - floatAt(m->m_postureModule, kPosturePrevPos);
                vy = floatAt(m->m_postureModule, kPosturePos + 4) - floatAt(m->m_postureModule, kPosturePrevPos + 4);
            }
        }

        bool segThreatens(const soModuleEnumeration* selfMod, const ShieldBubble& bubble, bool hurtFallback, float x0,
                          float y0, float x1, float y1, float hitR, float r)
        {
            if (segHitsCircle(x0, y0, x1, y1, bubble.x, bubble.y, hitR))
            {
                return true;
            }
            return hurtFallback && segHitsCircle(x0, y0, x1, y1, bubble.x, bubble.y, bubble.radius + 20.0f + r)
                   && segHitsHurtboxes(selfMod, x0, y0, x1, y1, r);
        }

        bool objectThreatens(const soModuleEnumeration* m, const soModuleEnumeration* selfMod, const void* defColl,
                             const ShieldBubble& bubble, bool useKinetic)
        {
            if (m == NULL || !game::isRam(m->m_collisionAttackModule))
            {
                return false;
            }
            const void* attack = m->m_collisionAttackModule;
            const void* vtable = reinterpret_cast<const void*>(game::wordAt(attack, 0));
            if (!game::isRam(vtable) || game::wordAt(vtable, kSlotRenderDebug) != kAttackRenderDebug
                || (game::wordAt(attack, kAttackFlags) & kAttackAsleep) != 0)
            {
                return false;
            }
            const void* parts = reinterpret_cast<const void*>(game::wordAt(attack, kAttackParts));
            const void* calls = game::isRam(parts) ? reinterpret_cast<const void*>(game::wordAt(parts, 0)) : NULL;
            if (!game::isRam(calls))
            {
                return false;
            }
            int count = safeArraySize(parts, calls);
            if (count <= 0)
            {
                return false;
            }
            const void* atkColl = reinterpret_cast<const void*>(reinterpret_cast<u32>(attack) + kAttackCollision);
            bool haveSpeed = false;
            float objVx = 0.0f, objVy = 0.0f;

            for (int id = 0; id < count && id < kMaxAttackParts; id++)
            {
                const void* part = safeArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kPartActive)
                {
                    continue;
                }
                u32 target = game::wordAt(part, kPartTargetCat);
                if ((target & kTargetFighter) == 0 || (target & kTargetGround) == 0)
                {
                    continue;
                }
                u32 attr = game::wordAt(part, kPartAttr);
                bool shieldable = (attr & kShieldable) != 0;
                bool reflectable = (attr & kReflectable) != 0;
                if (!shieldable && !reflectable)
                {
                    continue;
                }
                u32 flags2 = game::wordAt(part, kPartFlags2);
                if ((flags2 & kIsCatch) != 0)
                {
                    continue;
                }
                if (defColl != NULL && !canHit(atkColl, defColl, (flags2 & kPartNoTeam) != 0))
                {
                    continue;
                }
                const void* shape = safeShapeOf(atkColl, id, game::wordAt(part, kPartGroup));
                if (!game::isRam(shape) || game::byteAt(shape, kShapeActive) != 1)
                {
                    continue;
                }
                float r = floatAt(shape, kShapeRadius);
                if (!(r > 0.0f))
                {
                    continue;
                }
                bool capsule = game::wordAt(shape, kShapeVtable) == kCapsuleVtable;
                if (capsule)
                {
                    float sc = floatAt(shape, kCapsuleScale);
                    if (sc > 0.0f && sc < 10.0f)
                    {
                        r *= sc;
                    }
                }
                float nowX = floatAt(shape, 0);
                float nowY = floatAt(shape, 4);
                float befX = floatAt(shape, kShapeBefore);
                float befY = floatAt(shape, kShapeBefore + 4);
                float testR = (useKinetic && reflectable) ? (bubble.radius * kReflectScale) : bubble.radius;
                float hitR = testR + r;
                bool hurtFallback = useKinetic && reflectable;

                if (capsule)
                {
                    readSpeed(m, useKinetic, haveSpeed, objVx, objVy);
                    if (segThreatens(selfMod, bubble, hurtFallback, nowX, nowY, befX, befY, hitR, r)
                        || segThreatens(selfMod, bubble, hurtFallback, nowX + objVx, nowY + objVy, befX + objVx,
                                        befY + objVy, hitR, r))
                    {
                        return true;
                    }
                    continue;
                }

                float vx = nowX - befX;
                float vy = nowY - befY;
                if (useKinetic)
                {
                    readSpeed(m, true, haveSpeed, objVx, objVy);
                    if (objVx * objVx + objVy * objVy > 1e-6f)
                    {
                        vx = objVx;
                        vy = objVy;
                    }
                }
                if (segThreatens(selfMod, bubble, hurtFallback, nowX, nowY, nowX + vx, nowY + vy, hitR, r))
                {
                    return true;
                }
            }
            return false;
        }
    }

    bool justReflected(Fighter* fighter)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return false;
        }
        int st = game::fighterStatus(m);
        if (st != game::kStatusGuardOn && st != game::kStatusGuard)
        {
            return false;
        }
        return m->m_workManageModule->isFlag(kReflectBit);
    }

    int incomingHit(Fighter* self, int selfEntry, const View& v, ShieldBubble& bubble)
    {
        readShieldBubble(self, v, bubble);
        const soModuleEnumeration* selfMod = game::fighterModules(self);
        const void* defColl = NULL;
        if (selfMod != NULL)
        {
            if (game::isRam(selfMod->m_collisionShieldModule)
                && game::wordAt(selfMod->m_collisionShieldModule, 0) == kShieldModuleVtable)
            {
                defColl = reinterpret_cast<const void*>(
                    reinterpret_cast<u32>(selfMod->m_collisionShieldModule) + kShieldCollision);
            }
            else if (game::isRam(selfMod->m_collisionHitModule)
                     && game::wordAt(selfMod->m_collisionHitModule, 0) == kHitModuleVtable)
            {
                defColl = reinterpret_cast<const void*>(
                    reinterpret_cast<u32>(selfMod->m_collisionHitModule) + kHitCollision);
            }
        }

        // Other fighters and their partners (Nana).
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].entryId == selfEntry)
            {
                continue;
            }
            if (objectThreatens(game::fighterModules(refs[i].fighter), selfMod, defColl, bubble, false)
                || (refs[i].partner != NULL
                    && objectThreatens(game::fighterModules(refs[i].partner), selfMod, defColl, bubble, false)))
            {
                return kHitFighter;
            }
        }

        // Live weapons in gfTaskScheduler's Category_Weapon list (0x8002EFE8).
        const void* scheduler = g_taskScheduler;
        if (game::isRam(scheduler))
        {
            const void* task = reinterpret_cast<const void*>(
                game::wordAt(scheduler, kCategoryLists + 4 * gfTask::Category_Weapon));
            for (int w = 0; w < kMaxWeaponTasks && game::isRam(task); w++)
            {
                if ((game::wordAt(task, kTaskFlags) & kTaskAlive) != 0
                    && objectThreatens(modulesOf(task), selfMod, defColl, bubble, true))
                {
                    return kHitWeapon;
                }
                task = reinterpret_cast<const void*>(game::wordAt(task, kTaskNext));
            }
        }

        // Live items in itManager::m_itemArrayList (skips itManager itself and recycled items).
        itManager* items = itManager::getInstance();
        if (game::isRam(items))
        {
            soArray<BaseItem*>& list = items->m_itemArrayList;
            int count = list.size();
            for (int i = 0; i < count && i < kMaxItemUnits; i++)
            {
                BaseItem* item = list.at(i);
                if (game::isRam(item) && (game::wordAt(item, kTaskFlags) & kTaskAlive) != 0
                    && objectThreatens(modulesOf(item), selfMod, defColl, bubble, true))
                {
                    return kHitItem;
                }
            }
        }
        return kHitNone;
    }
}
}
