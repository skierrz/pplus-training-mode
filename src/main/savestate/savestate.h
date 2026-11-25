#pragma once

#include <types.h>

#include "main/game/match.h"
#include "shared/interface.h"

struct gfPadStatus;

// Savestates for Training matches: one slot, saved and loaded at the top of
// the main loop (the system pads callback, before anything else of ours),
// from the D-pad (hold right to save, left to load) or the lab menu.
// Preconditions: docs/internals.md, "Savestate preconditions".
namespace tm {
namespace savestate {
    void attach();
    // Frees the slot; must run before the scene ends.
    void detach();

    // From the stub's pad hooks, before the lab sees the pads.
    void onPads(u32 phase, gfPadStatus* pads);
    // From the stub's fighter-input hook, after the CPU driver.
    void onFighterInput(const iface::FighterInput* call);
    // From the render hook, before the lab draws: the save's screen flash.
    void onRender(const game::MatchState& match);

    // Lab menu actions: carried out the next time the match runs unpaused.
    void requestSave();
    void requestLoad();
    // D-pad bits currently bound to Load State / Save State (0 when menu/drill active).
    u32 boundBits();

    // The recording's start state (Save / Restore Positions), in its own
    // slot: at the next top of the main loop, before that pass's quick state
    // requests. The listener gets every outcome, and for a refusal whether it
    // passes (paused, busy, files loading) or not (more than two fighters, no
    // room, another match).
    enum Event { kEventSaved, kEventLoaded, kEventRefused };
    typedef void (*Listener)(int slot, int event, bool passing, const char* why);
    // The recording code listens to both slots (snapshot.h kSlot*).
    void setListener(Listener listener);
    // `feedback`: play the save or load confirmation sound; false stays silent.
    void requestRecordingSave(bool feedback = false);
    void requestRecordingLoad(bool feedback = false);
    bool recordingStateValid();
    void dropRecordingState();
    // True while gfFileIOManager has pending async file requests.
    bool loadingFiles();
    // Mid-match fighter swap: drops all savestate slots (freeing spare heaps)
    // and re-arms the automatic match-start savestate once the swap settles.
    void resetForSwap();
    // The lab menu's State row (Recording page): the slot's frame and age.
    enum { kStateTextSize = 40 };
    extern char g_stateText[kStateTextSize];
}
}
