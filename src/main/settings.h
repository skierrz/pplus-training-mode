#pragma once

#include <stddef.h>
#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "shared/block_layout.h"
#include "shared/interface.h"
#include "shared/version.h"

// Every lab setting, in one plain struct kept in the stub's settings block
// so it survives the match module's unloading between matches. Only bytes
// and big-endian halfwords, so it can be written to the SD card as it is;
// a new layout bumps kSettingsVersion and old blocks are reset to defaults.
//
// One struct per feature area, each with its own reserved bytes: an area's
// new settings go into its reserved space (shrunk by the size added), so no
// other area's fields move. Defaults of fields a menu row shows come from
// that row (ui/pages/pages.h); resetSettings() sets the rest.
namespace tm {
namespace main {
    enum {
        kSettingsMagic = 0x544D5354, // "TMST"
        kSettingsVersion = 1,
        kInfoRows = 8
    };

    // Who a per-fighter block describes: the human player or the CPU.
    enum Side {
        kSideHuman = 0,
        kSideCpu = 1,
        kSideCount = 2
    };

    // Controls > Frame Advance Button.
    enum AdvanceButton {
        kAdvanceL = 0,
        kAdvanceZ,
        kAdvanceX,
        kAdvanceY,
        kAdvanceR,
        kAdvanceButtonCount
    };

    // Info display text sizes.
    enum InfoSize {
        kInfoSmall = 0,
        kInfoMedium,
        kInfoLarge,
        kInfoSizeCount
    };

    // General page and its Ledgedash HUD page.
    struct GeneralSettings {
        u16 playerPercent;        // 0-999
        u8 lockPlayerPercent;     // 0/1
        u8 frameAdvance;          // 0/1
        u8 ledgeHud;              // 0/1
        u8 ledgeMaxFrames;        // cells in the bar, 30-64
        u8 ledgeGalint;           // 0/1: GALINT cells after the landing
        u8 ledgePanel;            // 0/1: the info panel beside the bar
        u8 cameraMode;            // display::CameraMode
        u8 gameSpeed;             // display::Speed
        u8 moveStaling;           // 0/1
        u8 diDisplay;             // 0/1
        u8 playerInfiniteShields; // cpu::InfiniteShields (Off / On Until Hit / On)
        u8 reserved[3];
    };

    // Controls > Menu Style.
    enum MenuStyle {
        kMenuStyleRail = 0,
        kMenuStyleTabs = 1,
        kMenuStyleCount
    };

    // Controls page.
    struct ControlSettings {
        u8 advanceButton; // AdvanceButton
        u8 advanceDelay;  // frames the Frame Advance Button is held before the game runs
        u8 startOpensMenu; // 0: Z inside Training's pause; 1: also Start in the match (default)
        u8 dpadLeft;       // savestate::DpadLeft
        u8 dpadRight;      // savestate::DpadRight
        u8 decrementButton; // DecrementButton
        u8 dpadUp;         // DpadUp
        u8 dpadDown;       // DpadDown
        u8 allowCodeMenu;  // 0: blocked in Training (default); 1: L+R+D-pad Down allowed
        u8 menuStyle;      // MenuStyle (0: Category Rail, 1: Top Tabs)
        u8 reserved[6];
    };

    enum DecrementButton {
        kDecrementNone = 0,
        kDecrementZ,
        kDecrementL,
        kDecrementR,
        kDecrementX,
        kDecrementY,
        kDecrementButtonCount
    };
    enum DpadUp { kUpTaunt = 0, kUpNone };
    enum DpadDown { kDownNone = 0, kDownFrameAdvance };

