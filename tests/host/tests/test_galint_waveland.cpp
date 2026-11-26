// Regression tests for same-frame horizontal waveland GALINT calculation across
// tracker.cpp, techniques.cpp, and drill_ledge_core.h.
//
// Reproduces the 0-air-frame horizontal waveland bug where entering EscapeAir
// (0x21) and LandingFallSpecial (0x19) on the same frame left Frame::actionable
// stuck at 1 on frame 1 of the 10-frame landing lag, causing ledgedash GALINT
// to fire 10 frames early.
#define TM_MODULE_MAIN 1

#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/event/so_event_manage_module_impl.h>
#include <so/event/so_event_system.h>
#include <so/status/so_status_event_presenter.h>
#include <string.h>

#include "extra/drill_ledge_core.h"
#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
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

    bool outOfPlay(int status, bool platform)
    {
        return status == kStatusDead || status == kStatusStandby || (platform && status == kStatusRebirth);
    }
}
}

namespace {
    static tm::game::Body s_testBody;
    static s32 s_testLedgeGrabs = 1;
    static void* s_testNetBuf = NULL;

    static void* galint_allocNetworkZero(u32 bytes)
    {
        s_testNetBuf = host::mem::alloc(bytes, 16);
        if (s_testNetBuf != NULL)
        {
            memset(s_testNetBuf, 0, bytes);
        }
        return s_testNetBuf;
    }

    static void galint_freeNetworkSlot(void** slot)
    {
        if (slot != NULL)
        {
            *slot = NULL;
        }
        s_testNetBuf = NULL;
    }

    static bool galint_readBody(Fighter*, tm::game::Body& out)
    {
        out = s_testBody;
        return true;
    }

    static s32 galint_instanceInt(const soModuleEnumeration*, u32 index)
    {
        return index == 79u ? s_testLedgeGrabs : 0;
    }
}

// Compile the real techniques.cpp wired directly to the real tracker.cpp under
// test (tracker_under_test_*), with distinct exported names so it does not
// clash with test_wii_lab.cpp's lightweight stubs.
#define trackerFrame() tracker_under_test_frame()
#define tracked(i) tracker_under_test_tracked(i)
#define readBody galint_readBody
#define instanceInt galint_instanceInt
#define allocNetworkZero galint_allocNetworkZero
#define freeNetworkSlot galint_freeNetworkSlot
#define techniquesAttach galint_techniquesAttach
#define techniquesDetach galint_techniquesDetach
#define techniquesReset galint_techniquesReset
#define techniquesFrame galint_techniquesFrame
#define techniqueState galint_techniqueState
#define lastLedgedash galint_lastLedgedash
#define lastLedgedashEvent galint_lastLedgedashEvent
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
#undef instanceInt
#undef readBody
#undef tracked
#undef trackerFrame

namespace {
    using namespace tm;

    struct TestUnit {
        host::fakes::FakeFighter* ff;
        Fighter* fighter;
        soEventManageModuleImpl* eventMod;
        soEventManager* eventMgr;
        s16 manageId;
    };

    static soEventManager* s_ftMgrEvents = NULL;

    struct SinkLog {
        int galintCount;
        game::Technique lastGalint;
        int wavedashCount;
        game::Technique lastWavedash;
    };
    static SinkLog s_sinkLog;

    static void recordTechnique(const game::Technique& t)
    {
        if (t.kind == game::kTechGalint)
        {
            s_sinkLog.galintCount++;
            s_sinkLog.lastGalint = t;
        }
        else if (t.kind == game::kTechWavedash)
        {
            s_sinkLog.wavedashCount++;
            s_sinkLog.lastWavedash = t;
        }
    }

