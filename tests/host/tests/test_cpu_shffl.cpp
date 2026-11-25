// Host unit tests for CPU Auto L-Cancel, SHFFL / FFL counter actions,
// SHFFL Timing (src/main/cpu/shffl_core.h, actions.cpp), P_CPU menu rows,
// descriptions, and 100% SD backward-compatible CpuOptions storage migration.
#include <stddef.h>
#include <string.h>

#include "extra/menu_descriptions.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/cpu/actions.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/shffl_core.h"
#include "main/game/status_ids.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

using namespace tm;

namespace {
    enum {
        kMario = 0x0,
        kLink = 0x2,
        kSamus = 0x3,
        kFox = 0x6,
        kNess = 0xA,
        kPeach = 0xC,
        kGanon = 0x14,
        kLucas = 0x1A,
        kMewtwo = 0x26
    };

    cpu::AutoLCancelInput baseLCancel()
    {
        cpu::AutoLCancelInput in;
        in.enabled = true;
        in.status = game::kStatusAttackAirFirst;
        in.air = true;
        in.canActNow = false;
        in.actInFrames = 10;
        in.hitlag = 0;
        in.lCancelTimer = 0;
        in.prevGuardHeld = false;
        in.attackPressed = false;
        in.vy = -1.5f;
        in.framesToLand = 3;
        in.hasGround = true;
        in.heightAboveGround = 4.5f;
        return in;
    }

    void bootStorage()
    {
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }

    int findStep(const cpu::Step* steps, int count, u16 buttons, u8 until)
    {
        for (int i = 0; i < count && steps[i].frames != 0; i++)
        {
            if (steps[i].buttons == buttons && steps[i].until == until)
            {
                return i;
            }
        }
        return -1;
    }
}

// ---- Auto L-Cancel eligibility (shffl_core.h) -------------------------------

