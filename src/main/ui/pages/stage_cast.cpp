#include "extra/stage_options_logic.h"
#include "main/extra.h"
#include "main/ui/pages/fighter_swap.h"
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
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch, kShowInactiveScratchOffset, 0, 1, 0, "Show Inactive",
               NULL, showInactiveChanged, NULL),
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
        TM_FROW(fighterMechanics.olimarPikmin, kOlimarPikmin, main::kOlimarPikminCount, "Olimar Pikmin", main::extraOlimarRows)
    };
#undef TM_SROW_P
#undef TM_FROW

    extern const Page kStageCast = TM_PAGE("Stage & Cast", kRows);
}
}

namespace main {
    // Called from extra/stage_options.cpp; keep from linker dead-stripping.
#pragma force_active on
    void setStagePlatformRowPreset(int preset)
    {
        using namespace ui::pages;
        ui::Option& row = kRows[3];
        switch (preset)
        {
        case extra::kPresetGenericMoving:
            row.data = kPlatGeneric;
            row.max = 1;
            break;
        case extra::kPresetLuigisMansion:
            row.data = kPlatLuigisMansion;
            row.max = 3;
            break;
        case extra::kPresetDelfinoSecret:
            row.data = kPlatDelfino;
            row.max = 3;
            break;
        case extra::kPresetSmashville:
        case extra::kPresetGreenHill:
            row.data = kPlatHorizontal;
            row.max = 4;
            break;
        case extra::kPresetDeadLine:
            row.data = kPlatDeadLine;
            row.max = 4;
            break;
        case extra::kPresetYoshisStory:
        case extra::kPresetYoshisIsland:
            row.data = kPlatRandall;
            row.max = 4;
            break;
        default:
            row.data = kPlatDefault;
            row.max = 4;
            break;
        }
    }
#pragma force_active reset
}
}
