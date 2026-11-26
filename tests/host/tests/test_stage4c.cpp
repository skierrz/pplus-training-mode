// Comprehensive PC host unit tests for all five Stage 4C novel P+ challenge
// backend cores and "TM4X" RAM debug block layout:
//   1. challenge_cancel_sprint_core.h
//   2. challenge_flow_course_core.h
//   3. challenge_reaction_techchase_core.h
//   4. challenge_recovery_floorhug_core.h
//   5. challenge_snake_recovery_core.h
//   + ChallengeDebugBlock ("TM4X") alignment and multi-slot independence.
#include <stddef.h>
#include <string.h>

#include "extra/challenge_backends.h"
#include "host/test.h"

using namespace tm::extra;

namespace {
    StageGeometry makeFdGeom()
    {
        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.blast.left = -246.0f;
        g.blast.right = 246.0f;
        g.blast.top = 188.0f;
        g.blast.bottom = -140.0f;
        g.blast.valid = true;
        g.floor.leftX = -85.0f;
        g.floor.rightX = 85.0f;
        g.floor.centerY = 0.0f;
        g.floor.valid = true;
        g.leftLedge.x = -85.0f;
        g.leftLedge.y = 0.0f;
        g.leftLedge.dir = 1.0f;
        g.rightLedge.x = 85.0f;
        g.rightLedge.y = 0.0f;
        g.rightLedge.dir = -1.0f;
        g.hasLedges = true;
        g.platformCount = 0;
        return g;
    }

    StageGeometry makeBfGeom()
    {
        StageGeometry g = makeFdGeom();
        g.floor.leftX = -68.4f;
        g.floor.rightX = 68.4f;
        g.leftLedge.x = -68.4f;
        g.rightLedge.x = 68.4f;
        g.platformCount = 3;
        g.platforms[0].leftX = -57.6f;
        g.platforms[0].rightX = -20.0f;
        g.platforms[0].y = 27.2f;
        g.platforms[0].valid = true;
        g.platforms[1].leftX = 20.0f;
        g.platforms[1].rightX = 57.6f;
        g.platforms[1].y = 27.2f;
        g.platforms[1].valid = true;
        g.platforms[2].leftX = -18.8f;
        g.platforms[2].rightX = 18.8f;
        g.platforms[2].y = 54.4f;
        g.platforms[2].valid = true;
        return g;
    }

    ChallengeFrame makeFrame(s32 f, int ft, s16 st, u32 groups = 0, s16 mk = -1)
    {
        ChallengeFrame in;
        memset(&in, 0, sizeof(in));
        in.valid = 1;
        in.frame = f;
        in.ftKind = ft;
        in.status = st;
        in.motionKind = mk;
        in.groups = groups;
        in.lr = 1.0f;
        return in;
    }

    void addEv(ChallengeFrame& in, s16 prev, s16 st, s16 mk = -1)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.events[in.eventCount].motionKind = mk;
        in.eventCount++;
    }
}

TEST(stage4c, cancel_sprint_spacies_shine_fox_falco_wolf)
{
    const u32 kJumpGrp = 1u << kCsGroupJumpGround;

    // Fox: 0x115 -> 0x11A (frame 4), same-frame JC -> 1f perfect, then 3f cancel, then miss.
    CancelSprintState fox;
    cancelSprintResetAll(fox);
    {
        ChallengeFrame in = makeFrame(10, kCsFtFox, kCsStSpecialLw);
        addEv(in, 0x00, kCsStSpecialLw);
        CHECK(!cancelSprintStep(fox, in));
        CHECK_EQ(1, fox.out.active);

        in = makeFrame(13, kCsFtFox, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStSpecialLw, kCsStSpacieShineLoop);
        addEv(in, kCsStSpacieShineLoop, kCsStJumpSquat);
        CHECK(cancelSprintStep(fox, in));
        CHECK_EQ(1u, fox.out.attempts);
        CHECK_EQ(1u, fox.out.totalCancels);
        CHECK_EQ(1u, fox.out.perfectCount);
        CHECK_EQ(1u, fox.out.bestFrames);
        CHECK_EQ(1u, fox.out.lastFrames);
        CHECK_EQ(1u, fox.out.streak);

        // 2nd shine: 0x11A opens at 23, JC at 25 -> 3f.
        in = makeFrame(20, kCsFtFox, kCsStSpecialLw);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(fox, in);
        in = makeFrame(23, kCsFtFox, kCsStSpacieShineLoop, kJumpGrp);
        addEv(in, kCsStSpecialLw, kCsStSpacieShineLoop);
        cancelSprintStep(fox, in);
        cancelSprintStep(fox, makeFrame(24, kCsFtFox, kCsStSpacieShineLoop, kJumpGrp));
        in = makeFrame(25, kCsFtFox, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStSpacieShineLoop, kCsStJumpSquat);
        CHECK(cancelSprintStep(fox, in));
        CHECK_EQ(2u, fox.out.totalCancels);
        CHECK_EQ(1u, fox.out.perfectCount);
        CHECK_EQ(3u, fox.out.lastFrames);
        CHECK_EQ(2u, fox.out.streak);
        CHECK_NEAR(2.0f, cancelSprintAvgFrames(fox), 0.001f);

        // 3rd shine: dropped into 0x11C without JC -> miss.
        in = makeFrame(30, kCsFtFox, kCsStSpecialLw);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(fox, in);
        in = makeFrame(33, kCsFtFox, kCsStSpacieShineEnd);
        addEv(in, kCsStSpacieShineLoop, kCsStSpacieShineEnd);
        CHECK(cancelSprintStep(fox, in));
        CHECK_EQ(3u, fox.out.attempts);
        CHECK_EQ(2u, fox.out.totalCancels);
        CHECK_EQ(0u, fox.out.streak);
        CHECK_EQ(2u, fox.out.bestStreak);
        CHECK_EQ(66u, cancelSprintRatePct(fox));
    }

    // Falco: stays in 0x115 (FitFalco.pac 0x2842c), jump group opens at frame 4.
    CancelSprintState falco;
    cancelSprintResetAll(falco);
    {
        ChallengeFrame in = makeFrame(50, kCsFtFalco, kCsStSpecialLw, 0);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(falco, in);
        cancelSprintStep(falco, makeFrame(51, kCsFtFalco, kCsStSpecialLw, 0));
        cancelSprintStep(falco, makeFrame(52, kCsFtFalco, kCsStSpecialLw, 0));
        cancelSprintStep(falco, makeFrame(53, kCsFtFalco, kCsStSpecialLw, kJumpGrp));
        in = makeFrame(54, kCsFtFalco, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStSpecialLw, kCsStJumpSquat);
        CHECK(cancelSprintStep(falco, in));
        CHECK_EQ(1u, falco.out.totalCancels);
        CHECK_EQ(2u, falco.out.lastFrames);
        CHECK_EQ(kCancelKindShine, falco.out.lastKind);
    }

    // Wolf: 0x115 -> 0x11A -> JumpAerial.
    CancelSprintState wolf;
    cancelSprintResetAll(wolf);
    {
        ChallengeFrame in = makeFrame(70, kCsFtWolf, kCsStSpecialLw);
        addEv(in, 0x0E, kCsStSpecialLw);
        cancelSprintStep(wolf, in);
        in = makeFrame(73, kCsFtWolf, kCsStJumpAerial, 1u << kCsGroupJumpAir);
        addEv(in, kCsStSpecialLw, kCsStSpacieShineLoop);
        addEv(in, kCsStSpacieShineLoop, kCsStJumpAerial);
        CHECK(cancelSprintStep(wolf, in));
        CHECK_EQ(1u, wolf.out.perfectCount);
        CHECK_EQ(1u, wolf.out.lastFrames);
    }
}

