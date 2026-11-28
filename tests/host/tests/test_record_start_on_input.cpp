// Host unit tests for SF6-style Start-On-First-Input recording logic
// (src/main/record/start_input.h), HUD Armed fit (src/main/record/hud.cpp),
// and RecordingSettings::startOnFirstInput sanitization.
#include <string.h>

#include "extra/lab_tools.h"
#include "host/canvas.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/extra.h"
#include "main/gfx/text.h"
#include "main/record/hud.h"
#include "main/record/slots.h"
#include "main/record/start_input.h"
#include "main/settings.h"
#include "main/ui/menu.h"
#include "main/ui/pages/alter_inputs.h"
#include "main/ui/pages/pages.h"

using namespace tm;

namespace {
    record::Input neutralInput()
    {
        record::Input in;
        memset(&in, 0, sizeof(in));
        return in;
    }
}

TEST(record_start_on_input, neutrality_thresholds_match_buttons_sticks_and_triggers)
{
    record::Input in = neutralInput();
    CHECK(record::isNeutralInput(in));

    // kFlickJump alone (tap-jump bit always set by AI/pad neutral) stays neutral.
    in.buttons = cpu::button::kFlickJump;
    CHECK(record::isNeutralInput(in));

    // Any real button breaks neutrality.
    in.buttons = cpu::button::kFlickJump | cpu::button::kAttack;
    CHECK(!record::isNeutralInput(in));
    in.buttons = cpu::button::kJump;
    CHECK(!record::isNeutralInput(in));
    in.buttons = cpu::button::kGuard;
    CHECK(!record::isNeutralInput(in));

    // Main stick & C-stick dead zone: strictly below 12/127 is neutral, >= 12/127 triggers.
    const float belowStick = 11.9f / 127.0f;
    const float atStick = 12.0f / 127.0f;

    in = neutralInput();
    in.x = belowStick;
    in.y = -belowStick;
    in.cx = belowStick;
    in.cy = -belowStick;
    CHECK(record::isNeutralInput(in));

    in = neutralInput();
    in.x = atStick;
    CHECK(!record::isNeutralInput(in));
    in = neutralInput();
    in.x = -atStick;
    CHECK(!record::isNeutralInput(in));
    in = neutralInput();
    in.y = atStick;
    CHECK(!record::isNeutralInput(in));
    in = neutralInput();
    in.cx = -atStick;
    CHECK(!record::isNeutralInput(in));
    in = neutralInput();
    in.cy = atStick;
    CHECK(!record::isNeutralInput(in));

    // Analog triggers: strictly below 40/255 is neutral, >= 40/255 triggers.
    const float belowTrig = 39.9f / 255.0f;
    const float atTrig = 40.0f / 255.0f;

    in = neutralInput();
    in.l = belowTrig;
    in.r = belowTrig;
    CHECK(record::isNeutralInput(in));

    in = neutralInput();
    in.l = atTrig;
    CHECK(!record::isNeutralInput(in));
    in = neutralInput();
    in.r = atTrig;
    CHECK(!record::isNeutralInput(in));
}