    // Display Options page and the Debug page.
    struct DisplaySettings {
        u8 debugOverlay; // 0/1
        u8 hud;          // 0/1
        u8 hitboxes;     // display::Who
        u8 models;       // display::Who: whose models are shown
        u8 envCollision; // 0/1
        u8 stageModel;   // 0/1
        u8 effects;      // 0/1
        u8 heatmap;            // display::Who: whose attack hitboxes enter the heatmap
        u8 heatmapBounds;      // 0/1: world-space AABB reach outline
        u8 heatmapOpacity;     // 20-100 (%)
        u8 heatmapColorFrames; // 1-30 (frames per color band)
        u8 grabVisualizer;     // display::Who (0 Off, 1 P1, 2 CPU, 3 Both)
        u8 reserved[4];
    };

    // HMN / CPU Info Display pages, one per side.
    struct InfoSettings {
        u8 preset;          // ui::InfoPreset
        u8 size;            // InfoSize
        u8 rows[kInfoRows]; // ui::InfoValue per row
        u8 reserved[6];
    };

    // Display > Theme: OSD background color presets and opacity steps (0..10 = 0%..100%).
    enum OsdBgColor {
        kOsdBgTeal = 0,
        kOsdBgDarkSlate,
        kOsdBgBrawlNavy,
        kOsdBgCharcoal,
        kOsdBgBlack,
        kOsdBgCrimson,
        kOsdBgViolet,
        kOsdBgColorCount
    };
    enum {
        kOsdAlphaMax = 10,
        kDefaultOsdAlpha = 5
    };

    // OSD Menu page.
    struct OsdSettings {
        u8 position;           // osd::Position
        u8 frameAdvantage;     // 0/1: on shield
        u8 frameAdvantageOnHit; // 0/1
        u8 advantageTolerance; // frames, 0-5
        // The other messages, each 0/1.
        u8 wavedash;
        u8 lCancel;
        u8 actOos;
        u8 actOoLag;
        u8 jumpCancel;
        u8 fastfall;
        u8 comboCounter;
        u8 sdi;
        u8 ledgedashInfo;
        u8 actOoHitstun;
        u8 actOoAirborne;
        u8 dashback;
        u8 powershield;
        u8 lockoutTimers;
        u8 boostGrab;
        u8 grabBreakout;
        u8 itemThrow;
        u8 maxMessages; // 1-6 per player (osd::kPerPlayer)
        u8 bgColor;     // OsdBgColor (0..6, default kOsdBgTeal)
        u8 bgAlpha;     // 0..10 (0%..100% in 10% steps, default 5 = 50%)
        u8 reserved[8];
    };

    // CPU Options and its subpages (Tech, Advanced Counter).
    struct CpuSettings {
        u16 percent;    // 0-999
        u8 lockPercent; // 0/1
        cpu::CpuOptions options; // the lab's CPU control (main/cpu/cpu_settings.h)
        u8 reserved[61 - sizeof(cpu::CpuOptions)];
    };

    // Recording, savestates and their subpages. Choice values are the
    // indices of the rows' labels in ui/pages/recording.cpp and its subpages.
    struct RecordingSettings {
        u8 randomPercent[kSideCount]; // up to this much damage added on each savestate load or restore
        u8 hmnMode;          // Off, Record, Playback, Re-Record
        u8 hmnSlot;          // 0 Random, 1-6
        u8 cpuMode;          // Off, Control, Record, Playback, Re-Record
        u8 cpuSlot;          // 0 Random, 1-6
        u8 mirrored;         // Off, On, Random
        u8 cpuCounter;       // Off, After Playback Ends, On CPU Hit, On HMN Hit, On Any Hit
        u8 loop;             // 0/1
        u8 autoRestore;      // Off, Playback Ends, CPU Counters
        u8 startPaused;      // 0/1
        u8 takeover;         // HMN, CPU, None
        u8 hmnChance[6];     // percent per slot (Random)
        u8 cpuChance[6];
        u8 manageSide;       // Slot Management: HMN, CPU
        u8 manageSlot;       // 0-5
        u8 manageCopyTo;     // 0-5
        u8 startOnFirstInput; // 0/1: arm recording until first non-neutral input
        u8 reserved[36];
    };

    enum { kOverlayGroups = 17 };

