// Host unit tests for the Unified P+ Lab Scenario Preset engine
// (src/extra/scenario_presets_core.h / .cpp) and its session-only menu page.
#include <math.h>
#include <string.h>

#include "extra/scenario_presets_core.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/cpu/fighter_input.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/pages/pages.h"
#include "main/ui/pages/scenarios.h"

using namespace tm;
using namespace tm::extra;

namespace {
    StageGeometry makeFdGeometry()
    {
        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.blast.left = -246.0f;
        g.blast.right = 246.0f;
        g.blast.top = 188.0f;
        g.blast.bottom = -140.0f;
        g.blast.valid = true;
        g.floor.leftX = -85.5f;
        g.floor.rightX = 85.5f;
        g.floor.leftY = 0.0f;
        g.floor.rightY = 0.0f;
        g.floor.centerY = 0.0f;
        g.floor.valid = true;
        g.leftLedge.x = -85.5f;
        g.leftLedge.y = 0.0f;
        g.leftLedge.dir = 1.0f;
        g.rightLedge.x = 85.5f;
        g.rightLedge.y = 0.0f;
        g.rightLedge.dir = -1.0f;
        g.hasLedges = true;
        g.platformCount = 0;
        return g;
    }

    StageGeometry makeBattlefieldGeometry()
    {
        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.blast.left = -224.0f;
        g.blast.right = 224.0f;
        g.blast.top = 200.0f;
        g.blast.bottom = -108.0f;
        g.blast.valid = true;
        g.floor.leftX = -68.4f;
        g.floor.rightX = 68.4f;
        g.floor.leftY = 0.0f;
        g.floor.rightY = 0.0f;
        g.floor.centerY = 0.0f;
        g.floor.valid = true;
        g.leftLedge.x = -68.4f;
        g.leftLedge.y = 0.0f;
        g.leftLedge.dir = 1.0f;
        g.rightLedge.x = 68.4f;
        g.rightLedge.y = 0.0f;
        g.rightLedge.dir = -1.0f;
        g.hasLedges = true;
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

    cpu::CpuOptions makeCustomCpuOpts()
    {
        cpu::CpuOptions o;
        memset(&o, 0, sizeof(o));
        o.control = cpu::kControlTraining;
        o.behavior = cpu::kBehaviorCrouch;
        o.tdi = cpu::kTdiOutwards;
        o.sdiAmount = 3;
        o.sdiDir = cpu::kSdiAway;
        o.counterGround = 9;
        o.counterAir = 11;
        o.counterShield = 4;
        o.counterDelay = 5;
        o.infiniteShields = cpu::kShieldsOff;
        o.shieldHealth = 42;
        o.tech = cpu::kTechInPlace;
        o.getup = cpu::kGetupAttack;
        o.grabEscape = cpu::kMashMedium;
        o.recovery = cpu::kRecoveryGame;
        return o;
    }

    main::ComboTrainerSettings makeCustomComboCfg()
    {
        main::ComboTrainerSettings c;
        memset(&c, 0, sizeof(c));
        c.enabled = 0;
        c.noclip = 0;
        c.fixPosition = 0;
        c.randomizePosition = 1;
        c.randOnGround = 1;
        c.randomizeDamage = 1;
        c.recoveryTrainer = 0;
        c.comboTimerAdjustment = 7;
        c.fixedDamage = 55;
        c.enabledMeters = main::kMeterHitstun | main::kMeterDi;
        return c;
    }

    ScenarioStepInput makeIdleInput(const StageGeometry& g)
    {
        ScenarioStepInput in;
        memset(&in, 0, sizeof(in));
        in.hmnStatus = 0x00;
        in.hmnActionable = true;
        in.cpuStatus = 0x00;
        in.cpuActionable = true;
        in.geom = g;
        return in;
    }

    ScenarioConfig makeCfg(u8 preset, u8 autoReset = 1, u8 side = kScenarioSideRight, u8 move = kMultiHitUpAir)
    {
        ScenarioConfig c;
        c.preset = preset;
        c.autoReset = autoReset;
        c.side = side;
        c.multiHitMove = move;
        return c;
    }
}

TEST(scenario_presets, all_ten_presets_map_settings_and_snapshot_restores_cleanly)
{
    const cpu::CpuOptions origCpu = makeCustomCpuOpts();
    const main::ComboTrainerSettings origCombo = makeCustomComboCfg();
    cpu::CpuOptions cpuOpts = origCpu;
    main::ComboTrainerSettings comboCfg = origCombo;
    ScenarioState st;
    scenarioStateInit(st);

    // 1. Combo Lab
    applyScenarioConfig(st, makeCfg(kScenarioComboLab), cpuOpts, comboCfg);
    CHECK(st.snapshotActive);
    CHECK_EQ(1, comboCfg.enabled);
    CHECK_EQ(1, comboCfg.fixPosition);
    CHECK_EQ(1, comboCfg.noclip);
    CHECK_EQ(0, comboCfg.comboTimerAdjustment);
    CHECK_EQ(0, comboCfg.recoveryTrainer);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(cpu::kTdiRandom, cpuOpts.tdi);
    CHECK_EQ(cpu::kRecoveryPPlusAi, cpuOpts.recovery);
    CHECK_EQ(cpu::kTechRandom, cpuOpts.tech);

    // 2. Techchase (transition A -> B must restore snapshot first: comboTrainer.enabled back to 0!)
    applyScenarioConfig(st, makeCfg(kScenarioTechchase), cpuOpts, comboCfg);
    CHECK(st.snapshotActive);
    CHECK_EQ(0, comboCfg.enabled);
    CHECK_EQ(0, comboCfg.fixPosition);
    CHECK_EQ(7, comboCfg.comboTimerAdjustment);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(cpu::kTechRandom, cpuOpts.tech);
    CHECK_EQ(cpu::kGetupRandom, cpuOpts.getup);
    CHECK_EQ(cpu::kTdiRandom, cpuOpts.tdi);
    CHECK_EQ(origCpu.recovery, cpuOpts.recovery); // not leaked from Combo Lab

    // 3. Edgeguard
    applyScenarioConfig(st, makeCfg(kScenarioEdgeguard), cpuOpts, comboCfg);
    CHECK_EQ(1, comboCfg.enabled);
    CHECK_EQ(1, comboCfg.recoveryTrainer);
    CHECK_EQ(1, comboCfg.noclip);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kRecoveryPPlusAi, cpuOpts.recovery);
    CHECK_EQ(cpu::kTdiSurvival, cpuOpts.tdi);

