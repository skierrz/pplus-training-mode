// Fighter tracker swap safety (src/main/game/tracker.cpp): RemoveWatch
// unregisters fighter observers and clears slots synchronously on
// ftOutsideEventObserver::notifyEventRemoveEntry before Fighter teardown,
// and the hardened managerAlive late path rejects recycled manageIds.
#define TM_MODULE_MAIN 1

#include <ft/ft_common_data_accesser.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gm/gm_global.h>
#include <so/event/so_event_manage_module_impl.h>
#include <so/event/so_event_system.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/body.h"
#include "main/game/fighters.h"

ftCommonDataAccesser g_ftCommonDataAccesser;
ftEntryManager* g_ftEntryManager = NULL;
ftManager* g_ftManager = NULL;

namespace tm {
namespace game {
    // Minimal implementations of fighters.cpp / body.cpp helpers needed by
    // tracker.cpp and character_rng.cpp in PC host tests.
    int listFighters(FighterRef* out, int max)
    {
        if (g_ftEntryManager == NULL)
        {
            return 0;
        }
        int n = 0;
        for (int i = 0; i < ftEntryManager::kMaxEntries && n < max; i++)
        {
            ftEntry* e = g_ftEntryManager->m_entries[i];
            if (e == NULL || e->m_entryId < 0)
            {
                continue;
            }
            int active = e->m_activeInstanceIndex;
            if (active < 0 || active >= 4 || e->m_instances[active].m_fighter == NULL)
            {
                continue;
            }
            out[n].entryId = e->m_entryId;
            out[n].playerNo = e->m_playerNo;
            out[n].cpu = e->m_playerNo != 0;
            out[n].port = out[n].cpu ? -1 : e->m_playerNo;
            out[n].fighter = e->m_instances[active].m_fighter;
            out[n].partner = e->m_subFighter;
            out[n].owner = e->m_owner;
            n++;
        }
        return n;
    }

    bool playerInMatch(int playerNo)
    {
        if (g_GameGlobal == NULL || g_GameGlobal->m_modeMelee == NULL || playerNo < 0 || playerNo >= kMaxFighters)
        {
            return true;
        }
        u8 state = g_GameGlobal->m_modeMelee->raw[0x98 + 0x5C * playerNo + 1];
        return state == 0 || state == 1;
    }

    s32 g_testWorkLCancel = 0;

    void readWorkValues(const soModuleEnumeration*, u32& flags, s32& lCancel)
    {
        flags = 0;
        lCancel = g_testWorkLCancel;
    }
}
}

#define trackerFrame tracker_under_test_frame
#define tracked tracker_under_test_tracked
#include "main/game/tracker.cpp"
#undef trackerFrame
#undef tracked

namespace {
    using namespace tm;

    struct FakeFighterUnit {
        Fighter* fighter;
        soEventManageModuleImpl* eventMod;
        soEventManager* eventMgr;
        s16 manageId;
    };

    static soEventManager* s_ftMgrEvents = NULL;

    static void setupTrackerWorld()
    {
        soEventSystem::getInstance()->reset();
        g_ftEntryManager = host::mem::make<ftEntryManager>();
        g_ftEntryManager->reset();
        g_ftManager = host::mem::make<ftManager>();
        s_ftMgrEvents = host::mem::make<soEventManager>();
        s_ftMgrEvents->reset();
        g_ftManager->m_eventManageModule.m_manageID = 3;
        soEventSystem::getInstance()->registerManager(3, s_ftMgrEvents);
        // Mirror Brawl: ftManager itself is registered on unit 0 of manageId 3.
        g_ftManager->ftOutsideEventObserver::addObserver(3, -1);
        for (int p = 0; p < game::kMaxFighters; p++)
        {
            host::fakes::setPlayer(p, static_cast<u8>(p == 0 ? 0 : 7), static_cast<u8>(p < 2 ? p : 3), 0, 0);
        }
        game::trackerAttach();
    }

    static FakeFighterUnit makeTrackedFighter(u32 taskId, s16 manageId)
    {
        host::fakes::FakeFighter& ff = host::fakes::addFighter();
        FakeFighterUnit u;
        u.fighter = ff.fighter;
        u.fighter->m_taskId = taskId;
        u.eventMod = host::mem::make<soEventManageModuleImpl>();
        u.eventMod->m_manageID = manageId;
        ff.modules->m_eventManageModule = u.eventMod;
        ff.modules->m_collisionAttackModule = host::mem::make<soCollisionAttackModuleImpl>();
        u.eventMgr = host::mem::make<soEventManager>();
        u.eventMgr->reset();
        u.manageId = manageId;
        soEventSystem::getInstance()->registerManager(manageId, u.eventMgr);
        // Mirror Brawl: Fighter's own status (unit 4) and anim (unit 5) observers.
        soStatusEventObserver* fs = static_cast<soStatusEventObserver*>(u.fighter);
        soAnimCmdEventObserver* fa = static_cast<soAnimCmdEventObserver*>(static_cast<StageObject*>(u.fighter));
        fs->m_unitID = 4;
        fs->addObserver(manageId, -1);
        fa->m_unitID = 5;
        fa->addObserver(manageId, 0);
        return u;
    }

    static ftEntry* bindEntry(int slotIdx, int playerNo, int entryId, const FakeFighterUnit& main,
                              const FakeFighterUnit* partner = NULL)
    {
        ftEntry* e = host::mem::make<ftEntry>();
        e->m_entryId = entryId;
        e->m_playerNo = playerNo;
        e->m_activeInstanceIndex = 0;
        e->m_instances[0].m_fighter = main.fighter;
        if (partner != NULL)
        {
            e->m_instances[1].m_fighter = partner->fighter;
            e->m_subFighter = partner->fighter;
        }
        e->m_owner = host::mem::make<ftOwner>();
        g_ftEntryManager->setSlot(slotIdx, e);
        return e;
    }
}

