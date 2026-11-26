// Do the HUD pieces that are on screen together stay clear of each other and
// of Brawl's own HUD, on 4:3 and on 16:9?
//
// Each piece's box is taken from the mod's own code: drawn on the canvas
// (host/canvas.h: the box around all it drew) where the drawing code is in
// the PC build (the recording bar, the watermark, the pause hint), else from
// the places the drawing code itself reads (src/main/ui/hud_layout.h,
// osd_place.h). Where the info display goes is worked out as the game does it:
// the lab's band follows its settings (layout::labBand), and the same placement
// functions as the drawing code's put the pieces aside.
//
// Which pieces are on screen together follows the code that shows them
// (src/main/lab.cpp onRender): the Ledgedash HUD's bar and the match timer
// take turns; the info display and the messages are hidden in Training's pause.
// Each combination is checked twice: with the sizes a player is likely to use
// ("typical") and with every size at its largest ("most": Large info panels
// with all eight rows, six messages per player, the longest bars).
//
// Brawl's HUD pieces are boxes measured on our screenshots (Mario against
// Fox, Final Destination), not read from the game: the game picks its HUD's
// scene animation by the screen setting (0x800DC264-0x800DC2A0, read in
// code), but where that puts each piece has not been traced to its data.
//
// Overlaps known today are listed in kKnown with the reason; a listed
// overlap does not fail the run, an unlisted one does, and a listed one that
// is gone fails until its line is taken out. `make check-host
// HOST_ARGS="--only hud_layout --log"` prints every overlap with its size.
#include <string.h>

#include "host/canvas.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/game/techniques.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/record/hud.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/ui/hud_layout.h"
#include "main/ui/osd_place.h"
#include "main/ui/style.h"
#include "main/ui/watermark.h"

namespace {
    static tm::game::Ledgedash s_testLedgedash;
}

namespace tm {
namespace game {
    const Ledgedash& lastLedgedash()
    {
        return s_testLedgedash;
    }
}
}

#define ledgedashHudVisible hudLayoutLedgedashVisible
#define ledgedashCells hudLayoutLedgedashCells
#define drawLedgedashHud hudLayoutDrawLedgedashHud
#include "main/ui/ledgedash_hud.cpp"
#undef drawLedgedashHud
#undef ledgedashCells
#undef ledgedashHudVisible

using namespace tm;
using ui::layout::Box;
namespace canvas = host::canvas;
namespace layout = tm::ui::layout;

namespace {
    // ---- the boxes ---------------------------------------------------------

    Box drawnBox()
    {
        canvas::Bounds b = canvas::bounds();
        Box out = {b.x0, b.y0, b.x1, b.y1};
        if (!b.any)
        {
            out.x0 = out.y0 = out.x1 = out.y1 = 0.0f;
        }
        return out;
    }

    // `count` messages of player `player`'s stack (osd.cpp drawMessage).
    Box osdStack(int position, int player, int count, bool wide, float floorY = 0.0f, int playerCount = 2)
    {
        float xs = wide ? 0.75f : 1.0f;
        osd::StackPlace at = osd::stackPlace(position, player, xs, floorY, 0.0f, playerCount);
        float w = osd::kBoxWidth * xs;
        Box all = layout::none();
        for (int slot = 0; slot < count; slot++)
        {
            float cx = at.x + slot * at.dx;
            float cy = at.y + slot * at.dy;
            Box one = {cx - w * 0.5f, cy - osd::kBoxHeight * 0.5f, cx + w * 0.5f, cy + osd::kBoxHeight * 0.5f};
            all = layout::unite(all, one);
        }
        return all;
    }

    // ---- Brawl's own pieces, measured --------------------------------------
    //
    // On our screenshots of each screen setting: the match timer at "00:06
    // 08" (4:3: x 267-375, y 44-61) and Brawl's top-left counter region (4:3:
    // x 36-126, y 52-73); the damage boxes of a four-player Training match
    // (portrait to "%"); Training's pause menu (its left panel and help bar).
    Box brawlTimer(bool wide)
    {
        Box b16 = {281.0f, 44.0f, 361.0f, 61.0f};
        Box b43 = {267.0f, 44.0f, 375.0f, 61.0f};
        return wide ? b16 : b43;
    }

    Box brawlCounter(bool wide)
    {
        Box b16 = {110.0f, 50.0f, 179.0f, 73.0f};
        Box b43 = {36.0f, 52.0f, 126.0f, 73.0f};
        return wide ? b16 : b43;
    }

    Box brawlDamage(int player, bool wide)
    {
        static const float kLeft16[4] = {63.0f, 209.0f, 351.0f, 493.0f};
        static const float kLeft43[4] = {41.7f, 189.4f, 336.4f, 483.0f};
        float left = wide ? kLeft16[player] : kLeft43[player];
        float width = wide ? 88.0f : 117.0f;
        Box b = {left, 397.0f, left + width, 445.0f};
        return b;
    }

    Box brawlPauseMenu(bool wide)
    {
        Box b16 = {62.0f, 0.0f, 236.0f, 480.0f};
        Box b43 = {0.0f, 0.0f, 209.0f, 480.0f};
        return wide ? b16 : b43;
    }

