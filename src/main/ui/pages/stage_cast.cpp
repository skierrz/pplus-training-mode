#include "extra/stage_options_logic.h"
#include "main/extra.h"
#include "main/ui/pages/fighter_swap.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

// Combined Stage & Cast category page. Inactive stage and fighter rows are
// compacted out of view unless the session-only Show Inactive toggle is On.
namespace tm {
namespace ui {
namespace pages {
    TM_STATIC_ASSERT(kShowInactiveScratchOffset >= kFighterSwapScratchOffset + sizeof(FighterSwapMenuState),
                     tm_show_inactive_scratch_after_fighter_swap);
    TM_STATIC_ASSERT(kShowInactiveScratchOffset + 1 <= kScratchBytes, tm_show_inactive_scratch_fits);

    static void showInactiveChanged()
    {
        g_showInactiveRows = menuScratch()[kShowInactiveScratchOffset] != 0;
    }

#ifdef __MWERKS__
    enum { kStageCastRows = 35 };
    static Option* s_stageCastRows = NULL;

    static const u16 kCastLblOffs[20] = {
        kPs_cast_kHazards, kPs_cast_kPlatDefault, kPs_cast_kFodHeight, kPs_cast_kTurnip, kPs_cast_kPeachSmash,
        kPs_cast_kUnlimited, kPs_cast_kMisfire, kPs_cast_kHammer, kPs_cast_kGnwBucket, kPs_cast_kCharge3,
        kPs_cast_kSamusBeam, kPs_cast_kLucarioAura, kPs_cast_kCharge2, kPs_cast_kRobBeam, kPs_cast_kLucasOffense,
        kPs_cast_kSheikNeedles, kPs_cast_kDededeWaddle, kPs_cast_kNanaThrow, kPs_cast_kOlimarPikmin,
        kPs_cast_kOlimarMaturity
    };
    static bool (*const kCastGates[])() = {
        main::extraStageHazardsRow, main::extraStagePlatformsRow, main::extraStageFodRow,
        main::extraPeachRows, main::extraLuigiRows, main::extraGameWatchRows, main::extraSamusRows,
        main::extraMewtwoRows, main::extraLucarioRows, main::extraWarioRows, main::extraDonkeyKongRows,
        main::extraIvysaurRows, main::extraRobRows, main::extraLucasRows, main::extraSquirtleRows,
        main::extraSheikRows, main::extraDededeRows, main::extraSnakeRows, main::extraGanondorfRows,
        main::extraIceClimbersRows, main::extraOlimarRows
    };
    struct CastRowSpec {
        u16 offset;
        u8 count;
        u8 labelIdx;
        u8 gateIdx;
        u8 isStage;
    };
    static const CastRowSpec kCastSpecs[33] = {
        {TM_FIELD(stageOptions.hazards), main::kStageHazardCount, 0, 0, 1},
        {TM_FIELD(stageOptions.platformMode), main::kStagePlatMaxChoices, 1, 1, 1},
        {TM_FIELD(stageOptions.fodLeft), main::kFodHeightCount, 2, 2, 1},
        {TM_FIELD(stageOptions.fodRight), main::kFodHeightCount, 2, 2, 1},
        {TM_FIELD(characterRng.peachTurnip), main::kPeachTurnipCount, 3, 3, 0},
        {TM_FIELD(characterRng.peachSmash), main::kPeachSmashCount, 4, 3, 0},
        {TM_FIELD(fighterMechanics.peachFloat), main::kMechToggleCount, 5, 3, 0},
        {TM_FIELD(characterRng.luigiMisfire), main::kLuigiMisfireCount, 6, 4, 0},
        {TM_FIELD(characterRng.gnwHammer), main::kGnwHammerCount, 7, 5, 0},
        {TM_FIELD(fighterMechanics.gnwBucket), main::kGnwBucketCount, 8, 5, 0},
        {TM_FIELD(fighterMechanics.samusCharge), main::kCharge3Count, 9, 6, 0},
        {TM_FIELD(fighterMechanics.samusBeam), main::kSamusBeamCount, 10, 6, 0},
        {TM_FIELD(fighterMechanics.samusAirBombs), main::kMechToggleCount, 5, 6, 0},
        {TM_FIELD(fighterMechanics.mewtwoShadowBall), main::kCharge3Count, 9, 7, 0},
        {TM_FIELD(fighterMechanics.mewtwoHover), main::kMechToggleCount, 5, 7, 0},
        {TM_FIELD(fighterMechanics.lucarioAura), main::kLucarioAuraCount, 11, 8, 0},
        {TM_FIELD(fighterMechanics.lucarioSphere), main::kCharge2Count, 12, 8, 0},
        {TM_FIELD(fighterMechanics.warioWaft), main::kCharge3Count, 9, 9, 0},
        {TM_FIELD(fighterMechanics.dkPunch), main::kCharge2Count, 12, 10, 0},
        {TM_FIELD(fighterMechanics.ivysaurSolarbeam), main::kCharge3Count, 9, 11, 0},
        {TM_FIELD(fighterMechanics.robAirBoosts), main::kMechToggleCount, 5, 12, 0},
        {TM_FIELD(fighterMechanics.robGyro), main::kCharge2Count, 12, 12, 0},
        {TM_FIELD(fighterMechanics.robBeam), main::kRobBeamCount, 13, 12, 0},
        {TM_FIELD(fighterMechanics.lucasOffenseUp), main::kLucasOffenseCount, 14, 13, 0},
        {TM_FIELD(fighterMechanics.squirtleWaterGun), main::kCharge2Count, 12, 14, 0},
        {TM_FIELD(fighterMechanics.sheikNeedles), main::kSheikNeedlesCount, 15, 15, 0},
        {TM_FIELD(fighterMechanics.dededeWaddle), main::kDededeWaddleCount, 16, 16, 0},
        {TM_FIELD(fighterMechanics.snakeTranq), main::kMechToggleCount, 5, 17, 0},
        {TM_FIELD(fighterMechanics.snakeCypher), main::kMechToggleCount, 5, 17, 0},
        {TM_FIELD(fighterMechanics.ganonFloat), main::kMechToggleCount, 5, 18, 0},
        {TM_FIELD(fighterMechanics.nanaThrow), main::kNanaThrowCount, 17, 19, 0},
        {TM_FIELD(fighterMechanics.olimarPikmin), main::kOlimarPikminCount, 18, 20, 0},
        {TM_FIELD(fighterMechanics.olimarMaturity), main::kOlimarMaturityCount, 19, 20, 0}
    };

