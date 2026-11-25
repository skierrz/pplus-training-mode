#pragma once

#include <types.h>

// The lab's settings on the SD card: two files written in turn
// ("settings-a.bin", "settings-b.bin", each with a save count), so a write
// cut short leaves the other one intact; the newest valid one is read.
// Read once per session, when the first match attaches (a blocking read at
// the scene change); written asynchronously when the lab menu closes, if
// the saved view of the settings changed. Some fields are never restored
// at boot (kSessionOnly in settings_file.cpp, listed in docs/files.md).
namespace tm {
namespace storage {
    // After main::loadSettings(block).
    void settingsAttach(u8* block);
    void settingsMenuClosed();
    // Reset Lab Settings: the next menu close writes the defaults.
    void settingsReset();
    // Once per rendered frame.
    void settingsUpdate();
    // Waits for a write in flight (the module is about to go).
    void settingsDetach();
}
}
