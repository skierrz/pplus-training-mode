#pragma once

#include <string.h>
#include <types.h>

#include "extra/stage_geom_math.h"

// Pure host-testable Project+ 3.2.1 Tech Practice driver core. Zero Brawl
// headers so all state machines, projectile timing tables, cliff-hang box
// snap math, wavedash target zone math, target patrol logic, and solo CPU
// standby transitions compile identically on PC (`make check-host`) and
// PowerPC (MWCC 4.2).
//
// Verified Project+ 3.2.1 ground truth:
//  - Neutral B projectile startup & short-hop physics: Fit*.pac attributes
//    (+0x0C Ground_Brake, +0x30 Jump_Squat, +0x44 Mini_Jump_Speed_Y,
//    +0x64 Air_Accel_Y) and SpecialN / SpecialAirN PSA scripts.
//  - Cliff-hang box geometry: Ledge.asm:16-52 (HOOK @ 0x8013598C -> 0x8056F608)
//    cancels negative offsetX (-4.0f * scale) on the front reach so the P+ grab
//    box spans [posX + lr*offsetX, posX + lr*(width + ecbHalfFront)] x
//    [posY + offsetY, posY + offsetY + height] (grCollStatus +0x3C..+0x48).
//  - Cliff_Wait auto-fall timer: RA-Basic[0] (0x20000000) -> RA-Bit[18] (0x8087ABC4).
//  - 5-ledge-grab invuln counter: LA-Basic[79] (0x1000004F, Ledge.asm:495-513).
//  - Wavedash initial speed & decay: v0 = 3.1 * cos(16.26 deg) * 0.9 = 2.6784,
//    10f Landing_Fall_Special (AirDodge.asm:77-302).
namespace tm {
namespace extra {
    enum PracticeDrillId {
        kPracticeDrillNone = 0,
        kPracticeDrillLedgedash = 1,
        kPracticeDrillWavedash = 2,
        kPracticeDrillLCancel = 3,
        kPracticeDrillHoverFloat = 4,
        kPracticeDrillPowershield = 5,
        kPracticeDrillRar = 6,
        kPracticeDrillDacus = 7,
        kPracticeDrillBReverse = 8,
        kPracticeDrillCount = 9
    };

    enum PsAttackMode {
        kPsAtkProjectile = 0,
        kPsAtkPhysical = 1,
        kPsAtkModeCount = 2
    };

    enum PsHeight {
        kPsHeightLow = 0,
        kPsHeightMid = 1,
        kPsHeightHigh = 2,
        kPsHeightGround = 3,
        kPsHeightRandom = 4,
        kPsHeightCount = 5
    };

    enum PsCadence {
        kPsCadenceSlow = 0,
        kPsCadenceMedium = 1,
        kPsCadenceFast = 2,
        kPsCadenceRandom = 3,
        kPsCadenceCount = 4
    };

    enum LedgeResetSide {
        kLedgeSideSame = 0,
        kLedgeSideOpposite = 1,
        kLedgeSideRandom = 2,
        kLedgeSideCount = 3
    };

    enum LedgeInvulnLimit {
        kLedgeInvulnPPlus5 = 0,
        kLedgeInvulnUnlimited = 1,
        kLedgeInvulnCount = 2
    };

    enum PracticeTargetMode {
        kTargetOff = 0,
        kTargetStationary = 1,
        kTargetShielding = 2,
        kTargetMoving = 3,
        kTargetModeCount = 4
    };

    enum WdZoneOutcome {
        kWdZoneNone = 0,
        kWdZoneHit = 1,
        kWdZoneShort = 2,
        kWdZoneOvershoot = 3
    };

    enum StandbyTransition {
        kStandbyNoop = 0,
        kStandbyEnter = 1,
        kStandbyHold = 2,
        kStandbyExit = 3
    };

    enum PracticeBtnMask {
        kPrBtnAttack = 0x1,
        kPrBtnSpecial = 0x2,
        kPrBtnJump = 0x4,
        kPrBtnGuard = 0x8
    };

