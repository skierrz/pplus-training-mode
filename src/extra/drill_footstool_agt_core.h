#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable Footstool & Lock + Aerial Glide Toss (AGT) / Grounded
// Glide Toss detection core for Project+ 3.2.1. Zero Brawl headers so it
// compiles on both PowerPC (MWCC 4.2) and PC host tests.
//
// Verified P+ 3.2.1 rules:
//   - Footstool (MoveDef/Misc.asm:939-954, ButtonPresses.asm:503-541,
//     ft/fighter.h:129-133):
//       * Taunt-only trigger requiring LA-Basic[82] (0x10000052) >= 20.
//       * Attacker: Tread_Jump (0x6D) -> Jump (0x0B), RA-Bit[6] = shortHop (0.75x).
//       * Grounded victim: Tread_Damage (0x6E) -> Tread_Damage_Rv (0x6F).
//       * Airborne victim: Tread_Damage_Air (0x70) -> Tread_Fall (0x71),
//         ground-techable (Passive 0x60 / Passive_Fb 0x61) or missed tech
//         (Down 0x4A..0x4E -> attacker hit = Footstool Lock).
//   - AGT & Glide Toss (MoveDef/Tethers.asm:148-204, VariableSet.asm:140):
//       * Escape_Air (0x21) or Escape_F/B (0x1F/0x20) -> Item_Throw (0x9B).
//       * LA-Basic[81] (0x10000051) tracks consecutive mid-air AGT count
//         (0.9^N decay) and auto-resets to 0 on Landing (0x16), ledge grab,
//         and Dead (0xBD).
namespace tm {
namespace extra {
    enum FootstoolAgtStatusId {
        kFaStatusJump = 0x0B,
        kFaStatusLanding = 0x16,
        kFaStatusEscapeF = 0x1F,
        kFaStatusEscapeB = 0x20,
        kFaStatusEscapeAir = 0x21,
        kFaStatusDamageFirst = 0x43,
        kFaStatusDamageFlyRoll = 0x46,
        kFaStatusDown = 0x4A,
        kFaStatusDownWaitContinue = 0x4E,
        kFaStatusDownDamage = 0x54,
        kFaStatusPassive = 0x60,
        kFaStatusPassiveFb = 0x61,
        kFaStatusTreadJump = 0x6D,
        kFaStatusTreadDamage = 0x6E,
        kFaStatusTreadDamageRv = 0x6F,
        kFaStatusTreadDamageAir = 0x70,
        kFaStatusTreadFall = 0x71,
        kFaStatusItemThrow = 0x9B,
        kFaStatusDead = 0xBD
    };

    enum FootstoolOutcome {
        kFootstoolNone = 0,
        kFootstoolGrounded = 1,
        kFootstoolAirborne = 2,
        kFootstoolFailTauntLockout = 3
    };

    enum {
        kFaMaxEvents = 8,
        kFaMaxOpponents = 8,
        kFaTauntLockoutThreshold = 20, // LA-Basic[82] >= 20 required in P+ 3.2.1
        kFaShortHopFlagBit = 1 << 6,   // RA-Bit[6] (Flag_Reserve_Jump_Mini)
        // ipPadButton taunt bits: Appeal_Hi (10), Appeal_S (11), Appeal_Lw (12),
        // Appeal_S_L (13), Appeal_S_R (14).
        kFaBtnTauntMask = (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14),
        kFaFollowupTimeout = 120
    };

    struct FaEvent {
        s16 status;
        s16 prev;
        u32 flags;
    };

    struct FaOpponentView {
        s8 slot;
        s16 status;
        s16 prevStatus;
        u8 airborne;
        u8 outOfPlay;
        u8 hitReceived;
        u8 shieldReceived;
        s8 lastReceivedOther;
        float x;
        float y;
        u8 eventCount;
        FaEvent events[kFaMaxEvents];
    };

