// Does every text the mod can draw fit where it is drawn?
//
// A text that is too long for its place still "works": the tip box shows
// what fits and drops the rest, a menu row squeezes its name until it
// cannot be read, a HUD value runs over the panel's edge. Nobody sees it
// until the one moment that text is on screen. These tests look at every
// text, in the place it is drawn, with the game's own font:
//
//   - wider than its box or column (by how many units of the 640x480
//     screen, on 4:3 and on 16:9);
//   - more lines than the place shows, or a line or a whole text longer
//     than the buffer it is copied through (the rest is not shown);
//   - squeezed below kMinSqueeze of its natural width, where the place
//     squeezes long text instead of letting it run over (the menus, the
//     messages, the info display);
//   - two texts on one line that meet.
//
// Where the texts come from: the lab menu's pages and the drill list are
// walked in the compiled sources; everything written where it is used (a
// tip, a HUD value, a message) is read out of the sources by
// tests/host/text_rules.py, a printf-style text with its longest values
// (host/text_items.h). A text the rules do not know fails here, so text
// that is added later cannot go unchecked.
//
// What draws: the mod's own code wherever it can be compiled on the PC:
// the text functions (src/main/gfx/text.cpp), the menu (src/main/ui/
// menu.cpp), the messages (src/main/ui/osd.cpp, included below), the
// notices (src/main/storage/notice.cpp), the drill list and the recordings
// browser (src/menu/drill_list.cpp and browser.cpp, included below), the
// recording's bar (src/main/record/hud.cpp), and the watermark. They print
// through a stand-in for the game's character writer that has the game's font's
// advances (host/canvas.h), so the widths are the console's. The places whose
// code cannot be compiled here (the info display, the ledgedash HUD's panel,
// the pause-menu hint, the debug overlay) are restated as numbers in
// text_rules.py and tied to the source by a checksum of the function: when it
// is edited, the run fails until the numbers were looked at again.
//
// Texts that do not fit today are listed in kKnown below with what is wrong.
// A listed text does not fail the run; a listed text that fits now does,
// until its line is taken out: fixing a text means deleting its line.
// `make check-host HOST_ARGS="--only text_fit --log"` prints every problem,
// listed or new, as a line for kKnown with its numbers.

// src/main/ui/osd.cpp is compiled into this file under another namespace
// name: the test layer has its own osd::post (host/fakes.cpp keeps the
// messages for other tests), and the real one is wanted here.
// src/menu/drill_list.cpp is compiled into this file as well (below). It
// belongs to the menu module; here it logs through the match module's
// services, which changes nothing it draws.
#define osd osd_real
#define TM_MODULE_MAIN 1

#include "extra/menu_descriptions.h"
#include "extra/menu_summaries.h"
#include "extra/stage_options_logic.h"
#include "host/canvas.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "host/text_items.h"
#include "main/extra.h"
#include "main/gfx/text.h"
#include "main/record/hud.h"
#include "main/record/slots.h"
#include "main/settings.h"
#include "main/storage/format.h"
#include "main/storage/notice.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/osd.h"
#include "main/ui/pages/pages.h"
#include "main/ui/settings_modal.h"
#include "main/ui/style.h"
#include "main/ui/watermark.h"
#include "menu/names.h"
#include "menu/osd_quick.h"
#include "menu/session.h"
#include "shared/interface.h"

#include "main/ui/osd.cpp"
// The drill list's drawing functions are file-local (static): it is
// included too, to call them.
#include "menu/drill_list.cpp"
#include "menu/browser.cpp"

using namespace tm;
using host::text::Item;
using host::text::kItems;
namespace canvas = host::canvas;

namespace {
    // A text squeezed to less than this of its natural width is taken as
    // not fitting. At 0.8 the letters remain clearly readable.
    const float kMinSqueeze = 0.8f;
    // The widescreen factors the mod draws with (lab.cpp, drill_list.cpp).
    const float kAspects[2] = {1.0f, 0.75f};
    const char* const kAspectNames[2] = {"4:3", "16:9"};
    // Room wanted between a text and the edge of its box, and between two
    // texts on one line.
    const float kEdge = 2.0f;
    const float kBetween = 8.0f;
    const float kScreen = 640.0f;

    // ---- texts that do not fit today -----------------------------------------
    //
    // {kind, text}: the kind is the first word of the failure's line, the
    // text is the whole text as the failure shows it. Delete a line when its
    // text was fixed (the run says so).
    struct Known {
        const char* kind;
        const char* text;
    };
    const Known kKnown[] = {
        // None of these can be fixed by wording: each is a number at its
        // largest, a name of the game's own, or a layout.
        // ---- Restated places (info display)
        // main/ui/info_display.cpp: every interrupt group open at once
        {"squeeze", "IASA: Spc Itm Grab Atk Roll Shld Jump Move  Cancel: true"},
        // main/ui/info_display.cpp: every interrupt group open at once
        {"squeeze", "IASA: ASpc AItm Tthr ADodge AAtk Trd WJmp DJmp  Cancel: true"},
        {NULL, NULL}
    };
    bool s_knownHit[sizeof(kKnown) / sizeof(kKnown[0])];
    // The problems of the running test, each once (a text can be wrong in
    // the same way on several rows or on both screen shapes; a text with two
    // different problems shows both).
    enum { kMaxSeen = 4096 };
    u32 s_seen[kMaxSeen];
    int s_seenCount = 0;
    int s_checked = 0;
    int s_knownCount = 0;
    int s_failed = 0;

    void begin()
    {
        memset(s_knownHit, 0, sizeof(s_knownHit));
        s_checked = s_knownCount = s_failed = s_seenCount = 0;
    }

    // `text` with line breaks and quotes written as in a C string.
    const char* shown(const char* text)
    {
        static char out[4][512];
        static int turn = 0;
        char* o = out[turn = (turn + 1) % 4];
        size_t n = 0;
        for (; *text != '\0' && n + 5 < sizeof(out[0]); text++)
        {
            unsigned char ch = static_cast<unsigned char>(*text);
            if (ch == '\n')
            {
                o[n++] = '\\';
                o[n++] = 'n';
            }
            else if (ch == '"' || ch == '\\')
            {
                o[n++] = '\\';
                o[n++] = static_cast<char>(ch);
            }
            else if (ch < 32 || ch > 126)
            {
                n += snprintf(o + n, 5, "\\%03o", ch);
            }
            else
            {
                o[n++] = static_cast<char>(ch);
            }
        }
        o[n] = '\0';
        return o;
    }

    // One text that does not fit: a failure, unless kKnown lists it.
    void misfit(const char* kind, const char* where, const char* text, const char* format, ...)
    {
        char what[384];
        va_list args;
        va_start(args, format);
        vsnprintf(what, sizeof(what), format, args);
        va_end(args);
        u32 hash = 2166136261u;
        for (const char* c = kind; *c != '\0'; c++)
        {
            hash = (hash ^ static_cast<u8>(*c)) * 16777619u;
        }
        for (const char* c = text; *c != '\0'; c++)
        {
            hash = (hash ^ static_cast<u8>(*c)) * 16777619u;
        }
        for (const char* c = what; *c != '\0'; c++)
        {
            hash = (hash ^ static_cast<u8>(*c)) * 16777619u;
        }
        for (int i = 0; i < s_seenCount; i++)
        {
            if (s_seen[i] == hash)
            {
                return;
            }
        }
        if (s_seenCount < kMaxSeen)
        {
            s_seen[s_seenCount++] = hash;
        }
        bool known = false;
        for (int i = 0; kKnown[i].kind != NULL && !known; i++)
        {
            if (strcmp(kKnown[i].kind, kind) == 0 && strcmp(kKnown[i].text, text) == 0)
            {
                s_knownCount += s_knownHit[i] ? 0 : 1;
                s_knownHit[i] = true;
                known = true;
            }
        }
        if (host::log::g_echo)
        {
            // Every problem, as a line for kKnown with what is wrong after it.
            printf("     | %s {\"%s\", \"%s\"}, // %s: %s\n", known ? "known" : "new  ", kind, shown(text), where, what);
        }
        if (known)
        {
            return;
        }
        if (++s_failed <= 12)
        {
            host::test::fail(__FILE__, __LINE__, "%s \"%s\" (%s): %s", kind, shown(text), where, what);
        }
    }

    // After a test's texts: the listed ones of its kinds that fit now.
    void end(const char* kinds)
    {
        for (int i = 0; kKnown[i].kind != NULL; i++)
        {
            char padded[64];
            snprintf(padded, sizeof(padded), " %s ", kKnown[i].kind);
            if (!s_knownHit[i] && strstr(kinds, padded) != NULL)
            {
                host::test::fail(__FILE__, __LINE__,
                                 "%s \"%s\" is listed in kKnown as not fitting, and it fits now (or is no longer "
                                 "drawn): take its line out of kKnown",
                                 kKnown[i].kind, shown(kKnown[i].text));
            }
        }
        if (s_failed > 12)
        {
            host::test::fail(__FILE__, __LINE__, "and %d more (HOST_ARGS=\"--only text_fit --log\" lists all)",
                             s_failed - 12);
        }
        if (s_knownCount > 0)
        {
            NOTE("%d texts checked; %d do not fit and are listed in kKnown", s_checked, s_knownCount);
        }
        else
        {
            NOTE("%d texts checked", s_checked);
        }
    }

    bool is(const Item& item, const char* kind)
    {
        return strcmp(item.kind, kind) == 0;
    }

    int lineCount(const char* text)
    {
        if (*text == '\0')
        {
            return 0;
        }
        int n = 1;
        for (; *text != '\0'; text++)
        {
            n += (*text == '\n' && text[1] != '\0') ? 1 : 0;
        }
        return n;
    }

    // Line `index` of `text` (lines end at '\n').
    const char* lineOf(const char* text, int index)
    {
        static char out[512];
        for (; index > 0 && *text != '\0'; text++)
        {
            index -= *text == '\n' ? 1 : 0;
        }
        size_t n = 0;
        while (text[n] != '\0' && text[n] != '\n' && n + 1 < sizeof(out))
        {
            out[n] = text[n];
            n++;
        }
        out[n] = '\0';
        return out;
    }

    int longestLine(const char* text)
    {
        int longest = 0;
        for (int i = 0; i < lineCount(text); i++)
        {
            int n = static_cast<int>(strlen(lineOf(text, i)));
            longest = n > longest ? n : longest;
        }
        return longest;
    }

    // The run that drew exactly `text`, the last one if there are several
    // (bold text and outlined text are drawn more than once); NULL if none.
    const canvas::Run* runOf(const char* text)
    {
        for (int i = canvas::runs() - 1; i >= 0; i--)
        {
            if (strcmp(canvas::run(i).text, text) == 0)
            {
                return &canvas::run(i);
            }
        }
        return NULL;
    }

    // How much of its natural width the menu's bold text kept (1: not
    // squeezed).
    float boldSqueeze(const canvas::Run& r)
    {
        return r.scaleX / (r.scaleY * gfx::xScale());
    }

    void checkGlyphs(const char* kind, const Item& item)
    {
        for (const char* c = item.text; *c != '\0'; c++)
        {
            unsigned char ch = static_cast<unsigned char>(*c);
            if (ch != '\n' && (ch < 0x20 || ch > 0x7E))
            {
                misfit(kind, item.where, item.text, "character %d is not one the font is known to have", ch);
                return;
            }
        }
    }

    // ---- the menu, drawn by src/main/ui/menu.cpp -------------------------------

    u8 s_values[64];
    u8* values()
    {
        return s_values;
    }

    // Draws a one-row page with `o` under the cursor.
    void drawRow(const char* title, const ui::Option& o)
    {
        static ui::Option row[1];
        static ui::Page page;
        static const ui::Page* pages[1] = {&page};
        row[0] = o;
        page.title = title;
        page.options = row;
        page.count = 1;
        ui::MenuDef def = {pages, 1, values};
        ui::MenuState state;
        memset(&state, 0, sizeof(state));
        memset(s_values, 0, sizeof(s_values));
        canvas::clear();
        ui::menuDraw(def, state);
    }

