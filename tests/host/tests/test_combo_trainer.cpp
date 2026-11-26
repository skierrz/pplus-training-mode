// Host unit tests for the clean-room Combo Trainer core
// (src/extra/combo_trainer_core.h): 1-frame Correct_None -> Correct_Air
// restore, comboTimerAdjustment, recoveryTrainer loop, and 7-meter ratios.
#include <math.h>
#include <string.h>

#include "extra/combo_trainer_core.h"
#include "host/test.h"

using namespace tm;
using namespace tm::extra;

namespace {
    main::ComboTrainerSettings defaultCfg()
    {
        main::ComboTrainerSettings c;
        memset(&c, 0, sizeof(c));
        c.enabled = 1;
        c.enabledMeters = main::kMeterAll;
        return c;
    }

    ComboStepInput baseInput()
    {
        ComboStepInput in;
        memset(&in, 0, sizeof(in));
        in.status = 0x00;
        in.x = 20.0f;
        in.y = 0.0f;
        return in;
    }
}

TEST(combo_trainer, noclip_sets_correct_none_on_reset_and_restores_grounded_correct_next_frame)
{
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.fixPosition = 1;
    cfg.noclip = 1;
    cfg.fixedDamage = 42;

    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;

    // Frame 1: idle captures anchor.
    ComboStepInput in = baseInput();
    stepComboTrainer(cfg, in, st, act);
    CHECK(st.anchorValid);
    CHECK(!act.doWarp);
    CHECK(!act.setCorrectNone);
    CHECK(!act.restoreCorrect);

    // Frame 2: fighter enters hitstun.
    in.status = 0x45;
    in.hitstun = 10;
    in.air = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(st.wasInHitstun);
    CHECK(!act.doWarp);

    // Frame 3: hitstun drops to 0 -> triggers grounded warp + Correct_None + arms restoreCorrect.
    in.status = 0x0E;
    in.hitstun = 0;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK(!act.warpFalling);
    CHECK(act.setCorrectNone);
    CHECK(!act.restoreCorrect);
    CHECK(act.applyFixedDamage);
    CHECK(st.restoreCorrect);
    CHECK_EQ(static_cast<int>(kCorrectGroundCliffStop), static_cast<int>(st.restoreCorrectKind));
    CHECK_EQ(static_cast<int>(kCorrectNone), static_cast<int>(st.lastCorrectWritten));
    CHECK_EQ(1u, st.resetCount);

    // Frame 4: explicit 1-frame restoreCorrect fires Correct_Ground_Cliff_Stop (2) for grounded warp!
    stepComboTrainer(cfg, in, st, act);
    CHECK(!act.doWarp);
    CHECK(!act.setCorrectNone);
    CHECK(act.restoreCorrect);
    CHECK_EQ(static_cast<int>(kCorrectGroundCliffStop), static_cast<int>(act.restoreCorrectKind));
    CHECK(!st.restoreCorrect);
    CHECK_EQ(static_cast<int>(kCorrectGroundCliffStop), static_cast<int>(st.lastCorrectWritten));

    // Frame 5: steady state, neither setCorrectNone nor restoreCorrect fires.
    stepComboTrainer(cfg, in, st, act);
    CHECK(!act.setCorrectNone);
    CHECK(!act.restoreCorrect);
}

TEST(combo_trainer, combo_timer_adjustment_negative_and_positive)
{
    // Negative adjustment (-5): resets 5 frames BEFORE hitstun hits 0.
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.fixPosition = 1;
    cfg.comboTimerAdjustment = -5;

    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;
    ComboStepInput in = baseInput();
    in.status = 0x45;
    in.hitstun = 10;
    stepComboTrainer(cfg, in, st, act); // eff = 5 -> armed
    CHECK(st.wasInHitstun);
    CHECK(!act.doWarp);

    in.hitstun = 5; // eff = 0 -> triggers reset early!
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(1u, st.resetCount);

    // Positive adjustment (+3): delays reset 3 frames AFTER hitstun drops to 0.
    cfg.comboTimerAdjustment = 3;
    comboSlotReset(st);
    in.hitstun = 1;
    stepComboTrainer(cfg, in, st, act); // eff = 4 -> comboTimer = 4
    CHECK(!act.doWarp);

    in.hitstun = 0;
    in.status = 0x0E;
    for (int f = 0; f < 3; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act); // 4th frame: timer reaches 0 -> warps!
    CHECK(act.doWarp);
    CHECK_EQ(1u, st.resetCount);
}

