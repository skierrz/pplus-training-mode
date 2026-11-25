#pragma once

#include <types.h>

#include "shared/block_layout.h"

// Pure module-load policy logic for scMelee (host-testable without Brawl or
// Syriinge headers). Decides whether Training or opted-in local offline Versus
// loads match (+ extra when <= 3 active players leave OverlayFighter4 free),
// while hard-blocking Netplay, Replays, and non-sqVsMelee modes.
namespace tm {
namespace policy {
    enum {
        kMeleeGameMode = 0x0,
        kTrainingGameMode = 0xD,
        kMaxVsPlayersForExtra = 3
    };

    struct SceneGateInput {
        bool sceneIsMelee;
        bool trainingMode;
        bool isSqVsMelee;
        int gameMode;
        bool isNetplayGct;
        bool isNetplayVsync;
        bool isReplayFlag;
        bool optInEnabled;
        u32 activePlayers;
    };

    enum MatchPlan {
        kPlanNone = 0,          // load no modules
        kPlanMatchOnly = 1,     // load match in OverlayMenu (leave OverlayFighter1..4 untouched)
        kPlanMatchAndExtra = 2  // load match in OverlayMenu + extra in OverlayFighter4
    };

    // True when the stub's resident settings block holds an initialized
    // Settings struct with general.offlineVersus enabled.
    static inline bool offlineVersusOptIn(const u8* b)
    {
        return b != NULL && *reinterpret_cast<const u32*>(b + block::kSettings) == block::kSettingsMagic
               && b[block::kOfflineVersusOffset] != 0;
    }

    // Pure scMelee load decision.
    static inline MatchPlan decideMeleeLoad(const SceneGateInput& in)
    {
        if (!in.sceneIsMelee)
        {
            return kPlanNone;
        }
        if (in.trainingMode)
        {
            return kPlanMatchAndExtra;
        }
        if (!in.optInEnabled || !in.isSqVsMelee || in.gameMode != kMeleeGameMode
            || in.isNetplayGct || in.isNetplayVsync || in.isReplayFlag)
        {
            return kPlanNone;
        }
        return in.activePlayers <= kMaxVsPlayersForExtra ? kPlanMatchAndExtra : kPlanMatchOnly;
    }
}
}
