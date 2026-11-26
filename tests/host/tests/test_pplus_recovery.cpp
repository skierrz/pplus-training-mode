// Host unit tests for the clean-room 44-fighter PPlusAI recovery core
// (src/extra/pplus_recovery_core.h/.cpp) and pure stage geometry math
// (src/extra/stage_geom_math.h).
#include <math.h>
#include <string.h>

#include "extra/pplus_recovery_core.h"
#include "extra/stage_geom_math.h"
#include "host/test.h"

using namespace tm;
using namespace tm::extra;

namespace {
    // All 44 playable Project+ fighters (+ Giga Bowser, Wario-Man, Nana).
    static const int kAllPlayableKinds[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
        0x22, 0x23, 0x25, 0x26, 0x27, 0x29, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31
    };

    RecoveryInput offstageInput(int ftKind, float x, float y, bool jumpsLeft = false)
    {
        RecoveryInput in;
        memset(&in, 0, sizeof(in));
        in.ftKind = ftKind;
        in.status = 0x0E; // Fall
        in.subaction = -1;
        in.x = x;
        in.y = y;
        in.lr = x < 0.0f ? 1.0f : -1.0f;
        in.ledgeX = x < 0.0f ? -68.0f : 68.0f;
        in.ledgeY = 0.0f;
        in.ledgeDir = x < 0.0f ? 1.0f : -1.0f;
        in.stageLeftX = -68.0f;
        in.stageRightX = 68.0f;
        in.stageY = 0.0f;
        in.blastBottom = -140.0f;
        in.air = true;
        in.canAct = true;
        in.jumpsLeft = jumpsLeft;
        in.numJumps = jumpsLeft ? 1 : 0;
        in.floorBelow = false;
        in.yDistFloor = -1.0f;
        return in;
    }
}

TEST(pplus_recovery, all_44_fighters_and_8_families_are_populated)
{
    int familyCounts[kFamCount];
    memset(familyCounts, 0, sizeof(familyCounts));
    const int n = static_cast<int>(sizeof(kAllPlayableKinds) / sizeof(kAllPlayableKinds[0]));
    CHECK_EQ(44, n);

    for (int i = 0; i < n; i++)
    {
        int kind = kAllPlayableKinds[i];
        const FighterRecoverySpec& s = recoverySpec(kind);
        CHECK_EQ(1, static_cast<int>(s.valid));
        CHECK(s.family < kFamCount);
        CHECK(s.upBX > 0 || s.sideBX > 0);
        familyCounts[s.family]++;

        // Every fighter must actively steer/recover when placed offstage.
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x12345678u + static_cast<u32>(kind) * 97u;
        RecoveryInput in = offstageInput(kind, 105.0f, -25.0f, false);
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK(out.active);
        // Must steer toward stage (negative X from right side) or input an Up-B / special.
        CHECK(out.stickX <= 0.01f || (out.buttons & kRecBtnSpecial) != 0);
    }

    for (int f = 0; f < kFamCount; f++)
    {
        CHECK(familyCounts[f] > 0);
    }
    // Spot-check P+ clones Mewtwo (0x26), Roy (0x27), Knuckles (0x2D).
    CHECK_EQ(static_cast<int>(kFamTeleport), static_cast<int>(recoverySpec(0x26).family));
    CHECK_EQ(0x120, static_cast<int>(recoverySpec(0x26).floatStatus));
    CHECK_EQ(0x73, static_cast<int>(recoverySpec(0x26).laResourceIdx));
    CHECK_EQ(static_cast<int>(kFamArc), static_cast<int>(recoverySpec(0x27).family));
    CHECK_EQ(static_cast<int>(kFamCharged), static_cast<int>(recoverySpec(0x2D).family));
    CHECK((recoverySpec(0x2D).flags & kRfWallClimb) != 0);
}

