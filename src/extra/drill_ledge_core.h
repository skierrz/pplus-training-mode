#pragma once

#include <math.h>
#include <string.h>
#include <types.h>

// Pure host-testable Project+ 3.2.1 Ledgedash detection, grading, and telemetry
// core. Zero Brawl headers so all logic compiles identically on PC (`make
// check-host`) and PowerPC (MWCC 4.2).
//
// P+ 3.2.1 ground-truth rules verified in source and memory dumps:
//  - 5-ledge-grab intangibility limit: LA-Basic[79] (0x1000004F); grabs 1..5
//    grant IC-Basic[23051] = 31 frames of m_xluFrameGlobal; grabs 6+ force 0
//    frames (Source/ProjectM/Ledge.asm:495-513, Constants.asm:111-112).
//    Resets only on ground, Dead (0xBD), or Damage_Air (0x44); Damage_Fly
//    (0x45) does NOT reset LA-Basic[79] (RSBE01.TXT:1036-1064).
//  - Earliest Cliff_Wait (0x75) drop frame: kEarliestDrop = 3 for both
//    stick-away and stick-down (Frame 1 has RA-Bit[19] == 0, Frame 2 latches
//    RA-Bit[19] = 1 on neutral stick, Frame 3 drops to Fall 0x0E).
//  - Tourney Winner: Cliff_Jump1 (0x79) / Cliff_Jump2 (0x7A); slow >= 100%
//    (IC-Basic[3193] = 100.0, subactions 0xDF/0xE0 vs quick 0xDA/0xDB).
//    0x7A -> Attack_Air (0x33) or SpecialN..Lw (0x112..0x115) strips
//    m_xluFrameGlobal to 0 (Source/ProjectM/Ledge.asm:516-530).
//  - Tether reel-in: Air_Lasso statuses 0x7F..0x83 (ft/fighter.h:147-151,
//    0x7E is Miss_Foot; Source/ProjectM/Ledge.asm:423-427, 483-484).
//  - Multi-jumpers use Fly (0x0D) alongside Jump_Aerial (0x0C).
//  - Wavedash landing lag in Landing_Fall_Special (0x19) is fixed at 10 frames
//    (LA-Float[0] = 10.0, Source/Project+/AirDodge.asm:289-302).
namespace tm {
namespace extra {
    enum {
        kLedgeCoreMaxCells = 128,
        kLedgeCoreMaxEvents = 8,
        kLedgeCoreNoFrame = -1,
        kLedgeCoreNoGalint = -32768,
        kLedgeCoreGrabLimit = 5,
        kLedgeCoreXluMax = 31,
        kLedgeCoreEarliestDrop = 3,
        kLedgeCoreEarliestOption = 1,
        kLedgeCoreGiveUpFrames = 120
    };

    // Common Brawl/P+ fighter statuses (ft/fighter.h) used by the pure core.
    enum {
        kLedgeStJump = 0x0B,
        kLedgeStJumpAerial = 0x0C,
        kLedgeStFly = 0x0D,
        kLedgeStFall = 0x0E,
        kLedgeStFallAerial = 0x0F,
        kLedgeStLandingFirst = 0x16,
        kLedgeStLandingFallSpecial = 0x19,
        kLedgeStEscapeAir = 0x21,
        kLedgeStAttackFirst = 0x24,
        kLedgeStAttackAir = 0x33,
        kLedgeStCatchFirst = 0x34,
        kLedgeStCatchDash = 0x36,
        kLedgeStCliffCatchMove = 0x73,
        kLedgeStCliffCatch = 0x74,
        kLedgeStCliffWait = 0x75,
        kLedgeStCliffAttack = 0x76,
        kLedgeStCliffClimb = 0x77,
        kLedgeStCliffEscape = 0x78,
        kLedgeStCliffJump1 = 0x79,
        kLedgeStCliffJump2 = 0x7A,
        kLedgeStAirLassoFirst = 0x7F, // Air_Lasso (0x7E is Miss_Foot)
        kLedgeStAirLassoRewind = 0x82,
        kLedgeStAirLassoLast = 0x83,  // Air_Lasso_Failure
        kLedgeStDead = 0xBD,
        kLedgeStRebirth = 0xBE,
        kLedgeStStandby = 0x10B,
        kLedgeStSpecialFirst = 0x112,
        kLedgeStSpecialLast = 0x115,
        kLedgeStFinal = 0x116,
        // Cliff_Jump slow subaction IDs (>= 100% damage).
        kLedgeMotCliffJumpSlow1 = 0xDF,
        kLedgeMotCliffJumpSlow2 = 0xE0
    };