TEST(stage4c, cancel_sprint_pk_magnet_mewtwo_hover_lucario_asc)
{
    const u32 kJumpGrp = 1u << kCsGroupJumpGround;

    // Ness Magnet: 0x115 -> 0x11E -> 0x0A.
    CancelSprintState ness;
    cancelSprintResetAll(ness);
    {
        ChallengeFrame in = makeFrame(10, kCsFtNess, kCsStSpecialLw);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(ness, in);
        in = makeFrame(14, kCsFtNess, kCsStNessMagnetHold, 0);
        addEv(in, kCsStSpecialLw, kCsStNessMagnetHold);
        cancelSprintStep(ness, in);
        cancelSprintStep(ness, makeFrame(16, kCsFtNess, kCsStNessMagnetHold, kJumpGrp));
        in = makeFrame(16, kCsFtNess, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStNessMagnetHold, kCsStJumpSquat);
        CHECK(cancelSprintStep(ness, in));
        CHECK_EQ(kCancelKindMagnet, ness.out.lastKind);
        CHECK_EQ(1u, ness.out.perfectCount);
    }

    // Lucas Magnet: 0x115 -> 0x11F -> 0x121 (FitLucas.pac 0x281d4) -> 0x0A.
    CancelSprintState lucas;
    cancelSprintResetAll(lucas);
    {
        ChallengeFrame in = makeFrame(20, kCsFtLucas, kCsStSpecialLw);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(lucas, in);
        in = makeFrame(24, kCsFtLucas, kCsStLucasMagnetHold, 0);
        addEv(in, kCsStSpecialLw, kCsStLucasMagnetHold);
        cancelSprintStep(lucas, in);
        in = makeFrame(28, kCsFtLucas, kCsStLucasMagnetEnd, 0);
        addEv(in, kCsStLucasMagnetHold, kCsStLucasMagnetEnd);
        cancelSprintStep(lucas, in);
        cancelSprintStep(lucas, makeFrame(29, kCsFtLucas, kCsStLucasMagnetEnd, kJumpGrp));
        in = makeFrame(30, kCsFtLucas, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStLucasMagnetEnd, kCsStJumpSquat);
        CHECK(cancelSprintStep(lucas, in));
        CHECK_EQ(kCancelKindMagnet, lucas.out.lastKind);
        CHECK_EQ(1u, lucas.out.totalCancels);
        CHECK_EQ(2u, lucas.out.lastFrames);
    }

    // Mewtwo Hover Cancel: 0x120 + AttackAirN (0x62) -> Landing (0x16).
    CancelSprintState m2;
    cancelSprintResetAll(m2);
    {
        ChallengeFrame in = makeFrame(40, kCsFtMewtwo, kCsStMewtwoHover, 0, -1);
        addEv(in, 0x0B, kCsStMewtwoHover);
        cancelSprintStep(m2, in);
        cancelSprintStep(m2, makeFrame(42, kCsFtMewtwo, kCsStMewtwoHover, 0, kCsMotAttackAirN));
        in = makeFrame(44, kCsFtMewtwo, kCsStLanding, 0, 0x16);
        addEv(in, kCsStMewtwoHover, kCsStLanding, kCsMotAttackAirN);
        CHECK(cancelSprintStep(m2, in));
        CHECK_EQ(kCancelKindHover, m2.out.lastKind);
        CHECK_EQ(1u, m2.out.totalCancels);
        CHECK_EQ(3u, m2.out.lastFrames);
    }

    // Lucario ASC: 0x117 -> 0x11E (shield, 1f), 0x117 -> 0x1F (roll, 2f), 0x117 -> 0x119 (miss).
    CancelSprintState luc;
    cancelSprintResetAll(luc);
    {
        ChallengeFrame in = makeFrame(60, kCsFtLucario, kCsStLucarioCharge);
        addEv(in, kCsStSpecialN, kCsStLucarioCharge);
        cancelSprintStep(luc, in);
        in = makeFrame(60, kCsFtLucario, kCsStLucarioAscCancel);
        addEv(in, kCsStLucarioCharge, kCsStLucarioAscCancel);
        CHECK(cancelSprintStep(luc, in));
        CHECK_EQ(kCancelKindAsc, luc.out.lastKind);
        CHECK_EQ(1u, luc.out.perfectCount);
        CHECK_EQ(1u, luc.out.streak);

        in = makeFrame(70, kCsFtLucario, kCsStLucarioCharge);
        addEv(in, kCsStSpecialN, kCsStLucarioCharge);
        cancelSprintStep(luc, in);
        in = makeFrame(71, kCsFtLucario, kCsStEscapeF);
        addEv(in, kCsStLucarioCharge, kCsStEscapeF);
        CHECK(cancelSprintStep(luc, in));
        CHECK_EQ(2u, luc.out.totalCancels);
        CHECK_EQ(2u, luc.out.lastFrames);
        CHECK_EQ(2u, luc.out.streak);

        in = makeFrame(80, kCsFtLucario, kCsStLucarioCharge);
        addEv(in, kCsStSpecialN, kCsStLucarioCharge);
        cancelSprintStep(luc, in);
        in = makeFrame(85, kCsFtLucario, kCsStLucarioShoot);
        addEv(in, kCsStLucarioCharge, kCsStLucarioShoot);
        CHECK(cancelSprintStep(luc, in));
        CHECK_EQ(3u, luc.out.attempts);
        CHECK_EQ(2u, luc.out.totalCancels);
        CHECK_EQ(0u, luc.out.streak);
        CHECK_EQ(2u, luc.out.bestStreak);
    }
}

TEST(stage4c, flow_course_dynamic_battlefield_and_fd_completion)
{
    StageGeometry bf = makeBfGeom();
    FlowCourseState st;
    flowCourseResetAll(st);
    flowCourseSetGeometry(st, bf);
    CHECK_EQ(1, st.layoutValid);
    CHECK_EQ(1, st.out.hasPlatformGate);
    CHECK_NEAR(27.2f, st.gates[kFlowGateWaveland].y, 0.01f);

    // Gate 0: Dash-dance turnaround near center.
    ChallengeFrame in = makeFrame(10, kCsFtFox, kFcStDash);
    in.x = 0.0f;
    in.vx = 1.8f;
    in.lr = 1.0f;
    CHECK(!flowCourseStep(st, in));
    in = makeFrame(14, kCsFtFox, kFcStTurnDash);
    in.x = 5.0f;
    in.vx = -1.2f;
    in.lr = -1.0f;
    addEv(in, kFcStDash, kFcStTurnDash);
    CHECK(flowCourseStep(st, in));
    CHECK_EQ(1, st.out.activeGate);
    CHECK_EQ(1, st.out.running);
    CHECK_NEAR(27.2f, st.out.gateY, 0.01f);

    // Gate 1: Platform waveland on right platform (x=35, y=27.2).
    in = makeFrame(26, kCsFtFox, kFcStEscapeAir);
    in.x = 35.0f;
    in.y = 29.0f;
    in.airborne = 1;
    addEv(in, 0x0B, kFcStEscapeAir);
    CHECK(!flowCourseStep(st, in));
    in = makeFrame(28, kCsFtFox, kFcStLandingFallSpecial);
    in.x = 36.0f;
    in.y = 27.2f;
    addEv(in, kFcStEscapeAir, kFcStLandingFallSpecial);
    CHECK(flowCourseStep(st, in));
    CHECK_EQ(2, st.out.activeGate);
    CHECK_EQ(14u, st.out.splitFrames[kFlowGateWaveland]);

    // Gate 2: RAR takeoff moving right (facing left, lr = -1, vx = +1.7).
    in = makeFrame(35, kCsFtFox, kFcStRun);
    in.x = 10.0f;
    in.vx = 2.0f;
    in.lr = 1.0f;
    flowCourseStep(st, in);
    in = makeFrame(36, kCsFtFox, kFcStTurnRun);
    in.x = 12.0f;
    in.vx = 1.8f;
    in.lr = -1.0f;
    addEv(in, kFcStRun, kFcStTurnRun);
    flowCourseStep(st, in);
    in = makeFrame(37, kCsFtFox, kFcStJumpSquat);
    in.x = 13.0f;
    in.vx = 1.7f;
    in.lr = -1.0f;
    addEv(in, kFcStTurnRun, kFcStJumpSquat);
    flowCourseStep(st, in);
    in = makeFrame(40, kCsFtFox, kFcStJump);
    in.x = 14.0f;
    in.y = 2.0f;
    in.vx = 1.7f;
    in.lr = -1.0f;
    in.airborne = 1;
    addEv(in, kFcStJumpSquat, kFcStJump);
    CHECK(flowCourseStep(st, in));
    CHECK_EQ(3, st.out.activeGate);
    CHECK(st.gates[kFlowGateEdgeBurst].maxX >= bf.floor.rightX);

    // Gate 3: Opposite-edge horizontal burst near right edge.
    in = makeFrame(55, kCsFtFox, kFcStDash);
    in.x = 60.0f;
    in.vx = 2.1f;
    CHECK(flowCourseStep(st, in));
    CHECK_EQ(1, st.courseCompletedThisFrame);
    CHECK_EQ(1u, st.out.coursesCompleted);
    CHECK_EQ(42u, st.out.bestCourseFrames);
    CHECK_EQ(1u, st.out.flowStreak);
    CHECK_EQ(0, st.out.activeGate);

    // Final Destination: flat stage fallback (floor wavedash instead of platform).
    StageGeometry fd = makeFdGeom();
    FlowCourseState fdSt;
    flowCourseResetAll(fdSt);
    flowCourseSetGeometry(fdSt, fd);
    CHECK_EQ(1, fdSt.layoutValid);
    CHECK_EQ(0, fdSt.out.hasPlatformGate);
    CHECK_NEAR(0.0f, fdSt.gates[kFlowGateWaveland].y, 0.01f);

    // Hitstun mid-course resets transient progress and breaks streak.
    in = makeFrame(100, kCsFtFox, kFcStTurnDash);
    addEv(in, kFcStDash, kFcStTurnDash);
    CHECK(flowCourseStep(st, in));
    CHECK_EQ(1, st.out.activeGate);
    in = makeFrame(105, kCsFtFox, 0x44);
    in.hitstun = 12;
    flowCourseStep(st, in);
    CHECK_EQ(0, st.out.activeGate);
    CHECK_EQ(0u, st.out.flowStreak);
    CHECK_EQ(1u, st.out.bestFlowStreak);
}

