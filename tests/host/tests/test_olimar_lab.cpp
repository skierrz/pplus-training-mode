// Olimar Lab Kit unit tests (src/extra/olimar_pikmin_logic.h,
// src/extra/olimar_lab.cpp, src/main/ui/pages/olimar.cpp,
// kAreaFighterMechanics ABI & old-save migration).
#include <ft/ft_common_data_accesser.h>
#include <string.h>

#include "extra/fighter_mechanics.h"
#include "extra/fighter_mechanics_logic.h"
#include "extra/olimar_lab.h"
#include "extra/olimar_pikmin_logic.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/pages/olimar.h"
#include "main/ui/pages/pages.h"

namespace {
    using namespace tm;

    static bool s_fakeOlimarInMatch = false;
    static int s_fakeActionCalls = 0;
    static u32 s_fakeLastAction = 0;
    static int s_fakeRngApplyCalls = 0;

    static bool fakeCharacterRng(int call)
    {
        if (call == main::kRngApply)
        {
            s_fakeRngApplyCalls++;
            return true;
        }
        if (call == main::kRngOlimar || call == main::kRngOlimarP1)
        {
            return s_fakeOlimarInMatch;
        }
        return false;
    }

    static void fakeOlimarAction(u32 action, u32, u32)
    {
        s_fakeActionCalls++;
        s_fakeLastAction = action;
    }

    static const main::ExtraHooks kOlimarTestHooks = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        fakeCharacterRng, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, fakeOlimarAction
    };

    void bootClean()
    {
        storage::detach();
        host::card::reset();
        memset(ui::menuScratch(), 0, ui::kScratchBytes);
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
    }

    struct OlimarPacData {
        u8 header[0x90];
        s32 gid;
        float speed;
        float weights[5];
    };

    OlimarPacData* makeOlimarPac()
    {
        OlimarPacData* pac = host::mem::make<OlimarPacData>();
        memset(pac, 0, sizeof(*pac));
        pac->gid = 0;
        pac->speed = 1.0f;
        for (int i = 0; i < 5; i++)
        {
            pac->weights[i] = 1.0f;
        }
        u32 tableAddr = reinterpret_cast<u32>(&pac->gid);
        u32 count = 30;
        memcpy(pac->header + 0x7C, &tableAddr, 4);
        memcpy(pac->header + 0x80, &count, 4);
        return pac;
    }
}

