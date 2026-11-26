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
//  - Cliff-hang box geometry: soGroundModuleImpl::selectCliffHangData
//    (0x80731C5C) -> grCollStatus (+0x3C offsetX, +0x40 offsetY, +0x44 width,
//    +0x48 height), box [posX + lr*offsetX, posX + lr*(offsetX + width)] x
//    [posY + offsetY, posY + offsetY + height] (0x801357F0, 0x80135950-A58).
//  - Cliff_Wait auto-fall timer: RA-Basic[0] (0x20000000) decrements to 0 and
//    sets RA-Bit[18] (0x22000012) (0x8087ABC4-AC48).
//  - 5-ledge-grab invuln counter: LA-Basic[79] (0x1000004F, Ledge.asm:495-513).
//  - Wavedash initial speed & landing lag: v0 = 3.1 * cos(16.26 deg) = 2.976,
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
        kPsAtkProjectile = 0, // Default when CPU has Neutral B projectile
        kPsAtkPhysical = 1,   // Jab approach
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
        kProjFlagHoldRelease = 1 << 0, // Bow / Grenade / Popgun: hold B briefly then release
        kProjFlagChargeTap = 1 << 1,   // Samus / Lucario / Mewtwo / Sheik: tap B to charge, tap again to fire
        kProjFlagGroundOnly = 1 << 2   // Ice Shot (Popo): grounded Neutral B only
    };

    struct PracticeConfig {
        u8 psAttackMode;     // PsAttackMode (default kPsAtkProjectile)
        u8 psHeight;         // PsHeight (default kPsHeightLow)
        u8 psCadence;        // PsCadence (default kPsCadenceMedium)
        u8 ledgeResetSide;   // LedgeResetSide (default kLedgeSideSame)
        u8 ledgeAutoReset;   // 0 Off, 1 On (default 1)
        u8 ledgeInvulnLimit; // LedgeInvulnLimit (default kLedgeInvulnPPlus5)
        u8 lcancelTarget;    // PracticeTargetMode (default kTargetStationary)
        u8 floatDjcTarget;   // PracticeTargetMode (default kTargetStationary)
        u8 wdTargetZone;     // 0 Off (default), 1 On
        u8 reserved[3];
    };

    inline void practiceConfigDefaults(PracticeConfig& cfg)
    {
        memset(&cfg, 0, sizeof(cfg));
        cfg.psAttackMode = kPsAtkProjectile;
        cfg.psHeight = kPsHeightLow;
        cfg.psCadence = kPsCadenceMedium;
        cfg.ledgeResetSide = kLedgeSideSame;
        cfg.ledgeAutoReset = 1;
        cfg.ledgeInvulnLimit = kLedgeInvulnPPlus5;
        cfg.lcancelTarget = kTargetStationary;
        cfg.floatDjcTarget = kTargetStationary;
        cfg.wdTargetZone = 0;
    }

    inline void clampOpt(u8& v, u8 count, u8 def)
    {
        if (v >= count)
        {
            v = def;
        }
    }

    inline void sanitizePracticeConfig(PracticeConfig& cfg)
    {
        clampOpt(cfg.psAttackMode, kPsAtkModeCount, kPsAtkProjectile);
        clampOpt(cfg.psHeight, kPsHeightCount, kPsHeightLow);
        clampOpt(cfg.psCadence, kPsCadenceCount, kPsCadenceMedium);
        clampOpt(cfg.ledgeResetSide, kLedgeSideCount, kLedgeSideSame);
        cfg.ledgeAutoReset = cfg.ledgeAutoReset != 0 ? 1 : 0;
        clampOpt(cfg.ledgeInvulnLimit, kLedgeInvulnCount, kLedgeInvulnPPlus5);
        clampOpt(cfg.lcancelTarget, kTargetModeCount, kTargetStationary);
        clampOpt(cfg.floatDjcTarget, kTargetModeCount, kTargetStationary);
        cfg.wdTargetZone = cfg.wdTargetZone != 0 ? 1 : 0;
    }

    inline float prAbsf(float v)
    {
        return v < 0.0f ? -v : v;
    }

    // -------------------------------------------------------------------------
    // 1. Powershield Projectile Table & CPU Driver (verified P+ 3.2.1 Fit*.pac)
    // -------------------------------------------------------------------------
    struct ProjectileProfile {
        u8 ftKind;      // ftKind from ft_entry.h
        u8 shStartup;   // frames from SpecialAirN entry to projectile release
        u8 shAirFrames; // total short-hop airtime ~ round(2 * miniJumpY / grav)
        u8 holdBFrames; // frames to hold B before release (1 for instant tap)
        u8 flags;       // ProjectileFlags
        u8 spacing;     // ideal horizontal spacing from P1 (stage units)
    };

    // Verified directly from P+ 3.2.1 Fit*.pac attributes (+0x44 MiniJumpY,
    // +0x64 Gravity) and SpecialN / SpecialAirN PSA scripts:
    //   Fox   (0x06): miniY=2.33, grav=0.23 (~20f SH), SpecialAirNStart(2)+Loop(4) -> +8
    //   Falco (0x13): miniY=2.07, grav=0.17 (~24f SH), SpecialAirNStart(3)+Loop(8) -> +12
    //   Wolf  (0x2C): miniY=2.00, grav=0.16 (~25f SH), SpecialN ART_00[1] @ f15    -> +14
    inline const ProjectileProfile* findProjectileProfile(int ftKind)
    {
        static const ProjectileProfile kTable[] = {
            // ftKind, shStartup, shAirFrames, holdBFrames, flags,                spacing
            {0x06,  8, 20,  1, kProjFlagNone,        36}, // Fox (Blaster)
            {0x13, 12, 24,  1, kProjFlagNone,        36}, // Falco (Blaster)
            {0x2C, 14, 25,  1, kProjFlagNone,        36}, // Wolf (Blaster)
            {0x00, 13, 31,  1, kProjFlagNone,        32}, // Mario (Fireball)
            {0x08, 16, 42,  1, kProjFlagNone,        34}, // Luigi (Fireball)
            {0x03, 14, 53,  1, kProjFlagChargeTap,   38}, // Samus (Charge Shot)
            {0x02, 17, 29, 10, kProjFlagHoldRelease, 38}, // Link (Hero's Bow)
            {0x29, 17, 29, 10, kProjFlagHoldRelease, 38}, // Toon Link (Hero's Bow)
            {0x17, 12, 29,  6, kProjFlagHoldRelease, 38}, // Pit (Palutena's Arrow)
            {0x07, 18, 33,  1, kProjFlagNone,        32}, // Pikachu (Thunder Jolt)
            {0x21, 14, 31,  1, kProjFlagChargeTap,   36}, // Lucario (Aura Sphere)
            {0x26, 15, 36,  1, kProjFlagChargeTap,   36}, // Mewtwo (Shadow Ball)
            {0x23, 24, 40,  1, kProjFlagNone,        38}, // R.O.B. (Robo Beam)
            {0x0E, 11, 38,  1, kProjFlagChargeTap,   34}, // Sheik (Needle Storm)
            {0x18, 15, 30,  2, kProjFlagHoldRelease, 36}, // Zero Suit Samus (Paralyzer)
            {0x1B, 12, 34,  6, kProjFlagHoldRelease, 34}, // Diddy Kong (Peanut Popgun)
            {0x0F, 16, 30,  1, kProjFlagGroundOnly,  30}, // Ice Climbers Popo (Ice Shot)
            {0x12, 17, 31,  1, kProjFlagNone,        30}, // Mr. Game & Watch (Chef)
            {0x1F,  9, 40,  1, kProjFlagNone,        30}, // Ivysaur (Bullet Seed)
            {0x2E, 14, 32,  4, kProjFlagHoldRelease, 32}  // Snake (Hand Grenade)
        };
        const u32 count = static_cast<u32>(sizeof(kTable) / sizeof(kTable[0]));
        for (u32 i = 0; i < count; i++)
        {
            if (static_cast<int>(kTable[i].ftKind) == ftKind)
            {
                return &kTable[i];
            }
        }
        return NULL;
    }

    inline bool hasNeutralBProjectile(int ftKind)
    {
        return findProjectileProfile(ftKind) != NULL;
    }

    inline u8 resolvePsAttackMode(u8 requestedMode, int cpuFtKind)
    {
        if (requestedMode == kPsAtkProjectile && hasNeutralBProjectile(cpuFtKind))
        {
            return kPsAtkProjectile;
        }
        return kPsAtkPhysical;
    }

    inline u16 psCadenceDelay(u8 cadence, u32& rng)
    {
        switch (cadence)
        {
        case kPsCadenceSlow:
            return 65u;
        case kPsCadenceFast:
            return 16u;
        case kPsCadenceRandom:
            return static_cast<u16>(18u + (geomNextRng(rng) % 51u)); // 18..68f
        case kPsCadenceMedium:
        default:
            return 38u;
        }
    }

    inline u8 psResolveHeight(u8 heightMode, bool groundOnly, u32& rng)
    {
        if (groundOnly)
        {
            return kPsHeightGround;
        }
        if (heightMode == kPsHeightRandom)
        {
            return static_cast<u8>(geomNextRng(rng) % 4u); // Low / Mid / High / Ground
        }
        return heightMode < kPsHeightRandom ? heightMode : static_cast<u8>(kPsHeightLow);
    }

    // Frames after leaving Jump_Squat to press Neutral B for Low / Mid / High.
    //  - High: press immediately (frame 1 airborne) so projectile fires near apex.
    //  - Mid: press at ~35% of remaining airtime.
    //  - Low: press near (shAirFrames - shStartup - 2) so projectile fires close
    //    to the floor just before landing (classic SHL / low laser timing).
    inline u8 psShortHopPressDelay(const ProjectileProfile& prof, u8 resolvedHeight)
    {
        if (resolvedHeight == kPsHeightGround)
        {
            return 0u;
        }
        if (resolvedHeight == kPsHeightHigh)
        {
            return 1u;
        }
        int avail = static_cast<int>(prof.shAirFrames) - static_cast<int>(prof.shStartup) - 2;
        if (avail < 2)
        {
            avail = 2;
        }
        if (resolvedHeight == kPsHeightMid)
        {
            int mid = avail / 2;
            return static_cast<u8>(mid < 2 ? 2 : mid);
        }
        // kPsHeightLow
        return static_cast<u8>(avail);
    }

    enum PsCpuPhase {
        kPsCpuIdle = 0,
        kPsCpuJumpSquat = 1,
        kPsCpuAirDelay = 2,
        kPsCpuFireHold = 3,
        kPsCpuChargeWait = 4,
        kPsCpuChargeRelease = 5
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

    inline void psStartFire(const ProjectileProfile& prof, const PracticeConfig& cfg, PsCpuState& st, PsCpuOutput& out)
    {
        out.buttons = kPrBtnSpecial;
        if ((prof.flags & kProjFlagChargeTap) != 0)
        {
            st.phase = kPsCpuChargeWait;
            st.timer = 0u;
        }
        else if (prof.holdBFrames > 1u)
        {
            st.phase = kPsCpuFireHold;
            st.holdRemaining = static_cast<u8>(prof.holdBFrames - 1u);
        }
        else
        {
            st.phase = kPsCpuIdle;
            st.cooldown = psCadenceDelay(cfg.psCadence, st.rng);
        }
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

        const u8 mode = resolvePsAttackMode(cfg.psAttackMode, in.cpuFtKind);
        const float dx = in.hmnX - in.cpuX;
        const float toward = dx >= 0.0f ? 1.0f : -1.0f;
        const float absDx = prAbsf(dx);

        if (mode == kPsAtkPhysical)
        {
            out.active = true;
            if (st.cooldown > 0u)
            {
                st.cooldown--;
                return;
            }
            if (in.cpuAir || !in.cpuCanAct)
            {
                return;
            }
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
            out.buttons = kPrBtnAttack;
            st.cooldown = psCadenceDelay(cfg.psCadence, st.rng);
            return;
        }

        // Projectile (Neutral B) mode.
        const ProjectileProfile* prof = findProjectileProfile(in.cpuFtKind);
        if (prof == NULL)
        {
            return;
        }
        out.active = true;
        out.wantIntangible = true;

        if (st.phase == kPsCpuJumpSquat)
        {
            st.timer++;
            if (in.cpuAir)
            {
                st.phase = kPsCpuAirDelay;
                st.timer = 0u;
            }
            else if (st.timer >= 10u)
            {
                st.phase = kPsCpuIdle;
                st.cooldown = 12u;
                return;
            }
            else
            {
                return;
            }
        }

        if (st.phase == kPsCpuAirDelay)
        {
            if (!in.cpuAir)
            {
                st.phase = kPsCpuIdle;
                st.cooldown = psCadenceDelay(cfg.psCadence, st.rng);
                return;
            }
            st.timer++;
            if (st.timer >= st.airWaitTarget)
            {
                psStartFire(*prof, cfg, st, out);
            }
            return;
        }

        if (st.phase == kPsCpuFireHold)
        {
            if (st.holdRemaining > 0u)
            {
                out.buttons = kPrBtnSpecial;
                st.holdRemaining--;
                return;
            }
            st.phase = kPsCpuIdle;
            st.cooldown = psCadenceDelay(cfg.psCadence, st.rng);
            return;
        }

        if (st.phase == kPsCpuChargeWait)
        {
            st.timer++;
            if (st.timer >= 12u)
            {
                st.phase = kPsCpuChargeRelease;
                st.timer = 0u;
            }
            return;
        }

        if (st.phase == kPsCpuChargeRelease)
        {
            out.buttons = kPrBtnSpecial;
            st.phase = kPsCpuIdle;
            st.cooldown = psCadenceDelay(cfg.psCadence, st.rng);
            return;
        }

        if (st.cooldown > 0u)
        {
            st.cooldown--;
            return;
        }
        if (in.cpuAir || !in.cpuCanAct)
        {
            return;
        }

        const float ideal = static_cast<float>(prof->spacing);
        if (absDx < ideal - 12.0f)
        {
            float awayDir = -toward;
            if ((awayDir < 0.0f) ? (in.cpuX > in.floorLeft + 10.0f) : (in.cpuX < in.floorRight - 10.0f))
            {
                out.stickX = awayDir * 0.55f;
                return;
            }
        }
        else if (absDx > ideal + 16.0f)
        {
            out.stickX = toward * 0.55f;
            return;
        }

        if (in.cpuLr * toward < 0.0f)
        {
            out.stickX = toward * 0.35f;
            return;
        }

        st.resolvedHeight = psResolveHeight(cfg.psHeight, (prof->flags & kProjFlagGroundOnly) != 0, st.rng);
        if (st.resolvedHeight == kPsHeightGround)
        {
            psStartFire(*prof, cfg, st, out);
        }
        else
        {
            out.buttons = kPrBtnJump;
            st.airWaitTarget = psShortHopPressDelay(*prof, st.resolvedHeight);
            st.phase = kPsCpuJumpSquat;
            st.timer = 0u;
        }
    }

    // -------------------------------------------------------------------------
    // 2. Ledgedash Active Ledge-Snap, Cliff-Hang Box Math & Auto-Reset
    // -------------------------------------------------------------------------
    struct CliffHangBox {
        float offsetX; // grCollStatus + 0x3C (scaled by posture scale)
        float offsetY; // grCollStatus + 0x40
        float width;   // grCollStatus + 0x44 (forward X extent)
        float height;  // grCollStatus + 0x48 (vertical extent)
    };

    inline CliffHangBox defaultCliffHangBox()
    {
        CliffHangBox b;
        b.offsetX = 0.0f;
        b.offsetY = 9.0f;
        b.width = 10.0f;
        b.height = 10.0f;
        return b;
    }

    // Places a fighter facing `ledgeDir` (+1 right for left ledge, -1 left for
    // right ledge) so the ledge point (ledgeX, ledgeY) sits directly inside the
    // fighter's own scaled cliff-hang box [posX + lr*offsetX, posX + lr*(offsetX + width)]
    // x [posY + offsetY, posY + offsetY + height], while keeping the fighter's
    // ECB strictly outside the stage lip by >= 2.0 units so fast-fallers like
    // Wolf snap cleanly on Battlefield, FD, and Smashville without SDing.
    inline void computeCliffSnapSpawn(float ledgeX, float ledgeY, float ledgeDir, const CliffHangBox& rawBox,
                                      float& outX, float& outY)
    {
        CliffHangBox b = rawBox;
        if (!(b.width > 1.0f && b.width < 40.0f) || !(b.height > 1.0f && b.height < 40.0f)
            || !(b.offsetX >= -20.0f && b.offsetX <= 25.0f) || !(b.offsetY >= -15.0f && b.offsetY <= 30.0f))
        {
            b = defaultCliffHangBox();
        }
        float dir = ledgeDir >= 0.0f ? 1.0f : -1.0f;
        float dx = b.offsetX + 0.5f * b.width;
        if (dx < 2.2f)
        {
            dx = 2.2f;
        }
        else if (dx > 16.0f)
        {
            dx = 16.0f;
        }
        // Place ledgeY at 45% up from bottom of cliff-hang box so 1 frame of
        // downward fall sweeps straight through the vertical sweetspot.
        float dy = b.offsetY + 0.45f * b.height;
        if (dy < 2.0f)
        {
            dy = 2.0f;
        }
        else if (dy > 24.0f)
        {
            dy = 24.0f;
        }
        outX = ledgeX - dir * dx;
        outY = ledgeY - dy;
    }

    inline u8 pickNextLedgeSide(u8 currentSide, u8 resetSideMode, u32& rng)
    {
        u8 cur = currentSide & 1u;
        if (resetSideMode == kLedgeSideOpposite)
        {
            return static_cast<u8>(1u - cur);
        }
        if (resetSideMode == kLedgeSideRandom)
        {
            return static_cast<u8>(geomNextRng(rng) & 1u);
        }
        return cur;
    }

    inline void computeLedgeCameraRect(float ledgeX, float ledgeY, float ledgeDir, float& left, float& right,
                                       float& top, float& bottom)
    {
        float dir = ledgeDir >= 0.0f ? 1.0f : -1.0f;
        float cx = ledgeX + dir * 12.0f;
        float cy = ledgeY + 8.0f;
        left = cx - 34.0f;
        right = cx + 34.0f;
        bottom = cy - 22.0f;
        top = cy + 28.0f;
    }

    struct LedgeDriverState {
        u8 activeSide;         // 0 = Left ledge, 1 = Right ledge
        u8 snapPending;        // 1 = snap P1 to activeSide next tick
        u8 snapWatchdog;       // frames remaining to verify cliff catch took hold
        u8 snapRetries;        // retry cap (at most 1 retry per snap request)
        u8 waitGrace;          // countdown frames after rep completion before auto-reset
        u32 lastSeenLedgeSeq;  // synced to LedgeCoreState::seq
        u8 syncedSeq;
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
        st.activeSide = 0u;
        st.snapPending = 1u;
    }

    inline void stepLedgeDriver(const LedgeDriverInput& in, const PracticeConfig& cfg, LedgeDriverState& st,
                                LedgeDriverAction& act)
    {
        memset(&act, 0, sizeof(act));
        act.applyCamera = true;
        act.holdCliffTimer = (in.hmnStatus == 0x75); // Cliff_Wait
        act.clearGrabCounter = (cfg.ledgeInvulnLimit == kLedgeInvulnUnlimited) && (in.hmnStatus != 0x75);

        if (!st.syncedSeq)
        {
            st.lastSeenLedgeSeq = in.ledgeSeq;
            st.syncedSeq = 1u;
        }

        // Instant D-Pad Left / Right ledge switch.
        if (in.dpadLeftEdge || in.dpadRightEdge)
        {
            st.activeSide = in.dpadLeftEdge ? 0u : 1u;
            st.snapPending = 1u;
            st.waitGrace = 0u;
            st.snapWatchdog = 0u;
            st.snapRetries = 0u;
            st.lastSeenLedgeSeq = in.ledgeSeq;
        }

        const bool onLedge = (in.hmnStatus == 0x73 || in.hmnStatus == 0x74 || in.hmnStatus == 0x75);
        if (onLedge)
        {
            st.snapWatchdog = 0u;
            st.snapRetries = 0u;
        }

        // Detect rep completion or offstage SD when Auto-Reset is enabled.
        if (in.ledgeSeq != st.lastSeenLedgeSeq)
        {
            st.lastSeenLedgeSeq = in.ledgeSeq;
            st.snapWatchdog = 0u;
            if (cfg.ledgeAutoReset != 0 && !st.snapPending)
            {
                bool wasSd = (in.lastLedgeOutcome == 5u); // kLedgeOutcomeSd
                st.waitGrace = wasSd ? 8u : 28u;
            }
        }
        else if (cfg.ledgeAutoReset != 0 && !st.snapPending && st.waitGrace == 0u && !onLedge)
        {
            if (in.hmnOutOfPlay || in.hmnY < in.ledgeY - 48.0f)
            {
                st.waitGrace = 8u;
            }
        }

        if (st.waitGrace > 0u)
        {
            st.waitGrace--;
            if (st.waitGrace == 0u)
            {
                st.activeSide = pickNextLedgeSide(st.activeSide, cfg.ledgeResetSide, st.rng);
                st.snapPending = 1u;
                st.snapRetries = 0u;
            }
        }

        // Watchdog retries at most once if fighter somehow didn't catch after snap.
        if (st.snapWatchdog > 0u)
        {
            st.snapWatchdog--;
            if (st.snapWatchdog == 0u && !onLedge && !in.hmnOutOfPlay && st.snapRetries < 1u)
            {
                st.snapRetries++;
                st.snapPending = 1u;
            }
        }

        // Defer actual warp while Dead (0xBD) or Standby (0x10B) so Brawl's KO
        // transition completes cleanly into Rebirth (0xBE).
        const bool isDeadOrStandby = (in.hmnStatus == 0xBD || in.hmnStatus == 0x10B);
        if (st.snapPending && !isDeadOrStandby)
        {
            st.snapPending = 0u;
            st.snapWatchdog = 6u;
            st.snapCount++;
            act.doSnap = true;
            act.snapSide = st.activeSide;
            if (cfg.ledgeInvulnLimit == kLedgeInvulnUnlimited)
            {
                act.clearGrabCounter = true;
            }
        }
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

    inline void computeTargetAnchor(float hmnX, float hmnLr, float dist, const StageGeometry& geom, float& outX,
                                    float& outY, float& outLr)
    {
        float dir = hmnLr >= 0.0f ? 1.0f : -1.0f;
        float minX = geom.floor.valid ? (geom.floor.leftX + 10.0f) : -55.0f;
        float maxX = geom.floor.valid ? (geom.floor.rightX - 10.0f) : 55.0f;
        float y = geom.floor.valid ? geom.floor.centerY : 0.0f;
        float x = hmnX + dir * dist;
        if (x < minX || x > maxX)
        {
            dir = -dir;
            x = geomClamp(hmnX + dir * dist, minX, maxX);
        }
        outX = x;
        outY = y;
        outLr = (hmnX >= x) ? 1.0f : -1.0f;
    }

    struct TargetCpuState {
        float anchorX;
        float anchorY;
        float anchorLr;
        bool anchorValid;
        float patrolDir;
        u16 knockbackResetTimer;
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

        float toward = (in.hmnX >= in.cpuX) ? 1.0f : -1.0f;
        if (in.targetMode == kTargetShielding)
        {
            if (!in.cpuAir)
            {
                out.buttons = kPrBtnGuard;
            }
            return;
        }

        if (in.targetMode == kTargetStationary)
        {
            // Stay quietly anchored facing P1 (zero attack inputs).
            if (!in.cpuAir && in.cpuCanAct && in.cpuLr * toward < 0.0f)
            {
                out.stickX = toward * 0.30f;
            }
            return;
        }

        // kTargetMoving: smooth horizontal patrol along walkable stage floor.
        if (!in.cpuAir && in.cpuCanAct)
        {
            float leftBound = in.floorLeft + 16.0f;
            float rightBound = in.floorRight - 16.0f;
            if (rightBound <= leftBound + 4.0f)
            {
                float mid = 0.5f * (in.floorLeft + in.floorRight);
                leftBound = mid - 6.0f;
                rightBound = mid + 6.0f;
            }
            if (st.patrolDir == 0.0f)
            {
                st.patrolDir = 1.0f;
            }
            if (in.cpuX <= leftBound)
            {
                st.patrolDir = 1.0f;
            }
            else if (in.cpuX >= rightBound)
            {
                st.patrolDir = -1.0f;
            }
            out.stickX = st.patrolDir * 0.55f;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Wavedash / Waveland GX Target Zone (55% - 80% of P+ Wavedash Distance)
    // -------------------------------------------------------------------------
    // Computes a fighter's real P+ 3.2.1 max wavedash slide distance from
    // Customize_Param_Float_Ground_Brake (ParamFloat 3004) over 10 frames of
    // Landing_Fall_Special (0x19) starting from shallowest airdodge horizontal
    // speed v0 = 3.1 * cos(16.2602 deg) = 2.976.
    inline float computeWavedashMaxDist(float groundBrake)
    {
        float brake = groundBrake;
        if (!(brake >= 0.015f && brake <= 0.20f))
        {
            brake = 0.08f; // Fox/Falco/Falcon baseline
        }
        float v = 2.976f; // 3.1 * cos(16.2602 deg)
        float dist = 0.0f;
        for (int f = 0; f < 10; f++)
        {
            dist += v;
            // Brawl/P+ high-speed ground slide brakes at 2x traction above
            // walk speed threshold (~1.5), and 1x traction below.
            float decel = (v > 1.5f) ? (brake * 2.0f) : brake;
            v -= decel;
            if (v < 0.0f)
            {
                v = 0.0f;
            }
        }
        return dist;
    }

    struct WdZoneState {
        u8 active;
        u8 lastOutcome; // WdZoneOutcome
        u8 flashFrames;
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
                                  const StageGeometry& geom)
    {
        if (!geom.floor.valid)
        {
            z.active = 0u;
            return false;
        }
        const float maxDist = computeWavedashMaxDist(groundBrake);
        const float nearOff = maxDist * 0.55f;
        const float farOff = maxDist * 0.80f;
        const float minFloor = geom.floor.leftX + 4.0f;
        const float maxFloor = geom.floor.rightX - 4.0f;
        if (maxFloor - minFloor < 8.0f)
        {
            z.active = 0u;
            return false;
        }

        float dir = p1Lr >= 0.0f ? 1.0f : -1.0f;
        float testFar = p1X + dir * farOff;
        if (testFar < minFloor || testFar > maxFloor)
        {
            dir = -dir;
        }

        float a = geomClamp(p1X + dir * nearOff, minFloor, maxFloor);
        float b = geomClamp(p1X + dir * farOff, minFloor, maxFloor);
        float lo = a < b ? a : b;
        float hi = a < b ? b : a;
        if (hi - lo < 2.5f)
        {
            float span = geomMax(3.5f, (farOff - nearOff));
            if (dir > 0.0f)
            {
                lo = geomClamp(maxFloor - span, minFloor, maxFloor - 2.5f);
                hi = maxFloor;
            }
            else
            {
                lo = minFloor;
                hi = geomClamp(minFloor + span, minFloor + 2.5f, maxFloor);
            }
        }

        z.active = 1u;
        z.originX = p1X;
        z.minX = lo;
        z.maxX = hi;
        z.y = geom.floor.centerY;
        (void)p1Y;
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
            z.streak++;
            if (z.streak > z.bestStreak)
            {
                z.bestStreak = z.streak;
            }
        }
        else
        {
            if (wdSuccess)
            {
                bool past = (z.dir > 0.0f) ? (endX > z.maxX) : (endX < z.minX);
                out = past ? static_cast<u8>(kWdZoneOvershoot) : static_cast<u8>(kWdZoneShort);
            }
            else
            {
                out = kWdZoneShort;
            }
            z.streak = 0u;
        }
        z.lastOutcome = out;
        z.flashFrames = 18u;
        return out;
    }

    inline void computeWavedashCameraRect(float p1X, float p1Y, const WdZoneState& z, float& left, float& right,
                                          float& top, float& bottom)
    {
        float lo = p1X < z.minX ? p1X : z.minX;
        float hi = p1X > z.maxX ? p1X : z.maxX;
        left = lo - 18.0f;
        right = hi + 18.0f;
        float baseY = z.y < p1Y ? z.y : p1Y;
        bottom = baseY - 12.0f;
        top = baseY + 36.0f;
    }

    // -------------------------------------------------------------------------
    // 5. Solo Tech Practice CPU Standby State Machine
    // -------------------------------------------------------------------------
    inline bool shouldCpuStandby(u8 activeDrill, const PracticeConfig& cfg)
    {
        switch (activeDrill)
        {
        case kPracticeDrillLedgedash:
        case kPracticeDrillWavedash:
        case kPracticeDrillRar:
        case kPracticeDrillDacus:
        case kPracticeDrillBReverse:
            return true;
        case kPracticeDrillLCancel:
            return cfg.lcancelTarget == kTargetOff;
        case kPracticeDrillHoverFloat:
            return cfg.floatDjcTarget == kTargetOff;
        case kPracticeDrillPowershield:
        case kPracticeDrillNone:
        default:
            return false;
        }
    }

    inline u8 stepStandbyState(bool wantStandby, bool& isStandby)
    {
        if (wantStandby)
        {
            if (!isStandby)
            {
                isStandby = true;
                return kStandbyEnter;
            }
            return kStandbyHold;
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
