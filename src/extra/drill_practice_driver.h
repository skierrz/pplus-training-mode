#pragma once

#include <types.h>

#include "extra/drill_practice_core.h"
#include "main/game/match.h"

class Fighter;
struct gfPadStatus;

// Clean-room Project+ 3.2.1 Tech Practice runtime driver for Powershield
// projectile/physical reps, Ledgedash active cliff-hang box snap & camera
// framing, L-Cancel & Float/Hover/DJC targets, Wavedash GX Target Zone, and
// shared Solo CPU Standby (used by solo drills and Break the Targets!).
namespace tm {
namespace cpu {
    struct Owner;
    struct View;
    struct Frame;
}
namespace extra {
    enum {
        kPracticeDebugMagic = 0x544D5044u, // "TMPD"
        kPracticeDebugVersion = 1u
    };

    struct DrillPracticeDebugBlock {
        u32 magic;             // 0x00: "TMPD"
        u32 version;           // 0x04: 1
        u8 activeDrill;        // 0x08: PracticeDrillId
        u8 cpuInStandby;       // 0x09: 1 while CPU is hidden in Solo Standby
        u8 psResolvedMode;     // 0x0A: PsAttackMode
        u8 psResolvedHeight;   // 0x0B: PsHeight
        u8 ledgeActiveSide;    // 0x0C: 0 left, 1 right
        u8 wdZoneActive;       // 0x0D: 1 while Wavedash GX Target Zone is live
        u8 wdZoneLastOutcome;  // 0x0E: WdZoneOutcome
        u8 targetModeActive;   // 0x0F: PracticeTargetMode
        u32 ledgeSnapCount;    // 0x10
        u32 wdZoneAttempts;    // 0x14
        u32 wdZoneHits;        // 0x18
        u32 wdZoneStreak;      // 0x1C
        u32 wdZoneBestStreak;  // 0x20
        u32 drawCalls;         // 0x24
        float wdZoneMinX;      // 0x28
        float wdZoneMaxX;      // 0x2C
        float wdZoneY;         // 0x30
    };

    // Pure host-testable gating helpers shared with Break the Targets! and drills.
    inline bool shouldSoloCpuStandby(u8 activeDrill, const PracticeConfig& cfg, bool externalStandby)
    {
        return externalStandby || shouldCpuStandby(activeDrill, cfg);
    }

    // Refills P1 shield during drills where repeated L/R presses (wavedash,
    // L-cancel, float/hover, powershield) should never cause shield break.
    inline bool shouldRefillHumanShield(u8 activeDrill)
    {
        return activeDrill == kPracticeDrillWavedash || activeDrill == kPracticeDrillLCancel
               || activeDrill == kPracticeDrillHoverFloat || activeDrill == kPracticeDrillPowershield;
    }

    void drillPracticeAttach();
    void drillPracticeDetach();
    void drillPracticeReset();
    void drillPracticeFrame();
    void drillPracticeDraw(const game::MatchState& match);
    bool drillPracticeCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
    bool drillPracticeOnPad(u32 phase, gfPadStatus* pads);
    bool drillPracticeClaimsDpadLR();
    void drillPracticeSetActiveDrill(u8 drillId);
    void drillPracticeApplyMenu();
    PracticeConfig& drillPracticeConfig();

    // Shared Solo CPU Standby coordination for external solo modes (e.g. Break the Targets!).
    void drillPracticeSetExternalStandby(bool want);
    bool drillPracticeCpuInStandby();

    const WdZoneState* drillPracticeWdZone();
    const DrillPracticeDebugBlock* drillPracticeDebugBlock();
}
}
