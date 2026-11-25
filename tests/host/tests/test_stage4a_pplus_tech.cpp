// Comprehensive PC host unit tests for the five pure P+-exclusive tech and
// Floorhug / ASDI-Down detection backends:
//   1. drill_rar_core.h
//   2. drill_dacus_core.h
//   3. drill_breverse_core.h
//   4. drill_footstool_agt_core.h
//   5. drill_floorhug_core.h
//   + PPlusTechDebugBlock / PPlusTechDebugSlot layout & multi-slot independence.
#include <string.h>

#include "extra/drill_pplus_tech_engines.h"
#include "host/test.h"

using namespace tm::extra;

namespace {
    RarFrameInput makeRarFrame(s32 f, s16 st, float lr, float vx, u8 air = 0, s16 mot = -1)
    {
        RarFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.status = st;
        in.motionKind = mot;
        in.airborne = air;
        in.lr = lr;
        in.vx = vx;
        return in;
    }

    void addRarEv(RarFrameInput& in, s16 prev, s16 st)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.eventCount++;
    }

    DacusFrameInput makeDacusFrame(s32 f, s16 st, float x, float vx, s16 hl = 0, s16 prevHl = 0)
    {
        DacusFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.status = st;
        in.x = x;
        in.vx = vx;
        in.hitlag = hl;
        in.prevHitlag = prevHl;
        return in;
    }

    void addDacusEv(DacusFrameInput& in, s16 prev, s16 st)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.eventCount++;
    }

    BReverseFrameInput makeBRevFrame(s32 f, s16 st, s32 la55, float lr, float vx, float stickX = 0.0f, u8 air = 1)
    {
        BReverseFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.status = st;
        in.laBasic55 = la55;
        in.lr = lr;
        in.vx = vx;
        in.stickX = stickX;
        in.airborne = air;
        return in;
    }

    void addBRevEv(BReverseFrameInput& in, s16 prev, s16 st)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.eventCount++;
    }

    FootstoolAgtFrameInput makeFaFrame(s32 f, s8 selfSlot, s16 st, u8 air = 1)
    {
        FootstoolAgtFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.selfSlot = selfSlot;
        in.status = st;
        in.airborne = air;
        in.hitDealtTarget = -1;
        return in;
    }

    void addFaEv(FootstoolAgtFrameInput& in, s16 prev, s16 st, u32 flags = 0)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.events[in.eventCount].flags = flags;
        in.eventCount++;
    }

    FloorhugFrameInput makeFhFrame(s32 f, s16 st, s16 hl = 0, s16 prevHl = 0, s16 hs = 0, s16 prevHs = 0)
    {
        FloorhugFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = f;
        in.status = st;
        in.hitlag = hl;
        in.prevHitlag = prevHl;
        in.hitstun = hs;
        in.prevHitstun = prevHs;
        in.hitDealtTarget = -1;
        in.attackerSlot = -1;
        in.lr = 1.0f;
        in.landingFrame = 3.0f; // Fox / Mario IC-Basic[3074] = 3.0 -> 4f landing lag
        return in;
    }

    void addFhEv(FloorhugFrameInput& in, s16 prev, s16 st)
    {
        in.events[in.eventCount].prev = prev;
        in.events[in.eventCount].status = st;
        in.eventCount++;
    }
}

// ============================================================================
// Engine 1: RAR (Reverse Aerial Rush)
// ============================================================================

TEST(stage4a_pplus_tech, rar_frame_perfect_bair_and_momentum_retention)
{
    RarState st;
    rarReset(st);

    // Frame 10: Run right at vx = +2.0, lr = +1.
    CHECK(!rarStep(st, makeRarFrame(10, kRarStatusRun, 1.0f, 2.0f)));

    // Frame 11: Run (0x04) -> Turn_Run (0x08).
    RarFrameInput f11 = makeRarFrame(11, kRarStatusTurnRun, -1.0f, 1.8f);
    addRarEv(f11, kRarStatusRun, kRarStatusTurnRun);
    CHECK(!rarStep(st, f11));

    // Frame 12: Turn_Run (0x08) -> Jump_Squat (0x0A) after 1 frame (frame-perfect).
    RarFrameInput f12 = makeRarFrame(12, kRarStatusJumpSquat, -1.0f, 1.6f);
    addRarEv(f12, kRarStatusTurnRun, kRarStatusJumpSquat);
    CHECK(!rarStep(st, f12));

    // Frames 13-14: remaining Jump_Squat frames.
    CHECK(!rarStep(st, makeRarFrame(13, kRarStatusJumpSquat, -1.0f, 1.5f)));
    CHECK(!rarStep(st, makeRarFrame(14, kRarStatusJumpSquat, -1.0f, 1.5f)));

    // Frame 15: Jump_Squat (0x0A) -> Jump (0x0B) facing left (-1) with vx = +1.6 (backward!).
    RarFrameInput f15 = makeRarFrame(15, kRarStatusJump, -1.0f, 1.6f, 1);
    addRarEv(f15, kRarStatusJumpSquat, kRarStatusJump);
    CHECK(!rarStep(st, f15));

    // Frame 16: Jump (0x0B) -> Attack_Air (0x33) with motionKind == Attack_Air_B (0x64).
    RarFrameInput f16 = makeRarFrame(16, kRarStatusAttackAir, -1.0f, 1.55f, 1, kRarMotionAttackAirB);
    addRarEv(f16, kRarStatusJump, kRarStatusAttackAir);
    CHECK(rarStep(st, f16));

    CHECK_EQ(static_cast<int>(kRarSuccessBair), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.turnRunFrames));
    CHECK_EQ(2, static_cast<int>(st.last.bairDelay));
    CHECK_EQ(1, static_cast<int>(st.last.backwardMomentum));
    CHECK_NEAR(0.80f, st.last.momentumRetained, 0.001);
    CHECK_EQ(1u, st.attempts);
    CHECK_EQ(1u, st.successBairCount);
}

