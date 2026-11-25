#pragma once

#include <types.h>

// Names for the recordings browser: Project+ 3.2.1 character kinds
// (gmCharacterKind) and base stage kinds (srStageKind). Alternate stages
// selected by held buttons share their base stage's srStageKind and are stored
// separately in altStage; unknown kinds get a number.
namespace tm {
namespace menu {
    const char* characterName(u32 kind);
    const char* stageName(u32 kind);
}
}
