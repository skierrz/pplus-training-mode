// The menu's option logic (src/main/ui/menu.cpp) and the lab menu's own
// tables (src/main/ui/pages/, every page compiled as it is).
//
// Every setting the mod has is a row of these tables: a byte offset into
// main::Settings, a range and a default. A row added with a wrong offset,
// a default outside its range, or a field the card does not keep works on
// screen and is wrong where nobody looks: these tests look at every row.
#include "extra/menu_descriptions.h"
#include "host/canvas.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/extra.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/settings.h"
#include "main/storage/storage.h"
#include "main/storage/settings_file.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/pages/pages.h"
#include "main/ui/pages/scenarios.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"

using namespace tm;
using main::Settings;

namespace {
    bool stored(const ui::Option& o)
    {
        return ui::hasValue(o) && (o.flags & (ui::kFlagUnavailable | ui::kFlagScratch)) == 0;
    }

    int low(const ui::Option& o)
    {
        return o.kind == ui::kKindInt ? o.min : 0;
    }

    int high(const ui::Option& o)
    {
        return o.kind == ui::kKindToggle ? 1 : o.max;
    }

    const char* nameOf(const ui::Option& o)
    {
        return o.name != NULL ? o.name : "(no name)";
    }

    void bootLab()
    {
        storage::detach();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }

    // The sounds asked for since the log was last cleared, by id.
    int sounds(int id)
    {
        char line[32];
        snprintf(line, sizeof(line), "ui: sound %d ", id);
        return host::log::count(line);
    }
}

// ---- the lab menu's tables --------------------------------------------------

TEST(lab_menu, every_row_is_well_formed)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    CHECK_EQ(ui::pages::P_COUNT, menu.pageCount);
    int rows = 0;
    for (int p = 0; p < menu.pageCount; p++)
    {
        const ui::Page& page = *menu.pages[p];
        REQUIRE(page.title != NULL);
        for (int i = 0; i < page.count; i++)
        {
            const ui::Option& o = page.options[i];
            const char* problem = NULL;
            int width = (o.flags & ui::kFlagHalf) ? 2 : 1;
            if (o.kind > ui::kKindInfo)
            {
                problem = "an unknown kind";
            }
            else if (o.name == NULL && (o.flags & ui::kFlagHidden) == 0)
            {
                problem = "no name, and not a hidden row";
            }
            else if (o.kind == ui::kKindMenu && (o.max < 0 || o.max >= menu.pageCount))
            {
                problem = "opens a page that does not exist";
            }
            else if (ui::hasValue(o) && (o.def < low(o) || o.def > high(o)))
            {
                problem = "its default is outside its range";
            }
            else if (ui::hasValue(o) && low(o) > high(o))
            {
                problem = "an empty range";
            }
            else if (o.kind == ui::kKindChoice && o.data == NULL)
            {
                problem = "a choice without labels";
            }
            else if (stored(o) && (o.offset < offsetof(Settings, general) || o.offset + width > sizeof(Settings)))
            {
                problem = "its value lies outside the settings' fields";
            }
            else if (stored(o) && width == 2 && (o.offset & 1) != 0)
            {
                problem = "a 16-bit value at an odd offset";
            }
            else if ((o.flags & ui::kFlagScratch) != 0 && o.offset + width > ui::kScratchBytes)
            {
                problem = "its value lies outside the menu's scratch bytes";
            }
            else if (stored(o) && width == 1 && (low(o) < -128 || high(o) > ((o.flags & ui::kFlagSigned) ? 127 : 255)))
            {
                problem = "a range a byte cannot hold";
            }
            else if (stored(o) && low(o) < 0 && (o.flags & ui::kFlagSigned) == 0)
            {
                problem = "a negative range on an unsigned value";
            }
            else if ((o.flags & ui::kFlagBit) != 0
                     && (o.kind != ui::kKindToggle || (o.flags & (ui::kFlagHalf | ui::kFlagSigned)) != 0
                         || o.min < 1 || o.min > 0xFF || (o.min & (o.min - 1)) != 0))
            {
                problem = "a bit flag row must be an unsigned byte toggle with a single-bit mask";
            }
            if (problem == NULL && o.kind == ui::kKindChoice)
            {
                const char* const* labels = static_cast<const char* const*>(o.data);
                for (int v = 0; v <= o.max && problem == NULL; v++)
                {
                    problem = labels[v] == NULL ? "a choice value without a label" : NULL;
                }
            }
            if (problem == NULL && o.desc != NULL)
            {
                problem = "match-module Option::desc must stay NULL (descriptions live in extra)";
            }
            if (problem != NULL)
            {
                host::test::fail(__FILE__, __LINE__, "page \"%s\" row %d \"%s\": %s", page.title, i, nameOf(o), problem);
            }
            rows++;
        }
    }
    NOTE("%d rows on %d pages", rows, menu.pageCount);
    storage::detach();
}

TEST(lab_menu, defaults_are_the_rows_defaults_and_clamping_keeps_them)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    Settings defaults = main::settings();
    for (int p = 0; p < menu.pageCount; p++)
    {
        const ui::Page& page = *menu.pages[p];
        for (int i = 0; i < page.count; i++)
        {
            const ui::Option& o = page.options[i];
            if (stored(o) && ui::optionValue(menu, o) != o.def)
            {
                // Two rows on one field must agree on its default.
                host::test::fail(__FILE__, __LINE__, "page \"%s\" row \"%s\": after a reset its value is %d, its default %d",
                                 page.title, nameOf(o), ui::optionValue(menu, o), o.def);
            }
        }
    }
    ui::clampSettings();
    CHECK(memcmp(&defaults, &main::settings(), sizeof(Settings)) == 0);
    storage::detach();
}

// The card must keep what a row sets, or the row is one of those kept for
// the session only on purpose (src/main/storage/settings_file.cpp lists
// them and says why). Each row in turn: set to another value, saved, the
// console switched off and on.
TEST(lab_menu, every_rows_setting_survives_a_save_and_a_boot_unless_it_is_for_the_session_only)
{
    static const char* const kSessionOnly[] = {
        "general.playerPercent", "general.lockPlayerPercent", "general.frameAdvance", "general.gameSpeed",
        "display.debugOverlay",  "display.hud",               "display.models",       "display.stageModel",
        "display.effects",       "cpu.percent",               "cpu.lockPercent",      "cpu.options.control",
        "comboTrainer.enabled",  "recording (from HMN Mode on)",
    };
    struct Range {
        u32 from, to;
    };
#define FIELD(f) {offsetof(Settings, f), offsetof(Settings, f) + sizeof(((Settings*)0)->f)}
    static const Range kRanges[] = {
        FIELD(general.playerPercent), FIELD(general.lockPlayerPercent), FIELD(general.frameAdvance),
        FIELD(general.gameSpeed),     FIELD(display.debugOverlay),      FIELD(display.hud),
        FIELD(display.models),        FIELD(display.stageModel),        FIELD(display.effects),
        FIELD(cpu.percent),           FIELD(cpu.lockPercent),           FIELD(cpu.options.control),
        FIELD(comboTrainer.enabled),
        {offsetof(Settings, recording.hmnMode), offsetof(Settings, recording.reserved)},
    };
#undef FIELD
    CHECK_EQ(sizeof(kSessionOnly) / sizeof(kSessionOnly[0]), sizeof(kRanges) / sizeof(kRanges[0]));

    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    int kept = 0, session = 0;
    for (int p = 0; p < menu.pageCount; p++)
    {
        const ui::Page& page = *menu.pages[p];
        for (int i = 0; i < page.count; i++)
        {
            const ui::Option& o = page.options[i];
            if (!stored(o) || low(o) == high(o))
            {
                continue;
            }
            bool sessionOnly = false;
            for (u32 r = 0; r < sizeof(kRanges) / sizeof(kRanges[0]); r++)
            {
                sessionOnly = sessionOnly || (o.offset >= kRanges[r].from && o.offset < kRanges[r].to);
            }
            host::card::reset();
            bootLab();
            int other = o.def < high(o) ? o.def + 1 : o.def - 1;
            ui::setOptionValue(menu, o, other);
            storage::settingsMenuClosed();
            for (int f = 0; f < 4; f++)
            {
                storage::settingsUpdate();
            }
            bootLab();
            int found = ui::optionValue(menu, o);
            if (!sessionOnly && found != other)
            {
                host::test::fail(__FILE__, __LINE__,
                                 "page \"%s\" row \"%s\" (offset %d of main::Settings): set to %d and saved, it is %d "
                                 "after a boot. Its field is not in a stored area (src/main/storage/areas.cpp), or lies "
                                 "in an area's reserved bytes.",
                                 page.title, nameOf(o), o.offset, other, found);
            }
            if (sessionOnly && found != o.def)
            {
                host::test::fail(__FILE__, __LINE__,
                                 "page \"%s\" row \"%s\": a session-only setting came back from the card as %d", page.title,
                                 nameOf(o), found);
            }
            kept += sessionOnly ? 0 : 1;
            session += sessionOnly ? 1 : 0;
        }
    }
    CHECK(kept > 100);
    NOTE("%d rows kept by the card, %d for the session only", kept, session);
    storage::detach();
}

// ---- stepping through a menu ------------------------------------------------

namespace {
    // A small menu over eight bytes: every kind of row.
    u8 s_values[8];
    int s_funcCalls = 0;
    int s_changes = 0;
    bool s_gate = false;

    u8* values()
    {
        return s_values;
    }
    void func()
    {
        s_funcCalls++;
    }
    void changed()
    {
        s_changes++;
    }
    bool gate()
    {
        return s_gate;
    }

    const char* const kSpeeds[] = {"Slow", TM_UNAVAILABLE_LABEL "Medium", TM_UNAVAILABLE_LABEL "Fast", "Full"};
    const char* const kEnds[] = {TM_UNAVAILABLE_LABEL "None", "One", TM_UNAVAILABLE_LABEL "Two"};

