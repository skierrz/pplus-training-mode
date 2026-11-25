#include "extra/scenario_presets_core.h"

#include <string.h>

#include "main/cpu/fighter_input.h"

namespace tm {
namespace extra {
    namespace {
        // Common Brawl/P+ Fighter::Status::Kind values (lib/BrawlHeaders
        // ft/fighter.h:16-143, verified against src/main/game/status_names.cpp).
        enum {
            kStWait = 0x00,
            kStWalk = 0x01,
            kStWalkBrake = 0x02,
            kStJumpSquat = 0x0A,
            kStJump = 0x0B,
            kStJumpAerial = 0x0C,
            kStFly = 0x0D,
            kStFall = 0x0E,
            kStFallAerial = 0x0F,
            kStSquat = 0x11,
            kStSquatWait = 0x12,
            kStGuardOn = 0x1A,
            kStGuard = 0x1B,
            kStGuardOff = 0x1C,
            kStGuardDamage = 0x1D,
            kStEscapeAir = 0x21,
            kStAttackFirst = 0x24,
            kStAttackS3 = 0x27,
            kStAttackAir = 0x33,
            kStCatch = 0x34,
            kStCatchPull = 0x35,
            kStCatchDash = 0x36,
            kStCatchDashPull = 0x37,
            kStCatchWait = 0x39,
            kStThrow = 0x3C,
            kStCapturePulled = 0x3D,
            kStCaptureWait = 0x3E,
            kStCaptureDamage = 0x3F,
            kStCaptureCut = 0x40,
            kStCaptureJump = 0x41,
            kStCaptureThrown = 0x42,
            kStCapturePulledYoshi = 0x107,
            kStCaptureWaitYoshi = 0x108,
            kStCaptureDamageYoshi = 0x109,
            kStCaptureCutYoshi = 0x10A,
            kStDamage = 0x43,
            kStDamageFly = 0x45,
            kStDamageFall = 0x49,
            kStDownFirst = 0x4A,
            kStDownLast = 0x55,
            kStShieldBreakFly = 0x56,
            kStFuraFuraEnd = 0x5B,
            kStPassive = 0x60,
            kStPassiveFb = 0x61,
            kStPassiveWall = 0x62,
            kStPassiveWallJump = 0x63,
            kStPass = 0x72,
            kStCliffCatch = 0x74,
            kStCliffWait = 0x75,
            kStCliffLast = 0x7B,
            kStSlipFirst = 0x89,
            kStSlipLast = 0x92,
            kStDead = 0xBD,
            kStRebirth = 0xBE,
            kStStandby = 0x10B,
            kStSpecialFirst = 0x112
        };

        enum {
            kTechGraceFrames = 42,
            kShieldGraceFrames = 32,
            kShieldDropGraceFrames = 26,
            kLedgetechGraceFrames = 30,
            kSlideOffGraceFrames = 28,
            kGrabGraceFrames = 28,
            kSdiEscapeGraceFrames = 26,
            kLedgetechMinPercent = 50,
            kLedgetechTargetPercent = 60,
            kEdgeguardMinCpuPercent = 30,
            kEdgeguardTargetCpuPercent = 35,
            kSlideOffMinPercent = 75,
            kSlideOffTargetPercent = 95,
            kUpAirMinFrames = 5,
            kUpAirMaxFrames = 24,
            kUpAirAirborneDelay = 2
        };

        inline float absf(float v)
        {
            return v < 0.0f ? -v : v;
        }

        bool isGuardStatus(int s)
        {
            return s == kStGuardOn || s == kStGuard || s == kStGuardOff || s == kStGuardDamage;
        }

        bool isShieldBreakStatus(int s)
        {
            return s >= kStShieldBreakFly && s <= kStFuraFuraEnd;
        }

        bool isDownOrTechStatus(int s)
        {
            return (s >= kStDownFirst && s <= kStDownLast) || s == kStPassive || s == kStPassiveFb
                   || (s >= kStSlipFirst && s <= kStSlipLast);
        }

        bool isTumbleStatus(int s)
        {
            return s >= kStDamageFly && s <= kStDamageFall;
        }

        bool isAirEscapeStatus(int s)
        {
            return s == kStJump || s == kStJumpAerial || s == kStFly || s == kStFall || s == kStFallAerial
                   || s == kStEscapeAir;
        }

        bool isCaptureHoldStatus(int s)
        {
            return (s >= kStCapturePulled && s <= kStCaptureDamage)
                   || (s >= kStCapturePulledYoshi && s <= kStCaptureDamageYoshi);
        }

        bool isCaptureEscapeStatus(int s)
        {
            return s == kStCaptureCut || s == kStCaptureJump || s == kStCaptureCutYoshi;
        }

        bool isWallTechStatus(int s)
        {
            return s == kStPassiveWall || s == kStPassiveWallJump;
        }

        bool isLedgeStatus(int s)
        {
            return s == kStCliffCatch || s == kStCliffWait;
        }

        bool isAttackOrSpecial(int s)
        {
            return (s >= kStAttackFirst && s <= kStThrow) || s >= kStSpecialFirst;
        }

        bool isNeutralGrounded(int s, bool air, int hitstun, int hitlag)
        {
            if (air || hitstun > 0 || hitlag > 0)
            {
                return false;
            }
            return s == kStWait || s == kStWalk || s == kStWalkBrake || s == kStSquat || s == kStSquatWait
                   || s == kStGuardOn || s == kStGuard;
        }

        bool isDeadOrStandby(int s)
        {
            return s == kStDead || s == kStStandby;
        }

        // True when a fighter is approaching a blast zone so auto-reset can warp
        // it cleanly before Brawl starts a KO respawn countdown.
        bool nearBlastZone(const StageGeometry& geom, float x, float y, bool inTumble)
        {
            if (!geom.blast.valid)
            {
                return false;
            }
            float lowY = geom.blast.bottom + 28.0f;
            if (geom.floor.valid)
            {
                lowY = geomMax(lowY, geom.floor.centerY - 95.0f);
            }
            if (y < lowY || x < geom.blast.left + 20.0f || x > geom.blast.right - 20.0f)
            {
                return true;
            }
            if (inTumble && y > geom.blast.top - 20.0f)
            {
                return true;
            }
            return false;
        }

#pragma dont_inline on
        void copySnapshotTo(const ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                            main::GeneralSettings* generalCfg, main::CpuSettings* cpuSettings,
                            main::OsdSettings* osdCfg)
        {
            cpuOpts = st.snapshotCpu;
            comboCfg = st.snapshotCombo;
            if (st.snapshotPlayerValid && generalCfg != NULL)
            {
                generalCfg->playerPercent = st.snapshotPlayerPercent;
                generalCfg->lockPlayerPercent = st.snapshotLockPlayerPercent;
                generalCfg->playerInfiniteShields = st.snapshotPlayerInfShields;
            }
            if (st.snapshotCpuValid && cpuSettings != NULL)
            {
                cpuSettings->percent = st.snapshotCpuPercent;
                cpuSettings->lockPercent = st.snapshotLockCpuPercent;
            }
            if (st.snapshotOsdValid && osdCfg != NULL)
            {
                osdCfg->grabBreakout = st.snapshotGrabBreakoutOsd;
            }
        }
#pragma dont_inline reset

#pragma dont_inline on
        void applyPresetOverrides(const ScenarioConfig& cfg, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                                  main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg)
        {
            switch (cfg.preset)
            {
            case kScenarioComboLab:
                comboCfg.enabled = 1;
                comboCfg.fixPosition = 1;
                comboCfg.noclip = 1;
                comboCfg.comboTimerAdjustment = 0;
                comboCfg.recoveryTrainer = 0;
                if (comboCfg.enabledMeters == 0)
                {
                    comboCfg.enabledMeters = main::kMeterAll;
                }
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tdi = cpu::kTdiRandom;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.tech = cpu::kTechRandom;
                break;

            case kScenarioTechchase:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tech = cpu::kTechRandom;
                cpuOpts.getup = cpu::kGetupRandom;
                cpuOpts.tdi = cpu::kTdiRandom;
                break;

            case kScenarioEdgeguard:
                comboCfg.enabled = 1;
                comboCfg.recoveryTrainer = 1;
                comboCfg.noclip = 1;
                comboCfg.fixPosition = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.tdi = cpu::kTdiSurvival;
                if (cpuSettings != NULL && cpuSettings->percent < kEdgeguardMinCpuPercent)
                {
                    cpuSettings->percent = kEdgeguardTargetCpuPercent;
                    cpuSettings->lockPercent = 1;
                }
                break;

            case kScenarioShieldPressure:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorShield;
                cpuOpts.infiniteShields = cpu::kShieldsOn;
                cpuOpts.shieldHealth = cpu::kShieldFull;
                cpuOpts.counterShield = resolveOosCounterShield(cfg.oosAction);
                cpuOpts.counterGround = kCounterGroundJab;
                cpuOpts.counterAir = kCounterAirJumpNeutral;
                break;

            case kScenarioShieldDrop:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tdi = cpu::kTdiNone;
                if (generalCfg != NULL)
                {
                    generalCfg->playerInfiniteShields = cpu::kShieldsOn;
                }
                break;

            case kScenarioLedgetech:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tech = cpu::kTechNone;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
                if (generalCfg != NULL && generalCfg->playerPercent < kLedgetechMinPercent)
                {
                    generalCfg->playerPercent = kLedgetechTargetPercent;
                    generalCfg->lockPlayerPercent = 1;
                }
                break;

            case kScenarioSlideOff:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tdi = cpu::kTdiNone;
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
                if (generalCfg != NULL && generalCfg->playerPercent < kSlideOffMinPercent)
                {
                    generalCfg->playerPercent = kSlideOffTargetPercent;
                    generalCfg->lockPlayerPercent = 1;
                }
                break;

            case kScenarioGrabEscape:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.grabEscape = cfg.grabAction == kGrabActStand ? cpu::kMashHigh : cpu::kMashNone;
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
                if (osdCfg != NULL)
                {
                    osdCfg->grabBreakout = 1;
                }
                break;

            case kScenarioMultiHitSdi:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tdi = cpu::kTdiNone;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
                break;

            default:
                break;
            }
        }
#pragma dont_inline reset

