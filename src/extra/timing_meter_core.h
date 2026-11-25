#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable Overhead Timing Helper Meter core (L-Cancel, Wavedash,
// Act-Out). Zero Brawl headers so logic compiles identically on PC (`make
// check-host`) and PowerPC (MWCC 4.2).
//
// P+ 3.2.1 ground-truth mechanics:
//  - L-Cancel counter: LA-Basic[90] = 7 on press in Attack_Air (0x33),
//    decrements each non-hitlag frame; > 0 on Landing_Attack_Air (0x18) = hit
//    (Source/Project+/L-Cancel.asm:38-154).
//  - Wavedash 1-frame earlier physics (Source/Project+/Physics.asm:413-597):
//    frame-1 wavedash traverses Jump_Squat (0x0A) -> Jump (0x0B) ->
//    Escape_Air (0x21) -> Landing_Fall_Special (0x19) on a single game frame.
//  - Act-Out: driven by extra::framesUntilActionable() (extra/frame_data.h).
namespace tm {
namespace extra {
    enum TimingMeterKind {
        kTmKindNone = 0,
        kTmKindLCancel = 1,
        kTmKindWavedash = 2,
        kTmKindActOut = 3
    };

    enum TimingMeterOutcome {
        kTmOutcomeNone = 0,
        kTmOutcomeHit = 1,   // clean L-Cancel / 1-2f Wavedash / Act-Out actionable flash
        kTmOutcomeEarly = 2, // early L-Cancel press expired before landing
        kTmOutcomeLate = 3,  // 3-4f Wavedash (acceptable, cyan/gold)
        kTmOutcomeMiss = 4   // missed L-Cancel / >= 5f or whiffed Wavedash
    };

    enum TimingMeterPhase {
        kTmPhaseHidden = 0,
        kTmPhaseActive = 1,
        kTmPhaseHold = 2
    };

    enum {
        kTmHoldFrames = 30,
        kTmActFlashFrames = 12,
        kTmLcWindowFrames = 7,
        kTmMaxEvents = 8,
        kTmModeOff = 0,
        kTmModeAll = 1,
        kTmModeLCancel = 2,
        kTmModeWavedash = 3,
        kTmModeActOut = 4,

        // Common Brawl/P+ fighter statuses (ft/fighter.h).
        kTmStJumpSquat = 0x0A,
        kTmStJump = 0x0B,
        kTmStFall = 0x0E,
        kTmStLanding = 0x16,
        kTmStLandingLight = 0x17,
        kTmStLandingAttackAir = 0x18,
        kTmStLandingFallSpecial = 0x19,
        kTmStEscapeAir = 0x21,
        kTmStAttackAir = 0x33,
        kTmStDead = 0xBD,
        kTmStRebirth = 0xBE,
        kTmStStandby = 0x10B
    };

    // High-contrast RGBA palette for the 3D overhead timing bar.
    static const u32 kTmColGreen = 0x22C55EFFu;
    static const u32 kTmColFlashGreen = 0x4ADE80FFu;
    static const u32 kTmColAmber = 0xF59E0BFFu;
    static const u32 kTmColGold = 0xFACC15FFu;
    static const u32 kTmColCyan = 0x38BDF8FFu;
    static const u32 kTmColRed = 0xEF4444FFu;
    static const u32 kTmColNeutral = 0x64748BFFu;

    struct TimingStatusEv {
        s16 status;
        s16 prev;
        s16 lCancel; // LA-Basic[90] sampled at status transition
    };

    struct TimingStepInput {
        s32 frame;
        s16 status;
        s16 prevStatus;
        s16 hitlag;
        s16 hitstun;
        s16 actIn;         // extra::framesUntilActionable() (-1 if unknown, 0 actionable, >0 lag)
        s16 jumpSquatLen;  // fighter jumpsquat animation length (0 if unknown -> defaults to 4)
        s32 lCancelCounter; // LA-Basic[90] at frame end
        u8 mode;           // main::TimingMeterMode
        u8 eventCount;
        TimingStatusEv events[kTmMaxEvents];
    };

