#include "main/cpu/driver.h"

#include <string.h>

#include "main/cpu/nana_throw_core.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/heap.h"
#include "shared/log.h"

namespace tm {
namespace cpu {
    struct Slot {
        Source* source;
        u32 flags;
        Owner who;
        bool silenced;   // we cleared the AI flag and owe it back
        bool suspended;  // KO'd: taken, but not driven until it is back
        u8 savedFlag;    // the AI flag to put back
        bool nanaReady;  // `nana` holds this frame's controls for Nana
        bool nanaStill;  // and its stick is held still (holdStill)
        Frame nana;
        u32 lastButtons;
        u32 frame;       // main-input frames seen since attach
        u32 driven;      // frames driven since taken
    };

    struct DriverNet {
        Slot slots[kMaxSlots];
        Slot saved[2][kMaxSlots];
    };
#ifdef __MWERKS__
    static DriverNet* s_net = NULL;
#else
    static DriverNet s_hostNet;
    static DriverNet* s_net = &s_hostNet;
#endif
    static Claimer s_claim[2];
    static u32 s_takes = 0;
    static u32 s_releases = 0;
    static u32 s_restores = 0;
    static u32 s_drivenTotal = 0;
    static bool s_attached = false;
    static int s_keepAi = -1; // keepAi: the slot whose frame leaves the AI on
    static int s_holdStill = -1; // holdStill: the slot whose stick is not a flick

    void keepAi(int slot)
    {
        s_keepAi = slot;
    }

    void holdStill(int slot)
    {
        s_holdStill = slot;
    }

    static void clear(Slot& s)
    {
        s.source = NULL;
        s.flags = 0;
        s.who.entryId = -1;
        s.silenced = false;
        s.suspended = false;
        s.savedFlag = 0;
        s.nanaReady = false;
        s.nanaStill = false;
        s.lastButtons = 0;
        s.driven = 0;
    }

    // Puts the AI flag back if we cleared it.
    static void unsilence(Slot& s, int slot, const char* why)
    {
        if (!s.silenced)
        {
            return;
        }
        s.silenced = false;
        if (setAiFlag(slot, s.savedFlag))
        {
            s_restores++;
            TM_LOG("cpu: slot %d AI flag back to %u (%s)\n", slot, s.savedFlag, why);
        }
    }

    // Clears the AI flag for this frame. Training's menu sets it again when
    // it closes, so this runs every driven frame; a nonzero value found here
    // is what the game wants once we let go.
    static void silence(Slot& s, int slot)
    {
        u8 flag = 0;
        if (!aiFlag(slot, flag))
        {
            return;
        }
        if (!s.silenced || flag != 0)
        {
            s.savedFlag = flag;
        }
        if (!s.silenced)
        {
            TM_LOG("cpu: slot %d AI off (flag was %u)\n", slot, flag);
        }
        s.silenced = true;
        if (flag != 0)
        {
            setAiFlag(slot, 0);
        }
    }

    bool take(const Owner& who, Source* source, u32 flags)
    {
        if (s_net == NULL || who.slot < 0 || who.slot >= kMaxSlots || source == NULL || who.sub)
        {
            return false;
        }
        Slot& s = s_net->slots[who.slot];
        if (s.source != NULL)
        {
            release(who.slot, "taken by another source");
        }
        u32 frame = s.frame;
        clear(s);
        s.frame = frame;
        s.source = source;
        s.flags = flags;
        s.who = who;
        s_takes++;
        TM_LOG("cpu: take slot %d (P%d entry %x, %s), flags %x\n", who.slot, who.playerNo + 1, who.entryId,
               who.cpu ? "cpu" : "human", flags);
        return true;
    }

    void release(int slot, const char* why)
    {
        if (s_net == NULL || slot < 0 || slot >= kMaxSlots || s_net->slots[slot].source == NULL)
        {
            return;
        }
        Slot& s = s_net->slots[slot];
        unsilence(s, slot, why);
        Source* source = s.source;
        Owner who = s.who;
        TM_LOG("cpu: release slot %d after %u frames (%s)\n", slot, s.driven, why);
        u32 frame = s.frame;
        clear(s);
        s.frame = frame;
        s_releases++;
        source->released(who);
    }

    bool controlled(int slot)
    {
        return s_net != NULL && slot >= 0 && slot < kMaxSlots && s_net->slots[slot].source != NULL;
    }

    Source* sourceOf(int slot)
    {
        return controlled(slot) ? s_net->slots[slot].source : NULL;
    }

    u32 lastButtons(int slot)
    {
        return s_net != NULL && slot >= 0 && slot < kMaxSlots ? s_net->slots[slot].lastButtons : 0;
    }

    u32 frameOf(int slot)
    {
        return s_net != NULL && slot >= 0 && slot < kMaxSlots ? s_net->slots[slot].frame : 0;
    }