    static TestUnit setupWorld()
    {
        memset(&s_testBody, 0, sizeof(s_testBody));
        s_testLedgeGrabs = 1;
        memset(&s_sinkLog, 0, sizeof(s_sinkLog));

        soEventSystem::getInstance()->reset();
        g_ftEntryManager = host::mem::make<ftEntryManager>();
        g_ftEntryManager->reset();
        g_ftManager = host::mem::make<ftManager>();
        s_ftMgrEvents = host::mem::make<soEventManager>();
        s_ftMgrEvents->reset();
        g_ftManager->m_eventManageModule.m_manageID = 3;
        soEventSystem::getInstance()->registerManager(3, s_ftMgrEvents);
        g_ftManager->ftOutsideEventObserver::addObserver(3, -1);

        for (int p = 0; p < game::kMaxFighters; p++)
        {
            host::fakes::setPlayer(p, static_cast<u8>(p == 0 ? 0 : 7), static_cast<u8>(p < 2 ? p : 3), 0, 0);
        }

        game::trackerAttach();
        game::galint_techniquesAttach();
        game::galint_techniquesReset(recordTechnique);

        host::fakes::FakeFighter& ff = host::fakes::addFighter();
        TestUnit u;
        u.ff = &ff;
        u.fighter = ff.fighter;
        u.fighter->m_taskId = 501;
        u.eventMod = host::mem::make<soEventManageModuleImpl>();
        u.eventMod->m_manageID = 10;
        ff.modules->m_eventManageModule = u.eventMod;
        ff.modules->m_collisionAttackModule = host::mem::make<soCollisionAttackModuleImpl>();
        u.eventMgr = host::mem::make<soEventManager>();
        u.eventMgr->reset();
        u.manageId = 10;
        soEventSystem::getInstance()->registerManager(10, u.eventMgr);

        soStatusEventObserver* fs = static_cast<soStatusEventObserver*>(u.fighter);
        soAnimCmdEventObserver* fa = static_cast<soAnimCmdEventObserver*>(static_cast<StageObject*>(u.fighter));
        fs->m_unitID = 4;
        fs->addObserver(10, -1);
        fa->m_unitID = 5;
        fa->addObserver(10, 0);

        ftEntry* e = host::mem::make<ftEntry>();
        e->m_entryId = 0x10000;
        e->m_playerNo = 0;
        e->m_activeInstanceIndex = 0;
        e->m_instances[0].m_fighter = u.fighter;
        e->m_owner = host::mem::make<ftOwner>();
        g_ftEntryManager->setSlot(0, e);

        // Register observers on P1.
        game::trackerStep(true);
        return u;
    }

    static void teardownWorld()
    {
        game::galint_techniquesDetach();
        game::trackerDetach();
    }

    static void fireStatusChange(TestUnit& u, int prevStatus, int newStatus, SituationKind sit)
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

    // Mirrors src/extra/drill_core_engines.cpp:393-416 so the Ledgedash Drill
    // core receives the exact Tracked frame that tracker.cpp produced.
    static bool feedLedgeCore(extra::LedgeCoreState& st, const game::Tracked* t, const game::Body& body,
                              u8 ledgeGrabs)
    {
        extra::LedgeFrameInput li;
        memset(&li, 0, sizeof(li));
        li.frame = t->cur.frame;
        li.status = t->cur.status;
        li.prevStatus = t->prev.status;
        li.motionKind = static_cast<s16>(body.motionKind);
        li.hitstun = t->cur.hitstun;
        li.xluFrames = body.xluFrames;
        li.ledgeGrabCount = ledgeGrabs;
        li.airborne = t->cur.airborne;
        li.actionable = t->cur.actionable;
        li.stickX = body.stickX;
        li.stickY = body.stickY;
        int evCount = t->eventCount < extra::kLedgeCoreMaxEvents ? t->eventCount : extra::kLedgeCoreMaxEvents;
        li.eventCount = static_cast<u8>(evCount);
        for (int k = 0; k < evCount; k++)
        {
            li.events[k].status = t->events[k].status;
            li.events[k].prev = t->events[k].prev;
            li.events[k].motionKind = t->events[k].motionKind;
            li.events[k].airborne = t->events[k].airborne;
        }
        return extra::ledgeCoreStep(st, li);
    }

    static const game::Tracked* finishAndStep(extra::LedgeCoreState* ledgeSt, bool* ledgeDone = NULL)
    {
        game::trackerStepRuns(true);
        game::trackerHandOver();
        const game::Tracked* t = game::tracker_under_test_tracked(0);
        game::galint_techniquesFrame();
        if (ledgeSt != NULL)
        {
            bool done = feedLedgeCore(*ledgeSt, t, s_testBody, static_cast<u8>(s_testLedgeGrabs));
            if (ledgeDone != NULL)
            {
                *ledgeDone = done;
            }
        }
        // Advance tracker frame counter as the next step's pad hook does.
        game::trackerStep(true);
        return t;
    }
}

