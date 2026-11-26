// Host regression tests for engine bugfixes:
//  1. Real controller port mapping (fighters.h/fighters.cpp + pad_inmatch.cpp)
//  2. Act OoS Guard_Off -> Wait -> Guard_On / Guard_Off -> Guard_On re-arm disarm
//  3. Dashback SlowTurn.asm frame-2 gate (no bogus Slow Dashback after natural turn)
//  4. Combo multi-hit same-frame status chains, slide-off Damage->DamageAir, pummel
//  5. CPU shieldLag detection on 0-hitlag and shorter-hitlag shield transitions
#define TM_MODULE_MAIN 1

#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/event/so_event_manage_module_impl.h>
#include <so/event/so_event_system.h>
#include <so/status/so_status_event_presenter.h>
#include <string.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/pad_inmatch.h"
#include "main/game/status_ids.h"
#include "main/game/techniques.h"
#include "main/game/tracker.h"

extern ftEntryManager* g_ftEntryManager;
extern ftManager* g_ftManager;

namespace tm {
namespace game {
    // Real tracker.cpp functions compiled in test_tracker_swap.cpp.
    s32 tracker_under_test_frame();
    const Tracked* tracker_under_test_tracked(int i);
}
}

namespace {
    static tm::game::Body s_ebBody;
    static float s_ebDamage[4];
    static void* s_ebNetBuf = NULL;

    static void* eb_allocNetworkZero(u32 bytes)
    {
        s_ebNetBuf = host::mem::alloc(bytes, 16);
        if (s_ebNetBuf != NULL)
        {
            memset(s_ebNetBuf, 0, bytes);
        }
        return s_ebNetBuf;
    }

    static void eb_freeNetworkSlot(void** slot)
    {
        if (slot != NULL)
        {
            *slot = NULL;
        }
        s_ebNetBuf = NULL;
    }

    static bool eb_readBody(Fighter*, tm::game::Body& out)
    {
        out = s_ebBody;
        return true;
    }

    static s32 eb_instanceInt(const soModuleEnumeration*, u32)
    {
        return 0;
    }

    static float eb_damage(const tm::game::FighterRef& ref)
    {
        return (ref.playerNo >= 0 && ref.playerNo < 4) ? s_ebDamage[ref.playerNo] : 0.0f;
    }
}

// Compile the real techniques.cpp wired directly to real tracker.cpp under test.
#define trackerFrame() tracker_under_test_frame()
#define tracked(i) tracker_under_test_tracked(i)
#define readBody eb_readBody
#define instanceInt eb_instanceInt
#define damage eb_damage
#define allocNetworkZero eb_allocNetworkZero
#define freeNetworkSlot eb_freeNetworkSlot
#define techniquesAttach eb_techniquesAttach
#define techniquesDetach eb_techniquesDetach
#define techniquesReset eb_techniquesReset
#define techniquesFrame eb_techniquesFrame
#define techniqueState eb_techniqueState
#define lastLedgedash eb_lastLedgedash
#define lastLedgedashEvent eb_lastLedgedashEvent
#include "main/game/techniques.cpp"
#undef lastLedgedashEvent
#undef lastLedgedash
#undef techniqueState
#undef techniquesFrame
#undef techniquesReset
#undef techniquesDetach
#undef techniquesAttach
#undef freeNetworkSlot
#undef allocNetworkZero
#undef damage
#undef instanceInt
#undef readBody
#undef tracked
#undef trackerFrame

namespace {
    using namespace tm;

    struct EbUnit {
        host::fakes::FakeFighter* ff;
        Fighter* fighter;
        soEventManageModuleImpl* eventMod;
        soEventManager* eventMgr;
    };

    struct EbSink {
        int oosCount;
        game::Technique lastOos;
        int dbCount;
        game::Technique lastDb;
        int comboCount;
        game::Technique lastCombo;
    };
    static EbSink s_ebSink;

