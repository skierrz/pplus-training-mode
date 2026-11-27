// In-match Wii controller support in src/main/lab.cpp:
// - Lab menu open/nav/fast-repeat/close on Classic, Nunchuk, Sideways
// - Pause-menu open (ZL/ZR, Z, -) and per-controller pause hint text
// - Frame Advance default L: Sideways B (0x400040) must not hijack L; - advances
// - D-pad Down (Frame Advance toggle & Place CPU) on Classic/Nunchuk vs Sideways
//   - + D-pad chord, with bare D-pad movement preserved and FA chord gating
// - Disconnect and attachment hot-swap clear pressed edges and release masks.
#define TM_MODULE_MAIN 1

#include <cstdio>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_pad_status.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "host/canvas.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/extra.h"
#include "main/game/advantage.h"
#include "main/game/code_menu.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/match.h"
#include "main/game/overlays.h"
#include "main/game/techniques.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/savestate/settings.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/storage/settings_file.h"
#include "main/ui/debug_overlay.h"
#include "main/ui/hud_layout.h"
#include "main/ui/info_display.h"
#include "main/ui/lab_menu.h"
#include "main/ui/ledgedash_hud.h"
#include "main/ui/match_hud.h"
#include "main/ui/menu.h"
#include "main/ui/osd.h"
#include "main/ui/pages/pages.h"
#include "main/ui/style.h"
#include "main/ui/watermark.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_map.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "shared/perf.h"

namespace {
    struct LabStubs {
        bool frozen;
        bool gameplayPaused;
        bool cameraPaused;
        int trainingCursor;
        int rewindCalls;
        bool rewindEdge;
        int hideAnalogL;
        int hideAnalogR;
        bool ownerValid;
        tm::cpu::Owner owner;
    };
    static LabStubs s_labStubs;

    static int s_fakeDrillFlags = 0;
    static u32 fakeDrillHook(u32, gfPadStatus*) { return static_cast<u32>(s_fakeDrillFlags); }

    static void resetLabStubs()
    {
        memset(&s_labStubs, 0, sizeof(s_labStubs));
        s_labStubs.trainingCursor = 0;
        s_fakeDrillFlags = 0;
    }

    static bool fakeFalse() { return false; }
    static void fakeVoid() {}
    static void fakeVoidMatch(const tm::game::MatchState&) {}
    static void fakeVoidBool(bool) {}
    static int fakeActIn(Fighter*, int*) { return -1; }
    static bool fakeAdv(const tm::game::Advantage&) { return false; }
    static bool fakeRng(int) { return false; }
}

namespace tm {
namespace game {
    void advantageSetTolerance(u8) {}
    void advantageReset(void (*)(const Advantage&)) {}
    void advantageFrame() {}
    void techniquesAttach() {}
    void techniquesDetach() {}
    void techniquesReset(void (*)(const Technique&)) {}
    void techniquesFrame() {}
    const Ledgedash& lastLedgedashEvent()
    {
        static Ledgedash l;
        return l;
    }
    void overlaysApply(bool, const OverlayConfig&) {}
    void overlaysClear() {}
    bool overlayShowsCollision(const Fighter*) { return false; }
    u32 otherStopBits() { return 0; }
    bool heldByOthers() { return false; }
    bool categoriesPaused() { return false; }
    void setFrozen(bool on) { s_labStubs.frozen = on; }
    void setGameplayPaused(bool on) { s_labStubs.gameplayPaused = on; }
    void setCameraPaused(bool on) { s_labStubs.cameraPaused = on; }
    void reapplyFrozen() {}
    EndResult endTrainingMatch() { return kEndByWait; }
    bool trainingEnding() { return false; }
    int trainingMenuCursor() { return s_labStubs.trainingCursor; }
    bool widescreen() { return false; }
    float damage(const FighterRef&) { return 0.0f; }
    void setDamage(const FighterRef&, float) {}
    bool humanPlayer(int playerNo) { return playerNo == 0; }
}
namespace display {
    void inputFighterPad(u32, const gfPadStatus*) {}
}
namespace cpu {
    bool findOwner(const void*, int, Owner& out)
    {
        if (!s_labStubs.ownerValid)
        {
            return false;
        }
        out = s_labStubs.owner;
        return true;
    }
    Fighter* fighterOf(int, bool) { return NULL; }
    void setShieldHealth(Fighter*, float) {}
    void hideAnalogTrigger(Fighter*, bool right)
    {
        if (right)
        {
            s_labStubs.hideAnalogR++;
        }
        else
        {
            s_labStubs.hideAnalogL++;
        }
    }
}
namespace record {
    int rewindStep() { return kRewindIdle; }
    bool rewindRequest(bool edge)
    {
        s_labStubs.rewindCalls++;
        s_labStubs.rewindEdge = edge;
        return true;
    }
}
namespace ui {
    void drawDebugOverlay(u32, const game::MatchState&) {}
    bool ledgedashHudVisible() { return false; }
    int ledgedashCells() { return 0; }
    void drawLedgedashHud() {}
}
namespace osd {
    void reset() {}
    void setPosition(u8) {}
    bool visible() { return false; }
    void draw() {}
    void lineColor(Message*, int, gfx::Rgba) {}
}
}

