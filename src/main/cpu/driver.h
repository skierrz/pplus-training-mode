#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "shared/interface.h"

// Driving a fighter's controls from the module: once per fighter input
// object per simulated frame (the stub's fighter-input hook) a Source may
// replace what the AI or the pad produced. A slot taken with kSilenceAi has
// the AI off (aiMgr's per-slot flag cleared) while it is driven, so the AI
// does not run and Project+'s Random DI leaves the DI to the driven stick.
// The flag is put back on release, on a KO and when the match ends.
namespace tm {
namespace cpu {
    // A taken fighter is not driven while gone from the stage
    // (game::outOfPlay without the revival platform).
    enum { kMaxSlots = 4 };

    enum TakeFlags {
        kSilenceAi = 0x1, // clear the slot's AI flag while driving
        kWithNana = 0x2   // write the same frame into Nana's input too
    };

    class Source {
    public:
        // Still wanted for this owner; returning false releases the slot.
        virtual bool wants(const Owner& who) = 0;
        // `produced` is update()'s result (the AI's or the pad's). False leaves
        // it, and a silenced slot gets its AI flag back until the source drives
        // again.
        virtual bool next(const Owner& who, const Frame& produced, Frame& out) = 0;
        virtual void released(const Owner& who) = 0;
    };

    // Offered every fighter each frame, before it is driven, to call take().
    // At most two: the first may take a slot from the second (the harness
    // script source over the CPU options), which only gets free slots.
    typedef void (*Claimer)(const Owner& who);

    void attach(Claimer first, Claimer second);
    // Releases every slot (AI flags put back).
    void detach();

    // The fighter-input callback (iface::Callbacks::fighterInput).
    void onFighterInput(const iface::FighterInput* call);

    bool take(const Owner& who, Source* source, u32 flags);
    void release(int slot, const char* why);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_controlled(int slot);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool controlled(int slot) { return x_controlled(slot); }
#else
    bool controlled(int slot);
#endif
#else
    bool controlled(int slot);
#endif
    Source* sourceOf(int slot);
    // From a source's next() before it returns true: its frame replaces the
    // AI's and the AI stays on (a silenced slot gets its flag back).
    void keepAi(int slot);
    // From a source's next() before it returns true: its stick counts as held
    // this frame (holdStickStill), with kWithNana for Nana too.
    void holdStill(int slot);
    // From a source's next() before it returns true: silence the AI flag as
    // normal but skip writeFrame() so hardware pad triggers/C-stick pass through.
    void passThrough(int slot);

    // Buttons written to the slot's main input on the last driven frame (0 if
    // none): the game derives presses as held & ~previous.
    u32 lastButtons(int slot);
    // Main-input frames seen for the slot since attach.
    u32 frameOf(int slot);

    // Savestates: a copy of the slots per savestate slot (0 the quick state, 1
    // the recording's start state), so the AI flags the driver owes back match
    // those the load restored in aiMgr.
    void driverSaved(int which = 0);
    void driverLoaded(int which = 0);
}
}