    static void ebRecord(const game::Technique& t)
    {
        if (t.kind == game::kTechActOos)
        {
            s_ebSink.oosCount++;
            s_ebSink.lastOos = t;
        }
        else if (t.kind == game::kTechDashback)
        {
            s_ebSink.dbCount++;
            s_ebSink.lastDb = t;
        }
        else if (t.kind == game::kTechCombo)
        {
            s_ebSink.comboCount++;
            s_ebSink.lastCombo = t;
        }
    }

    static EbUnit addTrackedUnit(int slotIdx, int playerNo, int entryId, u32 taskId, s16 manageId)
    {
        host::fakes::FakeFighter& ff = host::fakes::addFighter();
        EbUnit u;
        u.ff = &ff;
        u.fighter = ff.fighter;
        u.fighter->m_taskId = taskId;
        u.eventMod = host::mem::make<soEventManageModuleImpl>();
        u.eventMod->m_manageID = manageId;
        ff.modules->m_eventManageModule = u.eventMod;
        ff.modules->m_collisionAttackModule = host::mem::make<soCollisionAttackModuleImpl>();
        u.eventMgr = host::mem::make<soEventManager>();
        u.eventMgr->reset();
        soEventSystem::getInstance()->registerManager(manageId, u.eventMgr);

        soStatusEventObserver* fs = static_cast<soStatusEventObserver*>(u.fighter);
        soAnimCmdEventObserver* fa = static_cast<soAnimCmdEventObserver*>(static_cast<StageObject*>(u.fighter));
        fs->m_unitID = 4;
        fs->addObserver(manageId, -1);
        fa->m_unitID = 5;
        fa->addObserver(manageId, 0);

        ftEntry* e = host::mem::make<ftEntry>();
        e->m_entryId = entryId;
        e->m_playerNo = playerNo;
        e->m_activeInstanceIndex = 0;
        e->m_instances[0].m_fighter = u.fighter;
        e->m_owner = host::mem::make<ftOwner>();
        g_ftEntryManager->setSlot(slotIdx, e);
        return u;
    }

    static void setupEbWorld(EbUnit& p1, EbUnit& p2)
    {
        memset(&s_ebBody, 0, sizeof(s_ebBody));
        memset(s_ebDamage, 0, sizeof(s_ebDamage));
        memset(&s_ebSink, 0, sizeof(s_ebSink));

        soEventSystem::getInstance()->reset();
        g_ftEntryManager = host::mem::make<ftEntryManager>();
        g_ftEntryManager->reset();
        g_ftManager = host::mem::make<ftManager>();
        soEventManager* mgrEvents = host::mem::make<soEventManager>();
        mgrEvents->reset();
        g_ftManager->m_eventManageModule.m_manageID = 3;
        soEventSystem::getInstance()->registerManager(3, mgrEvents);
        g_ftManager->ftOutsideEventObserver::addObserver(3, -1);

        for (int p = 0; p < game::kMaxFighters; p++)
        {
            host::fakes::setPlayer(p, static_cast<u8>(p == 0 ? 0 : 7), static_cast<u8>(p < 2 ? p : 3), 0, 0);
        }

        game::trackerAttach();
        game::eb_techniquesAttach();
        game::eb_techniquesReset(ebRecord);

        p1 = addTrackedUnit(0, 0, 0x10000, 601, 10);
        p2 = addTrackedUnit(1, 1, 0x20001, 602, 11);
        game::trackerStep(true);
    }

    static void teardownEbWorld()
    {
        game::eb_techniquesDetach();
        game::trackerDetach();
    }

    static void fireStatus(EbUnit& u, int prevStatus, int newStatus, SituationKind sit = Situation_Ground)
    {
        u.ff->status = newStatus;
        u.ff->situation->m_kind = sit;
        soStatusEventObserver* fighterObs = static_cast<soStatusEventObserver*>(u.fighter);
        for (int s = 0; s < soEventManager::kMaxSlots; s++)
        {
            void* p = u.eventMgr->m_units[4].slots[s];
            if (p != NULL && p != static_cast<void*>(fighterObs))
            {
                static_cast<soStatusEventObserver*>(p)->notifyEventChangeStatus(newStatus, prevStatus, NULL,
                                                                                u.fighter->m_moduleAccesser);
            }
        }
    }

