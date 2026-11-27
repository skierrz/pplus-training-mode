// Host unit tests for Stage 4A Stream 1 core drill backends:
//  - src/extra/drill_ledge_core.h
//  - src/extra/drill_wavedash_core.h
//  - src/extra/drill_lcancel_core.h
//  - CoreDrillDebugSlot layout stability (src/extra/drill_core_engines.h)
#include <math.h>
#include <string.h>

#include "extra/drill_core_engines.h"
#include "host/test.h"

using namespace tm;
using namespace tm::extra;

namespace {
    LedgeFrameInput makeLedgeIn(s32 frame, s16 status, s16 prev)
    {
        LedgeFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.status = status;
        in.prevStatus = prev;
        in.motionKind = -1;
        return in;
    }

    void addLedgeEv(LedgeFrameInput& in, s16 prev, s16 status, s16 mot = -1, u8 air = 0)
    {
        LedgeStatusEv& e = in.events[in.eventCount++];
        e.prev = prev;
        e.status = status;
        e.motionKind = mot;
        e.airborne = air;
    }

    WdFrameInput makeWdIn(s32 frame, s16 status, s16 prev, float x, float y)
    {
        WdFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.status = status;
        in.prevStatus = prev;
        in.x = x;
        in.y = y;
        return in;
    }

    void addWdEv(WdFrameInput& in, s16 prev, s16 status, u32 flags = 0, u8 air = 0)
    {
        WdStatusEv& e = in.events[in.eventCount++];
        e.prev = prev;
        e.status = status;
        e.flags = flags;
        e.airborne = air;
    }

    LcFrameInput makeLcIn(s32 frame, s16 status, s16 prev)
    {
        LcFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.status = status;
        in.prevStatus = prev;
        in.motionKind = -1;
        return in;
    }

    void addLcEv(LcFrameInput& in, s16 prev, s16 status, s16 lc, s16 mot = 0x62)
    {
        LcStatusEv& e = in.events[in.eventCount++];
        e.prev = prev;
        e.status = status;
        e.lCancel = lc;
        e.motionKind = mot;
    }
}

// ---- Debug block layout sanity ---------------------------------------------

TEST(stage4a_core, debug_block_layout_and_alignment)
{
    CHECK_EQ(284u, static_cast<unsigned>(sizeof(CoreDrillDebugSlot)));
    CHECK_EQ(24u + 8u * 284u, static_cast<unsigned>(sizeof(CoreDrillDebugBlock)));
    CHECK_EQ(0x544D3443u, static_cast<unsigned>(kCoreDrillDebugMagic));
}

// ---- Engine 1: Ledgedash ---------------------------------------------------

TEST(stage4a_core, ledge_optimal_ledgedash_galint_breakdown_and_cells)
{
    LedgeCoreState st;
    ledgeCoreResetAll(st);

    // 6 frames of Cliff_Catch (0x74, xlu = 0) do not count as Cliff_Wait cells.
    for (s32 f = 1; f <= 6; f++)
    {
        LedgeFrameInput in = makeLedgeIn(f, kLedgeStCliffCatch, f == 1 ? kLedgeStFall : kLedgeStCliffCatch);
        if (f == 1)
        {
            addLedgeEv(in, kLedgeStFall, kLedgeStCliffCatch);
        }
        CHECK(!ledgeCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLedgePhaseOnLedge), static_cast<int>(st.phase));
    CHECK_EQ(0, static_cast<int>(st.waitFrames));

    // Cliff_Wait Frame 1 (f=7): xlu = 31, RA-Bit[19] = 0, grab #1.
    {
        LedgeFrameInput in = makeLedgeIn(7, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        in.neutralLatch = 0;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        CHECK(!ledgeCoreStep(st, in));
    }
    // Cliff_Wait Frame 2 (f=8): xlu = 30, RA-Bit[19] latches 1 on neutral stick.
    {
        LedgeFrameInput in = makeLedgeIn(8, kLedgeStCliffWait, kLedgeStCliffWait);
        in.xluFrames = 30;
        in.ledgeGrabCount = 1;
        in.neutralLatch = 1;
        CHECK(!ledgeCoreStep(st, in));
    }
    // Cliff_Wait Frame 3 (f=9): optimal drop -> Fall (0x0E), xlu = 29.
    {
        LedgeFrameInput in = makeLedgeIn(9, kLedgeStFall, kLedgeStCliffWait);
        in.xluFrames = 29;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        CHECK(!ledgeCoreStep(st, in));
    }
    // Double jump (f=10): Jump_Aerial (0x0C), xlu = 28.
    {
        LedgeFrameInput in = makeLedgeIn(10, kLedgeStJumpAerial, kLedgeStFall);
        in.xluFrames = 28;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStJumpAerial, -1, 1);
        CHECK(!ledgeCoreStep(st, in));
    }
    // Frame-perfect airdodge -> immediate land into 0x19 (f=11), xlu = 27.
    {
        LedgeFrameInput in = makeLedgeIn(11, kLedgeStLandingFallSpecial, kLedgeStJumpAerial);
        in.xluFrames = 27;
        in.airborne = 0;
        in.stickX = 0.96f;
        in.stickY = -0.28f;
        addLedgeEv(in, kLedgeStJumpAerial, kLedgeStEscapeAir, -1, 1);
        addLedgeEv(in, kLedgeStEscapeAir, kLedgeStLandingFallSpecial, -1, 0);
        CHECK(!ledgeCoreStep(st, in));
    }
    // Remaining 9 frames of 10f landing lag (f=12..20), xlu = 26..18.
    for (s32 f = 12; f <= 20; f++)
    {
        LedgeFrameInput in = makeLedgeIn(f, kLedgeStLandingFallSpecial, kLedgeStLandingFallSpecial);
        in.xluFrames = 38 - f;
        in.airborne = 0;
        CHECK(!ledgeCoreStep(st, in));
    }
    // First grounded actionable frame (f=21): xlu = 17!
    {
        LedgeFrameInput in = makeLedgeIn(21, 0x00, kLedgeStLandingFallSpecial);
        in.xluFrames = 17;
        in.airborne = 0;
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFallSpecial, 0x00, -1, 0);
        CHECK(ledgeCoreStep(st, in));
    }

    CHECK_EQ(1u, st.seq);
    CHECK_EQ(static_cast<int>(kLedgeOutcomeLedgedash), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kLedgeFailNone), static_cast<int>(st.last.failFlags));
    CHECK_EQ(17, static_cast<int>(st.last.galint));
    CHECK_EQ(1, static_cast<int>(st.last.dropFrame));
    CHECK_EQ(2, static_cast<int>(st.last.cliffWaitFrames));
    CHECK_EQ(2, static_cast<int>(st.last.neutralLatchFrames));
    CHECK_EQ(1, static_cast<int>(st.last.fallFrames));
    CHECK_EQ(1, static_cast<int>(st.last.jumpFrames));
    CHECK_EQ(0, static_cast<int>(st.last.airdodgeAirFrames));
    CHECK_EQ(10, static_cast<int>(st.last.landingLagFrames));
    CHECK_EQ(14, static_cast<int>(st.last.cellCount));
    CHECK_EQ(static_cast<int>(kLedgeCellLedge), static_cast<int>(st.last.cells[0]));
    CHECK_EQ(static_cast<int>(kLedgeCellLedge), static_cast<int>(st.last.cells[1]));
    CHECK_EQ(static_cast<int>(kLedgeCellFall), static_cast<int>(st.last.cells[2]));
    CHECK_EQ(static_cast<int>(kLedgeCellJump), static_cast<int>(st.last.cells[3]));
    CHECK_EQ(static_cast<int>(kLedgeCellAirdodge), static_cast<int>(st.last.cells[4]));
    CHECK_EQ(static_cast<int>(kLedgeCellLanding), static_cast<int>(st.last.cells[5]));
    CHECK_NEAR(16.2602f, st.last.airdodgeAngle, 0.05f);
    CHECK_EQ(1, static_cast<int>(st.last.grabCount));
    CHECK_EQ(0, static_cast<int>(st.last.fromTether));
    CHECK_EQ(0, static_cast<int>(st.last.usedFly));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.attempts));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.successes));
    CHECK_EQ(17, static_cast<int>(st.stats.bestGalint));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.streak));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.bestStreak));
}