TEST(stage4a_pplus_tech, rar_jump_only_wrong_aerial_no_flip_and_walk_turn)
{
    RarState st;
    rarReset(st);

    // 1. kRarSuccessJumpOnly: 3 frames in Turn_Run, reversed jump, lands without aerial.
    rarStep(st, makeRarFrame(1, kRarStatusRun, 1.0f, 2.0f));
    RarFrameInput tr = makeRarFrame(2, kRarStatusTurnRun, -1.0f, 1.7f);
    addRarEv(tr, kRarStatusRun, kRarStatusTurnRun);
    rarStep(st, tr);
    rarStep(st, makeRarFrame(3, kRarStatusTurnRun, -1.0f, 1.5f));
    rarStep(st, makeRarFrame(4, kRarStatusTurnRun, -1.0f, 1.3f));
    RarFrameInput js = makeRarFrame(5, kRarStatusJumpSquat, -1.0f, 1.2f);
    addRarEv(js, kRarStatusTurnRun, kRarStatusJumpSquat);
    rarStep(st, js);
    RarFrameInput jp = makeRarFrame(8, kRarStatusJump, -1.0f, 1.2f, 1);
    addRarEv(jp, kRarStatusJumpSquat, kRarStatusJump);
    rarStep(st, jp);
    RarFrameInput land = makeRarFrame(20, 0x16, -1.0f, 0.0f, 0);
    addRarEv(land, kRarStatusFall, 0x16);
    CHECK(rarStep(st, land));
    CHECK_EQ(static_cast<int>(kRarSuccessJumpOnly), static_cast<int>(st.last.outcome));
    CHECK_EQ(3, static_cast<int>(st.last.turnRunFrames));
    CHECK_NEAR(0.60f, st.last.momentumRetained, 0.001);
    CHECK_EQ(1u, st.successJumpOnlyCount);

    // 2. kRarFailWrongAerial: reversed jump followed by Fair (motionKind 0x63 != 0x64).
    rarStep(st, makeRarFrame(30, kRarStatusRun, -1.0f, -2.0f));
    RarFrameInput tr2 = makeRarFrame(31, kRarStatusTurnRun, 1.0f, -1.8f);
    addRarEv(tr2, kRarStatusRun, kRarStatusTurnRun);
    rarStep(st, tr2);
    RarFrameInput js2 = makeRarFrame(32, kRarStatusJumpSquat, 1.0f, -1.7f);
    addRarEv(js2, kRarStatusTurnRun, kRarStatusJumpSquat);
    rarStep(st, js2);
    RarFrameInput jp2 = makeRarFrame(35, kRarStatusJump, 1.0f, -1.5f, 1);
    addRarEv(jp2, kRarStatusJumpSquat, kRarStatusJump);
    rarStep(st, jp2);
    RarFrameInput fair = makeRarFrame(35, kRarStatusAttackAir, 1.0f, -1.5f, 1, 0x63);
    addRarEv(fair, kRarStatusJump, kRarStatusAttackAir);
    // Test frame-1 aerial on frame 36 (delay 2) vs same-frame.
    fair.frame = 36;
    CHECK(rarStep(st, fair));
    CHECK_EQ(static_cast<int>(kRarFailWrongAerial), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.backwardMomentum));
    CHECK_NEAR(0.75f, st.last.momentumRetained, 0.001);

    // 3. kRarFailNoFlip: jumped straight from Run still facing run direction.
    rarStep(st, makeRarFrame(50, kRarStatusRun, 1.0f, 2.0f));
    RarFrameInput js3 = makeRarFrame(51, kRarStatusJumpSquat, 1.0f, 1.9f);
    addRarEv(js3, kRarStatusRun, kRarStatusJumpSquat);
    rarStep(st, js3);
    RarFrameInput jp3 = makeRarFrame(54, kRarStatusJump, 1.0f, 1.8f, 1);
    addRarEv(jp3, kRarStatusJumpSquat, kRarStatusJump);
    CHECK(rarStep(st, jp3));
    CHECK_EQ(static_cast<int>(kRarFailNoFlip), static_cast<int>(st.last.outcome));

    // 4. kRarFailWalkTurn: entered standing Turn (0x06) instead of Turn_Run (0x08).
    rarStep(st, makeRarFrame(60, kRarStatusRun, 1.0f, 2.0f));
    RarFrameInput wt = makeRarFrame(61, kRarStatusTurn, -1.0f, 0.2f);
    addRarEv(wt, kRarStatusRunBrake, kRarStatusTurn);
    rarStep(st, wt);
    RarFrameInput js4 = makeRarFrame(62, kRarStatusJumpSquat, -1.0f, 0.1f);
    addRarEv(js4, kRarStatusTurn, kRarStatusJumpSquat);
    rarStep(st, js4);
    RarFrameInput jp4 = makeRarFrame(65, kRarStatusJump, -1.0f, 0.1f, 1);
    addRarEv(jp4, kRarStatusJumpSquat, kRarStatusJump);
    CHECK(rarStep(st, jp4));
    CHECK_EQ(static_cast<int>(kRarFailWalkTurn), static_cast<int>(st.last.outcome));
}

// ============================================================================
// Engine 2: DACUS & Gatling DACUS (MoveDef/Misc.asm:565-572)
// ============================================================================

TEST(stage4a_pplus_tech, dacus_whiff_frame2_frame3_and_slide_distance)
{
    DacusState st;
    dacusReset(st);

    // Whiff Frame 2 DACUS: da on frame 10, 0x26 -> 0x30 on frame 11.
    DacusFrameInput f10 = makeDacusFrame(10, kDacusStatusAttackDash, 0.0f, 1.5f);
    f10.hasLiveRaBasic1 = 1;
    f10.raBasic1 = 1;
    addDacusEv(f10, 0x03, kDacusStatusAttackDash);
    CHECK(!dacusStep(st, f10));

    DacusFrameInput f11 = makeDacusFrame(11, kDacusStatusAttackHi4Start, 2.5f, 2.5f);
    addDacusEv(f11, kDacusStatusAttackDash, kDacusStatusAttackHi4Start);
    CHECK(dacusStep(st, f11));
    CHECK_EQ(static_cast<int>(kDacusSuccess), static_cast<int>(st.last.outcome));
    CHECK_EQ(2, static_cast<int>(st.last.rawDashFrame));
    CHECK_EQ(2, static_cast<int>(st.last.nonHitlagDashFrame));
    CHECK_EQ(0, static_cast<int>(st.last.gatling));
    CHECK_NEAR(2.5f, st.last.vxCarry, 0.001);

    // Slide continues through 0x30 / 0x31 / 0x32 (x goes 2.5 -> 5.0 -> 8.2 -> 10.5).
    dacusStep(st, makeDacusFrame(12, kDacusStatusAttackHi4Start, 5.0f, 2.5f));
    dacusStep(st, makeDacusFrame(13, kDacusStatusAttackHi4Hold, 8.2f, 3.2f));
    dacusStep(st, makeDacusFrame(14, kDacusStatusAttackHi4, 10.5f, 2.3f));
    CHECK_NEAR(10.5f, st.last.slideDistance, 0.01);

    // Whiff Frame 3 DACUS: da on frame 30, stays in 0x26 frame 31, cancels on frame 32.
    DacusFrameInput f30 = makeDacusFrame(30, kDacusStatusAttackDash, 0.0f, 1.4f);
    f30.hasLiveRaBasic1 = 1;
    f30.raBasic1 = 1;
    addDacusEv(f30, 0x03, kDacusStatusAttackDash);
    dacusStep(st, f30);

    DacusFrameInput f31 = makeDacusFrame(31, kDacusStatusAttackDash, 1.5f, 1.5f);
    f31.hasLiveRaBasic1 = 1;
    f31.raBasic1 = 2;
    dacusStep(st, f31);

    DacusFrameInput f32 = makeDacusFrame(32, kDacusStatusAttackHi4Start, 3.8f, 2.3f);
    addDacusEv(f32, kDacusStatusAttackDash, kDacusStatusAttackHi4Start);
    CHECK(dacusStep(st, f32));
    CHECK_EQ(static_cast<int>(kDacusSuccess), static_cast<int>(st.last.outcome));
    CHECK_EQ(3, static_cast<int>(st.last.rawDashFrame));
    CHECK_EQ(3, static_cast<int>(st.last.nonHitlagDashFrame));
    CHECK_EQ(0, static_cast<int>(st.last.gatling));
}