    struct FootstoolAgtFrameInput {
        s32 frame;
        s8 selfSlot;
        s16 status;
        s16 hitstun;
        u8 airborne;
        u8 outOfPlay;
        u8 attackActive;
        u8 hitDealt;
        s8 hitDealtTarget;
        float x;
        float y;
        float vx;
        float vy;
        u32 buttons;
        u32 triggerButtons;
        u32 statusFlags0; // RA flag word 0 (bit 6: short-hop footstool)
        s32 laBasic81;    // LA-Basic[81] consecutive mid-air AGT count
        s32 laBasic82;    // LA-Basic[82] frames since prior taunt press
        u8 eventCount;
        FaEvent events[kFaMaxEvents];
        u8 opponentCount;
        FaOpponentView opponents[kFaMaxOpponents];
    };

    struct FootstoolResult {
        u8 outcome; // FootstoolOutcome
        u8 shortHop;
        u8 victimTeched;
        u8 victimMissedTech;
        u8 footstoolLockHit;
        s8 victimSlot;
        s16 tauntLockoutFrames; // LA-Basic[82]
        s32 frame;
    };

    struct AgtResult {
        u8 isAerial;      // 1 = Escape_Air (0x21) -> 0x9B, 0 = Escape_F/B -> 0x9B
        u8 throwFrame;    // f - dodgeStart + 1 (1 = optimal frame-1 AGT)
        s16 agtChainCount; // LA-Basic[81] on this AGT
        float vxCarry;
        float vyCarry;
        s32 frame;
    };

    struct FootstoolAgtState {
        // Footstool tracking.
        u8 inTreadJump;
        u8 watchingVictim;
        u8 victimPairPending; // allows pairing on same frame or next frame
        u8 victimWasDown;
        s8 watchVictimSlot;
        s32 footstoolStartFrame;
        u32 footstoolCount;
        u32 lockCount;
        u32 failLockoutCount;
        u8 footstoolCompletedThisFrame;
        FootstoolResult lastFootstool;

        // AGT / Glide Toss tracking.
        s32 dodgeStartFrame;
        s16 dodgeStatus;
        s16 currentAgtChain; // live LA-Basic[81]
        s16 prevLaBasic81;
        u32 agtCount;
        u32 groundedGlideTossCount;
        u32 agtChainResetCount;
        s32 lastAgtResetFrame;
        u8 agtCompletedThisFrame;
        AgtResult lastAgt;

        u32 prevButtons;
    };

    inline float faAbsf(float v)
    {
        return v < 0.0f ? -v : v;
    }

    inline u8 faClampU8(s32 v)
    {
        if (v < 0)
        {
            return 0;
        }
        return v > 255 ? static_cast<u8>(255) : static_cast<u8>(v);
    }

    inline bool faDownStatus(s16 s)
    {
        return s >= kFaStatusDown && s <= kFaStatusDownWaitContinue;
    }

    inline bool faPassiveStatus(s16 s)
    {
        return s == kFaStatusPassive || s == kFaStatusPassiveFb;
    }

    inline bool faSawStatus(const FaOpponentView& opp, s16 target)
    {
        if (opp.status == target)
        {
            return true;
        }
        const int n = opp.eventCount < kFaMaxEvents ? opp.eventCount : kFaMaxEvents;
        for (int k = 0; k < n; k++)
        {
            if (opp.events[k].status == target)
            {
                return true;
            }
        }
        return false;
    }

    inline bool faSawTransition(const FaOpponentView& opp, s16 fromStatus, s16 toStatus)
    {
        const int n = opp.eventCount < kFaMaxEvents ? opp.eventCount : kFaMaxEvents;
        for (int k = 0; k < n; k++)
        {
            if (opp.events[k].prev == fromStatus && opp.events[k].status == toStatus)
            {
                return true;
            }
        }
        return false;
    }