TEST(stage4a_core, ledge_negative_galint_late_drop_and_late_neutral_latch)
{
    LedgeCoreState st;
    ledgeCoreResetAll(st);

    // Hang for 20 frames in Cliff_Wait (xlu = 31..12); RA-Bit[19] doesn't latch until frame 4.
    for (s32 f = 1; f <= 20; f++)
    {
        LedgeFrameInput in = makeLedgeIn(f, kLedgeStCliffWait, f == 1 ? kLedgeStCliffCatch : kLedgeStCliffWait);
        in.xluFrames = 32 - f;
        in.ledgeGrabCount = 2;
        in.neutralLatch = f >= 4 ? 1 : 0;
        if (f == 1)
        {
            addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        }
        ledgeCoreStep(st, in);
    }
    // Drop on frame 21 (dropFrame = 21 - 3 + 1 = 19), jump f=22, airdodge f=23 (1f air), land f=24..33, act f=34.
    // Last intangible frame is f=31 (xlu=1); f=32..34 have xlu=0 -> galint = -(34 - 31) = -3.
    {
        LedgeFrameInput in = makeLedgeIn(21, kLedgeStFall, kLedgeStCliffWait);
        in.xluFrames = 11;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(22, kLedgeStJumpAerial, kLedgeStFall);
        in.xluFrames = 10;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStJumpAerial, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(23, kLedgeStEscapeAir, kLedgeStJumpAerial);
        in.xluFrames = 9;
        in.airborne = 1;
        in.stickX = 0.8f;
        in.stickY = -0.6f;
        addLedgeEv(in, kLedgeStJumpAerial, kLedgeStEscapeAir, -1, 1);
        ledgeCoreStep(st, in);
    }
    for (s32 f = 24; f <= 33; f++)
    {
        LedgeFrameInput in = makeLedgeIn(f, kLedgeStLandingFallSpecial,
                                         f == 24 ? kLedgeStEscapeAir : kLedgeStLandingFallSpecial);
        in.xluFrames = f <= 31 ? (32 - f) : 0;
        if (f == 24)
        {
            addLedgeEv(in, kLedgeStEscapeAir, kLedgeStLandingFallSpecial, -1, 0);
        }
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(34, 0x00, kLedgeStLandingFallSpecial);
        in.xluFrames = 0;
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFallSpecial, 0x00, -1, 0);
        CHECK(ledgeCoreStep(st, in));
    }

    CHECK_EQ(static_cast<int>(kLedgeOutcomeLedgedash), static_cast<int>(st.last.outcome));
    CHECK_EQ(-3, static_cast<int>(st.last.galint));
    CHECK_EQ(19, static_cast<int>(st.last.dropFrame));
    CHECK_EQ(4, static_cast<int>(st.last.neutralLatchFrames));
    CHECK_EQ(1, static_cast<int>(st.last.airdodgeAirFrames));
    CHECK((st.last.failFlags & kLedgeFailLateDrop) != 0);
    CHECK((st.last.failFlags & kLedgeFailLateStickNeutral) != 0);
    CHECK((st.last.failFlags & kLedgeFailNoGalint) != 0);
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.successes));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.streak));
}

TEST(stage4a_core, ledge_five_grab_limit_and_damage_fly_non_reset)
{
    LedgeCoreState st;
    ledgeCoreResetAll(st);

    // Grab #5 still grants xlu (no limit hit), then gets hit into Damage_Fly (0x45)
    // which does NOT reset LA-Basic[79] in P+ 3.2.1 (RSBE01.TXT:1052).
    {
        LedgeFrameInput in = makeLedgeIn(10, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 5;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
        CHECK_EQ(0, static_cast<int>(st.noInvulnLimit));
        CHECK_EQ(0u, static_cast<unsigned>(st.stats.noInvulnGrabs));
    }
    {
        // Knocked into Damage_Fly (0x45) while on ledge -> aborts hang cleanly.
        LedgeFrameInput in = makeLedgeIn(11, 0x45, kLedgeStCliffWait);
        in.hitstun = 25;
        in.airborne = 1;
        in.ledgeGrabCount = 5;
        addLedgeEv(in, kLedgeStCliffWait, 0x45, -1, 1);
        ledgeCoreStep(st, in);
        CHECK_EQ(static_cast<int>(kLedgePhaseOff), static_cast<int>(st.phase));
    }
    // Next grab is #6 (LA-Basic[79] == 6, xlu == 0) -> immediate kLedgeFailNoInvulnLimit.
    {
        LedgeFrameInput in = makeLedgeIn(50, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 0;
        in.ledgeGrabCount = 6;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
        CHECK_EQ(1, static_cast<int>(st.noInvulnLimit));
        CHECK_EQ(1u, static_cast<unsigned>(st.stats.noInvulnGrabs));
        CHECK_EQ(6, static_cast<int>(st.grabCount));
    }
    // Complete ledgedash from grab #6 -> outcome = kLedgeOutcomeNoInvulnLimit, galint = 0.
    {
        LedgeFrameInput in = makeLedgeIn(51, kLedgeStCliffWait, kLedgeStCliffWait);
        in.xluFrames = 0;
        in.ledgeGrabCount = 6;
        in.neutralLatch = 1;
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(52, kLedgeStFall, kLedgeStCliffWait);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(53, kLedgeStJumpAerial, kLedgeStFall);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStJumpAerial, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(54, kLedgeStLandingFallSpecial, kLedgeStJumpAerial);
        addLedgeEv(in, kLedgeStJumpAerial, kLedgeStEscapeAir, -1, 1);
        addLedgeEv(in, kLedgeStEscapeAir, kLedgeStLandingFallSpecial, -1, 0);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(64, 0x00, kLedgeStLandingFallSpecial);
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFallSpecial, 0x00, -1, 0);
        CHECK(ledgeCoreStep(st, in));
    }

    CHECK_EQ(static_cast<int>(kLedgeOutcomeNoInvulnLimit), static_cast<int>(st.last.outcome));
    CHECK((st.last.failFlags & kLedgeFailNoInvulnLimit) != 0);
    CHECK_EQ(1, static_cast<int>(st.last.noXlu));
    CHECK_EQ(0, static_cast<int>(st.last.galint));
    CHECK_EQ(6, static_cast<int>(st.last.grabCount));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.noInvulnGrabs));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.successes));
}