TEST(stage4a_pplus_tech, dacus_sonic_gatling_hitlag_freeze_up_to_raw_frame_7)
{
    DacusState st;
    dacusReset(st);

    // Sonic Frame-2 dash attack hits target; RA-Basic[1] freezes during hitlag:
    //   f10 (da, f1): hitlag=0, RA-Basic[1]=1
    //   f11 (f2, hits!): hitlag=5, RA-Basic[1]=2
    //   f12..f15 (f3..f6): hitlag=4,3,2,1, RA-Basic[1] frozen at 2
    //   f16 (f7, post_hitlag_f1): cancels 0x26 -> 0x30! rawDashFrame=7, nonHitlagDashFrame=3, gatling=1.
    DacusFrameInput f10 = makeDacusFrame(10, kDacusStatusAttackDash, 0.0f, 1.6f, 0, 0);
    f10.hasLiveRaBasic1 = 1;
    f10.raBasic1 = 1;
    addDacusEv(f10, 0x03, kDacusStatusAttackDash);
    dacusStep(st, f10);

    DacusFrameInput f11 = makeDacusFrame(11, kDacusStatusAttackDash, 1.6f, 1.6f, 5, 0);
    f11.hasLiveRaBasic1 = 1;
    f11.raBasic1 = 2;
    dacusStep(st, f11);

    for (int k = 0; k < 4; k++)
    {
        DacusFrameInput fh = makeDacusFrame(12 + k, kDacusStatusAttackDash, 1.6f, 0.0f, 4 - k, 5 - k);
        fh.hasLiveRaBasic1 = 1;
        fh.raBasic1 = 2;
        dacusStep(st, fh);
    }

    DacusFrameInput f16 = makeDacusFrame(16, kDacusStatusAttackHi4Start, 3.2f, 1.6f, 0, 1);
    addDacusEv(f16, kDacusStatusAttackDash, kDacusStatusAttackHi4Start);
    CHECK(dacusStep(st, f16));
    CHECK_EQ(static_cast<int>(kDacusSuccessGatling), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.gatling));
    CHECK_EQ(7, static_cast<int>(st.last.rawDashFrame));
    CHECK_EQ(3, static_cast<int>(st.last.nonHitlagDashFrame));
    CHECK_EQ(1u, st.gatlingCount);

    // Also verify mid-hitlag cancel on f15 (last_hitlag, prevHitlag=2) yields raw=6, nonHitlag=2, gatling=1.
    dacusReset(st);
    dacusStep(st, f10);
    dacusStep(st, f11);
    for (int k = 0; k < 3; k++)
    {
        DacusFrameInput fh = makeDacusFrame(12 + k, kDacusStatusAttackDash, 1.6f, 0.0f, 4 - k, 5 - k);
        fh.hasLiveRaBasic1 = 1;
        fh.raBasic1 = 2;
        dacusStep(st, fh);
    }
    DacusFrameInput f15Cancel = makeDacusFrame(15, kDacusStatusAttackHi4Start, 3.0f, 1.4f, 0, 2);
    addDacusEv(f15Cancel, kDacusStatusAttackDash, kDacusStatusAttackHi4Start);
    CHECK(dacusStep(st, f15Cancel));
    CHECK_EQ(static_cast<int>(kDacusSuccessGatling), static_cast<int>(st.last.outcome));
    CHECK_EQ(6, static_cast<int>(st.last.rawDashFrame));
    CHECK_EQ(2, static_cast<int>(st.last.nonHitlagDashFrame));
    CHECK_EQ(1, static_cast<int>(st.last.gatling));
}

TEST(stage4a_pplus_tech, dacus_miss_late_and_jump_cancel)
{
    DacusState st;
    dacusReset(st);

    // Late miss: Up + Attack pressed on RA-Basic[1] == 4 (1f late).
    DacusFrameInput f1 = makeDacusFrame(1, kDacusStatusAttackDash, 0.0f, 1.2f);
    f1.hasLiveRaBasic1 = 1;
    f1.raBasic1 = 1;
    addDacusEv(f1, 0x03, kDacusStatusAttackDash);
    dacusStep(st, f1);

    DacusFrameInput f2 = makeDacusFrame(2, kDacusStatusAttackDash, 1.2f, 1.2f);
    f2.hasLiveRaBasic1 = 1;
    f2.raBasic1 = 2;
    dacusStep(st, f2);

    DacusFrameInput f3 = makeDacusFrame(3, kDacusStatusAttackDash, 2.4f, 1.2f);
    f3.hasLiveRaBasic1 = 1;
    f3.raBasic1 = 3;
    dacusStep(st, f3);

    DacusFrameInput f4 = makeDacusFrame(4, kDacusStatusAttackDash, 3.6f, 1.2f);
    f4.hasLiveRaBasic1 = 1;
    f4.raBasic1 = 4;
    f4.stickY = 0.90f;
    f4.buttons = kDacusBtnAttack;
    CHECK(dacusStep(st, f4));
    CHECK_EQ(static_cast<int>(kDacusMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.lateByFrames));
    CHECK_EQ(4, static_cast<int>(st.last.nonHitlagDashFrame));

    // Jump-Cancel Up-Smash miss: Jump_Squat (0x0A) -> Attack_Hi4_Start (0x30).
    DacusFrameInput jc = makeDacusFrame(20, kDacusStatusAttackHi4Start, 5.0f, 1.1f);
    addDacusEv(jc, kDacusStatusJumpSquat, kDacusStatusAttackHi4Start);
    CHECK(dacusStep(st, jc));
    CHECK_EQ(static_cast<int>(kDacusMissJumpCancel), static_cast<int>(st.last.outcome));
}

// ============================================================================
// Engine 3: B-Reverse, Wavebounce, Turnaround-B (Constants.asm:105-106)
// ============================================================================

