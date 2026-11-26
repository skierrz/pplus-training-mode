// Interrupt-group reading and "actionable during a frame" adapt ProjectPunch's
// anim_cmd_watcher.cpp and playerdata.cpp (MIT; see THIRD_PARTY_NOTICES.md);
// details: docs/internals.md, "Fighter trackers".
#include "main/game/tracker.h"

#include <ft/fighter.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/anim/so_anim_cmd_event_presenter.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/event/so_event_manage_module_impl.h>
#include <so/event/so_event_system.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_event_presenter.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <stddef.h>
#include <string.h>
#ifdef __MWERKS__
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include "shared/game/heap.h"
#endif

#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/status_ids.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Per-fighter frame trackers fed by observers they add to each fighter's
// event units; the observers must be off whenever the game may remove a
// fighter or memory is copied. Where each value comes from, and how the
// observers go on and off: docs/internals.md, "Fighter trackers".
namespace tm {
namespace game {
    struct Slot;

    class StatusWatch : public soStatusEventObserver {
    public:
        StatusWatch() : soStatusEventObserver(-1), slot(NULL)
        {
        }
        virtual void notifyEventChangeStatus(int statusKind, int prevStatusKind, soStatusData* statusData,
                                             soModuleAccesser* moduleAccesser);
        Slot* slot;
    };

    class AnimWatch : public soAnimCmdEventObserver {
    public:
        // The base registers nowhere for manage id -1.
        AnimWatch() : soAnimCmdEventObserver(-1, -1), slot(NULL)
        {
        }
        virtual bool isObserv(char unk);
        virtual bool notifyEventAnimCmd(acAnimCmd* acmd, soModuleAccesser* moduleAccesser, s32 unk3);
        Slot* slot;
    };

    // Registered on g_ftManager's outside-event unit (capacity 8, unit 0):
    // ftManager::removeEntry (0x80814370) fires notifyEventRemoveEntry
    // (0x80863B98, vtable +0x60) synchronously before ftEntry::destroy
    // (0x8081E1E8) destroys any Fighter or its soEventManager.
    class RemoveWatch : public ftOutsideEventObserver {
    public:
        RemoveWatch() : ftOutsideEventObserver(0)
        {
        }
        virtual void notifyEventRemoveEntry(int entryId);
    };

    struct Slot {
        void* operator new(size_t, void* p)
        {
            return p;
        }
        void operator delete(void*, void*)
        {
        }
        Tracked t;
        StatusWatch status;
        AnimWatch anim;
        soModuleAccesser* accesser; // events from anything else are ignored
        soEventManager* manager;    // the manager the observers are on
        s16 manageId;
        bool registered;
        bool leaving; // the match setup no longer lists the player
        bool cancelOpen;
        // Collected during the frame, folded into the frame at its end.
        u8 changes;
        u8 actionable;
        u32 groupsOn;
        s16 lastPrev;
        StatusEvent events[kMaxEvents]; // this frame's changes so far
        // Bookkeeping for the log and the test.
        bool everRegistered;
        s16 firstStatusSend;
        s16 firstAnimSend;
        u32 statusEvents;
        u32 animEvents;
    };

#ifdef __MWERKS__
    static Slot* s_slots = NULL;
#else
    static Slot s_slotsStatic[kMaxTracked];
    static Slot* s_slots = s_slotsStatic;
#endif
    static RemoveWatch s_removeWatch;
    static soEventManager* s_removeManager = NULL;
    static s16 s_removeManageId = -1;
    static bool s_removeRegistered = false;
    static bool s_removeLogged = false;
    static s32 s_frame = 0;
    // Hand-over state: docs/internals.md, "Frame hand-over".
    static bool s_pending = false;
    static bool s_lastRan = false;
    static bool s_owed = false;
    static bool s_healthy = true;
    static FrameListener s_listener = NULL;
    static StatusListener s_statusListener = NULL;
    static u32 s_registrations = 0;
    static u32 s_removals = 0;
    static u32 s_lost = 0;        // fighters that vanished while observed
    static u32 s_sendChanges = 0; // re-registrations that got another list slot
    static u32 s_eventsLost = 0;  // status changes past kMaxEvents in a frame
    u8 g_trackerTrace = 0;

