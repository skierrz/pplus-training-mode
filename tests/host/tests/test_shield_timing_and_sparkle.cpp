// Host unit tests for CPU Shield Timing (First Gap vs Full Move), FrameRecord
// multi-hit wave tracking (frameDataMoreHits), and Tech Sparkle cue logic.
#include <string.h>

#include "extra/frame_data.h"
#include "extra/tech_sparkle_core.h"
#include "host/test.h"
#include "main/cpu/shield_timing_logic.h"
#include "main/game/status_ids.h"
#include "main/settings.h"

using namespace tm;

namespace {
    cpu::ShieldHoldInput baseHold()
    {
        cpu::ShieldHoldInput in;
        in.shieldTiming = cpu::kShieldTimingFullMove;
        in.attackerReadable = true;
        in.latchedStatus = game::kStatusAttackAirFirst;
        in.attackerStatus = game::kStatusAttackAirFirst;
        in.attackerHitstun = 0;
        in.attackerDamageHitlag = false;
        in.attackerCanAct = false;
        in.attackerHasMoreHits = true;
        in.distSq = 100.0f;
        in.holdFrames = 0;
        return in;
    }

    extra::TechSparkleInput mkInput(s32 frame, u8 mode)
    {
        extra::TechSparkleInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.mode = mode;
        return in;
    }

    void addEv(extra::TechSparkleInput& in, s16 status, s16 prev, u32 flags = 0)
    {
        extra::TechSparkleEvent& e = in.events[in.eventCount++];
        e.status = status;
        e.prev = prev;
        e.flags = flags;
    }
}

TEST(shield_timing, full_move_holds_through_gaps_and_releases_on_every_exit_condition)
{
    cpu::ShieldHoldInput in = baseHold();
    CHECK(cpu::shouldHoldFullMoveShield(in));

    // First Gap mode never holds past shieldstun.
    in.shieldTiming = cpu::kShieldTimingFirstGap;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.shieldTiming = cpu::kShieldTimingFullMove;

    // Attacker finishes hits (e.g. single-hit move or after final wave).
    in.attackerHasMoreHits = false;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.attackerHasMoreHits = true;

    // Attacker lands into LandingAttackAir (status changes).
    in.attackerStatus = game::kStatusLandingAttackAir;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.attackerStatus = game::kStatusAttackAirFirst;

    // Attacker hits IASA / becomes actionable.
    in.attackerCanAct = true;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.attackerCanAct = false;

    // Attacker takes damage or hitstun.
    in.attackerHitstun = 5;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.attackerHitstun = 0;
    in.attackerDamageHitlag = true;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.attackerDamageHitlag = false;

    // Attacker drifts out of melee range (> 45 units).
    in.distSq = 50.0f * 50.0f;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.distSq = 100.0f;

    // Safety cap stops infinite rapid-jab locks.
    in.holdFrames = cpu::kFullMoveMaxHold;
    CHECK(!cpu::shouldHoldFullMoveShield(in));
    in.holdFrames = cpu::kFullMoveMaxHold - 1;
    CHECK(cpu::shouldHoldFullMoveShield(in));

    // Non-attack statuses are rejected (prevents projectile mis-latch).
    CHECK(cpu::isAttackerMoveStatus(game::kStatusAttackAirFirst));
    CHECK(cpu::isAttackerMoveStatus(game::kStatusSpecialN));
    CHECK(!cpu::isAttackerMoveStatus(game::kStatusWait));
    CHECK(!cpu::isAttackerMoveStatus(game::kStatusLanding));
    CHECK(!cpu::isAttackerMoveStatus(game::kStatusEscape));
}

