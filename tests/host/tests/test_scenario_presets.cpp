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
#include "main/ui/pages/cpu_rows.h"
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

    ScenarioConfig makeCfg(u8 preset, u8 autoReset = 1, u8 side = kScenarioSideRight, u8 move = kMultiHitAuto)
    {
        ScenarioConfig c;
        memset(&c, 0, sizeof(c));
        c.preset = preset;
        c.autoReset = autoReset;
        c.side = side;
        c.multiHitMove = move;
        c.oosAction = kOosGrab;
        c.ledgetechSpawn = kLtSpawnCloseLow;
        c.ledgetechDj = kLtDjAvailable;
        c.ledgetechAttack = kLtAtkDtilt;
        c.shieldDropPlat = side == kScenarioSideLeft ? kSdPlatLeft : kSdPlatRight;
        c.shieldDropSpacing = kSdSpaceClose;
        c.shieldDropFacing = kSdFaceToward;
        return c;
    }

    main::Settings packSettings(const cpu::CpuOptions& cpuOpts, const main::ComboTrainerSettings& comboCfg,
                                const main::GeneralSettings* gen, const main::CpuSettings* cpuSet,
                                const main::OsdSettings* osd, const main::TechInvisSettings* techInvis)
    {
        main::Settings s;
        memset(&s, 0, sizeof(s));
        s.cpu.options = cpuOpts;
        s.comboTrainer = comboCfg;
        if (gen != NULL) s.general = *gen;
        if (cpuSet != NULL)
        {
            s.cpu.percent = cpuSet->percent;
            s.cpu.lockPercent = cpuSet->lockPercent;
        }
        if (osd != NULL) s.osd = *osd;
        if (techInvis != NULL) s.techInvis = *techInvis;
        return s;
    }

    void unpackSettings(const main::Settings& s, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                        main::GeneralSettings* gen, main::CpuSettings* cpuSet, main::OsdSettings* osd,
                        main::TechInvisSettings* techInvis)
    {
        cpuOpts = s.cpu.options;
        comboCfg = s.comboTrainer;
        if (gen != NULL) *gen = s.general;
        if (cpuSet != NULL)
        {
            cpuSet->percent = s.cpu.percent;
            cpuSet->lockPercent = s.cpu.lockPercent;
        }
        if (osd != NULL) *osd = s.osd;
        if (techInvis != NULL) *techInvis = s.techInvis;
    }

    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* gen = NULL,
                             main::CpuSettings* cpuSet = NULL, main::OsdSettings* osd = NULL,
                             main::TechInvisSettings* techInvis = NULL)
    {
        main::Settings s = packSettings(cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        bool r = tm::extra::applyScenarioConfig(st, rawCfg, s);
        unpackSettings(s, cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        return r;
    }

    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg,
                                        cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                        main::GeneralSettings* gen = NULL, main::CpuSettings* cpuSet = NULL,
                                        main::OsdSettings* osd = NULL, main::TechInvisSettings* techInvis = NULL)
    {
        main::Settings s = packSettings(cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        tm::extra::reapplyActiveScenarioOverrides(st, rawCfg, s);
        unpackSettings(s, cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
    }

    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* gen = NULL, main::CpuSettings* cpuSet = NULL,
                                 main::OsdSettings* osd = NULL, main::TechInvisSettings* techInvis = NULL)
    {
        main::Settings s = packSettings(cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        tm::extra::restoreScenarioSnapshot(st, s);
        unpackSettings(s, cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
    }

    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* gen = NULL,
                                  main::CpuSettings* cpuSet = NULL, main::OsdSettings* osd = NULL,
                                  main::TechInvisSettings* techInvis = NULL)
    {
        main::Settings s = packSettings(cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        tm::extra::projectScenarioSavedView(st, s);
        unpackSettings(s, cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
    }

    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* gen = NULL, main::CpuSettings* cpuSet = NULL,
                      main::OsdSettings* osd = NULL, main::TechInvisSettings* techInvis = NULL)
    {
        main::Settings s = packSettings(cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
        tm::extra::stepScenario(st, rawCfg, in, s, act);
        unpackSettings(s, cpuOpts, comboCfg, gen, cpuSet, osd, techInvis);
    }
}

TEST(scenario_presets, all_nine_presets_map_settings_and_snapshot_restores_cleanly)
{
    const cpu::CpuOptions origCpu = makeCustomCpuOpts();
    main::ComboTrainerSettings origCombo = makeCustomComboCfg();
    origCombo.enabledMeters = 0;
    cpu::CpuOptions cpuOpts = origCpu;
    main::ComboTrainerSettings comboCfg = origCombo;
    main::GeneralSettings gen;
    memset(&gen, 0, sizeof(gen));
    gen.playerPercent = 10;
    gen.lockPlayerPercent = 0;
    gen.playerInfiniteShields = cpu::kShieldsUntilHit;
    const main::GeneralSettings origGen = gen;
    main::CpuSettings cpuSet;
    memset(&cpuSet, 0, sizeof(cpuSet));
    cpuSet.percent = 0;
    cpuSet.lockPercent = 0;
    const main::CpuSettings origCpuSet = cpuSet;
    ScenarioState st;
    scenarioStateInit(st);

    // 1. Combo Lab (calm Hitstun | DI Ring starter meters when unset, noclip = 0)
    applyScenarioConfig(st, makeCfg(kScenarioComboLab), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK(st.snapshotActive);
    CHECK_EQ(1, comboCfg.enabled);
    CHECK_EQ(1, comboCfg.fixPosition);
    CHECK_EQ(0, comboCfg.noclip);
    CHECK_EQ(static_cast<int>(main::kMeterHitstun | main::kMeterDi), static_cast<int>(comboCfg.enabledMeters));
    CHECK_EQ(0, comboCfg.comboTimerAdjustment);
    CHECK_EQ(0, comboCfg.recoveryTrainer);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(cpu::kTdiRandom, cpuOpts.tdi);
    CHECK_EQ(cpu::kRecoveryPPlusAi, cpuOpts.recovery);
    CHECK_EQ(cpu::kTechRandom, cpuOpts.tech);

    // 2. Techchase (transition A -> B must restore snapshot first: comboTrainer.enabled back to 0!)
    applyScenarioConfig(st, makeCfg(kScenarioTechchase), cpuOpts, comboCfg, &gen, &cpuSet);
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

    // 3. Edgeguard (seeds CPU 35% when < 30%)
    applyScenarioConfig(st, makeCfg(kScenarioEdgeguard), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(1, comboCfg.enabled);
    CHECK_EQ(1, comboCfg.recoveryTrainer);
    CHECK_EQ(1, comboCfg.noclip);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kRecoveryPPlusAi, cpuOpts.recovery);
    CHECK_EQ(cpu::kTdiSurvival, cpuOpts.tdi);
    CHECK_EQ(35, static_cast<int>(cpuSet.percent));
    CHECK_EQ(1, static_cast<int>(cpuSet.lockPercent));

    // 4. Unified Shield Pressure / OoS (CPU OoS defaults to Grab, updates live on change)
    ScenarioConfig spCfg = makeCfg(kScenarioShieldPressure);
    applyScenarioConfig(st, spCfg, cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(0, comboCfg.enabled);
    CHECK_EQ(0, comboCfg.recoveryTrainer);
    CHECK_EQ(0, static_cast<int>(cpuSet.percent)); // restored from Edgeguard!
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorShield, cpuOpts.behavior);
    CHECK_EQ(cpu::kShieldsOn, cpuOpts.infiniteShields);
    CHECK_EQ(kCounterShieldGrab, cpuOpts.counterShield);
    CHECK_EQ(kCounterGroundJab, cpuOpts.counterGround);
    CHECK_EQ(kCounterAirJumpNeutral, cpuOpts.counterAir);
    spCfg.oosAction = kOosUpB;
    applyScenarioConfig(st, spCfg, cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(kCounterShieldUpB, cpuOpts.counterShield);

    // 5. Shield Drop (enables Player Infinite Shields = On while active)
    applyScenarioConfig(st, makeCfg(kScenarioShieldDrop), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(origCpu.counterShield, cpuOpts.counterShield); // no leak from Shield Pressure / OoS
    CHECK_EQ(cpu::kShieldsOn, gen.playerInfiniteShields);

    // 6. Ledgetech (seeds P1 60% when < 50% and restores Infinite Shields from Shield Drop)
    applyScenarioConfig(st, makeCfg(kScenarioLedgetech), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kTechNone, cpuOpts.tech);
    CHECK_EQ(60, static_cast<int>(gen.playerPercent));
    CHECK_EQ(1, static_cast<int>(gen.lockPlayerPercent));
    CHECK_EQ(cpu::kShieldsUntilHit, gen.playerInfiniteShields);

    // 7. Slide-Off
    applyScenarioConfig(st, makeCfg(kScenarioSlideOff), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(95, static_cast<int>(gen.playerPercent));

    // 8. Grab Defense / Throw DI (default Hold preserves CPU grabEscape; Stand sets kMashHigh)
    ScenarioConfig grabStand = makeCfg(kScenarioGrabEscape);
    grabStand.grabAction = kGrabActStand;
    applyScenarioConfig(st, grabStand, cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(cpu::kMashHigh, cpuOpts.grabEscape);
    CHECK_EQ(10, static_cast<int>(gen.playerPercent));

    // 9. P+ Multi-Hit SDI
    applyScenarioConfig(st, makeCfg(kScenarioMultiHitSdi), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK_EQ(cpu::kControlLab, cpuOpts.control);
    CHECK_EQ(cpu::kBehaviorStand, cpuOpts.behavior);
    CHECK_EQ(origCpu.grabEscape, cpuOpts.grabEscape); // no leak from Grab Escape

    // Back to Off: exact byte-for-byte restore of original user settings!
    applyScenarioConfig(st, makeCfg(kScenarioOff), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK(!st.snapshotActive);
    CHECK(memcmp(&origCpu, &cpuOpts, sizeof(origCpu)) == 0);
    CHECK(memcmp(&origCombo, &comboCfg, sizeof(origCombo)) == 0);
    CHECK(memcmp(&origGen, &gen, sizeof(origGen)) == 0);
    CHECK(memcmp(&origCpuSet, &cpuSet, sizeof(origCpuSet)) == 0);

    // Detach restore while preset active also restores byte-for-byte.
    applyScenarioConfig(st, makeCfg(kScenarioEdgeguard), cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK(st.snapshotActive);
    restoreScenarioSnapshot(st, cpuOpts, comboCfg, &gen, &cpuSet);
    CHECK(!st.snapshotActive);
    CHECK(memcmp(&origCpu, &cpuOpts, sizeof(origCpu)) == 0);
    CHECK(memcmp(&origCombo, &comboCfg, sizeof(origCombo)) == 0);
    CHECK(memcmp(&origGen, &gen, sizeof(origGen)) == 0);
    CHECK(memcmp(&origCpuSet, &cpuSet, sizeof(origCpuSet)) == 0);
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

    // Slide-Off on Battlefield defaults to Under-Platform Up Air: HMN near platform edge,
    // CPU on main floor underneath the platform.
    computeScenarioSpawns(kScenarioSlideOff, kScenarioSideRight, kMultiHitUpAir, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    CHECK(sp.hmnX >= 20.0f && sp.hmnX <= 26.0f);
    CHECK(fabsf(sp.hmnY - 27.2f) < 0.1f);
    CHECK(fabsf(sp.cpuY - 0.0f) < 0.1f);

    // Slide-Off On-Plat Ftilt places CPU on the platform beside HMN.
    ScenarioConfig onPlatSlide = makeCfg(kScenarioSlideOff, 1, kScenarioSideRight, kMultiHitUpAir);
    onPlatSlide.slideOffAttack = kSlideAtkOnPlatFtilt;
    u32 slideRng = 77u;
    computeScenarioSpawnsEx(onPlatSlide, -1, slideRng, bf, sp);
    CHECK(sp.valid);
    CHECK(sp.hasPlatform);
    CHECK(sp.cpuX > sp.hmnX && sp.cpuX <= 57.6f);
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
    memset(&bad, 0xFF, sizeof(bad));
    ScenarioConfig clean = sanitizeScenarioConfig(bad);
    CHECK_EQ(static_cast<int>(kScenarioOff), static_cast<int>(clean.preset));
    CHECK_EQ(1, static_cast<int>(clean.autoReset));
    CHECK_EQ(static_cast<int>(kScenarioSideRight), static_cast<int>(clean.side));
    CHECK_EQ(static_cast<int>(kMultiHitAuto), static_cast<int>(clean.multiHitMove));
    CHECK_EQ(static_cast<int>(kOosGrab), static_cast<int>(clean.oosAction));
    CHECK_EQ(static_cast<int>(kLtSpawnCloseLow), static_cast<int>(clean.ledgetechSpawn));
    CHECK_EQ(static_cast<int>(kLtDjAvailable), static_cast<int>(clean.ledgetechDj));
    CHECK_EQ(static_cast<int>(kLtAtkDtilt), static_cast<int>(clean.ledgetechAttack));
    CHECK_EQ(static_cast<int>(kSdPlatLeft), static_cast<int>(clean.shieldDropPlat));
    CHECK_EQ(static_cast<int>(kSdSpaceRandom), static_cast<int>(clean.shieldDropSpacing));
    CHECK_EQ(static_cast<int>(kSdFaceToward), static_cast<int>(clean.shieldDropFacing));

    // Lab (kGeneral) places Offline VS Displays at row 8 and Controls at row 9;
    // Display exposes Frame Meter at row 0, Floating Meters at row 14, and Lab Debug at row 17.
    const ui::Page& gen = ui::pages::kGeneral;
    REQUIRE(gen.count == 13);
    CHECK(strcmp(gen.options[8].name, "Offline VS Displays") == 0);
    CHECK(strcmp(gen.options[9].name, "Controls") == 0);
    const ui::Page& disp = ui::pages::kDisplayOptions;
    REQUIRE(disp.count == 18);
    CHECK(strcmp(disp.options[0].name, "Frame Meter") == 0);
    CHECK(strcmp(disp.options[14].name, "Floating Meters") == 0);
    CHECK_EQ(ui::pages::P_COMBO_METERS, disp.options[14].max);
    CHECK(strcmp(disp.options[17].name, "Lab Debug") == 0);
    CHECK_EQ(ui::pages::P_DEBUG, disp.options[17].max);
    const ui::Page& scen = ui::pages::kScenarios;
    REQUIRE(scen.count == 53);
    CHECK(strcmp(scen.options[0].name, "Preset") == 0);
    CHECK_EQ(kScenarioCount - 1, scen.options[0].max);
    CHECK(strcmp(scen.options[3].name, "Post-Hitstun") == 0);
    CHECK(strcmp(scen.options[4].name, "Edit Air Pool") == 0);
    CHECK_EQ(ui::pages::P_COUNTER_POOL, scen.options[4].max);
    CHECK(strcmp(scen.options[5].name, "Reset Delay") == 0);
    CHECK(strcmp(scen.options[6].name, "Trajectory DI") == 0);
    CHECK(strcmp(scen.options[7].name, "Tech Option") == 0);
    CHECK(strcmp(scen.options[8].name, "Reset P1") == 0);
    CHECK(strcmp(scen.options[9].name, "Noclip Reset") == 0);
    CHECK(strcmp(scen.options[10].name, "Setup") == 0);
    CHECK(strcmp(scen.options[11].name, "Position") == 0);
    CHECK(strcmp(scen.options[12].name, "Escape") == 0);
    CHECK(strcmp(scen.options[24].name, "Role") == 0);
    CHECK(strcmp(scen.options[25].name, "CPU OoS") == 0);
    CHECK_EQ(kOosCount - 1, scen.options[25].max);
    CHECK(strcmp(scen.options[26].name, "Edit Shield Pool") == 0);
    CHECK_EQ(ui::pages::P_COUNTER_POOL, scen.options[26].max);
    CHECK(strcmp(scen.options[27].name, "CPU Attack") == 0);
    CHECK(strcmp(scen.options[28].name, "Start") == 0);
    CHECK(strcmp(scen.options[29].name, "Timing") == 0);
    CHECK(strcmp(scen.options[30].name, "Spawn Position") == 0);
    CHECK(strcmp(scen.options[31].name, "P1 Double Jump") == 0);
    CHECK(strcmp(scen.options[32].name, "CPU Edgeguard") == 0);
    CHECK_EQ(static_cast<int>(kLtAtkDsmash), static_cast<int>(scen.options[32].def));
    CHECK(strcmp(scen.options[33].name, "Platform") == 0);
    CHECK(strcmp(scen.options[34].name, "CPU Attack") == 0);
    CHECK(strcmp(scen.options[35].name, "CPU Spacing") == 0);
    CHECK(strcmp(scen.options[36].name, "CPU Facing") == 0);
    CHECK(strcmp(scen.options[37].name, "CPU Attack") == 0);
    CHECK(strcmp(scen.options[38].name, "CPU Action") == 0);
    CHECK(strcmp(scen.options[39].name, "P1 Damage") == 0);
    CHECK(strcmp(scen.options[40].name, "Multi-Hit Move") == 0);
    CHECK(strcmp(scen.options[42].name, "Practice Mode") == 0);
    CHECK(strcmp(scen.options[51].name, "Target Zone") == 0);
    CHECK(strcmp(scen.options[52].name, "More Combo Options") == 0);
    CHECK_EQ(ui::pages::P_COMBO_TRAINER, scen.options[52].max);
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

    ScenarioConfig slideOnPlat = makeCfg(kScenarioSlideOff, 1, kScenarioSideRight, kMultiHitUpAir);
    slideOnPlat.slideOffAttack = kSlideAtkOnPlatFtilt;
    u32 slideSpRng = 55u;
    computeScenarioSpawnsEx(slideOnPlat, -1, slideSpRng, bf, sp);
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

    // Ledgetech Timed Swing: CPU waits while HMN is still at far spawn offset (13.5u out, -12u low)
    // and throws dtilt as soon as HMN enters ledge recovery proximity.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioLedgetech;
    ci.ledgetechTiming = kLtTimeTimed;
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

TEST(scenario_presets, techchase_resets_on_air_escape_but_keeps_platform_tumble_intact)
{
    StageGeometry bf = makeBattlefieldGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioStepInput in = makeIdleInput(bf);
    ScenarioStepAction act;

    stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
    CHECK_EQ(1u, st.resetCount);

    // CPU is launched into tumble (DamageFly 0x45) onto a platform: must engage
    // techchase WITHOUT advancing graceFrames while tumbling!
    in.cpuAir = true;
    in.cpuStatus = 0x45; // DamageFly
    in.cpuHitstun = 15;
    in.cpuActionable = false;
    for (int f = 0; f < 50; f++)
    {
        stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
        CHECK(!act.warpCpu);
        CHECK_EQ(0u, static_cast<u32>(st.graceFrames));
    }
    // Even after hitstun ends while still in DamageFall (0x49) heading to platform:
    in.cpuHitstun = 0;
    in.cpuStatus = 0x49; // DamageFall
    for (int f = 0; f < 50; f++)
    {
        stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
        CHECK(!act.warpCpu);
        CHECK_EQ(0u, static_cast<u32>(st.graceFrames));
    }

    // Now CPU jumps out into the air (JumpAerial 0x0C, hitstun == 0): MUST auto-reset!
    in.cpuStatus = 0x0C; // JumpAerial
    in.cpuActionable = true;
    for (int f = 0; f < 50 && !act.warpCpu; f++)
    {
        stepScenario(st, makeCfg(kScenarioTechchase), in, cpuOpts, comboCfg, act);
    }
    CHECK(act.warpCpu);
    CHECK_EQ(2u, st.resetCount);
}

TEST(scenario_presets, edgeguard_side_control_ledgetech_options_shield_drop_and_44_fighter_multihit)
{
    StageGeometry bf = makeBattlefieldGeometry();
    StageGeometry ps2 = bf;
    ps2.platformCount = 2; // Left + Right only, no Top platform

    // 1. Edgeguard sampleRecoveryCandidate respects explicit Right (0) vs Left (1) vs Random (2).
    u32 rng = 12345u;
    for (int i = 0; i < 20; i++)
    {
        float x = 0.0f, y = 0.0f;
        CHECK(sampleRecoveryCandidate(bf, rng, x, y, kScenarioSideRight));
        CHECK(x > bf.rightLedge.x);
        CHECK(sampleRecoveryCandidate(bf, rng, x, y, kScenarioSideLeft));
        CHECK(x < bf.leftLedge.x);
    }

    // 2. Shield Pressure / OoS counter indices match exact kCounterShield labels!
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosGrab)], "Grab") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosUpB)], "Up B") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosSpotdodge)], "Spotdodge") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosRollAway)], "Roll Away") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosShNair)], "SH Nair") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosWavedash)], "Wavedash Away") == 0);
    CHECK(strcmp(ui::pages::kCounterShield[resolveOosCounterShield(kOosRandom)], "Random") == 0);

    // 3. Shield Drop Platform (Left / Right / Top) + PS2 absent-Top fallback + Under/Close/Far spacing + Facing.
    ScenarioConfig sd = makeCfg(kScenarioShieldDrop);
    sd.shieldDropPlat = kSdPlatTop;
    sd.shieldDropSpacing = kSdSpaceUnder;
    sd.shieldDropFacing = kSdFaceAway;
    ScenarioSpawns sp;
    computeScenarioSpawnsEx(sd, -1, rng, bf, sp);
    CHECK(sp.hasPlatform);
    CHECK_EQ(static_cast<u8>(kPlatSlotTop), sp.resolvedPlatformSlot);
    CHECK(fabsf(sp.hmnY - 54.4f) < 0.1f);
    CHECK(fabsf(sp.cpuX - sp.hmnX) < 0.1f); // Directly Under!

    // On 2-platform stage (no Top), requesting Top cleanly falls back to Left!
    computeScenarioSpawnsEx(sd, -1, rng, ps2, sp);
    CHECK(sp.hasPlatform);
    CHECK_EQ(static_cast<u8>(kPlatSlotLeft), sp.resolvedPlatformSlot);

    // 4. Ledgetech 4 Spawn Positions + Double Jump Spent + CPU Edgeguard attacks.
    ScenarioConfig lt = makeCfg(kScenarioLedgetech);
    lt.ledgetechSpawn = kLtSpawnDeepLow;
    lt.ledgetechDj = kLtDjSpent;
    computeScenarioSpawnsEx(lt, -1, rng, bf, sp);
    CHECK(sp.hmnFalling);
    CHECK(sp.hmnY < bf.rightLedge.y - 30.0f);
    lt.ledgetechSpawn = kLtSpawnHigh;
    computeScenarioSpawnsEx(lt, -1, rng, bf, sp);
    CHECK(sp.hmnY > bf.rightLedge.y + 5.0f);
    lt.ledgetechSpawn = kLtSpawnFarOut;
    computeScenarioSpawnsEx(lt, -1, rng, bf, sp);
    CHECK(sp.hmnX > bf.rightLedge.x + 20.0f);

    // 5. Complete P+ 3.2.1 Multi-Hit SDI Auto table key matchups!
    CHECK_EQ(static_cast<u8>(kMultiHitNair), bestMultiHitMoveForFighter(0x1F));  // Ivysaur -> Nair
    CHECK_EQ(static_cast<u8>(kMultiHitUpB), bestMultiHitMoveForFighter(0x15));   // Wario -> Up B
    CHECK_EQ(static_cast<u8>(kMultiHitSideB), bestMultiHitMoveForFighter(0x16)); // Meta Knight -> Side B
    CHECK_EQ(static_cast<u8>(kMultiHitDownAir), bestMultiHitMoveForFighter(0x06)); // Fox -> Dair
    CHECK_EQ(static_cast<u8>(kMultiHitNair), bestMultiHitMoveForFighter(0x1A));  // Lucas -> Nair
    CHECK_EQ(static_cast<u8>(kMultiHitFair), bestMultiHitMoveForFighter(0x07));  // Pikachu -> Fair
    CHECK_EQ(static_cast<u8>(kMultiHitUpAir), bestMultiHitMoveForFighter(0x03)); // Samus -> Uair
    CHECK_EQ(static_cast<u8>(kMultiHitDownAir), bestMultiHitMoveForFighter(0x0C)); // Peach -> Dair
    CHECK_EQ(static_cast<u8>(kMultiHitNair), bestMultiHitMoveForFighter(0x26));  // Mewtwo -> Nair
    CHECK_EQ(static_cast<u8>(kMultiHitDownB), bestMultiHitMoveForFighter(0x08)); // Luigi -> Down B
    CHECK_EQ(static_cast<u8>(kMultiHitRapidJab), bestMultiHitMoveForFighter(0x0E)); // Sheik -> Rapid Jab

    // Verify Auto resolves and fires Up B for Wario (0x15) and Nair for Ivysaur (0x1F).
    ScenarioCpuState cs;
    ScenarioCpuInput ci;
    ScenarioCpuOutput co;
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioMultiHitSdi;
    ci.multiHitMove = kMultiHitAuto;
    ci.cpuFtKind = 0x15; // Wario -> Up B
    ci.cpuCanAct = true;
    ci.cpuLr = 1.0f;
    ci.cpuX = -2.5f;
    ci.hmnX = 2.5f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kSpecial), co.buttons);
    CHECK(co.stickY > 0.9f);

    scenarioCpuStateReset(cs);
    ci.cpuFtKind = 0x1F; // Ivysaur -> Nair (jump then neutral A in air)
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(static_cast<u32>(cpu::button::kJump), co.buttons);
    ci.cpuAir = true;
    ci.cpuCanAct = false;
    stepScenarioCpu(ci, cs, co);
    CHECK_EQ(static_cast<u32>(cpu::button::kAttack), co.buttons);
    CHECK(fabsf(co.stickX) < 0.01f && fabsf(co.stickY) < 0.01f);

    // 6. Unified Shield Pressure / OoS auto-resets after CPU Up-B returns to Guard.
    ScenarioState stOos;
    scenarioStateInit(stOos);
    ScenarioConfig oosCfg = makeCfg(kScenarioShieldPressure);
    oosCfg.oosAction = kOosUpB;
    cpu::CpuOptions oosCpu = makeCustomCpuOpts();
    main::ComboTrainerSettings oosCombo = makeCustomComboCfg();
    ScenarioStepInput oosIn = makeIdleInput(bf);
    ScenarioStepAction oosAct;
    stepScenario(stOos, oosCfg, oosIn, oosCpu, oosCombo, oosAct); // initial warp
    CHECK_EQ(1u, stOos.resetCount);
    oosIn.cpuStatus = 0x1D; // GuardDamage
    oosIn.cpuShieldStun = 6;
    stepScenario(stOos, oosCfg, oosIn, oosCpu, oosCombo, oosAct);
    oosIn.cpuStatus = 0x114; // SpecialHi (Up B OoS)
    oosIn.cpuShieldStun = 0;
    stepScenario(stOos, oosCfg, oosIn, oosCpu, oosCombo, oosAct);
    CHECK(stOos.oosCountered);
    oosIn.cpuStatus = 0x1B; // Back in Guard (Behavior Shield)
    for (int f = 0; f < 40 && !oosAct.warpHmn; f++)
    {
        stepScenario(stOos, oosCfg, oosIn, oosCpu, oosCombo, oosAct);
    }
    CHECK(oosAct.warpHmn);
    CHECK_EQ(2u, stOos.resetCount);
}

