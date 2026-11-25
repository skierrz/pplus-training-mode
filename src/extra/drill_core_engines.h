#pragma once

#include <types.h>

#include "extra/drill_lcancel_core.h"
#include "extra/drill_ledge_core.h"
#include "extra/drill_wavedash_core.h"
#include "main/game/tracker.h"

// Live Project+ 3.2.1 runtime adapter for the core skill drill
// backends (Ledgedash, Wavedash/Waveland, L-Cancel). Strictly read-only
// observation over all tracked fighters (kMaxTracked = 8, including Ice
// Climbers partners), with a "TM4C" RAM debug block logged once on attach().
namespace tm {
namespace extra {
    enum {
        kCoreDrillDebugMagic = 0x544D3443u, // "TM4C"
        kCoreDrillDebugVersion = 1u
    };

    struct CoreDrillDebugSlot {
        // Slot header (8 bytes).
        u8 used;
        s8 playerNo;
        u8 partner;
        u8 cpu;
        u32 frame;

        // Engine 1: Ledgedash (184 bytes).
        u32 ledgeSeq;
        u8 ledgeOutcome;
        u8 ledgeFailFlags;
        u8 ledgeGrabCount;
        u8 ledgeFromTether;
        u8 ledgeUsedFly;
        u8 ledgeTourneySlow;
        u8 ledgeJumpXluStripped;
        u8 ledgeCellCount;
        s16 ledgeGalint;
        s16 ledgeDropFrame;
        s16 ledgeCliffWaitFrames;
        s16 ledgeNeutralLatchFrames;
        s16 ledgeFallFrames;
        s16 ledgeJumpFrames;
        s16 ledgeAirdodgeAirFrames;
        s16 ledgeLandingLagFrames;
        float ledgeAirdodgeAngle;
        u8 ledgeActivePhase;
        u8 ledgeActiveGrabs;
        u8 ledgeActiveNoInvuln;
        u8 ledgeActiveFromTether;
        u16 ledgeAttempts;
        u16 ledgeSuccesses;
        u16 ledgeTourneyWinners;
        u16 ledgeSds;
        u16 ledgeNoInvulnGrabs;
        s16 ledgeBestGalint;
        u16 ledgeStreak;
        u16 ledgeBestStreak;
        u8 ledgeHasBestGalint;
        u8 ledgePad[3];
        u8 ledgeCells[kLedgeCoreMaxCells];

        // Engine 2: Wavedash & Platform Waveland (48 bytes).
        u32 wdSeq;
        u8 wdKind;
        u8 wdOutcome;
        u8 wdShortHop;
        u8 wdOnPlatform;
        s16 wdJumpsquatToDodge;
        s16 wdJumpHoldFrames;
        s16 wdAirdodgeAirFrames;
        s16 wdPad;
        float wdAngleDeg;
        float wdSlideDistance;
        float wdExpectedSpeed;
        u16 wdAttempts;
        u16 wdSuccesses;
        u16 wdPerfect;
        u16 wdStreak;
        u16 wdBestStreak;
        u16 wlAttempts;
        u16 wlSuccesses;
        u16 wlPerfect;
        u16 wlStreak;
        u16 wlBestStreak;

        // Engine 3: L-Cancel (44 bytes).
        u32 lcSeq;
        u8 lcOutcome;
        u8 lcContext;
        u8 lcInputKind;
        u8 lcAutoLCancelActive;
        u8 lcBuffer3fActive;
        u8 lcPad;
        s16 lcPressFrame;
        s16 lcEarlyFrames;
        s16 lcLateFrames;
        u16 lcTotal;
        u16 lcSuccesses;
        u16 lcAutoSuccesses;
        u16 lcAutoCanceled;
        u16 lcWhiffHits;
        u16 lcWhiffTotal;
        u16 lcHitHits;
        u16 lcHitTotal;
        u16 lcShieldHits;
        u16 lcShieldTotal;
        u16 lcLateMisses;
        u16 lcEarlyMisses;
        u16 lcStreak;
        u16 lcBestStreak;
    };

    struct CoreDrillDebugBlock {
        u32 magic;
        u32 version;
        u32 frame;
        u32 slotSize;
        u32 savestateResets;
        u8 autoLCancelActive;
        u8 inputBuffer3fActive;
        u8 maxSlots;
        u8 reserved;
        CoreDrillDebugSlot slots[game::kMaxTracked];
    };

    void coreDrillEnginesAttach();
    void coreDrillEnginesDetach();
    void coreDrillEnginesFrame();
    void coreDrillEnginesSavestateReset();

    // Read-only accessors for downstream drill orchestrators / UIs.
    const LedgeCoreState* coreDrillLedgeState(int tracked);
    const WdCoreState* coreDrillWavedashState(int tracked);
    const LcCoreState* coreDrillLCancelState(int tracked);
    bool coreDrillAutoLCancelActive();
    bool coreDrillInputBuffer3fActive();
}
}