TEST(stage4c, reaction_lab_cues_exact_median_mean_and_ring_wrap)
{
    u16 ring[kReactionRingCap];
    ReactionTechchaseState st;
    reactionTechchaseResetAll(st, ring, kReactionRingCap);

    ChallengeFrame frames[2];
    frames[0] = makeFrame(10, kCsFtFox, 0x00);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    frames[1] = makeFrame(10, kCsFtFox, kRtStPassiveFb);
    frames[1].entryId = 1;
    frames[1].lr = 1.0f;
    addEv(frames[1], kRtStDamageFall, kRtStPassiveFb, kRtMotPassiveStandF);

    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1, st.out.rlCueActive);
    CHECK_EQ(kCueTechRollRight, st.cueKind);

    frames[0] = makeFrame(24, kCsFtFox, kRtStDash);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    addEv(frames[0], 0x00, kRtStDash);
    frames[1] = makeFrame(24, kCsFtFox, 0x00);
    frames[1].entryId = 1;

    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1, st.sampledThisFrame);
    CHECK_EQ(1u, st.out.rlCount);
    CHECK_EQ(14u, st.out.rlMinFrames);
    CHECK_EQ(14u, st.out.rlMaxFrames);
    CHECK_NEAR(14.0f, st.out.rlMeanFrames, 0.001f);
    CHECK_NEAR(14.0f, st.out.rlMedianFrames, 0.001f);
    CHECK_EQ(kCueTechRollRight, st.out.rlLastCueKind);

    // {14, 20, 12, 30} -> min=12, max=30, mean=19.0, even median=17.0.
    reactionLabPushSample(st, 20, kCueTechNeutral);
    reactionLabPushSample(st, 12, kCueLedgeOption);
    reactionLabPushSample(st, 30, kCueGetupAttack);
    CHECK_EQ(4u, st.out.rlCount);
    CHECK_EQ(4u, st.out.rlRingCount);
    CHECK_EQ(12u, st.out.rlMinFrames);
    CHECK_EQ(30u, st.out.rlMaxFrames);
    CHECK_NEAR(19.0f, st.out.rlMeanFrames, 0.001f);
    CHECK_NEAR(17.0f, st.out.rlMedianFrames, 0.001f);

    // 5th sample -> {12, 14, 16, 20, 30}: odd median = 16.0.
    reactionLabPushSample(st, 16, kCueAttackStartup);
    CHECK_EQ(5u, st.out.rlCount);
    CHECK_NEAR(16.0f, st.out.rlMedianFrames, 0.001f);

    // Ring wrap-around past 64 samples.
    for (int i = 0; i < kReactionRingCap; i++)
    {
        reactionLabPushSample(st, 15, kCueTechNeutral);
    }
    CHECK_EQ(static_cast<u32>(5 + kReactionRingCap), st.out.rlCount);
    CHECK_EQ(static_cast<u32>(kReactionRingCap), st.out.rlRingCount);
    CHECK_EQ(15u, st.out.rlMinFrames);
    CHECK_EQ(15u, st.out.rlMaxFrames);
    CHECK_NEAR(15.0f, st.out.rlMeanFrames, 0.001f);
    CHECK_NEAR(15.0f, st.out.rlMedianFrames, 0.001f);
}

TEST(stage4c, universal_techchase_escape_perfect_delayed_and_caught)
{
    u16 ring[kReactionRingCap];
    ReactionTechchaseState st;
    reactionTechchaseResetAll(st, ring, kReactionRingCap);

    ChallengeFrame f;

    // 1. Throw -> Passive_Fb -> 0f frame-perfect Spotdodge escape.
    f = makeFrame(10, kCsFtFox, kRtStCaptureThrown);
    addEv(f, kRtStCaptureWait, kRtStCaptureThrown);
    reactionTechchaseStep(st, &f, 1, 0);
    CHECK_EQ(kTcPhaseVulnerable, st.out.tcPhase);

    f = makeFrame(25, kCsFtFox, kRtStPassiveFb);
    reactionTechchaseStep(st, &f, 1, 0);

    f = makeFrame(30, kCsFtFox, kRtStEscape);
    f.actionable = 1;
    addEv(f, kRtStPassiveFb, kRtStEscape);
    reactionTechchaseStep(st, &f, 1, 0);
    CHECK_EQ(1, st.tcCompletedThisFrame);
    CHECK_EQ(1u, st.out.tcEscapes);
    CHECK_EQ(1u, st.out.tcPerfectEscapes);
    CHECK_EQ(kTcToolSpotdodge, st.out.tcLastTool);
    CHECK_EQ(0u, st.out.tcLastTightness);
    CHECK_EQ(1u, st.out.tcStreak);

    // 2. Capture_Cut -> 3f delayed Jump escape.
    f = makeFrame(50, kCsFtFox, kRtStCaptureCut);
    addEv(f, kRtStCaptureWait, kRtStCaptureCut);
    reactionTechchaseStep(st, &f, 1, 0);

    f = makeFrame(55, kCsFtFox, 0x00);
    f.actionable = 1;
    reactionTechchaseStep(st, &f, 1, 0);
    CHECK_EQ(kTcPhaseActionableWait, st.out.tcPhase);

    f = makeFrame(58, kCsFtFox, kRtStJumpSquat);
    f.actionable = 1;
    addEv(f, 0x00, kRtStJumpSquat);
    reactionTechchaseStep(st, &f, 1, 0);
    CHECK_EQ(2u, st.out.tcEscapes);
    CHECK_EQ(1u, st.out.tcPerfectEscapes);
    CHECK_EQ(kTcToolJump, st.out.tcLastTool);
    CHECK_EQ(3u, st.out.tcLastTightness);
    CHECK_EQ(0u, st.out.tcBestTightness);
    CHECK_EQ(2u, st.out.tcStreak);

    // 3. Knockdown -> actionable -> re-grabbed before escaping -> caughtCount++.
    f = makeFrame(70, kCsFtFox, kRtStDownFirst);
    reactionTechchaseStep(st, &f, 1, 0);
    f = makeFrame(75, kCsFtFox, 0x00);
    f.actionable = 1;
    reactionTechchaseStep(st, &f, 1, 0);
    f = makeFrame(77, kCsFtFox, kRtStCapturePulled);
    addEv(f, 0x00, kRtStCapturePulled);
    reactionTechchaseStep(st, &f, 1, 0);
    CHECK_EQ(1u, st.out.tcCaughtCount);
    CHECK_EQ(0u, st.out.tcStreak);
    CHECK_EQ(2u, st.out.tcBestStreak);
}

