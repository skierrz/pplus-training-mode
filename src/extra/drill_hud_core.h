#pragma once

#include <cstdio>
#include <string.h>
#include <types.h>

#include "extra/drill_breverse_core.h"
#include "extra/drill_dacus_core.h"
#include "extra/drill_float_djc_core.h"
#include "extra/drill_lcancel_core.h"
#include "extra/drill_ledge_core.h"
#include "extra/drill_powershield_core.h"
#include "extra/drill_rar_core.h"
#include "extra/drill_wavedash_core.h"
#include "shared/drill_session.h"

// Pure host-testable Tech Practice HUD & optional 10/20-Rep Set state machine.
// Defaults to infinite Free Practice (zero forced modal freeze) while exposing
// optional scored 10-Rep and 20-Rep sets with the End-Screen Summary Modal.
// Consumes read-only counters/results from existing Stage 4A drill engines
// with zero game-state detection duplication.
namespace tm {
namespace extra {
    enum {
        kDrillSetReps10 = 10,
        kDrillSetReps20 = 20,
        kDrillMaxSetReps = 20,
        kDrillFreePips = 10,
        kDrillFeedbackBuf = 28,
        kDrillHighlightBuf = 24
    };

    enum DrillPracticeMode {
        kDrillModeFree = 0,  // default: infinite continuous grinding
        kDrillModeSet10 = 1, // optional scored 10-rep set
        kDrillModeSet20 = 2, // optional scored 20-rep set
        kDrillModeCount = 3
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
        bool autoLCancel;
        bool ineligibleFighter;
    };

    // Prefix layout (activeDrill .. bestMetric, 14 bytes) mirrors
    // DrillHudDebugBlock offsets 0x08..0x15 for single-memcpy telemetry sync.
    struct DrillHudState {
        u8 activeDrill;  // 0x00: drills::DrillId (0..8)
        u8 repsDone;     // 0x01: clamped 0..255 (or 0..targetReps in Set mode)
        u8 repsHit;      // 0x02: clamped 0..255 (or 0..targetReps in Set mode)
        u8 streak;       // 0x03: current hit streak (clamped 0..255)
        u8 bestStreak;   // 0x04: best hit streak this run (clamped 0..255)
        u8 newBest;      // 0x05: 1 if sessionBest improved
        u8 modalOpen;    // 0x06: 1 while End-Screen Modal is open (Set modes only)
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
        u8 repPips[kDrillMaxSetReps];
        u32 prev[3];
        char lastFeedback[kDrillFeedbackBuf];
    };

    inline bool isValidSkillDrill(u8 drillId)
    {
        return drillId >= drills::kDrillLedgedash && drillId < drills::kDrillIdCount;
    }

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

    inline const char* drillModeLabel(u8 mode)
    {
        static const char* const kLabels[kDrillModeCount] = {"FREE", "10-REP", "20-REP"};
        return kLabels[mode < kDrillModeCount ? mode : 0];
    }

    inline const char* drillHudTitle(u8 drillId)
    {
        static const char* const kNames[drills::kDrillIdCount] = {
            "TECH PRACTICE", "LEDGEDASH", "WAVEDASH", "L-CANCEL", "FLOAT / HOVER",
            "POWERSHIELD",   "RAR",       "DACUS",    "B-REVERSE"
        };
        return kNames[drillId < drills::kDrillIdCount ? drillId : 0];
    }

