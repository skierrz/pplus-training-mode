// Fighter Mechanics unit tests (src/extra/fighter_mechanics_logic.h,
// src/extra/fighter_mechanics.cpp, kAreaFighterMechanics storage round-trip).
#include <ft/ft_common_data_accesser.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/fighter_mechanics.h"
#include "extra/fighter_mechanics_logic.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"

namespace {
    using namespace tm;

    const extra::WorkOp* findOp(const extra::FighterPlan& plan, u8 kind, u32 id)
    {
        for (int i = 0; i < plan.count; i++)
        {
            if (plan.ops[i].kind == kind && plan.ops[i].id == id)
            {
                return &plan.ops[i];
            }
        }
        return NULL;
    }

    struct DededeDataBlock {
        u8 header[0xA0];
        float table[12];
    };

    DededeDataBlock* makeDededeData()
    {
        DededeDataBlock* b = host::mem::make<DededeDataBlock>();
        memset(b, 0, sizeof(*b));
        b->table[0] = 1.4f;
        b->table[1] = 1.5f;
        b->table[2] = 18.0f;
        b->table[3] = -45.0f;
        b->table[4] = 65.0f;
        b->table[6] = 35.0f; // +0x18 Waddle Dee
        b->table[7] = 10.0f; // +0x1C Waddle Doo
        b->table[8] = 4.0f;  // +0x20 Gordo
        b->table[9] = 0.0f;  // +0x24 Capsule
        u32 tableAddr = reinterpret_cast<u32>(&b->table[0]);
        memcpy(b->header + 0x80, &tableAddr, 4);
        return b;
    }

    host::fakes::FakeFighter& makeDededeFighter()
    {
        host::fakes::FakeFighter& ff = host::fakes::addFighter();
        ff.accesser->setConstant(4015, 35.0f);
        ff.accesser->setConstant(4016, 10.0f);
        ff.accesser->setConstant(4017, 4.0f);
        return ff;
    }
}

TEST(fighter_mechanics, ftkind_dispatch_and_mario_fox_disabled)
{
    // Mario vs Fox on FD must remain disabled so MAIN_DISABLED = (6, 7) holds.
    CHECK(!extra::fighterHasMechanicsRow(extra::kFtMario));
    CHECK(!extra::fighterHasMechanicsRow(Fighter_Fox));
    CHECK(!extra::fighterHasMechanicsRow(extra::kFtRoy));
    CHECK(!extra::fighterHasMechanicsRow(extra::kFtKnuckles));

    // All 17 supported fighters (plus Ice Climbers Nana Throw gated via live partner).
    static const int kSupported[] = {
        extra::kFtPeach,     extra::kFtLuigi,    extra::kFtGameWatch, extra::kFtSamus,
        extra::kFtMewtwo,    extra::kFtLucario,  extra::kFtWario,     extra::kFtDonkeyKong,
        extra::kFtIvysaur,   extra::kFtRob,      extra::kFtLucas,     extra::kFtSquirtle,
        extra::kFtSheik,     extra::kFtDedede,   extra::kFtSnake,     extra::kFtGanondorf,
        extra::kFtOlimar,
    };
    for (u32 i = 0; i < sizeof(kSupported) / sizeof(kSupported[0]); i++)
    {
        CHECK(extra::fighterHasMechanicsRow(kSupported[i]));
    }
}

TEST(fighter_mechanics, all_defaults_and_out_of_range_values_emit_no_ops)
{
    main::FighterMechanicsSettings s;
    memset(&s, 0, sizeof(s));
    extra::FighterPlan plan;

    static const int kFighters[] = {
        extra::kFtPeach,    extra::kFtGameWatch, extra::kFtSamus,   extra::kFtMewtwo,
        extra::kFtLucario,  extra::kFtWario,     extra::kFtDonkeyKong, extra::kFtIvysaur,
        extra::kFtRob,      extra::kFtLucas,     extra::kFtSquirtle, extra::kFtSheik,
        extra::kFtDedede,   extra::kFtSnake,     extra::kFtGanondorf,
    };
    for (u32 i = 0; i < sizeof(kFighters) / sizeof(kFighters[0]); i++)
    {
        extra::buildFighterPlan(kFighters[i], 0x120, true, s, plan);
        CHECK_EQ(0, plan.count);
    }

    // Corrupt / out-of-range values clamp to Default (0) and emit no ops.
    memset(&s, 0xFF, sizeof(s));
    for (u32 i = 0; i < sizeof(kFighters) / sizeof(kFighters[0]); i++)
    {
        extra::buildFighterPlan(kFighters[i], 0x120, true, s, plan);
        CHECK_EQ(0, plan.count);
    }
}

