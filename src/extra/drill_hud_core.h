#pragma once

#include <cstdio>
#include <string.h>
#include <types.h>

#include "extra/challenge_cancel_sprint_core.h"
#include "extra/challenge_flow_course_core.h"
#include "extra/challenge_reaction_techchase_core.h"
#include "extra/challenge_recovery_floorhug_core.h"
#include "extra/challenge_snake_recovery_core.h"
#include "extra/drill_breverse_core.h"
#include "extra/drill_dacus_core.h"
#include "extra/drill_float_djc_core.h"
#include "extra/drill_lcancel_core.h"
#include "extra/drill_ledge_core.h"
#include "extra/drill_powershield_core.h"
#include "extra/drill_rar_core.h"
#include "extra/drill_wavedash_core.h"
#include "shared/drill_session.h"

// Pure host-testable Tech Practice & Challenge HUD state machine.
// Defaults to infinite Free Practice (zero forced modal freeze) while exposing
// optional scored 10/20-Rep Sets and 60s Timed mode with End-Screen Summary.
// Consumes read-only counters/results from existing Stage 4A and Stage 4C
// engines with zero game-state detection duplication.
namespace tm {
namespace extra {
    enum {
        kDrillSetReps10 = 10,
        kDrillSetReps20 = 20,
        kDrillMaxSetReps = 20,
        kDrillFreePips = 10,
        kDrillTimedFrames = 3600, // 60 seconds at 60 fps
        kDrillFeedbackBuf = 28,
        kDrillHighlightBuf = 24
    };

    enum DrillPracticeMode {
        kDrillModeFree = 0,    // default: infinite continuous grinding
        kDrillModeSet10 = 1,   // optional scored 10-rep set
        kDrillModeSet20 = 2,   // optional scored 20-rep set
        kDrillModeTimed60 = 3, // optional 60-second timed sprint
        kDrillModeCount = 4
    };

    enum DrillGrade {
        kDrillGradeNone = 0,
        kDrillGradePerfect = 1,
        kDrillGradeGood = 2,
        kDrillGradeMiss = 3,
        kDrillGradeWarn = 4
    };

    enum DrillRepPip {
        kRepPipEmpty = 0,
        kRepPipPerfect = 1,
        kRepPipGood = 2,
        kRepPipMiss = 3,
        kRepPipWarn = 4
    };

    struct DrillEngineFeed {
        const LedgeCoreState* ledge;
        const WdCoreState* wd;
        const LcCoreState* lc;
        const FloatDjcSlot* fd;
        const PsSlot* ps;
        const RarState* rar;
        const DacusState* dacus;
        const BReverseState* brev;
        const CancelSprintState* cs;
        const FlowCourseState* fc;
        const ReactionTechchaseState* rt;
        const RecoveryFloorhugState* rf;
        const SnakeRecoveryState* snk;
        bool autoLCancel;
        bool ineligibleFighter;
    };

    // Prefix layout (activeDrill .. bestMetric, 14 bytes) mirrors
    // DrillHudDebugBlock offsets 0x08..0x15 for single-memcpy telemetry sync.
    struct DrillHudState {
        u8 activeDrill;  // 0x00: drills::DrillId (0..15)
        u8 repsDone;     // 0x01: clamped 0..255 (or 0..targetReps in Set mode)
        u8 repsHit;      // 0x02: clamped 0..255 (or 0..targetReps in Set mode)
        u8 streak;       // 0x03: current hit streak (clamped 0..255)
        u8 bestStreak;   // 0x04: best hit streak this run (clamped 0..255)
        u8 newBest;      // 0x05: 1 if sessionBest improved
        u8 modalOpen;    // 0x06: 1 while End-Screen Modal is open (Set/Timed modes)
        u8 lastGrade;    // 0x07: DrillGrade
        u16 sessionBest; // 0x08: persisted best streak / score for activeDrill
        u16 retryCount;  // 0x0A
        s16 bestMetric;  // 0x0C
        u8 mode;         // DrillPracticeMode (default kDrillModeFree)
        u8 hasBestMetric;
        u8 alcNotice;
        u8 freePipCount;
        u16 totalAtt;
        u16 totalHit;
        u16 streakFull;
        u16 bestStreakFull;
        u16 timerFrames; // remaining frames in kDrillModeTimed60 (3600..0)
        u8 repPips[kDrillMaxSetReps];
        u32 prev[3];
        char lastFeedback[kDrillFeedbackBuf];
    };

