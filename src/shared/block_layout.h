#pragma once

#include "shared/interface.h"

// The stub's settings block: iface::kSettingsBlockSize bytes the resident
// part keeps from boot (all zero) until the game is switched off. The match
// module and the menu module are loaded and unloaded many times in between;
// what they must remember lives here. This is the one map of who owns which
// bytes; each owner's header takes its offset from here and checks that it
// fits in its range.
//
//   0   .. 576  the lab's settings        main::Settings      (main/settings.h)
//   576 .. 592  the settings file's state storage::State      (main/storage/block.h)
//   592 .. 640  a recording to import     storage::Handoff    (main/storage/block.h)
//   640 .. 768  the menu session          drills::Session     (shared/drill_session.h)
namespace tm {
namespace block {
    enum {
        kSize = iface::kSettingsBlockSize,
        kSettings = 0,
        kSettingsEnd = 576,
        kStorageState = 576,
        kStorageStateEnd = 592,
        kStorageHandoff = 592,
        kStorageHandoffEnd = 640,
        kDrillSession = 640,
        kDrillSessionEnd = kSize
    };
}
}