TEST(olimar_lab, settings_abi_and_bitpack_roundtrip)
{
    CHECK_EQ(464u, static_cast<u32>(sizeof(main::Settings)));
    CHECK_EQ(32u, static_cast<u32>(sizeof(main::FighterMechanicsSettings)));
    CHECK_EQ(455u, offsetof(main::Settings, fighterMechanics.olimarPikmin));
    CHECK_EQ(456u, offsetof(main::Settings, fighterMechanics.olimarMaturity));
    CHECK_EQ(457u, offsetof(main::Settings, fighterMechanics.olimarPullAndWhistle));
    CHECK_EQ(458u, offsetof(main::Settings, fighterMechanics.olimarColorMatAndMode));
    CHECK_EQ(460u, offsetof(main::Settings, fighterMechanics.olimarStartLineup));
    CHECK_EQ(462u, offsetof(main::Settings, fighterMechanics.olimarDpadPresets));

    main::FighterMechanicsSettings fm;
    memset(&fm, 0, sizeof(fm));

    // Pull Pool bits 0..4 + Whistle bits 5..7 do not interfere.
    extra::olimarSetRawPullMask(fm, 0x1Au); // Y + W + P
    extra::olimarSetWhistleColor(fm, main::kPikminPurple);
    CHECK_EQ(0x1Au, static_cast<u32>(extra::olimarRawPullMask(fm)));
    CHECK_EQ(static_cast<u32>(main::kPikminPurple), static_cast<u32>(extra::olimarWhistleColor(fm)));
    extra::olimarSetRawPullMask(fm, 0x05u); // R + B
    CHECK_EQ(static_cast<u32>(main::kPikminPurple), static_cast<u32>(extra::olimarWhistleColor(fm)));
    CHECK_EQ(0x05u, static_cast<u32>(extra::olimarRawPullMask(fm)));

    // Per-color maturities (5x 2-bit) + D-Pad Mode (2-bit) round-trip cleanly.
    const u8 wantMat[5] = {main::kPikminFlower, main::kPikminLeaf, main::kPikminMaturityDefault,
                           main::kPikminBud, main::kPikminFlower};
    for (int c = 0; c < 5; c++)
    {
        extra::olimarSetColorMaturityRaw(fm, c, wantMat[c]);
    }
    extra::olimarSetDpadMode(fm, main::kOlimarDpadPresets);
    for (int c = 0; c < 5; c++)
    {
        CHECK_EQ(static_cast<int>(wantMat[c]), static_cast<int>(extra::olimarColorMaturityRaw(fm, c)));
    }
    CHECK_EQ(static_cast<int>(main::kOlimarDpadPresets), static_cast<int>(extra::olimarDpadMode(fm)));

    // Starting Lineup 4x 3-bit slots (0..6) round-trip cleanly.
    const u8 wantStart[4] = {main::kStartSlotPurple, main::kStartSlotYellow, main::kStartSlotEmpty, main::kStartSlotRed};
    for (int i = 0; i < 4; i++)
    {
        extra::olimarSetStartSlot(fm, i, wantStart[i]);
    }
    CHECK(extra::olimarStartLineupEnabled(fm));
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(static_cast<int>(wantStart[i]), static_cast<int>(extra::olimarStartSlot(fm, i)));
    }

    // D-Pad Presets 4x 4-bit IDs (0..15) round-trip cleanly.
    const u8 wantPresets[4] = {6, 13, 0, 15};
    for (int d = 0; d < 4; d++)
    {
        extra::olimarSetDpadPreset(fm, d, wantPresets[d]);
    }
    for (int d = 0; d < 4; d++)
    {
        CHECK_EQ(static_cast<int>(wantPresets[d]), static_cast<int>(extra::olimarDpadPreset(fm, d)));
    }

    // Corrupt / out-of-range bits sanitize cleanly.
    fm.olimarPikmin = 99;
    fm.olimarMaturity = 7;
    fm.olimarPullAndWhistle = static_cast<u8>((7u << 5) | 0x15u);
    fm.olimarColorMatAndMode = 0xFFFFu;
    fm.olimarStartLineup = 0xFFFFu;
    extra::olimarSanitizeSettings(fm);
    CHECK_EQ(0, static_cast<int>(fm.olimarPikmin));
    CHECK_EQ(0, static_cast<int>(fm.olimarMaturity));
    CHECK_EQ(0x15u, static_cast<u32>(extra::olimarRawPullMask(fm)));
    CHECK_EQ(0, static_cast<int>(extra::olimarWhistleColor(fm)));
    CHECK_EQ(0, static_cast<int>(extra::olimarDpadMode(fm)));
    CHECK_EQ(0u, static_cast<u32>(fm.olimarColorMatAndMode & 0xF000u));
    CHECK_EQ(0u, static_cast<u32>(fm.olimarStartLineup));
}

