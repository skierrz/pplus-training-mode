#pragma once

#include <types.h>

struct gfPadStatus;

// Per-controller-type in-match button maps for GameCube (0) and Wii
// controllers (Classic 1, sideways Wii Remote 2, Remote + Nunchuk 3).
// Tables and shared pad helpers live in pad_inmatch.cpp so match module TUs
// share one copy.
namespace tm {
namespace game {
    struct InMatchPad {
        u32 start;       // Start / + (common + extended bits)
        u32 pauseLab;    // Z in Pause / jump to Training Menu
        u32 fastRepeat;  // Lab menu fast key repeat
        u32 prevCat;     // Lab menu previous category / tab
        u32 nextCat;     // Lab menu next category / tab
        u32 advance[5];  // main::AdvanceButton: L, Z, X, Y, R
    };

    inline bool hasAnalogTriggers(u32 controllerType)
    {
        return controllerType <= 1u; // GCC (0) and WII_CLASSIC (1)
    }

    const InMatchPad& inMatchPad(u32 controllerType);
    const char* pauseHint(u32 controllerType);
    // isDecrement == false: main::AdvanceButton; true: main::DecrementButton.
    u32 faButtonBits(u32 controllerType, u8 button, bool isDecrement);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_padConnectedInMatch(int port, const gfPadStatus& pad);
    void x_clearPadBits(gfPadStatus& pad, u32 mask);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool padConnectedInMatch(int port, const gfPadStatus& pad) { return x_padConnectedInMatch(port, pad); }
    int playerPort(int playerNo);
    static inline void clearPadBits(gfPadStatus& pad, u32 mask) { x_clearPadBits(pad, mask); }
#else
    bool padConnectedInMatch(int port, const gfPadStatus& pad);
    int playerPort(int playerNo);
    void clearPadBits(gfPadStatus& pad, u32 mask);
#endif
#else
    bool padConnectedInMatch(int port, const gfPadStatus& pad);
    int playerPort(int playerNo);
    void clearPadBits(gfPadStatus& pad, u32 mask);
#endif
}
}