    Box brawlPauseHelp(bool wide)
    {
        Box b16 = {185.0f, 352.0f, 640.0f, 452.0f};
        Box b43 = {205.0f, 352.0f, 640.0f, 452.0f};
        return wide ? b16 : b43;
    }

    // ---- what is shown together --------------------------------------------

    enum Kind {
        kPlain,
        kBand,  // in the top band: inside layout::topBand()
        kMenu,  // parts of the open lab menu; never compared with each other
        kBrawl  // the game's own; two of them are never compared
    };

    struct Piece {
        const char* name;
        Box box;
        int kind;
    };

    struct Envelope {
        const char* name;
        int infoSize;    // 0 Small, 1 Medium, 2 Large
        int humanRows;
        int cpuRows;
        int messages;    // per player's stack
        int ledgeCells;  // the Ledgedash HUD's bar (30 to 64)
    };
    const Envelope kEnvelopes[2] = {{"typical", 1, 8, 2, 3, 30}, {"most", 2, 8, 8, 6, 64}};

    enum { kMaxPieces = 32 };
    struct Scene {
        Piece pieces[kMaxPieces];
        int count;

        void add(const char* name, const Box& b, int kind = kPlain)
        {
            if (count < kMaxPieces && b.x1 > b.x0)
            {
                Piece& p = pieces[count++];
                p.name = name;
                p.box = b;
                p.kind = kind;
            }
        }
    };

    // ---- the combinations ---------------------------------------------------

    enum SceneId {
        kLabRunning,  // the free lab: timer on
        kLabLedgeHud, // the Ledgedash HUD on
        kLabFour,     // three CPUs: four damage boxes
        kLabPaused,   // Training's own pause menu
        kRecording,   // the lab with a recording playing
        kLabMenuOpen, // the lab menu open with a storage notice
        kSceneCount
    };
    const char* const kSceneNames[kSceneCount] = {
        "lab", "lab-ledgedash-hud", "lab-4p", "lab-paused", "lab-recording", "lab-menu"};

    bool isMenuOpen(int id)
    {
        return id == kLabMenuOpen;
    }

    // The open lab menu (ui/menu.cpp menuDraw, ui/style.h): outer frame,
    // rows pane, and scrollbar.
    void addOpenMenu(Scene& s)
    {
        using namespace ui::style;
        canvas::clear();
        float left = cx(kLabLeft);
        float width = w(kLabRight - kLabLeft);
        gfx::fillRect(left, kLabTop, width, kLabBottom - kLabTop, kLabBaseBg);
        s.add("menu-panel", drawnBox(), kMenu);

        canvas::clear();
        gfx::fillRect(cx(kRailPaneLeft), kRailBodyTop, w(kLabInnerRight - kRailPaneLeft),
                      kLabDescTop - kRailBodyTop, kLabBaseBg);
        s.add("menu-rows", drawnBox(), kMenu);

        canvas::clear();
        gfx::fillRect(cx(kLabScrollLeft), kRailRowTop, w(kLabScrollW), kRailScrollH, kLabDivider);
        s.add("menu-scroll", drawnBox(), kMenu);
    }

    void seedLedgedash(int cells, bool panel, bool noXlu = false)
    {
        main::loadSettings(host::fakes::block());
        main::GeneralSettings& g = main::settings().general;
        g.ledgeHud = 1;
        g.ledgeMaxFrames = static_cast<u8>(cells);
        g.ledgeGalint = 1;
        g.ledgePanel = panel ? 1 : 0;
        memset(&s_testLedgedash, 0, sizeof(s_testLedgedash));
        s_testLedgedash.done = 100;
        s_testLedgedash.count = static_cast<u8>(cells);
        s_testLedgedash.galint = noXlu ? 0 : -20;
        s_testLedgedash.ledgeFrames = 99;
        s_testLedgedash.noXlu = noXlu ? 1 : 0;
        s_testLedgedash.hasAngle = 1;
        s_testLedgedash.angle = 45.5f;
        for (int i = 0; i < cells && i < game::kLedgeCells; i++)
        {
            s_testLedgedash.cells[i] = static_cast<u8>(i % game::kCellTypes);
        }
        host::fakes::setTrackerFrame(100);
    }

    Box drawnLedgedashBar(int cells)
    {
        seedLedgedash(cells, false);
        canvas::clear();
        ui::hudLayoutDrawLedgedashHud();
        return drawnBox();
    }