TEST(stage4c, recovery_sweetspot_and_floorhug_challenge)
{
    StageGeometry fd = makeFdGeom();
    RecoveryFloorhugState rf;
    recoveryFloorhugResetAll(rf);

    // 1. Clean sweetspot from below right ledge.
    ChallengeFrame ri = makeFrame(10, kCsFtFox, 0x114);
    ri.airborne = 1;
    ri.x = 100.0f;
    ri.y = -20.0f;
    CHECK(!recoverySweetspotStep(rf, fd, ri));
    CHECK_EQ(1, rf.offstageActive);

    ri.frame = 18;
    ri.x = 88.0f;
    ri.y = -2.0f;
    CHECK(!recoverySweetspotStep(rf, fd, ri));

    // Cliff_Catch_Move (0x73) and Cliff_Catch (0x74) precede Ledge.asm's
    // LA-Basic[79]/XLU update on Cliff_Wait (0x75) and must not grade early.
    ri = makeFrame(19, kCsFtFox, kRecStCliffCatch);
    ri.x = 85.0f;
    ri.y = -1.0f;
    addEv(ri, 0x114, kRecStCliffCatch);
    CHECK(!recoverySweetspotStep(rf, fd, ri));

    ri = makeFrame(20, kCsFtFox, kRecStCliffWait);
    ri.ledgeGrabs = 1;
    ri.xluFrames = 31;
    ri.x = 85.0f;
    ri.y = -1.0f;
    addEv(ri, kRecStCliffCatch, kRecStCliffWait);
    CHECK(recoverySweetspotStep(rf, fd, ri));
    CHECK_EQ(kRecGradeSweetspot, rf.out.recLastGrade);
    CHECK_EQ(1u, rf.out.recSweetspots);
    CHECK_EQ(1u, rf.out.recStreak);
    CHECK_EQ(1, rf.out.recLastGrabs);
    CHECK_EQ(31, rf.out.recLastXlu);

    // 2. High overshoot (peaked y=18 near ledge, then landed onstage).
    ri = makeFrame(40, kCsFtFox, 0x114);
    ri.airborne = 1;
    ri.x = -100.0f;
    ri.y = -10.0f;
    recoverySweetspotStep(rf, fd, ri);
    ri.frame = 46;
    ri.x = -84.0f;
    ri.y = 18.0f;
    recoverySweetspotStep(rf, fd, ri);
    ri = makeFrame(52, kCsFtFox, 0x19);
    ri.x = -70.0f;
    ri.y = 0.0f;
    CHECK(recoverySweetspotStep(rf, fd, ri));
    CHECK_EQ(kRecGradeHighOvershoot, rf.out.recLastGrade);
    CHECK_EQ(1u, rf.out.recHighCount);
    CHECK_EQ(0u, rf.out.recStreak);

    // 3. P+ 5-ledge-grab limit exceeded (LA-Basic[79] = 6, xlu = 0).
    ri = makeFrame(70, kCsFtFox, 0x0E);
    ri.airborne = 1;
    ri.x = 95.0f;
    ri.y = -10.0f;
    recoverySweetspotStep(rf, fd, ri);
    ri = makeFrame(75, kCsFtFox, kRecStCliffWait);
    ri.ledgeGrabs = 6;
    ri.xluFrames = 0;
    ri.x = 85.0f;
    ri.y = -1.0f;
    CHECK(recoverySweetspotStep(rf, fd, ri));
    CHECK_EQ(kRecGradeNoInvulnLimit, rf.out.recLastGrade);
    CHECK_EQ(1u, rf.out.recNoInvulnCount);

    // 4. SD (Dead 0xBD).
    ri = makeFrame(90, kCsFtFox, 0x0E);
    ri.airborne = 1;
    ri.x = 120.0f;
    ri.y = -90.0f;
    recoverySweetspotStep(rf, fd, ri);
    ri = makeFrame(96, kCsFtFox, kRecStDead);
    ri.outOfPlay = 1;
    ri.x = 125.0f;
    ri.y = -145.0f;
    CHECK(recoverySweetspotStep(rf, fd, ri));
    CHECK_EQ(kRecGradeSd, rf.out.recLastGrade);
    CHECK_EQ(1u, rf.out.recSdCount);

    // Floorhug / ASDI-Down Challenge composing with FloorhugState.
    FloorhugState fh;
    floorhugReset(fh);
    FloorhugFrameInput fi;
    memset(&fi, 0, sizeof(fi));
    fi.frame = 100;
    fi.status = kFhStatusDamageAir;
    fi.hitlag = 1;
    fi.hitstun = 14;
    fi.hitReceived = 1;
    fi.attackerSlot = 1;
    fi.cStickY = -1.0f;
    fi.landingFrame = 4.0f;
    fi.eventCount = 1;
    fi.events[0].prev = kFhStatusWait;
    fi.events[0].status = kFhStatusDamageAir;
    floorhugStep(fh, fi);
    floorhugChallengeObserve(rf, fh);

    memset(&fi, 0, sizeof(fi));
    fi.frame = 101;
    fi.status = kFhStatusLanding;
    fi.prevHitlag = 1;
    fi.hitstun = 14;
    fi.prevHitstun = 14;
    fi.cStickY = -1.0f;
    fi.landingFrame = 4.0f;
    fi.eventCount = 1;
    fi.events[0].prev = kFhStatusDamageAir;
    fi.events[0].status = kFhStatusLanding;
    floorhugStep(fh, fi);
    CHECK(floorhugChallengeObserve(rf, fh));
    CHECK_EQ(1u, rf.out.fhReps);
    CHECK_EQ(1u, rf.out.fhSuccesses);
    CHECK_EQ(9, rf.lastHitstunSaved);
    CHECK_EQ(9u, rf.totalHitstunSaved);
    CHECK_EQ(1u, rf.out.fhStreak);
    CHECK_EQ(1, rf.lastAsdiFromCStick);
}