    enum { kRowInt, kRowHalf, kRowChoice, kRowEnds, kRowToggle, kRowFunc, kRowGated, kRowHidden, kRowNa, kRowSub };
    const ui::Option kTop[] = {
        {ui::kKindInt, ui::kFlagSigned, 0, -3, 3, 0, 0, "Signed", "", "%d", changed, NULL},
        {ui::kKindInt, ui::kFlagHalf, 2, 0, 999, 500, 0, "Percent", "", "%d", changed, NULL},
        {ui::kKindChoice, 0, 4, 0, 3, 0, 0, "Speed", "", kSpeeds, changed, NULL},
        {ui::kKindChoice, 0, 5, 0, 2, 1, 0, "Ends", "", kEnds, changed, NULL},
        {ui::kKindToggle, 0, 6, 0, 1, 0, 0, "Toggle", "", NULL, changed, NULL},
        {ui::kKindFunc, 0, 0, 0, 0, 0, 0, "Run", "", NULL, func, NULL},
        {ui::kKindToggle, 0, 7, 0, 1, 0, 0, "Gated", "", NULL, changed, gate},
        {ui::kKindInfo, ui::kFlagHidden, 0, 0, 0, 0, 0, NULL, "", NULL, NULL, NULL},
        {ui::kKindToggle, ui::kFlagUnavailable, 0, 0, 1, 1, 0, "Not built", "", NULL, NULL, NULL},
        {ui::kKindMenu, 0, 0, 0, 1, 0, 0, "More", "", NULL, NULL, NULL},
    };
    const ui::Option kLong[] = {
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r0", "", NULL, NULL, NULL},  {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r1", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r2", "", NULL, NULL, NULL},  {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r3", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r4", "", NULL, NULL, NULL},  {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r5", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r6", "", NULL, NULL, NULL},  {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r7", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r8", "", NULL, NULL, NULL},  {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r9", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r10", "", NULL, NULL, NULL}, {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r11", "", NULL, NULL, NULL},
        {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r12", "", NULL, NULL, NULL}, {ui::kKindInfo, 0, 0, 0, 0, 0, 0, "r13", "", NULL, NULL, NULL},
        {ui::kKindMenu, 0, 0, 0, 1, 0, 0, "Again", "", NULL, NULL, NULL},
    };
    const ui::Page kTopPage = {"Top", kTop, sizeof(kTop) / sizeof(kTop[0]), {0, 0, 0}};
    const ui::Page kLongPage = {"Long", kLong, sizeof(kLong) / sizeof(kLong[0]), {0, 0, 0}};
    const ui::Page* const kSmallPages[] = {&kTopPage, &kLongPage};
    const ui::MenuDef kSmall = {kSmallPages, 2, values};

    ui::MenuState s_state;

    void open()
    {
        memset(s_values, 0, sizeof(s_values));
        s_funcCalls = s_changes = 0;
        s_gate = false;
        ui::g_menuQuiet = false;
        ui::defaultAll(kSmall);
        ui::menuReset(kSmall, s_state);
        host::log::clear();
    }

    bool press(int up, int down, int left, int right, bool a, bool b)
    {
        ui::MenuInput in = {up, down, left, right, a, b};
        return ui::menuStep(kSmall, s_state, in);
    }
    void down(int times = 1)
    {
        for (int i = 0; i < times; i++)
        {
            press(0, 1, 0, 0, false, false);
        }
    }
    void right(int times = 1)
    {
        for (int i = 0; i < times; i++)
        {
            press(0, 0, 0, 1, false, false);
        }
    }
    void left(int times = 1)
    {
        for (int i = 0; i < times; i++)
        {
            press(0, 0, 1, 0, false, false);
        }
    }
    int cursor()
    {
        return s_state.cursor[ui::currentPage(s_state)];
    }
    int value(int row)
    {
        return ui::optionValue(kSmall, kTop[row]);
    }
}

TEST(menu, values_change_within_their_range_and_sound_only_when_they_change)
{
    open();
    CHECK_EQ(0, value(kRowInt));
    CHECK_EQ(500, value(kRowHalf));
    CHECK_EQ(1, value(kRowEnds));
    CHECK_EQ(1, value(kRowNa)); // a row not built shows its default

    // A signed value down to its minimum, and no further.
    left(5);
    CHECK_EQ(-3, value(kRowInt));
    CHECK_EQ(static_cast<s8>(-3), static_cast<s8>(s_values[0]));
    CHECK_EQ(3, s_changes);
    CHECK_EQ(3, sounds(game::kSoundMove));
    right(9);
    CHECK_EQ(3, value(kRowInt));

    // A 16-bit value: two bytes, the others untouched.
    down();
    CHECK_EQ(kRowHalf, cursor());
    right(2);
    CHECK_EQ(502, value(kRowHalf));
    CHECK_EQ(0, s_values[4]);
    left(600);
    CHECK_EQ(0, value(kRowHalf));
    ui::setOptionValue(kSmall, kTop[kRowHalf], 999);
    right();
    CHECK_EQ(999, value(kRowHalf));
}

// Values listed but not built yet are stepped over, or the row stays.
TEST(menu, a_choice_steps_over_values_not_built_yet)
{
    open();
    down(2);
    CHECK_EQ(kRowChoice, cursor());
    right();
    CHECK_EQ(3, value(kRowChoice)); // Slow to Full, past Medium and Fast
    right();
    CHECK_EQ(3, value(kRowChoice));
    left();
    CHECK_EQ(0, value(kRowChoice));
    CHECK_EQ(2, s_changes);

    // Nothing built on either side: the value stays, silently.
    down();
    host::log::clear();
    right();
    left();
    CHECK_EQ(1, value(kRowEnds));
    CHECK_EQ(2, s_changes);
    CHECK_EQ(0, sounds(game::kSoundMove));
}

// A retired choice value keeps its index: stepping never stops on it, a value
// holding it shows its name, and retireDropped resets it to the row's default.
namespace {
    u8 s_dropValues[2];
    u8* dropValues()
    {
        return s_dropValues;
    }
    const char* const kDropLabels[] = {"A", TM_DROPPED_LABEL "B", "C", TM_DROPPED_LABEL "D"};
    const ui::Option kDropRows[] = {
        {ui::kKindChoice, 0, 0, 0, 3, 2, 0, "Drop", "", kDropLabels, NULL, NULL},
    };
    const ui::Page kDropPage = {"Drop", kDropRows, 1, {0, 0, 0}};
    const ui::Page* const kDropPages[] = {&kDropPage};
    const ui::MenuDef kDrop = {kDropPages, 1, dropValues};
}

TEST(menu, a_choice_steps_over_dropped_values_and_loading_retires_them)
{
    ui::MenuState state;
    ui::g_menuQuiet = true;
    s_dropValues[0] = 0;
    ui::menuReset(kDrop, state);
    ui::MenuInput right = {0, 0, 0, 1, false, false};
    ui::MenuInput left = {0, 0, 1, 0, false, false};
    ui::menuStep(kDrop, state, right);
    CHECK_EQ(2, s_dropValues[0]); // A to C, past B
    ui::menuStep(kDrop, state, right);
    CHECK_EQ(2, s_dropValues[0]); // D is the last: stays on C
    ui::menuStep(kDrop, state, left);
    CHECK_EQ(0, s_dropValues[0]);
    s_dropValues[0] = 3; // set by other code (a drill)
    ui::retireDropped(kDrop);
    CHECK_EQ(2, s_dropValues[0]); // the row's default
    ui::g_menuQuiet = false;
}

TEST(menu, the_cursor_skips_rows_that_are_disabled_or_hidden)
{
    open();
    down(4);
    CHECK_EQ(kRowToggle, cursor());
    press(0, 0, 0, 0, true, false); // A flips a toggle
    CHECK_EQ(1, value(kRowToggle));
    right();
    CHECK_EQ(0, value(kRowToggle));
    down();
    CHECK_EQ(kRowFunc, cursor());
    host::log::clear();
    press(0, 0, 0, 0, true, false);
    CHECK_EQ(1, s_funcCalls);
    CHECK_EQ(kRowFunc, ui::activeRow());
    CHECK_EQ(1, sounds(game::kSoundConfirm));

    // The gated row is closed, the next is hidden and the one after is not
    // built yet: down skips all three disabled/hidden rows.
    down();
    CHECK_EQ(kRowSub, cursor());
    CHECK_EQ(1, value(kRowNa));
    press(1, 0, 0, 0, false, false);
    CHECK_EQ(kRowFunc, cursor());

    // The gate opens: the row can be reached. It closes under the cursor:
    // the cursor leaves it at the next step.
    s_gate = true;
    down();
    CHECK_EQ(kRowGated, cursor());
    s_gate = false;
    press(0, 0, 0, 1, false, false);
    CHECK_EQ(0, value(kRowGated));
    CHECK(cursor() != kRowGated);

    // Up at the top and down at the bottom stay.
    ui::menuReset(kSmall, s_state);
    press(1, 0, 0, 0, false, false);
    CHECK_EQ(0, cursor());
    down(30);
    CHECK_EQ(kRowSub, cursor());
}

TEST(menu, pages_open_and_close_and_the_depth_is_bounded)
{
    open();
    down(30);
    press(0, 0, 0, 0, true, false);
    CHECK_EQ(1, ui::currentPage(s_state));
    CHECK_EQ(1, s_state.depth);
    CHECK_EQ(1, sounds(game::kSoundConfirm));

    // The window follows the cursor: it stays kScrollMargin rows from the
    // window's edges except at the page's ends.
    for (int i = 0; i < 14; i++)
    {
        int cur = cursor();
        int top = s_state.scroll[1];
        CHECK(cur >= top && cur < top + ui::style::kLabVisibleRows);
        CHECK(top + ui::style::kLabVisibleRows <= kLongPage.count);
        if (cur >= ui::style::kScrollMargin && cur < kLongPage.count - ui::style::kScrollMargin)
        {
            CHECK(cur - top >= ui::style::kScrollMargin
                  && cur - top <= ui::style::kLabVisibleRows - 1 - ui::style::kScrollMargin);
        }
        down();
    }
    CHECK_EQ(14, cursor());
    CHECK_EQ(kLongPage.count - ui::style::kLabVisibleRows, s_state.scroll[1]);

    // The page opens itself again and again: never deeper than the state
    // has room for.
    for (int i = 0; i < 10; i++)
    {
        down(30);
        press(0, 0, 0, 0, true, false);
        CHECK(s_state.depth < ui::kMaxDepth);
    }
    CHECK_EQ(ui::kMaxDepth - 1, s_state.depth);

    // B goes back a page at a time; the page left starts at its top next
    // time; on the top page B is the caller's to handle.
    host::log::clear();
    int steps = 0;
    while (s_state.depth > 0 && steps < 20)
    {
        CHECK(press(0, 0, 0, 0, false, true));
        steps++;
    }
    CHECK_EQ(ui::kMaxDepth - 1, steps);
    // Internal subpage returns use stock Brawl navigation tick (kSoundMove), not heavy exit SFX.
    CHECK_EQ(steps, sounds(game::kSoundMove));
    CHECK_EQ(0, sounds(game::kSoundBack));
    CHECK_EQ(0, s_state.cursor[1]);
    CHECK_EQ(kRowSub, cursor());
    CHECK(!press(0, 0, 0, 0, false, true));

    // Quiet (the lab replays frames on its own): no sound is asked for.
    ui::g_menuQuiet = true;
    host::log::clear();
    press(1, 0, 0, 0, false, false);
    CHECK_EQ(0, host::log::count("ui: sound"));
    ui::g_menuQuiet = false;
}

