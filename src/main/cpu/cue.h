#pragma once

#include <types.h>

// Making the CPU act when a drill says so. A cue is one action of a Counter
// Action list (actions.h), played exactly as a counter action: the same
// scripts, first-input rules and retry. A hold is a stick and buttons the
// CPU keeps while idle, in place of its Behavior. Both act only while CPU
// Control is Lab, keyed by player number (0 = P1); savestates keep them.
// Frames are game::trackerFrame() numbers: a frame callback reading `now`
// can cue now + 1 at the earliest. An idle CPU a drill put in tumble or down
// (changeStatusForce) techs and gets up as the Tech options say.
namespace tm {
namespace cpu {
    struct Action;

    enum CueState {
        kCueNone,    // no cue (never given, cancelled, or replaced)
        kCueWaiting, // not started: before its frame, or the CPU cannot act yet
        kCueRunning, // its inputs are being written, or the game has yet to take them
        kCueDone,    // the game took the action and its inputs are over
        kCueMissed   // the game did not take it within 30 frames, or a hit cut it short
    };

    enum { kCueAsap = -1 };

    // Cues action `index` of `list` from game frame `frame` (kCueAsap: at
    // once), on the first frame the CPU can act in the list's situation
    // (kListAir: in the air). While it waits the CPU still DIs, techs and gets
    // up, and the cue replaces the counter action after a hit. A running action
    // plays out first; a new cue replaces the old. False if it names no action.
    bool cue(int playerNo, int list, int index, s32 frame = kCueAsap);
    // The same with the caller's own action (its steps must stay valid while
    // the cue lives), e.g. an attack facing forward (kRefForward). `list` only
    // gives the situation. False without steps.
    bool cueAction(int playerNo, const Action* a, int list, s32 frame = kCueAsap);
    int cueState(int playerNo);
    // The frame the cue's first input was written in (-1 before that).
    s32 cueStart(int playerNo);
    // Drops the cue; a running one stops writing at once (the game finishes
    // what it started).
    void cueCancel(int playerNo);

    // Drops the cue and the hold and puts the CPU back to idle: for a drill
    // that placed the CPU without a savestate load.
    void cueReset(int playerNo);
    // The first frame a shield press can tech again (40 frames or fewer after
    // the last press is refused: options.cpp).
    s32 techReady(int playerNo);

    // While idle or lying down (in place of Get Up: nothing keeps it down,
    // attack gets it up), the CPU holds these buttons and this stick (-1..1, x
    // as `ref` says: StickRef) in place of its Behavior, until holdRelease.
    // `repeat` lets go every other frame, so each press is a new one.
    void hold(int playerNo, u32 buttons, float x, float y, int ref, bool repeat);
    void holdRelease(int playerNo);
}
}