TEST(stage4a_pplus_tech, breverse_frame5_flip_and_frame6_exact_neg1x_all_variants)
{
    BReverseState st;

    // 1. B-Reverse (lr0=+1, lr1=+1, lr5=-1, vx5=+1.30, vx6=-1.28 -> ratio ~0.985, NO 0.8x decay).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.32f, 0.0f));
    BReverseFrameInput sp1 = makeBRevFrame(2, 0x112, 4, 1.0f, 1.30f, 0.0f);
    addBRevEv(sp1, 0x0B, 0x112);
    CHECK(!breverseStep(st, sp1));
    CHECK(!breverseStep(st, makeBRevFrame(3, 0x112, 3, 1.0f, 1.30f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(4, 0x112, 2, 1.0f, 1.30f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(5, 0x112, 1, 1.0f, 1.30f, 0.0f)));
    // Frame 5 (step 6): LA-Basic[55] 1 -> 0, lr flips to -1.0, vx5 still +1.30.
    CHECK(!breverseStep(st, makeBRevFrame(6, 0x112, 0, -1.0f, 1.30f, -1.0f)));
    // Frame 6 (step 7): reversed vx6 = -1.28 appears here!
    CHECK(breverseStep(st, makeBRevFrame(7, 0x112, 0, -1.0f, -1.28f, 0.0f)));
    CHECK_EQ(static_cast<int>(kBRevBReverse), static_cast<int>(st.last.outcome));
    CHECK_NEAR(1.30f, st.last.vxBefore, 0.001);
    CHECK_NEAR(-1.28f, st.last.vxAfter, 0.001);
    CHECK_NEAR(1.28f / 1.30f, st.last.momentumRatio, 0.001);
    CHECK_EQ(1u, st.bReverseCount);

    // 2. Wavebounce (Lucas 0x115 -> 0x11F sub-status: lr0=+1, lr1=-1, lr5=+1, vx6 reversed!).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.21f, 0.0f));
    BReverseFrameInput wb1 = makeBRevFrame(2, 0x115, 4, -1.0f, 1.21f, -0.38f);
    addBRevEv(wb1, 0x0B, 0x115);
    breverseStep(st, wb1);
    breverseStep(st, makeBRevFrame(3, 0x11F, 3, -1.0f, 1.21f, 0.0f));
    breverseStep(st, makeBRevFrame(4, 0x11F, 2, -1.0f, 1.21f, 0.0f));
    breverseStep(st, makeBRevFrame(5, 0x11F, 1, -1.0f, 1.21f, 0.0f));
    breverseStep(st, makeBRevFrame(6, 0x11F, 0, 1.0f, 1.21f, 1.0f));
    CHECK(breverseStep(st, makeBRevFrame(7, 0x11F, 0, 1.0f, -1.18f, 0.0f)));
    CHECK_EQ(static_cast<int>(kBRevWavebounce), static_cast<int>(st.last.outcome));
    CHECK_NEAR(-1.18f, st.last.vxAfter, 0.001);
    CHECK_EQ(1u, st.wavebounceCount);

    // 3. Turnaround-B (lr0=+1, lr1=-1, lr5=-1, vx preserved forward!).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.25f, 0.0f));
    BReverseFrameInput tb1 = makeBRevFrame(2, 0x112, 4, -1.0f, 1.25f, -0.38f);
    addBRevEv(tb1, 0x0B, 0x112);
    breverseStep(st, tb1);
    breverseStep(st, makeBRevFrame(3, 0x112, 3, -1.0f, 1.23f, 0.0f));
    breverseStep(st, makeBRevFrame(4, 0x112, 2, -1.0f, 1.21f, 0.0f));
    breverseStep(st, makeBRevFrame(5, 0x112, 1, -1.0f, 1.19f, 0.0f));
    breverseStep(st, makeBRevFrame(6, 0x112, 0, -1.0f, 1.17f, 0.0f));
    CHECK(breverseStep(st, makeBRevFrame(7, 0x112, 0, -1.0f, 1.15f, 0.0f)));
    CHECK_EQ(static_cast<int>(kBRevTurnaroundB), static_cast<int>(st.last.outcome));
    CHECK(st.last.vxAfter > 0.0f);
    CHECK_EQ(1u, st.turnaroundCount);
}

TEST(stage4a_pplus_tech, breverse_neutral_early_and_late_flick_hints)
{
    BReverseState st;

    // Early flick: opposite stick pulsed on Frame 3 (LA-Basic[55]==2) and released before Frame 5 -> 2f early.
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.20f, 0.0f));
    BReverseFrameInput e1 = makeBRevFrame(2, 0x112, 4, 1.0f, 1.20f, 0.0f);
    addBRevEv(e1, 0x0B, 0x112);
    breverseStep(st, e1);
    breverseStep(st, makeBRevFrame(3, 0x112, 3, 1.0f, 1.20f, 0.0f));  // SpF 2
    breverseStep(st, makeBRevFrame(4, 0x112, 2, 1.0f, 1.20f, -1.0f)); // SpF 3 opposite pulse
    breverseStep(st, makeBRevFrame(5, 0x112, 1, 1.0f, 1.20f, 0.0f));  // SpF 4 released
    breverseStep(st, makeBRevFrame(6, 0x112, 0, 1.0f, 1.20f, 0.0f));  // SpF 5 neutral -> no flip
    CHECK(breverseStep(st, makeBRevFrame(7, 0x112, 0, 1.0f, 1.18f, 0.0f)));
    CHECK_EQ(static_cast<int>(kBRevNeutral), static_cast<int>(st.last.outcome));
    CHECK_EQ(2, static_cast<int>(st.last.earlyFlickFrames));
    CHECK_EQ(0, static_cast<int>(st.last.lateFlickFrames));

    // Late flick: neutral through Frame 5, then opposite stick arrives 3 frames after Frame 5 (step 9).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.20f, 0.0f));
    BReverseFrameInput l1 = makeBRevFrame(2, 0x112, 4, 1.0f, 1.20f, 0.0f);
    addBRevEv(l1, 0x0B, 0x112);
    breverseStep(st, l1);
    breverseStep(st, makeBRevFrame(3, 0x112, 3, 1.0f, 1.20f, 0.0f));
    breverseStep(st, makeBRevFrame(4, 0x112, 2, 1.0f, 1.20f, 0.0f));
    breverseStep(st, makeBRevFrame(5, 0x112, 1, 1.0f, 1.20f, 0.0f));
    breverseStep(st, makeBRevFrame(6, 0x112, 0, 1.0f, 1.20f, 0.0f)); // Frame 5 (reversalFrame = 6)
    CHECK(breverseStep(st, makeBRevFrame(7, 0x112, 0, 1.0f, 1.18f, 0.0f))); // Frame 6 (+1)
    CHECK_EQ(static_cast<int>(kBRevNeutral), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.lateFlickFrames));
    breverseStep(st, makeBRevFrame(8, 0x112, 0, 1.0f, 1.16f, 0.0f));        // +2
    breverseStep(st, makeBRevFrame(9, 0x112, 0, 1.0f, 1.14f, -1.0f));       // +3 late flick!
    CHECK_EQ(3, static_cast<int>(st.last.lateFlickFrames));
}

// ============================================================================
// Engine 4: Footstool & Lock + AGT (MoveDef/Misc.asm, Tethers.asm)
// ============================================================================

TEST(stage4a_pplus_tech, footstool_grounded_airborne_tech_lock_and_taunt_lockout)
{
    FootstoolAgtState st;
    footstoolAgtReset(st);

    // 1. Taunt-spam lockout failure: airborne near opponent in slot 2 (multi-fighter!) with LA-Basic[82] == 12 < 20.
    FootstoolAgtFrameInput failIn = makeFaFrame(10, 0, 0x0E, 1);
    failIn.x = 10.0f;
    failIn.y = 14.0f;
    failIn.buttons = 1u << 10; // Appeal_Hi
    failIn.laBasic82 = 12;
    failIn.opponentCount = 2;
    failIn.opponents[0].slot = 1;
    failIn.opponents[0].x = -60.0f; // far away
    failIn.opponents[1].slot = 2;
    failIn.opponents[1].x = 12.0f;  // nearby opponent in 3-player match!
    failIn.opponents[1].y = 4.0f;
    footstoolAgtStep(st, failIn);
    CHECK_EQ(static_cast<int>(kFootstoolFailTauntLockout), static_cast<int>(st.lastFootstool.outcome));
    CHECK_EQ(12, static_cast<int>(st.lastFootstool.tauntLockoutFrames));
    CHECK_EQ(1u, st.failLockoutCount);

    // 2. Grounded shorthop footstool (Tread_Jump 0x6D + RA-Bit[6], victim Tread_Damage 0x6E).
    FootstoolAgtFrameInput gnd = makeFaFrame(35, 0, kFaStatusTreadJump, 1);
    gnd.x = 12.0f;
    gnd.y = 14.0f;
    gnd.laBasic82 = 25;
    gnd.statusFlags0 = kFaShortHopFlagBit;
    addFaEv(gnd, 0x0E, kFaStatusTreadJump, kFaShortHopFlagBit);
    gnd.opponentCount = 1;
    gnd.opponents[0].slot = 2;
    gnd.opponents[0].status = kFaStatusTreadDamage;
    gnd.opponents[0].x = 12.0f;
    gnd.opponents[0].y = 0.0f;
    footstoolAgtStep(st, gnd);
    CHECK_EQ(static_cast<int>(kFootstoolGrounded), static_cast<int>(st.lastFootstool.outcome));
    CHECK_EQ(1, static_cast<int>(st.lastFootstool.shortHop));
    CHECK_EQ(2, static_cast<int>(st.lastFootstool.victimSlot));
    CHECK_EQ(1u, st.footstoolCount);

    // 3. Airborne footstool -> victim misses tech (Tread_Fall 0x71 -> Down 0x4A) -> attacker hits = Footstool Lock!
    FootstoolAgtFrameInput airFs = makeFaFrame(60, 0, kFaStatusTreadJump, 1);
    airFs.x = 20.0f;
    airFs.y = 22.0f;
    airFs.laBasic82 = 25;
    addFaEv(airFs, 0x0E, kFaStatusTreadJump);
    airFs.opponentCount = 1;
    airFs.opponents[0].slot = 3;
    airFs.opponents[0].status = kFaStatusTreadDamageAir;
    airFs.opponents[0].airborne = 1;
    airFs.opponents[0].x = 20.0f;
    airFs.opponents[0].y = 14.0f;
    footstoolAgtStep(st, airFs);
    CHECK_EQ(static_cast<int>(kFootstoolAirborne), static_cast<int>(st.lastFootstool.outcome));
    CHECK_EQ(3, static_cast<int>(st.lastFootstool.victimSlot));

    // Victim falls into Down (missed tech).
    FootstoolAgtFrameInput missTech = makeFaFrame(75, 0, 0x0E, 1);
    missTech.opponentCount = 1;
    missTech.opponents[0].slot = 3;
    missTech.opponents[0].status = kFaStatusDown;
    missTech.opponents[0].eventCount = 1;
    missTech.opponents[0].events[0].prev = kFaStatusTreadFall;
    missTech.opponents[0].events[0].status = kFaStatusDown;
    footstoolAgtStep(st, missTech);
    CHECK_EQ(1, static_cast<int>(st.lastFootstool.victimMissedTech));
    CHECK_EQ(0, static_cast<int>(st.lastFootstool.victimTeched));

    // Attacker hits victim during Down (0x4A..0x4E) -> Footstool Lock!
    FootstoolAgtFrameInput lockHit = makeFaFrame(80, 0, 0x33, 1);
    lockHit.hitDealt = 1;
    lockHit.hitDealtTarget = 3;
    lockHit.opponentCount = 1;
    lockHit.opponents[0].slot = 3;
    lockHit.opponents[0].status = kFaStatusDownDamage;
    lockHit.opponents[0].hitReceived = 1;
    lockHit.opponents[0].lastReceivedOther = 0;
    footstoolAgtStep(st, lockHit);
    CHECK_EQ(1, static_cast<int>(st.lastFootstool.footstoolLockHit));
    CHECK_EQ(1u, st.lockCount);

    // 4. Airborne footstool where victim techs ground impact (0x71 -> Passive 0x60).
    FootstoolAgtFrameInput airFs2 = makeFaFrame(120, 0, kFaStatusTreadJump, 1);
    airFs2.laBasic82 = 30;
    addFaEv(airFs2, 0x0E, kFaStatusTreadJump);
    airFs2.opponentCount = 1;
    airFs2.opponents[0].slot = 1;
    airFs2.opponents[0].status = kFaStatusTreadDamageAir;
    footstoolAgtStep(st, airFs2);
    FootstoolAgtFrameInput teched = makeFaFrame(135, 0, 0x0E, 1);
    teched.opponentCount = 1;
    teched.opponents[0].slot = 1;
    teched.opponents[0].status = kFaStatusPassive;
    teched.opponents[0].eventCount = 1;
    teched.opponents[0].events[0].prev = kFaStatusTreadFall;
    teched.opponents[0].events[0].status = kFaStatusPassive;
    footstoolAgtStep(st, teched);
    CHECK_EQ(1, static_cast<int>(st.lastFootstool.victimTeched));
    CHECK_EQ(0, static_cast<int>(st.lastFootstool.footstoolLockHit));
}