    // Visual render state for one fighter on the current frame.
    struct TimingBarView {
        u8 phase;        // TimingMeterPhase
        u8 kind;         // TimingMeterKind
        u8 outcome;      // TimingMeterOutcome
        u8 holdLeft;     // remaining hold frames (when phase == kTmPhaseHold)
        u8 hasZone;      // 1 to draw green target zone band
        u8 isFlash;      // 1 for full-bar Act-Out actionable flash
        float ratio;     // primary fill/cursor ratio in [0, 1]
        float zoneStart; // target zone start in [0, 1]
        float zoneEnd;   // target zone end in [0, 1]
        u32 fillColor;   // RGBA fill color
    };

    struct TimingSlotState {
        // L-Cancel transient state.
        u8 lcInAerial;
        u8 lcPressed;
        u8 lcEarlyExpired;
        s16 lcWindowLeft;
        s16 lcEarlyFrames;

        // Wavedash transient state (0 idle, 1 squat, 2 jumpAir, 3 escapeAir).
        u8 wdPhase;
        s16 wdSquatFrames;
        s16 wdSquatLen;
        s32 wdAirStartFrame;
        s16 wdDelay;

        // Act-Out transient state.
        u8 actTracking;
        s16 actStatus;
        s16 actPeak;
        s16 actLast;

        // Active hold feedback banner.
        u8 holdKind;
        u8 holdOutcome;
        u8 holdFrames;
        u8 holdMax;
        u8 holdFlash;
        s16 holdDelta;
        float holdRatio;
        u32 holdColor;

        // Sticky last-outcome telemetry for RAM debug block ("TMTM").
        u8 lastKind;
        u8 lastOutcome;
        s16 lastDelta;
        u32 eventCount;
        u32 lCancelHits;
        u32 wavedashHits;
        u32 actOutFlashes;

        // Current frame's resolved render view.
        TimingBarView view;
    };

    inline bool tmOutOfPlay(int s)
    {
        return s == kTmStDead || s == kTmStRebirth || s == kTmStStandby;
    }