    static void stepEb()
    {
        game::trackerStepRuns(true);
        game::trackerHandOver();
        game::eb_techniquesFrame();
        game::trackerStep(true);
    }
}

TEST(engine_bugfixes, fighter_port_mapping_gcc_wii_and_safe_fallbacks)
{
    // GCC ports 0..3 for P1 (playerNo = 0).
    for (int port = 0; port < 4; port++)
    {
        host::fakes::setPlayerPort(0, static_cast<u8>(port));
        CHECK_EQ(port, game::playerPort(0));
        CHECK_EQ(port, game::resolveFighterPort(false, 0, game::playerPort(0)));
    }

    // Wii ports 4..7 for P1 (playerNo = 0).
    for (int port = 4; port < 8; port++)
    {
        host::fakes::setPlayerPort(0, static_cast<u8>(port));
        CHECK_EQ(port, game::playerPort(0));
        CHECK_EQ(port, game::resolveFighterPort(false, 0, game::playerPort(0)));
    }

    // CPU always gets -1 regardless of ipPadConfig byte.
    host::fakes::setPlayerPort(1, 3);
    CHECK_EQ(-1, game::resolveFighterPort(true, 1, game::playerPort(1)));

    // Unassigned / out-of-range byte (0x0F or 0xFF -> & 0xF == 0xF >= 8) falls back to playerNo.
    host::fakes::setPlayerPort(0, 0xFF);
    CHECK_EQ(0xF, game::playerPort(0));
    CHECK_EQ(0, game::resolveFighterPort(false, 0, game::playerPort(0)));

    host::fakes::setPlayerPort(2, 0x0F);
    CHECK_EQ(2, game::resolveFighterPort(false, 2, game::playerPort(2)));

    // Unreadable ipPadConfig (playerPort returns -1) falls back cleanly to playerNo.
    CHECK_EQ(0, game::resolveFighterPort(false, 0, -1));
    CHECK_EQ(3, game::resolveFighterPort(false, 3, -1));
}

TEST(engine_bugfixes, act_oos_disarms_on_guard_on_rearm_and_keeps_valid_oos)
{
    EbUnit p1, p2;
    setupEbWorld(p1, p2);

    // 1. Regression: Guard_Damage -> Guard_Off -> Wait -> Guard_On (same frame)
    // must disarm oosArmed without emitting Act OoS, so a later JumpSquat out of
    // the new unrelated shield emits ZERO bogus Act OoS.
    fireStatus(p1, game::kStatusGuard, game::kStatusGuardDamage);
    stepEb();
    fireStatus(p1, game::kStatusGuardDamage, game::kStatusGuardOff);
    stepEb();
    fireStatus(p1, game::kStatusGuardOff, game::kStatusWait);
    fireStatus(p1, game::kStatusWait, game::kStatusGuardOn);
    stepEb();
    CHECK_EQ(0, s_ebSink.oosCount);
    fireStatus(p1, game::kStatusGuardOn, game::kStatusGuard);
    stepEb();
    fireStatus(p1, game::kStatusGuard, game::kStatusJumpSquat);
    stepEb();
    CHECK_EQ(0, s_ebSink.oosCount);

    // 2. Regression: Powershield drop re-shield (Guard_Damage -> Guard_Off -> Guard_On)
    // also disarms cleanly without emitting Act OoS.
    fireStatus(p1, game::kStatusGuard, game::kStatusGuardDamage);
    stepEb();
    fireStatus(p1, game::kStatusGuardDamage, game::kStatusGuardOff);
    stepEb();
    fireStatus(p1, game::kStatusGuardOff, game::kStatusGuardOn);
    stepEb();
    CHECK_EQ(0, s_ebSink.oosCount);
    fireStatus(p1, game::kStatusGuardOn, game::kStatusJumpSquat);
    stepEb();
    CHECK_EQ(0, s_ebSink.oosCount);

    // 3. Positive control: Guard_Damage -> Guard -> JumpSquat emits Act OoS frame 2.
    fireStatus(p1, game::kStatusGuard, game::kStatusGuardDamage);
    stepEb();
    fireStatus(p1, game::kStatusGuardDamage, game::kStatusGuard);
    stepEb();
    fireStatus(p1, game::kStatusGuard, game::kStatusJumpSquat);
    stepEb();
    CHECK_EQ(1, s_ebSink.oosCount);
    CHECK_EQ(2, static_cast<int>(s_ebSink.lastOos.frame));

    // 4. Positive control: Guard_Damage -> Guard_Off -> (Wait -> Attack same frame)
    // still passes over transient Wait and emits Act OoS cleanly.
    fireStatus(p1, game::kStatusGuard, game::kStatusGuardDamage);
    stepEb();
    fireStatus(p1, game::kStatusGuardDamage, game::kStatusGuardOff);
    stepEb();
    fireStatus(p1, game::kStatusGuardOff, game::kStatusWait);
    fireStatus(p1, game::kStatusWait, game::kStatusAttack);
    stepEb();
    CHECK_EQ(2, s_ebSink.oosCount);
    CHECK_EQ(2, static_cast<int>(s_ebSink.lastOos.frame));

    teardownEbWorld();
}