    // 4. Shield Pressure
    applyScenarioConfig(st, makeCfg(kScenarioShieldPressure), cpuOpts, comboCfg);
    CHECK_EQ(0, comboCfg.enabled);
    CHECK_EQ(0, comboCfg.recoveryTrainer);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorShield, cpuOpts.behavior);
    CHECK_EQ(cpu::kShieldsOn, cpuOpts.infiniteShields);
    CHECK_EQ(kCounterShieldGrab, cpuOpts.counterShield);

    // 5. OoS / Reversal
    applyScenarioConfig(st, makeCfg(kScenarioOosReversal), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorShield, cpuOpts.behavior);
    CHECK_EQ(cpu::kShieldsUntilHit, cpuOpts.infiniteShields);
    CHECK_EQ(kCounterShieldUpB, cpuOpts.counterShield);
    CHECK_EQ(kCounterGroundJab, cpuOpts.counterGround);
    CHECK_EQ(kCounterAirJumpNeutral, cpuOpts.counterAir);

    // 6. Shield Drop
    applyScenarioConfig(st, makeCfg(kScenarioShieldDrop), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(origCpu.counterShield, cpuOpts.counterShield); // no leak from OoS / Reversal

    // 7. Ledgetech
    applyScenarioConfig(st, makeCfg(kScenarioLedgetech), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kTechNone, cpuOpts.tech);

    // 8. Slide-Off
    applyScenarioConfig(st, makeCfg(kScenarioSlideOff), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);

    // 9. Grab Escape
    applyScenarioConfig(st, makeCfg(kScenarioGrabEscape), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(cpu::kMashHigh, cpuOpts.grabEscape);

    // 10. P+ Multi-Hit SDI
    applyScenarioConfig(st, makeCfg(kScenarioMultiHitSdi), cpuOpts, comboCfg);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(origCpu.grabEscape, cpuOpts.grabEscape); // no leak from Grab Escape

    // Back to Off: exact byte-for-byte restore of original user settings!
    applyScenarioConfig(st, makeCfg(kScenarioOff), cpuOpts, comboCfg);
    CHECK(!st.snapshotActive);
    CHECK(memcmp(&origCpu, &cpuOpts, sizeof(origCpu)) == 0);
    CHECK(memcmp(&origCombo, &comboCfg, sizeof(origCombo)) == 0);

    // Detach restore while preset active also restores byte-for-byte.
    applyScenarioConfig(st, makeCfg(kScenarioEdgeguard), cpuOpts, comboCfg);
    CHECK(st.snapshotActive);
    restoreScenarioSnapshot(st, cpuOpts, comboCfg);
    CHECK(!st.snapshotActive);
    CHECK(memcmp(&origCpu, &cpuOpts, sizeof(origCpu)) == 0);
    CHECK(memcmp(&origCombo, &comboCfg, sizeof(origCombo)) == 0);
}

TEST(scenario_presets, platform_selection_and_fd_fallback_for_shield_drop_and_slide_off)
{
    StageGeometry bf = makeBattlefieldGeometry();
    StageGeometry fd = makeFdGeometry();
    ScenarioSpawns sp;

    // Battlefield Right Side -> right side platform (x in [20, 57.6], y = 27.2)
    computeScenarioSpawns(kScenarioShieldDrop, kScenarioSideRight, kMultiHitUpAir, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    CHECK(sp.hmnX > 20.0f && sp.hmnX < 57.6f);
    CHECK(fabsf(sp.hmnY - 27.2f) < 0.1f);
    CHECK(fabsf(sp.cpuY - 0.0f) < 0.1f);

    // Battlefield Left Side -> left side platform (x in [-57.6, -20], y = 27.2)
    computeScenarioSpawns(kScenarioShieldDrop, kScenarioSideLeft, kMultiHitUpAir, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    CHECK(sp.hmnX < -20.0f && sp.hmnX > -57.6f);
    CHECK(fabsf(sp.hmnY - 27.2f) < 0.1f);

    // FD (platless) -> hasPlatform == false, safe center stage fallback
    computeScenarioSpawns(kScenarioShieldDrop, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    CHECK(!sp.hasPlatform);
    CHECK(fabsf(sp.hmnY) < 0.1f);
    CHECK(fabsf(sp.cpuY) < 0.1f);

    // Slide-Off on Battlefield places HMN near platform edge and CPU on platform
    computeScenarioSpawns(kScenarioSlideOff, kScenarioSideRight, kMultiHitUpAir, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    CHECK(sp.hmnX >= 20.0f && sp.hmnX <= 26.0f);
    CHECK(sp.cpuX > sp.hmnX && sp.cpuX <= 57.6f);
    CHECK(fabsf(sp.hmnY - 27.2f) < 0.1f);
    CHECK(fabsf(sp.cpuY - 27.2f) < 0.1f);

    // Slide-Off on FD falls back to main stage lip
    computeScenarioSpawns(kScenarioSlideOff, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    CHECK(!sp.hasPlatform);
    CHECK(sp.hmnX > 78.0f && sp.hmnX < 85.5f);
    CHECK(sp.cpuX < sp.hmnX);

    // Shield Pressure spacing is exactly 12.0 units
    computeScenarioSpawns(kScenarioShieldPressure, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(fabsf((sp.cpuX - sp.hmnX) - 12.0f) < 0.1f);
}

TEST(scenario_presets, noclip_one_frame_restore_for_grounded_and_airborne_spawns)
{
    StageGeometry fd = makeFdGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(fd);
    ScenarioStepAction act;

    // Grounded spawns (Combo Lab): both HMN & CPU get Correct_None (0) -> Correct_Ground_Cliff_Stop (2).
    stepScenario(st, makeCfg(kScenarioComboLab), in, cpuOpts, comboCfg, act);
    CHECK(act.warpHmn && act.warpCpu);
    CHECK(act.setNoclip[0] && act.setNoclip[1]);
    CHECK(!act.restoreCorrect[0] && !act.restoreCorrect[1]);
    CHECK_EQ(kCorrectNone, st.lastCorrectWritten[0]);
    CHECK_EQ(kCorrectNone, st.lastCorrectWritten[1]);

    stepScenario(st, makeCfg(kScenarioComboLab), in, cpuOpts, comboCfg, act);
    CHECK(!act.warpHmn && !act.warpCpu);
    CHECK(act.restoreCorrect[0] && act.restoreCorrect[1]);
    CHECK_EQ(kCorrectGroundCliffStop, act.restoreCorrectKind[0]);
    CHECK_EQ(kCorrectGroundCliffStop, act.restoreCorrectKind[1]);
    CHECK_EQ(kCorrectGroundCliffStop, st.lastCorrectWritten[0]);
    CHECK_EQ(kCorrectGroundCliffStop, st.lastCorrectWritten[1]);

    // Mixed spawns (Ledgetech): HMN is airborne -> Correct_Air (5), CPU is grounded -> Correct_Ground_Cliff_Stop (2).
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    CHECK(act.warpHmn && act.warpCpu);
    CHECK(act.spawns.hmnFalling);
    CHECK(!act.spawns.cpuFalling);
    CHECK_EQ(kCorrectNone, st.lastCorrectWritten[0]);
    CHECK_EQ(kCorrectNone, st.lastCorrectWritten[1]);

    // Even if switched straight to Off on the very next frame, noclip MUST still restore!
    stepScenario(st, makeCfg(kScenarioOff), in, cpuOpts, comboCfg, act);
    CHECK(act.restoreCorrect[0] && act.restoreCorrect[1]);
    CHECK_EQ(kCorrectAir, act.restoreCorrectKind[0]);
    CHECK_EQ(kCorrectGroundCliffStop, act.restoreCorrectKind[1]);
    CHECK_EQ(kCorrectAir, st.lastCorrectWritten[0]);
    CHECK_EQ(kCorrectGroundCliffStop, st.lastCorrectWritten[1]);
}

TEST(scenario_presets, auto_reset_machines_for_techchase_shield_drop_ledgetech_slideoff_grab)
{
    StageGeometry bf = makeBattlefieldGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    ScenarioStepAction act;

    // 1. Techchase: CPU enters Down (0x4A) -> returns to Wait (0x00) -> grace -> auto-reset.
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(bf);
    stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act); // initial warp
    CHECK_EQ(1u, st.resetCount);
    in.cpuStatus = 0x4A; // Down
    stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
    in.cpuStatus = 0x60; // Passive tech
    stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
    in.cpuStatus = 0x00; // Wait
    for (int f = 0; f < 50 && !act.warpCpu; f++)
    {
        stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpCpu);
    CHECK_EQ(1u, st.completionCount);
    CHECK_EQ(2u, st.resetCount);

    // 2. Shield Drop: HMN Guard (0x1B) -> Pass (0x72) -> lands on floor -> auto-reset.
    scenarioStateInit(st);
    in = makeIdleInput(bf);
    stepScenario(st, makeCfg(kScenarioShieldDrop), in, cpuOpts, comboCfg, act); // initial warp
    CHECK(st.hasPlatform);
    in.hmnStatus = 0x1B; // Guard
    in.hmnY = 27.2f;
    stepScenario(st, makeCfg(kScenarioShieldDrop), in, cpuOpts, comboCfg, act);
    in.hmnStatus = 0x72; // Pass (Fighter::Status::Pass)
    in.hmnAir = true;
    stepScenario(st, makeCfg(kScenarioShieldDrop), in, cpuOpts, comboCfg, act);
    CHECK(st.sawShieldDrop);
    CHECK_EQ(1u, st.completionCount);
    in.hmnStatus = 0x00;
    in.hmnAir = false;
    in.hmnY = 0.0f;
    for (int f = 0; f < 40 && !act.warpHmn; f++)
    {
        stepScenario(st, makeCfg(kScenarioShieldDrop), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpHmn);
    CHECK_EQ(2u, st.resetCount);

    // 3. Ledgetech: HMN enters PassiveWall (0x62) -> sawWallTech -> auto-reset.
    scenarioStateInit(st);
    in = makeIdleInput(bf);
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    in.hmnStatus = 0x62; // PassiveWall
    in.hmnAir = true;
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    CHECK(st.sawWallTech);
    CHECK_EQ(1u, st.completionCount);
    for (int f = 0; f < 40 && !act.warpHmn; f++)
    {
        stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpHmn);

    // 4. Slide-Off: grounded -> airborne tumble -> sawSlideOff -> land -> auto-reset.
    scenarioStateInit(st);
    in = makeIdleInput(bf);
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act);
    in.hmnAir = false;
    in.hmnStatus = 0x00;
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act); // arms slideOffArmed
    in.hmnAir = true;
    in.hmnStatus = 0x45; // DamageFly
    in.hmnHitstun = 18;
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act);
    CHECK(st.sawSlideOff);
    CHECK_EQ(1u, st.completionCount);
    in.hmnAir = false;
    in.hmnStatus = 0x00;
    in.hmnHitstun = 0;
    for (int f = 0; f < 40 && !act.warpHmn; f++)
    {
        stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpHmn);

    // 5. Grab Escape: CapturePulled (0x3D) -> CaptureWait (0x3E) x5 -> CaptureCut (0x40) -> auto-reset.
    scenarioStateInit(st);
    in = makeIdleInput(bf);
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    in.cpuStatus = 0x3D;
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    in.cpuStatus = 0x3E;
    for (int f = 0; f < 5; f++)
    {
        stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    }
    CHECK_EQ(6u, st.grabEscapeFrames);
    in.cpuStatus = 0x40; // CaptureCut
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    CHECK(st.sawGrabEscape);
    CHECK_EQ(1u, st.completionCount);
    in.cpuStatus = 0x00;
    for (int f = 0; f < 40 && !act.warpCpu; f++)
    {
        stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpCpu);
}

TEST(scenario_presets, multi_hit_sdi_tracks_cumulative_displacement_and_escape_and_cpu_scripts)
{
    StageGeometry fd = makeFdGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(fd);
    ScenarioStepAction act;

    ScenarioConfig cfg = makeCfg(kScenarioMultiHitSdi, 1, kScenarioSideRight, kMultiHitSideB);
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act); // initial warp
    CHECK_EQ(1u, st.resetCount);

    // Settle 1 frame after warp
    in.hmnX = 7.0f;
    in.hmnY = 0.0f;
    in.cpuX = -7.0f;
    in.cpuY = 0.0f;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);

    // Hit 1: 3 frames of hitlag, HMN SDI shifts +3.0x, +1.5y during hitlag
    in.hmnStatus = 0x43;
    in.hmnDamageHitlag = true;
    in.hmnHitlag = 3;
    in.hmnHitstun = 14;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    in.hmnHitlag = 2;
    in.hmnX += 3.0f;
    in.hmnY += 1.5f;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    in.hmnHitlag = 1;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);

    // Hit 2: 2 frames of hitlag, HMN SDI shifts +4.0x, +2.0y
    in.hmnHitlag = 2;
    in.hmnX += 4.0f;
    in.hmnY += 2.0f;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    in.hmnHitlag = 1;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);

    CHECK_EQ(2u, st.sdiHitCount);
    CHECK_EQ(5u, st.sdiHitlagFrames);
    CHECK(fabsf(st.sdiDispX - 7.0f) < 0.01f);
    CHECK(fabsf(st.sdiDispY - 3.5f) < 0.01f);

    // Hitlag ends, HMN has displaced far enough to escape (> 16 units from CPU)
    in.hmnHitlag = 0;
    in.hmnDamageHitlag = false;
    in.hmnX = 12.0f; // sepX = 19.0 > 16.0
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    CHECK(st.sdiEscaped);
    CHECK_EQ(1u, st.completionCount);

    // Grace expires -> auto-resets, keeping SDI telemetry latched for inspection!
    for (int f = 0; f < 35 && !act.warpHmn; f++)
    {
        stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpHmn);
    CHECK_EQ(2u, st.resetCount);
    CHECK_EQ(2u, st.sdiHitCount);
    CHECK(st.sdiEscaped);

    // Verify CPU pressure scripts: Off produces active == false; SideB fires Special + toward.
    ScenarioCpuState cs;
    scenarioCpuStateReset(cs);
    ScenarioCpuInput ci;
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioOff;
    ci.cpuCanAct = true;
    ScenarioCpuOutput co;
    stepScenarioCpu(ci, cs, co);
    CHECK(!co.active);

    ci.preset = kScenarioMultiHitSdi;
    ci.multiHitMove = kMultiHitSideB;
    ci.cpuX = -7.0f;
    ci.hmnX = 7.0f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kSpecial), co.buttons);
    CHECK(co.stickX > 0.5f);
}