TEST(pplus_recovery, ganondorf_float_polarity_and_mewtwo_hover_exit_actionable)
{
    // Ganondorf LA-Basic[0x62] counts float uses consumed (< 2 available, >= 2 exhausted).
    bool sawFloatAvail = false;
    bool sawFloatExhausted = false;
    for (u32 trial = 0; trial < 32; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x9999u + trial * 131u;
        RecoveryInput in = offstageInput(0x14, 120.0f, -5.0f, false);
        in.laInt = 0; // 0 uses consumed -> float (Neutral-B) allowed
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptNeutralB)
        {
            sawFloatAvail = true;
        }
    }
    for (u32 trial = 0; trial < 32; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x9999u + trial * 131u;
        RecoveryInput in = offstageInput(0x14, 120.0f, -5.0f, false);
        in.laInt = 2; // 2 uses consumed -> float blocked
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptNeutralB)
        {
            sawFloatExhausted = true;
        }
    }
    CHECK(sawFloatAvail);
    CHECK(!sawFloatExhausted);

    // Mewtwo status 0x11E is 13-frame hover-exit, NOT Teleport special fall:
    // when actionable in 0x11E near the blastzone, Mewtwo must still trigger Up-B / jump.
    RecoveryState m2st;
    recoveryStateReset(m2st);
    u32 m2rng = 42u;
    RecoveryInput m2 = offstageInput(0x26, 95.0f, -120.0f, false);
    m2.status = 0x11E;
    m2.canAct = true;
    RecoveryOutput m2out;
    stepRecovery(m2, m2st, m2rng, m2out);
    CHECK(m2out.active);
    CHECK((m2out.buttons & kRecBtnSpecial) != 0);
    CHECK(m2out.stickY > 0.5f);
}

TEST(pplus_recovery, weighted_die_accumulates_and_rolls_deterministically)
{
    WeightedDie die;
    CHECK_EQ(0, die.size());
    CHECK_EQ(static_cast<int>(kOptNone), die.roll(0.5f, kOptNone));

    die.add(kOptJump, 10.0f);
    die.add(kOptUpB, 30.0f);
    die.add(kOptJump, 10.0f); // merges into existing kOptJump entry -> weight 20
    die.add(kOptSideB, 0.0f); // ignored (non-positive)
    CHECK_EQ(2, die.size());
    CHECK(fabsf(die.totalWeight() - 50.0f) < 0.001f);

    CHECK_EQ(static_cast<int>(kOptJump), die.roll(0.0f));
    CHECK_EQ(static_cast<int>(kOptJump), die.roll(0.39f));
    CHECK_EQ(static_cast<int>(kOptUpB), die.roll(0.41f));
    CHECK_EQ(static_cast<int>(kOptUpB), die.roll(0.999f));

    die.clear();
    CHECK_EQ(0, die.size());
}

TEST(pplus_recovery, spacie_360_aim_notch_clamp_and_side_b_shorten)
{
    float x = 0.18f, y = 0.15f;
    clampAimNotches(x, y);
    CHECK(fabsf(x - 0.3f) < 0.001f);
    CHECK(fabsf(y - 0.3f) < 0.001f);

    x = -0.18f;
    y = -0.5f;
    clampAimNotches(x, y);
    CHECK(fabsf(x + 0.3f) < 0.001f);
    CHECK(fabsf(y + 0.5f) < 0.001f);

    // Fox active Up-B aims normalized toward right ledge.
    RecoveryState st;
    recoveryStateReset(st);
    u32 rng = 7u;
    RecoveryInput in = offstageInput(0x06, 110.0f, -35.0f, false);
    in.status = 0x114; // SpecialHi
    RecoveryOutput out;
    stepRecovery(in, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX < -0.5f);
    CHECK(out.stickY > 0.4f);

    // Fox Side-B shorten presses B at frame >= 18.
    st.option = kOptSideBShort;
    in.status = 0x113;
    in.animFrame = 18.0f;
    in.lastButtons = 0;
    stepRecovery(in, st, rng, out);
    CHECK((out.buttons & kRecBtnSpecial) != 0);
}

TEST(pplus_recovery, ceiling_avoidance_and_emergency_blastzone_fallback)
{
    RecoveryState st;
    recoveryStateReset(st);
    u32 rng = 42u;

    // Directly under stage with ceiling above -> steers outward away from center.
    RecoveryInput in = offstageInput(0x11, 55.0f, -25.0f, false); // Marth under right side
    in.ceilingAbove = true;
    RecoveryOutput out;
    stepRecovery(in, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX > 0.5f); // steers right (outward) instead of bonking underside
    CHECK_EQ(0u, out.buttons);

    // Near bottom blastzone -> emergency fallback forces Up-B even without jumps.
    in = offstageInput(0x11, 95.0f, -120.0f, false);
    in.ceilingAbove = false;
    stepRecovery(in, st, rng, out);
    CHECK(out.active);
    CHECK((out.buttons & kRecBtnSpecial) != 0);
    CHECK(out.stickY > 0.5f);
}