#define lab lab_uut
#include "main/lab.cpp"
#undef lab

using namespace tm;

namespace {
    void initLabPads(gfPadStatus pads[iface::kPadCount], u32 connectedMask,
                     u32 wiiType = static_cast<u32>(gfPadType::WIIMOTE))
    {
        memset(pads, 0, sizeof(gfPadStatus) * iface::kPadCount);
        for (int i = 0; i < iface::kPadCount; i++)
        {
            bool on = (connectedMask & (1u << i)) != 0;
            u32 type = i < 4 ? static_cast<u32>(gfPadType::GCC) : wiiType;
            pads[i].m_error = static_cast<s8>(on ? gfPadError::NONE : gfPadError::NO_CONTROLLER);
            pads[i].m_controllerType = static_cast<gfPadType::PadType>(type);
        }
    }

    void setPad(gfPadStatus& p, u32 bits, s8 sx = 0, s8 sy = 0, u8 rAnalog = 0)
    {
        p.m_buttonsCurrentFrame.bits = bits;
        p.m_buttonsCurrentFrame2.bits = bits;
        p.m_buttonsHeld.bits = bits;
        p.m_buttonsPressedThisFrame.bits = bits;
        p.m_buttonsPressedThisFrame2.bits = bits;
        p.m_stickX = sx;
        p.m_stickY = sy;
        p.m_rTriggerAnalog = static_cast<char>(rAnalog);
    }

    void stepBoth(gfPadStatus pads[iface::kPadCount])
    {
        gfPadStatus gamePads[iface::kPadCount];
        memcpy(gamePads, pads, sizeof(gamePads));
        lab_uut::onPads(iface::kPadsSystem, pads);
        lab_uut::onPads(iface::kPadsGame, gamePads);
    }

    static void setupLab()
    {
        resetLabStubs();
        g_ftEntryManager = NULL;
        g_ftManager = NULL;
        main::loadSettings(host::fakes::block());
        main::resetSettings();
        static main::ExtraHooks hooks;
        memset(&hooks, 0, sizeof(hooks));
        hooks.alterRow = fakeFalse;
        hooks.alterEdited = fakeVoid;
        hooks.diDraw = fakeVoidMatch;
        hooks.diReset = fakeVoid;
        hooks.diHideDrawDi = fakeVoidBool;
        hooks.actIn = fakeActIn;
        hooks.frameData = fakeVoid;
        hooks.advantageMeasured = fakeAdv;
        hooks.characterRng = fakeRng;
        hooks.drillHook = fakeDrillHook;
        main::setExtraHooks(&hooks);
        host::fakes::match().running = true;
        host::fakes::match().training = true;
        host::fakes::match().paused = false;
        lab_uut::attach();
    }

    static void teardownLab()
    {
        lab_uut::detach();
        main::setExtraHooks(NULL);
    }

    bool canvasDrew(const char* text)
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
}

