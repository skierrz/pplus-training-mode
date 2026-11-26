#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable DACUS and Gatling DACUS detection and grading core for
// Project+ 3.2.1. Zero Brawl headers so it compiles on both PowerPC (MWCC 4.2)
// and PC host tests.
//
// Verified P+ 3.2.1 mechanics (MoveDef/Misc.asm:565-572, Constants.asm:21-22):
//   - Transition: Attack_Dash (0x26) -> Attack_Hi4_Start (0x30) -> 0x31 / 0x32.
//   - Whiff cancel window is strictly nonHitlagDashFrame in {2, 3} (RA-Basic[1] < 4;
//     Frame 1 is impossible).
//   - RA-Basic[1] freezes during hitlag on all fighters, enabling Gatling DACUS
//     (e.g., Sonic Frame-2 dash attack hitting target and cancelling through
//     hitlag up to rawDashFrame f - da + 1 == 7).
namespace tm {
namespace extra {
    enum DacusStatusId {
        kDacusStatusJumpSquat = 0x0A,
        kDacusStatusAttackDash = 0x26,
        kDacusStatusAttackHi4Start = 0x30,
        kDacusStatusAttackHi4Hold = 0x31,
        kDacusStatusAttackHi4 = 0x32
    };

    enum DacusOutcome {
        kDacusNone = 0,
        kDacusSuccess = 1,
        kDacusSuccessGatling = 2,
        kDacusMissLate = 3,
        kDacusMissJumpCancel = 4
    };

    enum {
        kDacusMaxEvents = 8,
        kDacusBtnAttack = 1 << 0 // ipPadButton::MASK_ATTACK (also set by Z grab)
    };

    struct DacusEvent {
        s16 status;
        s16 prev;
    };

    struct DacusFrameInput {
        s32 frame;
        s16 status;
        s16 hitlag;
        s16 prevHitlag;
        s16 hitstun;
        u8 outOfPlay;
        u8 hasLiveRaBasic1; // 1 when raBasic1 was read live from status work
        s32 raBasic1;       // RA-Basic[1] (0x20000001) during Attack_Dash
        float x;
        float vx;
        float stickY;       // control stick Y (-1..1, up positive)
        float cStickY;      // C-stick Y (-1..1, up positive)
        u32 buttons;        // ipPadButton held mask
        u32 triggerButtons; // optional rising-edge trigger mask (0 if unavailable)
        u8 eventCount;
        DacusEvent events[kDacusMaxEvents];
    };

    struct DacusResult {
        u8 outcome;            // DacusOutcome
        u8 rawDashFrame;       // f - daStart + 1 (up to 7 on Sonic Gatling)
        u8 nonHitlagDashFrame; // strictly 2 or 3 on valid DACUS / Gatling
        u8 gatling;            // 1 if hitlag occurred during Attack_Dash before 0x30
        u8 lateByFrames;       // frames past window (1 = frame 4) on kDacusMissLate
        float vxCarry;         // horizontal velocity on Attack_Hi4_Start (0x30) entry
        float slideDistance;   // |x - x_start| tracked through 0x30 / 0x31 / 0x32
        s32 frame;
    };

    struct DacusState {
        u8 inDashAttack;
        u8 sawHitlagInDash;
        u8 lateMissReported;
        s32 dashStartFrame;
        s32 nonHitlagFrames; // matches RA-Basic[1] progression (freezes in hitlag)
        u8 trackingSlide;
        float slideStartX;
        u32 prevButtons;
        float prevCStickY;
        u32 attempts;
        u32 successCount;
        u32 gatlingCount;
        u8 completedThisFrame;
        DacusResult last;
    };