TEST(stage4a_core, ledge_tourney_winner_quick_slow_and_xlu_strip)
{
    LedgeCoreState st;
    ledgeCoreResetAll(st);

    // Quick Tourney Winner (< 100%) with 0x7A -> Attack_Air (0x33) xlu strip.
    {
        LedgeFrameInput in = makeLedgeIn(1, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        in.damage = 45.0f;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(2, kLedgeStCliffJump1, kLedgeStCliffWait);
        in.xluFrames = 30;
        in.damage = 45.0f;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStCliffJump1, 0xDA, 0);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(12, kLedgeStCliffJump2, kLedgeStCliffJump1);
        in.xluFrames = 20;
        in.airborne = 1;
        in.damage = 45.0f;
        addLedgeEv(in, kLedgeStCliffJump1, kLedgeStCliffJump2, 0xDB, 1);
        ledgeCoreStep(st, in);
    }
    {
        // Aerial out of Cliff_Jump2 strips m_xluFrameGlobal to 0 (Ledge.asm:516-530).
        LedgeFrameInput in = makeLedgeIn(15, kLedgeStAttackAir, kLedgeStCliffJump2);
        in.xluFrames = 0;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffJump2, kLedgeStAttackAir, 0x62, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(30, 0x00, kLedgeStLandingFirst);
        in.xluFrames = 0;
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFirst, 0x00, 0, 0);
        CHECK(ledgeCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeTourneyWinner), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.tourneyWinnerSlow));
    CHECK_EQ(1, static_cast<int>(st.last.ledgeJumpXluStripped));
    CHECK((st.last.failFlags & kLedgeFailLedgeJumpXluStrip) != 0);
    CHECK((st.last.failFlags & kLedgeFailTourneyWinnerSlow) == 0);
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.tourneyWinners));

    // Slow Tourney Winner (>= 100%).
    {
        LedgeFrameInput in = makeLedgeIn(40, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        in.damage = 112.0f;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(41, kLedgeStCliffJump1, kLedgeStCliffWait);
        in.xluFrames = 30;
        in.damage = 112.0f;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStCliffJump1, kLedgeMotCliffJumpSlow1, 0);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(60, kLedgeStCliffJump2, kLedgeStCliffJump1);
        in.airborne = 1;
        in.damage = 112.0f;
        addLedgeEv(in, kLedgeStCliffJump1, kLedgeStCliffJump2, kLedgeMotCliffJumpSlow2, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(80, 0x00, kLedgeStLandingFirst);
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFirst, 0x00, 0, 0);
        CHECK(ledgeCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeTourneyWinner), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.tourneyWinnerSlow));
    CHECK((st.last.failFlags & kLedgeFailTourneyWinnerSlow) != 0);
    CHECK_EQ(2u, static_cast<unsigned>(st.stats.tourneyWinners));
}

TEST(stage4a_core, ledge_tether_entry_multijumper_fly_and_sd)
{
    LedgeCoreState st;
    ledgeCoreResetAll(st);

    // 1. Tether reel-in: Air_Lasso_Rewind (0x82) -> Cliff_Wait (0x75), then
    //    multi-jumper Fly (0x0D) ledgedash.
    {
        LedgeFrameInput in = makeLedgeIn(1, kLedgeStAirLassoRewind, 0x81);
        in.airborne = 1;
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(2, kLedgeStCliffWait, kLedgeStAirLassoRewind);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        addLedgeEv(in, kLedgeStAirLassoRewind, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
        CHECK_EQ(1, static_cast<int>(st.fromTether));
    }
    {
        LedgeFrameInput in = makeLedgeIn(3, kLedgeStCliffWait, kLedgeStCliffWait);
        in.xluFrames = 30;
        in.neutralLatch = 1;
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(4, kLedgeStFall, kLedgeStCliffWait);
        in.xluFrames = 29;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(5, kLedgeStFly, kLedgeStFall);
        in.xluFrames = 28;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStFly, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(6, kLedgeStLandingFallSpecial, kLedgeStFly);
        in.xluFrames = 27;
        in.stickX = 0.9f;
        in.stickY = -0.35f;
        addLedgeEv(in, kLedgeStFly, kLedgeStEscapeAir, -1, 1);
        addLedgeEv(in, kLedgeStEscapeAir, kLedgeStLandingFallSpecial, -1, 0);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(16, 0x00, kLedgeStLandingFallSpecial);
        in.xluFrames = 17;
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFallSpecial, 0x00, -1, 0);
        CHECK(ledgeCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeLedgedash), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.fromTether));
    CHECK_EQ(1, static_cast<int>(st.last.usedFly));
    CHECK_EQ(1, static_cast<int>(st.last.jumpFrames));
    CHECK_EQ(static_cast<int>(kLedgeCellJump), static_cast<int>(st.last.cells[3]));
    CHECK_EQ(17, static_cast<int>(st.last.galint));

    // Verify 0x7E (Miss_Foot) is NOT treated as Air_Lasso!
    CHECK(!isLedgeTetherStatus(0x7E));
    CHECK(isLedgeTetherStatus(0x7F));
    CHECK(isLedgeTetherStatus(0x82));
    CHECK(isLedgeTetherStatus(0x83));

    // 2. SD detection: drop off ledge and enter Dead (0xBD).
    {
        LedgeFrameInput in = makeLedgeIn(30, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(33, kLedgeStFall, kLedgeStCliffWait);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        ledgeCoreStep(st, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(60, kLedgeStDead, kLedgeStFall);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStDead, -1, 1);
        CHECK(ledgeCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeSd), static_cast<int>(st.last.outcome));
    CHECK((st.last.failFlags & kLedgeFailSd) != 0);
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.sds));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.streak));
}

// ---- Engine 2: Wavedash & Platform Waveland --------------------------------

