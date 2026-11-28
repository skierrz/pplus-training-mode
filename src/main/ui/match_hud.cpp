#include "main/ui/match_hud.h"

#include "main/services.h"
#include "main/ui/pages/scenarios.h"
#include "shared/drill_session.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// The centre HUD is IfMngr+0x44 (0x800DBFE0), made for every match by
// 0x800DD528 / 0x800DD6E4 from heap 40. RSBE01 rev 2 as Project+ 3.2.1 runs it.
namespace tm {
namespace ui {

    enum {
        kIfMngr = 0x805A02D0,
        kMngrKind = 0x4,     // 5 in a Training match (0x800DB944: info_training)
        kMngrCentre = 0x44,
        kMngrPlayers = 0x4C, // IfPlayer* [4] (0x809625D4-0x809625EC)
        kMngrPlayerSlots = 0x68, // u8 slot[4] mapping playerNo -> IfPlayer index
        kIfPlayerParts = 0x28,   // MuObject* [50] (0x800E1408-0x800E1440)
        kIfPlayerPartCount = 50,
        kIfPlayerGroups = 0xF0,  // ScnGroup* [3] roots (0x800E13D8-0x800E1404)
        kIfPlayerGroupCount = 3,
        kScnObjFlags = 0xCC,     // OPTID_DISABLE_GATHER bit 0x10 (0x801AAE64)
        kScnObjDisableGather = 0x10,
        kKindTraining = 5,
        kFlags = 0x0,
        kState = 0x4,        // IfCenter state: 0=idle, 1=Ready, 2=Count, 3=GO! (0x800DE360)
        kTimerOn = 0x80,     // set time returns at once without it (0x800DDD24-30)
        kNoCountdown = 0x40, // without it, 300 frames or less shows the countdown (0x800DDD34-64)
        kGroup = 0x8,        // the HUD's scene group; the timer prefers +0xC's (0x800DDD78-AC)
        kTimerGroup = 0xC,
        kMtitleObj = 0x40,   // MuObject* InfMtitle_TopN (0x800DE698)
        kReadyObj = 0x44,    // MuObject* InfReady_TopN (0x800DE64C)
        kGoObj = 0x48,       // MuObject* InfGo_TopN (0x800DE8E4)
        kMuObjScnMdl = 0x10, // ScnMdl* inside MuObject (0x800DE66C)
        kTimerMdl = 0x154,   // made for every match (0x800DD738-54)
        kGoFollows = 0x16C,  // u8 flag: transition from Ready -> GO! (0x800DE760)
        kMdlParent = 0x4,    // non-zero while in a group (0x800DDD6C, 0x800DE20C)
        kGroupRemove = 0x3C, // the group's vtable: take a model out (0x800DDD8C, 0x800DF510)
        kTimerSetup = 0x800DDB54, // (centre); allocates nothing
        kSetTime = 0x800DDCF8     // (centre, frames)
    };

    typedef void (*CentreFn)(void* centre);
    typedef void (*CentreValueFn)(void* centre, s32 value);
    typedef void (*RemoveFn)(void* group, void* model);

    // Asked for since the last render call.
    static bool s_wantTimer = false;
    static s32 s_frames = 0;
    static u8 s_wantHideMask = 0;
    static u8 s_hiddenMask = 0;

    // The centre as first seen in this match. Savestates restore its flags
    // but not this module's data, so who owns the timer is decided once, from
    // the flags before we touch them.
    static u8* s_centre = NULL;
    static u8 s_firstFlags = 0;
    static bool s_loggedReadySuppress = false;

    static u8* trainingManager()
    {
        u8* manager = *reinterpret_cast<u8**>(kIfMngr);
        if (!game::isRam(manager) || game::wordAt(manager, kMngrKind) != kKindTraining)
        {
            return NULL;
        }
        return manager;
    }

    static u8* trainingCentre()
    {
        u8* manager = trainingManager();
        if (manager == NULL)
        {
            return NULL;
        }
        u8* centre = reinterpret_cast<u8*>(game::wordAt(manager, kMngrCentre));
        return game::isRam(centre) ? centre : NULL;
    }