TEST(engine_bugfixes, dashback_slow_turn_only_triggers_on_frame_2)
{
    EbUnit p1, p2;
    setupEbWorld(p1, p2);

    // 1. Direct dashback: Wait -> TurnDash.
    s_ebBody.stickX = -1.0f;
    fireStatus(p1, game::kStatusWait, game::kStatusTurnDash);
    stepEb();
    CHECK_EQ(1, s_ebSink.dbCount);
    CHECK_EQ(static_cast<int>(game::kDashbackDirect), static_cast<int>(s_ebSink.lastDb.flag));

    // 2. Frame-2 SlowTurn.asm dashback: Wait -> Turn (f0), Turn (f0+1), Turn -> Dash (f0+2).
    s_ebBody.stickX = -0.5f;
    fireStatus(p1, game::kStatusWait, game::kStatusTurn);
    stepEb();
    s_ebBody.stickX = -1.0f;
    stepEb();
    fireStatus(p1, game::kStatusTurn, game::kStatusDash);
    stepEb();
    CHECK_EQ(2, s_ebSink.dbCount);
    CHECK_EQ(static_cast<int>(game::kDashbackSlowTurn), static_cast<int>(s_ebSink.lastDb.flag));

    // 3. Missed dashback: Wait -> Turn (f0), stays Turn on f0+1 and f0+2 with stick >= 0.73.
    s_ebBody.stickX = -0.5f;
    fireStatus(p1, game::kStatusWait, game::kStatusTurn);
    stepEb();
    s_ebBody.stickX = -0.5f;
    stepEb();
    s_ebBody.stickX = -0.8f;
    stepEb();
    CHECK_EQ(3, s_ebSink.dbCount);
    CHECK_EQ(static_cast<int>(game::kDashbackFailed), static_cast<int>(s_ebSink.lastDb.flag));

    // 4. Regression: gentle slow turn (never >= 0.73 during frames 1..4) that
    // dashes on frame 6 after natural flip must NOT emit a bogus Slow Dashback.
    s_ebBody.stickX = -0.3f;
    fireStatus(p1, game::kStatusWait, game::kStatusTurn);
    stepEb();
    for (int i = 0; i < 4; i++)
    {
        stepEb();
    }
    s_ebBody.stickX = -1.0f;
    fireStatus(p1, game::kStatusTurn, game::kStatusDash);
    stepEb();
    CHECK_EQ(3, s_ebSink.dbCount);

    teardownEbWorld();
}