TEST(stage4a_core, wavedash_pplus_stick_math_and_speed)
{
    // Shallowest downward stick outside 0.28 deadzone within radius 100: (96, -28).
    float sx = 0.0f, sy = 0.0f;
    wdNormalizeRawStick(96, -28, sx, sy);
    CHECK_NEAR(96.0f * 0.010325f, sx, 0.001f);
    CHECK_NEAR(-28.0f * 0.010325f, sy, 0.001f);
    CHECK_NEAR(16.2602f, wdStickAngleDeg(sx, sy), 0.05f);
    CHECK_NEAR(-16.2602f, wdSignedAngleDeg(sx, sy), 0.05f);
    // hypot(sx, sy) * 1.28 > 1.0 -> clamped to 1.0 * 3.1 = 3.1.
    CHECK_NEAR(3.1f, wdAirdodgeSpeed(sx, sy), 0.001f);

    // Deadzone: (96, -27) -> -27 * 0.010325 = -0.278775 < 0.28 -> Y snaps to 0!
    wdNormalizeRawStick(96, -27, sx, sy);
    CHECK_NEAR(0.0f, sy, 0.0001f);
    CHECK_NEAR(0.0f, wdStickAngleDeg(sx, sy), 0.001f);

    // Partial magnitude inside 1/1.28 radius: (0.5, 0.0) -> 0.5 * 1.28 * 3.1 = 1.984.
    CHECK_NEAR(0.5f * 1.28f * 3.1f, wdAirdodgeSpeed(0.5f, 0.0f), 0.001f);
    CHECK_NEAR(0.0f, wdAirdodgeSpeed(0.0f, 0.0f), 0.0001f);
}

TEST(stage4a_core, wavedash_frame1_grounded_and_platform_waveland_distinct)
{
    WdCoreState st;
    wdCoreResetAll(st);

    // 1. Frame-1 grounded wavedash on FD (0x0A -> 0x0B -> 0x21 -> 0x19) with slide distance.
    for (s32 f = 1; f <= 3; f++)
    {
        WdFrameInput in = makeWdIn(f, kWdStJumpSquat, f == 1 ? 0x00 : kWdStJumpSquat, 0.0f, 0.0f);
        in.buttons = (f <= 2) ? kWdCoreButtonJump : 0;
        if (f == 1)
        {
            addWdEv(in, 0x00, kWdStJumpSquat);
        }
        CHECK(!wdCoreStep(st, in));
    }
    // Frame 4: Jump -> Escape_Air -> Landing_Fall_Special all on frame 4!
    {
        WdFrameInput in = makeWdIn(4, kWdStLandingFallSpecial, kWdStJumpSquat, 2.5f, 0.0f);
        in.stickX = 0.96f;
        in.stickY = -0.28f;
        addWdEv(in, kWdStJumpSquat, kWdStJump, kWdCoreShortHopFlag, 1);
        addWdEv(in, kWdStJump, kWdStEscapeAir, 0, 1);
        addWdEv(in, kWdStEscapeAir, kWdStLandingFallSpecial, 0, 0);
        CHECK(!wdCoreStep(st, in));
    }
    // Slide across 10 frames of 0x19 from x=2.5 to x=18.5, actionable on frame 14.
    for (s32 f = 5; f <= 13; f++)
    {
        WdFrameInput in = makeWdIn(f, kWdStLandingFallSpecial, kWdStLandingFallSpecial, 2.5f + (f - 4) * 1.6f, 0.0f);
        CHECK(!wdCoreStep(st, in));
    }
    {
        WdFrameInput in = makeWdIn(14, 0x00, kWdStLandingFallSpecial, 18.5f, 0.0f);
        in.actionable = 1;
        addWdEv(in, kWdStLandingFallSpecial, 0x00);
        CHECK(wdCoreStep(st, in));
    }

    CHECK_EQ(static_cast<int>(kWdKindWavedash), static_cast<int>(st.last.kind));
    CHECK_EQ(static_cast<int>(kWdOutcomeSuccess), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.jumpsquatToDodge));
    CHECK_EQ(2, static_cast<int>(st.last.jumpHoldFrames));
    CHECK_EQ(1, static_cast<int>(st.last.shortHop));
    CHECK_EQ(0, static_cast<int>(st.last.onPlatform));
    CHECK_NEAR(16.2602f, st.last.angleDeg, 0.05f);
    CHECK_NEAR(16.0f, st.last.slideDistance, 0.01f);
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wdAttempts));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wdSuccesses));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wdPerfect));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.wlAttempts));

    // 2. Platform waveland on Battlefield: jump from y=0.0 up onto platform at y=27.2!
    {
        WdFrameInput in = makeWdIn(20, kWdStJumpSquat, 0x00, 10.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(23, kWdStJump, kWdStJumpSquat, 10.0f, 1.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, 0, 1);
        wdCoreStep(st, in);
    }
    // 14 frames later (f=37), fighter reaches platform height and airdodges onto it (frame-1 waveland).
    {
        WdFrameInput in = makeWdIn(37, kWdStLandingFallSpecial, kWdStJump, -22.0f, 27.2f);
        in.onPlatform = 1;
        in.stickX = -0.92f;
        in.stickY = -0.35f;
        addWdEv(in, kWdStJump, kWdStEscapeAir, 0, 1);
        addWdEv(in, kWdStEscapeAir, kWdStLandingFallSpecial, 0, 0);
        CHECK(!wdCoreStep(st, in));
    }
    {
        WdFrameInput in = makeWdIn(47, 0x00, kWdStLandingFallSpecial, -35.0f, 27.2f);
        in.actionable = 1;
        addWdEv(in, kWdStLandingFallSpecial, 0x00);
        CHECK(wdCoreStep(st, in));
    }

    CHECK_EQ(static_cast<int>(kWdKindWaveland), static_cast<int>(st.last.kind));
    CHECK_EQ(static_cast<int>(kWdOutcomeSuccess), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.airdodgeAirFrames));
    CHECK_EQ(1, static_cast<int>(st.last.onPlatform));
    CHECK_NEAR(13.0f, st.last.slideDistance, 0.01f);
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wdAttempts)); // unchanged!
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wlAttempts));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wlSuccesses));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wlPerfect));

    // 3. Fall-based waveland with 2f airborne airdodge (non-perfect).
    {
        WdFrameInput in = makeWdIn(60, kWdStEscapeAir, kWdStFall, 0.0f, 30.0f);
        in.airborne = 1;
        in.stickX = 0.85f;
        in.stickY = -0.50f;
        addWdEv(in, kWdStFall, kWdStEscapeAir, 0, 1);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(61, kWdStLandingFallSpecial, kWdStEscapeAir, 2.0f, 27.2f);
        in.onPlatform = 1;
        addWdEv(in, kWdStEscapeAir, kWdStLandingFallSpecial, 0, 0);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(71, 0x00, kWdStLandingFallSpecial, 10.0f, 27.2f);
        in.actionable = 1;
        addWdEv(in, kWdStLandingFallSpecial, 0x00);
        CHECK(wdCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kWdKindWaveland), static_cast<int>(st.last.kind));
    CHECK_EQ(2, static_cast<int>(st.last.airdodgeAirFrames));
    CHECK_EQ(2u, static_cast<unsigned>(st.stats.wlSuccesses));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.wlPerfect));
}