TEST(pplus_recovery, family_behaviors_glide_float_tether_pkt_and_cliffwait)
{
    u32 rng = 1337u;
    RecoveryState st;
    RecoveryOutput out;

    // Meta Knight Glide (0x85) pitches up below ledge and cancels near stage.
    recoveryStateReset(st);
    RecoveryInput mk = offstageInput(0x16, 110.0f, -20.0f, false);
    mk.status = 0x85;
    stepRecovery(mk, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX > 0.5f); // pull back to pitch up while facing left toward stage

    mk.x = 74.0f;
    mk.y = 10.0f;
    stepRecovery(mk, st, rng, out);
    CHECK((out.buttons & kRecBtnJump) != 0); // glide cancel near ledge

    // Peach Float (0x11E) holds Jump and drifts toward ledge.
    recoveryStateReset(st);
    RecoveryInput peach = offstageInput(0x0C, 120.0f, -5.0f, false);
    peach.status = 0x11E;
    stepRecovery(peach, st, rng, out);
    CHECK((out.buttons & kRecBtnJump) != 0);
    CHECK(out.stickX < -0.5f);

    // Mewtwo Hover (0x120) holds Jump and 8-way aims toward ledge.
    recoveryStateReset(st);
    RecoveryInput m2 = offstageInput(0x26, 115.0f, -25.0f, false);
    m2.status = 0x120;
    stepRecovery(m2, st, rng, out);
    CHECK((out.buttons & kRecBtnJump) != 0);
    CHECK(out.stickX < -0.4f);
    CHECK(out.stickY > 0.2f);

    // Samus tether hang (0x81) reels in.
    recoveryStateReset(st);
    RecoveryInput samus = offstageInput(0x03, 95.0f, -20.0f, false);
    samus.status = 0x81;
    stepRecovery(samus, st, rng, out);
    CHECK(out.stickY > 0.5f);

    // Ness PK Thunder weapon orbit steers PKT toward target behind Ness.
    recoveryStateReset(st);
    RecoveryInput ness = offstageInput(0x0A, 115.0f, -35.0f, false);
    ness.status = 0x114;
    ness.hasPkt = true;
    ness.pktX = 115.0f;
    ness.pktY = -10.0f;
    ness.pktVx = 0.0f;
    ness.pktVy = 1.5f;
    stepRecovery(ness, st, rng, out);
    CHECK(out.active);
    // Target is down-right of Ness (opposite the up-left cliff vector), so stick steers down-right.
    CHECK(out.stickX > 0.1f);
    CHECK(out.stickY < -0.1f);

    // CliffWait (0x75) exits ledge cleanly after 2 frames.
    recoveryStateReset(st);
    RecoveryInput cliff = offstageInput(0x06, 68.0f, 0.0f, false);
    cliff.status = 0x75;
    stepRecovery(cliff, st, rng, out); // frame 1: wait
    stepRecovery(cliff, st, rng, out); // frame 2: getup input
    CHECK(out.active);
    CHECK(out.buttons != 0 || out.stickX != 0.0f);
}

TEST(pplus_recovery, stage_geom_sampling_stays_within_live_bounds)
{
    StageGeometry g;
    g.blast.left = -240.0f;
    g.blast.right = 240.0f;
    g.blast.top = 180.0f;
    g.blast.bottom = -140.0f;
    g.blast.valid = true;
    g.floor.leftX = -85.0f;
    g.floor.rightX = 85.0f;
    g.floor.leftY = 0.0f;
    g.floor.rightY = 0.0f;
    g.floor.centerY = 0.0f;
    g.floor.valid = true;
    g.leftLedge.x = -85.0f;
    g.leftLedge.y = 0.0f;
    g.leftLedge.dir = 1.0f;
    g.rightLedge.x = 85.0f;
    g.rightLedge.y = 0.0f;
    g.rightLedge.dir = -1.0f;
    g.hasLedges = true;

    u32 rng = 999u;
    for (int i = 0; i < 64; i++)
    {
        float x = 0.0f, y = 0.0f;
        CHECK(sampleSafeCandidate(g, rng, true, x, y));
        CHECK(x >= g.floor.leftX + 9.9f && x <= g.floor.rightX - 9.9f);

        CHECK(sampleSafeCandidate(g, rng, false, x, y));
        CHECK(x >= g.blast.left + 24.9f && x <= g.blast.right - 24.9f);
        CHECK(y >= g.floor.centerY + 7.9f && y <= g.blast.top - 29.9f);

        CHECK(sampleRecoveryCandidate(g, rng, x, y));
        CHECK(x < g.floor.leftX - 19.0f || x > g.floor.rightX + 19.0f);
        CHECK(x > g.blast.left + 20.0f && x < g.blast.right - 20.0f);
        CHECK(y > g.blast.bottom + 35.0f && y < g.blast.top - 30.0f);
    }
}

