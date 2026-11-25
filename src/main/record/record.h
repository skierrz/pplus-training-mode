#pragma once

#include <types.h>

#include "main/record/start_state.h"
#include "shared/interface.h"

// Input recording and playback: the player's (HMN) and the CPU's final inputs
// recorded per game step into slots, played back from saved start positions,
// looped, and taken over.
//
// Both go through the fighter-input hook: this module wraps the fighterInput
// callback (the CPU driver, then the savestates' taunt strip), takes the
// side's slot with a cpu::Source before it runs (playback, the player's
// fighter held neutral, the CPU copying the player's pad) and reads the
// input object after it (what the fighter acts on this step). It wraps the
// pads callback too: before the savestate code at the top of the main loop,
// the last step's bookkeeping (loop, auto restore, CPU Counter) and the
// request for the start state (a save or load in the recording's own slot);
// after the lab, live input (system pads), takeover, the CPU's controller
// and Start Paused (game pads).
//
// The start state is a savestate when one can be taken (exact), else the
// lite one (start_state.h). After a quick state (D-pad) is loaded, the run
// goes on from that state's frame if it was saved since the positions were;
// an older state stops recording and playback until the next restore.
namespace tm {
namespace record {
    // Wraps iface's fighterInput and pads callbacks (call after the CPU
    // code and the lab filled them).
    void attach(iface::Interface* iface);
    // Releases what it drives, puts the CPU's controller back, frees the storage.
    void detach();

    // Draws the HUD inside a gfx::begin()/end() pass.
    void drawHud();
    bool hudVisible();

    // ---- menu ----
    bool positionsSaved();
    // Save Positions, or Restore Positions once saved.
    void saveOrRestore();
    void resavePositions();
    void prunePositions();
    void deletePositions();
    // Double-press guarded menu callbacks and confirm labels ("A: Confirm").
    extern char g_deletePosLabel[];
    extern char g_deleteSlotLabel[];
    void deletePositionsPressed();
    void deleteManagedSlotPressed();
    void clearDeleteConfirm();
    // A row of the Recording page changed.
    void hmnModeChanged();
    void cpuModeChanged();
    void slotChanged();
    void mirroredChanged();
    // Reconciles the CPU controller swap immediately (e.g. when Controlled By
    // turns off or Reset All Settings runs while the menu is open).
    void reconcileController();
    // Mid-match fighter swap: exits any active recording/playback/CPU control
    // mode, restores m_controllerNo, and deletes saved positions and slots.
    void prepareSwap();
    // Rows available now.
    bool cpuRowsAvailable();    // positions saved and CPU Control is Lab
    bool modeRowsAvailable();   // positions saved, not mirrored
    bool loopRowsAvailable();   // positions saved, nothing recording
    bool mirrorRowAvailable();  // positions saved, a side playing back, nothing recording
    bool slotHasInputs(int side, int slot);
    // Slot chances: slot `slot` of `side` changed; the others are rebalanced to 100.
    void chanceChanged(int side, int slot);
    // Slot Management.
    void deleteManagedSlot();
    void copyManagedSlot();
    // A line on the recording's HUD for a few seconds.
    void showMessage(const char* text);

    // ---- Frame Decrement (lab.cpp) ----
    // Rewind: loads the recording's start state and replays both sides' run
    // log (slots.h) up to the frame before the current one, undrawn, then
    // holds there. Needs the positions saved as a savestate, a side in
    // Playback or Re-Record, a frame past the run's first, and no loop.
    // Called while frame advance holds the game; false if it cannot go back
    // (with `press`, the reason is logged).
    bool rewindRequest(bool press);
    enum RewindStep {
        kRewindIdle, // no rewind: frame advance as usual
        kRewindHold, // the game must not step (the load is pending, or done)
        kRewindRun   // the game must step: the replay is short of its frame
    };
    // From the lab's game pads, before each game step.
    int rewindStep();

    // ---- import (main/storage/recording_file.cpp) ----
    // With the inputs already in the slots: at the next top of the main loop
    // that has both fighters (match running, not paused), places them as
    // `place` says (HMN, CPU; their damage included) and saves the positions
    // there as Save Positions does (a savestate when it can), keeping the
    // slots. A failure shows on the recording's HUD.
    void importStart(const LitePlacement place[2]);
    bool importPending();
    // The recording's storage was not free in this match's first seconds.
    bool storageGaveUp();
}
}
