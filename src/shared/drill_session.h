#pragma once

#include <stddef.h>
#include <types.h>

#include "shared/block_layout.h"
#include "shared/interface.h"
#include "shared/version.h"

// Session handoff between the menu module and match/extra modules through the
// stub's settings block, kept until the game is switched off.
//
// `labChosen` records whether the user entered Training Lab from the CSS menu;
// `drill` and `best[]` are reserved slots for future P+ scenarios and skill
// drills (persisted by menu/bests_file.cpp).
namespace tm {
namespace drills {
    // Drill identifiers (0: none). Stable: they index best[].
    enum DrillId {
        kDrillNone = 0,
        kDrillLedgedash = 1,
        kDrillWavedash = 2,
        kDrillLCancel = 3,
        kDrillHoverFloat = 4,
        kDrillPowershield = 5,
        kDrillPPlusTech = 6,
        kDrillIdCount = 7
    };

    enum {
        kSessionMagic = 0x544D444C, // "TMDL"
        kSessionVersion = 5,        // 2: drill hand-over and bests; 3: sixteen bests; 4: 32; 5: card state
        kSessionSize = 88,
        kSessionOffset = block::kDrillSession,
        kBestSlots = 32
    };

    struct Session {
        u32 magic;      // kSessionMagic
        u8 version;     // kSessionVersion
        u8 labChosen;   // 1: the player chose Training Lab (or closed the list)
        u8 page;        // the page and row the list was left on
        u8 row;
        u16 drillsStarted;
        u8 drill;       // DrillId to run in the next Training match
        u8 drillRan;    // the match module started it
        u8 cardSlot;        // 0/1: which bests file holds cardSequence
        u8 cardNewer;       // 1: newest bests file on the card is a newer format (no writes this session)
        u8 pendingScenario; // extra::ScenarioId (1..10) armed from boot menu; 0 = none
        u8 matchRan;        // 1: a Training match ran; auto-reopen boot menu on return to CSS
        // Session best score per DrillId (0: none yet; higher is better).
        u16 best[kBestSlots];
        u32 cardSequence; // of the newest bests file on the card (0: none)
        u32 cardSum;      // checksum of best[] as the card holds it
    };
    TM_STATIC_ASSERT(sizeof(Session) == kSessionSize, tm_drill_session_size);
    TM_STATIC_ASSERT(offsetof(Session, cardNewer) == 13, tm_drill_session_card_newer_offset);
    TM_STATIC_ASSERT(offsetof(Session, pendingScenario) == 14, tm_drill_session_pending_scenario_offset);
    TM_STATIC_ASSERT(offsetof(Session, matchRan) == 15, tm_drill_session_match_ran_offset);
    TM_STATIC_ASSERT(offsetof(Session, best) == 16, tm_drill_session_best_offset);
    TM_STATIC_ASSERT(offsetof(Session, cardSequence) == 80, tm_drill_session_card_seq_offset);
    TM_STATIC_ASSERT(offsetof(Session, cardSum) == 84, tm_drill_session_card_sum_offset);
    TM_STATIC_ASSERT(kSessionOffset + kSessionSize <= block::kDrillSessionEnd, tm_drill_session_in_its_range);
    TM_STATIC_ASSERT(kDrillIdCount <= kBestSlots, tm_drill_bests_fit);

    // Higher is better; 0 is "none yet".
    inline u16 betterBest(int id, u16 a, u16 b)
    {
        (void)id;
        if (a == 0 || b == 0)
        {
            return a != 0 ? a : b;
        }
        return a > b ? a : b;
    }

    // The session in `block` if it holds one, else NULL.
    inline Session* sessionIn(u8* block)
    {
        if (block == NULL)
        {
            return NULL;
        }
        Session* s = reinterpret_cast<Session*>(block + kSessionOffset);
        return s->magic == kSessionMagic && s->version == kSessionVersion ? s : NULL;
    }
}
}
