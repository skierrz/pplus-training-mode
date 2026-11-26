#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable B-Reverse, Wavebounce, and Turnaround-B detection core for
// Project+ 3.2.1. Zero Brawl headers so it compiles on both PowerPC (MWCC 4.2)
// and PC host tests.
//
// Verified P+ 3.2.1 timing (Constants.asm:105-106, RSBE01 rev 2 0x8083BE04/0x8083A174):
//   - Special Frame 1: Phase 1 setStickLR(0.0f) flips lr only (no reflectSpeed)
//     and sets LA-Basic[55] (0x10000037) = 4.
//   - Special Frames 2 - 4: LA-Basic[55] decrements 4 -> 3 -> 2 -> 1 (single-frame
//     opposite stick pulses on Frames 2 - 4 do NOT reverse).
//   - Special Frame 5: LA-Basic[55] decrements 1 -> 0; if stickX opposes lr,
//     flips lr and calls reflectSpeed(-1.0f) after Frame 5's displacement.
//   - Special Frame 6: Reversed vx first appears with exact -1.0x multiplier
//     (no 0.8x decay).
namespace tm {
namespace extra {
    enum BReverseStatusId {
        kBRevStatusSpecialFirst = 0x112, // SpecialN
        kBRevStatusSpecialLast = 0x115,  // SpecialLw
        kBRevStatusFinal = 0x116
    };

    enum BReverseOutcome {
        kBRevNone = 0,
        kBRevTurnaroundB = 1, // lr1 == -lr0 && lr5 == -lr0
        kBRevBReverse = 2,    // lr1 ==  lr0 && lr5 == -lr0
        kBRevWavebounce = 3,  // lr1 == -lr0 && lr5 ==  lr0
        kBRevNeutral = 4      // lr1 ==  lr0 && lr5 ==  lr0
    };

    enum BReversePhase {
        kBRevPhaseIdle = 0,
        kBRevPhaseCountdown = 1,  // Special Frames 1..4 (LA-Basic[55] = 4..1)
        kBRevPhaseWaitFrame6 = 2, // Frame 5 hit LA-Basic[55] == 0; wait Frame 6 for vxAfter
        kBRevPhaseLateWatch = 3   // Watching Frames 6..11 for late flick hint on kBRevNeutral
    };

    enum {
        kBRevMaxEvents = 8,
        kBRevLateWatchMax = 6
    };

    struct BReverseEvent {
        s16 status;
        s16 prev;
    };

    struct BReverseFrameInput {
        s32 frame;
        s16 status;
        s16 hitstun;
        u8 airborne;
        u8 outOfPlay;
        s32 laBasic55; // LA-Basic[55] (0x10000037)
        float lr;      // +1.0 right, -1.0 left
        float vx;      // horizontal velocity / displacement on this frame
        float stickX;  // post-deadzone control stick X (-1..1)
        u8 eventCount;
        BReverseEvent events[kBRevMaxEvents];
    };

    struct BReverseResult {
        u8 outcome;          // BReverseOutcome
        u8 earlyFlickFrames; // 1..3 if opposite stick was held on Frames 2 - 4 and released before Frame 5
        u8 lateFlickFrames;  // 1..6 if opposite stick arrived 1..6 frames after Frame 5
        u8 airborne;         // 1 if airborne on Special Frame 1
        float lr0;           // facing on frame before special entry
        float lr1;           // facing after Special Frame 1
        float lr5;           // facing after Special Frame 5 (LA-Basic[55]: 1 -> 0)
        float vxBefore;      // horizontal velocity on Special Frame 5 (pre-reversal)
        float vxAfter;       // horizontal velocity on Special Frame 6 (post-reversal)
        float momentumRatio; // |vxAfter / vxBefore|
        s32 frame;
    };

    struct BReverseState {
        u8 phase;
        u8 hasPrev;
        float prevLr;
        float prevVx;
        s32 prevLa55;
        // Active special tracking.
        s32 startFrame;
        s32 reversalFrame;
        u8 specialFrame;
        u8 entryAirborne;
        float lr0;
        float vx0;
        float lr1;
        float lr5;
        float vx5;
        u8 lastEarlyOppositeSpFrame; // 2..4
        u8 lateWatchRemaining;
        // Totals and last completed result.
        u32 attempts;
        u32 bReverseCount;
        u32 wavebounceCount;
        u32 turnaroundCount;
        u8 completedThisFrame;
        BReverseResult last;
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline float brevAbsf(float v)
    {
        return v < 0.0f ? -v : v;
    }