TEST(galint_waveland, horizontal_frame1_waveland_waits_10f_landing_lag_for_galint)
{
    TestUnit u = setupWorld();
    extra::LedgeCoreState ledgeSt;
    extra::ledgeCoreResetAll(ledgeSt);

    // Grab ledge (Cliff_Catch 0x74), then hang 4 frames in Cliff_Wait (0x75)
    // with xlu = 31, 30, 29, 28 (mirrors Bowser/Sonic report).
    s_testBody.xluFrames = 0;
    fireStatusChange(u, game::kStatusFall, 0x74, Situation_Cliff);
    finishAndStep(&ledgeSt);

    for (int f = 0; f < 4; f++)
    {
        s_testBody.xluFrames = 31 - f;
        if (f == 0)
        {
            fireStatusChange(u, 0x74, 0x75, Situation_Cliff);
        }
        finishAndStep(&ledgeSt);
    }

    // Ledge drop -> Fall (0x0E), xlu = 27.
    s_testBody.xluFrames = 27;
    fireStatusChange(u, 0x75, game::kStatusFall, Situation_Air);
    const game::Tracked* t = finishAndStep(&ledgeSt);
    CHECK_EQ(1, static_cast<int>(t->cur.airborne));

    // Double jump -> Jump_Aerial (0x0C), xlu = 26.
    s_testBody.xluFrames = 26;
    fireStatusChange(u, game::kStatusFall, game::kStatusJumpAerial, Situation_Air);
    t = finishAndStep(&ledgeSt);
    CHECK_EQ(1, static_cast<int>(t->cur.airborne));

    // Frame-1 horizontal waveland: Jump_Aerial (0x0C) -> Escape_Air (0x21) ->
    // Landing_Fall_Special (0x19) all in the same game frame, xlu = 25.
    s_testBody.xluFrames = 25;
    s_testBody.stickX = 1.0f;
    s_testBody.stickY = 0.0f;
    u.ff->statusModule->m_groups = 0;
    fireStatusChange(u, game::kStatusJumpAerial, game::kStatusEscapeAir, Situation_Air);
    fireStatusChange(u, game::kStatusEscapeAir, game::kStatusLandingFallSpecial, Situation_Ground);
    bool drillDone = false;
    t = finishAndStep(&ledgeSt, &drillDone);

    // Grounded frame 1 of 10f waveland lag MUST NOT be marked actionable and
    // MUST NOT prematurely emit GALINT +25!
    CHECK_EQ(0, static_cast<int>(t->cur.airborne));
    CHECK_EQ(static_cast<int>(game::kStatusLandingFallSpecial), static_cast<int>(t->cur.status));
    CHECK_EQ(0, static_cast<int>(t->cur.actionable));
    CHECK_EQ(0, s_sinkLog.galintCount);
    CHECK(!drillDone);

    // Remaining 9 frames of 10-frame Landing_Fall_Special lag (xlu = 24..16).
    for (int lag = 2; lag <= 10; lag++)
    {
        s_testBody.xluFrames = 26 - lag;
        drillDone = false;
        t = finishAndStep(&ledgeSt, &drillDone);
        CHECK_EQ(0, static_cast<int>(t->cur.actionable));
        CHECK_EQ(0, s_sinkLog.galintCount);
        CHECK(!drillDone);
    }

    // First grounded actionable frame after 10f lag: Landing_Fall_Special -> Wait, xlu = 15.
    s_testBody.xluFrames = 15;
    fireStatusChange(u, game::kStatusLandingFallSpecial, game::kStatusWait, Situation_Ground);
    drillDone = false;
    t = finishAndStep(&ledgeSt, &drillDone);

    CHECK_EQ(1, static_cast<int>(t->cur.actionable));
    CHECK_EQ(1, s_sinkLog.galintCount);
    CHECK_EQ(15, static_cast<int>(s_sinkLog.lastGalint.frame));
    CHECK_EQ(4, static_cast<int>(s_sinkLog.lastGalint.a));

    // Lab Ledgedash HUD cells: 4 ledge + 1 fall + 1 jump + 1 airdodge + 9 landing = 16 cells.
    const game::Ledgedash& ld = game::galint_lastLedgedash();
    CHECK_EQ(15, static_cast<int>(ld.galint));
    CHECK_EQ(16, static_cast<int>(ld.count));
    CHECK_EQ(static_cast<int>(game::kCellAirdodge), static_cast<int>(ld.cells[6]));
    for (int k = 7; k < 16; k++)
    {
        CHECK_EQ(static_cast<int>(game::kCellLanding), static_cast<int>(ld.cells[k]));
    }

    // Ledgedash Drill core matches identically: GALINT +15 and all 10 lag frames recorded.
    CHECK(drillDone);
    CHECK_EQ(static_cast<int>(extra::kLedgeOutcomeLedgedash), static_cast<int>(ledgeSt.last.outcome));
    CHECK_EQ(15, static_cast<int>(ledgeSt.last.galint));
    CHECK_EQ(4, static_cast<int>(ledgeSt.last.cliffWaitFrames));
    CHECK_EQ(0, static_cast<int>(ledgeSt.last.airdodgeAirFrames));
    CHECK_EQ(10, static_cast<int>(ledgeSt.last.landingLagFrames));
    CHECK_EQ(16, static_cast<int>(ledgeSt.last.cellCount));

    teardownWorld();
}