TEST(pplus_recovery, multijump_fly_rising_guard_prevents_premature_roll)
{
    // All 6 multi-jump fighters use status 0x0D (Fly) for midair jumps.
    // While rising (vy > 0 or animFrame < 4), recovery must only steer toward stage
    // without spending another jump or rolling a special.
    static const int kMultiJumpers[] = {0x05, 0x16, 0x17, 0x1D, 0x20, 0x25};
    for (u32 i = 0; i < sizeof(kMultiJumpers) / sizeof(kMultiJumpers[0]); i++)
    {
        int kind = kMultiJumpers[i];
        for (u32 trial = 0; trial < 32; trial++)
        {
            RecoveryState st;
            recoveryStateReset(st);
            u32 rng = 0xA100u + static_cast<u32>(kind) * 53u + trial * 17u;
            RecoveryInput in = offstageInput(kind, 105.0f, -20.0f, true);
            in.status = 0x0D; // Fly
            in.numJumps = 3;
            in.vy = 1.4f;
            in.animFrame = 6.0f;
            RecoveryOutput out;
            stepRecovery(in, st, rng, out);
            CHECK(out.active);
            CHECK_EQ(0u, out.buttons);
            CHECK(out.stickX < -0.5f);
            CHECK_EQ(static_cast<int>(kOptNone), static_cast<int>(st.option));

            // Early takeoff frames (animFrame < 4) are also guarded even if vy <= 0.
            in.vy = 0.0f;
            in.animFrame = 2.0f;
            stepRecovery(in, st, rng, out);
            CHECK_EQ(0u, out.buttons);
            CHECK(out.stickX < -0.5f);
        }

        // Under stage ceiling while rising in Fly: steer outward without pressing buttons.
        RecoveryState stCeil;
        recoveryStateReset(stCeil);
        u32 rngCeil = 77u + static_cast<u32>(kind);
        RecoveryInput inCeil = offstageInput(kind, 55.0f, -20.0f, true);
        inCeil.status = 0x0D;
        inCeil.numJumps = 2;
        inCeil.vy = 1.2f;
        inCeil.animFrame = 5.0f;
        inCeil.ceilingAbove = true;
        RecoveryOutput outCeil;
        stepRecovery(inCeil, stCeil, rngCeil, outCeil);
        CHECK_EQ(0u, outCeil.buttons);
        CHECK(outCeil.stickX > 0.5f);
    }

    // Once descending past frame 4 in Fly, rolling another jump is allowed again.
    bool sawJump = false;
    for (u32 trial = 0; trial < 32; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x5555u + trial * 29u;
        RecoveryInput in = offstageInput(0x05, 105.0f, -25.0f, true);
        in.status = 0x0D;
        in.numJumps = 3;
        in.vy = -1.2f;
        in.animFrame = 14.0f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptJump && (out.buttons & kRecBtnJump) != 0)
        {
            sawJump = true;
        }
    }
    CHECK(sawJump);
}

TEST(pplus_recovery, glide_dispatches_per_fighter_special_input)
{
    // Meta Knight (0x16): kOptGlide -> Up-B (Special + Up).
    bool sawMkGlide = false;
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x1001u + trial * 97u;
        RecoveryInput in = offstageInput(0x16, 115.0f, -10.0f, false);
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptGlide)
        {
            sawMkGlide = true;
            CHECK((out.buttons & kRecBtnSpecial) != 0);
            CHECK(fabsf(out.stickX) < 0.01f);
            CHECK(out.stickY > 0.9f);
        }
    }
    CHECK(sawMkGlide);

    // Pit (0x17): kOptGlide -> Side-B toward ledge (Special + towardLedge, stickY == 0), both sides.
    bool sawPitRight = false;
    bool sawPitLeft = false;
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x2002u + trial * 83u;
        RecoveryInput inR = offstageInput(0x17, 115.0f, -10.0f, false);
        RecoveryOutput outR;
        stepRecovery(inR, st, rng, outR);
        if (st.option == kOptGlide)
        {
            sawPitRight = true;
            CHECK((outR.buttons & kRecBtnSpecial) != 0);
            CHECK(outR.stickX < -0.9f);
            CHECK(fabsf(outR.stickY) < 0.01f);
        }

        recoveryStateReset(st);
        rng = 0x3003u + trial * 83u;
        RecoveryInput inL = offstageInput(0x17, -115.0f, -10.0f, false);
        RecoveryOutput outL;
        stepRecovery(inL, st, rng, outL);
        if (st.option == kOptGlide)
        {
            sawPitLeft = true;
            CHECK((outL.buttons & kRecBtnSpecial) != 0);
            CHECK(outL.stickX > 0.9f);
            CHECK(fabsf(outL.stickY) < 0.01f);
        }
    }
    CHECK(sawPitRight);
    CHECK(sawPitLeft);

    // Charizard (0x1D): kOptGlide -> Down-B (Special + Down).
    bool sawZardGlide = false;
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x4004u + trial * 71u;
        RecoveryInput in = offstageInput(0x1D, 115.0f, -10.0f, false);
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptGlide)
        {
            sawZardGlide = true;
            CHECK((out.buttons & kRecBtnSpecial) != 0);
            CHECK(fabsf(out.stickX) < 0.01f);
            CHECK(out.stickY < -0.9f);
        }
    }
    CHECK(sawZardGlide);
}

