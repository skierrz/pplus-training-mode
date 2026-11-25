#pragma once

#include <string.h>
#include <types.h>

#include "extra/challenge_common_core.h"
#include "extra/stage_geom_math.h"

// Pure host-testable Project+ 3.2.1 Movement Flow Course core.
// Dynamically computes a 4-gate movement sequence from StageGeometry:
//   Gate 0: Dash-dance turnaround zone near stage center
//   Gate 1: Platform waveland (or floor wavedash on flat stages)
//   Gate 2: Reverse Aerial Rush (RAR) takeoff gate
//   Gate 3: Opposite-edge horizontal burst finish
namespace tm {
namespace extra {
    enum FlowGateKind {
        kFlowGateDashDance = 0,
        kFlowGateWaveland = 1,
        kFlowGateRar = 2,
        kFlowGateEdgeBurst = 3,
        kFlowGateCount = 4
    };

    enum FlowCourseConst {
        kFcStDash = 0x03,
        kFcStRun = 0x04,
        kFcStRunBrake = 0x05,
        kFcStTurnDash = 0x07,
        kFcStTurnRun = 0x08,
        kFcStJumpSquat = 0x0A,
        kFcStJump = 0x0B,
        kFcStLandingFallSpecial = 0x19,
        kFcStEscapeAir = 0x21
    };

    struct FlowGate {
        float minX;
        float maxX;
        float y;
        float tolY;
    };

    // 36-byte naturally aligned telemetry block mirrored directly into "TM4X".
    struct FlowCourseStats {
        u32 coursesCompleted;
        u32 currentElapsed;
        u32 bestCourseFrames;
        u16 splitFrames[kFlowGateCount];
        u16 flowStreak;
        u16 bestFlowStreak;
        u8 activeGate;
        u8 hasPlatformGate;
        u8 running;
        u8 pad;
        float gateX;
        float gateY;
    };

    struct FlowCourseState {
        FlowCourseStats out;
        u8 layoutValid;
        u8 rarStage;     // 0=none, 1=sawTurnRun, 2=sawJumpSquat
        u8 sawEscapeAir;
        u8 dashDirs;     // bit 0=right, bit 1=left
        u8 prevFacing;   // 0=unset, 1=right, 2=left
        u8 rarRunFacing; // 1=right, 2=left
        u8 gateAdvancedThisFrame;
        u8 courseCompletedThisFrame;
        float floorLeftX;
        float floorRightX;
        float edgeWidth;
        s32 courseStartFrame;
        s32 gateStartFrame;
        s32 lastRunFrame;
        FlowGate gates[kFlowGateCount];
    };

    inline void fcSetTarget(FlowCourseState& st)
    {
        const FlowGate& g = st.gates[st.out.activeGate & 3u];
        st.out.gateX = 0.5f * (g.minX + g.maxX);
        st.out.gateY = g.y;
    }

    inline void flowCourseResetTransient(FlowCourseState& st)
    {
        st.out.activeGate = 0;
        st.out.running = 0;
        st.out.currentElapsed = 0;
        st.rarStage = 0;
        st.sawEscapeAir = 0;
        st.dashDirs = 0;
        st.prevFacing = 0;
        st.rarRunFacing = 0;
        st.courseStartFrame = kChallengeNoFrame;
        st.gateStartFrame = kChallengeNoFrame;
        st.lastRunFrame = kChallengeNoFrame;
        st.gateAdvancedThisFrame = 0;
        st.courseCompletedThisFrame = 0;
        fcSetTarget(st);
    }

    inline void flowCourseResetAll(FlowCourseState& st)
    {
        memset(&st, 0, sizeof(st));
        flowCourseResetTransient(st);
    }

