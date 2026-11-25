#include "main/game/match_log.h"

#include "shared/game/frame.h"
#include "shared/log.h"

namespace tm {
namespace game {
    // The module is loaded fresh for every match, so this starts out clear.
    static MatchState s_last = {false, false, false};

    void logMatchChange(const MatchState& now, u32 call)
    {
        if (now.running != s_last.running)
        {
            if (now.running)
            {
                TM_LOG("match: started (%s) at render call %u, frame counter %u\n",
                       now.training ? "Training" : "not Training", call, frameCounter());
            }
            else
            {
                TM_LOG("match: ended at render call %u\n", call);
            }
        }
        // A match that ends while paused is not reported as resumed.
        if (now.paused != s_last.paused && now.running)
        {
            TM_LOG("match: %s at render call %u, frame counter %u\n",
                   now.paused ? "paused" : "resumed", call, frameCounter());
        }
        s_last = now;
    }
}
}