TEST(record_start_on_input, standalone_vs_coplayback_arming_and_clock_hold)
{
    CHECK(!record::shouldArmRecording(false, true, false));
    CHECK(!record::shouldArmRecording(true, false, false));
    CHECK(record::shouldArmRecording(true, true, false));
    CHECK(record::shouldArmRecording(true, false, true));

    // Standalone recording holds s_t = 0 while armed; co-playback lets s_t advance.
    CHECK(record::shouldHoldRecordingClock(true, false));
    CHECK(!record::shouldHoldRecordingClock(true, true));
    CHECK(!record::shouldHoldRecordingClock(false, false));

    // Simulate standalone recording with stale prior slot data.
    static record::Slot slot;
    record::slotClear(slot);
    record::Input stale = neutralInput();
    stale.buttons = cpu::button::kJump;
    record::slotWrite(slot, 0, stale);
    record::slotWrite(slot, 1, stale);
    CHECK_EQ(0u, static_cast<unsigned>(slot.start));
    CHECK_EQ(2u, static_cast<unsigned>(slot.end));

    bool armed = record::shouldArmRecording(true, true, false);
    int t = 0;
    bool clearSlot = false;
    record::Input neut = neutralInput();
    neut.buttons = cpu::button::kFlickJump;
    for (int f = 0; f < 10; f++)
    {
        CHECK(!record::consumeArmedInput(armed, neut, true, clearSlot));
        CHECK(!clearSlot);
        CHECK(armed);
        if (!record::shouldHoldRecordingClock(armed, false))
        {
            t++;
        }
    }
    CHECK_EQ(0, t);

    // First non-neutral input clears stale data, writes frame 0, and unarms clock.
    record::Input act = neutralInput();
    act.buttons = cpu::button::kAttack;
    CHECK(record::consumeArmedInput(armed, act, true, clearSlot));
    CHECK(clearSlot);
    CHECK(!armed);
    if (clearSlot)
    {
        record::slotClear(slot);
    }
    record::slotWrite(slot, t, act);
    if (!record::shouldHoldRecordingClock(armed, false))
    {
        t++;
    }
    CHECK_EQ(1, t);
    CHECK_EQ(0u, static_cast<unsigned>(slot.start));
    CHECK_EQ(1u, static_cast<unsigned>(slot.end));

    // Co-playback + record: s_t advances while armed; first input sets slot.start = t.
    record::slotClear(slot);
    armed = true;
    t = 0;
    for (int f = 0; f < 15; f++)
    {
        CHECK(!record::consumeArmedInput(armed, neut, true, clearSlot));
        if (!record::shouldHoldRecordingClock(armed, true))
        {
            t++;
        }
    }
    CHECK_EQ(15, t);
    CHECK(record::consumeArmedInput(armed, act, true, clearSlot));
    CHECK(clearSlot);
    CHECK(!armed);
    record::slotWrite(slot, t, act);
    CHECK_EQ(15u, static_cast<unsigned>(slot.start));
    CHECK_EQ(16u, static_cast<unsigned>(slot.end));

    // Re-record takeover preserves earlier frames (clearSlot stays false).
    armed = true;
    CHECK(record::consumeArmedInput(armed, act, false, clearSlot));
    CHECK(!clearSlot);
    CHECK(!armed);
}