TEST(olimar_lab, pull_pool_all_zero_clamp_and_legacy_migration)
{
    const float orig[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float out[5];
    main::FighterMechanicsSettings fm;
    memset(&fm, 0, sizeof(fm));

    // All-0 pull mask (no legacy olimarPikmin) MUST clamp to 0x1F (original weights)
    // so 0x80a6b864 never returns -1 and aborts plucking!
    CHECK_EQ(0x1Fu, static_cast<u32>(extra::olimarEffectivePullMask(fm)));
    extra::olimarPullPoolWeights(fm, orig, out);
    for (int i = 0; i < 5; i++)
    {
        CHECK_NEAR(1.0f, out[i], 0.001f);
    }

    // Legacy migration: old save with olimarPikmin=Purple and pullMask=0 or 0x1F -> Purple only.
    fm.olimarPikmin = main::kPikminPurple;
    CHECK_EQ(0x10u, static_cast<u32>(extra::olimarEffectivePullMask(fm)));
    extra::olimarPullPoolWeights(fm, orig, out);
    CHECK_NEAR(0.0f, out[0], 0.001f);
    CHECK_NEAR(0.0f, out[1], 0.001f);
    CHECK_NEAR(0.0f, out[2], 0.001f);
    CHECK_NEAR(0.0f, out[3], 0.001f);
    CHECK_NEAR(1.0f, out[4], 0.001f);

    extra::olimarSetRawPullMask(fm, 0x1Fu);
    CHECK_EQ(0x10u, static_cast<u32>(extra::olimarEffectivePullMask(fm)));

    // Explicit Pull Pool subset (White + Purple = 0x18) overrides legacy and emits {0,0,0,1,1}.
    extra::olimarSetRawPullMask(fm, 0x18u);
    CHECK_EQ(0x18u, static_cast<u32>(extra::olimarEffectivePullMask(fm)));
    extra::olimarPullPoolWeights(fm, orig, out);
    CHECK_NEAR(0.0f, out[0], 0.001f);
    CHECK_NEAR(0.0f, out[1], 0.001f);
    CHECK_NEAR(0.0f, out[2], 0.001f);
    CHECK_NEAR(1.0f, out[3], 0.001f);
    CHECK_NEAR(1.0f, out[4], 0.001f);

    // Runtime Olimar PAC apply + no-Olimar-frame restoration.
    g_ftCommonDataAccesser.reset();
    bootClean();
    extra::olimarLabAttach();
    OlimarPacData* pac = makeOlimarPac();
    g_ftCommonDataAccesser.setData(extra::kFtOlimar, pac->header);
    host::fakes::FakeFighter& oli = host::fakes::addFighter();

    extra::olimarSetRawPullMask(main::settings().fighterMechanics, 0x18u);
    extra::olimarLabApplyFighter(oli.fighter, oli.modules, 0, 0, false);
    CHECK_NEAR(0.0f, pac->weights[0], 0.001f);
    CHECK_NEAR(0.0f, pac->weights[1], 0.001f);
    CHECK_NEAR(0.0f, pac->weights[2], 0.001f);
    CHECK_NEAR(1.0f, pac->weights[3], 0.001f);
    CHECK_NEAR(1.0f, pac->weights[4], 0.001f);

    // Olimar leaves match -> weights restored to orig immediately.
    extra::olimarLabNoOlimarFrame();
    for (int i = 0; i < 5; i++)
    {
        CHECK_NEAR(1.0f, pac->weights[i], 0.001f);
    }
    extra::olimarLabDetach();
}

TEST(olimar_lab, per_color_maturity_resolution_and_whistle_seed)
{
    main::FighterMechanicsSettings fm;
    memset(&fm, 0, sizeof(fm));

    // All Default -> -1 everywhere, hasAnyMaturityLock false.
    CHECK(!extra::olimarHasAnyMaturityLock(fm));
    for (int c = 0; c < 5; c++)
    {
        CHECK_EQ(-1, extra::olimarResolvedMaturityTarget(fm, c));
    }

    // Legacy global olimarMaturity=Flower applies to all 5 colors when per-color bits are 0.
    fm.olimarMaturity = main::kPikminFlower;
    CHECK(extra::olimarHasAnyMaturityLock(fm));
    for (int c = 0; c < 5; c++)
    {
        CHECK_EQ(2, extra::olimarResolvedMaturityTarget(fm, c));
    }

    // Per-color override wins over global fallback; clearing global leaves unlocked colors aging (-1).
    fm.olimarMaturity = main::kPikminMaturityDefault;
    extra::olimarSetColorMaturityRaw(fm, extra::kOliColorRed, main::kPikminFlower);
    extra::olimarSetColorMaturityRaw(fm, extra::kOliColorBlue, main::kPikminLeaf);
    extra::olimarSetColorMaturityRaw(fm, extra::kOliColorPurple, main::kPikminBud);
    CHECK(extra::olimarHasAnyMaturityLock(fm));
    CHECK_EQ(2, extra::olimarResolvedMaturityTarget(fm, extra::kOliColorRed));
    CHECK_EQ(-1, extra::olimarResolvedMaturityTarget(fm, extra::kOliColorYellow));
    CHECK_EQ(0, extra::olimarResolvedMaturityTarget(fm, extra::kOliColorBlue));
    CHECK_EQ(-1, extra::olimarResolvedMaturityTarget(fm, extra::kOliColorWhite));
    CHECK_EQ(1, extra::olimarResolvedMaturityTarget(fm, extra::kOliColorPurple));
    CHECK_EQ(-1, extra::olimarResolvedMaturityTarget(fm, -1));
    CHECK_EQ(-1, extra::olimarResolvedMaturityTarget(fm, 5));

    // Down-B whistle seed math: (seed + 1) % 5 == targetColor for every color 0..4.
    for (int c = 0; c < 5; c++)
    {
        s32 seed = extra::olimarWhistleSeedForColor(c);
        CHECK_EQ(c, static_cast<int>((seed + 1) % 5));
    }
}

TEST(olimar_lab, all_16_presets_and_start_lineup_resolution)
{
    main::FighterMechanicsSettings fm;
    memset(&fm, 0, sizeof(fm));
    extra::OlimarLineupSpec spec;

    // Preset 0 mirrors Starting Lineup (default = 4 Pull-Pool slots).
    extra::olimarResolvePreset(0, fm, spec);
    CHECK_EQ(4, static_cast<int>(spec.count));
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(-1, static_cast<int>(spec.colors[i]));
    }

    // Custom Starting Lineup with an Empty slot -> 3 concrete slots in exact order.
    extra::olimarSetStartSlot(fm, 0, main::kStartSlotPurple);
    extra::olimarSetStartSlot(fm, 1, main::kStartSlotYellow);
    extra::olimarSetStartSlot(fm, 2, main::kStartSlotEmpty);
    extra::olimarSetStartSlot(fm, 3, main::kStartSlotRed);
    extra::olimarResolvePreset(0, fm, spec);
    CHECK_EQ(3, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[1]));
    CHECK_EQ(extra::kOliColorRed, static_cast<int>(spec.colors[2]));

    // Presets 1..5: 4x monocolor R, Y, B, W, P.
    for (u8 id = 1; id <= 5; id++)
    {
        extra::olimarResolvePreset(id, fm, spec);
        CHECK_EQ(4, static_cast<int>(spec.count));
        for (int i = 0; i < 4; i++)
        {
            CHECK_EQ(static_cast<int>(id - 1), static_cast<int>(spec.colors[i]));
        }
    }

    // Presets 6..11: competitive 4-Pikmin pairs/stacks.
    extra::olimarResolvePreset(6, fm, spec); // 2Y + 2P
    CHECK_EQ(4, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[1]));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[2]));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[3]));

    extra::olimarResolvePreset(7, fm, spec); // 2P + 2W
    CHECK_EQ(4, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorWhite, static_cast<int>(spec.colors[3]));

    extra::olimarResolvePreset(10, fm, spec); // P+Y+W+R
    CHECK_EQ(4, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[1]));
    CHECK_EQ(extra::kOliColorWhite, static_cast<int>(spec.colors[2]));
    CHECK_EQ(extra::kOliColorRed, static_cast<int>(spec.colors[3]));

    extra::olimarResolvePreset(11, fm, spec); // Y+P+B+R
    CHECK_EQ(4, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[1]));
    CHECK_EQ(extra::kOliColorBlue, static_cast<int>(spec.colors[2]));
    CHECK_EQ(extra::kOliColorRed, static_cast<int>(spec.colors[3]));

    // Presets 12..15: 2-Pikmin and 1-Pikmin drills.
    extra::olimarResolvePreset(12, fm, spec); // Y + P
    CHECK_EQ(2, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[1]));

    extra::olimarResolvePreset(13, fm, spec); // P + W
    CHECK_EQ(2, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[0]));
    CHECK_EQ(extra::kOliColorWhite, static_cast<int>(spec.colors[1]));

    extra::olimarResolvePreset(14, fm, spec); // 1 Purple
    CHECK_EQ(1, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorPurple, static_cast<int>(spec.colors[0]));

    extra::olimarResolvePreset(15, fm, spec); // 1 Yellow
    CHECK_EQ(1, static_cast<int>(spec.count));
    CHECK_EQ(extra::kOliColorYellow, static_cast<int>(spec.colors[0]));
}

