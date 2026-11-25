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
    static const char* const kHazards[] = {"Default", "On", "Off"};
    static const char* const kFodHeight[] = {"Default", "Hidden",    "Lowest", "Left Def",
                                             "Average", "Right Def", "Highest"};
    static const char* const kPlatDefault[] = {"Cycle", "Freeze", "Preset 1", "Preset 2", "Preset 3"};
    static const char* const kPlatGeneric[] = {"Cycle", "Freeze"};
    static const char* const kPlatLuigisMansion[] = {"Cycle", "Freeze", "Split", "Combined"};
    static const char* const kPlatDelfino[] = {"Cycle", "Freeze", "Tri-Plat", "Two-Plat"};
    static const char* const kPlatHorizontal[] = {"Cycle", "Freeze", "Center", "Left", "Right"};
    static const char* const kPlatDeadLine[] = {"Cycle", "Freeze", "Close", "Far", "Mid"};
    static const char* const kPlatRandall[] = {"Cycle", "Freeze", "Left Out", "Right Out", "Hidden"};

    TM_STATIC_ASSERT(TM_COUNT(kHazards) == main::kStageHazardCount, tm_stage_hazards_labels_match);
    TM_STATIC_ASSERT(TM_COUNT(kFodHeight) == main::kFodHeightCount, tm_fod_height_labels_match);
    TM_STATIC_ASSERT(TM_COUNT(kPlatDefault) == main::kStagePlatMaxChoices, tm_stage_plat_default_labels_match);

    // Fighter RNG and Mechanics choice tables.
    static const char* const kTurnip[] = {"Default",     "Smile",       "T-Eyes",
                                          "Line Eyes",   "Circle Eyes", "Arch Eyes",
                                          "Winky",       "Dot Eyes",    "Stitch",
                                          "Mr. Saturn"};
    static const char* const kPeachSmash[] = {"Default", "Golf Club", "Frying Pan", "Tennis Racket"};
    static const char* const kMisfire[] = {"Default", "Always", "Never"};
    static const char* const kHammer[] = {"Default", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static const char* const kAlways[] = {"Default", "Always"};
    static const char* const kUnlimited[] = {"Default", "Unlimited"};
    static const char* const kCharge2[] = {"Default", "Empty", "Full"};
    static const char* const kCharge3[] = {"Default", "Empty", "Half", "Full"};
    static const char* const kGnwBucket[] = {"Default", "Empty", "1", "2", "Full"};
    static const char* const kSamusBeam[] = {"Default", "Plasma", "Ice"};
    static const char* const kLucarioAura[] = {"Default", "0", "1", "2"};
    static const char* const kRobBeam[] = {"Default", "Uncharged", "Normal", "Super"};
    static const char* const kLucasOffense[] = {"Default", "Off", "Charged"};
    static const char* const kSheikNeedles[] = {"Default", "0", "6"};
    static const char* const kDededeWaddle[] = {"Default", "Waddle Dee", "Waddle Doo", "Gordo"};
    static const char* const kNanaThrow[] = {"Default", "F-Throw", "B-Throw", "U-Throw", "D-Throw"};
    static const char* const kOlimarPikmin[] = {"Default", "Red", "Yellow", "Blue", "White", "Purple"};

    TM_STATIC_ASSERT(TM_COUNT(kTurnip) == main::kPeachTurnipCount, tm_turnip_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kPeachSmash) == main::kPeachSmashCount, tm_smash_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kHammer) == main::kGnwHammerCount, tm_hammer_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kMisfire) == main::kLuigiMisfireCount, tm_misfire_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kAlways) == main::kMechToggleCount, tm_always_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kUnlimited) == main::kMechToggleCount, tm_unlimited_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kCharge2) == main::kCharge2Count, tm_charge2_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kCharge3) == main::kCharge3Count, tm_charge3_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kGnwBucket) == main::kGnwBucketCount, tm_bucket_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kSamusBeam) == main::kSamusBeamCount, tm_samus_beam_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kLucarioAura) == main::kLucarioAuraCount, tm_lucario_aura_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kRobBeam) == main::kRobBeamCount, tm_rob_beam_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kLucasOffense) == main::kLucasOffenseCount, tm_lucas_offense_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kSheikNeedles) == main::kSheikNeedlesCount, tm_sheik_needles_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kDededeWaddle) == main::kDededeWaddleCount, tm_dedede_waddle_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kNanaThrow) == main::kNanaThrowCount, tm_nana_throw_labels_match_the_setting);
    TM_STATIC_ASSERT(TM_COUNT(kOlimarPikmin) == main::kOlimarPikminCount, tm_olimar_pikmin_labels_match_the_setting);

#define TM_SROW(field, labels, name, gate)                                                                     \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(field), 0, TM_COUNT(labels) - 1, 0, \
           name, labels, main::extraStageOptionsChanged, gate)