TEST(stage4c, snake_infinite_recovery_c4_and_grenade_loops)
{
    StageGeometry fd = makeFdGeom();
    SnakeRecoveryState st;
    snakeRecoveryResetAll(st);

    ChallengeFrame in = makeFrame(10, kSnkFtSnake, kSnkStCypherStart);
    in.airborne = 1;
    in.cypherLockout = 1;
    in.cypherStacks = 1.0f;
    in.damage = 20.0f;
    in.x = 115.0f;
    in.y = -40.0f;
    addEv(in, 0x0E, kSnkStCypherStart);
    CHECK(!snakeRecoveryStep(st, fd, in));
    CHECK_EQ(1, st.out.offstage);
    CHECK_EQ(1u, st.out.totalCyphers);
    CHECK_EQ(1, st.out.cypherStacks);
    CHECK_EQ(1, st.out.cypherLockout);

    // Air C4 drop (0x11D) then Detonate (0x119).
    in = makeFrame(30, kSnkFtSnake, kSnkStC4AirDrop);
    in.airborne = 1;
    in.cypherLockout = 1;
    in.cypherStacks = 1.0f;
    in.damage = 20.0f;
    in.x = 120.0f;
    in.y = -25.0f;
    addEv(in, kSnkStCypherCut, kSnkStC4AirDrop);
    snakeRecoveryStep(st, fd, in);

    in = makeFrame(45, kSnkFtSnake, kSnkStC4Detonate);
    in.airborne = 1;
    in.cypherLockout = 1;
    in.cypherStacks = 1.0f;
    in.damage = 20.0f;
    in.x = 120.0f;
    in.y = -35.0f;
    addEv(in, 0x0E, kSnkStC4Detonate);
    snakeRecoveryStep(st, fd, in);

    // Frame 73: C4 self-hit (+15% dmg, clears LA-Bit[61]).
    in = makeFrame(73, kSnkFtSnake, kSnkStDamageFly);
    in.hitstun = 18;
    in.airborne = 1;
    in.cypherLockout = 0;
    in.cypherStacks = 1.0f;
    in.damage = 35.0f;
    in.x = 122.0f;
    in.y = -60.0f;
    addEv(in, kSnkStC4Detonate, kSnkStDamageFly);
    CHECK(snakeRecoveryStep(st, fd, in));
    CHECK_EQ(1u, st.out.refreshCount);
    CHECK_EQ(1u, st.out.loopChain);
    CHECK_EQ(kSnakeSelfHitC4, st.out.lastSelfHitKind);
    CHECK_NEAR(15.0f, st.out.lastSelfHitDamage, 0.01f);

    // 2nd Cypher climbs from y=-60 to y=-5 -> altitudeRecovered = 55.0, stacks = 2.
    in = makeFrame(95, kSnkFtSnake, kSnkStCypherStart);
    in.airborne = 1;
    in.cypherLockout = 1;
    in.cypherStacks = 2.0f;
    in.damage = 35.0f;
    in.x = 105.0f;
    in.y = -5.0f;
    addEv(in, 0x49, kSnkStCypherStart);
    snakeRecoveryStep(st, fd, in);
    CHECK_EQ(2u, st.out.totalCyphers);
    CHECK_EQ(2, st.out.cypherStacks);
    CHECK_NEAR(55.0f, st.out.altitudeRecovered, 0.01f);

    // Grenade pull + 12% self-hit -> 2nd loop in chain!
    in = makeFrame(110, kSnkFtSnake, kSnkStSpecialN);
    in.airborne = 1;
    in.cypherLockout = 1;
    in.cypherStacks = 2.0f;
    in.damage = 35.0f;
    in.x = 102.0f;
    in.y = -20.0f;
    addEv(in, 0x0E, kSnkStSpecialN);
    snakeRecoveryStep(st, fd, in);

    in = makeFrame(140, kSnkFtSnake, kSnkStDamageFly);
    in.hitstun = 16;
    in.airborne = 1;
    in.cypherLockout = 0;
    in.cypherStacks = 2.0f;
    in.damage = 47.0f;
    in.x = 102.0f;
    in.y = -20.0f;
    addEv(in, 0x0E, kSnkStDamageFly);
    CHECK(snakeRecoveryStep(st, fd, in));
    CHECK_EQ(2u, st.out.refreshCount);
    CHECK_EQ(2u, st.out.loopChain);
    CHECK_EQ(2u, st.out.bestLoopChain);
    CHECK_EQ(kSnakeSelfHitGrenade, st.out.lastSelfHitKind);
    CHECK_NEAR(27.0f, st.out.totalSelfDamage, 0.01f);

    // Opponent-dealt hit must NOT increment self-hit refreshCount.
    in.frame = 155;
    in.damage = 59.0f;
    in.opponentDealtHit = 1;
    CHECK(!snakeRecoveryStep(st, fd, in));
    CHECK_EQ(2u, st.out.refreshCount);

    // Grab ledge -> completes recovery chain, resets transient loopChain.
    in = makeFrame(170, kSnkFtSnake, 0x74);
    in.airborne = 1;
    in.damage = 59.0f;
    in.x = 85.0f;
    in.y = 0.0f;
    addEv(in, kSnkStCypherHang, 0x74);
    snakeRecoveryStep(st, fd, in);
    CHECK_EQ(1u, st.out.completedRecoveries);
    CHECK_EQ(0u, st.out.loopChain);
    CHECK_EQ(2u, st.out.bestLoopChain);
    CHECK_EQ(6, snkClampStacks(7.0f));
    CHECK_EQ(0, snkClampStacks(-1.0f));
}

TEST(stage4c, debug_block_layout_and_multi_slot_independence)
{
    CHECK_EQ(0x544D3458u, static_cast<u32>(kChallengeDebugMagic));
    CHECK_EQ(200u, static_cast<u32>(sizeof(ChallengeDebugSlot)));
    CHECK_EQ(816u, static_cast<u32>(sizeof(ChallengeDebugBlock)));
    CHECK_EQ(0u, static_cast<u32>(offsetof(ChallengeDebugSlot, used)));
    CHECK_EQ(8u, static_cast<u32>(offsetof(ChallengeDebugSlot, cs)));
    CHECK_EQ(36u, static_cast<u32>(offsetof(ChallengeDebugSlot, fc)));
    CHECK_EQ(72u, static_cast<u32>(offsetof(ChallengeDebugSlot, rt)));
    CHECK_EQ(120u, static_cast<u32>(offsetof(ChallengeDebugSlot, rf)));
    CHECK_EQ(168u, static_cast<u32>(offsetof(ChallengeDebugSlot, snk)));

    // Exercise 4 independent fighter slots concurrently (no 2-fighter assumption).
    CancelSprintState slots[4];
    for (int i = 0; i < 4; i++)
    {
        cancelSprintResetAll(slots[i]);
    }
    const int kinds[4] = {kCsFtFox, kCsFtFalco, kCsFtLucario, kCsFtWolf};
    for (int i = 0; i < 4; i++)
    {
        ChallengeFrame in = makeFrame(10, kinds[i], (i == 2) ? kCsStLucarioCharge : kCsStSpecialLw);
        addEv(in, 0x00, in.status);
        cancelSprintStep(slots[i], in);
    }
    {
        ChallengeFrame in = makeFrame(10, kCsFtLucario, kCsStLucarioAscCancel);
        addEv(in, kCsStLucarioCharge, kCsStLucarioAscCancel);
        CHECK(cancelSprintStep(slots[2], in));
        in = makeFrame(13, kCsFtWolf, kCsStJumpSquat, 1u << kCsGroupJumpGround);
        addEv(in, kCsStSpecialLw, kCsStSpacieShineLoop);
        addEv(in, kCsStSpacieShineLoop, kCsStJumpSquat);
        CHECK(cancelSprintStep(slots[3], in));
    }
    CHECK_EQ(0u, slots[0].out.totalCancels);
    CHECK_EQ(0u, slots[1].out.totalCancels);
    CHECK_EQ(1u, slots[2].out.totalCancels);
    CHECK_EQ(1u, slots[3].out.totalCancels);
}