TEST(fighter_mechanics, peach_and_mewtwo_float_hover_status_guards)
{
    main::FighterMechanicsSettings s;
    memset(&s, 0, sizeof(s));
    extra::FighterPlan plan;

    // Peach Float = Always: clears LA-Bit[62] always, refills RA-Basic[2] ONLY in 0x11E.
    s.peachFloat = main::kMechAlways;
    extra::buildFighterPlan(extra::kFtPeach, 0x115, false, s, plan); // Turnip pull
    CHECK_EQ(1, plan.count);
    const extra::WorkOp* op = findOp(plan, extra::kOpFlag, extra::kVarPeachFloatUsed);
    REQUIRE(op != NULL);
    CHECK_EQ(0, op->flagValue);
    CHECK(findOp(plan, extra::kOpInt, extra::kVarPeachFloatFrames) == NULL);

    extra::buildFighterPlan(extra::kFtPeach, extra::kStatusPeachFloat, false, s, plan);
    CHECK_EQ(2, plan.count);
    op = findOp(plan, extra::kOpInt, extra::kVarPeachFloatFrames);
    REQUIRE(op != NULL);
    CHECK_EQ(150, op->intValue);

    // Mewtwo Hover = Unlimited: always clears LA-Bit[115] (HasUsedHover), and
    // refills RA-Basic[2]=60 ONLY in status 0x120 with LA-Bit[97] active.
    s.mewtwoHover = main::kMechAlways;
    extra::buildFighterPlan(extra::kFtMewtwo, 0x11E, true, s, plan); // 0x11E is hover-exit on Mewtwo!
    CHECK_EQ(1, plan.count);
    op = findOp(plan, extra::kOpFlag, extra::kVarMewtwoHoverUsed);
    REQUIRE(op != NULL);
    CHECK_EQ(0, op->flagValue);
    CHECK(findOp(plan, extra::kOpInt, extra::kVarMewtwoHoverFrames) == NULL);

    extra::buildFighterPlan(extra::kFtMewtwo, extra::kStatusMewtwoHover, false, s, plan);
    CHECK_EQ(1, plan.count);
    op = findOp(plan, extra::kOpFlag, extra::kVarMewtwoHoverUsed);
    REQUIRE(op != NULL);
    CHECK_EQ(0, op->flagValue);
    CHECK(findOp(plan, extra::kOpInt, extra::kVarMewtwoHoverFrames) == NULL);

    extra::buildFighterPlan(extra::kFtMewtwo, extra::kStatusMewtwoHover, true, s, plan);
    CHECK_EQ(2, plan.count);
    op = findOp(plan, extra::kOpFlag, extra::kVarMewtwoHoverUsed);
    REQUIRE(op != NULL);
    CHECK_EQ(0, op->flagValue);
    op = findOp(plan, extra::kOpInt, extra::kVarMewtwoHoverFrames);
    REQUIRE(op != NULL);
    CHECK_EQ(60, op->intValue);
}

