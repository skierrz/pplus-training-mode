#pragma once

#include <types.h>

// Stopping and restarting the game's simulation, one game step at a time.
namespace tm {
namespace game {
    // Sets or clears our stop bit in the task scheduler. It takes effect at
    // the next gfTaskScheduler::process call, i.e. for the next game step.
    // Drawing goes on while it is set. Only the bit we set is ever cleared.
    void setFrozen(bool frozen);
    bool frozen();
    // Writes our stop bit as frozen() says, whatever the byte holds now
    // (after a savestate load put back an older scheduler).
    void reapplyFrozen();
    // True while our gameplay or camera category pause is on (savestates
    // wait: those flags live in the tasks a load would put back).
    bool categoriesPaused();

    // Pauses or resumes every task of the gameplay categories (fighters,
    // items, weapons, effects, stage, physics, AI, collision) with the
    // game's own gfTaskScheduler::setPauseCategory, leaving the HUD, the
    // camera and the match operators (Training's pause menu among them)
    // running. Takes effect from the next game step.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    void x_setGameplayPaused(bool paused);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline void setGameplayPaused(bool paused) { x_setGameplayPaused(paused); }
#else
    void setGameplayPaused(bool paused);
#endif
#else
    void setGameplayPaused(bool paused);
#endif

    // Pauses or resumes the camera's tasks the same way (the Start-opened lab
    // menu freezes both simulation and camera while Start is held).
    void setCameraPaused(bool paused);

    // The scheduler's halfword +0xA (bytes 0xA and 0xB) without our bit:
    // non-zero while P+'s debug pause code has something pending there.
    u32 otherStopBits();

    // The stop bit is set while we did not set it: someone else holds the
    // game (ProjectPunch's menu or frame advance set the same bit), so no
    // game step runs.
    bool heldByOthers();
}
}
