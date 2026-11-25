#pragma once

#include <types.h>

struct gfPadStatus;

// Training Menu overlay drawn over Training's character select: opens
// automatically unless Training Lab was chosen earlier in the session, reopens
// via the controller reopen combo (L+R on GC/Classic), and captures pads while
// open.
namespace tm {
namespace menu {
namespace list {
    void attach();
    void detach();
    void onRender(u32 call);
    void onPads(u32 phase, gfPadStatus* pads);
    // Reopen hint for the controller type present (GameCube if several).
    const char* reopenHint();
}
}
}