    ui::Option plainRow(const char* name, const char* desc, const char* value)
    {
        ui::Option o;
        memset(&o, 0, sizeof(o));
        o.kind = ui::kKindFunc;
        o.name = name;
        o.desc = desc;
        o.data = value;
        return o;
    }

    // What the menu did with one text: how far it squeezed each line.
    void checkSqueezed(const char* kind, const char* where, const char* whole, const char* line, const char* part)
    {
        const canvas::Run* r = runOf(line);
        if (r == NULL)
        {
            misfit(kind, where, whole, "%s was not drawn", part);
            return;
        }
        float kept = boldSqueeze(*r);
        if (kept < kMinSqueeze - 0.0005f)
        {
            float natural = (r->right - r->x) / kept;
            misfit(kind, where, whole, "%s is squeezed to %.0f%% of its width (%.0f units into %.0f; at least %.0f%% "
                   "is wanted): %.0f units too wide",
                   part, kept * 100.0f, natural, r->right - r->x, kMinSqueeze * 100.0f,
                   natural - (r->right - r->x) / kMinSqueeze);
        }
    }

    void bootLab()
    {
        storage::detach();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }
    bool textFitYes()
    {
        return true;
    }
    bool textFitYesCall(int)
    {
        return true;
    }
}

// ---- nothing unchecked ------------------------------------------------------------

TEST(text_fit, every_text_in_the_sources_is_known)
{
    // tests/host/text_rules.py names what every string literal of the mod's
    // sources is. One it does not know, a printf-style text with an argument
    // whose longest value it does not know, or a drawing function whose
    // layout it restates and which was edited since, comes here as a
    // "problem".
    int items = 0;
    int problems = 0;
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        items++;
        if (is(*item, "problem"))
        {
            if (++problems <= 20)
            {
                host::test::fail(__FILE__, __LINE__, "%s: %s", item->where, item->text);
            }
        }
        else
        {
            checkGlyphs(item->kind, *item);
        }
    }
    CHECK(items > 100);
    if (problems > 20)
    {
        host::test::fail(__FILE__, __LINE__, "and %d more", problems - 20);
    }
    NOTE("%d texts read out of the sources", items - problems);
}

TEST(text_fit, the_font_is_the_games)
{
    // The stand-in writer's advances are the game's font's: "Frame Advance"
    // at scale 1 adds up to 150, and at scaleForCap(kRowCap) scales linearly.
    CHECK_NEAR(150.0, gfx::textWidth("Frame Advance", 1.0f), 0.001);
    CHECK_NEAR(150.0 * ui::style::scaleForCap(ui::style::kRowCap),
               gfx::textWidth("Frame Advance", ui::style::scaleForCap(ui::style::kRowCap)), 0.01);
    CHECK_NEAR(24.0, gfx::lineHeight(1.0f), 0.001);
    // On 16:9 text is narrowed with everything else.
    gfx::setXScale(0.75f);
    CHECK_NEAR(112.5, gfx::textWidth("Frame Advance", 1.0f), 0.001);
    // Without the font nothing is drawn or measured.
    canvas::setFont(false);
    CHECK_EQ(0.0f, gfx::textWidth("Frame Advance", 1.0f));
    CHECK_EQ(0.0f, gfx::lineHeight(1.0f));
}

// ---- on-screen messages -----------------------------------------------------------

TEST(text_fit, osd_messages)
{
    // osd::post splits a message into at most kMaxLines lines of at most
    // kLineChars characters; osd::draw squeezes a line wider than 114 units.
    begin();
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        if (!is(*item, "osd"))
        {
            continue;
        }
        s_checked++;
        const char* text = item->text;
        int lines = lineCount(text);
        if (lines > osd::kMaxLines)
        {
            misfit("osd", item->where, text, "%d lines, a message has %d", lines, osd::kMaxLines);
        }
        if (longestLine(text) > osd::kLineChars)
        {
            misfit("osd", item->where, text, "a line of %d characters, a message shows %d of a line",
                   longestLine(text), osd::kLineChars);
        }
        for (int a = 0; a < 2; a++)
        {
            gfx::setXScale(kAspects[a]);
            canvas::clear();
            osd::reset();
            host::fakes::setTrackerFrame(100);
            REQUIRE(osd::post(0, osd::kIdMiscellaneous, osd::kWhite, "%s", text) != NULL);
            osd::draw();
            int fgRuns = 0;
            for (int i = 0; i < canvas::runs(); i++)
            {
                const canvas::Run& r = canvas::run(i);
                float kept = r.scaleX / (r.scaleY * kAspects[a]);
                if (kept < kMinSqueeze - 0.0005f)
                {
                    float natural = (r.right - r.x) / kept;
                    misfit("osd", item->where, text, "\"%s\" is squeezed to %.0f%% of its width on %s (%.0f units "
                           "into %.0f; at least %.0f%% is wanted): %.0f units too wide",
                           shown(r.text), kept * 100.0f, kAspectNames[a], natural, r.right - r.x, kMinSqueeze * 100.0f,
                           natural - (r.right - r.x) / kMinSqueeze);
                }
                // Default 80% smoked cards draw a 1px black drop shadow behind each line.
                if ((r.color & 0xFFFFFF00u) != 0u)
                {
                    fgRuns++;
                }
            }
            if (fgRuns != (lines < osd::kMaxLines ? lines : osd::kMaxLines))
            {
                misfit("osd", item->where, text, "%d of its %d lines were drawn on %s", fgRuns, lines,
                       kAspectNames[a]);
            }
        }
    }
    CHECK(s_checked >= 40);
    end(" osd ");
}

// ---- the menus --------------------------------------------------------------------

TEST(text_fit, menu_rows_written_in_the_sources)
{
    // Rows the test cannot walk: the drills' own option rows (their tables
    // are local to sources that are not compiled here) and what the lab
    // menu's pages write into a row while the match runs. Each is drawn by
    // the real menu on a page of its own: name, value and title are
    // squeezed into their columns (238, 152 and 465 wide), a description
    // into the panel's width (479).
    begin();
    gfx::setXScale(1.0f);
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        const char* text = item->text;
        if (*text == '\0')
        {
            continue;
        }
        if (is(*item, "menu-name"))
        {
            s_checked++;
            drawRow("Title", plainRow(text, "", NULL));
            checkSqueezed("menu-name", item->where, text, text, "the name");
        }
        else if (is(*item, "menu-value"))
        {
            s_checked++;
            drawRow("Title", plainRow("Name", "", text));
            checkSqueezed("menu-value", item->where, text, text, "the value");
        }
        else if (is(*item, "menu-title"))
        {
            s_checked++;
            drawRow(text, plainRow("Name", "", NULL));
            checkSqueezed("menu-title", item->where, text, text, "the title");
        }
    }
    CHECK(s_checked >= 5);
    end(" menu-name menu-value menu-title ");
}