TEST(wii_lab, classic_nunchuk_sideways_open_nav_close_and_pause_hints)
{
    setupLab();
    gfPadStatus pads[iface::kPadCount];

    // Classic Controller on port 5 (index 4): + opens, A enters General, + closes.
    initLabPads(pads, 1u << 4, gfPadType::WII_CLASSIC);
    host::fakes::setPlayerPort(0, 4);
    stepBoth(pads); // neutral frame establishes connection
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits); // hidden while open

    // Neutral, move down 9 to Controls (row 8 is Offline VS Displays) and press A to enter Controls (depth 1).
    for (int i = 0; i < 9; i++)
    {
        setPad(pads[4], 0);
        stepBoth(pads);
        setPad(pads[4], game::kPadDDown);
        stepBoth(pads);
    }
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadA);
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(lab_uut::s_menu.depth));
    CHECK_EQ(ui::pages::P_CONTROLS, ui::currentPage(lab_uut::s_menu));

    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());
    // Held Classic + must stay hidden (both common and extended bits) until released.
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // Pause hint & Z-in-Pause on Classic (ZL/ZR -> "ZL/ZR: Lab Menu").
    main::settings().controls.startOpensMenu = 0;
    host::fakes::match().paused = true;
    setPad(pads[4], 0);
    stepBoth(pads);
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(canvasDrew("ZL/ZR: Lab Menu"));

    setPad(pads[4], game::kPadZ | game::kPadClassicZL);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    // Close with B (1st B leaves Controls -> Lab, 2nd B focuses rail, 3rd B closes Z-in-Pause menu).
    for (int i = 0; i < 3; i++)
    {
        setPad(pads[4], 0);
        stepBoth(pads);
        setPad(pads[4], game::kPadB);
        stepBoth(pads);
    }
    CHECK(!lab_uut::menuOpen());

    // Hot-swap port 5 to Sideways Wii Remote while paused: hint updates to "-: Lab Menu"
    // and '-' opens the lab menu in pause (reopens on rail, so one 1/B closes).
    pads[4].m_controllerType = gfPadType::WIIMOTE;
    setPad(pads[4], 0);
    stepBoth(pads);
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(canvasDrew("-: Lab Menu"));
    CHECK(!canvasDrew("ZL/ZR: Lab Menu"));

    setPad(pads[4], game::kPadWiiMinus);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());

    // Close with Sideways 1 (kPadB | kPadWii1).
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadB | game::kPadWii1);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());

    // Hot-swap to Nunchuk: hint is "Z: Lab Menu", Z opens, B closes.
    pads[4].m_controllerType = gfPadType::NUNCHUK;
    setPad(pads[4], 0);
    stepBoth(pads);
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(canvasDrew("Z: Lab Menu"));

    setPad(pads[4], game::kPadZ);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadB);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());

    // Classic L/R category switching & R tap-vs-hold fast-repeat protection.
    pads[4].m_controllerType = gfPadType::WII_CLASSIC;
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadZ | game::kPadClassicZL);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    CHECK_EQ(0, static_cast<int>(lab_uut::s_menu.category));
    // Short R tap with neutral stick switches category 0 (Lab) -> 1 (Scenarios) on release.
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadR);
    stepBoth(pads);
    stepBoth(pads);
    setPad(pads[4], 0);
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(lab_uut::s_menu.category));
    // L switches back 1 (Scenarios) -> 0 (Lab) immediately on press.
    setPad(pads[4], game::kPadL);
    stepBoth(pads);
    CHECK_EQ(0, static_cast<int>(lab_uut::s_menu.category));
    // R held with Right direction fast-repeats P1 Damage and MUST NOT switch category on release.
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadDDown); // row 0 (Frame Advance) -> row 1 (P1 Damage)
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(lab_uut::s_menu.cursor[ui::pages::P_GENERAL]));
    setPad(pads[4], 0);
    stepBoth(pads);
    for (int f = 0; f < 6; f++)
    {
        setPad(pads[4], game::kPadR, 80, 0);
        stepBoth(pads);
    }
    setPad(pads[4], 0);
    stepBoth(pads);
    CHECK_EQ(0, static_cast<int>(lab_uut::s_menu.category));
    CHECK(main::settings().general.playerPercent > 0);
    teardownLab();
}

