#pragma once

#include <types.h>

#include "shared/block_layout.h"

// Pure module-load policy logic for scMelee (host-testable without Brawl or
// Syriinge headers). Decides whether Training or opted-in local offline Versus
// loads match (+ extra in AssistFigureResource, or OverlayFighter4 fallback in
// <= 3P Versus when Assist Trophies are enabled), while hard-blocking Netplay,
// Replays, and non-sqVsMelee modes.
namespace tm {
namespace policy {
    enum {
        kMeleeGameMode = 0x0,
        kTrainingGameMode = 0xD,
        kMaxVsPlayersForOverlay4Fallback = 3
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
        bool assistTrophiesEnabled;
        u32 activePlayers;
    };

    enum MatchPlan {
        kPlanNone = 0,          // load no modules
        kPlanMatchOnly = 1,     // load match in OverlayMenu (leave fighter/assist heaps untouched)
        kPlanMatchAndExtra = 2  // load match in OverlayMenu + extra in selected heap
    };

    enum ExtraHeapChoice {
        kExtraHeapNone = 0,
        kExtraHeapAssist = 1,   // Heaps::AssistFigureResource (MEM2, 410 KiB)
        kExtraHeapOverlay4 = 2  // Heaps::OverlayFighter4 (MEM1 fallback when VS Assist Trophies ON)
    };

    // True when the stub's resident settings block holds an initialized
    // Settings struct with general.offlineVersus enabled.
    static inline bool offlineVersusOptIn(const u8* b)
    {
        return b != NULL && *reinterpret_cast<const u32*>(b + block::kSettings) == block::kSettingsMagic
               && b[block::kOfflineVersusOffset] != 0;
    }

    // Pure scMelee extra-heap selection.
    static inline ExtraHeapChoice decideExtraHeap(const SceneGateInput& in)
    {
        if (!in.sceneIsMelee)
        {
            return kExtraHeapNone;
        }
        if (in.trainingMode)
        {
            return kExtraHeapAssist;
        }
        if (!in.optInEnabled || !in.isSqVsMelee || in.gameMode != kMeleeGameMode
            || in.isNetplayGct || in.isNetplayVsync || in.isReplayFlag)
        {
            return kExtraHeapNone;
        }
        if (!in.assistTrophiesEnabled)
        {
            return kExtraHeapAssist;
        }
        return in.activePlayers <= kMaxVsPlayersForOverlay4Fallback ? kExtraHeapOverlay4 : kExtraHeapNone;
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
        return decideExtraHeap(in) != kExtraHeapNone ? kPlanMatchAndExtra : kPlanMatchOnly;
    }
}
}