TEST(shield_timing, frame_data_more_hits_distinguishes_single_hit_multihit_and_rehit)
{
    extra::FrameRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.flags = extra::kFdValid;

    // Single-hit move (e.g. Mario F-Smash or Fox Nair): hitWaves == 1 -> never delays OoS.
    rec.hitWaves = 1;
    rec.lastHitOn = 6.0f;
    rec.lastHitOff = 9.0f;
    CHECK(!extra::frameDataMoreHits(rec, 6.0f, true, false));
    CHECK(!extra::frameDataMoreHits(rec, 8.0f, true, false));

    // Meta Knight Fair (3 hits: f6-7, f10-11, f13-14): holds in f8-9 and f12 gaps,
    // releases once clock reaches lastHitOff (f14).
    rec.hitWaves = 3;
    rec.lastHitOn = 12.0f;
    rec.lastHitOff = 14.0f;
    CHECK(extra::frameDataMoreHits(rec, 7.0f, true, false));
    CHECK(extra::frameDataMoreHits(rec, 9.0f, true, false));
    CHECK(extra::frameDataMoreHits(rec, 11.0f, true, false));
    CHECK(extra::frameDataMoreHits(rec, 13.0f, true, false));
    CHECK(!extra::frameDataMoreHits(rec, 14.0f, true, false));
    CHECK(!extra::frameDataMoreHits(rec, 20.0f, true, false));

    // Lucas Nair (5 hits ending at f19, IASA at f39): releases immediately at f19
    // instead of waiting through 20f of airborne endlag.
    rec.hitWaves = 5;
    rec.lastHitOn = 16.0f;
    rec.lastHitOff = 19.0f;
    CHECK(extra::frameDataMoreHits(rec, 15.0f, true, false));
    CHECK(!extra::frameDataMoreHits(rec, 19.0f, true, false));

    // Fox Dair (7 hits, past kFdMaxHit=6): still tracks all 7 waves accurately.
    rec.hitWaves = 7;
    rec.lastHitOn = 23.0f;
    rec.lastHitOff = 24.0f;
    CHECK(extra::frameDataMoreHits(rec, 21.0f, true, false));
    CHECK(!extra::frameDataMoreHits(rec, 24.0f, true, false));

    // Active rehit hitbox (0x0615 SpecialOffensiveCollision with m_serialHitFrame > 0):
    // holds even with hitWaves == 1 while hitbox remains open.
    rec.hitWaves = 1;
    rec.lastHitOn = 4.0f;
    rec.lastHitOff = -1.0f;
    CHECK(extra::frameDataMoreHits(rec, 12.0f, true, true));
    CHECK(!extra::frameDataMoreHits(rec, 12.0f, true, false));

    // Infinite loop (Attack100 rapid jab): holds while kFdLoop is set.
    rec.flags = extra::kFdValid | extra::kFdLoop;
    CHECK(extra::frameDataMoreHits(rec, 25.0f, true, false));
}