    Box drawnLedgedashHeader(int cells, bool noXlu = false)
    {
        seedLedgedash(cells, true, noXlu);
        canvas::clear();
        ui::hudLayoutDrawLedgedashHud();
        // The last rect and text run are the summary strip drawn after the bar.
        Box out = layout::none();
        if (canvas::rects() > 0)
        {
            const canvas::Rect& r = canvas::rect(canvas::rects() - 1);
            Box rb = {r.x, r.y, r.x + r.width, r.y + r.height};
            out = layout::unite(out, rb);
        }
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& run = canvas::run(i);
            Box tb = {run.x, run.y, run.right, run.y + 24.0f * run.scaleY};
            out = layout::unite(out, tb);
        }
        return out;
    }

    void build(Scene& s, int id, const Envelope& e, int position, bool wide)
    {
        s.count = 0;
        gfx::setWidescreen(wide);
        gfx::forgetWidths();
        layout::setLabBand(layout::none());
        if (isMenuOpen(id))
        {
            addOpenMenu(s);
            gfx::forgetWidths();
            storage::notice(storage::kNoticeOk, "Exported 991231-235959-99.tmr - load from Training Menu (X)");
            for (int k = 0; k < 2; k++)
            {
                canvas::clear();
                storage::noticeDraw(true);
            }
            storage::noticeClear();
            s.add("notice", drawnBox());
            return;
        }
        {
            int ledge = id == kLabLedgeHud ? e.ledgeCells : 0;
            layout::setLabBand(layout::labBand(true, ledge, wide));
            if (id == kLabLedgeHud)
            {
                s.add("ledge-bar", drawnLedgedashBar(e.ledgeCells), kBand);
                s.add("ledge-key", drawnLedgedashHeader(e.ledgeCells), kBand);
            }
            if (id != kLabLedgeHud && id != kLabPaused)
            {
                s.add("brawl-timer", brawlTimer(wide), kBrawl);
            }
        }
        // The info display (info_display.cpp drawInfo); hidden in Training's
        // pause menu (lab.cpp onRender).
        float width = layout::infoWidth(e.infoSize, wide);
        float top = layout::infoTop(layout::topBand(), width, wide);
        Box human = layout::box(layout::infoLeft(wide), top, width, layout::infoHeight(e.humanRows, e.infoSize));
        if (id != kLabPaused)
        {
            s.add("info-hmn", human);
            s.add("info-cpu", layout::box(layout::infoLeft(wide), layout::infoCpuTop(top, human.y1), width,
                                          layout::infoHeight(e.cpuRows, e.infoSize)));
        }
        if (id == kLabPaused)
        {
            // Recording HUD/refusals (!match.paused, module.cpp:112) and
            // storage notices (!match.paused || lab::menuOpen(), storage.cpp)
            // are hidden over Brawl's Training pause menu.
            canvas::clear();
            const char* pHint = "ZL/ZR: Lab Menu";
            float hx = layout::kHintRight - gfx::textWidth(pHint, 0.5f);
            for (int k = 0; k < 2; k++)
            {
                gfx::textBox(0, hx, layout::kHintY, pHint, 0xFFFFFFFF, 0x000000C0, 0.5f, false);
            }
            s.add("pause-hint", drawnBox());
            canvas::clear();
            ui::drawWatermark();
            s.add("watermark", drawnBox());
            s.add("brawl-pause-menu", brawlPauseMenu(wide), kBrawl);
            s.add("brawl-pause-help", brawlPauseHelp(wide), kBrawl);
            return;
        }
        int players = id == kLabFour ? 4 : 2;
        static const char* const kStacks[osd::kPositionCount][4] = {
            {"osd-hud-p1", "osd-hud-p2", "osd-hud-p3", "osd-hud-p4"},
            {"osd-sides-p1", "osd-sides-p2", "osd-sides-p3", "osd-sides-p4"},
            {"osd-topleft-p1", "osd-topleft-p2", "osd-topleft-p3", "osd-topleft-p4"},
            {"osd-topright-p1", "osd-topright-p2", "osd-topright-p3", "osd-topright-p4"}};
        static const char* const kDamage[4] = {"brawl-damage-p1", "brawl-damage-p2", "brawl-damage-p3",
                                               "brawl-damage-p4"};
        float osdFloor = id == kRecording ? layout::recBarOsdFloor() : 0.0f;
        for (int p = 0; p < players; p++)
        {
            s.add(kStacks[position][p], osdStack(position, p, e.messages, wide, osdFloor, players));
            s.add(kDamage[p], brawlDamage(p, wide), kBrawl);
        }
        canvas::clear();
        ui::drawWatermark();
        s.add("watermark", drawnBox());
        if (id == kLabRunning)
        {
            // A storage notice (storage/notice.cpp): shown for 240 frames
            // after a settings file is read or saved; the longest one.
            storage::notice(storage::kNoticeOk, "Lab settings from an older version: new ones at defaults");
            for (int k = 0; k < 2; k++)
            {
                canvas::clear();
                storage::noticeDraw(false);
            }
            storage::noticeClear();
            s.add("notice", drawnBox());
        }
        if (id == kRecording)
        {
            canvas::clear();
            record::HudInfo info = {record::kHudPlayback, 3599, 3600, false, NULL};
            record::hudDraw(info);
            s.add("rec-bar", drawnBox());
            // A refusal, drawn above the bar for a moment: the longest one
            // (record.cpp message()).
            info.mode = record::kHudHidden;
            info.message = "Recording stopped: the state is from other positions.";
            for (int k = 0; k < 2; k++)
            {
                canvas::clear();
                record::hudDraw(info);
            }
            s.add("rec-message", drawnBox());
        }
    }

    // ---- overlaps known today ----------------------------------------------
    //
    // {scene, screen, sizes, piece, piece, why}. Pieces in the order the
    // scene adds them; "*" in the scene, screen or sizes matches any.
    struct Known {
        const char* scene;
        const char* screen;
        const char* sizes;
        const char* a;
        const char* b;
        const char* why;
    };
    const Known kKnown[] = {
        {"*", "*", "*", "brawl-timer", "osd-topleft-p1", "top spots"},
        {"*", "*", "*", "brawl-timer", "osd-topright-p1", "top spots"},
        {"lab-4p", "4:3", "typical", "info-cpu", "osd-topleft-p3", "top spots"},
        {"lab-4p", "16:9", "*", "info-cpu", "osd-topleft-p3", "top spots"},
        {"lab-4p", "*", "most", "info-cpu", "osd-topleft-p4", "top spots"},
        {"lab-4p", "4:3", "typical", "info-cpu", "osd-topright-p3", "top spots"},
        {"lab-4p", "16:9", "most", "info-cpu", "osd-topright-p3", "top spots"},
        {"lab-4p", "*", "most", "info-cpu", "osd-topright-p4", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topleft-p1", "top spots"},
        {"*", "16:9", "typical", "info-hmn", "osd-topleft-p1", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topleft-p1", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topleft-p2", "top spots"},
        {"*", "16:9", "typical", "info-hmn", "osd-topleft-p2", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topleft-p2", "top spots"},
        {"lab-4p", "*", "most", "info-hmn", "osd-topleft-p3", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topright-p1", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topright-p1", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topright-p2", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topright-p2", "top spots"},
        {"lab-4p", "*", "most", "info-hmn", "osd-topright-p3", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-bar", "osd-topleft-p2", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-bar", "osd-topright-p2", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-key", "osd-topleft-p1", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-key", "osd-topright-p1", "top spots"},
        {"*", "*", "most", "info-cpu", "osd-hud-p1", "six messages"},
        {"*", "*", "most", "info-cpu", "osd-hud-p2", "six messages"},
        {"*", "*", "most", "info-cpu", "osd-sides-p1", "six messages"},
        {"lab-4p", "*", "most", "info-cpu", "osd-sides-p2", "six messages"},
        {"*", "*", "most", "info-hmn", "osd-hud-p1", "six messages"},
        {"*", "*", "most", "info-hmn", "osd-hud-p2", "six messages"},
        {"*", "*", "most", "info-hmn", "osd-sides-p1", "six messages"},
        {"lab-4p", "*", "most", "info-hmn", "osd-sides-p2", "six messages"},
        {"lab-paused", "*", "*", "watermark", "brawl-pause-help", "the pause"},
        {"lab", "*", "*", "osd-hud-p2", "notice", "transient notice"},
        {"lab-recording", "*", "*", "osd-hud-p2", "rec-message", "transient refusal"},
        {NULL, NULL, NULL, NULL, NULL, NULL}
    };
    enum { kKnownCount = sizeof(kKnown) / sizeof(kKnown[0]) };
    bool s_knownHit[kKnownCount];

    bool matches(const char* pattern, const char* value)
    {
        return strcmp(pattern, "*") == 0 || strcmp(pattern, value) == 0;
    }

    // Pieces closer than this are taken as touching, not overlapping; Brawl's
    // pieces, measured on screenshots, within kBrawlSlack.
    const float kSlack = 0.5f;
    const float kBrawlSlack = 2.0f;

    bool inside(const Box& a, const Box& room)
    {
        return a.x0 >= room.x0 - kSlack && a.y0 >= room.y0 - kSlack && a.x1 <= room.x1 + kSlack
               && a.y1 <= room.y1 + kSlack;
    }

    // `osdOnly`: only pairs with a message stack (the other pairs were
    // checked with the first position).
    void checkScene(int id, const Envelope& e, int position, bool wide, bool osdOnly, int& overlaps)
    {
        Scene s;
        build(s, id, e, position, wide);
        const char* screen = wide ? "16:9" : "4:3";
        for (int i = 0; i < s.count; i++)
        {
            for (int j = i + 1; j < s.count; j++)
            {
                const Piece& a = s.pieces[i];
                const Piece& b = s.pieces[j];
                bool stack = strncmp(a.name, "osd-", 4) == 0 || strncmp(b.name, "osd-", 4) == 0;
                if ((a.kind == kBrawl && b.kind == kBrawl) || (a.kind == kMenu && b.kind == kMenu)
                    || (osdOnly && !stack))
                {
                    continue;
                }
                float dx = (a.box.x1 < b.box.x1 ? a.box.x1 : b.box.x1) - (a.box.x0 > b.box.x0 ? a.box.x0 : b.box.x0);
                float dy = (a.box.y1 < b.box.y1 ? a.box.y1 : b.box.y1) - (a.box.y0 > b.box.y0 ? a.box.y0 : b.box.y0);
                float slack = a.kind == kBrawl || b.kind == kBrawl ? kBrawlSlack : kSlack;
                if (dx <= slack || dy <= slack)
                {
                    continue;
                }
                overlaps++;
                bool known = false;
                for (int k = 0; kKnown[k].scene != NULL; k++)
                {
                    const Known& n = kKnown[k];
                    if (matches(n.scene, kSceneNames[id]) && matches(n.screen, screen) && matches(n.sizes, e.name)
                        && strcmp(n.a, a.name) == 0 && strcmp(n.b, b.name) == 0)
                    {
                        known = true;
                        s_knownHit[k] = true;
                    }
                }
                if (host::log::g_echo)
                {
                    printf("     | %s {\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"}, "
                           "// %.1f x %.1f; %s (%.0f,%.0f)-(%.0f,%.0f), %s (%.0f,%.0f)-(%.0f,%.0f)\n",
                           known ? "known" : "new  ", kSceneNames[id], screen, e.name, a.name, b.name, dx, dy, a.name,
                           a.box.x0, a.box.y0, a.box.x1, a.box.y1, b.name, b.box.x0, b.box.y0, b.box.x1, b.box.y1);
                }
                if (!known)
                {
                    host::test::fail(__FILE__, __LINE__, "%s, %s, %s sizes: %s and %s overlap by %.1f x %.1f",
                                     kSceneNames[id], screen, e.name, a.name, b.name, dx, dy);
                }
            }
        }
    }
}

