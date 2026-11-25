#pragma once

#include <math.h>
#include <string.h>
#include <types.h>

#include "shared/game/stick.h"

// Pure host-testable Project+ 3.2.1 Wavedash & Platform Waveland detection,
// grading, and telemetry core. Zero Brawl headers so all logic compiles
// identically on PC (`make check-host`) and PowerPC (MWCC 4.2).
//
// P+ 3.2.1 ground-truth rules verified in source and memory dumps:
//  - Stick math: 0.010325f scale, radius 100 cap (DirectionalInput.asm:4-47),
//    0.28f per-axis deadzone -> shallowest downward airdodge (96, -28) =
//    16.26 deg below horizontal.
//  - Airdodge initial speed: min(1.0, hypot(sx, sy) * 1.28) * 3.1, 28f length,
//    0.9x/frame decay (Source/Project+/AirDodge.asm:77-276; IEEE-754
//    0x3FA3D70A = 1.28f at line 117).
//  - 1-frame earlier physics (Source/Project+/Physics.asm:413-597): a frame-1
//    wavedash traverses 0x0A -> 0x0B -> 0x21 -> 0x19 on a single game frame.
//  - Landing lag: LA-Float[0] = 10.0f in Landing_Fall_Special (0x19)
//    (Source/Project+/AirDodge.asm:289-302).
//  - Tether Z-input (RSBE01.TXT:1777, mem1 0x80FAA6EC..0x80FAA760): pressing Z
//    in air asserts Attack + Guard pressed on the same frame, entering
//    Air_Lasso (0x7F) instead of Escape_Air (0x21) for tether characters.
namespace tm {
namespace extra {
    enum {
        kWdCoreMaxEvents = 8,
        kWdCoreNoFrame = -1,
        kWdCoreAirWindow = 30,         // max airborne frames for grounded wavedash watch
        kWdCoreMaxGoodDelay = 10,      // jumpsquatToDodgeFrames <= 10 -> wavedash, > 10 same-floor -> late miss
        kWdCoreLateGuardWindow = 6,    // frames after Landing (0x16/0x17) where guard press -> kWdMissLateAirdodge
        kWdCoreLandingLag = 10,        // LA-Float[0] = 10.0
        kWdCoreShortHopFlag = 1 << 6,  // RA-Bit[6]
        kWdCoreButtonJump = 1 << 2,    // ipPadButton::MASK_JUMP
        kWdCoreButtonGuard = 1 << 3    // ipPadButton::MASK_GUARD
    };

    // Common Brawl/P+ fighter statuses (ft/fighter.h).
    enum {
        kWdStJumpSquat = 0x0A,
        kWdStJump = 0x0B,
        kWdStJumpAerial = 0x0C,
        kWdStFly = 0x0D,
        kWdStFall = 0x0E,
        kWdStFallAerial = 0x0F,
        kWdStFallSpecial = 0x10,
        kWdStLanding = 0x16,
        kWdStLandingLight = 0x17,
        kWdStLandingAttackAir = 0x18,
        kWdStLandingFallSpecial = 0x19,
        kWdStGuardOn = 0x1A,
        kWdStEscapeAir = 0x21,
        kWdStAttackAir = 0x33,
        kWdStPass = 0x72,
        kWdStAirLassoFirst = 0x7F, // Air_Lasso (0x7E is Miss_Foot)
        kWdStAirLassoLast = 0x83,
        kWdStItemThrow = 0x9B,
        kWdStDead = 0xBD,
        kWdStRebirth = 0xBE,
        kWdStStandby = 0x10B
    };

    const float kWdDeadzone = 0.28f;
    const float kWdSpeedScale = 1.28f; // AirDodge.asm:117 (0x3FA3D70A)
    const float kWdMaxSpeed = 3.1f;    // AirDodge.asm:133 (0x40466666)
    const float kWdDecayPerFrame = 0.9f;
    const float kWdShallowestDownDeg = 16.2602047f; // atan2(28, 96) deg
    const float kWdPlatformDeltaY = 3.0f;           // elevation change distinguishing jump-to-platform waveland

