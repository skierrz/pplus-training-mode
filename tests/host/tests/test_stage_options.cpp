// Unit tests for P+ 3.2.1 Stage Options logic (src/extra/stage_options_logic.h),
// dynamic row configuration (src/main/ui/pages/stage_cast.cpp), clamping,
// and settings storage round-trip (kAreaStageOptions = 14).
#include <string.h>

#include "extra/stage_options_logic.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/pages/pages.h"

namespace {
    using namespace tm;

    static void bootStorage()
    {
        storage::detach();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }
}

TEST(stage_options, dynamic_stage_classification_and_visibility)
{
    extra::StageDetectInput empty;
    memset(&empty, 0, sizeof(empty));
    CHECK_EQ(extra::kPresetNone, extra::classifyStagePreset(empty));
    CHECK(!extra::presetAnyOption(extra::kPresetNone));
    CHECK(!extra::presetHasHazards(extra::kPresetNone));
    CHECK(!extra::presetHasPlatforms(extra::kPresetNone));
    CHECK(!extra::presetHasFod(extra::kPresetNone));

    // Fountain of Dreams (and alts sharing st_dxfountain_ht.rel).
    extra::StageDetectInput fod = empty;
    fod.modFod = true;
    fod.hasSpout80 = true;
    fod.hasSpout81 = true;
    int p = extra::classifyStagePreset(fod);
    CHECK_EQ(extra::kPresetFod, p);
    CHECK(extra::presetAnyOption(p));
    CHECK(extra::presetHasHazards(p));
    CHECK(!extra::presetHasPlatforms(p));
    CHECK(extra::presetHasFod(p));

    // Dream Land (and alts sharing st_dreamland64_ht.rel).
    extra::StageDetectInput dl = empty;
    dl.modDreamLand = true;
    dl.hasWhispy2 = true;
    p = extra::classifyStagePreset(dl);
    CHECK_EQ(extra::kPresetDreamLand, p);
    CHECK(extra::presetHasHazards(p));
    CHECK(!extra::presetHasPlatforms(p));
    CHECK(!extra::presetHasFod(p));

    // Smashville (and alts sharing st_smashville_ht.rel).
    extra::StageDetectInput sv = empty;
    sv.modSmashville = true;
    sv.hasAshiba80 = true;
    sv.hasAshiba81 = true;
    p = extra::classifyStagePreset(sv);
    CHECK_EQ(extra::kPresetSmashville, p);
    CHECK(extra::presetHasHazards(p));
    CHECK(extra::presetHasPlatforms(p));
    CHECK_EQ(4, extra::presetPlatformMax(p));

    // Green Hill Zone vs Dead Line (both on st_greenhill_ht.rel).
    extra::StageDetectInput ghz = empty;
    ghz.modGreenHill = true;
    ghz.hasGhzPendulum = true;
    CHECK_EQ(extra::kPresetGreenHill, extra::classifyStagePreset(ghz));
    CHECK_EQ(4, extra::presetPlatformMax(extra::kPresetGreenHill));

    extra::StageDetectInput dead = empty;
    dead.modGreenHill = true;
    dead.hasDeadLinePlats = true;
    CHECK_EQ(extra::kPresetDeadLine, extra::classifyStagePreset(dead));
    CHECK_EQ(4, extra::presetPlatformMax(extra::kPresetDeadLine));

    // Yoshi's Island Brawl (st_crayon_noshyguy_ht.rel + support clouds ModelData[80]).
    extra::StageDetectInput yi = empty;
    yi.modCrayon = true;
    yi.hasCrayonClouds80 = true;
    p = extra::classifyStagePreset(yi);
    CHECK_EQ(extra::kPresetYoshisIsland, p);
    CHECK(extra::presetAnyOption(p));
    CHECK(extra::presetHasHazards(p));
    CHECK(extra::presetHasPlatforms(p));
    CHECK(!extra::presetHasFod(p));
    CHECK_EQ(4, extra::presetPlatformMax(p));

    // Yoshi's Island without ModelData[80] (e.g. Hazards OFF entry or static alt) stays None.
    extra::StageDetectInput yiStatic = empty;
    yiStatic.modCrayon = true;
    CHECK_EQ(extra::kPresetNone, extra::classifyStagePreset(yiStatic));

    // Delfino's Secret (PlatformLeft + PlatformCenter) vs Luigi's Mansion (PlatformLeft only).
    extra::StageDetectInput delfino = empty;
    delfino.hasPlatformLeft = true;
    delfino.hasPlatformCenter = true;
    CHECK_EQ(extra::kPresetDelfinoSecret, extra::classifyStagePreset(delfino));
    CHECK(!extra::presetHasHazards(extra::kPresetDelfinoSecret));
    CHECK(extra::presetHasPlatforms(extra::kPresetDelfinoSecret));
    CHECK_EQ(3, extra::presetPlatformMax(extra::kPresetDelfinoSecret));

    extra::StageDetectInput lm = empty;
    lm.hasPlatformLeft = true;
    CHECK_EQ(extra::kPresetLuigisMansion, extra::classifyStagePreset(lm));
    CHECK_EQ(3, extra::presetPlatformMax(extra::kPresetLuigisMansion));

    // Yoshi's Story (Randall) and generic animated COLL stages.
    extra::StageDetectInput ys = empty;
    ys.hasRandall = true;
    CHECK_EQ(extra::kPresetYoshisStory, extra::classifyStagePreset(ys));
    CHECK_EQ(4, extra::presetPlatformMax(extra::kPresetYoshisStory));

    extra::StageDetectInput gen = empty;
    gen.hasGenericMovingColl = true;
    CHECK_EQ(extra::kPresetGenericMoving, extra::classifyStagePreset(gen));
    CHECK(!extra::presetHasHazards(extra::kPresetGenericMoving));
    CHECK(extra::presetHasPlatforms(extra::kPresetGenericMoving));
    CHECK_EQ(1, extra::presetPlatformMax(extra::kPresetGenericMoving));
}