    enum LedgeCoreCell {
        kLedgeCellLedge = 0,
        kLedgeCellFall = 1,
        kLedgeCellJump = 2,
        kLedgeCellAirdodge = 3,
        kLedgeCellLanding = 4,
        kLedgeCellAttack = 5,
        kLedgeCellOther = 6,
        kLedgeCellGalint = 7
    };

    enum LedgeCorePhase {
        kLedgePhaseOff = 0,
        kLedgePhaseOnLedge = 1,
        kLedgePhaseAirborne = 2
    };

    enum LedgeOutcome {
        kLedgeOutcomeNone = 0,
        kLedgeOutcomeLedgedash = 1,
        kLedgeOutcomeNoInvulnLimit = 2,
        kLedgeOutcomeTourneyWinner = 3,
        kLedgeOutcomeOtherGetup = 4,
        kLedgeOutcomeSd = 5,
        kLedgeOutcomeInterrupted = 6,
        kLedgeOutcomeLandedNoDodge = 7
    };

    enum LedgeFailFlag {
        kLedgeFailNone = 0,
        kLedgeFailNoInvulnLimit = 1 << 0,     // LA-Basic[79] > 5 forced 0f xlu
        kLedgeFailLateDrop = 1 << 1,          // dropFrame > 1
        kLedgeFailLateStickNeutral = 1 << 2,  // RA-Bit[19] latched after frame 2
        kLedgeFailNoGalint = 1 << 3,          // galint <= 0 on actionable frame
        kLedgeFailTourneyWinnerSlow = 1 << 4, // Tourney Winner at >= 100%
        kLedgeFailLedgeJumpXluStrip = 1 << 5, // 0x7A -> aerial/special stripped xlu
        kLedgeFailSd = 1 << 6                 // Dead (0xBD)
    };

    struct LedgeStatusEv {
        s16 status;
        s16 prev;
        s16 motionKind;
        u8 airborne;
    };

    struct LedgeFrameInput {
        s32 frame;
        s16 status;
        s16 prevStatus;
        s16 motionKind;
        s16 hitstun;
        s32 xluFrames;     // m_xluFrameGlobal
        u8 ledgeGrabCount; // LA-Basic[79]
        u8 airborne;
        u8 actionable;
        u8 neutralLatch;   // RA-Bit[19] (0x22000013)
        float damage;
        float stickX;
        float stickY;
        u8 eventCount;
        LedgeStatusEv events[kLedgeCoreMaxEvents];
    };

    struct LedgeResult {
        s32 doneFrame;
        u8 outcome;              // LedgeOutcome
        u8 failFlags;            // LedgeFailFlag bitmask
        u8 grabCount;            // LA-Basic[79]
        u8 noXlu;                // 1 if hang had 0 xlu (grab > 5)
        u8 fromTether;           // 1 if entered Cliff_Wait from Air_Lasso (0x7F..0x83)
        u8 usedAirdodge;         // 1 if Escape_Air (0x21) occurred
        u8 usedFly;              // 1 if multi-jumper Fly (0x0D) occurred
        u8 tourneyWinnerSlow;    // 1 if >= 100% slow Tourney Winner
        u8 ledgeJumpXluStripped; // 1 if 0x7A -> aerial/special stripped xlu
        u8 cellCount;
        s16 galint;
        s16 dropFrame;           // 1 = optimal (leaveWaitFrame - kEarliestDrop + 1)
        s16 cliffWaitFrames;     // frames ended in Cliff_Wait (0x75)
        s16 neutralLatchFrames;  // 1-based Cliff_Wait frame RA-Bit[19] first latched (0 if never)
        s16 fallFrames;
        s16 jumpFrames;
        s16 airdodgeAirFrames;
        s16 landingLagFrames;
        float airdodgeAngle;
        u8 cells[kLedgeCoreMaxCells];
    };