    enum WdKind {
        kWdKindNone = 0,
        kWdKindWavedash = 1,
        kWdKindWaveland = 2
    };

    enum WdOutcome {
        kWdOutcomeNone = 0,
        kWdOutcomeSuccess = 1,
        kWdMissNoAirdodge = 2,
        kWdMissLateAirdodge = 3,
        kWdMissUpwardAirdodge = 4,
        kWdMissZairTether = 5
    };

    enum WdPhase {
        kWdPhaseIdle = 0,
        kWdPhaseSquat = 1,
        kWdPhaseJumpAir = 2,
        kWdPhaseAirdodge = 3,
        kWdPhaseSliding = 4,
        kWdPhaseMissedLandWait = 5
    };

    struct WdStatusEv {
        s16 status;
        s16 prev;
        u32 flags;
        u8 airborne;
    };

    struct WdFrameInput {
        s32 frame;
        s16 status;
        s16 prevStatus;
        s16 hitstun;
        u8 airborne;
        u8 actionable;
        u8 onPlatform;      // true if landing surface is a pass-through platform (stageGeom or y > floorY + 1)
        u8 guardEdge;       // fresh digital shield press or analog L/R press this frame
        u32 buttons;        // ipPadButton held bits
        float x;
        float y;
        float stickX;       // post-deadzone controller stickX (-1..1)
        float stickY;       // post-deadzone controller stickY (-1..1)
        u8 eventCount;
        WdStatusEv events[kWdCoreMaxEvents];
    };

    struct WdResult {
        s32 doneFrame;
        u8 kind;                 // WdKind (wavedash vs waveland)
        u8 outcome;              // WdOutcome
        u8 shortHop;             // 1 if short hop (grounded wavedash)
        u8 onPlatform;           // 1 if landed on a platform
        s16 jumpsquatToDodge;    // 1 = frame-perfect wavedash (0 for non-JumpSquat wavelands)
        s16 jumpHoldFrames;      // frames jump was held in Jump_Squat
        s16 airdodgeAirFrames;   // 1 = frame-perfect waveland (landFrame - airdodgeFrame + 1)
        float angleDeg;          // degrees below horizontal (techniques.cpp convention)
        float signedAngleDeg;    // signed angle (-down, +up)
        float expectedSpeed;     // P+ AirDodge.asm initial speed
        float slideDistance;     // |xAtActionable - xAtLand|
        float signedSlideDx;     // xAtActionable - xAtLand
    };

    struct WdStats {
        u16 wdAttempts;
        u16 wdSuccesses;
        u16 wdPerfect;
        u16 wdStreak;
        u16 wdBestStreak;
        u16 wlAttempts;
        u16 wlSuccesses;
        u16 wlPerfect;
        u16 wlStreak;
        u16 wlBestStreak;
    };

    struct WdCoreState {
        // Active transient state.
        u8 phase;
        u8 pendingKind;          // WdKind
        u8 pendingOutcome;       // WdOutcome (used during sliding)
        u8 fromJumpSquat;
        u8 shortHop;
        u8 onPlatform;
        s32 jumpSquatFrame;
        s32 airFrame;
        s32 airdodgeFrame;
        s32 landFrame;
        s16 jumpHoldFrames;
        s16 jumpsquatToDodge;
        s16 airdodgeAirFrames;
        float jumpTakeoffY;
        float xAtLand;
        float angleDeg;
        float signedAngleDeg;
        float expectedSpeed;

        // Completed result and cumulative stats.
        u32 seq;
        WdResult last;
        WdStats stats;
    };

