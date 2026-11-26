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

#pragma dont_inline on
        float minf(float a, float b)
        {
            return a < b ? a : b;
        }

        float maxf(float a, float b)
        {
            return a > b ? a : b;
        }

        float clampf(float v, float lo, float hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        bool isGuardStatus(int s)
        {
            return s >= kStGuardOn && s <= kStGuardDamage;
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
            return (s >= kStCapturePulled && s <= kStCaptureDamage)
                   || (s >= kStCapturePulledYoshi && s <= kStCaptureDamageYoshi);
        }

        bool isAttackOrSpecial(int s)
        {
            return (s >= kStAttackFirst && s <= kStThrow) || s >= kStSpecialFirst;
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
                lowY = maxf(lowY, geom.floor.centerY - 95.0f);
            }
            if (y < lowY || x < geom.blast.left + 20.0f || x > geom.blast.right - 20.0f)
            {
                return true;
            }
            return inTumble && y > geom.blast.top - 20.0f;
        }

        bool isShieldBreakStatus(int s)
        {
            return s >= kStShieldBreakFly && s <= kStFuraFuraEnd;
        }

        bool isAirEscapeStatus(int s)
        {
            return (s >= kStJump && s <= kStFallAerial) || s == kStEscapeAir;
        }

        bool isNeutralGrounded(int s, bool air, int hitstun, int hitlag)
        {
            if (air || hitstun > 0 || hitlag > 0)
            {
                return false;
            }
            return s <= kStWalkBrake || s == kStSquat || s == kStSquatWait || s == kStGuardOn || s == kStGuard;
        }

        void clearEventTelemetry(ScenarioState& st)
        {
            st.sawShieldDrop = false;
            st.sawWallTech = false;
            st.sawSlideOff = false;
            st.sawGrabEscape = false;
            st.sdiHitCount = 0;
            st.sdiHitlagFrames = 0;
            st.sdiDispX = 0.0f;
            st.sdiDispY = 0.0f;
            st.sdiEscaped = false;
        }
