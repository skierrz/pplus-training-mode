#pragma once

#include <gf/gf_pad_status.h>
#include <types.h>

// Per-controller-type button bits and connection check for gfPadStatus records
// at kPadsSystem (gfPadSystem::updateLowWii at 0x80029750; bit tables at
// 0x80422D10 sideways Wii Remote, 0x80422D80 Nunchuk, 0x80422DF8 Classic,
// 0x8059C650 Classic host Wiimote +, and ZL|ZR -> 0x10 at 0x80029D88).
namespace tm {
namespace game {
    enum PadBits {
        kPadDLeft = 0x00000001u,
        kPadDRight = 0x00000002u,
        kPadDDown = 0x00000004u,
        kPadDUp = 0x00000008u,
        kPadZ = 0x00000010u,           // GCC Z, Classic ZL|ZR common bit, Nunchuk Z
        kPadR = 0x00000020u,           // GCC R, Classic R
        kPadL = 0x00000040u,           // GCC L, Classic L (sideways B also sets 0x40)
        kPadA = 0x00000100u,           // GCC A, Classic A, sideways 2, Nunchuk A
        kPadB = 0x00000200u,           // GCC B, Classic B, sideways 1, Nunchuk B
        kPadX = 0x00000400u,           // GCC X, Classic X (Nunchuk C also sets 0x400)
        kPadY = 0x00000800u,           // GCC Y, Classic Y
        kPadStart = 0x00001000u,       // GCC Start, Classic/sideways/Nunchuk +
        kPadClassicZL = 0x00004000u,   // Classic ZL (also sets kPadZ)
        kPadClassicZR = 0x00008000u,   // Classic ZR (also sets kPadZ)
        kPadWii1 = 0x00010000u,        // sideways 1 (also sets kPadB), Nunchuk 1
        kPadWii2 = 0x00020000u,        // sideways 2 (also sets kPadA), Nunchuk 2
        kPadNunchukC = 0x00040000u,    // Nunchuk C (also sets kPadX)
        kPadWiiMinus = 0x00080000u,    // Classic/sideways/Nunchuk -
        kPadClassicPlus = 0x00100000u, // Classic + (also sets kPadStart)
        kPadWiiA = 0x00200000u,        // sideways A, Nunchuk A (also sets kPadA)
        kPadWiiB = 0x00400000u,        // sideways B (also sets kPadL), Nunchuk B (also sets kPadB)
        kPadCorePlus = 0x00800000u     // Classic host Wiimote +
    };

    // Matches Brawl CSS controller scan (0x806894C0-D4): Wii ports 4-7 require
    // both m_error == NONE and an identified Wii controller type (1-3).
    inline bool padConnected(int port, const gfPadStatus& pad)
    {
        if (port < 0 || port >= 8 || pad.m_error != gfPadError::NONE)
        {
            return false;
        }
        if (port >= 4)
        {
            return pad.m_controllerType == gfPadType::WII_CLASSIC
                   || pad.m_controllerType == gfPadType::WIIMOTE
                   || pad.m_controllerType == gfPadType::NUNCHUK;
        }
        return true;
    }
}
}