TEST(stage4a_pplus_tech, agt_chain_la81_landing_reset_and_grounded_glide_toss)
{
    FootstoolAgtState st;
    footstoolAgtReset(st);

    // 1. Frame-1 AGT: Escape_Air (0x21) -> Item_Throw (0x9B) on same frame or next frame, LA-Basic[81] = 1.
    FootstoolAgtFrameInput f1 = makeFaFrame(10, 0, kFaStatusEscapeAir, 1);
    f1.laBasic81 = 0;
    addFaEv(f1, 0x0B, kFaStatusEscapeAir);
    footstoolAgtStep(st, f1);

    FootstoolAgtFrameInput f2 = makeFaFrame(11, 0, kFaStatusItemThrow, 1);
    f2.laBasic81 = 1;
    f2.vx = 1.775f;
    f2.vy = 1.775f;
    addFaEv(f2, kFaStatusEscapeAir, kFaStatusItemThrow);
    footstoolAgtStep(st, f2);
    CHECK_EQ(1, static_cast<int>(st.agtCompletedThisFrame));
    CHECK_EQ(1, static_cast<int>(st.lastAgt.isAerial));
    CHECK_EQ(2, static_cast<int>(st.lastAgt.throwFrame));
    CHECK_EQ(1, static_cast<int>(st.lastAgt.agtChainCount));
    CHECK_EQ(1, static_cast<int>(st.currentAgtChain));
    CHECK_NEAR(1.775f, st.lastAgt.vxCarry, 0.001);
    CHECK_NEAR(1.775f, st.lastAgt.vyCarry, 0.001);

    // 2. Second consecutive mid-air AGT (frame-1 optimal same-frame cancel): LA-Basic[81] = 2!
    FootstoolAgtFrameInput f3 = makeFaFrame(25, 0, kFaStatusItemThrow, 1);
    f3.laBasic81 = 2;
    f3.vx = 1.598f;
    f3.vy = 1.598f;
    addFaEv(f3, 0x0E, kFaStatusEscapeAir);
    addFaEv(f3, kFaStatusEscapeAir, kFaStatusItemThrow);
    footstoolAgtStep(st, f3);
    CHECK_EQ(1, static_cast<int>(st.lastAgt.throwFrame));
    CHECK_EQ(2, static_cast<int>(st.lastAgt.agtChainCount));
    CHECK_EQ(2u, st.agtCount);

    // 3. Landing auto-resets LA-Basic[81] 2 -> 0 (VariableSet.asm:140).
    FootstoolAgtFrameInput fLand = makeFaFrame(40, 0, kFaStatusLanding, 0);
    fLand.laBasic81 = 0;
    addFaEv(fLand, 0x0E, kFaStatusLanding);
    footstoolAgtStep(st, fLand);
    CHECK_EQ(0, static_cast<int>(st.currentAgtChain));
    CHECK_EQ(1u, st.agtChainResetCount);
    CHECK_EQ(40, st.lastAgtResetFrame);

    // 4. Grounded Glide Toss: Escape_F (0x1F) -> Item_Throw (0x9B).
    FootstoolAgtFrameInput roll = makeFaFrame(60, 0, kFaStatusEscapeF, 0);
    addFaEv(roll, 0x1B, kFaStatusEscapeF);
    footstoolAgtStep(st, roll);
    FootstoolAgtFrameInput gThrow = makeFaFrame(61, 0, kFaStatusItemThrow, 0);
    gThrow.vx = 2.1f;
    gThrow.vy = 0.0f;
    addFaEv(gThrow, kFaStatusEscapeF, kFaStatusItemThrow);
    footstoolAgtStep(st, gThrow);
    CHECK_EQ(0, static_cast<int>(st.lastAgt.isAerial));
    CHECK_EQ(2, static_cast<int>(st.lastAgt.throwFrame));
    CHECK_EQ(1u, st.groundedGlideTossCount);
}

// ============================================================================
// Engine 5: Floorhug / ASDI-Down & Crouch-Cancel (Hitstun.asm, Knockback.asm)
// ============================================================================

