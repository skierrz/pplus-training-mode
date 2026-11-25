#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable Floorhug / ASDI-Down and Crouch-Cancel detection, hitstun
// saved, and counter-punish grading core for Project+ 3.2.1. Zero Brawl headers
// so it compiles on both PowerPC (MWCC 4.2) and PC host tests.
//
// Verified P+ 3.2.1 rules:
//   - Crouch-Cancel (Knockback.asm:1-15): 2/3 KB & hitlag ONLY in Squat (0x11)
//     or Squat_Wait (0x12).
//   - ASDI C-Stick priority (DirectionalInfluence.asm:52-88, ButtonPresses.asm:216):
//     evaluated on last hitlag frame with threshold 0.7 (0.49 squared). If
//     LA-Float[34]^2 + LA-Float[35]^2 >= 0.49, C-Stick overrides Control Stick
//     (x = cX * lr, y = cY); else Control Stick if sX^2 + sY^2 >= 0.49.
//   - Floorhug cancel (MoveDef/Hitstun.asm:10-74): Damage_Air (0x44) -> Landing
//     (0x16) with hitstun > 0. Actionable once motionFrame + motionRate >
//     IC-Basic[3074] (Customize_Param_Float_Landing_Frame) + 0.01f (view.cpp:284).
//   - Hitstun Saved = max(0, hitstunAtLanding - (floor(landingFrame) + 1)).
namespace tm {
namespace extra {
    enum FloorhugStatusId {
        kFhStatusWait = 0x00,
        kFhStatusWalk = 0x01,
        kFhStatusWalkBrake = 0x02,
        kFhStatusDash = 0x03,
        kFhStatusRun = 0x04,
        kFhStatusTurn = 0x06,
        kFhStatusTurnDash = 0x07,
        kFhStatusJumpSquat = 0x0A,
        kFhStatusJumpAerial = 0x0C,
        kFhStatusFly = 0x0D,
        kFhStatusSquat = 0x11,
        kFhStatusSquatWait = 0x12,
        kFhStatusSquatRv = 0x15,
        kFhStatusLanding = 0x16,
        kFhStatusGuardOn = 0x1A,
        kFhStatusEscape = 0x1E,
        kFhStatusEscapeAir = 0x21,
        kFhStatusAttackFirst = 0x24,
        kFhStatusAttackAir = 0x33,
        kFhStatusCatch = 0x34,
        kFhStatusCatchPull = 0x35,
        kFhStatusCatchDash = 0x36,
        kFhStatusCatchDashPull = 0x37,
        kFhStatusDamage = 0x43,
        kFhStatusDamageAir = 0x44,
        kFhStatusDamageFly = 0x45,
        kFhStatusDamageFlyRoll = 0x46,
        kFhStatusDamageFall = 0x49,
        kFhStatusDown = 0x4A,
        kFhStatusPassive = 0x60,
        kFhStatusPassiveFb = 0x61,
        kFhStatusSpecialN = 0x112,
        kFhStatusFinal = 0x116
    };

    enum FloorhugOutcome {
        kFhNone = 0,
        kFhCcFloorhugCancel = 1,       // wasCrouching && 0x44 -> 0x16 with hitstun > 0
        kFhStandingFloorhugCancel = 2, // !wasCrouching && 0x44 -> 0x16 with hitstun > 0
        kFhGroundedCc = 3,             // wasCrouching && 0x43 without 0x44
        kFhAsdiDownGroundTech = 4,     // (0x45 || 0x46) && asdiDown && Passive (0x60/0x61)
        kFhBrokenTumble = 5,           // (0x45 || 0x46) without ground tech
        kFhMissedFloorhug = 6          // 0x44 without frame-1 cancel into 0x16
    };

    enum FloorhugPhase {
        kFhPhaseIdle = 0,
        kFhPhaseInHit = 1,      // waiting for hitlag end + outcome classification
        kFhPhasePunishWatch = 2 // watching post-Landing / post-Damage counter-action & true punish
    };

    enum {
        kFhMaxEvents = 8,
        kFhMaxAttackers = 8,
        kFhTumbleTechWindow = 6,
        kFhPunishWatchTimeout = 45
    };

    struct FhEvent {
        s16 status;
        s16 prev;
    };

    struct FhAttackerView {
        s8 slot;
        s16 status;
        s16 hitlag;
        s16 hitstun;
        u8 actionable;
        u8 outOfPlay;
    };

