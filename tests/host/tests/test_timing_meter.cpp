// Host unit tests for the Overhead Timing Helper Meter (extra/timing_meter_core.h)
// and its menu row (OSDs > Overhead Timing Bar).
#include <string.h>

#include "extra/menu_descriptions.h"
#include "extra/timing_meter_core.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/ui/lab_menu.h"
#include "main/ui/pages/pages.h"

using namespace tm;
using namespace tm::extra;

namespace {
    TimingStepInput makeInput(s32 frame, s16 status, s16 prev, u8 mode)
    {
        TimingStepInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.status = status;
        in.prevStatus = prev;
        in.actIn = -1;
        in.mode = mode;
        return in;
    }

    void addEv(TimingStepInput& in, s16 status, s16 prev, s16 lCancel = 0)
    {
        if (in.eventCount < kTmMaxEvents)
        {
            TimingStatusEv& e = in.events[in.eventCount++];
            e.status = status;
            e.prev = prev;
            e.lCancel = lCancel;
        }
    }
}

TEST(timing_meter, lcancel_hit_early_miss_and_autocancel)
{
    TimingSlotState st;
    tmResetSlot(st);

    // 1. Clean L-cancel hit on frame 3/7 (LA-Basic[90] = 5 on 0x33 -> 0x18).
    TimingStepInput f1 = makeInput(10, kTmStAttackAir, kTmStJump, kTmModeLCancel);
    addEv(f1, kTmStAttackAir, kTmStJump, 0);
    tmStepSlot(st, f1);
    CHECK_EQ(static_cast<int>(kTmPhaseActive), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmKindLCancel), static_cast<int>(st.view.kind));
    CHECK_EQ(1, static_cast<int>(st.view.hasZone));

    // Press L -> LA-Basic[90] = 7.
    TimingStepInput f2 = makeInput(11, kTmStAttackAir, kTmStAttackAir, kTmModeLCancel);
    f2.lCancelCounter = 7;
    tmStepSlot(st, f2);
    CHECK_EQ(static_cast<int>(kTmPhaseActive), static_cast<int>(st.view.phase));
    CHECK_EQ(kTmColGreen, st.view.fillColor);
    CHECK_NEAR(1.0f, st.view.ratio, 0.01f);

    // Land on frame 3 of the 7f window (LA-Basic[90] = 5 -> pressFrame = 8 - 5 = 3).
    TimingStepInput f3 = makeInput(13, kTmStLandingAttackAir, kTmStAttackAir, kTmModeLCancel);
    addEv(f3, kTmStLandingAttackAir, kTmStAttackAir, 5);
    tmStepSlot(st, f3);
    CHECK_EQ(static_cast<int>(kTmPhaseHold), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmKindLCancel), static_cast<int>(st.view.kind));
    CHECK_EQ(static_cast<int>(kTmOutcomeHit), static_cast<int>(st.lastOutcome));
    CHECK_EQ(3, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmHoldFrames, static_cast<int>(st.holdFrames));
    CHECK_EQ(kTmColGreen, st.view.fillColor);
    CHECK_EQ(1u, st.lCancelHits);

    // 2. Early press that expires before landing -> Amber (kTmOutcomeEarly).
    tmResetSlot(st);
    TimingStepInput e1 = makeInput(20, kTmStAttackAir, kTmStFall, kTmModeLCancel);
    addEv(e1, kTmStAttackAir, kTmStFall, 0);
    e1.lCancelCounter = 7;
    tmStepSlot(st, e1);

    TimingStepInput e2 = makeInput(28, kTmStAttackAir, kTmStAttackAir, kTmModeLCancel);
    e2.lCancelCounter = -1;
    tmStepSlot(st, e2);
    CHECK_EQ(kTmColAmber, st.view.fillColor);

    TimingStepInput e3 = makeInput(29, kTmStLandingAttackAir, kTmStAttackAir, kTmModeLCancel);
    addEv(e3, kTmStLandingAttackAir, kTmStAttackAir, -2);
    tmStepSlot(st, e3);
    CHECK_EQ(static_cast<int>(kTmOutcomeEarly), static_cast<int>(st.lastOutcome));
    CHECK_EQ(-3, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmColAmber, st.view.fillColor);

    // 3. Missed L-cancel with no press -> Red (kTmOutcomeMiss).
    tmResetSlot(st);
    TimingStepInput m1 = makeInput(40, kTmStAttackAir, kTmStFall, kTmModeLCancel);
    addEv(m1, kTmStAttackAir, kTmStFall, 0);
    tmStepSlot(st, m1);

    TimingStepInput m2 = makeInput(45, kTmStLandingAttackAir, kTmStAttackAir, kTmModeLCancel);
    addEv(m2, kTmStLandingAttackAir, kTmStAttackAir, 0);
    tmStepSlot(st, m2);
    CHECK_EQ(static_cast<int>(kTmOutcomeMiss), static_cast<int>(st.lastOutcome));
    CHECK_EQ(0, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmColRed, st.view.fillColor);

    // 4. Autocanceled aerial (0x33 -> 0x16) must NOT trigger a miss.
    tmResetSlot(st);
    TimingStepInput ac1 = makeInput(60, kTmStAttackAir, kTmStFall, kTmModeLCancel);
    addEv(ac1, kTmStAttackAir, kTmStFall, 0);
    tmStepSlot(st, ac1);

    TimingStepInput ac2 = makeInput(65, kTmStLanding, kTmStAttackAir, kTmModeLCancel);
    addEv(ac2, kTmStLanding, kTmStAttackAir, 0);
    tmStepSlot(st, ac2);
    CHECK_EQ(static_cast<int>(kTmPhaseHidden), static_cast<int>(st.view.phase));
    CHECK_EQ(0u, st.eventCount);
}

