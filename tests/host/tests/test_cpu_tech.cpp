// Pure CPU tech roll stick and slip tech/get-up helpers (src/main/cpu/tech_logic.h).
#include "host/test.h"
#include "main/cpu/tech_logic.h"

using namespace tm::cpu;

TEST(cpu_tech, roll_stick_waits_until_landing_window_and_never_wiggles)
{
    const float roll = 0.2f;
    const float wiggle = 0.75f;
    const float mag = 0.5f * (roll + wiggle);

    // Airborne far from landing (Earliest=15 or Latest=5): stay neutral so no air drift.
    for (int n = 2; n <= 15; n++)
    {
        CHECK_NEAR(0.0f, techRollStickX(kTechAway, 1.0f, n, true, 0, true, roll, wiggle), 1e-6f);
        CHECK_NEAR(0.0f, techRollStickX(kTechTowards, 1.0f, n, true, 0, true, roll, wiggle), 1e-6f);
    }
    // No ground predicted (e.g. wall tech): stay neutral.
    CHECK_NEAR(0.0f, techRollStickX(kTechAway, 1.0f, -1, true, 0, true, roll, wiggle), 1e-6f);
    // Guard not yet pressed or In Place / None: stay neutral even on landing frame.
    CHECK_NEAR(0.0f, techRollStickX(kTechAway, 1.0f, 0, false, 0, true, roll, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, techRollStickX(kTechInPlace, 1.0f, 0, true, 0, true, roll, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, techRollStickX(kTechNone, 1.0f, 0, true, 0, true, roll, wiggle), 1e-6f);
    // Hitlag: never overwrite ASDI/SDI stick.
    CHECK_NEAR(0.0f, techRollStickX(kTechAway, 1.0f, 0, true, 1, true, roll, wiggle), 1e-6f);

    // Within landing window (0..1 frames) outside hitlag: roll in chosen direction,
    // magnitude strictly between roll (0.2) and wiggle (0.75).
    CHECK_NEAR(-mag, techRollStickX(kTechAway, 1.0f, 1, true, 0, true, roll, wiggle), 1e-6f);
    CHECK_NEAR(-mag, techRollStickX(kTechAway, 1.0f, 0, true, 0, true, roll, wiggle), 1e-6f);
    CHECK_NEAR(mag, techRollStickX(kTechTowards, 1.0f, 0, true, 0, true, roll, wiggle), 1e-6f);
    CHECK_NEAR(mag, techRollStickX(kTechAway, -1.0f, 0, true, 0, true, roll, wiggle), 1e-6f);
    CHECK(mag > roll && mag < wiggle);

    // Low spike landing during kDiHold (allowStick=false): frame before landing
    // preserves TDI, landing frame (n==0) applies tech roll stick.
    CHECK_NEAR(0.0f, techRollStickX(kTechAway, 1.0f, 1, true, 0, false, roll, wiggle), 1e-6f);
    CHECK_NEAR(-mag, techRollStickX(kTechAway, 1.0f, 0, true, 0, false, roll, wiggle), 1e-6f);
}

TEST(cpu_tech, slip_tech_countdown_matches_pplus_frame_20_threshold)
{
    // P+ MoveDef/Items.asm:10-39 enables Passive/Passive_Fb when IC_Basic[0] >= 20.0.
    CHECK_EQ(-1, framesToSlipTech(10.0f, 0.0f));
    CHECK_EQ(-1, framesToSlipTech(10.0f, -1.0f));
    CHECK_EQ(19, framesToSlipTech(0.0f, 1.0f));
    CHECK_EQ(15, framesToSlipTech(4.0f, 1.0f));  // Earliest (lead 15) presses guard here
    CHECK_EQ(5, framesToSlipTech(14.0f, 1.0f));  // Latest (lead 5) presses guard here
    CHECK_EQ(1, framesToSlipTech(18.0f, 1.0f));  // Roll stick window starts here
    CHECK_EQ(0, framesToSlipTech(19.0f, 1.0f));  // Coming frame plays 20.0 (tech fires)
    CHECK_EQ(0, framesToSlipTech(20.0f, 1.0f));  // Past 20.0: still techable if lockout clears
    CHECK_EQ(0, framesToSlipTech(30.0f, 1.0f));
    // Faster animation speed scales countdown cleanly.
    CHECK_EQ(9, framesToSlipTech(0.0f, 2.0f));
    CHECK_EQ(0, framesToSlipTech(18.0f, 2.0f));
}

TEST(cpu_tech, slip_and_down_status_classification)
{
    // Techable slips: Slip (0x89) and Slip_Down (0x8A) only.
    CHECK(isSlipTechStatus(0x89));
    CHECK(isSlipTechStatus(0x8A));
    CHECK(!isSlipTechStatus(0x8B));
    CHECK(!isSlipTechStatus(0x8C));
    CHECK(!isSlipTechStatus(0x8D));

    // Down / slip recovery statuses (0x4A..0x53 and 0x8B..0x91).
    CHECK(!isDownStatus(0x89));
    CHECK(!isDownStatus(0x8A));
    for (int s = 0x4A; s <= 0x53; s++)
    {
        CHECK(isDownStatus(s));
    }
    for (int s = 0x8B; s <= 0x91; s++)
    {
        CHECK(isDownStatus(s));
    }
    CHECK(!isDownStatus(0x92));

    // Get-up wait statuses accepting Stand / Roll / Attack inputs.
    CHECK(isGetupWaitStatus(0x4D)); // Down_Wait
    CHECK(isGetupWaitStatus(0x4E)); // Down_Wait_Continue
    CHECK(isGetupWaitStatus(0x50)); // Lay_Down
    CHECK(isGetupWaitStatus(0x8D)); // Slip_Wait
    CHECK(!isGetupWaitStatus(0x89));
    CHECK(!isGetupWaitStatus(0x8A));
    CHECK(!isGetupWaitStatus(0x8E));
}

TEST(cpu_tech, downward_ground_ray_has_zero_horizontal_lean)
{
    float dx = 99.0f;
    float dy = 99.0f;
    groundRayDir(400.0f, dx, dy);
    CHECK_NEAR(0.0f, dx, 1e-6f);
    CHECK_NEAR(-400.0f, dy, 1e-6f);

    // At any near-edge horizontal position and height, ray (start + t * dir)
    // stays vertically aligned at startX with zero outward lean.
    const float startX = 85.0f;
    const float startY = 30.0f;
    const float floorY = 0.0f;
    const float t = (floorY - startY) / dy;
    CHECK_NEAR(startX, startX + t * dx, 1e-6f);
}

TEST(cpu_tech, multijump_fly_counts_as_jumped_and_started)
{
    CHECK(isJumpStatus(tm::game::kStatusJump));
    CHECK(isJumpStatus(tm::game::kStatusJumpAerial));
    CHECK(isJumpStatus(tm::game::kStatusFly));
    CHECK(!isJumpStatus(tm::game::kStatusJumpSquat));
    CHECK(!isJumpStatus(tm::game::kStatusFall));
    CHECK(!isJumpStatus(tm::game::kStatusFallAerial));
    CHECK(!isJumpStatus(tm::game::kStatusWait));

    // Multi-jumper Fly (0x0D) must be recognized as started alongside Jump and
    // JumpAerial for both air/ground and shield counters.
    CHECK(counterActionStarted(tm::game::kStatusFly, false));
    CHECK(counterActionStarted(tm::game::kStatusFly, true));
    CHECK(counterActionStarted(tm::game::kStatusJumpAerial, false));
    CHECK(counterActionStarted(tm::game::kStatusJump, false));
    CHECK(counterActionStarted(tm::game::kStatusJumpSquat, false));
    CHECK(counterActionStarted(tm::game::kStatusDash, false));
    CHECK(counterActionStarted(tm::game::kStatusEscapeAir, false));
    CHECK(counterActionStarted(tm::game::kStatusAttackAirFirst, false));
    CHECK(counterActionStarted(tm::game::kStatusLandingFallSpecial, false));

    // Waiting, falling, landing, guard-off, and hit statuses remain not started.
    CHECK(!counterActionStarted(tm::game::kStatusWait, false));
    CHECK(!counterActionStarted(tm::game::kStatusFall, false));
    CHECK(!counterActionStarted(tm::game::kStatusFallAerial, false));
    CHECK(!counterActionStarted(tm::game::kStatusLanding, false));
    CHECK(!counterActionStarted(tm::game::kStatusGuardOff, false));
    CHECK(!counterActionStarted(tm::game::kStatusDamage, false));
    CHECK(!counterActionStarted(0x49, false)); // Damage_Fall
    CHECK(!counterActionStarted(tm::game::kStatusGuard, true));
    CHECK(counterActionStarted(tm::game::kStatusGuard, false));
}

TEST(cpu_tech, pre_tech_drift_modes_and_safety_guards)
{
    const float wiggle = 0.75f;
    // Off stays neutral everywhere.
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftOff, kTechAway, 1.0f, 2, 8, 0, true, wiggle), 1e-6f);
    // Hitlag and !allowStick (TDI hold) stay neutral.
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftIn, kTechAway, 1.0f, 0, 8, 1, true, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftIn, kTechAway, 1.0f, 0, 8, 0, false, wiggle), 1e-6f);
    // Landing window (0..kTechRollLead) defers to techRollStickX.
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftIn, kTechAway, 1.0f, 0, 0, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftIn, kTechAway, 1.0f, 0, 1, 0, true, wiggle), 1e-6f);

    // In / Out drift toward or away from opponent, strictly below wiggle threshold.
    float inX = preTechDriftStickX(kDriftIn, kTechNone, 1.0f, 0, 10, 0, true, wiggle);
    float outX = preTechDriftStickX(kDriftOut, kTechNone, 1.0f, 0, -1, 0, true, wiggle);
    CHECK(inX > 0.0f && inX < wiggle);
    CHECK(outX < 0.0f && -outX < wiggle);
    CHECK_NEAR(-inX, outX, 1e-6f);
    CHECK_NEAR(-inX, preTechDriftStickX(kDriftIn, kTechNone, -1.0f, 0, 10, 0, true, wiggle), 1e-6f);

    // Random cycles Out (0), Neutral (1), In (2).
    CHECK_NEAR(-inX, preTechDriftStickX(kDriftRandom, kTechInPlace, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftRandom, kTechInPlace, 1.0f, 1, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(inX, preTechDriftStickX(kDriftRandom, kTechInPlace, 1.0f, 2, 8, 0, true, wiggle), 1e-6f);

    // Toward Tech / Away From Tech follow roll direction; neutral for In Place / Miss.
    CHECK_NEAR(-inX, preTechDriftStickX(kDriftTowardTech, kTechAway, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(inX, preTechDriftStickX(kDriftAwayTech, kTechAway, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(inX, preTechDriftStickX(kDriftTowardTech, kTechTowards, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(-inX, preTechDriftStickX(kDriftAwayTech, kTechTowards, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftTowardTech, kTechInPlace, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
    CHECK_NEAR(0.0f, preTechDriftStickX(kDriftAwayTech, kTechNone, 1.0f, 0, 8, 0, true, wiggle), 1e-6f);
}