TEST(stage4c, regressions_cancel_sprint_falco_upb_and_reflect_hitlag)
{
    const u32 kJumpGrp = 1u << kCsGroupJumpGround;

    // 1a. Falco Fire Bird End shares action 0x118 with Shine Reflect Hit:
    // 0x114 -> 0x117 -> 0x118 -> 0x10 must NEVER start or miss a Shine attempt.
    CancelSprintState falco;
    cancelSprintResetAll(falco);
    {
        ChallengeFrame in = makeFrame(10, kCsFtFalco, 0x114);
        addEv(in, 0x0E, 0x114);
        CHECK(!cancelSprintStep(falco, in));
        in = makeFrame(25, kCsFtFalco, 0x117);
        addEv(in, 0x114, 0x117);
        CHECK(!cancelSprintStep(falco, in));
        in = makeFrame(40, kCsFtFalco, kCsStFalcoShineHit);
        addEv(in, 0x117, kCsStFalcoShineHit);
        CHECK(!cancelSprintStep(falco, in));
        CHECK_EQ(0, falco.out.active);
        in = makeFrame(46, kCsFtFalco, 0x10);
        addEv(in, kCsStFalcoShineHit, 0x10);
        CHECK(!cancelSprintStep(falco, in));
        CHECK_EQ(0u, falco.out.attempts);
        CHECK_EQ(0u, falco.out.totalCancels);
    }

    // 1b. Fox Shine reflect hit-lag (0x11B) does not enable jump cancel;
    // windowOpenFrame must wait until 0x11A opens (not premature on 0x11B).
    CancelSprintState fox;
    cancelSprintResetAll(fox);
    {
        ChallengeFrame in = makeFrame(10, kCsFtFox, kCsStSpecialLw, 0);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(fox, in);
        in = makeFrame(12, kCsFtFox, kCsStSpacieShineHit, 0);
        addEv(in, kCsStSpecialLw, kCsStSpacieShineHit);
        cancelSprintStep(fox, in);
        CHECK_EQ(1, fox.out.active);
        CHECK_EQ(0, fox.windowOpen);
        in = makeFrame(16, kCsFtFox, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStSpacieShineHit, kCsStSpacieShineLoop);
        addEv(in, kCsStSpacieShineLoop, kCsStJumpSquat);
        CHECK(cancelSprintStep(fox, in));
        CHECK_EQ(1u, fox.out.totalCancels);
        CHECK_EQ(1u, fox.out.lastFrames);
        CHECK_EQ(1u, fox.out.perfectCount);
    }

    // 1c. Lucas Magnet hit-lag (0x120) must not open window before 0x121.
    CancelSprintState lucas;
    cancelSprintResetAll(lucas);
    {
        ChallengeFrame in = makeFrame(20, kCsFtLucas, kCsStSpecialLw, 0);
        addEv(in, 0x00, kCsStSpecialLw);
        cancelSprintStep(lucas, in);
        in = makeFrame(24, kCsFtLucas, kCsStLucasMagnetHold, 0);
        addEv(in, kCsStSpecialLw, kCsStLucasMagnetHold);
        cancelSprintStep(lucas, in);
        in = makeFrame(26, kCsFtLucas, kCsStLucasMagnetHit, 0);
        addEv(in, kCsStLucasMagnetHold, kCsStLucasMagnetHit);
        cancelSprintStep(lucas, in);
        CHECK_EQ(0, lucas.windowOpen);
        in = makeFrame(30, kCsFtLucas, kCsStLucasMagnetEnd, 0);
        addEv(in, kCsStLucasMagnetHit, kCsStLucasMagnetEnd);
        cancelSprintStep(lucas, in);
        in = makeFrame(31, kCsFtLucas, kCsStJumpSquat, kJumpGrp);
        addEv(in, kCsStLucasMagnetEnd, kCsStJumpSquat);
        CHECK(cancelSprintStep(lucas, in));
        CHECK_EQ(1u, lucas.out.lastFrames);
        CHECK_EQ(1u, lucas.out.perfectCount);
    }
}

TEST(stage4c, regressions_reaction_lab_and_techchase_accuracy)
{
    u16 ring[kReactionRingCap];
    ReactionTechchaseState st;
    reactionTechchaseResetAll(st, ring, kReactionRingCap);

    // 2a & 2b. Stale StatusEvent::motionKind = 0x29 (Damage_Fall) while live
    // ov.motionKind = 0xC0 (Passive_Stand_B), facing right with positive launch
    // vx -> must use 0xC0 (rolling LEFT) AND same-frame dash on P1 must NOT
    // push a bogus 1f sample.
    ChallengeFrame frames[2];
    frames[0] = makeFrame(10, kCsFtFox, kRtStDash);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    frames[0].triggerButtons = 1u;
    addEv(frames[0], 0x00, kRtStDash);

    frames[1] = makeFrame(10, kCsFtFox, kRtStPassiveFb, 0, kRtMotPassiveStandB);
    frames[1].entryId = 1;
    frames[1].lr = 1.0f;
    frames[1].vx = 1.8f; // opposite roll direction
    addEv(frames[1], kRtStDamageFall, kRtStPassiveFb, 0x29); // stale Damage_Fall subaction

    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1, st.out.rlCueActive);
    CHECK_EQ(kCueTechRollLeft, st.cueKind);
    CHECK_EQ(0u, st.out.rlCount); // no same-frame 1f false sample

    // Real response 12 frames later produces an exact 12f sample.
    frames[0] = makeFrame(22, kCsFtFox, kRtStJumpSquat);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    addEv(frames[0], 0x00, kRtStJumpSquat);
    frames[1] = makeFrame(22, kCsFtFox, 0x00);
    frames[1].entryId = 1;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1u, st.out.rlCount);
    CHECK_EQ(12u, st.out.rlLastFrames);
    CHECK_EQ(kCueTechRollLeft, st.out.rlLastCueKind);

    // 3. Techchase: Damage_Fall actionable -> lands into Passive_Fb (no hit/grab)
    // must return to kTcPhaseVulnerable WITHOUT incrementing tcCaughtCount or
    // resetting tcStreak; subsequent escape after tech increments streak.
    st.out.tcStreak = 2;
    st.out.tcBestStreak = 2;
    frames[0] = makeFrame(50, kCsFtFox, kRtStDamageFall);
    frames[0].entryId = 0;
    frames[0].hitstun = 4;
    addEv(frames[0], 0x45, kRtStDamageFall);
    frames[1] = makeFrame(50, kCsFtFox, 0x00);
    frames[1].entryId = 1;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(kTcPhaseVulnerable, st.out.tcPhase);

    // Hitstun ends in Damage_Fall -> actionable wait.
    frames[0] = makeFrame(54, kCsFtFox, kRtStDamageFall);
    frames[0].entryId = 0;
    frames[0].hitstun = 0;
    frames[0].actionable = 1;
    frames[1].frame = 54;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(kTcPhaseActionableWait, st.out.tcPhase);

    // Defender lands and tech-rolls (0x61) without being hit/grabbed -> NOT caught!
    frames[0] = makeFrame(56, kCsFtFox, kRtStPassiveFb);
    frames[0].entryId = 0;
    frames[0].hitstun = 0;
    frames[0].actionable = 0;
    addEv(frames[0], kRtStDamageFall, kRtStPassiveFb);
    frames[1].frame = 56;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(kTcPhaseVulnerable, st.out.tcPhase);
    CHECK_EQ(0u, st.out.tcCaughtCount);
    CHECK_EQ(2u, st.out.tcStreak);

    // Tech roll ends -> frame-0 spotdodge escape -> streak bumps to 3!
    frames[0] = makeFrame(70, kCsFtFox, kRtStEscape);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    addEv(frames[0], kRtStPassiveFb, kRtStEscape);
    frames[1].frame = 70;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1u, st.out.tcEscapes);
    CHECK_EQ(3u, st.out.tcStreak);
    CHECK_EQ(0u, st.out.tcCaughtCount);

    // True re-grab (0x3D) during ActionableWait DOES count as caught and breaks streak.
    frames[0] = makeFrame(90, kCsFtFox, kRtStPassive);
    frames[0].entryId = 0;
    addEv(frames[0], kRtStDamageFall, kRtStPassive);
    frames[1].frame = 90;
    reactionTechchaseStep(st, frames, 2, 0);
    frames[0] = makeFrame(100, kCsFtFox, 0x00);
    frames[0].entryId = 0;
    frames[0].actionable = 1;
    frames[1].frame = 100;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(kTcPhaseActionableWait, st.out.tcPhase);
    frames[0] = makeFrame(102, kCsFtFox, kRtStCapturePulled);
    frames[0].entryId = 0;
    addEv(frames[0], 0x00, kRtStCapturePulled);
    frames[1].frame = 102;
    reactionTechchaseStep(st, frames, 2, 0);
    CHECK_EQ(1u, st.out.tcCaughtCount);
    CHECK_EQ(0u, st.out.tcStreak);
    CHECK_EQ(kTcPhaseVulnerable, st.out.tcPhase);
}