    struct LedgeStats {
        u16 attempts;
        u16 successes;
        u16 tourneyWinners;
        u16 sds;
        u16 noInvulnGrabs;
        s16 bestGalint;
        u8 hasBestGalint;
        u16 streak;
        u16 bestStreak;
    };

    struct LedgeCoreState {
        // Active transient state.
        u8 phase;                // LedgeCorePhase
        u8 ledgeAir;
        u8 hangXlu;
        u8 noInvulnLimit;
        u8 countedNoInvulnGrab;
        u8 fromTether;
        u8 isTourneyWinner;
        u8 tourneyWinnerSlow;
        u8 ledgeJumpXluStripped;
        u8 isOtherGetup;
        u8 usedAirdodge;
        u8 usedFly;
        u8 failFlags;
        u8 grabCount;
        u8 prevFrameWasTether;
        s32 leftFrame;
        s32 lastIntangible;
        s16 waitFrames;
        s16 dropFrame;
        s16 neutralLatchFrames;
        s16 fallFrames;
        s16 jumpFrames;
        s16 airdodgeAirFrames;
        s16 landingLagFrames;
        float airdodgeAngle;
        u16 cellCount;
        u8 cells[kLedgeCoreMaxCells];

        // Completed result and cumulative stats.
        u32 seq;
        LedgeResult last;
        LedgeStats stats;
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline bool isLedgeTetherStatus(int s)
    {
        return s >= kLedgeStAirLassoFirst && s <= kLedgeStAirLassoLast;
    }