    struct FloorhugFrameInput {
        s32 frame;
        s16 status;
        s16 hitlag;
        s16 prevHitlag;
        s16 hitstun;
        s16 prevHitstun;
        u8 airborne;
        u8 actionable;
        u8 hitReceived;
        u8 shieldReceived;
        u8 hitDealt;
        s8 hitDealtTarget;
        s8 attackerSlot; // from tracker lastReceived.other on hitReceived (-1 if unknown)
        u8 outOfPlay;
        float lr;
        float stickX;
        float stickY;
        float cStickX;      // LA-Float[34] (C-Stick Relative X)
        float cStickY;      // LA-Float[35] (C-Stick Y)
        float motionFrame;  // soMotionModule::getFrame()
        float motionRate;   // soMotionModule::getRate()
        float landingFrame; // IC-Basic[3074] Customize_Param_Float_Landing_Frame
        u8 eventCount;
        FhEvent events[kFhMaxEvents];
        u8 attackerCount;
        FhAttackerView attackers[kFhMaxAttackers];
    };

    struct AsdiEval {
        u8 hasAsdi;
        u8 fromCStick;
        u8 asdiDown;
        float effX;
        float effY;
    };

    struct FloorhugResult {
        u8 outcome;           // FloorhugOutcome
        u8 wasCrouching;      // Squat (0x11) or Squat_Wait (0x12) before hit
        u8 asdiDown;          // 1 if effective ASDI Y < 0 on last hitlag frame
        u8 asdiFromCStick;    // 1 if C-Stick won priority (>= 0.7 radius)
        u8 trueCounterPunish; // 1 if counter-attack/grab connected before attacker actionable
        s16 hitstunAtLanding;
        s16 hitstunSaved;     // max(0, hitstunAtLanding - (floor(landingFrame) + 1))
        s16 punishActFrame;   // 1 = frame-perfect counter-action (0 if none yet)
        float effectiveAsdiX;
        float effectiveAsdiY;
        float landingFrame;
        s32 frame;
    };

    struct FloorhugState {
        u8 phase;
        s16 prevFrameStatus;
        // Per-hit transient tracking.
        s16 preHitStatus;
        u8 wasCrouching;
        u8 sawDamage43;
        u8 sawDamage44;
        u8 sawTumble;
        u8 asdiSampled;
        AsdiEval asdi;
        s8 attackerSlot;
        u8 attackerBecameActionable;
        s32 postHitlagFrames;
        // Punish watch tracking.
        u8 punishSourceLanding; // 1 = Landing (0x16), 0 = Damage (0x43)
        s32 firstActionableFrame;
        u8 counterActionStarted;
        s32 punishWatchStart;
        // Totals & last result.
        u32 hitCount;
        u32 cancelCount;
        u32 truePunishCount;
        u8 completedThisFrame;
        FloorhugResult last;
    };

    inline s32 fhFloorInt(float v)
    {
        s32 i = static_cast<s32>(v);
        return (v < 0.0f && static_cast<float>(i) != v) ? (i - 1) : i;
    }

    inline bool fhDamageStatus(s16 s)
    {
        return s >= kFhStatusDamage && s <= kFhStatusDamageFlyRoll;
    }

    inline bool fhCrouchStatus(s16 s)
    {
        return s == kFhStatusSquat || s == kFhStatusSquatWait;
    }

    inline bool fhCounterActionStatus(s16 s)
    {
        return s == kFhStatusDash || s == kFhStatusTurnDash || s == kFhStatusJumpSquat
               || s == kFhStatusJumpAerial || s == kFhStatusFly || s == kFhStatusGuardOn
               || (s >= kFhStatusEscape && s <= kFhStatusEscapeAir)
               || (s >= kFhStatusAttackFirst && s <= kFhStatusCatchDash)
               || (s >= kFhStatusSpecialN && s < kFhStatusFinal);
    }