TEST(lab_menu, seven_top_level_categories_and_subpage_placement)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    CHECK_EQ(ui::pages::kCategoryCount, static_cast<int>(menu.categoryCount));
    static const u8 kExpectedCats[ui::pages::kCategoryCount] = {
        ui::pages::P_GENERAL, ui::pages::P_SCENARIOS, ui::pages::P_CPU,
        ui::pages::P_RECORDING, ui::pages::P_DISPLAY, ui::pages::P_OSD_MENU,
        ui::pages::P_STAGE_CAST
    };
    for (int c = 0; c < ui::pages::kCategoryCount; c++)
    {
        CHECK_EQ(static_cast<int>(kExpectedCats[c]), static_cast<int>(menu.categoryPages[c]));
    }
    CHECK_EQ(3, static_cast<int>(menu.utilityCount));

    const ui::Page& lab = ui::pages::kGeneral;
    for (int i = 0; i < lab.count; i++)
    {
        CHECK(!(lab.options[i].kind == ui::kKindMenu && lab.options[i].max == ui::pages::P_OSD_MENU));
        CHECK(!(lab.options[i].name != NULL && strcmp(lab.options[i].name, "OSD Menu") == 0));
    }
    REQUIRE(lab.count == 13);
    CHECK(strcmp(lab.options[3].name, "P1 Load Random %") == 0);
    CHECK_EQ(static_cast<u16>(offsetof(main::Settings, recording.randomPercent[0])), lab.options[3].offset);
    CHECK(strcmp(lab.options[8].name, "Offline VS Displays") == 0);
    CHECK((lab.options[8].flags & ui::kFlagTabsOnly) == 0);
    CHECK(strcmp(lab.options[9].name, "Controls") == 0);
    CHECK_EQ(ui::pages::P_CONTROLS, lab.options[9].max);
    CHECK((lab.options[10].flags & ui::kFlagTabsOnly) != 0);
    CHECK((lab.options[11].flags & ui::kFlagTabsOnly) != 0);
    CHECK((lab.options[12].flags & ui::kFlagTabsOnly) != 0);

    const ui::Page& ctrls = ui::pages::kControls;
    CHECK(strcmp(ctrls.options[0].name, "Advance Button") == 0);
    CHECK(strcmp(ctrls.options[1].name, "Hold Repeat Delay") == 0);
    CHECK(strcmp(ctrls.options[2].name, "Rewind Button") == 0);

    const ui::Page& disp = ui::pages::kDisplayOptions;
    REQUIRE(disp.count == 18);
    CHECK(strcmp(disp.options[0].name, "Frame Meter") == 0);
    CHECK(strcmp(disp.options[1].name, "Input Viewer") == 0);
    CHECK(strcmp(disp.options[2].name, "Hitboxes") == 0);
    CHECK(strcmp(disp.options[3].name, "Hitbox Colors") == 0);
    CHECK((disp.options[3].flags & ui::kFlagHideWhenDisabled) != 0);
    CHECK(strcmp(disp.options[9].name, "DI Display") == 0);
    CHECK(strcmp(disp.options[10].name, "DI Vector") == 0);
    CHECK((disp.options[10].flags & ui::kFlagHideWhenDisabled) != 0);
    CHECK(strcmp(disp.options[11].name, "Stage Collision") == 0);
    CHECK(strcmp(disp.options[14].name, "Floating Meters") == 0);
    CHECK_EQ(ui::pages::P_COMBO_METERS, disp.options[14].max);
    CHECK(strcmp(disp.options[16].name, "Theme") == 0);
    CHECK_EQ(ui::pages::P_THEME, disp.options[16].max);
    CHECK(strcmp(disp.options[17].name, "Lab Debug") == 0);
    CHECK_EQ(ui::pages::P_DEBUG, disp.options[17].max);

    const ui::Page& dbg = ui::pages::kDebug;
    REQUIRE(dbg.count == 8);
    CHECK(strcmp(dbg.options[0].name, "P1 Info Display") == 0);
    CHECK_EQ(ui::pages::P_HMN_INFO, dbg.options[0].max);
    CHECK(strcmp(dbg.options[1].name, "CPU Info Display") == 0);
    CHECK_EQ(ui::pages::P_CPU_INFO, dbg.options[1].max);
    CHECK(strcmp(dbg.options[2].name, "PSA Debugger") == 0);
    CHECK(strcmp(dbg.options[3].name, "Debug Overlay") == 0);
    CHECK(strcmp(dbg.options[4].name, "Match HUD") == 0);
    CHECK(strcmp(dbg.options[5].name, "Fighter Models") == 0);
    CHECK(strcmp(dbg.options[6].name, "Stage Model") == 0);
    CHECK(strcmp(dbg.options[7].name, "Effects") == 0);

    const ui::Page& cpuP = ui::pages::kCpuOptions;
    REQUIRE(cpuP.count == 14);
    CHECK(strcmp(cpuP.options[2].name, "CPU Load Random %") == 0);
    CHECK_EQ(static_cast<u16>(offsetof(main::Settings, recording.randomPercent[1])), cpuP.options[2].offset);
    CHECK_EQ(ui::pages::P_CPU_DI, cpuP.options[5].max);
    CHECK_EQ(ui::pages::P_CPU_COUNTERS, cpuP.options[6].max);
    CHECK_EQ(ui::pages::P_TECH, cpuP.options[7].max);
    CHECK_EQ(ui::pages::P_CPU_DEFENSE, cpuP.options[8].max);

    const ui::Page& defP = ui::pages::kCpuDefense;
    CHECK(strcmp(defP.options[defP.count - 1].name, "Status Mash") == 0);

    const ui::Page& osdP = ui::pages::kOsdMenu;
    CHECK(strcmp(osdP.options[0].name, "OSD Preset") == 0);
    CHECK(strcmp(osdP.options[4].name, "Tech Sparkle") == 0);
    CHECK(strcmp(osdP.options[10].name, "Shield Advantage") == 0);

    const ui::Page& scen = ui::pages::kScenarios;
    REQUIRE(scen.count == 51);
    CHECK(strcmp(scen.options[40].name, "Practice Mode") == 0);
    CHECK(strcmp(scen.options[50].name, "More Combo Options") == 0);
    CHECK_EQ(ui::pages::P_COMBO_TRAINER, scen.options[50].max);

    const ui::Page& sc = ui::pages::kStageCast;
    REQUIRE(sc.count == 34);
    CHECK(strcmp(sc.options[0].name, "Fighter Swap") == 0);
    CHECK_EQ(ui::pages::P_FIGHTER_SWAP, sc.options[0].max);
    CHECK(strcmp(sc.options[1].name, "Show Inactive Fighters") == 0);
    CHECK(strcmp(sc.options[33].name, "Olimar") == 0);
    CHECK_EQ(ui::pages::P_OLIMAR, sc.options[33].max);
    storage::detach();
}

namespace {
    // The extra module's hooks, doing nothing: with them registered the
    // rows that need the extra module are open.
    void nothing()
    {
    }
    bool yes()
    {
        return true;
    }
    bool no()
    {
        return false;
    }
    void noMatch(const game::MatchState&)
    {
    }
    void noFlag(bool)
    {
    }
    int noActIn(Fighter*, int*)
    {
        return -1;
    }
    bool noAdvantage(const game::Advantage&)
    {
        return false;
    }
    // The Character RNG page: a Peach is in the match.
    bool yesCall(int)
    {
        return true;
    }
    bool noRecovery(const cpu::Owner&, const cpu::View&, Fighter*, cpu::Frame&, u32)
    {
        return false;
    }
    void noSlot(int)
    {
    }
    int noAdaptiveBias(int, int, bool, float, bool)
    {
        return -1;
    }
    void noSettings(main::Settings&)
    {
    }
    bool noShieldHold(int, int, const cpu::View&, bool)
    {
        return false;
    }
    const main::ExtraHooks kNoHooks = {NULL, yes, nothing, noMatch, nothing, noFlag,
                                       noActIn, nothing, noAdvantage, noShieldHold, yesCall, yesCall,
                                       noRecovery, noSlot, noMatch, nothing, noMatch, nothing,
                                       noAdaptiveBias, nothing, noSlot, noRecovery, noSettings, noMatch,
                                       yes, nothing, nothing, no, extra::optionDesc};
}

// Random presses on the real lab menu, every row's own code running, with
// every row that depends on the match open: 2,000 presses starting on each
// page a row leads to. The state stays inside its bounds and every value
// in its range.
TEST(lab_menu, random_presses_never_leave_the_tables)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    host::fakes::recording().positions = true;
    host::fakes::recording().rows = true;
    main::setExtraHooks(&kNoHooks);

    // The way to every page: seeded from all 7 top-level categories.
    int parent[ui::kMaxPages];
    for (int p = 0; p < ui::kMaxPages; p++)
    {
        parent[p] = -1;
    }
    int queue[ui::kMaxPages];
    int head = 0, tail = 0;
    for (int c = 0; c < menu.categoryCount; c++)
    {
        int pg = menu.categoryPages[c];
        queue[tail++] = pg;
        parent[pg] = pg;
    }
    while (head < tail)
    {
        int from = queue[head++];
        const ui::Page& page = *menu.pages[from];
        for (int i = 0; i < page.count; i++)
        {
            const ui::Option& o = page.options[i];
            if (o.kind == ui::kKindMenu && (o.flags & ui::kFlagUnavailable) == 0 && o.max >= 0 && o.max < menu.pageCount
                && parent[o.max] < 0)
            {
                parent[o.max] = from;
                queue[tail++] = o.max;
            }
        }
    }

    u32 seed = 12345;
    int visited = 0;
    char missed[200] = "";
    for (int target = 0; target < menu.pageCount && !host::test::failing(); target++)
    {
        ui::MenuState state;
        ui::menuReset(menu, state);
        u8 path[ui::kMaxDepth];
        int length = 0;
        bool reached = parent[target] >= 0;
        for (int p = target; reached; p = parent[p])
        {
            reached = length < ui::kMaxDepth;
            if (reached)
            {
                path[length++] = static_cast<u8>(p);
            }
            if (parent[p] == p)
            {
                break;
            }
        }
        for (int i = 0; i < length / 2; i++)
        {
            u8 swap = path[i];
            path[i] = path[length - 1 - i];
            path[length - 1 - i] = swap;
        }
        if (!reached || !ui::menuJump(menu, state, path, length))
        {
            // No row leads there (a page not built yet), or its row is closed.
            if (strlen(missed) + strlen(menu.pages[target]->title) + 3 < sizeof(missed))
            {
                strcat(missed, missed[0] != '\0' ? ", " : "");
                strcat(missed, menu.pages[target]->title);
            }
            continue;
        }
        CHECK_EQ(target, ui::currentPage(state));
        visited++;
        for (int step = 0; step < 2000 && !host::test::failing(); step++)
        {
            seed = seed * 1664525u + 1013904223u;
            u32 pick = (seed >> 8) % 22;
            ui::MenuInput in = {0, 0, 0, 0, false, false, false, false};
            in.up = pick < 4 ? 1 : 0;
            in.down = pick >= 4 && pick < 10 ? 1 : 0;
            in.left = pick == 10 || pick == 11 ? 1 : 0;
            in.right = pick >= 12 && pick < 15 ? 1 : 0;
            in.a = pick >= 15 && pick < 19;
            in.b = pick == 19;
            in.prevCat = pick == 20;
            in.nextCat = pick == 21;
            ui::menuStep(menu, state, in);
            host::log::clear();
            REQUIRE(state.depth < ui::kMaxDepth);
            int id = ui::currentPage(state);
            REQUIRE(id >= 0 && id < menu.pageCount);
            const ui::Page& page = *menu.pages[id];
            REQUIRE(page.count == 0 || state.cursor[id] < page.count);
            REQUIRE(state.scroll[id] <= state.cursor[id]);
            const ui::Option* o = ui::currentOption(menu, state);
            if (o != NULL && stored(*o))
            {
                int v = ui::optionValue(menu, *o);
                if (v < low(*o) || v > high(*o))
                {
                    host::test::fail(__FILE__, __LINE__, "from page \"%s\", step %d: page \"%s\" row \"%s\" holds %d, outside "
                                                         "%d..%d",
                                     menu.pages[target]->title, step, page.title, nameOf(*o), v, low(*o), high(*o));
                }
            }
        }
    }
    // Whatever was pressed, clamping finds nothing to change.
    Settings after = main::settings();
    ui::clampSettings();
    CHECK(memcmp(&after, &main::settings(), sizeof(Settings)) == 0);
    main::setExtraHooks(NULL);
    CHECK(visited >= 20);
    NOTE("%d of %d pages%s%s", visited, menu.pageCount, missed[0] != '\0' ? "; no open row leads to: " : "", missed);
    storage::detach();
}

