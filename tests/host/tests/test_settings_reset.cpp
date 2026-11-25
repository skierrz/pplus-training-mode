// Unit tests for the pure per-area, per-page, and per-option settings reset API
// (src/main/settings.cpp, src/main/storage/areas.cpp, src/main/ui/menu.cpp,
// src/main/ui/lab_menu.cpp).
#include <string.h>

#include "host/card.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/cpu/lab_data.h"
#include "main/game/overlays.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/osd_place.h"
#include "main/ui/pages/alter_inputs.h"
#include "main/ui/pages/pages.h"
#include "main/ui/pages/scenarios.h"

using namespace tm;
using main::Settings;

namespace {
    void bootSettings()
    {
        storage::detach();
        host::card::reset();
        memset(ui::menuScratch(), 0, ui::kScratchBytes);
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
    }

    // Populates menuScratch() with valid non-default session values so tests
    // verify resetSettingsAreas/Page/Option do not default scratch rows.
    void populateValidScratch(u8 out[ui::kScratchBytes])
    {
        memset(ui::menuScratch(), 0, ui::kScratchBytes);
        ui::pages::AlterCopy* alter = reinterpret_cast<ui::pages::AlterCopy*>(ui::menuScratch());
        alter->frame = 15;
        alter->axis[0] = 40;
        alter->button[0] = 1;
        ui::pages::ScenarioMenuState* scen = ui::pages::scenarioMenuState();
        scen->preset = 3;
        scen->autoReset = 0; // default is 1
        scen->side = 1;
        scen->multiHitMove = 2;
        memcpy(out, ui::menuScratch(), ui::kScratchBytes);
    }

    void checkBytes(const char* file, int line, const char* label, const void* expected, const void* found, u32 len)
    {
        const u8* e = reinterpret_cast<const u8*>(expected);
        const u8* f = reinterpret_cast<const u8*>(found);
        for (u32 i = 0; i < len; i++)
        {
            if (e[i] != f[i])
            {
                host::test::fail(file, line, "%s differs at offset %lu: expected 0x%02x, found 0x%02x", label, i, e[i],
                                 f[i]);
                return;
            }
        }
    }
#define CHECK_BYTES(label, exp, got, len) checkBytes(__FILE__, __LINE__, (label), (exp), (got), (len))