    extern Page kStageCast = {"Stage & Cast", NULL, 0, {0, 0, 0}};

    void initStageCastPage(Option* buf)
    {
        s_stageCastRows = buf;
        static const Option kHead[2] = {
            TM_MENU_IF("Fighter Swap", P_FIGHTER_SWAP, main::extraFighterSwapRow),
            TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch, kShowInactiveScratchOffset, 0, 1, 0,
                   "Show Inactive Fighters", NULL, showInactiveChanged, NULL)
        };
        buf[0] = kHead[0];
        buf[1] = kHead[1];
        const char* nm = pageStr(kPs_cast_names);
        for (int i = 0; i < 33; i++)
        {
            const CastRowSpec& sp = kCastSpecs[i];
            Option& o = buf[2 + i];
            o.kind = ::tm::ui::kKindChoice;
            o.flags = ::tm::ui::kFlagHideWhenDisabled;
            o.offset = sp.offset;
            o.min = TM_CHOICE_LO;
            o.def = 0;
            o.max = static_cast<s16>(sp.count - 1);
            o.name = nm;
            o.data = pageStr(kCastLblOffs[sp.labelIdx]);
            o.onChange = sp.isStage ? main::extraStageOptionsChanged : main::extraCharacterRng;
            o.enabled = kCastGates[sp.gateIdx];
            while (*nm++ != '\0') {}
        }
        kStageCast.options = buf;
        kStageCast.count = kStageCastRows;
    }

