#pragma once

#include <types.h>

#include "shared/drill_session.h"

// Session bests on the SD card: two files written in turn ("bests-a.bin",
// "bests-b.bin") so an interrupted write leaves the previous copy intact
// (docs/files.md). Read once per session when the menu initializes the
// session; written on return after a match if any best improved.
namespace tm {
namespace menu {
namespace bests {
    // A new session: the newest file's bests, each the better of the file's
    // and the session's (a blocking read at the scene change).
    void load(drills::Session& s);
    // The list is back after a match: starts a write if a best changed.
    void save(drills::Session& s);
    // Once per rendered frame.
    void update();
    // Waits for a write in flight (the module is about to go).
    void detach();
}
}
}