TEST(stage4a_pplus_tech, floorhug_cstick_priority_and_hitstun_saved)
{
    // C-Stick >= 0.7 overrides Control Stick (even if Control Stick points up!).
    AsdiEval e1 = fhEvalAsdi(0.0f, -0.85f, 0.0f, 1.0f, 1.0f);
    CHECK_EQ(1, static_cast<int>(e1.hasAsdi));
    CHECK_EQ(1, static_cast<int>(e1.fromCStick));
    CHECK_EQ(1, static_cast<int>(e1.asdiDown));
    CHECK_NEAR(-0.85f, e1.effY, 0.001);

    // Relative C-Stick X scales by lr (-1.0 flips X sign).
    AsdiEval e2 = fhEvalAsdi(0.8f, -0.2f, 0.0f, 0.0f, -1.0f);
    CHECK_EQ(1, static_cast<int>(e2.fromCStick));
    CHECK_NEAR(-0.8f, e2.effX, 0.001);
    CHECK_EQ(1, static_cast<int>(e2.asdiDown));

    // C-Stick below 0.7 radius (0.5^2 + 0.4^2 = 0.41 < 0.49) falls back to Control Stick.
    AsdiEval e3 = fhEvalAsdi(0.5f, -0.4f, 0.0f, -0.9f, 1.0f);
    CHECK_EQ(1, static_cast<int>(e3.hasAsdi));
    CHECK_EQ(0, static_cast<int>(e3.fromCStick));
    CHECK_EQ(1, static_cast<int>(e3.asdiDown));

    // Neither stick >= 0.7 -> no ASDI.
    AsdiEval e4 = fhEvalAsdi(0.3f, -0.3f, 0.4f, -0.4f, 1.0f);
    CHECK_EQ(0, static_cast<int>(e4.hasAsdi));
    CHECK_EQ(0, static_cast<int>(e4.asdiDown));

    // Hitstun Saved = max(0, hitstunAtLanding - (floor(landingFrame) + 1)).
    // Fox/Mario landingFrame = 3.0 -> lag = 4f. 18 - 4 = 14 saved.
    CHECK_EQ(14, static_cast<int>(fhComputeHitstunSaved(18, 3.0f)));
    // Bowser landingFrame = 4.0 -> lag = 5f. 18 - 5 = 13 saved.
    CHECK_EQ(13, static_cast<int>(fhComputeHitstunSaved(18, 4.0f)));
    // Clamped >= 0.
    CHECK_EQ(0, static_cast<int>(fhComputeHitstunSaved(3, 4.0f)));
}

TEST(stage4a_pplus_tech, floorhug_all_six_outcomes_and_true_counter_punish)
{
    FloorhugState st;

    // 1. kFhCcFloorhugCancel + frame-1 punish + True Counter-Punish!
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusSquatWait));

    // Frame 2: hit received while crouching (0x12 -> 0x44), hitlag = 2, hitstun = 16, attacker = slot 1.
    FloorhugFrameInput h2 = makeFhFrame(2, kFhStatusDamageAir, 2, 0, 16, 0);
    h2.hitReceived = 1;
    h2.attackerSlot = 1;
    h2.attackerCount = 1;
    h2.attackers[0].slot = 1;
    h2.attackers[0].actionable = 0;
    addFhEv(h2, kFhStatusSquatWait, kFhStatusDamageAir);
    floorhugStep(st, h2);

    // Frame 3: last hitlag frame (cur.hitlag == 1), C-Stick Down held!
    FloorhugFrameInput h3 = makeFhFrame(3, kFhStatusDamageAir, 1, 2, 16, 16);
    h3.cStickY = -1.0f;
    h3.attackerCount = 1;
    h3.attackers[0].slot = 1;
    h3.attackers[0].actionable = 0;
    floorhugStep(st, h3);

    // Frame 4: hitlag ends (prevHitlag == 1 -> hitlag == 0), 0x44 -> 0x16 Landing with hitstun = 15!
    FloorhugFrameInput h4 = makeFhFrame(4, kFhStatusLanding, 0, 1, 15, 16);
    h4.cStickY = -1.0f;
    h4.motionFrame = 0.0f;
    h4.motionRate = 1.0f;
    h4.attackerCount = 1;
    h4.attackers[0].slot = 1;
    h4.attackers[0].actionable = 0;
    addFhEv(h4, kFhStatusDamageAir, kFhStatusLanding);
    CHECK(floorhugStep(st, h4));
    CHECK_EQ(static_cast<int>(kFhCcFloorhugCancel), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.wasCrouching));
    CHECK_EQ(1, static_cast<int>(st.last.asdiDown));
    CHECK_EQ(1, static_cast<int>(st.last.asdiFromCStick));
    CHECK_EQ(15, static_cast<int>(st.last.hitstunAtLanding));
    CHECK_EQ(11, static_cast<int>(st.last.hitstunSaved)); // 15 - (floor(3.0) + 1) = 11

    // Frames 5, 6, 7: Landing animation frames 1.0, 2.0, 3.0 (at 3.0, 3+1 > 3.01 -> Frame 8 is first actionable!).
    for (int k = 1; k <= 3; k++)
    {
        FloorhugFrameInput lf = makeFhFrame(4 + k, kFhStatusLanding, 0, 0, 15 - k, 16 - k);
        lf.motionFrame = static_cast<float>(k);
        lf.motionRate = 1.0f;
        lf.attackerCount = 1;
        lf.attackers[0].slot = 1;
        lf.attackers[0].actionable = 0;
        floorhugStep(st, lf);
    }

    // Frame 8: Frame-perfect counter-grab (0x16 -> 0x34 Catch) while attacker still in endlag!
    FloorhugFrameInput act8 = makeFhFrame(8, kFhStatusCatch, 0, 0, 11, 12);
    act8.attackerCount = 1;
    act8.attackers[0].slot = 1;
    act8.attackers[0].actionable = 0;
    addFhEv(act8, kFhStatusLanding, kFhStatusCatch);
    floorhugStep(st, act8);
    CHECK_EQ(1, static_cast<int>(st.last.punishActFrame));

    // Frame 11: Catch_Pull (0x35) connects BEFORE attacker becomes actionable -> True Counter-Punish!
    FloorhugFrameInput pull11 = makeFhFrame(11, kFhStatusCatchPull, 0, 0, 0, 0);
    pull11.attackerCount = 1;
    pull11.attackers[0].slot = 1;
    pull11.attackers[0].actionable = 0;
    addFhEv(pull11, kFhStatusCatch, kFhStatusCatchPull);
    floorhugStep(st, pull11);
    CHECK_EQ(1, static_cast<int>(st.last.trueCounterPunish));
    CHECK_EQ(1u, st.truePunishCount);

    // 2. kFhStandingFloorhugCancel (Wait 0x00 before hit, Control Stick Down).
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusWait));
    FloorhugFrameInput s2 = makeFhFrame(2, kFhStatusDamageAir, 1, 0, 14, 0);
    s2.hitReceived = 1;
    s2.stickY = -1.0f;
    addFhEv(s2, kFhStatusWait, kFhStatusDamageAir);
    floorhugStep(st, s2);
    FloorhugFrameInput s3 = makeFhFrame(3, kFhStatusLanding, 0, 1, 13, 14);
    s3.stickY = -1.0f;
    addFhEv(s3, kFhStatusDamageAir, kFhStatusLanding);
    CHECK(floorhugStep(st, s3));
    CHECK_EQ(static_cast<int>(kFhStandingFloorhugCancel), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.wasCrouching));
    CHECK_EQ(1, static_cast<int>(st.last.asdiDown));
    CHECK_EQ(0, static_cast<int>(st.last.asdiFromCStick));
    CHECK_EQ(9, static_cast<int>(st.last.hitstunSaved)); // 13 - 4 = 9

    // 3. kFhGroundedCc (Squat 0x11 -> Damage 0x43, no 0x44).
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusSquat));
    FloorhugFrameInput g2 = makeFhFrame(2, kFhStatusDamage, 1, 0, 6, 0);
    g2.hitReceived = 1;
    g2.stickY = -1.0f;
    addFhEv(g2, kFhStatusSquat, kFhStatusDamage);
    floorhugStep(st, g2);
    FloorhugFrameInput g3 = makeFhFrame(3, kFhStatusDamage, 0, 1, 5, 6);
    g3.stickY = -1.0f;
    CHECK(floorhugStep(st, g3));
    CHECK_EQ(static_cast<int>(kFhGroundedCc), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.wasCrouching));

    // 4. kFhAsdiDownGroundTech (0x45 Damage_Fly + ASDI-Down + Passive 0x60).
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusWait));
    FloorhugFrameInput t2 = makeFhFrame(2, kFhStatusDamageFly, 1, 0, 30, 0);
    t2.hitReceived = 1;
    t2.cStickY = -1.0f;
    addFhEv(t2, kFhStatusWait, kFhStatusDamageFly);
    floorhugStep(st, t2);
    FloorhugFrameInput t3 = makeFhFrame(3, kFhStatusPassive, 0, 1, 0, 30);
    t3.cStickY = -1.0f;
    addFhEv(t3, kFhStatusDamageFly, kFhStatusPassive);
    CHECK(floorhugStep(st, t3));
    CHECK_EQ(static_cast<int>(kFhAsdiDownGroundTech), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.asdiDown));

    // 5. kFhBrokenTumble (0x45 Damage_Fly without ground tech).
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusSquatWait));
    FloorhugFrameInput b2 = makeFhFrame(2, kFhStatusDamageFly, 1, 0, 35, 0);
    b2.hitReceived = 1;
    b2.stickY = -1.0f;
    addFhEv(b2, kFhStatusSquatWait, kFhStatusDamageFly);
    floorhugStep(st, b2);
    bool emittedTumble = false;
    for (int f = 3; f <= 10; f++)
    {
        FloorhugFrameInput bf = makeFhFrame(f, kFhStatusDamageFly, 0, (f == 3 ? 1 : 0), 35 - (f - 2), 35);
        bf.airborne = 1;
        if (floorhugStep(st, bf))
        {
            emittedTumble = true;
            break;
        }
    }
    CHECK(emittedTumble);
    CHECK_EQ(static_cast<int>(kFhBrokenTumble), static_cast<int>(st.last.outcome));

    // 6. kFhMissedFloorhug (0x44 without cancelling into 0x16).
    floorhugReset(st);
    floorhugStep(st, makeFhFrame(1, kFhStatusWait));
    FloorhugFrameInput m2 = makeFhFrame(2, kFhStatusDamageAir, 1, 0, 14, 0);
    m2.hitReceived = 1;
    addFhEv(m2, kFhStatusWait, kFhStatusDamageAir);
    floorhugStep(st, m2);
    floorhugStep(st, makeFhFrame(3, kFhStatusDamageAir, 0, 1, 13, 14));
    CHECK(floorhugStep(st, makeFhFrame(4, kFhStatusDamageAir, 0, 0, 12, 13)));
    CHECK_EQ(static_cast<int>(kFhMissedFloorhug), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.asdiDown));
}