    // Fills every single area (1..16) with valid non-default values, including
    // fields outside visible lab menu rows (overlay Jumps, cpuTables).
    void populateAllAreasNonDefault(Settings& s)
    {
        // Area 1: General
        s.general.playerPercent = 123;
        s.general.lockPlayerPercent = 1;
        s.general.frameAdvance = 1;
        s.general.ledgeHud = 0;
        s.general.ledgeMaxFrames = 42;
        s.general.ledgeGalint = 0;
        s.general.ledgePanel = 0;
        s.general.cameraMode = 2;
        s.general.gameSpeed = 2;
        s.general.moveStaling = 0;
        s.general.diDisplay = 1;
        s.general.playerInfiniteShields = cpu::kShieldsOn;

        // Area 2: Controls
        s.controls.advanceButton = main::kAdvanceY;
        s.controls.advanceDelay = 15;
        s.controls.startOpensMenu = 0;
        s.controls.dpadLeft = 1;
        s.controls.dpadRight = 1;
        s.controls.decrementButton = main::kDecrementZ;
        s.controls.dpadUp = main::kUpNone;
        s.controls.dpadDown = main::kDownFrameAdvance;
        s.controls.allowCodeMenu = 1;
        s.controls.menuStyle = main::kMenuStyleTabs;

        // Area 3: Display
        s.display.debugOverlay = 1;
        s.display.hud = 0;
        s.display.hitboxes = 3;
        s.display.models = 1;
        s.display.envCollision = 1;
        s.display.stageModel = 0;
        s.display.effects = 0;
        s.display.heatmap = 2;
        s.display.heatmapBounds = 1;
        s.display.heatmapOpacity = 85;
        s.display.heatmapColorFrames = 12;
        s.display.grabVisualizer = 3;

        // Area 4 & 5: Info HMN / CPU
        s.info[0].preset = 1;
        s.info[0].size = main::kInfoLarge;
        s.info[0].rows[0] = 2;
        s.info[0].rows[7] = 4;
        s.info[1].preset = 2;
        s.info[1].size = main::kInfoSmall;
        s.info[1].rows[1] = 3;
        s.info[1].rows[6] = 5;

        // Area 6: OSD
        s.osd.position = osd::kPositionTopLeft;
        s.osd.frameAdvantage = 0;
        s.osd.frameAdvantageOnHit = 0;
        s.osd.advantageTolerance = 4;
        s.osd.wavedash = 0;
        s.osd.lCancel = 0;
        s.osd.actOos = 0;
        s.osd.actOoLag = 1;
        s.osd.jumpCancel = 1;
        s.osd.fastfall = 1;
        s.osd.comboCounter = 1;
        s.osd.sdi = 1;
        s.osd.ledgedashInfo = 0;
        s.osd.actOoHitstun = 1;
        s.osd.actOoAirborne = 1;
        s.osd.dashback = 1;
        s.osd.powershield = 1;
        s.osd.lockoutTimers = 1;
        s.osd.boostGrab = 1;
        s.osd.grabBreakout = 1;
        s.osd.itemThrow = 1;
        s.osd.maxMessages = 3;
        s.osd.preset = main::kOsdPresetCustom;
        s.osd.techSparkle = main::kTechSparkleOff;

        // Area 7: CPU
        s.cpu.percent = 77;
        s.cpu.lockPercent = 1;
        s.cpu.options.control = cpu::kControlTraining;
        s.cpu.options.behavior = cpu::kBehaviorShield;
        s.cpu.options.tdi = cpu::kTdiSurvival;
        s.cpu.options.sdiAmount = 4;
        s.cpu.options.sdiDir = cpu::kSdiAway;
        s.cpu.options.counterGround = 3;
        s.cpu.options.counterAir = 5;
        s.cpu.options.counterShield = 4;
        s.cpu.options.counterDelay = 7;
        s.cpu.options.infiniteShields = cpu::kShieldsOff;
        s.cpu.options.shieldHealth = 35;
        s.cpu.options.intangible = cpu::kIntangibleNoReaction;
        s.cpu.options.tech = cpu::kTechAway;
        s.cpu.options.getup = cpu::kGetupAttack;
        s.cpu.options.techChance[0] = 40;
        s.cpu.options.techChance[1] = 20;
        s.cpu.options.techChance[2] = 20;
        s.cpu.options.techChance[3] = 20;
        s.cpu.options.getupChance[0] = 10;
        s.cpu.options.getupChance[1] = 30;
        s.cpu.options.getupChance[2] = 30;
        s.cpu.options.getupChance[3] = 30;
        s.cpu.options.asdi = cpu::kAsdiTowards;
        s.cpu.options.shieldAngle = cpu::kAngleDown;
        s.cpu.options.grabEscape = cpu::kMashHigh;
        s.cpu.options.grabRelease = cpu::kReleaseAirborne;
        s.cpu.options.controlledBy = 2;
        s.cpu.options.techSound = 1;
        s.cpu.options.techTrap = cpu::kTrapLatest;
        s.cpu.options.techLockout = cpu::kLockoutLatest;
        s.cpu.options.missTechWait = 50;
        s.cpu.options.advHit = 4;
        s.cpu.options.advLogic = cpu::kLogicCustom;
        s.cpu.options.advGround = 5;
        s.cpu.options.advAir = 3;
        s.cpu.options.advShield = 2;
        s.cpu.options.advDelayGround = 6;
        s.cpu.options.advDelayAir = 7;
        s.cpu.options.advDelayShield = 8;
        s.cpu.options.recovery = cpu::kRecoveryGame;
        s.cpu.options.shieldTiming = cpu::kShieldTimingFirstGap;
        s.cpu.options.ledgeOption = cpu::kLedgeRoll;
        s.cpu.options.clatterEscape = cpu::kMashMedium;
        s.cpu.options.adaptiveMode = cpu::kAdaptiveActive;
        s.cpu.options.dualStickSdi = 1;

        // Area 8: Recording
        s.recording.randomPercent[0] = 15;
        s.recording.randomPercent[1] = 30;
        s.recording.hmnMode = 2;
        s.recording.hmnSlot = 4;
        s.recording.cpuMode = 3;
        s.recording.cpuSlot = 5;
        s.recording.mirrored = 1;
        s.recording.cpuCounter = 2;
        s.recording.loop = 1;
        s.recording.autoRestore = 1;
        s.recording.startPaused = 1;
        s.recording.takeover = 1;
        s.recording.hmnChance[0] = 60;
        s.recording.hmnChance[1] = 40;
        s.recording.cpuChance[2] = 100;
        s.recording.manageSide = 1;
        s.recording.manageSlot = 3;
        s.recording.manageCopyTo = 2;
        s.recording.startOnFirstInput = 1;

        // Area 9 & 10: Overlays (including non-row kGroupJumps = 12)
        s.overlay[0].preset = 1;
        s.overlay[0].groups[game::kGroupWait] = game::kOverlayGreen;
        s.overlay[0].groups[game::kGroupJumps] = game::kOverlayRed;
        s.overlay[1].preset = 1;
        s.overlay[1].groups[game::kGroupIasa] = game::kOverlayOrange;
        s.overlay[1].groups[game::kGroupJumps] = game::kOverlayBlue;

        // Area 11: Tech Invisibility
        s.techInvis.mode = main::kTechInvisAll;
        s.techInvis.delay = 9;

        // Area 12: CPU Tables (Advanced Counter pages, outside menu rows)
        s.cpuTables.ready = 0;
        for (int i = 0; i < cpu::kAdvancedHits; i++)
        {
            s.cpuTables.hits[i].logic = cpu::kLogicCustom;
            s.cpuTables.hits[i].action[0] = 2;
            s.cpuTables.hits[i].action[1] = 3;
            s.cpuTables.hits[i].action[2] = 4;
            s.cpuTables.hits[i].delay[0] = 5;
            s.cpuTables.hits[i].delay[1] = 6;
            s.cpuTables.hits[i].delay[2] = 7;
        }

        // Area 13: Character RNG
        s.characterRng.peachTurnip = main::kTurnipStitchFace;
        s.characterRng.peachSmash = main::kSmashFryingPan;
        s.characterRng.gnwHammer = 9;
        s.characterRng.luigiMisfire = main::kMisfireAlways;

        // Area 14: Stage Options
        s.stageOptions.hazards = main::kStageHazardOff;
        s.stageOptions.platformMode = main::kStagePlatFreeze;
        s.stageOptions.fodLeft = main::kFodHidden;
        s.stageOptions.fodRight = main::kFodHighest;

        // Area 15: Combo Trainer
        s.comboTrainer.enabled = 1;
        s.comboTrainer.noclip = 1;
        s.comboTrainer.fixPosition = 1;
        s.comboTrainer.randomizePosition = 1;
        s.comboTrainer.randOnGround = 1;
        s.comboTrainer.randomizeDamage = 1;
        s.comboTrainer.recoveryTrainer = 1;
        s.comboTrainer.comboTimerAdjustment = -12;
        s.comboTrainer.fixedDamage = 65;
        s.comboTrainer.enabledMeters = main::kMeterHitlag | main::kMeterDi;

        // Area 16: Fighter Mechanics
        s.fighterMechanics.peachFloat = main::kMechAlways;
        s.fighterMechanics.gnwBucket = main::kBucketFull;
        s.fighterMechanics.samusBeam = main::kBeamIce;
        s.fighterMechanics.mewtwoHover = main::kMechAlways;
        s.fighterMechanics.lucarioAura = main::kAura2;
        s.fighterMechanics.robBeam = main::kRobBeamSuper;
        s.fighterMechanics.dededeWaddle = main::kWaddleGordo;
        s.fighterMechanics.nanaThrow = main::kNanaThrowUp;
    }
}