    enum {
        kCmdGroupStatus = 2,
        kCmdEnableGroup = 10,
        kCmdDisableGroup = 11,
        kCmdGroupCancel = 0x64,
        kCmdEnableCancel = 0,
        // Groups that start an action, per situation.
        kGroundActionGroups = (1 << kGroupGroundSpecial) | (1 << kGroupGroundCatch) | (1 << kGroupGroundAttack)
                              | (1 << kGroupGroundEscape) | (1 << kGroupGroundGuard) | (1 << kGroupGroundJump),
        kAirActionGroups = (1 << kGroupAirSpecial) | (1 << kGroupAirEscape) | (1 << kGroupAirAttack)
                           | (1 << kGroupAirJumpAerial)
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    // Statuses a fighter is free to act in, and those only a choice enters:
    // entering any means it could act during that frame.
    static bool freeStatus(int s)
    {
        const u32 kMask = (1u << kStatusWait) | (1u << kStatusWalk) | (1u << kStatusWalkBrake)
                          | (1u << kStatusDash) | (1u << kStatusRun) | (1u << kStatusTurn)
                          | (1u << kStatusTurnDash) | (1u << kStatusTurnRun) | (1u << kStatusSquat)
                          | (1u << kStatusSquatWait) | (1u << kStatusFall) | (1u << kStatusFallAerial)
                          | (1u << kStatusJumpSquat) | (1u << kStatusGuardOn) | (1u << kStatusGuard)
                          | (1u << kStatusGuardOff);
        return static_cast<u32>(s) < 32u && ((kMask >> s) & 1u) != 0u;
    }

    // Moves: entering one from something that is not itself a move is a
    // choice (a smash's start, hold and release phases follow each other on
    // their own and do not count).
    static bool moveStatus(int s)
    {
        return (s >= kStatusAttack && s <= kStatusAttackAirFirst) || (s >= kStatusCatch && s <= kStatusCatch + 2)
               || (s >= kStatusEscape && s <= kStatusEscapeAir) || (s >= kStatusSpecialN && s < kStatusFinal);
    }

    static bool entersActionable(int status, int prev)
    {
        return freeStatus(status) || (moveStatus(status) && !moveStatus(prev) && prev != kStatusJumpSquat);
    }

    static bool landingStatus(int s)
    {
        return s >= kStatusLanding && s <= kStatusLandingFallSpecial;
    }

    // The statuses a hit puts a fighter in.
    static bool damageStatus(int s)
    {
        return (s >= kStatusDamage && s <= kStatusDamageFlyRoll) || s == kStatusDownDamage;
    }

