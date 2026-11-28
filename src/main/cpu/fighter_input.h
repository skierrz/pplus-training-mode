#pragma once

#include <types.h>

// The per-fighter input object, an `aiInput` (sora_melee, derived from
// Input): soControllerImpl::update calls its update(), which runs the AI or
// reads the pad, then reads the result through its getters. Nana has her
// own. Layout read in the emulator on RSBE01 rev 2 + P+ 3.2.1.
namespace tm {
namespace cpu {
    // aiInput's held buttons (ipPadButton), after the controller
    // settings: the C-stick arrives as kAttack | kSmash | kCStick with the
    // main stick pushed for one frame, then kCStick alone.
    namespace button {
        enum {
            kAttack = 0x1,
            kSpecial = 0x2,
            kJump = 0x4,
            kGuard = 0x8, // also set by the controller when a trigger is > 0
            kSmash = 0x20,
            kCStick = 0x100,
            kFlickJump = 0x8000 // "tap jump on"; the AI path always sets it
        };
    }

    // Sticks -1..1, +x right, +y up.
    struct Frame {
        u32 buttons;
        float x;
        float y;
    };

    struct Owner {
        int entryId;
        int playerNo; // 0-based HUD slot / port of the fighter
        int slot;     // ftEntry slot index: aiMgr's per-slot index (= port in Training)
        bool cpu;     // a CPU player in the match setup (game::cpuPlayer)
        bool sub;     // Nana's input (ftInput's second one)
        bool trainingControl; // Training's CPU option is Control (CPU type 25)
        void* input;          // the input object
    };

    bool findOwner(const void* input, int entryId, Owner& out);

    void readFrame(const void* input, Frame& out);

    // Replaces update()'s result; clears the triggers too, so the
    // controller adds no guard of its own.
    void writeFrame(void* input, const Frame& frame);

    // Makes this frame's stick count as held, so it cannot trigger smash DI.
    // Call from the fighter-input callback (docs/internals.md, "Smash DI
    // suppression").
    bool holdStickStill(int entryId, bool sub, const Frame& frame);

    // aiMgr's per-slot AI byte (> 0: the AI drives the fighter). At 0,
    // aiInput::update reads the slot's pad (all zero without a controller)
    // and P+'s Random DI leaves the DI to the stick. Training's Control
    // clears it; its other CPU options set it when its menu closes.
    bool aiFlag(int slot, u8& out);
    bool setAiFlag(int slot, u8 value);

    // aiInput's CPU type (+0x113). 25 (Training's Control) reads the
    // controller's pad instead of running the AI, whatever the AI flag.
    enum { kCpuTypeControl = 25 };
    int cpuType(const void* input);
    void setCpuType(void* input, int type);

    // aiInput +0x28: human-pad flag checked by P+ ButtonPresses.asm (0x80575C04)
    // before reading analog C-Stick and L/R triggers into LA-Float[34..37].
    u8 humanPadFlag(const void* input);
    void setHumanPadFlag(void* input, u8 flag);
}
}