TEST(stage_options, fod_exact_pplus_frame_table_and_world_heights)
{
    CHECK(extra::fodFrameForChoice(main::kFodDefault) < 0.0f);
    CHECK_NEAR(0.0f, extra::fodFrameForChoice(main::kFodHidden), 0.001f);
    CHECK_NEAR(39.0f, extra::fodFrameForChoice(main::kFodLowest), 0.001f);
    CHECK_NEAR(43.0f, extra::fodFrameForChoice(main::kFodLeftDefault), 0.001f);
    CHECK_NEAR(57.0f, extra::fodFrameForChoice(main::kFodAverage), 0.001f);
    CHECK_NEAR(64.0f, extra::fodFrameForChoice(main::kFodRightDefault), 0.001f);
    CHECK_NEAR(75.0f, extra::fodFrameForChoice(main::kFodHighest), 0.001f);
    CHECK(extra::fodFrameForChoice(99) < 0.0f);

    // Y_world = 0.375 * frame - 0.75 (P+ 3.2.1 STDT[20] linear scale 0.75).
    CHECK_NEAR(-0.75f, extra::fodWorldYForFrame(0.0f), 0.001f);
    CHECK_NEAR(13.875f, extra::fodWorldYForFrame(39.0f), 0.001f);
    CHECK_NEAR(15.375f, extra::fodWorldYForFrame(43.0f), 0.001f);
    CHECK_NEAR(20.625f, extra::fodWorldYForFrame(57.0f), 0.001f);
    CHECK_NEAR(23.25f, extra::fodWorldYForFrame(64.0f), 0.001f);
    CHECK_NEAR(27.375f, extra::fodWorldYForFrame(75.0f), 0.001f);
}

