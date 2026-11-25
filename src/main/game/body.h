#pragma once

#include <types.h>

class Fighter;
class soModuleEnumeration;

// Values of one fighter beyond what the trackers keep, read at the end of a
// game frame for the technique displays, the colour overlays and the tech
// invisibility (docs/internals.md, "Fighter body values").
namespace tm {
namespace game {
    struct Body {
        float x, y;
        float vy;          // y change over the last frame
        float stickX;      // control stick, -1..1, right and up positive
        float stickY;
        u32 buttons;       // ipPadButton bits held
        s32 xluFrames;     // body intangibility frames left (ledge, respawn)
        u8 intangible;     // the body's hurtboxes are invincible or intangible
        u8 fastfall;       // P+'s fastfall flag
        u8 shortHop;       // the short-hop flag the jump squat sets
        u8 landingLag;     // the move's landing lag is on (clear: autocancel)
        s32 lCancel;       // P+'s L-cancel counter (LA int 90): > 0 on landing = success
        s32 motionKind;    // subaction
        s32 motionFrame;   // its animation frame, 0 on the first
        float clatter;     // grab timer while grabbed (mashing takes it down)
        u8 powershield;    // P+'s powershield sound bit (LA bit 96)
        u8 invFrames;      // body invincibility frames left (up to 255)
    };

    bool readBody(Fighter* fighter, Body& out);

    // The status work's flag word 0 and P+'s L-cancel counter, as in Body
    // (0 when not readable), for sampling at a status change.
    void readWorkValues(const soModuleEnumeration* m, u32& flags, s32& lCancel);

    // Instance work ("LA") int `index`, 0 if not readable.
    s32 instanceInt(const soModuleEnumeration* m, u32 index);

    // Project+'s extra data block of the fighter (body.cpp), or NULL.
    const void* extraInfo(const soModuleEnumeration* m);

    // ipPadButton bits of the jump and shield buttons.
    enum { kButtonJump = 1 << 2, kButtonGuard = 1 << 3 };
}
}