        void clearCycleTrackers(ScenarioState& st)
        {
            st.havePrev = false;
            st.hmnGuardWindow = 0;
            st.techEngaged = false;
            st.shieldEngaged = false;
            st.oosCountered = false;
            st.shieldDropDone = false;
            st.ledgetechDone = false;
            st.slideOffArmed = false;
            st.slideOffDone = false;
            st.grabActive = false;
            st.grabDone = false;
            st.activeGrabFrames = 0;
            st.graceFrames = 0;
            st.sinceResetFrames = 0;
            st.sdiSequenceActive = false;
            st.sdiLatchedReset = true;
        }
    }

    u8 resolveOosCounterShield(u8 oosAction)
    {
        switch (oosAction)
        {
        case kOosGrab:
            return kCounterShieldGrab;
        case kOosUpB:
            return kCounterShieldUpB;
        case kOosSpotdodge:
            return kCounterShieldSpotdodge;
        case kOosRollAway:
            return kCounterShieldRollAway;
        case kOosShNair:
            return kCounterShieldShNair;
        case kOosWavedash:
            return kCounterShieldWavedashAway;
        case kOosRandom:
            return kCounterShieldRandom;
        default:
            return kCounterShieldGrab;
        }
    }

    // Complete P+ 3.2.1 per-fighter multi-hit SDI table verified directly from
    // sandbox/sd/Project+/pf/fighter/*/Fit*.pac subactionMain scripts.
    u8 bestMultiHitMoveForFighter(int ftKind)
    {
        static const u8 kBest[0x32] = {
            kMultiHitDownAir,  // 0x00 Mario
            kMultiHitUpB,      // 0x01 DK
            kMultiHitUpB,      // 0x02 Link
            kMultiHitUpAir,    // 0x03 Samus
            kMultiHitDownAir,  // 0x04 Yoshi
            kMultiHitDownAir,  // 0x05 Kirby
            kMultiHitDownAir,  // 0x06 Fox
            kMultiHitFair,     // 0x07 Pikachu
            kMultiHitDownB,    // 0x08 Luigi
            kMultiHitRapidJab, // 0x09 Falcon
            kMultiHitFair,     // 0x0A Ness
            kMultiHitDownAir,  // 0x0B Bowser
            kMultiHitDownAir,  // 0x0C Peach
            kMultiHitNair,     // 0x0D Zelda
            kMultiHitRapidJab, // 0x0E Sheik
            kMultiHitSideB,    // 0x0F Popo
            kMultiHitSideB,    // 0x10 Nana
            kMultiHitNair,     // 0x11 Marth
            kMultiHitRapidJab, // 0x12 G&W
            kMultiHitFair,     // 0x13 Falco
            kMultiHitNair,     // 0x14 Ganondorf
            kMultiHitUpB,      // 0x15 Wario
            kMultiHitSideB,    // 0x16 Meta Knight
            kMultiHitNair,     // 0x17 Pit
            kMultiHitFair,     // 0x18 ZSS
            kMultiHitNair,     // 0x19 Olimar
            kMultiHitNair,     // 0x1A Lucas
            kMultiHitRapidJab, // 0x1B Diddy
            kMultiHitDownAir,  // 0x1C PokeTrainer (unused)
            kMultiHitSideB,    // 0x1D Charizard
            kMultiHitDownAir,  // 0x1E Squirtle
            kMultiHitNair,     // 0x1F Ivysaur
            kMultiHitDownAir,  // 0x20 Dedede
            kMultiHitDownAir,  // 0x21 Lucario
            kMultiHitUpB,      // 0x22 Ike
            kMultiHitUpAir,    // 0x23 R.O.B.
            kMultiHitDownAir,  // 0x24 PraMai (unused)
            kMultiHitDownAir,  // 0x25 Jigglypuff
            kMultiHitNair,     // 0x26 Mewtwo
            kMultiHitUpB,      // 0x27 Roy
            kMultiHitDownAir,  // 0x28 DrMario (unused)
            kMultiHitUpB,      // 0x29 Toon Link
            kMultiHitDownAir,  // 0x2A ToonZelda (unused)
            kMultiHitDownAir,  // 0x2B ToonSheik (unused)
            kMultiHitNair,     // 0x2C Wolf
            kMultiHitRapidJab, // 0x2D Knuckles
            kMultiHitDownAir,  // 0x2E Snake
            kMultiHitUpAir,    // 0x2F Sonic
            kMultiHitDownAir,  // 0x30 Giga Bowser
            kMultiHitNair      // 0x31 Wario-Man
        };
        if (ftKind >= 0 && ftKind < static_cast<int>(sizeof(kBest)))
        {
            return kBest[ftKind];
        }
        return kMultiHitDownAir;
    }

    u8 resolveMultiHitMove(u8 multiHitMove, int cpuFtKind)
    {
        if (multiHitMove == kMultiHitAuto || multiHitMove >= kMultiHitMoveCount)
        {
            return bestMultiHitMoveForFighter(cpuFtKind);
        }
        return multiHitMove;
    }

    ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw)
    {
        ScenarioConfig c;
        c.preset = raw.preset < kScenarioCount ? raw.preset : static_cast<u8>(kScenarioOff);
        c.autoReset = raw.autoReset != 0 ? 1 : 0;
        c.side = raw.side < kScenarioSideCount ? raw.side : static_cast<u8>(kScenarioSideRight);
        c.multiHitMove = raw.multiHitMove < kMultiHitMoveCount ? raw.multiHitMove : static_cast<u8>(kMultiHitAuto);
        c.oosAction = raw.oosAction < kOosCount ? raw.oosAction : static_cast<u8>(kOosGrab);
        c.ledgetechSpawn = raw.ledgetechSpawn < kLtSpawnCount ? raw.ledgetechSpawn : static_cast<u8>(kLtSpawnCloseLow);
        c.ledgetechDj = raw.ledgetechDj < kLtDjCount ? raw.ledgetechDj : static_cast<u8>(kLtDjAvailable);
        c.ledgetechAttack = raw.ledgetechAttack < kLtAtkCount ? raw.ledgetechAttack : static_cast<u8>(kLtAtkDtilt);
        c.shieldDropPlat = raw.shieldDropPlat < kSdPlatCount ? raw.shieldDropPlat : static_cast<u8>(kSdPlatLeft);
        c.shieldDropSpacing =
            raw.shieldDropSpacing < kSdSpaceCount ? raw.shieldDropSpacing : static_cast<u8>(kSdSpaceRandom);
        c.shieldDropFacing =
            raw.shieldDropFacing < kSdFaceCount ? raw.shieldDropFacing : static_cast<u8>(kSdFaceToward);
        c.grabAction = raw.grabAction < kGrabActCount ? raw.grabAction : static_cast<u8>(kGrabActHold);
        c.grabPctMode = raw.grabPctMode < kGrabPctCount ? raw.grabPctMode : static_cast<u8>(kGrabPctRandom);
        return c;
    }