TEST(stage4c, regressions_recovery_snake_and_flow_course)
{
    StageGeometry fd = makeFdGeom();

    // 4. Recovery Sweetspot: outward launch high over lip (hitstun > 0) and
    // jumping out past 18 units must NOT poison peak height for low sweetspot.
    RecoveryFloorhugState rf;
    recoveryFloorhugResetAll(rf);
    {
        ChallengeFrame in = makeFrame(10, kCsFtFox, 0x45);
        in.airborne = 1;
        in.hitstun = 20;
        in.x = 90.0f;
        in.y = 26.0f; // high above lip during outward launch
        recoverySweetspotStep(rf, fd, in);
        CHECK_EQ(1, rf.offstageActive);
        CHECK_EQ(0, rf.sawPeakNearLip);

        // Actionable high jump going outward, then crossing outside 18-unit zone.
        in = makeFrame(30, kCsFtFox, 0x0C);
        in.airborne = 1;
        in.hitstun = 0;
        in.x = 92.0f;
        in.y = 20.0f;
        recoverySweetspotStep(rf, fd, in);
        CHECK_EQ(1, rf.sawPeakNearLip);

        in = makeFrame(36, kCsFtFox, 0x0E);
        in.airborne = 1;
        in.x = 112.0f; // |dx| = 27 > 18 -> resets peak
        in.y = 5.0f;
        recoverySweetspotStep(rf, fd, in);
        CHECK_EQ(0, rf.sawPeakNearLip);

        // Clean low approach into lip and Cliff_Wait (0x75) -> Sweetspot!
        in = makeFrame(46, kCsFtFox, 0x114);
        in.airborne = 1;
        in.x = 89.0f;
        in.y = -3.0f;
        recoverySweetspotStep(rf, fd, in);
        in = makeFrame(50, kCsFtFox, kRecStCliffWait);
        in.ledgeGrabs = 1;
        in.xluFrames = 31;
        in.x = 85.0f;
        in.y = -1.0f;
        addEv(in, kRecStCliffCatch, kRecStCliffWait);
        CHECK(recoverySweetspotStep(rf, fd, in));
        CHECK_EQ(kRecGradeSweetspot, rf.out.recLastGrade);
        CHECK_EQ(1u, rf.out.recSweetspots);
    }

    // 5. Snake Infinite Recovery: non-flinching Fox laser (+3%) and heavy-armor
    // absorbed hit (+8%) with cypherLockout == 1 must NOT count as refresh!
    SnakeRecoveryState snk;
    snakeRecoveryResetAll(snk);
    {
        ChallengeFrame in = makeFrame(10, kSnkFtSnake, kSnkStCypherHang);
        in.airborne = 1;
        in.cypherLockout = 1;
        in.cypherStacks = 1.0f;
        in.damage = 20.0f;
        in.x = 110.0f;
        in.y = -20.0f;
        addEv(in, kSnkStCypherStart, kSnkStCypherHang);
        snakeRecoveryStep(snk, fd, in);

        // Fox laser (non-flinching, kByElimination -> opponentDealtHit = 0, hitstun = 0).
        in = makeFrame(18, kSnkFtSnake, kSnkStCypherHang);
        in.airborne = 1;
        in.cypherLockout = 1;
        in.cypherStacks = 1.0f;
        in.damage = 23.0f;
        in.x = 110.0f;
        in.y = -15.0f;
        CHECK(!snakeRecoveryStep(snk, fd, in));
        CHECK_EQ(0u, snk.out.refreshCount);

        // <10% hit absorbed by Cypher Hang heavy armor (still locked, no hitstun).
        in = makeFrame(24, kSnkFtSnake, kSnkStCypherHang);
        in.airborne = 1;
        in.cypherLockout = 1;
        in.cypherStacks = 1.0f;
        in.damage = 31.0f;
        in.x = 110.0f;
        in.y = -10.0f;
        CHECK(!snakeRecoveryStep(snk, fd, in));
        CHECK_EQ(0u, snk.out.refreshCount);

        // Real C4 self-hit (+15%, Damage_Fly + hitstun > 0, LA-Bit[61] cleared) -> counts!
        in = makeFrame(35, kSnkFtSnake, kSnkStDamageFly);
        in.airborne = 1;
        in.hitstun = 18;
        in.cypherLockout = 0;
        in.cypherStacks = 1.0f;
        in.damage = 46.0f;
        in.x = 112.0f;
        in.y = -25.0f;
        addEv(in, kSnkStC4Detonate, kSnkStDamageFly);
        CHECK(snakeRecoveryStep(snk, fd, in));
        CHECK_EQ(1u, snk.out.refreshCount);
        CHECK_EQ(kSnakeSelfHitC4, snk.out.lastSelfHitKind);
    }

    // 6a & 6b. Flow Course: missed airdodge followed by Up-B LandingFallSpecial
    // must NOT false-positive Gate 1; moving platforms update Gate 1 live; and
    // Gate 3 opposite-edge target is preserved while running on Gate 3.
    StageGeometry sv = makeFdGeom();
    sv.platformCount = 1;
    sv.platforms[0].leftX = -20.0f;
    sv.platforms[0].rightX = 20.0f;
    sv.platforms[0].y = 25.0f;
    sv.platforms[0].valid = true;

    FlowCourseState fc;
    flowCourseResetAll(fc);
    flowCourseSetGeometry(fc, sv);
    CHECK_NEAR(0.0f, fc.gates[kFlowGateWaveland].minX + 23.0f, 0.01f);

    // Move Smashville platform horizontally to [30, 70] -> Gate 1 updates live!
    sv.platforms[0].leftX = 30.0f;
    sv.platforms[0].rightX = 70.0f;
    sv.platforms[0].y = 28.0f;
    flowCourseSetGeometry(fc, sv);
    CHECK_NEAR(27.0f, fc.gates[kFlowGateWaveland].minX, 0.01f);
    CHECK_NEAR(73.0f, fc.gates[kFlowGateWaveland].maxX, 0.01f);
    CHECK_NEAR(28.0f, fc.gates[kFlowGateWaveland].y, 0.01f);

    // Advance Gate 0 -> Gate 1.
    ChallengeFrame in = makeFrame(10, kCsFtFox, kFcStTurnDash);
    in.x = 0.0f;
    addEv(in, kFcStDash, kFcStTurnDash);
    CHECK(flowCourseStep(fc, in));
    CHECK_EQ(1, fc.out.activeGate);
    CHECK_NEAR(50.0f, fc.out.gateX, 0.01f);
    CHECK_NEAR(28.0f, fc.out.gateY, 0.01f);

    // Missed mid-air airdodge -> Fall -> Up-B -> LandingFallSpecial on platform:
    // sawEscapeAir must have been cleared, so Gate 1 must NOT advance.
    in = makeFrame(20, kCsFtFox, kFcStEscapeAir);
    in.airborne = 1;
    in.x = 50.0f;
    in.y = 45.0f;
    addEv(in, 0x0B, kFcStEscapeAir);
    flowCourseStep(fc, in);
    in = makeFrame(35, kCsFtFox, 0x114);
    in.airborne = 1;
    in.x = 50.0f;
    in.y = 32.0f;
    addEv(in, 0x0E, 0x114);
    flowCourseStep(fc, in);
    in = makeFrame(42, kCsFtFox, kFcStLandingFallSpecial);
    in.x = 50.0f;
    in.y = 28.0f;
    addEv(in, 0x114, kFcStLandingFallSpecial);
    CHECK(!flowCourseStep(fc, in));
    CHECK_EQ(1, fc.out.activeGate);

    // Preserve Gate 3 opposite-edge bounds when platform visibility toggles mid-Gate 3.
    fc.out.activeGate = kFlowGateEdgeBurst;
    fc.out.running = 1;
    fc.gates[kFlowGateEdgeBurst].minX = sv.floor.rightX - fc.edgeWidth;
    fc.gates[kFlowGateEdgeBurst].maxX = sv.floor.rightX;
    sv.platformCount = 0; // e.g., Fountain of Dreams platform disappears
    flowCourseSetGeometry(fc, sv);
    CHECK_NEAR(sv.floor.rightX, fc.gates[kFlowGateEdgeBurst].maxX, 0.01f);
    CHECK_NEAR(sv.floor.rightX - fc.edgeWidth, fc.gates[kFlowGateEdgeBurst].minX, 0.01f);
}

