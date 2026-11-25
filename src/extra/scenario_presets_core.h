#pragma once

#include <types.h>

#include "extra/combo_trainer_core.h"
#include "extra/stage_geom_math.h"
#include "main/cpu/cpu_settings.h"
#include "main/settings.h"

// Pure host-testable Unified P+ Lab Scenario Preset engine (zero Brawl
// headers). Supports Off + 10 P+ Lab presets with settings snapshot/restore,
// dynamic stage-geometry placement, 1-frame noclip restore, per-preset
// completion/auto-reset state machines, multi-hit SDI telemetry, and optional
// CPU pressure input scripting.
namespace tm {
namespace extra {
    enum ScenarioId {
        kScenarioOff = 0,
        kScenarioComboLab = 1,
        kScenarioTechchase = 2,
        kScenarioEdgeguard = 3,
        kScenarioShieldPressure = 4,
        kScenarioOosReversal = 5,
        kScenarioShieldDrop = 6,
        kScenarioLedgetech = 7,
        kScenarioSlideOff = 8,
        kScenarioGrabEscape = 9,
        kScenarioMultiHitSdi = 10,
        kScenarioCount = 11
    };

    enum ScenarioSide {
        kScenarioSideRight = 0,
        kScenarioSideLeft = 1,
        kScenarioSideCount = 2
    };

    enum MultiHitMove {
        kMultiHitUpAir = 0,
        kMultiHitSideB = 1,
        kMultiHitRapidJab = 2,
        kMultiHitDownAir = 3,
        kMultiHitMoveCount = 4
    };

    // Counter action indices from src/main/cpu/actions.cpp and
    // src/main/ui/pages/cpu_options.cpp.
    enum {
        kCounterGroundJab = 13,
        kCounterAirJumpNeutral = 4,
        kCounterShieldGrab = 1,
        kCounterShieldUpB = 7
    };

    struct ScenarioConfig {
        u8 preset;       // ScenarioId
        u8 autoReset;    // 0/1
        u8 side;         // ScenarioSide
        u8 multiHitMove; // MultiHitMove
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
    };

    struct ScenarioState {
        u8 activePreset;
        u8 lastAutoReset;
        u8 lastSide;
        u8 lastMultiHitMove;
        bool snapshotActive;
        cpu::CpuOptions snapshotCpu;
        main::ComboTrainerSettings snapshotCombo;
        u16 snapshotPlayerPercent;
        u8 snapshotLockPlayerPercent;
        bool snapshotPlayerValid;

        bool pendingReset;
        bool hasPlatform;
        bool restoreCorrect[2];
        u8 restoreCorrectKind[2];
        u8 lastCorrectWritten[2];

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
        bool reversalEngaged;
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
        u8 multiHitMove;
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

    void scenarioStateInit(ScenarioState& st);
    void scenarioTransientReset(ScenarioState& st);
    void scenarioCpuStateReset(ScenarioCpuState& st);

    // Transitions settings for `rawCfg` (snapshots on Off -> Preset, restores
    // from snapshot on Preset A -> Preset B or Preset -> Off).
    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg = NULL);

    // Restores pre-preset snapshot on detach (no-op if snapshotActive is false).
    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* generalCfg = NULL);

    // Clears snapshot and active preset after a full Lab "Reset Settings" so
    // the freshly defaulted settings are never overwritten with stale snapshots.
    void discardScenarioSnapshot(ScenarioState& st);

    // Projects the saved-settings view so temporary preset overrides never leak
    // to the SD card while a preset is active.
    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg = NULL);

    // Requests an immediate manual reset on the next live frame (no-op if Off).
    void requestScenarioManualReset(ScenarioState& st);

    // Computes live stage-geometry spawns for any preset.
    void computeScenarioSpawns(u8 preset, u8 side, u8 multiHitMove, const StageGeometry& geom, ScenarioSpawns& out);

    // Steps one live frame of the active scenario state machine.
    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* generalCfg = NULL);

    // Steps optional CPU pressure input scripting for active scenarios.
    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);
}
}