TEST(combo_trainer, recovery_trainer_loops_offstage_and_restores_correct)
{
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.recoveryTrainer = 1;
    cfg.noclip = 1;
    cfg.randomizeDamage = 1;

    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;

    // Grounded on stage initially -> warps offstage immediately.
    ComboStepInput in = baseInput();
    in.landedSafeOnStage = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK(act.warpFalling);
    CHECK(act.pickOffstagePos);
    CHECK(act.pickRandomDamage);
    CHECK(act.setCorrectNone);
    CHECK_EQ(1u, st.recoveryResets);

    // Next frame airborne offstage -> restores Correct_Air (5) and does not warp.
    in.landedSafeOnStage = false;
    in.air = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.restoreCorrect);
    CHECK_EQ(static_cast<int>(kCorrectAir), static_cast<int>(act.restoreCorrectKind));
    CHECK_EQ(static_cast<int>(kCorrectAir), static_cast<int>(st.lastCorrectWritten));
    CHECK(!act.doWarp);

    // Drops below reset threshold -> warps offstage again.
    in.belowRecoveryReset = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK(act.pickOffstagePos);
    CHECK_EQ(2u, st.recoveryResets);
}

TEST(combo_trainer, seven_floating_meters_compute_exact_ratios_and_mask)
{
    ComboSlotState st;
    comboSlotReset(st);
    ComboMeterValues mv;
    ComboStepInput in = baseInput();

    // 1. Hitlag + Hitstun + DI
    in.hitlag = 8;
    in.hitstun = 20;
    in.stickX = 0.75f;
    in.stickY = -0.50f;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterHitlag) != 0);
    CHECK((mv.activeMask & main::kMeterHitstun) != 0);
    CHECK((mv.activeMask & main::kMeterDi) != 0);
    CHECK(fabsf(mv.hitlagRatio - 1.0f) < 0.001f);
    CHECK(fabsf(mv.hitstunRatio - 1.0f) < 0.001f);
    CHECK(fabsf(mv.diX - 0.75f) < 0.001f);
    CHECK(fabsf(mv.diY + 0.50f) < 0.001f);

    // Halfway through hitlag & hitstun: ratios drop to 0.5 and DI stays latched.
    in.hitlag = 4;
    in.hitstun = 10;
    in.stickX = 0.0f;
    in.stickY = 0.0f;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK(fabsf(mv.hitlagRatio - 0.5f) < 0.001f);
    CHECK(fabsf(mv.hitstunRatio - 0.5f) < 0.001f);

    // 2. Shieldstun
    in.hitlag = 0;
    in.hitstun = 0;
    in.shieldStun = 12;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterShieldstun) != 0);
    CHECK(fabsf(mv.shieldStunRatio - 1.0f) < 0.001f);
    in.shieldStun = 6;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK(fabsf(mv.shieldStunRatio - 0.5f) < 0.001f);

    // 3. Time until IASA
    in.shieldStun = 0;
    in.framesUntilIasa = 16;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterIasa) != 0);
    CHECK(fabsf(mv.iasaRatio - 1.0f) < 0.001f);
    in.framesUntilIasa = 4;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK(fabsf(mv.iasaRatio - 0.25f) < 0.001f);

    // 4. Ledge intangibility (starts on ledge, persists after leaving ledge)
    in.framesUntilIasa = 0;
    in.onLedge = true;
    in.xluFrames = 30;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterLedgeIntang) != 0);
    CHECK((mv.activeMask & main::kMeterIntang) == 0);
    in.onLedge = false;
    in.xluFrames = 15;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterLedgeIntang) != 0);
    CHECK(fabsf(mv.ledgeIntangRatio - 0.5f) < 0.001f);

    // 5. General intangibility / invincibility (invFrames)
    in.xluFrames = 0;
    in.invFrames = 10;
    computeComboMeters(main::kMeterAll, in, st, mv);
    CHECK((mv.activeMask & main::kMeterIntang) != 0);
    CHECK(fabsf(mv.intangRatio - 1.0f) < 0.001f);

    // 6. Mask filtering: disabling a meter suppresses it cleanly.
    computeComboMeters(main::kMeterHitstun, in, st, mv);
    CHECK_EQ(0u, static_cast<unsigned>(mv.activeMask));
}

