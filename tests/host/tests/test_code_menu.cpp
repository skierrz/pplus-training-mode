// Project+'s Code Menu state reader, Training activation block, and
// Controls > P+ Code Menu Shortcut live sync (src/shared/game/code_menu.h,
// src/main/game/code_menu.cpp, src/main/ui/pages/controls.cpp).
#include <string.h>

#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/code_menu.h"
#include "main/settings.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/pages/pages.h"
#include "shared/game/code_menu.h"

using namespace tm;

namespace {
    const u32 kRootPtr = 0x804E0004u;
    const u32 kRootAddr = 0x804E07D8u;
    const u32 kStateAddr = 0x804E0034u;
    const u32 kDefaultLine = 0x804E0C24u;
    const u32 kNetplayLine = 0x804E0C30u;
    const char kLabel[] = "Code Menu Activation:  %s";

    void writeActivationLine(u32 lineAddr, u32 value)
    {
        host::mem::setByte(lineAddr + 0, 0x00);
        host::mem::setByte(lineAddr + 1, 0x58);
        host::mem::setByte(lineAddr + 2, 0x00);
        host::mem::setByte(lineAddr + 5, 0x00);
        host::mem::setByte(lineAddr + 6, 0x28);
        host::mem::setWord(lineAddr + 0x08, value);
        host::mem::setWord(lineAddr + 0x10, 0);
        host::mem::setWord(lineAddr + 0x14, 2);
        for (u32 i = 0; i < sizeof(kLabel); i++)
        {
            host::mem::setByte(lineAddr + 0x28 + i, static_cast<u8>(kLabel[i]));
        }
    }

    void setUpMenu(u32 lineAddr, u32 value)
    {
        game::codeMenuMatchDetach();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        host::mem::setWord(kRootPtr, kRootAddr);
        host::mem::setWord(kStateAddr, 0);
        writeActivationLine(lineAddr, value);
        host::log::clear();
    }
}

TEST(code_menu, loaded_and_open_states_follow_root_pointer_and_state_word)
{
    CHECK(!game::codeMenuLoaded());
    CHECK(!game::codeMenuOpen());

    host::mem::setWord(kStateAddr, 4);
    CHECK(!game::codeMenuOpen()); // not loaded yet

    host::mem::setWord(kRootPtr, kRootAddr);
    CHECK(game::codeMenuLoaded());
    CHECK(game::codeMenuOpen());

    host::mem::setWord(kStateAddr, 0);
    CHECK(!game::codeMenuOpen());
    host::mem::setWord(kStateAddr, 1);
    CHECK(!game::codeMenuOpen());
    host::mem::setWord(kStateAddr, 2);
    CHECK(!game::codeMenuOpen());
    host::mem::setWord(kStateAddr, 3);
    CHECK(!game::codeMenuOpen());
    host::mem::setWord(kStateAddr, 4);
    CHECK(game::codeMenuOpen());
}

TEST(code_menu, training_attach_blocks_default_and_pm36_and_detach_restores)
{
    setUpMenu(kDefaultLine, 0);
    CHECK_EQ(0u, static_cast<u32>(main::settings().controls.allowCodeMenu));
    CHECK(game::codeMenuBlockedInTraining());

    // Sentinel values on neighbouring words must stay untouched.
    host::mem::setWord(kDefaultLine - 4, 0x11223344u);
    host::mem::setWord(kDefaultLine + 0x0C, 0x55667788u);

    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));
    CHECK_EQ(0x11223344u, host::mem::word(kDefaultLine - 4));
    CHECK_EQ(0x55667788u, host::mem::word(kDefaultLine + 0x0C));

    // A second attach before detach must not clobber the saved value.
    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));

    game::codeMenuMatchDetach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));

    // PM 3.6 (1) is also blocked and restored.
    setUpMenu(kDefaultLine, 1);
    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));
    game::codeMenuMatchDetach();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));

    // Already OFF (2) is left untouched.
    setUpMenu(kDefaultLine, 2);
    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));
    game::codeMenuMatchDetach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));
}