TEST(pplus_recovery, jigglypuff_picks_pound_never_sing_and_angles_pound_up)
{
    // 1. Normal recovery (including right next to the ledge where old inUpB fired Sing):
    // Jigglypuff (0x25, upBY == 0) must pick Side-B (Pound) and never Up-B (Sing).
    static const float kTestXs[] = {76.0f, 95.0f, 120.0f};
    static const float kTestYs[] = {-5.0f, -10.0f, -25.0f};
    bool sawNormalPound = false;
    bool sawAnyUpB = false;
    for (u32 xi = 0; xi < 3; xi++)
    {
        for (u32 yi = 0; yi < 3; yi++)
        {
            for (u32 trial = 0; trial < 32; trial++)
            {
                RecoveryState st;
                recoveryStateReset(st);
                u32 rng = 0x6000u + xi * 997u + yi * 313u + trial * 37u;
                RecoveryInput in = offstageInput(0x25, kTestXs[xi], kTestYs[yi], false);
                RecoveryOutput out;
                stepRecovery(in, st, rng, out);
                if (st.option == kOptUpB || st.option == kOptUpBHigh || st.option == kOptUpBCancel)
                {
                    sawAnyUpB = true;
                }
                if (st.option == kOptSideB)
                {
                    sawNormalPound = true;
                    CHECK((out.buttons & kRecBtnSpecial) != 0);
                    CHECK(out.stickX < -0.9f);
                    CHECK(fabsf(out.stickY) < 0.01f);
                }
            }
        }
    }
    CHECK(sawNormalPound);
    CHECK(!sawAnyUpB);

    // 2. Emergency fallback (deep offstage / near bottom blastzone, no jumps):
    // must deterministically pick Side-B (Pound), never Up-B (Sing).
    for (u32 trial = 0; trial < 16; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x7000u + trial * 41u;
        RecoveryInput in = offstageInput(0x25, 95.0f, -120.0f, false);
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK_EQ(static_cast<int>(kOptSideB), static_cast<int>(st.option));
        CHECK((out.buttons & kRecBtnSpecial) != 0);
        CHECK(out.stickX < -0.9f);
        CHECK(fabsf(out.stickY) < 0.01f);
    }

    // 3. Active Jigglypuff Side-B (status 0x113): holds stickY = 1.0f (angled-up Pound)
    // while drifting toward the ledge on both sides of the stage.
    RecoveryState stAct;
    recoveryStateReset(stAct);
    u32 rngAct = 123u;
    RecoveryOutput outAct;

    RecoveryInput inRight = offstageInput(0x25, 105.0f, -20.0f, false);
    inRight.status = 0x113;
    inRight.animFrame = 8.0f;
    stepRecovery(inRight, stAct, rngAct, outAct);
    CHECK(outAct.active);
    CHECK(outAct.stickX < -0.9f);
    CHECK(outAct.stickY > 0.9f);

    RecoveryInput inLeft = offstageInput(0x25, -105.0f, -20.0f, false);
    inLeft.status = 0x113;
    inLeft.animFrame = 8.0f;
    stepRecovery(inLeft, stAct, rngAct, outAct);
    CHECK(outAct.active);
    CHECK(outAct.stickX > 0.9f);
    CHECK(outAct.stickY > 0.9f);
}

