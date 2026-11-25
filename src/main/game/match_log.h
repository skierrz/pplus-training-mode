#pragma once

#include <types.h>

#include "main/game/match.h"

namespace tm {
namespace game {
    // Logs each change of the match state once (start, pause, resume).
    // `call` is the stub's render-call count.
    void logMatchChange(const MatchState& now, u32 call);
}
}