    enum ProjectileFlags {
        kProjFlagNone = 0,
        kProjFlagHoldRelease = 1 << 0,
        kProjFlagChargeTap = 1 << 1,
        kProjFlagGroundOnly = 1 << 2
    };

    struct PracticeConfig {
        u8 psAttackMode;
        u8 psHeight;
        u8 psCadence;
        u8 ledgeResetSide;
        u8 ledgeAutoReset;
        u8 ledgeInvulnLimit;
        u8 lcancelTarget;
        u8 floatDjcTarget;
        u8 wdTargetZone;
        u8 reserved[3];
    };

    inline void practiceConfigDefaults(PracticeConfig& cfg)
    {
        static const u8 kDefs[9] = {
            kPsAtkProjectile, kPsHeightLow, kPsCadenceMedium,
            kLedgeSideSame, 1, kLedgeInvulnPPlus5,
            kTargetStationary, kTargetStationary, 0
        };
        memset(&cfg, 0, sizeof(cfg));
        memcpy(&cfg.psAttackMode, kDefs, 9);
    }

    inline void sanitizePracticeConfig(PracticeConfig& cfg)
    {
        static const u8 kMax[9] = {
            kPsAtkModeCount, kPsHeightCount, kPsCadenceCount,
            kLedgeSideCount, 2, kLedgeInvulnCount,
            kTargetModeCount, kTargetModeCount, 2
        };
        static const u8 kDef[9] = {
            kPsAtkProjectile, kPsHeightLow, kPsCadenceMedium,
            kLedgeSideSame, 1, kLedgeInvulnPPlus5,
            kTargetStationary, kTargetStationary, 0
        };
        u8* f = &cfg.psAttackMode;
        for (int i = 0; i < 9; i++)
        {
            if (i == 4 || i == 8)
            {
                f[i] = f[i] != 0 ? 1 : 0;
            }
            else if (f[i] >= kMax[i])
            {
                f[i] = kDef[i];
            }
        }
    }

    // -------------------------------------------------------------------------
    // 1. Powershield Projectile Table & CPU Driver (verified P+ 3.2.1 Fit*.pac)
    // -------------------------------------------------------------------------
    struct ProjectileProfile {
        u8 ftKind;
        u8 shStartup;
        u8 shAirFrames;
        u8 holdBFrames;
        u8 flags;
        u8 spacing;
    };

    // Ivysaur (Synthesis/Solarbeam) and Snake (timed bouncing grenade) are
    // intentionally omitted so both cleanly fall back to Physical (Jab).
    inline const ProjectileProfile* findProjectileProfile(int ftKind)
    {
        static const ProjectileProfile kTable[] = {
            {0x06,  8, 20,  1, kProjFlagNone,        36}, // Fox
            {0x13, 12, 24,  1, kProjFlagNone,        36}, // Falco
            {0x2C, 14, 25,  1, kProjFlagNone,        36}, // Wolf
            {0x00, 13, 31,  1, kProjFlagNone,        32}, // Mario
            {0x08, 16, 42,  1, kProjFlagNone,        34}, // Luigi
            {0x03, 14, 53,  1, kProjFlagChargeTap,   38}, // Samus
            {0x02, 17, 29, 10, kProjFlagHoldRelease, 38}, // Link
            {0x29, 17, 29, 10, kProjFlagHoldRelease, 38}, // Toon Link
            {0x17, 12, 29,  6, kProjFlagHoldRelease, 38}, // Pit
            {0x07, 18, 33,  1, kProjFlagNone,        32}, // Pikachu
            {0x21, 14, 31,  1, kProjFlagChargeTap,   36}, // Lucario
            {0x26, 15, 36,  1, kProjFlagChargeTap,   36}, // Mewtwo
            {0x23, 24, 40,  1, kProjFlagNone,        38}, // R.O.B.
            {0x0E, 11, 38,  1, kProjFlagChargeTap,   34}, // Sheik
            {0x18, 15, 30,  2, kProjFlagHoldRelease, 36}, // ZSS
            {0x1B, 12, 34,  6, kProjFlagHoldRelease, 34}, // Diddy
            {0x0F, 16, 30,  1, kProjFlagGroundOnly,  30}, // Popo
            {0x12, 17, 31,  1, kProjFlagNone,        30}  // G&W
        };
        for (u32 i = 0; i < sizeof(kTable) / sizeof(kTable[0]); i++)
        {
            if (static_cast<int>(kTable[i].ftKind) == ftKind)
            {
                return &kTable[i];
            }
        }
        return NULL;
    }

