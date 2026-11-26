#include "extra/scenario_presets_core.h"

#include <string.h>

#include "extra/net_alloc.h"
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

#pragma dont_inline on
        bool isGuardStatus(int s)
        {
            return s >= kStGuardOn && s <= kStGuardDamage;
        }

        bool isTumbleStatus(int s)
        {
            return s >= kStDamageFly && s <= kStDamageFall;
        }

        bool isDeadOrStandby(int s)
        {
            return s == kStDead || s == kStStandby;
        }

        bool isDownOrTechStatus(int s)
        {
            return (s >= kStDownFirst && s <= kStDownLast) || s == kStPassive || s == kStPassiveFb
                   || (s >= kStSlipFirst && s <= kStSlipLast);
        }

        bool isCaptureHoldStatus(int s)
        {
            return (s >= kStCapturePulled && s <= kStCaptureDamage)
                   || (s >= kStCapturePulledYoshi && s <= kStCaptureDamageYoshi);
        }

        bool isAttackOrSpecial(int s)
        {
            return (s >= kStAttackFirst && s <= kStThrow) || s >= kStSpecialFirst;
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
                lowY = maxf(lowY, geom.floor.centerY - 95.0f);
            }
            if (y < lowY || x < geom.blast.left + 20.0f || x > geom.blast.right - 20.0f)
            {
                return true;
            }
            return inTumble && y > geom.blast.top - 20.0f;
        }

        void clearEventTelemetry(ScenarioState& st)
        {
            const char* start = reinterpret_cast<const char*>(&st.sdiHitCount);
            const char* end = reinterpret_cast<const char*>(&st.havePrev);
            memset(&st.sdiHitCount, 0, static_cast<size_t>(end - start));
        }
#pragma dont_inline reset

        inline bool isShieldBreakStatus(int s)
        {
            return s >= kStShieldBreakFly && s <= kStFuraFuraEnd;
        }

        inline bool isAirEscapeStatus(int s)
        {
            return (s >= kStJump && s <= kStFallAerial) || s == kStEscapeAir;
        }

        inline bool isNeutralGrounded(int s, bool air, int hitstun, int hitlag)
        {
            if (air || hitstun > 0 || hitlag > 0)
            {
                return false;
            }
            return s <= kStWalkBrake || s == kStSquat || s == kStSquatWait || s == kStGuardOn || s == kStGuard;
        }

        inline bool isCaptureEscapeStatus(int s)
        {
            return s == kStCaptureCut || s == kStCaptureJump || s == kStCaptureCutYoshi;
        }

        inline bool isWallTechStatus(int s)
        {
            return s == kStPassiveWall || s == kStPassiveWallJump;
        }

        inline bool isLedgeStatus(int s)
        {
            return s == kStCliffCatch || s == kStCliffWait;
        }

#pragma dont_inline on
        void transferSnapshot(ScenarioState& st, main::Settings& s, bool toSettings)
        {
            if (toSettings)
            {
                memcpy(&s.cpu.options, &st.snapshotCpu, sizeof(s.cpu.options));
                memcpy(&s.comboTrainer, &st.snapshotCombo, sizeof(s.comboTrainer));
                s.general.playerPercent = st.snapshotPlayerPercent;
                s.general.lockPlayerPercent = st.snapshotLockPlayerPercent;
                s.general.playerInfiniteShields = st.snapshotPlayerInfShields;
                s.cpu.percent = st.snapshotCpuPercent;
                s.cpu.lockPercent = st.snapshotLockCpuPercent;
                s.osd.grabBreakout = st.snapshotGrabBreakoutOsd;
                memcpy(&s.techInvis, &st.snapshotTechInvis, sizeof(s.techInvis));
            }
            else
            {
                memcpy(&st.snapshotCpu, &s.cpu.options, sizeof(s.cpu.options));
                memcpy(&st.snapshotCombo, &s.comboTrainer, sizeof(s.comboTrainer));
                st.snapshotPlayerPercent = s.general.playerPercent;
                st.snapshotLockPlayerPercent = s.general.lockPlayerPercent;
                st.snapshotPlayerInfShields = s.general.playerInfiniteShields;
                st.snapshotCpuPercent = s.cpu.percent;
                st.snapshotLockCpuPercent = s.cpu.lockPercent;
                st.snapshotGrabBreakoutOsd = s.osd.grabBreakout;
                memcpy(&st.snapshotTechInvis, &s.techInvis, sizeof(s.techInvis));
            }
        }

        void copySnapshotTo(const ScenarioState& st, main::Settings& s)
        {
            transferSnapshot(const_cast<ScenarioState&>(st), s, true);
        }

        void raiseMinPct(u16& pct, u8& lock, u16 minV, u16 targetV)
        {
            if (pct < minV)
            {
                pct = targetV;
                lock = 1;
            }
        }

        void applyPresetOverrides(const ScenarioConfig& cfg, main::Settings& s)
        {
            if (cfg.preset == kScenarioOff || cfg.preset >= kScenarioCount)
            {
                return;
            }
            cpu::CpuOptions& cpuOpts = s.cpu.options;
            main::ComboTrainerSettings& comboCfg = s.comboTrainer;
            comboCfg.enabled = 0;
            comboCfg.recoveryTrainer = 0;
            cpuOpts.control = cpu::kControlLab;
            cpuOpts.behavior = cpu::kBehaviorStand;
            switch (cfg.preset)
            {
            case kScenarioComboLab:
                comboCfg.enabled = 1;
                comboCfg.fixPosition = 1;
                comboCfg.noclip = 0;
                comboCfg.comboTimerAdjustment = 0;
                if (comboCfg.enabledMeters == 0)
                {
                    comboCfg.enabledMeters = main::kMeterHitstun | main::kMeterDi;
                }
                cpuOpts.tdi = cpu::kTdiRandom;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.tech = cpu::kTechRandom;
                break;

            case kScenarioTechchase:
                cpuOpts.tech = cpu::kTechRandom;
                cpuOpts.getup = cpu::kGetupRandom;
                cpuOpts.tdi = resolveTechTdi(cfg.techTdi);
                cpuOpts.techSound = cfg.techSound != 0 ? 1 : 0;
                cpuOpts.counterGround = resolveTechEscapeCounter(cfg.techEscape);
                s.techInvis.mode = cfg.techInvisMode;
                s.techInvis.delay = cfg.techInvisDelay;
                s.techInvis.preTechDrift = cfg.techPreDrift;
                break;

            case kScenarioEdgeguard:
                comboCfg.enabled = 1;
                comboCfg.recoveryTrainer = 1;
                comboCfg.noclip = 1;
                comboCfg.fixPosition = 0;
                comboCfg.recoveryMixup = cfg.edgeguardMixup;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.tdi = cpu::kTdiSurvival;
                raiseMinPct(s.cpu.percent, s.cpu.lockPercent, kEdgeguardMinCpuPercent, kEdgeguardTargetCpuPercent);
                break;

            case kScenarioShieldPressure:
                if (cfg.shieldRole == kShieldRoleDefense)
                {
                    cpuOpts.counterGround = 0;
                    cpuOpts.counterAir = 0;
                    cpuOpts.counterShield = 0;
                    cpuOpts.autoLCancel = 1;
                    s.general.playerInfiniteShields = cpu::kShieldsOn;
                }
                else
                {
                    cpuOpts.behavior = cpu::kBehaviorShield;
                    cpuOpts.infiniteShields = cpu::kShieldsOn;
                    cpuOpts.shieldHealth = cpu::kShieldFull;
                    cpuOpts.counterShield = resolveOosCounterShield(cfg.oosAction);
                    cpuOpts.counterGround = kCounterGroundJab;
                    cpuOpts.counterAir = kCounterAirJumpNeutral;
                }
                break;

            case kScenarioShieldDrop:
                cpuOpts.tdi = cpu::kTdiNone;
                if (cfg.shieldDropAttack != kSdAtkOff) cpuOpts.autoLCancel = 1;
                s.general.playerInfiniteShields = cpu::kShieldsOn;
                break;

            case kScenarioLedgetech:
            case kScenarioSlideOff:
            case kScenarioGrabEscape:
            case kScenarioMultiHitSdi:
                cpuOpts.counterGround = 0;
                cpuOpts.counterAir = 0;
                cpuOpts.counterShield = 0;
                if (cfg.preset == kScenarioLedgetech)
                {
                    cpuOpts.tech = cpu::kTechNone;
                    cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                    raiseMinPct(s.general.playerPercent, s.general.lockPlayerPercent, kLedgetechMinPercent,
                                kLedgetechTargetPercent);
                }
                else if (cfg.preset == kScenarioSlideOff)
                {
                    cpuOpts.tdi = cpu::kTdiNone;
                    raiseMinPct(s.general.playerPercent, s.general.lockPlayerPercent, kSlideOffMinPercent,
                                kSlideOffTargetPercent);
                }
                else if (cfg.preset == kScenarioGrabEscape)
                {
                    cpuOpts.grabEscape = cfg.grabAction == kGrabActStand ? cpu::kMashHigh : cpu::kMashNone;
                    s.osd.grabBreakout = 1;
                }
                else
                {
                    cpuOpts.tdi = cpu::kTdiNone;
                    cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                }
                break;

            default:
                break;
            }
        }