TEST(galint_waveland, multiframe_airdodge_and_landing_interrupt_open_work)
{
    TestUnit u = setupWorld();
    extra::LedgeCoreState ledgeSt;
    extra::ledgeCoreResetAll(ledgeSt);

    // 1. Multi-frame airdodge waveland (2 airborne EscapeAir frames + 10f lag).
    fireStatusChange(u, game::kStatusFall, 0x74, Situation_Cliff);
    finishAndStep(&ledgeSt);
    s_testBody.xluFrames = 31;
    fireStatusChange(u, 0x74, 0x75, Situation_Cliff);
    finishAndStep(&ledgeSt);
    s_testBody.xluFrames = 30;
    finishAndStep(&ledgeSt);

    s_testBody.xluFrames = 29;
    fireStatusChange(u, 0x75, game::kStatusFall, Situation_Air);
    finishAndStep(&ledgeSt);

    s_testBody.xluFrames = 28;
    fireStatusChange(u, game::kStatusFall, game::kStatusJumpAerial, Situation_Air);
    finishAndStep(&ledgeSt);

    // Frame 1 of EscapeAir (airborne): airborne actionable = 1, must not finish ledgedash.
    s_testBody.xluFrames = 27;
    s_testBody.stickX = 0.8f;
    s_testBody.stickY = -0.6f;
    fireStatusChange(u, game::kStatusJumpAerial, game::kStatusEscapeAir, Situation_Air);
    bool drillDone = false;
    const game::Tracked* t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(1, static_cast<int>(t->cur.airborne));
    CHECK_EQ(1, static_cast<int>(t->cur.actionable));
    CHECK_EQ(0, s_sinkLog.galintCount);
    CHECK(!drillDone);

    // Frame 2 of EscapeAir (airborne): actionable = 0.
    s_testBody.xluFrames = 26;
    t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(0, static_cast<int>(t->cur.actionable));

    // Land into Landing_Fall_Special (10 frames: xlu = 25..16).
    s_testBody.xluFrames = 25;
    fireStatusChange(u, game::kStatusEscapeAir, game::kStatusLandingFallSpecial, Situation_Ground);
    t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(0, static_cast<int>(t->cur.actionable));
    CHECK(!drillDone);

    for (int lag = 2; lag <= 10; lag++)
    {
        s_testBody.xluFrames = 26 - lag;
        t = finishAndStep(&ledgeSt, &drillDone);
        CHECK_EQ(0, static_cast<int>(t->cur.actionable));
        CHECK(!drillDone);
    }

    s_testBody.xluFrames = 15;
    fireStatusChange(u, game::kStatusLandingFallSpecial, game::kStatusWait, Situation_Ground);
    t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(1, static_cast<int>(t->cur.actionable));
    CHECK_EQ(1, s_sinkLog.galintCount);
    CHECK_EQ(15, static_cast<int>(s_sinkLog.lastGalint.frame));
    CHECK(drillDone);
    CHECK_EQ(15, static_cast<int>(ledgeSt.last.galint));
    CHECK_EQ(2, static_cast<int>(ledgeSt.last.airdodgeAirFrames));
    CHECK_EQ(10, static_cast<int>(ledgeSt.last.landingLagFrames));

    // 2. Same-frame Fall -> Landing (0x16) with ground interrupts opening on frame 4 of 0x16.
    s_sinkLog.galintCount = 0;
    s_testBody.xluFrames = 31;
    fireStatusChange(u, game::kStatusWait, 0x75, Situation_Cliff);
    finishAndStep(&ledgeSt);
    s_testBody.xluFrames = 30;
    fireStatusChange(u, 0x75, game::kStatusJumpAerial, Situation_Air);
    finishAndStep(&ledgeSt);

    // Same-frame JumpAerial -> Fall -> Landing (0x16): lag frames 1..3 have groups = 0.
    s_testBody.xluFrames = 29;
    u.ff->statusModule->m_groups = 0;
    fireStatusChange(u, game::kStatusJumpAerial, game::kStatusFall, Situation_Air);
    fireStatusChange(u, game::kStatusFall, game::kStatusLanding, Situation_Ground);
    t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(0, static_cast<int>(t->cur.actionable));
    CHECK_EQ(0, s_sinkLog.galintCount);
    CHECK(!drillDone);

    for (int lag = 2; lag <= 3; lag++)
    {
        s_testBody.xluFrames = 30 - lag;
        t = finishAndStep(&ledgeSt, &drillDone);
        CHECK_EQ(0, static_cast<int>(t->cur.actionable));
        CHECK(!drillDone);
    }

    // Frame 4 of Landing (0x16): P+ opens ground action groups while still in status 0x16.
    s_testBody.xluFrames = 26;
    u.ff->statusModule->m_groups = 1u << game::kGroupGroundJump;
    t = finishAndStep(&ledgeSt, &drillDone);
    CHECK_EQ(static_cast<int>(game::kStatusLanding), static_cast<int>(t->cur.status));
    CHECK_EQ(1, static_cast<int>(t->cur.actionable));
    CHECK_EQ(1, s_sinkLog.galintCount);
    CHECK_EQ(26, static_cast<int>(s_sinkLog.lastGalint.frame));
    CHECK(drillDone);
    CHECK_EQ(26, static_cast<int>(ledgeSt.last.galint));

    teardownWorld();
}

