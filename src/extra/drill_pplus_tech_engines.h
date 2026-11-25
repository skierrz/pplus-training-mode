#pragma once

#include <types.h>

#include "extra/drill_breverse_core.h"
#include "extra/drill_dacus_core.h"
#include "extra/drill_floorhug_core.h"
#include "extra/drill_footstool_agt_core.h"
#include "extra/drill_rar_core.h"

// Live P+ 3.2.1 runtime adapter and RAM debug telemetry block ("TM4T") for
// the five pure P+-exclusive tech and Floorhug / ASDI-Down detection engines.
// Strictly read-only observation; zero UI, zero heap allocation.
namespace tm {
namespace extra {
    enum {
        kPPlusTechDebugMagic = 0x544D3454, // "TM4T"
        kPPlusTechDebugVersion = 1,
        kPPlusTechDebugSlots = 4
    };

    // Per-player telemetry snapshot laid out with explicit 4-byte alignment
    // (152 bytes / 0x98 per slot) for live RAM inspection.
    struct PPlusTechDebugSlot {
        // Engine 1: RAR (0x00 .. 0x13, 20 bytes)
        u32 rarAttempts;
        u32 rarBairCount;
        u8 rarOutcome;
        u8 rarTurnRunFrames;
        u8 rarBairDelay;
        u8 rarBackwardMomentum;
        float rarMomentumRetained;
        float rarTakeoffVx;

        // Engine 2: DACUS & Gatling (0x14 .. 0x2B, 24 bytes)
        u32 dacusAttempts;
        u32 dacusSuccessCount;
        u32 dacusGatlingCount;
        u8 dacusOutcome;
        u8 dacusRawDashFrame;
        u8 dacusNonHitlagFrame;
        u8 dacusGatling;
        float dacusVxCarry;
        float dacusSlideDistance;

        // Engine 3: B-Reverse & Wavebounce (0x2C .. 0x4B, 32 bytes)
        u32 brevAttempts;
        u32 brevBReverseCount;
        u32 brevWavebounceCount;
        u32 brevTurnaroundCount;
        u8 brevOutcome;
        u8 brevEarlyFlick;
        u8 brevLateFlick;
        u8 brevAirborne;
        float brevVxBefore;
        float brevVxAfter;
        float brevMomentumRatio;

        // Engine 4: Footstool & Lock + AGT (0x4C .. 0x73, 40 bytes)
        u32 footstoolCount;
        u32 footstoolLockCount;
        u32 agtCount;
        u32 agtGroundedCount;
        u8 footstoolOutcome;
        u8 footstoolShortHop;
        u8 footstoolVictimTeched;
        u8 footstoolLockHit;
        s16 footstoolTauntLockout;
        s16 agtCurrentChain;
        u8 agtIsAerial;
        u8 agtThrowFrame;
        u8 agtLastChainCount;
        u8 agtChainResetCount;
        float agtVxCarry;
        float agtVyCarry;
        u32 footstoolFailLockoutCount;

        // Engine 5: Floorhug / ASDI-Down & Crouch-Cancel (0x74 .. 0x97, 36 bytes)
        u32 fhHitCount;
        u32 fhCancelCount;
        u32 fhTruePunishCount;
        u8 fhOutcome;
        u8 fhWasCrouching;
        u8 fhAsdiDown;
        u8 fhAsdiFromCStick;
        s16 fhHitstunAtLanding;
        s16 fhHitstunSaved;
        s16 fhPunishActFrame;
        u8 fhTrueCounterPunish;
        u8 fhPad;
        float fhEffectiveAsdiX;
        float fhEffectiveAsdiY;
        float fhLandingFrame;
    };

    struct PPlusTechDebugBlock {
        u32 magic;      // 0x00: kPPlusTechDebugMagic ("TM4T")
        u32 version;    // 0x04: kPPlusTechDebugVersion
        u32 frame;      // 0x08: trackerFrame()
        u32 resetCount; // 0x0C: incremented on savestate / explicit reset
        PPlusTechDebugSlot slots[kPPlusTechDebugSlots];
    };

    void pplusTechAttach();
    void pplusTechDetach();
    void pplusTechReset();
    void pplusTechFrame();

    const RarState* pplusTechRar(int trackedSlot);
    const DacusState* pplusTechDacus(int trackedSlot);
    const BReverseState* pplusTechBReverse(int trackedSlot);
    const FootstoolAgtState* pplusTechFootstoolAgt(int trackedSlot);
    const FloorhugState* pplusTechFloorhug(int trackedSlot);
    const PPlusTechDebugBlock* pplusTechDebugBlock();
}
}