TEST(record_start_on_input, standalone_rerecord_holds_clock_and_dual_record_clears_both_slots)
{
    // Standalone kModeReRecord: active playback advances s_t, then after playback
    // ends (cancelled == true) and armed activates, s_t holds in place until the
    // first non-neutral input so no neutral zero frames are padded.
    CHECK(record::hasActivePlayback(true, false, false, false));
    CHECK(!record::hasActivePlayback(true, true, false, false));
    CHECK(record::hasActivePlayback(true, true, true, false));
    CHECK(!record::hasActivePlayback(true, true, true, true));

    static record::Slot hmnSlot;
    static record::Slot cpuSlot;
    record::slotClear(hmnSlot);
    record::Input prev = neutralInput();
    prev.buttons = cpu::button::kJump;
    for (int f = 0; f < 5; f++)
    {
        record::slotWrite(hmnSlot, f, prev);
    }
    CHECK_EQ(5u, static_cast<unsigned>(hmnSlot.end));

    // Playback finished at frame 5 -> cancelled = true, armed = true.
    bool armed = true;
    int t = 5;
    record::Input neut = neutralInput();
    neut.buttons = cpu::button::kFlickJump;
    bool clearSlot = false;
    for (int f = 0; f < 10; f++)
    {
        CHECK(!record::consumeArmedInput(armed, neut, false, clearSlot));
        CHECK(!clearSlot);
        bool anyPlaying = record::hasActivePlayback(true, true, false, false);
        CHECK(record::shouldHoldRecordingClock(armed, anyPlaying));
        if (!record::shouldHoldRecordingClock(armed, anyPlaying))
        {
            t++;
        }
    }
    CHECK_EQ(5, t);
    CHECK(armed);

    // First non-neutral input writes directly at frame 5 (no neutral gap).
    record::Input act = neutralInput();
    act.buttons = cpu::button::kAttack;
    CHECK(record::consumeArmedInput(armed, act, false, clearSlot));
    CHECK(!clearSlot);
    CHECK(!armed);
    record::slotWrite(hmnSlot, t, act);
    if (!record::shouldHoldRecordingClock(armed, false))
    {
        t++;
    }
    CHECK_EQ(6, t);
    CHECK_EQ(0u, static_cast<unsigned>(hmnSlot.start));
    CHECK_EQ(6u, static_cast<unsigned>(hmnSlot.end));

    // Co-playback while HMN kModeReRecord is armed after ending: CPU is still
    // playing (cpuCancelled == false), so s_t keeps advancing for sync.
    CHECK(!record::shouldHoldRecordingClock(true, record::hasActivePlayback(true, true, true, false)));

    // Quick-load guard: only restore recArmed when Start On Input is still on
    // and both sides kept the same modes as when the quick state was saved.
    CHECK(record::shouldRestoreQuickArmed(true, 1, 1, 0, 0));
    CHECK(!record::shouldRestoreQuickArmed(false, 1, 1, 0, 0));
    CHECK(!record::shouldRestoreQuickArmed(true, 1, 0, 0, 0));
    CHECK(!record::shouldRestoreQuickArmed(true, 1, 1, 2, 0));

    // Dual-record pendingClear safety: both HMN and CPU slots have stale data;
    // when armed triggers on HMN first, CPU's pendingClear still clears CPU's
    // stale slot on its first write instead of appending to stale frames.
    record::slotClear(hmnSlot);
    record::slotClear(cpuSlot);
    record::slotWrite(hmnSlot, 0, prev);
    record::slotWrite(hmnSlot, 1, prev);
    record::slotWrite(cpuSlot, 0, prev);
    record::slotWrite(cpuSlot, 1, prev);

    armed = record::shouldArmRecording(true, true, true);
    bool pendingClear[2] = {armed, armed};
    CHECK(armed);
    // HMN triggers first at t = 0.
    CHECK(record::consumeArmedInput(armed, act, true, clearSlot));
    CHECK(!armed);
    if (clearSlot || pendingClear[0])
    {
        record::slotClear(hmnSlot);
        pendingClear[0] = false;
    }
    record::slotWrite(hmnSlot, 0, act);
    // CPU runs second in the same frame with armed == false, clearSlot == false.
    CHECK(record::consumeArmedInput(armed, neut, true, clearSlot));
    CHECK(!clearSlot);
    CHECK(pendingClear[1]);
    if (clearSlot || pendingClear[1])
    {
        record::slotClear(cpuSlot);
        pendingClear[1] = false;
    }
    record::slotWrite(cpuSlot, 0, neut);
    CHECK_EQ(1u, static_cast<unsigned>(hmnSlot.end));
    CHECK_EQ(1u, static_cast<unsigned>(cpuSlot.end));
    CHECK(!pendingClear[0] && !pendingClear[1]);
}

TEST(record_start_on_input, armed_hud_tag_fits_bar_on_both_aspects)
{
    const float aspects[2] = {1.0f, 0.75f};
    for (int a = 0; a < 2; a++)
    {
        gfx::setXScale(aspects[a]);
        host::canvas::clear();
        record::HudInfo info = {record::kHudArmed, 0, 0, false, NULL};
        record::hudDraw(info);
        REQUIRE(host::canvas::rects() >= 2);
        const host::canvas::Rect& bar = host::canvas::rect(0);
        const host::canvas::Rect& track = host::canvas::rect(1);
        bool sawArmed = false;
        for (int i = 0; i < host::canvas::runs(); i++)
        {
            const host::canvas::Run& r = host::canvas::run(i);
            if (strcmp(r.text, "Armed") == 0)
            {
                sawArmed = true;
                CHECK(r.x >= track.x + track.width - 0.05f);
                CHECK(r.right <= bar.x + bar.width + 0.05f);
            }
        }
        CHECK(sawArmed);
    }
}

TEST(record_start_on_input, settings_layout_and_sanitization)
{
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    CHECK_EQ(0u, static_cast<unsigned>(main::settings().recording.startOnFirstInput));

    main::settings().recording.startOnFirstInput = 250;
    main::sanitizeSettings(main::settings());
    CHECK_EQ(1u, static_cast<unsigned>(main::settings().recording.startOnFirstInput));
}