TEST(stage4c, recovery_sweetspot_3d_gx_guide_geometry_and_clamping)
{
    // Final Destination ledges (-85, 0) and (+85, 0) with custom cliff-hang box.
    StageGeometry fd = makeFdGeom();
    RecoveryGuideGeom g;
    CHECK(computeRecoveryGuideGeom(fd, 0.0f, 8.0f, 14.0f, 12.0f, g));
    CHECK_EQ(1, g.valid);

    // Left ledge (dir = +1 -> outward = -1): offstage pocket extends left of lip (-85 - 14 = -99).
    CHECK_NEAR(-85.0f, g.sides[0].lipX, 0.01f);
    CHECK_NEAR(0.0f, g.sides[0].lipY, 0.01f);
    CHECK_NEAR(-1.0f, g.sides[0].outward, 0.01f);
    CHECK_NEAR(-99.0f, g.sides[0].grabMinX, 0.01f);
    CHECK_NEAR(-85.0f, g.sides[0].grabMaxX, 0.01f);
    CHECK_NEAR(-20.0f, g.sides[0].grabMinY, 0.01f);
    CHECK_NEAR(-8.0f, g.sides[0].grabMaxY, 0.01f);
    CHECK_NEAR(kRecSweetspotMaxPeakAboveLip, g.sides[0].sweetCeilY, 0.01f);
    CHECK_NEAR(-85.0f - kRecNearLedgeHorizWindow, g.sides[0].sweetMinX, 0.01f);
    CHECK_NEAR(-85.0f, g.sides[0].sweetMaxX, 0.01f);

    // Right ledge (dir = -1 -> outward = +1): offstage pocket extends right of lip (+85 + 14 = +99).
    CHECK_NEAR(85.0f, g.sides[1].lipX, 0.01f);
    CHECK_NEAR(1.0f, g.sides[1].outward, 0.01f);
    CHECK_NEAR(85.0f, g.sides[1].grabMinX, 0.01f);
    CHECK_NEAR(99.0f, g.sides[1].grabMaxX, 0.01f);
    CHECK_NEAR(85.0f, g.sides[1].sweetMinX, 0.01f);
    CHECK_NEAR(85.0f + kRecNearLedgeHorizWindow, g.sides[1].sweetMaxX, 0.01f);

    // Zero / out-of-range cliff-hang dimensions clamp cleanly to safe defaults (w=10, offY=9, h=10).
    RecoveryGuideGeom gClamp;
    CHECK(computeRecoveryGuideGeom(fd, 0.0f, 0.0f, 0.0f, 999.0f, gClamp));
    CHECK_NEAR(10.0f, gClamp.sides[1].grabMaxX - gClamp.sides[1].grabMinX, 0.01f);
    CHECK_NEAR(-19.0f, gClamp.sides[1].grabMinY, 0.01f);
    CHECK_NEAR(-9.0f, gClamp.sides[1].grabMaxY, 0.01f);

    // Stage without ledges returns false / invalid.
    StageGeometry noLedge = fd;
    noLedge.hasLedges = false;
    RecoveryGuideGeom gInvalid;
    CHECK(!computeRecoveryGuideGeom(noLedge, 0.0f, 8.0f, 14.0f, 12.0f, gInvalid));
    CHECK_EQ(0, gInvalid.valid);
}

TEST(stage4c, floorhug_meta_knight_sticky_taunt_reset_guard)
{
    CHECK(floorhugIsTauntStatus(kFhStAppeal));
    CHECK(floorhugIsTauntStatus(kFhStSmashAppeal));
    CHECK(!floorhugIsTauntStatus(0x00));
    CHECK(!floorhugIsTauntStatus(0x43));

    // 1. Non-Floorhug, non-taunt savestate loads leave Meta Knight (0x16) and
    //    all other fighters completely untouched (neither taunt interrupt nor
    //    visibility/hurtbox module wipe).
    CHECK(!floorhugNeedsTauntInterrupt(kFhFtMetaKnight, 0x00, kFhTauntGuardFrames, false, false));
    CHECK(!floorhugNeedsTauntInterrupt(kFhFtMetaKnight, kFhStAppeal, kFhTauntGuardFrames, false, false));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(kFhFtMetaKnight, true, false, false));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(kCsFtFox, true, false, false));

    // 2. Non-Floorhug savestate loads that interrupted an active Meta Knight
    //    AppealHi (wasInTaunt == true) DO repair Meta Knight's taunt & modules.
    CHECK(floorhugNeedsTauntInterrupt(kFhFtMetaKnight, kFhStAppeal, kFhTauntGuardFrames, false, true));
    CHECK(floorhugNeedsTauntInterrupt(kFhFtMetaKnight, kFhStSmashAppeal, 1, false, true));
    CHECK(floorhugNeedsMetaKnightModuleRestore(kFhFtMetaKnight, true, false, true));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(kFhFtMetaKnight, false, false, true));

    // 3. Non-Floorhug savestate loads of another fighter (e.g. Luigi 0x09 or
    //    Snake 0x2A) in taunt do NOT cancel their taunt or restore modules.
    CHECK(!floorhugNeedsTauntInterrupt(0x09, kFhStAppeal, kFhTauntGuardFrames, false, true));
    CHECK(!floorhugNeedsTauntInterrupt(kSnkFtSnake, kFhStAppeal, kFhTauntGuardFrames, false, true));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(0x09, true, false, true));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(kSnkFtSnake, true, false, true));

    // 4. Active Floorhug resets (floorhugActive == true) protect Meta Knight
    //    and other fighters during the 6-frame guard window, and stop at 0.
    CHECK(floorhugNeedsTauntInterrupt(kFhFtMetaKnight, kFhStAppeal, kFhTauntGuardFrames, true, false));
    CHECK(floorhugNeedsTauntInterrupt(kCsFtFox, kFhStAppeal, 3, true, false));
    CHECK(floorhugNeedsMetaKnightModuleRestore(kFhFtMetaKnight, true, true, false));
    CHECK(!floorhugNeedsMetaKnightModuleRestore(kCsFtFox, true, true, false));
    CHECK(!floorhugNeedsTauntInterrupt(kFhFtMetaKnight, 0x00, kFhTauntGuardFrames, true, true));
    CHECK(!floorhugNeedsTauntInterrupt(kFhFtMetaKnight, kFhStAppeal, 0, true, true));

    // 5. Taunt mask bit keys by HUD playerNo (0..15) + partner bit (distinct non-zero bits).
    CHECK_EQ(1u << 0, floorhugTauntMaskBit(0, false));
    CHECK_EQ(1u << 1, floorhugTauntMaskBit(0, true));
    CHECK_EQ(1u << 2, floorhugTauntMaskBit(1, false));
    CHECK_EQ(1u << 7, floorhugTauntMaskBit(3, true));
    CHECK_EQ(0u, floorhugTauntMaskBit(-1, false));
    CHECK_EQ(0u, floorhugTauntMaskBit(16, false));
}