TEST(tech_sparkle, modes_cover_perfect_wavedash_shorthop_both_and_no_duplicate)
{
    extra::TechSparkleSlot slot;

    // 1. Perfect Wavedash mode (default): fires on f1 wavedash, silent on f2 wavedash and shorthop.
    extra::techSparkleResetSlot(slot);
    extra::TechSparkleInput in = mkInput(10, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));

    in = mkInput(13, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    addEv(in, game::kStatusEscapeAir, game::kStatusJump);
    addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
    CHECK_EQ(static_cast<u8>(extra::kSparklePerfectWavedash), extra::stepTechSparkle(slot, in));

    // Late f2 wavedash does NOT sparkle.
    in = mkInput(20, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(23, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(24, main::kTechSparkleWavedash);
    addEv(in, game::kStatusEscapeAir, game::kStatusJump);
    addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));

    // 2. Shorthop mode: fires on clean shorthop (including SH aerial), silent on fullhop.
    extra::techSparkleResetSlot(slot);
    in = mkInput(30, main::kTechSparkleShorthop);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(33, main::kTechSparkleShorthop);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, in));

    // Fullhop (no kTsShortHopFlag) is silent.
    in = mkInput(40, main::kTechSparkleShorthop);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(43, main::kTechSparkleShorthop);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, 0);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));

    // 3. Both mode: shorthop-button Frame-1 wavedash landing next frame fires ONLY Perfect Wavedash (no duplicate!).
    extra::techSparkleResetSlot(slot);
    in = mkInput(50, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(53, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    addEv(in, game::kStatusEscapeAir, game::kStatusJump);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(54, main::kTechSparkleBoth);
    addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
    CHECK_EQ(static_cast<u8>(extra::kSparklePerfectWavedash), extra::stepTechSparkle(slot, in));

    // 4. Off mode: always silent.
    extra::techSparkleResetSlot(slot);
    in = mkInput(60, main::kTechSparkleOff);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(63, main::kTechSparkleOff);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    addEv(in, game::kStatusEscapeAir, game::kStatusJump);
    addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
}

TEST(tech_sparkle, isolation_across_multiframe_wavedashes_and_modes)
{
    extra::TechSparkleSlot slot;

    // 1. Perfect Wavedash mode: NEVER fires Shorthop on clean shorthop, SH aerial,
    //    or 2f/3f shorthop-button wavedashes.
    extra::techSparkleResetSlot(slot);
    extra::TechSparkleInput in = mkInput(100, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(103, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    for (int f = 104; f <= 110; f++)
    {
        in = mkInput(f, main::kTechSparkleWavedash);
        CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    }

    // 3f shorthop-button wavedash in Perfect Wavedash mode -> completely silent.
    extra::techSparkleResetSlot(slot);
    in = mkInput(120, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(123, main::kTechSparkleWavedash);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(125, main::kTechSparkleWavedash);
    addEv(in, game::kStatusEscapeAir, game::kStatusJump);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(126, main::kTechSparkleWavedash);
    addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));

    // 2. Both mode: 2f and 3f shorthop-button wavedashes NEVER fire Shorthop sparkle.
    for (int delay = 1; delay <= 2; delay++)
    {
        extra::techSparkleResetSlot(slot);
        in = mkInput(200, main::kTechSparkleBoth);
        addEv(in, game::kStatusJumpSquat, game::kStatusWait);
        CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        in = mkInput(203, main::kTechSparkleBoth);
        addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
        CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        for (int f = 1; f < delay; f++)
        {
            in = mkInput(203 + f, main::kTechSparkleBoth);
            CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        }
        in = mkInput(203 + delay, main::kTechSparkleBoth);
        addEv(in, game::kStatusEscapeAir, game::kStatusJump);
        CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        in = mkInput(204 + delay, main::kTechSparkleBoth);
        addEv(in, game::kStatusLandingFallSpecial, game::kStatusEscapeAir);
        CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        // Subsequent idle frames stay silent (no delayed Shorthop leak).
        for (int f = 205 + delay; f <= 210 + delay; f++)
        {
            in = mkInput(f, main::kTechSparkleBoth);
            CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
        }
    }

    // 3. Both mode: clean shorthop emits Shorthop once after the 3f defer window.
    extra::techSparkleResetSlot(slot);
    in = mkInput(300, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(303, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(304, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(305, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, mkInput(306, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(307, main::kTechSparkleBoth)));

    // 4. Both mode: immediate SH aerial emits Shorthop right away (no extra lag) and only once.
    extra::techSparkleResetSlot(slot);
    in = mkInput(400, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(403, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(404, main::kTechSparkleBoth);
    addEv(in, game::kStatusAttackAirFirst, game::kStatusJump);
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, in));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(407, main::kTechSparkleBoth)));

    // 5. Both mode: instant SH DJC (JumpAerial) and SH Zair (AirLasso) emit Shorthop immediately.
    extra::techSparkleResetSlot(slot);
    in = mkInput(420, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(423, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(424, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpAerial, game::kStatusJump);
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, in));

    extra::techSparkleResetSlot(slot);
    in = mkInput(440, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(443, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    in = mkInput(444, main::kTechSparkleBoth);
    addEv(in, extra::kTsStatusAirLasso, game::kStatusJump);
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, in));

    // 6. Repeated frame (pause / frame-advance freeze) keeps state intact; rewind clears pendingShorthop.
    extra::techSparkleResetSlot(slot);
    in = mkInput(480, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(483, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, in));
    // Frozen frame 483 repeated does not lose pendingShorthop.
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(483, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(485, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleShorthop), extra::stepTechSparkle(slot, mkInput(486, main::kTechSparkleBoth)));

    // 7. Savestate rewind clears pendingShorthop cleanly.
    extra::techSparkleResetSlot(slot);
    in = mkInput(500, main::kTechSparkleBoth);
    addEv(in, game::kStatusJumpSquat, game::kStatusWait);
    extra::stepTechSparkle(slot, in);
    in = mkInput(503, main::kTechSparkleBoth);
    addEv(in, game::kStatusJump, game::kStatusJumpSquat, extra::kTsShortHopFlag);
    extra::stepTechSparkle(slot, in);
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(450, main::kTechSparkleBoth)));
    CHECK_EQ(static_cast<u8>(extra::kSparkleNone), extra::stepTechSparkle(slot, mkInput(455, main::kTechSparkleBoth)));
}
