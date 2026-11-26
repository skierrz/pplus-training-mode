#pragma once

#include <cstdio>
#include <string.h>
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

    enum EdgeguardReset {
        kEgResetStageSafe = 0,
        kEgResetLedgeGrab = 1,
        kEgResetCount = 2
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

    enum SlideOffStart {
        kSlideStartKnockdown = 0,
        kSlideStartTumble = 1,
        kSlideStartStanding = 2,
        kSlideStartCount = 3
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
        u8 slideOffStart;     // SlideOffStart
        u8 edgeguardReset;    // EdgeguardReset
        s8 spacingOffset;     // live D-Pad Left/Right spacing nudge (-8..+12)
    };

    struct ScenarioSpawns {
        float hmnX;
        float hmnY;
        float hmnLr;
        bool hmnFalling;
        bool hmnRevivalPlat;
        bool hmnDownWait;
        bool hmnTumbleDrop;
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
        u16 snapshotCpuPercent;
        u8 snapshotLockCpuPercent;
        u8 snapshotGrabBreakoutOsd;
        main::TechInvisSettings snapshotTechInvis;

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

        // Previous-frame snapshots (preserved across cycle reset until havePrev is set).
        int prevHmnStatus;
        int prevHmnHitlag;
        bool prevHmnAir;
        float prevHmnX;
        float prevHmnY;
        int prevCpuStatus;
        int prevCpuHitlag;
        bool prevCpuGuardDamage;
        bool sdiLatchedReset;

        // Contiguous event telemetry block (cleared together by clearEventTelemetry).
        u32 sdiHitCount;
        u32 sdiHitlagFrames;
        float sdiDispX;
        float sdiDispY;
        bool sdiEscaped;
        bool sawShieldDrop;
        bool sawWallTech;
        bool sawSlideOff;
        bool sawGrabEscape;

        // Contiguous per-cycle tracker block (cleared together by clearCycleTrackers).
        bool havePrev;
        u16 techFrames;
        u16 techActFrames;
        u8 techEarlyFrames;
        bool techWasPassive;
        bool techCoveredThisRep;
        bool techLateRecorded;
        bool sdiSequenceActive;
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
        s8 spacingOffset;    // Live D-Pad Left/Right spacing offset (-8..+12)
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
    inline ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw)
    {
        ScenarioConfig c;
        sanitizeScenarioConfigInto(raw, c);
        return c;
    }
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
    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, main::Settings& s);

    // Re-applies active preset overrides cleanly (e.g. after returning from
    // Brawl Training pause) without losing the original pre-preset snapshot.
    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg, main::Settings& s);

    // Restores pre-preset snapshot on detach (no-op if snapshotActive is false).
    void restoreScenarioSnapshot(ScenarioState& st, main::Settings& s);

    // Clears snapshot and active preset after a full Lab "Reset Settings" so
    // the freshly defaulted settings are never overwritten with stale snapshots.
    void discardScenarioSnapshot(ScenarioState& st);

    // Projects the saved-settings view so temporary preset overrides never leak
    // to the SD card while a preset is active.
    void projectScenarioSavedView(const ScenarioState& st, main::Settings& s);

    // Requests an immediate manual reset on the next live frame (no-op if Off).
    void requestScenarioManualReset(ScenarioState& st);

    // Computes live stage-geometry spawns for any preset with full option support.
    void computeScenarioSpawnsEx(const ScenarioConfig& rawCfg, int cpuFtKind, u32& rng, const StageGeometry& geom,
                                 ScenarioSpawns& out);

    // Convenience overload preserving simple 5-arg call sites.
    inline void computeScenarioSpawns(u8 preset, u8 side, u8 multiHitMove, const StageGeometry& geom,
                                      ScenarioSpawns& out) {
        ScenarioConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.preset = preset;
        cfg.autoReset = 1;
        cfg.side = side;
        cfg.multiHitMove = multiHitMove;
        cfg.shieldDropPlat = side == kScenarioSideLeft ? kSdPlatLeft : kSdPlatRight;
        cfg.shieldDropSpacing = kSdSpaceClose;
        cfg.shieldDropFacing = kSdFaceToward;
        u32 rng = 0x51A7C0DEu;
        computeScenarioSpawnsEx(cfg, -1, rng, geom, out);
    }

    // Steps one live frame of the active scenario state machine.
    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in, main::Settings& s,
                      ScenarioStepAction& act);

    // Steps optional CPU pressure input scripting for active scenarios.
    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out);

    // Pure host-testable Panel B right-margin stat plate formatter for the 7
    // non-Techchase active Lab Scenarios (Edgeguard through Multi-Hit SDI).
    enum {
        kScenarioPlateValBuf = 24
    };

    static const u32 kScenarioPlateWhite = 0xF4F8FCFFu;
    static const u32 kScenarioPlateMuted = 0xC6D4E0FFu;
    static const u32 kScenarioPlateGreen = 0x4CE688FFu;
    static const u32 kScenarioPlateAmber = 0xFFB848FFu;
    static const u32 kScenarioPlateGold = 0xFFD300FFu;

    struct ScenarioHudRow {
        const char* label;
        char value[kScenarioPlateValBuf];
        u32 valueColor;
    };

    struct ScenarioHudPlate {
        ScenarioHudRow rows[3];
        int count;
    };

    inline bool scenarioHasTopBand(u8 preset)
    {
        return preset == kScenarioTechchase;
    }

    inline bool scenarioHasStatPlate(u8 preset)
    {
        return preset >= kScenarioTechchase && preset < kScenarioCount;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline float scenarioSdiDist(float dx, float dy)
    {
        float sq = dx * dx + dy * dy;
        if (sq <= 0.0001f)
        {
            return 0.0f;
        }
        float r = (dx < 0.0f ? -dx : dx) + (dy < 0.0f ? -dy : dy);
        for (int i = 0; i < 4; i++)
        {
            r = 0.5f * (r + sq / r);
        }
        return r;
    }

    inline void scenarioSetRow(ScenarioHudRow& row, const char* label, const char* val, u32 col)
    {
        row.label = label;
        strncpy(row.value, val, sizeof(row.value) - 1);
        row.value[sizeof(row.value) - 1] = '\0';
        row.valueColor = col;
    }

    inline bool formatScenarioStatPlate(const ScenarioState& st, const ScenarioConfig& rawCfg, ScenarioHudPlate& out)
    {
        memset(&out, 0, sizeof(out));
        u8 p = st.activePreset;
        if (p < kScenarioEdgeguard || p >= kScenarioCount)
        {
            return false;
        }
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        out.count = 3;

        // Default Row 2 for 6 of the 7 scenarios: Cleared (completions / resets).
        out.rows[2].label = "Cleared";
        snprintf(out.rows[2].value, sizeof(out.rows[2].value), "%u / %u", static_cast<unsigned>(st.completionCount),
                 static_cast<unsigned>(st.resetCount));
        out.rows[2].valueColor = st.completionCount > 0u ? kScenarioPlateGreen : kScenarioPlateWhite;

        switch (p)
        {
        case kScenarioEdgeguard:
        {
            static const char* const kEgDists[kEgDistCount] = {"Mid", "Deep", "High", "Rand"};
            static const char* const kEgDjs[kEgDjCount] = {"DJ", "NoDJ", "Rnd"};
            if (cfg.edgeguardSetup == kEgSetupLaunch)
            {
                scenarioSetRow(out.rows[0], "Mode", "Launch", kScenarioPlateWhite);
            }
            else
            {
                out.rows[0].label = "Mode";
                snprintf(out.rows[0].value, sizeof(out.rows[0].value), "%s / %s", kEgDists[cfg.edgeguardDist],
                         kEgDjs[cfg.edgeguardDj]);
                out.rows[0].valueColor = kScenarioPlateWhite;
            }
            u8 effSide = st.resetCount > 0u ? st.resolvedSide : cfg.side;
            scenarioSetRow(out.rows[1], "Side",
                           effSide == kScenarioSideLeft ? "Left" : (effSide == kScenarioSideRandom ? "Random" : "Right"),
                           kScenarioPlateWhite);
            break;
        }

        case kScenarioShieldPressure:
        {
            static const char* const kOosNames[kOosCount] = {
                "Grab", "Up B", "Spotdodge", "Roll Away", "SH Nair", "Wavedash", "Random", "Custom"
            };
            static const char* const kDefNames[kShieldDefCount] = {
                "Aerial", "Tilt", "Smash", "Dash Atk", "Getup Atk", "Random"
            };
            bool isDef = cfg.shieldRole == kShieldRoleDefense;
            scenarioSetRow(out.rows[0], "Role", isDef ? "Defense" : "Offense", kScenarioPlateWhite);
            if (isDef)
            {
                u8 atk = (cfg.shieldDefAttack == kShieldDefRandom && st.resetCount > 0u) ? st.resolvedShieldDefAtk
                                                                                         : cfg.shieldDefAttack;
                scenarioSetRow(out.rows[1], "CPU", kDefNames[atk < kShieldDefCount ? atk : 0], kScenarioPlateWhite);
            }
            else
            {
                scenarioSetRow(out.rows[1], "CPU", kOosNames[cfg.oosAction], kScenarioPlateWhite);
            }
            out.rows[2].label = "Hits";
            snprintf(out.rows[2].value, sizeof(out.rows[2].value), "%u", static_cast<unsigned>(st.shieldHitCount));
            out.rows[2].valueColor = st.shieldHitCount > 0u ? kScenarioPlateGreen : kScenarioPlateWhite;
            break;
        }

        case kScenarioShieldDrop:
        case kScenarioSlideOff:
        {
            bool isDrop = p == kScenarioShieldDrop;
            bool cleared = isDrop ? st.sawShieldDrop : st.sawSlideOff;
            scenarioSetRow(out.rows[0], "Status", cleared ? (isDrop ? "Dropped" : "Slide-Off") : "Ready",
                           cleared ? kScenarioPlateGreen : kScenarioPlateMuted);
            if (st.hasPlatform)
            {
                out.rows[1].label = "Plat";
                snprintf(out.rows[1].value, sizeof(out.rows[1].value), "Plat %u",
                         static_cast<unsigned>(st.resolvedPlatformSlot) + 1u);
                out.rows[1].valueColor = kScenarioPlateWhite;
            }
            else
            {
                scenarioSetRow(out.rows[1], "Plat", "No Plat", kScenarioPlateAmber);
            }
            break;
        }

        case kScenarioLedgetech:
        {
            static const char* const kLtNames[kLtAtkCount] = {
                "D-Tilt", "F-Tilt", "F-Smash", "D-Smash", "D-Air", "Neutral B", "Side B", "Random"
            };
            scenarioSetRow(out.rows[0], "Status", st.sawWallTech ? "Wall Tech" : "Ready",
                           st.sawWallTech ? kScenarioPlateGreen : kScenarioPlateMuted);
            u8 atk = (cfg.ledgetechAttack == kLtAtkRandom && st.resetCount > 0u) ? st.resolvedLedgetechAttack
                                                                                 : cfg.ledgetechAttack;
            scenarioSetRow(out.rows[1], "Attack", kLtNames[atk < kLtAtkCount ? atk : kLtAtkDsmash],
                           kScenarioPlateWhite);
            break;
        }

        case kScenarioGrabEscape:
        {
            static const char* const kGrabNames[kGrabActCount] = {
                "Hold", "Rand Throw", "F-Throw", "B-Throw", "U-Throw", "D-Throw", "Stand"
            };
            if (st.sawGrabEscape || (st.grabActive && st.grabEscapeFrames > 0u))
            {
                out.rows[0].label = "Status";
                snprintf(out.rows[0].value, sizeof(out.rows[0].value), st.sawGrabEscape ? "%uf Out" : "%uf Held",
                         static_cast<unsigned>(st.grabEscapeFrames));
                out.rows[0].valueColor = st.sawGrabEscape ? kScenarioPlateGreen : kScenarioPlateGold;
            }
            else
            {
                scenarioSetRow(out.rows[0], "Status", "Ready", kScenarioPlateMuted);
            }
            scenarioSetRow(out.rows[1], "Action", kGrabNames[cfg.grabAction], kScenarioPlateWhite);
            break;
        }

        case kScenarioMultiHitSdi:
        {
            if (st.sdiEscaped)
            {
                scenarioSetRow(out.rows[0], "Status", "Escaped", kScenarioPlateGreen);
            }
            else if (st.sdiHitCount == 1u)
            {
                scenarioSetRow(out.rows[0], "Status", "1 Hit", kScenarioPlateGold);
            }
            else if (st.sdiHitCount > 1u)
            {
                out.rows[0].label = "Status";
                snprintf(out.rows[0].value, sizeof(out.rows[0].value), "%u Hits",
                         static_cast<unsigned>(st.sdiHitCount));
                out.rows[0].valueColor = kScenarioPlateGold;
            }
            else
            {
                scenarioSetRow(out.rows[0], "Status", "Ready", kScenarioPlateMuted);
            }
            if (st.sdiHitCount > 0u || st.sdiHitlagFrames > 0u)
            {
                float dist = scenarioSdiDist(st.sdiDispX, st.sdiDispY);
                float clamped = dist > 99.9f ? 99.9f : dist;
                unsigned t10 = static_cast<unsigned>(clamped * 10.0f + 0.5f);
                out.rows[1].label = "SDI";
                snprintf(out.rows[1].value, sizeof(out.rows[1].value), "%u.%u u", t10 / 10u, t10 % 10u);
                out.rows[1].valueColor = dist >= 0.5f ? kScenarioPlateWhite : kScenarioPlateMuted;
            }
            else
            {
                scenarioSetRow(out.rows[1], "SDI", "--", kScenarioPlateMuted);
            }
            break;
        }

        default:
            out.count = 0;
            return false;
        }
        return true;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
}
}