TEST(scenario_presets, grab_defense_cpu_approach_delay_hold_throw_and_yoshi)
{
    // 1. CPU approach + 40..99f randomized pre-grab delay + Hold (zero pummel) vs Throw.
    ScenarioCpuState cs;
    ScenarioCpuInput ci;
    ScenarioCpuOutput co;
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioGrabEscape;
    ci.grabAction = kGrabActHold;
    ci.cpuCanAct = true;
    ci.cpuX = 12.0f;
    ci.hmnX = -12.0f;
    ci.cpuLr = -1.0f;

    // First frame sets a randomized 40..99f delay and does not grab immediately.
    ci.rng = 12345u;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active);
    CHECK_EQ(0u, co.buttons);
    CHECK(cs.delay >= 40 && cs.delay <= 99);

    // While counting down delay, CPU stays neutral/non-grabbing.
    int delay = cs.delay;
    for (int f = 1; f < delay; f++)
    {
        stepScenarioCpu(ci, cs, co);
        CHECK_EQ(0u, co.buttons);
    }

    // Once delay finishes and far (>11u), CPU approaches toward P1.
    stepScenarioCpu(ci, cs, co);
    CHECK(co.stickX < -0.5f && co.buttons == 0);

    // Within grab range (<=11u) facing P1: presses Grab (Guard | Attack).
    ci.cpuX = -4.0f;
    ci.hmnX = -12.0f;
    stepScenarioCpu(ci, cs, co);
    CHECK((co.buttons & (cpu::button::kGuard | cpu::button::kAttack)) == (cpu::button::kGuard | cpu::button::kAttack));

    // In Hold mode while CPU holds P1 (CatchWait 0x39): zero buttons/stick (no pummel!).
    ci.cpuStatus = 0x39;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active && co.buttons == 0 && fabsf(co.stickX) < 0.01f && fabsf(co.stickY) < 0.01f);

    // In U-Throw mode during CatchWait after brief hold: inputs stickY = +1.0f.
    ci.grabAction = kGrabActUThrow;
    cs.timer = 10;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.stickY > 0.9f);

    // In Stand mode (P1 Grabs CPU): CPU driver stays inactive.
    ci.grabAction = kGrabActStand;
    stepScenarioCpu(ci, cs, co);
    CHECK(!co.active);

    // 2. Grab Defense tracks P1 grabbed (including Yoshi tongue 0x107..0x10A) and resets on escape.
    StageGeometry fd = makeFdGeometry();
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    main::GeneralSettings gen;
    main::CpuSettings cpuSet;
    main::OsdSettings osd;
    memset(&gen, 0, sizeof(gen));
    memset(&cpuSet, 0, sizeof(cpuSet));
    memset(&osd, 0, sizeof(osd));
    ScenarioState st;
    scenarioStateInit(st);
    ScenarioConfig gCfg = makeCfg(kScenarioGrabEscape);
    gCfg.grabAction = kGrabActHold;
    gCfg.grabPctMode = kGrabPctRandom;
    applyScenarioConfig(st, gCfg, cpuOpts, comboCfg, &gen, &cpuSet, &osd);
    CHECK_EQ(1, static_cast<int>(osd.grabBreakout));

    ScenarioStepInput in = makeIdleInput(fd);
    ScenarioStepAction act;
    stepScenario(st, gCfg, in, cpuOpts, comboCfg, act); // initial warp
    in.hmnStatus = 0x108; // Yoshi SwallowCapture
    stepScenario(st, gCfg, in, cpuOpts, comboCfg, act);
    CHECK_EQ(1u, st.grabEscapeFrames);
    in.hmnStatus = 0x00; // escaped
    stepScenario(st, gCfg, in, cpuOpts, comboCfg, act);
    CHECK(st.sawGrabEscape);
    CHECK_EQ(1u, st.completionCount);

    // Restore snapshot restores osd.grabBreakout back to 0.
    restoreScenarioSnapshot(st, cpuOpts, comboCfg, &gen, &cpuSet, &osd);
    CHECK_EQ(0, static_cast<int>(osd.grabBreakout));
}