TEST(settings_reset, area_bounds_cover_all_16_areas_with_no_gaps_or_overlaps)
{
    CHECK_EQ(436u, static_cast<u32>(sizeof(Settings)));

    u16 off = 0xBEEFu;
    u16 sz = 0xCAFEu;
    CHECK(!storage::areaBounds(-1, off, sz));
    CHECK(!storage::areaBounds(0, off, sz));
    CHECK(!storage::areaBounds(storage::kAreaLast + 1, off, sz));
    CHECK(!storage::areaBounds(99, off, sz));
    CHECK_EQ(0xBEEFu, static_cast<u32>(off));
    CHECK_EQ(0xCAFEu, static_cast<u32>(sz));

    u8 covered[sizeof(Settings)];
    memset(covered, 0, sizeof(covered));
    for (int id = 1; id <= storage::kAreaLast; id++)
    {
        REQUIRE(storage::areaBounds(id, off, sz));
        CHECK(sz > 0);
        CHECK(off >= offsetof(Settings, general));
        CHECK(static_cast<u32>(off) + sz <= sizeof(Settings));
        for (u32 i = off; i < static_cast<u32>(off) + sz; i++)
        {
            covered[i]++;
        }
    }
    for (u32 i = 0; i < offsetof(Settings, general); i++)
    {
        CHECK_EQ(0, static_cast<int>(covered[i]));
    }
    for (u32 i = offsetof(Settings, general); i < sizeof(Settings); i++)
    {
        CHECK_EQ(1, static_cast<int>(covered[i]));
    }
}

