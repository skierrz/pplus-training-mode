#pragma once

#include <types.h>

struct gfPadStatus;

// Live pad and stick capture kept by the match module's pad hooks so extra
// modules (such as DI Display) can read raw controller state before lab
// button hiding runs.
namespace tm {
namespace display {
    // The game pads, once per game step (lab.cpp).
    void inputCapture(const gfPadStatus* pads);
    // Port `port`'s sticks as last captured (main x, y, C-stick x, y; pad
    // units, y up) for GameCube (0-3) and Wii (4-7); false if out of range.
    bool inputStick(int port, s8* out);
    // Same as inputStick, also returning gfPadStatus::m_controllerType (0 GCC,
    // 1 Classic, 2 Wiimote, 3 Nunchuk) in `*controllerType` when non-NULL.
    bool inputStickWithType(int port, s8* out, u8* controllerType);

    // One port's pad as a fighter's controls were last made from it: the game's
    // copy in ipSwitch::update (the stub's fighter-pad hook, before the lab
    // hides anything). Ports 0-3 are GameCube controllers, 4-7 Wii Remotes
    // (with their attachments), as gfPadSystem lays them out.
    struct PortPad {
        u32 buttons; // gfPadButtons
        s8 sx, sy, cx, cy;
        u8 l, r;
        bool seen; // captured since the match module was attached
    };
    enum { kPortPads = 8 };
    void inputFighterPad(u32 port, const gfPadStatus* pad);
    // NULL for a port out of range or never captured.
    const PortPad* inputPortPad(int port);
}
}
