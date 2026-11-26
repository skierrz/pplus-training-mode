#pragma once

#include <types.h>

// Brawl/P+-styled v1 -> v2 Settings Update Modal shown once on Training match
// entry when a valid older layout (v1) save is detected on the SD card.
namespace tm {
namespace ui {
    struct SettingsModalInput {
        u32 held;
        u32 pressed;
        s8 stickX;
        s8 stickY;
        u32 controllerType;
    };

    void settingsModalAttach();
    bool settingsModalOpen();
    int settingsModalCursor();
    // Pure single-frame step for host tests and match pads. Returns true on confirm.
    bool settingsModalStep(const SettingsModalInput& in);
    void settingsModalDraw();
}
}