TEST(scenario_presets, clamps_out_of_range_inputs_and_general_row_index_is_12)
{
    ScenarioConfig bad;
    bad.preset = 99;
    bad.autoReset = 42;
    bad.side = 7;
    bad.multiHitMove = 12;
    ScenarioConfig clean = sanitizeScenarioConfig(bad);
    CHECK_EQ(static_cast<int>(kScenarioOff), static_cast<int>(clean.preset));
    CHECK_EQ(1, static_cast<int>(clean.autoReset));
    CHECK_EQ(static_cast<int>(kScenarioSideRight), static_cast<int>(clean.side));
    CHECK_EQ(static_cast<int>(kMultiHitUpAir), static_cast<int>(clean.multiHitMove));

    // Lab (kGeneral) places Controls at row 7 and Debug at row 8; Scenarios is
    // its own top-level category page with Combo Trainer at row 5.
    const ui::Page& gen = ui::pages::kGeneral;
    REQUIRE(gen.count == 13);
    CHECK(strcmp(gen.options[7].name, "Controls") == 0);
    CHECK(strcmp(gen.options[8].name, "Debug") == 0);
    const ui::Page& scen = ui::pages::kScenarios;
    REQUIRE(scen.count == 6);
    CHECK(strcmp(scen.options[0].name, "Preset") == 0);
    CHECK(strcmp(scen.options[5].name, "Combo Trainer") == 0);
    CHECK_EQ(ui::pages::P_COMBO_TRAINER, scen.options[5].max);
}