TEST(wii_lab, sideways_b_never_hijacks_frame_advance_l_and_minus_advances)
{
    setupLab();
    main::settings().general.frameAdvance = 1;
    main::settings().controls.advanceButton = main::kAdvanceL;

    gfPadStatus pads[iface::kPadCount];
    initLabPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setPlayerPort(0, 4);
    stepBoth(pads);
    CHECK(s_labStubs.frozen);

    // Sideways B sets kPadWiiB | kPadL (0x400040): must NOT advance or be hidden!
    u32 bBits = game::kPadWiiB | game::kPadL;
    setPad(pads[4], bBits);
    stepBoth(pads);
    CHECK(s_labStubs.frozen);
    CHECK_EQ(bBits, pads[4].m_buttonsCurrentFrame.bits);

    // Sideways '-' (kPadWiiMinus) advances 1 frame and is hidden from the game.
    setPad(pads[4], game::kPadWiiMinus);
    stepBoth(pads);
    CHECK(!s_labStubs.frozen);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // Nunchuk: 1 (kPadWii1) advances 1 frame for L.
    pads[4].m_controllerType = gfPadType::NUNCHUK;
    setPad(pads[4], 0);
    stepBoth(pads);
    CHECK(s_labStubs.frozen);
    setPad(pads[4], game::kPadWii1);
    stepBoth(pads);
    CHECK(!s_labStubs.frozen);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // Classic: L (kPadL) advances 1 frame for L.
    pads[4].m_controllerType = gfPadType::WII_CLASSIC;
    setPad(pads[4], 0);
    stepBoth(pads);
    CHECK(s_labStubs.frozen);
    setPad(pads[4], game::kPadL);
    stepBoth(pads);
    CHECK(!s_labStubs.frozen);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    teardownLab();
}

TEST(wii_lab, sideways_bare_dpad_untouched_minus_chord_toggles_fa_and_fa_gates_chord)
{
    setupLab();
    main::settings().controls.dpadDown = main::kDownFrameAdvance;

    gfPadStatus pads[iface::kPadCount];
    initLabPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setPlayerPort(0, 4);
    stepBoth(pads);

    // Bare D-pad Down on Sideways Wii Remote: MUST NOT hide or toggle Frame Advance.
    for (int f = 0; f < 4; f++)
    {
        setPad(pads[4], game::kPadDDown);
        stepBoth(pads);
        CHECK_EQ(static_cast<u32>(game::kPadDDown), pads[4].m_buttonsCurrentFrame.bits);
    }
    CHECK_EQ(0, static_cast<int>(main::settings().general.frameAdvance));

    // - + D-pad Down: hides D-pad Down (never '-' itself) and toggles Frame Advance on.
    main::settings().controls.advanceButton = main::kAdvanceX;
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadWiiMinus | game::kPadDDown);
    stepBoth(pads);
    CHECK_EQ(static_cast<u32>(game::kPadWiiMinus), pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(1, static_cast<int>(main::settings().general.frameAdvance));

    // Frame Advance active with '-' bound (advanceButton == L): - + D-pad Down must NOT toggle FA!
    main::settings().controls.dpadDown = main::kDownFrameAdvance;
    main::settings().controls.advanceButton = main::kAdvanceL;
    main::settings().general.frameAdvance = 1;
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadWiiMinus | game::kPadDDown);
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(main::settings().general.frameAdvance));
    // D-pad Down stays visible while '-' advances (only '-' is hidden).
    CHECK_EQ(static_cast<u32>(game::kPadDDown), pads[4].m_buttonsCurrentFrame.bits);

    // Classic Controller on port 5: raw D-pad Down is cleared in onFighterPad
    // before ipPadConfig mapping, so onFighterInput leaves post-mapping kTauntDown
    // intact (preserving face buttons remapped to Taunt/Footstool).
    main::settings().general.frameAdvance = 0;
    main::settings().controls.dpadDown = main::kDownFrameAdvance;
    pads[4].m_controllerType = gfPadType::WII_CLASSIC;
    setPad(pads[4], 0);
    stepBoth(pads);
    gfPadStatus fpad = pads[4];
    setPad(fpad, game::kPadDDown | game::kPadA);
    lab_uut::onFighterPad(4, &fpad);
    CHECK_EQ(static_cast<u32>(game::kPadA), fpad.m_buttonsCurrentFrame.bits);
    CHECK_EQ(static_cast<u32>(game::kPadA), fpad.m_buttonsCurrentFrame2.bits);
    s_labStubs.ownerValid = true;
    s_labStubs.owner.playerNo = 0;
    s_labStubs.owner.cpu = false;
    s_labStubs.owner.sub = 0;
    u32 fakeInput[8] = {0, 0, 0, 0x1000u, 0, 0, 0, 0}; // +0xC = kTauntDown
    iface::FighterInput call = {fakeInput, 0};
    lab_uut::onFighterInput(&call);
    CHECK_EQ(0x1000u, fakeInput[3]);

    // Disconnect clears releaseMask and hot-swap suppresses phantom pressed edge.
    pads[4].m_controllerType = gfPadType::WII_CLASSIC;
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits); // held close button masked
    // Disconnect port 5 for 1 frame, then reconnect holding the same button: mask cleared.
    pads[4].m_error = gfPadError::NO_CONTROLLER;
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    pads[4].m_error = gfPadError::NONE;
    setPad(pads[4], game::kPadStart | game::kPadClassicPlus);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen()); // no phantom open edge on reconnect
    CHECK_EQ(static_cast<u32>(game::kPadStart | game::kPadClassicPlus), pads[4].m_buttonsCurrentFrame.bits);
    teardownLab();
}