TEST(hud_layout, the_ledgedash_hud_is_where_the_layout_says)
{
    static const int kCellCounts[2] = {30, 64};
    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        gfx::setWidescreen(wide);
        gfx::forgetWidths();
        Box safe = layout::safeArea(wide);
        for (int ci = 0; ci < 2; ci++)
        {
            int cells = kCellCounts[ci];
            layout::setLabBand(layout::labBand(true, cells, wide));
            Box barDrawn = drawnLedgedashBar(cells);
            Box barBox = layout::frameBarBox(cells, wide);
            CHECK_NEAR(barBox.x0, barDrawn.x0, 0.01);
            CHECK_NEAR(barBox.x1, barDrawn.x1, 0.01);
            CHECK_NEAR(barBox.y0, barDrawn.y0, 0.01);
            CHECK_NEAR(barBox.y1, barDrawn.y1, 0.01);

            for (int noXlu = 0; noXlu <= 1; noXlu++)
            {
                Box hdrDrawn = drawnLedgedashHeader(cells, noXlu != 0);
                Box hdrBox = layout::ledgeHeaderBox(wide);
                CHECK(inside(hdrDrawn, hdrBox));
                CHECK(inside(hdrDrawn, layout::topBand()));
                CHECK(inside(hdrDrawn, safe));

                seedLedgedash(cells, true, noXlu != 0);
                canvas::clear();
                ui::hudLayoutDrawLedgedashHud();
                Box full = drawnBox();
                CHECK(inside(full, layout::topBand()));
                CHECK(inside(full, safe));
            }
        }
    }
}