    static void setScnObjGatherHidden(u8* scnObj, bool hide)
    {
        if (!game::isRam(scnObj))
        {
            return;
        }
        u32* flags = reinterpret_cast<u32*>(scnObj + kScnObjFlags);
        if (hide)
        {
            *flags |= kScnObjDisableGather;
        }
        else
        {
            *flags &= ~static_cast<u32>(kScnObjDisableGather);
        }
    }

    static void applyPlayerDamageHudHidden(u8* manager, int playerNo, bool hide)
    {
        if (!game::isRam(manager) || playerNo < 0 || playerNo >= 4)
        {
            return;
        }
        u8 slot = game::byteAt(manager, kMngrPlayerSlots + static_cast<u32>(playerNo));
        if (slot >= 4u)
        {
            return;
        }
        u8* ifPlayer = reinterpret_cast<u8*>(game::wordAt(manager, kMngrPlayers + static_cast<u32>(slot) * 4u));
        if (!game::isRam(ifPlayer))
        {
            return;
        }
        for (int i = 0; i < kIfPlayerPartCount; i++)
        {
            u8* part = reinterpret_cast<u8*>(game::wordAt(ifPlayer, kIfPlayerParts + static_cast<u32>(i) * 4u));
            if (game::isRam(part))
            {
                u8* scn = reinterpret_cast<u8*>(game::wordAt(part, kMuObjScnMdl));
                setScnObjGatherHidden(scn, hide);
            }
        }
        for (int g = 0; g < kIfPlayerGroupCount; g++)
        {
            u8* grp = reinterpret_cast<u8*>(game::wordAt(ifPlayer, kIfPlayerGroups + static_cast<u32>(g) * 4u));
            setScnObjGatherHidden(grp, hide);
        }
    }

    static bool removeModelPtr(u8* centre, u32 groupAt, void* mdl)
    {
        void* group = reinterpret_cast<void*>(game::wordAt(centre, groupAt));
        if (!game::isRam(group) || !game::isRam(mdl) || game::wordAt(mdl, kMdlParent) == 0)
        {
            return false;
        }
        u32 vtable = game::wordAt(group, 0);
        reinterpret_cast<RemoveFn>(game::wordAt(reinterpret_cast<void*>(vtable), kGroupRemove))(group, mdl);
        return true;
    }

    static void removeFrom(u8* centre, u32 groupAt, u32 mdlAt)
    {
        void* mdl = reinterpret_cast<void*>(game::wordAt(centre, mdlAt));
        removeModelPtr(centre, groupAt, mdl);
    }

    static bool removeMuObject(u8* centre, u32 objAt)
    {
        void* muObj = reinterpret_cast<void*>(game::wordAt(centre, objAt));
        if (!game::isRam(muObj))
        {
            return false;
        }
        void* mdl = reinterpret_cast<void*>(game::wordAt(muObj, kMuObjScnMdl));
        return removeModelPtr(centre, kGroup, mdl);
    }

    static bool shouldSuppressReadyGo()
    {
        const drills::Session* ds = drills::sessionIn(main::services().settings());
        if (ds != NULL)
        {
            // Focused Tech Practice drills & challenges (1..15). Leave normal
            // Training Lab (0) and Break the Targets! (17) countdown intact.
            if (ds->drill >= drills::kDrillLedgedash && ds->drill <= drills::kDrillSnakeRecovery)
            {
                return true;
            }
            if ((ds->pendingScenario & 0x7Fu) != 0u)
            {
                return true;
            }
        }
        const pages::ScenarioMenuState* sm = pages::scenarioMenuState();
        return sm != NULL && sm->preset != 0u;
    }

