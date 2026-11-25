#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable Reverse Aerial Rush (RAR) detection and grading core for
// Project+ 3.2.1. Zero Brawl headers so it compiles on both PowerPC (MWCC 4.2)
// and PC host tests.
//
// Verified P+ 3.2.1 sequence (ft/fighter.h:24-31, 71, 759):
//   Run (0x04) -> Turn_Run (0x08) -> Jump_Squat (0x0A) -> Jump (0x0B)
//   -> Attack_Air (0x33) with motionKind == Attack_Air_B (0x64).
namespace tm {
namespace extra {
    enum RarStatusId {
        kRarStatusWalk = 0x01,
        kRarStatusWalkBrake = 0x02,
        kRarStatusDash = 0x03,
        kRarStatusRun = 0x04,
        kRarStatusRunBrake = 0x05,
        kRarStatusTurn = 0x06,
        kRarStatusTurnRun = 0x08,
        kRarStatusJumpSquat = 0x0A,
        kRarStatusJump = 0x0B,
        kRarStatusFall = 0x0E,
        kRarStatusAttackAir = 0x33,
        kRarMotionAttackAirB = 0x64
    };

    enum RarOutcome {
        kRarNone = 0,
        kRarSuccessBair = 1,
        kRarSuccessJumpOnly = 2,
        kRarFailNoFlip = 3,
        kRarFailWrongAerial = 4,
        kRarFailWalkTurn = 5
    };

    enum RarPhase {
        kRarPhaseIdle = 0,
        kRarPhaseTurnRun = 1,
        kRarPhaseJumpSquat = 2,
        kRarPhaseAirborne = 3,
        kRarPhaseRunJumpSquat = 4,
        kRarPhaseWalkTurn = 5,
        kRarPhaseWalkTurnSquat = 6
    };

    enum {
        kRarMaxEvents = 8,
        kRarAirTimeoutFrames = 40,
        kRarRecentRunWindow = 15,
        kRarRunBrakeGap = 4
    };

    struct RarEvent {
        s16 status;
        s16 prev;
    };

    struct RarFrameInput {
        s32 frame;
        s16 status;
        s16 motionKind;
        s16 hitstun;
        u8 airborne;
        u8 outOfPlay;
        float lr; // +1.0 right, -1.0 left
        float vx; // horizontal displacement / velocity over this frame
        u8 eventCount;
        RarEvent events[kRarMaxEvents];
    };

    struct RarResult {
        u8 outcome;          // RarOutcome
        u8 turnRunFrames;    // frames in Turn_Run (0x08) before Jump_Squat (1 = frame-perfect)
        u8 bairDelay;        // frames from Jump (0x0B) entry to Attack_Air (0x33) (1 = frame-1 aerial)
        u8 backwardMomentum; // 1 if lr_jump == -lr_run and vx_takeoff * lr_jump < 0
        float momentumRetained; // |vx_takeoff| / |vx_run_pre_turn| when backwardMomentum == 1
        float takeoffVx;
        float runVxPreTurn;
        s32 frame;
    };

    struct RarState {
        u8 phase;
        s32 lastRunFrame;
        float runLr;
        float runVx;
        s32 turnRunStart;
        u8 pendingTurnRunFrames;
        float savedRunLr;
        float savedRunVx;
        s32 jumpStart;
        u8 pendingBackwardMomentum;
        float pendingMomentumRetained;
        float pendingTakeoffVx;
        u32 attempts;
        u32 successBairCount;
        u32 successJumpOnlyCount;
        u8 completedThisFrame;
        RarResult last;
    };

    inline float rarAbsf(float v)
    {
        return v < 0.0f ? -v : v;
    }

    inline u8 rarClampU8(s32 v)
    {
        if (v < 0)
        {
            return 0;
        }
        return v > 255 ? static_cast<u8>(255) : static_cast<u8>(v);
    }