TEST(stage4a_core, wavedash_all_four_miss_classifications)
{
    WdCoreState st;
    wdCoreResetAll(st);

    // 1. kWdMissUpwardAirdodge: 0x0A -> 0x0B -> 0x21 -> 0x10 (Fall_Special without 0x19).
    {
        WdFrameInput in = makeWdIn(1, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(4, kWdStEscapeAir, kWdStJumpSquat, 0.0f, 2.0f);
        in.airborne = 1;
        in.stickX = 0.7f;
        in.stickY = 0.7f;
        addWdEv(in, kWdStJumpSquat, kWdStJump, 0, 1);
        addWdEv(in, kWdStJump, kWdStEscapeAir, 0, 1);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(32, kWdStFallSpecial, kWdStEscapeAir, 10.0f, 15.0f);
        in.airborne = 1;
        addWdEv(in, kWdStEscapeAir, kWdStFallSpecial, 0, 1);
        CHECK(wdCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kWdMissUpwardAirdodge), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kWdKindWavedash), static_cast<int>(st.last.kind));

    // 2. kWdMissZairTether: Link/Samus presses Z out of Jump -> Air_Lasso (0x7F).
    {
        WdFrameInput in = makeWdIn(40, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(43, kWdStAirLassoFirst, kWdStJumpSquat, 0.0f, 2.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, 0, 1);
        addWdEv(in, kWdStJump, kWdStAirLassoFirst, 0, 1);
        CHECK(wdCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kWdMissZairTether), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kWdKindWavedash), static_cast<int>(st.last.kind));

    // 3. kWdMissLateAirdodge: landed in Landing (0x16) and pressed guard 2f later.
    {
        WdFrameInput in = makeWdIn(60, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(63, kWdStJump, kWdStJumpSquat, 0.0f, 2.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, kWdCoreShortHopFlag, 1);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(75, kWdStLanding, kWdStJump, 0.0f, 0.0f);
        addWdEv(in, kWdStJump, kWdStLanding, 0, 0);
        CHECK(!wdCoreStep(st, in));
    }
    {
        WdFrameInput in = makeWdIn(77, kWdStGuardOn, kWdStLanding, 0.0f, 0.0f);
        in.guardEdge = 1;
        addWdEv(in, kWdStLanding, kWdStGuardOn, 0, 0);
        CHECK(wdCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kWdMissLateAirdodge), static_cast<int>(st.last.outcome));

    // 4. kWdMissNoAirdodge: jumped and landed with no shield press for 6 frames.
    {
        WdFrameInput in = makeWdIn(90, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(93, kWdStJump, kWdStJumpSquat, 0.0f, 2.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, kWdCoreShortHopFlag, 1);
        wdCoreStep(st, in);
    }
    {
        WdFrameInput in = makeWdIn(105, kWdStLanding, kWdStJump, 0.0f, 0.0f);
        addWdEv(in, kWdStJump, kWdStLanding, 0, 0);
        CHECK(!wdCoreStep(st, in));
    }
    for (s32 f = 106; f <= 110; f++)
    {
        WdFrameInput in = makeWdIn(f, 0x00, kWdStLanding, 0.0f, 0.0f);
        CHECK(!wdCoreStep(st, in));
    }
    {
        WdFrameInput in = makeWdIn(111, 0x00, 0x00, 0.0f, 0.0f);
        CHECK(wdCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kWdMissNoAirdodge), static_cast<int>(st.last.outcome));
    CHECK_EQ(4u, static_cast<unsigned>(st.stats.wdAttempts));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.wdSuccesses));
}

// ---- Engine 3: L-Cancel ----------------------------------------------------

TEST(stage4a_core, lcancel_whiff_hit_shield_digital_light_and_zpress)
{
    LcCoreState st;
    lcCoreResetAll(st);

    // 1. Whiff manual digital L-cancel (frame 3/7 -> LA-Basic[90] = 5 on landing).
    {
        LcFrameInput in = makeLcIn(1, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(5, kLcStAttackAir, kLcStAttackAir);
        in.digitalGuardEdge = 1;
        in.lCancelCounter = 7;
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(7, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 5, 0x62);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessManual), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kLcContextWhiff), static_cast<int>(st.last.context));
    CHECK_EQ(static_cast<int>(kLcInputDigital), static_cast<int>(st.last.inputKind));
    CHECK_EQ(3, static_cast<int>(st.last.pressFrame));
    CHECK_EQ(0, static_cast<int>(st.last.lightPress));
    CHECK_EQ(0, static_cast<int>(st.last.zPress));

    // 2. Hit context with hitlag + analog light-press (LA-Basic[77] == 1, counter = 8 in hitlag).
    {
        LcFrameInput in = makeLcIn(20, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(24, kLcStAttackAir, kLcStAttackAir);
        in.dealtHit = 1;
        in.attackActive = 1;
        in.hitlag = 6;
        in.analogLRState = 1;
        in.lCancelCounter = 8;
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(30, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 7, 0x63);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessManual), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kLcContextHit), static_cast<int>(st.last.context));
    CHECK_EQ(static_cast<int>(kLcInputLightPress), static_cast<int>(st.last.inputKind));
    CHECK_EQ(1, static_cast<int>(st.last.lightPress));
    CHECK_EQ(1, static_cast<int>(st.last.pressFrame));
    CHECK_EQ(1, static_cast<int>(st.last.hitlagFrames));

    // 3. Shield context with Z-press (MASK_GUARD + MASK_ATTACK rising edge together).
    {
        LcFrameInput in = makeLcIn(50, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(54, kLcStAttackAir, kLcStAttackAir);
        in.dealtShield = 1;
        in.hitlag = 5;
        in.digitalGuardEdge = 1;
        in.digitalAttackEdge = 1;
        in.lCancelCounter = 8;
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(61, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 2, 0x64);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessManual), static_cast<int>(st.last.outcome));
    CHECK_EQ(static_cast<int>(kLcContextShield), static_cast<int>(st.last.context));
    CHECK_EQ(static_cast<int>(kLcInputZPress), static_cast<int>(st.last.inputKind));
    CHECK_EQ(1, static_cast<int>(st.last.zPress));
    CHECK_EQ(6, static_cast<int>(st.last.pressFrame));

    CHECK_EQ(3u, static_cast<unsigned>(st.stats.total));
    CHECK_EQ(3u, static_cast<unsigned>(st.stats.successes));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.whiffHits));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.whiffTotal));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.hitHits));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.hitTotal));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.shieldHits));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.shieldTotal));
    CHECK_EQ(3u, static_cast<unsigned>(st.stats.streak));
    CHECK_EQ(3u, static_cast<unsigned>(st.stats.bestStreak));
}