    // Normalizes raw GameCube stick units (px, py in -128..127) with P+ 3.2.1's
    // radius-100 cap, 0.010325f scale, and 0.28f per-axis deadzone.
    inline void wdNormalizeRawStick(s8 px, s8 py, float& sx, float& sy)
    {
        game::mainStick(px, py, &sx, &sy);
        if (sx > -kWdDeadzone && sx < kWdDeadzone)
        {
            sx = 0.0f;
        }
        if (sy > -kWdDeadzone && sy < kWdDeadzone)
        {
            sy = 0.0f;
        }
    }

    // Degrees from horizontal (matches techniques.cpp::stickAngle).
    inline float wdStickAngleDeg(float sx, float sy)
    {
        float ax = sx < 0.0f ? -sx : sx;
        float ay = sy < 0.0f ? -sy : sy;
        if (ax < 0.0001f && ay < 0.0001f)
        {
            return 0.0f;
        }
        return static_cast<float>(atan2(ay, ax) * 57.29577951308232);
    }

    // Signed stick pitch relative to horizontal (-90 straight down, +90 up).
    inline float wdSignedAngleDeg(float sx, float sy)
    {
        float ax = sx < 0.0f ? -sx : sx;
        float ay = sy < 0.0f ? -sy : sy;
        if (ax < 0.0001f && ay < 0.0001f)
        {
            return 0.0f;
        }
        return static_cast<float>(atan2(sy, ax) * 57.29577951308232);
    }

    // Exact P+ 3.2.1 AirDodge.asm:105-138 initial speed:
    // min(1.0, hypot(sx, sy) * 1.28) * 3.1.
    inline float wdAirdodgeSpeed(float sx, float sy)
    {
        float magSq = sx * sx + sy * sy;
        if (magSq < 1e-8f)
        {
            return 0.0f;
        }
        float mag = game::stickRoot(magSq) * kWdSpeedScale;
        if (mag > 1.0f)
        {
            mag = 1.0f;
        }
        return mag * kWdMaxSpeed;
    }

    inline bool isWdAirSource(int s)
    {
        return s == kWdStJump || s == kWdStJumpAerial || s == kWdStFly || s == kWdStFall || s == kWdStFallAerial
               || s == kWdStAttackAir || s == kWdStPass;
    }

    inline bool isWdTetherStatus(int s)
    {
        return s >= kWdStAirLassoFirst && s <= kWdStAirLassoLast;
    }

    inline bool isWdOutOfPlay(int s)
    {
        return s == kWdStDead || s == kWdStRebirth || s == kWdStStandby;
    }

    inline void wdCoreResetTransient(WdCoreState& st)
    {
        st.phase = kWdPhaseIdle;
        st.pendingKind = kWdKindNone;
        st.pendingOutcome = kWdOutcomeNone;
        st.fromJumpSquat = 0;
        st.shortHop = 0;
        st.onPlatform = 0;
        st.jumpSquatFrame = kWdCoreNoFrame;
        st.airFrame = kWdCoreNoFrame;
        st.airdodgeFrame = kWdCoreNoFrame;
        st.landFrame = kWdCoreNoFrame;
        st.jumpHoldFrames = 0;
        st.jumpsquatToDodge = 0;
        st.airdodgeAirFrames = 0;
        st.jumpTakeoffY = 0.0f;
        st.xAtLand = 0.0f;
        st.angleDeg = 0.0f;
        st.signedAngleDeg = 0.0f;
        st.expectedSpeed = 0.0f;
    }

    inline void wdCoreResetAll(WdCoreState& st)
    {
        memset(&st, 0, sizeof(st));
        wdCoreResetTransient(st);
        st.last.doneFrame = kWdCoreNoFrame;
    }

