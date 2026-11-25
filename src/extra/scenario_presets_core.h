#pragma once

#include <types.h>

#include "extra/combo_trainer_core.h"
#include "extra/stage_geom_math.h"
#include "main/cpu/cpu_settings.h"
#include "main/settings.h"

// Pure host-testable Unified P+ Lab Scenario Preset engine (zero Brawl
// headers). Supports Off + 9 P+ Lab presets with settings snapshot/restore,
// dynamic stage-geometry placement, 1-frame noclip restore, per-preset
// completion/auto-reset state machines, multi-hit SDI telemetry, 44-fighter
// P+ 3.2.1 auto multi-hit move selection, and CPU pressure input scripting.
namespace tm {
namespace extra {
    enum ScenarioId {
        kScenarioOff = 0,
        kScenarioComboLab = 1,
        kScenarioTechchase = 2,
        kScenarioEdgeguard = 3,
        kScenarioShieldPressure = 4, // Unified "Shield Pressure / OoS"
        kScenarioShieldDrop = 5,
        kScenarioLedgetech = 6,
        kScenarioSlideOff = 7,
        kScenarioGrabEscape = 8,
        kScenarioMultiHitSdi = 9,
        kScenarioCount = 10
    };

    enum ScenarioSide {
        kScenarioSideRight = 0,
        kScenarioSideLeft = 1,
        kScenarioSideRandom = 2,
        kScenarioSideCount = 3
    };

    enum ScenarioOosAction {
        kOosGrab = 0,
        kOosUpB = 1,
        kOosSpotdodge = 2,
        kOosRollAway = 3,
        kOosShNair = 4,
        kOosWavedash = 5,
        kOosRandom = 6,
        kOosCount = 7
    };

    enum LedgetechSpawn {
        kLtSpawnCloseLow = 0,
        kLtSpawnDeepLow = 1,
        kLtSpawnFarOut = 2,
        kLtSpawnHigh = 3,
        kLtSpawnRandom = 4,
        kLtSpawnCount = 5
    };

    enum LedgetechDj {
        kLtDjAvailable = 0,
        kLtDjSpent = 1,
        kLtDjCount = 2
    };

    enum LedgetechAttack {
        kLtAtkDtilt = 0,
        kLtAtkFtilt = 1,
        kLtAtkFsmash = 2,
        kLtAtkDsmash = 3,
        kLtAtkDair = 4,
        kLtAtkNeutralB = 5,
        kLtAtkSideB = 6,
        kLtAtkRandom = 7,
        kLtAtkCount = 8
    };

    enum ShieldDropPlat {
        kSdPlatLeft = 0,
        kSdPlatRight = 1,
        kSdPlatTop = 2,
        kSdPlatRandom = 3,
        kSdPlatCount = 4
    };

    enum ShieldDropSpacing {
        kSdSpaceRandom = 0,
        kSdSpaceClose = 1,
        kSdSpaceUnder = 2,
        kSdSpaceFar = 3,
        kSdSpaceCount = 4
    };

    enum ShieldDropFacing {
        kSdFaceToward = 0,
        kSdFaceAway = 1,
        kSdFaceRandom = 2,
        kSdFaceCount = 3
    };

    enum MultiHitMove {
        kMultiHitAuto = 0,
        kMultiHitNair = 1,
        kMultiHitFair = 2,
        kMultiHitUpAir = 3,
        kMultiHitDownAir = 4,
        kMultiHitRapidJab = 5,
        kMultiHitUpB = 6,
        kMultiHitSideB = 7,
        kMultiHitDownB = 8,
        kMultiHitMoveCount = 9
    };

    // Counter action indices from src/main/cpu/actions.cpp and
    // src/main/ui/pages/cpu_options.cpp.
    enum {
        kCounterGroundJab = 13,
        kCounterAirJumpNeutral = 4,
        kCounterShieldGrab = 1,
        kCounterShieldSpotdodge = 4,
        kCounterShieldRollAway = 5,
        kCounterShieldUpB = 7,
        kCounterShieldShNair = 15,
        kCounterShieldWavedashAway = 20,
        kCounterShieldRandom = 33
    };

