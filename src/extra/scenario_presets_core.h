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
        kOosCustomPool = 7,
        kOosCount = 8
    };

    enum TechchaseSetup {
        kTechSetupTumbleDrop = 0,
        kTechSetupFreeHit = 1,
        kTechSetupCount = 2
    };

    enum TechchaseEscape {
        kTechEscIntangFlash = 0,
        kTechEscSpotdodge = 1,
        kTechEscJab = 2,
        kTechEscJump = 3,
        kTechEscShield = 4,
        kTechEscRoll = 5,
        kTechEscNone = 6,
        kTechEscCount = 7
    };

    enum TechchaseTdi {
        kTechTdiRandom = 0,
        kTechTdiNone = 1,
        kTechTdiAway = 2,
        kTechTdiToward = 3,
        kTechTdiSurvival = 4,
        kTechTdiCount = 5
    };

    enum TechchaseResult {
        kTechResNone = 0,
        kTechResCover = 1,
        kTechResEarly = 2,
        kTechResLate = 3
    };

    enum EdgeguardSetup {
        kEgSetupLaunch = 0,
        kEgSetupSpawn = 1,
        kEgSetupCount = 2
    };

    enum EdgeguardStrength {
        kEgStrWeak = 0,
        kEgStrNormal = 1,
        kEgStrStrong = 2,
        kEgStrRandom = 3,
        kEgStrCount = 4
    };

    enum EdgeguardDist {
        kEgDistMid = 0,
        kEgDistDeep = 1,
        kEgDistHigh = 2,
        kEgDistRandom = 3,
        kEgDistCount = 4
    };

    enum EdgeguardDj {
        kEgDjAvail = 0,
        kEgDjSpent = 1,
        kEgDjRandom = 2,
        kEgDjCount = 3
    };

    enum LedgetechStart {
        kLtStartRevival = 0,
        kLtStartInstant = 1,
        kLtStartCount = 2
    };

    enum LedgetechTiming {
        kLtTimeHoldActive = 0,
        kLtTimeTimed = 1,
        kLtTimeCount = 2
    };

    enum ShieldRole {
        kShieldRoleOffense = 0,
        kShieldRoleDefense = 1,
        kShieldRoleCount = 2
    };

    enum ShieldDefAttack {
        kShieldDefAerial = 0,
        kShieldDefTilt = 1,
        kShieldDefSmash = 2,
        kShieldDefDash = 3,
        kShieldDefGetup = 4,
        kShieldDefRandom = 5,
        kShieldDefCount = 6
    };

    enum ShieldDropAttack {
        kSdAtkShAerial = 0,
        kSdAtkTilt = 1,
        kSdAtkDsmash = 2,
        kSdAtkOff = 3,
        kSdAtkCount = 4
    };

    enum SlideOffAttack {
        kSlideAtkUnderUair = 0,
        kSlideAtkUnderFair = 1,
        kSlideAtkUnderNair = 2,
        kSlideAtkOnPlatFtilt = 3,
        kSlideAtkCount = 4
    };

    enum {
        kTechPosMax = 4,
        kTechBestSlot = 16 // unused slot in drills::Session::best[32]
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

    enum GrabAction {
        kGrabActHold = 0,
        kGrabActRandThrow = 1,
        kGrabActFThrow = 2,
        kGrabActBThrow = 3,
        kGrabActUThrow = 4,
        kGrabActDThrow = 5,
        kGrabActStand = 6,
        kGrabActCount = 7
    };

    enum GrabPctMode {
        kGrabPctRandom = 0,
        kGrabPctCurrent = 1,
        kGrabPctCount = 2
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
        kCounterGroundSpotdodge = 1,
        kCounterGroundShield = 2,
        kCounterGroundJab = 13,
        kCounterGroundShortHop = 18,
        kCounterGroundRollRandom = 33,
        kCounterAirJumpNeutral = 4,
        kCounterShieldGrab = 1,
        kCounterShieldSpotdodge = 4,
        kCounterShieldRollAway = 5,
        kCounterShieldUpB = 7,
        kCounterShieldShNair = 15,
        kCounterShieldWavedashAway = 20,
        kCounterShieldRandom = 33,
        kCounterShieldCustomPool = 49
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
        u8 grabAction;        // GrabAction
        u8 grabPctMode;       // GrabPctMode
        u8 techSetup;         // TechchaseSetup
        u8 techPosition;      // 0 = Ground, 1..4 = Platform 1..4
        u8 techEscape;        // TechchaseEscape
        u8 techInvisMode;     // main::TechInvisibility
        u8 techInvisDelay;    // 0..15f
        u8 techSound;         // 0/1
        u8 techPreDrift;      // main::PreTechDrift
        u8 techTdi;           // TechchaseTdi
        u8 techShowReaction;  // 0/1
        u8 edgeguardSetup;    // EdgeguardSetup
        u8 edgeguardStrength; // EdgeguardStrength
        u8 edgeguardDist;     // EdgeguardDist
        u8 edgeguardDj;       // EdgeguardDj
        u8 edgeguardMixup;    // main::RecoveryMixup
        u8 ledgetechStart;    // LedgetechStart
        u8 ledgetechTiming;   // LedgetechTiming
        u8 shieldRole;        // ShieldRole
        u8 shieldDefAttack;   // ShieldDefAttack
        u8 shieldDropAttack;  // ShieldDropAttack
        u8 slideOffAttack;    // SlideOffAttack
        s8 spacingOffset;     // live D-Pad Left/Right spacing nudge (-8..+12)
    };

    struct ScenarioSpawns {
        float hmnX;
        float hmnY;
        float hmnLr;
        bool hmnFalling;
        bool hmnRevivalPlat;
        float cpuX;
        float cpuY;
        float cpuLr;
        bool cpuFalling;
        bool cpuTumbleDrop;
        bool cpuDownWait;
        bool hasPlatform;
        u8 resolvedPlatformSlot;
        u8 resolvedTechPos;
        bool valid;
    };

    struct EdgeguardLaunchParams {
        float startX;
        float startY;
        float kbX;
        float kbY;
        float speed;
        float angleDeg;
        int hitstun;
        int hitlag;
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
        int cpuFtKind;   // P+ ftKind for Multi-Hit SDI Auto resolution (-1 if unknown)
        int cpuTechDist; // fighter's P+ distinguishable tech frame (-1 if unknown)
        float cpuX;
        float cpuY;
        bool cpuAir;
        bool cpuOutOfPlay;
        bool cpuActionable;
        bool cpuIntangible;

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
        bool useExplicitHmnDmg;
        float hmnDmg;
        bool useExplicitCpuDmg;
        float cpuDmg;
        bool refreshHmnAir;
        bool hmnSpendDj;
        bool refreshCpuAir;
        bool techFlashCpu;       // trigger Intangible Flash cue on Techchase escape frame
        bool edgeguardLaunchCpu; // apply DamageFly Knockback Launch on Edgeguard warp
        bool cpuDownWait;        // place CPU in DownWait for Getup Attack OoS Defense
    };

    struct ScenarioState {
        u8 activePreset;
        ScenarioConfig lastCfg;

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
        u8 snapshotGrabBreakoutOsd;
        bool snapshotOsdValid;
        main::TechInvisSettings snapshotTechInvis;
        bool snapshotTechInvisValid;

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
        u8 resolvedTechPos;
        u8 resolvedShieldDefAtk;

        float hmnSpawnX;
        float hmnSpawnY;
        float cpuSpawnX;
        float cpuSpawnY;

        u32 resetCount;
        u32 completionCount;
        u32 shieldHitCount;
        u32 grabEscapeFrames;

        // Techchase timing, reaction, and streak telemetry.
        u8 techResult;          // TechchaseResult
        u8 techDeltaFrames;     // frames Early or Late
        s16 techReactionFrames; // frames past fighter's kDistinguishable (-1 if n/a)
        u16 techStreak;
        u16 techBest;
        u16 techFrames;
        u16 techActFrames;
        u8 techEarlyFrames;
        bool techCoveredThisRep;
        bool techLateRecorded;

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
        bool shieldDefSwung;
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
    // (P+ Multi-Hit SDI, Ledgetech, Slide-Off, Shield Drop, Shield Pressure Defense, Grab).
    struct ScenarioCpuInput {
        u8 preset;
        u8 multiHitMove;     // MultiHitMove (Auto resolved via cpuFtKind)
        u8 ledgetechAttack;  // Resolved LedgetechAttack (0..6)
        u8 ledgetechTiming;  // LedgetechTiming (Hold Active / Timed Swing)
        u8 shieldRole;       // ShieldRole (Offense / Defense)
        u8 shieldDefAttack;  // Resolved ShieldDefAttack (0..4)
        u8 shieldDropAttack; // ShieldDropAttack (0..3)
        u8 slideOffAttack;   // SlideOffAttack (0..3)
        u8 grabAction;       // GrabAction
        bool hasPlatform;    // true if active scenario spawned on a platform
        u32 rng;             // Per-frame stirred RNG for delays & random picks
        int cpuFtKind;
        int cpuStatus;
        int hmnStatus;
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
        u8 delay;
        u8 resolvedThrow;
    };

    struct ScenarioCpuOutput {
        bool active;
        u32 buttons;
        float stickX;
        float stickY;
    };

    void sanitizeScenarioConfigInto(const ScenarioConfig& raw, ScenarioConfig& out);
    ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw);
    u8 resolveOosCounterShield(u8 oosAction);
    u8 resolveTechEscapeCounter(u8 techEscape);
    u8 resolveTechTdi(u8 techTdi);
    float ledgetechCpuInwardOffset(u8 atk);
    u8 stepTechPosition(u8 currentPos, int platformCount, int delta);
    void computeEdgeguardLaunch(u8 strengthMode, const LedgeInfo& ledge, float outward, u32& rng,
                                EdgeguardLaunchParams& out);

    // True when Ledgetech Hold Active should freeze the CPU's attack animation
    // on its active hitbox frame (motionRate = 0).
    inline bool shouldHoldLedgetechActive(u8 timingMode, bool cpuHasActiveHitbox, bool hmnHitOrGrabbed,
                                          bool hmnOnLedge, bool hmnOutOfPlay)
    {
        return timingMode == kLtTimeHoldActive && cpuHasActiveHitbox && !hmnHitOrGrabbed && !hmnOnLedge
               && !hmnOutOfPlay;
    }

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
                             main::CpuSettings* cpuSettings = NULL, main::OsdSettings* osdCfg = NULL,
                             main::TechInvisSettings* techInvisCfg = NULL);

    // Re-applies active preset overrides cleanly (e.g. after returning from
    // Brawl Training pause) without losing the original pre-preset snapshot.
    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg,
                                        cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                        main::GeneralSettings* generalCfg = NULL,
                                        main::CpuSettings* cpuSettings = NULL, main::OsdSettings* osdCfg = NULL,
                                        main::TechInvisSettings* techInvisCfg = NULL);

    // Restores pre-preset snapshot on detach (no-op if snapshotActive is false).
    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* generalCfg = NULL, main::CpuSettings* cpuSettings = NULL,
                                 main::OsdSettings* osdCfg = NULL, main::TechInvisSettings* techInvisCfg = NULL);

    // Clears snapshot and active preset after a full Lab "Reset Settings" so
    // the freshly defaulted settings are never overwritten with stale snapshots.
    void discardScenarioSnapshot(ScenarioState& st);

    // Projects the saved-settings view so temporary preset overrides never leak
    // to the SD card while a preset is active.
    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg = NULL,
                                  main::CpuSettings* cpuSettings = NULL, main::OsdSettings* osdCfg = NULL,
                                  main::TechInvisSettings* techInvisCfg = NULL);

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
                      main::GeneralSettings* generalCfg = NULL, main::CpuSettings* cpuSettings = NULL,
                      main::OsdSettings* osdCfg = NULL, main::TechInvisSettings* techInvisCfg = NULL);

    // Steps optional CPU pressure input scripting for active scenarios.
    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);

    // Steps default repeating CPU jab/poke attacker for Boot > Skill Drills >
    // P+ Powershield when no Scenario Preset or Recording overrides the CPU.
    void stepPowershieldDrillCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);
}
}