    inline float dacusAbsf(float v)
    {
        return v < 0.0f ? -v : v;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline u8 dacusClampU8(s32 v)
    {
        if (v < 0)
        {
            return 0;
        }
        return v > 255 ? static_cast<u8>(255) : static_cast<u8>(v);
    }

    inline bool dacusHi4Status(s16 s)
    {
        return s == kDacusStatusAttackHi4Start || s == kDacusStatusAttackHi4Hold || s == kDacusStatusAttackHi4;
    }

    inline void dacusReset(DacusState& st)
    {
        memset(&st, 0, sizeof(st));
        st.dashStartFrame = -1;
        st.last.frame = -1;
    }

    inline void dacusEmit(DacusState& st, u8 outcome, u8 rawFrame, u8 nonHitlagFrame, u8 gatling, u8 lateBy,
                          float vxCarry, float slideDist, s32 frame)
    {
        st.attempts++;
        if (outcome == kDacusSuccess || outcome == kDacusSuccessGatling)
        {
            st.successCount++;
        }
        if (outcome == kDacusSuccessGatling)
        {
            st.gatlingCount++;
        }
        st.completedThisFrame = 1;
        st.last.outcome = outcome;
        st.last.rawDashFrame = rawFrame;
        st.last.nonHitlagDashFrame = nonHitlagFrame;
        st.last.gatling = gatling;
        st.last.lateByFrames = lateBy;
        st.last.vxCarry = vxCarry;
        st.last.slideDistance = slideDist;
        st.last.frame = frame;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline bool dacusStep(DacusState& st, const DacusFrameInput& in)
    {
        // U-Smash vertical stick threshold (Constants.asm:21-22, IC-Basic[3158] = 0.73).
        const float kUpSmashStick = 0.73f;

        st.completedThisFrame = 0;
        if (in.outOfPlay || in.hitstun > 0)
        {
            st.inDashAttack = 0;
            st.sawHitlagInDash = 0;
            st.lateMissReported = 0;
            st.trackingSlide = 0;
            st.prevButtons = in.buttons;
            st.prevCStickY = in.cStickY;
            return false;
        }

        const bool freshAttack = ((in.buttons & kDacusBtnAttack) != 0 && (st.prevButtons & kDacusBtnAttack) == 0)
                                 || ((in.triggerButtons & kDacusBtnAttack) != 0);
        const bool freshCStickUp = (in.cStickY >= kUpSmashStick && st.prevCStickY < kUpSmashStick);
        const bool upSmashInput = (freshAttack && in.stickY >= kUpSmashStick) || freshCStickUp;

        const int evCount = in.eventCount < kDacusMaxEvents ? in.eventCount : kDacusMaxEvents;
        bool enteredDashThisFrame = false;
        bool enteredHi4ThisFrame = false;

        for (int k = 0; k < evCount; k++)
        {
            const s16 s = in.events[k].status;
            const s16 prev = in.events[k].prev;

            if (s == kDacusStatusAttackDash)
            {
                st.inDashAttack = 1;
                st.sawHitlagInDash = 0;
                st.lateMissReported = 0;
                st.dashStartFrame = in.frame;
                st.nonHitlagFrames = 1;
                st.trackingSlide = 0;
                enteredDashThisFrame = true;
            }
            else if (s == kDacusStatusAttackHi4Start && prev == kDacusStatusAttackDash && st.inDashAttack)
            {
                // Valid DACUS or Gatling DACUS!
                const bool wasHitlag = st.sawHitlagInDash != 0 || in.prevHitlag > 0;
                s32 rawFrame = in.frame - st.dashStartFrame + 1;
                if (rawFrame < 1)
                {
                    rawFrame = 1;
                }
                // RA-Basic[1] freezes during hitlag: when cancelling mid-hitlag (prevHitlag > 1),
                // nonHitlagFrames did not advance on this tick; on normal or post-hitlag frame 1
                // (prevHitlag <= 1), it advances by 1 (yielding 2 or 3).
                s32 nhFrame = st.nonHitlagFrames + (in.prevHitlag > 1 ? 0 : 1);
                if (nhFrame < 2)
                {
                    nhFrame = 2;
                }
                float slideStart = in.x - in.vx;
                float initSlide = dacusAbsf(in.x - slideStart);
                u8 outcome = wasHitlag ? static_cast<u8>(kDacusSuccessGatling) : static_cast<u8>(kDacusSuccess);
                dacusEmit(st, outcome, dacusClampU8(rawFrame), dacusClampU8(nhFrame), wasHitlag ? 1 : 0, 0,
                          in.vx, initSlide, in.frame);
                st.inDashAttack = 0;
                st.trackingSlide = 1;
                st.slideStartX = slideStart;
                enteredHi4ThisFrame = true;
            }
            else if (s == kDacusStatusAttackHi4Start && prev == kDacusStatusJumpSquat)
            {
                // Jump-Cancel Up-Smash instead of DACUS.
                float slideStart = in.x - in.vx;
                float initSlide = dacusAbsf(in.x - slideStart);
                dacusEmit(st, kDacusMissJumpCancel, 0, 0, 0, 0, in.vx, initSlide, in.frame);
                st.inDashAttack = 0;
                st.trackingSlide = 1;
                st.slideStartX = slideStart;
                enteredHi4ThisFrame = true;
            }
            else if (st.inDashAttack && s != kDacusStatusAttackDash)
            {
                st.inDashAttack = 0;
            }
        }

        // Advance Attack_Dash frame tracking while remaining in 0x26.
        if (st.inDashAttack && in.status == kDacusStatusAttackDash)
        {
            if (in.hitlag > 0)
            {
                st.sawHitlagInDash = 1;
            }
            if (!enteredDashThisFrame)
            {
                // Matches Attack_Dash CIL + execStop hitlag freeze (MoveDef/Misc.asm:565-572):
                // RA-Basic[1] increments on the first hitlag frame (prevHitlag == 0)
                // and on non-hitlag frames, and freezes while prevHitlag > 0 && hitlag > 0.
                if (!(in.prevHitlag > 0 && in.hitlag > 0))
                {
                    st.nonHitlagFrames++;
                }
            }
            if (in.hasLiveRaBasic1 && in.raBasic1 > 0)
            {
                st.nonHitlagFrames = in.raBasic1;
            }

            // Detect late Up+Attack / Up-Smash attempt after RA-Basic[1] >= 4.
            s32 effectiveRa1 = (in.hasLiveRaBasic1 && in.raBasic1 > 0) ? in.raBasic1 : st.nonHitlagFrames;
            if (!st.lateMissReported && effectiveRa1 >= 4 && upSmashInput)
            {
                s32 rawFrame = in.frame - st.dashStartFrame + 1;
                s32 lateBy = effectiveRa1 - 3;
                dacusEmit(st, kDacusMissLate, dacusClampU8(rawFrame), dacusClampU8(effectiveRa1),
                          st.sawHitlagInDash, dacusClampU8(lateBy), in.vx, 0.0f, in.frame);
                st.lateMissReported = 1;
            }
        }
        else if (in.status != kDacusStatusAttackDash)
        {
            st.inDashAttack = 0;
        }

        // Continuously measure Up-Smash slide distance across 0x30 / 0x31 / 0x32.
        if (st.trackingSlide)
        {
            if (dacusHi4Status(in.status))
            {
                float dist = dacusAbsf(in.x - st.slideStartX);
                if (dist > st.last.slideDistance || enteredHi4ThisFrame)
                {
                    st.last.slideDistance = dist;
                }
            }
            else
            {
                st.trackingSlide = 0;
            }
        }

        st.prevButtons = in.buttons;
        st.prevCStickY = in.cStickY;
        return st.completedThisFrame != 0;
    }
}
}
