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
#include "host/mem.h"
#include "host/test.h"
#include "main/game/techniques.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/record/hud.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/ui/hud_layout.h"
#include "main/ui/match_hud.h"
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
        // Top-center calm white swatch-legend row sits in y < kBarTop, left of the right column.
        float plateSplit = layout::kViewerRight - layout::kRightColWidth * layout::xsOf(gfx::widescreen()) - 2.0f;
        Box out = layout::none();
        for (int i = 0; i < 6 && i < canvas::rects(); i++)
        {
            const canvas::Rect& r = canvas::rect(i);
            Box rb = {r.x, r.y, r.x + r.width, r.y + r.height};
            out = layout::unite(out, rb);
        }
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& run = canvas::run(i);
            if (run.y < layout::kBarTop && run.x < plateSplit)
            {
                Box tb = {run.x, run.y, run.right, run.y + 24.0f * run.scaleY};
                out = layout::unite(out, tb);
            }
        }
        return out;
    }

    Box drawnLedgedashPlate(int cells, bool noXlu = false)
    {
        seedLedgedash(cells, true, noXlu);
        canvas::clear();
        ui::hudLayoutDrawLedgedashHud();
        // Right-margin Panel B quiet smoked stat plate sits at x >= plateSplit.
        float plateSplit = layout::kViewerRight - layout::kRightColWidth * layout::xsOf(gfx::widescreen()) - 2.0f;
        Box out = layout::none();
        for (int i = 0; i < canvas::rects(); i++)
        {
            const canvas::Rect& r = canvas::rect(i);
            if (r.x >= plateSplit)
            {
                Box rb = {r.x, r.y, r.x + r.width, r.y + r.height};
                out = layout::unite(out, rb);
            }
        }
        for (int i = 0; i < canvas::runs(); i++)
        {
            const canvas::Run& run = canvas::run(i);
            if (run.x >= plateSplit)
            {
                Box tb = {run.x, run.y, run.right, run.y + 24.0f * run.scaleY};
                out = layout::unite(out, tb);
            }
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
                s.add("ledge-plate", drawnLedgedashPlate(e.ledgeCells), kPlain);
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
        {"lab-4p", "*", "typical", "info-hmn", "osd-topleft-p3", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topright-p1", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topright-p1", "top spots"},
        {"*", "4:3", "*", "info-hmn", "osd-topright-p2", "top spots"},
        {"*", "16:9", "most", "info-hmn", "osd-topright-p2", "top spots"},
        {"lab-4p", "*", "most", "info-hmn", "osd-topright-p3", "top spots"},
        {"lab-4p", "4:3", "typical", "info-hmn", "osd-topright-p3", "top spots"},
        {"lab-4p", "4:3", "most", "info-cpu", "osd-topleft-p3", "top spots"},
        {"lab-4p", "4:3", "most", "info-cpu", "osd-topright-p3", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-bar", "osd-topleft-p1", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-bar", "osd-topright-p1", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-key", "osd-topleft-p1", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-key", "osd-topright-p1", "top spots"},
        {"lab-ledgedash-hud", "*", "*", "ledge-plate", "osd-topright-p2", "top spots"},
        {"lab-ledgedash-hud", "*", "most", "ledge-plate", "osd-sides-p2", "six messages"},
        {"lab-ledgedash-hud", "*", "most", "ledge-plate", "osd-topleft-p2", "top spots"},
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
            Box barWithPointer = layout::unite(barBox, layout::ledgeLegendBox(wide));
            CHECK_NEAR(barBox.x0, barDrawn.x0, 0.05);
            CHECK_NEAR(barBox.x1, barDrawn.x1, 0.05);
            CHECK_NEAR(barBox.y0, barDrawn.y0, 0.05);
            CHECK(barDrawn.y1 <= barWithPointer.y1 + 0.05f);
            CHECK(inside(barDrawn, layout::topBand()));

            for (int noXlu = 0; noXlu <= 1; noXlu++)
            {
                Box hdrDrawn = drawnLedgedashHeader(cells, noXlu != 0);
                Box hdrRoom = layout::ledgeHeaderBox(wide);
                CHECK(inside(hdrDrawn, hdrRoom));
                CHECK(inside(hdrDrawn, layout::topBand()));
                CHECK(inside(hdrDrawn, safe));

                Box plateDrawn = drawnLedgedashPlate(cells, noXlu != 0);
                CHECK(inside(plateDrawn, safe));
                CHECK(plateDrawn.x1 <= 608.0f + 0.5f);
                CHECK(!layout::meets(plateDrawn, barBox));
            }
        }
    }
}