TEST(combo_trainer, post_hitstun_delay_frame_trap_cancel_ko_reset_and_reset_p1)
{
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.fixPosition = 1;
    cfg.resetP1 = 1;
    cfg.resetDelay = main::kComboDelay30; // 30f post-hitstun escape window

    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;
    ComboStepInput in = baseInput();

    // Frame 0: capture anchor.
    stepComboTrainer(cfg, in, st, act);
    CHECK(st.anchorValid);

    // Hit 1: enter hitstun.
    in.status = 0x45;
    in.hitstun = 10;
    stepComboTrainer(cfg, in, st, act);
    CHECK(st.wasInHitstun && !act.doWarp);

    // Hitstun ends: must NOT warp on frame 0; counts down 30f delay.
    in.status = 0x00;
    in.hitstun = 0;
    for (int f = 0; f < 10; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }

    // Frame-trap hit during the 30f window cancels the timer and continues the string!
    in.status = 0x45;
    in.hitstun = 8;
    stepComboTrainer(cfg, in, st, act);
    CHECK(!act.doWarp);
    CHECK(st.wasInHitstun);

    // Hitstun ends again: wait full 30f delay -> warps on frame 31 with resetP1 = true.
    in.status = 0x00;
    in.hitstun = 0;
    for (int f = 0; f < 30; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK(act.resetP1);
    CHECK_EQ(1u, st.resetCount);

    // KO (Rebirth 0xBE) bypasses the 30f delay and resets immediately!
    in.status = 0x45;
    in.hitstun = 12;
    stepComboTrainer(cfg, in, st, act);
    in.status = 0xBE; // Rebirth
    in.hitstun = 0;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK(act.resetP1);
    CHECK_EQ(2u, st.resetCount);

    // Yoshi tongue grab (0x107..0x10A) holds combo active without resetting.
    CHECK(isDownOrGrabStatus(0x107));
    CHECK(isDownOrGrabStatus(0x10A));
}

TEST(combo_trainer, edgeguard_landed_safe_rejects_landing_lag_accepts_teeter_and_uses_grace)
{
    // Landing (0x16..0x18) and LandingFallSpecial (0x19) must NEVER count as landed safe.
    for (int st = 0x16; st <= 0x19; st++)
    {
        CHECK(!isLandedSafeOnStage(st, false, true, 0, 0, 0.0f, -68.0f, 68.0f));
    }
    // Non-actionable Wait/Guard, airborne, hitstun, hitlag, or offstage must be rejected.
    CHECK(!isLandedSafeOnStage(0x00, false, false, 0, 0, 0.0f, -68.0f, 68.0f));
    CHECK(!isLandedSafeOnStage(0x00, true, true, 0, 0, 0.0f, -68.0f, 68.0f));
    CHECK(!isLandedSafeOnStage(0x00, false, true, 4, 0, 0.0f, -68.0f, 68.0f));
    CHECK(!isLandedSafeOnStage(0x00, false, true, 0, 2, 0.0f, -68.0f, 68.0f));
    CHECK(!isLandedSafeOnStage(0x00, false, true, 0, 0, 75.0f, -68.0f, 68.0f));

    // Actionable grounded neutral / guard / crouch and Teeter (Ottoto 0x7C / 0x7D) are accepted.
    CHECK(isLandedSafeOnStage(0x00, false, true, 0, 0, 0.0f, -68.0f, 68.0f));
    CHECK(isLandedSafeOnStage(0x03, false, true, 0, 0, 10.0f, -68.0f, 68.0f));
    CHECK(isLandedSafeOnStage(0x12, false, true, 0, 0, 10.0f, -68.0f, 68.0f));
    CHECK(isLandedSafeOnStage(0x1B, false, true, 0, 0, 10.0f, -68.0f, 68.0f));
    CHECK(isLandedSafeOnStage(0x7C, false, false, 0, 0, 66.0f, -68.0f, 68.0f));
    CHECK(isLandedSafeOnStage(0x7D, false, false, 0, 0, -66.0f, -68.0f, 68.0f));

    // Grace delay after becoming actionable onstage, and cancellation if P1 lands a punish.
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.recoveryTrainer = 1;
    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;
    ComboStepInput in = baseInput();

    // Initial warp is immediate.
    in.landedSafeOnStage = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(1u, st.recoveryResets);

    // Spend 15 frames airborne offstage.
    in.landedSafeOnStage = false;
    in.air = true;
    for (int f = 0; f < 15; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }

    // Lands in actionable Wait: must hold for kRecoveryGraceFrames before warping.
    in.air = false;
    in.landedSafeOnStage = true;
    for (int f = 0; f < 10; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    // P1 punishes during grace: hitstun cancels the grace countdown cleanly.
    in.hitstun = 8;
    in.landedSafeOnStage = false;
    stepComboTrainer(cfg, in, st, act);
    CHECK(!act.doWarp);
    CHECK_EQ(0, static_cast<int>(st.recoveryGraceFrames));

    // Ledge grab triggers full kRecoveryGraceFrames countdown before next rep.
    in.hitstun = 0;
    in.onLedge = true;
    for (int f = 0; f < kRecoveryGraceFrames; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(2u, st.recoveryResets);

    // KO -> Rebirth (0xBE) also waits kRecoveryGraceFrames then warps cleanly.
    in.onLedge = false;
    in.outOfPlay = true;
    in.status = 0xBE;
    for (int f = 0; f < kRecoveryGraceFrames - 1; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(3u, st.recoveryResets);
}

TEST(combo_trainer, edgeguard_ledge_trap_continuity_waits_for_stage_safe)
{
    main::ComboTrainerSettings cfg = defaultCfg();
    cfg.recoveryTrainer = 1;
    ComboSlotState st;
    comboSlotReset(st);
    ComboStepAction act;
    ComboStepInput in = baseInput();
    in.ignoreLedgeReset = true;

    // Rep 0 still warps offstage immediately even when ignoreLedgeReset is true.
    in.landedSafeOnStage = true;
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(1u, st.recoveryResets);

    // Spend 15 frames airborne offstage.
    in.landedSafeOnStage = false;
    in.air = true;
    for (int f = 0; f < 15; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }

    // Grabbing ledge and hanging for 60 frames must NOT trigger a reset.
    in.air = false;
    in.onLedge = true;
    in.status = 0x75; // CliffWait
    for (int f = 0; f < 60; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
        CHECK_EQ(0, static_cast<int>(st.recoveryGraceFrames));
    }

    // Executing Cliff_Climb (0x77) or LandingFallSpecial (0x19) must NOT trigger reset.
    in.status = 0x77;
    for (int f = 0; f < 10; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    in.onLedge = false;
    in.status = 0x19; // LandingFallSpecial
    for (int f = 0; f < 10; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }

    // Becoming actionable onstage (Wait 0x00, landedSafeOnStage) ticks grace,
    // hitstun cancels grace, and full grace completes the reset cleanly.
    in.status = 0x00;
    in.landedSafeOnStage = true;
    for (int f = 0; f < 10; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    in.hitstun = 6;
    in.landedSafeOnStage = false;
    stepComboTrainer(cfg, in, st, act);
    CHECK(!act.doWarp);
    CHECK_EQ(0, static_cast<int>(st.recoveryGraceFrames));

    in.hitstun = 0;
    in.landedSafeOnStage = true;
    for (int f = 0; f < kRecoveryGraceFrames; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(2u, st.recoveryResets);

    // Rep 1 placed directly offstage by Edgeguard scenario (recoveryResets == 0 after
    // comboTrainerReset, recoveryAirFrames >= 12): ledge grab with ignoreLedgeReset must
    // NOT fire initialPlace or reset on ledge, and must wait for onstage safe + grace.
    comboSlotReset(st);
    in = baseInput();
    in.ignoreLedgeReset = true;
    in.air = true;
    for (int f = 0; f < 15; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    in.air = false;
    in.onLedge = true;
    in.status = 0x74;
    for (int f = 0; f < 40; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    in.onLedge = false;
    in.status = 0x00;
    in.landedSafeOnStage = true;
    for (int f = 0; f < kRecoveryGraceFrames; f++)
    {
        stepComboTrainer(cfg, in, st, act);
        CHECK(!act.doWarp);
    }
    stepComboTrainer(cfg, in, st, act);
    CHECK(act.doWarp);
    CHECK_EQ(1u, st.recoveryResets);
}