TEST(wii_lab, z_in_pause_owner_disconnect_recovers_via_other_connected_port)
{
    setupLab();
    main::settings().controls.startOpensMenu = 0;
    host::fakes::match().paused = true;

    // Port 5 (Classic) and port 1 (GCC) are both connected.
    gfPadStatus pads[iface::kPadCount];
    initLabPads(pads, (1u << 0) | (1u << 4), gfPadType::WII_CLASSIC);
    host::fakes::setPlayerPort(0, 4);
    stepBoth(pads);

    // Port 5 opens the Lab Menu in Z-in-Pause mode.
    setPad(pads[4], game::kPadZ | game::kPadClassicZL);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());

    // Port 5 disconnects: 1-frame transient disconnect must NOT auto-close.
    setPad(pads[4], 0);
    pads[4].m_error = gfPadError::NO_CONTROLLER;
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());

    // Another connected port (GC port 1) closes the menu with B (or Z/Start).
    setPad(pads[0], game::kPadB);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());

    // Reopen on port 5 (Sideways '-'), disconnect port 5, then recover via
    // takeover on GC port 1 (press A -> takes ownership, then B closes).
    pads[4].m_error = gfPadError::NONE;
    pads[4].m_controllerType = gfPadType::WIIMOTE;
    setPad(pads[0], 0);
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadWiiMinus);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());

    setPad(pads[4], 0);
    pads[4].m_error = gfPadError::NO_CONTROLLER;
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());

    // Fresh non-close press hands ownership to port 1 without closing yet.
    setPad(pads[0], game::kPadDDown);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    // First B focuses Draft C rail, second B closes Z-in-Pause menu.
    setPad(pads[0], 0);
    stepBoth(pads);
    setPad(pads[0], game::kPadB);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    setPad(pads[0], 0);
    stepBoth(pads);
    setPad(pads[0], game::kPadB);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());
    teardownLab();
}