TEST(scenario_presets, techchase_platforms_tumble_drop_timing_streak_and_inline_invis)
{
    StageGeometry bf = makeBattlefieldGeometry();
    // 1. Dynamic platform sorting: lowest-first, left-to-right (BF: Left -> Right -> Top).
    StagePlatform sorted[kMaxStagePlatforms];
    int nPlats = sortPlatformsLowestLeftToRight(bf, sorted);
    CHECK_EQ(3, nPlats);
    CHECK(sorted[0].leftX < 0.0f && fabsf(sorted[0].y - 27.2f) < 0.1f);
    CHECK(sorted[1].leftX > 0.0f && fabsf(sorted[1].y - 27.2f) < 0.1f);
    CHECK(fabsf(sorted[2].y - 54.4f) < 0.1f);

    // Position stepping cycles Ground -> Platform 1 -> 2 -> 3 -> Ground cleanly.
    CHECK_EQ(1, static_cast<int>(stepTechPosition(0, 3, 1)));
    CHECK_EQ(3, static_cast<int>(stepTechPosition(0, 3, -1)));
    CHECK_EQ(0, static_cast<int>(stepTechPosition(3, 3, 1)));
    CHECK_EQ(0, static_cast<int>(stepTechPosition(0, 0, 1)));

    // 2. Tumble Drop spawns CPU airborne in DamageFall above ground or chosen platform.
    ScenarioConfig tc = makeCfg(kScenarioTechchase);
    tc.techSetup = kTechSetupTumbleDrop;
    tc.techPosition = 2; // BF right side platform
    tc.techEscape = kTechEscIntangFlash;
    tc.techInvisMode = main::kTechInvisFlash;
    tc.techInvisDelay = 2;
    tc.techSound = 1;
    tc.techPreDrift = main::kPreTechDriftAwayTech;
    tc.techTdi = kTechTdiSurvival;
    u32 rng = 777u;
    ScenarioSpawns sp;
    computeScenarioSpawnsEx(tc, -1, rng, bf, sp);
    CHECK(sp.valid && sp.hasPlatform && sp.cpuFalling && sp.cpuTumbleDrop);
    CHECK_EQ(2, static_cast<int>(sp.resolvedTechPos));
    CHECK(sp.cpuY > 40.0f); // above right platform (27.2 + 18)
    CHECK(fabsf(sp.hmnY) < 0.1f);

    // Inline Tech Invisibility / Pre-Tech Drift / Launch DI snapshot & restore without leak.
    ScenarioState st;
    scenarioStateInit(st);
    cpu::CpuOptions cpuOpts = makeCustomCpuOpts();
    main::ComboTrainerSettings comboCfg = makeCustomComboCfg();
    main::TechInvisSettings invis = {main::kTechInvisOff, 0, main::kPreTechDriftOff, 0};
    applyScenarioConfig(st, tc, cpuOpts, comboCfg, NULL, NULL, NULL, &invis);
    CHECK_EQ(static_cast<int>(main::kTechInvisFlash), static_cast<int>(invis.mode));
    CHECK_EQ(2, static_cast<int>(invis.delay));
    CHECK_EQ(static_cast<int>(main::kPreTechDriftAwayTech), static_cast<int>(invis.preTechDrift));
    CHECK_EQ(1, static_cast<int>(cpuOpts.techSound));
    CHECK_EQ(cpu::kTdiSurvival, cpuOpts.tdi);
    CHECK_EQ(0, cpuOpts.counterGround); // Intangible Flash leaves counterGround passive

    // Counter escape responses map cleanly to exact kCounterGround indices.
    CHECK(strcmp(ui::pages::kCounterGround[resolveTechEscapeCounter(kTechEscSpotdodge)], "Spotdodge") == 0);
    CHECK(strcmp(ui::pages::kCounterGround[resolveTechEscapeCounter(kTechEscJab)], "Jab") == 0);
    CHECK(strcmp(ui::pages::kCounterGround[resolveTechEscapeCounter(kTechEscJump)], "Short Hop") == 0);
    CHECK(strcmp(ui::pages::kCounterGround[resolveTechEscapeCounter(kTechEscShield)], "Shield") == 0);
    CHECK(strcmp(ui::pages::kCounterGround[resolveTechEscapeCounter(kTechEscRoll)], "Roll Random") == 0);

    // 3. True cover via launching airborne hit (DamageFly 0x45, cpuAir = true) increments
    //    streak/best + computes Reaction Frame; missed-tech knockdown cover leaves REACT = -1;
    //    late cover triggers Intangible Flash + resets streak.
    ScenarioStepInput in = makeIdleInput(bf);
    in.cpuTechDist = 4; // Fox distinguishable frame 4
    ScenarioStepAction act;
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis); // initial warp
    in.cpuStatus = 0x60; // Passive tech for 20 frames
    for (int f = 0; f < 20; f++)
    {
        stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    }
    // P1 hits CPU cleanly with a launching move (DamageFly 0x45, cpuAir = true) -> COVER, REACT = 16f, STREAK = 1!
    in.cpuStatus = 0x45;
    in.cpuAir = true;
    in.cpuHitstun = 14;
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    CHECK_EQ(static_cast<int>(kTechResCover), static_cast<int>(st.techResult));
    CHECK_EQ(16, static_cast<int>(st.techReactionFrames));
    CHECK_EQ(1, static_cast<int>(st.techStreak));
    CHECK_EQ(1, static_cast<int>(st.techBest));

    // Missed-tech knockdown (Down_Wait 0x4D -> Down_Stand 0x4F) covered by airborne launch:
    // increments streak to 2 and keeps techReactionFrames == -1 (never corrupted by Down frames).
    requestScenarioManualReset(st);
    in.cpuAir = false;
    in.cpuHitstun = 0;
    in.cpuStatus = 0x00;
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis); // warp
    in.cpuStatus = 0x4D; // Down_Wait for 12 frames
    for (int f = 0; f < 12; f++)
    {
        stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    }
    in.cpuStatus = 0x45;
    in.cpuAir = true;
    in.cpuHitstun = 14;
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    CHECK_EQ(static_cast<int>(kTechResCover), static_cast<int>(st.techResult));
    CHECK_EQ(-1, static_cast<int>(st.techReactionFrames));
    CHECK_EQ(2, static_cast<int>(st.techStreak));
    CHECK_EQ(2, static_cast<int>(st.techBest));

    // Next tech: P1 is 3 frames late -> triggers techFlashCpu on frame 1 actionable, records 3f Late, resets streak!
    requestScenarioManualReset(st);
    in.cpuAir = false;
    in.cpuHitstun = 0;
    in.cpuStatus = 0x00;
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis); // warp
    in.cpuStatus = 0x61; // Passive_Fb
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    in.cpuStatus = 0x00; // Wait (actionable)
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    CHECK(act.techFlashCpu);
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    in.hmnStatus = 0x24; // Attack (3rd actionable frame)
    stepScenario(st, tc, in, cpuOpts, comboCfg, act, NULL, NULL, NULL, &invis);
    CHECK_EQ(static_cast<int>(kTechResLate), static_cast<int>(st.techResult));
    CHECK_EQ(3, static_cast<int>(st.techDeltaFrames));
    CHECK_EQ(0, static_cast<int>(st.techStreak));
    CHECK_EQ(2, static_cast<int>(st.techBest)); // best preserved!

    // Restore snapshot restores TechInvisSettings byte-for-byte.
    restoreScenarioSnapshot(st, cpuOpts, comboCfg, NULL, NULL, NULL, &invis);
    CHECK_EQ(static_cast<int>(main::kTechInvisOff), static_cast<int>(invis.mode));
    CHECK_EQ(0, static_cast<int>(invis.delay));
    CHECK_EQ(static_cast<int>(main::kPreTechDriftOff), static_cast<int>(invis.preTechDrift));
}

