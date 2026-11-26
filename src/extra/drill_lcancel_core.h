#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable Project+ 3.2.1 L-Cancel detection, grading, and telemetry
// core. Zero Brawl headers so all logic compiles identically on PC (`make
// check-host`) and PowerPC (MWCC 4.2).
//
// P+ 3.2.1 ground-truth rules verified in source and memory dumps:
//  - Counter: LA-Basic[90] (0x1000005A).
//    - initStatus ($80874314): sets LA-Basic[90] = 0, then 7 on press.
//    - execStatus ($80874318): sets LA-Basic[90] = 7 on press, else decrements
//      by 1 each non-hitlag frame (continues negative below 0).
//    - execStop ($8087431C, hitlag): sets LA-Basic[90] = 8 on press; does NOT
//      decrement during hitlag (Source/Project+/L-Cancel.asm:38-154).
//  - Valid triggers: fresh digital shield press ((getButton & getTrigger) & 0x8,
//    which includes GameCube Z because Z asserts Attack (0x1) | Guard (0x8)) OR
//    fresh analog L/R press >= 0.3 (LA-Basic[77] == 1; ButtonPresses.asm:454,
//    L-Cancel.asm:52-62, 85-95, 133-143).
//  - Landing ($8087459C, Attack_Air 0x33 -> Landing_Attack_Air 0x18):
//    LA-Basic[90] > 0 = manual success (pressFrame = 8 - lCancel clamped 1..7).
//  - Auto L-Cancel (ALC) & 3f Buffer flags (L-Cancel.asm:235-240, 313-348):
//    *(u8*)(*(u32*)(*(u32*)0x805A00E0 + 0x08) + 0xE5), bit 0x01 = ALC,
//    bit 0x02 = 3-frame input buffer.
namespace tm {
namespace extra {
    enum {
        kLcCoreMaxEvents = 8,
        kLcCoreNoFrame = -1,
        kLcCoreWindowFrames = 7,
        kLcCoreEarlyMaxFrames = 30, // 0.5 s cap for reporting early misses
        kLcCoreLateMaxFrames = 10   // 1..10f after entering 0x18 -> kLcMissLate
    };

    // Common Brawl/P+ fighter statuses (ft/fighter.h).
    enum {
        kLcStLanding = 0x16,
        kLcStLandingLight = 0x17,
        kLcStLandingAttackAir = 0x18,
        kLcStAttackAir = 0x33
    };

    enum LcOutcome {
        kLcOutcomeNone = 0,
        kLcSuccessManual = 1, // manual press in 1..7f window (LA-Basic[90] > 0)
        kLcSuccessAuto = 2,   // ALC active, no manual press in window
        kLcAutoCanceled = 3,  // 0x33 -> Landing 0x16/0x17 (not L-cancelable)
        kLcMissEarly = 4,     // pressed > 7 non-hitlag frames before landing
        kLcMissLate = 5,      // fresh shield press 1..10f after entering 0x18
        kLcMissNoPress = 6    // missed with no press in early or late window
    };

    enum LcContext {
        kLcContextWhiff = 0,
        kLcContextHit = 1,
        kLcContextShield = 2
    };

    enum LcInputKind {
        kLcInputNone = 0,
        kLcInputDigital = 1,    // fresh digital L/R (MASK_GUARD without MASK_ATTACK edge)
        kLcInputLightPress = 2, // fresh analog L/R >= 0.3 (LA-Basic[77] == 1)
        kLcInputZPress = 3      // fresh Z macro (MASK_GUARD + MASK_ATTACK edge together)
    };

    struct LcStatusEv {
        s16 status;
        s16 prev;
        s16 motionKind;
        s16 lCancel; // LA-Basic[90] sampled at status transition
    };

    struct LcFrameInput {
        s32 frame;
        s16 status;
        s16 prevStatus;
        s16 motionKind;
        s16 hitlag;
        s32 lCancelCounter;      // LA-Basic[90] at frame end
        s32 analogLRState;       // LA-Basic[77] (1 = new analog L/R press >= 0.3)
        u8 digitalGuardEdge;     // fresh digital MASK_GUARD press this frame
        u8 digitalAttackEdge;    // fresh digital MASK_ATTACK press this frame
        u8 attackActive;         // fighter's hitbox inflicted hit this frame
        u8 dealtHit;             // tracker lastDealt matched this frame with shield == 0
        u8 dealtShield;          // tracker lastDealt matched this frame with shield == 1
        u8 autoLCancelActive;    // P+ rule flag bit 0x01
        u8 inputBuffer3fActive;  // P+ rule flag bit 0x02
        u8 eventCount;
        LcStatusEv events[kLcCoreMaxEvents];
    };