TEST(pplus_recovery, over_stage_outward_aim_and_special_fall_steer_inward)
{
    u32 rng = 0x2468u;
    RecoveryState st;
    RecoveryOutput out;

    // 1. Mewtwo in Special Fall (0x10) over FD right half (x = 70, edge = 85.5):
    // must steer INWARD toward stage center (stickX < 0), never outward toward right ledge.
    recoveryStateReset(st);
    RecoveryInput m2Fall = offstageInput(0x26, 70.0f, 52.0f, false);
    m2Fall.stageLeftX = -85.5f;
    m2Fall.stageRightX = 85.5f;
    m2Fall.ledgeX = 85.5f;
    m2Fall.floorBelow = true;
    m2Fall.status = 0x10;
    stepRecovery(m2Fall, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX < -0.5f);

    // 2. Mewtwo active Up-B (0x114 / 0x11B) over stage surface (x = 79, edge = 85.5):
    // must aim INWARD onto stage (stickX < 0), never outward offstage.
    recoveryStateReset(st);
    RecoveryInput m2UpB = m2Fall;
    m2UpB.x = 79.0f;
    m2UpB.y = 11.0f;
    m2UpB.status = 0x11B;
    stepRecovery(m2UpB, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX < -0.5f);

    // 3. Fox active Fire Fox (0x114 / 0x117) over Battlefield (x = 60, edge = 68.4):
    // must aim INWARD onto BF (stickX < 0), never outward offstage.
    recoveryStateReset(st);
    RecoveryInput foxUpB = offstageInput(0x06, 60.0f, 8.0f, false);
    foxUpB.stageLeftX = -68.4f;
    foxUpB.stageRightX = 68.4f;
    foxUpB.ledgeX = 68.4f;
    foxUpB.floorBelow = true;
    foxUpB.status = 0x117;
    stepRecovery(foxUpB, st, rng, out);
    CHECK(out.active);
    CHECK(out.stickX < -0.5f);

    // 4. Over stage surface with floor below (near lip x = 84 on FD or x = 67 on BF,
    // including Mewtwo hover-exit 0x11E): never roll a recovery special; drift inward or exit.
    for (u32 trial = 0; trial < 32; trial++)
    {
        recoveryStateReset(st);
        u32 trng = 0x3000u + trial * 59u;
        RecoveryInput m2Lip = m2Fall;
        m2Lip.x = 84.0f;
        m2Lip.y = 12.0f;
        m2Lip.status = 0x11E;
        stepRecovery(m2Lip, st, trng, out);
        CHECK_EQ(0u, out.buttons);
        if (out.active)
        {
            CHECK(out.stickX < -0.5f);
        }

        recoveryStateReset(st);
        RecoveryInput foxLip = foxUpB;
        foxLip.x = 67.0f;
        foxLip.y = 14.0f;
        foxLip.status = 0x0E;
        stepRecovery(foxLip, st, trng, out);
        CHECK_EQ(0u, out.buttons);
        if (out.active)
        {
            CHECK(out.stickX < -0.5f);
        }
    }
}

TEST(pplus_recovery, freefall_side_b_blocked_below_ledge_and_no_midair_offstage_shorten)
{
    // 1. Fox 26u below ledge (FD low start): must never roll freefall Side-B.
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x4100u + trial * 31u;
        RecoveryInput in = offstageInput(0x06, 99.0f, -26.0f, true);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK(st.option != kOptSideB && st.option != kOptSideBShort);
    }

    // 2. Ganondorf 27u below ledge (FD low start): must never roll freefall Side-B.
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x4200u + trial * 37u;
        RecoveryInput in = offstageInput(0x14, 98.0f, -27.0f, true);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK(st.option != kOptSideB);
    }

    // 3. Fox far offstage (absNCX = 42, y = 3.5): never rolls kOptSideBShort,
    // and full Illusion (kOptSideB) never prematurely shortens midair offstage at absNCX = 14.5.
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x4300u + trial * 43u;
        RecoveryInput in = offstageInput(0x06, 127.5f, 3.5f, false);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK(st.option != kOptSideBShort);
    }
    {
        RecoveryState st;
        recoveryStateReset(st);
        st.initialized = 1;
        st.option = kOptSideB;
        u32 rng = 99u;
        RecoveryInput midIllusion = offstageInput(0x06, 100.0f, 3.0f, false);
        midIllusion.stageLeftX = -85.5f;
        midIllusion.stageRightX = 85.5f;
        midIllusion.ledgeX = 85.5f;
        midIllusion.status = 0x113;
        midIllusion.animFrame = 18.0f;
        midIllusion.floorBelow = false;
        RecoveryOutput out;
        stepRecovery(midIllusion, st, rng, out);
        CHECK_EQ(0u, out.buttons & kRecBtnSpecial);
    }
}