    struct ScenarioConfig {
        u8 preset;            // ScenarioId
        u8 autoReset;         // 0/1
        u8 side;              // ScenarioSide
        u8 multiHitMove;      // MultiHitMove
        u8 oosAction;         // ScenarioOosAction
        u8 ledgetechSpawn;    // LedgetechSpawn
        u8 ledgetechDj;       // LedgetechDj
        u8 ledgetechAttack;   // LedgetechAttack
        u8 shieldDropPlat;    // ShieldDropPlat
        u8 shieldDropSpacing; // ShieldDropSpacing
        u8 shieldDropFacing;  // ShieldDropFacing
    };

    struct ScenarioSpawns {
        float hmnX;
        float hmnY;
        float hmnLr;
        bool hmnFalling;
        float cpuX;
        float cpuY;
        float cpuLr;
        bool cpuFalling;
        bool hasPlatform;
        u8 resolvedPlatformSlot;
        bool valid;
    };

    struct ScenarioStepInput {
        // Human fighter live state
        int hmnStatus;
        int hmnHitlag;
        int hmnHitstun;
        int hmnShieldStun;
        float hmnX;
        float hmnY;
        bool hmnAir;
        bool hmnOutOfPlay;
        bool hmnActionable;
        bool hmnEnteredPass; // true if status == 0x72 (Pass) or tracker saw 0x72 intra-frame
        bool hmnDamageHitlag;

        // CPU fighter live state
        int cpuStatus;
        int cpuHitlag;
        int cpuHitstun;
        int cpuShieldStun;
        int cpuFtKind; // P+ ftKind for Multi-Hit SDI Auto resolution (-1 if unknown)
        float cpuX;
        float cpuY;
        bool cpuAir;
        bool cpuOutOfPlay;
        bool cpuActionable;

        // Stage geometry for current frame
        StageGeometry geom;
    };

    struct ScenarioStepAction {
        bool warpHmn;
        bool warpCpu;
        bool setNoclip[2];      // [0] HMN, [1] CPU
        bool restoreCorrect[2]; // [0] HMN, [1] CPU
        u8 restoreCorrectKind[2];
        ScenarioSpawns spawns;
        bool resetComboTrainer;
        bool applyHmnPercent;
        bool applyCpuPercent;
        bool refreshHmnAir;
        bool hmnSpendDj;
        bool refreshCpuAir;
    };

    struct ScenarioState {
        u8 activePreset;
        u8 lastAutoReset;
        u8 lastSide;
        u8 lastMultiHitMove;
        u8 lastOosAction;
        u8 lastLedgetechSpawn;
        u8 lastLedgetechDj;
        u8 lastLedgetechAttack;
        u8 lastShieldDropPlat;
        u8 lastShieldDropSpacing;
        u8 lastShieldDropFacing;

        bool snapshotActive;
        cpu::CpuOptions snapshotCpu;
        main::ComboTrainerSettings snapshotCombo;
        u16 snapshotPlayerPercent;
        u8 snapshotLockPlayerPercent;
        u8 snapshotPlayerInfShields;
        bool snapshotPlayerValid;
        u16 snapshotCpuPercent;
        u8 snapshotLockCpuPercent;
        bool snapshotCpuValid;

        bool pendingReset;
        bool hasPlatform;
        bool restoreCorrect[2];
        u8 restoreCorrectKind[2];
        u8 lastCorrectWritten[2];

        u32 rng;
        u8 resolvedSide;
        u8 resolvedMultiHit;
        u8 resolvedLedgetechAttack;
        u8 resolvedPlatformSlot;

        float hmnSpawnX;
        float hmnSpawnY;
        float cpuSpawnX;
        float cpuSpawnY;

        u32 resetCount;
        u32 completionCount;
        u32 shieldHitCount;
        u32 grabEscapeFrames;

        // Multi-hit SDI live telemetry (latched across auto-reset until next hit).
        u32 sdiHitCount;
        u32 sdiHitlagFrames;
        float sdiDispX;
        float sdiDispY;
        bool sdiEscaped;
        bool sdiSequenceActive;
        bool sdiLatchedReset;

        // Live event flags (cleared on preset change or manual reset).
        bool sawShieldDrop;
        bool sawWallTech;
        bool sawSlideOff;
        bool sawGrabEscape;

        // Internal per-preset tracking state.
        bool havePrev;
        int prevHmnStatus;
        int prevHmnHitlag;
        bool prevHmnAir;
        float prevHmnX;
        float prevHmnY;
        int prevCpuStatus;
        int prevCpuHitlag;
        bool prevCpuGuardDamage;