TEST(menu, bit_flag_toggles_default_and_preserve_sibling_bits)
{
    // Standalone bit-flag rows with mixed defaults leave unmapped bits alone.
    static u8 bitBuf[2];
    struct BitVals {
        static u8* ptr() { return bitBuf; }
    };
    static const ui::Option kBitRows[] = {
        {ui::kKindToggle, ui::kFlagBit, 0, 0x01, 1, 1, 0, "B0", NULL, NULL, NULL, NULL},
        {ui::kKindToggle, ui::kFlagBit, 0, 0x02, 1, 0, 0, "B1", NULL, NULL, NULL, NULL},
        {ui::kKindToggle, ui::kFlagBit, 0, 0x08, 1, 1, 0, "B3", NULL, NULL, NULL, NULL},
    };
    static const ui::Page kBitPage = {"Bits", kBitRows, 3, {0, 0, 0}};
    static const ui::Page* const kBitPages[] = {&kBitPage};
    static const ui::MenuDef kBitMenu = {kBitPages, 1, BitVals::ptr};

    bitBuf[0] = 0x00;
    bitBuf[1] = 0x5A;
    ui::defaultAll(kBitMenu);
    CHECK_EQ(0x09, static_cast<int>(bitBuf[0]));
    CHECK_EQ(0x5A, static_cast<int>(bitBuf[1]));
    CHECK_EQ(1, ui::optionValue(kBitMenu, kBitRows[0]));
    CHECK_EQ(0, ui::optionValue(kBitMenu, kBitRows[1]));
    CHECK_EQ(1, ui::optionValue(kBitMenu, kBitRows[2]));

    bitBuf[0] = 0xFF;
    ui::defaultAll(kBitMenu);
    CHECK_EQ(0xFD, static_cast<int>(bitBuf[0])); // B1 cleared, B0/B3 set, unmapped bits preserved

    // Real lab menu Floating Meters subpage (P_COMBO_METERS).
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    const ui::Page& combo = ui::pages::kComboTrainer;
    REQUIRE(combo.count == 16);
    CHECK(combo.options[15].name != NULL && strcmp(combo.options[15].name, "Floating Meters") == 0);
    CHECK_EQ(ui::kKindMenu, combo.options[15].kind);
    CHECK_EQ(ui::pages::P_COMBO_METERS, combo.options[15].max);
    CHECK_EQ(1, static_cast<int>(main::settings().comboTrainer.noclip));
    CHECK_EQ(static_cast<int>(main::kComboDmgCpu), static_cast<int>(main::settings().comboTrainer.fixedDamageMode));

    const ui::Page& meters = ui::pages::kComboMeters;
    CHECK_EQ(7, static_cast<int>(meters.count));
    CHECK_EQ(0, static_cast<int>(main::settings().comboTrainer.enabledMeters));

    // Verify defaultAll produces 0x00 (safe fresh install) and every bit reads back normalized as 0.
    main::settings().comboTrainer.enabledMeters = main::kMeterAll;
    ui::defaultAll(menu);
    CHECK_EQ(0, static_cast<int>(main::settings().comboTrainer.enabledMeters));
    for (int i = 0; i < meters.count; i++)
    {
        CHECK((meters.options[i].flags & ui::kFlagBit) != 0);
        CHECK_EQ(0, ui::optionValue(menu, meters.options[i]));
    }

    // Toggling any single bit leaves the other 6 bits intact, and clampAll preserves partial masks.
    static const u8 kExpectedBits[7] = {
        main::kMeterHitlag, main::kMeterHitstun, main::kMeterShieldstun, main::kMeterDi,
        main::kMeterIasa,   main::kMeterLedgeIntang, main::kMeterIntang
    };
    main::settings().comboTrainer.enabledMeters = main::kMeterAll;
    for (int i = 0; i < 7; i++)
    {
        const ui::Option& o = meters.options[i];
        CHECK_EQ(static_cast<int>(kExpectedBits[i]), static_cast<int>(o.min));
        ui::setOptionValue(menu, o, 0);
        CHECK_EQ(0, ui::optionValue(menu, o));
        CHECK_EQ(static_cast<int>(0x7F & ~kExpectedBits[i]),
                 static_cast<int>(main::settings().comboTrainer.enabledMeters));
        ui::clampAll(menu);
        CHECK_EQ(static_cast<int>(0x7F & ~kExpectedBits[i]),
                 static_cast<int>(main::settings().comboTrainer.enabledMeters));
        for (int j = 0; j < 7; j++)
        {
            CHECK_EQ(j == i ? 0 : 1, ui::optionValue(menu, meters.options[j]));
        }
        ui::setOptionValue(menu, o, 1);
        CHECK_EQ(0x7F, static_cast<int>(main::settings().comboTrainer.enabledMeters));
    }

    // Floating Meters is accessible from Display even with Combo Trainer Off, and flips individual bits cleanly.
    main::settings().comboTrainer.enabled = 0;
    main::settings().comboTrainer.enabledMeters = main::kMeterAll;
    main::setExtraHooks(&kNoHooks);
    ui::MenuState st;
    ui::menuReset(menu, st);
    const u8 dispMeterPath[] = {ui::pages::P_DISPLAY, ui::pages::P_COMBO_METERS};
    REQUIRE(ui::menuJump(menu, st, dispMeterPath, 2));
    CHECK_EQ(ui::pages::P_COMBO_METERS, ui::currentPage(st));

    ui::MenuInput pressA = {0, 0, 0, 0, true, false};
    ui::MenuInput pressDown = {0, 1, 0, 0, false, false};
    ui::MenuInput pressRight = {0, 0, 0, 1, false, false};
    ui::menuStep(menu, st, pressA); // Hitlag Off
    CHECK_EQ(static_cast<int>(0x7F & ~main::kMeterHitlag),
             static_cast<int>(main::settings().comboTrainer.enabledMeters));
    ui::menuStep(menu, st, pressDown);
    ui::menuStep(menu, st, pressDown);
    ui::menuStep(menu, st, pressDown); // DI Ring
    ui::menuStep(menu, st, pressRight); // DI Ring Off
    CHECK_EQ(static_cast<int>(0x7F & ~(main::kMeterHitlag | main::kMeterDi)),
             static_cast<int>(main::settings().comboTrainer.enabledMeters));
    ui::menuStep(menu, st, pressRight); // DI Ring back On
    CHECK_EQ(static_cast<int>(0x7F & ~main::kMeterHitlag),
             static_cast<int>(main::settings().comboTrainer.enabledMeters));
    main::setExtraHooks(NULL);
    storage::detach();
}

// ---- kFlagHideWhenDisabled compaction, Category Rail & Top Tabs ergonomics -

namespace {
    bool s_rowOn[16];
    u8 s_compactVals[16];
    u8* compactVals()
    {
        return s_compactVals;
    }
    bool row1On() { return s_rowOn[1]; }
    bool row2On() { return s_rowOn[2]; }
    bool row3On() { return s_rowOn[3]; }
    bool row4On() { return s_rowOn[4]; }
    bool row5On() { return s_rowOn[5]; }
    bool row6On() { return s_rowOn[6]; }
    bool row7On() { return s_rowOn[7]; }
    bool row8On() { return s_rowOn[8]; }
    bool row9On() { return s_rowOn[9]; }
    bool row10On() { return s_rowOn[10]; }
    bool row11On() { return s_rowOn[11]; }
    bool row12On() { return s_rowOn[12]; }
    bool row13On() { return s_rowOn[13]; }
    bool row14On() { return s_rowOn[14]; }
    bool row15On() { return s_rowOn[15]; }

#define COMPACT_ROW(i, name, fn) \
    {ui::kKindToggle, ui::kFlagHideWhenDisabled, i, 0, 1, 0, 0, name, NULL, NULL, NULL, fn}
    const ui::Option kCompactRows[16] = {
        {ui::kKindToggle, 0, 0, 0, 1, 0, 0, "Always0", NULL, NULL, NULL, NULL},
        COMPACT_ROW(1, "Dyn1", row1On),   COMPACT_ROW(2, "Dyn2", row2On),   COMPACT_ROW(3, "Dyn3", row3On),
        COMPACT_ROW(4, "Dyn4", row4On),   COMPACT_ROW(5, "Dyn5", row5On),   COMPACT_ROW(6, "Dyn6", row6On),
        COMPACT_ROW(7, "Dyn7", row7On),   COMPACT_ROW(8, "Dyn8", row8On),   COMPACT_ROW(9, "Dyn9", row9On),
        COMPACT_ROW(10, "Dyn10", row10On), COMPACT_ROW(11, "Dyn11", row11On), COMPACT_ROW(12, "Dyn12", row12On),
        COMPACT_ROW(13, "Dyn13", row13On), COMPACT_ROW(14, "Dyn14", row14On), COMPACT_ROW(15, "Dyn15", row15On),
    };
#undef COMPACT_ROW
    const ui::Page kCompactPage = {"Compact", kCompactRows, 16, {0, 0, 0}};
    const ui::Page* const kCompactPages[] = {&kCompactPage};
    const ui::MenuDef kCompactMenu = {kCompactPages, 1, compactVals};

    // Mock Smashville Peach vs Fox hooks for Stage & Cast compaction check.
    bool smashvilleStage(int call)
    {
        return call == main::kStagePage || call == main::kStagePlatformsRow;
    }
    bool peachOnlyRng(int call)
    {
        return call == main::kRngAny || call == main::kRngPeach;
    }
    const main::ExtraHooks kSmashvillePeachHooks = {
        NULL, yes, nothing, noMatch, nothing, noFlag, noActIn, nothing, noAdvantage, noShieldHold,
        peachOnlyRng, smashvilleStage, noRecovery, noSlot, noMatch, nothing, noMatch,
        nothing, noAdaptiveBias, nothing, noSlot, noRecovery, noSettings, noMatch,
        yes, nothing, nothing, no, extra::optionDesc
    };
}