TEST(record_slots, packed_run_log_reclaims_ram_and_preserves_bit_exact_rewind)
{
    // Packed run log uses 2 record::Slot blocks (14 * sizeof(Slot) total vs
    // 12 * sizeof(Slot) + 2 * 3600 * 32 before), reclaiming 160,472 B (~157 KiB).
    const u32 oldBytes = 12u * static_cast<u32>(sizeof(record::Slot)) + 2u * record::kMaxFrames * 32u;
    const u32 newBytes = record::storageBytes();
    CHECK_EQ(14u * static_cast<u32>(sizeof(record::Slot)), newBytes);
    CHECK_EQ(160472u, oldBytes - newBytes);

    // Exercise Slot packing directly for bit-exact float preservation across
    // non-power-of-two analog stick / C-stick / trigger values and rewind reads.
    static record::Slot log;
    record::slotClear(log);
    const int kFrames = 12;
    record::Input seq[kFrames];
    for (int f = 0; f < kFrames; f++)
    {
        memset(&seq[f], 0, sizeof(seq[f]));
        seq[f].buttons = static_cast<u32>((f * 37 + 1) & 0xFFFF);
        seq[f].x = (static_cast<float>(f - 5) / 80.0f) * 0.9875f;
        seq[f].y = (static_cast<float>(5 - f) / 80.0f) * 0.9125f;
        seq[f].cx = (f % 3 == 0) ? 0.775f : -0.4125f;
        seq[f].cy = (f % 2 == 0) ? 0.0f : -0.6375f;
        seq[f].l = (f % 4 == 0) ? (43.0f / 140.0f) : 0.0f;
        seq[f].r = (f % 5 == 0) ? (120.0f / 140.0f) : 0.0f;
        seq[f].press = static_cast<u8>(f & 3);
        record::slotWrite(log, f, seq[f]);
    }
    CHECK_EQ(0u, log.lossy);
    CHECK_EQ(static_cast<u16>(kFrames), log.end);

    // Simulate Frame Decrement rewind: read frames 0..8 bit-for-bit without truncating.
    for (int f = 0; f < 9; f++)
    {
        record::Input got;
        REQUIRE(record::slotRead(log, f, got));
        CHECK_EQ(seq[f].buttons, got.buttons);
        CHECK_EQ(0, memcmp(&seq[f].x, &got.x, sizeof(float)));
        CHECK_EQ(0, memcmp(&seq[f].y, &got.y, sizeof(float)));
        CHECK_EQ(0, memcmp(&seq[f].cx, &got.cx, sizeof(float)));
        CHECK_EQ(0, memcmp(&seq[f].cy, &got.cy, sizeof(float)));
        CHECK_EQ(0, memcmp(&seq[f].l, &got.l, sizeof(float)));
        CHECK_EQ(0, memcmp(&seq[f].r, &got.r, sizeof(float)));
        CHECK_EQ(seq[f].press, got.press);
    }

    // After rewind reaches target frame 9, writing a new input truncates old future cleanly.
    record::Input alt = seq[9];
    alt.buttons = cpu::button::kJump;
    record::slotWrite(log, 9, alt);
    CHECK_EQ(10u, static_cast<unsigned>(log.end));
    record::Input check;
    REQUIRE(record::slotRead(log, 9, check));
    CHECK_EQ(alt.buttons, check.buttons);
    CHECK(!record::slotRead(log, 10, check));
}

namespace {
    u8* alterTestValues()
    {
        return reinterpret_cast<u8*>(&main::settings());
    }
}

