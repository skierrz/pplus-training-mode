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

    bool readMotion(Fighter* fighter, MotionInfo& out);
}
}
