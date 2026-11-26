#include "main/game/pad_inmatch.h"

#include <gf/gf_pad_status.h>
#include <ip/ip_padconfig.h>

#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/game/pad_bits.h"

namespace tm {
namespace game {
    // Indexed by gfPadType::PadType (0 GCC, 1 WII_CLASSIC, 2 WIIMOTE, 3 NUNCHUK).
    // Sideways Wii Remote B sets kPadL (0x40), so default Frame Advance L maps
    // to - (kPadWiiMinus) and never touches kPadL/kPadWiiB unless R is chosen.
    static const InMatchPad kInMatchPads[4] = {
        // GCC (0)
        {
            kPadStart,
            kPadZ,
            kPadR,
            kPadL,
            kPadR,
            kPadX,
            kPadY,
            {kPadL, kPadZ, kPadX, kPadY, kPadR}
        },
        // WII_CLASSIC (1)
        {
            kPadStart | kPadClassicPlus,
            kPadZ | kPadClassicZL | kPadClassicZR,
            kPadR,
            kPadL,
            kPadR,
            kPadX,
            kPadY,
            {kPadL, kPadZ | kPadClassicZL | kPadClassicZR, kPadX, kPadY, kPadR}
        },
        // WIIMOTE (2, sideways)
        {
            kPadStart,
            kPadWiiMinus,
            kPadWiiB,
            0,
            kPadWiiA,
            0,
            0,
            {kPadWiiMinus, kPadWiiA, kPadWiiA, kPadWiiMinus, kPadWiiB | kPadL}
        },
        // NUNCHUK (3)
        {
            kPadStart,
            kPadZ,
            kPadWii1,
            kPadNunchukC,
            kPadWii2,
            kPadWiiMinus,
            0,
            {kPadWii1, kPadZ, kPadX | kPadNunchukC, kPadWiiMinus, kPadWii2}
        }
    };

    const InMatchPad& inMatchPad(u32 controllerType)
    {
        return kInMatchPads[controllerType < 4 ? controllerType : 0];
    }

    const char* pauseHint(u32 controllerType)
    {
        return controllerType == gfPadType::WII_CLASSIC ? "ZL/ZR: Lab Menu"
               : controllerType == gfPadType::WIIMOTE   ? "-: Lab Menu"
                                                        : "Z: Lab Menu";
    }

    u32 faButtonBits(u32 controllerType, u8 button, bool isDecrement)
    {
        if (isDecrement)
        {
            // main::DecrementButton order: None, Z, L, R, X, Y -> AdvanceButton index.
            static const u8 kDecToAdv[main::kDecrementButtonCount] = {0xFF, 1, 0, 4, 2, 3};
            if (button == 0 || button >= main::kDecrementButtonCount)
            {
                return 0;
            }
            button = kDecToAdv[button];
        }
        u8 idx = button < main::kAdvanceButtonCount ? button : 0;
        return inMatchPad(controllerType).advance[idx];
    }

    bool padConnectedInMatch(int port, const gfPadStatus& pad)
    {
        if (pad.m_error != gfPadError::NONE)
        {
            return false;
        }
        if (port >= 4)
        {
            u32 type = static_cast<u32>(pad.m_controllerType);
            return type >= gfPadType::WII_CLASSIC && type <= gfPadType::NUNCHUK;
        }
        return true;
    }

    int playerPort(int playerNo)
    {
        const u8* config = reinterpret_cast<const u8*>(ipPadConfig::getInstance());
        if (!isRam(config) || playerNo < 0 || playerNo >= kMaxFighters)
        {
            return -1;
        }
        return config[0xB5 + playerNo] & 0xF;
    }

    void clearPadBits(gfPadStatus& pad, u32 mask)
    {
        if (mask == 0)
        {
            return;
        }
        u32 keep = ~mask;
        u32* words = &pad.m_buttonsCurrentFrame.bits;
        for (int i = 0; i < 6; i++)
        {
            words[i] &= keep;
        }
    }
}
}
