#pragma once

#include <types.h>

#include "main/extra.h"
#include "main/game/advantage.h"

class Fighter;

// Frame data read ahead from the scripts of a fighter's move: when its
// interrupts open, its body is intangible or invincible, its hitboxes and
// grab boxes are out, a throw releases. One fixed-size record per tracked
// fighter, rebuilt by walking the scripts when its status or subaction
// script changes; each frame's questions only count forward from the
// script's clock and the animation's speed. Nothing is allocated. frame_data.cpp
// says what is read and what assumed. Reached through main/extra.h's hooks;
// without the extra module every consumer behaves as before.
namespace tm {
namespace extra {
    enum {
        kFdMaxHit = 6,  // hitbox and grab box windows kept per move
        kFdMaxBody = 2, // whole-body collision windows kept per move
        kFdMaxRate = 4, // animation speed changes kept per move
        // Commands a walk reads at most: the subaction script (subroutines,
        // loop passes and both sides of undecided conditions included), and
        // the status's own scripts.
        kFdMaxSubCommands = 256,
        kFdMaxActCommands = 192
    };

    enum FdFlags {
        kFdValid = 0x0001,
        // Not statically knowable, each for its reason:
        kFdCond = 0x0002,          // an open, a timer, a speed or a jump under a condition the walk cannot decide
        kFdLoop = 0x0004,          // an endless loop, a While, or the command limit ran out
        kFdFlow = 0x0008,          // the script changes subaction, breaks a loop or resets its clock
        kFdCall = 0x0010,          // a subroutine too deep or not readable
        kFdRateVar = 0x0020,       // the animation speed comes from a variable
        kFdFrameSet = 0x0040,      // the script sets the animation frame
        kFdStatusDecides = 0x0080, // the status script opens interrupts by a variable or a timer of its own
        kFdEndCondGround = 0x0100, // what the animation's end leads to depends on a condition (on the ground)
        kFdReclose = 0x0200,       // interrupts are disallowed again after being allowed
        kFdEndCondAir = 0x4000,    // the same in the air
        // Notes that do not make the first actionable frame uncertain:
        kFdWindows = 0x0400,     // hitbox or body windows differ between the sides of a condition
        kFdOverflow = 0x0800,    // more windows than the record holds
        kFdPartialBody = 0x1000, // per-bone body collision commands (not in the windows)
        kFdStatusOpen = 0x2000,  // the status script opens interrupts when the status begins
        kFdUncertain = kFdCond | kFdLoop | kFdFlow | kFdCall | kFdRateVar | kFdFrameSet | kFdStatusDecides
                       | kFdReclose
    };

    // What the animation's end leads to (a Change Status command of the
    // status's scripts whose requirement is "animation end").
    enum FdEnd {
        kFdEndNone = 0,
        kFdEndFree = 1, // a status the fighter is free to act in (Wait, Fall, ...)
        kFdEndOther = 2 // another status: the move goes on there
    };

    enum FdWindowKind {
        kFdHit = 0,        // offensive collisions
        kFdCatch = 1,      // grab boxes
        kFdInvincible = 1, // body windows: the Body Collision command's states
        kFdIntangible = 2
    };

    // Script times: the value of the subaction script's clock at which the
    // command runs (it runs in the first frame the clock is at or past it).
    // `off` below 0: until the status ends.
    struct FdWindow {
        float on;
        float off;
        u8 kind;
        u8 reserved[3];
    };

    struct FdRate {
        float time;
        float value;
        u8 multiply; // the speed is multiplied by `value` instead of set to it
        u8 reserved[3];
    };

    struct FrameRecord {
        u16 flags; // FdFlags
        u8 hitCount;
        u8 bodyCount;
        u8 rateCount;
        u8 rule;        // StatusRule (frame_data.cpp)
        u8 end[2];      // FdEnd on the ground, in the air
        s16 endStatus[2];
        u16 commands;   // commands the walk read
        s16 status;     // what was walked
        s16 motionKind;
        float openTime; // first Allow Interrupt or action group enabled by the subaction script; below 0: none
        u32 openGroups; // bit g: interrupt group g; bit 31: Allow Interrupt (every group of the situation)
        float throwTime; // the throw's release; below 0: none
        FdWindow hit[kFdMaxHit];
        FdWindow body[kFdMaxBody];
        FdRate rate[kFdMaxRate];
        const void* subStart; // the scripts walked
        const void* actStart;
        const void* loopStart;
    };

    // Frames from now until the frame in which an input acts: 0 if the
    // fighter can act now, k if its interrupts open during the k-th coming
    // frame (an input in that frame acts in it), -1 if not known.
    struct ActIn {
        s16 frames;
        u8 how;     // main::ExtraActHow
        u8 certain; // 0: the scripts did not settle it (see FdFlags); frames is then the latest reading
    };

    void frameDataAttach();
    void frameDataReset();
    // Once per finished tracker frame: refreshes the records, and works out
    // the frame advantage of a hit that landed in that frame; a hit on
    // shield is shown at once, a hit that connects is only logged.
    void frameDataFrame();

    ActIn frameDataActIn(Fighter* fighter);
    // The match module's hook: the frames as above, -1 when unknown or not
    // certain; `how` receives main::ExtraActHow.
    int framesUntilActionable(Fighter* fighter, int* how);
    // The fighter's record, or NULL if it is not tracked.
    const FrameRecord* frameRecord(Fighter* fighter);
    // Frames from now until the subaction script's clock reaches
    // `scriptTime` (a window's `on` or `off`): 0 if it has, -1 if unknown.
    int frameDataFramesUntil(Fighter* fighter, float scriptTime);

    // The measured frame advantage of an exchange (the match module's
    // tracker), logged against the predicted one. True if a predicted value
    // was shown; a different one is then posted as "Measured: n Frames".
    bool frameDataMeasured(const game::Advantage& r);

    // Writing 1 here (the harness does) logs every tracked fighter's
    // reading each frame and every walk.
    extern u8 g_fdTrace;
}
}
