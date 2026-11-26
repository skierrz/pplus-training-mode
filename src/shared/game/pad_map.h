#pragma once

#include "shared/game/pad_bits.h"

// Per-controller-type menu button maps for Training's character select
// (sora_menu_sel_char) and the training menu / recordings browser.
// Training's character select reads getSysPadStatus for all 8 ports (0-3
// GameCube, 4-7 Wii) with 0x100 confirm, 0x200 back, 0x1000 start/+, D-pad
// 0x1..0x8 (already rotated 90 deg CCW for sideways Wii Remote), and costume+
// table at 0x806A0F80 (GCC 0x400, Classic 0x400, sideways 0x200000 A, Nunchuk
// 0x20000 2).
namespace tm {
namespace game {
    struct PadMap {
        u32 up;
        u32 down;
        u32 pagePrev;
        u32 pageNext;
        u32 confirm;
        u32 back;
        u32 alt;       // X on GCC/Classic, A on sideways Wii Remote, C on Nunchuk
        u32 reopenA;   // first button of CSS reopen combo (0 if none)
        u32 reopenB;   // second button (same as reopenA for a single-button reopen)
        const char* backName;
        const char* reopenName;
        const char* reopenHint;
    };

    inline const PadMap& padMap(u32 controllerType)
    {
        static const PadMap kGcc = {
            kPadDUp,
            kPadDDown,
            kPadL | kPadDLeft,
            kPadR | kPadDRight,
            kPadA,
            kPadB,
            kPadX,
            kPadL,
            kPadR,
            "B",
            "L+R",
            "L+R = Training Menu"
        };
        static const PadMap kClassic = {
            kPadDUp,
            kPadDDown,
            kPadL | kPadClassicZL | kPadDLeft,
            kPadR | kPadClassicZR | kPadDRight,
            kPadA,
            kPadB,
            kPadX,
            kPadL,
            kPadR,
            "B",
            "L+R",
            "L+R = Training Menu"
        };
        // Sideways Wii Remote: D-pad is already rotated in gfPadStatus, 2 is
        // confirm (0x20100), 1 is back (0x10200), A is X-equivalent (0x200000),
        // and - + B (0x80000 + 0x400000) reopens without colliding with P+'s
        // special-fighter hold on - alone (0x806A0808) or the Code Menu's
        // - + A + 2 combo. kPadL (0x40) is excluded from pagePrev because B sets it.
        static const PadMap kWiimote = {
            kPadDUp,
            kPadDDown,
            kPadDLeft | kPadWiiMinus,
            kPadDRight | kPadStart,
            kPadA,
            kPadB,
            kPadWiiA,
            kPadWiiMinus,
            kPadWiiB,
            "1",
            "- + B",
            "- + B = Training Menu"
        };
        // Wii Remote + Nunchuk: A confirm (0x200100), B back (0x400200), C is
        // X-equivalent (0x40400), and C+Z (0x40400 + 0x10) is free on CSS
        // (costume+/- use 2 and 1, not C/X).
        static const PadMap kNunchuk = {
            kPadDUp,
            kPadDDown,
            kPadDLeft | kPadWiiMinus,
            kPadDRight | kPadStart,
            kPadA,
            kPadB,
            kPadX | kPadNunchukC,
            kPadX | kPadNunchukC,
            kPadZ,
            "B",
            "C+Z",
            "C+Z = Training Menu"
        };
        static const PadMap kNone = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, "B", "L+R", "L+R = Training Menu"
        };
        switch (controllerType)
        {
        case gfPadType::GCC:
            return kGcc;
        case gfPadType::WII_CLASSIC:
            return kClassic;
        case gfPadType::WIIMOTE:
            return kWiimote;
        case gfPadType::NUNCHUK:
            return kNunchuk;
        default:
            return kNone;
        }
    }

    inline bool reopenPressed(const PadMap& map, u32 held, u32 pressed)
    {
        // Explicitly gated on !D-Down so L+R never collides with P+'s Code Menu (L+R+D-Down).
        if ((held & kPadDDown) != 0)
        {
            return false;
        }
        return map.reopenA != 0 && (held & map.reopenA) != 0 && (held & map.reopenB) != 0
               && (pressed & (map.reopenA | map.reopenB)) != 0;
    }
}
}