TEST(timing_meter, wavedash_perfect_mid_late_and_whiff)
{
    TimingSlotState st;
    tmResetSlot(st);

    // 1. P+ 1-frame earlier physics: 0x0A -> 0x0B -> 0x21 -> 0x19 all on one frame!
    TimingStepInput f1 = makeInput(100, kTmStJumpSquat, 0, kTmModeWavedash);
    f1.jumpSquatLen = 4;
    addEv(f1, kTmStJumpSquat, 0);
    tmStepSlot(st, f1);
    CHECK_EQ(static_cast<int>(kTmPhaseActive), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmKindWavedash), static_cast<int>(st.view.kind));
    CHECK_EQ(1, static_cast<int>(st.view.hasZone));

    TimingStepInput f2 = makeInput(104, kTmStLandingFallSpecial, kTmStJumpSquat, kTmModeWavedash);
    addEv(f2, kTmStJump, kTmStJumpSquat);
    addEv(f2, kTmStEscapeAir, kTmStJump);
    addEv(f2, kTmStLandingFallSpecial, kTmStEscapeAir);
    tmStepSlot(st, f2);
    CHECK_EQ(static_cast<int>(kTmPhaseHold), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmOutcomeHit), static_cast<int>(st.lastOutcome));
    CHECK_EQ(1, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmColGreen, st.view.fillColor);
    CHECK_EQ(1u, st.wavedashHits);

    // 2. 3-frame wavedash -> Cyan (kTmOutcomeLate).
    tmResetSlot(st);
    TimingStepInput m1 = makeInput(120, kTmStJumpSquat, 0, kTmModeWavedash);
    addEv(m1, kTmStJumpSquat, 0);
    tmStepSlot(st, m1);

    TimingStepInput m2 = makeInput(124, kTmStJump, kTmStJumpSquat, kTmModeWavedash);
    addEv(m2, kTmStJump, kTmStJumpSquat);
    tmStepSlot(st, m2);
    CHECK_EQ(static_cast<int>(kTmPhaseActive), static_cast<int>(st.view.phase));

    TimingStepInput m3 = makeInput(126, kTmStLandingFallSpecial, kTmStJump, kTmModeWavedash);
    addEv(m3, kTmStEscapeAir, kTmStJump);
    addEv(m3, kTmStLandingFallSpecial, kTmStEscapeAir);
    tmStepSlot(st, m3);
    CHECK_EQ(static_cast<int>(kTmOutcomeLate), static_cast<int>(st.lastOutcome));
    CHECK_EQ(3, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmColCyan, st.view.fillColor);

    // 3. 6-frame late wavedash -> Red (kTmOutcomeMiss).
    tmResetSlot(st);
    TimingStepInput l1 = makeInput(150, kTmStJumpSquat, 0, kTmModeWavedash);
    addEv(l1, kTmStJumpSquat, 0);
    tmStepSlot(st, l1);
    TimingStepInput l2 = makeInput(154, kTmStJump, kTmStJumpSquat, kTmModeWavedash);
    addEv(l2, kTmStJump, kTmStJumpSquat);
    tmStepSlot(st, l2);
    TimingStepInput l3 = makeInput(159, kTmStLandingFallSpecial, kTmStJump, kTmModeWavedash);
    addEv(l3, kTmStEscapeAir, kTmStJump);
    addEv(l3, kTmStLandingFallSpecial, kTmStEscapeAir);
    tmStepSlot(st, l3);
    CHECK_EQ(static_cast<int>(kTmOutcomeMiss), static_cast<int>(st.lastOutcome));
    CHECK_EQ(6, static_cast<int>(st.lastDelta));
    CHECK_EQ(kTmColRed, st.view.fillColor);

    // 4. Upward airdodge whiff (0x21 -> 0x0E Fall) -> Red miss.
    tmResetSlot(st);
    TimingStepInput w1 = makeInput(180, kTmStJumpSquat, 0, kTmModeWavedash);
    addEv(w1, kTmStJumpSquat, 0);
    tmStepSlot(st, w1);
    TimingStepInput w2 = makeInput(184, kTmStEscapeAir, kTmStJumpSquat, kTmModeWavedash);
    addEv(w2, kTmStJump, kTmStJumpSquat);
    addEv(w2, kTmStEscapeAir, kTmStJump);
    tmStepSlot(st, w2);
    TimingStepInput w3 = makeInput(212, kTmStFall, kTmStEscapeAir, kTmModeWavedash);
    addEv(w3, kTmStFall, kTmStEscapeAir);
    tmStepSlot(st, w3);
    CHECK_EQ(static_cast<int>(kTmOutcomeMiss), static_cast<int>(st.lastOutcome));
    CHECK_EQ(kTmColRed, st.view.fillColor);
}