TEST(cpu_shffl, auto_lcancel_eligibility_and_safety_guardrails)
{
    // Active AttackAir approaching landing: eligible for lCancelTimer == 0 and
    // negative values (P+'s decInt decrements LA-Basic[90] below 0).
    cpu::AutoLCancelInput in = baseLCancel();
    CHECK(cpu::shouldAutoLCancel(in));
    in.lCancelTimer = -12;
    CHECK(cpu::shouldAutoLCancel(in));

    // Boundary of framesToLand (0..5 inclusive accepted, 6 rejected when high).
    in = baseLCancel();
    in.heightAboveGround = 12.0f;
    in.framesToLand = 0;
    CHECK(cpu::shouldAutoLCancel(in));
    in.framesToLand = cpu::kAutoLCancelLead;
    CHECK(cpu::shouldAutoLCancel(in));
    in.framesToLand = cpu::kAutoLCancelLead + 1;
    CHECK(!cpu::shouldAutoLCancel(in));
    in.framesToLand = -1;
    CHECK(!cpu::shouldAutoLCancel(in));

    // Near-ground fallback when descending or on final hitlag frame (hitlag == 1).
    in = baseLCancel();
    in.framesToLand = -1;
    in.vy = -0.4f;
    in.heightAboveGround = 3.5f;
    CHECK(cpu::shouldAutoLCancel(in));
    in.vy = 0.0f;
    CHECK(cpu::shouldAutoLCancel(in));
    // Rising high or near ground with no predicted landing: rejected.
    in.vy = 0.8f;
    CHECK(!cpu::shouldAutoLCancel(in));
    // Final hitlag frame (hitlag == 1) near ground: allowed (execStop sets 8).
    in.vy = 0.0f;
    in.hitlag = 1;
    CHECK(cpu::shouldAutoLCancel(in));
    // Offstage (no floor below, real ground rays miss -> framesToLand == -1
    // even while descending across y = groundY): must never fire or trigger
    // 40f tech lockout.
    in = baseLCancel();
    in.framesToLand = -1;
    in.hasGround = false;
    in.vy = -2.2f;
    in.heightAboveGround = 0.5f;
    CHECK(!cpu::shouldAutoLCancel(in));
    in.hitlag = 1;
    in.vy = 0.0f;
    CHECK(!cpu::shouldAutoLCancel(in));
    // Moving horizontally from offstage onto the stage edge where a real
    // forward ground ray hits within kAutoLCancelLead still L-cancels cleanly.
    in = baseLCancel();
    in.hasGround = false;
    in.framesToLand = 2;
    CHECK(cpu::shouldAutoLCancel(in));

    // Guardrail: Auto L-Cancel Off never fires.
    in = baseLCancel();
    in.enabled = false;
    CHECK(!cpu::shouldAutoLCancel(in));

    // Guardrail: never outside AttackAir (Fall, Jump, JumpAerial, LandingAttackAir).
    static const int kBadStatuses[] = {game::kStatusWait, game::kStatusJump, game::kStatusJumpAerial,
                                       game::kStatusFall, game::kStatusFallAerial, game::kStatusLandingAttackAir,
                                       game::kStatusEscapeAir};
    for (u32 i = 0; i < sizeof(kBadStatuses) / sizeof(kBadStatuses[0]); i++)
    {
        in = baseLCancel();
        in.status = kBadStatuses[i];
        CHECK(!cpu::shouldAutoLCancel(in));
    }

    // Guardrail: never when grounded.
    in = baseLCancel();
    in.air = false;
    CHECK(!cpu::shouldAutoLCancel(in));

    // Guardrail: never during IASA or the frame IASA opens (prevents airdodge/Zair).
    in = baseLCancel();
    in.canActNow = true;
    CHECK(!cpu::shouldAutoLCancel(in));
    in = baseLCancel();
    in.actInFrames = 0;
    CHECK(!cpu::shouldAutoLCancel(in));
    in.actInFrames = 1;
    CHECK(!cpu::shouldAutoLCancel(in));
    in.actInFrames = 2;
    CHECK(cpu::shouldAutoLCancel(in));
    in.actInFrames = -1; // extra module absent: falls back to canActNow/groups
    CHECK(cpu::shouldAutoLCancel(in));

    // Guardrail: never while hitlag > 1, while LA-Basic[90] > 0, while Guard is
    // already held (needs fresh rising edge), or while Attack is pressed.
    in = baseLCancel();
    in.hitlag = 2;
    CHECK(!cpu::shouldAutoLCancel(in));
    in = baseLCancel();
    in.lCancelTimer = 1;
    CHECK(!cpu::shouldAutoLCancel(in));
    in.lCancelTimer = 7;
    CHECK(!cpu::shouldAutoLCancel(in));
    in = baseLCancel();
    in.prevGuardHeld = true;
    CHECK(!cpu::shouldAutoLCancel(in));
    in = baseLCancel();
    in.attackPressed = true;
    CHECK(!cpu::shouldAutoLCancel(in));
}

// ---- SHFFL Timing presets & script builder (shffl_core.h) -------------------