TEST(olimar_lab, dpad_pluck_mapping_suppression_and_comboL_coexistence)
{
    main::FighterMechanicsSettings fm;
    memset(&fm, 0, sizeof(fm));
    extra::olimarSetRawPullMask(fm, main::kOlimarPullPoolAll);

    // Full Pull Pool: bare B -> Red (0); D-Up/Left/Right/Down -> Y/B/W/P.
    CHECK_EQ(extra::kOliColorRed, extra::olimarPluckColorForDpad(fm, 0u));
    CHECK_EQ(extra::kOliColorYellow, extra::olimarPluckColorForDpad(fm, extra::kOliPadUp));
    CHECK_EQ(extra::kOliColorBlue, extra::olimarPluckColorForDpad(fm, extra::kOliPadLeft));
    CHECK_EQ(extra::kOliColorWhite, extra::olimarPluckColorForDpad(fm, extra::kOliPadRight));
    CHECK_EQ(extra::kOliColorPurple, extra::olimarPluckColorForDpad(fm, extra::kOliPadDown));

    // Narrowed Pull Pool: bare B -> -1 (use Pull Pool), D-pad still selects exact color.
    extra::olimarSetRawPullMask(fm, 0x18u);
    CHECK_EQ(-1, extra::olimarPluckColorForDpad(fm, 0u));
    CHECK_EQ(extra::kOliColorYellow, extra::olimarPluckColorForDpad(fm, extra::kOliPadUp));
    CHECK_EQ(extra::kOliColorPurple, extra::olimarPluckColorForDpad(fm, extra::kOliPadDown));

    // D-pad suppression gating + L+D-Pad (comboL) coexistence.
    CHECK(!extra::olimarShouldSuppressDpad(true, true, false, false, main::kOlimarDpadOff, true));
    CHECK(!extra::olimarShouldSuppressDpad(true, true, false, false, main::kOlimarDpadPluck, false));
    CHECK(!extra::olimarShouldSuppressDpad(true, true, true, false, main::kOlimarDpadPluck, true));
    CHECK(!extra::olimarShouldSuppressDpad(true, false, false, false, main::kOlimarDpadPresets, true));
    // L+D-Pad CPU % adjustment MUST take precedence when comboL is true.
    CHECK(!extra::olimarShouldSuppressDpad(true, true, false, true, main::kOlimarDpadPluck, true));
    CHECK(!extra::olimarShouldSuppressDpad(true, true, false, true, main::kOlimarDpadPresets, true));
    // Active in live Training match with P1 Olimar when comboL is false.
    CHECK(extra::olimarShouldSuppressDpad(true, true, false, false, main::kOlimarDpadPluck, true));
    CHECK(extra::olimarShouldSuppressDpad(true, true, false, false, main::kOlimarDpadPresets, true));
}