TEST(stage4a_pplus_tech, debug_block_layout_multi_slot_and_savestate_reset)
{
    // Verify exact 4-byte aligned binary layout expected by scripts/stage4a_pplus_tech_tests.py.
    CHECK_EQ(152u, static_cast<unsigned>(sizeof(PPlusTechDebugSlot)));
    CHECK_EQ(16u + 4u * 152u, static_cast<unsigned>(sizeof(PPlusTechDebugBlock)));

    // Verify independent multi-fighter slots (slots 0..3) and clean reset on savestate load.
    RarState rars[4];
    DacusState dacuses[4];
    BReverseState brevs[4];
    FootstoolAgtState fas[4];
    FloorhugState fhs[4];
    for (int i = 0; i < 4; i++)
    {
        rarReset(rars[i]);
        dacusReset(dacuses[i]);
        breverseReset(brevs[i]);
        footstoolAgtReset(fas[i]);
        floorhugReset(fhs[i]);
    }

    // Mutate slot 2 only; slots 0, 1, 3 must remain completely untouched.
    rarStep(rars[2], makeRarFrame(1, kRarStatusRun, 1.0f, 2.0f));
    RarFrameInput tr = makeRarFrame(2, kRarStatusTurnRun, -1.0f, 1.8f);
    addRarEv(tr, kRarStatusRun, kRarStatusTurnRun);
    rarStep(rars[2], tr);
    RarFrameInput js = makeRarFrame(3, kRarStatusJumpSquat, -1.0f, 1.7f);
    addRarEv(js, kRarStatusTurnRun, kRarStatusJumpSquat);
    rarStep(rars[2], js);
    RarFrameInput jp = makeRarFrame(6, kRarStatusJump, -1.0f, 1.6f, 1);
    addRarEv(jp, kRarStatusJumpSquat, kRarStatusJump);
    rarStep(rars[2], jp);
    RarFrameInput ba = makeRarFrame(7, kRarStatusAttackAir, -1.0f, 1.5f, 1, kRarMotionAttackAirB);
    addRarEv(ba, kRarStatusJump, kRarStatusAttackAir);
    rarStep(rars[2], ba);
    CHECK_EQ(1u, rars[2].successBairCount);
    CHECK_EQ(0u, rars[0].attempts);
    CHECK_EQ(0u, rars[1].attempts);
    CHECK_EQ(0u, rars[3].attempts);

    // Savestate reset clears all transient and cumulative fields cleanly.
    rarReset(rars[2]);
    CHECK_EQ(0u, rars[2].attempts);
    CHECK_EQ(0u, rars[2].successBairCount);
    CHECK_EQ(static_cast<int>(kRarNone), static_cast<int>(rars[2].last.outcome));
}