    inline bool isChallengeDrill(u8 drillId)
    {
        return drillId >= drills::kDrillCancelSprint && drillId <= drills::kDrillSnakeRecovery;
    }

    inline bool isValidSkillDrill(u8 drillId)
    {
        return drillId >= drills::kDrillLedgedash && drillId <= drills::kDrillSnakeRecovery;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline u8 drillModeTargetReps(u8 mode)
    {
        if (mode == kDrillModeSet10)
        {
            return kDrillSetReps10;
        }
        if (mode == kDrillModeSet20)
        {
            return kDrillSetReps20;
        }
        return 0;
    }

    inline const char* drillNthStr(const char* p, int idx)
    {
        while (idx-- > 0)
        {
            while (*p++ != '\0') {}
        }
        return p;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline const char* drillModeLabel(u8 mode)
    {
        static const char kLabels[] = "FREE\0" "10-REP\0" "20-REP\0" "60s";
        return drillNthStr(kLabels, mode < kDrillModeCount ? mode : 0);
    }

    inline const char* drillHudTitle(u8 drillId)
    {
        static const char kNames[] =
            "Tech Practice\0Ledgedash\0Wavedash\0L-Cancel\0Float / Hover\0"
            "Powershield\0RAR\0DACUS\0B-Reverse\0Cancel Endurance\0"
            "Movement Flow\0Reaction Lab\0Techchase Esc\0Sweetspot\0CC / ASDI-Down\0"
            "Snake C4 Recovery\0Techchase\0Break the Targets!";
        return drillNthStr(kNames, drillId < drills::kDrillIdCount ? drillId : 0);
    }

    inline const char* drillHighlightLabel(u8 drillId)
    {
        static const char kLabels[] =
            "Highlight\0Peak GALINT\0Frame-1 Perfect\0"
            "Fastest Window\0Best Act / Instant\0PS-Drop / Reflect\0"
            "Fastest Turn\0Gatling Cancels\0Wavebounces\0"
            "Fastest Cancel\0Fastest Course\0Fastest Reaction\0"
            "Tightest Escape\0Sweetspot Grabs\0Max Hitstun Saved\0"
            "Best Loop Chain\0Highlight\0Best Chain";
        return drillNthStr(kLabels, drillId < drills::kDrillIdCount ? drillId : 0);
    }

    inline void drillHudFormatHighlight(const DrillHudState& st, char* out, int outSize)
    {
        size_t sz = static_cast<size_t>(outSize);
        u8 d = st.activeDrill;
        u8 target = drillModeTargetReps(st.mode);
        if (d == drills::kDrillLCancel && st.alcNotice)
        {
            snprintf(out, sz, "Auto L-Cancel On");
            return;
        }
        if (d == drills::kDrillWavedash && target > 0)
        {
            snprintf(out, sz, "%d / %u", static_cast<int>(st.bestMetric), static_cast<unsigned>(target));
            return;
        }
        // Per-drill format index (0="--", 1="None", 2="%+df", 3="%d", 4="Frame %d/7",
        // 5="%d clean", 6="Frame %d", 7="%df", 8="+%df", 9="%d loops"); bit 0x80 = requires hasBestMetric.
        static const char kFmt[] =
            "--\0None\0%+df\0%d\0Frame %d/7\0%d clean\0Frame %d\0%df\0+%df\0%d loops";
        static const u8 kRule[drills::kDrillIdCount] = {
            0, 0x82, 3, 0x84, 0x85, 3, 0x86, 3, 3, 0x86, 0x87, 0x87, 0x88, 3, 0x87, 9, 0, 0
        };
        u8 r = d < drills::kDrillIdCount ? kRule[d] : 0u;
        int fi = r & 0x7Fu;
        if ((r & 0x80u) != 0 && !st.hasBestMetric)
        {
            fi = (d == drills::kDrillLedgedash) ? 1 : 0;
        }
        snprintf(out, sz, drillNthStr(kFmt, fi), static_cast<int>(st.bestMetric));
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline void drillHudInitMode(DrillHudState& st, u8 drillId, u16 priorBest, u8 mode)
    {
        memset(&st, 0, sizeof(st));
        st.activeDrill = isValidSkillDrill(drillId) ? drillId : static_cast<u8>(drills::kDrillNone);
        st.sessionBest = priorBest;
        st.mode = mode < kDrillModeCount ? mode : static_cast<u8>(kDrillModeFree);
        st.timerFrames = st.mode == kDrillModeTimed60 ? static_cast<u16>(kDrillTimedFrames) : 0u;
        strcpy(st.lastFeedback, "READY");
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline void drillHudInit(DrillHudState& st, u8 drillId, u16 priorBest)
    {
        drillHudInitMode(st, drillId, priorBest, kDrillModeFree);
    }

    inline void drillHudSetMode(DrillHudState& st, u8 mode)
    {
        if (!isValidSkillDrill(st.activeDrill))
        {
            return;
        }
        u16 r = st.retryCount;
        u16 best = st.sessionBest;
        u8 d = st.activeDrill;
        drillHudInitMode(st, d, best, mode);
        st.retryCount = r;
    }

    inline void drillHudCycleMode(DrillHudState& st)
    {
        u8 mod = isChallengeDrill(st.activeDrill) ? static_cast<u8>(kDrillModeCount) : static_cast<u8>(kDrillModeTimed60);
        u8 next = static_cast<u8>((st.mode + 1u) % mod);
        drillHudSetMode(st, next);
    }

    inline void drillHudRetry(DrillHudState& st)
    {
        if (isValidSkillDrill(st.activeDrill))
        {
            u16 r = static_cast<u16>(st.retryCount + 1);
            u8 mode = st.mode;
            drillHudInitMode(st, st.activeDrill, st.sessionBest, mode);
            st.retryCount = r;
        }
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline void drillHudComplete(DrillHudState& st)
    {
        if (!isValidSkillDrill(st.activeDrill))
        {
            return;
        }
        u8 target = drillModeTargetReps(st.mode);
        u16 score = st.totalHit;
        if (st.mode == kDrillModeTimed60)
        {
            st.timerFrames = 0;
        }
        else
        {
            if (target == 0)
            {
                target = kDrillSetReps10;
            }
            st.repsDone = target;
            if (score > target)
            {
                score = target;
            }
        }
        st.modalOpen = 1;
        u16 m = drills::betterBest(st.activeDrill, st.sessionBest, score);
        if (m > st.sessionBest)
        {
            st.sessionBest = m;
            st.newBest = 1;
        }
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Advances the 60s Timed mode countdown by one live gameplay frame.
    // Returns true on the exact frame the timer expires and opens the modal.
    inline bool drillHudTickTimer(DrillHudState& st)
    {
        if (!isValidSkillDrill(st.activeDrill) || st.mode != kDrillModeTimed60 || st.modalOpen)
        {
            return false;
        }
        if (st.timerFrames > 0u)
        {
            st.timerFrames--;
        }
        if (st.timerFrames == 0u)
        {
            drillHudComplete(st);
            return true;
        }
        return false;
    }

    // Shared baseline sync (syncOnly = true) and live event step (syncOnly = false).
    inline bool drillHudPoll(DrillHudState& st, const DrillEngineFeed& f, bool syncOnly)
    {
        u8 d = st.activeDrill;
        if (!isValidSkillDrill(d) || (!syncOnly && st.modalOpen))
        {
            return false;
        }

        // Single packed NUL-separated feedback string table (shared phrases reused across drills).
        static const char kMsg[] =
            "GALINT +%df INVULN\0"    // 0
            "GRAB %d/5 (NO INVULN)\0" // 1
            "GALINT %df (VULN)\0"     // 2
            "TOURNEY WINNER\0"        // 3
            "LEDGESLIP / SD\0"        // 4
            "NO AIRDODGE\0"           // 5
            "LEDGE GETUP\0"           // 6
            "PERFECT f1\0"            // 7
            "WAVEDASH f%d\0"          // 8
            "WAVELAND f%d\0"          // 9
            "LATE AIRDODGE\0"         // 10
            "UPWARD DODGE\0"          // 11
            "ZAIR / TETHER\0"         // 12
            "L-CANCEL f%d/7\0"        // 13
            "AUTO L-CANCEL ON\0"      // 14
            "AUTOCANCELED\0"          // 15
            "EARLY -%df\0"            // 16
            "LATE +%df\0"             // 17
            "MISSED L-CANCEL\0"       // 18
            "FLOAT CANCEL\0"          // 19
            "HOVER CANCEL\0"          // 20
            "DROPPED FLOAT\0"         // 21
            "INSTANT DJC f%d\0"       // 22
            "LOW DJC f%d\0"           // 23
            "LATE DJC f%d\0"          // 24
            "POWERSHIELD f%d\0"       // 25
            "PS REFLECT f%d\0"        // 26
            "PS-DROP f%d\0"           // 27
            "BUFFERED PS\0"           // 28
            "EARLY PS %df\0"          // 29
            "LATE PS +%df\0"          // 30
            "RAR BAIR f%d\0"          // 31
            "RAR JUMP\0"              // 32
            "RAR MISS\0"              // 33
            "GATLING DACUS f%d\0"     // 34
            "DACUS f%d\0"             // 35
            "DACUS MISS\0"            // 36
            "TURNAROUND B\0"          // 37
            "B-REVERSE\0"             // 38
            "WAVEBOUNCE\0"            // 39
            "PICK PEACH/MEWTWO/DJC\0" // 40
            "PERFECT CANCEL f1\0"     // 41
            "CANCEL f%d\0"            // 42
            "CANCEL MISS\0"           // 43
            "COURSE %df\0"            // 44
            "GATE %d/4 CLEAR\0"       // 45
            "REACT %df\0"             // 46
            "PERFECT ESC f0\0"        // 47
            "ESCAPE +%df\0"           // 48
            "CAUGHT\0"                // 49
            "SWEETSPOT +%df\0"        // 50
            "HIGH OVERSHOOT\0"        // 51
            "SD / MISSED LEDGE\0"     // 52
            "CC / ASDI-DN -%df\0"     // 53
            "ASDI-DOWN TECH\0"        // 54
            "BROKEN TUMBLE\0"         // 55
            "MISSED ASDI-DN\0"        // 56
            "TRUE PUNISH!\0"          // 57
            "C4 REFRESH x%d\0"        // 58
            "NADE REFRESH x%d\0"      // 59
            "SELF-HIT x%d\0"          // 60
            "RECOVERED x%d\0"         // 61
            "PICK SPACIE/PK/M2/LUC\0" // 62
            "PICK SNAKE";             // 63

        int msg = -1;
        int arg = 0;
        bool useWdAngle = false;
        float wdAngle = 0.0f;
        bool wdPerfect = false;
        u8 ev = 0;  // 1=missRep, 2=warnRep, 3=goodRep, 4=perfRep, 5=missMsg, 6=warnMsg, 7=perfMsg
        u8 mop = 0; // 1=inc, 2=max(arg), 3=min(arg)

        switch (d)
        {
        case drills::kDrillLedgedash:
            if (f.ledge)
            {
                u32 s = f.ledge->seq;
                if (!syncOnly && s != st.prev[0])
                {
                    const LedgeResult& r = f.ledge->last;
                    if (r.outcome != kLedgeOutcomeInterrupted && r.outcome != kLedgeOutcomeNone)
                    {
                        ev = 1;
                        if (r.outcome == kLedgeOutcomeLedgedash && r.galint > 0 && !r.noXlu)
                        {
                            mop = 2;
                            msg = 0;
                            arg = r.galint;
                            ev = (r.galint >= 6 && r.dropFrame <= 1) ? 4 : 3;
                        }
                        else if (r.outcome == kLedgeOutcomeNoInvulnLimit)
                        {
                            msg = 1;
                            arg = r.grabCount > 5 ? r.grabCount : 6;
                            ev = 2;
                        }
                        else if (r.outcome == kLedgeOutcomeLedgedash)
                        {
                            msg = 2;
                            arg = r.galint;
                        }
                        else
                        {
                            static const u8 kLedgeMiss[5] = {3, 6, 4, 6, 5};
                            int idx = r.outcome - kLedgeOutcomeTourneyWinner;
                            msg = kLedgeMiss[idx >= 0 && idx < 5 ? idx : 1];
                        }
                    }
                }
                st.prev[0] = s;
            }
            break;
        case drills::kDrillWavedash:
            if (f.wd)
            {
                u32 s = f.wd->seq;
                if (!syncOnly && s != st.prev[0])
                {
                    const WdResult& r = f.wd->last;
                    if (r.outcome == kWdOutcomeSuccess)
                    {
                        bool wl = (r.kind == kWdKindWaveland);
                        arg = wl ? r.airdodgeAirFrames : r.jumpsquatToDodge;
                        mop = arg == 1 ? 1 : 0;
                        msg = wl ? 9 : (arg == 1 ? 7 : 8);
                        ev = arg == 1 ? 4 : 3;
                        if (!wl && r.angleDeg > 0.0f)
                        {
                            useWdAngle = true;
                            wdAngle = r.angleDeg;
                            wdPerfect = (arg == 1);
                        }
                    }
                    else
                    {
                        static const u8 kWdIdx[4] = {5, 10, 11, 12};
                        ev = r.outcome == kWdMissZairTether ? 2 : 1;
                        int idx = r.outcome - kWdMissNoAirdodge;
                        msg = kWdIdx[idx >= 0 && idx < 4 ? idx : 0];
                    }
                }
                st.prev[0] = s;
            }
            break;
        case drills::kDrillLCancel:
            if (f.autoLCancel && !syncOnly)
            {
                st.alcNotice = 1;
            }
            if (f.lc)
            {
                u32 s = f.lc->seq;
                u32 t = f.lc->stats.total;
                if (!syncOnly && s != st.prev[0])
                {
                    const LcResult& r = f.lc->last;
                    if (r.autoLCancelActive)
                    {
                        st.alcNotice = 1;
                    }
                    if (r.outcome == kLcAutoCanceled)
                    {
                        msg = 15;
                        ev = 6;
                    }
                    else if (t == st.prev[1] && r.outcome == kLcMissLate)
                    {
                        msg = 17;
                        arg = r.lateFrames;
                        ev = 5;
                    }
                    else if (r.outcome == kLcSuccessManual)
                    {
                        mop = 3;
                        msg = 13;
                        arg = r.pressFrame;
                        ev = r.pressFrame <= 2 ? 4 : 3;
                    }
                    else if (r.outcome == kLcSuccessAuto)
                    {
                        msg = 14;
                        ev = 2;
                    }
                    else
                    {
                        ev = 1;
                        msg = r.outcome == kLcMissEarly ? 16 : (r.outcome == kLcMissLate ? 17 : 18);
                        arg = r.outcome == kLcMissEarly ? r.earlyFrames : r.lateFrames;
                    }
                }
                st.prev[0] = s;
                st.prev[1] = t;
            }
            break;
        case drills::kDrillHoverFloat:
            if (f.fd)
            {
                u32 a = f.fd->stats.fcAttempts;
                u32 b = f.fd->stats.djcAttempts;
                if (!syncOnly)
                {
                    if (a != st.prev[0])
                    {
                        bool ok = (f.fd->last.fcOutcome == kFcSuccess);
                        mop = ok ? 1 : 0;
                        ev = ok ? 4 : 1;
                        msg = ok ? (f.fd->last.fcIsHover ? 20 : 19) : 21;
                    }
                    else if (b != st.prev[1])
                    {
                        u8 dg = f.fd->last.djcGrade;
                        arg = f.fd->last.djcFrame;
                        mop = dg == kDjcInstant ? 1 : 0;
                        ev = dg == kDjcInstant ? 4 : (dg == kDjcLowRising ? 3 : 1);
                        msg = dg == kDjcInstant ? 22 : (dg == kDjcLowRising ? 23 : 24);
                    }
                }
                st.prev[0] = a;
                st.prev[1] = b;
            }
            break;
        case drills::kDrillPowershield:
            if (f.ps)
            {
                u32 a = f.ps->stats.attempts;
                u32 b = f.ps->stats.psDropCancels;
                if (!syncOnly)
                {
                    if (a != st.prev[0])
                    {
                        const PsResult& r = f.ps->last;
                        if (r.outcome == kPsPhysical || r.outcome == kPsReflect)
                        {
                            arg = r.windowFrame;
                            bool ref = (r.outcome == kPsReflect);
                            mop = ref ? 1 : 0;
                            ev = (ref || arg == 1) ? 4 : 3;
                            msg = ref ? 26 : 25;
                        }
                        else
                        {
                            ev = 1;
                            msg = r.outcome == kPsMissBuffered ? 28 : (r.outcome == kPsMissEarly ? 29 : 30);
                            arg = r.offsetFrames;
                        }
                    }
                    else if (b != st.prev[1])
                    {
                        mop = 1;
                        ev = 7;
                        msg = 27;
                        arg = f.ps->last.psDropActFrame;
                    }
                }
                st.prev[0] = a;
                st.prev[1] = b;
            }
            break;
        case drills::kDrillRar:
            if (f.rar)
            {
                u32 a = f.rar->attempts;
                if (!syncOnly && a != st.prev[0])
                {
                    u8 o = f.rar->last.outcome;
                    bool ok = (o == kRarSuccessBair || o == kRarSuccessJumpOnly);
                    arg = f.rar->last.turnRunFrames > 0 ? f.rar->last.turnRunFrames : 1;
                    if (o == kRarSuccessBair) mop = 3;
                    ev = !ok ? 1 : ((o == kRarSuccessBair && f.rar->last.turnRunFrames <= 2) ? 4 : 3);
                    msg = ok ? (o == kRarSuccessBair ? 31 : 32) : 33;
                }
                st.prev[0] = a;
            }
            break;
        case drills::kDrillDacus:
            if (f.dacus)
            {
                u32 b = f.dacus->attempts;
                if (!syncOnly && b != st.prev[0])
                {
                    u8 o = f.dacus->last.outcome;
                    bool ok = (o == kDacusSuccessGatling || o == kDacusSuccess);
                    arg = f.dacus->last.nonHitlagDashFrame > 0 ? f.dacus->last.nonHitlagDashFrame : 2;
                    if (o == kDacusSuccessGatling) mop = 1;
                    ev = ok ? 4 : 1;
                    msg = ok ? (o == kDacusSuccessGatling ? 34 : 35) : 36;
                }
                st.prev[0] = b;
            }
            break;
        case drills::kDrillBReverse:
            if (f.brev)
            {
                u32 c = f.brev->attempts;
                if (!syncOnly && c != st.prev[0])
                {
                    u8 o = f.brev->last.outcome;
                    if (o >= kBRevTurnaroundB && o <= kBRevWavebounce)
                    {
                        if (o == kBRevWavebounce) mop = 1;
                        ev = o == kBRevTurnaroundB ? 3 : 4;
                        msg = 36 + o;
                    }
                    else if (f.brev->last.earlyFlickFrames > 0 || f.brev->last.lateFlickFrames > 0)
                    {
                        ev = 1;
                        bool early = f.brev->last.earlyFlickFrames > 0;
                        msg = early ? 16 : 17;
                        arg = early ? f.brev->last.earlyFlickFrames : f.brev->last.lateFlickFrames;
                    }
                }
                st.prev[0] = c;
            }
            break;
        case drills::kDrillCancelSprint:
            if (f.cs)
            {
                u32 a = f.cs->out.attempts;
                if (!syncOnly && a != st.prev[0])
                {
                    if (f.cs->out.lastSuccess)
                    {
                        arg = f.cs->out.lastFrames > 0 ? f.cs->out.lastFrames : 1;
                        mop = 3;
                        ev = arg == 1 ? 4 : 3;
                        msg = arg == 1 ? 41 : 42;
                    }
                    else
                    {
                        ev = 1;
                        msg = 43;
                    }
                }
                st.prev[0] = a;
            }
            break;
        case drills::kDrillFlowCourse:
            if (f.fc)
            {
                u32 c = f.fc->out.coursesCompleted;
                u32 g = f.fc->out.activeGate;
                if (!syncOnly)
                {
                    if (c != st.prev[0])
                    {
                        arg = static_cast<int>(f.fc->out.currentElapsed > 0u ? f.fc->out.currentElapsed : 1u);
                        mop = 3;
                        ev = 4;
                        msg = 44;
                    }
                    else if (g != st.prev[1] && g > 0u)
                    {
                        arg = static_cast<int>(g);
                        ev = 7;
                        msg = 45;
                    }
                }
                st.prev[0] = c;
                st.prev[1] = g;
            }
            break;
        case drills::kDrillReactionLab:
            if (f.rt)
            {
                u32 c = f.rt->out.rlCount;
                if (!syncOnly && c != st.prev[0])
                {
                    arg = f.rt->out.rlLastFrames > 0 ? f.rt->out.rlLastFrames : 1;
                    mop = 3;
                    ev = arg <= 18 ? 4 : (arg <= 26 ? 3 : 2);
                    msg = 46;
                }
                st.prev[0] = c;
            }
            break;
        case drills::kDrillTechchaseEscape:
            if (f.rt)
            {
                u32 esc = f.rt->out.tcEscapes;
                u32 cgt = f.rt->out.tcCaughtCount;
                if (!syncOnly)
                {
                    if (esc != st.prev[0])
                    {
                        arg = f.rt->out.tcLastTightness;
                        mop = 3;
                        ev = arg == 0 ? 4 : 3;
                        msg = arg == 0 ? 47 : 48;
                    }
                    else if (cgt != st.prev[1])
                    {
                        ev = 1;
                        msg = 49;
                    }
                }
                st.prev[0] = esc;
                st.prev[1] = cgt;
            }
            break;
        case drills::kDrillRecoverySweetspot:
            if (f.rf)
            {
                u32 a = f.rf->out.recAttempts;
                if (!syncOnly && a != st.prev[0])
                {
                    u8 gr = f.rf->out.recLastGrade;
                    if (gr == kRecGradeSweetspot)
                    {
                        arg = f.rf->out.recLastXlu > 0 ? f.rf->out.recLastXlu : 1;
                        mop = 1;
                        ev = 4;
                        msg = 50;
                    }
                    else if (gr == kRecGradeNoInvulnLimit)
                    {
                        arg = f.rf->out.recLastGrabs > 5 ? f.rf->out.recLastGrabs : 6;
                        ev = 2;
                        msg = 1;
                    }
                    else
                    {
                        ev = 1;
                        msg = gr == kRecGradeHighOvershoot ? 51 : 52;
                    }
                }
                st.prev[0] = a;
            }
            break;
        case drills::kDrillFloorhug:
            if (f.rf)
            {
                u32 reps = f.rf->out.fhReps;
                u32 pun = f.rf->out.fhTruePunishes;
                if (!syncOnly)
                {
                    if (reps != st.prev[0])
                    {
                        u8 o = f.rf->lastFhOutcome;
                        if (o == kFhCcFloorhugCancel || o == kFhStandingFloorhugCancel)
                        {
                            arg = f.rf->lastHitstunSaved > 0 ? f.rf->lastHitstunSaved : 1;
                            mop = 2;
                            ev = 4;
                            msg = 53;
                        }
                        else if (o == kFhAsdiDownGroundTech)
                        {
                            ev = 3;
                            msg = 54;
                        }
                        else
                        {
                            ev = 1;
                            msg = o == kFhBrokenTumble ? 55 : 56;
                        }
                    }
                    else if (pun != st.prev[1])
                    {
                        ev = 7;
                        msg = 57;
                    }
                }
                st.prev[0] = reps;
                st.prev[1] = pun;
            }
            break;
        case drills::kDrillSnakeRecovery:
            if (f.snk)
            {
                u32 ref = f.snk->out.refreshCount;
                u32 comp = f.snk->out.completedRecoveries;
                if (!syncOnly)
                {
                    if (ref != st.prev[0])
                    {
                        arg = f.snk->out.loopChain > 0 ? f.snk->out.loopChain : 1;
                        mop = 2;
                        u8 k = f.snk->out.lastSelfHitKind;
                        ev = (k == kSnakeSelfHitC4 || k == kSnakeSelfHitGrenade) ? 4 : 3;
                        msg = k == kSnakeSelfHitC4 ? 58 : (k == kSnakeSelfHitGrenade ? 59 : 60);
                    }
                    else if (comp != st.prev[1])
                    {
                        arg = f.snk->out.bestLoopChain > 0 ? f.snk->out.bestLoopChain : 1;
                        ev = 7;
                        msg = 61;
                    }
                }
                st.prev[0] = ref;
                st.prev[1] = comp;
            }
            break;
        default:
            break;
        }

        if ((d == drills::kDrillHoverFloat || d == drills::kDrillCancelSprint || d == drills::kDrillSnakeRecovery)
            && f.ineligibleFighter && st.totalAtt == 0 && !syncOnly)
        {
            st.alcNotice = 1;
            st.lastGrade = kDrillGradeWarn;
            int wMsg = d == drills::kDrillHoverFloat ? 40 : (d == drills::kDrillCancelSprint ? 62 : 63);
            snprintf(st.lastFeedback, sizeof(st.lastFeedback), "%s", drillNthStr(kMsg, wMsg));
            return false;
        }
        if (syncOnly || ev == 0 || msg < 0)
        {
            return false;
        }
        if (mop == 1)
        {
            st.bestMetric++;
            st.hasBestMetric = 1;
        }
        else if ((mop == 2 && (!st.hasBestMetric || arg > st.bestMetric))
                 || (mop == 3 && (!st.hasBestMetric || arg < st.bestMetric)))
        {
            st.bestMetric = static_cast<s16>(arg);
            st.hasBestMetric = 1;
        }

        static const u8 kEvGrade[8] = {0, kDrillGradeMiss, kDrillGradeWarn, kDrillGradeGood,
                                       kDrillGradePerfect, kDrillGradeMiss, kDrillGradeWarn, kDrillGradePerfect};
        static const u8 kEvPip[5] = {0, kRepPipMiss, kRepPipWarn, kRepPipGood, kRepPipPerfect};
        st.lastGrade = kEvGrade[ev];
        if (useWdAngle)
        {
            float ang = static_cast<int>(wdAngle * 10.0f) / 10.0f;
            if (wdPerfect)
            {
                snprintf(st.lastFeedback, sizeof(st.lastFeedback), "PERFECT f1 %.1f", ang);
            }
            else
            {
                snprintf(st.lastFeedback, sizeof(st.lastFeedback), "WAVEDASH f%d %.1f", arg, ang);
            }
        }
        else
        {
            snprintf(st.lastFeedback, sizeof(st.lastFeedback), drillNthStr(kMsg, msg), arg);
        }
        if (ev <= 4)
        {
            u8 target = drillModeTargetReps(st.mode);
            if (st.totalAtt < 9999u)
            {
                st.totalAtt++;
            }
            if (target > 0)
            {
                if (st.repsDone < target && st.repsDone < kDrillMaxSetReps)
                {
                    st.repPips[st.repsDone++] = kEvPip[ev];
                }
            }
            else
            {
                if (st.repsDone < 255u)
                {
                    st.repsDone++;
                }
                if (st.freePipCount < kDrillFreePips)
                {
                    st.repPips[st.freePipCount++] = kEvPip[ev];
                }
                else
                {
                    for (int k = 0; k < kDrillFreePips - 1; k++)
                    {
                        st.repPips[k] = st.repPips[k + 1];
                    }
                    st.repPips[kDrillFreePips - 1] = kEvPip[ev];
                }
            }
            if (ev >= 3)
            {
                if (st.totalHit < 9999u)
                {
                    st.totalHit++;
                }
                if (st.repsHit < 255u)
                {
                    st.repsHit++;
                }
                if (st.streakFull < 9999u)
                {
                    st.streakFull++;
                }
                st.streak = static_cast<u8>(st.streakFull < 255u ? st.streakFull : 255u);
                if (st.streakFull > st.bestStreakFull)
                {
                    st.bestStreakFull = st.streakFull;
                    st.bestStreak = static_cast<u8>(st.bestStreakFull < 255u ? st.bestStreakFull : 255u);
                }
                u16 candidate = (target > 0 || st.mode == kDrillModeTimed60) ? st.totalHit : st.streakFull;
                u16 m = drills::betterBest(d, st.sessionBest, candidate);
                if (m > st.sessionBest)
                {
                    st.sessionBest = m;
                    st.newBest = 1;
                }
            }
            else
            {
                st.streakFull = 0;
                st.streak = 0;
            }
            if (target > 0 && st.repsDone >= target)
            {
                drillHudComplete(st);
            }
        }
        return true;
    }

    inline void drillHudSyncBaselines(DrillHudState& st, const DrillEngineFeed& f)
    {
        drillHudPoll(st, f, true);
    }

    inline bool drillHudStep(DrillHudState& st, const DrillEngineFeed& f)
    {
        return drillHudPoll(st, f, false);
    }

    // Maps a completed Wavedash / Waveland result onto the 15-cell timing bar
    // (0..6 = Early -7f..-1f, 7 = Perfect f1, 8..14 = 1f..7f Late; -1 = omit).
    inline int wdTimingBarPointerCell(const WdResult& r, int cellCount = 15)
    {
        if (r.outcome == kWdOutcomeSuccess)
        {
            int f = (r.kind == kWdKindWaveland) ? r.airdodgeAirFrames : r.jumpsquatToDodge;
            if (f < 1)
            {
                f = 1;
            }
            int c = 7 + (f - 1);
            return c >= cellCount ? (cellCount - 1) : c;
        }
        if (r.outcome == kWdMissNoAirdodge)
        {
            if (r.earlyGuardFrames > 0)
            {
                int c = 7 - static_cast<int>(r.earlyGuardFrames);
                return c < 0 ? 0 : c;
            }
            return -1;
        }
        if (r.outcome == kWdMissLateAirdodge)
        {
            return cellCount - 1;
        }
        if (r.outcome == kWdMissUpwardAirdodge || r.outcome == kWdMissZairTether)
        {
            return -1;
        }
        return cellCount - 1;
    }
}
}