TEST(scenario_presets, edgeguard_ledgetech_shield_defense_shield_drop_and_underplat_slideoff)
{
    StageGeometry bf = makeBattlefieldGeometry();
    u32 rng = 2468u;

    // 1. Edgeguard Knockback Launch speeds scale Weak < Normal < Strong, and Offstage Spawn honors Mid/Deep/High.
    EdgeguardLaunchParams w, n, s;
    computeEdgeguardLaunch(kEgStrWeak, bf.rightLedge, 1.0f, rng, w);
    computeEdgeguardLaunch(kEgStrNormal, bf.rightLedge, 1.0f, rng, n);
    computeEdgeguardLaunch(kEgStrStrong, bf.rightLedge, 1.0f, rng, s);
    CHECK(w.speed < n.speed && n.speed < s.speed);
    CHECK(w.kbX > 0.0f && w.kbY > 0.0f);
    CHECK(w.hitstun < n.hitstun && n.hitstun < s.hitstun);

    float dx = 0.0f, dy = 0.0f, hx = 0.0f, hy = 0.0f;
    CHECK(sampleEdgeguardSpawn(bf, kEgDistDeep, kScenarioSideRight, rng, dx, dy));
    CHECK(sampleEdgeguardSpawn(bf, kEgDistHigh, kScenarioSideRight, rng, hx, hy));
    CHECK(dy < bf.rightLedge.y - 10.0f);
    CHECK(hy > bf.rightLedge.y + 10.0f);

    // 2. Ledgetech Revival Platform + Hold Active predicate + Down Smash default spacing.
    ScenarioConfig lt = makeCfg(kScenarioLedgetech);
    lt.ledgetechStart = kLtStartRevival;
    lt.ledgetechTiming = kLtTimeHoldActive;
    lt.ledgetechAttack = kLtAtkDsmash;
    ScenarioSpawns sp;
    computeScenarioSpawnsEx(lt, -1, rng, bf, sp);
    CHECK(sp.hmnRevivalPlat);
    CHECK(fabsf((bf.rightLedge.x - sp.cpuX) - 4.2f) < 0.1f);
    CHECK(shouldHoldLedgetechActive(kLtTimeHoldActive, true, false, false, false));
    CHECK(!shouldHoldLedgetechActive(kLtTimeHoldActive, true, true, false, false));  // hit releases freeze
    CHECK(!shouldHoldLedgetechActive(kLtTimeHoldActive, true, false, true, false));  // ledge grab releases freeze
    CHECK(!shouldHoldLedgetechActive(kLtTimeTimed, true, false, false, false));

    // 3. Shield Pressure / OoS: Offense Custom Pool == 49; Defense 45-74f randomized attack driver.
    CHECK_EQ(kCounterShieldCustomPool, resolveOosCounterShield(kOosCustomPool));
    CHECK(strcmp(ui::pages::kCounterShield[kCounterShieldCustomPool], "Custom Pool") == 0);
    ScenarioCpuState cs;
    ScenarioCpuInput ci;
    ScenarioCpuOutput co;
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioShieldPressure;
    ci.shieldRole = kShieldRoleDefense;
    ci.shieldDefAttack = kShieldDefTilt;
    ci.cpuCanAct = true;
    ci.cpuLr = -1.0f;
    ci.cpuX = 6.0f;
    ci.hmnX = -4.0f;
    ci.rng = 1001u;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active && co.buttons == 0);
    CHECK(cs.delay >= 45 && cs.delay <= 74);
    int defDelay = cs.delay;
    for (int f = 1; f < defDelay - 1; f++)
    {
        stepScenarioCpu(ci, cs, co);
        CHECK_EQ(0u, co.buttons);
    }
    stepScenarioCpu(ci, cs, co);
    CHECK_EQ(static_cast<u32>(cpu::button::kAttack), co.buttons);

    // 4. Active Shield Drop CPU attacks (SH Aerial) vs Off.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioShieldDrop;
    ci.shieldDropAttack = kSdAtkShAerial;
    ci.cpuCanAct = true;
    ci.cpuX = 35.0f;
    ci.hmnX = 35.0f;
    ci.rng = 333u;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active && cs.delay >= 30 && cs.delay <= 65);
    int sdDelay = cs.delay;
    for (int f = 1; f < sdDelay - 1; f++)
    {
        stepScenarioCpu(ci, cs, co);
    }
    stepScenarioCpu(ci, cs, co);
    CHECK_EQ(static_cast<u32>(cpu::button::kJump), co.buttons);
    ci.cpuAir = true;
    stepScenarioCpu(ci, cs, co);
    CHECK((co.buttons & cpu::button::kCStick) != 0 && co.stickY > 0.9f);

    ci.shieldDropAttack = kSdAtkOff;
    stepScenarioCpu(ci, cs, co);
    CHECK(!co.active);

    // 5. Under-Platform Slide-Off: full-hop from main floor + Up Air up through platform.
    scenarioCpuStateReset(cs);
    memset(&ci, 0, sizeof(ci));
    ci.preset = kScenarioSlideOff;
    ci.slideOffAttack = kSlideAtkUnderUair;
    ci.hasPlatform = true;
    ci.cpuCanAct = true;
    ci.cpuX = 26.0f;
    ci.cpuY = 0.0f;
    ci.hmnX = 24.0f;
    ci.hmnY = 27.2f;
    stepScenarioCpu(ci, cs, co);
    CHECK(co.active && co.buttons == static_cast<u32>(cpu::button::kJump));
    ci.cpuAir = true;
    ci.cpuCanAct = false;
    bool firedUnderUair = false;
    for (int f = 0; f < 12; f++)
    {
        ci.cpuY += 3.0f;
        stepScenarioCpu(ci, cs, co);
        if ((co.buttons & cpu::button::kCStick) != 0 && co.stickY > 0.9f)
        {
            firedUnderUair = true;
            break;
        }
    }
    CHECK(firedUnderUair);

    // 6. Shield Pressure Defense with Auto-Reset Off clears shieldDefSwung after grace so CPU is not permanently intangible.
    ScenarioState defSt;
    scenarioStateInit(defSt);
    main::Settings defSettings;
    memset(&defSettings, 0, sizeof(defSettings));
    ScenarioConfig defCfg = makeCfg(kScenarioShieldPressure, 0);
    defCfg.shieldRole = kShieldRoleDefense;
    ScenarioStepInput defIn = makeIdleInput(bf);
    ScenarioStepAction defAct;
    tm::extra::stepScenario(defSt, defCfg, defIn, defSettings, defAct); // initial warp
    defIn.cpuStatus = 0x27; // AttackS3
    tm::extra::stepScenario(defSt, defCfg, defIn, defSettings, defAct);
    CHECK(defSt.shieldDefSwung);
    defIn.cpuStatus = 0x00; // back to Wait (actionable)
    for (int f = 0; f < 32; f++)
    {
        tm::extra::stepScenario(defSt, defCfg, defIn, defSettings, defAct);
        CHECK(!defAct.warpCpu);
    }
    CHECK(!defSt.shieldDefSwung);

    // 7. Leaving Grab Defense (which writes explicit rolled hmnDmg without changing Settings)
    //    forces applyHmnPercent = true so live RAM damage restores cleanly to 0%.
    ScenarioState grSt;
    scenarioStateInit(grSt);
    main::Settings grSettings;
    memset(&grSettings, 0, sizeof(grSettings));
    ScenarioConfig grCfg = makeCfg(kScenarioGrabEscape, 1);
    grCfg.grabAction = kGrabActHold;
    grCfg.grabPctMode = kGrabPctRandom;
    ScenarioStepInput grIn = makeIdleInput(bf);
    ScenarioStepAction grAct;
    tm::extra::stepScenario(grSt, grCfg, grIn, grSettings, grAct);
    CHECK(grAct.useExplicitHmnDmg);
    ScenarioConfig offCfg = makeCfg(kScenarioOff, 1);
    tm::extra::stepScenario(grSt, offCfg, grIn, grSettings, grAct);
    CHECK(grAct.applyHmnPercent && grAct.applyCpuPercent);
}