    inline void footstoolAgtReset(FootstoolAgtState& st)
    {
        memset(&st, 0, sizeof(st));
        st.watchVictimSlot = -1;
        st.footstoolStartFrame = -1;
        st.lastFootstool.victimSlot = -1;
        st.lastFootstool.frame = -1;
        st.dodgeStartFrame = -1;
        st.dodgeStatus = -1;
        st.lastAgtResetFrame = -1;
        st.lastAgt.frame = -1;
    }

    inline bool faPairVictim(FootstoolAgtState& st, const FootstoolAgtFrameInput& in)
    {
        const int oppN = in.opponentCount < kFaMaxOpponents ? in.opponentCount : kFaMaxOpponents;
        int bestIdx = -1;
        u8 bestOutcome = kFootstoolNone;
        float bestDist = 1e9f;

        for (int i = 0; i < oppN; i++)
        {
            const FaOpponentView& opp = in.opponents[i];
            if (opp.outOfPlay)
            {
                continue;
            }
            u8 kind = kFootstoolNone;
            if (faSawStatus(opp, kFaStatusTreadDamageAir) || faSawStatus(opp, kFaStatusTreadFall))
            {
                kind = kFootstoolAirborne;
            }
            else if (faSawStatus(opp, kFaStatusTreadDamage) || faSawStatus(opp, kFaStatusTreadDamageRv))
            {
                kind = kFootstoolGrounded;
            }
            if (kind == kFootstoolNone)
            {
                continue;
            }
            float dx = faAbsf(opp.x - in.x);
            float dy = faAbsf(opp.y - in.y);
            float dist = dx + dy;
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
                bestOutcome = kind;
            }
        }

        if (bestIdx < 0)
        {
            return false;
        }

        const FaOpponentView& vic = in.opponents[bestIdx];
        st.lastFootstool.outcome = bestOutcome;
        st.lastFootstool.victimSlot = vic.slot;
        st.victimPairPending = 0;
        if (bestOutcome == kFootstoolAirborne)
        {
            st.watchingVictim = 1;
            st.watchVictimSlot = vic.slot;
            st.victimWasDown = 0;
        }
        return true;
    }

    inline bool faNearAnyOpponent(const FootstoolAgtFrameInput& in)
    {
        const float kNearDx = 24.0f;
        const float kNearDy = 28.0f;
        const int oppN = in.opponentCount < kFaMaxOpponents ? in.opponentCount : kFaMaxOpponents;
        for (int i = 0; i < oppN; i++)
        {
            const FaOpponentView& opp = in.opponents[i];
            if (opp.outOfPlay)
            {
                continue;
            }
            if (faAbsf(opp.x - in.x) <= kNearDx && faAbsf(opp.y - in.y) <= kNearDy)
            {
                return true;
            }
        }
        return false;
    }