    inline void wdCoreFinalize(WdCoreState& st, s32 frame, u8 kind, u8 outcome, float slideDx)
    {
        WdResult& r = st.last;
        memset(&r, 0, sizeof(r));
        r.doneFrame = frame;
        r.kind = kind;
        r.outcome = outcome;
        r.shortHop = st.shortHop;
        r.onPlatform = st.onPlatform;
        r.jumpsquatToDodge = st.jumpsquatToDodge;
        r.jumpHoldFrames = st.jumpHoldFrames;
        r.airdodgeAirFrames = st.airdodgeAirFrames;
        r.angleDeg = st.angleDeg;
        r.signedAngleDeg = st.signedAngleDeg;
        r.expectedSpeed = st.expectedSpeed;
        r.signedSlideDx = slideDx;
        r.slideDistance = slideDx < 0.0f ? -slideDx : slideDx;

        if (kind == kWdKindWavedash)
        {
            st.stats.wdAttempts++;
            if (outcome == kWdOutcomeSuccess)
            {
                st.stats.wdSuccesses++;
                if (r.jumpsquatToDodge == 1)
                {
                    st.stats.wdPerfect++;
                }
                st.stats.wdStreak++;
                if (st.stats.wdStreak > st.stats.wdBestStreak)
                {
                    st.stats.wdBestStreak = st.stats.wdStreak;
                }
            }
            else
            {
                st.stats.wdStreak = 0;
            }
        }
        else if (kind == kWdKindWaveland)
        {
            st.stats.wlAttempts++;
            if (outcome == kWdOutcomeSuccess)
            {
                st.stats.wlSuccesses++;
                if (r.airdodgeAirFrames == 1)
                {
                    st.stats.wlPerfect++;
                }
                st.stats.wlStreak++;
                if (st.stats.wlStreak > st.stats.wlBestStreak)
                {
                    st.stats.wlBestStreak = st.stats.wlStreak;
                }
            }
            else
            {
                st.stats.wlStreak = 0;
            }
        }

        st.seq++;
        wdCoreResetTransient(st);
    }

