#pragma once

#include <types.h>

#include "shared/perf.h"

// The stub's side of the time measurements (shared/perf.h): the frame, and
// each hook whole. Without PERF=1 only perfBypassed() is left, always false.
namespace tm {
namespace stub {
#ifdef TM_PERF
    bool perfBypassed();
    // At the render hook's entry: the frame's length; returns the start.
    u32 perfRenderEnter();
    // At its exit: the hook's time, the pass's busy time, and the frame's end.
    void perfRenderExit(u32 start);
    // At the system pads hook's entry, the top of a main-loop pass.
    void perfLoopTop();
    // At the game pads hook's entry (once per game step).
    void perfGameTop();
#else
    inline bool perfBypassed()
    {
        return false;
    }
#endif
}
}