TEST(fighter_mechanics, every_tier2_fighter_work_var_mappings)
{
    main::FighterMechanicsSettings s;
    extra::FighterPlan plan;
    const extra::WorkOp* op = NULL;

    // G&W Oil Panic: Empty / 1 / 2 / Full
    memset(&s, 0, sizeof(s));
    s.gnwBucket = main::kBucketFull;
    extra::buildFighterPlan(extra::kFtGameWatch, 0, false, s, plan);
    REQUIRE((op = findOp(plan, extra::kOpFloat, extra::kVarGnwBucketCount)) != NULL);
    CHECK_NEAR(3.0f, op->floatValue, 0.001f);
    REQUIRE((op = findOp(plan, extra::kOpFloat, extra::kVarGnwBucketDamage)) != NULL);
    CHECK_NEAR(36.0f, op->floatValue, 0.001f);
    REQUIRE((op = findOp(plan, extra::kOpFlag, extra::kVarGnwBucketFull)) != NULL);
    CHECK_EQ(1, op->flagValue);

    s.gnwBucket = main::kBucket2;
    extra::buildFighterPlan(extra::kFtGameWatch, 0, false, s, plan);
    REQUIRE((op = findOp(plan, extra::kOpFloat, extra::kVarGnwBucketCount)) != NULL);
    CHECK_NEAR(2.0f, op->floatValue, 0.001f);
    REQUIRE((op = findOp(plan, extra::kOpFlag, extra::kVarGnwBucketFull)) != NULL);
    CHECK_EQ(0, op->flagValue);

    // Samus: Charge Shot, Beam, Air Bombs
    memset(&s, 0, sizeof(s));
    s.samusCharge = main::kCharge3Full;
    s.samusBeam = main::kBeamIce;
    s.samusAirBombs = main::kMechAlways;
    extra::buildFighterPlan(extra::kFtSamus, 0, false, s, plan);
    CHECK_EQ(3, plan.count);
    CHECK_EQ(7, findOp(plan, extra::kOpInt, extra::kVarSamusCharge)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpInt, extra::kVarSamusBeam)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarSamusAirBombs)->intValue);

    s.samusCharge = main::kCharge3Half;
    s.samusBeam = main::kBeamPlasma;
    extra::buildFighterPlan(extra::kFtSamus, 0, false, s, plan);
    CHECK_EQ(4, findOp(plan, extra::kOpInt, extra::kVarSamusCharge)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarSamusBeam)->intValue);

    // Mewtwo Shadow Ball: Empty (0/-1/-1/off/off), Half (64/3/3/on/off), Full (127/6/6/on/on)
    memset(&s, 0, sizeof(s));
    s.mewtwoShadowBall = main::kCharge3Empty;
    extra::buildFighterPlan(extra::kFtMewtwo, 0, false, s, plan);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallFrames)->intValue);
    CHECK_EQ(-1, findOp(plan, extra::kOpInt, extra::kVarMewtwoShadowBall)->intValue);
    CHECK_EQ(-1, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallStored)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallHas)->flagValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallFull)->flagValue);
    s.mewtwoShadowBall = main::kCharge3Half;
    extra::buildFighterPlan(extra::kFtMewtwo, 0, false, s, plan);
    CHECK_EQ(64, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallFrames)->intValue);
    CHECK_EQ(3, findOp(plan, extra::kOpInt, extra::kVarMewtwoShadowBall)->intValue);
    CHECK_EQ(3, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallStored)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallHas)->flagValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallFull)->flagValue);
    s.mewtwoShadowBall = main::kCharge3Full;
    extra::buildFighterPlan(extra::kFtMewtwo, 0, false, s, plan);
    CHECK_EQ(127, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallFrames)->intValue);
    CHECK_EQ(6, findOp(plan, extra::kOpInt, extra::kVarMewtwoShadowBall)->intValue);
    CHECK_EQ(6, findOp(plan, extra::kOpInt, extra::kVarMewtwoBallStored)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallHas)->flagValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarMewtwoBallFull)->flagValue);

    // Lucario: Aura Charges (0/60/120) + Aura Sphere (LA-Basic[64..66] + LA-Bit[61..62])
    memset(&s, 0, sizeof(s));
    s.lucarioAura = main::kAura2;
    s.lucarioSphere = main::kCharge2Full;
    extra::buildFighterPlan(extra::kFtLucario, 0, false, s, plan);
    CHECK_NEAR(120.0f, findOp(plan, extra::kOpFloat, extra::kVarLucarioAura)->floatValue, 0.001f);
    CHECK_EQ(16, findOp(plan, extra::kOpInt, extra::kVarLucarioSphereFrames)->intValue);
    CHECK_EQ(4, findOp(plan, extra::kOpInt, extra::kVarLucarioSphere)->intValue);
    CHECK_EQ(4, findOp(plan, extra::kOpInt, extra::kVarLucarioSphereStored)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarLucarioSphereHas)->flagValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarLucarioSphereFull)->flagValue);

    s.lucarioAura = main::kAura1;
    s.lucarioSphere = main::kCharge2Empty;
    extra::buildFighterPlan(extra::kFtLucario, 0, false, s, plan);
    CHECK_NEAR(60.0f, findOp(plan, extra::kOpFloat, extra::kVarLucarioAura)->floatValue, 0.001f);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarLucarioSphereFrames)->intValue);
    CHECK_EQ(-1, findOp(plan, extra::kOpInt, extra::kVarLucarioSphere)->intValue);
    CHECK_EQ(-1, findOp(plan, extra::kOpInt, extra::kVarLucarioSphereStored)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarLucarioSphereHas)->flagValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarLucarioSphereFull)->flagValue);

    // Wario Waft: Empty (0, 0, off), Half (1800, 2, off), Full (3600, 3, on)
    memset(&s, 0, sizeof(s));
    s.warioWaft = main::kCharge3Half;
    extra::buildFighterPlan(extra::kFtWario, 0, false, s, plan);
    CHECK_EQ(1800, findOp(plan, extra::kOpInt, extra::kVarWarioWaftFrames)->intValue);
    CHECK_EQ(2, findOp(plan, extra::kOpInt, extra::kVarWarioWaftStage)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarWarioWaftFull)->flagValue);

    s.warioWaft = main::kCharge3Full;
    extra::buildFighterPlan(extra::kFtWario, 0, false, s, plan);
    CHECK_EQ(3600, findOp(plan, extra::kOpInt, extra::kVarWarioWaftFrames)->intValue);
    CHECK_EQ(3, findOp(plan, extra::kOpInt, extra::kVarWarioWaftStage)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarWarioWaftFull)->flagValue);

    // DK Giant Punch: Empty (0), Full (10)
    memset(&s, 0, sizeof(s));
    s.dkPunch = main::kCharge2Full;
    extra::buildFighterPlan(extra::kFtDonkeyKong, 0, false, s, plan);
    CHECK_EQ(10, findOp(plan, extra::kOpInt, extra::kVarDkPunch)->intValue);

    // Ivysaur Solarbeam: Empty (0), Half (13), Full (26)
    memset(&s, 0, sizeof(s));
    s.ivysaurSolarbeam = main::kCharge3Half;
    extra::buildFighterPlan(extra::kFtIvysaur, 0, false, s, plan);
    CHECK_NEAR(13.0f, findOp(plan, extra::kOpFloat, extra::kVarIvysaurSolarbeam)->floatValue, 0.001f);
    s.ivysaurSolarbeam = main::kCharge3Full;
    extra::buildFighterPlan(extra::kFtIvysaur, 0, false, s, plan);
    CHECK_NEAR(26.0f, findOp(plan, extra::kOpFloat, extra::kVarIvysaurSolarbeam)->floatValue, 0.001f);

    // R.O.B.: Air Boosts, Gyro Charge, Robo Beam
    memset(&s, 0, sizeof(s));
    s.robAirBoosts = main::kMechAlways;
    s.robGyro = main::kCharge2Full;
    s.robBeam = main::kRobBeamSuper;
    extra::buildFighterPlan(extra::kFtRob, 0, false, s, plan);
    CHECK_EQ(3, findOp(plan, extra::kOpInt, extra::kVarRobAirBoosts)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarRobBoostExhausted)->flagValue);
    CHECK_NEAR(90.0f, findOp(plan, extra::kOpFloat, extra::kVarRobGyro)->floatValue, 0.001f);
    CHECK_NEAR(900.0f, findOp(plan, extra::kOpFloat, extra::kVarRobBeam)->floatValue, 0.001f);

    s.robBeam = main::kRobBeamNormal;
    extra::buildFighterPlan(extra::kFtRob, 0, false, s, plan);
    CHECK_NEAR(300.0f, findOp(plan, extra::kOpFloat, extra::kVarRobBeam)->floatValue, 0.001f);

    // Lucas Offense Up: Off vs Charged (and LA-Basic[53] bump when 0 at match start)
    memset(&s, 0, sizeof(s));
    s.lucasOffenseUp = main::kOffenseCharged;
    extra::buildFighterPlan(extra::kFtLucas, 0, false, s, plan, false);
    CHECK(findOp(plan, extra::kOpInt, extra::kVarLucasAttackSerial) == NULL);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarLucasOffenseFlag)->flagValue);
    CHECK_NEAR(120.0f, findOp(plan, extra::kOpFloat, extra::kVarLucasOffenseProg)->floatValue, 0.001f);
    extra::buildFighterPlan(extra::kFtLucas, 0, false, s, plan, true);
    REQUIRE((op = findOp(plan, extra::kOpInt, extra::kVarLucasAttackSerial)) != NULL);
    CHECK_EQ(1, op->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarLucasOffenseFlag)->flagValue);
    CHECK_NEAR(120.0f, findOp(plan, extra::kOpFloat, extra::kVarLucasOffenseProg)->floatValue, 0.001f);
    s.lucasOffenseUp = main::kOffenseOff;
    extra::buildFighterPlan(extra::kFtLucas, 0, false, s, plan, true);
    CHECK(findOp(plan, extra::kOpInt, extra::kVarLucasAttackSerial) == NULL);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarLucasOffenseFlag)->flagValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarLucasOffenseConsume)->flagValue);
    CHECK_NEAR(0.0f, findOp(plan, extra::kOpFloat, extra::kVarLucasOffenseProg)->floatValue, 0.001f);

    // Squirtle Water Gun: Empty (0), Full (60)
    memset(&s, 0, sizeof(s));
    s.squirtleWaterGun = main::kCharge2Full;
    extra::buildFighterPlan(extra::kFtSquirtle, 0, false, s, plan);
    CHECK_EQ(60, findOp(plan, extra::kOpInt, extra::kVarSquirtleWaterGun)->intValue);

    // Sheik Needles: 0 vs 6 + LA-Bit[61]
    memset(&s, 0, sizeof(s));
    s.sheikNeedles = main::kNeedles6;
    extra::buildFighterPlan(extra::kFtSheik, 0, false, s, plan);
    CHECK_EQ(6, findOp(plan, extra::kOpInt, extra::kVarSheikNeedles)->intValue);
    CHECK_EQ(1, findOp(plan, extra::kOpFlag, extra::kVarSheikNeedlesFull)->flagValue);
    s.sheikNeedles = main::kNeedles0;
    extra::buildFighterPlan(extra::kFtSheik, 0, false, s, plan);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarSheikNeedles)->intValue);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarSheikNeedlesFull)->flagValue);

    // Snake Tranq + Cypher
    memset(&s, 0, sizeof(s));
    s.snakeTranq = main::kMechAlways;
    s.snakeCypher = main::kMechAlways;
    extra::buildFighterPlan(extra::kFtSnake, 0, false, s, plan);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarSnakeTranq)->intValue);
    CHECK_NEAR(0.0f, findOp(plan, extra::kOpFloat, extra::kVarSnakeCypherUses)->floatValue, 0.001f);
    CHECK_EQ(0, findOp(plan, extra::kOpFlag, extra::kVarSnakeCypherUsed)->flagValue);

    // Ganondorf Float
    memset(&s, 0, sizeof(s));
    s.ganonFloat = main::kMechAlways;
    extra::buildFighterPlan(extra::kFtGanondorf, 0, false, s, plan);
    CHECK_EQ(0, findOp(plan, extra::kOpInt, extra::kVarGanonFloatCount)->intValue);
}