TEST(record_slots, alter_inputs_frame_min_clamps_above_255)
{
    host::fakes::reset();
    main::ExtraHooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.alterRow = extra::alterRow;
    hooks.alterEdited = extra::alterEdited;
    main::setExtraHooks(&hooks);

    REQUIRE(record::storageAllocate());
    main::settings().recording.manageSide = 0;
    main::settings().recording.manageSlot = 0;
    record::Slot* s = record::slotAt(0, 0);
    REQUIRE(s != NULL);

    static const ui::Page* const kOnePage[1] = {&ui::pages::kAlterInputs};
    static const ui::MenuDef kDef = {kOnePage, 1, alterTestValues, 0, NULL, NULL, 0, NULL};
    ui::pages::AlterCopy& copy = *reinterpret_cast<ui::pages::AlterCopy*>(ui::menuScratch());

    const int kStarts[2] = {255, 300};
    for (int tc = 0; tc < 2; tc++)
    {
        const int start = kStarts[tc];
        const int end = start + 6;
        record::slotClear(*s);
        for (int f = start; f < end; f++)
        {
            record::Input in = neutralInput();
            in.x = static_cast<float>(f - start + 1) / 80.0f;
            in.buttons = ((f - start) & 1) ? 0x1u : 0x4u;
            record::slotWrite(*s, f, in);
        }
        CHECK_EQ(static_cast<u16>(start), s->start);
        CHECK_EQ(static_cast<u16>(end), s->end);

        memset(&copy, 0, sizeof(copy));
        ui::MenuState state;
        ui::menuReset(kDef, state);

        REQUIRE(extra::alterRow());
        ui::Option* frameRow = ui::pages::alterFrameRow();
        REQUIRE(frameRow != NULL);
        CHECK_EQ(255, static_cast<int>(frameRow->min));
        CHECK_EQ(end, static_cast<int>(frameRow->max));
        CHECK_EQ(static_cast<u16>(start + 1), copy.frame);
        CHECK_EQ(1, static_cast<int>(copy.axis[0]));

        // Stepping Left (or fast-Left) from s->start + 1 must stay clamped at
        // s->start + 1 and never drop to 0 or pre-start frames.
        ui::MenuInput in;
        memset(&in, 0, sizeof(in));
        in.left = 1;
        CHECK(ui::menuStep(kDef, state, in));
        CHECK_EQ(static_cast<u16>(start + 1), copy.frame);
        CHECK_EQ(1, static_cast<int>(copy.axis[0]));

        in.left = 20;
        CHECK(ui::menuStep(kDef, state, in));
        CHECK_EQ(static_cast<u16>(start + 1), copy.frame);

        // Stepping Right advances into the slot and refreshes the displayed frame.
        memset(&in, 0, sizeof(in));
        in.right = 2;
        CHECK(ui::menuStep(kDef, state, in));
        CHECK_EQ(static_cast<u16>(start + 3), copy.frame);
        CHECK_EQ(3, static_cast<int>(copy.axis[0]));

        // Large Left step back clamps cleanly to s->start + 1.
        memset(&in, 0, sizeof(in));
        in.left = 50;
        CHECK(ui::menuStep(kDef, state, in));
        CHECK_EQ(static_cast<u16>(start + 1), copy.frame);
        CHECK_EQ(1, static_cast<int>(copy.axis[0]));

        // Out-of-range copy.frame is ignored safely by alterEdited().
        copy.frame = 0;
        copy.axis[0] = 77;
        extra::alterEdited();
        copy.frame = static_cast<u16>(start);
        extra::alterEdited();
        record::Input verify;
        REQUIRE(record::slotRead(*s, start, verify));
        CHECK_EQ(1, static_cast<int>(verify.x * 80.0f + 0.5f));

        // Valid edit inside [s->start + 1, s->end] persists cleanly.
        copy.frame = static_cast<u16>(start + 2);
        REQUIRE(extra::alterRow());
        copy.axis[0] = 42;
        extra::alterEdited();
        REQUIRE(record::slotRead(*s, start + 1, verify));
        CHECK_EQ(42, static_cast<int>(verify.x * 80.0f + 0.5f));
    }

    if (ui::Option* frameRow = ui::pages::alterFrameRow())
    {
        frameRow->min = 1;
        frameRow->max = 3600;
    }
    main::setExtraHooks(NULL);
    record::storageFree();
}

TEST(record_start_on_input, start_paused_yields_to_lab_exit_and_pause_transitions)
{
    bool release = false;

    // Not holding: never freezes or releases.
    CHECK(!record::shouldHoldStartPaused(false, true, false, false, release));
    CHECK(!release);

    // Holding in live match with no lab transition: keeps game frozen.
    CHECK(record::shouldHoldStartPaused(true, true, false, false, release));
    CHECK(!release);

    // Holding while lab transition is active (Return to CSS, Z -> Training Menu,
    // pause catch-up, fighter swap, modals): yields freeze without clearing hold.
    CHECK(!record::shouldHoldStartPaused(true, true, false, true, release));
    CHECK(!release);

    // Live input releases the hold and does not freeze.
    CHECK(!record::shouldHoldStartPaused(true, true, true, false, release));
    CHECK(release);

    // Turning Start Paused Off while holding releases the hold immediately.
    CHECK(!record::shouldHoldStartPaused(true, false, false, false, release));
    CHECK(release);
}