TEST(pplus_recovery, ness_pk_thunder_arcs_outward_and_never_aims_downward)
{
    RecoveryState st;
    RecoveryOutput out;
    u32 rng = 0x5100u;

    // 1. Ness above ledge and close (91, 13 on FD): skip PK Thunder so he drifts onto stage.
    for (u32 trial = 0; trial < 32; trial++)
    {
        recoveryStateReset(st);
        u32 trng = 0x5200u + trial * 23u;
        RecoveryInput in = offstageInput(0x0A, 91.0f, 13.0f, false);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        stepRecovery(in, st, trng, out);
        CHECK(st.option != kOptUpB);
    }

    // 2. Startup before PKT spawns (!hasPkt): steer OUTWARD (right on right side) so PKT arcs away from stage.
    recoveryStateReset(st);
    RecoveryInput start = offstageInput(0x0A, 76.0f, -58.0f, false);
    start.stageLeftX = -68.4f;
    start.stageRightX = 68.4f;
    start.ledgeX = 68.4f;
    start.status = 0x114;
    start.hasPkt = false;
    stepRecovery(start, st, rng, out);
    CHECK(out.stickX > 0.5f);

    // 3. PKT spawned above Ness at BF under-lip (76, -58): must steer OUTWARD-DOWN (stickX > 0.5, stickY < 0).
    RecoveryInput active = start;
    active.status = 0x11A;
    active.hasPkt = true;
    active.pktX = 76.0f;
    active.pktY = -42.0f;
    active.pktVx = 0.0f;
    active.pktVy = 2.5f;
    stepRecovery(active, st, rng, out);
    CHECK(out.stickX > 0.5f);
    CHECK(out.stickY < -0.1f);

    // 4. Even if active PKT runs while Ness is above ledge (110, 4 on BF) and PKT has swung outward,
    // target must remain BELOW Ness (stickY < 0) so PKT2 launches upward, never downward.
    recoveryStateReset(st);
    RecoveryInput highPkt = offstageInput(0x0A, 110.0f, 4.0f, false);
    highPkt.stageLeftX = -68.4f;
    highPkt.stageRightX = 68.4f;
    highPkt.ledgeX = 68.4f;
    highPkt.status = 0x11A;
    highPkt.hasPkt = true;
    highPkt.pktX = 128.0f; // already outward of Ness
    highPkt.pktY = 12.0f;
    highPkt.pktVx = 0.5f;
    highPkt.pktVy = -1.0f;
    stepRecovery(highPkt, st, rng, out);
    CHECK(out.stickY < -0.2f);

    // 5. Post-launch PKT2 end lag (0x11B with !hasPkt): must drift INWARD toward stage, never outward!
    RecoveryInput pktEnd = offstageInput(0x0A, 90.4f, 30.1f, false);
    pktEnd.stageLeftX = -85.5f;
    pktEnd.stageRightX = 85.5f;
    pktEnd.ledgeX = 85.5f;
    pktEnd.status = 0x11B;
    pktEnd.hasPkt = false;
    stepRecovery(pktEnd, st, rng, out);
    CHECK(out.stickX < -0.5f);
}

TEST(pplus_recovery, ivysaur_tether_requires_in_range_and_below_ledge)
{
    // 1. Ivysaur far and above ledge (118, 10 on FD, dx = 32.5, jump intact):
    // must NEVER fire Up-B Vine Whip into empty sky.
    for (u32 trial = 0; trial < 64; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x6100u + trial * 47u;
        RecoveryInput in = offstageInput(0x1F, 118.0f, 10.0f, true);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        CHECK(st.option != kOptUpB);
    }

    // 2. Ivysaur within whip range and below ledge (105, -30 on FD, dx = 19.5, no jump):
    // DOES fire Up-B Vine Whip.
    bool sawVineWhip = false;
    for (u32 trial = 0; trial < 32; trial++)
    {
        RecoveryState st;
        recoveryStateReset(st);
        u32 rng = 0x6200u + trial * 47u;
        RecoveryInput in = offstageInput(0x1F, 105.0f, -30.0f, false);
        in.stageLeftX = -85.5f;
        in.stageRightX = 85.5f;
        in.ledgeX = 85.5f;
        RecoveryOutput out;
        stepRecovery(in, st, rng, out);
        if (st.option == kOptUpB && (out.buttons & kRecBtnSpecial) != 0 && out.stickY > 0.5f)
        {
            sawVineWhip = true;
        }
    }
    CHECK(sawVineWhip);
}