TEST(cpu_shffl, timing_resolution_random_bounds_and_script_builder)
{
    CHECK_EQ(cpu::kShfflFramePerfect, cpu::resolveShfflTiming(cpu::kShfflFramePerfect, 99));
    CHECK_EQ(cpu::kShfflEarly, cpu::resolveShfflTiming(cpu::kShfflEarly, 99));
    CHECK_EQ(cpu::kShfflNormal, cpu::resolveShfflTiming(cpu::kShfflNormal, 99));
    CHECK_EQ(cpu::kShfflLate, cpu::resolveShfflTiming(cpu::kShfflLate, 99));
    // Invalid setting falls back to Frame-Perfect.
    CHECK_EQ(cpu::kShfflFramePerfect, cpu::resolveShfflTiming(99, 3));

    // Random covers all 4 deterministic timings within 0..3.
    int seen[4] = {0, 0, 0, 0};
    for (u32 r = 0; r < 64; r++)
    {
        u8 t = cpu::resolveShfflTiming(cpu::kShfflRandom, r);
        REQUIRE(t < 4);
        seen[t]++;
    }
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(16, seen[i]);
    }

    const cpu::Step nair = cpu::makeStep(cpu::button::kAttack, 0, 0, cpu::kRefAbs, 1, cpu::kUntilNone);
    const cpu::Step fair =
        cpu::makeStep(cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick, 100, 0, cpu::kRefForward, 1,
                      cpu::kUntilNone);
    cpu::Step buf[cpu::kMaxShfflSteps];

    // Guard invalid buffer args.
    CHECK_EQ(0, cpu::buildShfflScript(NULL, cpu::kMaxShfflSteps, nair, true, cpu::kShfflFramePerfect, 0));
    CHECK_EQ(0, cpu::buildShfflScript(buf, 1, nair, true, cpu::kShfflFramePerfect, 0));

    // 1) Frame-Perfect SHFFL: takeoff -> immediate aerial -> kUntilFalling -> immediate fastfall.
    int nFp = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, nair, true, cpu::kShfflFramePerfect, 0);
    REQUIRE(nFp > 4 && nFp <= cpu::kMaxShfflSteps);
    CHECK_EQ(0, buf[nFp - 1].frames);
    CHECK_EQ(cpu::button::kJump, buf[0].buttons);
    CHECK_EQ(cpu::kUntilTakeoff, buf[1].until);
    CHECK_EQ(cpu::button::kAttack, buf[2].buttons);
    CHECK_EQ(cpu::kUntilFalling, buf[3].until);
    CHECK_EQ(-100, buf[4].y);
    CHECK_EQ(cpu::kUntilFastfall, buf[4].until);

    // 2) Early SHFFL: takeoff -> 2f delay -> aerial -> kUntilFalling -> 1f delay -> fastfall.
    int nEarly = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, fair, true, cpu::kShfflEarly, 0);
    REQUIRE(nEarly > 6 && nEarly <= cpu::kMaxShfflSteps);
    CHECK_EQ(0, buf[nEarly - 1].frames);
    CHECK_EQ(cpu::kShfflEarlyAerialDelay, buf[2].frames);
    CHECK_EQ(0, buf[2].buttons);
    CHECK_EQ(fair.buttons, buf[3].buttons);
    CHECK_EQ(100, buf[3].x);
    CHECK_EQ(cpu::kRefForward, buf[3].ref);
    CHECK_EQ(cpu::kUntilFalling, buf[4].until);
    CHECK_EQ(cpu::kShfflEarlyFfDelay, buf[5].frames);
    CHECK_EQ(cpu::kUntilFastfall, buf[6].until);

    // 3) Normal SHFFL: takeoff -> 4f delay -> aerial -> kUntilFalling -> 2f delay -> fastfall.
    int nNorm = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, nair, true, cpu::kShfflNormal, 0);
    REQUIRE(nNorm > 6 && nNorm <= cpu::kMaxShfflSteps);
    CHECK_EQ(0, buf[nNorm - 1].frames);
    CHECK_EQ(cpu::kShfflNormalAerialDelay, buf[2].frames);
    CHECK_EQ(cpu::button::kAttack, buf[3].buttons);
    CHECK_EQ(cpu::kUntilFalling, buf[4].until);
    CHECK_EQ(cpu::kShfflNormalFfDelay, buf[5].frames);
    CHECK_EQ(cpu::kUntilFastfall, buf[6].until);

    // 4) Late SHFFL: takeoff -> neutral -> kUntilFalling -> late apex aerial -> immediate fastfall!
    int nLate = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, fair, true, cpu::kShfflLate, 0);
    REQUIRE(nLate > 6 && nLate <= cpu::kMaxShfflSteps);
    CHECK_EQ(0, buf[nLate - 1].frames);
    int fallIdx = findStep(buf, nLate, 0, cpu::kUntilFalling);
    int aerIdx = findStep(buf, nLate, fair.buttons, cpu::kUntilNone);
    int ffIdx = findStep(buf, nLate, 0, cpu::kUntilFastfall);
    CHECK(fallIdx >= 2);
    CHECK_EQ(fallIdx + 1, aerIdx);
    CHECK_EQ(aerIdx + 1, ffIdx);

    // 5) FFL (shortHop = false): no jump step; Late delays post-apex then aerial then FF.
    int nFflFp = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, nair, false, cpu::kShfflFramePerfect, 0);
    REQUIRE(nFflFp > 3 && nFflFp <= cpu::kMaxShfflSteps);
    CHECK_EQ(cpu::button::kAttack, buf[0].buttons);
    CHECK_EQ(cpu::kUntilFalling, buf[1].until);
    CHECK_EQ(cpu::kUntilFastfall, buf[2].until);

    int nFflLate = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, nair, false, cpu::kShfflLate, 0);
    REQUIRE(nFflLate > 4 && nFflLate <= cpu::kMaxShfflSteps);
    CHECK_EQ(cpu::kUntilFalling, buf[0].until);
    CHECK_EQ(cpu::kShfflLateTumbleDelay, buf[1].frames);
    CHECK_EQ(cpu::button::kAttack, buf[2].buttons);
    CHECK_EQ(cpu::kUntilFastfall, buf[3].until);

    // 6) Random writes resolvedTimingOut in 0..3.
    u8 resolved = 99;
    int nRand = cpu::buildShfflScript(buf, cpu::kMaxShfflSteps, nair, true, cpu::kShfflRandom, 2, &resolved);
    CHECK(nRand > 4 && nRand <= cpu::kMaxShfflSteps);
    CHECK_EQ(cpu::kShfflNormal, resolved);
}