TEST(text_fit, lab_menu_pages)
{
    // Every page of the lab menu in both Draft C (Rail) and Draft A (Tabs),
    // every dynamic Stage & Cast preset, every deep breadcrumb, every
    // controller bottom strip, and every clean-room 1-line description in
    // src/extra/menu_descriptions.cpp checked on 4:3 and 16:9.
    begin();
    bootLab();
    static main::ExtraHooks descHooks;
    memset(&descHooks, 0, sizeof(descHooks));
    descHooks.alterRow = textFitYes;
    descHooks.characterRng = textFitYesCall;
    descHooks.stageOptions = textFitYesCall;
    descHooks.fighterSwapRow = textFitYes;
    descHooks.optionDesc = extra::optionDesc;
    descHooks.submenuSummary = extra::menuSubmenuSummary;
    main::setExtraHooks(&descHooks);
    ui::g_showInactiveRows = true;

    const ui::MenuDef& menu = ui::labMenu();
    for (int style = 0; style < main::kMenuStyleCount; style++)
    {
        main::settings().controls.menuStyle = static_cast<u8>(style);
        gfx::setXScale(1.0f);
        for (int p = 0; p < menu.pageCount; p++)
        {
            const ui::Page& page = *menu.pages[p];
            for (int i = 0; i < page.count; i++)
            {
                const ui::Option& o = page.options[i];
                if (o.name == NULL)
                {
                    continue;
                }
                char where[96];
                snprintf(where, sizeof(where), "lab menu (%s), page \"%s\", row \"%s\"",
                         style == main::kMenuStyleTabs ? "Tabs" : "Rail", page.title, o.name);
                // Every row on every page must have a clean-room description in extra.
                const char* d = extra::optionDesc(static_cast<u8>(p), static_cast<u8>(i));
                if (d == NULL || d[0] == '\0')
                {
                    misfit("lab-desc", where, o.name, "missing description in src/extra/menu_descriptions.cpp");
                }
                bool fixedValue = (o.flags & (ui::kFlagUnavailable | ui::kFlagScratch)) != 0 || !ui::hasValue(o);
                int low = o.kind == ui::kKindInt ? o.min : 0;
                int high = o.kind == ui::kKindToggle ? 1 : o.max;
                int kept = fixedValue ? 0 : ui::optionValue(menu, o);
                for (int v = low; v <= (fixedValue ? low : high); v++)
                {
                    if (o.kind == ui::kKindInt && v != low && v != high)
                    {
                        continue;
                    }
                    if (!fixedValue)
                    {
                        ui::setOptionValue(menu, o, v);
                    }
                    ui::MenuState state;
                    memset(&state, 0, sizeof(state));
                    state.stack[0] = static_cast<u8>(p);
                    state.cursor[p] = static_cast<u8>(i);
                    state.scroll[p] = static_cast<u8>(i);
                    canvas::clear();
                    ui::menuDraw(menu, state);
                    s_checked++;
                    float rTop = style == main::kMenuStyleTabs ? ui::style::kTabsRowTop : ui::style::kRailRowTop;
                    for (int k = 0; k < canvas::runs(); k++)
                    {
                        const canvas::Run& r = canvas::run(k);
                        bool inTopRow = r.y > rTop - 4.0f && r.y < rTop + ui::style::kLabRowH;
                        bool inHeader = r.y < ui::style::kTabsBarBottom;
                        bool inDesc = r.y >= ui::style::kLabDescTop - 4.0f && r.y < ui::style::kLabFooterTop;
                        if (!(inTopRow || inHeader || inDesc))
                        {
                            continue;
                        }
                        float squeeze = boldSqueeze(r);
                        if (squeeze < kMinSqueeze - 0.0005f)
                        {
                            float natural = (r.right - r.x) / squeeze;
                            char text[256];
                            snprintf(text, sizeof(text), "%s", r.text);
                            misfit(inDesc ? "lab-desc" : (inHeader ? "lab-title" : "lab-row"), where, text,
                                   "squeezed to %.0f%% of its width (%.0f units into %.0f; at least %.0f%% is wanted): "
                                   "%.0f units too wide",
                                   squeeze * 100.0f, natural, r.right - r.x, kMinSqueeze * 100.0f,
                                   natural - (r.right - r.x) / kMinSqueeze);
                        }
                    }
                    CHECK(o.desc == NULL);
                }
                if (!fixedValue)
                {
                    ui::setOptionValue(menu, o, kept);
                }
            }
        }
    }

    // Check non-default submenu summary strings fit on both Rail and Top Tabs.
    {
        struct SumCase {
            u8 page;
            u8 row;
        };
        static const SumCase kCases[] = {
            {ui::pages::P_GENERAL, 9},       // Controls ("Category Rail" / "Top Tabs")
            {ui::pages::P_DISPLAY, 5},       // Boxes & Heatmap ("Grab Boxes" / "Stage Coll" / "2 Active")
            {ui::pages::P_DISPLAY, 6},       // Overlays & Meters ("4 Active")
            {ui::pages::P_DISPLAY, 7},       // Theme ("Brawl Navy" / "Custom")
            {ui::pages::P_DISPLAY, 8},       // Lab Debug ("Info HUD" / "Debug HUD")
            {ui::pages::P_CPU, 5},           // DI & SDI ("Down & Away" / "SDI + ASDI")
            {ui::pages::P_CPU, 6},           // Counters & Reversals ("Reversals")
            {ui::pages::P_CPU, 7},           // Tech & Getup ("In Place" / "Custom")
            {ui::pages::P_CPU, 8},           // Shield & Grab ("Until Hit" / "Infinite")
            {ui::pages::P_CPU_COUNTERS, 6},  // Custom Pools ("G.. A.. S..")
            {ui::pages::P_RECORDING, 7},     // Playback & Positions ("Mirrored")
            {ui::pages::P_STAGE_CAST, 4},    // Olimar ("D-Pad Pluck")
            {ui::pages::P_HUD_OVERLAYS, 0},  // P1 Overlays ("ProjectPunch")
            {ui::pages::P_DEBUG, 0}          // P1 Info Display ("Frame Data")
        };
        main::settings().controls.menuStyle = main::kMenuStyleRail;
        main::settings().display.grabVisualizer = 1;
        main::settings().osd.menuAccent = main::kMenuAccentNavy;
        main::settings().info[0].preset = 4;
        main::settings().info[0].rows[0] = 1;
        main::settings().cpu.options.tdi = cpu::kTdiDownAway;
        main::settings().cpu.options.multiReversal = 1;
        main::settings().cpu.options.tech = cpu::kTechInPlace;
        main::settings().cpu.options.infiniteShields = cpu::kShieldsOn;
        main::settings().recording.mirrored = 1;
        main::settings().fighterMechanics.olimarColorMatAndMode =
            static_cast<u16>(main::kOlimarDpadPluck << 10);
        main::settings().overlay[0].preset = 1;
        for (int st = 0; st < main::kMenuStyleCount; st++)
        {
            main::settings().controls.menuStyle = static_cast<u8>(st);
            for (u32 ci = 0; ci < sizeof(kCases) / sizeof(kCases[0]); ci++)
            {
                ui::MenuState state;
                memset(&state, 0, sizeof(state));
                state.stack[0] = kCases[ci].page;
                state.cursor[kCases[ci].page] = kCases[ci].row;
                state.scroll[kCases[ci].page] = kCases[ci].row;
                canvas::clear();
                ui::menuDraw(menu, state);
                for (int k = 0; k < canvas::runs(); k++)
                {
                    const canvas::Run& r = canvas::run(k);
                    float squeeze = boldSqueeze(r);
                    if (squeeze < kMinSqueeze - 0.0005f)
                    {
                        misfit("lab-row", "extra/menu_summaries.cpp", r.text, "summary squeezed to %.0f%%",
                               squeeze * 100.0f);
                    }
                }
            }
        }
        main::resetSettings();
    }

    // Check every dynamic Stage & Cast platform preset label table on Draft C (tightest value pill).
    main::settings().controls.menuStyle = main::kMenuStyleRail;
    static const int kStagePresets[] = {
        extra::kPresetGenericMoving, extra::kPresetLuigisMansion, extra::kPresetDelfinoSecret,
        extra::kPresetSmashville,    extra::kPresetDeadLine,      extra::kPresetYoshisStory,
        extra::kPresetYoshisIsland
    };
    const ui::Page& stagePage = *menu.pages[ui::pages::P_STAGE_CAST];
    const ui::Option& platRow = stagePage.options[3];
    int keptPlat = ui::optionValue(menu, platRow);
    for (u32 idx = 0; idx < sizeof(kStagePresets) / sizeof(kStagePresets[0]); idx++)
    {
        main::setStagePlatformRowPreset(kStagePresets[idx]);
        char where[96];
        snprintf(where, sizeof(where), "lab menu, page \"%s\", row \"%s\" (preset %d)", stagePage.title, platRow.name,
                 kStagePresets[idx]);
        for (int v = 0; v <= platRow.max; v++)
        {
            ui::setOptionValue(menu, platRow, v);
            ui::MenuState state;
            memset(&state, 0, sizeof(state));
            state.category = 6;
            state.stack[0] = static_cast<u8>(ui::pages::P_STAGE_CAST);
            state.cursor[ui::pages::P_STAGE_CAST] = 3;
            state.scroll[ui::pages::P_STAGE_CAST] = 3;
            canvas::clear();
            ui::menuDraw(menu, state);
            s_checked++;
            for (int k = 0; k < canvas::runs(); k++)
            {
                const canvas::Run& r = canvas::run(k);
                bool row = r.y > ui::style::kRailRowTop - 4.0f && r.y < ui::style::kRailRowTop + ui::style::kLabRowH;
                if (!row)
                {
                    continue;
                }
                float squeeze = boldSqueeze(r);
                if (squeeze < kMinSqueeze - 0.0005f)
                {
                    float natural = (r.right - r.x) / squeeze;
                    char text[256];
                    snprintf(text, sizeof(text), "%s", r.text);
                    misfit("lab-row", where, text,
                           "squeezed to %.0f%% of its width (%.0f units into %.0f; at least %.0f%% is wanted): "
                           "%.0f units too wide",
                           squeeze * 100.0f, natural, r.right - r.x, kMinSqueeze * 100.0f,
                           natural - (r.right - r.x) / kMinSqueeze);
                }
            }
        }
    }
    main::setStagePlatformRowPreset(extra::kPresetNone);
    ui::setOptionValue(menu, platRow, keptPlat);

    // Check every clean-room description literal from extra/menu_descriptions.cpp
    // plus rail labels, deep breadcrumbs, and all 4 controller footers on 4:3 and 16:9.
    for (int a = 0; a < 2; a++)
    {
        gfx::setXScale(kAspects[a]);
        for (int style = 0; style < main::kMenuStyleCount; style++)
        {
            main::settings().controls.menuStyle = static_cast<u8>(style);
            for (u8 ct = 0; ct < 4; ct++)
            {
                for (int openStart = 0; openStart <= 1; openStart++)
                {
                    ui::MenuState state;
                    memset(&state, 0, sizeof(state));
                    state.category = 1;
                    state.depth = 2;
                    state.stack[0] = ui::pages::P_SCENARIOS;
                    state.stack[1] = ui::pages::P_COMBO_TRAINER;
                    state.stack[2] = ui::pages::P_COMBO_METERS;
                    state.openStart = openStart != 0;
                    state.controllerType = ct;
                    canvas::clear();
                    ui::menuDraw(menu, state);
                    s_checked++;
                    for (int k = 0; k < canvas::runs(); k++)
                    {
                        const canvas::Run& r = canvas::run(k);
                        float squeeze = boldSqueeze(r);
                        if (squeeze < kMinSqueeze - 0.0005f)
                        {
                            misfit("lab-title", "lab chrome", r.text, "squeezed to %.0f%% on %s", squeeze * 100.0f,
                                   kAspectNames[a]);
                        }
                        if (r.right > ui::style::cx(ui::style::kLabInnerRight) + 0.5f)
                        {
                            misfit("lab-title", "lab chrome", r.text, "runs past right edge by %.1f on %s",
                                   r.right - ui::style::cx(ui::style::kLabInnerRight), kAspectNames[a]);
                        }
                    }

                    // Worst-case dense footer: Lab > P1 Damage ("Reset 0%" + "Categories" + "Brawl Training" + "Reset" + "Reset Page").
                    ui::MenuState dmgState;
                    memset(&dmgState, 0, sizeof(dmgState));
                    dmgState.category = 0;
                    dmgState.depth = 0;
                    dmgState.stack[0] = ui::pages::P_GENERAL;
                    dmgState.cursor[ui::pages::P_GENERAL] = 1; // P1 Damage
                    dmgState.openStart = openStart != 0;
                    dmgState.controllerType = ct;
                    canvas::clear();
                    ui::menuDraw(menu, dmgState);
                    s_checked++;
                    for (int k = 0; k < canvas::runs(); k++)
                    {
                        const canvas::Run& r = canvas::run(k);
                        if (r.right > ui::style::cx(ui::style::kLabInnerRight) + 0.5f)
                        {
                            misfit("lab-title", "lab dense footer", r.text, "runs past right edge by %.1f on %s",
                                   r.right - ui::style::cx(ui::style::kLabInnerRight), kAspectNames[a]);
                        }
                    }
                }
            }
        }
        // Check all 10 Draft C rail item descriptions too.
        main::settings().controls.menuStyle = main::kMenuStyleRail;
        for (u8 rc = 0; rc < menu.categoryCount + menu.utilityCount; rc++)
        {
            ui::MenuState state;
            memset(&state, 0, sizeof(state));
            state.railFocus = true;
            state.railCursor = rc;
            canvas::clear();
            ui::menuDraw(menu, state);
            s_checked++;
            const char* expected = extra::optionDesc(menu.pageCount, rc);
            REQUIRE(expected != NULL);
            const canvas::Run* r = runOf(expected);
            if (r == NULL)
            {
                misfit("lab-desc", "rail", expected, "rail description was not drawn on %s", kAspectNames[a]);
            }
            else if (boldSqueeze(*r) < kMinSqueeze - 0.0005f)
            {
                misfit("lab-desc", "rail", expected, "squeezed to %.0f%% on %s", boldSqueeze(*r) * 100.0f,
                       kAspectNames[a]);
            }
        }
    }
    gfx::setXScale(1.0f);
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        if (!is(*item, "lab-desc"))
        {
            continue;
        }
        s_checked++;
        float wDesc = ui::style::boldWidth(item->text, ui::style::scaleForCap(ui::style::kLabDescCap));
        float maxW = 544.0f;
        if (wDesc > maxW / kMinSqueeze)
        {
            misfit("lab-desc", item->where, item->text, "description width %.1f exceeds %.1f", wDesc,
                   maxW / kMinSqueeze);
        }
    }

    ui::g_showInactiveRows = false;
    main::setExtraHooks(NULL);
    CHECK(s_checked >= 600);
    end(" lab-title lab-row lab-desc ");
}

namespace {
    // What a row of the drill list shows at its right end: the run on the
    // name's line that is not the name, wherever it starts.
    const canvas::Run* rowRight(const canvas::Run& name)
    {
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& o = canvas::run(i);
            if (o.y > name.y - 0.5f && o.y < name.y + 0.5f && strcmp(o.text, name.text) != 0)
            {
                return &o;
            }
        }
        return NULL;
    }

    bool endsWith(const char* text, const char* end)
    {
        size_t n = strlen(text);
        size_t k = strlen(end);
        return n >= k && strcmp(text + n - k, end) == 0;
    }

    float runCapTop(const canvas::Run& r)
    {
        float off = r.displayFont ? ui::style::kDisplayCapOffset : ui::style::kCapOffset;
        return r.y + off * r.scaleY;
    }

    float runCapBottom(const canvas::Run& r)
    {
        float h = r.displayFont ? ui::style::kDisplayCapHeight : ui::style::kCapHeight;
        return runCapTop(r) + h * r.scaleY;
    }

    // Draws the list twice (right-aligned text learns its width on the
    // first pass, as on the console's first frame).
    void drawList()
    {
        gfx::forgetWidths();
        for (int f = 0; f < 9; f++)
        {
            canvas::clear();
            menu::list::draw();
        }
        canvas::clear();
        menu::list::draw();
    }

    // The footer band's text: each line within the footer's width and not
    // squeezed below kMinSqueeze; every character one the font has.
    void checkListFooter(const char* kind, const char* where, float xs, int a, bool bold)
    {
        using namespace ui::style;
        float top = textYForCap(kFooterCapTop, scaleForCap(kRowCap)) - 0.5f;
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& r = canvas::run(i);
            if (r.y < top)
            {
                continue;
            }
            float kept = bold ? boldSqueeze(r) : r.scaleX / (r.scaleY * xs);
            if (kept < kMinSqueeze - 0.0005f)
            {
                float natural = (r.right - r.x) / kept;
                misfit(kind, where, r.text, "\"%s\" is squeezed to %.0f%% of its width on %s (%.0f units into %.0f; at "
                       "least %.0f%% is wanted): %.0f units too wide",
                       shown(r.text), kept * 100.0f, kAspectNames[a], natural, r.right - r.x,
                       kMinSqueeze * 100.0f, natural - (r.right - r.x) / kMinSqueeze);
            }
            float limit = cx(kFooterX) + w(kFooterMaxW);
            if (r.right > limit + 0.01f)
            {
                misfit(kind, where, r.text, "\"%s\" is %.1f units wider than the footer on %s", shown(r.text),
                       r.right - limit, kAspectNames[a]);
            }
            if (r.unknown > 0)
            {
                misfit(kind, where, r.text, "\"%s\" has %d characters the font has no glyph for",
                       shown(r.text), r.unknown);
            }
        }
    }
}