    inline const char* drillHighlightLabel(u8 drillId)
    {
        static const char* const kLabels[drills::kDrillIdCount] = {
            "Highlight",          "Peak GALINT",       "Frame-1 Perfect",
            "Fastest Window",     "Best Act / Instant", "PS-Drop / Reflect",
            "Fastest Turn",       "Gatling Cancels",    "Wavebounces"
        };
        return kLabels[drillId < drills::kDrillIdCount ? drillId : 0];
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
        if (d == drills::kDrillLedgedash)
        {
            if (!st.hasBestMetric)
            {
                snprintf(out, sz, "None");
            }
            else
            {
                snprintf(out, sz, "%+df", static_cast<int>(st.bestMetric));
            }
            return;
        }
        if (d == drills::kDrillWavedash)
        {
            if (target > 0)
            {
                snprintf(out, sz, "%d / %u", static_cast<int>(st.bestMetric), static_cast<unsigned>(target));
            }
            else
            {
                snprintf(out, sz, "%d", static_cast<int>(st.bestMetric));
            }
            return;
        }
        if (d == drills::kDrillLCancel)
        {
            if (st.hasBestMetric)
            {
                snprintf(out, sz, "Frame %d/7", static_cast<int>(st.bestMetric));
            }
            else
            {
                snprintf(out, sz, "--");
            }
            return;
        }
        if (d == drills::kDrillHoverFloat)
        {
            if (st.hasBestMetric)
            {
                snprintf(out, sz, "%d clean", static_cast<int>(st.bestMetric));
            }
            else
            {
                snprintf(out, sz, "--");
            }
            return;
        }
        if (d == drills::kDrillPowershield || d == drills::kDrillDacus || d == drills::kDrillBReverse)
        {
            snprintf(out, sz, "%d", static_cast<int>(st.bestMetric));
            return;
        }
        if (d == drills::kDrillRar)
        {
            if (st.hasBestMetric)
            {
                snprintf(out, sz, "Frame %d", static_cast<int>(st.bestMetric));
            }
            else
            {
                snprintf(out, sz, "--");
            }
            return;
        }
        snprintf(out, sz, "--");
    }

    inline void drillHudInitMode(DrillHudState& st, u8 drillId, u16 priorBest, u8 mode)
    {
        memset(&st, 0, sizeof(st));
        st.activeDrill = isValidSkillDrill(drillId) ? drillId : static_cast<u8>(drills::kDrillNone);
        st.sessionBest = priorBest;
        st.mode = mode < kDrillModeCount ? mode : static_cast<u8>(kDrillModeFree);
        strcpy(st.lastFeedback, "READY");
    }

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
        u8 next = static_cast<u8>((st.mode + 1u) % kDrillModeCount);
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

    inline void drillHudComplete(DrillHudState& st)
    {
        if (!isValidSkillDrill(st.activeDrill))
        {
            return;
        }
        u8 target = drillModeTargetReps(st.mode);
        if (target == 0)
        {
            target = kDrillSetReps10;
        }
        st.repsDone = target;
        st.modalOpen = 1;
        u16 score = st.totalHit < target ? st.totalHit : static_cast<u16>(target);
        u16 m = drills::betterBest(st.activeDrill, st.sessionBest, score);
        if (m > st.sessionBest)
        {
            st.sessionBest = m;
            st.newBest = 1;
        }
    }