TEST(fighter_mechanics, dedede_waddle_table_swap_safety_and_detach_restore)
{
    g_ftCommonDataAccesser.reset();
    main::loadSettings(host::fakes::block());
    extra::fighterMechanicsAttach();

    DededeDataBlock* pac1 = makeDededeData();
    g_ftCommonDataAccesser.setData(extra::kFtDedede, pac1->header);
    host::fakes::FakeFighter& ddd = makeDededeFighter();
    ddd.work->setInt(5, extra::kVarDededeWaddleCd);

    // Force Gordo: weights become {0, 0, 100, 0} and cooldown LA-Basic[65] clears to 0.
    main::settings().fighterMechanics.dededeWaddle = main::kWaddleGordo;
    extra::fighterMechanicsApplyFighter(ddd.fighter, ddd.modules, extra::kFtDedede, 0);
    CHECK_NEAR(0.0f, pac1->table[6], 0.001f);
    CHECK_NEAR(0.0f, pac1->table[7], 0.001f);
    CHECK_NEAR(100.0f, pac1->table[8], 0.001f);
    CHECK_NEAR(0.0f, pac1->table[9], 0.001f);
    CHECK_EQ(0, ddd.work->getInt(extra::kVarDededeWaddleCd));

    // Swap Dedede out: getData(kFtDedede) cleared and memory reused -> detach must NOT overwrite!
    g_ftCommonDataAccesser.setData(extra::kFtDedede, NULL);
    pac1->table[6] = 999.0f;
    extra::fighterMechanicsDetach();
    CHECK_NEAR(999.0f, pac1->table[6], 0.001f);

    // Re-attach and load a fresh Dedede PAC at a new address: re-finds, patches, and restores on detach.
    extra::fighterMechanicsAttach();
    DededeDataBlock* pac2 = makeDededeData();
    g_ftCommonDataAccesser.setData(extra::kFtDedede, pac2->header);
    main::settings().fighterMechanics.dededeWaddle = main::kWaddleDoo;
    extra::fighterMechanicsApplyFighter(ddd.fighter, ddd.modules, extra::kFtDedede, 0);
    CHECK_NEAR(0.0f, pac2->table[6], 0.001f);
    CHECK_NEAR(100.0f, pac2->table[7], 0.001f);
    CHECK_NEAR(0.0f, pac2->table[8], 0.001f);

    extra::fighterMechanicsDetach();
    CHECK_NEAR(35.0f, pac2->table[6], 0.001f);
    CHECK_NEAR(10.0f, pac2->table[7], 0.001f);
    CHECK_NEAR(4.0f, pac2->table[8], 0.001f);
    CHECK_NEAR(0.0f, pac2->table[9], 0.001f);
    CHECK_NEAR(999.0f, pac1->table[6], 0.001f);
}