    inline bool hasNeutralBProjectile(int ftKind) { return findProjectileProfile(ftKind) != NULL; }

    inline u8 resolvePsAttackMode(u8 requestedMode, int cpuFtKind)
    {
        return (requestedMode == kPsAtkProjectile && findProjectileProfile(cpuFtKind) != NULL)
                   ? static_cast<u8>(kPsAtkProjectile)
                   : static_cast<u8>(kPsAtkPhysical);
    }

    inline bool psMustGroundFire(const ProjectileProfile& prof)
    {
        return ((prof.flags & (kProjFlagGroundOnly | kProjFlagChargeTap)) != 0)
               || (static_cast<int>(prof.shAirFrames) - static_cast<int>(prof.shStartup)
                   - static_cast<int>(prof.holdBFrames) - 2 < 2);
    }

    inline u16 psCadenceDelay(u8 cadence, u32& rng)
    {
        static const u8 kFixed[3] = {65, 38, 16};
        return cadence < 3u ? static_cast<u16>(kFixed[cadence]) : static_cast<u16>(18u + (geomNextRng(rng) % 51u));
    }

    inline u8 psResolveHeight(u8 heightMode, bool groundOnly, u32& rng)
    {
        if (groundOnly)
        {
            return kPsHeightGround;
        }
        return heightMode == kPsHeightRandom ? static_cast<u8>(geomNextRng(rng) % 4u)
                                             : (heightMode < kPsHeightRandom ? heightMode : static_cast<u8>(kPsHeightLow));
    }

    inline u8 psShortHopPressDelay(const ProjectileProfile& prof, u8 resolvedHeight)
    {
        if (resolvedHeight == kPsHeightGround || psMustGroundFire(prof))
        {
            return 0u;
        }
        if (resolvedHeight == kPsHeightHigh)
        {
            return 1u;
        }
        int avail = static_cast<int>(prof.shAirFrames) - static_cast<int>(prof.shStartup)
                    - static_cast<int>(prof.holdBFrames) - 2;
        if (avail < 2)
        {
            avail = 2;
        }
        return static_cast<u8>(resolvedHeight == kPsHeightMid ? (avail / 2 < 2 ? 2 : avail / 2) : avail);
    }

    enum PsCpuPhase {
        kPsCpuIdle = 0,
        kPsCpuJumpSquat = 1,
        kPsCpuAirDelay = 2,
        kPsCpuFireHold = 3,
        kPsCpuChargeWait = 4
    };

    struct PsCpuState {
        u8 phase;
        u8 resolvedHeight;
        u8 airWaitTarget;
        u8 holdRemaining;
        u16 timer;
        u16 cooldown;
        u32 rng;
    };

    struct PsCpuInput {
        int cpuFtKind;
        int cpuStatus;
        bool cpuAir;
        bool cpuCanAct;
        bool cpuOutOfPlay;
        bool hmnOutOfPlay;
        float cpuX;
        float cpuLr;
        float hmnX;
        float floorLeft;
        float floorRight;
    };

    struct PsCpuOutput {
        bool active;
        bool wantIntangible;
        u32 buttons;
        float stickX;
        float stickY;
    };

    inline void psCpuStateReset(PsCpuState& st)
    {
        u32 seed = st.rng != 0 ? st.rng : 0x6A09E667u;
        memset(&st, 0, sizeof(st));
        st.rng = seed;
        st.cooldown = 20u;
    }