    // Shared baseline sync (syncOnly = true) and live event step (syncOnly = false).
    inline bool drillHudPoll(DrillHudState& st, const DrillEngineFeed& f, bool syncOnly)
    {
        u8 d = st.activeDrill;
        if (!isValidSkillDrill(d) || (!syncOnly && st.modalOpen))
        {
            return false;
        }

        // Single indexed feedback string table (shared phrases reused across drills).
        static const char* const kMsg[] = {
            "GALINT +%df",           // 0
            "GRAB %d/5 (NO INVULN)", // 1
            "GALINT %df",            // 2
            "TOURNEY WINNER",        // 3
            "LEDGESLIP / SD",        // 4
            "NO AIRDODGE",           // 5
            "LEDGE GETUP",           // 6
            "PERFECT f1",            // 7
            "WAVEDASH f%d",          // 8
            "WAVELAND f%d",          // 9
            "LATE AIRDODGE",         // 10
            "UPWARD DODGE",          // 11
            "ZAIR / TETHER",         // 12
            "L-CANCEL f%d/7",        // 13
            "AUTO L-CANCEL ON",      // 14
            "AUTOCANCELED",          // 15
            "EARLY -%df",            // 16
            "LATE +%df",             // 17
            "MISSED L-CANCEL",       // 18
            "FLOAT CANCEL",          // 19
            "HOVER CANCEL",          // 20
            "DROPPED FLOAT",         // 21
            "INSTANT DJC f%d",       // 22
            "LOW DJC f%d",           // 23
            "LATE DJC f%d",          // 24
            "POWERSHIELD f%d",       // 25
            "PS REFLECT f%d",        // 26
            "PS-DROP f%d",           // 27
            "BUFFERED PS",           // 28
            "EARLY PS %df",          // 29
            "LATE PS +%df",          // 30
            "RAR BAIR f%d",          // 31
            "RAR JUMP",              // 32
            "RAR MISS",              // 33
            "GATLING DACUS f%d",     // 34
            "DACUS f%d",             // 35
            "DACUS MISS",            // 36
            "TURNAROUND B",          // 37
            "B-REVERSE",             // 38
            "WAVEBOUNCE",            // 39
            "PICK PEACH/MEWTWO/DJC"  // 40
        };

        int msg = -1;
        int arg = 0;
        bool useWdAngle = false;
        float wdAngle = 0.0f;
        bool wdPerfect = false;
        u8 ev = 0;  // 1=missRep, 2=warnRep, 3=goodRep, 4=perfRep, 5=missMsg, 6=warnMsg, 7=perfMsg
        u8 mop = 0; // 1=inc, 2=max(arg), 3=min(arg)

        if (d == drills::kDrillLedgedash && f.ledge)
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
                        // TourneyWinner(3), OtherGetup(4), Sd(5), Interrupted(6), LandedNoDodge(7).
                        static const u8 kLedgeMiss[5] = {3, 6, 4, 6, 5};
                        int idx = r.outcome - kLedgeOutcomeTourneyWinner;
                        msg = kLedgeMiss[idx >= 0 && idx < 5 ? idx : 1];
                    }
                }
            }
            st.prev[0] = s;
        }
        else if (d == drills::kDrillWavedash && f.wd)
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
        else if (d == drills::kDrillLCancel)
        {
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
        }
        else if (d == drills::kDrillHoverFloat && f.fd)
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
        else if (d == drills::kDrillPowershield && f.ps)
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
        else if (d == drills::kDrillRar && f.rar)
        {
            u32 a = f.rar->attempts;
            if (!syncOnly && a != st.prev[0])
            {
                u8 o = f.rar->last.outcome;
                bool ok = (o == kRarSuccessBair || o == kRarSuccessJumpOnly);
                arg = f.rar->last.turnRunFrames > 0 ? f.rar->last.turnRunFrames : 1;
                if (o == kRarSuccessBair)
                {
                    mop = 3; // min turnRunFrames
                }
                ev = !ok ? 1 : ((o == kRarSuccessBair && f.rar->last.turnRunFrames <= 2) ? 4 : 3);
                msg = ok ? (o == kRarSuccessBair ? 31 : 32) : 33;
            }
            st.prev[0] = a;
        }
        else if (d == drills::kDrillDacus && f.dacus)
        {
            u32 b = f.dacus->attempts;
            if (!syncOnly && b != st.prev[0])
            {
                u8 o = f.dacus->last.outcome;
                bool ok = (o == kDacusSuccessGatling || o == kDacusSuccess);
                arg = f.dacus->last.nonHitlagDashFrame > 0 ? f.dacus->last.nonHitlagDashFrame : 2;
                if (o == kDacusSuccessGatling)
                {
                    mop = 1;
                }
                ev = ok ? 4 : 1;
                msg = ok ? (o == kDacusSuccessGatling ? 34 : 35) : 36;
            }
            st.prev[0] = b;
        }
        else if (d == drills::kDrillBReverse && f.brev)
        {
            u32 c = f.brev->attempts;
            if (!syncOnly && c != st.prev[0])
            {
                u8 o = f.brev->last.outcome;
                if (o >= kBRevTurnaroundB && o <= kBRevWavebounce)
                {
                    if (o == kBRevWavebounce)
                    {
                        mop = 1;
                    }
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

        if (d == drills::kDrillHoverFloat && f.ineligibleFighter && st.totalAtt == 0 && !syncOnly)
        {
            st.alcNotice = 1;
            st.lastGrade = kDrillGradeWarn;
            snprintf(st.lastFeedback, sizeof(st.lastFeedback), "%s", kMsg[40]);
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
            snprintf(st.lastFeedback, sizeof(st.lastFeedback), kMsg[msg], arg);
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
                u16 candidate = target > 0 ? st.totalHit : st.streakFull;
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
}
}