#pragma dont_inline reset

#pragma dont_inline on
        void clearCycleTrackers(ScenarioState& st)
        {
            const char* start = reinterpret_cast<const char*>(&st.havePrev);
            const char* end = reinterpret_cast<const char*>(&st + 1);
            memset(&st.havePrev, 0, static_cast<size_t>(end - start));
            st.sdiLatchedReset = true;
        }
#pragma dont_inline reset

        bool bytesDiffer(const void* a, const void* b, u32 n)
        {
            const u8* pa = static_cast<const u8*>(a);
            const u8* pb = static_cast<const u8*>(b);
            for (u32 i = 0; i < n; i++)
            {
                if (pa[i] != pb[i])
                {
                    return true;
                }
            }
            return false;
        }
    }

    u8 resolveOosCounterShield(u8 oosAction)
    {
        static const u8 kMap[kOosCount] = {
            kCounterShieldGrab,      kCounterShieldUpB,          kCounterShieldSpotdodge,
            kCounterShieldRollAway,  kCounterShieldShNair,       kCounterShieldWavedashAway,
            kCounterShieldRandom,    kCounterShieldCustomPool
        };
        return oosAction < kOosCount ? kMap[oosAction] : static_cast<u8>(kCounterShieldGrab);
    }

    u8 resolveTechEscapeCounter(u8 techEscape)
    {
        static const u8 kMap[kTechEscCount] = {
            0, kCounterGroundSpotdodge, kCounterGroundJab, kCounterGroundShortHop,
            kCounterGroundShield, kCounterGroundRollRandom, 0
        };
        return techEscape < kTechEscCount ? kMap[techEscape] : 0;
    }

    u8 resolveTechTdi(u8 techTdi)
    {
        static const u8 kMap[kTechTdiCount] = {
            cpu::kTdiRandom, cpu::kTdiNone, cpu::kTdiOutwards, cpu::kTdiInwards, cpu::kTdiSurvival
        };
        return techTdi < kTechTdiCount ? kMap[techTdi] : static_cast<u8>(cpu::kTdiRandom);
    }

    float ledgetechCpuInwardOffset(u8 atk)
    {
        static const u8 kOffsets10[7] = {45, 50, 60, 42, 38, 65, 65};
        return i2fScale(atk < 7 ? kOffsets10[atk] : 42, 0.1f);
    }

    u8 stepTechPosition(u8 currentPos, int platformCount, int delta)
    {
        int maxPos = platformCount > kTechPosMax ? kTechPosMax : (platformCount > 0 ? platformCount : 0);
        int total = maxPos + 1;
        int cur = currentPos <= maxPos ? static_cast<int>(currentPos) : 0;
        int next = (cur + delta) % total;
        if (next < 0)
        {
            next += total;
        }
        return static_cast<u8>(next);
    }

    void computeEdgeguardLaunch(u8 strengthMode, const LedgeInfo& ledge, float outward, u32& rng,
                                EdgeguardLaunchParams& out)
    {
        struct EggLaunchRow {
            u8 minSpd20, maxSpd20, minAng, maxAng, hitstun, hitlag;
            s8 lipOff, relY;
        };
        static const EggLaunchRow kRows[3] = {
            {53, 61, 26, 36, 24, 6, -4, 4},  // Weak (2.65..3.05)
            {67, 77, 22, 34, 34, 8, -2, 6},  // Normal (3.35..3.85)
            {83, 94, 18, 30, 44, 10, 2, 8}   // Strong (4.15..4.70)
        };
        u8 mode = strengthMode >= kEgStrRandom ? static_cast<u8>(geomNextRng(rng) % 3u) : strengthMode;
        const EggLaunchRow& r = kRows[mode];
        float speed = i2fScale(r.minSpd20, 0.05f) + geomRand01(rng) * i2fScale(r.maxSpd20 - r.minSpd20, 0.05f);
        float angleDeg = i2f(r.minAng) + geomRand01(rng) * i2f(r.maxAng - r.minAng);
        // Compact polynomial sin/cos on [18..36 deg] (zero libm dependency).
        float rad = angleDeg * 0.01745329f;
        float r2 = rad * rad;
        float sinA = rad * (1.0f - r2 * (0.1666667f - r2 * 0.0083333f));
        float cosA = 1.0f - r2 * (0.5f - r2 * 0.0416667f);
        out.startX = ledge.x + outward * i2f(r.lipOff);
        out.startY = ledge.y + i2f(r.relY);
        out.kbX = outward * speed * cosA;
        out.kbY = speed * sinA;
        out.speed = speed;
        out.angleDeg = angleDeg;
        out.hitstun = r.hitstun;
        out.hitlag = r.hitlag;
    }

    // Complete P+ 3.2.1 per-fighter multi-hit SDI table verified directly from
    // P+ 3.2.1 fighter Fit*.pac subactionMain scripts.
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

    TM_STATIC_ASSERT(sizeof(ScenarioConfig) == 36, tm_scenario_cfg_size);

    void sanitizeScenarioConfigInto(const ScenarioConfig& raw, ScenarioConfig& c)
    {
        static const u8 kLim[35] = {
            kScenarioCount, kScenarioSideCount, kScenarioSideCount, kMultiHitMoveCount, kOosCount,
            kLtSpawnCount, kLtDjCount, kLtAtkCount, kSdPlatCount, kSdSpaceCount,
            kSdFaceCount, kGrabActCount, kGrabPctCount, kTechSetupCount, kTechPosMax + 1,
            kTechEscCount, main::kTechInvisCount, 16, 2, main::kPreTechDriftCount,
            kTechTdiCount, 2, kEgSetupCount, kEgStrCount, kEgDistCount,
            kEgDjCount, main::kRecMixupCount, kLtStartCount, kLtTimeCount, kShieldRoleCount,
            kShieldDefCount, kSdAtkCount, kSlideAtkCount, kSlideStartCount, kEgResetCount
        };
        const u8* src = reinterpret_cast<const u8*>(&raw);
        u8* dst = reinterpret_cast<u8*>(&c);
        for (int i = 0; i < 35; i++)
        {
            dst[i] = src[i] < kLim[i] ? src[i] : 0;
        }
        c.autoReset = raw.autoReset != 0 ? 1 : 0;
        c.techSound = raw.techSound != 0 ? 1 : 0;
        c.techShowReaction = raw.techShowReaction != 0 ? 1 : 0;
        c.techInvisDelay = raw.techInvisDelay <= 15 ? raw.techInvisDelay : 15;
        if (raw.edgeguardStrength >= kEgStrCount)
        {
            c.edgeguardStrength = kEgStrNormal;
        }
        c.spacingOffset = raw.spacingOffset < -8 ? -8 : (raw.spacingOffset > 12 ? 12 : raw.spacingOffset);
    }

    void scenarioStateInit(ScenarioState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastCfg.autoReset = 1;
        st.rng = 0x51A7C0DEu;
        st.lastCorrectWritten[0] = 0xFF;
        st.lastCorrectWritten[1] = 0xFF;
        st.restoreCorrectKind[0] = kCorrectGroundCliffStop;
        st.restoreCorrectKind[1] = kCorrectGroundCliffStop;
        st.techReactionFrames = -1;
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

    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg, main::Settings& s)
    {
        if (st.activePreset == kScenarioOff)
        {
            return;
        }
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        cfg.preset = st.activePreset;
        if (st.snapshotActive)
        {
            copySnapshotTo(st, s);
        }
        applyPresetOverrides(cfg, s);
    }

    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, main::Settings& s)
    {
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        ScenarioConfig prev;
        memcpy(&prev, &st.lastCfg, sizeof(prev));
        memcpy(&st.lastCfg, &cfg, sizeof(cfg));

        if (cfg.preset == st.activePreset)
        {
            if (cfg.preset == kScenarioOff || !bytesDiffer(&prev, &cfg, 35))
            {
                return false;
            }
            bool techOptsChanged = bytesDiffer(&prev.techEscape, &cfg.techEscape, 6);
            // Non-layout fields: preset(0), autoReset(1), techEscape..techShowReaction(15..21),
            // edgeguardMixup(26), edgeguardReset(34).
            const u8* pb = reinterpret_cast<const u8*>(&prev);
            const u8* cb = reinterpret_cast<const u8*>(&cfg);
            static const u32 kNonLayoutBits = (1u << 0) | (1u << 1) | (0x7Fu << 15) | (1u << 26);
            bool layoutChanged = false;
            for (int i = 2; i < 35; i++)
            {
                bool isLayout = i < 32 ? (((kNonLayoutBits >> i) & 1u) == 0) : (i != 34);
                if (isLayout && pb[i] != cb[i]) { layoutChanged = true; break; }
            }
            bool edgeMixupChanged = cfg.edgeguardMixup != prev.edgeguardMixup;
            bool needReapply =
                (cfg.preset == kScenarioShieldPressure && (cfg.oosAction != prev.oosAction || cfg.shieldRole != prev.shieldRole))
                || (cfg.preset == kScenarioTechchase && techOptsChanged)
                || (cfg.preset == kScenarioShieldDrop && cfg.shieldDropAttack != prev.shieldDropAttack)
                || (cfg.preset == kScenarioEdgeguard && edgeMixupChanged)
                || (cfg.preset == kScenarioGrabEscape && (cfg.grabAction != prev.grabAction || cfg.grabPctMode != prev.grabPctMode));
            if (needReapply)
            {
                if (st.snapshotActive) copySnapshotTo(st, s);
                applyPresetOverrides(cfg, s);
            }
            if (layoutChanged) st.pendingReset = true;
            return layoutChanged || techOptsChanged || edgeMixupChanged;
        }

        if (cfg.preset == kScenarioOff)
        {
            restoreScenarioSnapshot(st, s);
            st.activePreset = kScenarioOff;
            st.pendingReset = false;
            st.hasPlatform = false;
            clearCycleTrackers(st);
            return true;
        }

        // Snapshot pre-preset settings ONLY on Off -> Preset transition.
        if (!st.snapshotActive)
        {
            transferSnapshot(st, s, false);
            st.snapshotActive = true;
        }
        else
        {
            copySnapshotTo(st, s);
        }

        applyPresetOverrides(cfg, s);
        st.activePreset = cfg.preset;
        st.pendingReset = true;
        clearEventTelemetry(st);
        st.grabEscapeFrames = 0;
        st.shieldHitCount = 0;
        st.techResult = kTechResNone;
        st.techDeltaFrames = 0;
        st.techReactionFrames = -1;
        st.techStreak = 0;
        clearCycleTrackers(st);
        return true;
    }

    void restoreScenarioSnapshot(ScenarioState& st, main::Settings& s)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, s);
            st.snapshotActive = false;
        }
    }

    void discardScenarioSnapshot(ScenarioState& st)
    {
        st.snapshotActive = false;
        st.activePreset = kScenarioOff;
        st.pendingReset = false;
        st.hasPlatform = false;
        clearCycleTrackers(st);
    }

    void projectScenarioSavedView(const ScenarioState& st, main::Settings& s)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, s);
        }
    }

    void requestScenarioManualReset(ScenarioState& st)
    {
        if (st.activePreset != kScenarioOff)
        {
            st.pendingReset = true;
            clearEventTelemetry(st);
            clearCycleTrackers(st);
        }
    }

    static inline void setSpawnPair(ScenarioSpawns& out, float hx, float hy, float hlr, float cx, float cy, float clr)
    {
        out.hmnX = hx;
        out.hmnY = hy;
        out.hmnLr = hlr;
        out.cpuX = cx;
        out.cpuY = cy;
        out.cpuLr = clr;
    }