    // HMN / CPU Color Overlays, one per side: a choice per state group
    // (ui::OverlayChoice), in the page's order.
    struct OverlaySettings {
        u8 preset; // 0 Custom, 1 ProjectPunch
        u8 groups[kOverlayGroups];
        u8 reserved[2];
    };

    // Tech invisibility (shown on Tech Options).
    enum TechInvisibility {
        kTechInvisOff = 0,
        kTechInvisCpu,
        kTechInvisAll,
        kTechInvisCount
    };
    struct TechInvisSettings {
        u8 mode;  // TechInvisibility
        u8 delay; // delay in frames after the distinguishable frame, 0-15
        u8 reserved[2];
    };

    // Character RNG page: what a character's random move gives. 0 is the
    // game's own choice in every row. The turnip's faces are in the order
    // of the turnip's own face numbers, which is also the order of their
    // thrown damage (extra/character_rng.cpp).
    enum PeachTurnip {
        kTurnipDefault = 0,
        kTurnipSmile,
        kTurnipTEyes,
        kTurnipLineEyes,
        kTurnipCircleEyes,
        kTurnipArchedEyes,
        kTurnipWinky,
        kTurnipDotEyes,
        kTurnipStitchFace,
        kTurnipMrSaturn,
        kPeachTurnipCount
    };
    enum PeachSmash {
        kSmashDefault = 0,
        kSmashGolfClub,
        kSmashFryingPan,
        kSmashTennisRacket,
        kPeachSmashCount
    };
    enum { kHammerDefault = 0, kGnwHammerCount = 10 };
    enum LuigiMisfire { kMisfireDefault = 0, kMisfireAlways, kMisfireNever, kLuigiMisfireCount };
    struct CharacterRngSettings {
        u8 peachTurnip; // PeachTurnip
        u8 peachSmash;  // PeachSmash
        u8 gnwHammer;   // 0 Default, else the Judge number (1-9)
        u8 luigiMisfire; // LuigiMisfire
    };

    // Stage Options page (extra/stage_options.cpp): 0 is the stage's own
    // default / normal cycle in every field.
    enum StageHazardMode {
        kStageHazardDefault = 0,
        kStageHazardOn,
        kStageHazardOff,
        kStageHazardCount
    };

    // Fountain of Dreams side platform heights (exact P+ 3.2.1 STDT[20] frames:
    // 0, 39, 43, 57, 64, 75).
    enum StageFodHeight {
        kFodDefault = 0,
        kFodHidden,
        kFodLowest,
        kFodLeftDefault,
        kFodAverage,
        kFodRightDefault,
        kFodHighest,
        kFodHeightCount
    };

    enum {
        kStagePlatCycle = 0,
        kStagePlatFreeze = 1,
        kStagePlatMaxChoices = 5
    };

    struct StageOptionsSettings {
        u8 hazards;      // StageHazardMode (0 Default, 1 On, 2 Off)
        u8 platformMode; // 0 Cycle, 1 Freeze, 2..4 per-stage preset
        u8 fodLeft;      // StageFodHeight (0..6)
        u8 fodRight;     // StageFodHeight (0..6)
        u8 reserved[12];
    };

    // Combo Trainer floating meter bitmask (7 bits).
    enum ComboMeterBit {
        kMeterHitlag = 1 << 0,
        kMeterHitstun = 1 << 1,
        kMeterShieldstun = 1 << 2,
        kMeterDi = 1 << 3,
        kMeterIasa = 1 << 4,
        kMeterLedgeIntang = 1 << 5,
        kMeterIntang = 1 << 6,
        kMeterAll = 0x7F
    };