TEST(text_fit, drill_list)
{
    // Boot menu (src/menu/drill_list.cpp, included above): every
    // section, row, details card, chip, starting banner, and per-controller
    // footer badge strip on both 4:3 and 16:9, plus retired drawPending().
    using namespace ui::style;
    namespace list = menu::list;
    begin();
    for (int a = 0; a < 2; a++)
    {
        gfx::setXScale(kAspects[a]);
        float xs = kAspects[a];
        const float safeLeft = 32.0f;
        const float safeRight = 608.0f;
        const float safeTop = 24.0f;
        const float safeBottom = 456.0f;

        for (int p = 0; p < list::kSectionCount; p++)
        {
            const list::LauncherSection& sec = list::kSections[p];
            for (int i = 0; i < sec.count; i++)
            {
                const list::LauncherRow& r = list::rowAt(sec, i);
                s_checked += a == 0 ? 1 : 0;
                list::s_page = p;
                list::s_row = i;
                list::s_scroll = i >= list::kVisibleRows ? i - list::kVisibleRows + 1 : 0;
                list::s_starting = false;
                if (r.kind == list::kRowSkillDrill && r.targetId < drills::kBestSlots)
                {
                    menu::session().best[r.targetId] = (i % 2 == 0) ? 10 : 0;
                }
                drawList();

                const canvas::Run* banner = runOf("Training Mode");
                REQUIRE(banner != NULL);
                const canvas::Run* secRun = runOf(sec.name);
                REQUIRE(secRun != NULL);
                const canvas::Run* setup = runOf("Setup");
                REQUIRE(setup != NULL);
                REQUIRE(runOf(r.chipA) != NULL);
                REQUIRE(runOf(r.chipB) != NULL);

                for (int k = 0; k < canvas::runs(); k++)
                {
                    const canvas::Run& run = canvas::run(k);
                    float kept = run.scaleX / (run.scaleY * xs);
                    if (kept < kMinSqueeze - 0.0005f)
                    {
                        float natural = (run.right - run.x) / kept;
                        misfit("list-desc", r.name, run.text,
                               "squeezed to %.0f%% on %s (%.0f into %.0f; min %.0f%%)",
                               kept * 100.0f, kAspectNames[a], natural, run.right - run.x, kMinSqueeze * 100.0f);
                    }
                    float ct = runCapTop(run);
                    float cb = runCapBottom(run);
                    if (run.x < safeLeft - 0.05f || run.right > safeRight + 0.05f || ct < safeTop - 0.05f
                        || cb > safeBottom + 0.05f)
                    {
                        misfit("list-desc", r.name, run.text,
                               "leaves CRT safe area (%.1f..%.1f, %.1f..%.1f) on %s",
                               run.x, run.right, ct, cb, kAspectNames[a]);
                    }
                    if (run.unknown > 0)
                    {
                        misfit("list-desc", r.name, run.text, "%d unknown glyph(s) on %s", run.unknown,
                               kAspectNames[a]);
                    }
                    for (const char* ch = run.text; *ch != '\0'; ch++)
                    {
                        unsigned char u = static_cast<unsigned char>(*ch);
                        if (u < 0x20 || u > 0x7E)
                        {
                            misfit("list-desc", r.name, run.text, "non-ASCII byte %d on %s", u, kAspectNames[a]);
                            break;
                        }
                    }
                    // Top banner runs must stay clear of the slanted teal edge.
                    if (cb < list::kBannerBottom + 0.5f)
                    {
                        float t = (cb - list::kBannerTop) / (list::kBannerBottom - list::kBannerTop);
                        float edgeX = cx(list::kBannerRightTop + (list::kBannerRightBot - list::kBannerRightTop) * t);
                        if (run.right > edgeX - w(4.0f))
                        {
                            misfit("list-title", sec.name, run.text, "reaches %.1f past banner slant %.1f on %s",
                                   run.right, edgeX, kAspectNames[a]);
                        }
                    }
                    // Left column runs must stay inside kColLeft..kColRight.
                    else if (ct >= list::kColTop - 1.0f && cb <= list::kCardBottom + 1.0f
                             && run.x < cx(list::kCardLeft))
                    {
                        if (run.right > cx(list::kColBarRight) + 0.05f)
                        {
                            misfit("list-name", sec.name, run.text, "%.1f units past left column on %s",
                                   run.right - cx(list::kColBarRight), kAspectNames[a]);
                        }
                    }
                    // Right details card runs must stay inside inner card borders.
                    else if (ct >= list::kCardTop - 1.0f && cb <= list::kCardBottom + 1.0f
                             && run.x >= cx(list::kCardLeft))
                    {
                        float cardMaxX = cx(list::kCardRight - list::kCardBorder);
                        if (run.right > cardMaxX + 0.05f)
                        {
                            misfit("list-desc", r.name, run.text, "%.1f units past details card on %s",
                                   run.right - cardMaxX, kAspectNames[a]);
                        }
                    }
                }
                // No two distinct runs on overlapping lines may collide.
                for (int u = 0; u < canvas::runs(); u++)
                {
                    const canvas::Run& pRun = canvas::run(u);
                    float pt = runCapTop(pRun);
                    float pb = runCapBottom(pRun);
                    for (int v = 0; v < canvas::runs(); v++)
                    {
                        if (u == v)
                        {
                            continue;
                        }
                        const canvas::Run& qRun = canvas::run(v);
                        float qt = runCapTop(qRun);
                        float qb = runCapBottom(qRun);
                        if (pRun.x > qRun.x || pb <= qt || qb <= pt)
                        {
                            continue;
                        }
                        if (pRun.right + kBetween * xs > qRun.x)
                        {
                            misfit("list-desc", r.name, pRun.text, "meets \"%s\" on %s (gap %.1f)",
                                   shown(qRun.text), kAspectNames[a], qRun.x - pRun.right);
                        }
                    }
                }
            }
        }

        // Starting match status inside the details card.
        list::s_page = 0;
        list::s_row = 0;
        list::s_scroll = 0;
        list::s_starting = true;
        drawList();
        const canvas::Run* startingRun = runOf("Starting match...");
        REQUIRE(startingRun != NULL);
        if (startingRun->x < cx(list::kCardLeft) || startingRun->right > cx(list::kCardRight - list::kCardBorder))
        {
            misfit("list-reason", "menu/drill_list.cpp", startingRun->text, "outside details card on %s",
                   kAspectNames[a]);
        }
        list::s_starting = false;

        // Per-controller footer badges & banner shoulder pills across all 4 controller types.
        static const struct {
            int port;
            u32 type;
            const char* confirmGlyph;
            const char* closeGlyph;
            const char* importGlyph;
            const char* osdGlyph;
            const char* sectionGlyph;
        } kFooterCases[] = {
            {0, gfPadType::GCC, "A", "B", "X", "Y", "L/R"},
            {4, gfPadType::WII_CLASSIC, "A", "B", "X", "Y", "L/R"},
            {4, gfPadType::WIIMOTE, "2", "1", "A", "-+A", "-/+"},
            {4, gfPadType::NUNCHUK, "A", "B", "C", "Z", "-/+"},
        };
        for (u32 c = 0; c < sizeof(kFooterCases) / sizeof(kFooterCases[0]); c++)
        {
            for (int i = 0; i < list::kPorts; i++)
            {
                list::s_sys[i].connected = false;
                list::s_sys[i].type = 0;
            }
            list::s_sys[kFooterCases[c].port].connected = true;
            list::s_sys[kFooterCases[c].port].type = kFooterCases[c].type;
            list::s_owner = kFooterCases[c].port;
            drawList();
            REQUIRE(runOf(kFooterCases[c].confirmGlyph) != NULL);
            REQUIRE(runOf(kFooterCases[c].closeGlyph) != NULL);
            REQUIRE(runOf(kFooterCases[c].importGlyph) != NULL);
            REQUIRE(runOf(kFooterCases[c].osdGlyph) != NULL);
            REQUIRE(runOf(kFooterCases[c].sectionGlyph) != NULL);
            REQUIRE(runOf("Select") != NULL);
            REQUIRE(runOf("Close") != NULL);
            REQUIRE(runOf("Recordings") != NULL);
            REQUIRE(runOf("Quick Setup") != NULL);
            REQUIRE(runOf("Section") != NULL);
            for (int k = 0; k < canvas::runs(); k++)
            {
                const canvas::Run& run = canvas::run(k);
                float ct = runCapTop(run);
                float cb = runCapBottom(run);
                if (ct >= list::kStripTop - 1.0f)
                {
                    if (run.x < cx(list::kColLeft) - 0.05f || run.right > cx(list::kCardRight) + 0.05f
                        || cb > safeBottom + 0.05f)
                    {
                        misfit("list-desc", "menu/drill_list.cpp", run.text,
                               "footer item leaves safe strip (%.1f..%.1f, y %.1f..%.1f) on %s",
                               run.x, run.right, ct, cb, kAspectNames[a]);
                    }
                }
            }
            s_checked += a == 0 ? 1 : 0;
        }

        // Check every controller-type reopen hint still maps via reopenHint(),
        // and drawPending() draws a CRT-safe Brawl CSS mode pill on 4:3 and 16:9.
        const char* hints[8] = {};
        bool hintSeen[8] = {};
        int hintCount = 0;
        for (const Item* item = kItems; item->kind != NULL; item++)
        {
            if (is(*item, "list-pending"))
            {
                bool dup = false;
                for (int h = 0; h < hintCount; h++)
                {
                    if (strcmp(hints[h], item->text) == 0)
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                {
                    REQUIRE(hintCount < 8);
                    hints[hintCount++] = item->text;
                }
            }
        }
        CHECK_EQ(3, hintCount);
        static const struct {
            int port;
            u32 type;
        } kHintCases[] = {
            {0, gfPadType::GCC},
            {4, gfPadType::WII_CLASSIC},
            {4, gfPadType::WIIMOTE},
            {4, gfPadType::NUNCHUK},
        };
        for (u32 c = 0; c < sizeof(kHintCases) / sizeof(kHintCases[0]); c++)
        {
            for (int i = 0; i < list::kPorts; i++)
            {
                list::s_sys[i].connected = false;
                list::s_sys[i].type = 0;
            }
            list::s_sys[kHintCases[c].port].connected = true;
            list::s_sys[kHintCases[c].port].type = kHintCases[c].type;
            list::s_owner = kHintCases[c].port;
            const char* hint = list::reopenHint();
            int match = -1;
            for (int h = 0; h < hintCount; h++)
            {
                if (strcmp(hints[h], hint) == 0)
                {
                    match = h;
                    hintSeen[h] = true;
                    break;
                }
            }
            CHECK(match >= 0);
            menu::session().drill = drills::kDrillBReverse; // widest active mode label
            gfx::forgetWidths();
            canvas::clear();
            list::drawPending();
            CHECK(canvas::runs() >= 2 && canvas::rects() >= 2);
            for (int k = 0; k < canvas::runs(); k++)
            {
                const canvas::Run& run = canvas::run(k);
                float ct = runCapTop(run);
                float cb = runCapBottom(run);
                if (run.x < safeLeft - 0.05f || run.right > safeRight + 0.05f || ct < safeTop - 0.05f
                    || cb > safeBottom + 0.05f)
                {
                    misfit("list-pending", "menu/drill_list.cpp", run.text,
                           "CSS pill leaves CRT safe area (%.1f..%.1f, %.1f..%.1f) on %s",
                           run.x, run.right, ct, cb, kAspectNames[a]);
                }
            }
            menu::session().drill = drills::kDrillNone;
        }
        for (int h = 0; h < hintCount; h++)
        {
            CHECK(hintSeen[h]);
        }

        // Also check Y: Quick Setup (and All OSDs sub-view) fits cleanly inside 5% CRT safe area across all 4 controller types.
        static const u32 kOsdCtrlTypes[4] = {gfPadType::GCC, gfPadType::WII_CLASSIC, gfPadType::WIIMOTE,
                                             gfPadType::NUNCHUK};
        for (int ct = 0; ct < 4; ct++)
        {
            const game::PadMap& pmap = game::padMap(kOsdCtrlTypes[ct]);
            menu::osdQuick::open(0, kOsdCtrlTypes[ct]);
            for (int view = 0; view < 5; view++)
            {
                if (view == 1)
                {
                    // Cycle OSD Preset to Custom 1 (Empty) + walk main rows 0..7 to exercise all kMainHelp lines.
                    menu::osdQuick::Input in = {0, 0, 0, 0, kOsdCtrlTypes[ct]};
                    for (int s = 0; s < 4; s++)
                    {
                        in.held = game::kPadDRight;
                        menu::osdQuick::step(0, in);
                        in.held = 0;
                        menu::osdQuick::step(0, in);
                    }
                    // Step down to Frame Meter (row 3) and cycle to "CPU Top", then down to row 7.
                    for (int s = 0; s < 3; s++)
                    {
                        in.held = pmap.down;
                        menu::osdQuick::step(0, in);
                        in.held = 0;
                        menu::osdQuick::step(0, in);
                    }
                    in.held = game::kPadDRight;
                    menu::osdQuick::step(0, in);
                    in.held = 0;
                    menu::osdQuick::step(0, in);
                }
                else if (view == 2)
                {
                    // Navigate down to row 7 (All OSDs) and open sub-view.
                    menu::osdQuick::Input in = {0, 0, 0, 0, kOsdCtrlTypes[ct]};
                    for (int s = 0; s < 4; s++)
                    {
                        in.held = pmap.down;
                        menu::osdQuick::step(0, in);
                        in.held = 0;
                        menu::osdQuick::step(0, in);
                    }
                    in.pressed = pmap.confirm;
                    menu::osdQuick::step(0, in);
                }
                else if (view == 3)
                {
                    // Scroll All OSDs sub-view into middle rows (9..12).
                    menu::osdQuick::Input in = {0, 0, 0, 0, kOsdCtrlTypes[ct]};
                    for (int s = 0; s < 11; s++)
                    {
                        in.held = pmap.down;
                        menu::osdQuick::step(0, in);
                        in.held = 0;
                        menu::osdQuick::step(0, in);
                    }
                }
                else if (view == 4)
                {
                    // Scroll All OSDs sub-view to bottom (Max OSDs).
                    menu::osdQuick::Input in = {0, 0, 0, 0, kOsdCtrlTypes[ct]};
                    for (int s = 0; s < 10; s++)
                    {
                        in.held = pmap.down;
                        menu::osdQuick::step(0, in);
                        in.held = 0;
                        menu::osdQuick::step(0, in);
                    }
                }
                canvas::clear();
                menu::osdQuick::draw();
                CHECK(canvas::runs() >= 8);
                CHECK(runOf(view <= 1 ? "Quick Setup" : "All OSDs") != NULL);
                for (int k = 0; k < canvas::runs(); k++)
                {
                    const canvas::Run& run = canvas::run(k);
                    float kept = run.scaleX / (run.scaleY * xs);
                    if (kept < kMinSqueeze - 0.0005f)
                    {
                        misfit("list-desc", "menu/osd_quick.cpp", run.text, "squeezed to %.0f%% on %s",
                               kept * 100.0f, kAspectNames[a]);
                    }
                    float ctY = runCapTop(run);
                    float cbY = runCapBottom(run);
                    if (run.x < safeLeft - 0.05f || run.right > safeRight + 0.05f || ctY < safeTop - 0.05f
                        || cbY > safeBottom + 0.05f)
                    {
                        misfit("list-desc", "menu/osd_quick.cpp", run.text,
                               "leaves CRT safe area (%.1f..%.1f, %.1f..%.1f) on %s",
                               run.x, run.right, ctY, cbY, kAspectNames[a]);
                    }
                }
            }
            menu::osdQuick::detach();
        }
        for (int i = 0; i < list::kPorts; i++)
        {
            list::s_sys[i].connected = false;
            list::s_sys[i].type = 0;
        }
        list::s_owner = 0;
        list::s_page = 0;
        list::s_row = 0;
    }
    CHECK(s_checked >= 16);
    end(" list-title list-name list-best list-desc list-reason list-pending ");
}

// ---- the recording's bar and the watermark ---------------------------------------

TEST(text_fit, recording_bar_and_watermark)
{
    // record::hudDraw (src/main/record/hud.cpp): the frame on the left of
    // the track and the length (or the mode's tag) on its right, inside the
    // bar; a refusal in a box over it. ui::drawWatermark: the plugin's name
    // and version at the bottom right.
    using namespace ui::style;
    begin();
    for (int a = 0; a < 2; a++)
    {
        gfx::setXScale(kAspects[a]);
        const record::HudInfo infos[4] = {
            {record::kHudRecord, record::kMaxFrames, 0, false, NULL},
            {record::kHudControl, record::kMaxFrames, 0, false, NULL},
            {record::kHudPlayback, record::kMaxFrames, record::kMaxFrames, false, NULL},
            {record::kHudPlayback, record::kMaxFrames, record::kMaxFrames, true, NULL},
        };
        for (int k = 0; k < 4; k++)
        {
            s_checked += a == 0 ? 1 : 0;
            canvas::clear();
            record::hudDraw(infos[k]);
            REQUIRE(canvas::rects() >= 2);
            const canvas::Rect& bar = canvas::rect(0);
            const canvas::Rect& track = canvas::rect(1);
            for (int i = 0; i < canvas::runs(); i++)
            {
                const canvas::Run& r = canvas::run(i);
                float right = r.right;
                bool left = right <= track.x + track.width * 0.5f;
                float from = left ? bar.x : track.x + track.width;
                float to = left ? track.x : bar.x + bar.width;
                float out = (from - r.x > 0.0f ? from - r.x : 0.0f) + (right - to > 0.0f ? right - to : 0.0f);
                if (out > 0.05f)
                {
                    misfit("bar-number", "main/record/hud.cpp", r.text, "%.1f units outside its part of the bar on %s "
                           "(%.1f wide, the part %.1f)",
                           out, kAspectNames[a], right - r.x, to - from);
                }
            }
        }
        for (const Item* item = kItems; item->kind != NULL; item++)
        {
            if (!is(*item, "bar-message"))
            {
                continue;
            }
            s_checked += a == 0 ? 1 : 0;
            record::HudInfo info = {record::kHudHidden, 0, 0, false, item->text};
            gfx::forgetWidths();
            canvas::clear();
            record::hudDraw(info);
            canvas::clear();
            record::hudDraw(info);
            REQUIRE(canvas::rects() == 1);
            const canvas::Rect& box = canvas::rect(0);
            if (box.x < 0.0f || box.x + box.width > kScreen)
            {
                misfit("bar-message", item->where, item->text, "its box is %.1f units wider than the screen on %s",
                       box.width - kScreen, kAspectNames[a]);
            }
        }
    }
    // Watermark recomputes width per frame so switching 16:9 -> 4:3 never clips.
    const float wAspects[2] = {0.75f, 1.0f};
    for (int wa = 0; wa < 2; wa++)
    {
        gfx::setXScale(wAspects[wa]);
        canvas::clear();
        ui::drawWatermark();
        s_checked++;
        REQUIRE(canvas::runs() > 0);
        const canvas::Run& mark = canvas::run(canvas::runs() - 1);
        ui::layout::Box safe = ui::layout::safeArea(wa == 0);
        if (mark.x < safe.x0 - 0.5f || mark.right > safe.x1 + 0.5f)
        {
            misfit("watermark", "main/ui/watermark.cpp", mark.text, "leaves CRT safe area (%.1f..%.1f vs %.1f..%.1f)",
                   mark.x, mark.right, safe.x0, safe.x1);
        }
    }
    // Check v1 -> v2 Settings Update Modal on 4:3 and 16:9 across all 4 controller types & 3 choices.
    {
        host::card::reset();
        bootLab();
        storage::settingsReset();
        storage::settingsMenuClosed();
        for (int f = 0; f < 4; f++)
        {
            storage::onRender(host::fakes::match());
        }
        storage::detach();
        storage::SettingsHead* v1h = reinterpret_cast<storage::SettingsHead*>(host::card::data("settings-a.bin")
                                                                              + sizeof(storage::FileHeader));
        v1h->layout = 1;
        storage::FileHeader* fh = reinterpret_cast<storage::FileHeader*>(host::card::data("settings-a.bin"));
        fh->checksum = storage::checksum(host::card::data("settings-a.bin") + storage::kChecksumFrom,
                                         fh->size - storage::kChecksumFrom);
        bootLab();
        REQUIRE(storage::settingsUpgradePending());
        ui::settingsModalAttach();

        static const u32 kModalCtrls[4] = {gfPadType::GCC, gfPadType::WII_CLASSIC, gfPadType::WIIMOTE,
                                           gfPadType::NUNCHUK};
        ui::SettingsModalInput neutral = {0, 0, 0, 0, static_cast<u32>(gfPadType::GCC)};
        ui::settingsModalStep(neutral);
        for (int a = 0; a < 2; a++)
        {
            gfx::setXScale(kAspects[a]);
            float xs = kAspects[a];
            ui::layout::Box safe = ui::layout::safeArea(a == 1);
            for (int ct = 0; ct < 4; ct++)
            {
                ui::SettingsModalInput setType = {0, 0, 0, 0, kModalCtrls[ct]};
                ui::settingsModalStep(setType);
                canvas::clear();
                ui::settingsModalDraw();
                CHECK(canvas::runs() >= 10);
                for (int k = 0; k < canvas::runs(); k++)
                {
                    const canvas::Run& run = canvas::run(k);
                    float kept = run.scaleX / (run.scaleY * xs);
                    if (kept < kMinSqueeze - 0.0005f)
                    {
                        misfit("watermark", "main/ui/settings_modal.cpp", run.text, "squeezed to %.0f%% on %s",
                               kept * 100.0f, kAspectNames[a]);
                    }
                    float ctY = runCapTop(run);
                    float cbY = runCapBottom(run);
                    if (run.x < safe.x0 - 0.5f || run.right > safe.x1 + 0.5f || ctY < safe.y0 - 0.5f
                        || cbY > safe.y1 + 0.5f)
                    {
                        misfit("watermark", "main/ui/settings_modal.cpp", run.text,
                               "leaves CRT safe area (%.1f..%.1f, %.1f..%.1f) on %s",
                               run.x, run.right, ctY, cbY, kAspectNames[a]);
                    }
                }
            }
        }
        storage::detach();
        host::card::reset();
    }
    end(" bar-number bar-message watermark ");
}

// ---- the recordings browser ----------------------------------------------------------

namespace {
    // Where a run's capital letters are, from top to bottom.
    float capTop(const canvas::Run& r)
    {
        return runCapTop(r);
    }

    float capBottom(const canvas::Run& r)
    {
        return runCapBottom(r);
    }

    // Two different texts drawn so close that they meet: the left one is
    // reported, with its neighbour.
    void checkMeetings(const char* kind, const char* where, int a)
    {
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& p = canvas::run(i);
            // Bold text is drawn twice: the second, right-hand copy counts.
            bool copied = false;
            for (int k = i + 1; k < canvas::runs() && !copied; k++)
            {
                copied = strcmp(canvas::run(k).text, p.text) == 0 && canvas::run(k).y == p.y;
            }
            if (copied)
            {
                continue;
            }
            for (int k = 0; k < canvas::runs(); k++)
            {
                const canvas::Run& q = canvas::run(k);
                if (strcmp(p.text, q.text) == 0 || p.x > q.x || capBottom(p) <= capTop(q) || capBottom(q) <= capTop(p))
                {
                    continue;
                }
                if (p.right + kBetween > q.x)
                {
                    misfit(kind, where, p.text, "%.1f units too wide: it meets \"%s\" on %s",
                           p.right + kBetween - q.x, shown(q.text), kAspectNames[a]);
                }
            }
        }
    }

    // The browser's footer band: bold lines squeezed to the footer's width,
    // each written through an 80-byte line.
    void checkBrowserDescription(const char* where, int a)
    {
        using namespace ui::style;
        float top = textYForCap(kFooterCapTop, scaleForCap(kRowCap)) - 0.5f;
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& r = canvas::run(i);
            if (r.y < top)
            {
                continue;
            }
            float kept = boldSqueeze(r);
            if (kept < kMinSqueeze - 0.0005f)
            {
                float natural = (r.right - r.x) / kept;
                misfit("browser-desc", where, r.text, "squeezed to %.0f%% of its width on %s (%.0f units into %.0f; at "
                       "least %.0f%% is wanted): %.0f units too wide",
                       kept * 100.0f, kAspectNames[a], natural, r.right - r.x, kMinSqueeze * 100.0f,
                       natural - (r.right - r.x) / kMinSqueeze);
            }
            if (strlen(r.text) >= 79)
            {
                misfit("browser-desc", where, r.text, "fills its 80-byte line: the end may be cut");
            }
        }
    }

    void drawBrowser()
    {
        gfx::forgetWidths();
        for (int f = 0; f < 8; f++)
        {
            canvas::clear();
            menu::browser::draw();
        }
        canvas::clear();
        menu::browser::draw();
    }

    // The character and stage whose names are widest (the browser's own
    // names: menu/names.cpp, a number for one it does not know).
    u32 widestKind(const char* (*name)(u32), u32 count)
    {
        u32 best = 0;
        float widest = -1.0f;
        for (u32 k = 0; k < count; k++)
        {
            float width = gfx::textWidth(name(k), 1.0f);
            if (width > widest)
            {
                widest = width;
                best = k;
            }
        }
        return best;
    }
}