TEST(settings_reset, reset_all_areas_is_bit_for_bit_identical_to_reset_settings)
{
    bootSettings();
    main::resetSettings();
    const Settings fromResetAll = main::settings();

    // Test from two distinct corrupt/dirty byte patterns covering every payload
    // byte (including reserved tails, non-row overlay Jumps, cpuTables, bit 7
    // of enabledMeters, and out-of-range heatmap fields).
    static const u8 kPatterns[2] = {0xA5, 0xFF};
    for (int p = 0; p < 2; p++)
    {
        u8* payload = reinterpret_cast<u8*>(&main::settings()) + offsetof(Settings, general);
        memset(payload, kPatterns[p], sizeof(Settings) - offsetof(Settings, general));

        u8 scratchBefore[ui::kScratchBytes];
        populateValidScratch(scratchBefore);

        u32 done = main::resetSettingsAreas(storage::kAllAreas);
        CHECK_EQ(storage::kAllAreas, done);
        const Settings fromResetAreasAll = main::settings();

        CHECK_BYTES("resetSettingsAreas(kAllAreas) vs resetSettings()", &fromResetAll, &fromResetAreasAll,
                    sizeof(Settings));
        CHECK(memcmp(&fromResetAll, &fromResetAreasAll, sizeof(Settings)) == 0);
        CHECK(memcmp(scratchBefore, ui::menuScratch(), sizeof(scratchBefore)) == 0);
        CHECK_EQ(0, host::card::writes());
    }
}

TEST(settings_reset, every_individual_area_resets_only_its_own_byte_slice)
{
    bootSettings();
    main::resetSettings();
    const Settings defaults = main::settings();

    Settings dirty = defaults;
    populateAllAreasNonDefault(dirty);
    main::settings() = dirty;
    ui::clampSettings();
    main::sanitizeSettings(main::settings());
    // Confirm our non-default fixture is already valid and stable under clamp/sanitize.
    CHECK_BYTES("dirty fixture stable", &dirty, &main::settings(), sizeof(Settings));

    const u8* defBytes = reinterpret_cast<const u8*>(&defaults);
    const u8* dirtyBytes = reinterpret_cast<const u8*>(&dirty);
    for (int id = 1; id <= storage::kAreaLast; id++)
    {
        u16 off = 0, sz = 0;
        REQUIRE(storage::areaBounds(id, off, sz));
        // Every area must start genuinely non-default so the reset is observable.
        CHECK(memcmp(defBytes + off, dirtyBytes + off, sz) != 0);

        main::settings() = dirty;
        CHECK(main::resetSettingsArea(id));

        const u8* got = reinterpret_cast<const u8*>(&main::settings());
        CHECK_BYTES("target area slice", defBytes + off, got + off, sz);
        CHECK_BYTES("bytes before target area", dirtyBytes, got, off);
        CHECK_BYTES("bytes after target area", dirtyBytes + off + sz, got + off + sz,
                    static_cast<u32>(sizeof(Settings)) - off - sz);
    }

    // Multi-area mask with mixed valid and invalid bits resets only the valid areas.
    main::settings() = dirty;
    const u32 validMask = storage::areaBit(storage::kAreaDisplay) | storage::areaBit(storage::kAreaCpuTables)
                          | storage::areaBit(storage::kAreaComboTrainer);
    u32 done = main::resetSettingsAreas(validMask | (1u << 0) | (1u << 25));
    CHECK_EQ(validMask, done);

    Settings expected = dirty;
    expected.display = defaults.display;
    expected.cpuTables = defaults.cpuTables;
    expected.comboTrainer = defaults.comboTrainer;
    CHECK_BYTES("multi-area reset", &expected, &main::settings(), sizeof(Settings));
    CHECK_EQ(0, host::card::writes());
}

