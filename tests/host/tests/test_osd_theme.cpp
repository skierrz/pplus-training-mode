// Tests for OSD theme customization (Display > Theme): pure color/alpha/shadow
// math (osd_theme_core.h), Display > Theme menu navigation, descriptions, and
// backward-compatible SD storage migration from a 22-byte pre3 kAreaOsd area.
#include <stddef.h>
#include <string.h>

#include "extra/menu_descriptions.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/osd_theme_core.h"
#include "main/ui/pages/pages.h"

using namespace tm;
using main::Settings;

namespace {
    const char kFileA[] = "settings-a.bin";
    const char kFileB[] = "settings-b.bin";

    u8* bootStorage()
    {
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
        return block;
    }

    void saveStorage()
    {
        storage::settingsMenuClosed();
        for (int i = 0; i < 4; i++)
        {
            storage::onRender(host::fakes::match());
        }
    }

    storage::FileHeader* fileHeader(const char* name)
    {
        return reinterpret_cast<storage::FileHeader*>(host::card::data(name));
    }

    void resealFile(const char* name)
    {
        storage::FileHeader* h = fileHeader(name);
        h->checksum = storage::checksum(host::card::data(name) + storage::kChecksumFrom,
                                        h->size - storage::kChecksumFrom);
    }

    storage::AreaEntry* findAreaEntry(const char* name, int id)
    {
        u8* file = host::card::data(name);
        storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(file + sizeof(storage::FileHeader));
        storage::AreaEntry* table = reinterpret_cast<storage::AreaEntry*>(file + sizeof(storage::FileHeader)
                                                                          + sizeof(storage::SettingsHead));
        for (int i = 0; i < head->areaCount; i++)
        {
            if (table[i].id == id)
            {
                return &table[i];
            }
        }
        return NULL;
    }

    ui::MenuInput makeInput(int up, int down, int left, int right, bool a, bool b)
    {
        ui::MenuInput in;
        memset(&in, 0, sizeof(in));
        in.up = up;
        in.down = down;
        in.left = left;
        in.right = right;
        in.a = a;
        in.b = b;
        return in;
    }
}

TEST(osd_theme, presets_opacity_track_darkening_and_drop_shadow)
{
    static const u32 kExpectedRgb[main::kOsdBgColorCount] = {
        0x0F242Cu, // 0: P+ Teal
        0x141A24u, // 1: Dark Slate
        0x101E3Au, // 2: Brawl Navy
        0x1A1A1Eu, // 3: Charcoal
        0x080A0Eu, // 4: Black
        0x281018u, // 5: Crimson
        0x1D122Cu  // 6: Violet
    };
    static const u8 kExpectedAlpha[main::kOsdAlphaMax + 1] = {
        0, 26, 51, 77, 102, 128, 153, 179, 204, 230, 255
    };

    // Every preset RGB and darkened track tint.
    for (u8 c = 0; c < main::kOsdBgColorCount; c++)
    {
        u32 rgb = osd::bgRgb(c);
        CHECK_EQ(kExpectedRgb[c], rgb);
        u32 dark = osd::darkenedRgb(rgb);
        CHECK(((dark >> 16) & 0xFFu) < ((rgb >> 16) & 0xFFu));
        CHECK(((dark >> 8) & 0xFFu) < ((rgb >> 8) & 0xFFu));
        CHECK((dark & 0xFFu) < (rgb & 0xFFu));

        osd::ThemeColors t50 = osd::resolveTheme(c, main::kDefaultOsdAlpha);
        CHECK_EQ((rgb << 8) | 128u, t50.bodyColor);
        CHECK_EQ(dark, (t50.trackColor >> 8) & 0xFFFFFFu);
        CHECK((t50.trackColor & 0xFFu) >= 128u);
        CHECK_EQ(0u, t50.shadowColor);
    }

    // All 11 opacity steps (0%..100%) and drop-shadow threshold (<= 20%).
    u8 prevTrackA = 0;
    for (u8 step = 0; step <= main::kOsdAlphaMax; step++)
    {
        CHECK_EQ(static_cast<int>(kExpectedAlpha[step]), static_cast<int>(osd::bodyAlphaByte(step)));
        osd::ThemeColors t = osd::resolveTheme(main::kOsdBgTeal, step);
        if (step == 0)
        {
            CHECK_EQ(0u, t.bodyColor);
            CHECK_EQ(0u, t.trackColor);
            CHECK_EQ(0x000000D0u, t.shadowColor);
        }
        else
        {
            CHECK_EQ((0x0F242Cu << 8) | kExpectedAlpha[step], t.bodyColor);
            u8 trackA = static_cast<u8>(t.trackColor & 0xFFu);
            CHECK(trackA >= kExpectedAlpha[step]);
            CHECK(trackA >= prevTrackA);
            prevTrackA = trackA;
            if (step == 1)
            {
                CHECK_EQ(0x000000ACu, t.shadowColor);
            }
            else if (step == 2)
            {
                CHECK_EQ(0x00000088u, t.shadowColor);
            }
            else
            {
                CHECK_EQ(0u, t.shadowColor);
            }
        }
    }
    CHECK_EQ(255, static_cast<int>(osd::resolveTheme(main::kOsdBgTeal, 10).trackColor & 0xFFu));

    // Out-of-range byte clamping.
    CHECK_EQ(static_cast<int>(main::kOsdBgViolet), static_cast<int>(osd::clampBgColor(7)));
    CHECK_EQ(static_cast<int>(main::kOsdBgViolet), static_cast<int>(osd::clampBgColor(255)));
    CHECK_EQ(static_cast<int>(main::kOsdAlphaMax), static_cast<int>(osd::clampBgAlpha(11)));
    CHECK_EQ(static_cast<int>(main::kOsdAlphaMax), static_cast<int>(osd::clampBgAlpha(255)));
    osd::ThemeColors clamped = osd::resolveTheme(200, 200);
    osd::ThemeColors maxTheme = osd::resolveTheme(main::kOsdBgViolet, main::kOsdAlphaMax);
    CHECK_EQ(maxTheme.bodyColor, clamped.bodyColor);
    CHECK_EQ(maxTheme.trackColor, clamped.trackColor);
    CHECK_EQ(0u, clamped.shadowColor);
}