TEST(menu, hide_when_disabled_compacts_rows_clamps_scroll_and_snaps_cursor)
{
    memset(s_rowOn, 0, sizeof(s_rowOn));
    memset(s_compactVals, 0, sizeof(s_compactVals));
    s_rowOn[5] = true;
    s_rowOn[14] = true;

    ui::MenuState st;
    ui::menuReset(kCompactMenu, st);
    CHECK(!ui::g_showInactiveRows);
    CHECK_EQ(0, static_cast<int>(st.cursor[0]));
    CHECK_EQ(0, static_cast<int>(st.scroll[0]));

    // Only rows 0, 5, and 14 are visible (3 rows <= 11): scroll stays 0 even at raw index 14!
    ui::MenuInput down = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput noop = {0, 0, 0, 0, false, false, false, false};
    ui::menuStep(kCompactMenu, st, down);
    CHECK_EQ(5, static_cast<int>(st.cursor[0]));
    CHECK_EQ(0, static_cast<int>(st.scroll[0]));
    ui::menuStep(kCompactMenu, st, down);
    CHECK_EQ(14, static_cast<int>(st.cursor[0]));
    CHECK_EQ(0, static_cast<int>(st.scroll[0]));

    // Disable row 14 while highlighted: cursor snaps cleanly to nearest selectable (row 5).
    s_rowOn[14] = false;
    ui::menuStep(kCompactMenu, st, noop);
    CHECK_EQ(5, static_cast<int>(st.cursor[0]));
    CHECK_EQ(0, static_cast<int>(st.scroll[0]));

    // Show Inactive expands all 16 rows even while rows 1..15 remain disabled,
    // allowing full scrolling to inspect descriptions while refusing edits.
    ui::g_showInactiveRows = true;
    ui::menuScratch()[ui::kShowInactiveScratchOffset] = 1;
    for (int i = 0; i < 16; i++)
    {
        ui::menuStep(kCompactMenu, st, down);
    }
    CHECK_EQ(15, static_cast<int>(st.cursor[0]));
    CHECK_EQ(16 - ui::style::kLabVisibleRows, static_cast<int>(st.scroll[0]));
    // Disabled revealed row 15 cannot be mutated by Right or A (A plays error sound).
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::menuStep(kCompactMenu, st, inRight);
    CHECK_EQ(0, static_cast<int>(s_compactVals[15]));
    host::log::clear();
    ui::menuStep(kCompactMenu, st, inA);
    CHECK_EQ(0, static_cast<int>(s_compactVals[15]));
    CHECK_EQ(1, sounds(game::kSoundError));

    // defaultAll() resets BOTH the scratch toggle and g_showInactiveRows so
    // Reset Settings never leaves inactive rows expanded while showing Off.
    ui::defaultAll(kCompactMenu);
    CHECK(!ui::g_showInactiveRows);
    CHECK_EQ(0, static_cast<int>(ui::menuScratch()[ui::kShowInactiveScratchOffset]));
    ui::menuStep(kCompactMenu, st, noop);
    CHECK_EQ(5, static_cast<int>(st.cursor[0]));
    CHECK_EQ(0, static_cast<int>(st.scroll[0]));
}

TEST(lab_menu, rail_and_tabs_navigation_and_stage_cast_compaction)
{
    bootLab();
    main::setExtraHooks(&kSmashvillePeachHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    // Default Draft C (Rail): opens directly in Lab options pane, depth 0.
    CHECK_EQ(static_cast<int>(main::kMenuStyleRail), static_cast<int>(main::settings().controls.menuStyle));
    CHECK_EQ(ui::pages::P_GENERAL, ui::currentPage(st));
    CHECK(!st.railFocus);

    // B at depth 0 focuses left rail; B on rail returns false (caller decides close).
    ui::MenuInput inB = {0, 0, 0, 0, false, true, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput inNext = {0, 0, 0, 0, false, false, false, true};
    ui::MenuInput inPrev = {0, 0, 0, 0, false, false, true, false};
    CHECK(ui::menuStep(menu, st, inB));
    CHECK(st.railFocus);
    CHECK_EQ(0, static_cast<int>(st.railCursor));
    CHECK(!ui::menuStep(menu, st, inB));

    // Scroll rail down to Stage & Cast (index 6) and enter options pane.
    for (int i = 0; i < 6; i++)
    {
        CHECK(ui::menuStep(menu, st, inDown));
    }
    CHECK_EQ(6, static_cast<int>(st.railCursor));
    CHECK_EQ(ui::pages::P_STAGE_CAST, ui::currentPage(st));
    CHECK(ui::menuStep(menu, st, inA));
    CHECK(!st.railFocus);

    // Smashville Peach vs Fox with Show Inactive Off: exactly 6 active rows!
    // (0 Fighter Swap, 1 Show Inactive, 3 Platforms, 6 Peach Turnip, 7 Peach F-Smash, 8 Peach Float)
    const int kExpectedRows[6] = {0, 1, 3, 6, 7, 8};
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_STAGE_CAST]));
    for (int k = 1; k < 6; k++)
    {
        ui::menuStep(menu, st, inDown);
        CHECK_EQ(kExpectedRows[k], static_cast<int>(st.cursor[ui::pages::P_STAGE_CAST]));
    }
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(8, static_cast<int>(st.cursor[ui::pages::P_STAGE_CAST])); // bottom of compacted list
    CHECK_EQ(0, static_cast<int>(st.scroll[ui::pages::P_STAGE_CAST]));

    // L / R category switching wraps across all 7 categories and resets railFocus.
    ui::menuStep(menu, st, inNext);
    CHECK_EQ(0, static_cast<int>(st.category));
    CHECK_EQ(ui::pages::P_GENERAL, ui::currentPage(st));
    ui::menuStep(menu, st, inPrev);
    CHECK_EQ(6, static_cast<int>(st.category));
    CHECK_EQ(ui::pages::P_STAGE_CAST, ui::currentPage(st));

    // Live switch to Draft A (Tabs): B at depth 0 returns false directly (no rail).
    main::settings().controls.menuStyle = main::kMenuStyleTabs;
    CHECK(!ui::menuStep(menu, st, inB));
    CHECK(!st.railFocus);
    ui::menuStep(menu, st, inNext);
    CHECK_EQ(0, static_cast<int>(st.category));

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(lab_menu, show_inactive_fighters_scoped_to_stage_cast_only)
{
    bootLab();
    main::setExtraHooks(&kSmashvillePeachHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};

    // Jump to Stage & Cast and toggle Show Inactive Fighters (row 1) On.
    const u8 scPath[] = {ui::pages::P_STAGE_CAST};
    REQUIRE(ui::menuJump(menu, st, scPath, 1));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(1, static_cast<int>(st.cursor[ui::pages::P_STAGE_CAST]));
    ui::menuStep(menu, st, inA);
    CHECK(ui::g_showInactiveRows);

    // Stage & Cast now reveals inactive rows (stepping past row 8 reaches row 9+).
    for (int i = 0; i < 10; i++)
    {
        ui::menuStep(menu, st, inDown);
    }
    CHECK(static_cast<int>(st.cursor[ui::pages::P_STAGE_CAST]) > 8);

    // Scenarios MUST still compact hidden preset/drill rows (only Preset [0] and More Combo Options [50] visible).
    const u8 scenPath[] = {ui::pages::P_SCENARIOS};
    REQUIRE(ui::menuJump(menu, st, scenPath, 1));
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(50, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(50, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));

    // Context-scoped Tab 1: when a Tech Practice drill is active, Preset and More Combo Options hide,
    // leaving only the active drill's rows (Ledgedash: Practice Mode [40], Reset Ledge [44],
    // Ledge Auto-Reset [45], Ledge Invuln [46]).
    ui::pages::practiceMenuState()->activeDrill = 1;
    REQUIRE(ui::menuJump(menu, st, scenPath, 1));
    CHECK_EQ(40, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(44, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(45, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(46, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(46, static_cast<int>(st.cursor[ui::pages::P_SCENARIOS]));
    ui::pages::practiceMenuState()->activeDrill = 0;

    // CPU > Counters & Reversals: Counter Mode starts at Single Action (1), hiding Custom Pools,
    // Per-Hit, Reversals, and Adaptive even while Show Inactive Fighters is On!
    const u8 ctrPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_COUNTERS};
    REQUIRE(ui::menuJump(menu, st, ctrPath, 2));
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_CPU_COUNTERS]));
    CHECK_EQ(1, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    // Switch Counter Mode right to Custom Pool (2): reveals Custom Pools (row 6), hides Ground/Air/Shield (1..3).
    ui::menuStep(menu, st, inRight);
    CHECK_EQ(2, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(4, static_cast<int>(st.cursor[ui::pages::P_CPU_COUNTERS])); // Shield Timing
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(5, static_cast<int>(st.cursor[ui::pages::P_CPU_COUNTERS])); // Counter Delay
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(6, static_cast<int>(st.cursor[ui::pages::P_CPU_COUNTERS])); // Custom Pools

    // Enter Custom Pools: Category=Ground MUST still compact Air/Shield rows even while Show Inactive Fighters is On!
    ui::menuStep(menu, st, inA);
    CHECK_EQ(ui::pages::P_COUNTER_POOL, ui::currentPage(st));
    for (int i = 0; i < 40; i++)
    {
        ui::menuStep(menu, st, inDown);
    }
    // 4 head rows (0..3) + 28 Ground pool rows (4..31) => bottom clamps at row 31, never reaches Air (32+).
    CHECK_EQ(31, static_cast<int>(st.cursor[ui::pages::P_COUNTER_POOL]));

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(lab_menu, counter_mode_clears_per_hit_overrides_when_switching_away)
{
    bootLab();
    main::setExtraHooks(&kSmashvillePeachHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    ui::MenuInput inLeft = {0, 0, 1, 0, false, false, false, false};
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};
    ui::MenuInput noop = {0, 0, 0, 0, false, false, false, false};

    // Arm a Per-Hit Custom override (and a Disable override on hit 2): Counter Mode derives Per-Hit (4).
    main::settings().cpuTables.hits[0].logic = cpu::kLogicCustom;
    main::settings().cpuTables.hits[1].logic = cpu::kLogicDisable;
    main::settings().cpu.options.advLogic = cpu::kLogicCustom;

    const u8 ctrPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_COUNTERS};
    REQUIRE(ui::menuJump(menu, st, ctrPath, 2));
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_CPU_COUNTERS]));
    CHECK_EQ(4, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));

    // Step left from Per-Hit (4) -> Reversal Slots (3) -> Custom Pool (2) -> Single Action (1).
    ui::menuStep(menu, st, inLeft);
    ui::menuStep(menu, st, inLeft);
    ui::menuStep(menu, st, inLeft);
    CHECK_EQ(1, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpuTables.hits[0].logic));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpuTables.hits[1].logic));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpu.options.advLogic));
    // Subsequent step must stay on Single Action (1), never snapping back to Per-Hit.
    ui::menuStep(menu, st, noop);
    CHECK_EQ(1, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));

    // Re-arm Per-Hit Custom override and switch to Custom Pool (2): overrides clear and mode stays at 2.
    main::settings().cpuTables.hits[0].logic = cpu::kLogicCustom;
    main::settings().cpu.options.advLogic = cpu::kLogicCustom;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(4, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    ui::menuStep(menu, st, inLeft); // 3 = Reversal Slots
    ui::menuStep(menu, st, inLeft); // 2 = Custom Pool
    CHECK_EQ(2, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpuTables.hits[0].logic));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpu.options.advLogic));
    ui::menuStep(menu, st, noop);
    CHECK_EQ(2, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));

    // Also verify Disable-only Per-Hit override derives Per-Hit (4) and clears on switching to Adaptive (5).
    main::settings().cpuTables.hits[0].logic = cpu::kLogicDisable;
    main::settings().cpu.options.advLogic = cpu::kLogicDisable;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(4, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    ui::menuStep(menu, st, inRight); // 5 = Adaptive
    CHECK_EQ(5, ui::optionValue(menu, ui::pages::kCpuCounters.options[0]));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpuTables.hits[0].logic));
    CHECK_EQ(static_cast<int>(cpu::kLogicDefault), static_cast<int>(main::settings().cpu.options.advLogic));

    main::setExtraHooks(NULL);
    storage::detach();
}