#pragma dont_inline on
    static void setSymmetricFloor(ScenarioSpawns& out, float centerX, float floorY, float dir, float span)
    {
        setSpawnPair(out, centerX - dir * span, floorY, dir, centerX + dir * span, floorY, -dir);
    }

    static float clampFloorX(const StageGeometry& geom, float x, float pad)
    {
        return clampf(x, geom.floor.leftX + pad, geom.floor.rightX - pad);
    }

    static float ledgeMaxOut(const StageGeometry& geom, bool leftSide, float pad)
    {
        return leftSide ? (geom.leftLedge.x - geom.blast.left - pad) : (geom.blast.right - geom.rightLedge.x - pad);
    }

    static float clampBlastY(const StageGeometry& geom, float targetY, float botPad, float fbBot)
    {
        float loY = geom.blast.valid ? (geom.blast.bottom + botPad) : fbBot;
        float hiY = geom.blast.valid ? (geom.blast.top - 30.0f) : (fbBot + 74.0f);
        return clampf(targetY, loY, maxf(loY, hiY));
    }
#pragma dont_inline reset

    void computeScenarioSpawnsEx(const ScenarioConfig& rawCfg, int cpuFtKind, u32& rng, const StageGeometry& geom,
                                 ScenarioSpawns& out)
    {
        memset(&out, 0, sizeof(out));
        if (!geom.floor.valid)
        {
            return;
        }
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        u8 effSide = cfg.side == kScenarioSideRandom ? static_cast<u8>(geomNextRng(rng) & 1u) : cfg.side;
        bool leftSide = effSide == kScenarioSideLeft;
        float dir = leftSide ? -1.0f : 1.0f;
        float centerX = 0.5f * (geom.floor.leftX + geom.floor.rightX);
        float halfW = 0.5f * (geom.floor.rightX - geom.floor.leftX);
        float floorY = geom.floor.centerY;

        out.valid = true;
        out.resolvedPlatformSlot = kPlatSlotLeft;

        switch (cfg.preset)
        {
        case kScenarioComboLab:
            setSymmetricFloor(out, centerX, floorY, dir, minf(12.0f, halfW * 0.30f));
            break;

        case kScenarioTechchase:
        {
            StagePlatform sortedPlats[kMaxStagePlatforms];
            int nPlats = sortPlatformsLowestLeftToRight(geom, sortedPlats);
            bool tumbleDrop = cfg.techSetup == kTechSetupTumbleDrop;
            if (cfg.techPosition >= 1 && nPlats > 0)
            {
                int idx = static_cast<int>(cfg.techPosition) - 1;
                if (idx >= nPlats) idx = nPlats - 1;
                const StagePlatform& plat = sortedPlats[idx];
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                out.hasPlatform = true;
                out.resolvedTechPos = static_cast<u8>(idx + 1);
                if (tumbleDrop)
                {
                    float hx = clampFloorX(geom, platMid - dir * 6.0f, 8.0f);
                    float hlr = platMid >= hx ? 1.0f : -1.0f;
                    setSpawnPair(out, hx, floorY, hlr, platMid, plat.y + 18.0f, -hlr);
                }
                else
                {
                    float pHalf = minf(5.0f, 0.35f * (plat.rightX - plat.leftX));
                    setSpawnPair(out, clampf(platMid - dir * pHalf, plat.leftX + 3.0f, plat.rightX - 3.0f), plat.y, dir,
                                 clampf(platMid + dir * pHalf, plat.leftX + 3.0f, plat.rightX - 3.0f), plat.y, -dir);
                }
            }
            else
            {
                out.resolvedTechPos = 0;
                setSymmetricFloor(out, centerX, floorY, dir,
                                  tumbleDrop ? minf(10.0f, halfW * 0.26f) : minf(14.0f, halfW * 0.35f));
                if (tumbleDrop) out.cpuY = floorY + 18.0f;
            }
            if (tumbleDrop)
            {
                out.cpuFalling = true;
                out.cpuTumbleDrop = true;
            }
            break;
        }

        case kScenarioEdgeguard:
        {
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            float sx = ledge.x + outward * 38.0f;
            float sy = ledge.y - 10.0f;
            if (cfg.edgeguardSetup == kEgSetupSpawn)
            {
                sampleEdgeguardSpawn(geom, cfg.edgeguardDist, effSide, rng, sx, sy);
            }
            else
            {
                sx = ledge.x + outward * clampf(38.0f, 18.0f, maxf(18.0f, ledgeMaxOut(geom, leftSide, 28.0f)));
                sy = clampBlastY(geom, ledge.y - 10.0f, 40.0f, ledge.y - 40.0f);
            }
            setSpawnPair(out, ledge.x - outward * minf(14.0f, halfW * 0.40f), ledge.y, outward, sx, sy, -outward);
            out.cpuFalling = true;
            break;
        }

        case kScenarioShieldPressure:
        {
            float baseDist = 12.0f;
            if (cfg.shieldRole == kShieldRoleDefense)
            {
                if (cfg.shieldDefAttack == kShieldDefGetup) out.cpuDownWait = true;
                baseDist = clampf((cfg.shieldDefAttack == kShieldDefGetup
                                       ? 9.0f
                                       : (cfg.shieldDefAttack == kShieldDefDash ? 22.0f : 14.0f))
                                      + i2f(cfg.spacingOffset),
                                  7.0f, 34.0f);
            }
            setSymmetricFloor(out, centerX, floorY, dir, minf(baseDist * 0.5f, halfW * 0.42f));
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
                u8 spaceMode = cfg.shieldDropSpacing == kSdSpaceRandom
                                   ? static_cast<u8>(1u + (geomNextRng(rng) % 3u))
                                   : cfg.shieldDropSpacing;
                static const u8 kSdOff[4] = {0, 6, 0, 14};
                float cx = clampFloorX(
                    geom, platMid + inward * i2f((spaceMode < 4 ? kSdOff[spaceMode] : 0) + cfg.spacingOffset), 10.0f);
                float towardP1 = absf(platMid - cx) > 0.5f ? (platMid >= cx ? 1.0f : -1.0f) : -inward;
                u8 faceMode = cfg.shieldDropFacing == kSdFaceRandom ? static_cast<u8>(geomNextRng(rng) & 1u)
                                                                    : cfg.shieldDropFacing;
                setSpawnPair(out, platMid, plat.y, inward, cx, floorY, faceMode == kSdFaceAway ? -towardP1 : towardP1);
            }
            else
            {
                setSymmetricFloor(out, centerX, floorY, dir, minf(10.0f, halfW * 0.30f));
            }
            break;
        }

        case kScenarioLedgetech:
        {
            static const s8 kLtPos[4][2] = {{8, -20}, {12, -36}, {26, -12}, {18, 10}};
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            u8 spawnMode = cfg.ledgetechSpawn == kLtSpawnRandom ? static_cast<u8>(geomNextRng(rng) % 4u)
                                                                : cfg.ledgetechSpawn;
            float outDist = clampf(i2f(kLtPos[spawnMode][0]), 6.0f, maxf(8.0f, ledgeMaxOut(geom, leftSide, 24.0f)));
            u8 atk = cfg.ledgetechAttack < 7 ? cfg.ledgetechAttack : static_cast<u8>(kLtAtkDsmash);
            float maxInward = minf(22.0f, halfW * 0.45f);
            float inwardOff = clampf(ledgetechCpuInwardOffset(atk) + i2f(cfg.spacingOffset), 1.8f, maxInward);
            float cx = clampFloorX(geom, ledge.x - outward * inwardOff, 1.8f);
            setSpawnPair(out, ledge.x + outward * outDist,
                         clampBlastY(geom, ledge.y + i2f(kLtPos[spawnMode][1]), 36.0f, ledge.y - 44.0f), -outward,
                         cx, ledge.y, outward);
            out.hmnFalling = true;
            out.hmnRevivalPlat = cfg.ledgetechStart == kLtStartRevival;
            break;
        }

        case kScenarioSlideOff:
        {
            StagePlatform plat;
            if (selectPlatform(geom, leftSide, plat))
            {
                out.hasPlatform = true;
                bool rightPlat = 0.5f * (plat.leftX + plat.rightX) >= centerX;
                float edgeX = rightPlat ? plat.leftX : plat.rightX;
                float intoPlat = rightPlat ? 1.0f : -1.0f;
                float platW = plat.rightX - plat.leftX;
                bool onPlat = cfg.slideOffAttack == kSlideAtkOnPlatFtilt;
                float hx = edgeX + intoPlat * (onPlat ? minf(3.5f, platW * 0.20f) : minf(4.0f, platW * 0.22f));
                float cx = onPlat ? (hx + intoPlat * minf(9.0f, maxf(5.5f, platW - 6.0f)))
                                  : clampFloorX(geom, hx + intoPlat * (cfg.slideOffAttack == kSlideAtkUnderFair ? 5.0f : 2.5f),
                                                8.0f);
                setSpawnPair(out, hx, plat.y, intoPlat, cx, onPlat ? plat.y : floorY, -intoPlat);
            }
            else
            {
                const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
                float inward = ledge.dir;
                setSpawnPair(out, ledge.x + inward * 4.5f, ledge.y, inward,
                             ledge.x + inward * minf(13.5f, halfW * 0.45f), ledge.y, -inward);
            }
            if (cfg.slideOffStart == kSlideStartKnockdown)
            {
                out.hmnDownWait = true;
            }
            else if (cfg.slideOffStart == kSlideStartTumble)
            {
                out.hmnY += 12.0f;
                out.hmnFalling = true;
                out.hmnTumbleDrop = true;
            }
            break;
        }

        case kScenarioGrabEscape:
            setSymmetricFloor(out, centerX, floorY, dir, minf(3.25f, halfW * 0.20f));
            break;

        case kScenarioMultiHitSdi:
        {
            static const u8 kSdiSp2[kMultiHitMoveCount] = {11, 12, 15, 11, 10, 14, 11, 21, 11};
            u8 move = resolveMultiHitMove(cfg.multiHitMove, cpuFtKind);
            setSymmetricFloor(out, centerX, floorY, -dir,
                              minf(i2fScale(move < kMultiHitMoveCount ? kSdiSp2[move] : 11, 0.25f), halfW * 0.30f));
            break;
        }

        default:
            out.valid = false;
            break;
        }
    }