TEST(text_fit, recordings_browser)
{
    // menu::browser::draw (src/menu/browser.cpp, included above): a full
    // page of recordings that were read (the widest names: the same
    // character on both sides, the widest stage, the longest length and
    // every slot), a page of files that were not (newer, older, damaged,
    // not read yet), the delete question, the empty folder, and every
    // message it shows in place of the description. Nothing on a line may
    // meet the text beside it (the title and the help lines, a row's name
    // and its date or state), and the description lines are squeezed no
    // further than kMinSqueeze.
    namespace browser = menu::browser;
    begin();
    // A file name as recording_file.cpp's fileName() makes them.
    static const char kFile[] = "991231-235959-99.tmr";
    u32 fighter = widestKind(menu::characterName, 256);
    u32 stage = widestKind(menu::stageName, 0x400);
    for (int a = 0; a < 2; a++)
    {
        gfx::setXScale(kAspects[a]);
        static const u8 kStatuses[5] = {browser::kReady, browser::kNewer, browser::kOlder, browser::kDamaged,
                                        browser::kUnread};
        for (int s = 0; s < 5; s++)
        {
            for (int i = 0; i < browser::kMaxEntries; i++)
            {
                browser::Entry& e = browser::s_entries[i];
                memset(&e, 0, sizeof(e));
                snprintf(e.file, sizeof(e.file), "%s", kFile);
                e.status = kStatuses[s];
                e.info.hmnKind = e.info.cpuKind = static_cast<u8>(fighter);
                e.info.hmnColor = e.info.cpuColor = 19;
                e.info.stage = static_cast<u16>(stage);
                e.info.frames = record::kMaxFrames;
                e.info.hmnSlots = e.info.cpuSlots = 0x3F;
                e.info.year = 2099;
                e.info.month = 12;
                e.info.day = 28;
                e.info.hour = 23;
                e.info.minute = 59;
            }
            browser::s_count = browser::kMaxEntries;
            browser::s_row = browser::kMaxEntries - 1;
            browser::s_phase = browser::kIdle;
            browser::s_confirm = false;
            browser::s_message = NULL;
            drawBrowser();
            s_checked += a == 0 ? 1 : 0;
            checkMeetings("browser-row", "menu/browser.cpp", a);
            checkBrowserDescription("menu/browser.cpp", a);
            // A row's name went through a 48-byte buffer.
            for (int i = 0; i < canvas::runs(); i++)
            {
                float y = canvas::run(i).y;
                if (y > ui::style::kRowTop - 5.0f && y < ui::style::kRowsBandBottom && strlen(canvas::run(i).text) >= 47)
                {
                    misfit("browser-row", "menu/browser.cpp", canvas::run(i).text, "fills its 48-byte buffer: the end "
                           "may be cut");
                }
            }
        }
        // The delete question, deleting, and an empty folder across all 4 controller types.
        static const u32 kCtrlTypes[4] = {gfPadType::GCC, gfPadType::WII_CLASSIC, gfPadType::WIIMOTE,
                                          gfPadType::NUNCHUK};
        for (int ct = 0; ct < 4; ct++)
        {
            browser::s_controllerType = kCtrlTypes[ct];
            browser::s_count = browser::kMaxEntries;
            browser::s_row = browser::kMaxEntries - 1;
            browser::s_phase = browser::kIdle;
            browser::s_confirm = false;
            browser::s_message = NULL;
            drawBrowser();
            checkMeetings("browser-row", "menu/browser.cpp", a);
            browser::s_entries[0].status = browser::kReady;
            browser::s_row = 0;
            browser::s_confirm = true;
            drawBrowser();
            checkMeetings("browser-row", "menu/browser.cpp", a);
            checkBrowserDescription("menu/browser.cpp", a);
            s_checked += a == 0 ? 1 : 0;
        }
        browser::s_controllerType = gfPadType::GCC;
        browser::s_confirm = false;
        browser::s_phase = browser::kDelete;
        drawBrowser();
        checkBrowserDescription("menu/browser.cpp", a);
        browser::s_phase = browser::kIdle;
        browser::s_count = 0;
        drawBrowser();
        checkMeetings("browser-row", "menu/browser.cpp", a);
        checkBrowserDescription("menu/browser.cpp", a);
        s_checked += a == 0 ? 3 : 0;
        // Messages in place of the description.
        browser::s_count = 1;
        for (const Item* item = kItems; item->kind != NULL; item++)
        {
            if (!is(*item, "browser-message"))
            {
                continue;
            }
            s_checked += a == 0 ? 1 : 0;
            browser::s_message = item->text;
            drawBrowser();
            checkBrowserDescription(item->where, a);
        }
        browser::s_message = NULL;
        browser::s_count = 0;
    }
    CHECK(s_checked >= 10);
    end(" browser-row browser-desc ");
}

