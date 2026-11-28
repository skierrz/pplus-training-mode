// Host unit tests for the Hover / Float-Cancel, DJC, and Powershield backends:
//   - src/extra/drill_float_djc_core.h (Peach FC, Mewtwo Hover Cancel, DJC)
//   - src/extra/drill_powershield_core.h (Physical/Reflect PS, Frame-2
//     light-to-hard Frame-3 PS, Re-PS, 5f PS-Drop, Buffered/Early/Late misses)
//   - src/extra/drill_hover_ps_engines.h (HoverPsDebugBlock layout)
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "extra/drill_float_djc_core.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_powershield_core.h"
#include "host/test.h"

using namespace tm;
using namespace tm::extra;

namespace {
    FloatDjcInput makeFd(s32 frame, int ftKind, s16 status, s16 motion, u8 air, float y, float vy)
    {
        FloatDjcInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.ftKind = ftKind;
        in.status = status;
        in.motionKind = motion;
        in.airborne = air;
        in.y = y;
        in.vy = vy;
        return in;
    }

    void addFdEv(FloatDjcInput& in, s16 prev, s16 status, s16 motion = -1, s16 lCancel = 0, u8 air = 0)
    {
        FloatDjcStatusEvent& e = in.events[in.eventCount++];
        e.prev = prev;
        e.status = status;
        e.motionKind = motion;
        e.lCancel = lCancel;
        e.airborne = air;
    }

    PsInput makePs(s32 frame, s16 status, s32 ra1 = 0, s32 ra3 = 0, u8 la96 = 0, u8 ra10 = 0, u8 guardBtn = 0)
    {
        PsInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.status = status;
        in.raBasic1 = ra1;
        in.raBasic3 = ra3;
        in.laBit96 = la96;
        in.raBit10 = ra10;
        in.guardButton = guardBtn;
        return in;
    }

    void addPsEv(PsInput& in, s16 prev, s16 status)
    {
        PsStatusEvent& e = in.events[in.eventCount++];
        e.prev = prev;
        e.status = status;
    }
}

TEST(stage4a_hover_ps, verified_fighter_kind_ids_and_debug_block_layout)
{
    // Verified directly against lib/BrawlHeaders/Brawl/Include/ft/ft_entry.h
    // and P+ 3.2.1 CloneEngine.asm: Yoshi is 0x04 (Kirby is 0x05).
    CHECK_EQ(0x04, static_cast<int>(kDrillFtYoshi));
    CHECK_EQ(0x0A, static_cast<int>(kDrillFtNess));
    CHECK_EQ(0x0C, static_cast<int>(kDrillFtPeach));
    CHECK_EQ(0x14, static_cast<int>(kDrillFtGanondorf));
    CHECK_EQ(0x1A, static_cast<int>(kDrillFtLucas));
    CHECK_EQ(0x26, static_cast<int>(kDrillFtMewtwo));

    CHECK(isFloatCapable(kDrillFtPeach));
    CHECK(isFloatCapable(kDrillFtMewtwo));
    CHECK(isFloatCapable(kDrillFtGanondorf));
    CHECK(!isFloatCapable(kDrillFtLucas));
    CHECK(!isFloatCapable(0x00));

    CHECK(isDjcCapable(kDrillFtYoshi));
    CHECK(isDjcCapable(kDrillFtNess));
    CHECK(isDjcCapable(kDrillFtPeach));
    CHECK(isDjcCapable(kDrillFtLucas));
    CHECK(isDjcCapable(kDrillFtMewtwo));
    CHECK(!isDjcCapable(kDrillFtGanondorf));
    CHECK(!isDjcCapable(0x05)); // Kirby is not a DJC character
    CHECK(!isDjcCapable(0x00)); // Mario

    // Exact big-endian Python struct layout check for TM4P debug block.
    CHECK_EQ(0x544D3450u, static_cast<unsigned>(kHoverPsDebugMagic));
    CHECK_EQ(152u, static_cast<unsigned>(sizeof(HoverPsDebugSlot)));
    CHECK_EQ(16u, static_cast<unsigned>(offsetof(HoverPsDebugBlock, slots)));
    CHECK_EQ(16u + 8u * 152u, static_cast<unsigned>(sizeof(HoverPsDebugBlock)));
}