TEST(osd_theme, display_theme_menu_navigation_and_descriptions)
{
    bootStorage();
    const ui::MenuDef& menu = ui::labMenu();
    const ui::Page& displayPage = *menu.pages[ui::pages::P_DISPLAY];
    const ui::Page& themePage = *menu.pages[ui::pages::P_THEME];

    // Theme is appended at the very end of Display so no existing row shifts.
    const u8 themeRow = static_cast<u8>(displayPage.count - 1);
    CHECK_STR("Theme", displayPage.options[themeRow].name);
    CHECK_EQ(static_cast<int>(ui::kKindMenu), static_cast<int>(displayPage.options[themeRow].kind));
    CHECK_EQ(static_cast<int>(ui::pages::P_THEME), static_cast<int>(displayPage.options[themeRow].max));

    // Theme page has 2 choice rows with expected defaults.
    CHECK_STR("Theme", themePage.title);
    CHECK_EQ(2, static_cast<int>(themePage.count));
    CHECK_STR("OSD Background", themePage.options[0].name);
    CHECK_STR("OSD Opacity", themePage.options[1].name);
    CHECK_EQ(static_cast<int>(main::kOsdBgTeal), static_cast<int>(main::settings().osd.bgColor));
    CHECK_EQ(static_cast<int>(main::kDefaultOsdAlpha), static_cast<int>(main::settings().osd.bgAlpha));

    // Descriptions exist for Display > Theme and both Theme rows.
    const char* displayThemeDesc = extra::optionDesc(ui::pages::P_DISPLAY, themeRow);
    const char* bgDesc = extra::optionDesc(ui::pages::P_THEME, 0);
    const char* alphaDesc = extra::optionDesc(ui::pages::P_THEME, 1);
    REQUIRE(displayThemeDesc != NULL && displayThemeDesc[0] != '\0');
    REQUIRE(bgDesc != NULL && bgDesc[0] != '\0');
    REQUIRE(alphaDesc != NULL && alphaDesc[0] != '\0');

    // Navigate from Display into Theme, change both rows, and back out with B.
    ui::MenuState state;
    ui::menuReset(menu, state);
    state.category = 4; // Display
    state.railFocus = false;
    state.depth = 0;
    state.stack[0] = ui::pages::P_DISPLAY;
    state.cursor[ui::pages::P_DISPLAY] = themeRow;

    CHECK(ui::menuStep(menu, state, makeInput(0, 0, 0, 0, true, false)));
    CHECK_EQ(static_cast<int>(ui::pages::P_THEME), ui::currentPage(state));
    CHECK_EQ(1, static_cast<int>(state.depth));
    CHECK_EQ(0, static_cast<int>(state.cursor[ui::pages::P_THEME]));

    // Step Right twice on OSD Background: Teal (0) -> Dark Slate (1) -> Brawl Navy (2).
    CHECK(ui::menuStep(menu, state, makeInput(0, 0, 0, 1, false, false)));
    CHECK(ui::menuStep(menu, state, makeInput(0, 0, 0, 1, false, false)));
    CHECK_EQ(static_cast<int>(main::kOsdBgBrawlNavy), static_cast<int>(main::settings().osd.bgColor));

    // Move Down to OSD Opacity and step Left 5 times: 50% (5) -> 0% (0).
    CHECK(ui::menuStep(menu, state, makeInput(0, 1, 0, 0, false, false)));
    CHECK_EQ(1, static_cast<int>(state.cursor[ui::pages::P_THEME]));
    for (int i = 0; i < 5; i++)
    {
        CHECK(ui::menuStep(menu, state, makeInput(0, 0, 1, 0, false, false)));
    }
    CHECK_EQ(0, static_cast<int>(main::settings().osd.bgAlpha));

    // B returns cleanly to Display.
    CHECK(ui::menuStep(menu, state, makeInput(0, 0, 0, 0, false, true)));
    CHECK_EQ(static_cast<int>(ui::pages::P_DISPLAY), ui::currentPage(state));
    CHECK_EQ(0, static_cast<int>(state.depth));
    storage::detach();
}