    // Combo Trainer backend settings (src/extra/combo_trainer.*).
    struct ComboTrainerSettings {
        u8 enabled;              // 0/1
        u8 noclip;               // 0/1: set Correct_None on reset, restore ground/air next frame
        u8 fixPosition;          // 0/1: reset on hitstun drop
        u8 randomizePosition;    // 0/1: pick random safe stage position on reset
        u8 randOnGround;         // 0/1: constrain random reset position to floor/platforms
        u8 randomizeDamage;      // 0/1: randomize CPU damage (0-99%) on reset
        u8 recoveryTrainer;      // 0/1: loop offstage recovery spawns with PPlusAI
        s8 comboTimerAdjustment; // -60..60 frame offset on hitstun reset timer
        u16 fixedDamage;         // 0..999 fixed damage applied on reset
        u8 enabledMeters;        // 7-bit ComboMeterBit mask
        u8 reserved[5];
    };

    // Fighter Mechanics (extra/fighter_mechanics.*): per-fighter charge,
    // resource, float, and RNG overrides. 0 is Default (untouched) everywhere.
    enum MechToggle { kMechDefault = 0, kMechAlways, kMechToggleCount };
    enum Charge2 { kCharge2Default = 0, kCharge2Empty, kCharge2Full, kCharge2Count };
    enum Charge3 { kCharge3Default = 0, kCharge3Empty, kCharge3Half, kCharge3Full, kCharge3Count };
    enum GnwBucket { kBucketDefault = 0, kBucketEmpty, kBucket1, kBucket2, kBucketFull, kGnwBucketCount };
    enum SamusBeam { kBeamDefault = 0, kBeamPlasma, kBeamIce, kSamusBeamCount };
    enum LucarioAura { kAuraDefault = 0, kAura0, kAura1, kAura2, kLucarioAuraCount };
    enum RobBeam { kRobBeamDefault = 0, kRobBeamUncharged, kRobBeamNormal, kRobBeamSuper, kRobBeamCount };
    enum LucasOffense { kOffenseDefault = 0, kOffenseOff, kOffenseCharged, kLucasOffenseCount };
    enum SheikNeedles { kNeedlesDefault = 0, kNeedles0, kNeedles6, kSheikNeedlesCount };
    enum DededeWaddle { kWaddleDefault = 0, kWaddleDee, kWaddleDoo, kWaddleGordo, kDededeWaddleCount };
    enum NanaThrow {
        kNanaThrowDefault = 0,
        kNanaThrowForward,
        kNanaThrowBack,
        kNanaThrowUp,
        kNanaThrowDown,
        kNanaThrowCount
    };

    struct FighterMechanicsSettings {
        u8 peachFloat;       // MechToggle (Default / Always)
        u8 gnwBucket;        // GnwBucket (Default / Empty / 1 / 2 / Full)
        u8 samusCharge;      // Charge3 (Default / Empty / Half / Full)
        u8 samusBeam;        // SamusBeam (Default / Plasma / Ice)
        u8 samusAirBombs;    // MechToggle (Default / Unlimited)
        u8 mewtwoShadowBall; // Charge3 (Default / Empty / Half / Full)
        u8 mewtwoHover;      // MechToggle (Default / Unlimited)
        u8 lucarioAura;      // LucarioAura (Default / 0 / 1 / 2)
        u8 lucarioSphere;    // Charge2 (Default / Empty / Full)
        u8 warioWaft;        // Charge3 (Default / Empty / Half / Full)
        u8 dkPunch;          // Charge2 (Default / Empty / Full)
        u8 ivysaurSolarbeam; // Charge3 (Default / Empty / Half / Full)
        u8 robAirBoosts;     // MechToggle (Default / Unlimited)
        u8 robGyro;          // Charge2 (Default / Empty / Full)
        u8 robBeam;          // RobBeam (Default / Uncharged / Normal / Super)
        u8 lucasOffenseUp;   // LucasOffense (Default / Off / Charged)
        u8 squirtleWaterGun; // Charge2 (Default / Empty / Full)
        u8 sheikNeedles;     // SheikNeedles (Default / 0 / 6)
        u8 dededeWaddle;     // DededeWaddle (Default / Waddle Dee / Waddle Doo / Gordo)
        u8 snakeTranq;       // MechToggle (Default / Unlimited)
        u8 snakeCypher;      // MechToggle (Default / Unlimited)
        u8 ganonFloat;       // MechToggle (Default / Unlimited)
        u8 nanaThrow;        // NanaThrow (Default / F-Throw / B-Throw / U-Throw / D-Throw)
        u8 reserved[9];
    };