    // Steps one finished game frame for one tracked fighter. Returns true if a
    // wavedash/waveland attempt finalized on this frame (result in st.last).
    inline bool wdCoreStep(WdCoreState& st, const WdFrameInput& in)
    {
        const int n = in.eventCount < kWdCoreMaxEvents ? static_cast<int>(in.eventCount) : kWdCoreMaxEvents;
        bool finalized = false;

        // Hitstun or out-of-play aborts non-sliding watches cleanly; if sliding
        // when hit, finalize slide up to current x.
        if (in.hitstun > 0 || isWdOutOfPlay(in.status))
        {
            if (st.phase == kWdPhaseSliding)
            {
                wdCoreFinalize(st, in.frame, st.pendingKind, st.pendingOutcome, in.x - st.xAtLand);
                return true;
            }
            if (st.phase == kWdPhaseAirdodge && in.status == kWdStDead)
            {
                u8 kind = (st.fromJumpSquat && st.jumpsquatToDodge <= kWdCoreMaxGoodDelay) ? kWdKindWavedash
                                                                                           : kWdKindWaveland;
                wdCoreFinalize(st, in.frame, kind, kWdMissUpwardAirdodge, 0.0f);
                return true;
            }
            wdCoreResetTransient(st);
            return false;
        }

        // If currently sliding in Landing_Fall_Special (0x19), check for exit
        // or first actionable frame before processing new status events so a
        // back-to-back Jump_Squat on frame 10 finalizes the previous slide AND
        // starts the next wavedash cleanly.
        if (st.phase == kWdPhaseSliding)
        {
            bool leftSlide = (in.status != kWdStLandingFallSpecial) || (in.actionable != 0);
            if (!leftSlide)
            {
                for (int k = 0; k < n; k++)
                {
                    if (in.events[k].prev == kWdStLandingFallSpecial && in.events[k].status != kWdStLandingFallSpecial)
                    {
                        leftSlide = true;
                        break;
                    }
                }
            }
            if (leftSlide)
            {
                wdCoreFinalize(st, in.frame, st.pendingKind, st.pendingOutcome, in.x - st.xAtLand);
                finalized = true;
            }
        }

        // If waiting after an empty grounded jump landed in Landing (0x16/0x17)
        // to distinguish kWdMissLateAirdodge vs kWdMissNoAirdodge.
        if (st.phase == kWdPhaseMissedLandWait)
        {
            bool sawGuard = (in.guardEdge != 0) || (in.status == kWdStGuardOn);
            for (int k = 0; k < n && !sawGuard; k++)
            {
                if (in.events[k].status == kWdStGuardOn)
                {
                    sawGuard = true;
                }
            }
            if (sawGuard)
            {
                wdCoreFinalize(st, in.frame, kWdKindWavedash, kWdMissLateAirdodge, 0.0f);
                finalized = true;
            }
            else if (in.frame - st.landFrame >= kWdCoreLateGuardWindow || in.status == kWdStJumpSquat)
            {
                wdCoreFinalize(st, in.frame, kWdKindWavedash, kWdMissNoAirdodge, 0.0f);
                finalized = true;
            }
        }

        for (int k = 0; k < n; k++)
        {
            const WdStatusEv& e = in.events[k];
            int s = e.status;
            int prev = e.prev;

            if (s == kWdStJumpSquat)
            {
                wdCoreResetTransient(st);
                st.phase = kWdPhaseSquat;
                st.fromJumpSquat = 1;
                st.jumpSquatFrame = in.frame;
                st.jumpTakeoffY = in.y;
                continue;
            }

            if (s == kWdStJump && prev == kWdStJumpSquat && st.phase == kWdPhaseSquat)
            {
                st.phase = kWdPhaseJumpAir;
                st.airFrame = in.frame;
                st.shortHop = (e.flags & kWdCoreShortHopFlag) != 0 ? 1 : 0;
                continue;
            }

            // Tether Z-input miss: entered Air_Lasso (0x7F..0x83) from Jump/Fall/EscapeAir.
            if (isWdTetherStatus(s) && (isWdAirSource(prev) || prev == kWdStEscapeAir))
            {
                bool groundedAttempt = (st.fromJumpSquat != 0 && st.airFrame != kWdCoreNoFrame
                                        && in.frame - st.airFrame < kWdCoreAirWindow);
                u8 kind = groundedAttempt ? kWdKindWavedash : kWdKindWaveland;
                if (groundedAttempt)
                {
                    st.jumpsquatToDodge = static_cast<s16>(in.frame - st.airFrame + 1);
                }
                st.angleDeg = wdStickAngleDeg(in.stickX, in.stickY);
                st.signedAngleDeg = wdSignedAngleDeg(in.stickX, in.stickY);
                st.expectedSpeed = wdAirdodgeSpeed(in.stickX, in.stickY);
                wdCoreFinalize(st, in.frame, kind, kWdMissZairTether, 0.0f);
                finalized = true;
                continue;
            }

            // Entering Escape_Air (0x21).
            if (s == kWdStEscapeAir)
            {
                bool groundedWd = (st.fromJumpSquat != 0 && st.phase == kWdPhaseJumpAir
                                   && st.airFrame != kWdCoreNoFrame && in.frame - st.airFrame < kWdCoreAirWindow);
                if (groundedWd)
                {
                    st.jumpsquatToDodge = static_cast<s16>(in.frame - st.airFrame + 1);
                }
                else
                {
                    // Distinct aerial/platform waveland watch (from Fall, Jump_Aerial,
                    // Fly, Attack_Air, Pass, or late Jump past kWdCoreAirWindow).
                    if (!isWdAirSource(prev))
                    {
                        wdCoreResetTransient(st);
                        continue;
                    }
                    wdCoreResetTransient(st);
                    st.fromJumpSquat = 0;
                }
                st.phase = kWdPhaseAirdodge;
                st.airdodgeFrame = in.frame;
                st.angleDeg = wdStickAngleDeg(in.stickX, in.stickY);
                st.signedAngleDeg = wdSignedAngleDeg(in.stickX, in.stickY);
                st.expectedSpeed = wdAirdodgeSpeed(in.stickX, in.stickY);
                continue;
            }

            // Landing out of Escape_Air (0x21 -> 0x19 Landing_Fall_Special).
            if (s == kWdStLandingFallSpecial && prev == kWdStEscapeAir && st.phase == kWdPhaseAirdodge)
            {
                st.landFrame = in.frame;
                st.airdodgeAirFrames = static_cast<s16>(in.frame - st.airdodgeFrame + 1);
                st.xAtLand = in.x;
                st.onPlatform = in.onPlatform;

                float dy = in.y - st.jumpTakeoffY;
                if (dy < 0.0f)
                {
                    dy = -dy;
                }
                bool elevatedJumpToPlatform = (st.fromJumpSquat != 0 && dy >= kWdPlatformDeltaY);

                if (st.fromJumpSquat != 0 && !elevatedJumpToPlatform)
                {
                    st.pendingKind = kWdKindWavedash;
                    st.pendingOutcome = (st.jumpsquatToDodge <= kWdCoreMaxGoodDelay) ? kWdOutcomeSuccess
                                                                                     : kWdMissLateAirdodge;
                }
                else
                {
                    st.pendingKind = kWdKindWaveland;
                    st.pendingOutcome = kWdOutcomeSuccess;
                    if (elevatedJumpToPlatform)
                    {
                        st.jumpsquatToDodge = 0;
                    }
                }
                st.phase = kWdPhaseSliding;
                continue;
            }

            // Exiting Escape_Air without landing into 0x19.
            if (prev == kWdStEscapeAir && st.phase == kWdPhaseAirdodge && s != kWdStLandingFallSpecial)
            {
                if (s == kWdStItemThrow)
                {
                    // Aerial Glide Toss (0x21 -> 0x9B): not a failed wavedash.
                    wdCoreResetTransient(st);
                }
                else
                {
                    u8 kind = (st.fromJumpSquat != 0 && st.jumpsquatToDodge <= kWdCoreMaxGoodDelay) ? kWdKindWavedash
                                                                                                   : kWdKindWaveland;
                    wdCoreFinalize(st, in.frame, kind, kWdMissUpwardAirdodge, 0.0f);
                    finalized = true;
                }
                continue;
            }

            // Grounded Jump_Squat -> Jump -> [Fall] -> Landing (0x16/0x17) without airdodge.
            if (st.phase == kWdPhaseJumpAir && st.fromJumpSquat != 0)
            {
                if (s == kWdStFall || s == kWdStFallAerial)
                {
                    // Stay in JumpAir watch if falling back down from a short/full hop.
                    if (in.frame - st.airFrame >= kWdCoreAirWindow)
                    {
                        wdCoreResetTransient(st);
                    }
                    continue;
                }
                if (s == kWdStLanding || s == kWdStLandingLight)
                {
                    st.landFrame = in.frame;
                    st.onPlatform = in.onPlatform;
                    st.phase = kWdPhaseMissedLandWait;
                    continue;
                }
                // Aerial, special, double-jump, ledge grab, etc. abort grounded watch quietly.
                wdCoreResetTransient(st);
                continue;
            }

            // Any unexpected exit from Jump_Squat (e.g., JC grab/up-smash) aborts quietly.
            if (st.phase == kWdPhaseSquat && s != kWdStJumpSquat)
            {
                wdCoreResetTransient(st);
            }
        }

        // Count Jump_Squat button hold frames (matches techniques.cpp:329-333).
        if (st.phase == kWdPhaseSquat && in.status == kWdStJumpSquat && st.jumpSquatFrame != kWdCoreNoFrame)
        {
            if ((in.buttons & kWdCoreButtonJump) != 0 && st.jumpHoldFrames == in.frame - st.jumpSquatFrame)
            {
                st.jumpHoldFrames++;
            }
        }

        return finalized;
    }
}
}
