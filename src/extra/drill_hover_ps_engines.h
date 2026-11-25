#pragma once

#include <types.h>

#include "extra/drill_float_djc_core.h"
#include "extra/drill_powershield_core.h"

// Live P+ 3.2.1 runtime adapter and RAM debug block ("TM4P") for the
// Hover / Float-Cancel, DJC, and Powershield / PS-Drop pure backends:
//   - Hover / Float-Cancel & DJC (drill_float_djc_core.h)
//   - P+ Powershield & PS-Drop (drill_powershield_core.h)
// Strictly read-only observation across all tracked fighter slots (kMaxTracked).
namespace tm {
namespace extra {
    enum {
        kHoverPsDebugMagic = 0x544D3450, // "TM4P"
        kHoverPsDebugVersion = 1,
        kHoverPsMaxSlots = 8             // matches game::kMaxTracked
    };

    // Naturally aligned 152-byte (0x98) per-slot telemetry snapshot for live
    // Dolphin RAM verification.
    struct HoverPsDebugSlot {
        // Identification (8 bytes, offset 0..8)
        u8 used;
        u8 partner;
        s8 playerNo;
        u8 ftKind;
        s16 curStatus;
        s16 curMotion;

        // Float / Hover Cancel telemetry (44 bytes, offset 8..52)
        u32 fcAttempts;
        u32 fcSuccesses;
        u32 fcStreak;
        u32 fcBestStreak;
        u8 lastFcOutcome;
        u8 lastFcIsHover;
        u8 inFloat;
        u8 floatAerialActive;
        s16 lastFcDuration;
        s16 lastFcAerialFrame;
        s16 lastFcActFrame;
        s16 bestActFrame;
        s16 lastFcMotion;
        u8 lastFcLandingLCancel;
        u8 padFc;
        s32 lastFcFrame;
        float lastFcEntryY;
        float lastFcMinY;

        // DJC telemetry (40 bytes, offset 52..92)
        u32 djcAttempts;
        u32 djcInstant;
        u32 djcStreak;
        u32 djcBestStreak;
        u8 lastDjcGrade;
        u8 lastDjcLandOutcome;
        u8 lastDjcLandedClean;
        u8 lastDjcHadBit33;
        s16 lastDjcFrame;
        s16 lastDjcMotion;
        s32 lastDjcEventFrame;
        float lastDjcVyBefore;
        float lastDjcVyAfter;
        float lastDjcHeight;

        // Powershield & PS-Drop telemetry (60 bytes, offset 92..152)
        u32 psAttempts;
        u32 psPhysicalHits;
        u32 psReflectHits;
        u32 psRePsHits;
        u32 psLightToHardHits;
        u32 psEarlyMisses;
        u32 psLateMisses;
        u32 psBufferedMisses;
        u32 psDropCancels;
        u32 psStreak;
        u32 psBestStreak;
        u8 lastPsOutcome;
        u8 lastPsRePs;
        u8 lastPsLightToHard;
        u8 lastPsDropUsed;
        u8 lastPsDropChainedRePs;
        u8 guardArmed;
        s16 lastPsWindowFrame;
        s16 lastPsOffsetFrames;
        s16 lastPsDropActFrame;
        s32 lastPsEventFrame;
    };

    struct HoverPsDebugBlock {
        u32 magic;      // kHoverPsDebugMagic ("TM4P")
        u32 version;    // kHoverPsDebugVersion
        u32 frame;      // latest tracker frame
        u32 resetCount; // increments on every savestate transient reset
        HoverPsDebugSlot slots[kHoverPsMaxSlots];
    };

    void hoverPsAttach();
    void hoverPsDetach();
    void hoverPsResetTransient();
    void hoverPsResetAll();
    void hoverPsFrame();

    const FloatDjcSlot* hoverPsFloatDjcSlot(int trackedIndex);
    const PsSlot* hoverPsPowershieldSlot(int trackedIndex);
}
}