TEST(code_menu, live_controls_row_toggle_and_reset_all_sync_activation)
{
    // Player had PM 3.6 (1); starts blocked by default (allowCodeMenu = 0).
    setUpMenu(kDefaultLine, 1);
    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));

    // Controls > P+ Code Menu -> On restores player's exact 1 live.
    main::settings().controls.allowCodeMenu = 1;
    CHECK(!game::codeMenuBlockedInTraining());
    game::codeMenuSync();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));

    // Back to Off re-blocks immediately to 2.
    main::settings().controls.allowCodeMenu = 0;
    CHECK(game::codeMenuBlockedInTraining());
    game::codeMenuSync();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));

    // Back On, then Reset All Settings sets allowCodeMenu = 0 and re-blocks.
    main::settings().controls.allowCodeMenu = 1;
    game::codeMenuSync();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));
    main::resetSettings();
    game::codeMenuSync();
    CHECK_EQ(0u, static_cast<u32>(main::settings().controls.allowCodeMenu));
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));

    // Match end restores player's 1, never leaving OFF behind.
    game::codeMenuMatchDetach();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));

    // Starting a match with allowCodeMenu already On leaves Activation untouched,
    // toggling Off mid-match blocks it, and detach restores it.
    setUpMenu(kDefaultLine, 0);
    main::settings().controls.allowCodeMenu = 1;
    game::codeMenuMatchAttach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));
    main::settings().controls.allowCodeMenu = 0;
    game::codeMenuSync();
    CHECK_EQ(2u, host::mem::word(kDefaultLine + 0x08));
    game::codeMenuMatchDetach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));
}

TEST(code_menu, shifted_netplay_line_is_found_blocked_and_restored)
{
    setUpMenu(kNetplayLine, 0);
    game::codeMenuMatchAttach();
    CHECK_EQ(2u, host::mem::word(kNetplayLine + 0x08));
    game::codeMenuMatchDetach();
    CHECK_EQ(0u, host::mem::word(kNetplayLine + 0x08));
}

TEST(code_menu, non_training_matches_and_malformed_data_are_left_alone)
{
    setUpMenu(kDefaultLine, 0);
    host::fakes::match().training = false;
    game::codeMenuMatchAttach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));
    game::codeMenuMatchDetach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));

    // Missing/custom label in Training: changes nothing and logs once.
    host::fakes::match().training = true;
    setUpMenu(kDefaultLine, 1);
    host::mem::setByte(kDefaultLine + 0x28, 'X');
    game::codeMenuMatchAttach();
    game::codeMenuSync();
    game::codeMenuSync();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));
    CHECK_EQ(1, host::log::count("code menu: activation line not found, left alone"));
    game::codeMenuMatchDetach();
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));

    // Out-of-range activation value (> 2): rejected as malformed and untouched.
    setUpMenu(kDefaultLine, 3);
    game::codeMenuMatchAttach();
    CHECK_EQ(3u, host::mem::word(kDefaultLine + 0x08));
    CHECK_EQ(1, host::log::count("code menu: activation line not found, left alone"));
    game::codeMenuMatchDetach();
    CHECK_EQ(3u, host::mem::word(kDefaultLine + 0x08));

    // Unloaded Code Menu: left alone.
    host::mem::setWord(kRootPtr, 0);
    host::mem::setWord(kDefaultLine + 0x08, 0);
    game::codeMenuMatchAttach();
    CHECK_EQ(0u, host::mem::word(kDefaultLine + 0x08));
    game::codeMenuMatchDetach();
}

namespace {
    u8* dummyValues()
    {
        return reinterpret_cast<u8*>(&main::settings());
    }
}

TEST(code_menu, controls_row_placement_and_lab_reachability)
{
    setUpMenu(kDefaultLine, 1);

    // Check row name, wording, and placement on Controls page.
    const ui::Page& controls = ui::pages::kControls;
    REQUIRE(controls.count > 8);
    const ui::Option& row = controls.options[8];
    CHECK(row.name != NULL && strcmp(row.name, "P+ Code Menu Shortcut") == 0);
    CHECK(row.desc == NULL);
    CHECK_EQ(ui::kKindToggle, static_cast<int>(row.kind));

    // In Training Lab with row On: unblocked, row reachable.
    main::settings().controls.allowCodeMenu = 1;
    game::codeMenuMatchAttach();
    CHECK(!game::codeMenuBlockedInTraining());
    CHECK_EQ(1u, host::mem::word(kDefaultLine + 0x08));

    const ui::Page* const kOnePage[] = {&controls};
    const ui::MenuDef controlsMenu = {kOnePage, 1, dummyValues};
    ui::MenuState st;
    ui::menuReset(controlsMenu, st);
    ui::MenuInput down = {0, 1, 0, 0, false, false};
    for (int i = 0; i < 8; i++)
    {
        ui::menuStep(controlsMenu, st, down);
    }
    CHECK_EQ(8, static_cast<int>(st.cursor[0]));
    for (int i = 0; i < 4; i++)
    {
        ui::menuStep(controlsMenu, st, down);
    }
    CHECK_EQ(9, static_cast<int>(st.cursor[0])); // clamps on Menu Style (row 9)

    game::codeMenuMatchDetach();
}