TEST(scenario_presets, multi_hit_sdi_ignores_attacker_hitlag)
{
    StageGeometry fd = makeFdGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(fd);
    ScenarioStepAction act;

    ScenarioConfig cfg = makeCfg(kScenarioMultiHitSdi, 1, kScenarioSideRight, kMultiHitSideB);
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act); // initial warp
    CHECK_EQ(1u, st.resetCount);

    in.hmnX = 7.0f;
    in.hmnY = 0.0f;
    in.cpuX = -7.0f;
    in.cpuY = 0.0f;
    stepScenario(st, cfg, in, cpuOpts, comboCfg, act);

    // Human hits CPU: attacker hitlag has hmnHitlag > 0, hmnDamageHitlag == false,
    // and attack status 0x24. None of the victim SDI counters may advance.
    in.hmnStatus = 0x24;
    in.hmnDamageHitlag = false;
    for (int lag = 5; lag >= 1; lag--)
    {
        in.hmnHitlag = lag;
        in.hmnX += 1.5f;
        in.hmnY += 0.5f;
        stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    }
    in.hmnHitlag = 0;
    in.hmnStatus = 0x00;
    in.hmnActionable = true;
    for (int f = 0; f < 30; f++)
    {
        stepScenario(st, cfg, in, cpuOpts, comboCfg, act);
    }

    CHECK_EQ(0u, st.sdiHitCount);
    CHECK_EQ(0u, st.sdiHitlagFrames);
    CHECK(fabsf(st.sdiDispX) < 0.001f);
    CHECK(fabsf(st.sdiDispY) < 0.001f);
    CHECK(!st.sdiEscaped);
    CHECK(!st.sdiSequenceActive);
    CHECK_EQ(0u, st.completionCount);
    CHECK_EQ(1u, st.resetCount);
}