    // Exact P+ 3.2.1 ASDI stick evaluation (DirectionalInfluence.asm:52-88):
    // C-Stick takes priority over Control Stick if cX^2 + cY^2 >= 0.49 (0.7^2).
    inline AsdiEval fhEvalAsdi(float cStickX, float cStickY, float stickX, float stickY, float lr)
    {
        const float kThresholdSq = 0.49f; // 0.7f * 0.7f (IC-Basic[3173])
        AsdiEval out;
        memset(&out, 0, sizeof(out));
        const float cSq = cStickX * cStickX + cStickY * cStickY;
        if (cSq >= kThresholdSq)
        {
            out.hasAsdi = 1;
            out.fromCStick = 1;
            out.effX = cStickX * (lr < 0.0f ? -1.0f : 1.0f);
            out.effY = cStickY;
            out.asdiDown = (cStickY < 0.0f) ? 1 : 0;
            return out;
        }
        const float sSq = stickX * stickX + stickY * stickY;
        if (sSq >= kThresholdSq)
        {
            out.hasAsdi = 1;
            out.fromCStick = 0;
            out.effX = stickX;
            out.effY = stickY;
            out.asdiDown = (stickY < 0.0f) ? 1 : 0;
            return out;
        }
        return out;
    }

    inline s16 fhComputeHitstunSaved(s16 hitstunAtLanding, float landingFrame)
    {
        if (hitstunAtLanding <= 0)
        {
            return 0;
        }
        s32 lagFrames = fhFloorInt(landingFrame) + 1;
        if (lagFrames < 1)
        {
            lagFrames = 1;
        }
        s32 saved = static_cast<s32>(hitstunAtLanding) - lagFrames;
        return saved > 0 ? static_cast<s16>(saved) : 0;
    }

    inline const FhAttackerView* fhFindAttacker(const FloorhugFrameInput& in, s8 slot)
    {
        if (slot < 0)
        {
            return NULL;
        }
        const int n = in.attackerCount < kFhMaxAttackers ? in.attackerCount : kFhMaxAttackers;
        for (int i = 0; i < n; i++)
        {
            if (in.attackers[i].slot == slot)
            {
                return &in.attackers[i];
            }
        }
        return NULL;
    }

    inline void floorhugReset(FloorhugState& st)
    {
        memset(&st, 0, sizeof(st));
        st.prevFrameStatus = -1;
        st.preHitStatus = -1;
        st.attackerSlot = -1;
        st.firstActionableFrame = -1;
        st.punishWatchStart = -1;
        st.last.frame = -1;
    }

    inline void fhEmitOutcome(FloorhugState& st, u8 outcome, s16 hitstunAtLand, float landingFrame, s32 frame,
                              bool startPunishWatch, bool sourceLanding)
    {
        st.hitCount++;
        if (outcome == kFhCcFloorhugCancel || outcome == kFhStandingFloorhugCancel)
        {
            st.cancelCount++;
        }
        st.completedThisFrame = 1;
        st.last.outcome = outcome;
        st.last.wasCrouching = st.wasCrouching;
        st.last.asdiDown = st.asdi.asdiDown;
        st.last.asdiFromCStick = st.asdi.fromCStick;
        st.last.trueCounterPunish = 0;
        st.last.hitstunAtLanding = hitstunAtLand;
        st.last.hitstunSaved =
            (outcome == kFhCcFloorhugCancel || outcome == kFhStandingFloorhugCancel)
                ? fhComputeHitstunSaved(hitstunAtLand, landingFrame)
                : 0;
        st.last.punishActFrame = 0;
        st.last.effectiveAsdiX = st.asdi.effX;
        st.last.effectiveAsdiY = st.asdi.effY;
        st.last.landingFrame = landingFrame;
        st.last.frame = frame;

        if (startPunishWatch)
        {
            st.phase = kFhPhasePunishWatch;
            st.punishSourceLanding = sourceLanding ? 1 : 0;
            st.firstActionableFrame = -1;
            st.counterActionStarted = 0;
            st.punishWatchStart = frame;
        }
        else
        {
            st.phase = kFhPhaseIdle;
        }
    }

