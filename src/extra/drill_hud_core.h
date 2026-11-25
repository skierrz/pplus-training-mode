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

// Pure host-testable 10-rep Skill Drill HUD & End-Screen state machine.
// Consumes read-only counters/results from existing Stage 4A drill engines
// with zero game-state detection duplication.
namespace tm {
namespace extra {
    enum {
        kDrillSetReps = 10,
        kDrillFeedbackBuf = 28,
        kDrillHighlightBuf = 24
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
    };

    // Prefix layout (activeDrill .. bestMetric, 14 bytes) mirrors
    // DrillHudDebugBlock offsets 0x08..0x15 for single-memcpy telemetry sync.
    struct DrillHudState {
        u8 activeDrill;  // 0x00: drills::DrillId (0..6)
        u8 repsDone;     // 0x01: 0..kDrillSetReps
        u8 repsHit;      // 0x02: 0..kDrillSetReps
        u8 streak;       // 0x03
        u8 bestStreak;   // 0x04
        u8 newBest;      // 0x05
        u8 modalOpen;    // 0x06
        u8 lastGrade;    // 0x07: DrillGrade
        u16 sessionBest; // 0x08
        u16 retryCount;  // 0x0A
        s16 bestMetric;  // 0x0C
        u8 hasBestMetric;
        u8 alcNotice;
        u8 rarHits;
        u8 dacusHits;
        u8 brevHits;
        u8 repPips[kDrillSetReps];
        u32 prev[3];
        char lastFeedback[kDrillFeedbackBuf];
    };

    inline bool isValidSkillDrill(u8 drillId)
    {
        return drillId >= drills::kDrillLedgedash && drillId < drills::kDrillIdCount;
    }

    inline const char* drillHudTitle(u8 drillId)
    {
        static const char* const kNames[drills::kDrillIdCount] = {
            "SKILL DRILL", "LEDGEDASH", "WAVEDASH", "L-CANCEL", "FLOAT / HOVER", "POWERSHIELD", "P+ TECH"
        };
        return kNames[drillId < drills::kDrillIdCount ? drillId : 0];
    }

    inline const char* drillHighlightLabel(u8 drillId)
    {
        static const char* const kLabels[drills::kDrillIdCount] = {
            "Highlight",      "Peak GALINT",       "Frame-1 Perfect",
            "Fastest Window", "Best Act / Instant", "PS-Drop / Reflect", "RAR / DACUS / B-Rev"
        };
        return kLabels[drillId < drills::kDrillIdCount ? drillId : 0];
    }

    inline void drillHudFormatHighlight(const DrillHudState& st, char* out, int outSize)
    {
        size_t sz = static_cast<size_t>(outSize);
        u8 d = st.activeDrill;
        if (d == drills::kDrillPPlusTech)
        {
            snprintf(out, sz, "%u / %u / %u", static_cast<unsigned>(st.rarHits), static_cast<unsigned>(st.dacusHits),
                     static_cast<unsigned>(st.brevHits));
            return;
        }
        static const char* const kFmt[6] = {"--", "%+df", "%d / 10", "Frame %d/7", "%d clean", "%d"};
        const char* fmt = (d == drills::kDrillLCancel && st.alcNotice)                   ? "Auto L-Cancel On"
                          : (d == drills::kDrillLedgedash && !st.hasBestMetric)          ? "None"
                          : (d >= 1 && d <= 5 && (st.hasBestMetric || d == 2 || d == 5)) ? kFmt[d]
                                                                                         : "--";
        snprintf(out, sz, fmt, static_cast<int>(st.bestMetric));
    }

    inline void drillHudInit(DrillHudState& st, u8 drillId, u16 priorBest)
    {
        memset(&st, 0, sizeof(st));
        st.activeDrill = isValidSkillDrill(drillId) ? drillId : static_cast<u8>(drills::kDrillNone);
        st.sessionBest = priorBest;
        strcpy(st.lastFeedback, "READY");
    }

    inline void drillHudRetry(DrillHudState& st)
    {
        if (isValidSkillDrill(st.activeDrill))
        {
            u16 r = static_cast<u16>(st.retryCount + 1);
            drillHudInit(st, st.activeDrill, st.sessionBest);
            st.retryCount = r;
        }
    }

    inline void drillHudComplete(DrillHudState& st)
    {
        if (!isValidSkillDrill(st.activeDrill))
        {
            return;
        }
        st.repsDone = kDrillSetReps;
        st.modalOpen = 1;
        u16 m = drills::betterBest(st.activeDrill, st.sessionBest, static_cast<u16>(st.repsHit));
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
            "RAR BAIR",              // 31
            "RAR JUMP",              // 32
            "RAR MISS",              // 33
            "GATLING DACUS",         // 34
            "DACUS",                 // 35
            "DACUS MISS",            // 36
            "TURNAROUND B",          // 37
            "B-REVERSE",             // 38
            "WAVEBOUNCE"             // 39
        };

        int msg = -1;
        int arg = 0;
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
        else if (d == drills::kDrillPPlusTech)
        {
            u32 a = f.rar ? f.rar->attempts : 0u;
            u32 b = f.dacus ? f.dacus->attempts : 0u;
            u32 c = f.brev ? f.brev->attempts : 0u;
            if (!syncOnly)
            {
                if (a != st.prev[0])
                {
                    u8 o = f.rar->last.outcome;
                    bool ok = (o == kRarSuccessBair || o == kRarSuccessJumpOnly);
                    if (ok)
                    {
                        st.rarHits++;
                    }
                    ev = !ok ? 1 : ((o == kRarSuccessBair && f.rar->last.turnRunFrames <= 2) ? 4 : 3);
                    msg = ok ? (o == kRarSuccessBair ? 31 : 32) : 33;
                }
                else if (b != st.prev[1])
                {
                    u8 o = f.dacus->last.outcome;
                    bool ok = (o == kDacusSuccessGatling || o == kDacusSuccess);
                    if (ok)
                    {
                        st.dacusHits++;
                    }
                    ev = ok ? 4 : 1;
                    msg = ok ? (o == kDacusSuccessGatling ? 34 : 35) : 36;
                }
                else if (c != st.prev[2])
                {
                    u8 o = f.brev->last.outcome;
                    if (o >= kBRevTurnaroundB && o <= kBRevWavebounce)
                    {
                        st.brevHits++;
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
            }
            st.prev[0] = a;
            st.prev[1] = b;
            st.prev[2] = c;
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
        snprintf(st.lastFeedback, sizeof(st.lastFeedback), kMsg[msg], arg);
        if (ev <= 4)
        {
            if (st.repsDone < kDrillSetReps)
            {
                st.repPips[st.repsDone++] = kEvPip[ev];
            }
            if (ev >= 3)
            {
                st.repsHit++;
                if (++st.streak > st.bestStreak)
                {
                    st.bestStreak = st.streak;
                }
                u16 m = drills::betterBest(d, st.sessionBest, static_cast<u16>(st.repsHit));
                if (m > st.sessionBest)
                {
                    st.sessionBest = m;
                    st.newBest = 1;
                }
            }
            else
            {
                st.streak = 0;
            }
            if (st.repsDone >= kDrillSetReps)
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