TEST(stage4a_hover_ps, peach_float_cancel_success_and_act_oo_fc)
{
    FloatDjcSlot st;
    floatDjcResetAll(st);

    // Frame 10: Peach enters Float (0x11E) near ground at y = 2.5.
    FloatDjcInput in = makeFd(10, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 2.5f, 0.0f);
    in.floatRem = 150;
    in.floatUsed = 1;
    addFdEv(in, kFdStJump, kFdStPeachFloat, 0x10, 0, 1);
    stepFloatDjc(in, st);
    CHECK(st.inFloat);
    CHECK(!st.floatAerialActive);
    CHECK(fabsf(st.floatEntryY - 2.5f) < 0.001f);

    // Frame 12: Starts Float Fair (subaction 0x63 inside status 0x11E) at y = 2.0.
    in = makeFd(12, kDrillFtPeach, kFdStPeachFloat, 0x63, 1, 2.0f, 0.0f);
    in.floatRem = 148;
    stepFloatDjc(in, st);
    CHECK(st.inFloat);
    CHECK(st.floatAerialActive);
    CHECK_EQ(0x63, static_cast<int>(st.floatAerialMotion));

    // Frame 15: Releases jump and drops slightly to y = 0.8 while still in 0x11E + Fair.
    in = makeFd(15, kDrillFtPeach, kFdStPeachFloat, 0x63, 1, 0.8f, -0.9f);
    in.floatRem = 0;
    stepFloatDjc(in, st);
    CHECK(fabsf(st.minFloatY - 0.8f) < 0.001f);

    // Frame 16: Lands into Landing (0x16) -> Float Cancel Success!
    in = makeFd(16, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, -0.8f);
    addFdEv(in, kFdStPeachFloat, kFdStLanding, 0x63, 0, 0);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcSuccess), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(0, static_cast<int>(st.last.fcIsHover));
    CHECK_EQ(1u, st.stats.fcAttempts);
    CHECK_EQ(1u, st.stats.fcSuccesses);
    CHECK_EQ(1u, st.stats.fcStreak);
    CHECK_EQ(3, static_cast<int>(st.last.fcAerialFrame));
    CHECK_EQ(0x63, static_cast<int>(st.last.fcMotionKind));
    CHECK(fabsf(st.last.fcEntryY - 2.5f) < 0.001f);
    CHECK(fabsf(st.last.fcMinY - 0.8f) < 0.001f);
    CHECK(st.fcActArmed);

    // Frames 17-19: 4-frame Landing lag (frames 1..3 busy, frame 4 actionable on frame 20).
    for (s32 fr = 17; fr <= 19; fr++)
    {
        in = makeFd(fr, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, 0.0f);
        stepFloatDjc(in, st);
        CHECK(st.fcActArmed);
    }

    // Frame 20: First actionable frame -> enters Wait.
    in = makeFd(20, kDrillFtPeach, kFdStWait, 0x00, 0, 0.0f, 0.0f);
    in.actionable = 1;
    addFdEv(in, kFdStLanding, kFdStWait);
    stepFloatDjc(in, st);
    CHECK(st.fcActArmed);
    CHECK_EQ(20, st.fcActFirstFrame);

    // Frame 21: Dashes on actionable frame 2 -> actOoFcFrame = 2!
    in = makeFd(21, kDrillFtPeach, kFdStDash, 0x05, 0, 0.0f, 0.0f);
    in.actionable = 1;
    addFdEv(in, kFdStWait, kFdStDash);
    stepFloatDjc(in, st);
    CHECK(!st.fcActArmed);
    CHECK_EQ(2, static_cast<int>(st.last.fcActFrame));
    CHECK_EQ(2, static_cast<int>(st.stats.bestActFrame));

    // Second FC: frame-perfect Dash directly out of Landing -> bestActFrame improves to 1!
    in = makeFd(30, kDrillFtPeach, kFdStPeachFloat, 0x62, 1, 1.5f, 0.0f);
    addFdEv(in, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in, st);
    in = makeFd(32, kDrillFtPeach, kFdStLandingLight, 0x31, 0, 0.0f, -0.5f);
    addFdEv(in, kFdStPeachFloat, kFdStLandingLight, 0x62);
    stepFloatDjc(in, st);
    CHECK_EQ(2u, st.stats.fcSuccesses);
    CHECK_EQ(2u, st.stats.fcStreak);
    in = makeFd(36, kDrillFtPeach, kFdStDash, 0x05, 0, 0.0f, 0.0f);
    in.actionable = 1;
    addFdEv(in, kFdStLandingLight, kFdStDash);
    stepFloatDjc(in, st);
    CHECK_EQ(1, static_cast<int>(st.last.fcActFrame));
    CHECK_EQ(1, static_cast<int>(st.stats.bestActFrame));
}

TEST(stage4a_hover_ps, peach_dropped_float_early_miss_full_lag)
{
    FloatDjcSlot st;
    floatDjcResetAll(st);

    // Enters float, then drops float into Fall (0x0E) before starting aerial (0x33),
    // and lands in Landing_Attack_Air (0x18) -> kFcMissFullLag!
    FloatDjcInput in = makeFd(10, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 8.0f, 0.0f);
    addFdEv(in, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in, st);

    in = makeFd(14, kDrillFtPeach, kFdStFall, 0x19, 1, 7.5f, -0.5f);
    addFdEv(in, kFdStPeachFloat, kFdStFall);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);
    CHECK_EQ(14, st.droppedFloatFrame);

    in = makeFd(16, kDrillFtPeach, kFdStAttackAir, 0x63, 1, 5.0f, -1.2f);
    addFdEv(in, kFdStFall, kFdStAttackAir, 0x19);
    stepFloatDjc(in, st);
    CHECK(st.droppedFloatAerial);

    in = makeFd(20, kDrillFtPeach, kFdStLandingAttackAir, 0x68, 0, 0.0f, -1.8f);
    addFdEv(in, kFdStAttackAir, kFdStLandingAttackAir, 0x63, 0); // missed L-cancel too
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcMissFullLag), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(0, static_cast<int>(st.last.fcLandingLCancelled));
    CHECK_EQ(1u, st.stats.fcAttempts);
    CHECK_EQ(0u, st.stats.fcSuccesses);
    CHECK_EQ(0u, st.stats.fcStreak);
}