TEST(fighter_mechanics, olimar_pikmin_weights_swap_safety_and_detach_restore)
{
    struct OlimarDataBlock {
        u8 header[0x90];
        s32 gid;
        float speed;
        float weights[5];
    };
    g_ftCommonDataAccesser.reset();
    main::loadSettings(host::fakes::block());
    extra::fighterMechanicsAttach();

    OlimarDataBlock* pac1 = host::mem::make<OlimarDataBlock>();
    memset(pac1, 0, sizeof(*pac1));
    pac1->gid = 0;
    pac1->speed = 1.0f;
    for (int i = 0; i < 5; i++) pac1->weights[i] = 1.0f;
    u32 tableAddr = reinterpret_cast<u32>(&pac1->gid);
    u32 count = 30;
    memcpy(pac1->header + 0x7C, &tableAddr, 4);
    memcpy(pac1->header + 0x80, &count, 4);

    g_ftCommonDataAccesser.setData(extra::kFtOlimar, pac1->header);
    host::fakes::FakeFighter& olimar = host::fakes::addFighter();

    // Force Purple (index 4): weights become {0, 0, 0, 0, 1}.
    main::settings().fighterMechanics.olimarPikmin = main::kPikminPurple;
    extra::fighterMechanicsApplyFighter(olimar.fighter, olimar.modules, extra::kFtOlimar, 0);
    CHECK_NEAR(0.0f, pac1->weights[0], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[1], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[2], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[3], 0.001f);
    CHECK_NEAR(1.0f, pac1->weights[4], 0.001f);

    // Force Blue (index 2): weights become {0, 0, 1, 0, 0}.
    main::settings().fighterMechanics.olimarPikmin = main::kPikminBlue;
    extra::fighterMechanicsApplyFighter(olimar.fighter, olimar.modules, extra::kFtOlimar, 0);
    CHECK_NEAR(0.0f, pac1->weights[0], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[1], 0.001f);
    CHECK_NEAR(1.0f, pac1->weights[2], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[3], 0.001f);
    CHECK_NEAR(0.0f, pac1->weights[4], 0.001f);

    // Default restores original {1, 1, 1, 1, 1}.
    main::settings().fighterMechanics.olimarPikmin = main::kPikminDefault;
    extra::fighterMechanicsApplyFighter(olimar.fighter, olimar.modules, extra::kFtOlimar, 0);
    for (int i = 0; i < 5; i++) CHECK_NEAR(1.0f, pac1->weights[i], 0.001f);

    // Swap Olimar out: getData(kFtOlimar) cleared -> detach must NOT overwrite freed PAC.
    main::settings().fighterMechanics.olimarPikmin = main::kPikminRed;
    extra::fighterMechanicsApplyFighter(olimar.fighter, olimar.modules, extra::kFtOlimar, 0);
    g_ftCommonDataAccesser.setData(extra::kFtOlimar, NULL);
    pac1->weights[0] = 77.0f;
    extra::fighterMechanicsDetach();
    CHECK_NEAR(77.0f, pac1->weights[0], 0.001f);

    // Re-attach with fresh PAC and verify detach restores all 5 weights to 1.0.
    extra::fighterMechanicsAttach();
    OlimarDataBlock* pac2 = host::mem::make<OlimarDataBlock>();
    memset(pac2, 0, sizeof(*pac2));
    pac2->gid = 0;
    pac2->speed = 1.0f;
    for (int i = 0; i < 5; i++) pac2->weights[i] = 1.0f;
    u32 tableAddr2 = reinterpret_cast<u32>(&pac2->gid);
    memcpy(pac2->header + 0x7C, &tableAddr2, 4);
    memcpy(pac2->header + 0x80, &count, 4);
    g_ftCommonDataAccesser.setData(extra::kFtOlimar, pac2->header);
    main::settings().fighterMechanics.olimarPikmin = main::kPikminWhite;
    extra::fighterMechanicsApplyFighter(olimar.fighter, olimar.modules, extra::kFtOlimar, 0);
    CHECK_NEAR(1.0f, pac2->weights[3], 0.001f);
    CHECK_NEAR(0.0f, pac2->weights[0], 0.001f);
    extra::fighterMechanicsDetach();
    for (int i = 0; i < 5; i++) CHECK_NEAR(1.0f, pac2->weights[i], 0.001f);
}