    void scenarioStateInit(ScenarioState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastAutoReset = 1;
        st.rng = 0x51A7C0DEu;
        st.lastCorrectWritten[0] = 0xFF;
        st.lastCorrectWritten[1] = 0xFF;
        st.restoreCorrectKind[0] = kCorrectGroundCliffStop;
        st.restoreCorrectKind[1] = kCorrectGroundCliffStop;
    }

    void scenarioTransientReset(ScenarioState& st)
    {
        clearCycleTrackers(st);
        st.restoreCorrect[0] = false;
        st.restoreCorrect[1] = false;
    }

    void scenarioCpuStateReset(ScenarioCpuState& st)
    {
        memset(&st, 0, sizeof(st));
    }

    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                                        main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                                        main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg)
    {
        if (st.activePreset == kScenarioOff)
        {
            return;
        }
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        cfg.preset = st.activePreset;
        if (st.snapshotActive)
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
        }
        applyPresetOverrides(cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
    }

    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                             main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg)
    {
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        bool presetChanged = cfg.preset != st.activePreset;
        bool oosChanged = cfg.oosAction != st.lastOosAction;
        bool grabChanged = cfg.grabAction != st.lastGrabAction || cfg.grabPctMode != st.lastGrabPctMode;
        bool layoutChanged = cfg.side != st.lastSide || cfg.multiHitMove != st.lastMultiHitMove || oosChanged
                             || grabChanged || cfg.ledgetechSpawn != st.lastLedgetechSpawn
                             || cfg.ledgetechDj != st.lastLedgetechDj || cfg.ledgetechAttack != st.lastLedgetechAttack
                             || cfg.shieldDropPlat != st.lastShieldDropPlat
                             || cfg.shieldDropSpacing != st.lastShieldDropSpacing
                             || cfg.shieldDropFacing != st.lastShieldDropFacing;
        st.lastAutoReset = cfg.autoReset;
        st.lastSide = cfg.side;
        st.lastMultiHitMove = cfg.multiHitMove;
        st.lastOosAction = cfg.oosAction;
        st.lastLedgetechSpawn = cfg.ledgetechSpawn;
        st.lastLedgetechDj = cfg.ledgetechDj;
        st.lastLedgetechAttack = cfg.ledgetechAttack;
        st.lastShieldDropPlat = cfg.shieldDropPlat;
        st.lastShieldDropSpacing = cfg.shieldDropSpacing;
        st.lastShieldDropFacing = cfg.shieldDropFacing;
        st.lastGrabAction = cfg.grabAction;
        st.lastGrabPctMode = cfg.grabPctMode;

        if (!presetChanged)
        {
            if (cfg.preset == kScenarioShieldPressure && oosChanged)
            {
                cpuOpts.counterShield = resolveOosCounterShield(cfg.oosAction);
            }
            if (cfg.preset == kScenarioGrabEscape && grabChanged)
            {
                cpuOpts.grabEscape = cfg.grabAction == kGrabActStand ? cpu::kMashHigh : cpu::kMashNone;
            }
            if (cfg.preset != kScenarioOff && layoutChanged)
            {
                st.pendingReset = true;
            }
            return layoutChanged && cfg.preset != kScenarioOff;
        }

        if (cfg.preset == kScenarioOff)
        {
            restoreScenarioSnapshot(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
            st.activePreset = kScenarioOff;
            st.pendingReset = false;
            st.hasPlatform = false;
            clearCycleTrackers(st);
            return true;
        }

        // Snapshot pre-preset settings ONLY on Off -> Preset transition.
        if (!st.snapshotActive)
        {
            st.snapshotCpu = cpuOpts;
            st.snapshotCombo = comboCfg;
            if (generalCfg != NULL)
            {
                st.snapshotPlayerPercent = generalCfg->playerPercent;
                st.snapshotLockPlayerPercent = generalCfg->lockPlayerPercent;
                st.snapshotPlayerInfShields = generalCfg->playerInfiniteShields;
                st.snapshotPlayerValid = true;
            }
            else
            {
                st.snapshotPlayerValid = false;
            }
            if (cpuSettings != NULL)
            {
                st.snapshotCpuPercent = cpuSettings->percent;
                st.snapshotLockCpuPercent = cpuSettings->lockPercent;
                st.snapshotCpuValid = true;
            }
            else
            {
                st.snapshotCpuValid = false;
            }
            if (osdCfg != NULL)
            {
                st.snapshotGrabBreakoutOsd = osdCfg->grabBreakout;
                st.snapshotOsdValid = true;
            }
            else
            {
                st.snapshotOsdValid = false;
            }
            st.snapshotActive = true;
        }
        else
        {
            // Preset A -> Preset B: always restore original snapshot first so
            // no overrides from Preset A leak into Preset B.
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
        }

        applyPresetOverrides(cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
        st.activePreset = cfg.preset;
        st.pendingReset = true;
        st.sawShieldDrop = false;
        st.sawWallTech = false;
        st.sawSlideOff = false;
        st.sawGrabEscape = false;
        st.sdiHitCount = 0;
        st.sdiHitlagFrames = 0;
        st.sdiDispX = 0.0f;
        st.sdiDispY = 0.0f;
        st.sdiEscaped = false;
        st.grabEscapeFrames = 0;
        st.shieldHitCount = 0;
        clearCycleTrackers(st);
        return true;
    }

    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* generalCfg, main::CpuSettings* cpuSettings,
                                 main::OsdSettings* osdCfg)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
            st.snapshotPlayerValid = false;
            st.snapshotCpuValid = false;
            st.snapshotOsdValid = false;
            st.snapshotActive = false;
        }
    }

    void discardScenarioSnapshot(ScenarioState& st)
    {
        st.snapshotActive = false;
        st.snapshotPlayerValid = false;
        st.snapshotCpuValid = false;
        st.snapshotOsdValid = false;
        st.activePreset = kScenarioOff;
        st.pendingReset = false;
        st.hasPlatform = false;
        clearCycleTrackers(st);
    }

    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                                  main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
        }
    }

    void requestScenarioManualReset(ScenarioState& st)
    {
        if (st.activePreset != kScenarioOff)
        {
            st.pendingReset = true;
            st.sawShieldDrop = false;
            st.sawWallTech = false;
            st.sawSlideOff = false;
            st.sawGrabEscape = false;
            st.sdiHitCount = 0;
            st.sdiHitlagFrames = 0;
            st.sdiDispX = 0.0f;
            st.sdiDispY = 0.0f;
            st.sdiEscaped = false;
            clearCycleTrackers(st);
        }
    }

    void computeScenarioSpawnsEx(const ScenarioConfig& rawCfg, int cpuFtKind, u32& rng, const StageGeometry& geom,
                                 ScenarioSpawns& out)
    {
        memset(&out, 0, sizeof(out));
        if (!geom.floor.valid)
        {
            return;
        }
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        u8 effSide = cfg.side == kScenarioSideRandom ? static_cast<u8>(geomNextRng(rng) & 1u) : cfg.side;
        bool leftSide = effSide == kScenarioSideLeft;
        float dir = leftSide ? -1.0f : 1.0f;
        float floorLeft = geom.floor.leftX;
        float floorRight = geom.floor.rightX;
        float centerX = 0.5f * (floorLeft + floorRight);
        float halfW = 0.5f * (floorRight - floorLeft);
        float floorY = geom.floor.centerY;

        out.valid = true;
        out.hmnFalling = false;
        out.cpuFalling = false;
        out.hasPlatform = false;
        out.resolvedPlatformSlot = kPlatSlotLeft;

        switch (cfg.preset)
        {
        case kScenarioComboLab:
        {
            float span = geomMin(12.0f, halfW * 0.30f);
            out.hmnX = centerX - dir * span;
            out.hmnY = floorY;
            out.hmnLr = dir;
            out.cpuX = centerX + dir * span;
            out.cpuY = floorY;
            out.cpuLr = -dir;
            break;
        }

        case kScenarioTechchase:
        {
            float span = geomMin(14.0f, halfW * 0.35f);
            out.hmnX = centerX - dir * span;
            out.hmnY = floorY;
            out.hmnLr = dir;
            out.cpuX = centerX + dir * span;
            out.cpuY = floorY;
            out.cpuLr = -dir;
            break;
        }

        case kScenarioEdgeguard:
        {
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            float inward = -outward;
            out.hmnX = ledge.x + inward * geomMin(14.0f, halfW * 0.40f);
            out.hmnY = ledge.y;
            out.hmnLr = outward;
            out.hmnFalling = false;

            float maxOut = leftSide ? (ledge.x - geom.blast.left - 28.0f) : (geom.blast.right - ledge.x - 28.0f);
            float distOut = geomClamp(38.0f, 18.0f, geomMax(18.0f, maxOut));
            out.cpuX = ledge.x + outward * distOut;
            float loY = geom.blast.valid ? (geom.blast.bottom + 40.0f) : (ledge.y - 40.0f);
            float hiY = geom.blast.valid ? (geom.blast.top - 30.0f) : (ledge.y + 30.0f);
            out.cpuY = geomClamp(ledge.y - 10.0f, loY, geomMax(loY, hiY));
            out.cpuLr = inward;
            out.cpuFalling = true;
            break;
        }

        case kScenarioShieldPressure:
        {
            // Close facing spacing: 12.0 units.
            float span = geomMin(6.0f, halfW * 0.25f);
            out.hmnX = centerX - dir * span;
            out.hmnY = floorY;
            out.hmnLr = dir;
            out.cpuX = centerX + dir * span;
            out.cpuY = floorY;
            out.cpuLr = -dir;
            break;
        }

        case kScenarioShieldDrop:
        {
            StagePlatform plat;
            u8 actualSlot = kPlatSlotLeft;
            if (selectPlatformBySlot(geom, cfg.shieldDropPlat, rng, plat, &actualSlot))
            {
                out.hasPlatform = true;
                out.resolvedPlatformSlot = actualSlot;
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                float inward = platMid >= centerX ? -1.0f : 1.0f;
                out.hmnX = platMid;
                out.hmnY = plat.y;
                out.hmnLr = inward;

                u8 spaceMode = cfg.shieldDropSpacing;
                if (spaceMode == kSdSpaceRandom)
                {
                    spaceMode = static_cast<u8>(1u + (geomNextRng(rng) % 3u)); // Close, Under, or Far
                }
                float offset = 0.0f;
                if (spaceMode == kSdSpaceClose)
                {
                    offset = inward * 6.0f;
                }
                else if (spaceMode == kSdSpaceFar)
                {
                    offset = inward * 14.0f;
                }
                else
                {
                    // Under: directly below P1 on the platform.
                    offset = 0.0f;
                }
                out.cpuX = geomClamp(platMid + offset, floorLeft + 10.0f, floorRight - 10.0f);
                out.cpuY = floorY;

                float towardP1 = absf(platMid - out.cpuX) > 0.5f ? (platMid >= out.cpuX ? 1.0f : -1.0f) : -inward;
                u8 faceMode = cfg.shieldDropFacing;
                if (faceMode == kSdFaceRandom)
                {
                    faceMode = static_cast<u8>(geomNextRng(rng) & 1u);
                }
                out.cpuLr = faceMode == kSdFaceAway ? -towardP1 : towardP1;
            }
            else
            {
                out.hasPlatform = false;
                float span = geomMin(10.0f, halfW * 0.30f);
                out.hmnX = centerX - dir * span;
                out.hmnY = floorY;
                out.hmnLr = dir;
                out.cpuX = centerX + dir * span;
                out.cpuY = floorY;
                out.cpuLr = -dir;
            }
            break;
        }

        case kScenarioLedgetech:
        {
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            float inward = -outward;
            u8 spawnMode = cfg.ledgetechSpawn;
            if (spawnMode == kLtSpawnRandom)
            {
                spawnMode = static_cast<u8>(geomNextRng(rng) % 4u);
            }
            float outDist = 8.0f;
            float relY = -20.0f;
            switch (spawnMode)
            {
            case kLtSpawnDeepLow:
                outDist = 12.0f;
                relY = -36.0f;
                break;
            case kLtSpawnFarOut:
                outDist = 26.0f;
                relY = -12.0f;
                break;
            case kLtSpawnHigh:
                outDist = 18.0f;
                relY = 10.0f;
                break;
            case kLtSpawnCloseLow:
            default:
                outDist = 8.0f;
                relY = -20.0f;
                break;
            }
            float maxOut = leftSide ? (ledge.x - geom.blast.left - 24.0f) : (geom.blast.right - ledge.x - 24.0f);
            outDist = geomClamp(outDist, 6.0f, geomMax(8.0f, maxOut));
            out.hmnX = ledge.x + outward * outDist;
            float loY = geom.blast.valid ? (geom.blast.bottom + 36.0f) : (ledge.y - 44.0f);
            float hiY = geom.blast.valid ? (geom.blast.top - 30.0f) : (ledge.y + 30.0f);
            out.hmnY = geomClamp(ledge.y + relY, loY, geomMax(loY, hiY));
            out.hmnLr = inward;
            out.hmnFalling = true;

            // Place CPU right at the ledge lip so dtilt/ftilt/smash/dair pokes
            // past the edge across short- and long-reach fighters alike.
            out.cpuX = ledge.x + inward * geomMin(4.5f, halfW * 0.25f);
            out.cpuY = ledge.y;
            out.cpuLr = outward;
            out.cpuFalling = false;
            break;
        }

        case kScenarioSlideOff:
        {
            StagePlatform plat;
            if (selectPlatform(geom, leftSide, plat))
            {
                out.hasPlatform = true;
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                bool rightPlat = platMid >= centerX;
                float edgeX = rightPlat ? plat.leftX : plat.rightX;
                float intoPlat = rightPlat ? 1.0f : -1.0f;
                float platW = plat.rightX - plat.leftX;
                out.hmnX = edgeX + intoPlat * geomMin(3.5f, platW * 0.20f);
                out.hmnY = plat.y;
                out.hmnLr = intoPlat;
                out.cpuX = out.hmnX + intoPlat * geomMin(9.0f, geomMax(5.5f, platW - 6.0f));
                out.cpuY = plat.y;
                out.cpuLr = -intoPlat;
            }
            else
            {
                out.hasPlatform = false;
                const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
                float inward = ledge.dir;
                out.hmnX = ledge.x + inward * 4.5f;
                out.hmnY = ledge.y;
                out.hmnLr = inward;
                out.cpuX = ledge.x + inward * geomMin(13.5f, halfW * 0.45f);
                out.cpuY = ledge.y;
                out.cpuLr = -inward;
            }
            break;
        }

        case kScenarioGrabEscape:
        {
            // Close standing-grab spacing (6.5u) so short-arm fighters connect on Frame 1.
            float span = geomMin(3.25f, halfW * 0.20f);
            out.hmnX = centerX - dir * span;
            out.hmnY = floorY;
            out.hmnLr = dir;
            out.cpuX = centerX + dir * span;
            out.cpuY = floorY;
            out.cpuLr = -dir;
            break;
        }

        case kScenarioMultiHitSdi:
        {
            u8 move = resolveMultiHitMove(cfg.multiHitMove, cpuFtKind);
            float spacing = 5.5f;
            switch (move)
            {
            case kMultiHitNair:
                spacing = 6.0f;
                break;
            case kMultiHitFair:
                spacing = 7.5f;
                break;
            case kMultiHitUpAir:
                spacing = 5.5f;
                break;
            case kMultiHitDownAir:
                spacing = 5.0f;
                break;
            case kMultiHitRapidJab:
                spacing = 7.0f;
                break;
            case kMultiHitUpB:
            case kMultiHitDownB:
                spacing = 5.5f;
                break;
            case kMultiHitSideB:
                spacing = 10.5f;
                break;
            default:
                spacing = 5.5f;
                break;
            }
            float span = geomMin(spacing * 0.5f, halfW * 0.30f);
            out.hmnX = centerX + dir * span;
            out.hmnY = floorY;
            out.hmnLr = -dir;
            out.cpuX = centerX - dir * span;
            out.cpuY = floorY;
            out.cpuLr = dir;
            break;
        }

        default:
            out.valid = false;
            break;
        }
    }

    void computeScenarioSpawns(u8 preset, u8 side, u8 multiHitMove, const StageGeometry& geom, ScenarioSpawns& out)
    {
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

#pragma dont_inline on
    static void triggerWarp(ScenarioState& st, const ScenarioConfig& cfg, int cpuFtKind, const StageGeometry& geom,
                            ScenarioStepAction& act, bool warpHmn, bool warpCpu)
    {
        st.resolvedMultiHit = resolveMultiHitMove(cfg.multiHitMove, cpuFtKind);
        st.resolvedLedgetechAttack = cfg.ledgetechAttack == kLtAtkRandom
                                         ? static_cast<u8>(geomNextRng(st.rng) % 7u)
                                         : cfg.ledgetechAttack;
        st.resolvedSide = cfg.side == kScenarioSideRandom ? static_cast<u8>(geomNextRng(st.rng) & 1u) : cfg.side;

        ScenarioConfig resolvedCfg = cfg;
        resolvedCfg.side = st.resolvedSide;
        ScenarioSpawns sp;
        computeScenarioSpawnsEx(resolvedCfg, cpuFtKind, st.rng, geom, sp);
        if (!sp.valid)
        {
            return;
        }
        st.hasPlatform = sp.hasPlatform;
        st.resolvedPlatformSlot = sp.resolvedPlatformSlot;
        st.hmnSpawnX = sp.hmnX;
        st.hmnSpawnY = sp.hmnY;
        st.cpuSpawnX = sp.cpuX;
        st.cpuSpawnY = sp.cpuY;
        st.resetCount++;
        st.pendingReset = false;
        clearCycleTrackers(st);

        act.spawns = sp;
        act.warpHmn = warpHmn;
        act.warpCpu = warpCpu;
        act.resetComboTrainer = true;
        if (warpHmn)
        {
            act.setNoclip[0] = true;
            act.refreshHmnAir = sp.hmnFalling;
            act.hmnSpendDj = cfg.preset == kScenarioLedgetech && cfg.ledgetechDj == kLtDjSpent;
            if (cfg.preset == kScenarioGrabEscape && cfg.grabAction != kGrabActStand
                && cfg.grabPctMode == kGrabPctRandom)
            {
                act.useExplicitHmnDmg = true;
                act.hmnDmg = static_cast<float>(geomNextRng(st.rng) % 80u);
            }
            st.restoreCorrect[0] = true;
            st.restoreCorrectKind[0] = sp.hmnFalling ? kCorrectAir : kCorrectGroundCliffStop;
            st.lastCorrectWritten[0] = kCorrectNone;
        }
        if (warpCpu)
        {
            act.setNoclip[1] = true;
            act.refreshCpuAir = sp.cpuFalling;
            st.restoreCorrect[1] = true;
            st.restoreCorrectKind[1] = sp.cpuFalling ? kCorrectAir : kCorrectGroundCliffStop;
            st.lastCorrectWritten[1] = kCorrectNone;
        }
    }
#pragma dont_inline reset

    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* generalCfg, main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg)
    {
        memset(&act, 0, sizeof(act));
        act.restoreCorrectKind[0] = kCorrectGroundCliffStop;
        act.restoreCorrectKind[1] = kCorrectGroundCliffStop;

        // Always honor pending 1-frame noclip restores first, even if Preset
        // was just turned Off, so no fighter is ever stranded in Correct_None.
        for (int s = 0; s < 2; s++)
        {
            if (st.restoreCorrect[s])
            {
                act.restoreCorrect[s] = true;
                act.restoreCorrectKind[s] = st.restoreCorrectKind[s];
                st.restoreCorrect[s] = false;
                st.lastCorrectWritten[s] = st.restoreCorrectKind[s];
            }
        }

        u16 prevPct = generalCfg != NULL ? generalCfg->playerPercent : 0;
        u16 prevCpuPct = cpuSettings != NULL ? cpuSettings->percent : 0;
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        applyScenarioConfig(st, cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg);
        if (generalCfg != NULL && generalCfg->playerPercent != prevPct)
        {
            act.applyHmnPercent = true;
        }
        if (cpuSettings != NULL && cpuSettings->percent != prevCpuPct)
        {
            act.applyCpuPercent = true;
        }
        if (st.activePreset == kScenarioOff)
        {
            return;
        }

        if (st.pendingReset)
        {
            // Defer warping while either fighter is in Dead (0xBD) or Standby
            // (0x10B) so Brawl's KO countdown completes into Rebirth (0xBE)
            // instead of firing a delayed halo respawn after switching presets.
            if (isDeadOrStandby(in.hmnStatus) || isDeadOrStandby(in.cpuStatus))
            {
                return;
            }
            triggerWarp(st, cfg, in.cpuFtKind, in.geom, act, true, true);
            return;
        }

        st.sinceResetFrames++;
        bool wantReset = false;
        bool resetHmn = true;
        bool resetCpu = true;
        bool autoReset = cfg.autoReset != 0;
        bool hmnDanger = in.hmnOutOfPlay || nearBlastZone(in.geom, in.hmnX, in.hmnY, isTumbleStatus(in.hmnStatus));
        bool cpuDanger = in.cpuOutOfPlay || nearBlastZone(in.geom, in.cpuX, in.cpuY, isTumbleStatus(in.cpuStatus));

        switch (st.activePreset)
        {
        case kScenarioComboLab:
            // Combo Trainer drives CPU hitstun-drop auto-resets; reset both
            // only if HMN gets KO'd or approaches a blast zone.
            if (autoReset && hmnDanger)
            {
                wantReset = true;
            }
            break;

        case kScenarioTechchase:
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            if (isDownOrTechStatus(in.cpuStatus) || (in.cpuAir && isTumbleStatus(in.cpuStatus)))
            {
                st.techEngaged = true;
                st.graceFrames = 0;
            }
            else if (st.techEngaged)
            {
                bool cpuStillComboed = in.cpuHitstun > 0 || in.cpuHitlag > 0 || isCaptureHoldStatus(in.cpuStatus)
                                       || in.cpuStatus == kStCaptureThrown || (in.cpuAir && isTumbleStatus(in.cpuStatus));
                if (cpuStillComboed)
                {
                    st.graceFrames = 0;
                }
                else
                {
                    bool groundedSettled = isNeutralGrounded(in.cpuStatus, in.cpuAir, in.cpuHitstun, in.cpuHitlag)
                                           || (!in.cpuAir && in.cpuActionable);
                    // Also reset if CPU escapes into the air (Jump, Double Jump,
                    // Airdodge, or post-wiggle Fall) once out of hitstun/tumble,
                    // without cutting short platform techchases during tumble!
                    bool airEscaped = in.cpuAir && !isTumbleStatus(in.cpuStatus) && !isDownOrTechStatus(in.cpuStatus)
                                      && (in.cpuActionable || isAirEscapeStatus(in.cpuStatus));
                    if (groundedSettled || airEscaped)
                    {
                        if (st.graceFrames == 0)
                        {
                            st.completionCount++;
                        }
                        st.graceFrames++;
                        if (autoReset && st.graceFrames >= kTechGraceFrames)
                        {
                            wantReset = true;
                        }
                    }
                }
            }
            break;

        case kScenarioEdgeguard:
            // Combo Trainer recoveryTrainer loops CPU offstage spawns; keep
            // HMN anchored at the ledge lip if HMN gets KO'd or nears a blast zone.
            if (autoReset && hmnDanger)
            {
                wantReset = true;
                resetHmn = true;
                resetCpu = false;
            }
            break;

        case kScenarioShieldPressure:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            bool cpuGuardDmg = in.cpuStatus == kStGuardDamage || in.cpuShieldStun > 0;
            if (cpuGuardDmg && !st.prevCpuGuardDamage)
            {
                st.shieldHitCount++;
                st.completionCount++;
                st.shieldEngaged = true;
                st.graceFrames = 0;
            }
            else if (in.cpuHitstun > 0)
            {
                st.shieldEngaged = true;
                st.graceFrames = 0;
            }
            st.prevCpuGuardDamage = cpuGuardDmg;
            if (isShieldBreakStatus(in.cpuStatus))
            {
                st.shieldEngaged = true;
                st.graceFrames++;
                if (autoReset && st.graceFrames >= 12)
                {
                    wantReset = true;
                }
            }
            else if (st.shieldEngaged)
            {
                bool cpuOosAction = isAttackOrSpecial(in.cpuStatus) || in.cpuStatus == kStGuardOff
                                    || (in.cpuStatus >= 0x1E && in.cpuStatus <= 0x20)
                                    || in.cpuStatus == kStJumpSquat || in.cpuStatus == kStJump;
                if (cpuOosAction)
                {
                    st.oosCountered = true;
                }
                bool activeExchange = in.hmnHitstun > 0 || in.hmnHitlag > 0 || in.cpuHitstun > 0 || in.cpuHitlag > 0
                                      || isAttackOrSpecial(in.cpuStatus) || isAttackOrSpecial(in.hmnStatus)
                                      || cpuGuardDmg;
                if (activeExchange)
                {
                    st.graceFrames = 0;
                }
                else if (st.oosCountered || !isGuardStatus(in.cpuStatus) || in.cpuStatus == kStGuardOff)
                {
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kShieldGraceFrames)
                    {
                        wantReset = true;
                    }
                }
                else
                {
                    st.graceFrames = 0;
                }
            }
            break;
        }

        case kScenarioShieldDrop:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            if (isGuardStatus(in.hmnStatus))
            {
                st.hmnGuardWindow = 4;
            }
            else if (st.hmnGuardWindow > 0)
            {
                st.hmnGuardWindow--;
            }

            // Detect live Guard -> Pass (0x1A..0x1D -> 0x72) shield drop.
            bool passNow = in.hmnStatus == kStPass || in.hmnEnteredPass;
            if (!st.shieldDropDone && passNow && (st.hmnGuardWindow > 0 || isGuardStatus(st.prevHmnStatus)))
            {
                st.sawShieldDrop = true;
                st.shieldDropDone = true;
                st.completionCount++;
                st.graceFrames = 0;
            }
            else if (st.shieldDropDone)
            {
                bool settledOnFloor = !in.hmnAir && in.hmnY <= in.geom.floor.centerY + 4.0f
                                      && in.hmnHitlag == 0 && in.hmnHitstun == 0 && in.hmnActionable;
                if (settledOnFloor || st.graceFrames > 0)
                {
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kShieldDropGraceFrames)
                    {
                        wantReset = true;
                    }
                }
            }
            break;
        }

        case kScenarioLedgetech:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            if (isWallTechStatus(in.hmnStatus))
            {
                if (!st.sawWallTech)
                {
                    st.sawWallTech = true;
                    st.completionCount++;
                }
                st.ledgetechDone = true;
            }
            else if (isLedgeStatus(in.hmnStatus))
            {
                st.ledgetechDone = true;
            }
            else if (st.sinceResetFrames > 6 && !in.hmnAir && in.geom.floor.valid
                     && in.hmnX >= in.geom.floor.leftX && in.hmnX <= in.geom.floor.rightX)
            {
                st.ledgetechDone = true;
            }

            if (st.ledgetechDone)
            {
                st.graceFrames++;
                if (autoReset && st.graceFrames >= kLedgetechGraceFrames)
                {
                    wantReset = true;
                }
            }
            break;
        }

        case kScenarioSlideOff:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            if (!in.hmnAir && st.sinceResetFrames >= 1)
            {
                st.slideOffArmed = true;
            }
            bool airborneTumble = in.hmnAir && (isTumbleStatus(in.hmnStatus) || in.hmnHitstun > 0);
            if (!st.slideOffDone && st.slideOffArmed && st.havePrev && !st.prevHmnAir && airborneTumble)
            {
                st.sawSlideOff = true;
                st.slideOffDone = true;
                st.completionCount++;
                st.graceFrames = 0;
            }
            else if (st.slideOffDone)
            {
                bool settled = (!in.hmnAir && in.hmnHitstun == 0 && in.hmnHitlag == 0
                                && (in.hmnActionable || isDownOrTechStatus(in.hmnStatus)))
                               || st.graceFrames > 0;
                if (settled)
                {
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kSlideOffGraceFrames)
                    {
                        wantReset = true;
                    }
                }
            }
            break;
        }

        case kScenarioGrabEscape:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            bool anyHeld = isCaptureHoldStatus(in.hmnStatus) || isCaptureHoldStatus(in.cpuStatus);
            bool anyEscaped = isCaptureEscapeStatus(in.hmnStatus) || isCaptureEscapeStatus(in.cpuStatus);
            bool anyThrown = in.hmnStatus == kStCaptureThrown || in.cpuStatus == kStCaptureThrown;

            if (anyHeld)
            {
                if (!st.grabActive)
                {
                    st.grabActive = true;
                    st.activeGrabFrames = 0;
                }
                st.activeGrabFrames++;
                st.grabEscapeFrames = st.activeGrabFrames;
                st.graceFrames = 0;
            }
            else if (anyEscaped)
            {
                if (!st.grabDone)
                {
                    st.sawGrabEscape = true;
                    st.grabDone = true;
                    st.grabEscapeFrames = st.activeGrabFrames > 0 ? st.activeGrabFrames : 1;
                    st.completionCount++;
                    st.graceFrames = 0;
                }
                st.grabActive = false;
            }
            else if (anyThrown)
            {
                st.grabActive = false;
                st.grabDone = true;
                st.graceFrames = 0;
            }
            else if (st.grabDone)
            {
                bool settled = in.hmnHitstun == 0 && in.cpuHitstun == 0 && in.hmnHitlag == 0 && in.cpuHitlag == 0
                               && !in.hmnAir && !in.cpuAir;
                if (settled || st.graceFrames > 0)
                {
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kGrabGraceFrames)
                    {
                        wantReset = true;
                    }
                }
            }
            else if (st.grabActive)
            {
                // Escaped directly to Wait/Fall (e.g. Yoshi SwallowCapture breakout).
                if (!st.grabDone)
                {
                    st.sawGrabEscape = true;
                    st.grabDone = true;
                    st.grabEscapeFrames = st.activeGrabFrames > 0 ? st.activeGrabFrames : 1;
                    st.completionCount++;
                }
                st.grabActive = false;
                st.graceFrames = 1;
            }
            break;
        }

        case kScenarioMultiHitSdi:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            bool hmnInDamageLag = in.hmnHitlag > 0 && in.hmnDamageHitlag;
            bool newHitLink = hmnInDamageLag && (!st.havePrev || in.hmnHitlag > st.prevHmnHitlag || st.prevHmnHitlag == 0);
            if (newHitLink)
            {
                if (st.sdiLatchedReset)
                {
                    st.sdiHitCount = 0;
                    st.sdiHitlagFrames = 0;
                    st.sdiDispX = 0.0f;
                    st.sdiDispY = 0.0f;
                    st.sdiEscaped = false;
                    st.sdiLatchedReset = false;
                }
                st.sdiSequenceActive = true;
                st.sdiHitCount++;
                st.graceFrames = 0;
            }
            if (hmnInDamageLag)
            {
                st.sdiHitlagFrames++;
                if (st.havePrev)
                {
                    float dx = in.hmnX - st.prevHmnX;
                    float dy = in.hmnY - st.prevHmnY;
                    if (absf(dx) < 15.0f && absf(dy) < 15.0f)
                    {
                        st.sdiDispX += dx;
                        st.sdiDispY += dy;
                    }
                }
            }
            if (st.sdiSequenceActive && !hmnInDamageLag)
            {
                float totalSdi = absf(st.sdiDispX) + absf(st.sdiDispY);
                float sepX = absf(in.hmnX - in.cpuX);
                float sepY = in.hmnY - in.cpuY;
                bool escapedNow = (in.hmnHitstun == 0 && in.hmnActionable)
                                  || (totalSdi >= 0.5f && (sepX > 16.0f || sepY > 14.0f));
                if (escapedNow)
                {
                    if (!st.sdiEscaped && (totalSdi >= 0.5f || st.sdiHitCount >= 1))
                    {
                        st.sdiEscaped = true;
                        st.completionCount++;
                    }
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kSdiEscapeGraceFrames)
                    {
                        wantReset = true;
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        st.havePrev = true;
        st.prevHmnStatus = in.hmnStatus;
        st.prevHmnHitlag = in.hmnHitlag;
        st.prevHmnAir = in.hmnAir;
        st.prevHmnX = in.hmnX;
        st.prevHmnY = in.hmnY;
        st.prevCpuStatus = in.cpuStatus;
        st.prevCpuHitlag = in.cpuHitlag;

        if (wantReset)
        {
            if ((resetHmn && isDeadOrStandby(in.hmnStatus)) || (resetCpu && isDeadOrStandby(in.cpuStatus)))
            {
                st.pendingReset = true;
            }
            else
            {
                triggerWarp(st, cfg, in.cpuFtKind, in.geom, act, resetHmn, resetCpu);
            }
        }
    }

    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out)
    {
        out.active = false;
        out.buttons = 0;
        out.stickX = 0.0f;
        out.stickY = 0.0f;

        if (in.preset == kScenarioOff || in.hmnOutOfPlay || in.cpuOutOfPlay)
        {
            return;
        }

        float dx = in.hmnX - in.cpuX;
        float toward = dx >= 0.0f ? 1.0f : -1.0f;
        float absDx = absf(dx);

        if (in.preset == kScenarioMultiHitSdi)
        {
            u8 move = resolveMultiHitMove(in.multiHitMove, in.cpuFtKind);
            if (st.lastMove != move)
            {
                scenarioCpuStateReset(st);
                st.lastMove = move;
            }
            out.active = true;
            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }

            switch (move)
            {
            case kMultiHitNair:
            case kMultiHitFair:
            case kMultiHitUpAir:
            case kMultiHitDownAir:
            {
                // Gentle takeoff stick + velocity-damped airborne steering so
                // CPU lands right on HMN's live X without overshooting.
                float takeoffDiv = move == kMultiHitUpAir ? 14.0f : (move == kMultiHitFair ? 11.0f : 9.0f);
                float approachDist = move == kMultiHitFair ? 10.5f : 8.5f;
                float takeoffSteerX = geomClamp(dx / takeoffDiv, -1.0f, 1.0f);
                float airSteerX = geomClamp((dx - in.cpuVx * 8.0f) / 3.0f, -1.0f, 1.0f);
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > approachDist || (in.hmnAir && in.hmnY > in.cpuY + 18.0f))
                        {
                            out.stickX = geomClamp(dx / 5.0f, -0.55f, 0.55f);
                            return;
                        }
                        if (move == kMultiHitFair && in.cpuLr * toward < 0.0f)
                        {
                            out.stickX = toward * 0.45f;
                            return;
                        }
                        out.buttons = cpu::button::kJump;
                        out.stickX = takeoffSteerX;
                        st.phase = 1;
                        st.timer = 0;
                    }
                }
                else if (st.phase == 1)
                {
                    st.timer++;
                    if (in.cpuAir)
                    {
                        if (move == kMultiHitDownAir)
                        {
                            out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                            out.stickX = 0.0f;
                            out.stickY = -1.0f;
                            st.phase = 3;
                            st.timer = 0;
                        }
                        else if (move == kMultiHitNair)
                        {
                            out.buttons = cpu::button::kAttack;
                            out.stickX = 0.0f;
                            out.stickY = 0.0f;
                            st.phase = 3;
                            st.timer = 0;
                        }
                        else if (move == kMultiHitFair)
                        {
                            out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                            out.stickX = in.cpuLr;
                            out.stickY = 0.0f;
                            st.phase = 3;
                            st.timer = 0;
                        }
                        else
                        {
                            out.stickX = airSteerX;
                            st.phase = 2;
                            st.timer = 1;
                        }
                    }
                    else if (st.timer >= 12)
                    {
                        st.phase = 0;
                        st.timer = 0;
                    }
                    else
                    {
                        out.stickX = takeoffSteerX;
                    }
                }
                else if (st.phase == 2)
                {
                    if (!in.cpuAir)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        return;
                    }
                    st.timer++;
                    bool readyForUpAir =
                        in.hmnAir ? (st.timer >= kUpAirAirborneDelay)
                                  : ((st.timer >= kUpAirMinFrames && in.cpuVy <= 0.25f && in.cpuY <= in.hmnY + 12.0f)
                                     || st.timer >= kUpAirMaxFrames);
                    if (readyForUpAir)
                    {
                        out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                        out.stickX = 0.0f;
                        out.stickY = 1.0f;
                        st.phase = 3;
                        st.timer = 0;
                    }
                    else
                    {
                        out.stickX = airSteerX;
                    }
                }
                else
                {
                    st.timer++;
                    if (!in.cpuAir || st.timer >= 30)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 10;
                    }
                    else
                    {
                        out.stickX = airSteerX;
                    }
                }
                return;
            }

            case kMultiHitUpB:
            {
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > 8.0f)
                        {
                            out.stickX = toward * 0.60f;
                            return;
                        }
                        if (in.cpuLr * toward < 0.0f)
                        {
                            out.stickX = toward * 0.45f;
                            return;
                        }
                        out.buttons = cpu::button::kSpecial;
                        out.stickX = 0.0f;
                        out.stickY = 1.0f;
                        st.phase = 1;
                        st.timer = 0;
                    }
                }
                else
                {
                    st.timer++;
                    out.stickX = geomClamp(dx / 6.0f, -0.45f, 0.45f);
                    if (st.timer >= 14)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 26;
                    }
                }
                return;
            }

            case kMultiHitSideB:
            {
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > 14.0f)
                        {
                            out.stickX = toward * 0.60f;
                            return;
                        }
                        out.buttons = cpu::button::kSpecial;
                        out.stickX = toward;
                        out.stickY = 0.0f;
                        st.phase = 1;
                        st.timer = 0;
                    }
                }
                else
                {
                    st.timer++;
                    out.stickX = toward;
                    if (st.timer >= 12)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 22;
                    }
                }
                return;
            }

            case kMultiHitDownB:
            {
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > 8.0f)
                        {
                            out.stickX = toward * 0.60f;
                            return;
                        }
                        out.buttons = cpu::button::kSpecial;
                        out.stickX = 0.0f;
                        out.stickY = -1.0f;
                        st.phase = 1;
                        st.timer = 1;
                    }
                }
                else
                {
                    st.timer++;
                    out.stickX = geomClamp(dx / 5.0f, -0.55f, 0.55f);
                    if ((st.timer & 1) == 0 && (in.lastButtons & cpu::button::kSpecial) == 0)
                    {
                        out.buttons = cpu::button::kSpecial;
                    }
                    if (st.timer >= 16)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 24;
                    }
                }
                return;
            }

            case kMultiHitRapidJab:
            default:
            {
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > 8.5f)
                        {
                            out.stickX = toward * 0.65f;
                            return;
                        }
                        if (in.cpuLr * toward < 0.0f)
                        {
                            out.stickX = toward * 0.45f;
                            return;
                        }
                        out.buttons = cpu::button::kAttack;
                        st.phase = 1;
                        st.timer = 1;
                    }
                }
                else
                {
                    st.timer++;
                    if ((st.timer & 1) == 0 && (in.lastButtons & cpu::button::kAttack) == 0)
                    {
                        out.buttons = cpu::button::kAttack;
                    }
                    if (st.timer >= 24)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 22;
                    }
                }
                return;
            }
            }
        }

        if (in.preset == kScenarioLedgetech)
        {
            u8 atk = in.ledgetechAttack < 7 ? in.ledgetechAttack : static_cast<u8>(kLtAtkDtilt);
            // Finish airborne Down Air phase cleanly before cooldown.
            if (atk == kLtAtkDair && st.phase > 0)
            {
                out.active = true;
                st.timer++;
                if (st.phase == 1)
                {
                    if (in.cpuAir)
                    {
                        out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                        out.stickX = 0.0f;
                        out.stickY = -1.0f;
                        st.phase = 2;
                        st.timer = 0;
                    }
                    else if (st.timer >= 10)
                    {
                        st.phase = 0;
                        st.timer = 0;
                    }
                }
                else
                {
                    // Drift slightly back onstage while swinging Dair at the ledge.
                    out.stickX = -toward * 0.35f;
                    if (!in.cpuAir || st.timer >= 24)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 20;
                    }
                }
                return;
            }

            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }
            float maxX = (atk == kLtAtkNeutralB || atk == kLtAtkSideB) ? 16.0f : 12.4f;
            bool inLedgeWindow = absDx <= maxX && in.hmnY >= in.cpuY - 18.0f && in.hmnY <= in.cpuY + 20.0f;
            if (!in.cpuAir && in.cpuCanAct && inLedgeWindow && in.hmnHitstun == 0)
            {
                out.active = true;
                if (in.cpuLr * toward < 0.0f)
                {
                    out.stickX = toward * 0.45f;
                    return;
                }
                switch (atk)
                {
                case kLtAtkFtilt:
                    out.buttons = cpu::button::kAttack;
                    out.stickX = toward * 0.55f;
                    st.cooldown = 18;
                    break;
                case kLtAtkFsmash:
                    out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                    out.stickX = toward;
                    st.cooldown = 28;
                    break;
                case kLtAtkDsmash:
                    out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                    out.stickY = -1.0f;
                    st.cooldown = 26;
                    break;
                case kLtAtkDair:
                    out.buttons = cpu::button::kJump;
                    out.stickX = toward * 0.20f;
                    st.phase = 1;
                    st.timer = 0;
                    break;
                case kLtAtkNeutralB:
                    out.buttons = cpu::button::kSpecial;
                    st.cooldown = 24;
                    break;
                case kLtAtkSideB:
                    out.buttons = cpu::button::kSpecial;
                    out.stickX = toward;
                    st.cooldown = 28;
                    break;
                case kLtAtkDtilt:
                default:
                    out.buttons = cpu::button::kAttack;
                    out.stickX = 0.0f;
                    out.stickY = -0.55f;
                    st.cooldown = 16;
                    break;
                }
            }
            return;
        }

        if (in.preset == kScenarioSlideOff)
        {
            // Chain multi-hit ftilts (e.g. Meta Knight Attack_S3 = 0x27) into
            // their launching finisher while leaving single-hit ftilts untouched.
            if (!in.cpuAir && in.cpuStatus == kStAttackS3 && (in.lastButtons & cpu::button::kAttack) == 0)
            {
                out.active = true;
                out.buttons = cpu::button::kAttack;
                out.stickX = toward * 0.55f;
                out.stickY = 0.0f;
                return;
            }
            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }
            if (!in.cpuAir && !in.hmnAir && in.cpuCanAct && absDx < 22.0f && in.hmnHitstun == 0)
            {
                out.active = true;
                out.buttons = cpu::button::kAttack;
                out.stickX = toward * 0.55f; // ftilt across edge
                out.stickY = 0.0f;
                st.cooldown = 38;
            }
            return;
        }

        if (in.preset == kScenarioGrabEscape && in.grabAction != kGrabActStand)
        {
            out.active = true;
            bool holdingHmn = isCaptureHoldStatus(in.hmnStatus) || in.cpuStatus == kStCatchPull
                              || in.cpuStatus == kStCatchDashPull || in.cpuStatus == kStCatchWait;
            if (holdingHmn)
            {
                if (st.phase != 2)
                {
                    st.phase = 2;
                    st.timer = 0;
                    if (in.grabAction == kGrabActRandThrow)
                    {
                        st.resolvedThrow = static_cast<u8>(kGrabActFThrow + (in.rng & 3u));
                    }
                    else
                    {
                        st.resolvedThrow = in.grabAction;
                    }
                }
                else if (st.timer < 255)
                {
                    st.timer++;
                }
                if (in.grabAction != kGrabActRandThrow)
                {
                    st.resolvedThrow = in.grabAction;
                }
                // Hold (Mash Out): zero pummel inputs so P1 practices clean mash-outs.
                if (in.grabAction == kGrabActHold)
                {
                    return;
                }
                // Throw DI: execute chosen throw once CatchWait settles.
                if (in.cpuStatus == kStCatchWait && st.timer >= 2)
                {
                    switch (st.resolvedThrow)
                    {
                    case kGrabActFThrow:
                        out.stickX = in.cpuLr;
                        break;
                    case kGrabActBThrow:
                        out.stickX = -in.cpuLr;
                        break;
                    case kGrabActUThrow:
                        out.stickY = 1.0f;
                        break;
                    case kGrabActDThrow:
                    default:
                        out.stickY = -1.0f;
                        break;
                    }
                }
                return;
            }

            // Released or thrown: wait cleanly for scenario auto-reset.
            if (st.phase == 2)
            {
                st.phase = 3;
                st.cooldown = 40;
                return;
            }
            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }
            // Randomized 40..99f pre-grab delay so P1 cannot blindly pre-mash.
            if (st.phase == 0)
            {
                if (st.delay == 0)
                {
                    st.delay = static_cast<u8>(40u + (in.rng % 60u));
                    st.timer = 0;
                }
                st.timer++;
                if (st.timer < st.delay)
                {
                    return;
                }
                st.phase = 1;
                st.timer = 0;
            }
            if (!in.cpuAir && in.cpuCanAct)
            {
                if (absDx > 8.5f)
                {
                    out.stickX = toward * 0.65f;
                    return;
                }
                if (in.cpuLr * toward < 0.0f)
                {
                    out.stickX = toward * 0.45f;
                    return;
                }
                out.buttons = cpu::button::kAttack | cpu::button::kGuard;
                st.cooldown = 20;
            }
            return;
        }
    }

    void stepPowershieldDrillCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out)
    {
        out.active = false;
        out.buttons = 0;
        out.stickX = 0.0f;
        out.stickY = 0.0f;

        if (in.hmnOutOfPlay || in.cpuOutOfPlay)
        {
            return;
        }
        out.active = true;
        if (st.cooldown > 0)
        {
            st.cooldown--;
            return;
        }
        if (in.cpuAir || !in.cpuCanAct)
        {
            return;
        }
        float dx = in.hmnX - in.cpuX;
        float toward = dx >= 0.0f ? 1.0f : -1.0f;
        float absDx = absf(dx);
        if (absDx > 11.0f)
        {
            out.stickX = toward * 0.55f;
            return;
        }
        if (in.cpuLr * toward < 0.0f)
        {
            out.stickX = toward * 0.35f;
            return;
        }
        out.buttons = cpu::button::kAttack;
        st.cooldown = 52;
    }
}
}