TEST(stage4a_hover_ps, mewtwo_hover_cancel_and_miss)
{
    FloatDjcSlot st;
    floatDjcResetAll(st);

    // Mewtwo enters Hover (0x120) with LA-Bit[97] = 1, starts Hover Bair (0x64),
    // lands in Landing (0x16) -> kFcSuccess with fcIsHover = 1!
    FloatDjcInput in = makeFd(10, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 3.0f, 0.0f);
    in.hoverActive = 1;
    in.floatUsed = 1;
    in.floatRem = 60;
    addFdEv(in, kFdStJump, kFdStMewtwoHover);
    stepFloatDjc(in, st);
    CHECK(st.inFloat);
    CHECK(st.isHover);

    in = makeFd(13, kDrillFtMewtwo, kFdStMewtwoHover, 0x64, 1, 2.2f, -0.3f);
    in.hoverActive = 1;
    in.floatRem = 57;
    stepFloatDjc(in, st);
    CHECK(st.floatAerialActive);

    in = makeFd(17, kDrillFtMewtwo, kFdStLanding, 0x30, 0, 0.0f, -0.8f);
    addFdEv(in, kFdStMewtwoHover, kFdStLanding, 0x64);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcSuccess), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(1, static_cast<int>(st.last.fcIsHover));
    CHECK_EQ(1u, st.stats.fcSuccesses);
    CHECK_EQ(4, static_cast<int>(st.last.fcAerialFrame));
    CHECK_EQ(0x64, static_cast<int>(st.last.fcMotionKind));

    // Mewtwo drops hover into 0x11E (HoverExit) -> 0x0E (Fall) -> 0x33 -> 0x18 (missed HC).
    in = makeFd(40, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 10.0f, 0.0f);
    in.hoverActive = 1;
    in.floatRem = 60;
    addFdEv(in, kFdStJump, kFdStMewtwoHover);
    stepFloatDjc(in, st);

    in = makeFd(43, kDrillFtMewtwo, kFdStMewtwoHoverExit, 0x1C, 1, 9.5f, -0.5f);
    addFdEv(in, kFdStMewtwoHover, kFdStMewtwoHoverExit);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);
    CHECK_EQ(43, st.droppedFloatFrame);

    in = makeFd(45, kDrillFtMewtwo, kFdStFall, 0x19, 1, 8.0f, -1.0f);
    addFdEv(in, kFdStMewtwoHoverExit, kFdStFall);
    stepFloatDjc(in, st);
    CHECK_EQ(43, st.droppedFloatFrame);

    in = makeFd(47, kDrillFtMewtwo, kFdStAttackAir, 0x62, 1, 5.0f, -1.5f);
    addFdEv(in, kFdStFall, kFdStAttackAir);
    stepFloatDjc(in, st);

    in = makeFd(51, kDrillFtMewtwo, kFdStLandingAttackAir, 0x67, 0, 0.0f, -2.0f);
    addFdEv(in, kFdStAttackAir, kFdStLandingAttackAir, 0x62, 5); // L-cancelled miss
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcMissFullLag), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(1, static_cast<int>(st.last.fcLandingLCancelled));
    CHECK_EQ(2u, st.stats.fcAttempts);
    CHECK_EQ(1u, st.stats.fcSuccesses);
    CHECK_EQ(0u, st.stats.fcStreak);
}

TEST(stage4a_hover_ps, djc_instant_low_rising_late_and_landing_cleanliness)
{
    FloatDjcSlot st;
    floatDjcResetAll(st);

    // 1. Lucas Instant DJC (Frame 2: Jump_Aerial on f=10 -> Attack_Air on f=11) + L-cancel ok.
    FloatDjcInput in = makeFd(10, kDrillFtLucas, kFdStJumpAerial, 0x13, 1, 4.0f, 1.8f);
    in.djcBit = 1;
    addFdEv(in, kFdStJump, kFdStJumpAerial);
    stepFloatDjc(in, st);
    CHECK(st.inJumpAerial);

    in = makeFd(11, kDrillFtLucas, kFdStAttackAir, 0x63, 1, 4.2f, 0.2f);
    in.djcBit = 1;
    addFdEv(in, kFdStJumpAerial, kFdStAttackAir, 0x13);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kDjcInstant), static_cast<int>(st.last.djcGrade));
    CHECK_EQ(2, static_cast<int>(st.last.djcFrame));
    CHECK_EQ(1, static_cast<int>(st.last.djcHadBit33));
    CHECK(fabsf(st.last.djcVyBefore - 1.8f) < 0.001f);
    CHECK(fabsf(st.last.djcHeight - 4.2f) < 0.001f);

    // Frame 12: post-DJC gravity drops vy to -0.6 while still airborne in Attack_Air.
    in = makeFd(12, kDrillFtLucas, kFdStAttackAir, 0x63, 1, 3.6f, -0.6f);
    stepFloatDjc(in, st);
    CHECK(fabsf(st.last.djcVyAfter + 0.6f) < 0.001f);

    // Frame 16: Lands in Landing_Attack_Air with L-cancel counter > 0 -> clean!
    in = makeFd(16, kDrillFtLucas, kFdStLandingAttackAir, 0x68, 0, 0.0f, -1.5f);
    addFdEv(in, kFdStAttackAir, kFdStLandingAttackAir, 0x63, 6);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kDjcLandLCancelOk), static_cast<int>(st.last.djcLandOutcome));
    CHECK_EQ(1, static_cast<int>(st.last.djcLandedClean));
    CHECK_EQ(1u, st.stats.djcAttempts);
    CHECK_EQ(1u, st.stats.djcInstant);
    CHECK_EQ(1u, st.stats.djcStreak);

    // 2. Ness same-frame Instant DJC (Frame 1) + autocancel landing (0x16).
    FloatDjcSlot stNess;
    floatDjcResetAll(stNess);
    in = makeFd(20, kDrillFtNess, kFdStAttackAir, 0x62, 1, 3.0f, -0.2f);
    in.djcBit = 1;
    addFdEv(in, kFdStJump, kFdStJumpAerial);
    addFdEv(in, kFdStJumpAerial, kFdStAttackAir, 0x13);
    stepFloatDjc(in, stNess);
    CHECK_EQ(static_cast<int>(kDjcInstant), static_cast<int>(stNess.last.djcGrade));
    CHECK_EQ(1, static_cast<int>(stNess.last.djcFrame));
    in = makeFd(28, kDrillFtNess, kFdStLanding, 0x30, 0, 0.0f, -1.0f);
    addFdEv(in, kFdStAttackAir, kFdStLanding, 0x62, 0);
    stepFloatDjc(in, stNess);
    CHECK_EQ(static_cast<int>(kDjcLandAutocancel), static_cast<int>(stNess.last.djcLandOutcome));
    CHECK_EQ(1, static_cast<int>(stNess.last.djcLandedClean));

    // 3. Yoshi (0x04!) Low-Rising DJC (Frame 5) + missed L-cancel, then Late DJC (Frame 9).
    FloatDjcSlot stYoshi;
    floatDjcResetAll(stYoshi);
    in = makeFd(30, kDrillFtYoshi, kFdStJumpAerial, 0x13, 1, 5.0f, 1.2f);
    in.djcBit = 1;
    addFdEv(in, kFdStJump, kFdStJumpAerial);
    stepFloatDjc(in, stYoshi);
    in = makeFd(34, kDrillFtYoshi, kFdStAttackAir, 0x65, 1, 10.0f, -0.3f);
    addFdEv(in, kFdStJumpAerial, kFdStAttackAir, 0x13);
    stepFloatDjc(in, stYoshi);
    CHECK_EQ(static_cast<int>(kDjcLowRising), static_cast<int>(stYoshi.last.djcGrade));
    CHECK_EQ(5, static_cast<int>(stYoshi.last.djcFrame));
    in = makeFd(42, kDrillFtYoshi, kFdStLandingAttackAir, 0x6A, 0, 0.0f, -1.8f);
    addFdEv(in, kFdStAttackAir, kFdStLandingAttackAir, 0x65, 0);
    stepFloatDjc(in, stYoshi);
    CHECK_EQ(static_cast<int>(kDjcLandLCancelMiss), static_cast<int>(stYoshi.last.djcLandOutcome));
    CHECK_EQ(0, static_cast<int>(stYoshi.last.djcLandedClean));

    in = makeFd(60, kDrillFtYoshi, kFdStJumpAerial, 0x13, 1, 5.0f, 1.5f);
    addFdEv(in, kFdStJump, kFdStJumpAerial);
    stepFloatDjc(in, stYoshi);
    in = makeFd(68, kDrillFtYoshi, kFdStAttackAir, 0x62, 1, 18.0f, 0.4f);
    addFdEv(in, kFdStJumpAerial, kFdStAttackAir, 0x13);
    stepFloatDjc(in, stYoshi);
    CHECK_EQ(static_cast<int>(kDjcLate), static_cast<int>(stYoshi.last.djcGrade));
    CHECK_EQ(9, static_cast<int>(stYoshi.last.djcFrame));
    CHECK_EQ(2u, stYoshi.stats.djcAttempts);
    CHECK_EQ(0u, stYoshi.stats.djcInstant);
}