TEST(fighter_mechanics, settings_offsets_and_storage_roundtrip)
{
    CHECK_EQ(292u, offsetof(main::Settings, characterRng));
    CHECK_EQ(296u, offsetof(main::Settings, cpuTables));
    CHECK_EQ(372u, offsetof(main::Settings, stageOptions));
    CHECK_EQ(388u, offsetof(main::Settings, comboTrainer));
    CHECK_EQ(404u, offsetof(main::Settings, fighterMechanics));
    CHECK_EQ(427u, offsetof(main::Settings, fighterMechanics.olimarPikmin));
    CHECK_EQ(32u, sizeof(main::FighterMechanicsSettings));
    CHECK_EQ(436u, sizeof(main::Settings));

    storage::detach();
    host::card::reset();
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::attach(block);

    main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
    fm.peachFloat = main::kMechAlways;
    fm.samusCharge = main::kCharge3Full;
    fm.samusBeam = main::kBeamIce;
    fm.mewtwoHover = main::kMechAlways;
    fm.warioWaft = main::kCharge3Half;
    fm.robBeam = main::kRobBeamSuper;
    fm.dededeWaddle = main::kWaddleGordo;
    fm.ganonFloat = main::kMechAlways;
    fm.olimarPikmin = main::kPikminPurple;

    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::onRender(host::fakes::match());
    }
    storage::detach();

    // Fresh boot reads all FighterMechanicsSettings fields back from kAreaFighterMechanics.
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::attach(block);
    const main::FighterMechanicsSettings& loaded = main::settings().fighterMechanics;
    CHECK_EQ(main::kMechAlways, loaded.peachFloat);
    CHECK_EQ(main::kCharge3Full, loaded.samusCharge);
    CHECK_EQ(main::kBeamIce, loaded.samusBeam);
    CHECK_EQ(main::kMechAlways, loaded.mewtwoHover);
    CHECK_EQ(main::kCharge3Half, loaded.warioWaft);
    CHECK_EQ(main::kRobBeamSuper, loaded.robBeam);
    CHECK_EQ(main::kWaddleGordo, loaded.dededeWaddle);
    CHECK_EQ(main::kMechAlways, loaded.ganonFloat);
    CHECK_EQ(main::kPikminPurple, loaded.olimarPikmin);
    storage::detach();
}