    struct LcResult {
        s32 doneFrame;
        u8 outcome;             // LcOutcome
        u8 context;             // LcContext (Whiff / Hit / Shield)
        u8 inputKind;           // LcInputKind (None / Digital / LightPress / ZPress)
        u8 lightPress;          // 1 if triggered by analog light-press
        u8 zPress;              // 1 if triggered by Z macro
        u8 autoLCancelActive;   // 1 if ALC rule flag was on
        u8 inputBuffer3fActive; // 1 if 3-frame buffer rule flag was on
        s16 motionKind;         // aerial subaction ID
        s16 pressFrame;         // 1..7 on kLcSuccessManual, 0 otherwise
        s16 earlyFrames;        // frames earlier than 7f window on kLcMissEarly
        s16 lateFrames;         // 1..10 frames after entering 0x18 on kLcMissLate
        s16 hitlagFrames;       // hitlag frames experienced during the aerial
    };

    struct LcStats {
        u16 total;          // total L-cancelable 0x18 landings (excludes autocancels)
        u16 successes;      // manual L-cancel successes
        u16 autoSuccesses;  // ALC-only successes (no manual press in window)
        u16 autoCanceled;   // 0x33 -> 0x16/0x17 autocanceled aerials
        u16 whiffHits;
        u16 whiffTotal;
        u16 hitHits;
        u16 hitTotal;
        u16 shieldHits;
        u16 shieldTotal;
        u16 lateMisses;
        u16 earlyMisses;
        u16 streak;
        u16 bestStreak;
    };

    struct LcCoreState {
        // Active aerial state.
        u8 inAerial;
        u8 pressedInAerial;
        u8 pressInputKind;
        u8 aerialContext;
        s16 aerialMotionKind;
        s16 hitlagFrames;
        s16 nonHitlagSincePress;

        // Post-landing late-miss watch (1..10f inside Landing_Attack_Air 0x18).
        u8 lateWatchActive;
        s32 lateWatchStart;

        // Completed result and cumulative stats.
        u32 seq;
        LcResult last;
        LcStats stats;
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline u8 lcDetectTriggerKind(const LcFrameInput& in)
    {
        if (in.digitalGuardEdge != 0 && in.digitalAttackEdge != 0)
        {
            return kLcInputZPress;
        }
        if (in.digitalGuardEdge != 0)
        {
            return kLcInputDigital;
        }
        if (in.analogLRState == 1)
        {
            return kLcInputLightPress;
        }
        return kLcInputNone;
    }

    inline void lcCoreResetTransient(LcCoreState& st)
    {
        memset(&st, 0, reinterpret_cast<char*>(&st.seq) - reinterpret_cast<char*>(&st));
        st.aerialMotionKind = -1;
        st.lateWatchStart = kLcCoreNoFrame;
    }

    inline void lcCoreResetAll(LcCoreState& st)
    {
        memset(&st, 0, sizeof(st));
        lcCoreResetTransient(st);
        st.last.doneFrame = kLcCoreNoFrame;
        st.last.motionKind = -1;
    }

    inline void lcUpdateContext(LcCoreState& st, const LcFrameInput& in)
    {
        if (in.dealtHit != 0)
        {
            st.aerialContext = kLcContextHit;
        }
        else if (in.dealtShield != 0 && st.aerialContext != kLcContextHit)
        {
            st.aerialContext = kLcContextShield;
        }
        else if ((in.attackActive != 0 || in.hitlag > 0) && st.aerialContext == kLcContextWhiff)
        {
            st.aerialContext = kLcContextHit;
        }
    }

    inline void lcRecordContextTotals(LcStats& stats, u8 ctx, bool manualSuccess)
    {
        u16* pair = (ctx == kLcContextHit) ? &stats.hitHits : (ctx == kLcContextShield ? &stats.shieldHits : &stats.whiffHits);
        pair[1]++;
        if (manualSuccess)
        {
            pair[0]++;
        }
    }