    inline void flowCourseSetGeometry(FlowCourseState& st, const StageGeometry& g)
    {
        const float lx = g.floor.leftX;
        const float rx = g.floor.rightX;
        const float span = rx - lx;
        if (!g.floor.valid || span <= 10.0f)
        {
            return;
        }

        const StagePlatform* best = NULL;
        int np = g.platformCount < kMaxStagePlatforms ? g.platformCount : kMaxStagePlatforms;
        for (int i = 0; i < np; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (p.valid && p.rightX > p.leftX + 4.0f
                && (best == NULL || p.y < best->y || (p.y == best->y && p.leftX > best->leftX)))
            {
                best = &p;
            }
        }

        const u8 hasPlat = best != NULL ? 1u : 0u;
        const bool floorChanged = !st.layoutValid || st.floorLeftX != lx || st.floorRightX != rx
                                  || st.out.hasPlatformGate != hasPlat;
        const bool keepGate3 = st.layoutValid && st.out.running && st.out.activeGate == kFlowGateEdgeBurst;

        if (floorChanged)
        {
            const float fy = g.floor.centerY;
            st.layoutValid = 1;
            st.floorLeftX = lx;
            st.floorRightX = rx;
            st.out.hasPlatformGate = hasPlat;
            float ew = span * 0.22f;
            st.edgeWidth = ew < 16.0f ? 16.0f : ew;

            static const float kSpec[4][3] = {
                {0.25f, 0.75f, 6.0f},  // Gate 0: center dash-dance
                {0.15f, 0.85f, 6.0f},  // Gate 1: flat fallback wavedash
                {0.10f, 0.90f, 40.0f}, // Gate 2: RAR takeoff
                {0.00f, 0.22f, 12.0f}  // Gate 3: edge burst
            };
            int nGates = keepGate3 ? 3 : 4;
            for (int i = 0; i < nGates; i++)
            {
                st.gates[i].minX = lx + span * kSpec[i][0];
                st.gates[i].maxX = lx + span * kSpec[i][1];
                st.gates[i].y = fy;
                st.gates[i].tolY = kSpec[i][2];
            }
            if (!keepGate3)
            {
                st.gates[3].maxX = lx + st.edgeWidth;
            }
        }

        bool platUpdated = false;
        if (best != NULL)
        {
            float pMin = best->leftX - 3.0f;
            float pMax = best->rightX + 3.0f;
            if (floorChanged || st.gates[1].minX != pMin || st.gates[1].maxX != pMax || st.gates[1].y != best->y)
            {
                st.gates[1].minX = pMin;
                st.gates[1].maxX = pMax;
                st.gates[1].y = best->y;
                st.gates[1].tolY = 5.0f;
                platUpdated = true;
            }
        }
        if (floorChanged || platUpdated)
        {
            fcSetTarget(st);
        }
    }