TEST(hud_layout, every_piece_is_inside_the_room_kept_for_it)
{
    // A piece in the top band inside the band, a panel inside the panel's
    // room, Brawl's timer and counter inside the room kept for them.
    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        CHECK(inside(brawlTimer(wide), layout::timerRoom(wide)));
        CHECK(inside(brawlCounter(wide), layout::counterRoom(wide)));
        for (int en = 0; en < 2; en++)
        {
            for (int id = 0; id < kSceneCount; id++)
            {
                Scene s;
                build(s, id, kEnvelopes[en], osd::kPositionSides, wide);
                for (int i = 0; i < s.count; i++)
                {
                    const Piece& p = s.pieces[i];
                    bool ok = p.kind == kBand ? inside(p.box, layout::topBand()) : true;
                    if (!ok)
                    {
                        host::test::fail(__FILE__, __LINE__, "%s, %s, %s sizes: %s (%.0f,%.0f)-(%.0f,%.0f) is not "
                                         "inside the room kept for it",
                                         kSceneNames[id], wide ? "16:9" : "4:3", kEnvelopes[en].name, p.name,
                                         p.box.x0, p.box.y0, p.box.x1, p.box.y1);
                    }
                }
            }
        }
    }
}

TEST(hud_layout, every_piece_stays_on_screen)
{
    for (int a = 0; a < 2; a++)
    {
        for (int en = 0; en < 2; en++)
        {
            for (int id = 0; id < kSceneCount; id++)
            {
                int posCount = (id == kLabPaused || isMenuOpen(id)) ? 1 : osd::kPositionCount;
                for (int p = 0; p < posCount; p++)
                {
                    Scene s;
                    build(s, id, kEnvelopes[en], p, a == 1);
                    for (int i = 0; i < s.count; i++)
                    {
                        // Full-depth message stacks (osd::kPerPlayer) can exceed
                        // the screen bounds in extreme "most" combinations.
                        if (en == 1 && strncmp(s.pieces[i].name, "osd-", 4) == 0)
                        {
                            continue;
                        }
                        const Box& b = s.pieces[i].box;
                        if (b.x0 < -kSlack || b.y0 < -kSlack || b.x1 > 640.0f + kSlack || b.y1 > 480.0f + kSlack)
                        {
                            host::test::fail(__FILE__, __LINE__,
                                             "%s, %s, %s sizes: %s leaves the screen: (%.0f,%.0f)-(%.0f,%.0f)",
                                             kSceneNames[id], a == 1 ? "16:9" : "4:3", kEnvelopes[en].name,
                                             s.pieces[i].name, b.x0, b.y0, b.x1, b.y1);
                        }
                    }
                }
            }
        }
    }
}