TEST(stage4a_hover_ps, powershield_physical_frame1_frame2_and_5f_ps_drop_punish)
{
    PsSlot st;
    psResetAll(st);

    // Frame 10: Fresh Guard_On Frame 1 (RA-Basic[1] = 2), hit physically on Frame 2!
    PsInput in = makePs(10, kPsStGuardOn, 2, 0, 0, 0, 1);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    CHECK(st.guardArmed);
    CHECK_EQ(1u, static_cast<unsigned>(st.guardFrames));

    // Frame 11: Guard_On -> Guard_Damage with LA-Bit[96] == 1 and RA-Basic[3] == 5!
    in = makePs(11, kPsStGuardDamage, 1, 5, 1, 0, 1);
    in.shieldReceived = 1;
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsPhysical), static_cast<int>(st.last.outcome));
    CHECK_EQ(2, static_cast<int>(st.last.windowFrame));
    CHECK_EQ(0, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(0, static_cast<int>(st.last.rePs));
    CHECK_EQ(0, static_cast<int>(st.last.lightToHard));
    CHECK_EQ(1u, st.stats.physicalHits);
    CHECK_EQ(1u, st.stats.streak);

    // Frames 12-14: Shieldstun in Guard_Damage (RA-Basic[3] = 5).
    for (s32 fr = 12; fr <= 14; fr++)
    {
        in = makePs(fr, kPsStGuardDamage, 0, 5, 1, 0, 1);
        stepPowershield(in, st);
    }

    // Frame 15: Guard_Damage ends into Guard_Off AND cancels same-frame into Forward Tilt (0x27)!
    in = makePs(15, 0x27, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStGuardDamage, kPsStGuardOff);
    addPsEv(in, kPsStGuardOff, 0x27);
    stepPowershield(in, st);
    CHECK_EQ(1, static_cast<int>(st.last.psDropUsed));
    CHECK_EQ(1, static_cast<int>(st.last.psDropActFrame)); // frame-perfect 1f!
    CHECK_EQ(1u, st.stats.psDropCancels);

    // Same-frame Frame-1 Physical PS (Wait -> Guard_On -> Guard_Damage in 1 frame),
    // followed by 2f Guard hold (RA-Basic[3]: 5->4->3) -> Guard_Off -> Attack on frame 4!
    in = makePs(30, kPsStGuardDamage, 2, 5, 1, 0, 1);
    in.shieldReceived = 1;
    addPsEv(in, kPsStWait, kPsStGuardOn);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsPhysical), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.windowFrame));
    CHECK_EQ(2u, st.stats.physicalHits);
    CHECK_EQ(2u, st.stats.streak);

    in = makePs(31, kPsStGuard, 0, 4, 1, 0, 1);
    addPsEv(in, kPsStGuardDamage, kPsStGuard);
    stepPowershield(in, st);
    in = makePs(32, kPsStGuard, 0, 3, 1, 0, 1);
    stepPowershield(in, st);
    in = makePs(33, kPsStGuardOff, 0, 3, 1, 0, 0);
    addPsEv(in, kPsStGuard, kPsStGuardOff);
    stepPowershield(in, st);
    in = makePs(34, 0x24, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStGuardOff, 0x24);
    stepPowershield(in, st);
    CHECK_EQ(1, static_cast<int>(st.last.psDropUsed));
    CHECK_EQ(4, static_cast<int>(st.last.psDropActFrame));
    CHECK_EQ(2u, st.stats.psDropCancels);
}