TEST(settings_reset, invalid_area_ids_and_empty_mask_do_nothing)
{
    bootSettings();
    u8* payload = reinterpret_cast<u8*>(&main::settings()) + offsetof(Settings, general);
    memset(payload, 0xEE, sizeof(Settings) - offsetof(Settings, general));
    const Settings before = main::settings();

    CHECK(!main::resetSettingsArea(-1));
    CHECK(!main::resetSettingsArea(0));
    CHECK(!main::resetSettingsArea(storage::kAreaLast + 1));
    CHECK(!main::resetSettingsArea(32));
    CHECK(!main::resetSettingsArea(33));
    CHECK_EQ(0u, main::resetSettingsAreas(0));
    CHECK_EQ(0u, main::resetSettingsAreas(1u << 0));
    CHECK_EQ(0u, main::resetSettingsAreas(0xFFFE0000u));

    CHECK_BYTES("invalid area reset must not mutate", &before, &main::settings(), sizeof(Settings));
}

TEST(settings_reset, reset_settings_page_resets_only_target_page)
{
    bootSettings();
    main::resetSettings();
    const Settings defaults = main::settings();

    Settings dirty = defaults;
    populateAllAreasNonDefault(dirty);

    // P_COMBO_METERS: resets enabledMeters to 0x00 (safe fresh default) without touching other comboTrainer or page settings.
    main::settings() = dirty;
    CHECK(main::resetSettingsPage(ui::pages::P_COMBO_METERS));
    Settings expected = dirty;
    expected.comboTrainer.enabledMeters = 0;
    CHECK_BYTES("P_COMBO_METERS", &expected, &main::settings(), sizeof(Settings));

    // P_COMBO_TRAINER: resets rows 0..8 without touching enabledMeters (on P_COMBO_METERS) or other pages.
    main::settings() = dirty;
    CHECK(main::resetSettingsPage(ui::pages::P_COMBO_TRAINER));
    expected = dirty;
    expected.comboTrainer = defaults.comboTrainer;
    expected.comboTrainer.enabledMeters = dirty.comboTrainer.enabledMeters;
    CHECK_BYTES("P_COMBO_TRAINER", &expected, &main::settings(), sizeof(Settings));

    // P_STAGE_CAST: resets stageOptions, characterRng, and fighterMechanics without touching other pages or scratch.
    main::settings() = dirty;
    u8 stageScratchBefore[ui::kScratchBytes];
    populateValidScratch(stageScratchBefore);
    ui::menuScratch()[ui::kShowInactiveScratchOffset] = 1;
    stageScratchBefore[ui::kShowInactiveScratchOffset] = 1;
    CHECK(main::resetSettingsPage(ui::pages::P_STAGE_CAST));
    expected = dirty;
    expected.stageOptions = defaults.stageOptions;
    expected.characterRng = defaults.characterRng;
    expected.fighterMechanics = defaults.fighterMechanics;
    CHECK_BYTES("P_STAGE_CAST", &expected, &main::settings(), sizeof(Settings));
    CHECK(memcmp(stageScratchBefore, ui::menuScratch(), sizeof(stageScratchBefore)) == 0);

    // P_CPU: resets top-level CPU page rows while preserving CPU subpage fields (DI, Counters, Tech, Defense).
    main::settings() = dirty;
    CHECK(main::resetSettingsPage(ui::pages::P_CPU));
    CHECK_EQ(defaults.cpu.percent, main::settings().cpu.percent);
    CHECK_EQ(defaults.cpu.lockPercent, main::settings().cpu.lockPercent);
    CHECK_EQ(defaults.cpu.options.control, main::settings().cpu.options.control);
    CHECK_EQ(defaults.cpu.options.recovery, main::settings().cpu.options.recovery);
    CHECK_EQ(dirty.cpu.options.tdi, main::settings().cpu.options.tdi);
    CHECK_EQ(dirty.cpu.options.counterGround, main::settings().cpu.options.counterGround);
    CHECK_EQ(dirty.cpu.options.dualStickSdi, main::settings().cpu.options.dualStickSdi);
    // Resetting P_CPU_DI and P_CPU_COUNTERS individually resets only their own rows.
    CHECK(main::resetSettingsPage(ui::pages::P_CPU_DI));
    CHECK_EQ(defaults.cpu.options.tdi, main::settings().cpu.options.tdi);
    CHECK_EQ(defaults.cpu.options.dualStickSdi, main::settings().cpu.options.dualStickSdi);
    CHECK_EQ(dirty.cpu.options.counterGround, main::settings().cpu.options.counterGround);
    CHECK(main::resetSettingsPage(ui::pages::P_CPU_COUNTERS));
    CHECK_EQ(defaults.cpu.options.counterGround, main::settings().cpu.options.counterGround);
    // Tech & Getup and Advanced Counter fields live on separate pages and stay untouched.
    CHECK_EQ(dirty.cpu.options.tech, main::settings().cpu.options.tech);
    CHECK_EQ(dirty.cpu.options.getup, main::settings().cpu.options.getup);
    CHECK_EQ(dirty.cpu.options.advHit, main::settings().cpu.options.advHit);
    CHECK_EQ(dirty.cpu.options.advLogic, main::settings().cpu.options.advLogic);
    CHECK_EQ(dirty.techInvis.mode, main::settings().techInvis.mode);
    CHECK_BYTES("P_CPU leaves general untouched", &dirty.general, &main::settings().general, sizeof(dirty.general));
    CHECK_BYTES("P_CPU leaves display untouched", &dirty.display, &main::settings().display, sizeof(dirty.display));
    CHECK_BYTES("P_CPU leaves cpuTables untouched", &dirty.cpuTables, &main::settings().cpuTables,
                sizeof(dirty.cpuTables));

    // Scratch-only page leaves both menuScratch() and Settings untouched.
    main::settings() = dirty;
    u8 scratchBefore[ui::kScratchBytes];
    populateValidScratch(scratchBefore);
    CHECK(main::resetSettingsPage(ui::pages::P_SCENARIOS));
    CHECK_BYTES("P_SCENARIOS leaves Settings untouched", &dirty, &main::settings(), sizeof(Settings));
    CHECK(memcmp(scratchBefore, ui::menuScratch(), sizeof(scratchBefore)) == 0);

    // Out-of-range pages return false with zero mutations (even on out-of-range fields).
    main::settings().display.heatmapOpacity = 250;
    const Settings unmutated = main::settings();
    CHECK(!main::resetSettingsPage(-1));
    CHECK(!main::resetSettingsPage(ui::pages::P_COUNT));
    CHECK(!main::resetSettingsPage(99));
    CHECK_BYTES("invalid page reset must not mutate", &unmutated, &main::settings(), sizeof(Settings));
    CHECK_EQ(0, host::card::writes());
}

