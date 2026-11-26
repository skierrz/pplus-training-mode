#pragma once

#include <types.h>

class Fighter;

namespace tm {
namespace game {
    // The fighter's current animation (subaction): its kind, frame and
    // length, and its name from the animation file the game loaded.
    struct MotionInfo {
        int kind;
        int frame;
        float exactFrame; // the frame as the game keeps it (fractional when the rate is not 1)
        int length;
        const char* name; // NULL if not readable
    };

#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_readMotion(Fighter* fighter, MotionInfo& out);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool readMotion(Fighter* fighter, MotionInfo& out) { return x_readMotion(fighter, out); }
#else
    bool readMotion(Fighter* fighter, MotionInfo& out);
#endif
#else
    bool readMotion(Fighter* fighter, MotionInfo& out);
#endif
}
}