TEST(stage4a_hover_ps, powershield_reflect_and_frame2_light_to_hard_frame3)
{
    PsSlot st;
    psResetAll(st);

    // 1. Fresh Frame-1 Reflect PS (RA-Bit[10] == 1 in Guard_On, no Guard_Damage).
    PsInput in = makePs(10, kPsStGuardOn, 2, 0, 0, 1, 1);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsReflect), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.windowFrame));
    CHECK_EQ(0, static_cast<int>(st.last.lightToHard));
    CHECK_EQ(1u, st.stats.reflectHits);
    CHECK(!st.guardArmed);

    // 2. Frame-2 Light-to-Hard press reflecting a laser on Frame 3!
    // Matches P+ 3.2.1 PowerShield.asm: RA-Basic[1] = [0, 2, 1, 0].
    in = makePs(20, kPsStGuardOn, 0, 0, 0, 0, 0); // Frame 1: light shield only
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    CHECK(st.guardArmed);
    CHECK(!st.lightToHard);

    in = makePs(21, kPsStGuardOn, 2, 0, 0, 0, 1); // Frame 2: fresh hard digital L press!
    stepPowershield(in, st);
    CHECK(st.guardArmed);
    CHECK(st.lightToHard);
    CHECK_EQ(3u, static_cast<unsigned>(st.windowEnd));

    in = makePs(22, kPsStGuardOn, 1, 0, 0, 1, 1); // Frame 3: RA-Bit[10] == 1 reflect!
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsReflect), static_cast<int>(st.last.outcome));
    CHECK_EQ(3, static_cast<int>(st.last.windowFrame));
    CHECK_EQ(1, static_cast<int>(st.last.lightToHard));
    CHECK_EQ(2u, st.stats.reflectHits);
    CHECK_EQ(1u, st.stats.lightToHardHits);
    CHECK_EQ(2u, st.stats.streak);

    // 3. Frame-2 Light-to-Hard press with hit on Frame 4 -> Early Miss -1f (NOT -2f!).
    in = makePs(40, kPsStGuardOn, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    in = makePs(41, kPsStGuardOn, 2, 0, 0, 0, 1);
    stepPowershield(in, st);
    in = makePs(42, kPsStGuardOn, 1, 0, 0, 0, 1);
    stepPowershield(in, st);
    in = makePs(43, kPsStGuardDamage, 0, 0, 0, 0, 1);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissEarly), static_cast<int>(st.last.outcome));
    CHECK_EQ(-1, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(1, static_cast<int>(st.last.lightToHard));
    CHECK_EQ(1u, st.stats.earlyMisses);
    CHECK_EQ(0u, st.stats.streak);
}

TEST(stage4a_hover_ps, powershield_chained_re_ps_and_all_miss_categories)
{
    PsSlot st;
    psResetAll(st);

    // Physical PS -> PS-Drop into Guard_On (0x1C -> 0x1A) -> Chained Re-PS!
    PsInput in = makePs(10, kPsStGuardDamage, 2, 5, 1, 0, 1);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(1u, st.stats.physicalHits);

    in = makePs(14, kPsStGuardOn, 2, 0, 0, 0, 1);
    addPsEv(in, kPsStGuardDamage, kPsStGuardOff);
    addPsEv(in, kPsStGuardOff, kPsStGuardOn);
    stepPowershield(in, st);
    CHECK_EQ(1, static_cast<int>(st.last.psDropUsed));
    CHECK(st.psDropPendingRePs);
    CHECK(st.guardRePs);

    in = makePs(15, kPsStGuardDamage, 1, 5, 1, 0, 1);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsPhysical), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.rePs));
    CHECK_EQ(1, static_cast<int>(st.last.psDropChainedRePs));
    CHECK_EQ(1u, st.stats.rePsHits);
    CHECK_EQ(2u, st.stats.streak);

    // Buffered shield miss: Guard_On Frame 1 hit with LA-Bit[96] == 0.
    in = makePs(30, kPsStGuardDamage, 0, 0, 0, 0, 1);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissBuffered), static_cast<int>(st.last.outcome));
    CHECK_EQ(1u, st.stats.bufferedMisses);
    CHECK_EQ(0u, st.stats.streak);

    // Early miss: normal Guard_On held 5 frames (Frame 5 -> -3f early).
    in = makePs(40, kPsStGuardOn, 2, 0, 0, 0, 1);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    for (s32 fr = 41; fr <= 43; fr++)
    {
        in = makePs(fr, kPsStGuardOn, fr == 41 ? 1 : 0, 0, 0, 0, 1);
        stepPowershield(in, st);
    }
    in = makePs(44, kPsStGuardDamage, 0, 0, 0, 0, 1);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissEarly), static_cast<int>(st.last.outcome));
    CHECK_EQ(-3, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(1u, st.stats.earlyMisses);

    // Late miss during hitlag (digital rising edge 2f after hit).
    in = makePs(60, kPsStDamageFirst, 0, 0, 0, 0, 0);
    in.curHitlag = 5;
    addPsEv(in, kPsStWait, kPsStDamageFirst);
    stepPowershield(in, st);
    CHECK(st.lateMissArmed);

    in = makePs(61, kPsStDamageFirst, 0, 0, 0, 0, 0);
    in.curHitlag = 4;
    in.prevHitlag = 5;
    stepPowershield(in, st);

    in = makePs(62, kPsStDamageFirst, 0, 0, 0, 0, 1); // fresh digital guard press during hitlag!
    in.curHitlag = 3;
    in.prevHitlag = 4;
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(2, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(1u, st.stats.lateMisses);

    // Late miss via fresh analog L/R press (LA-Basic[77] == 1) 1f after hit.
    in = makePs(80, 0x45, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStWait, 0x45);
    stepPowershield(in, st);
    in = makePs(81, 0x45, 0, 0, 0, 0, 0);
    in.laBasic77 = 1;
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissLate), static_cast<int>(st.last.outcome));
    CHECK_EQ(1, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(2u, st.stats.lateMisses);
}