    inline void rarReset(RarState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastRunFrame = -1;
        st.turnRunStart = -1;
        st.jumpStart = -1;
        st.last.frame = -1;
    }

    inline void rarEmit(RarState& st, u8 outcome, u8 turnFrames, u8 bairDelay, u8 backwardMom,
                        float momRetained, float takeoffVx, float runVx, s32 frame)
    {
        st.attempts++;
        if (outcome == kRarSuccessBair)
        {
            st.successBairCount++;
        }
        else if (outcome == kRarSuccessJumpOnly)
        {
            st.successJumpOnlyCount++;
        }
        st.completedThisFrame = 1;
        st.last.outcome = outcome;
        st.last.turnRunFrames = turnFrames;
        st.last.bairDelay = bairDelay;
        st.last.backwardMomentum = backwardMom;
        st.last.momentumRetained = momRetained;
        st.last.takeoffVx = takeoffVx;
        st.last.runVxPreTurn = runVx;
        st.last.frame = frame;
        st.phase = kRarPhaseIdle;
    }

    inline bool rarWalkTurnSource(s16 prev)
    {
        return prev == kRarStatusWalk || prev == kRarStatusWalkBrake || prev == kRarStatusDash
               || prev == kRarStatusRun || prev == kRarStatusRunBrake;
    }