    void detachStageCastPage()
    {
        s_stageCastRows = NULL;
    }
#else
    // Stage Options choice tables.
    TM_LABELS_BEGIN(kHazards) TM_L("Default") TM_L("On") TM_L("Off") TM_LABELS_END
    TM_LABELS_BEGIN(kFodHeight)
        TM_L("Default") TM_L("Hidden") TM_L("Lowest") TM_L("Left Def") TM_L("Average") TM_L("Right Def") TM_L("Highest")
    TM_LABELS_END
    TM_LABELS_BEGIN(kPlatDefault) TM_L("Cycle") TM_L("Freeze") TM_L("Preset 1") TM_L("Preset 2") TM_L("Preset 3") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatGeneric) TM_L("Cycle") TM_L("Freeze") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatLuigisMansion) TM_L("Cycle") TM_L("Freeze") TM_L("Split") TM_L("Combined") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatDelfino) TM_L("Cycle") TM_L("Freeze") TM_L("Tri-Plat") TM_L("Two-Plat") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatHorizontal) TM_L("Cycle") TM_L("Freeze") TM_L("Center") TM_L("Left") TM_L("Right") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatDeadLine) TM_L("Cycle") TM_L("Freeze") TM_L("Close") TM_L("Far") TM_L("Mid") TM_LABELS_END
    TM_LABELS_BEGIN(kPlatRandall) TM_L("Cycle") TM_L("Freeze") TM_L("Left Out") TM_L("Right Out") TM_L("Hidden") TM_LABELS_END

    TM_LABELS_ASSERT(kHazards, main::kStageHazardCount, tm_stage_hazards_labels_match);
    TM_LABELS_ASSERT(kFodHeight, main::kFodHeightCount, tm_fod_height_labels_match);
    TM_LABELS_ASSERT(kPlatDefault, main::kStagePlatMaxChoices, tm_stage_plat_default_labels_match);

    // Fighter RNG and Mechanics choice tables.
    TM_LABELS_BEGIN(kTurnip)
        TM_L("Default") TM_L("Smile") TM_L("T-Eyes") TM_L("Line Eyes") TM_L("Circle Eyes")
        TM_L("Arch Eyes") TM_L("Winky") TM_L("Dot Eyes") TM_L("Stitch") TM_L("Mr. Saturn")
    TM_LABELS_END
    TM_LABELS_BEGIN(kPeachSmash) TM_L("Default") TM_L("Golf Club") TM_L("Frying Pan") TM_L("Tennis Racket") TM_LABELS_END
    TM_LABELS_BEGIN(kMisfire) TM_L("Default") TM_L("Always") TM_L("Never") TM_LABELS_END
    TM_LABELS_BEGIN(kHammer) TM_L("Default") TM_L("1") TM_L("2") TM_L("3") TM_L("4") TM_L("5") TM_L("6") TM_L("7") TM_L("8") TM_L("9") TM_LABELS_END
    TM_LABELS_BEGIN(kUnlimited) TM_L("Default") TM_L("Unlimited") TM_LABELS_END
    TM_LABELS_BEGIN(kCharge2) TM_L("Default") TM_L("Empty") TM_L("Full") TM_LABELS_END
    TM_LABELS_BEGIN(kCharge3) TM_L("Default") TM_L("Empty") TM_L("Half") TM_L("Full") TM_LABELS_END
    TM_LABELS_BEGIN(kGnwBucket) TM_L("Default") TM_L("Empty") TM_L("1") TM_L("2") TM_L("Full") TM_LABELS_END
    TM_LABELS_BEGIN(kSamusBeam) TM_L("Default") TM_L("Plasma") TM_L("Ice") TM_LABELS_END
    TM_LABELS_BEGIN(kLucarioAura) TM_L("Default") TM_L("0") TM_L("1") TM_L("2") TM_LABELS_END
    TM_LABELS_BEGIN(kRobBeam) TM_L("Default") TM_L("Uncharged") TM_L("Normal") TM_L("Super") TM_LABELS_END
    TM_LABELS_BEGIN(kLucasOffense) TM_L("Default") TM_L("Off") TM_L("Charged") TM_LABELS_END
    TM_LABELS_BEGIN(kSheikNeedles) TM_L("Default") TM_L("0") TM_L("6") TM_LABELS_END
    TM_LABELS_BEGIN(kDededeWaddle) TM_L("Default") TM_L("Waddle Dee") TM_L("Waddle Doo") TM_L("Gordo") TM_LABELS_END
    TM_LABELS_BEGIN(kNanaThrow) TM_L("Default") TM_L("F-Throw") TM_L("B-Throw") TM_L("U-Throw") TM_L("D-Throw") TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarPikmin) TM_L("Default") TM_L("Red") TM_L("Yellow") TM_L("Blue") TM_L("White") TM_L("Purple") TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarMaturity) TM_L("Default") TM_L("Leaf") TM_L("Bud") TM_L("Flower") TM_LABELS_END

    TM_LABELS_ASSERT(kTurnip, main::kPeachTurnipCount, tm_turnip_labels_match_the_setting);
    TM_LABELS_ASSERT(kPeachSmash, main::kPeachSmashCount, tm_smash_labels_match_the_setting);
    TM_LABELS_ASSERT(kHammer, main::kGnwHammerCount, tm_hammer_labels_match_the_setting);
    TM_LABELS_ASSERT(kMisfire, main::kLuigiMisfireCount, tm_misfire_labels_match_the_setting);
    TM_LABELS_ASSERT(kUnlimited, main::kMechToggleCount, tm_unlimited_labels_match_the_setting);
    TM_LABELS_ASSERT(kCharge2, main::kCharge2Count, tm_charge2_labels_match_the_setting);
    TM_LABELS_ASSERT(kCharge3, main::kCharge3Count, tm_charge3_labels_match_the_setting);
    TM_LABELS_ASSERT(kGnwBucket, main::kGnwBucketCount, tm_bucket_labels_match_the_setting);
    TM_LABELS_ASSERT(kSamusBeam, main::kSamusBeamCount, tm_samus_beam_labels_match_the_setting);
    TM_LABELS_ASSERT(kLucarioAura, main::kLucarioAuraCount, tm_lucario_aura_labels_match_the_setting);
    TM_LABELS_ASSERT(kRobBeam, main::kRobBeamCount, tm_rob_beam_labels_match_the_setting);
    TM_LABELS_ASSERT(kLucasOffense, main::kLucasOffenseCount, tm_lucas_offense_labels_match_the_setting);
    TM_LABELS_ASSERT(kSheikNeedles, main::kSheikNeedlesCount, tm_sheik_needles_labels_match_the_setting);
    TM_LABELS_ASSERT(kDededeWaddle, main::kDededeWaddleCount, tm_dedede_waddle_labels_match_the_setting);
    TM_LABELS_ASSERT(kNanaThrow, main::kNanaThrowCount, tm_nana_throw_labels_match_the_setting);
    TM_LABELS_ASSERT(kOlimarPikmin, main::kOlimarPikminCount, tm_olimar_pikmin_labels_match_the_setting);
    TM_LABELS_ASSERT(kOlimarMaturity, main::kOlimarMaturityCount, tm_olimar_maturity_labels_match_the_setting);