// ---- Action tables, Ganon floats, menu rows & descriptions ------------------

TEST(cpu_shffl, action_tables_preserve_indices_fix_ganon_floats_and_expose_shffl_ffl)
{
    // Action counts match UI label tables exactly.
    CHECK_EQ(ui::pages::kCounterGroundSize, cpu::actionCount(cpu::kListGround));
    CHECK_EQ(ui::pages::kCounterAirSize, cpu::actionCount(cpu::kListAir));
    CHECK_EQ(ui::pages::kCounterShieldSize, cpu::actionCount(cpu::kListShield));
    CHECK_EQ(56, cpu::actionCount(cpu::kListGround));
    CHECK_EQ(34, cpu::actionCount(cpu::kListAir));
    CHECK_EQ(49, cpu::actionCount(cpu::kListShield));

    // Existing indices untouched: Random at 40 (ground), 28 (air), 33 (shield);
    // DJC at 41..45 (ground), 34..38 (shield); Float at 46..50 (ground), 39..43 (shield).
    CHECK_EQ(cpu::kActionRandom, cpu::actionKind(cpu::kListGround, 40));
    CHECK_EQ(cpu::kActionRandom, cpu::actionKind(cpu::kListAir, 28));
    CHECK_EQ(cpu::kActionRandom, cpu::actionKind(cpu::kListShield, 33));

    const int floatStarts[2] = {46, 39};
    const int shfflStarts[2] = {51, 44};
    const int lists[2] = {cpu::kListGround, cpu::kListShield};
    const s8 wantX[5] = {0, 100, -100, 0, 0};
    const s8 wantY[5] = {0, 0, 0, 100, -100};
    const u8 wantRef[5] = {cpu::kRefAbs, cpu::kRefForward, cpu::kRefForward, cpu::kRefAbs, cpu::kRefAbs};
    const u16 wantBtn[5] = {
        cpu::button::kAttack,
        cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick,
        cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick,
        cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick,
        cpu::button::kAttack | cpu::button::kSmash | cpu::button::kCStick
    };

    for (int l = 0; l < 2; l++)
    {
        for (int i = 0; i < 5; i++)
        {
            // Ganon float mapping MUST still point to kGanonFloats[0..4] after SHFFL append!
            const cpu::Action* ganon = cpu::actionFor(lists[l], floatStarts[l] + i, kGanon);
            REQUIRE(ganon != NULL);
            CHECK((ganon->flags & cpu::kForFloat) != 0);
            CHECK_EQ(cpu::button::kSpecial, ganon->steps[2].buttons);
            CHECK_EQ(wantBtn[i], ganon->steps[4].buttons);
            CHECK_EQ(wantX[i], ganon->steps[4].x);
            CHECK_EQ(wantY[i], ganon->steps[4].y);
            CHECK_EQ(wantRef[i], ganon->steps[4].ref);

            // SHFFL actions sit at 51..55 (ground) / 44..48 (shield) and fit ALL fighters.
            const cpu::Action* shffl = cpu::actionFor(lists[l], shfflStarts[l] + i, kFox);
            REQUIRE(shffl != NULL);
            CHECK((shffl->flags & cpu::kForShffl) != 0);
            CHECK((shffl->flags & cpu::kEndsWithSteps) != 0);
            CHECK(cpu::actionFor(lists[l], shfflStarts[l] + i, kGanon) == shffl);
            CHECK(cpu::actionFor(lists[l], shfflStarts[l] + i, kSamus) == shffl);
            CHECK(cpu::actionFor(lists[l], shfflStarts[l] + i, kLink) == shffl);
            CHECK_EQ(cpu::button::kJump, shffl->steps[0].buttons);
            CHECK_EQ(cpu::kUntilTakeoff, shffl->steps[1].until);
            CHECK_EQ(wantBtn[i], shffl->steps[2].buttons);
            CHECK_EQ(wantX[i], shffl->steps[2].x);
            CHECK_EQ(wantY[i], shffl->steps[2].y);
            CHECK_EQ(wantRef[i], shffl->steps[2].ref);
            CHECK_EQ(cpu::kUntilFalling, shffl->steps[3].until);
        }
    }

    // FFL actions sit at 29..33 in kListAir and fit ALL fighters.
    for (int i = 0; i < 5; i++)
    {
        const cpu::Action* ffl = cpu::actionFor(cpu::kListAir, 29 + i, kMario);
        REQUIRE(ffl != NULL);
        CHECK((ffl->flags & cpu::kForFfl) != 0);
        CHECK((ffl->flags & cpu::kEndsWithSteps) != 0);
        CHECK(cpu::actionFor(cpu::kListAir, 29 + i, kLink) == ffl);
        CHECK_EQ(wantBtn[i], ffl->steps[0].buttons);
        CHECK_EQ(wantX[i], ffl->steps[0].x);
        CHECK_EQ(wantY[i], ffl->steps[0].y);
        CHECK_EQ(wantRef[i], ffl->steps[0].ref);
        CHECK_EQ(cpu::kUntilFalling, ffl->steps[1].until);
    }

    // Menu rows appended at END of P_CPU, with matching descriptions.
    const ui::Page& page = ui::pages::kCpuOptions;
    REQUIRE(page.count == 30);
    CHECK(strcmp(page.options[27].name, "Adaptive CPU") == 0);
    CHECK(strcmp(page.options[28].name, "Auto L-Cancel") == 0);
    CHECK_EQ(ui::kKindToggle, page.options[28].kind);
    CHECK_EQ(1, page.options[28].def);
    CHECK(strcmp(page.options[29].name, "SHFFL Timing") == 0);
    CHECK_EQ(ui::kKindChoice, page.options[29].kind);
    CHECK_EQ(cpu::kShfflFramePerfect, page.options[29].def);
    CHECK_EQ(cpu::kShfflTimingCount - 1, page.options[29].max);

    for (int r = 0; r < page.count; r++)
    {
        const char* desc = extra::optionDesc(ui::pages::P_CPU, static_cast<u8>(r));
        CHECK(desc != NULL && desc[0] != '\0');
    }
}