namespace {
    u8 s_allOffVals[4];
    bool s_allOffEnabled = true;
    u8* allOffValues()
    {
        return s_allOffVals;
    }
    bool allOffRowEnabled()
    {
        return s_allOffEnabled;
    }
    void disableAllRows()
    {
        s_allOffEnabled = false;
    }
    const ui::Option kAllOffOpts[] = {
        {ui::kKindToggle, 0, 0, 0, 1, 0, 0, "Row 0", "", NULL, NULL, allOffRowEnabled},
        {ui::kKindToggle, 0, 1, 0, 1, 0, 0, "Row 1", "", NULL, NULL, allOffRowEnabled},
        {ui::kKindFunc, 0, 0, 0, 0, 0, 0, "Apply", "", NULL, disableAllRows, allOffRowEnabled},
    };
    const ui::Page kAllOffPage = {"All Off", kAllOffOpts, 3, {0, 0, 0}};
    const ui::Page* const kAllOffPages[] = {&kAllOffPage};
    const ui::MenuDef kAllOffMenu = {kAllOffPages, 1, allOffValues};
}

TEST(menu, all_disabled_page_preserves_cursor_across_transient_lockout)
{
    memset(s_allOffVals, 0, sizeof(s_allOffVals));
    s_allOffEnabled = true;
    ui::MenuState st;
    ui::menuReset(kAllOffMenu, st);

    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::menuStep(kAllOffMenu, st, inDown);
    ui::menuStep(kAllOffMenu, st, inDown);
    CHECK_EQ(2, static_cast<int>(st.cursor[0]));

    // Mirror Fighter Swap > Apply Swap: onChange temporarily disables every row on the page.
    // nearestSelectable must keep cursor at row 2 rather than clobbering it to 0.
    ui::menuStep(kAllOffMenu, st, inA);
    CHECK(!s_allOffEnabled);
    CHECK_EQ(2, static_cast<int>(st.cursor[0]));
}

namespace {
    bool menuCanvasDrew(const char* text)
    {
        for (int i = 0; i < host::canvas::runs(); i++)
        {
            if (strcmp(host::canvas::run(i).text, text) == 0)
            {
                return true;
            }
        }
        return false;
    }

    int s_overlayCalls = 0;
    u8 s_lastOverlayPage = 0xFF;
    bool s_lastOverlayRail = false;
    void recordPageOverlay(u8 pageId, bool railMode)
    {
        s_overlayCalls++;
        s_lastOverlayPage = pageId;
        s_lastOverlayRail = railMode;
    }
}

TEST(lab_menu, rail_hides_tabs_only_utilities_and_clamps_on_live_style_switch)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    ui::MenuInput inUp = {1, 0, 0, 0, false, false, false, false};
    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput noop = {0, 0, 0, 0, false, false, false, false};

    // Draft C (Rail): Lab has 10 visible rows (0..9); rows 10..12 (kFlagTabsOnly) are omitted.
    main::settings().controls.menuStyle = main::kMenuStyleRail;
    for (int i = 0; i < 15; i++)
    {
        ui::menuStep(menu, st, inDown);
    }
    CHECK_EQ(9, static_cast<int>(st.cursor[ui::pages::P_GENERAL]));
    host::canvas::clear();
    ui::menuDraw(menu, st);
    CHECK(menuCanvasDrew("Offline VS Displays"));
    CHECK(menuCanvasDrew("Controls"));
    CHECK(!menuCanvasDrew("Brawl Training Menu"));

    // Live switch to Draft A (Tabs): all 13 rows appear including Reset / Brawl Training / Return to CSS.
    main::settings().controls.menuStyle = main::kMenuStyleTabs;
    for (int i = 0; i < 5; i++)
    {
        ui::menuStep(menu, st, inDown);
    }
    CHECK_EQ(12, static_cast<int>(st.cursor[ui::pages::P_GENERAL]));
    host::canvas::clear();
    ui::menuDraw(menu, st);
    CHECK(menuCanvasDrew("Brawl Training Menu"));
    CHECK(menuCanvasDrew("Return to CSS"));

    // Live switch back to Draft C (Rail) while row 12 is highlighted: cursor clamps cleanly to row 9.
    main::settings().controls.menuStyle = main::kMenuStyleRail;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(9, static_cast<int>(st.cursor[ui::pages::P_GENERAL]));
    (void)inUp;
    storage::detach();
}

TEST(lab_menu, footer_hides_confirm_badge_on_disabled_row_and_invokes_page_overlay)
{
    bootLab();
    main::ExtraHooks hooks = kSmashvillePeachHooks;
    hooks.drawPageOverlay = recordPageOverlay;
    main::setExtraHooks(&hooks);
    s_overlayCalls = 0;

    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);
    const u8 path[] = {ui::pages::P_STAGE_CAST};
    REQUIRE(ui::menuJump(menu, st, path, 1));

    // Active row 0 (Fighter Swap): footer shows [A] Open and extraDrawPageOverlay runs.
    host::canvas::clear();
    ui::menuDraw(menu, st);
    CHECK(menuCanvasDrew("A"));
    CHECK(menuCanvasDrew("Open"));
    CHECK_EQ(1, s_overlayCalls);
    CHECK_EQ(static_cast<int>(ui::pages::P_STAGE_CAST), static_cast<int>(s_lastOverlayPage));
    CHECK(s_lastOverlayRail);

    // Reveal inactive rows and highlight row 2 (Hazards, disabled on Smashville): [A] is hidden.
    ui::g_showInactiveRows = true;
    ui::menuScratch()[ui::kShowInactiveScratchOffset] = 1;
    st.cursor[ui::pages::P_STAGE_CAST] = 2;
    host::canvas::clear();
    ui::menuDraw(menu, st);
    CHECK(!menuCanvasDrew("A"));
    CHECK(!menuCanvasDrew("Select"));
    CHECK(!menuCanvasDrew("Toggle"));
    CHECK(!menuCanvasDrew("Open"));
    CHECK(menuCanvasDrew("B"));
    CHECK(menuCanvasDrew("Categories"));
    CHECK(menuCanvasDrew("START"));

    ui::g_showInactiveRows = false;
    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(menu, display_font_cmapped_and_fallback_characters)
{
    CHECK(gfx::displayFontLoaded());
    CHECK(gfx::displayHasChar('A'));
    CHECK(gfx::displayHasChar('z'));
    CHECK(gfx::displayHasChar('0'));
    CHECK(gfx::displayHasChar('&'));
    CHECK(gfx::displayHasChar('+'));
    CHECK(gfx::displayHasChar('/'));
    CHECK(!gfx::displayHasChar('('));
    CHECK(!gfx::displayHasChar(')'));
    CHECK(!gfx::displayHasChar('>'));
    CHECK(!gfx::displayHasChar('<'));

    gfx::setXScale(1.0f);
    const char* sample = "Scenarios > Combo (1)";
    float scale = ui::style::displayScaleForCap(10.0f);
    float measured = gfx::displayTextWidth(sample, scale);
    CHECK(measured > 0.0f);

    host::canvas::clear();
    float endX = gfx::displayText(50.0f, ui::style::displayYForCap(40.0f, scale), sample, 0xFFFFFFFF, scale);
    CHECK_NEAR(measured, endX - 50.0f, 0.05);

    int dispRuns = 0;
    int fallbackRuns = 0;
    for (int i = 0; i < host::canvas::runs(); i++)
    {
        const host::canvas::Run& r = host::canvas::run(i);
        CHECK_EQ(0, r.unknown);
        if (r.displayFont)
        {
            dispRuns++;
        }
        else
        {
            fallbackRuns++;
        }
    }
    CHECK(dispRuns >= 3);
    CHECK(fallbackRuns >= 3);
}

TEST(menu, compressed_option_descriptions_decode_all_pages_and_bounds)
{
    const ui::MenuDef& menu = ui::labMenu();
    for (int p = 0; p < menu.pageCount; p++)
    {
        const ui::Page& page = *menu.pages[p];
        for (int r = 0; r < page.count; r++)
        {
            const char* d = extra::optionDesc(static_cast<u8>(p), static_cast<u8>(r));
            REQUIRE(d != NULL);
            size_t len = strlen(d);
            CHECK(len > 10 && len < 120);
            CHECK_EQ('.', d[len - 1]);
            for (size_t i = 0; i < len; i++)
            {
                u8 ch = static_cast<u8>(d[i]);
                CHECK(ch >= 0x20 && ch <= 0x7E);
            }
        }
        CHECK(extra::optionDesc(static_cast<u8>(p), static_cast<u8>(page.count)) == NULL);
    }
    // Category rail entries (pageId == P_COUNT) and out-of-range page/row bounds.
    int railTotal = menu.categoryCount + menu.utilityCount;
    for (int rc = 0; rc < railTotal; rc++)
    {
        const char* d = extra::optionDesc(static_cast<u8>(ui::pages::P_COUNT), static_cast<u8>(rc));
        REQUIRE(d != NULL);
        CHECK(strlen(d) > 10);
    }
    CHECK(extra::optionDesc(static_cast<u8>(ui::pages::P_COUNT), static_cast<u8>(railTotal)) == NULL);
    CHECK(extra::optionDesc(static_cast<u8>(ui::pages::P_COUNT + 1), 0) == NULL);

    // Exact verbatim spot checks across shared and unique strings.
    CHECK(strcmp(extra::optionDesc(ui::pages::P_GENERAL, 0),
                 "Freeze gameplay and step one frame per advance press.") == 0);
    CHECK(strcmp(extra::optionDesc(ui::pages::P_GENERAL, 11),
                 "Switch directly to Brawl's built-in Training pause menu.") == 0);
    CHECK(strcmp(extra::optionDesc(ui::pages::P_STAGE_CAST, 33),
                 "Configure Olimar pull pool, maturity, lineup, whistle, and presets.") == 0);
    CHECK(strcmp(extra::optionDesc(ui::pages::P_COUNT, 9),
                 "End this session and return to the Character Select Screen.") == 0);
}