#define TM_SROW_P(field, labels, count, name, gate)                                                            \
    TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(field), count, 0,                                \
                    name, labels, main::extraStageOptionsChanged, gate)
#define TM_FROW(field, labels, count, name, gate)                                                              \
    TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(field), count, 0,                                \
                    name, labels, main::extraCharacterRng, gate)

    // Non-const so extra/stage_options.cpp can swap row 3 (Platforms) labels
    // and max index to match the active stage.
    static Option kRows[] = {
        TM_MENU_IF("Fighter Swap", P_FIGHTER_SWAP, main::extraFighterSwapRow),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch, kShowInactiveScratchOffset, 0, 1, 0,
               "Show Inactive Fighters", NULL, showInactiveChanged, NULL),
        TM_SROW_P(stageOptions.hazards, kHazards, main::kStageHazardCount, "Hazards", main::extraStageHazardsRow),
        TM_SROW_P(stageOptions.platformMode, kPlatDefault, main::kStagePlatMaxChoices, "Platforms", main::extraStagePlatformsRow),
        TM_SROW_P(stageOptions.fodLeft, kFodHeight, main::kFodHeightCount, "Left Platform", main::extraStageFodRow),
        TM_SROW_P(stageOptions.fodRight, kFodHeight, main::kFodHeightCount, "Right Platform", main::extraStageFodRow),
        TM_FROW(characterRng.peachTurnip, kTurnip, main::kPeachTurnipCount, "Peach Turnip", main::extraPeachRows),
        TM_FROW(characterRng.peachSmash, kPeachSmash, main::kPeachSmashCount, "Peach F-Smash", main::extraPeachRows),
        TM_FROW(fighterMechanics.peachFloat, kUnlimited, main::kMechToggleCount, "Peach Float", main::extraPeachRows),
        TM_FROW(characterRng.luigiMisfire, kMisfire, main::kLuigiMisfireCount, "Luigi Misfire", main::extraLuigiRows),
        TM_FROW(characterRng.gnwHammer, kHammer, main::kGnwHammerCount, "G&W Judge", main::extraGameWatchRows),
        TM_FROW(fighterMechanics.gnwBucket, kGnwBucket, main::kGnwBucketCount, "G&W Oil Panic", main::extraGameWatchRows),
        TM_FROW(fighterMechanics.samusCharge, kCharge3, main::kCharge3Count, "Samus Charge", main::extraSamusRows),
        TM_FROW(fighterMechanics.samusBeam, kSamusBeam, main::kSamusBeamCount, "Samus Beam", main::extraSamusRows),
        TM_FROW(fighterMechanics.samusAirBombs, kUnlimited, main::kMechToggleCount, "Samus Air Bombs", main::extraSamusRows),
        TM_FROW(fighterMechanics.mewtwoShadowBall, kCharge3, main::kCharge3Count, "Mewtwo Shadow Ball", main::extraMewtwoRows),
        TM_FROW(fighterMechanics.mewtwoHover, kUnlimited, main::kMechToggleCount, "Mewtwo Hover", main::extraMewtwoRows),
        TM_FROW(fighterMechanics.lucarioAura, kLucarioAura, main::kLucarioAuraCount, "Lucario Aura", main::extraLucarioRows),
        TM_FROW(fighterMechanics.lucarioSphere, kCharge2, main::kCharge2Count, "Lucario Sphere", main::extraLucarioRows),
        TM_FROW(fighterMechanics.warioWaft, kCharge3, main::kCharge3Count, "Wario Waft", main::extraWarioRows),
        TM_FROW(fighterMechanics.dkPunch, kCharge2, main::kCharge2Count, "DK Giant Punch", main::extraDonkeyKongRows),
        TM_FROW(fighterMechanics.ivysaurSolarbeam, kCharge3, main::kCharge3Count, "Ivysaur Solarbeam", main::extraIvysaurRows),
        TM_FROW(fighterMechanics.robAirBoosts, kUnlimited, main::kMechToggleCount, "ROB Air Boosts", main::extraRobRows),
        TM_FROW(fighterMechanics.robGyro, kCharge2, main::kCharge2Count, "ROB Gyro Charge", main::extraRobRows),
        TM_FROW(fighterMechanics.robBeam, kRobBeam, main::kRobBeamCount, "ROB Beam", main::extraRobRows),
        TM_FROW(fighterMechanics.lucasOffenseUp, kLucasOffense, main::kLucasOffenseCount, "Lucas Offense Up", main::extraLucasRows),
        TM_FROW(fighterMechanics.squirtleWaterGun, kCharge2, main::kCharge2Count, "Squirtle Water Gun", main::extraSquirtleRows),
        TM_FROW(fighterMechanics.sheikNeedles, kSheikNeedles, main::kSheikNeedlesCount, "Sheik Needles", main::extraSheikRows),
        TM_FROW(fighterMechanics.dededeWaddle, kDededeWaddle, main::kDededeWaddleCount, "Dedede Waddle", main::extraDededeRows),
        TM_FROW(fighterMechanics.snakeTranq, kUnlimited, main::kMechToggleCount, "Snake Tranq Ammo", main::extraSnakeRows),
        TM_FROW(fighterMechanics.snakeCypher, kUnlimited, main::kMechToggleCount, "Snake Cypher", main::extraSnakeRows),
        TM_FROW(fighterMechanics.ganonFloat, kUnlimited, main::kMechToggleCount, "Ganon Float", main::extraGanondorfRows),
        TM_FROW(fighterMechanics.nanaThrow, kNanaThrow, main::kNanaThrowCount, "Nana Throw", main::extraIceClimbersRows),
        TM_FROW(fighterMechanics.olimarPikmin, kOlimarPikmin, main::kOlimarPikminCount, "Olimar Pikmin", main::extraOlimarRows),
        TM_FROW(fighterMechanics.olimarMaturity, kOlimarMaturity, main::kOlimarMaturityCount, "Olimar Maturity", main::extraOlimarRows)
    };
