#include "main/ui/match_hud.h"

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
        kKindTraining = 5,
        kFlags = 0x0,
        kTimerOn = 0x80,     // set time returns at once without it (0x800DDD24-30)
        kNoCountdown = 0x40, // without it, 300 frames or less shows the countdown (0x800DDD34-64)
        kGroup = 0x8,        // the HUD's scene group; the timer prefers +0xC's (0x800DDD78-AC)
        kTimerGroup = 0xC,
        kTimerMdl = 0x154,   // made for every match (0x800DD738-54)
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

    // The centre as first seen in this match. Savestates restore its flags
    // but not this module's data, so who owns the timer is decided once, from
    // the flags before we touch them.
    static u8* s_centre = NULL;
    static u8 s_firstFlags = 0;

    static u8* trainingCentre()
    {
        u8* manager = *reinterpret_cast<u8**>(kIfMngr);
        if (!game::isRam(manager) || game::wordAt(manager, kMngrKind) != kKindTraining)
        {
            return NULL;
        }
        u8* centre = reinterpret_cast<u8*>(game::wordAt(manager, kMngrCentre));
        return game::isRam(centre) ? centre : NULL;
    }

    static void removeFrom(u8* centre, u32 groupAt, u32 mdlAt)
    {
        void* group = reinterpret_cast<void*>(game::wordAt(centre, groupAt));
        void* mdl = reinterpret_cast<void*>(game::wordAt(centre, mdlAt));
        if (!game::isRam(group) || !game::isRam(mdl) || game::wordAt(mdl, kMdlParent) == 0)
        {
            return;
        }
        u32 vtable = game::wordAt(group, 0);
        reinterpret_cast<RemoveFn>(game::wordAt(reinterpret_cast<void*>(vtable), kGroupRemove))(group, mdl);
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

    void matchHudApply(const game::MatchState& m)
    {
        u8* centre = m.running ? trainingCentre() : NULL;
        // Kept while the match is not running: the next match reloads this
        // module, so a centre at the same address is never taken for new.
        if (centre != NULL && centre != s_centre)
        {
            s_centre = centre;
            s_firstFlags = centre[kFlags];
            TM_LOG("match hud: centre %08x, flags %02x%s\n", centre, s_firstFlags,
                   (s_firstFlags & kTimerOn) ? ": the game's own timer is on, left alone" : "");
        }
        if (centre != NULL)
        {
            applyTimer(centre);
        }
        s_wantTimer = false;
    }
}
}