TEST(stage4a_hover_ps, multi_fighter_independence_and_savestate_transient_reset)
{
    FloatDjcSlot fd[4];
    PsSlot ps[4];
    for (int i = 0; i < 4; i++)
    {
        floatDjcResetAll(fd[i]);
        psResetAll(ps[i]);
    }

    // Slot 0 (Peach) floats & starts aerial; Slot 2 (Lucas) enters Jump_Aerial;
    // Slot 3 (Mario) arms Guard_On. Verify complete independence across all 4 slots.
    FloatDjcInput in0 = makeFd(10, kDrillFtPeach, kFdStPeachFloat, 0x62, 1, 2.0f, 0.0f);
    addFdEv(in0, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in0, fd[0]);

    FloatDjcInput in2 = makeFd(10, kDrillFtLucas, kFdStJumpAerial, 0x13, 1, 3.0f, 1.0f);
    addFdEv(in2, kFdStJump, kFdStJumpAerial);
    stepFloatDjc(in2, fd[2]);

    PsInput p3 = makePs(10, kPsStGuardOn, 2, 0, 0, 0, 1);
    addPsEv(p3, kPsStWait, kPsStGuardOn);
    stepPowershield(p3, ps[3]);

    CHECK(fd[0].inFloat && fd[0].floatAerialActive);
    CHECK(!fd[1].inFloat && !fd[2].inFloat && !fd[3].inFloat);
    CHECK(fd[2].inJumpAerial && !fd[0].inJumpAerial);
    CHECK(ps[3].guardArmed && !ps[0].guardArmed);

    // Record 1 success on Slot 0 and Slot 3 before savestate reset.
    in0 = makeFd(12, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, -0.5f);
    addFdEv(in0, kFdStPeachFloat, kFdStLanding, 0x62);
    stepFloatDjc(in0, fd[0]);
    CHECK_EQ(1u, fd[0].stats.fcSuccesses);

    p3 = makePs(11, kPsStGuardOn, 1, 0, 0, 1, 1);
    stepPowershield(p3, ps[3]);
    CHECK_EQ(1u, ps[3].stats.reflectHits);

    // Re-arm transient state on Slot 0, Slot 2, Slot 3, then fire savestate reset!
    in0 = makeFd(20, kDrillFtPeach, kFdStPeachFloat, 0x63, 1, 2.0f, 0.0f);
    addFdEv(in0, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in0, fd[0]);
    p3 = makePs(20, kPsStGuardOn, 2, 0, 0, 0, 1);
    addPsEv(p3, kPsStWait, kPsStGuardOn);
    stepPowershield(p3, ps[3]);
    CHECK(fd[0].inFloat && fd[2].inJumpAerial && ps[3].guardArmed);

    for (int i = 0; i < 4; i++)
    {
        floatDjcResetTransient(fd[i]);
        psResetTransient(ps[i]);
    }
    // Transient armed flags cleared; session stats preserved!
    CHECK(!fd[0].inFloat && !fd[0].floatAerialActive && !fd[2].inJumpAerial && !ps[3].guardArmed);
    CHECK_EQ(1u, fd[0].stats.fcSuccesses);
    CHECK_EQ(1u, ps[3].stats.reflectHits);

    // A landing right after savestate load must NOT spuriously trigger kFcSuccess!
    in0 = makeFd(1, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, 0.0f);
    addFdEv(in0, kFdStPeachFloat, kFdStLanding, 0x63);
    stepFloatDjc(in0, fd[0]);
    CHECK_EQ(1u, fd[0].stats.fcSuccesses);
}

