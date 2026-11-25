#pragma once

#include <types.h>

#include "main/game/match.h"

// The match module's use of the SD card: the lab's settings, Export and the
// match side of an import (settings_file.h, recording_file.h).
namespace tm {
namespace storage {
    // After main::loadSettings(block), before the features read the settings.
    void attach(u8* block);
    // Before the module goes: finishes what is in flight.
    void detach();
    // After the lab drew.
    void onRender(const game::MatchState& match);
}
}
