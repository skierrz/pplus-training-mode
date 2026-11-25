#pragma once

#include <types.h>

// Test aids for scripts/savestate_tests.py, driven by the emulator harness
// through one block in this module's memory (its address is logged at
// attach); nothing here runs unless the harness writes to it.
//  - trace: one log line per game frame describing the match state a
//    savestate must bring back: the frame counter, the random number
//    generator, both fighters (and partners), a hash of the fighters'
//    instance heaps and of every set heap's usage;
//  - request: ask for a save or load at the next top of the main loop;
//  - rulesV3: save with the fighters' free space left out, to compare;
//  - park: hold the game at the top of the main loop once the frame counter
//    reaches a value, until released, so the harness can act between two
//    exact frames while emulation keeps running at full speed;
//  - held: D-pad bits added to port 1's pads at the top of the main loop
//    for an exact number of passes.
// The tests drive the fighters frame-exactly through the CPU code's script
// source (main/cpu/script_source.h), restarted at every save and load.
struct gfPadStatus;

namespace tm {
namespace savestate {
    struct Probe {
        u32 magic;                // 0x00 "TMSP"
        volatile u8 trace;        // 0x04 1: log a trace line per frame
        volatile u8 request;      // 0x05 1 save, 2 load (cleared when taken)
        u8 rulesV3;               // 0x06 1: fighter instance heaps by allocated block (the
                                  //      experiments' v3 rules), for comparisons
        volatile u8 osdVisible;   // 0x07 whether an OSD message showed at the last park
        volatile u32 parkAt;      // 0x08 park once the frame counter >= this (0: never)
        volatile u32 release;     // 0x0C the harness writes parkSeq here to release
        volatile u32 parkSeq;     // 0x10 +1 per park
        volatile u32 parkedFrame; // 0x14 frame counter while parked, else 0
        volatile u16 heldBits;    // 0x18 buttons added to port 1's pads at the top of the
        volatile u16 heldPasses;  // 0x1A main loop for this many more passes (the D-pad tests)
    };

    extern Probe g_probe;

    // At the top of the main loop: one line if the frame counter moved
    // since the last one, or always with `tag` ('S' saved, 'L' loaded).
    void traceTop(char tag);
    // At the top of the main loop, after the trace: parks if asked to;
    // returns true if it did.
    bool parkTop();
    void probeReset();
    // At the top of the main loop, before the pads are read: port 1 holds
    // heldBits while heldPasses lasts.
    void heldPads(gfPadStatus* pads);
}
}