    inline bool isLedgeHangStatus(int s)
    {
        return s == kLedgeStCliffCatchMove || s == kLedgeStCliffCatch || s == kLedgeStCliffWait;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline bool isLedgeOutOfPlay(int s)
    {
        return s == kLedgeStDead || s == kLedgeStRebirth || s == kLedgeStStandby;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline float ledgeStickAngleDeg(float x, float y)
    {
        float ax = x < 0.0f ? -x : x;
        float ay = y < 0.0f ? -y : y;
        if (ax < 0.0001f && ay < 0.0001f)
        {
            return 0.0f;
        }
        return static_cast<float>(atan2(ay, ax) * 57.29577951308232);
    }

    inline u8 ledgeCoreCellOf(int s)
    {
        if (s == kLedgeStCliffWait || s == kLedgeStCliffCatch || s == kLedgeStCliffCatchMove)
        {
            return kLedgeCellLedge;
        }
        if (s == kLedgeStFall || s == kLedgeStFallAerial)
        {
            return kLedgeCellFall;
        }
        if (s == kLedgeStJumpAerial || s == kLedgeStFly || s == kLedgeStJump || s == kLedgeStCliffJump1
            || s == kLedgeStCliffJump2)
        {
            return kLedgeCellJump;
        }
        if (s == kLedgeStEscapeAir)
        {
            return kLedgeCellAirdodge;
        }
        if (s >= kLedgeStLandingFirst && s <= kLedgeStLandingFallSpecial)
        {
            return kLedgeCellLanding;
        }
        if ((s >= kLedgeStAttackFirst && s <= kLedgeStAttackAir)
            || (s >= kLedgeStSpecialFirst && s < kLedgeStFinal)
            || (s >= kLedgeStCatchFirst && s <= kLedgeStCatchDash))
        {
            return kLedgeCellAttack;
        }
        return kLedgeCellOther;
    }

    inline void ledgeCoreResetTransient(LedgeCoreState& st)
    {
        memset(&st, 0, reinterpret_cast<char*>(&st.seq) - reinterpret_cast<char*>(&st));
        st.hangXlu = 1;
        st.leftFrame = kLedgeCoreNoFrame;
        st.lastIntangible = kLedgeCoreNoFrame;
    }

    inline void ledgeCoreResetAll(LedgeCoreState& st)
    {
        memset(&st, 0, sizeof(st));
        ledgeCoreResetTransient(st);
        st.last.doneFrame = kLedgeCoreNoFrame;
        st.stats.bestGalint = kLedgeCoreNoGalint;
    }

    inline void ledgeCorePushCell(LedgeCoreState& st, u8 cell)
    {
        if (st.cellCount >= kLedgeCoreMaxCells)
        {
            for (int k = 1; k < kLedgeCoreMaxCells; k++)
            {
                st.cells[k - 1] = st.cells[k];
            }
            st.cellCount = kLedgeCoreMaxCells - 1;
        }
        st.cells[st.cellCount++] = cell;
    }

    inline u8 ledgeCoreFrameCell(const LedgeFrameInput& in)
    {
        u8 cell = ledgeCoreCellOf(in.status);
        int n = in.eventCount < kLedgeCoreMaxEvents ? static_cast<int>(in.eventCount) : kLedgeCoreMaxEvents;
        for (int k = 0; k < n; k++)
        {
            u8 c = ledgeCoreCellOf(in.events[k].status);
            if ((c == kLedgeCellJump || c == kLedgeCellAirdodge) && cell != kLedgeCellJump
                && cell != kLedgeCellAirdodge)
            {
                cell = c;
            }
        }
        return cell;
    }

    inline void ledgeCoreFinalize(LedgeCoreState& st, s32 frame, u8 outcome, s16 galint)
    {
        LedgeResult& r = st.last;
        memset(&r, 0, sizeof(r));
        r.doneFrame = frame;
        r.outcome = outcome;
        r.failFlags = st.failFlags;
        r.grabCount = st.grabCount;
        r.noXlu = st.hangXlu ? 0 : 1;
        r.fromTether = st.fromTether;
        r.usedAirdodge = st.usedAirdodge;
        r.usedFly = st.usedFly;
        r.tourneyWinnerSlow = st.tourneyWinnerSlow;
        r.ledgeJumpXluStripped = st.ledgeJumpXluStripped;
        r.galint = galint;
        r.dropFrame = st.dropFrame;
        r.cliffWaitFrames = st.waitFrames;
        r.neutralLatchFrames = st.neutralLatchFrames;
        r.fallFrames = st.fallFrames;
        r.jumpFrames = st.jumpFrames;
        r.airdodgeAirFrames = st.airdodgeAirFrames;
        r.landingLagFrames = st.landingLagFrames;
        r.airdodgeAngle = st.airdodgeAngle;
        r.cellCount = static_cast<u8>(st.cellCount <= kLedgeCoreMaxCells ? st.cellCount : kLedgeCoreMaxCells);
        if (r.cellCount > 0)
        {
            memcpy(r.cells, st.cells, r.cellCount);
        }

        if (outcome != kLedgeOutcomeInterrupted)
        {
            st.stats.attempts++;
        }
        if (outcome == kLedgeOutcomeLedgedash && galint > 0 && st.hangXlu)
        {
            st.stats.successes++;
            st.stats.streak++;
            if (st.stats.streak > st.stats.bestStreak)
            {
                st.stats.bestStreak = st.stats.streak;
            }
            if (!st.stats.hasBestGalint || galint > st.stats.bestGalint)
            {
                st.stats.bestGalint = galint;
                st.stats.hasBestGalint = 1;
            }
        }
        else if (outcome == kLedgeOutcomeLedgedash)
        {
            st.stats.streak = 0;
            if (st.hangXlu && (!st.stats.hasBestGalint || galint > st.stats.bestGalint))
            {
                st.stats.bestGalint = galint;
                st.stats.hasBestGalint = 1;
            }
        }
        else if (outcome == kLedgeOutcomeTourneyWinner)
        {
            st.stats.tourneyWinners++;
            st.stats.streak = 0;
        }
        else if (outcome == kLedgeOutcomeSd)
        {
            st.stats.sds++;
            st.stats.streak = 0;
        }
        else if (outcome == kLedgeOutcomeNoInvulnLimit || outcome == kLedgeOutcomeOtherGetup
                 || outcome == kLedgeOutcomeLandedNoDodge)
        {
            st.stats.streak = 0;
        }

        st.seq++;
        st.phase = kLedgePhaseOff;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Steps one finished game frame for one tracked fighter. Returns true if a
    // ledge attempt finalized on this frame (result available in st.last).
    inline bool ledgeCoreStep(LedgeCoreState& st, const LedgeFrameInput& in)
    {
        const int s = in.status;
        const int n = in.eventCount < kLedgeCoreMaxEvents ? static_cast<int>(in.eventCount) : kLedgeCoreMaxEvents;

        if (in.xluFrames > 0)
        {
            st.lastIntangible = in.frame;
        }

        bool grabbed = (s == kLedgeStCliffCatchMove || s == kLedgeStCliffCatch);
        bool enteredFromTether = (st.prevFrameWasTether != 0) && isLedgeHangStatus(s);
        for (int k = 0; k < n; k++)
        {
            int es = in.events[k].status;
            int ep = in.events[k].prev;
            if (isLedgeHangStatus(es))
            {
                grabbed = true;
                if (isLedgeTetherStatus(ep))
                {
                    enteredFromTether = true;
                }
            }
        }
        if (grabbed && isLedgeTetherStatus(in.prevStatus))
        {
            enteredFromTether = true;
        }

        if (grabbed && st.phase != kLedgePhaseOnLedge)
        {
            // Entering a fresh ledge hang (including ledge-hop regrabs).
            u8 savedPrevTether = st.prevFrameWasTether;
            s32 savedLastIntang = st.lastIntangible;
            ledgeCoreResetTransient(st);
            st.prevFrameWasTether = savedPrevTether;
            st.lastIntangible = savedLastIntang;
            st.phase = kLedgePhaseOnLedge;
            st.hangXlu = 1;
            st.fromTether = enteredFromTether ? 1 : 0;
        }

        // Track whether this frame ended in an Air_Lasso status for next frame.
        bool endedInTether = isLedgeTetherStatus(s);
        for (int k = 0; k < n && !endedInTether; k++)
        {
            if (isLedgeTetherStatus(in.events[k].status) && !isLedgeHangStatus(s))
            {
                endedInTether = true;
            }
        }
        st.prevFrameWasTether = endedInTether ? 1 : 0;

        if (st.phase == kLedgePhaseOff)
        {
            return false;
        }

        // Check SD (Dead 0xBD) before generic out-of-play abort.
        bool died = (s == kLedgeStDead);
        for (int k = 0; k < n && !died; k++)
        {
            if (in.events[k].status == kLedgeStDead)
            {
                died = true;
            }
        }
        if (died)
        {
            if (st.phase == kLedgePhaseAirborne)
            {
                st.failFlags |= kLedgeFailSd;
                ledgeCoreFinalize(st, in.frame, kLedgeOutcomeSd, 0);
                return true;
            }
            st.phase = kLedgePhaseOff;
            return false;
        }

        // Hitstun (including Damage_Fly 0x45), non-Dead out-of-play, or timeout
        // interrupts the active attempt without touching LA-Basic[79].
        if (isLedgeOutOfPlay(s) || in.hitstun > 0
            || (st.phase == kLedgePhaseAirborne && in.frame - st.leftFrame > kLedgeCoreGiveUpFrames))
        {
            if (st.phase == kLedgePhaseAirborne && st.isTourneyWinner)
            {
                ledgeCoreFinalize(st, in.frame, kLedgeOutcomeTourneyWinner, 0);
                return true;
            }
            if (st.phase == kLedgePhaseAirborne)
            {
                ledgeCoreFinalize(st, in.frame, kLedgeOutcomeInterrupted, 0);
                return true;
            }
            st.phase = kLedgePhaseOff;
            return false;
        }

        if (st.phase == kLedgePhaseOnLedge)
        {
            if (s == kLedgeStCliffWait)
            {
                if (in.ledgeGrabCount > 0)
                {
                    st.grabCount = in.ledgeGrabCount;
                }
                bool firstWait = (st.waitFrames == 0);
                st.hangXlu = ((st.waitFrames > 0 && st.hangXlu != 0) || in.xluFrames > 0) ? 1 : 0;
                st.waitFrames++;
                if (st.neutralLatchFrames == 0 && in.neutralLatch != 0)
                {
                    st.neutralLatchFrames = st.waitFrames;
                    if (st.neutralLatchFrames > 2)
                    {
                        st.failFlags |= kLedgeFailLateStickNeutral;
                    }
                }
                ledgeCorePushCell(st, kLedgeCellLedge);
                if (firstWait && !st.hangXlu && st.grabCount > kLedgeCoreGrabLimit)
                {
                    st.noInvulnLimit = 1;
                    st.failFlags |= kLedgeFailNoInvulnLimit;
                    if (!st.countedNoInvulnGrab)
                    {
                        st.countedNoInvulnGrab = 1;
                        st.stats.noInvulnGrabs++;
                    }
                }
                return false;
            }
            if (s == kLedgeStCliffCatch || s == kLedgeStCliffCatchMove)
            {
                return false;
            }

            // Transitioning off Cliff_Wait on this frame.
            if (st.grabCount == 0 && in.ledgeGrabCount > 0)
            {
                st.grabCount = in.ledgeGrabCount;
            }
            if (st.waitFrames == 0 && st.grabCount > kLedgeCoreGrabLimit && in.xluFrames == 0)
            {
                st.hangXlu = 0;
                st.noInvulnLimit = 1;
                st.failFlags |= kLedgeFailNoInvulnLimit;
                if (!st.countedNoInvulnGrab)
                {
                    st.countedNoInvulnGrab = 1;
                    st.stats.noInvulnGrabs++;
                }
            }

            // Determine the first non-ledge status entered when leaving Cliff_Wait.
            int firstLeaveStatus = s;
            int firstLeaveMotion = in.motionKind;
            for (int k = 0; k < n; k++)
            {
                int es = in.events[k].status;
                if (!isLedgeHangStatus(es))
                {
                    firstLeaveStatus = es;
                    firstLeaveMotion = in.events[k].motionKind;
                    break;
                }
            }

            st.phase = kLedgePhaseAirborne;
            st.leftFrame = in.frame;

            int leaveWaitFrame = st.waitFrames + 1;
            if (firstLeaveStatus == kLedgeStCliffJump1 || firstLeaveStatus == kLedgeStCliffJump2)
            {
                st.isTourneyWinner = 1;
                st.dropFrame = static_cast<s16>(leaveWaitFrame - kLedgeCoreEarliestOption + 1);
                if (in.damage >= 100.0f || firstLeaveMotion == kLedgeMotCliffJumpSlow1
                    || firstLeaveMotion == kLedgeMotCliffJumpSlow2 || in.motionKind == kLedgeMotCliffJumpSlow1
                    || in.motionKind == kLedgeMotCliffJumpSlow2)
                {
                    st.tourneyWinnerSlow = 1;
                    st.failFlags |= kLedgeFailTourneyWinnerSlow;
                }
            }
            else if (firstLeaveStatus >= kLedgeStCliffAttack && firstLeaveStatus <= kLedgeStCliffEscape)
            {
                st.isOtherGetup = 1;
                st.dropFrame = static_cast<s16>(leaveWaitFrame - kLedgeCoreEarliestOption + 1);
            }
            else
            {
                int df = leaveWaitFrame - kLedgeCoreEarliestDrop + 1;
                st.dropFrame = static_cast<s16>(df < 1 ? 1 : df);
                if (st.dropFrame > 1)
                {
                    st.failFlags |= kLedgeFailLateDrop;
                }
            }
        }

        // Phase: kLedgePhaseAirborne (left the ledge, moving toward onstage actionable).
        if (in.airborne)
        {
            st.ledgeAir = 1;
        }
        if (s == kLedgeStFly)
        {
            st.usedFly = 1;
        }
        if (st.isTourneyWinner
            && (in.motionKind == kLedgeMotCliffJumpSlow1 || in.motionKind == kLedgeMotCliffJumpSlow2))
        {
            st.tourneyWinnerSlow = 1;
            st.failFlags |= kLedgeFailTourneyWinnerSlow;
        }

        bool enteredLanding = false;
        for (int k = 0; k < n; k++)
        {
            int es = in.events[k].status;
            int ep = in.events[k].prev;
            if (es == kLedgeStFly)
            {
                st.usedFly = 1;
            }
            if (es == kLedgeStEscapeAir)
            {
                st.usedAirdodge = 1;
                st.airdodgeAngle = ledgeStickAngleDeg(in.stickX, in.stickY);
            }
            if (es >= kLedgeStLandingFirst && es <= kLedgeStLandingFallSpecial)
            {
                enteredLanding = true;
            }
            // P+ Ledge.asm:516-530: Cliff_Jump2 (0x7A) -> Attack_Air (0x33) or
            // SpecialN..Lw (0x112..0x115) strips m_xluFrameGlobal to 0.
            if (ep == kLedgeStCliffJump2
                && (es == kLedgeStAttackAir || (es >= kLedgeStSpecialFirst && es <= kLedgeStSpecialLast)))
            {
                st.ledgeJumpXluStripped = 1;
                st.failFlags |= kLedgeFailLedgeJumpXluStrip;
            }
        }

        // Grounded actionable completion check. Frame 1 of a newly entered
        // landing lag status (0x16..0x19) is never grounded-actionable.
        bool inNewLandingLag = enteredLanding && s >= kLedgeStLandingFirst && s <= kLedgeStLandingFallSpecial;
        bool otherGetupDone = st.isOtherGetup != 0 && !in.airborne && in.actionable != 0 && !inNewLandingLag
                              && !(s >= kLedgeStCliffAttack && s <= kLedgeStCliffEscape);
        bool airborneDone = !in.airborne && in.actionable != 0 && st.ledgeAir != 0 && !inNewLandingLag;

        if (airborneDone || otherGetupDone)
        {
            // Exact Section 1.4 GALINT formula:
            //   !hangXlu ? 0 : xluFrames > 0 ? xluFrames : -(f - lastIntangible)
            int galint = !st.hangXlu ? 0
                         : (in.xluFrames > 0) ? in.xluFrames
                         : (st.lastIntangible != kLedgeCoreNoFrame) ? -(in.frame - st.lastIntangible)
                                                                    : 0;
            u8 outcome = kLedgeOutcomeLedgedash;
            if (st.isTourneyWinner)
            {
                outcome = kLedgeOutcomeTourneyWinner;
            }
            else if (st.isOtherGetup)
            {
                outcome = kLedgeOutcomeOtherGetup;
            }
            else if (st.noInvulnLimit || !st.hangXlu)
            {
                outcome = kLedgeOutcomeNoInvulnLimit;
                st.failFlags |= kLedgeFailNoInvulnLimit;
            }
            else if (!st.usedAirdodge)
            {
                outcome = kLedgeOutcomeLandedNoDodge;
                if (galint <= 0)
                {
                    st.failFlags |= kLedgeFailNoGalint;
                }
            }
            else
            {
                outcome = kLedgeOutcomeLedgedash;
                if (galint <= 0)
                {
                    st.failFlags |= kLedgeFailNoGalint;
                }
            }
            ledgeCoreFinalize(st, in.frame, outcome, static_cast<s16>(galint));
            return true;
        }

        // Accumulate per-frame breakdown counts and ProjectPunch-style cell.
        if (s == kLedgeStFall || s == kLedgeStFallAerial)
        {
            st.fallFrames++;
        }
        else if (s == kLedgeStJump || s == kLedgeStJumpAerial || s == kLedgeStFly || s == kLedgeStCliffJump1
                 || s == kLedgeStCliffJump2)
        {
            st.jumpFrames++;
        }
        else if (s == kLedgeStEscapeAir)
        {
            st.airdodgeAirFrames++;
        }
        else if (s >= kLedgeStLandingFirst && s <= kLedgeStLandingFallSpecial)
        {
            st.landingLagFrames++;
        }

        ledgeCorePushCell(st, ledgeCoreFrameCell(in));
        return false;
    }
}
}