namespace {
    ScenarioState* s_savedViewState = NULL;

    void testSavedViewHook(main::Settings& out)
    {
        if (s_savedViewState != NULL)
        {
            projectScenarioSavedView(*s_savedViewState, out.cpu.options, out.comboTrainer);
        }
    }

    void pumpStorageFrames(int count = 4)
    {
        for (int i = 0; i < count; i++)
        {
            storage::onRender(host::fakes::match());
        }
    }

    void bootStorage()
    {
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }
}

TEST(scenario_presets, saved_view_projects_snapshot_and_prevents_preset_leak_to_sd)
{
    host::card::reset();
    bootStorage();

    ScenarioState st;
    scenarioStateInit(st);
    s_savedViewState = &st;
    main::ExtraHooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.scenarioSavedView = testSavedViewHook;
    main::setExtraHooks(&hooks);

    // Establish distinctive persisted user settings and save baseline to SD.
    main::Settings& s = main::settings();
    s.general.cameraMode = 1;
    s.general.moveStaling = 1;
    s.cpu.options = makeCustomCpuOpts();
    s.comboTrainer = makeCustomComboCfg();
    storage::settingsMenuClosed();
    pumpStorageFrames();

    REQUIRE(host::card::has("settings-a.bin"));
    CHECK(!host::card::has("settings-b.bin"));
    u32 seqAfterBaseline = reinterpret_cast<storage::FileHeader*>(host::card::data("settings-a.bin"))->sequence;

    // Activate Combo Lab preset (mutates live cpu.options + comboTrainer).
    applyScenarioConfig(st, makeCfg(kScenarioComboLab), s.cpu.options, s.comboTrainer);
    CHECK(st.snapshotActive);
    CHECK_EQ(1, s.comboTrainer.fixPosition);
    CHECK_EQ(cpu::kBehaviorStand, s.cpu.options.behavior);

    // Closing the menu with only a preset active must NOT trigger an SD write.
    storage::settingsMenuClosed();
    pumpStorageFrames();
    CHECK(!host::card::has("settings-b.bin"));
    CHECK_EQ(seqAfterBaseline,
             reinterpret_cast<storage::FileHeader*>(host::card::data("settings-a.bin"))->sequence);

    // Edit an unrelated user setting while preset is still active and close menu.
    s.general.moveStaling = 0;
    storage::settingsMenuClosed();
    pumpStorageFrames();
    REQUIRE(host::card::has("settings-b.bin"));

    // Reboot from SD: unrelated user edit persisted, preset overrides did NOT leak!
    main::setExtraHooks(NULL);
    s_savedViewState = NULL;
    bootStorage();
    const main::Settings& reloaded = main::settings();
    CHECK_EQ(1, reloaded.general.cameraMode);
    CHECK_EQ(0, reloaded.general.moveStaling);
    CHECK_EQ(cpu::kBehaviorCrouch, reloaded.cpu.options.behavior);
    CHECK_EQ(cpu::kTdiOutwards, reloaded.cpu.options.tdi);
    CHECK_EQ(cpu::kTechInPlace, reloaded.cpu.options.tech);
    CHECK_EQ(0, reloaded.comboTrainer.fixPosition);
    CHECK_EQ(7, reloaded.comboTrainer.comboTimerAdjustment);
    CHECK_EQ(55, reloaded.comboTrainer.fixedDamage);

    // After Reset Settings (discardScenarioSnapshot), closing menu writes clean defaults.
    s_savedViewState = &st;
    main::setExtraHooks(&hooks);
    discardScenarioSnapshot(st);
    CHECK(!st.snapshotActive);
    main::Settings& afterReset = main::settings();
    afterReset.cpu.options.behavior = cpu::kBehaviorStand;
    afterReset.comboTrainer.comboTimerAdjustment = 0;
    storage::settingsMenuClosed();
    pumpStorageFrames();

    main::setExtraHooks(NULL);
    s_savedViewState = NULL;
    bootStorage();
    CHECK_EQ(cpu::kBehaviorStand, main::settings().cpu.options.behavior);
    CHECK_EQ(0, main::settings().comboTrainer.comboTimerAdjustment);

    storage::detach();
    host::card::reset();
}