    inline float tmClamp01(float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    inline void tmResetSlot(TimingSlotState& st)
    {
        memset(&st, 0, sizeof(st));
    }

    inline void tmStartHold(TimingSlotState& st, u8 kind, u8 outcome, s16 delta, float ratio, u32 color, u8 frames,
                            u8 flash)
    {
        st.holdKind = kind;
        st.holdOutcome = outcome;
        st.holdDelta = delta;
        st.holdRatio = tmClamp01(ratio);
        st.holdColor = color;
        st.holdFrames = frames;
        st.holdMax = frames;
        st.holdFlash = flash;
        st.lastKind = kind;
        st.lastOutcome = outcome;
        st.lastDelta = delta;
        st.eventCount++;
        if (kind == kTmKindLCancel && outcome == kTmOutcomeHit)
        {
            st.lCancelHits++;
        }
        else if (kind == kTmKindWavedash && outcome == kTmOutcomeHit)
        {
            st.wavedashHits++;
        }
        else if (kind == kTmKindActOut && outcome == kTmOutcomeHit)
        {
            st.actOutFlashes++;
        }
    }

    inline u32 tmFadeAlpha(u32 rgba, u8 holdLeft, u8 holdMax)
    {
        if (holdLeft >= 8 || holdMax == 0)
        {
            return rgba;
        }
        u32 baseA = rgba & 0xFFu;
        u32 a = (baseA * static_cast<u32>(holdLeft)) / 8u;
        return (rgba & 0xFFFFFF00u) | (a & 0xFFu);
    }

    inline void tmStepSlot(TimingSlotState& st, const TimingStepInput& in)
    {
        memset(&st.view, 0, sizeof(st.view));
        if (in.mode == kTmModeOff || in.mode > kTmModeActOut)
        {
            st.lcInAerial = 0;
            st.wdPhase = 0;
            st.actTracking = 0;
            st.holdFrames = 0;
            return;
        }

        const bool wantLc = (in.mode == kTmModeAll || in.mode == kTmModeLCancel);
        const bool wantWd = (in.mode == kTmModeAll || in.mode == kTmModeWavedash);
        const bool wantAct = (in.mode == kTmModeAll || in.mode == kTmModeActOut);

        if (st.holdFrames > 0)
        {
            st.holdFrames--;
        }

        const bool abortState = (in.hitstun > 0 || tmOutOfPlay(in.status));
        if (abortState)
        {
            st.lcInAerial = 0;
            st.lcPressed = 0;
            st.lcEarlyExpired = 0;
            st.wdPhase = 0;
            st.actTracking = 0;
        }

        const int n = in.eventCount < kTmMaxEvents ? static_cast<int>(in.eventCount) : kTmMaxEvents;

        // ---- 1. L-Cancel tracking (AttackAir 0x33 -> LandingAttackAir 0x18) ----
        bool lcJustTriggered = false;
        if (wantLc && !abortState)
        {
            bool aerial = (st.lcInAerial != 0) || (in.prevStatus == kTmStAttackAir);
            for (int k = 0; k < n; k++)
            {
                const TimingStatusEv& e = in.events[k];
                if (e.status == kTmStAttackAir)
                {
                    aerial = true;
                    st.lcInAerial = 1;
                    st.lcPressed = 0;
                    st.lcEarlyExpired = 0;
                    st.lcWindowLeft = 0;
                    st.lcEarlyFrames = 0;
                }
                else if (e.status == kTmStLandingAttackAir && e.prev == kTmStAttackAir && aerial)
                {
                    if (e.lCancel > 0)
                    {
                        int pf = 8 - e.lCancel;
                        if (pf < 1)
                        {
                            pf = 1;
                        }
                        else if (pf > kTmLcWindowFrames)
                        {
                            pf = kTmLcWindowFrames;
                        }
                        tmStartHold(st, kTmKindLCancel, kTmOutcomeHit, static_cast<s16>(pf),
                                    static_cast<float>(pf) / static_cast<float>(kTmLcWindowFrames), kTmColGreen,
                                    kTmHoldFrames, 0);
                    }
                    else if (st.lcPressed != 0)
                    {
                        int early = 1 - e.lCancel;
                        if (early < 1)
                        {
                            early = st.lcEarlyFrames > 0 ? st.lcEarlyFrames : 1;
                        }
                        tmStartHold(st, kTmKindLCancel, kTmOutcomeEarly, static_cast<s16>(-early), 1.0f, kTmColAmber,
                                    kTmHoldFrames, 0);
                    }
                    else
                    {
                        tmStartHold(st, kTmKindLCancel, kTmOutcomeMiss, 0, 0.0f, kTmColRed, kTmHoldFrames, 0);
                    }
                    st.lcInAerial = 0;
                    st.lcPressed = 0;
                    st.lcEarlyExpired = 0;
                    aerial = false;
                    lcJustTriggered = true;
                }
                else
                {
                    st.lcInAerial = 0;
                    st.lcPressed = 0;
                    st.lcEarlyExpired = 0;
                    aerial = false;
                }
            }

            if (aerial && in.status == kTmStAttackAir)
            {
                st.lcInAerial = 1;
                if (in.lCancelCounter > 0)
                {
                    int left = in.lCancelCounter > kTmLcWindowFrames ? kTmLcWindowFrames : static_cast<int>(in.lCancelCounter);
                    st.lcPressed = 1;
                    st.lcEarlyExpired = 0;
                    st.lcWindowLeft = static_cast<s16>(left);
                    st.lcEarlyFrames = 0;
                }
                else if (st.lcPressed != 0)
                {
                    st.lcEarlyExpired = 1;
                    st.lcWindowLeft = 0;
                    if (in.hitlag <= 0 && st.lcEarlyFrames < 60)
                    {
                        st.lcEarlyFrames++;
                    }
                }
            }
            else if (in.status != kTmStAttackAir)
            {
                st.lcInAerial = 0;
            }
        }
        else if (!wantLc)
        {
            st.lcInAerial = 0;
        }

        // ---- 2. Wavedash tracking (JumpSquat 0x0A -> Jump 0x0B -> EscapeAir 0x21 -> 0x19) ----
        bool wdJustTriggered = false;
        if (wantWd && !abortState)
        {
            for (int k = 0; k < n; k++)
            {
                const TimingStatusEv& e = in.events[k];
                if (e.status == kTmStJumpSquat)
                {
                    st.wdPhase = 1;
                    st.wdSquatFrames = 1;
                    st.wdSquatLen = (in.jumpSquatLen >= 2 && in.jumpSquatLen <= 12) ? in.jumpSquatLen : 4;
                    st.wdDelay = 0;
                }
                else if (e.status == kTmStJump && e.prev == kTmStJumpSquat && st.wdPhase == 1)
                {
                    st.wdPhase = 2;
                    st.wdAirStartFrame = in.frame;
                }
                else if (e.status == kTmStEscapeAir && st.wdPhase == 2 && (in.frame - st.wdAirStartFrame) <= 12)
                {
                    st.wdPhase = 3;
                    st.wdDelay = static_cast<s16>(in.frame - st.wdAirStartFrame + 1);
                }
                else if (e.status == kTmStLandingFallSpecial && e.prev == kTmStEscapeAir && st.wdPhase == 3)
                {
                    if (st.wdDelay <= 2)
                    {
                        tmStartHold(st, kTmKindWavedash, kTmOutcomeHit, st.wdDelay, 0.72f, kTmColGreen, kTmHoldFrames,
                                    0);
                    }
                    else if (st.wdDelay <= 4)
                    {
                        u32 col = st.wdDelay == 3 ? kTmColCyan : kTmColGold;
                        tmStartHold(st, kTmKindWavedash, kTmOutcomeLate, st.wdDelay, 0.88f, col, kTmHoldFrames, 0);
                    }
                    else
                    {
                        tmStartHold(st, kTmKindWavedash, kTmOutcomeMiss, st.wdDelay, 1.0f, kTmColRed, kTmHoldFrames, 0);
                    }
                    st.wdPhase = 0;
                    wdJustTriggered = true;
                }
                else if (e.prev == kTmStEscapeAir && st.wdPhase == 3 && e.status != kTmStLandingFallSpecial)
                {
                    tmStartHold(st, kTmKindWavedash, kTmOutcomeMiss, st.wdDelay, 1.0f, kTmColRed, kTmHoldFrames, 0);
                    st.wdPhase = 0;
                    wdJustTriggered = true;
                }
                else if (st.wdPhase == 2 && e.status != kTmStJump && e.status != kTmStFall)
                {
                    // Normal aerial, special, double jump, or landing without airdodge.
                    st.wdPhase = 0;
                }
                else if (st.wdPhase == 1 && e.status != kTmStJumpSquat)
                {
                    st.wdPhase = 0;
                }
            }

            if (st.wdPhase == 1)
            {
                if (in.status == kTmStJumpSquat)
                {
                    if (st.wdSquatFrames < st.wdSquatLen)
                    {
                        st.wdSquatFrames++;
                    }
                }
                else
                {
                    st.wdPhase = 0;
                }
            }
            else if (st.wdPhase == 2)
            {
                if ((in.status != kTmStJump && in.status != kTmStFall) || (in.frame - st.wdAirStartFrame) > 10)
                {
                    st.wdPhase = 0;
                }
            }
        }
        else if (!wantWd)
        {
            st.wdPhase = 0;
        }

        // ---- 3. Act-Out lag countdown + exact first-actionable flash ----
        if (wantAct && !abortState)
        {
            if (in.actIn > 0 && in.status != kTmStJumpSquat)
            {
                if (st.actTracking == 0 || in.status != st.actStatus)
                {
                    st.actTracking = 1;
                    st.actStatus = in.status;
                    st.actPeak = in.actIn;
                }
                else if (in.actIn > st.actPeak)
                {
                    st.actPeak = in.actIn;
                }
                st.actLast = in.actIn;
            }
            else if (st.actTracking != 0)
            {
                if (in.actIn == 0 && st.actLast > 0)
                {
                    // Do not clobber a freshly triggered L-Cancel or Wavedash hold in All mode.
                    bool keepHigher = (in.mode == kTmModeAll) && (lcJustTriggered || wdJustTriggered
                                      || (st.holdFrames > 0
                                          && (st.holdKind == kTmKindLCancel || st.holdKind == kTmKindWavedash)));
                    if (!keepHigher)
                    {
                        tmStartHold(st, kTmKindActOut, kTmOutcomeHit, 0, 1.0f, kTmColFlashGreen, kTmActFlashFrames, 1);
                    }
                }
                st.actTracking = 0;
                st.actLast = 0;
            }
        }
        else
        {
            st.actTracking = 0;
        }

        // ---- 4. Resolve active render view (priority: live LC > live WD > hold > live ActOut) ----
        TimingBarView& v = st.view;
        if (wantLc && st.lcInAerial != 0)
        {
            v.phase = kTmPhaseActive;
            v.kind = kTmKindLCancel;
            v.hasZone = 1;
            if (st.lcPressed != 0 && st.lcEarlyExpired == 0)
            {
                v.zoneStart = 0.0f;
                v.zoneEnd = 1.0f;
                v.ratio = tmClamp01(static_cast<float>(st.lcWindowLeft) / static_cast<float>(kTmLcWindowFrames));
                v.fillColor = kTmColGreen;
            }
            else if (st.lcEarlyExpired != 0)
            {
                v.zoneStart = 0.65f;
                v.zoneEnd = 1.0f;
                v.ratio = 1.0f;
                v.fillColor = kTmColAmber;
            }
            else
            {
                v.zoneStart = 0.65f;
                v.zoneEnd = 1.0f;
                v.ratio = 0.0f;
                v.fillColor = kTmColNeutral;
            }
            return;
        }

        if (wantWd && (st.wdPhase == 1 || st.wdPhase == 2))
        {
            v.phase = kTmPhaseActive;
            v.kind = kTmKindWavedash;
            v.hasZone = 1;
            v.zoneStart = 0.62f;
            v.zoneEnd = 0.82f;
            if (st.wdPhase == 1)
            {
                float denom = st.wdSquatLen > 0 ? static_cast<float>(st.wdSquatLen) : 4.0f;
                v.ratio = tmClamp01((static_cast<float>(st.wdSquatFrames) / denom) * 0.62f);
                v.fillColor = kTmColCyan;
            }
            else
            {
                int airElapsed = static_cast<int>(in.frame - st.wdAirStartFrame + 1);
                float t = tmClamp01(static_cast<float>(airElapsed) / 6.0f);
                v.ratio = tmClamp01(0.62f + t * 0.38f);
                v.fillColor = airElapsed <= 2 ? kTmColGreen : (airElapsed <= 4 ? kTmColGold : kTmColRed);
            }
            return;
        }

        if (st.holdFrames > 0)
        {
            v.phase = kTmPhaseHold;
            v.kind = st.holdKind;
            v.outcome = st.holdOutcome;
            v.holdLeft = st.holdFrames;
            v.isFlash = st.holdFlash;
            v.ratio = st.holdRatio;
            v.fillColor = tmFadeAlpha(st.holdColor, st.holdFrames, st.holdMax);
            return;
        }

        if (wantAct && st.actTracking != 0 && st.actPeak > 0 && st.actLast > 0)
        {
            v.phase = kTmPhaseActive;
            v.kind = kTmKindActOut;
            v.hasZone = 0;
            v.ratio = tmClamp01(static_cast<float>(st.actLast) / static_cast<float>(st.actPeak));
            v.fillColor = st.actLast <= 3 ? kTmColGold : kTmColCyan;
        }
    }
}
}