    inline bool brevIsSpecialStatus(s16 s)
    {
        return s >= kBRevStatusSpecialFirst && s != kBRevStatusFinal;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline void breverseReset(BReverseState& st)
    {
        memset(&st, 0, sizeof(st));
        st.startFrame = -1;
        st.reversalFrame = -1;
        st.last.frame = -1;
    }

    inline bool breverseStep(BReverseState& st, const BReverseFrameInput& in)
    {
        const float kOppositeStickEps = 0.01f;
        st.completedThisFrame = 0;

        if (in.outOfPlay || in.hitstun > 0)
        {
            st.phase = kBRevPhaseIdle;
            st.lateWatchRemaining = 0;
            st.hasPrev = 0;
            st.prevLa55 = 0;
            return false;
        }

        // Detect entry into a reversible special (Special Frame 1: LA-Basic[55] armed to > 0,
        // typically 4). Supports both 0x112..0x115 and fighter-specific special sub-statuses.
        bool enteredSpecial = false;
        const int evCount = in.eventCount < kBRevMaxEvents ? in.eventCount : kBRevMaxEvents;
        for (int k = 0; k < evCount; k++)
        {
            if (brevIsSpecialStatus(in.events[k].status) && !brevIsSpecialStatus(in.events[k].prev))
            {
                enteredSpecial = true;
                break;
            }
        }
        bool freshLa55Arm = brevIsSpecialStatus(in.status) && in.laBasic55 >= 4 && st.prevLa55 <= 0;

        if ((enteredSpecial || freshLa55Arm) && in.laBasic55 > 0)
        {
            st.phase = kBRevPhaseCountdown;
            st.startFrame = in.frame;
            st.reversalFrame = -1;
            st.specialFrame = 1;
            st.entryAirborne = in.airborne ? 1 : 0;
            st.lr0 = st.hasPrev ? st.prevLr : in.lr;
            st.vx0 = st.hasPrev ? st.prevVx : in.vx;
            st.lr1 = in.lr;
            st.lr5 = in.lr;
            st.vx5 = in.vx;
            st.lastEarlyOppositeSpFrame = 0;
            st.lateWatchRemaining = 0;
        }
        else if (st.phase == kBRevPhaseCountdown)
        {
            if (!brevIsSpecialStatus(in.status))
            {
                // Left special early before completing the Frame 5 reversal tick.
                st.phase = kBRevPhaseIdle;
            }
            else
            {
                st.specialFrame++;
                if (in.laBasic55 == 0 && st.prevLa55 > 0)
                {
                    // Exact Special Frame 5 (LA-Basic[55]: 1 -> 0).
                    st.lr5 = in.lr;
                    st.vx5 = in.vx;
                    st.reversalFrame = in.frame;
                    st.phase = kBRevPhaseWaitFrame6;
                }
                else if (in.laBasic55 > 0)
                {
                    // Special Frames 2 - 4: record opposite stick for early-flick miss hint.
                    if (in.stickX * st.lr1 < -kOppositeStickEps)
                    {
                        st.lastEarlyOppositeSpFrame = st.specialFrame;
                    }
                }
                else
                {
                    st.phase = kBRevPhaseIdle;
                }
            }
        }
        else if (st.phase == kBRevPhaseWaitFrame6)
        {
            if (!brevIsSpecialStatus(in.status))
            {
                // Left special on Frame 6 before post-reversal velocity could be sampled.
                st.phase = kBRevPhaseIdle;
            }
            else
            {
                // Exact Special Frame 6: reversed vx appears here at -1.0x!
                const bool flipped1 = (st.lr1 * st.lr0 < 0.0f);
                const bool flipped5vs0 = (st.lr5 * st.lr0 < 0.0f);
                u8 outcome = kBRevNeutral;
                if (flipped1 && flipped5vs0)
                {
                    outcome = kBRevTurnaroundB;
                    st.turnaroundCount++;
                }
                else if (!flipped1 && flipped5vs0)
                {
                    outcome = kBRevBReverse;
                    st.bReverseCount++;
                }
                else if (flipped1 && !flipped5vs0)
                {
                    outcome = kBRevWavebounce;
                    st.wavebounceCount++;
                }
                else
                {
                    outcome = kBRevNeutral;
                }

                float vxBefore = st.vx5;
                float vxAfter = in.vx;
                float ratio = 0.0f;
                if (brevAbsf(vxBefore) > 0.01f)
                {
                    ratio = brevAbsf(vxAfter) / brevAbsf(vxBefore);
                }

                u8 earlyHint = 0;
                if (outcome == kBRevNeutral && st.lastEarlyOppositeSpFrame >= 2 && st.lastEarlyOppositeSpFrame < 5)
                {
                    earlyHint = static_cast<u8>(5 - st.lastEarlyOppositeSpFrame);
                }

                u8 lateHint = 0;
                if (outcome == kBRevNeutral && in.stickX * st.lr1 < -kOppositeStickEps)
                {
                    lateHint = 1; // 1 frame after Frame 5 (Special Frame 6)
                }

                st.attempts++;
                st.completedThisFrame = 1;
                st.last.outcome = outcome;
                st.last.earlyFlickFrames = earlyHint;
                st.last.lateFlickFrames = lateHint;
                st.last.airborne = st.entryAirborne;
                st.last.lr0 = st.lr0;
                st.last.lr1 = st.lr1;
                st.last.lr5 = st.lr5;
                st.last.vxBefore = vxBefore;
                st.last.vxAfter = vxAfter;
                st.last.momentumRatio = ratio;
                st.last.frame = in.frame;

                if (outcome == kBRevNeutral && lateHint == 0)
                {
                    st.phase = kBRevPhaseLateWatch;
                    st.lateWatchRemaining = kBRevLateWatchMax - 1; // frames +2..+6 remain
                }
                else
                {
                    st.phase = kBRevPhaseIdle;
                }
            }
        }
        else if (st.phase == kBRevPhaseLateWatch)
        {
            if (st.lateWatchRemaining == 0 || !brevIsSpecialStatus(in.status))
            {
                st.phase = kBRevPhaseIdle;
            }
            else
            {
                if (in.stickX * st.lr1 < -kOppositeStickEps)
                {
                    s32 late = in.frame - st.reversalFrame;
                    if (late >= 1 && late <= kBRevLateWatchMax)
                    {
                        st.last.lateFlickFrames = static_cast<u8>(late);
                    }
                    st.phase = kBRevPhaseIdle;
                    st.lateWatchRemaining = 0;
                }
                else
                {
                    st.lateWatchRemaining--;
                    if (st.lateWatchRemaining == 0)
                    {
                        st.phase = kBRevPhaseIdle;
                    }
                }
            }
        }

        st.hasPrev = 1;
        st.prevLr = in.lr;
        st.prevVx = in.vx;
        st.prevLa55 = in.laBasic55;
        return st.completedThisFrame != 0;
    }
}
}