TEST(scenario_presets, tightened_spawn_spacings_for_grab_ledgetech_slideoff_and_multihit)
{
    StageGeometry fd = makeFdGeometry();
    StageGeometry bf = makeBattlefieldGeometry();
    ScenarioSpawns sp;

    // Grab Escape: close enough (~6.5u) for short-arm standing grabs (Pikachu, Lucas).
    computeScenarioSpawns(kScenarioGrabEscape, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    float grabDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(grabDx >= 5.5f && grabDx <= 7.0f);

    // Ledgetech: CPU spawns right at the ledge lip (4.0 - 5.0u inward), facing offstage.
    computeScenarioSpawns(kScenarioLedgetech, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    float rightLipDist = fd.rightLedge.x - sp.cpuX;
    CHECK(rightLipDist >= 3.8f && rightLipDist <= 5.2f);
    CHECK(sp.cpuLr > 0.0f);

    computeScenarioSpawns(kScenarioLedgetech, kScenarioSideLeft, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    float leftLipDist = sp.cpuX - fd.leftLedge.x;
    CHECK(leftLipDist >= 3.8f && leftLipDist <= 5.2f);
    CHECK(sp.cpuLr < 0.0f);

    // Slide-Off: tightened spacing (~8.5 - 9.5u) on both FD floor and Battlefield platform.
    computeScenarioSpawns(kScenarioSlideOff, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    CHECK(sp.valid);
    float slideFdDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(slideFdDx >= 8.0f && slideFdDx <= 9.6f);

    computeScenarioSpawns(kScenarioSlideOff, kScenarioSideRight, kMultiHitUpAir, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    float slideBfDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(slideBfDx >= 8.0f && slideBfDx <= 9.6f);

    // P+ Multi-Hit SDI: per-move spacing so aerials, rapid jab, and side-B connect cleanly.
    computeScenarioSpawns(kScenarioMultiHitSdi, kScenarioSideRight, kMultiHitUpAir, fd, sp);
    float uairDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(uairDx >= 4.8f && uairDx <= 6.2f);

    computeScenarioSpawns(kScenarioMultiHitSdi, kScenarioSideRight, kMultiHitDownAir, fd, sp);
    float dairDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(dairDx >= 4.3f && dairDx <= 5.7f);

    computeScenarioSpawns(kScenarioMultiHitSdi, kScenarioSideRight, kMultiHitRapidJab, fd, sp);
    float jabDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(jabDx >= 6.3f && jabDx <= 7.7f);

    computeScenarioSpawns(kScenarioMultiHitSdi, kScenarioSideRight, kMultiHitSideB, fd, sp);
    float sideBDx = fabsf(sp.cpuX - sp.hmnX);
    CHECK(sideBDx >= 9.8f && sideBDx <= 11.2f);
}

TEST(scenario_presets, multihit_aerial_cpu_steers_horizontally_and_ledgetech_waits_for_proximity)
{
    ScenarioCpuState cs;
    ScenarioCpuInput ci;
    ScenarioCpuOutput co;

    // Multi-Hit SDI Down Air: jump & airborne frames steer horizontally toward HMN,
    // scaled by dx so CPU does not overshoot, while C-stick frame keeps stickX == 0.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioMultiHitSdi;
    ci.multiHitMove = kMultiHitDownAir;
    ci.cpuCanAct = true;
    ci.cpuX = -2.5f;
    ci.hmnX = 2.5f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kJump), co.buttons);
    CHECK(co.stickX > 0.3f);

    // Jump squat continues steering toward HMN.
    ci.cpuCanAct = false;
    ci.cpuAir = false;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK(co.stickX > 0.3f);

    // First airborne frame triggers C-stick Down Air with clean vertical stick.
    ci.cpuAir = true;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick), co.buttons);
    CHECK(fabsf(co.stickX) < 0.01f);
    CHECK(co.stickY < -0.9f);

    // Subsequent airborne frames track HMN X (both sides) and taper near zero delta.
    ci.cpuX = -1.0f;
    ci.hmnX = 2.0f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK(co.stickX > 0.2f);

    ci.cpuX = 3.0f;
    ci.hmnX = 0.0f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK(co.stickX < -0.2f);

    ci.cpuX = 2.0f;
    ci.hmnX = 2.1f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK(fabsf(co.stickX) < 0.2f);

    // Multi-Hit SDI Up Air: short-hop steers toward HMN and delays C-stick Up slightly
    // after takeoff so rising/falling arcs hit grounded short and tall fighters alike.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioMultiHitSdi;
    ci.multiHitMove = kMultiHitUpAir;
    ci.cpuCanAct = true;
    ci.cpuX = -2.75f;
    ci.hmnX = 2.75f;
    stepScenarioCpu(ci, cs, co);
    CHECK_EQ(static_cast<u32>(cpu::button::kJump), co.buttons);
    CHECK(co.stickX > 0.3f);

    ci.cpuCanAct = false;
    ci.cpuAir = true;
    bool firedUpAir = false;
    for (int f = 0; f < 10; f++)
    {
        stepScenarioCpu(ci, cs, co);
        if ((co.buttons & cpu::button::kCStick) != 0)
        {
            CHECK(fabsf(co.stickX) < 0.01f);
            CHECK(co.stickY > 0.9f);
            firedUpAir = true;
            break;
        }
        CHECK(co.stickX > 0.3f);
    }
    CHECK(firedUpAir);

    // Ledgetech: CPU waits while HMN is still at far spawn offset (13.5u out, -12u low)
    // and throws dtilt as soon as HMN enters ledge recovery proximity.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioLedgetech;
    ci.cpuCanAct = true;
    ci.cpuLr = 1.0f;
    ci.cpuX = 81.0f;
    ci.cpuY = 0.0f;
    ci.hmnX = 94.5f;
    ci.hmnY = -12.0f;
    ci.hmnAir = true;
    stepScenarioCpu(ci, cs, co);
    CHECK(!co.active);

    ci.hmnX = 90.0f;
    ci.hmnY = -8.0f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kAttack), co.buttons);
    CHECK(co.stickY < -0.3f);
}