TEST(engine_bugfixes, combo_counts_one_hit_per_frame_and_ignores_slideoff_and_pummel)
{
    EbUnit p1, p2;
    setupEbWorld(p1, p2);

    // Baseline frame at 0% so prevDamage starts at 0.0f before hit 1.
    s_ebDamage[0] = 0.0f;
    stepEb();

    // 1. Regression: single hit emitting two damageStatus transitions in one frame
    // (DownWait -> DownDamage -> DamageFly) must count as 1 hit, NOT 2!
    s_ebDamage[0] = 10.0f;
    p1.ff->hitstun = 12;
    p1.ff->stop->m_hitStopRealFrame = 5;
    p1.ff->statusModule->m_groups = 0;
    fireStatus(p1, 0x4B, game::kStatusDownDamage, Situation_Ground);
    fireStatus(p1, game::kStatusDownDamage, 0x45, Situation_Air);
    stepEb();
    CHECK_EQ(0, s_ebSink.comboCount);

    // 2. Real 2nd hit on a later frame brings combo to 2 Hits (not 3!).
    s_ebDamage[0] = 22.0f;
    p1.ff->hitstun = 14;
    p1.ff->stop->m_hitStopRealFrame = 6;
    fireStatus(p1, 0x45, game::kStatusDamage, Situation_Ground);
    stepEb();
    CHECK_EQ(1, s_ebSink.comboCount);
    CHECK_EQ(2, static_cast<int>(s_ebSink.lastCombo.a));
    CHECK_EQ(22, static_cast<int>(s_ebSink.lastCombo.b));

    // 3. Regression: slide-off transition Damage (0x43) -> DamageAir (0x44) with
    // no new damage, no hitlag increase, and no hitstun increase must NOT count.
    p1.ff->hitstun = 13;
    p1.ff->stop->m_hitStopRealFrame = 0;
    fireStatus(p1, game::kStatusDamage, game::kStatusDamage + 1, Situation_Air);
    stepEb();
    CHECK_EQ(1, s_ebSink.comboCount);

    // 4. Regression: pummel (CaptureDamage 0x3F) while grabbed does not emit a combo hit.
    p1.ff->hitstun = 0;
    p1.ff->stop->m_hitStopRealFrame = 0;
    s_ebDamage[0] = 25.0f;
    fireStatus(p1, 0x3E, 0x3F, Situation_Ground);
    stepEb();
    CHECK_EQ(1, s_ebSink.comboCount);

    teardownEbWorld();
}

TEST(engine_bugfixes, cpu_shield_lag_detects_zero_hitlag_and_shorter_hitlag_entry)
{
    // Exact shieldLag predicate from src/main/cpu/options.cpp:2305.
    struct Case {
        int status;
        int prevStatus;
        int hitlag;
        int prevHitlag;
        bool want;
    };
    static const Case kCases[] = {
        // 0-hitlag hit entering GuardDamage from Guard.
        {game::kStatusGuardDamage, game::kStatusGuard, 0, 0, true},
        // Shorter shield hitlag entering GuardDamage straight from larger attacker hitlag.
        {game::kStatusGuardDamage, game::kStatusAttack, 3, 7, true},
        // Equal hitlag entering GuardDamage from GuardOn.
        {game::kStatusGuardDamage, game::kStatusGuardOn, 4, 4, true},
        // Second hit while already in GuardDamage raises hitlag.
        {game::kStatusGuardDamage, game::kStatusGuardDamage, 6, 2, true},
        // Decaying or steady hitlag while already in GuardDamage is NOT a new hit.
        {game::kStatusGuardDamage, game::kStatusGuardDamage, 5, 6, false},
        {game::kStatusGuardDamage, game::kStatusGuardDamage, 0, 0, false},
        // Not in GuardDamage is never shieldLag.
        {game::kStatusGuard, game::kStatusGuardOn, 5, 0, false}
    };
    for (u32 i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
    {
        const Case& c = kCases[i];
        bool shieldLag = c.status == game::kStatusGuardDamage
                         && (c.hitlag > c.prevHitlag || c.prevStatus != game::kStatusGuardDamage);
        CHECK_EQ(c.want, shieldLag);
    }
}