TEST(wii_lab, sideways_a_cycles_tabs_and_draws_badges)
{
    setupLab();
    main::settings().controls.menuStyle = main::kMenuStyleTabs;

    gfPadStatus pads[iface::kPadCount];
    initLabPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setPlayerPort(0, 4);
    stepBoth(pads);

    // Open Draft A (Tabs) with Sideways '+'.
    setPad(pads[4], game::kPadStart);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    CHECK_EQ(0, static_cast<int>(lab_uut::s_menu.category));

    // Draft A Start mode at depth 0 draws [A] Tab, [+] Close, [-] Brawl Pause,
    // omits the no-op [1] Back pill, and suppresses the bottom-right watermark while open.
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("Tab"));
    CHECK(canvasDrew("2"));
    CHECK(canvasDrew("+"));
    CHECK(canvasDrew("Brawl Pause"));
    CHECK(!canvasDrew("1"));
    bool sawBottomWatermark = false;
    for (int i = 0; i < host::canvas::runs(); i++)
    {
        if (strcmp(host::canvas::run(i).text, TM_VERSION_MARK) == 0 && host::canvas::run(i).y >= 435.0f)
        {
            sawBottomWatermark = true;
        }
    }
    CHECK(!sawBottomWatermark);

    // Sideways A (kPadWiiA) cycles through all 7 tabs and wraps back to Lab without
    // toggling Frame Advance (row 0).
    u8 catCount = ui::labMenu().categoryCount;
    CHECK_EQ(7, static_cast<int>(catCount));
    for (int step = 1; step <= catCount; step++)
    {
        setPad(pads[4], 0);
        stepBoth(pads);
        setPad(pads[4], game::kPadWiiA);
        stepBoth(pads);
        CHECK_EQ(step % catCount, static_cast<int>(lab_uut::s_menu.category));
    }
    CHECK_EQ(0, static_cast<int>(main::settings().general.frameAdvance));

    // Sideways 2 (kPadA | kPadWii2) still confirms and toggles Frame Advance on row 0.
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadA | game::kPadWii2);
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(main::settings().general.frameAdvance));

    // Switch to Draft C (Rail): Sideways A also switches categories and draws "Category" and "Categories".
    main::settings().controls.menuStyle = main::kMenuStyleRail;
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("Category"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("Categories"));
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadWiiA);
    stepBoth(pads);
    CHECK_EQ(1, static_cast<int>(lab_uut::s_menu.category));

    // In Draft A Z-in-Pause mode, footer omits dead [+] Close, labels [-] and [1] Close,
    // and Sideways 1 closes directly (no rail focus step).
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadStart);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());
    main::settings().controls.menuStyle = main::kMenuStyleTabs;
    main::settings().controls.startOpensMenu = 0;
    host::fakes::match().paused = true;
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadWiiMinus);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    host::canvas::clear();
    lab_uut::onRender(0, host::fakes::match());
    CHECK(!canvasDrew("+"));
    CHECK(!canvasDrew("Brawl Pause"));
    CHECK(canvasDrew("-"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("Close"));
    setPad(pads[4], 0);
    stepBoth(pads);
    setPad(pads[4], game::kPadB | game::kPadWii1);
    stepBoth(pads);
    CHECK(!lab_uut::menuOpen());

    // Pre-holding R on Classic before opening menu and releasing within kRepeatStart
    // must NOT fire a phantom category switch.
    pads[4].m_controllerType = gfPadType::WII_CLASSIC;
    setPad(pads[4], game::kPadR);
    stepBoth(pads);
    setPad(pads[4], game::kPadR | game::kPadZ | game::kPadClassicZL);
    stepBoth(pads);
    CHECK(lab_uut::menuOpen());
    u8 catBefore = lab_uut::s_menu.category;
    setPad(pads[4], game::kPadR);
    stepBoth(pads);
    setPad(pads[4], 0);
    stepBoth(pads);
    CHECK_EQ(static_cast<int>(catBefore), static_cast<int>(lab_uut::s_menu.category));
    teardownLab();
}