    inline void stepPsPracticeCpu(const PsCpuInput& in, const PracticeConfig& cfg, PsCpuState& st, PsCpuOutput& out)
    {
        out.active = false;
        out.wantIntangible = false;
        out.buttons = 0u;
        out.stickX = 0.0f;
        out.stickY = 0.0f;
        if (in.hmnOutOfPlay || in.cpuOutOfPlay)
        {
            st.phase = kPsCpuIdle;
            return;
        }

        const ProjectileProfile* prof = (cfg.psAttackMode == kPsAtkProjectile)
                                            ? findProjectileProfile(in.cpuFtKind)
                                            : NULL;
        out.active = true;
        out.wantIntangible = (prof != NULL);
        if (prof == NULL)
        {
            st.phase = kPsCpuIdle;
        }
        else if (st.phase == kPsCpuJumpSquat)
        {
            if (in.cpuAir)
            {
                st.phase = kPsCpuAirDelay;
                st.timer = 0u;
            }
            else
            {
                if (++st.timer >= 10u)
                {
                    st.phase = kPsCpuIdle;
                    st.cooldown = 12u;
                }
                return;
            }
        }

        u32 btn = 0u;
        float sx = 0.0f;
        bool setCd = false;
        bool fire = false;

        if (st.phase == kPsCpuAirDelay)
        {
            if (!in.cpuAir)
            {
                st.phase = kPsCpuIdle;
                setCd = true;
            }
            else if (++st.timer >= st.airWaitTarget)
            {
                fire = true;
            }
        }
        else if (st.phase == kPsCpuFireHold || st.phase == kPsCpuChargeWait)
        {
            bool done = (--st.timer == 0u);
            if (st.phase == kPsCpuFireHold || done)
            {
                btn = kPrBtnSpecial;
            }
            if (done)
            {
                st.phase = kPsCpuIdle;
                setCd = true;
            }
        }
        else if (st.cooldown > 0u)
        {
            st.cooldown--;
        }
        else if (!in.cpuAir && in.cpuCanAct)
        {
            float dx = in.hmnX - in.cpuX;
            float toward = dx >= 0.0f ? 1.0f : -1.0f;
            float d = (dx < 0.0f ? -dx : dx) - (prof != NULL ? static_cast<float>(prof->spacing) : -5.0f);
            float move = 0.0f;
            if (d > 16.0f)
            {
                move = 0.55f;
            }
            else if (d < -12.0f
                     && ((toward > 0.0f) ? (in.cpuX > in.floorLeft + 10.0f) : (in.cpuX < in.floorRight - 10.0f)))
            {
                move = -0.55f;
            }
            else if (in.cpuLr * toward < 0.0f)
            {
                move = 0.35f;
            }
            if (move != 0.0f)
            {
                sx = toward * move;
            }
            else if (prof == NULL)
            {
                btn = kPrBtnAttack;
                setCd = true;
            }
            else
            {
                int avail = prof->shAirFrames - prof->shStartup - prof->holdBFrames - 2;
                u8 h = (((prof->flags & (kProjFlagGroundOnly | kProjFlagChargeTap)) != 0) || avail < 2)
                           ? static_cast<u8>(kPsHeightGround)
                           : (cfg.psHeight == kPsHeightRandom ? static_cast<u8>(geomNextRng(st.rng) & 3u) : cfg.psHeight);
                st.resolvedHeight = h;
                if (h == kPsHeightGround)
                {
                    fire = true;
                }
                else
                {
                    btn = kPrBtnJump;
                    st.airWaitTarget = static_cast<u8>(
                        h == kPsHeightHigh ? 1 : (h == kPsHeightMid ? (avail / 2 < 2 ? 2 : avail / 2) : avail));
                    st.phase = kPsCpuJumpSquat;
                    st.timer = 0u;
                }
            }
        }

        if (fire)
        {
            btn = kPrBtnSpecial;
            if ((prof->flags & kProjFlagChargeTap) != 0)
            {
                st.phase = kPsCpuChargeWait;
                st.timer = 13u;
            }
            else if (prof->holdBFrames > 1u)
            {
                st.phase = kPsCpuFireHold;
                st.timer = static_cast<u16>(prof->holdBFrames - 1u);
            }
            else
            {
                st.phase = kPsCpuIdle;
                setCd = true;
            }
        }
        if (setCd)
        {
            static const u8 kCad[3] = {65, 38, 16};
            st.cooldown = cfg.psCadence < 3u ? static_cast<u16>(kCad[cfg.psCadence])
                                             : static_cast<u16>(18u + (geomNextRng(st.rng) % 51u));
        }
        out.buttons = btn;
        out.stickX = sx;
    }