TEST(timing_meter, act_out_countdown_and_first_actionable_flash)
{
    TimingSlotState st;
    tmResetSlot(st);

    // Enter LandingFallSpecial (0x19) with 10 frames of lag -> shrinking countdown bar.
    TimingStepInput a1 = makeInput(200, kTmStLandingFallSpecial, kTmStEscapeAir, kTmModeActOut);
    a1.actIn = 10;
    tmStepSlot(st, a1);
    CHECK_EQ(static_cast<int>(kTmPhaseActive), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmKindActOut), static_cast<int>(st.view.kind));
    CHECK_NEAR(1.0f, st.view.ratio, 0.01f);
    CHECK_EQ(kTmColCyan, st.view.fillColor);

    TimingStepInput a2 = makeInput(207, kTmStLandingFallSpecial, kTmStLandingFallSpecial, kTmModeActOut);
    a2.actIn = 3;
    tmStepSlot(st, a2);
    CHECK_NEAR(0.3f, st.view.ratio, 0.01f);
    CHECK_EQ(kTmColGold, st.view.fillColor);

    // Exact first actionable frame (actIn == 0) triggers crisp green flash!
    TimingStepInput a3 = makeInput(210, 0, kTmStLandingFallSpecial, kTmModeActOut);
    a3.actIn = 0;
    tmStepSlot(st, a3);
    CHECK_EQ(static_cast<int>(kTmPhaseHold), static_cast<int>(st.view.phase));
    CHECK_EQ(static_cast<int>(kTmKindActOut), static_cast<int>(st.view.kind));
    CHECK_EQ(static_cast<int>(kTmOutcomeHit), static_cast<int>(st.lastOutcome));
    CHECK_EQ(1, static_cast<int>(st.view.isFlash));
    CHECK_EQ(kTmColFlashGreen, st.view.fillColor);
    CHECK_EQ(1u, st.actOutFlashes);

    // Hold decays cleanly to hidden after kTmActFlashFrames ticks.
    for (int i = 1; i <= kTmActFlashFrames; i++)
    {
        TimingStepInput idle = makeInput(210 + i, 0, 0, kTmModeActOut);
        idle.actIn = 0;
        tmStepSlot(st, idle);
    }
    CHECK_EQ(static_cast<int>(kTmPhaseHidden), static_cast<int>(st.view.phase));

    // Off mode keeps bar hidden.
    TimingStepInput off = makeInput(250, kTmStAttackAir, kTmStJump, kTmModeOff);
    addEv(off, kTmStAttackAir, kTmStJump);
    tmStepSlot(st, off);
    CHECK_EQ(static_cast<int>(kTmPhaseHidden), static_cast<int>(st.view.phase));
}

TEST(timing_meter, osd_menu_row_and_description)
{
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    const ui::MenuDef& menu = ui::labMenu();
    const ui::Page& osdPage = *menu.pages[ui::pages::P_OSD_MENU];
    const int meterRow = 2;
    CHECK_STR("Overhead Timing Bar", osdPage.options[meterRow].name);
    CHECK_EQ(static_cast<int>(ui::kKindChoice), static_cast<int>(osdPage.options[meterRow].kind));
    CHECK_EQ(static_cast<int>(main::kTimingMeterCount - 1), static_cast<int>(osdPage.options[meterRow].max));
    CHECK_EQ(static_cast<int>(main::kTimingMeterOff), static_cast<int>(osdPage.options[meterRow].def));
    CHECK(osdPage.options[meterRow].enabled != NULL);
    const char* desc = extra::optionDesc(ui::pages::P_OSD_MENU, static_cast<u8>(meterRow));
    REQUIRE(desc != NULL && desc[0] != '\0');
}