TEST(olimar_lab, safe_state_gates_and_pending_latch)
{
    // Safe idle state passes all gates.
    CHECK(extra::olimarCanSwapLineup(0x00, false, false, false, 0, false)); // Wait
    CHECK(extra::olimarCanSwapLineup(0x04, false, false, false, 0, false)); // Run
    CHECK(extra::olimarCanSwapLineup(0x0E, false, false, false, 0, false)); // Fall

    // Critical blocked statuses (SpecialHi Jetpack 0x114, Air_Lasso 0x7F..0x83,
    // SpecialN/S/Lw, Catch/Throw, Hitstun, Entry, Dead/Rebirth, Final Smash).
    CHECK(!extra::olimarCanSwapLineup(0x114, false, false, false, 0, false)); // SpecialHi
    CHECK(!extra::olimarCanSwapLineup(0x7F, false, false, false, 0, false));  // Air_Lasso
    CHECK(!extra::olimarCanSwapLineup(0x83, false, false, false, 0, false));  // Air_Lasso end
    CHECK(!extra::olimarCanSwapLineup(0x112, false, false, false, 0, false)); // SpecialN
    CHECK(!extra::olimarCanSwapLineup(0x113, false, false, false, 0, false)); // SpecialS
    CHECK(!extra::olimarCanSwapLineup(0x115, false, false, false, 0, false)); // SpecialLw
    CHECK(!extra::olimarCanSwapLineup(0x34, false, false, false, 0, false));  // Catch
    CHECK(!extra::olimarCanSwapLineup(0x40, false, false, false, 0, false));  // Throw
    CHECK(!extra::olimarCanSwapLineup(0x43, false, false, false, 0, false));  // Damage/Hitstun
    CHECK(!extra::olimarCanSwapLineup(0x10E, false, false, false, 0, false)); // Entry
    CHECK(!extra::olimarCanSwapLineup(0xBE, false, false, false, 0, false));  // Rebirth
    CHECK(!extra::olimarCanSwapLineup(0x117, false, false, false, 0, false)); // Final Smash

    // Non-status gates: hitlag, existArt1, existArt2 (wnPikminChain), List1 > 0, swapBusy.
    CHECK(!extra::olimarCanSwapLineup(0x00, true, false, false, 0, false));
    CHECK(!extra::olimarCanSwapLineup(0x00, false, true, false, 0, false));
    CHECK(!extra::olimarCanSwapLineup(0x00, false, false, true, 0, false));
    CHECK(!extra::olimarCanSwapLineup(0x00, false, false, false, 1, false));
    CHECK(!extra::olimarCanSwapLineup(0x00, false, false, false, 0, true));

    // 15-frame pending latch: holds while blocked, fires on first safe frame, expires after 15 blocked frames.
    extra::OlimarPendingLatch latch;
    latch.clear();
    CHECK_EQ(-1, latch.step(true));
    latch.latch(6); // 2Y + 2P
    for (int f = 0; f < 5; f++)
    {
        CHECK_EQ(-1, latch.step(false));
    }
    CHECK_EQ(6, latch.step(true));
    CHECK_EQ(-1, latch.step(true)); // cleared after execution

    // Expiry after 15 blocked frames.
    latch.latch(13);
    for (int f = 0; f < 15; f++)
    {
        CHECK_EQ(-1, latch.step(false));
    }
    CHECK_EQ(-1, latch.step(true)); // expired
}