TEST(stage_options, platform_presets_clamping_and_hazards_resolution)
{
    CHECK_EQ(0, extra::clampPlatformMode(extra::kPresetNone, 3));
    CHECK_EQ(1, extra::clampPlatformMode(extra::kPresetGenericMoving, 4));
    CHECK_EQ(3, extra::clampPlatformMode(extra::kPresetLuigisMansion, 4));
    CHECK_EQ(4, extra::clampPlatformMode(extra::kPresetSmashville, 9));
    CHECK_EQ(0, extra::clampPlatformMode(extra::kPresetSmashville, -1));

    // Luigi's Mansion Split (f=0) and Combined (f=1800).
    CHECK(extra::platformTargetFrame(extra::kPresetLuigisMansion, 0, false) < 0.0f);
    CHECK(extra::platformTargetFrame(extra::kPresetLuigisMansion, 1, false) < 0.0f);
    CHECK_NEAR(0.0f, extra::platformTargetFrame(extra::kPresetLuigisMansion, 2, false), 0.001f);
    CHECK_NEAR(1800.0f, extra::platformTargetFrame(extra::kPresetLuigisMansion, 3, false), 0.001f);

    // Delfino's Secret Tri-Plat (f=0) and Two-Plat (f=2000).
    CHECK_NEAR(0.0f, extra::platformTargetFrame(extra::kPresetDelfinoSecret, 2, false), 0.001f);
    CHECK_NEAR(2000.0f, extra::platformTargetFrame(extra::kPresetDelfinoSecret, 3, false), 0.001f);

    // Green Hill Zone Center shifts between HazOn (f=0) and HazOff (f=115).
    CHECK_NEAR(0.0f, extra::platformTargetFrame(extra::kPresetGreenHill, 2, false), 0.001f);
    CHECK_NEAR(115.0f, extra::platformTargetFrame(extra::kPresetGreenHill, 2, true), 0.001f);

    // Dead Line Close / Far / Mid.
    CHECK_NEAR(0.0f, extra::platformTargetFrame(extra::kPresetDeadLine, 2, false), 0.001f);
    CHECK_NEAR(3000.0f, extra::platformTargetFrame(extra::kPresetDeadLine, 3, false), 0.001f);
    CHECK_NEAR(6000.0f, extra::platformTargetFrame(extra::kPresetDeadLine, 4, false), 0.001f);

    // Yoshi's Island Brawl Left Out / Right Out / Hidden (2880f timedGhosts).
    CHECK(extra::platformTargetFrame(extra::kPresetYoshisIsland, 0, false) < 0.0f);
    CHECK(extra::platformTargetFrame(extra::kPresetYoshisIsland, 1, false) < 0.0f);
    CHECK_NEAR(1020.0f, extra::platformTargetFrame(extra::kPresetYoshisIsland, 2, false), 0.001f);
    CHECK_NEAR(2460.0f, extra::platformTargetFrame(extra::kPresetYoshisIsland, 3, false), 0.001f);
    CHECK_NEAR(0.0f, extra::platformTargetFrame(extra::kPresetYoshisIsland, 4, false), 0.001f);

    // Hazard tri-state resolution.
    CHECK(!extra::resolveHazardOff(main::kStageHazardDefault, false));
    CHECK(extra::resolveHazardOff(main::kStageHazardDefault, true));
    CHECK(!extra::resolveHazardOff(main::kStageHazardOn, true));
    CHECK(extra::resolveHazardOff(main::kStageHazardOff, false));
}

TEST(stage_options, restore_global_hazard_off_guard)
{
    // Dirty exit on hazard stages must restore initialHazardOff in both directions.
    static const int kHazardPresets[] = {
        extra::kPresetFod, extra::kPresetDreamLand, extra::kPresetSmashville,
        extra::kPresetGreenHill, extra::kPresetDeadLine, extra::kPresetYoshisIsland
    };
    for (u32 i = 0; i < sizeof(kHazardPresets) / sizeof(kHazardPresets[0]); i++)
    {
        int p = kHazardPresets[i];
        CHECK(extra::shouldRestoreGlobalHazardOff(true, p, false, true));
        CHECK(extra::shouldRestoreGlobalHazardOff(true, p, true, false));
        // Unchanged flag is a no-op.
        CHECK(!extra::shouldRestoreGlobalHazardOff(true, p, false, false));
        CHECK(!extra::shouldRestoreGlobalHazardOff(true, p, true, true));
        // Uninitialized state is always a no-op.
        CHECK(!extra::shouldRestoreGlobalHazardOff(false, p, false, true));
        CHECK(!extra::shouldRestoreGlobalHazardOff(false, p, true, false));
    }

    // Static and moving-platform-only stages never clobber the global flag.
    static const int kNonHazardPresets[] = {
        extra::kPresetNone, extra::kPresetLuigisMansion, extra::kPresetDelfinoSecret,
        extra::kPresetYoshisStory, extra::kPresetGenericMoving
    };
    for (u32 i = 0; i < sizeof(kNonHazardPresets) / sizeof(kNonHazardPresets[0]); i++)
    {
        int p = kNonHazardPresets[i];
        CHECK(!extra::shouldRestoreGlobalHazardOff(true, p, false, true));
        CHECK(!extra::shouldRestoreGlobalHazardOff(true, p, true, false));
        CHECK(!extra::shouldRestoreGlobalHazardOff(true, p, true, true));
    }

    // Round-trip through resolveHazardOff: Default -> Off -> Default ends clean.
    bool initial = false;
    bool applied = extra::resolveHazardOff(main::kStageHazardOff, initial);
    CHECK(extra::shouldRestoreGlobalHazardOff(true, extra::kPresetDreamLand, initial, applied));
    applied = extra::resolveHazardOff(main::kStageHazardDefault, initial);
    CHECK(!extra::shouldRestoreGlobalHazardOff(true, extra::kPresetDreamLand, initial, applied));
}