    // -------------------------------------------------------------------------
    // 2. Ledgedash Active Ledge-Snap, Cliff-Hang Box Math & Auto-Reset
    // -------------------------------------------------------------------------
    struct CliffHangBox {
        float offsetX;
        float offsetY;
        float width;
        float height;
    };

    inline void computeCliffSnapSpawn(float ledgeX, float ledgeY, float ledgeDir, const CliffHangBox& b,
                                      float& outX, float& outY)
    {
        bool ok = b.width > 1.0f && b.width < 40.0f && b.height > 1.0f && b.height < 40.0f
                  && b.offsetX >= -20.0f && b.offsetX <= 25.0f && b.offsetY >= -15.0f && b.offsetY <= 30.0f;
        float dx = 0.5f * (ok ? b.width : 10.0f);
        dx = dx < 4.5f ? 4.5f : (dx > 12.0f ? 12.0f : dx);
        float dy = (ok ? b.offsetY : 9.0f) + 0.45f * (ok ? b.height : 10.0f);
        dy = dy < 2.0f ? 2.0f : (dy > 24.0f ? 24.0f : dy);
        outX = ledgeX - (ledgeDir >= 0.0f ? dx : -dx);
        outY = ledgeY - dy;
    }

    inline u8 pickNextLedgeSide(u8 currentSide, u8 resetSideMode, u32& rng)
    {
        u8 cur = currentSide & 1u;
        return resetSideMode == kLedgeSideOpposite ? static_cast<u8>(1u - cur)
                                                   : (resetSideMode == kLedgeSideRandom
                                                          ? static_cast<u8>(geomNextRng(rng) & 1u)
                                                          : cur);
    }

    inline void computeLedgeCameraRect(float ledgeX, float ledgeY, float ledgeDir, float& left, float& right,
                                       float& top, float& bottom)
    {
        float cx = ledgeX + (ledgeDir >= 0.0f ? 12.0f : -12.0f);
        left = cx - 34.0f;
        right = cx + 34.0f;
        bottom = ledgeY - 14.0f;
        top = ledgeY + 36.0f;
    }

    struct LedgeDriverState {
        u8 activeSide;
        u8 snapPending;
        u8 snapWatchdog;
        u8 snapRetries;
        u8 waitGrace;
        u8 syncedSeq;
        u32 lastSeenLedgeSeq;
        u32 snapCount;
        u32 rng;
    };

    struct LedgeDriverInput {
        int hmnStatus;
        bool hmnOutOfPlay;
        float hmnY;
        float ledgeY;
        u32 ledgeSeq;
        u8 lastLedgeOutcome;
        bool dpadLeftEdge;
        bool dpadRightEdge;
    };

    struct LedgeDriverAction {
        bool doSnap;
        u8 snapSide;
        bool holdCliffTimer;
        bool clearGrabCounter;
        bool applyCamera;
    };

    inline void ledgeDriverReset(LedgeDriverState& st)
    {
        u32 seed = st.rng != 0 ? st.rng : 0xBB67AE85u;
        memset(&st, 0, sizeof(st));
        st.rng = seed;
        st.snapPending = 1u;
    }