    static bool attackingStatus(int s)
    {
        return (s >= kStatusAttack && s <= kStatusAttackAirFirst) || (s >= kStatusSpecialN && s < kStatusFinal)
               || s == kStatusThrow;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    void StatusWatch::notifyEventChangeStatus(int statusKind, int prevStatusKind, soStatusData*,
                                              soModuleAccesser* moduleAccesser)
    {
        Slot* s = slot;
        if (s == NULL || !s->registered || moduleAccesser != s->accesser)
        {
            return;
        }
        s->statusEvents++;
        StatusEvent e;
        const soModuleEnumeration* m = moduleAccesser->m_enumerationStart;
        e.status = static_cast<s16>(statusKind);
        e.prev = static_cast<s16>(prevStatusKind);
        e.order = s->changes;
        e.airborne = 0;
        e.flags = 0;
        e.lCancel = 0;
        e.motionKind = -1;
        e.motionFrame = 0;
        if (isRam(m))
        {
            s32 lc;
            readWorkValues(m, e.flags, lc);
            e.lCancel = static_cast<s16>(lc);
            e.airborne = isRam(m->m_situationModule) && m->m_situationModule->getKind() == Situation_Air ? 1 : 0;
        }
        MotionInfo mi;
        if (readMotion(s->t.fighter, mi))
        {
            e.motionKind = static_cast<s16>(mi.kind);
            e.motionFrame = static_cast<s16>(mi.frame);
        }
        if (s->changes < kMaxEvents)
        {
            s->events[s->changes] = e;
        }
        if (s->changes < 255)
        {
            s->changes++;
        }
        s->lastPrev = static_cast<s16>(prevStatusKind);
        s->cancelOpen = false;
        if (entersActionable(statusKind, prevStatusKind))
        {
            s->actionable = 1;
        }
        else if (landingStatus(statusKind))
        {
            // Entering landing lag (0x16..0x19) ends any airborne actionability
            // from earlier in the same frame (e.g. a 0-air-frame horizontal
            // waveland EscapeAir -> LandingFallSpecial, P+ AirDodge.asm:286-305).
            s->actionable = 0;
        }
        if (s_statusListener != NULL && s_slots != NULL)
        {
            s_statusListener(static_cast<int>(s - s_slots), e);
        }
    }

    bool AnimWatch::isObserv(char)
    {
        return true;
    }

    bool AnimWatch::notifyEventAnimCmd(acAnimCmd* acmd, soModuleAccesser* moduleAccesser, s32)
    {
        Slot* s = slot;
        if (s == NULL || !s->registered || moduleAccesser != s->accesser || !isRam(acmd))
        {
            return false;
        }
        const acAnimCmdConv* cmd = acmd->getCmdAddress();
        if (!isRam(cmd))
        {
            return false;
        }
        s->animEvents++;
        if (cmd->group == kCmdGroupStatus && (cmd->type == kCmdEnableGroup || cmd->type == kCmdDisableGroup)
            && cmd->argNum > 0 && isRam(cmd->args))
        {
            s32 group = cmd->args[0].data;
            if (cmd->type == kCmdEnableGroup && group > 0 && group < kGroupCount)
            {
                s->groupsOn |= 1u << group;
            }
        }
        else if (static_cast<u8>(cmd->group) == kCmdGroupCancel && cmd->type == kCmdEnableCancel)
        {
            s->cancelOpen = true;
        }
        return false;
    }

    static soEventManageModule* eventModule(Fighter* f)
    {
        const soModuleEnumeration* m = fighterModules(f);
        return m != NULL && isRam(m->m_eventManageModule) ? m->m_eventManageModule : NULL;
    }

    // What the manager reports for a unit, for the log only: the size call
    // returned 32 and 40 (the units' list sizes) and the capacity call 1
    // before and after our registration, so neither counts observers.
    static int unitCall(soEventManager* manager, int unit, bool capacity)
    {
        if (manager == NULL || unit < 0)
        {
            return -1;
        }
        return static_cast<int>(capacity ? manager->getObserverCapacity(unit) : manager->getObserverSize(unit));
    }

    static bool registerSlot(Slot& s, Fighter* f)
    {
        soEventManageModule* em = eventModule(f);
        if (em == NULL)
        {
            return false;
        }
        s16 manageId = em->getManageId();
        soStatusEventObserver* fs = static_cast<soStatusEventObserver*>(f);
        soAnimCmdEventObserver* fa = static_cast<soAnimCmdEventObserver*>(static_cast<StageObject*>(f));
        // The fighter's own observers must be on this manager; otherwise
        // the layout is not what we expect and nothing is registered.
        if (manageId < 0 || fs->m_manageID != manageId || fa->m_manageID != manageId || fs->m_unitID < 0
            || fa->m_unitID < 0 || fs->m_sendID < 0 || fa->m_sendID < 0)
        {
            TM_LOG("tracker: entry %x: unexpected observers (manage %d; status %d/%d/%d, anim %d/%d/%d); not tracked\n",
                   s.t.entryId, manageId, fs->m_manageID, fs->m_unitID, fs->m_sendID, fa->m_manageID, fa->m_unitID,
                   fa->m_sendID);
            return false;
        }
        soEventManager* manager = soEventSystem::getInstance()->getManager(manageId);
        int statusBefore = unitCall(manager, fs->m_unitID, true);
        int animBefore = unitCall(manager, fa->m_unitID, true);
        s.status.slot = &s;
        s.anim.slot = &s;
        s.status.m_unitID = fs->m_unitID;
        s.anim.m_unitID = fa->m_unitID;
        s.status.soStatusEventObserver::addObserver(manageId, -1);
        // The last argument is the list attribute: with -1 (what status
        // observers get) this unit delivered no command at all; with 0 it
        // delivers them (observed; 1, 2 and 46 worked too).
        s.anim.soAnimCmdEventObserver::addObserver(manageId, 0);
        bool ok = s.status.m_manageID == manageId && s.status.m_sendID >= 0 && s.anim.m_manageID == manageId
                  && s.anim.m_sendID >= 0;
        if (!s.everRegistered || !ok)
        {
            TM_LOG("tracker: P%d%s entry %x observers %s: status unit %d send %d (%d -> %d, %d), anim unit %d "
                   "send %d (%d -> %d, %d), manage %d\n",
                   s.t.playerNo + 1, s.t.partner ? " partner" : "", s.t.entryId, ok ? "on" : "FAILED",
                   s.status.m_unitID, s.status.m_sendID, statusBefore, unitCall(manager, fs->m_unitID, true),
                   unitCall(manager, fs->m_unitID, false), s.anim.m_unitID, s.anim.m_sendID, animBefore,
                   unitCall(manager, fa->m_unitID, true), unitCall(manager, fa->m_unitID, false), manageId);
            s.firstStatusSend = s.status.m_sendID;
            s.firstAnimSend = s.anim.m_sendID;
        }
        else if (s.status.m_sendID != s.firstStatusSend || s.anim.m_sendID != s.firstAnimSend)
        {
            // Removal frees the list slot it held, so the same slot comes
            // back every time; another one means a removal did not happen.
            s_sendChanges++;
        }
        s.everRegistered = true;
        s.manageId = manageId;
        s.manager = manager;
        s.accesser = f->m_moduleAccesser;
        s.registered = true; // also when half failed, so removal takes off what took
        if (ok)
        {
            s_registrations++;
        }
        return ok;
    }

    // True if the fighter that owns the slot is still in its ftEntry and its
    // event manager is still the same live object in soEventSystem.
    static bool managerAlive(const Slot& s)
    {
        soEventSystem* system = soEventSystem::getInstance();
        if (s.manageId < 0 || !system->getInstanceManager().isContain(s.manageId)
            || system->getManager(s.manageId) != s.manager)
        {
            return false;
        }
        ftEntryManager* entries = g_ftEntryManager;
        if (!isRam(entries) || !isRam(s.t.fighter))
        {
            return false;
        }
        ftEntry* entry = entries->getEntity(s.t.entryId);
        if (!isRam(entry) || entry->m_entryId != s.t.entryId)
        {
            return false;
        }
        bool owns = false;
        for (int k = 0; k < 4; k++)
        {
            if (entry->m_instances[k].m_fighter == s.t.fighter)
            {
                owns = true;
                break;
            }
        }
        if (!owns && isRam(g_ftManager) && g_ftManager->getSubFighter(s.t.entryId) == s.t.fighter)
        {
            owns = true;
        }
        if (!owns || s.t.fighter->m_taskId != s.t.taskId)
        {
            return false;
        }
        soEventManageModule* em = eventModule(s.t.fighter);
        return em != NULL && em->getManageId() == s.manageId;
    }

    static void registerRemoveWatch()
    {
        if (s_removeRegistered)
        {
            return;
        }
        ftManager* fm = g_ftManager;
        if (!isRam(fm))
        {
            return;
        }
        s16 manageId = fm->m_eventManageModule.getManageId();
        ftOutsideEventObserver* fo = static_cast<ftOutsideEventObserver*>(fm);
        soEventSystem* system = soEventSystem::getInstance();
        if (manageId < 0 || fo->m_manageID != manageId || fo->m_unitID < 0
            || !system->getInstanceManager().isContain(manageId))
        {
            return;
        }
        soEventManager* manager = system->getManager(manageId);
        s_removeWatch.m_unitID = fo->m_unitID;
        s_removeWatch.ftOutsideEventObserver::addObserver(manageId, -1);
        bool ok = s_removeWatch.m_manageID == manageId && s_removeWatch.m_sendID >= 0;
        if (!s_removeLogged || !ok)
        {
            TM_LOG("tracker: entry-remove observer %s: unit %d send %d, manage %d\n", ok ? "on" : "FAILED",
                   s_removeWatch.m_unitID, s_removeWatch.m_sendID, manageId);
        }
        s_removeLogged = true;
        if (ok)
        {
            s_removeManageId = manageId;
            s_removeManager = manager;
            s_removeRegistered = true;
        }
    }

    static void unregisterRemoveWatch(const char* why, bool loud)
    {
        if (!s_removeRegistered)
        {
            return;
        }
        soEventSystem* system = soEventSystem::getInstance();
        bool alive = s_removeManageId >= 0 && system->getInstanceManager().isContain(s_removeManageId)
                     && system->getManager(s_removeManageId) == s_removeManager && isRam(g_ftManager)
                     && g_ftManager->m_eventManageModule.getManageId() == s_removeManageId;
        if (alive && s_removeWatch.m_sendID >= 0)
        {
            s_removeWatch.removeObserver(s_removeWatch.m_manageID);
            if (loud)
            {
                TM_LOG("tracker: entry-remove observer off (%s)\n", why);
            }
        }
        else
        {
            s_removeWatch.m_manageID = -1;
            s_removeWatch.m_sendID = -1;
        }
        s_removeRegistered = false;
        s_removeManager = NULL;
        s_removeManageId = -1;
    }

    // Takes our observers off the fighter's units. `loud` logs the counts.
    static void unregisterSlot(Slot& s, const char* why, bool loud)
    {
        if (!s.registered)
        {
            return;
        }
        if (managerAlive(s))
        {
            int statusBefore = unitCall(s.manager, s.status.m_unitID, true);
            int animBefore = unitCall(s.manager, s.anim.m_unitID, true);
            if (s.status.m_sendID >= 0)
            {
                s.status.removeObserver(s.status.m_manageID);
            }
            if (s.anim.m_sendID >= 0)
            {
                s.anim.removeObserver(s.anim.m_manageID);
            }
            if (loud)
            {
                TM_LOG("tracker: P%d%s entry %x observers off (%s): status %d -> %d, anim %d -> %d\n",
                       s.t.playerNo + 1, s.t.partner ? " partner" : "", s.t.entryId, why, statusBefore,
                       unitCall(s.manager, s.status.m_unitID, true), animBefore,
                       unitCall(s.manager, s.anim.m_unitID, true));
            }
            s_removals++;
        }
        else
        {
            // The fighter and its event units are gone with our observers
            // in them; there is nothing left to take them off.
            TM_LOG("tracker: P%d%s entry %x gone while observed (%s)\n", s.t.playerNo + 1,
                   s.t.partner ? " partner" : "", s.t.entryId, why);
            s_lost++;
            s.status.m_manageID = -1;
            s.status.m_sendID = -1;
            s.anim.m_manageID = -1;
            s.anim.m_sendID = -1;
        }
        s.registered = false;
        s.accesser = NULL;
        s.manager = NULL;
    }

    static void resetFrame(FighterFrame& f)
    {
        memset(&f, 0, sizeof(f));
        f.frame = kNoFrame;
        f.status = -1;
        f.prevStatus = -1;
    }

    static void clearSlot(Slot& s)
    {
        Tracked& t = s.t;
        t.used = false;
        t.fighter = NULL;
        t.entryId = -1;
        resetFrame(t.cur);
        resetFrame(t.prev);
        t.statusStart = kNoFrame;
        t.actionableSince = kNoFrame;
        t.busySince = kNoFrame;
        for (int g = 0; g < 32; g++)
        {
            t.groupSince[g] = kNoFrame;
        }
        t.lastDealt.other = -1;
        t.lastDealt.how = kByNobody;
        t.lastDealt.frame = kNoFrame;
        t.lastReceived.other = -1;
        t.lastReceived.how = kByNobody;
        t.lastReceived.frame = kNoFrame;
        s.changes = 0;
        s.actionable = 0;
        s.groupsOn = 0;
        s.cancelOpen = false;
        s.lastPrev = -1;
        s.leaving = false;
        t.eventCount = 0;
        t.eventsLost = 0;
        s.manageId = -1;
        s.everRegistered = false;
        s.statusEvents = 0;
        s.animEvents = 0;
    }

    void RemoveWatch::notifyEventRemoveEntry(int entryId)
    {
        if (!s_removeRegistered || s_slots == NULL)
        {
            return;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (s_slots[i].t.used && s_slots[i].t.entryId == entryId)
            {
                unregisterSlot(s_slots[i], "entry removed", true);
                clearSlot(s_slots[i]);
            }
        }
    }

    static int findSlot(int entryId, bool partner)
    {
        if (s_slots == NULL)
        {
            return -1;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (s_slots[i].t.used && s_slots[i].t.entryId == entryId && s_slots[i].t.partner == partner)
            {
                return i;
            }
        }
        return -1;
    }

    static int freeSlot()
    {
        if (s_slots == NULL)
        {
            return -1;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (!s_slots[i].t.used)
            {
                return i;
            }
        }
        return -1;
    }

    // Makes sure the fighter has a slot, and while `live` that its
    // observers are on.
    static void track(const FighterRef& ref, Fighter* f, bool partner, bool live, bool* seen)
    {
        int i = findSlot(ref.entryId, partner);
        if (i >= 0 && (s_slots[i].t.fighter != f || s_slots[i].t.taskId != f->m_taskId))
        {
            // A transform switched the entry to another instance; the old one
            // stays (deactivated) in the entry, so its observers come off.
            unregisterSlot(s_slots[i], "instance changed", true);
            clearSlot(s_slots[i]);
            i = -1;
        }
        if (i < 0)
        {
            i = freeSlot();
            if (i < 0)
            {
                return;
            }
            Slot& s = s_slots[i];
            clearSlot(s);
            s.t.used = true;
            s.t.partner = partner;
            s.t.entryId = ref.entryId;
            s.t.playerNo = static_cast<s8>(ref.playerNo);
            s.t.cpu = ref.cpu;
            s.t.fighter = f;
            s.t.taskId = f->m_taskId;
        }
        Slot& s = s_slots[i];
        // Training's CPU option "Control" hands the CPU's fighter to a
        // controller (and back): who operates it is read every frame.
        s.t.cpu = ref.cpu;
        // A fighter the setup no longer lists is about to be removed, with
        // whatever observes it: it is not observed again.
        bool leaving = !playerInMatch(ref.playerNo);
        if (leaving && !s.leaving)
        {
            TM_LOG("tracker: P%d%s entry %x leaving the match\n", s.t.playerNo + 1, partner ? " partner" : "",
                   s.t.entryId);
        }
        s.leaving = leaving;
        if (leaving)
        {
            unregisterSlot(s, "leaving", true);
        }
        else if (live && !s.registered && !registerSlot(s, f))
        {
            s_healthy = false;
        }
        seen[i] = true;
    }

    static void sync(bool live)
    {
        if (s_slots == NULL)
        {
            return;
        }
        bool seen[kMaxTracked];
        for (int i = 0; i < kMaxTracked; i++)
        {
            seen[i] = false;
            if (!live)
            {
                unregisterSlot(s_slots[i], "paused", false);
            }
        }
        if (live)
        {
            registerRemoveWatch();
        }
        else
        {
            unregisterRemoveWatch("paused", false);
        }
        FighterRef refs[kMaxFighters];
        int n = listFighters(refs, kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            track(refs[i], refs[i].fighter, false, live, seen);
            if (refs[i].partner != NULL && fighterModules(refs[i].partner) != NULL)
            {
                track(refs[i], refs[i].partner, true, live, seen);
            }
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (s_slots[i].t.used && !seen[i])
            {
                unregisterSlot(s_slots[i], "left the match", true);
                clearSlot(s_slots[i]);
            }
        }
    }

    static int hitStop(const soModuleEnumeration* m)
    {
        if (!isRam(m->m_stopModule))
        {
            return 0;
        }
        int v = static_cast<soStopModuleImpl*>(m->m_stopModule)->getHitStopRealFrame();
        return v > 0 ? v : 0;
    }

    static void finishFrame(Slot& s)
    {
        Tracked& t = s.t;
        const soModuleEnumeration* m = fighterModules(t.fighter);
        if (m == NULL || !isRam(m->m_statusModule) || !isRam(m->m_situationModule))
        {
            return;
        }
        FighterFrame f;
        resetFrame(f);
        f.frame = s_frame;
        f.status = static_cast<s16>(fighterStatus(m));
        f.prevStatus = s.changes > 0 ? s.lastPrev : t.cur.prevStatus;
        f.statusChanges = s.changes;
        f.hitlag = static_cast<s16>(hitStop(m));
        f.hitstun = static_cast<s16>(fighterHitstun(m));
        f.shieldStun = static_cast<s16>(fighterShieldStun(m, f.status));
        f.airborne = m->m_situationModule->getKind() == Situation_Air ? 1 : 0;
        soStatusModule* status = m->m_statusModule;
        for (int g = 1; g < kGroupCount; g++)
        {
            if (status->isEnableTransitionTermGroup(g))
            {
                f.groups |= 1u << g;
            }
        }
        f.groupsEnabled = s.groupsOn;
        f.cancel = s.cancelOpen ? 1 : 0;
        if (isRam(m->m_collisionAttackModule))
        {
            const soCollisionAttackModuleImpl* atk =
                static_cast<const soCollisionAttackModuleImpl*>(m->m_collisionAttackModule);
            f.attackActive = atk->m_isInflict != 0 ? 1 : 0;
        }
        // A free or chosen status entered during the frame counts even if a
        // hit put the fighter in hitlag later in that frame (seen: a CPU
        // raising its shield on the frame an aerial reached it).
        bool stunned = f.hitlag > 0 || f.hitstun > 0 || f.status == kStatusGuardDamage;
        u32 actionGroups = f.airborne ? kAirActionGroups : kGroundActionGroups;
        bool byState = (f.groups & actionGroups) != 0 || f.cancel != 0 || (s.changes == 0 && freeStatus(f.status));
        f.actionable = s.actionable != 0 || (!stunned && byState) ? 1 : 0;

        bool first = t.cur.frame == kNoFrame;
        // Every hit changes the defender's status into a damage status,
        // also into the one it is in (observed: Damage_Air -> Damage_Air on
        // a second jab, Landing -> Damage_Air on a hit that gave no
        // hitstun), so hits are counted from the changes; stun going up
        // catches anything they miss.
        int kept = s.changes < kMaxEvents ? s.changes : kMaxEvents;
        for (int k = 0; k < kept; k++)
        {
            if (damageStatus(s.events[k].status) && f.hits < 255)
            {
                f.hits++;
            }
        }
        if (f.hits == 0 && !first && (f.hitstun > t.cur.hitstun || (f.hitstun > 0 && f.hitlag > t.cur.hitlag)))
        {
            f.hits = 1;
        }
        f.hitReceived = f.hits > 0 ? 1 : 0;
        f.shieldReceived = !first && f.status == kStatusGuardDamage
                                   && (t.cur.status != kStatusGuardDamage || f.shieldStun > t.cur.shieldStun
                                       || f.hitlag > t.cur.hitlag)
                               ? 1
                               : 0;

        if (first || s.changes > 0 || f.status != t.cur.status)
        {
            t.statusStart = s_frame;
        }
        if (f.actionable)
        {
            if (t.actionableSince == kNoFrame)
            {
                t.actionableSince = s_frame;
            }
            t.busySince = kNoFrame;
        }
        else
        {
            if (t.busySince == kNoFrame)
            {
                t.busySince = s_frame;
            }
            t.actionableSince = kNoFrame;
        }
        for (int g = 1; g < kGroupCount; g++)
        {
            bool on = (f.groups & (1u << g)) != 0;
            if (!on)
            {
                t.groupSince[g] = kNoFrame;
            }
            else if (t.groupSince[g] == kNoFrame)
            {
                t.groupSince[g] = s_frame;
            }
        }
        t.eventCount = s.changes < kMaxEvents ? s.changes : static_cast<u8>(kMaxEvents);
        t.eventsLost = static_cast<u8>(s.changes - t.eventCount);
        for (int k = 0; k < t.eventCount; k++)
        {
            t.events[k] = s.events[k];
        }
        if (t.eventsLost > 0 && s_eventsLost++ < 8)
        {
            TM_LOG("tracker: P%d%s frame %d: %d status changes, %d not kept\n", t.playerNo + 1,
                   t.partner ? " partner" : "", s_frame, s.changes, t.eventsLost);
        }
        t.prev = t.cur;
        t.cur = f;
        s.changes = 0;
        s.actionable = 0;
        s.groupsOn = 0;
    }

    // Who hit whom this frame: a defender whose hitstun or shield stun went
    // up was hit by a fighter whose attack connected this frame (the first
    // in slot order if several did; not verified with more than two
    // fighters), else, if there is just one other main fighter, by that one
    // (a projectile's hit, whose fighter's attack did not connect).
    static void attributeHits()
    {
        if (s_slots == NULL)
        {
            return;
        }
        for (int d = 0; d < kMaxTracked; d++)
        {
            Tracked& def = s_slots[d].t;
            if (!def.used || !(def.cur.hitReceived || def.cur.shieldReceived))
            {
                continue;
            }
            int attacker = -1;
            int how = kByInflict;
            int others = 0;
            int lastOther = -1;
            for (int a = 0; a < kMaxTracked; a++)
            {
                const Tracked& at = s_slots[a].t;
                if (a == d || !at.used || at.entryId == def.entryId)
                {
                    continue;
                }
                if (!at.partner)
                {
                    others++;
                    lastOther = a;
                }
                if (attacker < 0 && at.cur.attackActive)
                {
                    attacker = a;
                }
            }
            if (attacker < 0 && others == 1)
            {
                attacker = lastOther;
                how = kByElimination;
            }
            def.lastReceived.other = static_cast<s8>(attacker);
            def.lastReceived.how = static_cast<u8>(attacker >= 0 ? how : kByNobody);
            def.lastReceived.shield = def.cur.shieldReceived;
            def.lastReceived.frame = s_frame;
            if (attacker >= 0)
            {
                HitRecord& r = s_slots[attacker].t.lastDealt;
                r.other = static_cast<s8>(d);
                r.how = static_cast<u8>(how);
                r.shield = def.cur.shieldReceived;
                r.frame = s_frame;
            }
        }
    }

    // One line per tracked fighter per frame while g_trackerTrace is set
    // (scripts/display_tests.py); `at`: the callback that handed it over.
    static void trace(const char* at)
    {
        if (s_slots == NULL)
        {
            return;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            const Tracked& t = s_slots[i].t;
            if (!t.used)
            {
                continue;
            }
            const FighterFrame& f = t.cur;
            TM_LOG("trace f=%d p=%d%s st=%x pv=%x ch=%d sf=%d hl=%d hs=%d ss=%d air=%d act=%d grp=%x can=%d inf=%d "
                   "hit=%d shd=%d at=%s\n",
                   f.frame, t.playerNo + 1, t.partner ? "b" : "", f.status, f.prevStatus, f.statusChanges,
                   f.frame - t.statusStart + 1, f.hitlag, f.hitstun, f.shieldStun, f.airborne, f.actionable, f.groups,
                   f.cancel, f.attackActive, f.hitReceived, f.shieldReceived, at);
            for (int k = 0; k < t.eventCount; k++)
            {
                const StatusEvent& e = t.events[k];
                TM_LOG("trace ev f=%d p=%d%s #%d %x<-%x mot=%x/%d air=%d raf=%08x lc=%d\n", f.frame,
                       t.playerNo + 1, t.partner ? "b" : "", e.order, e.status, e.prev, e.motionKind,
                       e.motionFrame, e.airborne, e.flags, e.lCancel);
            }
        }
    }

    static void countOwed()
    {
        if (s_owed)
        {
            s_owed = false;
            s_frame++;
        }
    }

    // s_pending is cleared before the listener runs, so nothing the listener
    // calls can hand the same frame over again.
    static void handOver(bool render)
    {
        countOwed();
        if (!s_pending || s_slots == NULL)
        {
            return;
        }
        s_pending = false;
        bool any = false;
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (s_slots[i].t.used && s_slots[i].registered)
            {
                finishFrame(s_slots[i]);
                any = true;
            }
        }
        if (any)
        {
            attributeHits();
            if (g_trackerTrace)
            {
                trace(render ? "render" : "pad");
            }
            if (s_listener != NULL)
            {
                s_listener();
            }
            if (render)
            {
                s_owed = true;
            }
            else
            {
                s_frame++;
            }
        }
    }