    inline bool flowCourseStep(FlowCourseState& st, const ChallengeFrame& in)
    {
        st.gateAdvancedThisFrame = 0;
        st.courseCompletedThisFrame = 0;
        if (!st.layoutValid)
        {
            return false;
        }
        if (in.outOfPlay || in.hitstun > 0)
        {
            if (st.out.running)
            {
                st.out.flowStreak = 0;
            }
            flowCourseResetTransient(st);
            return false;
        }
        if (st.out.running)
        {
            st.out.currentElapsed = static_cast<u32>(in.frame - st.courseStartFrame);
        }

        const u8 facing = (in.lr < 0.0f) ? 2u : 1u;
        const int evN = in.eventCount < kChallengeMaxEvents ? static_cast<int>(in.eventCount) : kChallengeMaxEvents;
        u8 flags = (in.status == kFcStTurnDash) ? 1u : 0u; // bit0=turnDash, bit1=wl, bit2=rar

        for (int k = 0; k < evN; k++)
        {
            const s16 s = in.events[k].status;
            const s16 p = in.events[k].prev;
            switch (s)
            {
            case kFcStTurnDash:
                flags |= 1u;
                st.sawEscapeAir = 0;
                st.rarStage = 0;
                break;
            case kFcStEscapeAir:
                st.sawEscapeAir = 1;
                st.rarStage = 0;
                break;
            case kFcStLandingFallSpecial:
                if (p == kFcStEscapeAir || st.sawEscapeAir)
                {
                    flags |= 2u;
                }
                st.sawEscapeAir = 0;
                st.rarStage = 0;
                break;
            case kFcStTurnRun:
                st.sawEscapeAir = 0;
                if (p == kFcStRun || (p == kFcStRunBrake && in.frame - st.lastRunFrame <= 4))
                {
                    st.rarStage = 1;
                    st.rarRunFacing = st.prevFacing ? st.prevFacing : static_cast<u8>(3u - facing);
                }
                else
                {
                    st.rarStage = 0;
                }
                break;
            case kFcStJumpSquat:
                st.sawEscapeAir = 0;
                st.rarStage = (p == kFcStTurnRun && st.rarStage == 1) ? 2 : 0;
                break;
            case kFcStJump:
                st.sawEscapeAir = 0;
                if (p == kFcStJumpSquat && st.rarStage == 2 && facing != st.rarRunFacing)
                {
                    flags |= 4u;
                }
                st.rarStage = 0;
                break;
            default:
                st.sawEscapeAir = 0;
                st.rarStage = 0;
                break;
            }
        }
        if (in.status == kFcStEscapeAir)
        {
            st.sawEscapeAir = 1;
        }
        else if (!in.airborne && in.status != kFcStLandingFallSpecial)
        {
            st.sawEscapeAir = 0;
        }

        const u8 g = st.out.activeGate;
        const FlowGate& gate = st.gates[g];
        float dy = in.y - gate.y;
        bool hit = false;
        if (in.x >= gate.minX && in.x <= gate.maxX && dy * dy <= gate.tolY * gate.tolY)
        {
            if (g == kFlowGateDashDance)
            {
                if (chInRange(in.status, kFcStDash, kFcStRun))
                {
                    st.dashDirs |= facing;
                }
                hit = (flags & 1u) != 0 || st.dashDirs == 3;
            }
            else if (g == kFlowGateEdgeBurst)
            {
                hit = !in.airborne && (in.vx * in.vx >= 0.49f || chInRange(in.status, kFcStDash, kFcStRun) || in.status == kFcStLandingFallSpecial);
            }
            else
            {
                hit = (flags & (1u << g)) != 0;
            }
        }

        if (hit)
        {
            if (!st.out.running)
            {
                st.out.running = 1;
                st.courseStartFrame = in.frame;
                st.gateStartFrame = in.frame;
            }
            st.out.splitFrames[g] = chClampU16(in.frame - st.gateStartFrame, 1);
            st.gateStartFrame = in.frame;
            st.gateAdvancedThisFrame = 1;
            st.rarStage = 0;
            st.sawEscapeAir = 0;

            if (g == kFlowGateRar)
            {
                bool goR = (in.vx > 0.1f) || (in.vx == 0.0f && facing == 2);
                st.gates[3].minX = goR ? (st.floorRightX - st.edgeWidth) : st.floorLeftX;
                st.gates[3].maxX = goR ? st.floorRightX : (st.floorLeftX + st.edgeWidth);
            }

            st.out.activeGate = static_cast<u8>((g + 1) & 3u);
            if (g == kFlowGateEdgeBurst)
            {
                u32 total = static_cast<u32>(in.frame - st.courseStartFrame + 1);
                st.out.coursesCompleted++;
                st.out.currentElapsed = total;
                if (st.out.bestCourseFrames == 0 || total < st.out.bestCourseFrames)
                {
                    st.out.bestCourseFrames = total;
                }
                chBumpStreak(st.out.flowStreak, st.out.bestFlowStreak);
                st.courseCompletedThisFrame = 1;
                st.out.running = 0;
                st.dashDirs = 0;
            }
            fcSetTarget(st);
        }

        if (in.status == kFcStRun)
        {
            st.lastRunFrame = in.frame;
        }
        st.prevFacing = facing;
        return st.gateAdvancedThisFrame != 0;
    }
}
}