TEST(tracker_swap, remove_entry_unregisters_main_and_partner_before_destroy)
{
    setupTrackerWorld();
    host::fakes::setPlayer(2, 0x10, 1, 0, 0); // P3 Ice Climbers

    FakeFighterUnit p1 = makeTrackedFighter(101, 10);
    FakeFighterUnit p2 = makeTrackedFighter(102, 11);
    FakeFighterUnit p3Main = makeTrackedFighter(103, 12);
    FakeFighterUnit p3Sub = makeTrackedFighter(104, 13);
    bindEntry(0, 0, 0x10000, p1);
    bindEntry(1, 1, 0x20001, p2);
    bindEntry(2, 2, 0x30002, p3Main, &p3Sub);

    game::trackerStep(true);
    game::trackerStepRuns(true);
    CHECK_EQ(4, game::trackedCount());
    CHECK_EQ(2, p2.eventMgr->observerCount(4));     // fighter + our StatusWatch
    CHECK_EQ(2, p2.eventMgr->observerCount(5));     // fighter + our AnimWatch
    CHECK_EQ(2, p3Main.eventMgr->observerCount(4));
    CHECK_EQ(2, p3Sub.eventMgr->observerCount(4));
    CHECK(s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));

    // Simulate ftManager::removeEntry(0x30002) for P3 Ice Climbers: notify runs
    // BEFORE Fighter::~Fighter destroys the fighters or removes their managers.
    g_ftManager->notifyRemoveEntry(0x30002);

    // Both Popo and Nana must already be unregistered and cleared from s_slots.
    CHECK_EQ(1, p3Main.eventMgr->observerCount(4));
    CHECK_EQ(1, p3Main.eventMgr->observerCount(5));
    CHECK_EQ(1, p3Sub.eventMgr->observerCount(4));
    CHECK_EQ(1, p3Sub.eventMgr->observerCount(5));
    CHECK_EQ(0u, game::s_lost);
    CHECK_EQ(2, game::trackedCount());

    // Now destroy P3's managers and unlink its entry, then run same-frame handOver.
    soEventSystem::getInstance()->removeManager(12);
    soEventSystem::getInstance()->removeManager(13);
    g_ftEntryManager->setSlot(2, NULL);
    p3Main.fighter->m_moduleAccesser = NULL;
    p3Sub.fighter->m_moduleAccesser = NULL;
    game::trackerHandOver();

    // P1 and P2 stay tracked and healthy; zero lost across the swap.
    CHECK_EQ(2, game::trackedCount());
    CHECK_EQ(2, p1.eventMgr->observerCount(4));
    CHECK_EQ(2, p2.eventMgr->observerCount(4));

    game::trackerDetach();
    CHECK_EQ(0u, game::s_lost);
    CHECK_EQ(game::s_registrations, game::s_removals);
    CHECK(!s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));
}

TEST(tracker_swap, late_path_rejects_recycled_manage_id)
{
    setupTrackerWorld();
    FakeFighterUnit oldF = makeTrackedFighter(201, 20);
    bindEntry(0, 0, 0x10000, oldF);
    game::trackerStep(true);
    CHECK_EQ(1, game::trackedCount());
    CHECK_EQ(2, oldF.eventMgr->observerCount(4));

    // Simulate old fighter destroyed without notifyRemoveEntry firing, and a
    // brand-new fighter reusing BOTH the same manageId (20) and eventMgr!
    soEventSystem::getInstance()->removeManager(20);
    g_ftEntryManager->setSlot(0, NULL);

    FakeFighterUnit newF = makeTrackedFighter(202, 20);
    // Point manageId 20 at the exact same soEventManager address as oldF!
    oldF.eventMgr->reset();
    soEventSystem::getInstance()->registerManager(20, oldF.eventMgr);
    static_cast<soStatusEventObserver*>(newF.fighter)->addObserver(20, -1);
    static_cast<soAnimCmdEventObserver*>(static_cast<StageObject*>(newF.fighter))->addObserver(20, 0);
    // Add an unrelated observer into slot 1 (where oldF's StatusWatch sat).
    soStatusEventObserver bystander(4);
    bystander.addObserver(20, -1);
    CHECK(oldF.eventMgr->containsObserver(4, &bystander));
    bindEntry(0, 0, 0x20000, newF);

    // sync(true) runs late cleanup for old entry 0x10000: managerAlive MUST
    // reject it (entryId/fighter/taskId changed) so bystander is NOT erased!
    game::trackerStep(true);
    CHECK(oldF.eventMgr->containsObserver(4, &bystander));
    CHECK_EQ(1u, game::s_lost);

    game::trackerDetach();
}

TEST(tracker_swap, suspend_and_resume_toggle_remove_watch)
{
    setupTrackerWorld();
    FakeFighterUnit p1 = makeTrackedFighter(301, 15);
    bindEntry(0, 0, 0x10000, p1);
    game::trackerStep(true);
    CHECK(s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));

    // Before savestate copy: RemoveWatch must come off ftManager's unit.
    game::trackerSuspend();
    CHECK(!s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));
    CHECK_EQ(1, p1.eventMgr->observerCount(4));

    // After savestate copy: RemoveWatch goes back on.
    game::trackerResume(false);
    CHECK(s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));
    CHECK_EQ(2, p1.eventMgr->observerCount(4));

    // Pause (live=false): RemoveWatch comes off too.
    game::trackerStep(false);
    CHECK(!s_ftMgrEvents->containsObserver(0, &game::s_removeWatch));

    game::trackerDetach();
}