    inline void lcSetResultInputKind(LcResult& r, u8 kind)
    {
        r.inputKind = kind;
        r.lightPress = (kind == kLcInputLightPress) ? 1 : 0;
        r.zPress = (kind == kLcInputZPress) ? 1 : 0;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Steps one finished game frame for one tracked fighter. Returns true when
    // an L-cancel result is emitted or upgraded (kLcMissNoPress -> kLcMissLate).
    inline bool lcCoreStep(LcCoreState& st, const LcFrameInput& in)
    {
        const int n = in.eventCount < kLcCoreMaxEvents ? static_cast<int>(in.eventCount) : kLcCoreMaxEvents;
        const u8 trigKind = lcDetectTriggerKind(in);
        bool emitted = false;

        // 1. Check active late-miss watch (frames 1..10 after entering 0x18).
        if (st.lateWatchActive != 0)
        {
            s32 dt = in.frame - st.lateWatchStart;
            if (in.status != kLcStLandingAttackAir || dt > kLcCoreLateMaxFrames)
            {
                st.lateWatchActive = 0;
            }
            else if (dt >= 1 && trigKind != kLcInputNone)
            {
                // Upgrade the provisional kLcMissNoPress to kLcMissLate without
                // double-counting stats.total or per-context totals.
                st.last.outcome = kLcMissLate;
                st.last.lateFrames = static_cast<s16>(dt);
                lcSetResultInputKind(st.last, trigKind);
                st.stats.lateMisses++;
                st.lateWatchActive = 0;
                st.seq++;
                emitted = true;
            }
        }

        // 2. Process ordered status transitions during this frame.
        bool aerial = (st.inAerial != 0) || (in.prevStatus == kLcStAttackAir);
        bool startedThisFrame = false;

        if (aerial)
        {
            lcUpdateContext(st, in);
            if (in.hitlag > 0)
            {
                st.hitlagFrames++;
            }
        }

        for (int k = 0; k < n; k++)
        {
            const LcStatusEv& e = in.events[k];
            if (e.status == kLcStAttackAir)
            {
                aerial = true;
                startedThisFrame = true;
                st.inAerial = 1;
                st.pressedInAerial = 0;
                st.pressInputKind = kLcInputNone;
                st.aerialContext = kLcContextWhiff;
                st.aerialMotionKind = in.motionKind;
                st.hitlagFrames = in.hitlag > 0 ? 1 : 0;
                st.nonHitlagSincePress = 0;
                st.lateWatchActive = 0;
                lcUpdateContext(st, in);
            }
            else if (e.status == kLcStLandingAttackAir && e.prev == kLcStAttackAir && aerial)
            {
                // Only treat a landing-frame trigKind as an in-aerial press when
                // e.lCancel > 0; a landing-frame trigger with e.lCancel <= 0 is
                // late (not an early in-air press).
                if (e.lCancel > 0
                    && (e.lCancel >= kLcCoreWindowFrames || startedThisFrame || trigKind != kLcInputNone))
                {
                    st.pressedInAerial = 1;
                    if (trigKind != kLcInputNone)
                    {
                        st.pressInputKind = trigKind;
                    }
                    else if (st.pressInputKind == kLcInputNone)
                    {
                        st.pressInputKind = kLcInputDigital;
                    }
                    st.nonHitlagSincePress = 0;
                }

                LcResult& r = st.last;
                memset(&r, 0, sizeof(r));
                r.doneFrame = in.frame;
                r.context = st.aerialContext;
                r.autoLCancelActive = in.autoLCancelActive;
                r.inputBuffer3fActive = in.inputBuffer3fActive;
                r.motionKind = (e.motionKind >= 0) ? e.motionKind : st.aerialMotionKind;
                r.hitlagFrames = st.hitlagFrames;

                if (e.lCancel > 0)
                {
                    // Manual L-cancel success (P+ L-Cancel.asm:194-232 takes
                    // priority over ALC and triggers the white flash).
                    int pf = 8 - e.lCancel;
                    if (pf < 1)
                    {
                        pf = 1;
                    }
                    else if (pf > kLcCoreWindowFrames)
                    {
                        pf = kLcCoreWindowFrames;
                    }
                    u8 kind = st.pressInputKind != kLcInputNone ? st.pressInputKind : kLcInputDigital;
                    r.outcome = kLcSuccessManual;
                    r.pressFrame = static_cast<s16>(pf);
                    lcSetResultInputKind(r, kind);

                    st.stats.total++;
                    st.stats.successes++;
                    lcRecordContextTotals(st.stats, r.context, true);
                    st.stats.streak++;
                    if (st.stats.streak > st.stats.bestStreak)
                    {
                        st.stats.bestStreak = st.stats.streak;
                    }
                }
                else if (in.autoLCancelActive != 0)
                {
                    // ALC halved landing lag with no manual press in window.
                    r.outcome = kLcSuccessAuto;
                    if (st.pressedInAerial != 0)
                    {
                        int early = 1 - e.lCancel;
                        if (early < 1 && st.nonHitlagSincePress >= kLcCoreWindowFrames)
                        {
                            early = st.nonHitlagSincePress - (kLcCoreWindowFrames - 1);
                        }
                        if (early >= 1 && early <= kLcCoreEarlyMaxFrames)
                        {
                            r.earlyFrames = static_cast<s16>(early);
                        }
                        lcSetResultInputKind(r, st.pressInputKind);
                    }
                    st.stats.total++;
                    st.stats.autoSuccesses++;
                    lcRecordContextTotals(st.stats, r.context, false);
                    st.stats.streak = 0;
                }
                else
                {
                    // Missed manual L-cancel (ALC off).
                    int early = 0;
                    if (st.pressedInAerial != 0)
                    {
                        early = 1 - e.lCancel;
                        if (early < 1 && st.nonHitlagSincePress >= kLcCoreWindowFrames)
                        {
                            early = st.nonHitlagSincePress - (kLcCoreWindowFrames - 1);
                        }
                    }
                    if (st.pressedInAerial != 0 && early >= 1 && early <= kLcCoreEarlyMaxFrames)
                    {
                        r.outcome = kLcMissEarly;
                        r.earlyFrames = static_cast<s16>(early);
                        lcSetResultInputKind(r, st.pressInputKind);
                        st.stats.earlyMisses++;
                    }
                    else if (trigKind != kLcInputNone)
                    {
                        r.outcome = kLcMissLate;
                        r.lateFrames = 1;
                        lcSetResultInputKind(r, trigKind);
                        st.stats.lateMisses++;
                    }
                    else
                    {
                        r.outcome = kLcMissNoPress;
                        // Arm late-miss watch for frames 1..10 in 0x18.
                        st.lateWatchActive = 1;
                        st.lateWatchStart = in.frame;
                    }
                    st.stats.total++;
                    lcRecordContextTotals(st.stats, r.context, false);
                    st.stats.streak = 0;
                }

                st.inAerial = 0;
                st.pressedInAerial = 0;
                st.pressInputKind = kLcInputNone;
                st.nonHitlagSincePress = 0;
                aerial = false;
                st.seq++;
                emitted = true;
            }
            else if ((e.status == kLcStLanding || e.status == kLcStLandingLight) && e.prev == kLcStAttackAir && aerial)
            {
                // Autocanceled aerial (0x33 -> 0x16/0x17): not L-cancelable.
                LcResult& r = st.last;
                memset(&r, 0, sizeof(r));
                r.doneFrame = in.frame;
                r.outcome = kLcAutoCanceled;
                r.context = st.aerialContext;
                r.autoLCancelActive = in.autoLCancelActive;
                r.inputBuffer3fActive = in.inputBuffer3fActive;
                r.motionKind = (e.motionKind >= 0) ? e.motionKind : st.aerialMotionKind;
                r.hitlagFrames = st.hitlagFrames;

                st.stats.autoCanceled++;
                st.inAerial = 0;
                st.pressedInAerial = 0;
                st.pressInputKind = kLcInputNone;
                st.nonHitlagSincePress = 0;
                aerial = false;
                st.seq++;
                emitted = true;
            }
            else
            {
                st.inAerial = 0;
                st.pressedInAerial = 0;
                st.pressInputKind = kLcInputNone;
                st.nonHitlagSincePress = 0;
                aerial = false;
            }
        }

        // 3. Mid-aerial press tracking & non-hitlag countdown.
        if (aerial && in.status == kLcStAttackAir)
        {
            st.inAerial = 1;
            if (in.motionKind >= 0 && st.aerialMotionKind < 0)
            {
                st.aerialMotionKind = in.motionKind;
            }
            bool freshPress = (in.lCancelCounter >= kLcCoreWindowFrames)
                              || (startedThisFrame && in.lCancelCounter > 0) || (trigKind != kLcInputNone);
            if (freshPress)
            {
                st.pressedInAerial = 1;
                if (trigKind != kLcInputNone)
                {
                    st.pressInputKind = trigKind;
                }
                else if (st.pressInputKind == kLcInputNone)
                {
                    st.pressInputKind = kLcInputDigital;
                }
                st.nonHitlagSincePress = 0;
            }
            else if (st.pressedInAerial != 0 && in.hitlag <= 0)
            {
                st.nonHitlagSincePress++;
            }
        }
        else if (in.status != kLcStAttackAir)
        {
            st.inAerial = 0;
        }

        return emitted;
    }
}
}