TEST(scenario_presets, proactive_blastzone_reset_and_defer_warp_while_dead)
{
    StageGeometry bf = makeBattlefieldGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(bf);
    ScenarioStepAction act;

    // Activate Ledgetech on Battlefield (bottom blast zone = -108.0).
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    CHECK(act.warpHmn);
    CHECK_EQ(1u, st.resetCount);

    // 1-frame noclip restore while HMN falls safely above guard threshold.
    in.hmnAir = true;
    in.hmnStatus = 0x0E; // Fall
    in.hmnX = 77.4f;
    in.hmnY = -40.0f;
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    CHECK(!act.warpHmn);

    // HMN approaches bottom blast zone (-82.0 < -108.0 + 28.0): reset proactively
    // BEFORE crossing the blast zone (hmnOutOfPlay is still false).
    in.hmnY = -82.0f;
    stepScenario(st, makeCfg(kScenarioLedgetech), in, cpuOpts, comboCfg, act);
    CHECK(act.warpHmn);
    CHECK_EQ(2u, st.resetCount);

    // Switching presets while HMN is already in Dead (0xBD) or Standby (0x10B)
    // must defer the warp until Rebirth (0xBE) so no residual KO respawn timer fires later.
    in.hmnStatus = 0xBD; // kStatusDead
    in.hmnOutOfPlay = true;
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    CHECK(!act.warpHmn);
    CHECK(st.pendingReset);

    in.hmnStatus = 0x10B; // kStatusStandby
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    CHECK(!act.warpHmn);
    CHECK(st.pendingReset);

    in.hmnStatus = 0xBE; // kStatusRebirth
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act);
    CHECK(act.warpHmn);
    CHECK(!st.pendingReset);
    CHECK_EQ(3u, st.resetCount);
}