    void trackerHandOver()
    {
        handOver(true);
    }

    void trackerStep(bool live)
    {
        handOver(false);
        sync(live);
    }

    void trackerStepRuns(bool runs)
    {
        s_pending = runs;
        s_lastRan = runs;
    }

    bool trackerFramePending()
    {
        return s_pending;
    }

    bool trackerFrameOwed()
    {
        return s_owed;
    }

    void trackerAttach()
    {
        s_frame = 0;
        s_pending = false;
        s_lastRan = false;
        s_owed = false;
        s_healthy = true;
        s_registrations = 0;
        s_removals = 0;
        s_lost = 0;
        s_sendChanges = 0;
        s_eventsLost = 0;
        s_removeRegistered = false;
        s_removeLogged = false;
        s_removeManager = NULL;
        s_removeManageId = -1;
        s_removeWatch.m_manageID = -1;
        s_removeWatch.m_sendID = -1;
        g_trackerTrace = 0;
        TM_LOG("tracker: trace byte at %08x\n", &g_trackerTrace);
#ifdef __MWERKS__
        if (s_slots == NULL)
        {
            s_slots = static_cast<Slot*>(allocNetworkZero(static_cast<u32>(sizeof(Slot) * kMaxTracked)));
            if (s_slots != NULL)
            {
                for (int i = 0; i < kMaxTracked; i++)
                {
                    new (&s_slots[i]) Slot;
                }
            }
        }
        if (s_slots == NULL)
        {
            s_healthy = false;
            TM_LOG("tracker: slots alloc failed (%u B in Network)\n",
                   static_cast<unsigned>(sizeof(Slot) * kMaxTracked));
            return;
        }
#endif
        for (int i = 0; i < kMaxTracked; i++)
        {
            s_slots[i].registered = false;
            clearSlot(s_slots[i]);
        }
    }