TEST(stage4a_hover_ps, float_aerial_midair_completion_clears_and_second_aerial_records_fresh)
{
    // 1. Peach: float aerial finishes in midair back to float idle (0x1DE), then
    // lands in idle float -> must NOT credit kFcSuccess!
    FloatDjcSlot peach;
    floatDjcResetAll(peach);

    FloatDjcInput in = makeFd(10, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 6.0f, 0.0f);
    addFdEv(in, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in, peach);

    in = makeFd(12, kDrillFtPeach, kFdStPeachFloat, 0x62, 1, 6.0f, 0.0f); // Nair starts on float frame 3
    stepFloatDjc(in, peach);
    CHECK(peach.floatAerialActive);
    CHECK_EQ(0x62, static_cast<int>(peach.floatAerialMotion));

    in = makeFd(30, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 5.0f, 0.0f); // Nair finished -> idle float
    stepFloatDjc(in, peach);
    CHECK(!peach.floatAerialActive);
    CHECK_EQ(-1, static_cast<int>(peach.floatAerialMotion));
    CHECK_EQ(kFdNoFrame, peach.floatAerialStartFrame);

    in = makeFd(34, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, -0.5f); // Idle float landing
    addFdEv(in, kFdStPeachFloat, kFdStLanding, 0x1DE);
    stepFloatDjc(in, peach);
    CHECK_EQ(0u, peach.stats.fcAttempts);
    CHECK_EQ(0u, peach.stats.fcSuccesses);

    // 2. Peach: first float aerial completes in midair -> idle -> second float aerial (Fair 0x63)
    // lands -> must record the SECOND aerial's motion (0x63) and aerial frame (65 - 50 + 1 = 16)!
    in = makeFd(50, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 5.0f, 0.0f);
    addFdEv(in, kFdStJump, kFdStPeachFloat);
    stepFloatDjc(in, peach);

    in = makeFd(52, kDrillFtPeach, kFdStPeachFloat, 0x62, 1, 5.0f, 0.0f); // 1st aerial: Nair (float frame 3)
    stepFloatDjc(in, peach);
    in = makeFd(60, kDrillFtPeach, kFdStPeachFloat, 0x1DE, 1, 3.0f, 0.0f); // back to idle float
    stepFloatDjc(in, peach);
    CHECK(!peach.floatAerialActive);

    in = makeFd(65, kDrillFtPeach, kFdStPeachFloat, 0x63, 1, 2.0f, 0.0f); // 2nd aerial: Fair (float frame 16)
    stepFloatDjc(in, peach);
    CHECK(peach.floatAerialActive);
    CHECK_EQ(0x63, static_cast<int>(peach.floatAerialMotion));

    in = makeFd(68, kDrillFtPeach, kFdStLanding, 0x30, 0, 0.0f, -0.6f);
    addFdEv(in, kFdStPeachFloat, kFdStLanding, 0x63);
    stepFloatDjc(in, peach);
    CHECK_EQ(1u, peach.stats.fcAttempts);
    CHECK_EQ(1u, peach.stats.fcSuccesses);
    CHECK_EQ(static_cast<int>(kFcSuccess), static_cast<int>(peach.last.fcOutcome));
    CHECK_EQ(0x63, static_cast<int>(peach.last.fcMotionKind));
    CHECK_EQ(16, static_cast<int>(peach.last.fcAerialFrame));

    // 3. Mewtwo: hover aerial completes in midair back to hover idle (0x1E4) -> idle landing gives
    // 0 attempts, and second hover aerial (Bair 0x64) records second aerial's motion & frame!
    FloatDjcSlot m2;
    floatDjcResetAll(m2);

    in = makeFd(10, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 6.0f, 0.0f);
    in.hoverActive = 1;
    addFdEv(in, kFdStJump, kFdStMewtwoHover);
    stepFloatDjc(in, m2);

    in = makeFd(12, kDrillFtMewtwo, kFdStMewtwoHover, 0x66, 1, 6.0f, 0.0f); // Dair
    in.hoverActive = 1;
    stepFloatDjc(in, m2);
    in = makeFd(28, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 3.0f, 0.0f); // Idle hover
    in.hoverActive = 1;
    stepFloatDjc(in, m2);
    CHECK(!m2.floatAerialActive);

    in = makeFd(31, kDrillFtMewtwo, kFdStLanding, 0x30, 0, 0.0f, -0.5f); // Idle hover landing
    addFdEv(in, kFdStMewtwoHover, kFdStLanding, 0x1E4);
    stepFloatDjc(in, m2);
    CHECK_EQ(0u, m2.stats.fcAttempts);
    CHECK_EQ(0u, m2.stats.fcSuccesses);

    in = makeFd(40, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 5.0f, 0.0f);
    in.hoverActive = 1;
    addFdEv(in, kFdStJump, kFdStMewtwoHover);
    stepFloatDjc(in, m2);
    in = makeFd(42, kDrillFtMewtwo, kFdStMewtwoHover, 0x62, 1, 4.0f, 0.0f); // 1st aerial: Nair
    in.hoverActive = 1;
    stepFloatDjc(in, m2);
    in = makeFd(50, kDrillFtMewtwo, kFdStMewtwoHover, 0x1E4, 1, 2.5f, 0.0f); // back to idle hover
    in.hoverActive = 1;
    stepFloatDjc(in, m2);
    in = makeFd(54, kDrillFtMewtwo, kFdStMewtwoHover, 0x64, 1, 1.5f, 0.0f); // 2nd aerial: Bair (hover frame 15)
    in.hoverActive = 1;
    stepFloatDjc(in, m2);
    in = makeFd(57, kDrillFtMewtwo, kFdStLanding, 0x30, 0, 0.0f, -0.5f);
    addFdEv(in, kFdStMewtwoHover, kFdStLanding, 0x64);
    stepFloatDjc(in, m2);
    CHECK_EQ(1u, m2.stats.fcAttempts);
    CHECK_EQ(1u, m2.stats.fcSuccesses);
    CHECK_EQ(1, static_cast<int>(m2.last.fcIsHover));
    CHECK_EQ(0x64, static_cast<int>(m2.last.fcMotionKind));
    CHECK_EQ(15, static_cast<int>(m2.last.fcAerialFrame));
}