    inline void footstoolAgtStep(FootstoolAgtState& st, const FootstoolAgtFrameInput& in)
    {
        st.footstoolCompletedThisFrame = 0;
        st.agtCompletedThisFrame = 0;

        // Track live LA-Basic[81] and auto-resets (Landing 0x16, ledge, Dead 0xBD).
        s16 curLa81 = static_cast<s16>(in.laBasic81 > 0 ? in.laBasic81 : 0);
        if (st.prevLaBasic81 > 0 && curLa81 == 0)
        {
            st.agtChainResetCount++;
            st.lastAgtResetFrame = in.frame;
        }
        st.currentAgtChain = curLa81;
        st.prevLaBasic81 = curLa81;

        if (in.outOfPlay)
        {
            st.inTreadJump = 0;
            st.watchingVictim = 0;
            st.victimPairPending = 0;
            st.dodgeStartFrame = -1;
            st.dodgeStatus = -1;
            st.prevButtons = in.buttons;
            return;
        }

        const int evCount = in.eventCount < kFaMaxEvents ? in.eventCount : kFaMaxEvents;
        bool enteredTreadJump = false;

        for (int k = 0; k < evCount; k++)
        {
            const s16 s = in.events[k].status;
            const s16 prev = in.events[k].prev;

            // --- Footstool attacker entry: Tread_Jump (0x6D) ---
            if (s == kFaStatusTreadJump)
            {
                enteredTreadJump = true;
                st.inTreadJump = 1;
                st.watchingVictim = 0;
                st.victimPairPending = 1;
                st.victimWasDown = 0;
                st.watchVictimSlot = -1;
                st.footstoolStartFrame = in.frame;
                st.footstoolCount++;
                st.footstoolCompletedThisFrame = 1;
                st.lastFootstool.outcome = kFootstoolGrounded; // refined by victim pairing below
                st.lastFootstool.shortHop =
                    ((in.statusFlags0 & kFaShortHopFlagBit) != 0 || (in.events[k].flags & kFaShortHopFlagBit) != 0)
                        ? 1
                        : 0;
                st.lastFootstool.victimTeched = 0;
                st.lastFootstool.victimMissedTech = 0;
                st.lastFootstool.footstoolLockHit = 0;
                st.lastFootstool.victimSlot = -1;
                st.lastFootstool.tauntLockoutFrames = static_cast<s16>(in.laBasic82);
                st.lastFootstool.frame = in.frame;
            }
            else if (s == kFaStatusJump && prev == kFaStatusTreadJump && st.inTreadJump)
            {
                if ((in.events[k].flags & kFaShortHopFlagBit) != 0 || (in.statusFlags0 & kFaShortHopFlagBit) != 0)
                {
                    st.lastFootstool.shortHop = 1;
                }
                st.inTreadJump = 0;
            }
            else if (st.inTreadJump && s != kFaStatusTreadJump)
            {
                st.inTreadJump = 0;
            }

            // --- AGT & Grounded Glide Toss: Escape_F/B/Air -> Item_Throw (0x9B) ---
            if (s == kFaStatusEscapeF || s == kFaStatusEscapeB || s == kFaStatusEscapeAir)
            {
                st.dodgeStartFrame = in.frame;
                st.dodgeStatus = s;
            }
            else if (s == kFaStatusItemThrow
                     && (prev == kFaStatusEscapeF || prev == kFaStatusEscapeB || prev == kFaStatusEscapeAir)
                     && st.dodgeStartFrame >= 0)
            {
                const bool aerial = (prev == kFaStatusEscapeAir);
                s32 throwF = in.frame - st.dodgeStartFrame + 1;
                if (throwF < 1)
                {
                    throwF = 1;
                }
                if (aerial)
                {
                    st.agtCount++;
                }
                else
                {
                    st.groundedGlideTossCount++;
                }
                st.agtCompletedThisFrame = 1;
                st.lastAgt.isAerial = aerial ? 1 : 0;
                st.lastAgt.throwFrame = faClampU8(throwF);
                st.lastAgt.agtChainCount = aerial ? curLa81 : 0;
                st.lastAgt.vxCarry = in.vx;
                st.lastAgt.vyCarry = in.vy;
                st.lastAgt.frame = in.frame;
                st.dodgeStartFrame = -1;
                st.dodgeStatus = -1;
            }
            else if (st.dodgeStartFrame >= 0 && s != kFaStatusEscapeF && s != kFaStatusEscapeB
                     && s != kFaStatusEscapeAir)
            {
                st.dodgeStartFrame = -1;
                st.dodgeStatus = -1;
            }
        }

        // Latch RA-Bit[6] shorthop flag during Tread_Jump (0x6D).
        if (st.inTreadJump && in.status == kFaStatusTreadJump && (in.statusFlags0 & kFaShortHopFlagBit) != 0)
        {
            st.lastFootstool.shortHop = 1;
        }

        // Pair victim on Tread_Jump entry frame or the immediately following frame.
        if (st.victimPairPending)
        {
            if (!faPairVictim(st, in) && (in.frame - st.footstoolStartFrame) >= 1)
            {
                st.victimPairPending = 0;
            }
        }

        // Taunt-spam lockout failure detection (LA-Basic[82] < 20 while airborne near opponent).
        const bool freshTaunt = ((in.buttons & kFaBtnTauntMask) != 0 && (st.prevButtons & kFaBtnTauntMask) == 0)
                                || ((in.triggerButtons & kFaBtnTauntMask) != 0);
        if (!enteredTreadJump && freshTaunt && in.airborne && in.hitstun == 0
            && in.laBasic82 >= 0 && in.laBasic82 < kFaTauntLockoutThreshold && faNearAnyOpponent(in))
        {
            st.failLockoutCount++;
            st.footstoolCompletedThisFrame = 1;
            st.watchingVictim = 0;
            st.victimPairPending = 0;
            st.lastFootstool.outcome = kFootstoolFailTauntLockout;
            st.lastFootstool.shortHop = 0;
            st.lastFootstool.victimTeched = 0;
            st.lastFootstool.victimMissedTech = 0;
            st.lastFootstool.footstoolLockHit = 0;
            st.lastFootstool.victimSlot = -1;
            st.lastFootstool.tauntLockoutFrames = static_cast<s16>(in.laBasic82);
            st.lastFootstool.frame = in.frame;
        }

        // Watch airborne footstool victim for tech (0x71 -> 0x60/0x61), missed tech (0x71 -> 0x4A),
        // and Footstool Lock hit during Down (0x4A..0x4E).
        if (st.watchingVictim)
        {
            if (in.frame - st.footstoolStartFrame > kFaFollowupTimeout)
            {
                st.watchingVictim = 0;
            }
            else
            {
                const FaOpponentView* vic = NULL;
                const int oppN = in.opponentCount < kFaMaxOpponents ? in.opponentCount : kFaMaxOpponents;
                for (int i = 0; i < oppN; i++)
                {
                    if (in.opponents[i].slot == st.watchVictimSlot)
                    {
                        vic = &in.opponents[i];
                        break;
                    }
                }
                if (vic == NULL || vic->outOfPlay)
                {
                    st.watchingVictim = 0;
                }
                else
                {
                    if (!st.victimWasDown)
                    {
                        if (faSawTransition(*vic, kFaStatusTreadFall, kFaStatusPassive)
                            || faSawTransition(*vic, kFaStatusTreadFall, kFaStatusPassiveFb)
                            || faPassiveStatus(vic->status))
                        {
                            st.lastFootstool.victimTeched = 1;
                            st.watchingVictim = 0;
                        }
                        else if (faSawTransition(*vic, kFaStatusTreadFall, kFaStatusDown) || faDownStatus(vic->status))
                        {
                            st.lastFootstool.victimMissedTech = 1;
                            st.victimWasDown = 1;
                        }
                        else if (vic->status != kFaStatusTreadDamageAir && vic->status != kFaStatusTreadFall)
                        {
                            // Victim was interrupted mid-air or recovered without entering Passive or Down.
                            st.watchingVictim = 0;
                        }
                    }

                    if (st.watchingVictim && st.victimWasDown)
                    {
                        bool hitBySelf = (vic->hitReceived && !vic->shieldReceived
                                          && (vic->lastReceivedOther == in.selfSlot || in.attackActive
                                              || (in.hitDealt && in.hitDealtTarget == vic->slot)))
                                         || (faSawStatus(*vic, kFaStatusDownDamage)
                                             && (vic->lastReceivedOther == in.selfSlot || in.hitDealt));
                        if (hitBySelf)
                        {
                            st.lastFootstool.footstoolLockHit = 1;
                            st.lockCount++;
                            st.watchingVictim = 0;
                        }
                        else if (!faDownStatus(vic->status) && vic->status != kFaStatusDownDamage)
                        {
                            // Victim stood up, rolled, or got up without being locked.
                            st.watchingVictim = 0;
                        }
                    }
                }
            }
        }

        st.prevButtons = in.buttons;
    }
}
}
