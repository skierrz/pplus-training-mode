#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"

// A scripted input source for the emulator tests (scripts/plugin_input.py):
// the harness writes steps of controls into fixed-layout blocks in the
// module's memory (the first one's address is logged on attach; the others
// follow it), each played into one fighter's input frame by frame, so two
// fighters can be driven at once. Recordings do not play through it
// (record.cpp drives the fighters with its own RecordSource).
namespace tm {
namespace cpu {
    struct ScriptStep {
        u32 buttons;
        float x;
        float y;
        u32 frames;
    };

    enum {
        kScriptMagic = 0x544D4344, // "TMCD"
        kScriptSteps = 96,
        kScriptBlocks = 2
    };

    // Fixed layout: the harness writes these fields by offset.
    struct ScriptBlock {
        u32 magic;          // 0x00 kScriptMagic
        u32 generation;     // 0x04 the harness bumps it to (re)start the script
        s32 playerNo;       // 0x08 player to drive (0 = P1); -1 drives nobody
        u32 stepCount;      // 0x0C
        u32 loop;           // 0x10 nonzero: start again after the last step
        u32 alsoSub;        // 0x14 nonzero: drive Nana with the same controls
        u32 aiOff;          // 0x18 nonzero: switch the slot's AI off while driving
        // Written by the module:
        u32 framesDriven;   // 0x1C frames written since the script started
        u32 step;           // 0x20 current step (stepCount when done)
        u32 startFrame;     // 0x24 the game's frame counter when it started
        u32 aiButtons;      // 0x28 what update() produced this frame,
        float aiStickX;     // 0x2C before it was replaced
        float aiStickY;     // 0x30
        u32 callsThisFrame; // 0x34 driven calls during the last game frame
        u32 lastFrame;      // 0x38 the game's frame counter at the last call
        ScriptStep steps[kScriptSteps]; // 0x3C
    };

    void scriptAttach();
    // Driver claimer: takes the scripted player's slot when a new script starts.
    void scriptClaim(const Owner& who);
}
}