TEST(stage_options, platform_row_swaps_choices_and_restores_defaults)
{
    const ui::Page& page = *ui::labMenu().pages[ui::pages::P_STAGE_CAST];
    CHECK_EQ(35, page.count);
    CHECK(strcmp(page.options[3].name, "Platforms") == 0);

    main::setStagePlatformRowPreset(extra::kPresetYoshisIsland);
    CHECK_EQ(4, page.options[3].max);
    const char* const* yi = static_cast<const char* const*>(page.options[3].data);
    CHECK(strcmp(yi[2], "Left Out") == 0);
    CHECK(strcmp(yi[3], "Right Out") == 0);
    CHECK(strcmp(yi[4], "Hidden") == 0);

    main::setStagePlatformRowPreset(extra::kPresetLuigisMansion);
    CHECK_EQ(3, page.options[3].max);
    const char* const* lm = static_cast<const char* const*>(page.options[3].data);
    CHECK(strcmp(lm[2], "Split") == 0);
    CHECK(strcmp(lm[3], "Combined") == 0);

    main::setStagePlatformRowPreset(extra::kPresetGenericMoving);
    CHECK_EQ(1, page.options[3].max);

    main::setStagePlatformRowPreset(extra::kPresetDelfinoSecret);
    CHECK_EQ(3, page.options[3].max);
    const char* const* del = static_cast<const char* const*>(page.options[3].data);
    CHECK(strcmp(del[2], "Tri-Plat") == 0);
    CHECK(strcmp(del[3], "Two-Plat") == 0);

    // Restore default row table so subsequent menu tests see the full 0..4 range.
    main::setStagePlatformRowPreset(extra::kPresetNone);
    CHECK_EQ(main::kStagePlatMaxChoices - 1, page.options[3].max);
}

TEST(stage_options, settings_storage_round_trip_and_sanitization)
{
    host::card::reset();
    bootStorage();

    main::StageOptionsSettings& so = main::settings().stageOptions;
    so.hazards = main::kStageHazardOff;
    so.platformMode = 3;
    so.fodLeft = main::kFodLowest;
    so.fodRight = main::kFodHighest;

    storage::settingsMenuClosed();
    for (int f = 0; f < 4; f++)
    {
        storage::settingsUpdate();
    }

    // Reboot into a fresh settings block and verify kAreaStageOptions round-trips.
    bootStorage();
    const main::StageOptionsSettings& loaded = main::settings().stageOptions;
    CHECK_EQ(main::kStageHazardOff, loaded.hazards);
    CHECK_EQ(3, loaded.platformMode);
    CHECK_EQ(main::kFodLowest, loaded.fodLeft);
    CHECK_EQ(main::kFodHighest, loaded.fodRight);

    // Out-of-range values clamp cleanly in sanitizeSettings.
    main::Settings dirty = main::settings();
    dirty.stageOptions.hazards = 99;
    dirty.stageOptions.platformMode = 99;
    dirty.stageOptions.fodLeft = 99;
    dirty.stageOptions.fodRight = 99;
    main::sanitizeSettings(dirty);
    CHECK_EQ(main::kStageHazardCount - 1, dirty.stageOptions.hazards);
    CHECK_EQ(main::kStagePlatMaxChoices - 1, dirty.stageOptions.platformMode);
    CHECK_EQ(main::kFodHeightCount - 1, dirty.stageOptions.fodLeft);
    CHECK_EQ(main::kFodHeightCount - 1, dirty.stageOptions.fodRight);

    storage::detach();
}