TEST(stage4a_core, lcancel_early_late_nopress_alc_and_autocancel)
{
    LcCoreState st;
    lcCoreResetAll(st);

    // 1. Early miss: pressed 3 non-hitlag frames too early (LA-Basic[90] == -2 at landing).
    {
        LcFrameInput in = makeLcIn(1, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(2, kLcStAttackAir, kLcStAttackAir);
        in.digitalGuardEdge = 1;
        in.lCancelCounter = 7;
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(11, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -2);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcMissEarly), static_cast<int>(st.last.outcome));
    CHECK_EQ(3, static_cast<int>(st.last.earlyFrames));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.earlyMisses));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.total));

    // 2. Late miss: missed on landing (f=25), then fresh shield press at f=28 (+3f late).
    {
        LcFrameInput in = makeLcIn(20, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        // Note: LA-Basic[90] decrements negative even with no press!
        LcFrameInput in = makeLcIn(25, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -5);
        CHECK(lcCoreStep(st, in));
        CHECK_EQ(static_cast<int>(kLcMissNoPress), static_cast<int>(st.last.outcome));
        CHECK_EQ(2u, static_cast<unsigned>(st.stats.total));
    }
    {
        LcFrameInput in = makeLcIn(28, kLcStLandingAttackAir, kLcStLandingAttackAir);
        in.digitalGuardEdge = 1;
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(3, static_cast<int>(st.last.lateFrames));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.lateMisses));
    CHECK_EQ(2u, static_cast<unsigned>(st.stats.total)); // NOT double-counted!

    // 3. Pure No-Press miss: no press for > 10f in 0x18.
    {
        LcFrameInput in = makeLcIn(40, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(45, kLcStLandingAttackAir, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -5);
        CHECK(lcCoreStep(st, in));
    }
    {
        // Press at +11f is outside 1..10f late window.
        LcFrameInput in = makeLcIn(56, kLcStLandingAttackAir, kLcStLandingAttackAir);
        in.digitalGuardEdge = 1;
        CHECK(!lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcMissNoPress), static_cast<int>(st.last.outcome));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.lateMisses));

    // 4. Auto L-Cancel (ALC = 1, 3f Buffer = 1) with no manual press -> kLcSuccessAuto.
    {
        LcFrameInput in = makeLcIn(70, kLcStAttackAir, 0x0B);
        in.autoLCancelActive = 1;
        in.inputBuffer3fActive = 1;
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(75, kLcStLandingAttackAir, kLcStAttackAir);
        in.autoLCancelActive = 1;
        in.inputBuffer3fActive = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -5);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessAuto), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.autoLCancelActive));
    CHECK_EQ(1, static_cast<int>(st.last.inputBuffer3fActive));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.autoSuccesses));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.successes));

    // 5. Autocanceled aerial (0x33 -> 0x16 Landing): does NOT increment stats.total.
    u16 totalBefore = st.stats.total;
    {
        LcFrameInput in = makeLcIn(90, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(96, kLcStLanding, kLcStAttackAir);
        addLcEv(in, kLcStAttackAir, kLcStLanding, 0);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcAutoCanceled), static_cast<int>(st.last.outcome));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.autoCanceled));
    CHECK_EQ(totalBefore, st.stats.total);
}

// ---- Multi-fighter isolation & savestate transient reset -------------------