TEST(olimar_lab, menu_submenu_visibility_scratch_sync_and_storage_roundtrip)
{
    bootClean();
    u8* block = host::fakes::block();
    storage::attach(block);
    s_fakeOlimarInMatch = false;
    s_fakeActionCalls = 0;
    s_fakeLastAction = 0;
    main::setExtraHooks(&kOlimarTestHooks);

    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    // Without Olimar in match, jumping into P_OLIMAR fails (row 33 hidden/disabled).
    const u8 oliPath[2] = {ui::pages::P_STAGE_CAST, ui::pages::P_OLIMAR};
    CHECK(!ui::menuJump(menu, st, oliPath, 2));

    // With Olimar in match, P_OLIMAR opens cleanly and syncs scratch defaults.
    s_fakeOlimarInMatch = true;
    REQUIRE(ui::menuJump(menu, st, oliPath, 2));
    CHECK_EQ(static_cast<int>(ui::pages::P_OLIMAR), ui::currentPage(st));
    CHECK_EQ(22, static_cast<int>(menu.pages[ui::pages::P_OLIMAR]->count));

    // Edit D-Pad Mode (row 0) -> B+D-Pad Pluck (1), toggle Pull: Red Off (row 1),
    // set Whistle / Lead (row 6) -> Purple (5), press Move Lead Now (row 7),
    // set All Maturity (row 8) -> Flower (3), then override Blue Maturity (row 11) -> Leaf (1),
    // set Start Slot 1 (row 14) -> Purple (5), set Preset D-Up (row 18) -> 2Y + 2P (6).
    const ui::Page& oli = *menu.pages[ui::pages::P_OLIMAR];
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};

    st.cursor[ui::pages::P_OLIMAR] = 0; // D-Pad Mode -> 1
    ui::menuStep(menu, st, inRight);
    CHECK_EQ(static_cast<int>(main::kOlimarDpadPluck),
             static_cast<int>(extra::olimarDpadMode(main::settings().fighterMechanics)));

    st.cursor[ui::pages::P_OLIMAR] = 1; // Pull: Red -> Off (bit 0 cleared -> 0x1E)
    ui::menuStep(menu, st, inA);
    CHECK_EQ(0x1Eu, static_cast<u32>(extra::olimarEffectivePullMask(main::settings().fighterMechanics)));

    st.cursor[ui::pages::P_OLIMAR] = 6; // Whistle / Lead -> Purple (5)
    for (int k = 0; k < 5; k++) ui::menuStep(menu, st, inRight);
    CHECK_EQ(static_cast<int>(main::kPikminPurple),
             static_cast<int>(extra::olimarWhistleColor(main::settings().fighterMechanics)));

    st.cursor[ui::pages::P_OLIMAR] = 7; // Move Lead Now
    ui::menuStep(menu, st, inA);
    CHECK_EQ(1, s_fakeActionCalls);
    CHECK_EQ(static_cast<u32>(main::kOlimarMoveLead), s_fakeLastAction);

    st.cursor[ui::pages::P_OLIMAR] = 8; // All Maturity -> Flower (3)
    for (int k = 0; k < 3; k++) ui::menuStep(menu, st, inRight);
    for (int c = 0; c < 5; c++)
    {
        CHECK_EQ(2, extra::olimarResolvedMaturityTarget(main::settings().fighterMechanics, c));
    }
    st.cursor[ui::pages::P_OLIMAR] = 11; // Blue Maturity -> Leaf (1): step left twice from 3 -> 1
    ui::MenuInput inLeft = {0, 0, 1, 0, false, false, false, false};
    ui::menuStep(menu, st, inLeft);
    ui::menuStep(menu, st, inLeft);
    CHECK_EQ(0, extra::olimarResolvedMaturityTarget(main::settings().fighterMechanics, extra::kOliColorBlue));
    CHECK_EQ(2, extra::olimarResolvedMaturityTarget(main::settings().fighterMechanics, extra::kOliColorRed));
    CHECK_EQ(0, static_cast<int>(main::settings().fighterMechanics.olimarMaturity)); // mixed -> global 0

    st.cursor[ui::pages::P_OLIMAR] = 14; // Start Slot 1 -> Purple (5)
    for (int k = 0; k < 5; k++) ui::menuStep(menu, st, inRight);
    CHECK_EQ(static_cast<int>(main::kStartSlotPurple),
             static_cast<int>(extra::olimarStartSlot(main::settings().fighterMechanics, 0)));

    st.cursor[ui::pages::P_OLIMAR] = 18; // Preset D-Up -> 6 (2Y + 2P)
    for (int k = 0; k < 6; k++) ui::menuStep(menu, st, inRight);
    CHECK_EQ(6, static_cast<int>(extra::olimarDpadPreset(main::settings().fighterMechanics, extra::kOliDirUp)));

    // Persist to SD card and reload from scratch on a clean boot.
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::onRender(host::fakes::match());
    }
    storage::detach();

    memset(block, 0, iface::kSettingsBlockSize);
    memset(ui::menuScratch(), 0, ui::kScratchBytes);
    main::loadSettings(block);
    storage::attach(block);
    const main::FighterMechanicsSettings& loaded = main::settings().fighterMechanics;
    CHECK_EQ(static_cast<int>(main::kOlimarDpadPluck), static_cast<int>(extra::olimarDpadMode(loaded)));
    CHECK_EQ(0x1Eu, static_cast<u32>(extra::olimarEffectivePullMask(loaded)));
    CHECK_EQ(static_cast<int>(main::kPikminPurple), static_cast<int>(extra::olimarWhistleColor(loaded)));
    CHECK_EQ(0, extra::olimarResolvedMaturityTarget(loaded, extra::kOliColorBlue));
    CHECK_EQ(2, extra::olimarResolvedMaturityTarget(loaded, extra::kOliColorRed));
    CHECK_EQ(static_cast<int>(main::kStartSlotPurple), static_cast<int>(extra::olimarStartSlot(loaded, 0)));
    CHECK_EQ(6, static_cast<int>(extra::olimarDpadPreset(loaded, extra::kOliDirUp)));
    CHECK_EQ(static_cast<int>(oli.count), 22);

    // Re-entering P_OLIMAR re-syncs scratch mirrors from persisted bit-packed Settings.
    REQUIRE(ui::menuJump(menu, st, oliPath, 2));
    const ui::pages::OlimarMenuState* oms = ui::pages::olimarMenuState();
    CHECK_EQ(static_cast<int>(main::kOlimarDpadPluck), static_cast<int>(oms->dpadMode));
    CHECK_EQ(static_cast<int>(main::kPikminPurple), static_cast<int>(oms->whistleColor));
    CHECK_EQ(static_cast<int>(main::kPikminLeaf), static_cast<int>(oms->colorMat[extra::kOliColorBlue]));
    CHECK_EQ(static_cast<int>(main::kPikminFlower), static_cast<int>(oms->colorMat[extra::kOliColorRed]));
    CHECK_EQ(static_cast<int>(main::kStartSlotPurple), static_cast<int>(oms->startSlot[0]));
    CHECK_EQ(6, static_cast<int>(oms->dpadPreset[0]));

    storage::detach();
    main::setExtraHooks(NULL);
}