TEST(wii_lab, fighter_pad_dpad_suppression_across_controllers_and_all_off)
{
    setupLab();
    main::settings().controls.dpadLeft = savestate::kLeftLoadState;
    main::settings().controls.dpadRight = savestate::kRightSaveState;
    main::settings().controls.dpadUp = main::kUpTaunt;
    main::settings().controls.dpadDown = main::kDownNone;

    const u32 kTypes[3] = {
        static_cast<u32>(gfPadType::GCC),
        static_cast<u32>(gfPadType::WII_CLASSIC),
        static_cast<u32>(gfPadType::NUNCHUK)
    };
    const u32 kPortsForType[3] = {0u, 4u, 4u};

    // 1. GCC, Classic, and Nunchuk strip savestate D-pad Left/Right from all 6 button words
    //    on fighterPad before ipPadConfig mapping, while leaving other buttons intact.
    for (int t = 0; t < 3; t++)
    {
        gfPadStatus pad;
        memset(&pad, 0, sizeof(pad));
        pad.m_error = gfPadError::NONE;
        pad.m_controllerType = static_cast<gfPadType::PadType>(kTypes[t]);
        u32 bits = game::kPadDLeft | game::kPadDRight | game::kPadA;
        setPad(pad, bits);
        pad.m_buttonsReleasedThisFrame.bits = bits;
        lab_uut::onFighterPad(kPortsForType[t], &pad);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsCurrentFrame.bits);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsCurrentFrame2.bits);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsHeld.bits);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsPressedThisFrame.bits);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsPressedThisFrame2.bits);
        CHECK_EQ(static_cast<u32>(game::kPadA), pad.m_buttonsReleasedThisFrame.bits);
    }

    // 2. Sideways Wii Remote: bare D-pad Left/Right stays intact (movement);
    //    chording '-' + D-pad Left clears both D-pad Left AND '-' on fighterPad;
    //    standalone '-' without a bound D-pad press stays intact.
    {
        gfPadStatus pad;
        memset(&pad, 0, sizeof(pad));
        pad.m_error = gfPadError::NONE;
        pad.m_controllerType = gfPadType::WIIMOTE;

        setPad(pad, game::kPadDLeft | game::kPadDRight);
        lab_uut::onFighterPad(4u, &pad);
        CHECK_EQ(static_cast<u32>(game::kPadDLeft | game::kPadDRight), pad.m_buttonsCurrentFrame2.bits);

        setPad(pad, game::kPadWiiMinus | game::kPadDLeft);
        lab_uut::onFighterPad(4u, &pad);
        CHECK_EQ(0u, pad.m_buttonsCurrentFrame.bits);
        CHECK_EQ(0u, pad.m_buttonsCurrentFrame2.bits);

        setPad(pad, game::kPadWiiMinus);
        lab_uut::onFighterPad(4u, &pad);
        CHECK_EQ(static_cast<u32>(game::kPadWiiMinus), pad.m_buttonsCurrentFrame2.bits);
    }

    // 3. Skill Drill D-pad bits (4u = Left/Right, 16u = Down) are suppressed on fighterPad
    //    even when user D-pad bindings are all OFF.
    main::settings().controls.dpadLeft = savestate::kLeftNone;
    main::settings().controls.dpadRight = savestate::kRightNone;
    main::settings().controls.dpadUp = main::kUpTaunt;
    main::settings().controls.dpadDown = main::kDownNone;
    {
        s_fakeDrillFlags = 4 | 16;
        gfPadStatus pad;
        memset(&pad, 0, sizeof(pad));
        pad.m_error = gfPadError::NONE;
        pad.m_controllerType = gfPadType::GCC;
        setPad(pad, game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp);
        lab_uut::onFighterPad(0u, &pad);
        CHECK_EQ(static_cast<u32>(game::kPadDUp), pad.m_buttonsCurrentFrame2.bits);
        s_fakeDrillFlags = 0;
    }

    // 4. All Lab/savestate/drill D-pad bindings OFF: every controller passes all 4 D-pad bits cleanly.
    const u32 kAllTypes[4] = {
        static_cast<u32>(gfPadType::GCC),
        static_cast<u32>(gfPadType::WII_CLASSIC),
        static_cast<u32>(gfPadType::NUNCHUK),
        static_cast<u32>(gfPadType::WIIMOTE)
    };
    const u32 kAllPorts[4] = {0u, 4u, 4u, 4u};
    const u32 kAllDpad = game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadDUp;
    for (int t = 0; t < 4; t++)
    {
        gfPadStatus pad;
        memset(&pad, 0, sizeof(pad));
        pad.m_error = gfPadError::NONE;
        pad.m_controllerType = static_cast<gfPadType::PadType>(kAllTypes[t]);
        u32 bits = kAllDpad | (kAllTypes[t] == static_cast<u32>(gfPadType::WIIMOTE) ? game::kPadWiiMinus : 0u);
        setPad(pad, bits);
        lab_uut::onFighterPad(kAllPorts[t], &pad);
        CHECK_EQ(bits, pad.m_buttonsCurrentFrame.bits);
        CHECK_EQ(bits, pad.m_buttonsCurrentFrame2.bits);
    }

    teardownLab();
}