    inline void stepLedgeDriver(const LedgeDriverInput& in, const PracticeConfig& cfg, LedgeDriverState& st,
                                LedgeDriverAction& act)
    {
        const bool unlim = (cfg.ledgeInvulnLimit == kLedgeInvulnUnlimited);
        const bool onLedge = (in.hmnStatus >= 0x73 && in.hmnStatus <= 0x75);
        act.doSnap = false;
        act.holdCliffTimer = (in.hmnStatus == 0x75);
        act.clearGrabCounter = unlim && !act.holdCliffTimer;
        act.applyCamera = true;

        if (!st.syncedSeq)
        {
            st.lastSeenLedgeSeq = in.ledgeSeq;
            st.syncedSeq = 1u;
        }
        if (in.dpadLeftEdge || in.dpadRightEdge)
        {
            st.activeSide = in.dpadLeftEdge ? 0u : 1u;
            st.snapPending = 1u;
            st.waitGrace = st.snapWatchdog = st.snapRetries = 0u;
            st.lastSeenLedgeSeq = in.ledgeSeq;
        }
        if (onLedge)
        {
            st.snapWatchdog = st.snapRetries = 0u;
        }

        if (in.ledgeSeq != st.lastSeenLedgeSeq)
        {
            st.lastSeenLedgeSeq = in.ledgeSeq;
            st.snapWatchdog = 0u;
            if (cfg.ledgeAutoReset != 0 && !st.snapPending)
            {
                st.waitGrace = (in.lastLedgeOutcome == 5u) ? 8u : 28u;
            }
        }
        else if (cfg.ledgeAutoReset != 0 && !st.snapPending && st.waitGrace == 0u && !onLedge
                 && (in.hmnOutOfPlay || in.hmnY < in.ledgeY - 48.0f))
        {
            st.waitGrace = 8u;
        }

        if (st.waitGrace > 0u && --st.waitGrace == 0u)
        {
            u8 cur = st.activeSide & 1u;
            st.activeSide = cfg.ledgeResetSide == kLedgeSideOpposite
                                ? static_cast<u8>(1u - cur)
                                : (cfg.ledgeResetSide == kLedgeSideRandom ? static_cast<u8>(geomNextRng(st.rng) & 1u)
                                                                          : cur);
            st.snapPending = 1u;
            st.snapRetries = 0u;
        }
        if (st.snapWatchdog > 0u && --st.snapWatchdog == 0u && !onLedge && !in.hmnOutOfPlay && st.snapRetries == 0u)
        {
            st.snapRetries = 1u;
            st.snapPending = 1u;
        }
        if (st.snapPending && in.hmnStatus != 0xBD && in.hmnStatus != 0x10B)
        {
            st.snapPending = 0u;
            st.snapWatchdog = 6u;
            st.snapCount++;
            act.doSnap = true;
            act.clearGrabCounter |= unlim;
        }
        act.snapSide = st.activeSide;
    }

    // -------------------------------------------------------------------------
    // 3. L-Cancel & Float/Hover/DJC Target Driver
    // -------------------------------------------------------------------------
    inline u8 activeTargetMode(u8 activeDrill, const PracticeConfig& cfg)
    {
        if (activeDrill == kPracticeDrillLCancel)
        {
            return cfg.lcancelTarget < kTargetModeCount ? cfg.lcancelTarget : static_cast<u8>(kTargetStationary);
        }
        if (activeDrill == kPracticeDrillHoverFloat)
        {
            return cfg.floatDjcTarget < kTargetModeCount ? cfg.floatDjcTarget : static_cast<u8>(kTargetStationary);
        }
        return kTargetOff;
    }

    inline bool targetWantsNoReaction(u8 targetMode) { return targetMode == kTargetStationary; }

    inline void computeTargetAnchor(float hmnX, float hmnLr, float dist, const StageGeometry& geom, float& outX,
                                    float& outY, float& outLr)
    {
        float minX = geom.floor.valid ? (geom.floor.leftX + 10.0f) : -55.0f;
        float maxX = geom.floor.valid ? (geom.floor.rightX - 10.0f) : 55.0f;
        float d = hmnLr >= 0.0f ? dist : -dist;
        float x = hmnX + d;
        if (x < minX || x > maxX)
        {
            x = hmnX - d;
            if (x < minX) x = minX;
            if (x > maxX) x = maxX;
        }
        outX = x;
        outY = geom.floor.valid ? geom.floor.centerY : 0.0f;
        outLr = (hmnX >= x) ? 1.0f : -1.0f;
    }

