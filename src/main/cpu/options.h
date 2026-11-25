#pragma once

#include "main/cpu/fighter_input.h"

// The lab's CPU Options: every CPU is driven by a state machine (idle -> hit ->
// smash DI, trajectory DI -> tech or get-up -> counter action after N
// actionable frames -> idle; a shield variant and grab escape), reading the
// fighter live each frame.
namespace tm {
namespace cpu {
    void optionsAttach();
    void optionsDetach();
    // Driver claimer: takes every CPU's slot while the options are on.
    void optionsClaim(const Owner& who);

    // Savestates (with driverSaved/driverLoaded): copies every slot's state
    // machine, the RNG state, cues and tools (Move CPU, Freeze CPU,
    // Controlled By) at a save and restores them at a load. One copy per
    // savestate slot (quick state and recording start), matching the driver.
    void optionsSaved(int which = 0);
    void optionsLoaded(int which = 0);

    // The trajectory DI stick for the hit the CPU in `slot` is handling.
    bool optionsDi(int slot, float& x, float& y);

    // Play Slot N: frame `t` of the CPU's recording slot `slot` (0-5); false
    // past its end, which ends the action.
    typedef bool (*SlotFrame)(int slot, u32 t, Frame& out);
    void setSlotFrame(SlotFrame source);

    // Recording > CPU Counter while the CPU's recording plays. The options take
    // the CPU back the frame after playback ends, so the recording hands the
    // hit over:
    //  - playbackCounterHit (On CPU Hit), once per game step: counts each new
    //    hit and grab. True when that hit's counter is enabled: playback ends
    //    and the counter follows on the first actionable frame, after Counter
    //    Delay (no DI or tech). A grab is left to Grab Escape, whose release
    //    counts.
    //  - playbackHitHanded (On Any Hit): taken as a new hit in hitlag.
    bool playbackCounterHit(int playerNo, int hitlag, int hitstun, int status);
    void playbackHitHanded(int playerNo);

    // Custom mash rate override: when above 0, Medium Grab Escape and
    // Clatter Escape mash on `percent` of frames; 0 restores the default
    // Medium rate.
    void setMashChance(int percent);

    // Menu actions (CPU Options page); the labels are the rows' values.
    extern char g_moveCpuLabel[];
    extern char g_freezeCpuLabel[];
    void moveCpuToggle();
    void freezeCpuToggle();
    void controlledByChanged();

    // Advanced Counter Options: Hit Number changed (load that hit), or one of
    // its values changed (store them).
    void advancedHitChanged();
    void advancedValueChanged();

    // Mid-match fighter swap: releases driven slots, clears transient mover,
    // freeze, cue, and playback-handover state, and clamps incompatible
    // DJC/Float counter settings when `cpuSwapped` is true.
    void optionsPrepareSwap(bool cpuSwapped, int newCpuFtKind);

    // Resets one CPU slot's (or every slot's) option state machine cleanly back
    // to kIdle without releasing driver claims, port overrides, or noReaction.
    // Called on every Combo Trainer warp and Scenario preset reset.
    void optionsResetSlot(int playerNo);
    void optionsResetAll();
}
}
