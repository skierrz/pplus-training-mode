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
            kStSquat = 0x11,
            kStSquatWait = 0x12,
            kStGuardOn = 0x1A,
            kStGuard = 0x1B,
            kStGuardOff = 0x1C,
            kStGuardDamage = 0x1D,
            kStAttackFirst = 0x24,
            kStAttackS3 = 0x27,
            kStAttackAir = 0x33,
            kStCatch = 0x34,
            kStThrow = 0x3C,
            kStCapturePulled = 0x3D,
            kStCaptureWait = 0x3E,
            kStCaptureDamage = 0x3F,
            kStCaptureCut = 0x40,
            kStCaptureJump = 0x41,
            kStCaptureThrown = 0x42,
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
            kReversalGraceFrames = 24,
            kShieldDropGraceFrames = 26,
            kLedgetechGraceFrames = 30,
            kSlideOffGraceFrames = 28,
            kGrabGraceFrames = 28,
            kSdiEscapeGraceFrames = 26,
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

        bool isCaptureHoldStatus(int s)
        {
            return s >= kStCapturePulled && s <= kStCaptureDamage;
        }

        bool isCaptureEscapeStatus(int s)
        {
            return s == kStCaptureCut || s == kStCaptureJump;
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

        void applyPresetOverrides(u8 preset, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                  main::GeneralSettings* generalCfg)
        {
            switch (preset)
            {
            case kScenarioComboLab:
                comboCfg.enabled = 1;
                comboCfg.fixPosition = 1;
                comboCfg.noclip = 1;
                comboCfg.comboTimerAdjustment = 0;
                comboCfg.recoveryTrainer = 0;
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
                break;

            case kScenarioShieldPressure:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorShield;
                cpuOpts.infiniteShields = cpu::kShieldsOn;
                cpuOpts.shieldHealth = cpu::kShieldFull;
                cpuOpts.counterShield = kCounterShieldGrab;
                break;

            case kScenarioOosReversal:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorShield;
                cpuOpts.infiniteShields = cpu::kShieldsUntilHit;
                cpuOpts.shieldHealth = cpu::kShieldFull;
                cpuOpts.counterShield = kCounterShieldUpB;
                cpuOpts.counterGround = kCounterGroundJab;
                cpuOpts.counterAir = kCounterAirJumpNeutral;
                break;

            case kScenarioShieldDrop:
                comboCfg.enabled = 0;
                comboCfg.recoveryTrainer = 0;
                cpuOpts.control = cpu::kControlLab;
                cpuOpts.behavior = cpu::kBehaviorStand;
                cpuOpts.tdi = cpu::kTdiNone;
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
                cpuOpts.grabEscape = cpu::kMashHigh;
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
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

        void clearCycleTrackers(ScenarioState& st)
        {
            st.havePrev = false;
            st.hmnGuardWindow = 0;
            st.techEngaged = false;
            st.shieldEngaged = false;
            st.reversalEngaged = false;
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

    ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw)
    {
        ScenarioConfig c;
        c.preset = raw.preset < kScenarioCount ? raw.preset : static_cast<u8>(kScenarioOff);
        c.autoReset = raw.autoReset != 0 ? 1 : 0;
        c.side = raw.side < kScenarioSideCount ? raw.side : static_cast<u8>(kScenarioSideRight);
        c.multiHitMove = raw.multiHitMove < kMultiHitMoveCount ? raw.multiHitMove : static_cast<u8>(kMultiHitUpAir);
        return c;
    }

    void scenarioStateInit(ScenarioState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastAutoReset = 1;
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

    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg)
    {
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        bool presetChanged = cfg.preset != st.activePreset;
        bool layoutChanged = cfg.side != st.lastSide || cfg.multiHitMove != st.lastMultiHitMove;
        st.lastAutoReset = cfg.autoReset;
        st.lastSide = cfg.side;
        st.lastMultiHitMove = cfg.multiHitMove;

        if (!presetChanged)
        {
            if (cfg.preset != kScenarioOff && layoutChanged)
            {
                st.pendingReset = true;
            }
            return layoutChanged && cfg.preset != kScenarioOff;
        }

        if (cfg.preset == kScenarioOff)
        {
            restoreScenarioSnapshot(st, cpuOpts, comboCfg, generalCfg);
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
                st.snapshotPlayerValid = true;
            }
            else
            {
                st.snapshotPlayerValid = false;
            }
            st.snapshotActive = true;
        }
        else
        {
            // Preset A -> Preset B: always restore original snapshot first so
            // no overrides from Preset A leak into Preset B.
            cpuOpts = st.snapshotCpu;
            comboCfg = st.snapshotCombo;
            if (st.snapshotPlayerValid && generalCfg != NULL)
            {
                generalCfg->playerPercent = st.snapshotPlayerPercent;
                generalCfg->lockPlayerPercent = st.snapshotLockPlayerPercent;
            }
        }

        applyPresetOverrides(cfg.preset, cpuOpts, comboCfg, generalCfg);
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
                                 main::GeneralSettings* generalCfg)
    {
        if (st.snapshotActive)
        {
            cpuOpts = st.snapshotCpu;
            comboCfg = st.snapshotCombo;
            if (st.snapshotPlayerValid && generalCfg != NULL)
            {
                generalCfg->playerPercent = st.snapshotPlayerPercent;
                generalCfg->lockPlayerPercent = st.snapshotLockPlayerPercent;
            }
            st.snapshotPlayerValid = false;
            st.snapshotActive = false;
        }
    }

    void discardScenarioSnapshot(ScenarioState& st)
    {
        st.snapshotActive = false;
        st.snapshotPlayerValid = false;
        st.activePreset = kScenarioOff;
        st.pendingReset = false;
        st.hasPlatform = false;
        clearCycleTrackers(st);
    }

    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg)
    {
        if (st.snapshotActive)
        {
            cpuOpts = st.snapshotCpu;
            comboCfg = st.snapshotCombo;
            if (st.snapshotPlayerValid && generalCfg != NULL)
            {
                generalCfg->playerPercent = st.snapshotPlayerPercent;
                generalCfg->lockPlayerPercent = st.snapshotLockPlayerPercent;
            }
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

    void computeScenarioSpawns(u8 preset, u8 side, u8 multiHitMove, const StageGeometry& geom, ScenarioSpawns& out)
    {
        memset(&out, 0, sizeof(out));
        if (!geom.floor.valid)
        {
            return;
        }
        bool leftSide = side == kScenarioSideLeft;
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

        switch (preset)
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

        case kScenarioOosReversal:
        {
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
            if (selectPlatform(geom, leftSide, plat))
            {
                out.hasPlatform = true;
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                float inward = platMid >= centerX ? -1.0f : 1.0f;
                out.hmnX = platMid;
                out.hmnY = plat.y;
                out.hmnLr = inward;
                out.cpuX = geomClamp(platMid + inward * 4.0f, floorLeft + 10.0f, floorRight - 10.0f);
                out.cpuY = floorY;
                out.cpuLr = platMid >= out.cpuX ? 1.0f : -1.0f;
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
            out.hmnX = ledge.x + outward * 8.0f;
            float loY = geom.blast.valid ? (geom.blast.bottom + 36.0f) : (ledge.y - 36.0f);
            out.hmnY = geomMax(loY, ledge.y - 20.0f);
            out.hmnLr = inward;
            out.hmnFalling = true;

            // Place CPU right at the ledge lip so dtilt pokes past the edge
            // across short- and long-reach fighters alike.
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
            float spacing = 5.5f; // kMultiHitUpAir
            if (multiHitMove == kMultiHitDownAir)
            {
                spacing = 5.0f;
            }
            else if (multiHitMove == kMultiHitRapidJab)
            {
                spacing = 7.0f;
            }
            else if (multiHitMove == kMultiHitSideB)
            {
                spacing = 10.5f;
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

    static void triggerWarp(ScenarioState& st, const ScenarioConfig& cfg, const StageGeometry& geom,
                            ScenarioStepAction& act, bool warpHmn, bool warpCpu)
    {
        ScenarioSpawns sp;
        computeScenarioSpawns(cfg.preset, cfg.side, cfg.multiHitMove, geom, sp);
        if (!sp.valid)
        {
            return;
        }
        st.hasPlatform = sp.hasPlatform;
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
            st.restoreCorrect[0] = true;
            st.restoreCorrectKind[0] = sp.hmnFalling ? kCorrectAir : kCorrectGroundCliffStop;
            st.lastCorrectWritten[0] = kCorrectNone;
        }
        if (warpCpu)
        {
            act.setNoclip[1] = true;
            st.restoreCorrect[1] = true;
            st.restoreCorrectKind[1] = sp.cpuFalling ? kCorrectAir : kCorrectGroundCliffStop;
            st.lastCorrectWritten[1] = kCorrectNone;
        }
    }

    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* generalCfg)
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
        ScenarioConfig cfg = sanitizeScenarioConfig(rawCfg);
        applyScenarioConfig(st, cfg, cpuOpts, comboCfg, generalCfg);
        if (generalCfg != NULL && generalCfg->playerPercent != prevPct)
        {
            act.applyHmnPercent = true;
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
            triggerWarp(st, cfg, in.geom, act, true, true);
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
                if (in.cpuHitstun > 0 || in.cpuHitlag > 0 || isCaptureHoldStatus(in.cpuStatus))
                {
                    st.graceFrames = 0;
                }
                else if (isNeutralGrounded(in.cpuStatus, in.cpuAir, in.cpuHitstun, in.cpuHitlag)
                         || (!in.cpuAir && in.cpuActionable))
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
            else if (st.shieldEngaged && !isGuardStatus(in.cpuStatus) && !isAttackOrSpecial(in.cpuStatus)
                     && in.cpuHitstun == 0 && in.cpuHitlag == 0)
            {
                st.graceFrames++;
                if (autoReset && st.graceFrames >= kShieldGraceFrames)
                {
                    wantReset = true;
                }
            }
            else if (st.shieldEngaged && in.cpuStatus == kStGuardOff)
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
            break;
        }

        case kScenarioOosReversal:
        {
            if (autoReset && (hmnDanger || cpuDanger))
            {
                wantReset = true;
                break;
            }
            bool cpuPressured = in.cpuStatus == kStGuardDamage || in.cpuShieldStun > 0 || in.cpuHitstun > 0;
            if (cpuPressured)
            {
                st.reversalEngaged = true;
                st.graceFrames = 0;
            }
            else if (st.reversalEngaged)
            {
                bool activeExchange = in.hmnHitstun > 0 || in.hmnHitlag > 0 || in.cpuHitlag > 0
                                      || isAttackOrSpecial(in.cpuStatus) || isAttackOrSpecial(in.hmnStatus);
                if (activeExchange)
                {
                    st.graceFrames = 0;
                }
                else if (!in.cpuAir && !in.hmnAir && in.cpuActionable && in.hmnActionable)
                {
                    if (st.graceFrames == 0)
                    {
                        st.completionCount++;
                    }
                    st.graceFrames++;
                    if (autoReset && st.graceFrames >= kReversalGraceFrames)
                    {
                        wantReset = true;
                    }
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
                // Left capture directly without CaptureCut/Thrown.
                st.grabActive = false;
                st.grabDone = true;
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
                triggerWarp(st, cfg, in.geom, act, resetHmn, resetCpu);
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
            if (st.lastMove != in.multiHitMove)
            {
                scenarioCpuStateReset(st);
                st.lastMove = in.multiHitMove;
            }
            out.active = true;
            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }

            switch (in.multiHitMove)
            {
            case kMultiHitUpAir:
            case kMultiHitDownAir:
            {
                // Gentle takeoff stick + velocity-damped airborne steering so
                // CPU lands right on HMN's live X without overshooting.
                float takeoffDiv = in.multiHitMove == kMultiHitUpAir ? 14.0f : 9.0f;
                float takeoffSteerX = geomClamp(dx / takeoffDiv, -1.0f, 1.0f);
                float airSteerX = geomClamp((dx - in.cpuVx * 8.0f) / 3.0f, -1.0f, 1.0f);
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        // Walk smoothly beneath HMN while they are above
                        // short-hop reach (stay below dash threshold to avoid
                        // slingshot jumps).
                        if (absDx > 8.5f || (in.hmnAir && in.hmnY > in.cpuY + 18.0f))
                        {
                            out.stickX = geomClamp(dx / 5.0f, -0.55f, 0.55f);
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
                        if (in.multiHitMove == kMultiHitDownAir)
                        {
                            out.buttons = cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick;
                            out.stickX = 0.0f;
                            out.stickY = -1.0f;
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
                    // Wait for falling arc near HMN height so both fastfallers
                    // and floaties hit grounded hurtboxes instead of swinging
                    // over HMN's head at jump peak.
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

            case kMultiHitSideB:
            {
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
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
                    if (st.timer >= 10)
                    {
                        st.phase = 0;
                        st.timer = 0;
                        st.cooldown = 20;
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
                        // Turn toward HMN if facing away before starting rapid jab.
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
            if (st.cooldown > 0)
            {
                st.cooldown--;
                return;
            }
            // Wait until HMN begins recovering toward the ledge from spawn
            // (12.5u out, -20u low) so dtilt lands right at the ledge lip and
            // re-triggers cleanly if HMN lingers or recovers slowly.
            bool inLedgeWindow = absDx <= 12.4f && in.hmnY >= in.cpuY - 18.0f && in.hmnY <= in.cpuY + 12.0f;
            if (!in.cpuAir && in.cpuCanAct && inLedgeWindow && in.hmnHitstun == 0)
            {
                out.active = true;
                if (in.cpuLr * toward < 0.0f)
                {
                    out.stickX = toward * 0.45f;
                    return;
                }
                out.buttons = cpu::button::kAttack;
                out.stickX = 0.0f;
                out.stickY = -0.55f; // low dtilt / ledge poke
                st.cooldown = 16;
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
        // Walk smoothly into universal jab range without dashing offstage.
        if (absDx > 11.0f)
        {
            out.stickX = toward * 0.55f;
            return;
        }
        // Ensure CPU faces P1 before throwing the poke.
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