#pragma dont_inline reset

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
        void copySnapshotTo(const ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                            main::GeneralSettings* gen, main::CpuSettings* cpuSet, main::OsdSettings* osd,
                            main::TechInvisSettings* techInvis)
        {
            memcpy(&cpuOpts, &st.snapshotCpu, sizeof(cpuOpts));
            memcpy(&comboCfg, &st.snapshotCombo, sizeof(comboCfg));
            if (st.snapshotPlayerValid && gen != NULL)
            {
                gen->playerPercent = st.snapshotPlayerPercent;
                gen->lockPlayerPercent = st.snapshotLockPlayerPercent;
                gen->playerInfiniteShields = st.snapshotPlayerInfShields;
            }
            if (st.snapshotCpuValid && cpuSet != NULL)
            {
                cpuSet->percent = st.snapshotCpuPercent;
                cpuSet->lockPercent = st.snapshotLockCpuPercent;
            }
            if (st.snapshotOsdValid && osd != NULL)
            {
                osd->grabBreakout = st.snapshotGrabBreakoutOsd;
            }
            if (st.snapshotTechInvisValid && techInvis != NULL)
            {
                memcpy(techInvis, &st.snapshotTechInvis, sizeof(*techInvis));
            }
        }

        void applyPresetOverrides(const ScenarioConfig& cfg, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* gen,
                                  main::CpuSettings* cpuSet, main::OsdSettings* osd,
                                  main::TechInvisSettings* techInvis)
        {
            if (cfg.preset == kScenarioOff || cfg.preset >= kScenarioCount)
            {
                return;
            }
            comboCfg.enabled = 0;
            comboCfg.recoveryTrainer = 0;
            cpuOpts.control = cpu::kControlLab;
            cpuOpts.behavior = cpu::kBehaviorStand;
            switch (cfg.preset)
            {
            case kScenarioComboLab:
                comboCfg.enabled = 1;
                comboCfg.fixPosition = 1;
                comboCfg.noclip = 1;
                comboCfg.comboTimerAdjustment = 0;
                if (comboCfg.enabledMeters == 0)
                {
                    comboCfg.enabledMeters = main::kMeterAll;
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
                if (techInvis != NULL)
                {
                    techInvis->mode = cfg.techInvisMode;
                    techInvis->delay = cfg.techInvisDelay;
                    techInvis->preTechDrift = cfg.techPreDrift;
                }
                break;

            case kScenarioEdgeguard:
                comboCfg.enabled = 1;
                comboCfg.recoveryTrainer = 1;
                comboCfg.noclip = 1;
                comboCfg.fixPosition = 0;
                comboCfg.recoveryMixup = cfg.edgeguardMixup;
                cpuOpts.recovery = cpu::kRecoveryPPlusAi;
                cpuOpts.tdi = cpu::kTdiSurvival;
                if (cpuSet != NULL && cpuSet->percent < kEdgeguardMinCpuPercent)
                {
                    cpuSet->percent = kEdgeguardTargetCpuPercent;
                    cpuSet->lockPercent = 1;
                }
                break;

            case kScenarioShieldPressure:
                if (cfg.shieldRole == kShieldRoleDefense)
                {
                    cpuOpts.counterGround = 0;
                    cpuOpts.counterAir = 0;
                    cpuOpts.counterShield = 0;
                    cpuOpts.autoLCancel = 1;
                    if (gen != NULL)
                    {
                        gen->playerInfiniteShields = cpu::kShieldsOn;
                    }
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
                if (cfg.shieldDropAttack != kSdAtkOff)
                {
                    cpuOpts.autoLCancel = 1;
                }
                if (gen != NULL)
                {
                    gen->playerInfiniteShields = cpu::kShieldsOn;
                }
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
                    if (gen != NULL && gen->playerPercent < kLedgetechMinPercent)
                    {
                        gen->playerPercent = kLedgetechTargetPercent;
                        gen->lockPlayerPercent = 1;
                    }
                }
                else if (cfg.preset == kScenarioSlideOff)
                {
                    cpuOpts.tdi = cpu::kTdiNone;
                    if (gen != NULL && gen->playerPercent < kSlideOffMinPercent)
                    {
                        gen->playerPercent = kSlideOffTargetPercent;
                        gen->lockPlayerPercent = 1;
                    }
                }
                else if (cfg.preset == kScenarioGrabEscape)
                {
                    cpuOpts.grabEscape = cfg.grabAction == kGrabActStand ? cpu::kMashHigh : cpu::kMashNone;
                    if (osd != NULL)
                    {
                        osd->grabBreakout = 1;
                    }
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
            st.havePrev = false;
            st.hmnGuardWindow = 0;
            st.techEngaged = false;
            st.techFrames = 0;
            st.techActFrames = 0;
            st.techEarlyFrames = 0;
            st.techCoveredThisRep = false;
            st.techLateRecorded = false;
            st.shieldEngaged = false;
            st.oosCountered = false;
            st.shieldDefSwung = false;
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
        static const float kOffsets[7] = {
            4.5f, // kLtAtkDtilt
            5.0f, // kLtAtkFtilt
            6.0f, // kLtAtkFsmash
            4.2f, // kLtAtkDsmash
            3.8f, // kLtAtkDair
            6.5f, // kLtAtkNeutralB
            6.5f  // kLtAtkSideB
        };
        return atk < 7 ? kOffsets[atk] : 4.2f;
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
            float minSpd, maxSpd, minAng, maxAng, lipOff, relY;
            u8 hitstun, hitlag;
        };
        static const EggLaunchRow kRows[3] = {
            {2.65f, 3.05f, 26.0f, 36.0f, -4.0f, 4.0f, 24, 6},  // Weak
            {3.35f, 3.85f, 22.0f, 34.0f, -2.0f, 6.0f, 34, 8},  // Normal
            {4.15f, 4.70f, 18.0f, 30.0f, 2.0f, 8.0f, 44, 10}   // Strong
        };
        u8 mode = strengthMode >= kEgStrRandom ? static_cast<u8>(geomNextRng(rng) % 3u) : strengthMode;
        const EggLaunchRow& r = kRows[mode];
        float speed = r.minSpd + geomRand01(rng) * (r.maxSpd - r.minSpd);
        float angleDeg = r.minAng + geomRand01(rng) * (r.maxAng - r.minAng);
        // Compact polynomial sin/cos on [18..36 deg] (zero libm dependency).
        float rad = angleDeg * 0.01745329f;
        float r2 = rad * rad;
        float sinA = rad * (1.0f - r2 * (0.1666667f - r2 * 0.0083333f));
        float cosA = 1.0f - r2 * (0.5f - r2 * 0.0416667f);
        out.startX = ledge.x + outward * r.lipOff;
        out.startY = ledge.y + r.relY;
        out.kbX = outward * speed * cosA;
        out.kbY = speed * sinA;
        out.speed = speed;
        out.angleDeg = angleDeg;
        out.hitstun = r.hitstun;
        out.hitlag = r.hitlag;
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

    TM_STATIC_ASSERT(sizeof(ScenarioConfig) == 34, tm_scenario_cfg_size);

    void sanitizeScenarioConfigInto(const ScenarioConfig& raw, ScenarioConfig& c)
    {
        static const u8 kLim[33] = {
            kScenarioCount, kScenarioSideCount, kScenarioSideCount, kMultiHitMoveCount, kOosCount,
            kLtSpawnCount, kLtDjCount, kLtAtkCount, kSdPlatCount, kSdSpaceCount,
            kSdFaceCount, kGrabActCount, kGrabPctCount, kTechSetupCount, kTechPosMax + 1,
            kTechEscCount, main::kTechInvisCount, 16, 2, main::kPreTechDriftCount,
            kTechTdiCount, 2, kEgSetupCount, kEgStrCount, kEgDistCount,
            kEgDjCount, main::kRecMixupCount, kLtStartCount, kLtTimeCount, kShieldRoleCount,
            kShieldDefCount, kSdAtkCount, kSlideAtkCount
        };
        const u8* src = reinterpret_cast<const u8*>(&raw);
        u8* dst = reinterpret_cast<u8*>(&c);
        for (int i = 0; i < 33; i++)
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

    ScenarioConfig sanitizeScenarioConfig(const ScenarioConfig& raw)
    {
        ScenarioConfig c;
        sanitizeScenarioConfigInto(raw, c);
        return c;
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

    void reapplyActiveScenarioOverrides(const ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                                        main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                                        main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg,
                                        main::TechInvisSettings* techInvisCfg)
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
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
        }
        applyPresetOverrides(cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
    }

    bool applyScenarioConfig(ScenarioState& st, const ScenarioConfig& rawCfg, cpu::CpuOptions& cpuOpts,
                             main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                             main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg,
                             main::TechInvisSettings* techInvisCfg)
    {
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        ScenarioConfig prev;
        memcpy(&prev, &st.lastCfg, sizeof(prev));
        memcpy(&st.lastCfg, &cfg, sizeof(cfg));

        if (cfg.preset == st.activePreset)
        {
            if (cfg.preset == kScenarioOff || !bytesDiffer(&prev, &cfg, 33))
            {
                return false;
            }
            bool oosChanged = cfg.oosAction != prev.oosAction;
            bool shieldRoleChanged = cfg.shieldRole != prev.shieldRole;
            bool techOptsChanged = bytesDiffer(&prev.techEscape, &cfg.techEscape, 6);
            bool edgeMixupChanged = cfg.edgeguardMixup != prev.edgeguardMixup;
            bool sdAtkChanged = cfg.shieldDropAttack != prev.shieldDropAttack;
            bool grabChanged = cfg.grabAction != prev.grabAction || cfg.grabPctMode != prev.grabPctMode;

            ScenarioConfig a, b;
            memcpy(&a, &prev, sizeof(a));
            memcpy(&b, &cfg, sizeof(b));
            a.preset = b.preset = a.autoReset = b.autoReset = a.techShowReaction = b.techShowReaction =
                a.edgeguardMixup = b.edgeguardMixup = 0;
            memset(&a.techEscape, 0, 6);
            memset(&b.techEscape, 0, 6);
            bool layoutChanged = bytesDiffer(&a, &b, 33);

            bool needReapply = (cfg.preset == kScenarioShieldPressure && (oosChanged || shieldRoleChanged))
                               || (cfg.preset == kScenarioTechchase && techOptsChanged)
                               || (cfg.preset == kScenarioShieldDrop && sdAtkChanged);
            if (needReapply)
            {
                if (st.snapshotActive)
                {
                    copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
                }
                applyPresetOverrides(cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
            }
            if (cfg.preset == kScenarioEdgeguard && edgeMixupChanged)
            {
                comboCfg.recoveryMixup = cfg.edgeguardMixup;
            }
            if (cfg.preset == kScenarioGrabEscape && grabChanged)
            {
                cpuOpts.grabEscape = cfg.grabAction == kGrabActStand ? cpu::kMashHigh : cpu::kMashNone;
            }
            if (layoutChanged)
            {
                st.pendingReset = true;
            }
            return layoutChanged || techOptsChanged || edgeMixupChanged;
        }

        if (cfg.preset == kScenarioOff)
        {
            restoreScenarioSnapshot(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
            st.activePreset = kScenarioOff;
            st.pendingReset = false;
            st.hasPlatform = false;
            clearCycleTrackers(st);
            return true;
        }

        // Snapshot pre-preset settings ONLY on Off -> Preset transition.
        if (!st.snapshotActive)
        {
            memcpy(&st.snapshotCpu, &cpuOpts, sizeof(cpuOpts));
            memcpy(&st.snapshotCombo, &comboCfg, sizeof(comboCfg));
            st.snapshotPlayerValid = generalCfg != NULL;
            if (generalCfg != NULL)
            {
                st.snapshotPlayerPercent = generalCfg->playerPercent;
                st.snapshotLockPlayerPercent = generalCfg->lockPlayerPercent;
                st.snapshotPlayerInfShields = generalCfg->playerInfiniteShields;
            }
            st.snapshotCpuValid = cpuSettings != NULL;
            if (cpuSettings != NULL)
            {
                st.snapshotCpuPercent = cpuSettings->percent;
                st.snapshotLockCpuPercent = cpuSettings->lockPercent;
            }
            st.snapshotOsdValid = osdCfg != NULL;
            if (osdCfg != NULL)
            {
                st.snapshotGrabBreakoutOsd = osdCfg->grabBreakout;
            }
            st.snapshotTechInvisValid = techInvisCfg != NULL;
            if (techInvisCfg != NULL)
            {
                memcpy(&st.snapshotTechInvis, techInvisCfg, sizeof(*techInvisCfg));
            }
            st.snapshotActive = true;
        }
        else
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
        }

        applyPresetOverrides(cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
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

    void restoreScenarioSnapshot(ScenarioState& st, cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg,
                                 main::GeneralSettings* generalCfg, main::CpuSettings* cpuSettings,
                                 main::OsdSettings* osdCfg, main::TechInvisSettings* techInvisCfg)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
            st.snapshotPlayerValid = false;
            st.snapshotCpuValid = false;
            st.snapshotOsdValid = false;
            st.snapshotTechInvisValid = false;
            st.snapshotActive = false;
        }
    }

    void discardScenarioSnapshot(ScenarioState& st)
    {
        st.snapshotActive = false;
        st.snapshotPlayerValid = false;
        st.snapshotCpuValid = false;
        st.snapshotOsdValid = false;
        st.snapshotTechInvisValid = false;
        st.activePreset = kScenarioOff;
        st.pendingReset = false;
        st.hasPlatform = false;
        clearCycleTrackers(st);
    }

    void projectScenarioSavedView(const ScenarioState& st, cpu::CpuOptions& cpuOpts,
                                  main::ComboTrainerSettings& comboCfg, main::GeneralSettings* generalCfg,
                                  main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg,
                                  main::TechInvisSettings* techInvisCfg)
    {
        if (st.snapshotActive)
        {
            copySnapshotTo(st, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
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
#pragma dont_inline reset

    static inline float clampBlastY(const StageGeometry& geom, float targetY, float botPad, float fbBot)
    {
        float loY = geom.blast.valid ? (geom.blast.bottom + botPad) : fbBot;
        float hiY = geom.blast.valid ? (geom.blast.top - 30.0f) : (fbBot + 74.0f);
        return clampf(targetY, loY, maxf(loY, hiY));
    }

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
        float floorLeft = geom.floor.leftX;
        float floorRight = geom.floor.rightX;
        float centerX = 0.5f * (floorLeft + floorRight);
        float halfW = 0.5f * (floorRight - floorLeft);
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
                if (idx >= nPlats)
                {
                    idx = nPlats - 1;
                }
                const StagePlatform& plat = sortedPlats[idx];
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                out.hasPlatform = true;
                out.resolvedTechPos = static_cast<u8>(idx + 1);
                if (tumbleDrop)
                {
                    float hx = clampf(platMid - dir * 6.0f, floorLeft + 8.0f, floorRight - 8.0f);
                    float hlr = platMid >= hx ? 1.0f : -1.0f;
                    setSpawnPair(out, hx, floorY, hlr, platMid, plat.y + 18.0f, -hlr);
                    out.cpuFalling = true;
                    out.cpuTumbleDrop = true;
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
                float span = tumbleDrop ? minf(10.0f, halfW * 0.26f) : minf(14.0f, halfW * 0.35f);
                setSymmetricFloor(out, centerX, floorY, dir, span);
                if (tumbleDrop)
                {
                    out.cpuY = floorY + 18.0f;
                    out.cpuFalling = true;
                    out.cpuTumbleDrop = true;
                }
            }
            break;
        }

        case kScenarioEdgeguard:
        {
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            float inward = -outward;
            float sx = ledge.x + outward * 38.0f;
            float sy = ledge.y - 10.0f;
            if (cfg.edgeguardSetup == kEgSetupSpawn)
            {
                sampleEdgeguardSpawn(geom, cfg.edgeguardDist, effSide, rng, sx, sy);
            }
            else
            {
                float maxOut = leftSide ? (ledge.x - geom.blast.left - 28.0f) : (geom.blast.right - ledge.x - 28.0f);
                sx = ledge.x + outward * clampf(38.0f, 18.0f, maxf(18.0f, maxOut));
                sy = clampBlastY(geom, ledge.y - 10.0f, 40.0f, ledge.y - 40.0f);
            }
            setSpawnPair(out, ledge.x + inward * minf(14.0f, halfW * 0.40f), ledge.y, outward, sx, sy, inward);
            out.cpuFalling = true;
            break;
        }

        case kScenarioShieldPressure:
        {
            float baseDist = 12.0f;
            if (cfg.shieldRole == kShieldRoleDefense)
            {
                if (cfg.shieldDefAttack == kShieldDefGetup)
                {
                    baseDist = 9.0f;
                    out.cpuDownWait = true;
                }
                else
                {
                    baseDist = cfg.shieldDefAttack == kShieldDefDash ? 22.0f : 14.0f;
                }
                baseDist = clampf(baseDist + static_cast<float>(cfg.spacingOffset), 7.0f, 34.0f);
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
                out.hmnX = platMid;
                out.hmnY = plat.y;
                out.hmnLr = inward;

                u8 spaceMode = cfg.shieldDropSpacing == kSdSpaceRandom
                                   ? static_cast<u8>(1u + (geomNextRng(rng) % 3u))
                                   : cfg.shieldDropSpacing;
                static const float kSdOff[4] = {0.0f, 6.0f, 0.0f, 14.0f};
                float offset = inward * ((spaceMode < 4 ? kSdOff[spaceMode] : 0.0f) + static_cast<float>(cfg.spacingOffset));
                out.cpuX = clampf(platMid + offset, floorLeft + 10.0f, floorRight - 10.0f);
                out.cpuY = floorY;

                float towardP1 = absf(platMid - out.cpuX) > 0.5f ? (platMid >= out.cpuX ? 1.0f : -1.0f) : -inward;
                u8 faceMode = cfg.shieldDropFacing == kSdFaceRandom ? static_cast<u8>(geomNextRng(rng) & 1u)
                                                                    : cfg.shieldDropFacing;
                out.cpuLr = faceMode == kSdFaceAway ? -towardP1 : towardP1;
            }
            else
            {
                setSymmetricFloor(out, centerX, floorY, dir, minf(10.0f, halfW * 0.30f));
            }
            break;
        }

        case kScenarioLedgetech:
        {
            static const float kLtPos[4][2] = {
                {8.0f, -20.0f},  // CloseLow
                {12.0f, -36.0f}, // DeepLow
                {26.0f, -12.0f}, // FarOut
                {18.0f, 10.0f}   // High
            };
            const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
            float outward = leftSide ? -1.0f : 1.0f;
            float inward = -outward;
            u8 spawnMode = cfg.ledgetechSpawn == kLtSpawnRandom ? static_cast<u8>(geomNextRng(rng) % 4u)
                                                                : cfg.ledgetechSpawn;
            float maxOut = leftSide ? (ledge.x - geom.blast.left - 24.0f) : (geom.blast.right - ledge.x - 24.0f);
            float outDist = clampf(kLtPos[spawnMode][0], 6.0f, maxf(8.0f, maxOut));
            u8 atk = cfg.ledgetechAttack < 7 ? cfg.ledgetechAttack : static_cast<u8>(kLtAtkDsmash);
            setSpawnPair(out, ledge.x + outward * outDist,
                         clampBlastY(geom, ledge.y + kLtPos[spawnMode][1], 36.0f, ledge.y - 44.0f), inward,
                         ledge.x + inward * minf(ledgetechCpuInwardOffset(atk), halfW * 0.25f), ledge.y, outward);
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
                float platMid = 0.5f * (plat.leftX + plat.rightX);
                bool rightPlat = platMid >= centerX;
                float edgeX = rightPlat ? plat.leftX : plat.rightX;
                float intoPlat = rightPlat ? 1.0f : -1.0f;
                float platW = plat.rightX - plat.leftX;
                if (cfg.slideOffAttack == kSlideAtkOnPlatFtilt)
                {
                    out.hmnX = edgeX + intoPlat * minf(3.5f, platW * 0.20f);
                    out.hmnY = plat.y;
                    out.hmnLr = intoPlat;
                    out.cpuX = out.hmnX + intoPlat * minf(9.0f, maxf(5.5f, platW - 6.0f));
                    out.cpuY = plat.y;
                }
                else
                {
                    out.hmnX = edgeX + intoPlat * minf(4.0f, platW * 0.22f);
                    out.hmnY = plat.y;
                    out.hmnLr = intoPlat;
                    float underOffset = cfg.slideOffAttack == kSlideAtkUnderFair ? 5.0f : 2.5f;
                    out.cpuX = clampf(out.hmnX + intoPlat * underOffset, floorLeft + 8.0f, floorRight - 8.0f);
                    out.cpuY = floorY;
                }
                out.cpuLr = -intoPlat;
            }
            else
            {
                const LedgeInfo& ledge = leftSide ? geom.leftLedge : geom.rightLedge;
                float inward = ledge.dir;
                out.hmnX = ledge.x + inward * 4.5f;
                out.hmnY = ledge.y;
                out.hmnLr = inward;
                out.cpuX = ledge.x + inward * minf(13.5f, halfW * 0.45f);
                out.cpuY = ledge.y;
                out.cpuLr = -inward;
            }
            break;
        }

        case kScenarioGrabEscape:
            setSymmetricFloor(out, centerX, floorY, dir, minf(3.25f, halfW * 0.20f));
            break;

        case kScenarioMultiHitSdi:
        {
            static const float kSdiSp[kMultiHitMoveCount] = {
                5.5f, 6.0f, 7.5f, 5.5f, 5.0f, 7.0f, 5.5f, 10.5f, 5.5f
            };
            u8 move = resolveMultiHitMove(cfg.multiHitMove, cpuFtKind);
            float spacing = move < kMultiHitMoveCount ? kSdiSp[move] : 5.5f;
            setSymmetricFloor(out, centerX, floorY, -dir, minf(spacing * 0.5f, halfW * 0.30f));
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
#pragma dont_inline reset

    void stepScenario(ScenarioState& st, const ScenarioConfig& rawCfg, const ScenarioStepInput& in,
                      cpu::CpuOptions& cpuOpts, main::ComboTrainerSettings& comboCfg, ScenarioStepAction& act,
                      main::GeneralSettings* generalCfg, main::CpuSettings* cpuSettings, main::OsdSettings* osdCfg,
                      main::TechInvisSettings* techInvisCfg)
    {
        memset(&act, 0, sizeof(act));
        act.restoreCorrectKind[0] = kCorrectGroundCliffStop;
        act.restoreCorrectKind[1] = kCorrectGroundCliffStop;

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
        ScenarioConfig cfg;
        sanitizeScenarioConfigInto(rawCfg, cfg);
        applyScenarioConfig(st, cfg, cpuOpts, comboCfg, generalCfg, cpuSettings, osdCfg, techInvisCfg);
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

        if (autoReset && st.activePreset >= kScenarioTechchase && st.activePreset != kScenarioEdgeguard
            && (hmnDanger || cpuDanger))
        {
            wantReset = true;
        }
        else switch (st.activePreset)
        {
        case kScenarioComboLab:
            if (autoReset && hmnDanger)
            {
                wantReset = true;
            }
            break;

        case kScenarioTechchase:
        {
            bool inTechOrDown = isDownOrTechStatus(in.cpuStatus);
            bool inTumble = in.cpuAir && isTumbleStatus(in.cpuStatus);
            bool cpuStruck = in.cpuHitstun > 0 || in.cpuHitlag > 0 || isCaptureHoldStatus(in.cpuStatus)
                             || in.cpuStatus == kStCaptureThrown;
            bool hmnSwungNow = isAttackOrSpecial(in.hmnStatus) && !isAttackOrSpecial(st.prevHmnStatus);

            if (inTechOrDown)
            {
                st.techEngaged = true;
                st.graceFrames = 0;
                st.techActFrames = 0;
                st.techLateRecorded = false;
                if (st.techFrames < 600)
                {
                    st.techFrames++;
                }
                if (in.cpuIntangible && hmnSwungNow && st.techEarlyFrames == 0)
                {
                    int rem = 21 - static_cast<int>(st.techFrames);
                    st.techEarlyFrames = static_cast<u8>(rem > 1 ? rem : 1);
                }
            }
            else if (inTumble)
            {
                st.techEngaged = true;
                st.graceFrames = 0;
                st.techActFrames = 0;
            }
            else if (st.techEngaged)
            {
                if (cpuStruck)
                {
                    if (!st.techCoveredThisRep && st.techFrames > 0)
                    {
                        st.techCoveredThisRep = true;
                        st.techResult = kTechResCover;
                        st.techDeltaFrames = 0;
                        st.techReactionFrames = (in.cpuTechDist > 0 && static_cast<int>(st.techFrames) >= in.cpuTechDist)
                                                    ? static_cast<s16>(static_cast<int>(st.techFrames) - in.cpuTechDist)
                                                    : static_cast<s16>(-1);
                        st.techStreak++;
                        if (st.techStreak > st.techBest)
                        {
                            st.techBest = st.techStreak;
                        }
                    }
                    st.techFrames = 0;
                    st.techActFrames = 0;
                    st.techEarlyFrames = 0;
                    st.graceFrames = 0;
                }
                else
                {
                    bool groundedSettled = isNeutralGrounded(in.cpuStatus, in.cpuAir, in.cpuHitstun, in.cpuHitlag)
                                           || (!in.cpuAir && in.cpuActionable);
                    bool airEscaped = in.cpuAir && !isTumbleStatus(in.cpuStatus) && !isDownOrTechStatus(in.cpuStatus)
                                      && (in.cpuActionable || isAirEscapeStatus(in.cpuStatus));
                    if (groundedSettled || airEscaped)
                    {
                        if (st.techActFrames < 600)
                        {
                            st.techActFrames++;
                        }
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
                        if (st.graceFrames == 0)
                        {
                            st.completionCount++;
                        }
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
            }
            break;
        }

        case kScenarioEdgeguard:
            if (autoReset && hmnDanger)
            {
                wantReset = true;
                resetCpu = false;
            }
            break;

        case kScenarioShieldPressure:
        {
            bool activeExchange = in.hmnHitstun > 0 || in.hmnHitlag > 0 || in.cpuHitstun > 0 || in.cpuHitlag > 0
                                  || isAttackOrSpecial(in.cpuStatus) || isAttackOrSpecial(in.hmnStatus);
            if (cfg.shieldRole == kShieldRoleDefense)
            {
                bool hmnBlocked = in.hmnStatus == kStGuardDamage || in.hmnShieldStun > 0;
                if (isAttackOrSpecial(in.cpuStatus))
                {
                    st.shieldDefSwung = true;
                    st.shieldEngaged = true;
                }
                if (hmnBlocked || in.cpuHitstun > 0 || in.cpuHitlag > 0)
                {
                    if (hmnBlocked && !st.prevCpuGuardDamage)
                    {
                        st.shieldHitCount++;
                        st.completionCount++;
                    }
                    st.shieldEngaged = true;
                    st.graceFrames = 0;
                }
                st.prevCpuGuardDamage = hmnBlocked;
                if (st.shieldEngaged && st.shieldDefSwung)
                {
                    if (activeExchange || hmnBlocked)
                    {
                        st.graceFrames = 0;
                    }
                    else if (tickGrace(st, autoReset, kShieldGraceFrames))
                    {
                        wantReset = true;
                    }
                }
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
                if (tickGrace(st, autoReset, 12))
                {
                    wantReset = true;
                }
            }
            else if (st.shieldEngaged)
            {
                if (isAttackOrSpecial(in.cpuStatus) || in.cpuStatus == kStGuardOff
                    || (in.cpuStatus >= 0x1E && in.cpuStatus <= 0x20)
                    || in.cpuStatus == kStJumpSquat || in.cpuStatus == kStJump)
                {
                    st.oosCountered = true;
                }
                if (activeExchange || cpuGuardDmg)
                {
                    st.graceFrames = 0;
                }
                else if (st.oosCountered || !isGuardStatus(in.cpuStatus) || in.cpuStatus == kStGuardOff)
                {
                    if (tickGrace(st, autoReset, kShieldGraceFrames))
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
            if (isGuardStatus(in.hmnStatus))
            {
                st.hmnGuardWindow = 4;
            }
            else if (st.hmnGuardWindow > 0)
            {
                st.hmnGuardWindow--;
            }
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
                if ((settledOnFloor || st.graceFrames > 0) && tickGrace(st, autoReset, kShieldDropGraceFrames))
                {
                    wantReset = true;
                }
            }
            break;
        }

        case kScenarioLedgetech:
            if (isWallTechStatus(in.hmnStatus))
            {
                if (!st.sawWallTech)
                {
                    st.sawWallTech = true;
                    st.completionCount++;
                }
                st.ledgetechDone = true;
            }
            else if (isLedgeStatus(in.hmnStatus)
                     || (st.sinceResetFrames > 6 && !in.hmnAir && in.geom.floor.valid
                         && in.hmnX >= in.geom.floor.leftX && in.hmnX <= in.geom.floor.rightX))
            {
                st.ledgetechDone = true;
            }
            if (st.ledgetechDone && tickGrace(st, autoReset, kLedgetechGraceFrames))
            {
                wantReset = true;
            }
            break;

        case kScenarioSlideOff:
            if (!in.hmnAir && st.sinceResetFrames >= 1)
            {
                st.slideOffArmed = true;
            }
            if (!st.slideOffDone && st.slideOffArmed && st.havePrev && !st.prevHmnAir && in.hmnAir
                && (isTumbleStatus(in.hmnStatus) || in.hmnHitstun > 0))
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
                if (settled && tickGrace(st, autoReset, kSlideOffGraceFrames))
                {
                    wantReset = true;
                }
            }
            break;

        case kScenarioGrabEscape:
        {
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
                if ((settled || st.graceFrames > 0) && tickGrace(st, autoReset, kGrabGraceFrames))
                {
                    wantReset = true;
                }
            }
            break;
        }

        case kScenarioMultiHitSdi:
        {
            bool hmnInDamageLag = in.hmnHitlag > 0 && in.hmnDamageHitlag;
            if (hmnInDamageLag && (!st.havePrev || in.hmnHitlag > st.prevHmnHitlag || st.prevHmnHitlag == 0))
            {
                if (st.sdiLatchedReset)
                {
                    clearEventTelemetry(st);
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
                bool escapedNow = (in.hmnHitstun == 0 && in.hmnActionable)
                                  || (totalSdi >= 0.5f && (absf(in.hmnX - in.cpuX) > 16.0f || in.hmnY - in.cpuY > 14.0f));
                if (escapedNow)
                {
                    if (!st.sdiEscaped && (totalSdi >= 0.5f || st.sdiHitCount >= 1))
                    {
                        st.sdiEscaped = true;
                        st.completionCount++;
                    }
                    if (tickGrace(st, autoReset, kSdiEscapeGraceFrames))
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
                cpuEmit(out, cpu::button::kJump, isFair ? (toward * 0.80f) : clampf(dx / 12.0f, -0.5f, 0.5f), 0.0f);
                cpuSetPhase(st, 2);
            }
        }
        else if (st.phase == 2)
        {
            st.timer++;
            if (isFair) out.stickX = clampf(dx / 8.0f, -1.0f, 1.0f);
            if (in.cpuAir && (!isFair || in.cpuVy <= 0.1f || st.timer >= 8))
            {
                cpuCStick(out, isFair ? in.cpuLr : 0.0f, isFair ? 0.0f : 1.0f);
                cpuSetPhase(st, 3);
            }
            else if (!in.cpuAir && st.timer >= 12) cpuSetPhase(st, 1);
        }
        else if (!cpuAirWaitEnd(st, in.cpuAir, 28, isFair ? 36 : 28, true))
        {
            if (isFair) out.stickX = clampf(dx / 10.0f, -0.7f, 0.7f);
            if (isFair ? (in.cpuVy < 0.0f) : (in.cpuVy <= 0.0f)) out.stickY = -1.0f;
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

        if (in.preset == kScenarioMultiHitSdi)
        {
            u8 move = resolveMultiHitMove(in.multiHitMove, in.cpuFtKind);
            if (st.lastMove != move) { scenarioCpuStateReset(st); st.lastMove = move; }
            out.active = true;
            if (st.cooldown > 0) { st.cooldown--; return; }

            if (move >= kMultiHitNair && move <= kMultiHitDownAir)
            {
                float takeoffDiv = move == kMultiHitUpAir ? 14.0f : (move == kMultiHitFair ? 11.0f : 9.0f);
                float approachDist = move == kMultiHitFair ? 10.5f : 8.5f;
                float takeoffSteerX = clampf(dx / takeoffDiv, -1.0f, 1.0f);
                float airSteerX = clampf((dx - in.cpuVx * 8.0f) / 3.0f, -1.0f, 1.0f);
                if (st.phase == 0)
                {
                    if (!in.cpuAir && in.cpuCanAct)
                    {
                        if (absDx > approachDist || (in.hmnAir && in.hmnY > in.cpuY + 18.0f))
                        {
                            out.stickX = clampf(dx / 5.0f, -0.55f, 0.55f);
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
                        if (move == kMultiHitDownAir) { cpuCStick(out, 0.0f, -1.0f); cpuSetPhase(st, 3); }
                        else if (move == kMultiHitNair) { out.buttons = cpu::button::kAttack; cpuSetPhase(st, 3); }
                        else if (move == kMultiHitFair) { cpuCStick(out, in.cpuLr, 0.0f); cpuSetPhase(st, 3); }
                        else { out.stickX = airSteerX; cpuSetPhase(st, 2, 1); }
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
                    if (readyForUpAir) { cpuCStick(out, 0.0f, 1.0f); cpuSetPhase(st, 3); }
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
                float range, walkSpd, sxMul, sy, steerDiv, steerMax;
                u32 btn;
                u8 needFace, mash, endT, cd;
            };
            static const GroundSdiSpec kGnd[4] = {
                {8.5f, 0.65f, 0.0f, 0.0f, 0.0f, 0.0f, cpu::button::kAttack, 1, 1, 24, 22},   // RapidJab
                {8.0f, 0.60f, 0.0f, 1.0f, 6.0f, 0.45f, cpu::button::kSpecial, 1, 0, 14, 26}, // UpB
                {14.0f, 0.60f, 1.0f, 0.0f, -1.0f, 1.0f, cpu::button::kSpecial, 0, 0, 12, 22},// SideB
                {8.0f, 0.60f, 0.0f, -1.0f, 5.0f, 0.55f, cpu::button::kSpecial, 0, 1, 16, 24} // DownB
            };
            const GroundSdiSpec& g = kGnd[move >= kMultiHitRapidJab && move <= kMultiHitDownB ? (move - kMultiHitRapidJab) : 0];
            if (st.phase == 0)
            {
                if (!in.cpuAir && in.cpuCanAct)
                {
                    if (cpuApproachOrFace(in, absDx, toward, g.range, g.walkSpd, g.needFace != 0, out)) return;
                    cpuEmit(out, g.btn, toward * g.sxMul, g.sy);
                    cpuSetPhase(st, 1, g.mash);
                }
            }
            else
            {
                st.timer++;
                if (g.steerDiv < 0.0f) out.stickX = toward;
                else if (g.steerDiv > 0.0f) out.stickX = clampf(dx / g.steerDiv, -g.steerMax, g.steerMax);
                if (g.mash != 0 && (st.timer & 1) == 0 && (in.lastButtons & g.btn) == 0) out.buttons = g.btn;
                if (st.timer >= g.endT) cpuResetPhase(st, g.cd, false);
            }
            return;
        }

        if (in.preset == kScenarioLedgetech)
        {
            u8 atk = in.ledgetechAttack < 7 ? in.ledgetechAttack : static_cast<u8>(kLtAtkDtilt);
            if (atk == kLtAtkDair && st.phase > 0)
            {
                out.active = true;
                if (st.phase == 1)
                {
                    st.timer++;
                    if (in.cpuAir) { cpuCStick(out, 0.0f, -1.0f); cpuSetPhase(st, 2); }
                    else if (st.timer >= 10) cpuResetPhase(st, 0, false);
                }
                else
                {
                    out.stickX = -toward * 0.35f;
                    cpuAirWaitEnd(st, in.cpuAir, 24, 20, false);
                }
                return;
            }
            if (st.cooldown > 0) { st.cooldown--; return; }
            float maxX = (atk == kLtAtkNeutralB || atk == kLtAtkSideB) ? 16.0f : 12.4f;
            bool inLedgeWindow = in.ledgetechTiming == kLtTimeHoldActive
                                 || (absDx <= maxX && in.hmnY >= in.cpuY - 18.0f && in.hmnY <= in.cpuY + 20.0f);
            if (!in.cpuAir && in.cpuCanAct && inLedgeWindow && in.hmnHitstun == 0)
            {
                out.active = true;
                if (in.cpuLr * toward < 0.0f) { out.stickX = toward * 0.45f; return; }
                if (atk == kLtAtkDair) { cpuEmit(out, cpu::button::kJump, toward * 0.20f, 0.0f); cpuSetPhase(st, 1); return; }
                struct LtAtkSpec { u32 btn; float sxMul; float sy; u8 cd; };
                static const LtAtkSpec kLt[7] = {
                    {cpu::button::kAttack, 0.0f, -0.55f, 16},
                    {cpu::button::kAttack, 0.55f, 0.0f, 18},
                    {cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick, 1.0f, 0.0f, 28},
                    {cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick, 0.0f, -1.0f, 26},
                    {0, 0.0f, 0.0f, 0},
                    {cpu::button::kSpecial, 0.0f, 0.0f, 24},
                    {cpu::button::kSpecial, 1.0f, 0.0f, 28}
                };
                cpuEmit(out, kLt[atk].btn, toward * kLt[atk].sxMul, kLt[atk].sy);
                st.cooldown = kLt[atk].cd;
            }
            return;
        }

        if (in.preset == kScenarioShieldPressure && in.shieldRole == kShieldRoleDefense)
        {
            out.active = true;
            if (st.cooldown > 0) { st.cooldown--; return; }
            if (cpuWaitDelay(st, in.rng, 45, 30)) return;
            u8 defAtk = in.shieldDefAttack < 5 ? in.shieldDefAttack : static_cast<u8>(kShieldDefAerial);
            if (defAtk == kShieldDefGetup)
            {
                if (in.cpuStatus == 0x4D || in.cpuStatus == 0x4E || in.cpuStatus == 0x50)
                {
                    out.buttons = cpu::button::kAttack;
                    cpuResetPhase(st, 40, true);
                }
                return;
            }
            if (defAtk == kShieldDefAerial) { stepJumpPoke(in, st, out, dx, absDx, toward, true); return; }
            if (defAtk == kShieldDefDash)
            {
                if (!in.cpuAir)
                {
                    out.stickX = toward;
                    if (absDx <= 14.0f && (in.cpuStatus == 0x03 || in.cpuStatus == 0x04))
                    {
                        out.buttons = cpu::button::kAttack;
                        cpuResetPhase(st, 40, true);
                    }
                }
                return;
            }
            if (!in.cpuAir && in.cpuCanAct)
            {
                if (cpuApproachOrFace(in, absDx, toward, defAtk == kShieldDefSmash ? 13.0f : 11.5f, 0.65f, true, out)) return;
                if (defAtk == kShieldDefSmash) cpuCStick(out, toward, 0.0f);
                else cpuEmit(out, cpu::button::kAttack, toward * 0.55f, 0.0f);
                cpuResetPhase(st, 38, true);
            }
            return;
        }

        if (in.preset == kScenarioShieldDrop && in.shieldDropAttack != kSdAtkOff)
        {
            out.active = true;
            if (st.cooldown > 0) { st.cooldown--; return; }
            if (cpuWaitDelay(st, in.rng, 30, 36)) return;
            if (in.shieldDropAttack == kSdAtkShAerial) { stepJumpPoke(in, st, out, dx, absDx, toward, false); return; }
            if (!in.cpuAir && in.cpuCanAct)
            {
                if (cpuApproachOrFace(in, absDx, toward, 10.0f, 0.60f, false, out)) return;
                if (in.shieldDropAttack == kSdAtkDsmash) cpuCStick(out, 0.0f, -1.0f);
                else cpuEmit(out, cpu::button::kAttack, 0.0f, 0.55f);
                cpuResetPhase(st, 34, true);
            }
            return;
        }

        if (in.preset == kScenarioSlideOff)
        {
            if (in.hasPlatform && in.slideOffAttack != kSlideAtkOnPlatFtilt)
            {
                out.active = true;
                if (st.cooldown > 0) { st.cooldown--; return; }
                if (st.phase == 0)
                {
                    if (!in.cpuAir && !in.hmnAir && in.cpuCanAct && in.hmnHitstun == 0)
                    {
                        bool isFair = in.slideOffAttack == kSlideAtkUnderFair;
                        if (cpuApproachOrFace(in, absDx, toward, isFair ? 8.5f : 5.5f, 0.60f, isFair, out)) return;
                        cpuEmit(out, cpu::button::kJump, clampf(dx / 14.0f, -0.45f, 0.45f), 0.0f);
                        cpuSetPhase(st, 1, 1);
                    }
                }
                else if (st.phase == 1)
                {
                    st.timer++;
                    if (!in.cpuAir)
                    {
                        cpuEmit(out, cpu::button::kJump, clampf(dx / 14.0f, -0.45f, 0.45f), 0.0f);
                        if (st.timer >= 12) cpuResetPhase(st, 0, false);
                    }
                    else if (st.timer < 5)
                    {
                        cpuEmit(out, cpu::button::kJump, clampf(dx / 12.0f, -0.5f, 0.5f), 0.0f);
                    }
                    else if (in.cpuY >= in.hmnY - 16.0f || st.timer >= 10)
                    {
                        if (in.slideOffAttack == kSlideAtkUnderFair) cpuCStick(out, in.cpuLr, 0.0f);
                        else if (in.slideOffAttack == kSlideAtkUnderNair) out.buttons = cpu::button::kAttack;
                        else cpuCStick(out, 0.0f, 1.0f);
                        cpuSetPhase(st, 2);
                    }
                    else
                    {
                        out.stickX = clampf(dx / 12.0f, -0.5f, 0.5f);
                    }
                }
                else
                {
                    cpuAirWaitEnd(st, in.cpuAir, 30, 34, false);
                }
                return;
            }
            if (!in.cpuAir && in.cpuStatus == kStAttackS3 && (in.lastButtons & cpu::button::kAttack) == 0)
            {
                out.active = true;
                cpuEmit(out, cpu::button::kAttack, toward * 0.55f, 0.0f);
                return;
            }
            if (st.cooldown > 0) { st.cooldown--; return; }
            if (!in.cpuAir && !in.hmnAir && in.cpuCanAct && absDx < 22.0f && in.hmnHitstun == 0)
            {
                out.active = true;
                cpuEmit(out, cpu::button::kAttack, toward * 0.55f, 0.0f);
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
                    st.resolvedThrow = in.grabAction == kGrabActRandThrow
                                           ? static_cast<u8>(kGrabActFThrow + (in.rng & 3u))
                                           : in.grabAction;
                }
                else if (st.timer < 255)
                {
                    st.timer++;
                }
                if (in.grabAction != kGrabActRandThrow)
                {
                    st.resolvedThrow = in.grabAction;
                }
                if (in.grabAction != kGrabActHold && in.cpuStatus == kStCatchWait && st.timer >= 2)
                {
                    if (st.resolvedThrow == kGrabActFThrow) out.stickX = in.cpuLr;
                    else if (st.resolvedThrow == kGrabActBThrow) out.stickX = -in.cpuLr;
                    else out.stickY = st.resolvedThrow == kGrabActUThrow ? 1.0f : -1.0f;
                }
                return;
            }
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
            if (cpuWaitDelay(st, in.rng, 40, 60))
            {
                return;
            }
            if (!in.cpuAir && in.cpuCanAct)
            {
                if (cpuApproachOrFace(in, absDx, toward, 8.5f, 0.65f, true, out))
                {
                    return;
                }
                out.buttons = cpu::button::kAttack | cpu::button::kGuard;
                st.cooldown = 20;
            }
        }
    }

    void stepPowershieldDrillCpu(const ScenarioCpuInput& in, ScenarioCpuState& st, ScenarioCpuOutput& out)
    {
        memset(&out, 0, sizeof(out));
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
        if (absf(dx) > 11.0f)
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