#pragma dont_inline on
    static void triggerWarp(ScenarioState& st, const ScenarioConfig& cfg, int cpuFtKind, const StageGeometry& geom,
                            ScenarioStepAction& act, bool warpHmn, bool warpCpu)
    {
        st.resolvedMultiHit = resolveMultiHitMove(cfg.multiHitMove, cpuFtKind);
        st.resolvedLedgetechAttack = cfg.ledgetechAttack == kLtAtkRandom
                                         ? static_cast<u8>(geomNextRng(st.rng) % 7u)
                                         : cfg.ledgetechAttack;
        st.resolvedShieldDefAtk = cfg.shieldDefAttack == kShieldDefRandom
                                      ? static_cast<u8>(geomNextRng(st.rng) % 4u)
                                      : cfg.shieldDefAttack;
        st.resolvedSide = cfg.side == kScenarioSideRandom ? static_cast<u8>(geomNextRng(st.rng) & 1u) : cfg.side;

        ScenarioConfig resolvedCfg;
        memcpy(&resolvedCfg, &cfg, sizeof(cfg));
        resolvedCfg.side = st.resolvedSide;
        resolvedCfg.ledgetechAttack = st.resolvedLedgetechAttack;
        resolvedCfg.shieldDefAttack = st.resolvedShieldDefAtk;
        ScenarioSpawns sp;
        computeScenarioSpawnsEx(resolvedCfg, cpuFtKind, st.rng, geom, sp);
        if (!sp.valid)
        {
            return;
        }
        st.hasPlatform = sp.hasPlatform;
        st.resolvedPlatformSlot = sp.resolvedPlatformSlot;
        st.resolvedTechPos = sp.resolvedTechPos;
        st.hmnSpawnX = sp.hmnX;
        st.hmnSpawnY = sp.hmnY;
        st.cpuSpawnX = sp.cpuX;
        st.cpuSpawnY = sp.cpuY;
        st.resetCount++;
        st.pendingReset = false;
        clearCycleTrackers(st);

        memcpy(&act.spawns, &sp, sizeof(sp));
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
            act.edgeguardLaunchCpu = cfg.preset == kScenarioEdgeguard && cfg.edgeguardSetup == kEgSetupLaunch;
            act.cpuDownWait = sp.cpuDownWait;
            st.restoreCorrect[1] = true;
            st.restoreCorrectKind[1] = sp.cpuFalling ? kCorrectAir : kCorrectGroundCliffStop;
            st.lastCorrectWritten[1] = kCorrectNone;
        }
    }
#pragma dont_inline reset