TEST(hud_layout, pieces_shown_together_do_not_overlap)
{
    memset(s_knownHit, 0, sizeof(s_knownHit));
    int overlaps = 0;
    int combinations = 0;
    for (int a = 0; a < 2; a++)
    {
        for (int en = 0; en < 2; en++)
        {
            for (int id = 0; id < kSceneCount; id++)
            {
                // The message stacks in each OSD Position; the scene without
                // messages once.
                int positions = (id == kLabPaused || isMenuOpen(id)) ? 1 : osd::kPositionCount;
                for (int pos = 0; pos < positions; pos++)
                {
                    checkScene(id, kEnvelopes[en], pos, a == 1, pos > 0, overlaps);
                    combinations++;
                }
            }
        }
    }
    for (int k = 0; kKnown[k].scene != NULL; k++)
    {
        if (!s_knownHit[k])
        {
            host::test::fail(__FILE__, __LINE__,
                             "%s, %s, %s sizes: %s and %s are listed in kKnown as overlapping and no longer do: "
                             "take the line out",
                             kKnown[k].scene, kKnown[k].screen, kKnown[k].sizes, kKnown[k].a, kKnown[k].b);
        }
    }
    NOTE("%d overlaps across %d combinations", overlaps, combinations);
}

TEST(hud_layout, brawl_hud_respects_crt_safe_area)
{
    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        Box safe = layout::safeArea(wide);
        CHECK(inside(brawlTimer(wide), safe));
        CHECK(inside(brawlCounter(wide), safe));
        for (int p = 0; p < 4; p++)
        {
            CHECK(inside(brawlDamage(p, wide), safe));
        }
    }
}

TEST(hud_layout, every_piece_stays_in_crt_safe_area)
{
    // Every piece the mod draws across every modelled scene lies inside the
    // 5% CRT overscan safe area (layout::safeArea: 4:3 x 32..608, y 24..456;
    // 16:9 same vertical margin y 24..456). Only the full-depth OSD stack
    // (already accepted in every_piece_stays_on_screen) is skipped.
    //
    // Remaining pieces still outside the safe area are tracked in
    // kOutsideSafeAreaToFix with their current worst overhang on each edge.
    // Any unlisted piece leaving the safe area fails, any listed piece whose
    // overhang grows fails, and any listed entry that is now inside fails.
    struct SafeAreaTodo {
        const char* piece; // exact name, or prefix ending in '*'
        const char* screen;
        float left;
        float top;
        float right;
        float bottom;
    };
    static const SafeAreaTodo kOutsideSafeAreaToFix[] = {
        {NULL, NULL, 0.0f, 0.0f, 0.0f, 0.0f}
    };
    enum { kTodoCount = sizeof(kOutsideSafeAreaToFix) / sizeof(kOutsideSafeAreaToFix[0]) };
    bool todoHit[kTodoCount];
    memset(todoHit, 0, sizeof(todoHit));

    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        const char* screen = wide ? "16:9" : "4:3";
        Box safe = layout::safeArea(wide);
        for (int en = 0; en < 2; en++)
        {
            for (int id = 0; id < kSceneCount; id++)
            {
                int posCount = (id == kLabPaused || isMenuOpen(id)) ? 1 : osd::kPositionCount;
                for (int p = 0; p < posCount; p++)
                {
                    Scene s;
                    build(s, id, kEnvelopes[en], p, wide);
                    for (int i = 0; i < s.count; i++)
                    {
                        if (s.pieces[i].kind == kBrawl)
                        {
                            continue;
                        }
                        if (en == 1 && strncmp(s.pieces[i].name, "osd-", 4) == 0)
                        {
                            continue;
                        }
                        const Box& b = s.pieces[i].box;
                        if (inside(b, safe))
                        {
                            continue;
                        }
                        float lo = safe.x0 - b.x0 > 0.0f ? safe.x0 - b.x0 : 0.0f;
                        float to = safe.y0 - b.y0 > 0.0f ? safe.y0 - b.y0 : 0.0f;
                        float ro = b.x1 - safe.x1 > 0.0f ? b.x1 - safe.x1 : 0.0f;
                        float bo = b.y1 - safe.y1 > 0.0f ? b.y1 - safe.y1 : 0.0f;
                        if (host::log::g_echo)
                        {
                            printf("     | safe-area: %s %s %s %s L=%.2f T=%.2f R=%.2f B=%.2f (%.1f,%.1f)-(%.1f,%.1f)\n",
                                   kSceneNames[id], screen, kEnvelopes[en].name, s.pieces[i].name, lo, to, ro, bo,
                                   b.x0, b.y0, b.x1, b.y1);
                        }
                        int matched = -1;
                        for (int k = 0; kOutsideSafeAreaToFix[k].piece != NULL; k++)
                        {
                            const SafeAreaTodo& t = kOutsideSafeAreaToFix[k];
                            size_t len = strlen(t.piece);
                            bool nameOk = (len > 0 && t.piece[len - 1] == '*')
                                              ? strncmp(s.pieces[i].name, t.piece, len - 1) == 0
                                              : strcmp(s.pieces[i].name, t.piece) == 0;
                            if (nameOk && matches(t.screen, screen))
                            {
                                matched = k;
                                todoHit[k] = true;
                                break;
                            }
                        }
                        if (matched < 0)
                        {
                            host::test::fail(__FILE__, __LINE__,
                                             "%s, %s, %s sizes: %s leaves CRT safe area (%.0f,%.0f)-(%.0f,%.0f): "
                                             "(%.1f,%.1f)-(%.1f,%.1f) [overhang L=%.1f T=%.1f R=%.1f B=%.1f]",
                                             kSceneNames[id], screen, kEnvelopes[en].name, s.pieces[i].name,
                                             safe.x0, safe.y0, safe.x1, safe.y1, b.x0, b.y0, b.x1, b.y1,
                                             lo, to, ro, bo);
                        }
                        else
                        {
                            const SafeAreaTodo& t = kOutsideSafeAreaToFix[matched];
                            const float kTol = 0.15f;
                            if (lo > t.left + kTol || to > t.top + kTol || ro > t.right + kTol
                                || bo > t.bottom + kTol)
                            {
                                host::test::fail(__FILE__, __LINE__,
                                                 "%s, %s, %s sizes: %s overhang grew past kOutsideSafeAreaToFix "
                                                 "(L=%.1f/%.1f T=%.1f/%.1f R=%.1f/%.1f B=%.1f/%.1f)",
                                                 kSceneNames[id], screen, kEnvelopes[en].name, s.pieces[i].name,
                                                 lo, t.left, to, t.top, ro, t.right, bo, t.bottom);
                            }
                        }
                    }
                }
            }
        }
        // Training character select pending header (menu/drill_list.cpp drawPending):
        // widest real reopen hint ("- + B = Training Menu", pad_map.h) stays inside
        // the CRT safe area.
        gfx::setWidescreen(wide);
        gfx::forgetWidths();
        float scale = ui::style::scaleForCap(ui::style::kRowCap);
        float y = ui::style::textYForCap(layout::kCssPendingCapTop, scale);
        const char* hint = "- + B = Training Menu";
        float hintX = layout::kCssPendingRight - ui::style::boldWidth(hint, scale);
        canvas::clear();
        ui::style::boldText(hintX, y, hint, ui::style::kText, scale, scale);
        Box hintBox = drawnBox();
        if (!inside(hintBox, safe))
        {
            host::test::fail(__FILE__, __LINE__,
                             "css-pending, %s: leaves CRT safe area (%.0f,%.0f)-(%.0f,%.0f): "
                             "hint (%.1f,%.1f)-(%.1f,%.1f)",
                             screen, safe.x0, safe.y0, safe.x1, safe.y1,
                             hintBox.x0, hintBox.y0, hintBox.x1, hintBox.y1);
        }
    }
    for (int k = 0; kOutsideSafeAreaToFix[k].piece != NULL; k++)
    {
        if (!todoHit[k])
        {
            host::test::fail(__FILE__, __LINE__,
                             "kOutsideSafeAreaToFix entry %s (%s) is now inside the CRT safe area: remove it",
                             kOutsideSafeAreaToFix[k].piece, kOutsideSafeAreaToFix[k].screen);
        }
    }
}