    struct TargetCpuState {
        float anchorX;
        float anchorY;
        float anchorLr;
        bool anchorValid;
        float patrolDir;
    };

    struct TargetCpuInput {
        u8 targetMode;
        bool cpuAir;
        bool cpuCanAct;
        bool cpuOutOfPlay;
        float cpuX;
        float cpuLr;
        float hmnX;
        float floorLeft;
        float floorRight;
    };

    struct TargetCpuOutput {
        bool active;
        u32 buttons;
        float stickX;
        float stickY;
    };

    inline void targetCpuReset(TargetCpuState& st)
    {
        memset(&st, 0, sizeof(st));
        st.patrolDir = 1.0f;
    }

    inline void stepTargetCpu(const TargetCpuInput& in, TargetCpuState& st, TargetCpuOutput& out)
    {
        out.active = false;
        out.buttons = 0u;
        out.stickX = 0.0f;
        out.stickY = 0.0f;
        if (in.targetMode == kTargetOff || in.cpuOutOfPlay)
        {
            return;
        }
        out.active = true;
        if (in.cpuAir)
        {
            return;
        }
        if (in.targetMode == kTargetShielding)
        {
            out.buttons = kPrBtnGuard;
            return;
        }
        if (!in.cpuCanAct)
        {
            return;
        }
        if (in.targetMode == kTargetStationary)
        {
            float toward = (in.hmnX >= in.cpuX) ? 1.0f : -1.0f;
            if (in.cpuLr * toward < 0.0f)
            {
                out.stickX = toward * 0.30f;
            }
            return;
        }
        float lo = in.floorLeft + 16.0f;
        float hi = in.floorRight - 16.0f;
        if (hi <= lo + 4.0f)
        {
            lo = 0.5f * (in.floorLeft + in.floorRight) - 6.0f;
            hi = lo + 12.0f;
        }
        if (st.patrolDir == 0.0f || in.cpuX <= lo)
        {
            st.patrolDir = 1.0f;
        }
        else if (in.cpuX >= hi)
        {
            st.patrolDir = -1.0f;
        }
        out.stickX = st.patrolDir * 0.55f;
    }

    // -------------------------------------------------------------------------
    // 4. Wavedash / Waveland GX Target Zone (55% - 80% of P+ Wavedash Distance)
    // -------------------------------------------------------------------------
    inline float computeWavedashMaxDist(float groundBrake, float walkSpeedMax = 1.5f)
    {
        float brake = (groundBrake >= 0.015f && groundBrake <= 0.20f) ? groundBrake : 0.08f;
        float walkMax = (walkSpeedMax >= 0.5f && walkSpeedMax <= 3.0f) ? walkSpeedMax : 1.5f;
        float v = 2.976f * 0.9f;
        float dist = 0.0f;
        for (int f = 0; f < 10; f++)
        {
            dist += v;
            v -= (v > walkMax) ? (brake * 2.0f) : brake;
            if (v < 0.0f) v = 0.0f;
        }
        return dist;
    }

    struct WdZoneState {
        u8 active;
        u8 lastOutcome;
        u8 flashFrames;
        u8 respawnPending;
        u8 syncedSeq;
        float originX;
        float minX;
        float maxX;
        float y;
        float dir;
        float maxWdDist;
        u32 lastSeenWdSeq;
        u32 attempts;
        u32 hits;
        u32 streak;
        u32 bestStreak;
    };

    inline void wdZoneReset(WdZoneState& z)
    {
        memset(&z, 0, sizeof(z));
        z.dir = 1.0f;
    }