    struct Settings {
        u32 magic;   // kSettingsMagic
        u16 version; // kSettingsVersion
        u16 size;    // sizeof(Settings)
        GeneralSettings general;
        ControlSettings controls;
        DisplaySettings display;
        InfoSettings info[kSideCount];
        OsdSettings osd;
        CpuSettings cpu;
        RecordingSettings recording;
        OverlaySettings overlay[kSideCount];
        TechInvisSettings techInvis;
        CharacterRngSettings characterRng;
        cpu::CpuTables cpuTables; // Advanced Counter pages
        StageOptionsSettings stageOptions;
        ComboTrainerSettings comboTrainer;
        FighterMechanicsSettings fighterMechanics;
    };
    TM_STATIC_ASSERT(sizeof(CharacterRngSettings) == 4, tm_character_rng_settings_size);
    TM_STATIC_ASSERT(sizeof(StageOptionsSettings) == 16, tm_stage_options_settings_size);
    TM_STATIC_ASSERT(sizeof(ComboTrainerSettings) == 16, tm_combo_trainer_settings_size);
    TM_STATIC_ASSERT(sizeof(FighterMechanicsSettings) == 32, tm_fighter_mechanics_settings_size);

    TM_STATIC_ASSERT(sizeof(GeneralSettings) == 16, tm_general_settings_size);
    TM_STATIC_ASSERT(sizeof(ControlSettings) == 16, tm_control_settings_size);
    TM_STATIC_ASSERT(sizeof(DisplaySettings) == 16, tm_display_settings_size);
    TM_STATIC_ASSERT(sizeof(InfoSettings) == 16, tm_info_settings_size);
    TM_STATIC_ASSERT(sizeof(OsdSettings) == 32, tm_osd_settings_size);
    TM_STATIC_ASSERT(sizeof(CpuSettings) == 64, tm_cpu_settings_size);
    TM_STATIC_ASSERT(sizeof(RecordingSettings) == 64, tm_recording_settings_size);
    TM_STATIC_ASSERT(sizeof(OverlaySettings) == 20, tm_overlay_settings_size);
    TM_STATIC_ASSERT(sizeof(Settings) <= block::kSettingsEnd, tm_settings_in_their_range_of_the_block);
    TM_STATIC_ASSERT(offsetof(Settings, general) == 8, tm_settings_general_at_8);
    TM_STATIC_ASSERT(offsetof(Settings, controls) == 24, tm_settings_controls_at_24);
    TM_STATIC_ASSERT(offsetof(Settings, display) == 40, tm_settings_display_at_40);
    TM_STATIC_ASSERT(offsetof(Settings, display.heatmap) == 47, tm_settings_display_heatmap_at_47);
    TM_STATIC_ASSERT(offsetof(Settings, display.heatmapColorFrames) == 50, tm_settings_display_heatmap_color_at_50);
    TM_STATIC_ASSERT(offsetof(Settings, display.grabVisualizer) == 51, tm_settings_display_grab_viz_at_51);
    TM_STATIC_ASSERT(offsetof(Settings, info) == 56, tm_settings_info_at_56);
    TM_STATIC_ASSERT(offsetof(Settings, osd) == 88, tm_settings_osd_at_88);
    TM_STATIC_ASSERT(offsetof(Settings, osd.bgColor) == 110, tm_settings_osd_bg_color_at_110);
    TM_STATIC_ASSERT(offsetof(Settings, osd.bgAlpha) == 111, tm_settings_osd_bg_alpha_at_111);
    TM_STATIC_ASSERT(offsetof(Settings, general.playerInfiniteShields) == 20, tm_settings_player_infinite_shields_at_20);
    TM_STATIC_ASSERT(offsetof(Settings, controls.dpadLeft) == 27, tm_settings_dpad_left_at_27);
    TM_STATIC_ASSERT(offsetof(Settings, controls.dpadRight) == 28, tm_settings_dpad_right_at_28);
    TM_STATIC_ASSERT(offsetof(Settings, controls.decrementButton) == 29, tm_settings_decrement_at_29);
    TM_STATIC_ASSERT(offsetof(Settings, controls.dpadDown) == 31, tm_settings_dpad_down_at_31);
    TM_STATIC_ASSERT(offsetof(Settings, controls.allowCodeMenu) == 32, tm_settings_allow_code_menu_at_32);
    TM_STATIC_ASSERT(offsetof(Settings, controls.menuStyle) == 33, tm_settings_menu_style_at_33);
    TM_STATIC_ASSERT(offsetof(Settings, cpu) == 120, tm_settings_cpu_at_120);
    TM_STATIC_ASSERT(offsetof(Settings, recording.randomPercent) == 184, tm_settings_random_percent_at_184);
    TM_STATIC_ASSERT(offsetof(Settings, recording.hmnMode) == 186, tm_settings_recording_modes_at_186);
    TM_STATIC_ASSERT(offsetof(Settings, recording.manageCopyTo) == 210, tm_settings_recording_last_at_210);
    TM_STATIC_ASSERT(offsetof(Settings, recording.startOnFirstInput) == 211, tm_settings_recording_start_on_input_at_211);
    TM_STATIC_ASSERT(offsetof(Settings, overlay) == 248, tm_settings_overlay_at_248);
    TM_STATIC_ASSERT(offsetof(Settings, techInvis) == 288, tm_settings_tech_invis_at_288);
    TM_STATIC_ASSERT(offsetof(Settings, characterRng) == 292, tm_settings_character_rng_at_292);
    TM_STATIC_ASSERT(offsetof(Settings, cpuTables) == 296, tm_settings_cpu_tables_at_296);
    TM_STATIC_ASSERT(offsetof(Settings, stageOptions) == 372, tm_settings_stage_options_at_372);
    TM_STATIC_ASSERT(offsetof(Settings, comboTrainer) == 388, tm_settings_combo_trainer_at_388);
    TM_STATIC_ASSERT(offsetof(Settings, fighterMechanics) == 404, tm_settings_fighter_mechanics_at_404);
    TM_STATIC_ASSERT(offsetof(Settings, fighterMechanics.nanaThrow) == 426, tm_settings_nana_throw_at_426);
    TM_STATIC_ASSERT(sizeof(Settings) == 436, tm_settings_size_unchanged);