TEST(settings_reset, reset_settings_option_resets_single_row_and_preserves_sibling_bits)
{
    bootSettings();
    main::resetSettings();
    const Settings defaults = main::settings();

    Settings dirty = defaults;
    populateAllAreasNonDefault(dirty);

    // Resetting each kFlagBit row on P_COMBO_METERS restores ONLY that single bit to default 0.
    static const u8 kBits[7] = {
        main::kMeterHitlag, main::kMeterHitstun, main::kMeterShieldstun, main::kMeterDi,
        main::kMeterIasa,   main::kMeterLedgeIntang, main::kMeterIntang
    };
    for (int r = 0; r < 7; r++)
    {
        main::settings() = dirty;
        main::settings().comboTrainer.enabledMeters = main::kMeterAll;
        CHECK(main::resetSettingsOption(ui::pages::P_COMBO_METERS, r));
        CHECK_EQ(static_cast<int>(main::kMeterAll & ~kBits[r]),
                 static_cast<int>(main::settings().comboTrainer.enabledMeters));

        // Sibling bits already set stay set; sibling bits already clear stay clear.
        const u8 initial = static_cast<u8>(kBits[r] | kBits[(r + 1) % 7]);
        main::settings().comboTrainer.enabledMeters = initial;
        CHECK(main::resetSettingsOption(ui::pages::P_COMBO_METERS, r));
        CHECK_EQ(static_cast<int>(kBits[(r + 1) % 7]), static_cast<int>(main::settings().comboTrainer.enabledMeters));
    }

    // Corrupt bit 7 is cleared by sanitizeSettings while valid sibling bits remain intact.
    main::settings() = dirty;
    main::settings().comboTrainer.enabledMeters = static_cast<u8>(0x80u | main::kMeterHitlag | main::kMeterHitstun);
    CHECK(main::resetSettingsOption(ui::pages::P_COMBO_METERS, 1)); // Hitstun -> default 0
    CHECK_EQ(static_cast<int>(main::kMeterHitlag), static_cast<int>(main::settings().comboTrainer.enabledMeters));

    // Standard halfword, signed byte, choice, and toggle rows reset individually.
    main::settings() = dirty;
    CHECK(main::resetSettingsOption(ui::pages::P_GENERAL, 1)); // P1 Damage (u16)
    Settings expected = dirty;
    expected.general.playerPercent = defaults.general.playerPercent;
    CHECK_BYTES("single u16 option", &expected, &main::settings(), sizeof(Settings));

    main::settings() = dirty;
    CHECK(main::resetSettingsOption(ui::pages::P_COMBO_TRAINER, 8)); // Timer Adjust (s8)
    expected = dirty;
    expected.comboTrainer.comboTimerAdjustment = defaults.comboTrainer.comboTimerAdjustment;
    CHECK_BYTES("single s8 option", &expected, &main::settings(), sizeof(Settings));

    main::settings() = dirty;
    CHECK(main::resetSettingsOption(ui::pages::P_CONTROLS, 0)); // Frame Advance button (choice)
    expected = dirty;
    expected.controls.advanceButton = defaults.controls.advanceButton;
    CHECK_BYTES("single choice option", &expected, &main::settings(), sizeof(Settings));

    main::settings() = dirty;
    CHECK(main::resetSettingsOption(ui::pages::P_DISPLAY, 0)); // Match HUD (toggle)
    expected = dirty;
    expected.display.hud = defaults.display.hud;
    CHECK_BYTES("single toggle option", &expected, &main::settings(), sizeof(Settings));

    // Non-stored rows (menu, func, scratch) and out-of-range page/row return false with zero mutations.
    main::settings() = dirty;
    main::settings().display.heatmapOpacity = 250; // out of range to verify no clamp/sanitize runs
    const Settings unmutated = main::settings();
    u8 scratchBefore[ui::kScratchBytes];
    memset(scratchBefore, 0x6B, sizeof(scratchBefore));
    memcpy(ui::menuScratch(), scratchBefore, sizeof(scratchBefore));

    CHECK(!main::resetSettingsOption(ui::pages::P_GENERAL, 9));    // Controls (kKindMenu)
    CHECK(!main::resetSettingsOption(ui::pages::P_GENERAL, 10));   // Reset Settings (kKindFunc)
    CHECK(!main::resetSettingsOption(ui::pages::P_STAGE_CAST, 1)); // Show Inactive (kFlagScratch)
    CHECK(!main::resetSettingsOption(ui::pages::P_SCENARIOS, 0));  // kFlagScratch
    CHECK(!main::resetSettingsOption(ui::pages::P_ALTER_INPUTS, 0));
    CHECK(!main::resetSettingsOption(ui::pages::P_FIGHTER_SWAP, 0));
    CHECK(!main::resetSettingsOption(-1, 0));
    CHECK(!main::resetSettingsOption(ui::pages::P_COUNT, 0));
    CHECK(!main::resetSettingsOption(ui::pages::P_GENERAL, -1));
    CHECK(!main::resetSettingsOption(ui::pages::P_GENERAL, ui::pages::kGeneral.count));

    CHECK_BYTES("non-stored/invalid option must not mutate", &unmutated, &main::settings(), sizeof(Settings));
    CHECK(memcmp(scratchBefore, ui::menuScratch(), sizeof(scratchBefore)) == 0);
    memset(ui::menuScratch(), 0, ui::kScratchBytes);
    CHECK_EQ(0, host::card::writes());
}