    inline bool spawnWavedashZone(WdZoneState& z, float p1X, float p1Y, float p1Lr, float groundBrake,
                                  const StageGeometry& geom, float walkSpeedMax = 1.5f)
    {
        (void)p1Y;
        const float minF = geom.floor.leftX + 4.0f;
        const float maxF = geom.floor.rightX - 4.0f;
        if (!geom.floor.valid || maxF - minF < 8.0f)
        {
            z.active = 0u;
            return false;
        }
        float maxDist = computeWavedashMaxDist(groundBrake, walkSpeedMax);

        float dir = p1Lr >= 0.0f ? 1.0f : -1.0f;
        float farOff = maxDist * 0.80f;
        float farX = p1X + dir * farOff;
        if (farX < minF || farX > maxF)
        {
            dir = -dir;
            farX = p1X + dir * farOff;
        }
        float nearX = p1X + dir * (maxDist * 0.55f);
        float lo = nearX < farX ? nearX : farX;
        float hi = nearX < farX ? farX : nearX;
        if (lo < minF) lo = minF;
        if (lo > maxF - 2.5f) lo = maxF - 2.5f;
        if (hi < lo + 2.5f) hi = lo + 2.5f;
        if (hi > maxF) hi = maxF;

        z.active = 1u;
        z.respawnPending = 0u;
        z.originX = p1X;
        z.minX = lo;
        z.maxX = hi;
        z.y = geom.floor.centerY;
        z.dir = dir;
        z.maxWdDist = maxDist;
        return true;
    }

    inline u8 scoreWavedashZone(WdZoneState& z, float endX, bool wdSuccess)
    {
        if (!z.active)
        {
            return kWdZoneNone;
        }
        z.attempts++;
        u8 out = kWdZoneShort;
        if (wdSuccess && endX >= z.minX && endX <= z.maxX)
        {
            out = kWdZoneHit;
            z.hits++;
            if (++z.streak > z.bestStreak)
            {
                z.bestStreak = z.streak;
            }
        }
        else
        {
            if (wdSuccess && ((z.dir > 0.0f) ? (endX > z.maxX) : (endX < z.minX)))
            {
                out = kWdZoneOvershoot;
            }
            z.streak = 0u;
        }
        z.lastOutcome = out;
        z.flashFrames = 18u;
        z.respawnPending = 1u;
        return out;
    }

    inline bool wdZoneTickFlash(WdZoneState& z)
    {
        if (z.flashFrames > 0u)
        {
            z.flashFrames--;
            return false;
        }
        if (z.respawnPending != 0u)
        {
            z.respawnPending = 0u;
            return true;
        }
        return false;
    }

    inline void computeWavedashCameraRect(float p1X, float p1Y, const WdZoneState& z, float& left, float& right,
                                          float& top, float& bottom)
    {
        left = (p1X < z.minX ? p1X : z.minX) - 18.0f;
        right = (p1X > z.maxX ? p1X : z.maxX) + 18.0f;
        float baseY = z.y < p1Y ? z.y : p1Y;
        bottom = baseY - 12.0f;
        top = baseY + 36.0f;
    }

    // -------------------------------------------------------------------------
    // 5. Solo Tech Practice CPU Standby State Machine
    // -------------------------------------------------------------------------
    inline bool shouldCpuStandby(u8 activeDrill, const PracticeConfig& cfg)
    {
        if (activeDrill == kPracticeDrillLCancel)
        {
            return cfg.lcancelTarget == kTargetOff;
        }
        if (activeDrill == kPracticeDrillHoverFloat)
        {
            return cfg.floatDjcTarget == kTargetOff;
        }
        return activeDrill == kPracticeDrillLedgedash || activeDrill == kPracticeDrillWavedash
               || activeDrill == kPracticeDrillRar || activeDrill == kPracticeDrillDacus
               || activeDrill == kPracticeDrillBReverse;
    }

    inline u8 stepStandbyState(bool wantStandby, bool& isStandby)
    {
        if (wantStandby)
        {
            u8 tr = isStandby ? static_cast<u8>(kStandbyHold) : static_cast<u8>(kStandbyEnter);
            isStandby = true;
            return tr;
        }
        if (isStandby)
        {
            isStandby = false;
            return kStandbyExit;
        }
        return kStandbyNoop;
    }
}
}