TEST(hud_layout, ledgedash_hud_dismisses_on_mode_switch_until_new_attempt)
{
    seedLedgedash(30, true, false);
    CHECK(ui::hudLayoutLedgedashVisible());
    // Mode switch or scenario reset bumps the dismissal counter: stale bar hides immediately.
    ui::menuScratch()[ui::kLedgeDismissScratchOffset]++;
    CHECK(!ui::hudLayoutLedgedashVisible());
    // A brand-new ledgedash finishing on a later frame becomes visible again.
    s_testLedgedash.done = 220;
    host::fakes::setTrackerFrame(220);
    CHECK(ui::hudLayoutLedgedashVisible());
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
    // Verify unified top-center Techchase HUD banner stays inside CRT safe
    // bounds, above Brawl's damage HUD, and clears every active right-column
    // overlay across both 4:3 and 16:9 when Techchase owns topBand().
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
        Box tc = layout::techchaseHudBox(layout::none(), 0, false, false, wide, false);
        Box band = tc;
        CHECK(inside(tc, safe));
        CHECK(tc.y1 <= dmg0.y0 - 2.0f);
        for (int vSlots = 0; vSlots <= 2; vSlots++)
        {
            for (int comp = 0; comp <= 1; comp++)
            {
                for (int psa = 0; psa <= 1; psa++)
                {
                    for (int adap = 0; adap <= 1; adap++)
                    {
                        combos++;
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
    CHECK_EQ(combos, 48);
}

TEST(hud_layout, stream2_live_hud_ergonomics_f1_to_f8_43_and_169)
{
    // Verifies all Stream 2 ergonomic non-overlap invariants across 16:9 and 4:3:
    //  F1: SF6 Dual Frame Strip (32 cells @ 6px in 16:9, 24 cells @ 8px in 4:3) clears
    //      P2 % (<= 307 on 4:3), watermark (y 446), recBar (y <= 376), and auto-relocates
    //      to top-center in 3P/4P so P3/P4 portraits (y 396..450) are never touched.
    //  F2/F6: Frame-Step Inspector (Info + PSA) clamped to x0=34, y0=28, 4:3 width <= 218
    //         (6px clear of 4:3 timer x=258), stepping cleanly to y=83 below counterRoom
    //         and y=69..70 below labBand.
    //  F3/F4: Option A Bare GC Controller anchored at x1=608, y=26..82 (never teleports);
    //         16:9 dual side-by-side (x=423..608, 56px clear of timer); 4:3 dual stacked
    //         (y=26..82, 90..146, 110px clear of timer); Ledgedash 64-cell bar clears it;
    //         Panel B right-margin stat plate stacks cleanly below it.
    //  F5: Portrait-docked CPU: <State> tag (y=382..395) clears kPositionHud P2 OSD
    //      (floor 376, glide-in <= 380.9) and shifted recBar (y=356..376, floor 350).
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        float xs = layout::xsOf(wide);
        Box safe = layout::safeArea(wide);
        Box timer = layout::timerRoom(wide);
        Box counter = layout::counterRoom(wide);

        // F1: SF6 Dual Frame Strip 2P + 3P/4P auto-relocation.
        Box sf6_2p = layout::actionLogBox(wide, 2, true);
        CHECK(inside(sf6_2p, safe));
        CHECK_NEAR(398.0f, sf6_2p.y0, 0.01f);
        CHECK_NEAR(438.0f, sf6_2p.y1, 0.01f);
        CHECK_NEAR(608.0f, sf6_2p.x1, 0.01f);
        if (wide)
        {
            CHECK_NEAR(334.0f, sf6_2p.x0, 0.01f);
            CHECK(sf6_2p.x0 >= osd::kHudRight[1] + 40.0f);
        }
        else
        {
            CHECK_NEAR(322.0f, sf6_2p.x0, 0.01f);
            CHECK(sf6_2p.x0 >= 307.0f + 15.0f);
        }
        CHECK(sf6_2p.y1 <= layout::kWatermarkCapTop - 6.0f);
        CHECK(sf6_2p.y0 >= layout::kRecBarCentreY + layout::kRecBarHeight * 0.5f + 20.0f);

        Box sf6_4p_noTimer = layout::actionLogBox(wide, 4, false);
        Box sf6_4p_timer = layout::actionLogBox(wide, 4, true);
        CHECK(inside(sf6_4p_noTimer, safe));
        CHECK(inside(sf6_4p_timer, safe));
        CHECK_NEAR(24.0f, sf6_4p_noTimer.y0, 0.01f);
        CHECK_NEAR(62.0f, sf6_4p_noTimer.y1, 0.01f);
        CHECK_NEAR(71.0f, sf6_4p_timer.y0, 0.01f);
        CHECK_NEAR(109.0f, sf6_4p_timer.y1, 0.01f);
        CHECK(!layout::meets(sf6_4p_timer, timer));
        for (int p = 2; p < 4; p++)
        {
            CHECK(!layout::meets(sf6_4p_noTimer, brawlDamage(p, wide)));
            CHECK(!layout::meets(sf6_4p_timer, brawlDamage(p, wide)));
        }

        // F2/F6: Panel B Frame-Step Inspector bounds & stepping below counterRoom / labBand.
        float maxInfoW = layout::infoWidth(2, wide);
        CHECK(maxInfoW <= 218.0f * xs + 0.01f);
        CHECK_NEAR(32.0f, layout::infoLeft(wide), 0.01f);
        CHECK(layout::infoLeft(wide) + maxInfoW <= timer.x0 - 8.0f + 0.01f);
        CHECK_NEAR(24.0f, layout::infoTop(timer, maxInfoW, wide), 0.01f);
        CHECK_NEAR(counter.y1 + layout::kStepGap, layout::infoTopWithCounter(timer, maxInfoW, wide, true), 0.01f);
        CHECK_NEAR(85.0f, layout::infoTopWithCounter(timer, maxInfoW, wide, true), 0.01f);

        Box ledgeBand = layout::labBand(false, 64, wide);
        CHECK(layout::infoTop(ledgeBand, maxInfoW, wide) >= ledgeBand.y1 + layout::kStepGap - 0.01f);

        // Ledgedash Practice Drill dynamic top band (cellCount >= 28 & 54) forces Large Info Display
        // to step below y = 72 on both 16:9 and 4:3 (zero overlap with bar or swatch legend).
        Box drillLedge28 = layout::drillLedgedashBand(28, wide);
        Box drillLedge54 = layout::drillLedgedashBand(54, wide);
        CHECK(inside(drillLedge28, safe));
        CHECK(inside(drillLedge54, safe));
        CHECK(layout::infoTop(drillLedge28, maxInfoW, wide) >= drillLedge28.y1 + layout::kStepGap - 0.01f);
        CHECK(layout::infoTop(drillLedge54, maxInfoW, wide) >= drillLedge54.y1 + layout::kStepGap - 0.01f);
        Box infoLedge54 = layout::box(layout::infoLeft(wide), layout::infoTop(drillLedge54, maxInfoW, wide), maxInfoW,
                                      layout::infoHeight(4, 2));
        CHECK(!layout::meets(infoLedge54, drillLedge54));

        // Flush Panel B stacking: P1 + CPU Info Display stack with 0px gap so PSA sits flush below infoStackBottom.
        float hmnH = layout::infoHeight(2, 1);
        float cpuH = layout::infoHeight(1, 1);
        float stackedBot = layout::infoStackBottom(timer, 2, 1, 1, 1, wide);
        CHECK_NEAR(layout::kInfoTop + hmnH + cpuH, stackedBot, 0.01f);
        Box psaFlush = layout::psaInspectorBox(timer, 1, false, wide, stackedBot);
        CHECK_NEAR(stackedBot, psaFlush.y0, 0.01f);
        CHECK_NEAR(layout::kInfoLeft, psaFlush.x0, 0.01f);

        // PSA stays flush on Top-Left for 6-row Info at y=24 AND 4-row Info stepped to y=85
        // (infoBottom=147, y1=255 <= 348), and falls back cleanly only when infoBottom + kPsaHeight > 348.
        float sixRowBot = layout::kInfoTop + layout::infoHeight(6, 2);
        Box psaSix = layout::psaInspectorBox(timer, 1, false, wide, sixRowBot);
        CHECK_NEAR(layout::kInfoLeft, psaSix.x0, 0.01f);
        CHECK_NEAR(sixRowBot, psaSix.y0, 0.01f);
        CHECK(psaSix.y1 <= 348.0f + 0.01f);

        float counterInfoBot = layout::infoTopWithCounter(timer, maxInfoW, wide, true) + layout::infoHeight(4, 1);
        CHECK_NEAR(147.0f, counterInfoBot, 0.01f);
        Box psaStepped = layout::psaInspectorBox(timer, 1, false, wide, counterInfoBot);
        CHECK_NEAR(layout::kInfoLeft, psaStepped.x0, 0.01f);
        CHECK_NEAR(counterInfoBot, psaStepped.y0, 0.01f);
        CHECK_NEAR(255.0f, psaStepped.y1, 0.01f);

        float extreme8x8Bot = layout::infoStackBottom(timer, 8, 2, 8, 2, wide);
        CHECK(extreme8x8Bot + layout::kPsaHeight > 348.0f);
        Box psaFallback = layout::psaInspectorBox(timer, 1, false, wide, extreme8x8Bot);
        CHECK(psaFallback.x0 > layout::infoLeft(wide) + maxInfoW);
        CHECK(inside(psaFallback, safe));

        // F3/F4: Option A Bare GC Controller never teleports; dual layout clears timer & Ledgedash bar.
        Box v0Timer = layout::inputViewerCardBox(0, timer, wide);
        Box v0Ledge = layout::inputViewerCardBox(0, ledgeBand, wide);
        CHECK_NEAR(v0Timer.x0, v0Ledge.x0, 0.01f);
        CHECK_NEAR(v0Timer.y0, v0Ledge.y0, 0.01f);
        CHECK_NEAR(608.0f, v0Timer.x1, 0.01f);
        CHECK_NEAR(24.0f, v0Timer.y0, 0.01f);
        CHECK_NEAR(79.0f, v0Timer.y1, 0.01f);

        Box v1Timer = layout::inputViewerCardBox(1, timer, wide);
        CHECK(inside(v1Timer, safe));
        CHECK(!layout::meets(v0Timer, v1Timer));
        CHECK(!layout::meets(v1Timer, timer));
        if (wide)
        {
            CHECK_NEAR(423.0f, v1Timer.x0, 0.01f);
            CHECK_NEAR(24.0f, v1Timer.y0, 0.01f);
            CHECK(v1Timer.x0 >= timer.x1 + 56.0f - 0.5f);
        }
        else
        {
            CHECK_NEAR(490.0f, v1Timer.x0, 0.01f);
            CHECK_NEAR(87.0f, v1Timer.y0, 0.01f);
            CHECK_NEAR(142.0f, v1Timer.y1, 0.01f);
            CHECK(v1Timer.x0 >= timer.x1 + 110.0f - 0.5f);
        }

        // Ledgedash 64-cell bar clears Option A GC controller and Panel B stat plate sits in middle-right eye real estate.
        Box ledgeBar64 = layout::frameBarBox(64, wide);
        CHECK(inside(ledgeBar64, safe));
        CHECK(!layout::meets(ledgeBar64, v0Ledge));
        CHECK(ledgeBar64.x1 <= v0Ledge.x0 - 8.0f + 0.01f);
        Box statPlate = layout::drillStatPlateBox(3, v0Ledge.y1, wide);
        CHECK(inside(statPlate, safe));
        CHECK_NEAR(608.0f, statPlate.x1, 0.01f);
        CHECK_NEAR(v0Ledge.x0, statPlate.x0, 0.01f);
        CHECK_NEAR(layout::kDrillPlateMidY, statPlate.y0, 0.01f);
        CHECK(!layout::meets(statPlate, v0Ledge));
        CHECK(!layout::meets(statPlate, ledgeBar64));

        // F5: Portrait-docked CPU: <State> tag clears kPositionHud P2 OSD and shifted recBar.
        Box cpuTag = layout::cpuStatusTagBox(1, wide);
        CHECK(inside(cpuTag, safe));
        CHECK_NEAR(382.0f, cpuTag.y0, 0.01f);
        CHECK_NEAR(395.0f, cpuTag.y1, 0.01f);
        osd::StackPlace p2Hud = osd::stackPlace(osd::kPositionHud, 1, xs);
        float p2GlideBottom = p2Hud.y + osd::kEnterFrom * p2Hud.dy + osd::kBoxHeight * 0.5f;
        CHECK(p2Hud.y + osd::kBoxHeight * 0.5f <= 376.0f + 0.01f);
        CHECK(p2GlideBottom < cpuTag.y0);
        CHECK_NEAR(366.0f, layout::kRecBarCentreY, 0.01f);
        CHECK_NEAR(350.0f, layout::recBarOsdFloor(), 0.01f);
        CHECK(layout::kRecBarCentreY + layout::kRecBarHeight * 0.5f <= cpuTag.y0 - 6.0f + 0.01f);
    }
}

TEST(hud_layout, scenario_stat_plate_clears_timer_controllers_and_top_right_osds)
{
    // The 7 non-Techchase Lab Scenarios draw ONLY the quiet Panel B right-margin
    // 3-row stat plate (no top-center bar, Brawl's timer stays visible). Verify
    // 5% CRT safe containment and non-overlap against timer, Option A GC pads,
    // SF6 dual frame strip, damage row, and Top Right OSDs on 4:3 and 16:9.
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        Box safe = layout::safeArea(wide);
        Box timer = layout::timerRoom(wide);
        Box sf6 = layout::actionLogBox(wide, 2, true);
        for (int vSlots = 0; vSlots <= 2; vSlots++)
        {
            float viewerBot = 0.0f;
            for (int s = 0; s < vSlots; s++)
            {
                Box vb = layout::inputViewerCardBox(s, timer, wide);
                if (vb.y1 > viewerBot) viewerBot = vb.y1;
            }
            Box plate = layout::drillStatPlateBox(3, viewerBot, wide);
            CHECK(inside(plate, safe));
            CHECK_NEAR(608.0f, plate.x1, 0.01f);
            CHECK(!layout::meets(plate, timer));
            CHECK(!layout::meets(plate, sf6));
            for (int s = 0; s < vSlots; s++)
            {
                CHECK(!layout::meets(plate, layout::inputViewerCardBox(s, timer, wide)));
            }
            for (int p = 0; p < 4; p++)
            {
                CHECK(!layout::meets(plate, brawlDamage(p, wide)));
            }
            float rightBot = layout::rightColumnBottom(timer, vSlots, false, false, wide, false, 3);
            CHECK(rightBot >= plate.y1 - 0.01f);
            float osdY = layout::osdTopStartY(true, timer, 0.0f, rightBot);
            CHECK(osdY >= plate.y1 + layout::kStepGap - 0.01f);
        }
    }
}

TEST(hud_layout, training_sidebar_shifts_right_column_cleanly_43_and_169)
{
    // Edge Case 5A: When Brawl's stock Training Help: ON sidebar is active
    // (IfTraining[0x30] != 0), GC Input Viewer slots, Panel B Stat Plates,
    // and Adaptive CPU shift cleanly around trainingSidebarRoom(wide) with
    // zero overlap against the sidebar, timer, SF6 strip, recBar, and Brawl damage.
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        Box safe = layout::safeArea(wide);
        Box sb = layout::trainingSidebarRoom(wide);
        Box timer = layout::timerRoom(wide);
        Box sf6 = layout::actionLogBox(wide, 2, true);
        Box recBar = wide ? layout::Box{215.0f, 356.0f, 425.0f, 376.0f} : layout::Box{180.0f, 356.0f, 460.0f, 376.0f};
        CHECK(inside(sb, safe));

        for (int vSlots = 0; vSlots <= 2; vSlots++)
        {
            float viewerBot = 0.0f;
            Box viewers[2];
            for (int s = 0; s < vSlots; s++)
            {
                viewers[s] = layout::inputViewerCardBox(s, timer, wide, true);
                CHECK(inside(viewers[s], safe));
                CHECK(!layout::meets(viewers[s], sb));
                CHECK(!layout::meets(viewers[s], timer));
                CHECK(!layout::meets(viewers[s], sf6));
                CHECK(!layout::meets(viewers[s], recBar));
                for (int p = 0; p < 4; p++)
                {
                    CHECK(!layout::meets(viewers[s], brawlDamage(p, wide)));
                }
                if (viewers[s].y1 > viewerBot)
                {
                    viewerBot = viewers[s].y1;
                }
            }
            if (vSlots == 2)
            {
                CHECK(!layout::meets(viewers[0], viewers[1]));
            }

            // Panel B 3-row stat plate shifts cleanly left of sidebar.
            Box plate = layout::drillStatPlateBox(3, viewerBot, wide, true);
            CHECK(inside(plate, safe));
            CHECK(!layout::meets(plate, sb));
            CHECK(!layout::meets(plate, timer));
            CHECK(!layout::meets(plate, sf6));
            CHECK(!layout::meets(plate, recBar));
            for (int s = 0; s < vSlots; s++)
            {
                CHECK(!layout::meets(plate, viewers[s]));
            }
            for (int p = 0; p < 4; p++)
            {
                CHECK(!layout::meets(plate, brawlDamage(p, wide)));
            }

            // Adaptive CPU plate clears sidebar, viewers, timer, SF6, recBar, and Brawl damage.
            Box adap = layout::adaptiveHudBox(timer, vSlots, false, false, wide, 0.0f, true);
            CHECK(inside(adap, safe));
            CHECK(!layout::meets(adap, sb));
            CHECK(!layout::meets(adap, timer));
            CHECK(!layout::meets(adap, sf6));
            CHECK(!layout::meets(adap, recBar));
            for (int s = 0; s < vSlots; s++)
            {
                CHECK(!layout::meets(adap, viewers[s]));
            }
            for (int p = 0; p < 4; p++)
            {
                CHECK(!layout::meets(adap, brawlDamage(p, wide)));
            }

            float rightBot = layout::rightColumnBottom(timer, vSlots, false, false, wide, false, 3, true);
            CHECK(rightBot >= sb.y1 - 0.01f);
            if (vSlots >= 1)
            {
                CHECK(rightBot >= viewers[0].y1 - 0.01f);
            }
        }
    }
}

TEST(hud_layout, hide_player_damage_hud_sets_and_restores_optid_disable_gather)
{
    // Edge Case 5B: ui::hidePlayerDamageHud(1) + ui::matchHudApply() sets
    // OPTID_DISABLE_GATHER (0x10) on P2's 50 MuObject* ScnObj roots and 3
    // IfPlayer ScnGroup* roots (+0xF0..+0xF8), leaves P1 untouched, and
    // cleanly clears bit 0x10 on the next matchHudApply() without a request.
    host::mem::clear();

    const u32 kIfMngrPtr = 0x805A02D0u;
    const u32 kIfMngr = 0x80300000u;
    const u32 kIfPlayer0 = 0x80301000u;
    const u32 kIfPlayer1 = 0x80302000u;
    const u32 kMuBase0 = 0x80310000u;
    const u32 kMuBase1 = 0x80320000u;
    const u32 kScnBase0 = 0x80330000u;
    const u32 kScnBase1 = 0x80340000u;
    const u32 kGrpBase0 = 0x80350000u;
    const u32 kGrpBase1 = 0x80351000u;

    host::mem::setWord(kIfMngrPtr, kIfMngr);
    host::mem::setWord(kIfMngr + 0x04u, 5u); // kind 5 = Training
    host::mem::setWord(kIfMngr + 0x4Cu, kIfPlayer0);
    host::mem::setWord(kIfMngr + 0x50u, kIfPlayer1);
    host::mem::setByte(kIfMngr + 0x68u, 0u); // player 0 -> slot 0
    host::mem::setByte(kIfMngr + 0x69u, 1u); // player 1 -> slot 1

    // Seed P1 (playerNo 0) and P2 (playerNo 1) with 50 MuObject* parts and 3 ScnGroup* roots.
    for (int pl = 0; pl < 2; pl++)
    {
        u32 ifp = (pl == 0) ? kIfPlayer0 : kIfPlayer1;
        u32 muBase = (pl == 0) ? kMuBase0 : kMuBase1;
        u32 scnBase = (pl == 0) ? kScnBase0 : kScnBase1;
        u32 grpBase = (pl == 0) ? kGrpBase0 : kGrpBase1;
        for (int i = 0; i < 50; i++)
        {
            u32 mu = muBase + static_cast<u32>(i) * 0x20u;
            u32 scn = scnBase + static_cast<u32>(i) * 0x100u;
            host::mem::setWord(ifp + 0x28u + static_cast<u32>(i) * 4u, mu);
            host::mem::setWord(mu + 0x10u, scn);
            host::mem::setWord(scn + 0xCCu, 0x01u); // arbitrary unrelated flag preserved
        }
        for (int g = 0; g < 3; g++)
        {
            u32 grp = grpBase + static_cast<u32>(g) * 0x100u;
            host::mem::setWord(ifp + 0xF0u + static_cast<u32>(g) * 4u, grp);
            host::mem::setWord(grp + 0xCCu, 0x02u); // arbitrary unrelated flag preserved
        }
    }

    game::MatchState m;
    memset(&m, 0, sizeof(m));
    m.running = true;
    m.training = true;

    ui::hidePlayerDamageHud(1);
    ui::matchHudApply(m);

    // P2 has bit 0x10 set on all 50 parts + 3 groups; P1 is untouched.
    for (int i = 0; i < 50; i++)
    {
        CHECK_EQ(0x01u, host::mem::word(kScnBase0 + static_cast<u32>(i) * 0x100u + 0xCCu));
        CHECK_EQ(0x11u, host::mem::word(kScnBase1 + static_cast<u32>(i) * 0x100u + 0xCCu));
    }
    for (int g = 0; g < 3; g++)
    {
        CHECK_EQ(0x02u, host::mem::word(kGrpBase0 + static_cast<u32>(g) * 0x100u + 0xCCu));
        CHECK_EQ(0x12u, host::mem::word(kGrpBase1 + static_cast<u32>(g) * 0x100u + 0xCCu));
    }

    // Next frame without hidePlayerDamageHud(1): P2 is cleanly restored.
    ui::matchHudApply(m);
    for (int i = 0; i < 50; i++)
    {
        CHECK_EQ(0x01u, host::mem::word(kScnBase0 + static_cast<u32>(i) * 0x100u + 0xCCu));
        CHECK_EQ(0x01u, host::mem::word(kScnBase1 + static_cast<u32>(i) * 0x100u + 0xCCu));
    }
    for (int g = 0; g < 3; g++)
    {
        CHECK_EQ(0x02u, host::mem::word(kGrpBase0 + static_cast<u32>(g) * 0x100u + 0xCCu));
        CHECK_EQ(0x02u, host::mem::word(kGrpBase1 + static_cast<u32>(g) * 0x100u + 0xCCu));
    }
    host::mem::clear();
}

TEST(hud_layout, drill_ledgedash_band_matches_bar_header_and_legend_union)
{
    static const int kCells[3] = {10, 30, 64};
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        for (int i = 0; i < 3; i++)
        {
            int cells = kCells[i];
            Box actual = layout::drillLedgedashBand(cells, wide);
            Box expected = layout::unite(layout::frameBarBox(cells, wide),
                                         layout::unite(layout::ledgeHeaderBox(wide), layout::ledgeLegendBox(wide)));
            CHECK_NEAR(expected.x0, actual.x0, 0.001f);
            CHECK_NEAR(expected.y0, actual.y0, 0.001f);
            CHECK_NEAR(expected.x1, actual.x1, 0.001f);
            CHECK_NEAR(expected.y1, actual.y1, 0.001f);
        }
    }
}