TEST(stage4a_hover_ps, powershield_frame3_light_to_hard_non_ps_hit_is_buffered_not_zero_early)
{
    PsSlot st;
    psResetAll(st);

    // Frame 1: light shield (RA-Basic[1] = 0).
    PsInput in = makePs(10, kPsStGuardOn, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    CHECK(st.guardArmed);

    // Frame 2: fresh digital hard press -> lightToHard = true, windowEnd = 3.
    in = makePs(11, kPsStGuardOn, 2, 0, 0, 0, 1);
    stepPowershield(in, st);
    CHECK(st.lightToHard);
    CHECK_EQ(3u, static_cast<unsigned>(st.windowEnd));

    // Frame 3: Guard_Damage with LA-Bit[96] == 0 -> must classify as kPsMissBuffered
    // (NOT kPsMissEarly with offsetFrames == 0)!
    in = makePs(12, kPsStGuardDamage, 0, 0, 0, 0, 1);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissBuffered), static_cast<int>(st.last.outcome));
    CHECK_EQ(0, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(3, static_cast<int>(st.last.windowFrame));
    CHECK_EQ(1u, st.stats.bufferedMisses);
    CHECK_EQ(0u, st.stats.earlyMisses);

    // Contrast: Frame 4 non-PS hit under light-to-hard IS kPsMissEarly with offsetFrames == -1.
    in = makePs(30, kPsStGuardOn, 0, 0, 0, 0, 0);
    addPsEv(in, kPsStWait, kPsStGuardOn);
    stepPowershield(in, st);
    in = makePs(31, kPsStGuardOn, 2, 0, 0, 0, 1);
    stepPowershield(in, st);
    in = makePs(32, kPsStGuardOn, 1, 0, 0, 0, 1);
    stepPowershield(in, st);
    in = makePs(33, kPsStGuardDamage, 0, 0, 0, 0, 1);
    addPsEv(in, kPsStGuardOn, kPsStGuardDamage);
    stepPowershield(in, st);
    CHECK_EQ(static_cast<int>(kPsMissEarly), static_cast<int>(st.last.outcome));
    CHECK_EQ(-1, static_cast<int>(st.last.offsetFrames));
    CHECK_EQ(1u, st.stats.earlyMisses);
}

TEST(stage4a_hover_ps, ganondorf_dead_mans_float_cancel_and_grounded_warlock_punch_ignored)
{
    FloatDjcSlot st;
    floatDjcResetAll(st);

    // 1. Grounded Warlock Punch (status 0x112 with airborne == 0) even if drifting
    // off a platform later must NOT be graded as Dead Man's Float.
    FloatDjcInput in = makeFd(10, kDrillFtGanondorf, kFdStSpecialN, 0x1BE, 0, 0.0f, 0.0f);
    addFdEv(in, kFdStWait, kFdStSpecialN, 0x00, 0, 0);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);
    CHECK(st.groundedFloatStatus);

    in = makeFd(15, kDrillFtGanondorf, kFdStSpecialN, 0x1BE, 1, 2.0f, -0.5f);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);

    in = makeFd(20, kDrillFtGanondorf, kFdStLanding, 0x30, 0, 0.0f, -1.0f);
    addFdEv(in, kFdStSpecialN, kFdStLanding, 0x1BE, 0, 0);
    stepFloatDjc(in, st);
    CHECK_EQ(0u, st.stats.fcAttempts);
    CHECK_EQ(0u, st.stats.fcSuccesses);

    // 2. Airborne Dead Man's Float (0x112, airborne == 1) -> AttackAir (0x33) -> Landing (0x16) = Float Cancel Success!
    in = makeFd(30, kDrillFtGanondorf, kFdStSpecialN, 0x1BF, 1, 3.0f, 0.0f);
    addFdEv(in, kFdStJump, kFdStSpecialN, 0x10, 0, 1);
    stepFloatDjc(in, st);
    CHECK(st.inFloat);
    CHECK(!st.groundedFloatStatus);
    CHECK(!st.isHover);

    in = makeFd(35, kDrillFtGanondorf, kFdStAttackAir, 0x62, 1, 1.8f, -0.4f);
    addFdEv(in, kFdStSpecialN, kFdStAttackAir, 0x1BF, 0, 1);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);
    CHECK(st.floatAerialVia33);
    CHECK_EQ(0x62, static_cast<int>(st.floatAerialMotion));

    in = makeFd(38, kDrillFtGanondorf, kFdStLanding, 0x30, 0, 0.0f, -1.0f);
    addFdEv(in, kFdStAttackAir, kFdStLanding, 0x62, 0, 0);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcSuccess), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(0, static_cast<int>(st.last.fcIsHover));
    CHECK_EQ(1u, st.stats.fcAttempts);
    CHECK_EQ(1u, st.stats.fcSuccesses);
    CHECK_EQ(1u, st.stats.fcStreak);

    // 3. Dropped Dead Man's Float -> Fall (0x0E) -> AttackAir (0x33) -> Landing_Attack_Air (0x18) = Full Lag Miss.
    in = makeFd(50, kDrillFtGanondorf, kFdStSpecialN, 0x1BF, 1, 8.0f, 0.0f);
    addFdEv(in, kFdStJump, kFdStSpecialN, 0x10, 0, 1);
    stepFloatDjc(in, st);
    CHECK(st.inFloat);

    in = makeFd(54, kDrillFtGanondorf, kFdStFall, 0x19, 1, 7.2f, -0.6f);
    addFdEv(in, kFdStSpecialN, kFdStFall, 0x1BF, 0, 1);
    stepFloatDjc(in, st);
    CHECK(!st.inFloat);
    CHECK_EQ(54, st.droppedFloatFrame);

    in = makeFd(56, kDrillFtGanondorf, kFdStAttackAir, 0x63, 1, 4.5f, -1.2f);
    addFdEv(in, kFdStFall, kFdStAttackAir, 0x19, 0, 1);
    stepFloatDjc(in, st);
    CHECK(st.droppedFloatAerial);

    in = makeFd(60, kDrillFtGanondorf, kFdStLandingAttackAir, 0x68, 0, 0.0f, -1.8f);
    addFdEv(in, kFdStAttackAir, kFdStLandingAttackAir, 0x63, 0, 0);
    stepFloatDjc(in, st);
    CHECK_EQ(static_cast<int>(kFcMissFullLag), static_cast<int>(st.last.fcOutcome));
    CHECK_EQ(2u, st.stats.fcAttempts);
    CHECK_EQ(1u, st.stats.fcSuccesses);
    CHECK_EQ(0u, st.stats.fcStreak);
}
