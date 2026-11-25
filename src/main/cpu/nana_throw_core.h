#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/settings.h"

// Pure override helper for P+ 3.2.1 Nana AutoThrow (ai_nana 0x6100,
// CustomAI.asm:8-55). When Nana is in Catch_Wait (0x39) and ai_nana 0x6100
// outputs a throw or attack input, substitutes the chosen throw direction
// using Nana's live facing without mutating FitPopoMotionEtc.pac.
namespace tm {
namespace cpu {
    enum { kNanaStatusCatchWait = 0x39 };

    inline bool applyNanaThrowOverride(u8 mode, int status, float lr, const Frame& produced, Frame& out)
    {
        if (mode == main::kNanaThrowDefault || mode >= main::kNanaThrowCount || status != kNanaStatusCatchWait)
        {
            return false;
        }
        float ax = produced.x < 0.0f ? -produced.x : produced.x;
        float ay = produced.y < 0.0f ? -produced.y : produced.y;
        bool trigger = ax > 0.25f || ay > 0.25f || (produced.buttons & button::kAttack) != 0;
        if (!trigger)
        {
            return false;
        }
        float facing = lr < 0.0f ? -1.0f : 1.0f;
        out.buttons = 0;
        switch (mode)
        {
        case main::kNanaThrowForward:
            out.x = facing;
            out.y = 0.0f;
            return true;
        case main::kNanaThrowBack:
            out.x = -facing;
            out.y = 0.0f;
            return true;
        case main::kNanaThrowUp:
            out.x = 0.0f;
            out.y = 1.0f;
            return true;
        case main::kNanaThrowDown:
            out.x = 0.0f;
            out.y = -1.0f;
            return true;
        default:
            return false;
        }
    }
}
}