    void trackerDetach()
    {
        if (s_slots != NULL)
        {
            for (int i = 0; i < kMaxTracked; i++)
            {
                if (s_slots[i].t.used)
                {
                    unregisterSlot(s_slots[i], "match ending", true);
                    clearSlot(s_slots[i]);
                }
            }
        }
        unregisterRemoveWatch("match ending", true);
#ifdef __MWERKS__
        if (s_slots != NULL)
        {
            free(s_slots);
            s_slots = NULL;
        }
#endif
        countOwed();
        TM_LOG("tracker: %u registrations, %u removals, %u lost, %u moved, %d frames, %u with changes not kept\n",
               s_registrations, s_removals, s_lost, s_sendChanges, s_frame, s_eventsLost);
        s_listener = NULL;
        s_statusListener = NULL;
        s_pending = false;
        s_lastRan = false;
        s_owed = false;
    }

    void trackerSuspend()
    {
        if (s_slots != NULL)
        {
            for (int i = 0; i < kMaxTracked; i++)
            {
                unregisterSlot(s_slots[i], "savestate", false);
            }
        }
        unregisterRemoveWatch("savestate", false);
    }

    void trackerResume(bool forget)
    {
        if (s_slots == NULL)
        {
            return;
        }
        if (forget)
        {
            for (int i = 0; i < kMaxTracked; i++)
            {
                if (s_slots[i].t.used)
                {
                    clearSlot(s_slots[i]);
                }
            }
            // The loaded state is a frame to hand over when the step before
            // the load was simulated.
            s_pending = s_pending || s_lastRan;
        }
        sync(true);
    }

    s32 trackerFrame()
    {
        return s_frame;
    }

    int trackedCount()
    {
        if (s_slots == NULL)
        {
            return 0;
        }
        int n = 0;
        for (int i = 0; i < kMaxTracked; i++)
        {
            n += s_slots[i].t.used ? 1 : 0;
        }
        return n;
    }

    const Tracked* tracked(int i)
    {
        return s_slots != NULL && i >= 0 && i < kMaxTracked && s_slots[i].t.used ? &s_slots[i].t : NULL;
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    const Tracked* trackedEntry(int entryId)
    {
        int i = findSlot(entryId, false);
        return i >= 0 ? &s_slots[i].t : NULL;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    int trackedIndex(const Tracked* t)
    {
        if (s_slots == NULL)
        {
            return -1;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (&s_slots[i].t == t)
            {
                return i;
            }
        }
        return -1;
    }

    bool trackerHealthy()
    {
        return s_healthy;
    }

    void setFrameListener(FrameListener listener)
    {
        s_listener = listener;
    }

    void setStatusListener(StatusListener listener)
    {
        s_statusListener = listener;
    }
}
}