    inline bool floorhugStep(FloorhugState& st, const FloorhugFrameInput& in)
    {
        st.completedThisFrame = 0;
        if (in.outOfPlay)
        {
            st.phase = kFhPhaseIdle;
            st.prevFrameStatus = in.status;
            return false;
        }

        const int evCount = in.eventCount < kFhMaxEvents ? in.eventCount : kFhMaxEvents;

        // Start a fresh hit cycle on every unshielded hit received.
        if (in.hitReceived && !in.shieldReceived)
        {
            s16 preSt = st.prevFrameStatus;
            for (int k = 0; k < evCount; k++)
            {
                if (fhDamageStatus(in.events[k].status))
                {
                    preSt = in.events[k].prev;
                    break;
                }
            }
            st.phase = kFhPhaseInHit;
            st.preHitStatus = preSt;
            st.wasCrouching = fhCrouchStatus(preSt) ? 1 : 0;
            st.sawDamage43 = 0;
            st.sawDamage44 = 0;
            st.sawTumble = 0;
            st.asdiSampled = 0;
            memset(&st.asdi, 0, sizeof(st.asdi));
            st.attackerSlot = in.attackerSlot;
            st.attackerBecameActionable = 0;
            st.postHitlagFrames = 0;
            st.firstActionableFrame = -1;
            st.counterActionStarted = 0;
        }

        if (st.phase == kFhPhaseInHit)
        {
            // Track attacker actionability across hitlag/endlag so trueCounterPunish is accurate.
            const FhAttackerView* atk = fhFindAttacker(in, st.attackerSlot);
            if (atk != NULL && atk->actionable && atk->hitlag == 0 && atk->hitstun == 0)
            {
                st.attackerBecameActionable = 1;
            }

            // Record all damage statuses and transitions seen during this hit.
            bool saw44To16 = false;
            bool sawPassive = (in.status == kFhStatusPassive || in.status == kFhStatusPassiveFb);
            if (in.status == kFhStatusDamage)
            {
                st.sawDamage43 = 1;
            }
            else if (in.status == kFhStatusDamageAir)
            {
                st.sawDamage44 = 1;
            }
            else if (in.status == kFhStatusDamageFly || in.status == kFhStatusDamageFlyRoll)
            {
                st.sawTumble = 1;
            }
            for (int k = 0; k < evCount; k++)
            {
                const s16 s = in.events[k].status;
                const s16 prev = in.events[k].prev;
                if (s == kFhStatusDamage)
                {
                    st.sawDamage43 = 1;
                }
                else if (s == kFhStatusDamageAir)
                {
                    st.sawDamage44 = 1;
                }
                else if (s == kFhStatusDamageFly || s == kFhStatusDamageFlyRoll)
                {
                    st.sawTumble = 1;
                }
                else if (s == kFhStatusLanding && prev == kFhStatusDamageAir)
                {
                    st.sawDamage44 = 1;
                    saw44To16 = true;
                }
                else if (s == kFhStatusPassive || s == kFhStatusPassiveFb)
                {
                    sawPassive = true;
                }
            }

            // Sample ASDI input on the last hitlag frame (cur.hitlag == 1 or prev.hitlag == 1).
            // On cur.hitlag == 1, capture the held stick; on prev.hitlag == 1 (the 1 -> 0 tick
            // where P+ execStop evaluates ASDI), update if an active stick is present or if not
            // yet sampled.
            if (in.hitlag == 1 || in.prevHitlag == 1)
            {
                AsdiEval ev = fhEvalAsdi(in.cStickX, in.cStickY, in.stickX, in.stickY, in.lr);
                if (!st.asdiSampled || ev.hasAsdi || in.hitlag == 1)
                {
                    st.asdi = ev;
                    st.asdiSampled = 1;
                }
            }
            else if (!st.asdiSampled && in.hitlag == 0)
            {
                st.asdi = fhEvalAsdi(in.cStickX, in.cStickY, in.stickX, in.stickY, in.lr);
                st.asdiSampled = 1;
            }

            // Classify once hitlag ends (or immediately if 0x44 -> 0x16 already fired).
            if (in.hitlag == 0 || saw44To16)
            {
                st.postHitlagFrames++;
                if (st.sawTumble)
                {
                    if (sawPassive && st.asdi.asdiDown)
                    {
                        fhEmitOutcome(st, kFhAsdiDownGroundTech, 0, in.landingFrame, in.frame, false, false);
                    }
                    else if (in.status == kFhStatusDown || in.status == kFhStatusDamageFall
                             || st.postHitlagFrames >= kFhTumbleTechWindow
                             || (in.hitstun == 0 && !sawPassive))
                    {
                        fhEmitOutcome(st, kFhBrokenTumble, 0, in.landingFrame, in.frame, false, false);
                    }
                }
                else if (st.sawDamage44)
                {
                    s16 hsAtLand = in.hitstun > 0 ? in.hitstun : in.prevHitstun;
                    if (saw44To16 && hsAtLand > 0)
                    {
                        u8 outcome = st.wasCrouching ? static_cast<u8>(kFhCcFloorhugCancel)
                                                     : static_cast<u8>(kFhStandingFloorhugCancel);
                        fhEmitOutcome(st, outcome, hsAtLand, in.landingFrame, in.frame, true, true);
                    }
                    else if (st.postHitlagFrames >= 2 || (in.status != kFhStatusDamageAir && !saw44To16))
                    {
                        fhEmitOutcome(st, kFhMissedFloorhug, 0, in.landingFrame, in.frame, false, false);
                    }
                }
                else if (st.sawDamage43)
                {
                    if (st.wasCrouching)
                    {
                        fhEmitOutcome(st, kFhGroundedCc, in.hitstun, in.landingFrame, in.frame, true, false);
                    }
                    else
                    {
                        // Standing grounded Damage (0x43) without crouch: no CC/Floorhug event.
                        st.phase = kFhPhaseIdle;
                    }
                }
                else
                {
                    st.phase = kFhPhaseIdle;
                }
            }
        }

        // Punish execution grading after CC Floorhug, Standing Floorhug, or Grounded CC.
        if (st.phase == kFhPhasePunishWatch)
        {
            // Detect whether defender started a counter-action on this frame.
            bool startedAction = false;
            bool grabPulled = (in.status == kFhStatusCatchPull || in.status == kFhStatusCatchDashPull);
            for (int k = 0; k < evCount; k++)
            {
                const s16 s = in.events[k].status;
                if (fhCounterActionStatus(s))
                {
                    startedAction = true;
                }
                if (s == kFhStatusCatchPull || s == kFhStatusCatchDashPull)
                {
                    grabPulled = true;
                }
            }

            if (st.firstActionableFrame < 0)
            {
                if (st.punishSourceLanding)
                {
                    if (startedAction)
                    {
                        st.firstActionableFrame = in.frame;
                    }
                    else if (in.status == kFhStatusLanding && in.motionRate > 0.0f)
                    {
                        // view.cpp:284-288 & docs/internals.md:519-524:
                        // Landing opens interrupts on the frame where motionFrame advances
                        // past landingFrame + 0.01f.
                        if (in.motionFrame > in.landingFrame + 0.01f)
                        {
                            st.firstActionableFrame = in.frame;
                        }
                        else if (in.motionFrame + in.motionRate > in.landingFrame + 0.01f)
                        {
                            st.firstActionableFrame = in.frame + 1;
                        }
                    }
                    else if (in.status != kFhStatusLanding && in.actionable)
                    {
                        st.firstActionableFrame = in.frame;
                    }
                }
                else
                {
                    // Grounded CC in Damage (0x43): actionable when hitstun reaches 0.
                    if (startedAction || (in.hitstun == 0 && in.hitlag == 0 && in.actionable))
                    {
                        st.firstActionableFrame = in.frame;
                    }
                }
            }

            if (!st.counterActionStarted && startedAction)
            {
                s32 base = st.firstActionableFrame >= 0 ? st.firstActionableFrame : in.frame;
                s32 actFrame = in.frame - base + 1;
                if (actFrame < 1)
                {
                    actFrame = 1;
                }
                st.counterActionStarted = 1;
                st.last.punishActFrame = static_cast<s16>(actFrame);
            }

            // True Counter-Punish: defender's counter-attack/grab connects BEFORE attacker
            // becomes actionable again after the initiating attack!
            const FhAttackerView* atk = fhFindAttacker(in, st.attackerSlot);
            bool hitConnected = (in.hitDealt && (st.attackerSlot < 0 || in.hitDealtTarget == st.attackerSlot))
                                || grabPulled;
            if (hitConnected && !st.attackerBecameActionable)
            {
                st.last.trueCounterPunish = 1;
                st.truePunishCount++;
                st.phase = kFhPhaseIdle;
            }
            else
            {
                if (atk != NULL && atk->actionable && atk->hitlag == 0 && atk->hitstun == 0)
                {
                    st.attackerBecameActionable = 1;
                }
                if (in.frame - st.punishWatchStart >= kFhPunishWatchTimeout)
                {
                    st.phase = kFhPhaseIdle;
                }
                else if (st.attackerBecameActionable && st.counterActionStarted
                         && in.frame - st.punishWatchStart >= 15)
                {
                    st.phase = kFhPhaseIdle;
                }
            }
        }

        st.prevFrameStatus = in.status;
        return st.completedThisFrame != 0;
    }
}
}