// ---- notices and other boxes that follow their text -------------------------------

TEST(text_fit, notices)
{
    // storage::notice keeps 63 characters and draws them in a box centred
    // on the screen that is as wide as the text: the text must be whole and
    // the box on the screen.
    begin();
    gfx::setXScale(1.0f);
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        if (!is(*item, "notice"))
        {
            continue;
        }
        s_checked++;
        const char* text = item->text;
        gfx::forgetWidths();
        storage::notice(storage::kNoticeOk, "%s", text);
        canvas::clear();
        storage::noticeDraw(); // learns the width
        canvas::clear();
        storage::noticeDraw();
        REQUIRE(canvas::runs() == 1 && canvas::rects() == 1);
        const canvas::Run& r = canvas::run(0);
        const canvas::Rect& box = canvas::rect(0);
        if (strcmp(r.text, text) != 0)
        {
            misfit("notice", item->where, text, "%d characters, a notice keeps %d: shown as \"%s\"",
                   static_cast<int>(strlen(text)), static_cast<int>(strlen(r.text)), shown(r.text));
        }
        if (box.x < 0.0f || box.x + box.width > kScreen)
        {
            misfit("notice", item->where, text, "its box is %.1f units wider than the screen", box.width - kScreen);
        }
    }
    storage::noticeClear();
    CHECK(s_checked >= 15);
    end(" notice ");
}

TEST(text_fit, places_restated_from_their_drawing_code)
{
    // Places whose drawing code cannot be compiled on the PC: their numbers
    // are in tests/host/text_rules.py, beside the name of the function they
    // were read from.
    //   box      a box as wide as its text (gfx::textBox), centred on x or
    //            starting at x: n = scale, x, centred
    //   fit      text that is not squeezed: n = scale, bold, the width it
    //            has (narrowed on 16:9), a width that is not narrowed
    //   squeeze  text squeezed to a width: the same numbers
    //   buffer   a text longer than the char array it is written to (the
    //            rules write one only for a text that does not fit)
    begin();
    for (const Item* item = kItems; item->kind != NULL; item++)
    {
        const char* text = item->text;
        const float* n = item->n;
        if (is(*item, "buffer"))
        {
            s_checked++;
            misfit("buffer", item->where, text, "%d characters, its buffer holds %d", static_cast<int>(strlen(text)),
                   static_cast<int>(n[0]) - 1);
            continue;
        }
        for (int a = 0; a < 2; a++)
        {
            gfx::setXScale(kAspects[a]);
            float xs = kAspects[a];
            if (is(*item, "box"))
            {
                s_checked += a == 0 ? 1 : 0;
                gfx::forgetWidths();
                canvas::clear();
                gfx::textBox(0, n[1], 20.0f, text, 0xFFFFFFFF, 0x000000C0, n[0], n[2] != 0.0f);
                canvas::clear();
                gfx::textBox(0, n[1], 20.0f, text, 0xFFFFFFFF, 0x000000C0, n[0], n[2] != 0.0f);
                REQUIRE(canvas::rects() == 1);
                const canvas::Rect& box = canvas::rect(0);
                float over = box.x < 0.0f ? -box.x : box.x + box.width - kScreen;
                if (over > 0.0f)
                {
                    misfit("box", item->where, text, "its box leaves the screen by %.1f units on %s (%.1f wide)", over,
                           kAspectNames[a], box.width);
                }
            }
            else if (is(*item, "fit") || is(*item, "squeeze"))
            {
                s_checked += a == 0 ? 1 : 0;
                float width = n[1] != 0.0f ? ui::style::boldWidth(text, n[0]) : gfx::textWidth(text, n[0]);
                float room = n[2] * xs + n[3];
                if (is(*item, "fit") && width > room + 0.01f)
                {
                    misfit("fit", item->where, text, "%.1f units too wide on %s (%.1f of %.1f)", width - room,
                           kAspectNames[a], width, room);
                }
                else if (is(*item, "squeeze") && room / width < kMinSqueeze - 0.0005f)
                {
                    misfit("squeeze", item->where, text, "squeezed to %.0f%% of its width on %s (%.0f units into "
                           "%.0f; at least %.0f%% is wanted): %.0f units too wide",
                           room / width * 100.0f, kAspectNames[a], width, room, kMinSqueeze * 100.0f,
                           width - room / kMinSqueeze);
                }
            }
        }
    }
    CHECK(s_checked >= 60);
    end(" box fit squeeze buffer ");
}

// ---- Multi-HUD piece-against-piece non-overlap checks (4:3 & 16:9) -----------------