TEST(pplus_recovery, recovery_mixup_profiles_and_tumble_dj_wall_underside_guards)
{
    u32 rng = 0x8100u;
    RecoveryState st;
    RecoveryOutput out;

    // 1. Recovery Mixup profiles (Low vs High vs Direct) steer Fox 360-aim Up-B distinctly.
    recoveryStateReset(st);
    RecoveryInput foxLow = offstageInput(0x06, 105.0f, -25.0f, false);
    foxLow.status = 0x114;
    foxLow.recoveryMixup = 1; // Low (Sweetspot)
    stepRecovery(foxLow, st, rng, out);
    float lowY = out.stickY;

    recoveryStateReset(st);
    RecoveryInput foxHigh = foxLow;
    foxHigh.recoveryMixup = 2; // High
    stepRecovery(foxHigh, st, rng, out);
    float highY = out.stickY;
    CHECK(highY > lowY);

    // Low mixup never rolls kOptUpBHigh; High mixup sets highMixup > 0.
    for (u32 trial = 0; trial < 32; trial++)
    {
        recoveryStateReset(st);
        u32 trng = 0x8200u + trial * 37u;
        RecoveryInput inL = offstageInput(0x06, 100.0f, -20.0f, false);
        inL.recoveryMixup = 1;
        stepRecovery(inL, st, trng, out);
        CHECK(st.option != kOptUpBHigh);
        CHECK_EQ(0, static_cast<int>(st.highMixup));

        recoveryStateReset(st);
        RecoveryInput inH = inL;
        inH.recoveryMixup = 2;
        stepRecovery(inH, st, trng, out);
        CHECK(st.highMixup > 0);
    }

    // 2. Tumble (Damage_Fall 0x49) wiggles stick X across opposite directions to exit tumble.
    recoveryStateReset(st);
    RecoveryInput tumble = offstageInput(0x11, 130.0f, 25.0f, true);
    tumble.status = 0x49;
    tumble.vy = 0.5f; // rising -> preserves DJ, so drifts & wiggles out of tumble
    stepRecovery(tumble, st, rng, out);
    float firstX = out.stickX;
    stepRecovery(tumble, st, rng, out);
    float secondX = out.stickX;
    CHECK(firstX * secondX < -0.5f);

    // 3. Double jump preservation while rising or well above ledge near stage.
    for (u32 trial = 0; trial < 32; trial++)
    {
        recoveryStateReset(st);
        u32 trng = 0x8300u + trial * 41u;
        RecoveryInput rising = offstageInput(0x11, 100.0f, 15.0f, true);
        rising.vy = 1.2f;
        stepRecovery(rising, st, trng, out);
        CHECK(st.option != kOptJump);

        recoveryStateReset(st);
        RecoveryInput highNear = offstageInput(0x0E, 105.0f, 25.0f, true);
        highNear.vy = -0.3f;
        stepRecovery(highNear, st, trng, out);
        CHECK(st.option != kOptJump);
    }

    // 4. Wall-clearance aiming for Sheik/Fox 360-UpB and wall guard for Falco/Falcon Side-B.
    recoveryStateReset(st);
    RecoveryInput sheikWall = offstageInput(0x0E, 80.0f, -30.0f, false);
    sheikWall.status = 0x114;
    sheikWall.wallAhead = true;
    sheikWall.wallDist = 8.0f;
    stepRecovery(sheikWall, st, rng, out);
    CHECK(out.stickY > 0.85f); // aims steeply up along the wall to the ledge

    for (u32 trial = 0; trial < 32; trial++)
    {
        recoveryStateReset(st);
        u32 trng = 0x8400u + trial * 29u;
        RecoveryInput falcoWall = offstageInput(0x13, 86.0f, -6.0f, false);
        falcoWall.wallAhead = true;
        falcoWall.wallDist = 10.0f;
        stepRecovery(falcoWall, st, trng, out);
        CHECK(st.option != kOptSideB && st.option != kOptSideBShort);

        // Falcon Raptor Boost blocked below ledge.
        recoveryStateReset(st);
        RecoveryInput falconLow = offstageInput(0x09, 95.0f, -2.0f, false);
        stepRecovery(falconLow, st, trng, out);
        CHECK(st.option != kOptSideB);

        // Mario Cape blocked deep below ledge.
        recoveryStateReset(st);
        RecoveryInput marioDeep = offstageInput(0x00, 100.0f, -28.0f, false);
        marioDeep.vy = -1.0f;
        stepRecovery(marioDeep, st, trng, out);
        CHECK(st.option != kOptSideB);

        // Meta Knight with 3 jumps left saves freefall specials near ledge height.
        recoveryStateReset(st);
        RecoveryInput mkJumps = offstageInput(0x16, 100.0f, -10.0f, true);
        mkJumps.numJumps = 3;
        stepRecovery(mkJumps, st, trng, out);
        CHECK(st.option != kOptGlide && st.option != kOptUpB && st.option != kOptSideB);
    }

    // 5. Underside ceiling emergency: never fires Up-B straight up into solid stage underside.
    recoveryStateReset(st);
    RecoveryInput underEmerg = offstageInput(0x06, 50.0f, -125.0f, false);
    underEmerg.ceilingAbove = true;
    stepRecovery(underEmerg, st, rng, out);
    CHECK(out.stickX > 0.5f); // drifts outward from under right half
    CHECK_EQ(0u, out.buttons & kRecBtnSpecial);
}