TEST(lab_menu, progressive_disclosure_hides_child_rows_until_parent_enabled)
{
    bootLab();
    main::setExtraHooks(&kNoHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};

    // 1. Display: Hitbox Colors, Heatmap Bounds/Opacity/Step, and DI Vector stay hidden until enabled.
    ui::MenuState st;
    ui::menuReset(menu, st);
    const u8 dispPath[] = {ui::pages::P_DISPLAY};
    REQUIRE(ui::menuJump(menu, st, dispPath, 1));
    main::settings().display.hitboxes = 0;
    main::settings().display.heatmap = 0;
    main::settings().general.diDisplay = 0;
    // Row 0 Frame Meter -> 1 Input Viewer -> 2 Hitboxes -> 4 Hitbox Heatmap -> 8 Grab -> 9 DI Display -> 11 Stage Coll -> 12 P1 Overlays
    static const int kDispCompact[] = {0, 1, 2, 4, 8, 9, 11, 12};
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_DISPLAY]));
    for (int i = 1; i < 8; i++)
    {
        ui::menuStep(menu, st, inDown);
        CHECK_EQ(kDispCompact[i], static_cast<int>(st.cursor[ui::pages::P_DISPLAY]));
    }
    // Enable Hitboxes -> row 3 (Hitbox Colors) becomes reachable right under Hitboxes!
    main::settings().display.hitboxes = 1;
    st.cursor[ui::pages::P_DISPLAY] = 2;
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(3, static_cast<int>(st.cursor[ui::pages::P_DISPLAY]));

    // 2. CPU > DI & SDI: Pick DI Angle (row 1) hidden unless tdi==Custom; SDI rows (3..4) hidden while sdiAmount==0.
    const u8 diPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_DI};
    REQUIRE(ui::menuJump(menu, st, diPath, 2));
    main::settings().cpu.options.tdi = cpu::kTdiRandom;
    main::settings().cpu.options.sdiAmount = 0;
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_CPU_DI]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(2, static_cast<int>(st.cursor[ui::pages::P_CPU_DI])); // jumped past row 1 to SDI Inputs
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(5, static_cast<int>(st.cursor[ui::pages::P_CPU_DI])); // jumped past rows 3 & 4 to ASDI Direction
    main::settings().cpu.options.sdiAmount = 2;
    st.cursor[ui::pages::P_CPU_DI] = 2;
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(3, static_cast<int>(st.cursor[ui::pages::P_CPU_DI]));

    // 3. Recording: post-save rows (1..18) hidden when !positionsSaved; only row 0 visible.
    host::fakes::recording().positions = false;
    host::fakes::recording().rows = false;
    const u8 recPath[] = {ui::pages::P_RECORDING};
    REQUIRE(ui::menuJump(menu, st, recPath, 1));
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_RECORDING]));
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_RECORDING]));

    // 4. OSDs: Advantage Tolerance (row 12) hidden when Shield Advantage & Hit Advantage are both Off.
    const u8 osdPath[] = {ui::pages::P_OSD_MENU};
    REQUIRE(ui::menuJump(menu, st, osdPath, 1));
    main::settings().osd.frameAdvantage = 0;
    main::settings().osd.frameAdvantageOnHit = 0;
    st.cursor[ui::pages::P_OSD_MENU] = 11; // Hit Advantage
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(13, static_cast<int>(st.cursor[ui::pages::P_OSD_MENU])); // skips row 12 Advantage Tolerance
    main::settings().osd.frameAdvantage = 1;
    st.cursor[ui::pages::P_OSD_MENU] = 11;
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(12, static_cast<int>(st.cursor[ui::pages::P_OSD_MENU]));

    // 5. Combo Trainer: Fixed Damage (row 11) hidden when Reset Damage == Use CPU %.
    const u8 comboPath[] = {ui::pages::P_SCENARIOS, ui::pages::P_COMBO_TRAINER};
    main::settings().cpu.options.control = cpu::kControlLab;
    main::settings().comboTrainer.enabled = 1;
    main::settings().comboTrainer.fixPosition = 1;
    main::settings().comboTrainer.fixedDamageMode = main::kComboDmgCpu;
    REQUIRE(ui::menuJump(menu, st, comboPath, 2));
    st.cursor[ui::pages::P_COMBO_TRAINER] = 10; // Reset Damage
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(12, static_cast<int>(st.cursor[ui::pages::P_COMBO_TRAINER])); // skips row 11 Fixed Damage
    main::settings().comboTrainer.fixedDamageMode = main::kComboDmgCustom;
    st.cursor[ui::pages::P_COMBO_TRAINER] = 10;
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(11, static_cast<int>(st.cursor[ui::pages::P_COMBO_TRAINER]));

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(lab_menu, ux9_osd_presets_reversal_slot1_and_random_percent_mirrors)
{
    bootLab();
    main::setExtraHooks(&kNoHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);
    ui::MenuInput pressRight = {0, 0, 0, 1, false, false};

    // 1. OSD Preset: defaults to Essentials (all 6 core OSDs On, Max OSDs 4) and resets to Custom on manual edit.
    const u8 osdPath[] = {ui::pages::P_OSD_MENU};
    REQUIRE(ui::menuJump(menu, st, osdPath, 1));
    CHECK_EQ(static_cast<int>(main::kOsdPresetEssentials), static_cast<int>(main::settings().osd.preset));
    CHECK_EQ(4, static_cast<int>(main::settings().osd.maxMessages));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.wavedash));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.lCancel));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.ledgedashInfo));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.actOos));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.frameAdvantage));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.frameAdvantageOnHit));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.fastfall));

    // Manually toggling Wavedash (row 6) resets preset back to Custom.
    st.cursor[ui::pages::P_OSD_MENU] = 6;
    ui::menuStep(menu, st, pressRight);
    CHECK_EQ(0, static_cast<int>(main::settings().osd.wavedash));
    CHECK_EQ(static_cast<int>(main::kOsdPresetCustom), static_cast<int>(main::settings().osd.preset));

    // Stepping OSD Preset right from Custom re-applies Essentials.
    st.cursor[ui::pages::P_OSD_MENU] = 0;
    ui::menuStep(menu, st, pressRight);
    CHECK_EQ(static_cast<int>(main::kOsdPresetEssentials), static_cast<int>(main::settings().osd.preset));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.wavedash));

    // Movement and Defense presets turn on their targeted OSD groups.
    ui::setOptionValue(menu, ui::pages::kOsdMenu.options[0], main::kOsdPresetMovement);
    ui::pages::kOsdMenu.options[0].onChange();
    CHECK_EQ(1, static_cast<int>(main::settings().osd.actOoLag));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.fastfall));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.powershield));

    ui::setOptionValue(menu, ui::pages::kOsdMenu.options[0], main::kOsdPresetDefense);
    ui::pages::kOsdMenu.options[0].onChange();
    CHECK_EQ(1, static_cast<int>(main::settings().osd.powershield));
    CHECK_EQ(1, static_cast<int>(main::settings().osd.sdi));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.wavedash));

    // All Off clears every individual OSD toggle.
    st.cursor[ui::pages::P_OSD_MENU] = 0;
    ui::setOptionValue(menu, ui::pages::kOsdMenu.options[0], main::kOsdPresetAllOff);
    ui::pages::kOsdMenu.options[0].onChange();
    CHECK_EQ(0, static_cast<int>(main::settings().osd.lCancel));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.frameAdvantage));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.powershield));
    CHECK_EQ(0, static_cast<int>(main::settings().osd.lockoutTimers));

    // 2. Reversal Slots: row 2 dynamically shows active Slot 1 (Primary) for each trigger.
    const ui::Page& rev = ui::pages::kReversalSlots;
    REQUIRE(rev.count == 15);
    CHECK_EQ(ui::kKindInfo, rev.options[2].kind);
    CHECK(strcmp(rev.options[2].name, "Slot 1 (Primary)") == 0);
    CHECK(strcmp(rev.options[3].name, "Slot 2 Action") == 0);
    CHECK(strcmp(rev.options[4].name, "Slot 2 Delay") == 0);
    CHECK(strcmp(rev.options[5].name, "Slot 2 Chance") == 0);
    main::settings().cpu.options.control = cpu::kControlLab;
    main::settings().cpu.options.multiReversal = 1;
    main::settings().cpu.options.counterGround = 1; // Shield
    main::settings().cpu.options.counterShield = 2; // Grab
    main::settings().cpu.options.getup = 1;         // Stand
    main::settings().cpu.options.ledgeOption = 1;   // Neutral Getup
    for (int trig = 0; trig < 4; trig++)
    {
        ui::menuScratch()[rev.options[1].offset] = static_cast<u8>(trig);
        CHECK(rev.options[2].enabled());
        const char* s1 = static_cast<const char*>(rev.options[2].data);
        CHECK(s1 != NULL && s1[0] != '\0');
    }

    // 3. P1 / CPU Load Random % mirrors recording.randomPercent[0..1] directly.
    ui::setOptionValue(menu, ui::pages::kGeneral.options[3], 15);
    ui::setOptionValue(menu, ui::pages::kCpuOptions.options[2], 25);
    CHECK_EQ(15, static_cast<int>(main::settings().recording.randomPercent[0]));
    CHECK_EQ(25, static_cast<int>(main::settings().recording.randomPercent[1]));

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(lab_menu, rail_focus_suppresses_competing_category_highlight_and_uses_move_sfx)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    main::settings().controls.menuStyle = main::kMenuStyleRail;
    ui::MenuState st;
    memset(&st, 0, sizeof(st));
    st.stack[0] = menu.categoryPages[0];

    // Move focus from right pane back to left rail with B -> uses kSoundMove (0 kSoundBack).
    host::log::clear();
    ui::MenuInput pressB;
    memset(&pressB, 0, sizeof(pressB));
    pressB.b = true;
    CHECK(ui::menuStep(menu, st, pressB));
    CHECK(st.railFocus);
    CHECK_EQ(1, sounds(game::kSoundMove));
    CHECK_EQ(0, sounds(game::kSoundBack));

    // Move rail cursor down onto a utility row (Return to CSS) while category == 0.
    st.category = 0;
    st.railFocus = true;
    st.railCursor = menu.categoryCount;
    gfx::setXScale(1.0f);
    host::canvas::clear();
    ui::menuDraw(menu, st);

    // Count bevel rectangles drawn in the category 0 slot y-band:
    // category 0 must draw NO teal accent bevel bar while railFocus == true.
    float cat0Top = ui::style::kRailItemTop;
    float cat0Bot = cat0Top + ui::style::kRailItemH;
    int cat0BevelRects = 0;
    for (int i = 0; i < host::canvas::rects(); i++)
    {
        const host::canvas::Rect& r = host::canvas::rect(i);
        if (r.x < ui::style::kRailRight && r.y >= cat0Top - 1.0f && r.y + r.height <= cat0Bot + 1.0f
            && r.width > 100.0f)
        {
            cat0BevelRects++;
        }
    }
    CHECK_EQ(0, cat0BevelRects);

    // When focus returns to the right pane (railFocus == false), category 0 draws its active accent bevel bar.
    st.railFocus = false;
    host::canvas::clear();
    ui::menuDraw(menu, st);
    int activeBevelRects = 0;
    for (int i = 0; i < host::canvas::rects(); i++)
    {
        const host::canvas::Rect& r = host::canvas::rect(i);
        if (r.x < ui::style::kRailRight && r.y >= cat0Top - 1.0f && r.y + r.height <= cat0Bot + 1.0f
            && r.width > 100.0f)
        {
            activeBevelRects++;
        }
    }
    CHECK(activeBevelRects >= 1);
    storage::detach();
}