TEST(olimar_lab, legacy_olimar_pikmin_migration_and_page_reset)
{
    bootClean();
    s_fakeOlimarInMatch = true;
    s_fakeRngApplyCalls = 0;
    main::setExtraHooks(&kOlimarTestHooks);

    // 3A: Pre-B4-A 25-byte save has olimarPikmin = Blue (3) and default pull pool 0x1F.
    // Entering P_OLIMAR (olimarSyncFromSettings) must migrate it into olimarPullAndWhistle
    // bit 2 (Blue only = 0x04) and retire olimarPikmin = 0 so Pull toggles reflect Blue immediately.
    main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
    fm.olimarPikmin = main::kPikminBlue;
    fm.olimarPullAndWhistle = main::kOlimarPullPoolAll;

    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);
    const u8 oliPath[2] = {ui::pages::P_STAGE_CAST, ui::pages::P_OLIMAR};
    REQUIRE(ui::menuJump(menu, st, oliPath, 2));
    CHECK_EQ(static_cast<u32>(1u << extra::kOliColorBlue), static_cast<u32>(extra::olimarRawPullMask(fm)));
    CHECK_EQ(static_cast<int>(main::kPikminDefault), static_cast<int>(fm.olimarPikmin));

    // Also verify zero-initialized raw pull mask migrates identically.
    fm.olimarPikmin = main::kPikminPurple;
    fm.olimarPullAndWhistle = 0u;
    ui::pages::olimarSyncFromSettings();
    CHECK_EQ(static_cast<u32>(1u << extra::kOliColorPurple), static_cast<u32>(extra::olimarRawPullMask(fm)));
    CHECK_EQ(static_cast<int>(main::kPikminDefault), static_cast<int>(fm.olimarPikmin));

    // 3B: Mutate all 22 Olimar settings and verify 2-tap Y Reset Page resets all of them,
    // re-syncs scratch mirrors, and calls extraCharacterRng().
    fm.olimarPikmin = main::kPikminRed;
    fm.olimarMaturity = main::kPikminFlower;
    extra::olimarSetRawPullMask(fm, 0x05u);
    extra::olimarSetWhistleColor(fm, main::kPikminYellow);
    extra::olimarSetDpadMode(fm, main::kOlimarDpadPresets);
    for (int c = 0; c < extra::kOliColorCount; c++)
    {
        extra::olimarSetColorMaturityRaw(fm, c, main::kPikminBud);
    }
    for (int i = 0; i < 4; i++)
    {
        extra::olimarSetStartSlot(fm, i, static_cast<u8>(i + 1));
        extra::olimarSetDpadPreset(fm, i, static_cast<u8>(i + 2));
    }
    ui::pages::olimarSyncFromSettings();

    ui::MenuInput inResetPage = {0, 0, 0, 0, false, false, false, false, false, true};
    int applyBefore = s_fakeRngApplyCalls;

    // First Y tap arms confirmation without resetting yet.
    ui::menuStep(menu, st, inResetPage);
    CHECK(st.resetConfirmPage != 0);
    CHECK_EQ(static_cast<int>(main::kOlimarDpadPresets), static_cast<int>(extra::olimarDpadMode(fm)));

    // Second Y tap executes full Olimar page reset.
    ui::menuStep(menu, st, inResetPage);
    CHECK_EQ(0, static_cast<int>(st.resetConfirmPage));
    CHECK_EQ(0, static_cast<int>(fm.olimarPikmin));
    CHECK_EQ(0, static_cast<int>(fm.olimarMaturity));
    CHECK_EQ(static_cast<u32>(main::kOlimarPullPoolAll), static_cast<u32>(fm.olimarPullAndWhistle));
    CHECK_EQ(0u, static_cast<u32>(fm.olimarColorMatAndMode));
    CHECK_EQ(0u, static_cast<u32>(fm.olimarStartLineup));
    CHECK_EQ(0u, static_cast<u32>(fm.olimarDpadPresets));
    CHECK(s_fakeRngApplyCalls > applyBefore);

    const ui::pages::OlimarMenuState* oms = ui::pages::olimarMenuState();
    CHECK_EQ(0, static_cast<int>(oms->dpadMode));
    CHECK_EQ(0, static_cast<int>(oms->whistleColor));
    CHECK_EQ(0, static_cast<int>(oms->allMaturity));
    for (int c = 0; c < extra::kOliColorCount; c++)
    {
        CHECK_EQ(0, static_cast<int>(oms->colorMat[c]));
    }
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(0, static_cast<int>(oms->startSlot[i]));
        CHECK_EQ(0, static_cast<int>(oms->dpadPreset[i]));
    }

    main::setExtraHooks(NULL);
}