TEST(hud_layout, notices_and_rec_messages_stay_above_395_and_clear)
{
    // storage::noticeDraw, record::hudDraw's refusal, and the recording seek
    // bar sit inside the CRT safe area and well above the damage/portrait row
    // in both 4:3 and 16:9, clear of damage meters, watermark, 4P controllers,
    // typical free-lab Info Display, and each other; menu-open notices sit
    // below the menu panel (style::kPanelBottom).
    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        Box safe = layout::safeArea(wide);
        gfx::setWidescreen(wide);
        gfx::forgetWidths();
        storage::notice(storage::kNoticeOk, "Lab settings from an older version: new ones at defaults");
        for (int k = 0; k < 2; k++)
        {
            canvas::clear();
            storage::noticeDraw();
        }
        storage::noticeClear();
        Box notice = drawnBox();

        // Longest export notice (recording_file.cpp: "Exported %s - load from Training Menu (X)").
        gfx::forgetWidths();
        storage::notice(storage::kNoticeOk, "Exported 991231-235959-99.tmr - load from Training Menu (X)");
        for (int k = 0; k < 2; k++)
        {
            canvas::clear();
            storage::noticeDraw();
        }
        storage::noticeClear();
        Box exportNotice = drawnBox();
        CHECK(inside(exportNotice, safe) && exportNotice.y1 <= 395.0f);

        record::HudInfo info = {record::kHudHidden, 0, 0, false,
                                "Recording stopped: the state is from other positions."};
        for (int k = 0; k < 2; k++)
        {
            canvas::clear();
            record::hudDraw(info);
        }
        Box refusal = drawnBox();

        canvas::clear();
        record::HudInfo barInfo = {record::kHudPlayback, 3599, 3600, false, NULL};
        record::hudDraw(barInfo);
        Box recBar = drawnBox();

        CHECK(inside(notice, safe) && notice.y1 <= 395.0f);
        CHECK(inside(refusal, safe) && refusal.y1 <= 395.0f);
        CHECK(inside(recBar, safe) && recBar.y1 <= 386.0f);
        // Clear of each other when shown together (notice above bar with margin).
        CHECK(notice.y1 <= refusal.y0 + kSlack || refusal.y1 <= notice.y0 + kSlack);
        CHECK(notice.y1 <= recBar.y0 - 2.0f);
        CHECK(exportNotice.y1 <= recBar.y0 - 2.0f);
        CHECK(refusal.y1 <= recBar.y0 + kSlack || recBar.y1 <= refusal.y0 + kSlack);

        // Longest export notice with a menu open: sits in the strip below the
        // panel bottom (style::kPanelBottom), inside the safe area and clear
        // of the watermark on both 4:3 and 16:9.
        gfx::forgetWidths();
        storage::notice(storage::kNoticeOk, "Exported 991231-235959-99.tmr - load from Training Menu (X)");
        for (int k = 0; k < 2; k++)
        {
            canvas::clear();
            storage::noticeDraw(true);
        }
        storage::noticeClear();
        Box menuNotice = drawnBox();
        CHECK(inside(menuNotice, safe));
        CHECK(menuNotice.y0 >= ui::style::kPanelBottom - kSlack);

        canvas::clear();
        ui::drawWatermark();
        Box mark = drawnBox();
        CHECK(inside(mark, safe));
        CHECK(menuNotice.x1 <= mark.x0 + kSlack || mark.x1 <= menuNotice.x0 + kSlack
              || menuNotice.y1 <= mark.y0 + kSlack || mark.y1 <= menuNotice.y0 + kSlack);

        float scale = ui::style::scaleForCap(ui::style::kRowCap);
        float py = ui::style::textYForCap(layout::kCssPendingCapTop, scale);
        const char* hint = "- + B = Training Menu";
        float hintX = layout::kCssPendingRight - ui::style::boldWidth(hint, scale);
        canvas::clear();
        ui::style::boldText(hintX, py, hint, ui::style::kText, scale, scale);
        Box hintBox = drawnBox();
        CHECK(inside(hintBox, safe));

        layout::setLabBand(layout::labBand(true, 0, wide));
        float iw = layout::infoWidth(1, wide);
        float itop = layout::infoTop(layout::topBand(), iw, wide);
        Box infoHmn = layout::box(layout::infoLeft(wide), itop, iw, layout::infoHeight(8, 1));
        Box infoCpu = layout::box(layout::infoLeft(wide), layout::infoCpuTop(itop, infoHmn.y1), iw,
                                  layout::infoHeight(2, 1));

        const Box moved[3] = {notice, refusal, recBar};
        for (int bi = 0; bi < 3; bi++)
        {
            const Box& b = moved[bi];
            const Box others[3] = {mark, infoHmn, infoCpu};
            for (int oi = 0; oi < 3; oi++)
            {
                float dx = (b.x1 < others[oi].x1 ? b.x1 : others[oi].x1)
                           - (b.x0 > others[oi].x0 ? b.x0 : others[oi].x0);
                float dy = (b.y1 < others[oi].y1 ? b.y1 : others[oi].y1)
                           - (b.y0 > others[oi].y0 ? b.y0 : others[oi].y0);
                CHECK(dx <= kSlack || dy <= kSlack);
            }
            for (int p = 0; p < 4; p++)
            {
                Box dmg = brawlDamage(p, wide);
                CHECK(b.y1 <= dmg.y0 + kBrawlSlack);
            }
        }
    }
}