TEST(osd_theme, sd_migration_from_22_byte_pre3_osd_area_and_round_trip)
{
    CHECK_EQ(1, static_cast<int>(main::kSettingsVersion));
    CHECK_EQ(32u, sizeof(main::OsdSettings));
    CHECK_EQ(436u, sizeof(Settings));
    CHECK_EQ(110u, offsetof(Settings, osd.bgColor));
    CHECK_EQ(111u, offsetof(Settings, osd.bgAlpha));
    CHECK_EQ(22u, offsetof(main::OsdSettings, bgColor));
    CHECK_EQ(24u, offsetof(main::OsdSettings, reserved));

    // Boot clean and verify defaults.
    bootStorage();
    CHECK_EQ(static_cast<int>(main::kOsdBgTeal), static_cast<int>(main::settings().osd.bgColor));
    CHECK_EQ(static_cast<int>(main::kDefaultOsdAlpha), static_cast<int>(main::settings().osd.bgAlpha));

    // Save custom theme + pre-existing OSD settings and verify round-trip.
    main::settings().osd.wavedash = 1;
    main::settings().osd.maxMessages = 4;
    main::settings().osd.bgColor = main::kOsdBgCrimson;
    main::settings().osd.bgAlpha = 2;
    saveStorage();
    storage::detach();

    REQUIRE(host::card::has(kFileA));
    storage::AreaEntry* osdArea = findAreaEntry(kFileA, storage::kAreaOsd);
    REQUIRE(osdArea != NULL);
    CHECK_EQ(24u, osdArea->used);

    bootStorage();
    CHECK_LOG("16 areas, 0 with new fields, 0 at defaults, 0 unknown");
    CHECK_EQ(1, static_cast<int>(main::settings().osd.wavedash));
    CHECK_EQ(4, static_cast<int>(main::settings().osd.maxMessages));
    CHECK_EQ(static_cast<int>(main::kOsdBgCrimson), static_cast<int>(main::settings().osd.bgColor));
    CHECK_EQ(2, static_cast<int>(main::settings().osd.bgAlpha));
    storage::detach();

    // Simulate pre3 SD file where kAreaOsd had used == 22 (ending after maxMessages):
    // must migrate cleanly to (bgColor = P+ Teal [0], bgAlpha = 50% [5]) while
    // preserving earlier OSD settings.
    osdArea = findAreaEntry(kFileA, storage::kAreaOsd);
    REQUIRE(osdArea != NULL);
    osdArea->used = 22;
    resealFile(kFileA);

    bootStorage();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_LOG("storage: notice: Lab settings from an older version: new ones at defaults");
    CHECK_EQ(1, static_cast<int>(main::settings().osd.wavedash));
    CHECK_EQ(4, static_cast<int>(main::settings().osd.maxMessages));
    CHECK_EQ(static_cast<int>(main::kOsdBgTeal), static_cast<int>(main::settings().osd.bgColor));
    CHECK_EQ(static_cast<int>(main::kDefaultOsdAlpha), static_cast<int>(main::settings().osd.bgAlpha));
    storage::detach();

    // Corrupt / out-of-range theme bytes in kAreaOsd are clamped on load.
    osdArea = findAreaEntry(kFileA, storage::kAreaOsd);
    REQUIRE(osdArea != NULL);
    osdArea->used = 24;
    u8* raw = host::card::data(kFileA) + osdArea->offset;
    raw[offsetof(main::OsdSettings, bgColor)] = 0xFE;
    raw[offsetof(main::OsdSettings, bgAlpha)] = 0xFF;
    resealFile(kFileA);

    bootStorage();
    CHECK_EQ(static_cast<int>(main::kOsdBgViolet), static_cast<int>(main::settings().osd.bgColor));
    CHECK_EQ(static_cast<int>(main::kOsdAlphaMax), static_cast<int>(main::settings().osd.bgAlpha));
    storage::detach();
}