    static void suppressReadyGo(u8* centre)
    {
        u32* statePtr = reinterpret_cast<u32*>(centre + kState);
        u32 st = *statePtr;
        bool removed = false;
        removed |= removeMuObject(centre, kMtitleObj);
        removed |= removeMuObject(centre, kReadyObj);
        removed |= removeMuObject(centre, kGoObj);
        if (st == 1u || st == 3u)
        {
            centre[kGoFollows] = 0;
            *statePtr = 0u;
            removed = true;
        }
        if (removed && !s_loggedReadySuppress)
        {
            s_loggedReadySuppress = true;
            TM_LOG("match hud: suppressed Ready/GO banner for drill/scenario\n");
        }
    }

    static void applyTimer(u8* centre)
    {
        // A match with its own timer keeps it (never so in Training so far).
        if (s_firstFlags & kTimerOn)
        {
            return;
        }
        bool on = (centre[kFlags] & kTimerOn) != 0;
        if (s_wantTimer)
        {
            if (!on)
            {
                centre[kFlags] |= kTimerOn | kNoCountdown;
                reinterpret_cast<CentreFn>(kTimerSetup)(centre);
                TM_LOG("match hud: timer shown (centre %08x flags %02x)\n", centre, centre[kFlags]);
            }
            centre[kFlags] |= kNoCountdown;
            reinterpret_cast<CentreValueFn>(kSetTime)(centre, s_frames < 0 ? 0 : s_frames);
        }
        else if (on)
        {
            centre[kFlags] = static_cast<u8>((centre[kFlags] & ~(kTimerOn | kNoCountdown))
                                             | (s_firstFlags & kNoCountdown));
            // as set time does when it shows the countdown instead
            removeFrom(centre, game::wordAt(centre, kTimerGroup) != 0 ? kTimerGroup : kGroup, kTimerMdl);
            TM_LOG("match hud: timer hidden\n");
        }
    }

    void showMatchTimer(s32 frames)
    {
        s_wantTimer = true;
        s_frames = frames;
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void hidePlayerDamageHud(int playerNo)
    {
        if (playerNo >= 0 && playerNo < 4)
        {
            s_wantHideMask = static_cast<u8>(s_wantHideMask | (1u << playerNo));
        }
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    void matchHudApply(const game::MatchState& m)
    {
        u8* manager = trainingManager();
        u8* anyCentre = trainingCentre();
        static u8* s_seenCentre = NULL;
        if (anyCentre != s_seenCentre)
        {
            s_seenCentre = anyCentre;
#ifdef __MWERKS__
            s_loggedReadySuppress = false;
#endif
        }
#ifdef __MWERKS__
        if (anyCentre != NULL && shouldSuppressReadyGo())
        {
            suppressReadyGo(anyCentre);
        }
#endif
        u8* centre = m.running ? anyCentre : NULL;
        // Latch the game's timer flags once the match actually runs (after countdown/setup),
        // matching pre-ReadyGo-suppression behaviour so game-owned timers are never misread.
        if (centre != NULL && centre != s_centre)
        {
            s_centre = centre;
            s_firstFlags = game::byteAt(centre, kFlags);
            TM_LOG("match hud: centre %08x, flags %02x%s\n", centre, s_firstFlags,
                   (s_firstFlags & kTimerOn) ? ": the game's own timer is on, left alone" : "");
        }
#ifdef __MWERKS__
        if (centre != NULL)
        {
            applyTimer(centre);
        }
#else
        (void)m;
#endif
        if (manager != NULL)
        {
            u8 activeMask = static_cast<u8>(s_wantHideMask | s_hiddenMask);
            for (int p = 0; p < 4; p++)
            {
                u8 bit = static_cast<u8>(1u << p);
                if ((activeMask & bit) != 0u)
                {
                    // Re-apply every frame while requested so Brawl animations/respawns
                    // never flash P2's 0% back, and clear immediately once unrequested.
                    applyPlayerDamageHudHidden(manager, p, (s_wantHideMask & bit) != 0u);
                }
            }
            s_hiddenMask = s_wantHideMask;
        }
        else
        {
            s_hiddenMask = 0;
        }
        s_wantTimer = false;
        s_wantHideMask = 0;
    }
}
}