TEST(stage4a_core, multi_fighter_four_slots_and_savestate_transient_reset)
{
    LedgeCoreState ledge[4];
    WdCoreState wd[4];
    LcCoreState lc[4];
    for (int p = 0; p < 4; p++)
    {
        ledgeCoreResetAll(ledge[p]);
        wdCoreResetAll(wd[p]);
        lcCoreResetAll(lc[p]);
    }

    // Slot 2 (P3) completes a valid L-cancel while Slot 0 (P1) is mid-aerial and
    // Slot 3 (P4) is mid-ledge-hang.
    {
        LcFrameInput in0 = makeLcIn(10, kLcStAttackAir, 0x0B);
        addLcEv(in0, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(lc[0], in0);

        LcFrameInput in2 = makeLcIn(10, kLcStAttackAir, 0x0B);
        addLcEv(in2, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(lc[2], in2);
        LcFrameInput in2b = makeLcIn(12, kLcStLandingAttackAir, kLcStAttackAir);
        in2b.digitalGuardEdge = 1;
        addLcEv(in2b, kLcStAttackAir, kLcStLandingAttackAir, 7);
        CHECK(lcCoreStep(lc[2], in2b));

        LedgeFrameInput in3 = makeLedgeIn(10, kLedgeStCliffWait, kLedgeStCliffCatch);
        in3.xluFrames = 31;
        in3.ledgeGrabCount = 1;
        addLedgeEv(in3, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(ledge[3], in3);

        WdFrameInput in1 = makeWdIn(10, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in1, 0x00, kWdStJumpSquat);
        wdCoreStep(wd[1], in1);
    }

    CHECK_EQ(1u, static_cast<unsigned>(lc[2].stats.successes));
    CHECK_EQ(0u, static_cast<unsigned>(lc[0].stats.successes));
    CHECK_EQ(1, static_cast<int>(lc[0].inAerial));
    CHECK_EQ(static_cast<int>(kLedgePhaseOnLedge), static_cast<int>(ledge[3].phase));
    CHECK_EQ(static_cast<int>(kWdPhaseSquat), static_cast<int>(wd[1].phase));

    // Savestate load transient reset clears in-progress watches across all slots
    // while keeping completed stats intact.
    for (int p = 0; p < 4; p++)
    {
        ledgeCoreResetTransient(ledge[p]);
        wdCoreResetTransient(wd[p]);
        lcCoreResetTransient(lc[p]);
    }
    CHECK_EQ(0, static_cast<int>(lc[0].inAerial));
    CHECK_EQ(static_cast<int>(kLedgePhaseOff), static_cast<int>(ledge[3].phase));
    CHECK_EQ(static_cast<int>(kWdPhaseIdle), static_cast<int>(wd[1].phase));
    CHECK_EQ(1u, static_cast<unsigned>(lc[2].stats.successes));
}

TEST(stage4a_core, edge_cases_special_xlu_strip_alc_manual_priority_and_platform_wavedash)
{
    // 1. Tourney Winner 0x7A -> SpecialN (0x112) strips xlu, plus Cliff_Climb (0x77) OtherGetup.
    LedgeCoreState ls;
    ledgeCoreResetAll(ls);
    {
        LedgeFrameInput in = makeLedgeIn(1, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(2, kLedgeStCliffJump1, kLedgeStCliffWait);
        in.xluFrames = 30;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStCliffJump1, 0xDA, 0);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(12, kLedgeStCliffJump2, kLedgeStCliffJump1);
        in.xluFrames = 20;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffJump1, kLedgeStCliffJump2, 0xDB, 1);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(14, kLedgeStSpecialFirst, kLedgeStCliffJump2);
        in.xluFrames = 0;
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffJump2, kLedgeStSpecialFirst, -1, 1);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(30, 0x00, kLedgeStLandingFirst);
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFirst, 0x00, 0, 0);
        CHECK(ledgeCoreStep(ls, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeTourneyWinner), static_cast<int>(ls.last.outcome));
    CHECK_EQ(1, static_cast<int>(ls.last.ledgeJumpXluStripped));
    CHECK((ls.last.failFlags & kLedgeFailLedgeJumpXluStrip) != 0);

    // Cliff_Climb (0x77) -> kLedgeOutcomeOtherGetup (never airborne).
    {
        LedgeFrameInput in = makeLedgeIn(40, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 31;
        in.ledgeGrabCount = 1;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(41, kLedgeStCliffClimb, kLedgeStCliffWait);
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStCliffClimb, -1, 0);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(60, 0x00, kLedgeStCliffClimb);
        in.actionable = 1;
        addLedgeEv(in, kLedgeStCliffClimb, 0x00, -1, 0);
        CHECK(ledgeCoreStep(ls, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeOtherGetup), static_cast<int>(ls.last.outcome));

    // 2. ALC ON + valid manual press: manual press wins (L-Cancel.asm:194-232) and tags autoLCancelActive = 1.
    LcCoreState cs;
    lcCoreResetAll(cs);
    {
        LcFrameInput in = makeLcIn(1, kLcStAttackAir, 0x0B);
        in.autoLCancelActive = 1;
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(cs, in);
    }
    {
        LcFrameInput in = makeLcIn(5, kLcStLandingAttackAir, kLcStAttackAir);
        in.autoLCancelActive = 1;
        in.digitalGuardEdge = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 6);
        CHECK(lcCoreStep(cs, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessManual), static_cast<int>(cs.last.outcome));
    CHECK_EQ(1, static_cast<int>(cs.last.autoLCancelActive));
    CHECK_EQ(2, static_cast<int>(cs.last.pressFrame));
    CHECK_EQ(1u, static_cast<unsigned>(cs.stats.successes));
    CHECK_EQ(0u, static_cast<unsigned>(cs.stats.autoSuccesses));

    // 3. Wavedash starting and ending ON a platform (same elevation y=27.2) stays kWdKindWavedash with onPlatform=1,
    //    and a Fall -> Air_Lasso zair is classified as kWdKindWaveland + kWdMissZairTether.
    WdCoreState ws;
    wdCoreResetAll(ws);
    {
        WdFrameInput in = makeWdIn(1, kWdStJumpSquat, 0x00, -35.0f, 27.2f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(4, kWdStLandingFallSpecial, kWdStJumpSquat, -33.0f, 27.2f);
        in.onPlatform = 1;
        in.stickX = 0.96f;
        in.stickY = -0.28f;
        addWdEv(in, kWdStJumpSquat, kWdStJump, 0, 1);
        addWdEv(in, kWdStJump, kWdStEscapeAir, 0, 1);
        addWdEv(in, kWdStEscapeAir, kWdStLandingFallSpecial, 0, 0);
        wdCoreStep(ws, in);
    }
    {
        // Back-to-back Jump_Squat on frame 14 finalizes previous slide AND starts next wavedash.
        WdFrameInput in = makeWdIn(14, kWdStJumpSquat, kWdStLandingFallSpecial, -22.0f, 27.2f);
        in.actionable = 1;
        addWdEv(in, kWdStLandingFallSpecial, kWdStJumpSquat);
        CHECK(wdCoreStep(ws, in));
    }
    CHECK_EQ(static_cast<int>(kWdKindWavedash), static_cast<int>(ws.last.kind));
    CHECK_EQ(1, static_cast<int>(ws.last.onPlatform));
    CHECK_NEAR(11.0f, ws.last.slideDistance, 0.01f);
    CHECK_EQ(static_cast<int>(kWdPhaseSquat), static_cast<int>(ws.phase));

    // Airborne Fall -> Air_Lasso (0x7F) -> waveland tether miss.
    wdCoreResetTransient(ws);
    {
        WdFrameInput in = makeWdIn(30, kWdStAirLassoFirst, kWdStFall, 0.0f, 30.0f);
        in.airborne = 1;
        addWdEv(in, kWdStFall, kWdStAirLassoFirst, 0, 1);
        CHECK(wdCoreStep(ws, in));
    }
    CHECK_EQ(static_cast<int>(kWdKindWaveland), static_cast<int>(ws.last.kind));
    CHECK_EQ(static_cast<int>(kWdMissZairTether), static_cast<int>(ws.last.outcome));

    // 4. Ledgedash grab #1 with xluFrames == 0 (!hangXlu, grabCount <= 5) must NOT
    //    misclassify as kLedgeOutcomeNoInvulnLimit (no fabricated "GRAB 6/5").
    ledgeCoreResetAll(ls);
    {
        LedgeFrameInput in = makeLedgeIn(1, kLedgeStCliffWait, kLedgeStCliffCatch);
        in.xluFrames = 0;
        in.ledgeGrabCount = 1;
        addLedgeEv(in, kLedgeStCliffCatch, kLedgeStCliffWait);
        ledgeCoreStep(ls, in);
        CHECK_EQ(0, static_cast<int>(ls.noInvulnLimit));
    }
    {
        LedgeFrameInput in = makeLedgeIn(3, kLedgeStFall, kLedgeStCliffWait);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStCliffWait, kLedgeStFall, -1, 1);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(4, kLedgeStJumpAerial, kLedgeStFall);
        in.airborne = 1;
        addLedgeEv(in, kLedgeStFall, kLedgeStJumpAerial, -1, 1);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(5, kLedgeStLandingFallSpecial, kLedgeStJumpAerial);
        addLedgeEv(in, kLedgeStJumpAerial, kLedgeStEscapeAir, -1, 1);
        addLedgeEv(in, kLedgeStEscapeAir, kLedgeStLandingFallSpecial, -1, 0);
        ledgeCoreStep(ls, in);
    }
    {
        LedgeFrameInput in = makeLedgeIn(15, 0x00, kLedgeStLandingFallSpecial);
        in.actionable = 1;
        addLedgeEv(in, kLedgeStLandingFallSpecial, 0x00, -1, 0);
        CHECK(ledgeCoreStep(ls, in));
    }
    CHECK_EQ(static_cast<int>(kLedgeOutcomeLedgedash), static_cast<int>(ls.last.outcome));
    CHECK_EQ(1, static_cast<int>(ls.last.noXlu));
    CHECK_EQ(0, static_cast<int>(ls.last.galint));
    CHECK_EQ(0u, static_cast<unsigned>(ls.stats.noInvulnGrabs));

    // 5. Wavedash: empty hop from main floor (y=0) up onto a platform (y=27.2) without
    //    pressing L/R must abort quietly instead of recording kWdMissNoAirdodge;
    //    premature guard press during JumpSquat on same floor sets earlyGuardInSquat=1.
    wdCoreResetAll(ws);
    {
        WdFrameInput in = makeWdIn(1, kWdStJumpSquat, 0x00, -38.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(4, kWdStJump, kWdStJumpSquat, -38.0f, 2.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, 0, 1);
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(18, kWdStLanding, kWdStJump, -38.0f, 27.2f);
        in.onPlatform = 1;
        addWdEv(in, kWdStJump, kWdStLanding, 0, 0);
        CHECK(!wdCoreStep(ws, in));
    }
    for (s32 f = 19; f <= 26; f++)
    {
        WdFrameInput in = makeWdIn(f, 0x00, 0x00, -38.0f, 27.2f);
        CHECK(!wdCoreStep(ws, in));
    }
    CHECK_EQ(0u, ws.seq);
    CHECK_EQ(0u, static_cast<unsigned>(ws.stats.wdAttempts));

    // Premature guard edge during JumpSquat on same floor -> earlyGuardInSquat == 1.
    {
        WdFrameInput in = makeWdIn(40, kWdStJumpSquat, 0x00, 0.0f, 0.0f);
        addWdEv(in, 0x00, kWdStJumpSquat);
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(41, kWdStJumpSquat, kWdStJumpSquat, 0.0f, 0.0f);
        in.guardEdge = 1;
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(43, kWdStJump, kWdStJumpSquat, 0.0f, 2.0f);
        in.airborne = 1;
        addWdEv(in, kWdStJumpSquat, kWdStJump, kWdCoreShortHopFlag, 1);
        wdCoreStep(ws, in);
    }
    {
        WdFrameInput in = makeWdIn(55, kWdStLanding, kWdStJump, 0.0f, 0.0f);
        addWdEv(in, kWdStJump, kWdStLanding, 0, 0);
        wdCoreStep(ws, in);
    }
    for (s32 f = 56; f <= 60; f++)
    {
        WdFrameInput in = makeWdIn(f, 0x00, kWdStLanding, 0.0f, 0.0f);
        CHECK(!wdCoreStep(ws, in));
    }
    {
        WdFrameInput in = makeWdIn(61, 0x00, 0x00, 0.0f, 0.0f);
        CHECK(wdCoreStep(ws, in));
    }
    CHECK_EQ(static_cast<int>(kWdMissNoAirdodge), static_cast<int>(ws.last.outcome));
    CHECK_EQ(1, static_cast<int>(ws.last.earlyGuardInSquat));
}

TEST(stage4a_core, lcancel_landing_frame_late_press_with_alc_off_and_on)
{
    LcCoreState st;
    lcCoreResetAll(st);

    // 1. ALC OFF: player never pressed in air (LA-Basic[90] decremented to -12),
    //    then pressed Z late on the landing frame itself (0x33 -> 0x18 with e.lCancel <= 0).
    //    Must classify as kLcMissLate (lateFrames = 1, zPress = 1), NOT kLcMissEarly!
    {
        LcFrameInput in = makeLcIn(1, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(13, kLcStLandingAttackAir, kLcStAttackAir);
        in.digitalGuardEdge = 1;
        in.digitalAttackEdge = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -12, 0x62);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.lateFrames));
    CHECK_EQ(0, static_cast<int>(st.last.earlyFrames));
    CHECK_EQ(static_cast<int>(kLcInputZPress), static_cast<int>(st.last.inputKind));
    CHECK_EQ(1, static_cast<int>(st.last.zPress));
    CHECK_EQ(0, static_cast<int>(st.last.lightPress));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.lateMisses));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.earlyMisses));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.total));

    // Subsequent guard press in 0x18 must NOT re-emit or double-count lateMisses.
    {
        LcFrameInput in = makeLcIn(15, kLcStLandingAttackAir, kLcStLandingAttackAir);
        in.digitalGuardEdge = 1;
        CHECK(!lcCoreStep(st, in));
    }
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.lateMisses));

    // 2. ALC OFF: landing-frame analog light-press (LA-Basic[77] == 1) with e.lCancel == 0.
    {
        LcFrameInput in = makeLcIn(30, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(38, kLcStLandingAttackAir, kLcStAttackAir);
        in.analogLRState = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 0, 0x62);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.lateFrames));
    CHECK_EQ(0, static_cast<int>(st.last.earlyFrames));
    CHECK_EQ(static_cast<int>(kLcInputLightPress), static_cast<int>(st.last.inputKind));
    CHECK_EQ(1, static_cast<int>(st.last.lightPress));
    CHECK_EQ(2u, static_cast<unsigned>(st.stats.lateMisses));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.earlyMisses));

    // 3. ALC ON: never pressed in air, landing-frame digital press with e.lCancel <= 0
    //    must emit kLcSuccessAuto with earlyFrames == 0 (no fabricated early count).
    {
        LcFrameInput in = makeLcIn(50, kLcStAttackAir, 0x0B);
        in.autoLCancelActive = 1;
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(62, kLcStLandingAttackAir, kLcStAttackAir);
        in.autoLCancelActive = 1;
        in.digitalGuardEdge = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, -12, 0x62);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessAuto), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.earlyFrames));
    CHECK_EQ(0, static_cast<int>(st.last.lateFrames));
    CHECK_EQ(1u, static_cast<unsigned>(st.stats.autoSuccesses));
    CHECK_EQ(0u, static_cast<unsigned>(st.stats.earlyMisses));
    CHECK_EQ(2u, static_cast<unsigned>(st.stats.lateMisses));

    // 4. Same-frame valid press (e.lCancel == 7 with light-press) still tags kLcSuccessManual!
    {
        LcFrameInput in = makeLcIn(80, kLcStAttackAir, 0x0B);
        addLcEv(in, 0x0B, kLcStAttackAir, 0);
        lcCoreStep(st, in);
    }
    {
        LcFrameInput in = makeLcIn(85, kLcStLandingAttackAir, kLcStAttackAir);
        in.analogLRState = 1;
        addLcEv(in, kLcStAttackAir, kLcStLandingAttackAir, 7, 0x62);
        CHECK(lcCoreStep(st, in));
    }
    CHECK_EQ(static_cast<int>(kLcSuccessManual), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.pressFrame));
    CHECK_EQ(static_cast<int>(kLcInputLightPress), static_cast<int>(st.last.inputKind));
    CHECK_EQ(1, static_cast<int>(st.last.lightPress));
}
