// Stage 5D host unit tests:
//  - Can Walljump & Wall Cling info display formatting (main/ui/wall_info_core.h)
//  - Nana Throw ai_nana 0x6100 override (main/cpu/nana_throw_core.h)
//  - InfoValue enum stability, Fighter RNG Nana Throw row, and settings storage
#include <stddef.h>
#include <string.h>

#include "host/card.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/cpu/nana_throw_core.h"
#include "main/game/techniques.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/info_display.h"
#include "main/ui/lab_menu.h"
#include "main/ui/pages/pages.h"
#include "main/ui/wall_info_core.h"

namespace {
    using namespace tm;

    storage::FileHeader* header(const char* name)
    {
        return reinterpret_cast<storage::FileHeader*>(host::card::data(name));
    }

    storage::AreaEntry* areaEntry(const char* name, int id)
    {
        u8* file = host::card::data(name);
        storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(file + sizeof(storage::FileHeader));
        storage::AreaEntry* table =
            reinterpret_cast<storage::AreaEntry*>(file + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
        for (int i = 0; i < head->areaCount; i++)
        {
            if (table[i].id == id)
            {
                return &table[i];
            }
        }
        return NULL;
    }

    void reseal(const char* name)
    {
        storage::FileHeader* h = header(name);
        h->checksum = storage::checksum(host::card::data(name) + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    }
}

TEST(stage5d, can_walljump_formatting)
{
    char buf[64];

    // Fighter without walljump (e.g. Mewtwo 0x26, Roy 0x27) always reads No.
    ui::formatCanWalljump(0, false, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: No") == 0);
    ui::formatCanWalljump(0, true, 1, 15, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: No") == 0);

    // Fighter with walljump (e.g. Mario, Sheik, Lucario, Knuckles): Yes when ready.
    ui::formatCanWalljump(1, false, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: Yes") == 0);
    ui::formatCanWalljump(1, true, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: Yes") == 0);
    // Grounded resets cooldown even if a stale value were passed.
    ui::formatCanWalljump(1, false, 1, 12, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: Yes") == 0);

    // Airborne during 20f walljump cooldown (LA-Basic[4] counting down 20..1).
    ui::formatCanWalljump(1, true, 1, 20, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: CD 20f") == 0);
    ui::formatCanWalljump(1, true, 2, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "Can Walljump: CD 1f") == 0);
}

TEST(stage5d, wall_cling_formatting)
{
    char buf[64];

    // Non-clinger (Mario 0x00, Mewtwo 0x26, Lucario 0x21 patched to 0 in P+).
    ui::formatWallCling(0, 0x00, false, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: No") == 0);
    ui::formatWallCling(0, 0x21, true, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: No") == 0);

    // Clingers (Sheik 0x0E, Diddy 0x1B, Squirtle 0x1E): Yes when unused, Used when airborne after 1 cling.
    ui::formatWallCling(1, 0x0E, false, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Yes") == 0);
    ui::formatWallCling(1, 0x0E, true, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Yes") == 0);
    ui::formatWallCling(1, 0x0E, true, 1, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Used") == 0);
    ui::formatWallCling(1, 0x1B, true, 0, 100, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Used") == 0);
    // Grounded resets availability.
    ui::formatWallCling(1, 0x1E, false, 1, 100, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Yes") == 0);

    // Knuckles (0x2D): custom WallClimb* actions, reports Climb even though ftInfo 23190 == 0.
    ui::formatWallCling(0, ui::kFtKindKnuckles, false, 0, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Climb") == 0);
    ui::formatWallCling(0, ui::kFtKindKnuckles, true, 1, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wall Cling: Climb") == 0);
}

TEST(stage5d, info_display_enum_and_labels)
{
    // Existing enum indices remain untouched; Wall Cling appended at the end.
    CHECK_EQ(24, static_cast<int>(ui::kInfoCanWalljump));
    CHECK_EQ(35, static_cast<int>(ui::kInfoActIn));
    CHECK_EQ(36, static_cast<int>(ui::kInfoWallCling));
    CHECK_EQ(37, static_cast<int>(ui::kInfoValueCount));

    CHECK(ui::infoValueAvailable(ui::kInfoCanWalljump));
    CHECK(strcmp(ui::infoValueLabel(ui::kInfoCanWalljump), "Can Walljump") == 0);
    CHECK(ui::infoValueAvailable(ui::kInfoWallCling));
    CHECK(strcmp(ui::infoValueLabel(ui::kInfoWallCling), "Wall Cling") == 0);
}

TEST(stage5d, nana_throw_override_directions_and_gating)
{
    cpu::Frame uthrow = {cpu::button::kFlickJump, 0.0f, 1.0f};
    cpu::Frame fthrow = {0, 1.0f, 0.0f};
    cpu::Frame pummel = {cpu::button::kAttack, 0.0f, 0.0f};
    cpu::Frame neutral = {cpu::button::kFlickJump, 0.1f, -0.2f};
    cpu::Frame out;

    // Default or out-of-range mode: no override.
    out = uthrow;
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowDefault, cpu::kNanaStatusCatchWait, 1.0f, uthrow, out));
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowCount, cpu::kNanaStatusCatchWait, 1.0f, uthrow, out));

    // Wrong status: no override.
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowDown, 0x00, 1.0f, uthrow, out));
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowDown, 0x34, 1.0f, uthrow, out));
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowDown, 0x3A, 1.0f, uthrow, out));

    // Neutral wait frame inside Catch_Wait: preserve natural 0x6100 wait timing.
    CHECK(!cpu::applyNanaThrowOverride(main::kNanaThrowForward, cpu::kNanaStatusCatchWait, 1.0f, neutral, out));

    // Forward throw respects facing lr (+1 right, -1 left) and clears buttons.
    out = uthrow;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowForward, cpu::kNanaStatusCatchWait, 1.0f, uthrow, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(1.0f, out.x, 0.001f);
    CHECK_NEAR(0.0f, out.y, 0.001f);

    out = uthrow;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowForward, cpu::kNanaStatusCatchWait, -1.0f, uthrow, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(-1.0f, out.x, 0.001f);
    CHECK_NEAR(0.0f, out.y, 0.001f);

    // Back throw flips facing lr and suppresses attack button (no stray pummel).
    out = pummel;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowBack, cpu::kNanaStatusCatchWait, 1.0f, pummel, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(-1.0f, out.x, 0.001f);
    CHECK_NEAR(0.0f, out.y, 0.001f);

    out = fthrow;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowBack, cpu::kNanaStatusCatchWait, -1.0f, fthrow, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(1.0f, out.x, 0.001f);
    CHECK_NEAR(0.0f, out.y, 0.001f);

    // Up throw and Down throw are facing-independent.
    out = fthrow;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowUp, cpu::kNanaStatusCatchWait, -1.0f, fthrow, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(0.0f, out.x, 0.001f);
    CHECK_NEAR(1.0f, out.y, 0.001f);

    out = uthrow;
    CHECK(cpu::applyNanaThrowOverride(main::kNanaThrowDown, cpu::kNanaStatusCatchWait, 1.0f, uthrow, out));
    CHECK_EQ(0u, out.buttons);
    CHECK_NEAR(0.0f, out.x, 0.001f);
    CHECK_NEAR(-1.0f, out.y, 0.001f);
}

TEST(stage5d, nana_throw_menu_row_and_storage_compat)
{
    CHECK_EQ(454u, offsetof(main::Settings, fighterMechanics.nanaThrow));
    CHECK_EQ(32u, sizeof(main::FighterMechanicsSettings));
    CHECK_EQ(464u, sizeof(main::Settings));

    // Nana Throw is at index 32 (after Ganon Float at 31); Olimar Pikmin & Maturity follow at 33..34.
    const ui::Page* page = ui::labMenu().pages[ui::pages::P_STAGE_CAST];
    REQUIRE(page != NULL);
    CHECK_EQ(35, page->count);
    CHECK(strcmp(page->options[31].name, "Ganon Float") == 0);
    CHECK(strcmp(page->options[32].name, "Nana Throw") == 0);
    CHECK(strcmp(page->options[33].name, "Olimar Pikmin") == 0);
    CHECK(strcmp(page->options[34].name, "Olimar Maturity") == 0);
    CHECK_EQ(main::kNanaThrowCount - 1, static_cast<int>(page->options[32].max));
    CHECK_EQ(main::kOlimarPikminCount - 1, static_cast<int>(page->options[33].max));
    CHECK_EQ(main::kOlimarMaturityCount - 1, static_cast<int>(page->options[34].max));

    // Storage round-trip and backwards compatibility with pre-nanaThrow area length (22 bytes).
    storage::detach();
    host::card::reset();
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::attach(block);

    main::settings().fighterMechanics.ganonFloat = main::kMechAlways;
    main::settings().fighterMechanics.nanaThrow = main::kNanaThrowDown;
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::onRender(host::fakes::match());
    }
    storage::detach();

    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::attach(block);
    CHECK_EQ(main::kMechAlways, main::settings().fighterMechanics.ganonFloat);
    CHECK_EQ(main::kNanaThrowDown, main::settings().fighterMechanics.nanaThrow);
    storage::detach();

    // Truncate kAreaFighterMechanics to 22 bytes (before nanaThrow): loads Default (0).
    storage::AreaEntry* area = areaEntry("settings-a.bin", storage::kAreaFighterMechanics);
    REQUIRE(area != NULL);
    CHECK_EQ(25u, area->used);
    area->used = offsetof(main::FighterMechanicsSettings, nanaThrow);
    CHECK_EQ(22u, area->used);
    reseal("settings-a.bin");

    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::attach(block);
    CHECK_EQ(main::kMechAlways, main::settings().fighterMechanics.ganonFloat);
    CHECK_EQ(main::kNanaThrowDefault, main::settings().fighterMechanics.nanaThrow);
    storage::detach();

    // Ledgedash struct carries grabCount cleanly.
    game::Ledgedash l;
    memset(&l, 0, sizeof(l));
    l.grabCount = 6;
    CHECK_EQ(6, static_cast<int>(l.grabCount));
}