TEST(galint_waveland, preserves_same_frame_shield_hitlag_and_grounded_wavedash_osd)
{
    TestUnit u = setupWorld();

    // 1. Regression check for tracker.cpp:740-742: fighter enters GuardOn (0x1A)
    // and is immediately hit into GuardDamage (0x1D) with hitlag on the same frame.
    // Must still report actionable = 1 so frame advantage sees the action.
    u.ff->status = game::kStatusLandingAttackAir;
    u.ff->situation->m_kind = Situation_Ground;
    u.ff->statusModule->m_groups = 0;
    finishAndStep(NULL);

    u.ff->stop->m_hitStopRealFrame = 6;
    u.ff->shieldStun = 8;
    fireStatusChange(u, game::kStatusLandingAttackAir, game::kStatusGuardOn, Situation_Ground);
    fireStatusChange(u, game::kStatusGuardOn, game::kStatusGuardDamage, Situation_Ground);
    const game::Tracked* t = finishAndStep(NULL);
    CHECK_EQ(static_cast<int>(game::kStatusGuardDamage), static_cast<int>(t->cur.status));
    CHECK_EQ(6, static_cast<int>(t->cur.hitlag));
    CHECK_EQ(1, static_cast<int>(t->cur.actionable));

    // Clear hitlag/shieldstun.
    u.ff->stop->m_hitStopRealFrame = 0;
    u.ff->shieldStun = 0;
    fireStatusChange(u, game::kStatusGuardDamage, game::kStatusWait, Situation_Ground);
    finishAndStep(NULL);

    // 2. Frame-1 grounded wavedash (JumpSquat -> Jump -> EscapeAir -> LandingFallSpecial):
    // actionable must be 0 on frame 1 of LandingFallSpecial, while kTechWavedash
    // still emits Frame 1 cleanly.
    fireStatusChange(u, game::kStatusWait, game::kStatusJumpSquat, Situation_Ground);
    finishAndStep(NULL);

    s_testBody.stickX = 0.96f;
    s_testBody.stickY = -0.28f;
    u.ff->statusModule->m_groups = 0;
    fireStatusChange(u, game::kStatusJumpSquat, game::kStatusJump, Situation_Air);
    fireStatusChange(u, game::kStatusJump, game::kStatusEscapeAir, Situation_Air);
    fireStatusChange(u, game::kStatusEscapeAir, game::kStatusLandingFallSpecial, Situation_Ground);
    t = finishAndStep(NULL);
    CHECK_EQ(static_cast<int>(game::kStatusLandingFallSpecial), static_cast<int>(t->cur.status));
    CHECK_EQ(0, static_cast<int>(t->cur.actionable));
    CHECK_EQ(1, s_sinkLog.wavedashCount);
    CHECK_EQ(1, static_cast<int>(s_sinkLog.lastWavedash.frame));

    teardownWorld();
}

