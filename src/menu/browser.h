#pragma once

#include <types.h>

// Recordings Browser, opened from the Training Menu (X): lists exported SD
// recordings newest first, ten per page. A loads (starts the recording's match
// and hands off the file to the match module), X deletes after confirmation,
// and B returns to the Training Menu.
namespace tm {
namespace menu {
namespace browser {
    struct Input {
        u32 held;
        u32 pressed;
        s8 stickX, stickY;
        u32 controllerType;
    };

    void attach();
    // Waits for a request in flight and frees what it holds.
    void detach();

    // Lists the folder (a synchronous FA listing) and starts reading headers.
    void open(int port, u32 controllerType = 0);
    void setControllerType(u32 controllerType);
    bool isOpen();
    // One pass of input while open, for `port`. `starting` turns true once a match was started.
    void step(int port, const Input& in, bool& starting);
    // Every rendered frame: the requests in flight.
    void update();
    // Inside a gfx::begin()/end() pass.
    void draw();
}
}
}