TEST(menu, tech_practice_lab_rows_gate_by_active_drill)
{
    main::loadSettings(host::fakes::block());
    main::ExtraHooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    main::setExtraHooks(&hooks);
    ui::pages::PracticeMenuState* pm = ui::pages::practiceMenuState();
    memset(pm, 0, sizeof(*pm));
    const ui::Page& scen = ui::pages::kScenarios;
    // Rows 40..49 are the 10 conditional Tech Practice rows.
    for (int r = 40; r <= 49; r++)
    {
        CHECK((scen.options[r].flags & (ui::kFlagScratch | ui::kFlagHideWhenDisabled))
              == (ui::kFlagScratch | ui::kFlagHideWhenDisabled));
        CHECK(!scen.options[r].enabled());
    }
    // Powershield (5): Mode (40), Attack Mode (41), Shot Height (42, proj only), Cadence (43).
    pm->activeDrill = 5;
    pm->psAttackMode = 0;
    CHECK(scen.options[40].enabled() && scen.options[41].enabled() && scen.options[42].enabled()
          && scen.options[43].enabled() && !scen.options[44].enabled());
    pm->psAttackMode = 1;
    CHECK(scen.options[41].enabled() && !scen.options[42].enabled() && scen.options[43].enabled());
    // Ledgedash (1): Mode (40), Reset Ledge (44), Ledge Auto-Reset (45), Ledge Invuln (46).
    pm->activeDrill = 1;
    CHECK(scen.options[40].enabled() && !scen.options[41].enabled() && scen.options[44].enabled()
          && scen.options[45].enabled() && scen.options[46].enabled() && !scen.options[47].enabled());
    // L-Cancel (3) -> row 47; Float/Hover (4) -> row 48; Wavedash (2) -> row 49.
    pm->activeDrill = 3;
    CHECK(scen.options[40].enabled() && scen.options[47].enabled() && !scen.options[48].enabled());
    pm->activeDrill = 4;
    CHECK(scen.options[40].enabled() && !scen.options[47].enabled() && scen.options[48].enabled());
    pm->activeDrill = 2;
    CHECK(scen.options[40].enabled() && scen.options[49].enabled() && !scen.options[47].enabled());
    memset(pm, 0, sizeof(*pm));
    main::setExtraHooks(NULL);
}

TEST(lab_menu, stream1_damage_sync_and_one_tap_a_zero_reset)
{
    bootLab();
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    // Unlocked damage syncs live match % only in free Training (never when a
    // reset-owning mode like Combo Lab, Scenario preset, or Skill Drill is active).
    CHECK(lab::shouldSyncUnlockedPercentsOnOpen(0, ui::pages::kPresetOff, 0));
    CHECK(!lab::shouldSyncUnlockedPercentsOnOpen(1, ui::pages::kPresetOff, 0));
    CHECK(!lab::shouldSyncUnlockedPercentsOnOpen(0, ui::pages::kPresetComboLab, 0));
    CHECK(!lab::shouldSyncUnlockedPercentsOnOpen(0, ui::pages::kPresetTechchase, 0));
    CHECK(!lab::shouldSyncUnlockedPercentsOnOpen(0, ui::pages::kPresetOff, 1));
    CHECK(!lab::shouldSyncUnlockedPercentsOnOpen(1, ui::pages::kPresetComboLab, 1));

    // Unlocked damage syncs live match % into menu setting without mutating locked sides.
    u16 p1Pct = 10;
    u16 cpuPct = 20;
    CHECK(lab::syncUnlockedPercentValue(p1Pct, false, 67.8f));
    CHECK_EQ(67, static_cast<int>(p1Pct));
    CHECK(!lab::syncUnlockedPercentValue(cpuPct, true, 140.0f));
    CHECK_EQ(20, static_cast<int>(cpuPct));
    CHECK(lab::syncUnlockedPercentValue(p1Pct, false, -5.0f));
    CHECK_EQ(0, static_cast<int>(p1Pct));
    CHECK(lab::syncUnlockedPercentValue(p1Pct, false, 1250.0f));
    CHECK_EQ(999, static_cast<int>(p1Pct));

    // 1-tap A on Lab > P1 Damage resets playerPercent to 0 and plays confirm sound.
    main::settings().general.playerPercent = 84;
    st.depth = 0;
    st.stack[0] = ui::pages::P_GENERAL;
    st.railFocus = false;
    st.cursor[ui::pages::P_GENERAL] = 1; // P1 Damage
    host::log::clear();
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    CHECK(ui::menuStep(menu, st, inA));
    CHECK_EQ(0, static_cast<int>(main::settings().general.playerPercent));
    CHECK_EQ(1, sounds(game::kSoundConfirm));

    // 1-tap A on CPU > CPU Damage resets cpu.percent to 0 and plays confirm sound.
    main::settings().cpu.percent = 112;
    const u8 cpuPath[] = {ui::pages::P_CPU};
    REQUIRE(ui::menuJump(menu, st, cpuPath, 1));
    CHECK_EQ(0, static_cast<int>(st.cursor[ui::pages::P_CPU]));
    host::log::clear();
    CHECK(ui::menuStep(menu, st, inA));
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.percent));
    CHECK_EQ(1, sounds(game::kSoundConfirm));

    // Unrelated kKindInt row (P1 Load Random %) must NOT reset on A.
    main::settings().recording.randomPercent[0] = 35;
    const u8 genPath[] = {ui::pages::P_GENERAL};
    REQUIRE(ui::menuJump(menu, st, genPath, 1));
    st.cursor[ui::pages::P_GENERAL] = 3; // P1 Load Random %
    host::log::clear();
    CHECK(ui::menuStep(menu, st, inA));
    CHECK_EQ(35, static_cast<int>(main::settings().recording.randomPercent[0]));
    CHECK_EQ(0, sounds(game::kSoundConfirm));
    storage::detach();
}

TEST(lab_menu, stream1_cstick_live_di_aiming_and_display_lab_debug_structure)
{
    bootLab();
    main::setExtraHooks(&kNoHooks);
    const ui::MenuDef& menu = ui::labMenu();

    // Non-modal C-Stick live DI aiming: tilts map to [-100, +100] on the unit circle,
    // while releasing to neutral (allowNeutral=false) preserves the aimed angle.
    s8 dx = 100, dy = 0;
    CHECK(lab::stickToCustomDi(0.78f, 0.62f, false, dx, dy));
    CHECK_EQ(78, static_cast<int>(dx));
    CHECK_EQ(62, static_cast<int>(dy));
    CHECK(!lab::stickToCustomDi(0.05f, -0.05f, false, dx, dy));
    CHECK_EQ(78, static_cast<int>(dx));
    CHECK_EQ(62, static_cast<int>(dy));
    // Over-unit diagonal clamps cleanly to unit circle (1, 1) -> (~71, ~71).
    CHECK(lab::stickToCustomDi(1.0f, 1.0f, false, dx, dy));
    CHECK_EQ(71, static_cast<int>(dx));
    CHECK_EQ(71, static_cast<int>(dy));
    // Control-Stick modal fallback (allowNeutral=true) allows locking neutral (0, 0).
    CHECK(lab::stickToCustomDi(0.0f, 0.0f, true, dx, dy));
    CHECK_EQ(0, static_cast<int>(dx));
    CHECK_EQ(0, static_cast<int>(dy));

    // CPU > DI & SDI has no redundant Custom DI X / Y rows.
    const ui::Page& di = ui::pages::kCpuDi;
    REQUIRE(di.count == 6);
    for (int i = 0; i < di.count; i++)
    {
        CHECK(strcmp(di.options[i].name, "Custom DI X") != 0);
        CHECK(strcmp(di.options[i].name, "Custom DI Y") != 0);
    }

    // Display default visible list is exact 13-row player-first order.
    main::settings().display.hitboxes = 0;
    main::settings().display.heatmap = 0;
    main::settings().general.diDisplay = 0;
    static const char* const kExpectedDisp[13] = {
        "Frame Meter", "Input Viewer", "Hitboxes", "Hitbox Heatmap", "Grab / Tether Boxes",
        "DI Display", "Stage Collision", "P1 Overlays", "CPU Overlays", "Floating Meters",
        "Ledgedash HUD", "Theme", "Lab Debug"
    };
    const ui::Page& disp = ui::pages::kDisplayOptions;
    int vis = 0;
    for (int i = 0; i < disp.count; i++)
    {
        bool hidden = (disp.options[i].flags & ui::kFlagHideWhenDisabled) != 0
                      && disp.options[i].enabled != NULL && !disp.options[i].enabled();
        if (!hidden)
        {
            REQUIRE(vis < 13);
            CHECK(strcmp(disp.options[i].name, kExpectedDisp[vis]) == 0);
            vis++;
        }
    }
    CHECK_EQ(13, vis);

    // Display > Lab Debug exposes exact 8 debug/visual rows.
    static const char* const kExpectedDbg[8] = {
        "P1 Info Display", "CPU Info Display", "PSA Debugger", "Debug Overlay",
        "Match HUD", "Fighter Models", "Stage Model", "Effects"
    };
    const ui::Page& dbg = ui::pages::kDebug;
    REQUIRE(dbg.count == 8);
    for (int i = 0; i < 8; i++)
    {
        CHECK(strcmp(dbg.options[i].name, kExpectedDbg[i]) == 0);
    }

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(lab_menu, stream1_combo_lab_inline_rows_on_scenarios)
{
    bootLab();
    main::setExtraHooks(&kNoHooks);
    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);

    ui::pages::ScenarioMenuState* sm = ui::pages::scenarioMenuState();
    memset(sm, 0, sizeof(*sm));
    sm->preset = ui::pages::kPresetComboLab;
    sm->autoReset = 1;
    main::settings().cpu.options.control = cpu::kControlLab;

    const ui::Page& scen = ui::pages::kScenarios;
    static const char* const kExpectedComboLab[11] = {
        "Preset", "Auto-Reset", "Side",
        "Post-Hitstun", "Reset Delay", "Trajectory DI", "Tech Option", "Reset P1", "Noclip Reset",
        "Reset Now", "More Combo Options"
    };
    int vis = 0;
    for (int i = 0; i < scen.count; i++)
    {
        bool hidden = (scen.options[i].flags & ui::kFlagHideWhenDisabled) != 0
                      && scen.options[i].enabled != NULL && !scen.options[i].enabled();
        if (!hidden)
        {
            REQUIRE(vis < 11);
            CHECK(strcmp(scen.options[i].name, kExpectedComboLab[vis]) == 0);
            vis++;
        }
    }
    CHECK_EQ(11, vis);

    // Editing inline Post-Hitstun and Noclip Reset mutates main::settings().comboTrainer directly.
    const u8 scenPath[] = {ui::pages::P_SCENARIOS};
    REQUIRE(ui::menuJump(menu, st, scenPath, 1));
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};
    st.cursor[ui::pages::P_SCENARIOS] = 3; // Post-Hitstun
    main::settings().comboTrainer.postHitstunEscape = main::kComboEscapeDodge;
    CHECK(ui::menuStep(menu, st, inRight));
    CHECK_EQ(static_cast<int>(main::kComboEscapeJumpInvuln),
             static_cast<int>(main::settings().comboTrainer.postHitstunEscape));

    st.cursor[ui::pages::P_SCENARIOS] = 8; // Noclip Reset
    main::settings().comboTrainer.noclip = 0;
    CHECK(ui::menuStep(menu, st, inRight));
    CHECK_EQ(1, static_cast<int>(main::settings().comboTrainer.noclip));

    sm->preset = ui::pages::kPresetOff;
    main::setExtraHooks(NULL);
    storage::detach();
}
