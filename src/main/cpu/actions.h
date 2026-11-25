#pragma once

#include <types.h>

// Counter actions as small input scripts: buttons and stick per step, each
// step held for a number of frames or until the fighter reaches a state,
// played from the frame the action starts. Each list (ground, air, shield)
// is one table indexed like its menu row's labels; a new action is one more
// entry plus its label.
namespace tm {
namespace cpu {
    // How a step's stick x is meant.
    enum StickRef {
        kRefAbs,     // as written: +x right
        kRefToward,  // +x toward the opponent
        kRefForward, // +x the way the fighter faces
        kRefAway,    // +x away from the opponent
        kRefRandom   // toward or away, chosen once per script
    };

    // What a step waits for: it is held until this is true (at most
    // `frames` frames), then the next step plays on the same frame.
    enum StepUntil {
        kUntilNone,    // held exactly `frames` frames
        kUntilAir,     // airborne
        kUntilTakeoff, // the coming frame leaves the jump squat (the next
                       // step lands on the first airborne frame)
        kUntilDash,    // in an initial dash
        kUntilFalling, // airborne and no longer rising
        kUntilCanAct,  // able to start an action
        kUntilJumped,  // in a jump (aerial jump included)
        kUntilSpecial, // in a special (a fighter's own status)
        kUntilFastfall // falling faster than a frame before by a fastfall's jump
    };

    struct Step {
        u16 buttons;
        s8 x, y;   // percent of a full stick push
        u8 ref;    // StickRef
        u8 frames; // frames to hold the step (0 ends the script)
        u8 until;  // StepUntil
        u8 reserved;
    };

    enum ActionFlags {
        kNoTech = 0x1, // after it the CPU does not tech (Wait)
        // Taken once its steps are written: a wiggle ends in Fall and Wait
        // changes nothing, so no status change can confirm them.
        kEndsWithSteps = 0x2,
        // Only for characters that have the technique (fitsKind): Ness's,
        // Lucas's and Mewtwo's double jump cancel, Peach's, Ganondorf's and
        // Mewtwo's float. Others take it as None.
        kForDjc = 0x8,
        kForFloat = 0x10,
        // Needs an aerial jump left; without one, wiggles out of tumble or
        // does nothing instead of retrying X.
        kNeedsJump = 0x20
    };

    struct Action {
        const Step* steps; // ends with a step of 0 frames
        u16 flags;
        // Play Slot N: 1-6 plays the CPU's recording slot N instead of steps
        // (see options.h).
        u16 playSlot;
    };

    enum ActionList { kListGround, kListAir, kListShield, kListCount };

    // What an index of a list stands for.
    enum ActionKind {
        kActionNone,
        kActionScript,
        kActionRandom,
        kActionRandomAdvanced,
        kActionPlaySlot,      // Play Slot 1-6
        kActionPlayRandomSlot // Play Random Slot
    };

    int actionKind(int list, int index);
    enum { kKindAny = -2 };
    // Action `index` of a list for fighter kind `kind` (ftKind), or NULL if
    // none or `kind` lacks its technique. kKindAny skips the fighter check.
    const Action* actionFor(int list, int index, int kind);
    // Action `index` of a list, or NULL if it has no script and plays no
    // recording slot.
    inline const Action* action(int list, int index)
    {
        return actionFor(list, index, kKindAny);
    }
    int actionCount(int list);
    // An index from the Random action pool of a list, picked with `roll`.
    int randomAction(int list, u32 roll);
    // False if `a` is for characters with a technique fighter kind `kind`
    // (ftKind) lacks.
    bool fitsKind(const Action* a, int kind);
    // The short hop script (the Jump behavior).
    const Step* shortHop();
    // One-frame sideways wiggle out of tumble (kEndsWithSteps).
    const Action* wiggleOut();
}
}