// ---- SD Backward-Compatible Storage Migration -------------------------------

TEST(cpu_shffl, storage_migration_from_44_byte_cpu_options_and_round_trip)
{
    CHECK_EQ(46u, sizeof(cpu::CpuOptions));
    CHECK_EQ(64u, sizeof(main::CpuSettings));
    CHECK_EQ(436u, sizeof(main::Settings));
    CHECK_EQ(15u, sizeof(main::settings().cpu.reserved));

    host::card::reset();
    bootStorage();
    // Fresh defaults: Auto L-Cancel = On (1), SHFFL Timing = Frame-Perfect (0).
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.autoLCancel));
    CHECK_EQ(static_cast<int>(cpu::kShfflFramePerfect), static_cast<int>(main::settings().cpu.options.shfflTiming));

    // Persist non-default values (Off + Late) and verify round-trip.
    main::settings().cpu.options.dualStickSdi = 1;
    main::settings().cpu.options.autoLCancel = 0;
    main::settings().cpu.options.shfflTiming = cpu::kShfflLate;
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::settingsUpdate();
    }
    storage::detach();

    bootStorage();
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.dualStickSdi));
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.autoLCancel));
    CHECK_EQ(static_cast<int>(cpu::kShfflLate), static_cast<int>(main::settings().cpu.options.shfflTiming));

    // Persist Random (4) + On (1) and verify round-trip.
    main::settings().cpu.options.autoLCancel = 1;
    main::settings().cpu.options.shfflTiming = cpu::kShfflRandom;
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::settingsUpdate();
    }
    storage::detach();

    bootStorage();
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.autoLCancel));
    CHECK_EQ(static_cast<int>(cpu::kShfflRandom), static_cast<int>(main::settings().cpu.options.shfflTiming));
    storage::detach();

    // Simulate a pre-SHFFL 44-byte CpuOptions file (kAreaCpu.used == 47) where
    // the truncated bytes on disk are zeroed: loading MUST migrate to defaults
    // (autoLCancel = 1, shfflTiming = 0) while keeping earlier fields intact!
    const char* file = host::card::has("settings-b.bin") ? "settings-b.bin" : "settings-a.bin";
    REQUIRE(host::card::has(file));
    u8* raw = host::card::data(file);
    storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(raw + sizeof(storage::FileHeader));
    storage::AreaEntry* table =
        reinterpret_cast<storage::AreaEntry*>(raw + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
    storage::AreaEntry* cpuArea = NULL;
    for (int i = 0; i < head->areaCount; i++)
    {
        if (table[i].id == storage::kAreaCpu)
        {
            cpuArea = &table[i];
            break;
        }
    }
    REQUIRE(cpuArea != NULL);
    CHECK_EQ(49u, static_cast<u32>(cpuArea->used));
    const u32 preShfflUsed = offsetof(main::CpuSettings, options) + offsetof(cpu::CpuOptions, autoLCancel);
    CHECK_EQ(47u, preShfflUsed);
    // Zero the two bytes past used = 47 on disk to prove defaults come from
    // applyMenuDefaults(), not leftover file bytes.
    raw[cpuArea->offset + 47] = 0;
    raw[cpuArea->offset + 48] = 3;
    cpuArea->used = static_cast<u8>(preShfflUsed);
    storage::FileHeader* fh = reinterpret_cast<storage::FileHeader*>(raw);
    fh->checksum = storage::checksum(raw + storage::kChecksumFrom, fh->size - storage::kChecksumFrom);

    bootStorage();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.dualStickSdi));
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.autoLCancel));
    CHECK_EQ(static_cast<int>(cpu::kShfflFramePerfect), static_cast<int>(main::settings().cpu.options.shfflTiming));
    storage::detach();
}