TEST(stage4a_pplus_tech, breverse_early_landing_aborts_frame5_and_frame6)
{
    BReverseState st;

    // 1. Short-hop special lands on Special Frame 5 (Landing 0x16 as LA-Basic[55] drops 1 -> 0).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(1, 0x0B, 0, 1.0f, 1.25f, 0.0f));
    BReverseFrameInput sp1 = makeBRevFrame(2, 0x112, 4, 1.0f, 1.25f, 0.0f);
    addBRevEv(sp1, 0x0B, 0x112);
    CHECK(!breverseStep(st, sp1));
    CHECK(!breverseStep(st, makeBRevFrame(3, 0x112, 3, 1.0f, 1.24f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(4, 0x112, 2, 1.0f, 1.23f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(5, 0x112, 1, 1.0f, 1.22f, 0.0f)));
    BReverseFrameInput land5 = makeBRevFrame(6, 0x16, 0, 1.0f, 0.30f, -1.0f, 0);
    addBRevEv(land5, 0x112, 0x16);
    CHECK(!breverseStep(st, land5));
    CHECK_EQ(static_cast<int>(kBRevPhaseIdle), static_cast<int>(st.phase));
    CHECK_EQ(0u, st.attempts);
    CHECK_EQ(0, static_cast<int>(st.completedThisFrame));
    CHECK(!breverseStep(st, makeBRevFrame(7, 0x16, 0, 1.0f, 0.10f, 0.0f, 0)));
    CHECK_EQ(0u, st.attempts);

    // 2. Special completes Frame 5 airborne, then lands on Special Frame 6 (Landing 0x16).
    breverseReset(st);
    breverseStep(st, makeBRevFrame(10, 0x0B, 0, 1.0f, 1.30f, 0.0f));
    BReverseFrameInput sp2 = makeBRevFrame(11, 0x112, 4, 1.0f, 1.30f, 0.0f);
    addBRevEv(sp2, 0x0B, 0x112);
    CHECK(!breverseStep(st, sp2));
    CHECK(!breverseStep(st, makeBRevFrame(12, 0x112, 3, 1.0f, 1.30f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(13, 0x112, 2, 1.0f, 1.30f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(14, 0x112, 1, 1.0f, 1.30f, 0.0f)));
    CHECK(!breverseStep(st, makeBRevFrame(15, 0x112, 0, -1.0f, 1.30f, -1.0f)));
    CHECK_EQ(static_cast<int>(kBRevPhaseWaitFrame6), static_cast<int>(st.phase));
    BReverseFrameInput land6 = makeBRevFrame(16, 0x16, 0, -1.0f, 0.25f, 0.0f, 0);
    addBRevEv(land6, 0x112, 0x16);
    CHECK(!breverseStep(st, land6));
    CHECK_EQ(static_cast<int>(kBRevPhaseIdle), static_cast<int>(st.phase));
    CHECK_EQ(0u, st.attempts);
    CHECK_EQ(0, static_cast<int>(st.completedThisFrame));
}

TEST(stage4a_pplus_tech, footstool_airborne_victim_interrupted_midair_clears_watch)
{
    FootstoolAgtState st;
    footstoolAgtReset(st);

    // Airborne footstool pairs victim in slot 1 (Tread_Damage_Air 0x70).
    FootstoolAgtFrameInput airFs = makeFaFrame(20, 0, kFaStatusTreadJump, 1);
    airFs.x = 15.0f;
    airFs.y = 22.0f;
    airFs.laBasic82 = 30;
    addFaEv(airFs, 0x0E, kFaStatusTreadJump);
    airFs.opponentCount = 1;
    airFs.opponents[0].slot = 1;
    airFs.opponents[0].status = kFaStatusTreadDamageAir;
    airFs.opponents[0].airborne = 1;
    airFs.opponents[0].x = 15.0f;
    airFs.opponents[0].y = 14.0f;
    footstoolAgtStep(st, airFs);
    CHECK_EQ(static_cast<int>(kFootstoolAirborne), static_cast<int>(st.lastFootstool.outcome));
    CHECK_EQ(1, static_cast<int>(st.watchingVictim));

    // Victim transitions into Tread_Fall (0x71) while still airborne.
    FootstoolAgtFrameInput fall = makeFaFrame(25, 0, 0x0E, 1);
    fall.opponentCount = 1;
    fall.opponents[0].slot = 1;
    fall.opponents[0].status = kFaStatusTreadFall;
    fall.opponents[0].airborne = 1;
    footstoolAgtStep(st, fall);
    CHECK_EQ(1, static_cast<int>(st.watchingVictim));

    // Victim is interrupted mid-air into Damage_Fly (0x45) while airborne = 1 -> watch clears immediately.
    FootstoolAgtFrameInput hitMidAir = makeFaFrame(28, 0, 0x33, 1);
    hitMidAir.opponentCount = 1;
    hitMidAir.opponents[0].slot = 1;
    hitMidAir.opponents[0].status = 0x45;
    hitMidAir.opponents[0].airborne = 1;
    hitMidAir.opponents[0].hitReceived = 1;
    footstoolAgtStep(st, hitMidAir);
    CHECK_EQ(0, static_cast<int>(st.watchingVictim));

    // Victim later lands in Down (0x4A) and gets hit into Down_Damage (0x54): must NOT credit a footstool lock.
    FootstoolAgtFrameInput laterDown = makeFaFrame(45, 0, 0x00, 0);
    laterDown.opponentCount = 1;
    laterDown.opponents[0].slot = 1;
    laterDown.opponents[0].status = kFaStatusDown;
    laterDown.opponents[0].airborne = 0;
    footstoolAgtStep(st, laterDown);

    FootstoolAgtFrameInput laterHit = makeFaFrame(50, 0, 0x24, 0);
    laterHit.hitDealt = 1;
    laterHit.hitDealtTarget = 1;
    laterHit.opponentCount = 1;
    laterHit.opponents[0].slot = 1;
    laterHit.opponents[0].status = kFaStatusDownDamage;
    laterHit.opponents[0].hitReceived = 1;
    laterHit.opponents[0].lastReceivedOther = 0;
    footstoolAgtStep(st, laterHit);

    CHECK_EQ(0, static_cast<int>(st.lastFootstool.victimMissedTech));
    CHECK_EQ(0, static_cast<int>(st.lastFootstool.footstoolLockHit));
    CHECK_EQ(0u, st.lockCount);
}

TEST(stage4a_pplus_tech, rar_brief_run_brake_gap_allowed_and_stale_rejected)
{
    RarState st;
    rarReset(st);

    // 1. 1-frame neutral gap: Run (f10) -> Run_Brake (f11) -> Turn_Run (f12) -> Jump_Squat (f13) -> Jump (f16) -> Bair (f17).
    CHECK(!rarStep(st, makeRarFrame(10, kRarStatusRun, 1.0f, 2.0f)));
    RarFrameInput rb1 = makeRarFrame(11, kRarStatusRunBrake, 1.0f, 1.85f);
    addRarEv(rb1, kRarStatusRun, kRarStatusRunBrake);
    CHECK(!rarStep(st, rb1));
    RarFrameInput tr1 = makeRarFrame(12, kRarStatusTurnRun, -1.0f, 1.70f);
    addRarEv(tr1, kRarStatusRunBrake, kRarStatusTurnRun);
    CHECK(!rarStep(st, tr1));
    RarFrameInput js1 = makeRarFrame(13, kRarStatusJumpSquat, -1.0f, 1.60f);
    addRarEv(js1, kRarStatusTurnRun, kRarStatusJumpSquat);
    CHECK(!rarStep(st, js1));
    RarFrameInput jp1 = makeRarFrame(16, kRarStatusJump, -1.0f, 1.50f, 1);
    addRarEv(jp1, kRarStatusJumpSquat, kRarStatusJump);
    CHECK(!rarStep(st, jp1));
    RarFrameInput ba1 = makeRarFrame(17, kRarStatusAttackAir, -1.0f, 1.48f, 1, kRarMotionAttackAirB);
    addRarEv(ba1, kRarStatusJump, kRarStatusAttackAir);
    CHECK(rarStep(st, ba1));
    CHECK_EQ(static_cast<int>(kRarSuccessBair), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.turnRunFrames));
    CHECK_EQ(1, static_cast<int>(st.last.backwardMomentum));
    CHECK_NEAR(0.75f, st.last.momentumRetained, 0.001);
    CHECK_EQ(1u, st.attempts);
    CHECK_EQ(1u, st.successBairCount);

    // 2. Stale Run_Brake past kRarRunBrakeGap (4 frames): Run (f30) -> Run_Brake (f31..f34) -> Turn_Run (f35).
    CHECK(!rarStep(st, makeRarFrame(30, kRarStatusRun, 1.0f, 2.0f)));
    RarFrameInput rb2 = makeRarFrame(31, kRarStatusRunBrake, 1.0f, 1.5f);
    addRarEv(rb2, kRarStatusRun, kRarStatusRunBrake);
    rarStep(st, rb2);
    rarStep(st, makeRarFrame(32, kRarStatusRunBrake, 1.0f, 1.1f));
    rarStep(st, makeRarFrame(33, kRarStatusRunBrake, 1.0f, 0.8f));
    rarStep(st, makeRarFrame(34, kRarStatusRunBrake, 1.0f, 0.5f));
    RarFrameInput tr2 = makeRarFrame(35, kRarStatusTurnRun, -1.0f, 0.3f);
    addRarEv(tr2, kRarStatusRunBrake, kRarStatusTurnRun);
    rarStep(st, tr2);
    RarFrameInput js2 = makeRarFrame(36, kRarStatusJumpSquat, -1.0f, 0.2f);
    addRarEv(js2, kRarStatusTurnRun, kRarStatusJumpSquat);
    rarStep(st, js2);
    RarFrameInput jp2 = makeRarFrame(39, kRarStatusJump, -1.0f, 0.2f, 1);
    addRarEv(jp2, kRarStatusJumpSquat, kRarStatusJump);
    rarStep(st, jp2);
    RarFrameInput ba2 = makeRarFrame(40, kRarStatusAttackAir, -1.0f, 0.2f, 1, kRarMotionAttackAirB);
    addRarEv(ba2, kRarStatusJump, kRarStatusAttackAir);
    CHECK(!rarStep(st, ba2));
    CHECK_EQ(1u, st.attempts);
    CHECK_EQ(1u, st.successBairCount);
}