#pragma dont_inline on
    static bool tickGrace(ScenarioState& st, bool autoReset, u16 limit)
    {
        st.graceFrames++;
        return autoReset && st.graceFrames >= limit;
    }

    static void resetTechCounters(ScenarioState& st, bool clearFlags)
    {
        st.techFrames = 0;
        st.techWasPassive = false;
        st.techActFrames = 0;
        st.techEarlyFrames = 0;
        st.graceFrames = 0;
        if (clearFlags)
        {
            st.techCoveredThisRep = false;
            st.techLateRecorded = false;
        }
    }
#pragma dont_inline reset

    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in, main::Settings& s,
                      ScenarioStepAction& act)
    {
        memset(&act, 0, sizeof(act));
        act.restoreCorrectKind[0] = kCorrectGroundCliffStop;
        act.restoreCorrectKind[1] = kCorrectGroundCliffStop;

        for (int slot = 0; slot < 2; slot++)
        {
            if (st.restoreCorrect[slot])
            {
                act.restoreCorrect[slot] = true;
                act.restoreCorrectKind[slot] = st.restoreCorrectKind[slot];
                st.restoreCorrect[slot] = false;
                st.lastCorrectWritten[slot] = st.restoreCorrectKind[slot];
            }
        }

        u8 prevPreset = st.activePreset;
        u16 prevPct = s.general.playerPercent;
        u16 prevCpuPct = s.cpu.percent;
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        applyScenarioConfig(st, cfg, s);
        bool leftToOff = prevPreset != kScenarioOff && st.activePreset == kScenarioOff;
        if (s.general.playerPercent != prevPct || leftToOff
            || (prevPreset == kScenarioGrabEscape && st.activePreset != kScenarioGrabEscape))
        {
            act.applyHmnPercent = true;
        }
        if (s.cpu.percent != prevCpuPct || leftToOff)
        {
            act.applyCpuPercent = true;
        }
        if (st.activePreset == kScenarioOff)
        {
            return;
        }

        if (st.pendingReset)
        {
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
        bool hmnFree = in.hmnHitstun == 0 && in.hmnHitlag == 0;
        bool cpuFree = in.cpuHitstun == 0 && in.cpuHitlag == 0;
        u16 graceLim = 0;
        bool hmnDanger = in.hmnOutOfPlay || nearBlastZone(in.geom, in.hmnX, in.hmnY, isTumbleStatus(in.hmnStatus));
        bool cpuDanger = in.cpuOutOfPlay || nearBlastZone(in.geom, in.cpuX, in.cpuY, isTumbleStatus(in.cpuStatus));

        if (autoReset && st.activePreset >= kScenarioTechchase && st.activePreset != kScenarioEdgeguard
            && (hmnDanger || cpuDanger))
        {
            wantReset = true;
        }
        else switch (st.activePreset)
        {
        case kScenarioComboLab:
            if (autoReset && hmnDanger) wantReset = true;
            break;

        case kScenarioTechchase:
        {
            bool inPassiveTech = in.cpuStatus == kStPassive || in.cpuStatus == kStPassiveFb;
            bool inTechOrDown = isDownOrTechStatus(in.cpuStatus);
            bool inTumble = in.cpuAir && isTumbleStatus(in.cpuStatus);
            bool cpuStruck = !cpuFree || isCaptureHoldStatus(in.cpuStatus) || in.cpuStatus == kStCaptureThrown;
            bool hmnSwungNow = isAttackOrSpecial(in.hmnStatus) && !isAttackOrSpecial(st.prevHmnStatus);

            if (st.techEngaged && cpuStruck)
            {
                if (!st.techCoveredThisRep && st.techFrames > 0)
                {
                    st.techCoveredThisRep = true;
                    st.techResult = kTechResCover;
                    st.techDeltaFrames = 0;
                    st.techReactionFrames =
                        (st.techWasPassive && in.cpuTechDist > 0 && static_cast<int>(st.techFrames) >= in.cpuTechDist)
                            ? static_cast<s16>(static_cast<int>(st.techFrames) - in.cpuTechDist)
                            : static_cast<s16>(-1);
                    st.techStreak++;
                    if (st.techStreak > st.techBest) st.techBest = st.techStreak;
                }
                resetTechCounters(st, false);
            }
            else if (inTechOrDown)
            {
                st.techEngaged = true;
                st.graceFrames = 0;
                st.techActFrames = 0;
                st.techLateRecorded = false;
                if (inPassiveTech)
                {
                    if (!st.techWasPassive) { st.techFrames = 0; st.techWasPassive = true; }
                }
                else
                {
                    st.techWasPassive = false;
                }
                if (st.techFrames < 600) st.techFrames++;
                if (in.cpuIntangible && hmnSwungNow && st.techEarlyFrames == 0)
                {
                    int rem = 21 - static_cast<int>(st.techFrames);
                    st.techEarlyFrames = static_cast<u8>(rem > 1 ? rem : 1);
                }
            }
            else if (inTumble)
            {
                if (!st.techEngaged || !isTumbleStatus(st.prevCpuStatus)) resetTechCounters(st, true);
                st.techEngaged = true;
                st.graceFrames = 0;
                st.techActFrames = 0;
            }
            else if (st.techEngaged)
            {
                bool groundedSettled = isNeutralGrounded(in.cpuStatus, in.cpuAir, in.cpuHitstun, in.cpuHitlag)
                                       || (!in.cpuAir && in.cpuActionable);
                bool airEscaped = in.cpuAir && !isTumbleStatus(in.cpuStatus) && !isDownOrTechStatus(in.cpuStatus)
                                  && (in.cpuActionable || isAirEscapeStatus(in.cpuStatus));
                if (groundedSettled || airEscaped)
                {
                    if (st.techActFrames < 600) st.techActFrames++;
                    if (st.techActFrames == 1 && !st.techCoveredThisRep && cfg.techEscape == kTechEscIntangFlash)
                    {
                        act.techFlashCpu = true;
                    }
                    if (!st.techCoveredThisRep && !st.techLateRecorded && hmnSwungNow)
                    {
                        st.techLateRecorded = true;
                        st.techResult = kTechResLate;
                        st.techDeltaFrames = static_cast<u8>(st.techActFrames > 99 ? 99 : st.techActFrames);
                        st.techReactionFrames = -1;
                        st.techStreak = 0;
                    }
                    if (st.graceFrames == 0) st.completionCount++;
                    if (tickGrace(st, autoReset, kTechGraceFrames))
                    {
                        if (!st.techCoveredThisRep && !st.techLateRecorded)
                        {
                            if (st.techEarlyFrames > 0)
                            {
                                st.techResult = kTechResEarly;
                                st.techDeltaFrames = st.techEarlyFrames;
                                st.techReactionFrames = -1;
                            }
                            st.techStreak = 0;
                        }
                        wantReset = true;
                    }
                }
            }
            break;
        }

        case kScenarioEdgeguard:
            if (autoReset && hmnDanger) { wantReset = true; resetCpu = false; }
            break;

        case kScenarioShieldPressure:
        {
            bool activeExchange = !hmnFree || !cpuFree || isAttackOrSpecial(in.cpuStatus) || isAttackOrSpecial(in.hmnStatus);
            bool isDef = cfg.shieldRole == kShieldRoleDefense;
            bool blocked = isDef ? (in.hmnStatus == kStGuardDamage || in.hmnShieldStun > 0)
                                 : (in.cpuStatus == kStGuardDamage || in.cpuShieldStun > 0);
            if (blocked && !st.prevCpuGuardDamage)
            {
                st.shieldHitCount++;
                st.completionCount++;
                st.shieldEngaged = true;
                st.graceFrames = 0;
            }
            st.prevCpuGuardDamage = blocked;
            if (isDef)
            {
                if (isAttackOrSpecial(in.cpuStatus)) { st.shieldDefSwung = true; st.shieldEngaged = true; }
                if (blocked || !cpuFree) { st.shieldEngaged = true; st.graceFrames = 0; }
                if (st.shieldEngaged && st.shieldDefSwung)
                {
                    if (activeExchange || blocked) st.graceFrames = 0;
                    else if (tickGrace(st, true, kShieldGraceFrames))
                    {
                        st.shieldDefSwung = false;
                        if (autoReset) wantReset = true;
                    }
                }
                break;
            }
            if (in.cpuHitstun > 0) { st.shieldEngaged = true; st.graceFrames = 0; }
            if (isShieldBreakStatus(in.cpuStatus)) { st.shieldEngaged = true; graceLim = 12; }
            else if (st.shieldEngaged)
            {
                if (isAttackOrSpecial(in.cpuStatus) || in.cpuStatus == kStGuardOff
                    || (in.cpuStatus >= 0x1E && in.cpuStatus <= 0x20)
                    || in.cpuStatus == kStJumpSquat || in.cpuStatus == kStJump)
                {
                    st.oosCountered = true;
                }
                if (activeExchange || blocked) st.graceFrames = 0;
                else if (st.oosCountered || !isGuardStatus(in.cpuStatus) || in.cpuStatus == kStGuardOff)
                {
                    graceLim = kShieldGraceFrames;
                }
                else st.graceFrames = 0;
            }
            break;
        }

        case kScenarioShieldDrop:
        {
            if (isGuardStatus(in.hmnStatus)) st.hmnGuardWindow = 4;
            else if (st.hmnGuardWindow > 0) st.hmnGuardWindow--;
            bool passNow = in.hmnStatus == kStPass || in.hmnEnteredPass;
            if (!st.shieldDropDone && passNow && (st.hmnGuardWindow > 0 || isGuardStatus(st.prevHmnStatus)))
            {
                st.sawShieldDrop = true;
                st.shieldDropDone = true;
                st.completionCount++;
                st.graceFrames = 0;
            }
            else if (st.shieldDropDone
                     && ((!in.hmnAir && in.hmnY <= in.geom.floor.centerY + 4.0f && hmnFree && in.hmnActionable)
                         || st.graceFrames > 0))
            {
                graceLim = kShieldDropGraceFrames;
            }
            break;
        }

        case kScenarioLedgetech:
            if (isWallTechStatus(in.hmnStatus))
            {
                if (!st.sawWallTech) { st.sawWallTech = true; st.completionCount++; }
                st.ledgetechDone = true;
            }
            else if (isLedgeStatus(in.hmnStatus)
                     || (st.sinceResetFrames > 6 && !in.hmnAir && in.geom.floor.valid
                         && in.hmnX >= in.geom.floor.leftX && in.hmnX <= in.geom.floor.rightX))
            {
                st.ledgetechDone = true;
            }
            if (st.ledgetechDone) graceLim = kLedgetechGraceFrames;
            break;

        case kScenarioSlideOff:
            if (!in.hmnAir && st.sinceResetFrames >= 1) st.slideOffArmed = true;
            if (!st.slideOffDone && st.slideOffArmed && st.havePrev && !st.prevHmnAir && in.hmnAir
                && (isTumbleStatus(in.hmnStatus) || in.hmnHitstun > 0))
            {
                st.sawSlideOff = true;
                st.slideOffDone = true;
                st.completionCount++;
                st.graceFrames = 0;
            }
            else if (st.slideOffDone
                     && (((!in.hmnAir || (in.geom.floor.valid && in.hmnY > in.geom.floor.centerY + 80.0f))
                          && hmnFree
                          && (in.hmnActionable || isDownOrTechStatus(in.hmnStatus) || isTumbleStatus(in.hmnStatus)))
                         || st.graceFrames > 0))
            {
                graceLim = kSlideOffGraceFrames;
            }
            break;

        case kScenarioGrabEscape:
        {
            bool anyHeld = isCaptureHoldStatus(in.hmnStatus) || isCaptureHoldStatus(in.cpuStatus);
            bool anyEscaped = isCaptureEscapeStatus(in.hmnStatus) || isCaptureEscapeStatus(in.cpuStatus);
            bool anyThrown = in.hmnStatus == kStCaptureThrown || in.cpuStatus == kStCaptureThrown;
            if (anyHeld)
            {
                if (!st.grabActive) { st.grabActive = true; st.activeGrabFrames = 0; }
                st.grabEscapeFrames = ++st.activeGrabFrames;
                st.graceFrames = 0;
            }
            else if (anyEscaped || (st.grabActive && !anyThrown && !st.grabDone))
            {
                if (!st.grabDone)
                {
                    st.sawGrabEscape = true;
                    st.grabDone = true;
                    st.grabEscapeFrames = st.activeGrabFrames > 0 ? st.activeGrabFrames : 1;
                    st.completionCount++;
                    st.graceFrames = anyEscaped ? 0 : 1;
                }
                st.grabActive = false;
            }
            else if (anyThrown) { st.grabActive = false; st.grabDone = true; st.graceFrames = 0; }
            else if (st.grabDone && ((hmnFree && cpuFree && !in.hmnAir && !in.cpuAir) || st.graceFrames > 0))
            {
                graceLim = kGrabGraceFrames;
            }
            break;
        }

        case kScenarioMultiHitSdi:
        {
            bool hmnInDamageLag = in.hmnHitlag > 0 && in.hmnDamageHitlag;
            if (hmnInDamageLag && (!st.havePrev || in.hmnHitlag > st.prevHmnHitlag || st.prevHmnHitlag == 0))
            {
                if (st.sdiLatchedReset) { clearEventTelemetry(st); st.sdiLatchedReset = false; }
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
                    if (absf(dx) < 15.0f && absf(dy) < 15.0f) { st.sdiDispX += dx; st.sdiDispY += dy; }
                }
            }
            if (st.sdiSequenceActive && !hmnInDamageLag)
            {
                float totalSdi = absf(st.sdiDispX) + absf(st.sdiDispY);
                if ((in.hmnHitstun == 0 && in.hmnActionable)
                    || (totalSdi >= 0.5f && (absf(in.hmnX - in.cpuX) > 16.0f || in.hmnY - in.cpuY > 14.0f)))
                {
                    if (!st.sdiEscaped && (totalSdi >= 0.5f || st.sdiHitCount >= 1))
                    {
                        st.sdiEscaped = true;
                        st.completionCount++;
                    }
                    graceLim = kSdiEscapeGraceFrames;
                }
            }
            break;
        }

        default:
            break;
        }
        if (graceLim != 0 && tickGrace(st, autoReset, graceLim))
        {
            wantReset = true;
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

    static inline void cpuEmit(ScenarioCpuOutput& out, u32 btn, float sx, float sy)
    {
        out.buttons = btn;
        out.stickX = sx;
        out.stickY = sy;
    }

    static inline void cpuCStick(ScenarioCpuOutput& out, float sx, float sy)
    {
        cpuEmit(out, cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick, sx, sy);
    }

    static inline void cpuResetPhase(ScenarioCpuState& st, u8 cd, bool clearDelay)
    {
        st.phase = 0;
        st.timer = 0;
        if (clearDelay) st.delay = 0;
        if (cd != 0) st.cooldown = cd;
    }

    static inline void cpuSetPhase(ScenarioCpuState& st, u8 phase, u8 timer = 0)
    {
        st.phase = phase;
        st.timer = timer;
    }

#pragma dont_inline on
    static bool tickCooldown(ScenarioCpuState& st)
    {
        if (st.cooldown > 0)
        {
            st.cooldown--;
            return true;
        }
        return false;
    }

    static void emitAtkReset(ScenarioCpuState& st, ScenarioCpuOutput& out)
    {
        out.buttons = cpu::button::kAttack;
        cpuResetPhase(st, 40, true);
    }

    enum { kBtnAtk = 0, kBtnSpec = 1, kBtnCStk = 2 };

    static u32 decodeBtn(u8 k)
    {
        static const u32 kBtns[3] = {
            cpu::button::kAttack, cpu::button::kSpecial,
            cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick
        };
        return kBtns[k < 3 ? k : 0];
    }

    static void emitScaled(ScenarioCpuOutput& out, u8 btnKind, float toward, s8 sx20, s8 sy20)
    {
        cpuEmit(out, decodeBtn(btnKind), toward * i2fScale(sx20, 0.05f), i2fScale(sy20, 0.05f));
    }

    // 0 = Nair, 1 = Fair, 2 = Uair, 3 = Dair
    static void emitAirAtk(ScenarioCpuOutput& out, u8 dir, float lr)
    {
        if (dir == 0) out.buttons = cpu::button::kAttack;
        else cpuCStick(out, dir == 1 ? lr : 0.0f, dir == 2 ? 1.0f : (dir == 3 ? -1.0f : 0.0f));
    }

    static float stickDivClamp(float num, float div, float lim)
    {
        return clampf(num / div, -lim, lim);
    }

    static bool cpuApproachOrFace(const ScenarioCpuInput& in, float absDx, float toward, float maxDist, float walkSpd,
                                  bool needFace, ScenarioCpuOutput& out)
    {
        if (absDx > maxDist)
        {
            out.stickX = toward * walkSpd;
            return true;
        }
        if (needFace && in.cpuLr * toward < 0.0f)
        {
            out.stickX = toward * 0.45f;
            return true;
        }
        return false;
    }

    static bool cpuWaitDelay(ScenarioCpuState& st, u32 rng, u8 base, u8 span)
    {
        if (st.phase == 0)
        {
            if (st.delay == 0)
            {
                st.delay = static_cast<u8>(base + (rng % span));
                st.timer = 0;
            }
            st.timer++;
            if (st.timer < st.delay)
            {
                return true;
            }
            st.phase = 1;
            st.timer = 0;
        }
        return false;
    }

    static bool cpuAirWaitEnd(ScenarioCpuState& st, bool cpuAir, u8 maxT, u8 cd, bool clearDelay)
    {
        st.timer++;
        if (!cpuAir || st.timer >= maxT)
        {
            cpuResetPhase(st, cd, clearDelay);
            return true;
        }
        return false;
    }

    static void stepJumpPoke(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out, float dx,
                             float absDx, float toward, bool isFair)
    {
        if (st.phase == 1)
        {
            if (!in.cpuAir && in.cpuCanAct)
            {
                if (cpuApproachOrFace(in, absDx, toward, isFair ? 16.0f : 9.0f, isFair ? 0.70f : 0.65f, isFair, out))
                {
                    return;
                }
                cpuEmit(out, cpu::button::kJump, isFair ? (toward * 0.80f) : stickDivClamp(dx, 12.0f, 0.5f), 0.0f);
                cpuSetPhase(st, 2);
            }
        }
        else if (st.phase == 2)
        {
            st.timer++;
            if (isFair) out.stickX = stickDivClamp(dx, 8.0f, 1.0f);
            if (in.cpuAir && (!isFair || in.cpuVy <= 0.1f || st.timer >= 8))
            {
                emitAirAtk(out, isFair ? 1 : 2, in.cpuLr);
                cpuSetPhase(st, 3);
            }
            else if (!in.cpuAir && st.timer >= 12) cpuSetPhase(st, 1);
        }
        else if (!cpuAirWaitEnd(st, in.cpuAir, 28, isFair ? 36 : 28, true))
        {
            if (isFair) out.stickX = stickDivClamp(dx, 10.0f, 0.7f);
            if (isFair ? (in.cpuVy < 0.0f) : (in.cpuVy <= 0.0f)) out.stickY = -1.0f;
        }
    }

    static void runGroundPoke(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out, int idx,
                              float absDx, float toward)
    {
        struct PokeSpec { u8 dist2, walk20, btn, face, cd; s8 sx20, sy20; };
        static const PokeSpec kPoke[4] = {
            {26, 13, kBtnCStk, 1, 38, 20, 0},   // Fsmash
            {23, 13, kBtnAtk, 1, 38, 11, 0},    // Ftilt
            {20, 12, kBtnCStk, 0, 34, 0, -20},  // Dsmash
            {20, 12, kBtnAtk, 0, 34, 0, 11}     // Utilt
        };
        const PokeSpec& pk = kPoke[idx];
        if (!in.cpuAir && in.cpuCanAct
            && !cpuApproachOrFace(in, absDx, toward, i2fScale(pk.dist2, 0.5f), i2fScale(pk.walk20, 0.05f),
                                  pk.face != 0, out))
        {
            emitScaled(out, pk.btn, toward, pk.sx20, pk.sy20);
            cpuResetPhase(st, pk.cd, true);
        }
    }
#pragma dont_inline reset

    void stepScenarioCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out)
    {
        memset(&out, 0, sizeof(out));
        if (in.preset == kScenarioOff || in.hmnOutOfPlay || in.cpuOutOfPlay)
        {
            return;
        }

        float dx = in.hmnX - in.cpuX;
        float toward = dx >= 0.0f ? 1.0f : -1.0f;
        float absDx = absf(dx);
        const bool cpuGndAct = !in.cpuAir && in.cpuCanAct;

        switch (in.preset)
        {
        case kScenarioMultiHitSdi:
        {
            u8 move = resolveMultiHitMove(in.multiHitMove, in.cpuFtKind);
            if (st.lastMove != move) { scenarioCpuStateReset(st); st.lastMove = move; }
            out.active = true;
            if (tickCooldown(st)) return;

            if (move >= kMultiHitNair && move <= kMultiHitDownAir)
            {
                float takeoffDiv = move == kMultiHitUpAir ? 14.0f : (move == kMultiHitFair ? 11.0f : 9.0f);
                float approachDist = move == kMultiHitFair ? 10.5f : 8.5f;
                float takeoffSteerX = stickDivClamp(dx, takeoffDiv, 1.0f);
                float airSteerX = stickDivClamp(dx - in.cpuVx * 8.0f, 3.0f, 1.0f);
                if (st.phase == 0)
                {
                    if (cpuGndAct)
                    {
                        if (absDx > approachDist || (in.hmnAir && in.hmnY > in.cpuY + 18.0f))
                        {
                            out.stickX = stickDivClamp(dx, 5.0f, 0.55f);
                            return;
                        }
                        if (move == kMultiHitFair && in.cpuLr * toward < 0.0f) { out.stickX = toward * 0.45f; return; }
                        cpuEmit(out, cpu::button::kJump, takeoffSteerX, 0.0f);
                        cpuSetPhase(st, 1);
                    }
                }
                else if (st.phase == 1)
                {
                    st.timer++;
                    if (in.cpuAir)
                    {
                        if (move == kMultiHitUpAir) { out.stickX = airSteerX; cpuSetPhase(st, 2, 1); }
                        else
                        {
                            emitAirAtk(out, move == kMultiHitDownAir ? 3 : (move == kMultiHitFair ? 1 : 0), in.cpuLr);
                            cpuSetPhase(st, 3);
                        }
                    }
                    else if (st.timer >= 12) cpuResetPhase(st, 0, false);
                    else out.stickX = takeoffSteerX;
                }
                else if (st.phase == 2)
                {
                    if (!in.cpuAir) { cpuResetPhase(st, 0, false); return; }
                    st.timer++;
                    bool readyForUpAir =
                        in.hmnAir ? (st.timer >= kUpAirAirborneDelay)
                                  : ((st.timer >= kUpAirMinFrames && in.cpuVy <= 0.25f && in.cpuY <= in.hmnY + 12.0f)
                                     || st.timer >= kUpAirMaxFrames);
                    if (readyForUpAir) { emitAirAtk(out, 2, in.cpuLr); cpuSetPhase(st, 3); }
                    else out.stickX = airSteerX;
                }
                else if (!cpuAirWaitEnd(st, in.cpuAir, 30, 10, false))
                {
                    out.stickX = airSteerX;
                }
                return;
            }

            // Grounded Multi-Hit moves: RapidJab (5), UpB (6), SideB (7), DownB (8).
            struct GroundSdiSpec {
                u8 range2, walk20, btn, needFace, mash, steerMax20, endT, cd;
                s8 sx20, sy20, steerDiv;
            };
            static const GroundSdiSpec kGnd[4] = {
                {17, 13, kBtnAtk, 1, 1, 0, 24, 22, 0, 0, 0},     // RapidJab
                {16, 12, kBtnSpec, 1, 0, 9, 14, 26, 0, 20, 6},   // UpB
                {28, 12, kBtnSpec, 0, 0, 20, 12, 22, 20, 0, -1}, // SideB
                {16, 12, kBtnSpec, 0, 1, 11, 16, 24, 0, -20, 5}  // DownB
            };
            const GroundSdiSpec& g = kGnd[move >= kMultiHitRapidJab && move <= kMultiHitDownB ? (move - kMultiHitRapidJab) : 0];
            if (st.phase == 0)
            {
                if (cpuGndAct)
                {
                    if (cpuApproachOrFace(in, absDx, toward, i2fScale(g.range2, 0.5f), i2fScale(g.walk20, 0.05f),
                                          g.needFace != 0, out))
                    {
                        return;
                    }
                    emitScaled(out, g.btn, toward, g.sx20, g.sy20);
                    cpuSetPhase(st, 1, g.mash);
                }
            }
            else
            {
                st.timer++;
                if (g.steerDiv < 0) out.stickX = toward;
                else if (g.steerDiv > 0) out.stickX = stickDivClamp(dx, i2f(g.steerDiv), i2fScale(g.steerMax20, 0.05f));
                u32 btn = decodeBtn(g.btn);
                if (g.mash != 0 && (st.timer & 1) == 0 && (in.lastButtons & btn) == 0) out.buttons = btn;
                if (st.timer >= g.endT) cpuResetPhase(st, g.cd, false);
            }
            break;
        }

        case kScenarioLedgetech:
        {
            u8 atk = in.ledgetechAttack < 7 ? in.ledgetechAttack : static_cast<u8>(kLtAtkDtilt);
            if (atk == kLtAtkDair && st.phase > 0)
            {
                out.active = true;
                if (st.phase == 1)
                {
                    st.timer++;
                    if (in.cpuAir) { emitAirAtk(out, 3, in.cpuLr); cpuSetPhase(st, 2); }
                    else if (st.timer >= 10) cpuResetPhase(st, 0, false);
                }
                else
                {
                    out.stickX = -toward * 0.35f;
                    cpuAirWaitEnd(st, in.cpuAir, 24, 20, false);
                }
                return;
            }
            if (tickCooldown(st)) return;
            float maxX = (atk >= kLtAtkNeutralB ? 16.0f : 12.4f) + maxf(0.0f, i2f(in.spacingOffset));
            bool inLedgeWindow = in.ledgetechTiming == kLtTimeHoldActive
                                 || (absDx <= maxX && in.hmnY >= in.cpuY - 18.0f && in.hmnY <= in.cpuY + 20.0f);
            if (cpuGndAct && inLedgeWindow && in.hmnHitstun == 0)
            {
                out.active = true;
                if (in.cpuLr * toward < 0.0f) { out.stickX = toward * 0.45f; return; }
                if (atk == kLtAtkDair) { cpuEmit(out, cpu::button::kJump, toward * 0.20f, 0.0f); cpuSetPhase(st, 1); return; }
                struct LtAtkSpec { u8 btn; s8 sx20; s8 sy20; u8 cd; };
                static const LtAtkSpec kLt[7] = {
                    {kBtnAtk, 0, -11, 16}, {kBtnAtk, 11, 0, 18}, {kBtnCStk, 20, 0, 28},
                    {kBtnCStk, 0, -20, 26}, {0, 0, 0, 0}, {kBtnSpec, 0, 0, 24}, {kBtnSpec, 20, 0, 28}
                };
                emitScaled(out, kLt[atk].btn, toward, kLt[atk].sx20, kLt[atk].sy20);
                st.cooldown = kLt[atk].cd;
            }
            break;
        }

        case kScenarioShieldPressure:
        {
            if (in.shieldRole != kShieldRoleDefense) return;
            out.active = true;
            if (tickCooldown(st) || cpuWaitDelay(st, in.rng, 45, 30)) return;
            u8 defAtk = in.shieldDefAttack < 5 ? in.shieldDefAttack : static_cast<u8>(kShieldDefAerial);
            if (defAtk == kShieldDefGetup)
            {
                if (in.cpuStatus == 0x4D || in.cpuStatus == 0x4E || in.cpuStatus == 0x50) emitAtkReset(st, out);
            }
            else if (defAtk == kShieldDefAerial) stepJumpPoke(in, st, out, dx, absDx, toward, true);
            else if (defAtk == kShieldDefDash)
            {
                if (!in.cpuAir)
                {
                    out.stickX = toward;
                    if (absDx <= 14.0f && (in.cpuStatus == 0x03 || in.cpuStatus == 0x04)) emitAtkReset(st, out);
                }
            }
            else runGroundPoke(in, st, out, defAtk == kShieldDefSmash ? 0 : 1, absDx, toward);
            break;
        }

        case kScenarioShieldDrop:
            if (in.shieldDropAttack == kSdAtkOff) return;
            out.active = true;
            if (tickCooldown(st) || cpuWaitDelay(st, in.rng, 30, 36)) return;
            if (in.shieldDropAttack == kSdAtkShAerial) stepJumpPoke(in, st, out, dx, absDx, toward, false);
            else runGroundPoke(in, st, out, in.shieldDropAttack == kSdAtkDsmash ? 2 : 3, absDx, toward);
            break;

        case kScenarioSlideOff:
        {
            if (in.hasPlatform && in.slideOffAttack != kSlideAtkOnPlatFtilt)
            {
                out.active = true;
                if (tickCooldown(st)) return;
                float hopX = stickDivClamp(dx, 14.0f, 0.45f);
                if (st.phase == 0)
                {
                    if (cpuGndAct && !in.hmnAir && in.hmnHitstun == 0)
                    {
                        bool isFair = in.slideOffAttack == kSlideAtkUnderFair;
                        if (cpuApproachOrFace(in, absDx, toward, isFair ? 8.5f : 5.5f, 0.60f, isFair, out)) return;
                        cpuEmit(out, cpu::button::kJump, hopX, 0.0f);
                        cpuSetPhase(st, 1, 1);
                    }
                }
                else if (st.phase == 1)
                {
                    st.timer++;
                    float airX = stickDivClamp(dx, 12.0f, 0.5f);
                    if (!in.cpuAir)
                    {
                        cpuEmit(out, cpu::button::kJump, hopX, 0.0f);
                        if (st.timer >= 12) cpuResetPhase(st, 0, false);
                    }
                    else if (st.timer < 5) cpuEmit(out, cpu::button::kJump, airX, 0.0f);
                    else if (in.cpuY >= in.hmnY
                                            - (in.slideOffAttack == kSlideAtkUnderUair
                                                   ? (in.cpuVy > 2.2f ? 24.0f : 13.0f)
                                                   : 9.0f)
                             || (st.timer >= 16 && in.cpuVy <= 0.4f))
                    {
                        emitAirAtk(out, in.slideOffAttack == kSlideAtkUnderFair ? 1
                                            : (in.slideOffAttack == kSlideAtkUnderNair ? 0 : 2),
                                   in.cpuLr);
                        cpuSetPhase(st, 2);
                    }
                    else out.stickX = airX;
                }
                else cpuAirWaitEnd(st, in.cpuAir, 30, 34, false);
                return;
            }
            bool repeatS3 = !in.cpuAir && in.cpuStatus == kStAttackS3 && (in.lastButtons & cpu::button::kAttack) == 0;
            if (repeatS3 || (!tickCooldown(st) && cpuGndAct && !in.hmnAir && absDx < 22.0f && in.hmnHitstun == 0))
            {
                out.active = true;
                if (!repeatS3 && cpuApproachOrFace(in, absDx, toward, 9.5f, 0.60f, true, out)) return;
                emitScaled(out, kBtnAtk, toward, 13, -5);
                if (!repeatS3) st.cooldown = 38;
            }
            break;
        }

        case kScenarioGrabEscape:
        {
            if (in.grabAction == kGrabActStand) return;
            out.active = true;
            bool holdingHmn = isCaptureHoldStatus(in.hmnStatus) || in.cpuStatus == kStCatchPull
                              || in.cpuStatus == kStCatchDashPull || in.cpuStatus == kStCatchWait;
            if (holdingHmn)
            {
                if (st.phase != 2)
                {
                    st.phase = 2;
                    st.timer = 0;
                    st.resolvedThrow = in.grabAction == kGrabActRandThrow
                                           ? static_cast<u8>(kGrabActFThrow + (in.rng & 3u))
                                           : in.grabAction;
                }
                else if (st.timer < 255) st.timer++;
                if (in.grabAction != kGrabActRandThrow) st.resolvedThrow = in.grabAction;
                if (in.grabAction != kGrabActHold && in.cpuStatus == kStCatchWait && st.timer >= 2)
                {
                    if (st.resolvedThrow == kGrabActFThrow) out.stickX = in.cpuLr;
                    else if (st.resolvedThrow == kGrabActBThrow) out.stickX = -in.cpuLr;
                    else out.stickY = st.resolvedThrow == kGrabActUThrow ? 1.0f : -1.0f;
                }
                return;
            }
            if (st.phase == 2) { st.phase = 3; st.cooldown = 40; return; }
            if (tickCooldown(st) || cpuWaitDelay(st, in.rng, 40, 60)) return;
            if (cpuGndAct && !cpuApproachOrFace(in, absDx, toward, 8.5f, 0.65f, true, out))
            {
                out.buttons = cpu::button::kAttack | cpu::button::kGuard;
                st.cooldown = 20;
            }
            break;
        }

        default:
            break;
        }
    }
}
}