    inline bool rarStep(RarState& st, const RarFrameInput& in)
    {
        st.completedThisFrame = 0;
        if (in.outOfPlay || in.hitstun > 0)
        {
            st.phase = kRarPhaseIdle;
            st.lastRunFrame = -1;
            return false;
        }

        const int evCount = in.eventCount < kRarMaxEvents ? in.eventCount : kRarMaxEvents;
        bool sawAttackAir = false;
        for (int k = 0; k < evCount; k++)
        {
            const s16 s = in.events[k].status;
            const s16 prev = in.events[k].prev;

            if (s == kRarStatusTurnRun
                && (prev == kRarStatusRun
                    || (prev == kRarStatusRunBrake && st.lastRunFrame >= 0
                        && (in.frame - st.lastRunFrame) <= kRarRunBrakeGap)))
            {
                st.phase = kRarPhaseTurnRun;
                st.turnRunStart = in.frame;
                st.savedRunLr = st.runLr != 0.0f ? st.runLr : (in.lr != 0.0f ? -in.lr : 1.0f);
                st.savedRunVx = st.runVx != 0.0f ? st.runVx : in.vx;
                st.pendingTurnRunFrames = 0;
                continue;
            }

            if (s == kRarStatusTurn)
            {
                bool recentRun = st.lastRunFrame >= 0 && (in.frame - st.lastRunFrame) <= kRarRecentRunWindow;
                if (rarWalkTurnSource(prev) || recentRun)
                {
                    st.phase = kRarPhaseWalkTurn;
                    st.savedRunLr = st.runLr != 0.0f ? st.runLr : in.lr;
                    st.savedRunVx = st.runVx;
                }
                else
                {
                    st.phase = kRarPhaseIdle;
                }
                continue;
            }

            if (s == kRarStatusJumpSquat)
            {
                if (st.phase == kRarPhaseTurnRun && prev == kRarStatusTurnRun)
                {
                    s32 elapsed = in.frame - st.turnRunStart;
                    st.pendingTurnRunFrames = rarClampU8(elapsed > 0 ? elapsed : 1);
                    st.phase = kRarPhaseJumpSquat;
                }
                else if (prev == kRarStatusRun)
                {
                    st.savedRunLr = st.runLr != 0.0f ? st.runLr : in.lr;
                    st.savedRunVx = st.runVx != 0.0f ? st.runVx : in.vx;
                    st.pendingTurnRunFrames = 0;
                    st.phase = kRarPhaseRunJumpSquat;
                }
                else if (st.phase == kRarPhaseWalkTurn && prev == kRarStatusTurn)
                {
                    st.pendingTurnRunFrames = 0;
                    st.phase = kRarPhaseWalkTurnSquat;
                }
                else
                {
                    st.phase = kRarPhaseIdle;
                }
                continue;
            }

            if (s == kRarStatusJump && prev == kRarStatusJumpSquat)
            {
                if (st.phase == kRarPhaseJumpSquat)
                {
                    if (in.lr * st.savedRunLr > 0.0f)
                    {
                        rarEmit(st, kRarFailNoFlip, st.pendingTurnRunFrames, 0, 0, 0.0f, in.vx, st.savedRunVx,
                                in.frame);
                    }
                    else
                    {
                        bool backward = (in.lr * st.savedRunLr < 0.0f) && (in.vx * in.lr < 0.0f);
                        float retained = 0.0f;
                        if (backward && rarAbsf(st.savedRunVx) > 0.01f)
                        {
                            retained = rarAbsf(in.vx) / rarAbsf(st.savedRunVx);
                        }
                        st.phase = kRarPhaseAirborne;
                        st.jumpStart = in.frame;
                        st.pendingBackwardMomentum = backward ? 1 : 0;
                        st.pendingMomentumRetained = retained;
                        st.pendingTakeoffVx = in.vx;
                    }
                }
                else if (st.phase == kRarPhaseRunJumpSquat)
                {
                    if (in.lr * st.savedRunLr > 0.0f)
                    {
                        rarEmit(st, kRarFailNoFlip, 0, 0, 0, 0.0f, in.vx, st.savedRunVx, in.frame);
                    }
                    else
                    {
                        st.phase = kRarPhaseIdle;
                    }
                }
                else if (st.phase == kRarPhaseWalkTurnSquat)
                {
                    rarEmit(st, kRarFailWalkTurn, 0, 0, 0, 0.0f, in.vx, st.savedRunVx, in.frame);
                }
                continue;
            }

            if (s == kRarStatusAttackAir)
            {
                sawAttackAir = true;
                continue;
            }

            if (st.phase == kRarPhaseAirborne)
            {
                if (s != kRarStatusJump && s != kRarStatusFall)
                {
                    rarEmit(st, kRarSuccessJumpOnly, st.pendingTurnRunFrames, 0, st.pendingBackwardMomentum,
                            st.pendingMomentumRetained, st.pendingTakeoffVx, st.savedRunVx, in.frame);
                }
            }
            else if (st.phase != kRarPhaseIdle)
            {
                st.phase = kRarPhaseIdle;
            }
        }

        if (st.phase == kRarPhaseAirborne)
        {
            if (sawAttackAir || in.status == kRarStatusAttackAir)
            {
                u8 delay = rarClampU8(in.frame - st.jumpStart + 1);
                if (delay < 1)
                {
                    delay = 1;
                }
                u8 outcome = (in.motionKind == kRarMotionAttackAirB) ? static_cast<u8>(kRarSuccessBair)
                                                                     : static_cast<u8>(kRarFailWrongAerial);
                rarEmit(st, outcome, st.pendingTurnRunFrames, delay, st.pendingBackwardMomentum,
                        st.pendingMomentumRetained, st.pendingTakeoffVx, st.savedRunVx, in.frame);
            }
            else if (!in.airborne || (in.status != kRarStatusJump && in.status != kRarStatusFall)
                     || (in.frame - st.jumpStart >= kRarAirTimeoutFrames))
            {
                rarEmit(st, kRarSuccessJumpOnly, st.pendingTurnRunFrames, 0, st.pendingBackwardMomentum,
                        st.pendingMomentumRetained, st.pendingTakeoffVx, st.savedRunVx, in.frame);
            }
        }

        // Snapshot Run facing and horizontal speed for the next frame's potential Turn_Run.
        if (in.status == kRarStatusRun)
        {
            st.lastRunFrame = in.frame;
            st.runLr = in.lr;
            st.runVx = in.vx;
        }

        return st.completedThisFrame != 0;
    }
}
}
