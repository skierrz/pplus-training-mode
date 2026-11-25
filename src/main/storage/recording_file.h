#pragma once

#include <types.h>

// Recording > Export and the match side of an import (docs/files.md).
//
// Export writes the recording set as it is: every slot with inputs (both
// sides), the Recording, CPU and CPU tables areas, the lite start state (both
// fighters' position, facing, damage, shield), the characters, costumes and
// stage, and a header the browser lists. The savestate behind the positions
// is not stored: it only loads into the match it was taken in.
//
// Import: the menu module's browser starts a match with the file's
// characters, costumes and stage and leaves the file's name in the stub's
// block; here the file is read once the match runs, its slots and settings
// put back, the fighters placed and the positions saved again in this match
// (a savestate, so playback is exact from there on).
namespace tm {
namespace storage {
    void recordingAttach(u8* block);
    // Once per rendered frame.
    void recordingUpdate();
    // Waits for a write or read in flight and frees its buffer.
    void recordingDetach();

    // The Recording page's Export row.
    void exportRecording();
    bool exportAvailable();
}
}