        u8 hmnGuardWindow;
        bool techEngaged;
        bool shieldEngaged;
        bool oosCountered;
        bool shieldDropDone;
        bool ledgetechDone;
        bool slideOffArmed;
        bool slideOffDone;
        bool grabActive;
        bool grabDone;
        u32 activeGrabFrames;
        u16 graceFrames;
        u16 sinceResetFrames;
    };

    // Pure-logic CPU pressure scripting for scenarios that need active attacks
    // (P+ Multi-Hit SDI, Ledgetech, Slide-Off). Returns active = false for Off
    // and all other presets so standard cpu/options.cpp runs untouched.
    struct ScenarioCpuInput {
        u8 preset;
        u8 multiHitMove;    // MultiHitMove (Auto resolved via cpuFtKind)
        u8 ledgetechAttack; // Resolved LedgetechAttack (0..6)
        int cpuFtKind;
        int cpuStatus;
        bool cpuAir;
        bool cpuCanAct;
        bool cpuTakeoffNext;
        float cpuX;
        float cpuY;
        float cpuVx;
        float cpuVy;
        float cpuLr;
        float hmnX;
        float hmnY;
        bool hmnAir;
        int hmnHitstun;
        bool hmnOutOfPlay;
        bool cpuOutOfPlay;
        u32 lastButtons;
    };

    struct ScenarioCpuState {
        u8 phase;
        u8 timer;
        u8 cooldown;
        u8 lastMove;
    };

    struct ScenarioCpuOutput {
        bool active;
        u32 buttons;
        float stickX;
        float stickY;
    };

    ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw);
    u8 resolveOosCounterShield(u8 oosAction);
    // P+ 3.2.1 verified 44-fighter table mapping ftKind -> best multihit move.
    u8 bestMultiHitMoveForFighter(int ftKind);
    u8 resolveMultiHitMove(u8 multiHitMove, int cpuFtKind);

    void scenarioStateInit(ScenarioState& st);
    void scenarioTransientReset(ScenarioState& st);
    void scenarioCpuStateReset(ScenarioCpuState& st);

    // Transitions settings for `rawCfg` (snapshots on Off -> Preset, restores
    // from snapshot on Preset A -> Preset B or Preset -> Off).
    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg = NULL,
                             main::CpuSettings* cpuSettings = NULL);

    // Re-applies active preset overrides cleanly (e.g. after returning from
    // Brawl Training pause) without losing the original pre-preset snapshot.
    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg,
                                        cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                        main::GeneralSettings* generalCfg = NULL,
                                        main::CpuSettings* cpuSettings = NULL);

    // Restores pre-preset snapshot on detach (no-op if snapshotActive is false).
    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* generalCfg = NULL, main::CpuSettings* cpuSettings = NULL);

    // Clears snapshot and active preset after a full Lab "Reset Settings" so
    // the freshly defaulted settings are never overwritten with stale snapshots.
    void discardScenarioSnapshot(ScenarioState& st);

    // Projects the saved-settings view so temporary preset overrides never leak
    // to the SD card while a preset is active.
    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg = NULL,
                                  main::CpuSettings* cpuSettings = NULL);

    // Requests an immediate manual reset on the next live frame (no-op if Off).
    void requestScenarioManualReset(ScenarioState& st);

    // Computes live stage-geometry spawns for any preset with full option support.
    void computeScenarioSpawnsEx(const ScenarioConfig& rawCfg, int cpuFtKind, u32& rng, const StageGeometry& geom,
                                 ScenarioSpawns& out);

    // Convenience overload preserving simple 5-arg call sites.
    void computeScenarioSpawns(u8 preset, u8 side, u8 multiHitMove, const StageGeometry& geom, ScenarioSpawns& out);

    // Steps one live frame of the active scenario state machine.
    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* generalCfg = NULL, main::CpuSettings* cpuSettings = NULL);

    // Steps optional CPU pressure input scripting for active scenarios.
    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);

    // Steps default repeating CPU jab/poke attacker for Boot > Skill Drills >
    // P+ Powershield when no Scenario Preset or Recording overrides the CPU.
    void stepPowershieldDrillCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);
}
}