TEST(text_fit, multi_hud_non_overlap_43_and_169)
{
    using namespace ui::layout;

    struct HudPiece {
        const char* name;
        Box rect;
    };

    const float kTol = 0.5f;
    int combosChecked = 0;

    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        float xs = xsOf(wide);
        const char* aspect = wide ? "16:9" : "4:3";
        Box safe = safeArea(wide);

        // Drawn bottom-region HUD boxes (Recording Bar, Storage Notice, Watermark).
        gfx::setWidescreen(wide);
        gfx::forgetWidths();
        canvas::clear();
        record::HudInfo barInfo = {record::kHudPlayback, 3599, 3600, false, NULL};
        record::hudDraw(barInfo);
        canvas::Bounds bb = canvas::bounds();
        Box recBar = {bb.x0, bb.y0, bb.x1, bb.y1};

        storage::notice(storage::kNoticeOk, "Lab settings from an older version: new ones at defaults");
        for (int k = 0; k < 2; k++)
        {
            canvas::clear();
            storage::noticeDraw(false);
        }
        storage::noticeClear();
        canvas::Bounds nb = canvas::bounds();
        Box noticeBox = {nb.x0, nb.y0, nb.x1, nb.y1};

        canvas::clear();
        ui::drawWatermark();
        canvas::Bounds wb = canvas::bounds();
        Box watermarkBox = {wb.x0, wb.y0, wb.x1, wb.y1};

        // End-to-end rendered OSD glide check: posting two consecutive P1 OSDs
        // on Sides and HUD keeps every card cleanly above Brawl's P1 portrait
        // top (y = 397) AND keeps slot 0 / slot 1 separated with zero overlap
        // at frame 0, mid-glide (frame 4), and settled (frame 10) on 4:3 & 16:9.
        static const int kSampleAges[3] = {0, 4, 10};
        for (int pos = osd_real::kPositionHud; pos <= osd_real::kPositionSides; pos++)
        {
            osd_real::setPosition(pos);
            osd_real::reset();
            host::fakes::setTrackerFrame(95);
            REQUIRE(osd_real::post(0, osd_real::kIdWavedash, osd_real::kWhite, "Ledge Hang\n4 Frames") != NULL);
            host::fakes::setTrackerFrame(100);
            REQUIRE(osd_real::post(0, osd_real::kIdMiscellaneous, osd_real::kWhite, "GALINT\n+1 Frames") != NULL);
            for (int ai = 0; ai < 3; ai++)
            {
                host::fakes::setTrackerFrame(100 + kSampleAges[ai]);
                canvas::clear();
                osd_real::draw();
                canvas::Bounds ob = canvas::bounds();
                REQUIRE(ob.any);
                CHECK(ob.y1 <= 389.0f + kTol);
                // Check card body rects (height > 10 px): slot 1 bottom must stay above slot 0 top.
                float tops[2] = {0.0f, 0.0f};
                float bots[2] = {0.0f, 0.0f};
                int bodies = 0;
                for (int ri = 0; ri < canvas::rects() && bodies < 2; ri++)
                {
                    const canvas::Rect& r = canvas::rect(ri);
                    if (r.height > 10.0f)
                    {
                        tops[bodies] = r.y;
                        bots[bodies] = r.y + r.height;
                        bodies++;
                    }
                }
                CHECK_EQ(2, bodies);
                CHECK(bots[0] <= tops[1] + kTol);
            }
        }
        osd_real::setPosition(osd_real::kPositionSides);
        osd_real::reset();

        // Sweep: Timer vs Ledgedash (30 & 64) vs Live Skill Drill HUD
        //        x Info (Small 8+8, Med 8+2, Large 8+8)
        //        x Input Viewer (0, 1, 2) x DI Compass (0, 1) x PSA Card (0, 1).
        static const int kTopModes[4] = {0, 30, 64, -1}; // -1 = Live Skill Drill HUD
        struct InfoCfg {
            int size;
            int hmnRows;
            int cpuRows;
        };
        static const InfoCfg kInfos[3] = {
            {main::kInfoSmall, 8, 8},
            {main::kInfoMedium, 8, 2},
            {main::kInfoLarge, 8, 8}
        };

        Box modalBox = drillModalBox(wide);
        CHECK(modalBox.x0 >= safe.x0 - kTol && modalBox.x1 <= safe.x1 + kTol);
        CHECK(modalBox.y0 >= safe.y0 - kTol && modalBox.y1 <= safe.y1 + kTol);

        // Strict 5% CRT safe area (X in [32, 608], Y in [24, 456]) for BOTH 4:3 and 16:9
        // on Combo-Break Rush top HUD and Action Log strip, plus zero overlap with Brawl
        // P1/P2 bottom portraits and Techchase HUD.
        const Box strictSafe = {32.0f, 24.0f, 608.0f, 456.0f};
        const Box rushHud = comboBreakHudBox(wide);
        const Box actLog = actionLogBox(wide);
        CHECK(rushHud.x0 >= strictSafe.x0 - kTol && rushHud.x1 <= strictSafe.x1 + kTol);
        CHECK(rushHud.y0 >= strictSafe.y0 - kTol && rushHud.y1 <= strictSafe.y1 + kTol);
        CHECK(actLog.x0 >= strictSafe.x0 - kTol && actLog.x1 <= strictSafe.x1 + kTol);
        CHECK(actLog.y0 >= strictSafe.y0 - kTol && actLog.y1 <= strictSafe.y1 + kTol);
        const Box brawlP1 = wide ? Box{63.0f, 397.0f, 151.0f, 445.0f} : Box{41.7f, 397.0f, 158.7f, 445.0f};
        const Box brawlP2 = wide ? Box{209.0f, 397.0f, 297.0f, 445.0f} : Box{189.4f, 397.0f, 306.4f, 445.0f};

        // Edge Case 5A: Brawl Training Help: ON right-side sidebar non-overlap sweep
        // across vSlots=0..2 with 3-row Panel B stat plate and Adaptive CPU on 4:3 and 16:9.
        {
            Box sb = trainingSidebarRoom(wide);
            Box timer = timerRoom(wide);
            for (int vSlots = 0; vSlots <= 2; vSlots++)
            {
                HudPiece sbPieces[12];
                int n = 0;
                sbPieces[n++] = {"training-sidebar", sb};
                sbPieces[n++] = {"timer", timer};
                float vBot = 0.0f;
                for (int s = 0; s < vSlots; s++)
                {
                    Box vb = inputViewerCardBox(s, timer, wide, true);
                    if (vb.y1 > vBot) vBot = vb.y1;
                    sbPieces[n++] = {s == 0 ? "viewer-0" : "viewer-1", vb};
                }
                sbPieces[n++] = {"drill-plate", drillStatPlateBox(3, vBot, wide, true)};
                sbPieces[n++] = {"action-log", actLog};
                sbPieces[n++] = {"rec-bar", recBar};
                sbPieces[n++] = {"brawl-p1", brawlP1};
                sbPieces[n++] = {"brawl-p2", brawlP2};
                sbPieces[n++] = {"watermark", watermarkBox};
                for (int p = 0; p < n; p++)
                {
                    const Box& r = sbPieces[p].rect;
                    CHECK(r.x0 >= safe.x0 - kTol && r.y0 >= safe.y0 - kTol && r.x1 <= safe.x1 + kTol
                          && r.y1 <= safe.y1 + kTol);
                    for (int q = p + 1; q < n; q++)
                    {
                        const Box& rb = sbPieces[q].rect;
                        float dx = (r.x1 < rb.x1 ? r.x1 : rb.x1) - (r.x0 > rb.x0 ? r.x0 : rb.x0);
                        float dy = (r.y1 < rb.y1 ? r.y1 : rb.y1) - (r.y0 > rb.y0 ? r.y0 : rb.y0);
                        if (dx > kTol && dy > kTol)
                        {
                            host::test::fail(__FILE__, __LINE__,
                                             "%s (trainingSidebar=true v=%d): %s overlaps %s by %.1f x %.1f",
                                             aspect, vSlots, sbPieces[p].name, sbPieces[q].name, dx, dy);
                        }
                    }
                }
            }
        }

        for (int li = 0; li < 4; li++)
        {
            int ledge = kTopModes[li];
            bool drillHud = (ledge < 0);
            Box band = drillHud ? drillHudBox(wide) : labBand(ledge == 0, ledge, wide);

            for (int ii = 0; ii < 3; ii++)
            {
                const InfoCfg& ic = kInfos[ii];
                float iw = infoWidth(ic.size, wide);
                float iy = infoTop(band, iw, wide);
                Box infoHmn = box(infoLeft(wide), iy, iw, infoHeight(ic.hmnRows, ic.size));
                Box infoCpu = box(infoLeft(wide), infoCpuTop(iy, infoHmn.y1), iw, infoHeight(ic.cpuRows, ic.size));
                float infoBot = infoStackBottom(band, ic.hmnRows, ic.size, ic.cpuRows, ic.size, wide);
                CHECK_NEAR(infoCpu.y1, infoBot, 0.01f);

                for (int vSlots = 0; vSlots <= 2; vSlots++)
                {
                    for (int hasComp = 0; hasComp <= 1; hasComp++)
                    {
                        for (int hasPsa = 0; hasPsa <= 1; hasPsa++)
                        {
                            for (int hasAdap = 0; hasAdap <= 1; hasAdap++)
                            {
                            HudPiece pieces[20];
                            int count = 0;
                            if (drillHud)
                            {
                                pieces[count].name = "drill-hud";
                                pieces[count++].rect = drillHudBox(wide);
                            }
                            else if (ledge == 0)
                            {
                                pieces[count].name = "timer";
                                pieces[count++].rect = timerRoom(wide);
                            }
                            else
                            {
                                pieces[count].name = "ledge-bar";
                                pieces[count++].rect = frameBarBox(ledge, wide);
                                pieces[count].name = "ledge-header";
                                pieces[count++].rect = ledgeHeaderBox(wide);
                            }
                            pieces[count].name = "info-hmn";
                            pieces[count++].rect = infoHmn;
                            pieces[count].name = "info-cpu";
                            pieces[count++].rect = infoCpu;
                            for (int s = 0; s < vSlots; s++)
                            {
                                pieces[count].name = s == 0 ? "viewer-0" : "viewer-1";
                                pieces[count++].rect = inputViewerCardBox(s, band, wide);
                            }
                            if (hasComp)
                            {
                                pieces[count].name = "di-compass";
                                pieces[count++].rect = diCompassBox(band, vSlots, wide);
                            }
                            if (hasPsa)
                            {
                                pieces[count].name = "psa-card";
                                pieces[count++].rect = psaInspectorBox(band, vSlots, hasComp != 0, wide, infoBot);
                            }
                            if (hasAdap)
                            {
                                pieces[count].name = "adaptive-hud";
                                pieces[count++].rect = adaptiveHudBox(band, vSlots, hasComp != 0, hasPsa != 0, wide, infoBot);
                            }
                            pieces[count].name = "action-log";
                            pieces[count++].rect = actLog;
                            pieces[count].name = "brawl-p1-portrait";
                            pieces[count++].rect = brawlP1;
                            pieces[count].name = "brawl-p2-portrait";
                            pieces[count++].rect = brawlP2;
                            pieces[count].name = "rec-bar";
                            pieces[count++].rect = recBar;
                            pieces[count].name = "notice";
                            pieces[count++].rect = noticeBox;
                            pieces[count].name = "watermark";
                            pieces[count++].rect = watermarkBox;

                            // 1. Every piece inside 5% CRT safe area.
                            for (int p = 0; p < count; p++)
                            {
                                const Box& r = pieces[p].rect;
                                if (r.x0 < safe.x0 - kTol || r.y0 < safe.y0 - kTol || r.x1 > safe.x1 + kTol
                                    || r.y1 > safe.y1 + kTol)
                                {
                                    host::test::fail(
                                        __FILE__, __LINE__,
                                        "%s (ledge=%d info=%d v=%d comp=%d psa=%d adap=%d): %s leaves 5%% CRT safe area",
                                        aspect, ledge, ii, vSlots, hasComp, hasPsa, hasAdap, pieces[p].name);
                                }
                            }

                            // 2. Pairwise non-overlap across all active Lab HUD pieces.
                            for (int p = 0; p < count; p++)
                            {
                                for (int q = p + 1; q < count; q++)
                                {
                                    const Box& ra = pieces[p].rect;
                                    const Box& rb = pieces[q].rect;
                                    float dx = (ra.x1 < rb.x1 ? ra.x1 : rb.x1) - (ra.x0 > rb.x0 ? ra.x0 : rb.x0);
                                    float dy = (ra.y1 < rb.y1 ? ra.y1 : rb.y1) - (ra.y0 > rb.y0 ? ra.y0 : rb.y0);
                                    if (dx > kTol && dy > kTol)
                                    {
                                        host::test::fail(
                                            __FILE__, __LINE__,
                                            "%s (ledge=%d info=%d v=%d comp=%d psa=%d adap=%d): %s overlaps %s by %.1f x %.1f",
                                            aspect, ledge, ii, vSlots, hasComp, hasPsa, hasAdap, pieces[p].name,
                                            pieces[q].name, dx, dy);
                                    }
                                }
                            }

                            // 3. Stepped TopLeft and TopRight P1 OSD card clears top band,
                            //    same-side HUD column, PSA card, Adaptive HUD, and Brawl damage row (y 397).
                            float leftBot = infoBot;
                            float rightBot =
                                rightColumnBottom(band, vSlots, hasComp != 0, hasPsa != 0, wide, hasAdap != 0);
                            if (hasAdap)
                            {
                                Box adapBox = adaptiveHudBox(band, vSlots, hasComp != 0, hasPsa != 0, wide, infoBot);
                                if (adapBox.y1 > rightBot)
                                {
                                    rightBot = adapBox.y1;
                                }
                            }
                            if (hasPsa)
                            {
                                Box psaBox = psaInspectorBox(band, vSlots, hasComp != 0, wide, infoBot);
                                if (psaBox.x0 < 320.0f && psaBox.y1 > leftBot)
                                {
                                    leftBot = psaBox.y1;
                                }
                                if (psaBox.x1 > 320.0f && psaBox.y1 > rightBot)
                                {
                                    rightBot = psaBox.y1;
                                }
                            }
                            float topL = osdTopStartY(false, band, leftBot, rightBot);
                            float topR = osdTopStartY(true, band, leftBot, rightBot);
                            osd_real::StackPlace spL = osd_real::stackPlace(osd_real::kPositionTopLeft, 0, xs, 0.0f, topL);
                            osd_real::StackPlace spR = osd_real::stackPlace(osd_real::kPositionTopRight, 0, xs, 0.0f, topR);
                            float hw = osd_real::kBoxWidth * xs * 0.5f;
                            float hh = osd_real::kBoxHeight * 0.5f;
                            Box osdL = {spL.x - hw, spL.y - hh, spL.x + hw, spL.y + hh};
                            Box osdR = {spR.x - hw, spR.y - hh, spR.x + hw, spR.y + hh};

                            CHECK(osdL.x0 >= safe.x0 - kTol && osdL.x1 <= safe.x1 + kTol);
                            CHECK(osdL.y0 >= safe.y0 - kTol && osdL.y1 <= 397.0f + kTol);
                            CHECK(osdR.x0 >= safe.x0 - kTol && osdR.x1 <= safe.x1 + kTol);
                            CHECK(osdR.y0 >= safe.y0 - kTol && osdR.y1 <= 397.0f + kTol);

                            // TopLeft P1 OSD clears band, info panels, viewer, compass, psa, adaptive, watermark.
                            for (int p = 0; p < count; p++)
                            {
                                if (strcmp(pieces[p].name, "rec-bar") == 0 || strcmp(pieces[p].name, "notice") == 0)
                                {
                                    continue;
                                }
                                const Box& r = pieces[p].rect;
                                float dxL = (osdL.x1 < r.x1 ? osdL.x1 : r.x1) - (osdL.x0 > r.x0 ? osdL.x0 : r.x0);
                                float dyL = (osdL.y1 < r.y1 ? osdL.y1 : r.y1) - (osdL.y0 > r.y0 ? osdL.y0 : r.y0);
                                CHECK(!(dxL > kTol && dyL > kTol));

                                float dxR = (osdR.x1 < r.x1 ? osdR.x1 : r.x1) - (osdR.x0 > r.x0 ? osdR.x0 : r.x0);
                                float dyR = (osdR.y1 < r.y1 ? osdR.y1 : r.y1) - (osdR.y0 > r.y0 ? osdR.y0 : r.y0);
                                CHECK(!(dxR > kTol && dyR > kTol));
                            }

                            // 4. Lower OSD stacks (HUD & Sides, 3 typical messages for P1+CPU)
                            //    clear P1's Brawl bottom-HUD portrait zone (y >= 397) at BOTH
                            //    settled and mid-glide (kEnterFrom) frames, and never collide
                            //    with Timer, Ledgedash HUD, Drill HUD, Viewer, Compass, PSA, or Adaptive HUD.
                            if (ii == 1 && (ledge == 0 || drillHud))
                            {
                                for (int pos = osd_real::kPositionHud; pos <= osd_real::kPositionSides; pos++)
                                {
                                    for (int pl = 0; pl < 2; pl++)
                                    {
                                        // Without rec bar: both settled (slot 0) and frame-0 glide-in
                                        // (kEnterFrom) must clear P1's fighter portrait / 0% HUD.
                                        osd_real::StackPlace spBase = osd_real::stackPlace(pos, pl, xs, 0.0f);
                                        float settledBottom = spBase.y + hh;
                                        float glideBottom = spBase.y + osd_real::kEnterFrom * spBase.dy + hh;
                                        CHECK(settledBottom <= 385.0f + kTol);
                                        CHECK(glideBottom <= 389.0f + kTol);

                                        // With rec bar (recordings cannot run inside Skill Drills):
                                        // even frame-0 glide-in stays above rec-bar top (365).
                                        float floor = drillHud ? 0.0f : recBarOsdFloor();
                                        osd_real::StackPlace sp = osd_real::stackPlace(pos, pl, xs, floor);
                                        if (!drillHud)
                                        {
                                            float recGlideBottom = sp.y + osd_real::kEnterFrom * sp.dy + hh;
                                            CHECK(recGlideBottom <= recBar.y0 + kTol);
                                        }

                                        Box stack = none();
                                        // When 10-row Info (8+2) + Panel B PSA Inspector share the Top-Left
                                        // column (y <= 328), 1 newest lower-left OSD card clears cleanly below
                                        // PSA (y >= 330 without recBar, y >= 304 with recBar); when legacy DI
                                        // Compass is stacked below dual Viewers on 4:3 with recBar lifting Sides
                                        // to floor=350 (y0=206), 2 stacked Sides CPU OSDs fit cleanly.
                                        bool psaOnLeft = hasPsa && psaInspectorBox(band, vSlots, hasComp != 0, wide, infoBot).x0 < 320.0f;
                                        int stackSlots = 3;
                                        if (psaOnLeft && (pos == osd_real::kPositionHud || pl == 0))
                                        {
                                            stackSlots = 1;
                                        }
                                        else if (pl == 1 && pos == osd_real::kPositionSides && hasComp
                                                 && (vSlots >= 2 || (hasAdap && vSlots >= 1)))
                                        {
                                            stackSlots = 2;
                                        }
                                        for (int sl = 0; sl < stackSlots; sl++)
                                        {
                                            float sx = sp.x + sl * sp.dx;
                                            float sy = sp.y + sl * sp.dy;
                                            Box one = {sx - hw, sy - hh, sx + hw, sy + hh};
                                            stack = unite(stack, one);
                                        }
                                        for (int p = 0; p < count; p++)
                                        {
                                            if (strcmp(pieces[p].name, "notice") == 0
                                                || (drillHud && strcmp(pieces[p].name, "rec-bar") == 0))
                                            {
                                                continue;
                                            }
                                            const Box& r = pieces[p].rect;
                                            float dx = (stack.x1 < r.x1 ? stack.x1 : r.x1)
                                                       - (stack.x0 > r.x0 ? stack.x0 : r.x0);
                                            float dy = (stack.y1 < r.y1 ? stack.y1 : r.y1)
                                                       - (stack.y0 > r.y0 ? stack.y0 : r.y0);
                                            if (dx > kTol && dy > kTol)
                                            {
                                                host::test::fail(
                                                    __FILE__, __LINE__,
                                                    "%s (ledge=%d info=%d v=%d comp=%d psa=%d adap=%d pos=%d pl=%d): osd(%.1f..%.1f,%.1f..%.1f) overlaps %s(%.1f..%.1f,%.1f..%.1f) by %.1f x %.1f",
                                                    aspect, ledge, ii, vSlots, hasComp, hasPsa, hasAdap, pos, pl,
                                                    stack.x0, stack.x1, stack.y0, stack.y1, pieces[p].name,
                                                    r.x0, r.x1, r.y0, r.y1, dx, dy);
                                            }
                                        }
                                    }
                                }
                            }
                            combosChecked++;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK_EQ(2 * 4 * 3 * 3 * 2 * 2 * 2, combosChecked);
    NOTE("%d multi-HUD 4:3/16:9 combinations verified with 0 overlaps", combosChecked);
}

TEST(text_fit, osd_continuity_same_id_glide_and_exit)
{
    gfx::setXScale(1.0f);
    osd_real::setPosition(osd_real::kPositionSides);

    // 1. Settled single card snaps to integer pixels at rest.
    osd_real::reset();
    host::fakes::setTrackerFrame(100);
    REQUIRE(osd_real::post(0, osd_real::kIdWavedash, osd_real::kWhite, "Wavedash\nFrame 1") != NULL);
    host::fakes::setTrackerFrame(110);
    canvas::clear();
    osd_real::draw();
    float slot0RestTop = -1.0f;
    for (int i = 0; i < canvas::rects(); i++)
    {
        if (canvas::rect(i).height > 10.0f)
        {
            slot0RestTop = canvas::rect(i).y;
            break;
        }
    }
    REQUIRE(slot0RestTop > 0.0f);
    CHECK(slot0RestTop == ui::style::snapPx(slot0RestTop));

    // 2. Smooth 7f easeOutCubic stack glide (no 1-frame 49px teleport) + integer rest snap.
    host::fakes::setTrackerFrame(110);
    REQUIRE(osd_real::post(0, osd_real::kIdLCancel, osd_real::kWhite, "Second\nCard") != NULL);
    canvas::clear();
    osd_real::draw();
    float slot1Age0Top = -1.0f;
    for (int i = 0; i < canvas::rects(); i++)
    {
        if (canvas::rect(i).height > 10.0f)
        {
            slot1Age0Top = canvas::rect(i).y;
            break;
        }
    }
    host::fakes::setTrackerFrame(116);
    canvas::clear();
    osd_real::draw();
    float slot1RestTop = -1.0f;
    for (int i = 0; i < canvas::rects(); i++)
    {
        if (canvas::rect(i).height > 10.0f)
        {
            slot1RestTop = canvas::rect(i).y;
            break;
        }
    }
    // At frame 0 of push, slot 1 is mid-glide (between slot 0 and settled slot 1).
    CHECK(slot1Age0Top > slot1RestTop + 10.0f);
    CHECK(slot1Age0Top < slot0RestTop - 10.0f);
    CHECK(slot1RestTop == ui::style::snapPx(slot1RestTop));

    // 3. Pure append-only FIFO: posting the same id again appends a new card at slot 0,
    //    pushing older cards smoothly upward without overwriting or jumping slots.
    host::fakes::setTrackerFrame(118);
    REQUIRE(osd_real::post(0, osd_real::kIdWavedash, osd_real::kWhite, "Third\nCard") != NULL);
    host::fakes::setTrackerFrame(125);
    canvas::clear();
    osd_real::draw();
    int fullBodies = 0;
    for (int i = 0; i < canvas::rects(); i++)
    {
        if (canvas::rect(i).height > 40.0f)
        {
            fullBodies++;
        }
    }
    CHECK_EQ(3, fullBodies);
    CHECK(runOf("Wavedash") != NULL);
    CHECK(runOf("Second") != NULL);
    CHECK(runOf("Third") != NULL);

    // 4. 4f alpha + gentle slide exit renders across all 4 frames (f=0..3) with monotonically
    //    decreasing alpha, preserves scaleY, and clears cleanly on f=4.
    osd_real::reset();
    host::fakes::setTrackerFrame(200);
    REQUIRE(osd_real::post(0, osd_real::kIdWavedash, osd_real::kWhite, "Exit\nCheck") != NULL);
    float expectedSy = ui::style::scaleForCap(9.0f);
    u32 prevAlpha = 0x100u;
    for (int f = 0; f < 4; f++)
    {
        host::fakes::setTrackerFrame(200 + osd_real::kLifetime + f);
        canvas::clear();
        osd_real::draw();
        int exitFg = 0;
        u32 frameAlpha = 0u;
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& r = canvas::run(i);
            if ((r.color & 0xFFFFFF00u) != 0u)
            {
                CHECK(fabsf(r.scaleY - expectedSy) <= 0.001f);
                u32 a = r.color & 0xFFu;
                CHECK(a > 0u && a < prevAlpha);
                frameAlpha = a;
                exitFg++;
            }
        }
        CHECK_EQ(2, exitFg);
        prevAlpha = frameAlpha;
    }
    host::fakes::setTrackerFrame(200 + osd_real::kLifetime + 4);
    CHECK(!osd_real::visible());
    osd_real::reset();
}
