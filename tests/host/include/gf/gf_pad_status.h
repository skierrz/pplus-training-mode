#pragma once

#include <types.h>

// Stand-in: the fields of the game's pad record that src/menu/drill_list.cpp
// and src/menu/game/training.cpp read and clear.
namespace gfPadError {
    enum PadError {
        NONE = 0,
        NO_CONTROLLER = -1,
        NOT_READY = -2,
        TRANSFER = -3
    };
}

namespace gfPadType {
    enum PadType {
        GCC = 0,
        WII_CLASSIC = 1,
        WIIMOTE = 2,
        NUNCHUK = 3
    };
}

struct gfPadButtons {
    u32 bits;
};

struct gfPadStatus {
    gfPadButtons m_buttonsCurrentFrame;
    gfPadButtons m_buttonsCurrentFrame2;
    gfPadButtons m_buttonsHeld;
    gfPadButtons m_buttonsPressedThisFrame;
    gfPadButtons m_buttonsReleasedThisFrame;
    gfPadButtons m_buttonsPressedThisFrame2;
    float _0x18[6];
    char m_stickX;
    char m_stickY;
    char m_subStickX;
    char m_subStickY;
    char m_lTriggerAnalog;
    char m_rTriggerAnalog;
    char _0x36;
    char _0x37;
    s8 m_error;
    char _0x39;
    char _0x3a;
    char _0x3b;
    gfPadType::PadType m_controllerType;
};