    // Lab Menu Button's default: Start in the match opens the menu (0 keeps it
    // inside Training's pause, where Z opens it).
    enum { kDefaultStartOpensMenu = 1 };

    // The settings in the stub's block, reset to the defaults if the block
    // does not hold this layout. Valid while attached.
    Settings& settings();

    // Checks the block's header, resets it if needed, and clamps every
    // value to its option's range. Returns true if the block was reset.
    bool loadSettings(u8* block);

    // Brings fields not bound directly to a visible lab-menu row into their
    // valid ranges (called alongside ui::clampSettings after reading a file).
    void sanitizeSettings(Settings& s);

    void resetSettings();

    // Pure, UI-agnostic partial resets (no SD writes or UI callbacks).
    // Resets every valid storage::AreaId in `areaMask` and returns the
    // bitmask of areas reset (0 if none were valid).
    u32 resetSettingsAreas(u32 areaMask);
    bool resetSettingsArea(int areaId);
    // Resets all stored non-scratch options on `pageId`, or one option at
    // `(pageId, row)`, returning false if out of range or not a stored option.
    bool resetSettingsPage(int pageId);
    bool resetSettingsOption(int pageId, int row);

    // Per-side views of the percent options (human: General, CPU: CPU Options).
    u16 percentSetting(int side);
    bool lockSetting(int side);
}
}