#undef TM_SROW_P
#undef TM_FROW

    extern const Page kStageCast = TM_PAGE("Stage & Cast", kRows);
#endif
}
}

namespace main {
    // Called from extra/stage_options.cpp; keep from linker dead-stripping.
#pragma force_active on
    void setStagePlatformRowPreset(int preset)
    {
        using namespace ui::pages;
#ifdef __MWERKS__
        if (s_stageCastRows == NULL || !pageStringsReady())
        {
            return;
        }
        ui::Option& row = s_stageCastRows[3];
#else
        ui::Option& row = kRows[3];
#endif
        switch (preset)
        {
        case extra::kPresetGenericMoving:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatGeneric);
#else
            row.data = kPlatGeneric;
#endif
            row.max = 1;
            break;
        case extra::kPresetLuigisMansion:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatLuigisMansion);
#else
            row.data = kPlatLuigisMansion;
#endif
            row.max = 3;
            break;
        case extra::kPresetDelfinoSecret:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatDelfino);
#else
            row.data = kPlatDelfino;
#endif
            row.max = 3;
            break;
        case extra::kPresetSmashville:
        case extra::kPresetGreenHill:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatHorizontal);
#else
            row.data = kPlatHorizontal;
#endif
            row.max = 4;
            break;
        case extra::kPresetDeadLine:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatDeadLine);
#else
            row.data = kPlatDeadLine;
#endif
            row.max = 4;
            break;
        case extra::kPresetYoshisStory:
        case extra::kPresetYoshisIsland:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatRandall);
#else
            row.data = kPlatRandall;
#endif
            row.max = 4;
            break;
        default:
#ifdef __MWERKS__
            row.data = pageStr(kPs_cast_kPlatDefault);
#else
            row.data = kPlatDefault;
#endif
            row.max = 4;
            break;
        }
    }
#pragma force_active reset
}
}