TEST(settings_reset, menu_default_helpers_respect_flags_and_range_boundaries)
{
    static u8 buf[6];
    struct BufVals {
        static u8* ptr() { return buf; }
    };
    static const ui::Option kRows[] = {
        {ui::kKindToggle, ui::kFlagBit, 0, 0x01, 1, 1, 0, "Bit1", NULL, NULL, NULL, NULL},
        {ui::kKindToggle, ui::kFlagBit, 0, 0x02, 1, 0, 0, "Bit0", NULL, NULL, NULL, NULL},
        {ui::kKindInt, ui::kFlagHalf, 2, 0, 999, 250, 0, "Half", NULL, "%d", NULL, NULL},
        {ui::kKindToggle, ui::kFlagUnavailable, 4, 0, 1, 1, 0, "NA", NULL, NULL, NULL, NULL},
        {ui::kKindInt, ui::kFlagScratch, 0, 0, 99, 12, 0, "Scratch", NULL, "%d", NULL, NULL},
        {ui::kKindInfo, 0, 5, 0, 0, 0, 0, "Info", NULL, NULL, NULL, NULL},
    };
    static const ui::Page kPage = {"Custom", kRows, 6, {0, 0, 0}};
    static const ui::Page* const kPages[] = {&kPage};
    static const ui::MenuDef kMenu = {kPages, 1, BufVals::ptr};

    memset(buf, 0xFF, sizeof(buf));
    ui::menuScratch()[0] = 0x77;

    // defaultOption: bit rows honor defaults 1 and 0 without touching sibling bits.
    buf[0] = 0xAA; // bit 0 = 0, bit 1 = 1
    CHECK(ui::defaultOption(kMenu, kRows[0]));
    CHECK_EQ(0xAB, static_cast<int>(buf[0]));
    CHECK(ui::defaultOption(kMenu, kRows[1]));
    CHECK_EQ(0xA9, static_cast<int>(buf[0]));
    // Unavailable, scratch, and info return false and do not mutate.
    CHECK(!ui::defaultOption(kMenu, kRows[3]));
    CHECK(!ui::defaultOption(kMenu, kRows[4]));
    CHECK(!ui::defaultOption(kMenu, kRows[5]));
    CHECK_EQ(0xFF, static_cast<int>(buf[4]));
    CHECK_EQ(0xFF, static_cast<int>(buf[5]));
    CHECK_EQ(0x77, static_cast<int>(ui::menuScratch()[0]));

    // defaultRange: partial overlap on a 2-byte halfword [2, 4) does not reset it.
    *reinterpret_cast<u16*>(buf + 2) = 888;
    ui::defaultRange(kMenu, 2, 1); // [2, 3) does not contain all of [2, 4)
    CHECK_EQ(888, static_cast<int>(*reinterpret_cast<u16*>(buf + 2)));
    ui::defaultRange(kMenu, 3, 2); // [3, 5) does not start at <= 2
    CHECK_EQ(888, static_cast<int>(*reinterpret_cast<u16*>(buf + 2)));
    ui::defaultRange(kMenu, 2, 0); // empty range
    CHECK_EQ(888, static_cast<int>(*reinterpret_cast<u16*>(buf + 2)));
    // Exact fit [2, 4) resets it without touching scratch at offset 0.
    ui::defaultRange(kMenu, 2, 2);
    CHECK_EQ(250, static_cast<int>(*reinterpret_cast<u16*>(buf + 2)));
    CHECK_EQ(0x77, static_cast<int>(ui::menuScratch()[0]));

    // defaultPage: out of range returns false; in range resets non-scratch stored rows.
    CHECK(!ui::defaultPage(kMenu, -1));
    CHECK(!ui::defaultPage(kMenu, 1));
    buf[0] = 0xFE;
    *reinterpret_cast<u16*>(buf + 2) = 777;
    CHECK(ui::defaultPage(kMenu, 0));
    CHECK_EQ(0xFD, static_cast<int>(buf[0]));
    CHECK_EQ(250, static_cast<int>(*reinterpret_cast<u16*>(buf + 2)));
    CHECK_EQ(0x77, static_cast<int>(ui::menuScratch()[0]));
    memset(ui::menuScratch(), 0, ui::kScratchBytes);
}