namespace {
    void checkPlateAsciiAndBounds(const ScenarioHudPlate& plate)
    {
        CHECK_EQ(3, plate.count);
        for (int i = 0; i < plate.count; i++)
        {
            REQUIRE(plate.rows[i].label != NULL);
            CHECK(plate.rows[i].label[0] != '\0');
            CHECK(plate.rows[i].value[0] != '\0');
            CHECK(strlen(plate.rows[i].value) < static_cast<size_t>(kScenarioPlateValBuf));
            for (const char* p = plate.rows[i].label; *p != '\0'; p++)
            {
                unsigned char ch = static_cast<unsigned char>(*p);
                CHECK(ch >= 0x20u && ch < 0x7Fu);
            }
            for (const char* p = plate.rows[i].value; *p != '\0'; p++)
            {
                unsigned char ch = static_cast<unsigned char>(*p);
                CHECK(ch >= 0x20u && ch < 0x7Fu);
            }
        }
    }
}

TEST(scenario_presets, panel_b_stat_plates_cover_all_seven_remaining_scenarios)
{
    // Top-band claim belongs exclusively to Techchase; Combo Lab & Off stay quiet;
    // Techchase + all 7 remaining scenarios expose a 3-row Panel B stat plate.
    CHECK(!scenarioHasTopBand(kScenarioOff));
    CHECK(!scenarioHasTopBand(kScenarioComboLab));
    CHECK(scenarioHasTopBand(kScenarioTechchase));
    CHECK(!scenarioHasStatPlate(kScenarioOff));
    CHECK(!scenarioHasStatPlate(kScenarioComboLab));
    CHECK(scenarioHasStatPlate(kScenarioTechchase));
    for (u8 p = kScenarioEdgeguard; p < kScenarioCount; p++)
    {
        CHECK(!scenarioHasTopBand(p));
        CHECK(scenarioHasStatPlate(p));
    }

    ScenarioState st;
    ScenarioHudPlate plate;
    scenarioStateInit(st);

    // Off, Combo Lab, Techchase, and out-of-range return false from formatScenarioStatPlate.
    static const u8 kExcluded[4] = {kScenarioOff, kScenarioComboLab, kScenarioTechchase, kScenarioCount};
    for (int i = 0; i < 4; i++)
    {
        st.activePreset = kExcluded[i];
        CHECK(!formatScenarioStatPlate(st, makeCfg(kExcluded[i]), plate));
        CHECK_EQ(0, plate.count);
    }

    // 1. Edgeguard: Launch vs Offstage Spawn (Dist / DJ), Side, Cleared.
    scenarioStateInit(st);
    st.activePreset = kScenarioEdgeguard;
    st.resetCount = 1;
    st.resolvedSide = kScenarioSideRight;
    ScenarioConfig egCfg = makeCfg(kScenarioEdgeguard);
    egCfg.edgeguardSetup = kEgSetupLaunch;
    CHECK(formatScenarioStatPlate(st, egCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].label, "Mode") == 0 && strcmp(plate.rows[0].value, "Launch") == 0);
    CHECK(strcmp(plate.rows[1].label, "Side") == 0 && strcmp(plate.rows[1].value, "Right") == 0);
    CHECK(strcmp(plate.rows[2].label, "Cleared") == 0 && strcmp(plate.rows[2].value, "0 / 1") == 0);
    CHECK_EQ(kScenarioPlateWhite, plate.rows[2].valueColor);

    egCfg.edgeguardSetup = kEgSetupSpawn;
    egCfg.edgeguardDist = kEgDistDeep;
    egCfg.edgeguardDj = kEgDjSpent;
    st.resolvedSide = kScenarioSideLeft;
    st.completionCount = 2;
    st.resetCount = 3;
    CHECK(formatScenarioStatPlate(st, egCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Deep / NoDJ") == 0);
    CHECK(strcmp(plate.rows[1].value, "Left") == 0);
    CHECK(strcmp(plate.rows[2].value, "2 / 3") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[2].valueColor);

    // 2. Shield Pressure / OoS: Offense OoS labels, Defense attacks (including Random), Hits.
    scenarioStateInit(st);
    st.activePreset = kScenarioShieldPressure;
    ScenarioConfig spCfg = makeCfg(kScenarioShieldPressure);
    spCfg.shieldRole = kShieldRoleOffense;
    spCfg.oosAction = kOosWavedash;
    CHECK(formatScenarioStatPlate(st, spCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].label, "Role") == 0 && strcmp(plate.rows[0].value, "Offense") == 0);
    CHECK(strcmp(plate.rows[1].label, "CPU") == 0 && strcmp(plate.rows[1].value, "Wavedash") == 0);
    CHECK(strcmp(plate.rows[2].label, "Hits") == 0 && strcmp(plate.rows[2].value, "0") == 0);
    CHECK_EQ(kScenarioPlateWhite, plate.rows[2].valueColor);

    spCfg.oosAction = kOosCustomPool;
    st.shieldHitCount = 7;
    CHECK(formatScenarioStatPlate(st, spCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[1].value, "Custom") == 0);
    CHECK(strcmp(plate.rows[2].value, "7") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[2].valueColor);

    spCfg.shieldRole = kShieldRoleDefense;
    spCfg.shieldDefAttack = kShieldDefRandom;
    st.resetCount = 1;
    st.resolvedShieldDefAtk = kShieldDefGetup;
    CHECK(formatScenarioStatPlate(st, spCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Defense") == 0);
    CHECK(strcmp(plate.rows[1].value, "Getup Atk") == 0);

    // 3. Shield Drop: Ready vs Dropped, Plat %u vs No Plat amber warning, Cleared.
    scenarioStateInit(st);
    st.activePreset = kScenarioShieldDrop;
    st.resetCount = 1;
    st.hasPlatform = false;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioShieldDrop), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].label, "Status") == 0 && strcmp(plate.rows[0].value, "Ready") == 0);
    CHECK_EQ(kScenarioPlateMuted, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].label, "Plat") == 0 && strcmp(plate.rows[1].value, "No Plat") == 0);
    CHECK_EQ(kScenarioPlateAmber, plate.rows[1].valueColor);
    CHECK(strcmp(plate.rows[2].value, "0 / 1") == 0);

    st.hasPlatform = true;
    st.resolvedPlatformSlot = 2; // Top -> Plat 3
    st.sawShieldDrop = true;
    st.completionCount = 1;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioShieldDrop), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Dropped") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "Plat 3") == 0);
    CHECK_EQ(kScenarioPlateWhite, plate.rows[1].valueColor);
    CHECK(strcmp(plate.rows[2].value, "1 / 1") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[2].valueColor);

    // 4. Ledgetech: Ready vs Wall Tech green, resolved Attack label, Cleared.
    scenarioStateInit(st);
    st.activePreset = kScenarioLedgetech;
    ScenarioConfig ltCfg = makeCfg(kScenarioLedgetech);
    ltCfg.ledgetechAttack = kLtAtkRandom;
    CHECK(formatScenarioStatPlate(st, ltCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Ready") == 0);
    CHECK(strcmp(plate.rows[1].label, "Attack") == 0 && strcmp(plate.rows[1].value, "Random") == 0);

    st.resetCount = 4;
    st.completionCount = 3;
    st.resolvedLedgetechAttack = kLtAtkDsmash;
    st.sawWallTech = true;
    CHECK(formatScenarioStatPlate(st, ltCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Wall Tech") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "D-Smash") == 0);
    CHECK(strcmp(plate.rows[2].value, "3 / 4") == 0);

    // 5. Slide-Off: Ready vs Slide-Off green, Plat %u vs No Plat amber, Cleared.
    scenarioStateInit(st);
    st.activePreset = kScenarioSlideOff;
    st.resetCount = 1;
    st.hasPlatform = false;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioSlideOff), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Ready") == 0);
    CHECK(strcmp(plate.rows[1].value, "No Plat") == 0);
    CHECK_EQ(kScenarioPlateAmber, plate.rows[1].valueColor);

    st.hasPlatform = true;
    st.resolvedPlatformSlot = 0;
    st.sawSlideOff = true;
    st.completionCount = 1;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioSlideOff), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Slide-Off") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "Plat 1") == 0);

    // 6. Grab Escape: Ready -> Held -> %uf Out green, Action label, Cleared.
    scenarioStateInit(st);
    st.activePreset = kScenarioGrabEscape;
    st.resetCount = 2;
    ScenarioConfig grCfg = makeCfg(kScenarioGrabEscape);
    grCfg.grabAction = kGrabActHold;
    CHECK(formatScenarioStatPlate(st, grCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Ready") == 0);
    CHECK(strcmp(plate.rows[1].label, "Action") == 0 && strcmp(plate.rows[1].value, "Hold") == 0);

    st.grabActive = true;
    st.grabEscapeFrames = 14;
    CHECK(formatScenarioStatPlate(st, grCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "14f Held") == 0);
    CHECK_EQ(kScenarioPlateGold, plate.rows[0].valueColor);

    st.grabActive = false;
    st.sawGrabEscape = true;
    st.grabEscapeFrames = 22;
    st.completionCount = 1;
    grCfg.grabAction = kGrabActRandThrow;
    CHECK(formatScenarioStatPlate(st, grCfg, plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "22f Out") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "Rand Throw") == 0);
    CHECK(strcmp(plate.rows[2].value, "1 / 2") == 0);

    // 7. Multi-Hit SDI: Ready/-- -> %u Hits / displacement -> Escaped green + clamp.
    scenarioStateInit(st);
    st.activePreset = kScenarioMultiHitSdi;
    st.resetCount = 1;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioMultiHitSdi), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Ready") == 0);
    CHECK(strcmp(plate.rows[1].label, "SDI") == 0 && strcmp(plate.rows[1].value, "--") == 0);

    st.sdiHitCount = 1;
    st.sdiHitlagFrames = 3;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioMultiHitSdi), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "1 Hit") == 0);
    CHECK_EQ(kScenarioPlateGold, plate.rows[0].valueColor);

    st.sdiHitCount = 3;
    st.sdiHitlagFrames = 9;
    st.sdiDispX = 7.0f;
    st.sdiDispY = 3.5f;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioMultiHitSdi), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "3 Hits") == 0);
    CHECK_EQ(kScenarioPlateGold, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "7.8 u") == 0);
    CHECK_EQ(kScenarioPlateWhite, plate.rows[1].valueColor);

    st.sdiEscaped = true;
    st.completionCount = 1;
    st.sdiDispX = 150.0f;
    st.sdiDispY = 0.0f;
    CHECK(formatScenarioStatPlate(st, makeCfg(kScenarioMultiHitSdi), plate));
    checkPlateAsciiAndBounds(plate);
    CHECK(strcmp(plate.rows[0].value, "Escaped") == 0);
    CHECK_EQ(kScenarioPlateGreen, plate.rows[0].valueColor);
    CHECK(strcmp(plate.rows[1].value, "99.9 u") == 0);
    CHECK(strcmp(plate.rows[2].value, "1 / 1") == 0);
}