    static void onMain(void* input, const Owner& who)
    {
        Slot& s = s_net->slots[who.slot];
        s.frame++;
        s.nanaReady = false;
        if (s.source != NULL && s.who.entryId != who.entryId)
        {
            // The match was rebuilt (P+'s reset) and the slot holds another entry.
            release(who.slot, "entry changed");
        }
        if (s.source != NULL && !s.source->wants(who))
        {
            release(who.slot, "source done");
        }
        // The first claimer is asked every frame and may take a slot over;
        // the second only gets free slots.
        if (s_claim[0] != NULL)
        {
            s_claim[0](who);
        }
        if (s.source == NULL && s_claim[1] != NULL)
        {
            s_claim[1](who);
        }
        if (s.source == NULL)
        {
            s.lastButtons = 0;
            return;
        }
        View v;
        Fighter* f = fighterOf(who.entryId, false);
        if (f != NULL && readView(f, v) && game::outOfPlay(v.status, false))
        {
            if (!s.suspended)
            {
                s.suspended = true;
                TM_LOG("cpu: slot %d KO'd (status %x), not driven until back\n", who.slot, v.status);
                unsilence(s, who.slot, "KO");
            }
            s.lastButtons = 0;
            return;
        }
        if (s.suspended)
        {
            s.suspended = false;
            TM_LOG("cpu: slot %d back (status %x), driven again\n", who.slot, v.status);
        }
        Frame produced;
        readFrame(input, produced);
        Frame out = produced;
        s_keepAi = -1;
        s_holdStill = -1;
        bool wrote = s.source->next(who, produced, out);
        bool keep = s_keepAi == who.slot;
        bool still = wrote && s_holdStill == who.slot;
        if (!wrote || keep)
        {
            // The source lets the game have this frame, or changes what
            // the AI made of it: with the AI flag back, the AI runs from
            // the next frame on.
            unsilence(s, who.slot, "source lets the game drive");
            s.lastButtons = produced.buttons;
            if (!wrote)
            {
                return;
            }
        }
        else if (s.flags & kSilenceAi)
        {
            silence(s, who.slot);
        }
        writeFrame(input, out);
        if (still)
        {
            holdStickStill(who.entryId, false, out);
        }
        s.lastButtons = out.buttons;
        s.driven++;
        s_drivenTotal++;
        // Nana's AI made her own frame when the AI is left on.
        if ((s.flags & kWithNana) && !keep)
        {
            s.nana = out;
            s.nanaReady = true;
            s.nanaStill = still;
        }
    }

    // Nana's controller reads her input after Popo's in the same frame
    // (observed; opponent not recorded: Popo, Nana, then the others).
    static void onSub(void* input, const Owner& who)
    {
        Slot& s = s_net->slots[who.slot];
        if (s.source != NULL && s.nanaReady && s.who.entryId == who.entryId)
        {
            writeFrame(input, s.nana);
            if (s.nanaStill)
            {
                holdStickStill(who.entryId, true, s.nana);
            }
            s.nanaReady = false;
            return;
        }
        u8 mode = main::settings().fighterMechanics.nanaThrow;
        if (mode == main::kNanaThrowDefault)
        {
            return;
        }
        Fighter* nana = fighterOf(who.entryId, true);
        View v;
        if (nana == NULL || !readView(nana, v) || v.status != kNanaStatusCatchWait)
        {
            return;
        }
        Frame produced;
        readFrame(input, produced);
        Frame out = produced;
        if (applyNanaThrowOverride(mode, v.status, v.lr, produced, out))
        {
            writeFrame(input, out);
        }
    }

    void onFighterInput(const iface::FighterInput* call)
    {
        Owner who;
        if (!s_attached || s_net == NULL || !findOwner(call->input, call->entryId, who) || who.slot < 0
            || who.slot >= kMaxSlots)
        {
            return;
        }
        if (who.sub)
        {
            onSub(call->input, who);
        }
        else
        {
            onMain(call->input, who);
        }
    }

    void driverSaved(int which)
    {
        if (s_net != NULL)
        {
            memcpy(s_net->saved[which & 1], s_net->slots, sizeof(s_net->slots));
        }
    }

    void driverLoaded(int which)
    {
        if (s_net != NULL)
        {
            memcpy(s_net->slots, s_net->saved[which & 1], sizeof(s_net->slots));
        }
    }

    void attach(Claimer first, Claimer second)
    {
#ifdef __MWERKS__
        if (s_net == NULL)
        {
            s_net = static_cast<DriverNet*>(game::allocNetworkZero(static_cast<u32>(sizeof(DriverNet))));
        }
#endif
        if (s_net != NULL)
        {
            for (int i = 0; i < kMaxSlots; i++)
            {
                clear(s_net->slots[i]);
                s_net->slots[i].frame = 0;
            }
            driverSaved(0);
            driverSaved(1);
        }
        s_claim[0] = first;
        s_claim[1] = second;
        s_takes = 0;
        s_releases = 0;
        s_restores = 0;
        s_drivenTotal = 0;
        s_attached = s_net != NULL;
    }

    void detach()
    {
        for (int i = 0; i < kMaxSlots; i++)
        {
            release(i, "match ending");
        }
        s_attached = false;
#ifdef __MWERKS__
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_net));
#endif
        TM_LOG("cpu: %u takes, %u releases, %u AI flags put back, %u frames driven\n", s_takes, s_releases,
               s_restores, s_drivenTotal);
    }
}
}