#define TM_FROW(field, labels, name, gate)                                                                     \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(field), 0, TM_COUNT(labels) - 1, 0, \
           name, labels, main::extraCharacterRng, gate)

    // Non-const so extra/stage_options.cpp can swap row 3 (Platforms) labels
    // and max index to match the active stage.
    static Option kRows[] = {
        TM_MENU_IF("Fighter Swap", P_FIGHTER_SWAP, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch, kShowInactiveScratchOffset, 0, 1, 0, "Show Inactive",
               NULL, showInactiveChanged, NULL),
        TM_SROW(stageOptions.hazards, kHazards, "Hazards", main::extraStageHazardsRow),
        TM_SROW(stageOptions.platformMode, kPlatDefault, "Platforms", main::extraStagePlatformsRow),
        TM_SROW(stageOptions.fodLeft, kFodHeight, "Left Platform", main::extraStageFodRow),
        TM_SROW(stageOptions.fodRight, kFodHeight, "Right Platform", main::extraStageFodRow),
        TM_FROW(characterRng.peachTurnip, kTurnip, "Peach Turnip", main::extraPeachRows),
        TM_FROW(characterRng.peachSmash, kPeachSmash, "Peach F-Smash", main::extraPeachRows),
        TM_FROW(fighterMechanics.peachFloat, kAlways, "Peach Float", main::extraPeachRows),
        TM_FROW(characterRng.luigiMisfire, kMisfire, "Luigi Misfire", main::extraLuigiRows),
        TM_FROW(characterRng.gnwHammer, kHammer, "G&W Judge", main::extraGameWatchRows),
        TM_FROW(fighterMechanics.gnwBucket, kGnwBucket, "G&W Oil Panic", main::extraGameWatchRows),
        TM_FROW(fighterMechanics.samusCharge, kCharge3, "Samus Charge", main::extraSamusRows),
        TM_FROW(fighterMechanics.samusBeam, kSamusBeam, "Samus Beam", main::extraSamusRows),
        TM_FROW(fighterMechanics.samusAirBombs, kUnlimited, "Samus Air Bombs", main::extraSamusRows),
        TM_FROW(fighterMechanics.mewtwoShadowBall, kCharge3, "Mewtwo Shadow Ball", main::extraMewtwoRows),
        TM_FROW(fighterMechanics.mewtwoHover, kUnlimited, "Mewtwo Hover", main::extraMewtwoRows),
        TM_FROW(fighterMechanics.lucarioAura, kLucarioAura, "Lucario Aura", main::extraLucarioRows),
        TM_FROW(fighterMechanics.lucarioSphere, kCharge2, "Lucario Sphere", main::extraLucarioRows),
        TM_FROW(fighterMechanics.warioWaft, kCharge3, "Wario Waft", main::extraWarioRows),
        TM_FROW(fighterMechanics.dkPunch, kCharge2, "DK Giant Punch", main::extraDonkeyKongRows),
        TM_FROW(fighterMechanics.ivysaurSolarbeam, kCharge3, "Ivysaur Solarbeam", main::extraIvysaurRows),
        TM_FROW(fighterMechanics.robAirBoosts, kUnlimited, "ROB Air Boosts", main::extraRobRows),
        TM_FROW(fighterMechanics.robGyro, kCharge2, "ROB Gyro Charge", main::extraRobRows),
        TM_FROW(fighterMechanics.robBeam, kRobBeam, "ROB Beam", main::extraRobRows),
        TM_FROW(fighterMechanics.lucasOffenseUp, kLucasOffense, "Lucas Offense Up", main::extraLucasRows),
        TM_FROW(fighterMechanics.squirtleWaterGun, kCharge2, "Squirtle Water Gun", main::extraSquirtleRows),
        TM_FROW(fighterMechanics.sheikNeedles, kSheikNeedles, "Sheik Needles", main::extraSheikRows),
        TM_FROW(fighterMechanics.dededeWaddle, kDededeWaddle, "Dedede Waddle", main::extraDededeRows),
        TM_FROW(fighterMechanics.snakeTranq, kUnlimited, "Snake Tranq Ammo", main::extraSnakeRows),
        TM_FROW(fighterMechanics.snakeCypher, kUnlimited, "Snake Cypher", main::extraSnakeRows),
        TM_FROW(fighterMechanics.ganonFloat, kUnlimited, "Ganon Float", main::extraGanondorfRows),
        TM_FROW(fighterMechanics.nanaThrow, kNanaThrow, "Nana Throw", main::extraIceClimbersRows),
        TM_FROW(fighterMechanics.olimarPikmin, kOlimarPikmin, "Olimar Pikmin", main::extraOlimarRows)
    };
#undef TM_SROW
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
            row.max = TM_COUNT(kPlatGeneric) - 1;
            break;
        case extra::kPresetLuigisMansion:
            row.data = kPlatLuigisMansion;
            row.max = TM_COUNT(kPlatLuigisMansion) - 1;
            break;
        case extra::kPresetDelfinoSecret:
            row.data = kPlatDelfino;
            row.max = TM_COUNT(kPlatDelfino) - 1;
            break;
        case extra::kPresetSmashville:
        case extra::kPresetGreenHill:
            row.data = kPlatHorizontal;
            row.max = TM_COUNT(kPlatHorizontal) - 1;
            break;
        case extra::kPresetDeadLine:
            row.data = kPlatDeadLine;
            row.max = TM_COUNT(kPlatDeadLine) - 1;
            break;
        case extra::kPresetYoshisStory:
        case extra::kPresetYoshisIsland:
            row.data = kPlatRandall;
            row.max = TM_COUNT(kPlatRandall) - 1;
            break;
        default:
            row.data = kPlatDefault;
            row.max = TM_COUNT(kPlatDefault) - 1;
            break;
        }
    }
#pragma force_active reset
}
}