TEST(hud_layout, techchase_hud_clears_all_right_column_combos)
{
    // Verify Techchase HUD card stays inside CRT safe bounds, above Brawl's
    // damage HUD (y <= 395), and clear of topBand + every active right-column
    // overlay across all 288 combinations (4:3 & 16:9, timer/ledge/none,
    // 0..2 input viewer slots, DI compass, PSA inspector, Adaptive HUD).
    struct Overlap {
        static bool check(const Box& a, const Box& b)
        {
            return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
        }
    };
    int combos = 0;
    for (int a = 0; a < 2; a++)
    {
        bool wide = a == 1;
        Box safe = layout::safeArea(wide);
        Box dmg0 = brawlDamage(0, wide);
        for (int timer = 0; timer <= 1; timer++)
        {
            static const int kLedge[3] = {0, 30, 64};
            for (int li = 0; li < 3; li++)
            {
                Box band = layout::labBand(timer != 0, kLedge[li], wide);
                for (int vSlots = 0; vSlots <= 2; vSlots++)
                {
                    for (int comp = 0; comp <= 1; comp++)
                    {
                        for (int psa = 0; psa <= 1; psa++)
                        {
                            for (int adap = 0; adap <= 1; adap++)
                            {
                                combos++;
                                Box tc = layout::techchaseHudBox(band, vSlots, comp != 0, psa != 0, wide, adap != 0);
                                CHECK(inside(tc, safe));
                                CHECK(tc.y1 <= dmg0.y0 - 2.0f);
                                if (!layout::empty(band))
                                {
                                    CHECK(!Overlap::check(tc, band));
                                }
                                for (int s = 0; s < vSlots; s++)
                                {
                                    CHECK(!Overlap::check(tc, layout::inputViewerCardBox(s, band, wide)));
                                }
                                if (comp != 0)
                                {
                                    CHECK(!Overlap::check(tc, layout::diCompassBox(band, vSlots, wide)));
                                }
                                if (psa != 0)
                                {
                                    CHECK(!Overlap::check(tc, layout::psaInspectorBox(band, vSlots, comp != 0, wide)));
                                }
                                if (adap != 0)
                                {
                                    CHECK(!Overlap::check(
                                        tc, layout::adaptiveHudBox(band, vSlots, comp != 0, psa != 0, wide)));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK_EQ(combos, 288);
}