TEST(galint_waveland, ledge_core_direct_guard_blocks_stale_actionable_on_landing_entry)
{
    extra::LedgeCoreState st;
    extra::ledgeCoreResetAll(st);

    // Drive drill_ledge_core directly with synthetic inputs where a caller passes
    // actionable = 1 on the same frame EscapeAir enters LandingFallSpecial.
    extra::LedgeFrameInput in;
    memset(&in, 0, sizeof(in));
    in.frame = 1;
    in.status = extra::kLedgeStCliffWait;
    in.prevStatus = extra::kLedgeStCliffCatch;
    in.xluFrames = 31;
    in.ledgeGrabCount = 1;
    in.eventCount = 1;
    in.events[0].prev = extra::kLedgeStCliffCatch;
    in.events[0].status = extra::kLedgeStCliffWait;
    CHECK(!extra::ledgeCoreStep(st, in));

    memset(&in, 0, sizeof(in));
    in.frame = 2;
    in.status = extra::kLedgeStFall;
    in.prevStatus = extra::kLedgeStCliffWait;
    in.airborne = 1;
    in.xluFrames = 30;
    in.eventCount = 1;
    in.events[0].prev = extra::kLedgeStCliffWait;
    in.events[0].status = extra::kLedgeStFall;
    CHECK(!extra::ledgeCoreStep(st, in));

    memset(&in, 0, sizeof(in));
    in.frame = 3;
    in.status = extra::kLedgeStLandingFallSpecial;
    in.prevStatus = extra::kLedgeStFall;
    in.airborne = 0;
    in.actionable = 1; // Stale airborne actionable flag!
    in.xluFrames = 29;
    in.eventCount = 2;
    in.events[0].prev = extra::kLedgeStFall;
    in.events[0].status = extra::kLedgeStEscapeAir;
    in.events[1].prev = extra::kLedgeStEscapeAir;
    in.events[1].status = extra::kLedgeStLandingFallSpecial;
    CHECK(!extra::ledgeCoreStep(st, in));
    CHECK_EQ(1, static_cast<int>(st.landingLagFrames));

    for (s32 f = 4; f <= 12; f++)
    {
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.status = extra::kLedgeStLandingFallSpecial;
        in.prevStatus = extra::kLedgeStLandingFallSpecial;
        in.xluFrames = 32 - f;
        CHECK(!extra::ledgeCoreStep(st, in));
    }

    memset(&in, 0, sizeof(in));
    in.frame = 13;
    in.status = 0x00;
    in.prevStatus = extra::kLedgeStLandingFallSpecial;
    in.actionable = 1;
    in.xluFrames = 19;
    in.eventCount = 1;
    in.events[0].prev = extra::kLedgeStLandingFallSpecial;
    in.events[0].status = 0x00;
    CHECK(extra::ledgeCoreStep(st, in));
    CHECK_EQ(19, static_cast<int>(st.last.galint));
    CHECK_EQ(10, static_cast<int>(st.last.landingLagFrames));
}