TEST(scenario_presets, slideoff_temporarily_sets_tumble_percent_and_restores_without_sd_leak)
{
    StageGeometry fd = makeFdGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    main::GeneralSettings gen;
    memset(&gen, 0, sizeof(gen));
    gen.playerPercent = 12;
    gen.lockPlayerPercent = 0;

    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(fd);
    ScenarioStepAction act;

    // Activating Slide-Off below 75% sets playerPercent = 95, lockPlayerPercent = 1,
    // and requests applyHmnPercent so live HMN tumbles immediately on Frame 1.
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act, &gen);
    CHECK(st.snapshotActive);
    CHECK(st.snapshotPlayerValid);
    CHECK_EQ(95, static_cast<int>(gen.playerPercent));
    CHECK_EQ(1, static_cast<int>(gen.lockPlayerPercent));
    CHECK(act.applyHmnPercent);

    // Saved-settings view projects back the original pre-preset damage & lock flag.
    cpu::CpuOptions projCpu = cpuOpts;
    main::ComboTrainerSettings projCombo = comboCfg;
    main::GeneralSettings projGen = gen;
    projectScenarioSavedView(st, projCpu, projCombo, &projGen);
    CHECK_EQ(12, static_cast<int>(projGen.playerPercent));
    CHECK_EQ(0, static_cast<int>(projGen.lockPlayerPercent));

    // Switching Slide-Off -> Grab Escape restores original playerPercent & lock flag.
    stepScenario(st, makeCfg(kScenarioGrabEscape), in, cpuOpts, comboCfg, act, &gen);
    CHECK_EQ(12, static_cast<int>(gen.playerPercent));
    CHECK_EQ(0, static_cast<int>(gen.lockPlayerPercent));
    CHECK(act.applyHmnPercent);

    // Switching back to Slide-Off -> Off also restores bit-for-bit.
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act, &gen);
    CHECK_EQ(95, static_cast<int>(gen.playerPercent));
    CHECK_EQ(1, static_cast<int>(gen.lockPlayerPercent));
    stepScenario(st, makeCfg(kScenarioOff), in, cpuOpts, comboCfg, act, &gen);
    CHECK(!st.snapshotActive);
    CHECK(!st.snapshotPlayerValid);
    CHECK_EQ(12, static_cast<int>(gen.playerPercent));
    CHECK_EQ(0, static_cast<int>(gen.lockPlayerPercent));
    CHECK(act.applyHmnPercent);

    // If user already set >= 75%, Slide-Off preserves their chosen percent.
    gen.playerPercent = 80;
    gen.lockPlayerPercent = 1;
    stepScenario(st, makeCfg(kScenarioSlideOff), in, cpuOpts, comboCfg, act, &gen);
    CHECK_EQ(80, static_cast<int>(gen.playerPercent));
    CHECK_EQ(1, static_cast<int>(gen.lockPlayerPercent));
    CHECK(!act.applyHmnPercent);
    restoreScenarioSnapshot(st, cpuOpts, comboCfg, &gen);
    CHECK_EQ(80, static_cast<int>(gen.playerPercent));
    CHECK_EQ(1, static_cast<int>(gen.lockPlayerPercent));
}
