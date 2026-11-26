// Drill list port handling and Wii controller support (src/shared/game/pad_map.h,
// src/menu/game/training.cpp, src/menu/drill_list.cpp, src/menu/browser.cpp):
// - controllingPort() follows gmSetRule+0x24 across all 8 ports (GC 0-3, Wii
//   4-7), falls back to the first connected pad, and requires Wii ports to
//   report a valid Wii controller type (1-3);
// - any connected GC or Wii pad may open, operate, take over, close and reopen
//   the list and recordings browser using its controller-type button map;
// - inputs are hidden while open and held close buttons stay hidden until
//   released without masking other buttons.
#include <gf/gf_pad_status.h>
#include <gf/gf_scene.h>
#include <gm/gm_global.h>
#include <string.h>

#include "host/canvas.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "menu/drill_list.h"
#include "menu/game/select.h"
#include "menu/game/training.h"
#include "menu/osd_quick.h"
#include "menu/session.h"
#include "shared/game/pad_map.h"
#include "shared/interface.h"

using namespace tm;

namespace {
    enum {
        kButtonB = 0x200,
        kButtonA = 0x100,
        kButtonX = 0x400,
        kDpadDown = 0x4
    };

    void initPads(gfPadStatus pads[iface::kPadCount], u32 connectedMask,
                  u32 wiiType = static_cast<u32>(gfPadType::WIIMOTE))
    {
        menu::attachSession(host::fakes::block());
        memset(pads, 0, sizeof(gfPadStatus) * iface::kPadCount);
        for (int i = 0; i < iface::kPadCount; i++)
        {
            bool on = (connectedMask & (1u << i)) != 0;
            u32 type = i < 4 ? static_cast<u32>(gfPadType::GCC) : wiiType;
            pads[i].m_error = static_cast<s8>(on ? gfPadError::NONE : gfPadError::NO_CONTROLLER);
            pads[i].m_controllerType = static_cast<gfPadType::PadType>(type);
            host::fakes::setPadConnected(i, on);
            host::fakes::setPadType(i, type);
        }
    }

    void setPadType(gfPadStatus pads[iface::kPadCount], int port, u32 type)
    {
        pads[port].m_controllerType = static_cast<gfPadType::PadType>(type);
        host::fakes::setPadType(port, type);
    }

    void setPadInput(gfPadStatus& p, u32 buttons, s8 stickX = 0, s8 stickY = 0)
    {
        p.m_buttonsCurrentFrame.bits = buttons;
        p.m_buttonsCurrentFrame2.bits = buttons;
        p.m_buttonsHeld.bits = buttons;
        p.m_buttonsPressedThisFrame.bits = buttons;
        p.m_buttonsPressedThisFrame2.bits = buttons;
        p.m_stickX = stickX;
        p.m_stickY = stickY;
    }

    void stepFrame(gfPadStatus pads[iface::kPadCount])
    {
        menu::list::onPads(iface::kPadsSystem, pads);
    }

    void seedRecordingAndStartPath()
    {
        static const struct {
            u32 addr;
            u32 val;
        } kStartPath[] = {
            {0x806F1550u, 0x3BC00001u}, {0x806F1554u, 0x39C00007u}, {0x806F167Cu, 0x80110284u},
            {0x806F1680u, 0x2C000002u}, {0x806F16C0u, 0x93CF0008u}, {0x806F16C4u, 0x7DE37B78u},
            {0x806F16C8u, 0x91CF000Cu}, {0x806F16CCu, 0x9ACF0010u}, {0x806F16D0u, 0x48000279u},
            {0x807038C4u, 0x806F1628u}, {0x807038CCu, 0x806F167Cu}
        };
        for (u32 i = 0; i < sizeof(kStartPath) / sizeof(kStartPath[0]); i++)
        {
            host::mem::setWord(kStartPath[i].addr, kStartPath[i].val);
        }
        host::mem::setWord(0x80002810u, 0); // My Music flag off

        u8 rec[storage::kRecordingMinBytes];
        memset(rec, 0, sizeof(rec));
        storage::FileHeader* hdr = reinterpret_cast<storage::FileHeader*>(rec);
        hdr->magic = storage::kRecordingMagic;
        hdr->format = storage::kRecordingFormat;
        hdr->headerSize = sizeof(storage::FileHeader);
        hdr->size = storage::kRecordingMinBytes;
        reinterpret_cast<storage::RecordingHead*>(rec + storage::kRecordingListBytes)->placements =
            storage::kRecordingHeadBytes;
        storage::RecordingInfo* info = reinterpret_cast<storage::RecordingInfo*>(rec + sizeof(storage::FileHeader));
        strcpy(info->name, "260102-030405");
        info->hmnKind = 0x00;
        info->hmnIcon = 0x00;
        info->cpuKind = 0x07;
        info->cpuIcon = 0x07;
        info->stage = 0x02;
        hdr->checksum = storage::checksum(rec + storage::kChecksumFrom, hdr->size - storage::kChecksumFrom);
        host::card::put("260102-030405.tmr", rec, sizeof(rec));
    }
}

TEST(drill_list, controlling_port_supports_gc_and_wii_ports)
{
    gfPadStatus pads[iface::kPadCount];

    // Wii ports 4..7 return themselves when connected, even if GC port 0 is connected.
    initPads(pads, (1u << 0) | 0xF0u, gfPadType::WII_CLASSIC);
    for (u32 wii = 4; wii < 8; wii++)
    {
        host::fakes::setMenuPort(wii);
        CHECK_EQ(static_cast<int>(wii), menu::training::controllingPort());
    }

    // No GC pad connected (menu port defaulted to 0): falls back to connected Wii port 4.
    initPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setMenuPort(0);
    CHECK_EQ(4, menu::training::controllingPort());

    // Wii port with unidentified controllerType == 0 is treated as disconnected (0x806894D0).
    initPads(pads, 1u << 4, gfPadType::GCC);
    host::fakes::setMenuPort(4);
    CHECK_EQ(-1, menu::training::controllingPort());

    // No pad connected on any port 0..7: returns -1.
    initPads(pads, 0u);
    host::fakes::setMenuPort(0);
    CHECK_EQ(-1, menu::training::controllingPort());

    // Port 1 unplugged, port 3 (index 2) connected: falls back to port 3.
    initPads(pads, 1u << 2);
    host::fakes::setMenuPort(0);
    CHECK_EQ(2, menu::training::controllingPort());

    // Port 2 controlling and connected: returns port 2 (index 1).
    initPads(pads, (1u << 0) | (1u << 1));
    host::fakes::setMenuPort(1);
    CHECK_EQ(1, menu::training::controllingPort());
}

TEST(drill_list, wii_controlling_port_auto_opens_and_hides_wii_pad)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::WIIMOTE); // sideways Wii Remote 1 only
    host::fakes::setMenuPort(4);
    menu::list::attach();

    CHECK(host::log::contains("drills: list opened (auto) for port 5"));

    setPadInput(pads[4], game::kPadA | game::kPadWii2 | game::kPadDDown, 60, -70);
    stepFrame(pads);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame2.bits);
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickX));
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickY));
    menu::list::detach();
}

TEST(drill_list, no_connected_pad_does_not_auto_open_or_blank_pads)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 0u); // no GC or Wii pad connected
    host::fakes::setMenuPort(0);
    menu::list::attach();

    CHECK(!host::log::contains("drills: list opened"));
    CHECK(host::log::contains("no controlling pad"));
    setPadInput(pads[5], kButtonA, 40, 50);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(kButtonA), pads[5].m_buttonsCurrentFrame.bits);
    CHECK_EQ(40, static_cast<int>(pads[5].m_stickX));
    menu::list::detach();
}

TEST(drill_list, drill_done_with_wii_port_auto_opens)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::NUNCHUK);
    host::fakes::setMenuPort(4);
    menu::session().drill = 1;
    menu::session().drillRan = 1;
    menu::list::attach();

    CHECK(host::log::contains("drills: list opened (auto) for port 5"));
    CHECK_EQ(0, static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().drillRan));
    setPadInput(pads[4], game::kPadA | game::kPadWiiA);
    stepFrame(pads);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    menu::list::detach();
}

TEST(drill_list, any_connected_gcc_operates_list_when_port1_unplugged_and_wii_pad_hidden_while_open)
{
    gfPadStatus pads[iface::kPadCount];
    // Start with port 1, port 2, port 3, port 4 and Wii port 4 connected; menu on port 1.
    initPads(pads, 0x1Fu, gfPadType::WIIMOTE);
    host::fakes::setMenuPort(0);
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 1"));

    // Frame 1: GC ports neutral clears wait-release; Wii pad 4 is hidden while open.
    setPadInput(pads[4], game::kPadA | game::kPadWii2 | game::kPadDDown, 50, -50);
    stepFrame(pads);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickX));
    setPadInput(pads[4], 0);

    // Unplug port 1; port 2's first press claims ownership without acting on row 0.
    pads[0].m_error = gfPadError::NO_CONTROLLER;
    setPadInput(pads[1], kButtonB);
    stepFrame(pads);
    CHECK_EQ(0u, pads[1].m_buttonsCurrentFrame.bits); // GC pad hidden while list is open
    CHECK(!host::log::contains("drills: list closed"));

    // Port 2 goes neutral, switches to Offense (R), then moves down to row 1 (Techchase).
    setPadInput(pads[1], 0);
    stepFrame(pads);
    setPadInput(pads[1], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Offense"));
    setPadInput(pads[1], 0);
    stepFrame(pads);
    setPadInput(pads[1], kDpadDown);
    stepFrame(pads);
    CHECK(host::log::contains("drills: row 1 "));

    // Port 2 goes neutral (idle); port 3 claims ownership without acting, then moves back up to row 0.
    setPadInput(pads[1], 0);
    setPadInput(pads[2], game::kPadDUp);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: row 0 "));
    setPadInput(pads[2], 0);
    stepFrame(pads);
    setPadInput(pads[2], game::kPadDUp);
    stepFrame(pads);
    CHECK(host::log::contains("drills: row 0 "));

    // Port 3 goes neutral (idle); port 4 claims ownership without closing, then closes with B.
    setPadInput(pads[2], 0);
    setPadInput(pads[3], kButtonB);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list closed"));
    setPadInput(pads[3], 0);
    stepFrame(pads);
    setPadInput(pads[3], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (B)"));

    // Once closed, Wii pad 4 inputs pass through to the character select untouched.
    setPadInput(pads[3], 0);
    setPadInput(pads[4], game::kPadA | game::kPadWii2, 0, 0);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(game::kPadA | game::kPadWii2), pads[4].m_buttonsCurrentFrame.bits);
    menu::list::detach();
}

TEST(drill_list, active_owner_cannot_be_stolen_by_another_port)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 0x0Fu);
    host::fakes::setMenuPort(0);
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 1"));

    // Neutral frame to clear wait-release on all ports, then switch to Offense (5 rows).
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Offense"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Port 1 holds Down (active); port 2 presses B and must NOT steal or close the list.
    setPadInput(pads[0], kDpadDown);
    setPadInput(pads[1], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: row 1 "));
    CHECK(!host::log::contains("drills: list closed"));
    CHECK(!host::log::contains("drills: list handed to port 2"));

    // Port 2 goes neutral while port 1 is still active, then presses B again: still refused.
    setPadInput(pads[0], kDpadDown);
    setPadInput(pads[1], 0);
    stepFrame(pads);
    setPadInput(pads[0], kDpadDown);
    setPadInput(pads[1], kButtonB);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list closed"));
    CHECK(!host::log::contains("drills: list handed to port 2"));

    // Port 1 returns to neutral (idle), port 2 returns to neutral, then port 2 takes over and closes.
    setPadInput(pads[0], 0);
    setPadInput(pads[1], 0);
    stepFrame(pads);
    setPadInput(pads[1], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list handed to port 2"));
    CHECK(!host::log::contains("drills: list closed"));
    setPadInput(pads[1], 0);
    stepFrame(pads);
    setPadInput(pads[1], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (B)"));
    menu::list::detach();
}

TEST(drill_list, browser_load_uses_new_owner_port_after_takeover)
{
    // Seed sqTraining::setNext words checked by training::startMatch() and a
    // minimal valid recording on the fake SD card.
    static const struct {
        u32 addr;
        u32 val;
    } kStartPath[] = {
        {0x806F1550u, 0x3BC00001u}, {0x806F1554u, 0x39C00007u}, {0x806F167Cu, 0x80110284u},
        {0x806F1680u, 0x2C000002u}, {0x806F16C0u, 0x93CF0008u}, {0x806F16C4u, 0x7DE37B78u},
        {0x806F16C8u, 0x91CF000Cu}, {0x806F16CCu, 0x9ACF0010u}, {0x806F16D0u, 0x48000279u},
        {0x807038C4u, 0x806F1628u}, {0x807038CCu, 0x806F167Cu}
    };
    for (u32 i = 0; i < sizeof(kStartPath) / sizeof(kStartPath[0]); i++)
    {
        host::mem::setWord(kStartPath[i].addr, kStartPath[i].val);
    }
    host::mem::setWord(0x80002810u, 0); // My Music flag off

    // (The smallest recording the browser accepts: both headers and two placements.)
    u8 rec[storage::kRecordingMinBytes];
    memset(rec, 0, sizeof(rec));
    storage::FileHeader* hdr = reinterpret_cast<storage::FileHeader*>(rec);
    hdr->magic = storage::kRecordingMagic;
    hdr->format = storage::kRecordingFormat;
    hdr->headerSize = sizeof(storage::FileHeader);
    hdr->size = storage::kRecordingMinBytes;
    reinterpret_cast<storage::RecordingHead*>(rec + storage::kRecordingListBytes)->placements =
        storage::kRecordingHeadBytes;
    storage::RecordingInfo* info = reinterpret_cast<storage::RecordingInfo*>(rec + sizeof(storage::FileHeader));
    strcpy(info->name, "260102-030405");
    info->hmnKind = 0x00;
    info->hmnIcon = 0x00;
    info->cpuKind = 0x07;
    info->cpuIcon = 0x07;
    info->stage = 0x02;
    hdr->checksum = storage::checksum(rec + storage::kChecksumFrom, hdr->size - storage::kChecksumFrom);
    host::card::put("260102-030405.tmr", rec, sizeof(rec));

    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 0x03u); // ports 1 and 2 connected
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads); // neutral frame clears wait-release

    // Port 1 opens the recordings browser (caches port 0 on open).
    setPadInput(pads[0], kButtonX);
    stepFrame(pads);
    CHECK(host::log::contains("browser: opened for port 1"));
    setPadInput(pads[0], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }

    // Unplug port 1 while the browser is open; port 2 takes over and loads the recording.
    pads[0].m_error = gfPadError::NO_CONTROLLER;
    setPadInput(pads[1], kButtonA); // takeover press: claims ownership without acting
    stepFrame(pads);
    CHECK(host::log::contains("drills: list handed to port 2"));
    CHECK(!host::log::contains("browser: starting"));

    setPadInput(pads[1], 0); // neutral clears wait-release
    stepFrame(pads);
    setPadInput(pads[1], kButtonA); // port 2 loads the recording
    stepFrame(pads);
    setPadInput(pads[1], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }

    CHECK(host::log::contains("browser: starting 260102-030405.tmr"));
    // gmPlayerInitData+7 is controller (port + 1): port 2 must be 2, not stale port 1 (1).
    CHECK_EQ(2, static_cast<int>(g_GameGlobal->m_selCharData->raw[0xB8 + 7]));
    menu::list::detach();
}

TEST(drill_list, pad_map_buttons_and_reopen)
{
    const game::PadMap& gcc = game::padMap(gfPadType::GCC);
    const game::PadMap& cls = game::padMap(gfPadType::WII_CLASSIC);
    const game::PadMap& wii = game::padMap(gfPadType::WIIMOTE);
    const game::PadMap& nun = game::padMap(gfPadType::NUNCHUK);

    CHECK(strcmp(gcc.reopenHint, "L+R = Training Menu") == 0);
    CHECK(strcmp(cls.reopenHint, "L+R = Training Menu") == 0);
    CHECK(strcmp(wii.reopenHint, "- + B = Training Menu") == 0);
    CHECK(strcmp(nun.reopenHint, "C+Z = Training Menu") == 0);

    // Sideways B trigger sets 0x400040 (including kPadL 0x40) and must not match pagePrev.
    CHECK_EQ(0u, wii.pagePrev & game::kPadL);
    CHECK_EQ(0u, wii.pagePrev & game::kPadWiiB);

    // Classic ZL/ZR map to pagePrev/pageNext without colliding on shared kPadZ (0x10).
    CHECK((cls.pagePrev & game::kPadClassicZL) != 0);
    CHECK((cls.pageNext & game::kPadClassicZR) != 0);
    CHECK_EQ(0u, cls.pagePrev & game::kPadZ);
    CHECK_EQ(0u, cls.pageNext & game::kPadZ);

    // Reopen combos require both buttons held, a fresh edge on at least one, and !D-Down.
    CHECK(game::reopenPressed(gcc, game::kPadL | game::kPadR, game::kPadR));
    CHECK(!game::reopenPressed(gcc, game::kPadL | game::kPadR | game::kPadDDown, game::kPadR));
    CHECK(!game::reopenPressed(gcc, game::kPadL | game::kPadR, 0));
    CHECK(!game::reopenPressed(gcc, game::kPadL, game::kPadL));
    CHECK(!game::reopenPressed(wii, game::kPadWiiMinus, game::kPadWiiMinus));
    CHECK(!game::reopenPressed(wii, game::kPadWiiB | game::kPadL, game::kPadWiiB | game::kPadL));
    CHECK(game::reopenPressed(wii, game::kPadWiiMinus | game::kPadWiiB | game::kPadL, game::kPadWiiB | game::kPadL));
    CHECK(!game::reopenPressed(wii, game::kPadWiiMinus | game::kPadWiiB | game::kPadL | game::kPadDDown,
                               game::kPadWiiB | game::kPadL));
    CHECK(!game::reopenPressed(wii, game::kPadWiiMinus | game::kPadWiiB | game::kPadL, 0));
    CHECK(game::reopenPressed(nun, game::kPadX | game::kPadNunchukC | game::kPadZ, game::kPadZ));
    CHECK(!game::reopenPressed(nun, game::kPadX | game::kPadNunchukC | game::kPadZ | game::kPadDDown, game::kPadZ));
    CHECK(!game::reopenPressed(nun, game::kPadX | game::kPadNunchukC, game::kPadX | game::kPadNunchukC));
}

TEST(drill_list, sideways_and_nunchuk_actions_reopen_and_selective_hiding)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setMenuPort(4);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 5"));
    stepFrame(pads); // neutral clears wait-release

    // Sideways B trigger (0x400040) must not close or act on the list.
    setPadInput(pads[4], game::kPadL | game::kPadWiiB);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list closed"));
    CHECK(!host::log::contains("drills: exiting to Solo"));
    setPadInput(pads[4], 0);
    stepFrame(pads);

    // Sideways 2 (0x20100) selects Training Lab, closes to CSS, and hides both bits of 2
    // until released without hiding another button (1 = 0x10200) pressed after closing.
    setPadInput(pads[4], game::kPadA | game::kPadWii2);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (Training Lab)"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // While 2 is still held and 1 is newly pressed (and CSS back gate is closed): 2 stays hidden, 1 passes through.
    setPadInput(pads[4], game::kPadA | game::kPadWii2 | game::kPadB | game::kPadWii1);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(game::kPadB | game::kPadWii1), pads[4].m_buttonsCurrentFrame.bits);

    // Releasing 2 clears its mask; pressing - alone neither opens the list nor
    // hides - from the game (P+ reads - for special fighters at match start).
    setPadInput(pads[4], 0);
    stepFrame(pads);
    setPadInput(pads[4], game::kPadWiiMinus);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list opened (- + B)"));
    CHECK_EQ(static_cast<u32>(game::kPadWiiMinus), pads[4].m_buttonsCurrentFrame.bits);

    // Holding - then pressing B opens the list and hides B on that frame so CSS
    // does not step the costume backward.
    setPadInput(pads[4], game::kPadWiiMinus | game::kPadWiiB | game::kPadL);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (- + B) for port 5"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // Hot-swap to Nunchuk while a button is held: wait-release resets so no phantom action fires.
    setPadType(pads, 4, gfPadType::NUNCHUK);
    setPadInput(pads[4], game::kPadA | game::kPadWiiA);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: exiting to Solo"));
    setPadInput(pads[4], 0);
    stepFrame(pads);

    // Nunchuk A selects the highlighted row with stick held (down moves 0 -> 1 L-Cancel);
    // stick stays hidden until neutral.
    host::log::clear();
    setPadInput(pads[4], game::kPadA | game::kPadWiiA, 0, -80);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (L-Cancel)"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickY));
    setPadInput(pads[4], 0, 0, -80);
    stepFrame(pads);
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickY));
    setPadInput(pads[4], 0, 0, 0);
    stepFrame(pads);
    setPadInput(pads[4], 0, 0, -80);
    stepFrame(pads);
    CHECK_EQ(-80, static_cast<int>(pads[4].m_stickY));

    // Nunchuk C alone does not reopen; C+Z reopens and hides both buttons.
    setPadInput(pads[4], game::kPadX | game::kPadNunchukC);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list opened (C+Z)"));
    setPadInput(pads[4], game::kPadX | game::kPadNunchukC | game::kPadZ);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (C+Z) for port 5"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    menu::list::detach();
}

TEST(drill_list, reopen_hint_prefers_gc_then_present_wii_type)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setMenuPort(4);
    menu::list::attach();
    stepFrame(pads);
    CHECK(strcmp(menu::list::reopenHint(), "- + B = Training Menu") == 0);

    // Switching the sole Wii pad to Nunchuk updates the hint to C+Z.
    setPadType(pads, 4, gfPadType::NUNCHUK);
    stepFrame(pads);
    CHECK(strcmp(menu::list::reopenHint(), "C+Z = Training Menu") == 0);

    // Classic Controller alone reports L+R.
    setPadType(pads, 4, gfPadType::WII_CLASSIC);
    stepFrame(pads);
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

    // Sideways Wii Remote + GC pad together: GameCube hint wins.
    setPadType(pads, 4, gfPadType::WIIMOTE);
    pads[0].m_error = gfPadError::NONE;
    host::fakes::setPadConnected(0, true);
    stepFrame(pads);
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

    // Unplug GC pad: back to sideways hint.
    pads[0].m_error = gfPadError::NO_CONTROLLER;
    host::fakes::setPadConnected(0, false);
    stepFrame(pads);
    CHECK(strcmp(menu::list::reopenHint(), "- + B = Training Menu") == 0);
    menu::list::detach();
}

TEST(drill_list, wii_browser_load_writes_wii_controller_slot)
{
    seedRecordingAndStartPath();

    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::WIIMOTE); // sideways Wii Remote on port 5 (index 4)
    host::fakes::setMenuPort(4);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads); // neutral clears wait-release

    // Sideways A (0x200000) opens the recordings browser.
    setPadInput(pads[4], game::kPadWiiA);
    stepFrame(pads);
    CHECK(host::log::contains("browser: opened for port 5"));
    setPadInput(pads[4], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }

    // Sideways 2 (0x20100) loads the recording for port 5.
    setPadInput(pads[4], game::kPadA | game::kPadWii2);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }

    CHECK(host::log::contains("browser: starting 260102-030405.tmr"));
    // gmPlayerInitData+7 is controller (port + 1): port 5 (index 4) must be 5.
    CHECK_EQ(5, static_cast<int>(g_GameGlobal->m_selCharData->raw[0xB8 + 7]));
    menu::list::detach();
}

namespace {
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

TEST(drill_list, draw_pending_shows_css_mode_pill_and_reopen_works)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::NUNCHUK);
    host::fakes::setMenuPort(4);
    host::fakes::setTrainingSelectRunning(true);
    menu::session().labChosen = 1; // list stays closed so onRender runs drawPending()
    menu::list::attach();
    stepFrame(pads);

    // Brawl-styled CSS mode pill shows active mode + controller-appropriate reopen badges.
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(host::canvas::runs() >= 4);
    CHECK(host::canvas::rects() >= 4);
    CHECK(canvasDrew("TRAINING LAB"));
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("C+Z"));
    CHECK(canvasDrew("Modes"));
    CHECK(strcmp(menu::list::reopenHint(), "C+Z = Training Menu") == 0);

    setPadType(pads, 4, gfPadType::WIIMOTE);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("TRAINING LAB"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("- + B"));
    CHECK(canvasDrew("Modes"));
    CHECK(strcmp(menu::list::reopenHint(), "- + B = Training Menu") == 0);

    setPadType(pads, 4, gfPadType::WII_CLASSIC);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("L+R"));
    CHECK(canvasDrew("Modes"));
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

    pads[0].m_error = gfPadError::NONE;
    host::fakes::setPadConnected(0, true);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("L+R"));
    CHECK(canvasDrew("Modes"));
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

    // Armed tech practice shows its own name in the CSS mode pill.
    menu::session().drill = drills::kDrillDacus;
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("DACUS"));
    menu::session().drill = drills::kDrillNone;

    // L+R+D-Down must NOT reopen (never collides with P+ Code Menu).
    setPadInput(pads[0], game::kPadL | game::kPadR | game::kPadDDown);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list opened (L+R)"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Reopen combo (GC L+R) still opens the menu cleanly.
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (L+R) for port 1"));
    menu::list::detach();
}

TEST(drill_list, unidentified_wii_port_does_not_auto_open_or_blank)
{
    gfPadStatus pads[iface::kPadCount];
    // Wii port 4 with m_error == NONE but m_controllerType == 0 (unidentified).
    initPads(pads, 1u << 4, gfPadType::GCC);
    host::fakes::setMenuPort(4);
    menu::list::attach();

    CHECK(!host::log::contains("drills: list opened"));
    CHECK(host::log::contains("no controlling pad"));

    setPadInput(pads[4], game::kPadA | game::kPadDDown, 60, -70);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(game::kPadA | game::kPadDDown), pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(60, static_cast<int>(pads[4].m_stickX));
    CHECK_EQ(-70, static_cast<int>(pads[4].m_stickY));
    menu::list::detach();
}

TEST(drill_list, wii_disconnect_clears_release_mask_and_stick_wait)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::NUNCHUK);
    host::fakes::setMenuPort(4);
    menu::list::attach();
    stepFrame(pads); // neutral clears wait-release

    // Close the list with B held and stick tilted: both stay hidden until released.
    setPadInput(pads[4], game::kPadB | game::kPadWiiB, 0, -80);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (B)"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(0, static_cast<int>(pads[4].m_stickY));

    // Disconnect port 4 for one frame while release mask and stick wait are set.
    pads[4].m_error = gfPadError::NO_CONTROLLER;
    host::fakes::setPadConnected(4, false);
    setPadInput(pads[4], game::kPadB | game::kPadWiiB, 0, -80);
    stepFrame(pads);

    // Reconnect port 4 holding the same button and stick: neither mask nor stick
    // wait may have stuck across the disconnect.
    pads[4].m_error = gfPadError::NONE;
    host::fakes::setPadConnected(4, true);
    setPadInput(pads[4], game::kPadB | game::kPadWiiB, 0, -80);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(game::kPadB | game::kPadWiiB), pads[4].m_buttonsCurrentFrame.bits);
    CHECK_EQ(-80, static_cast<int>(pads[4].m_stickY));
    menu::list::detach();
}

TEST(drill_list, training_start_match_writes_substage_zero_and_hazard_off_byte)
{
    seedRecordingAndStartPath();
    host::fakes::setTrainingSelectRunning(true);

    // Pre-fill +0x24 and +0x25 with non-zero bytes so defaults must actively clear them.
    u8* stageData = reinterpret_cast<u8*>(g_GameGlobal->m_selStageData);
    stageData[0x24] = 0xAA;
    stageData[0x25] = 0xBB;

    menu::training::MatchSetup setup = {0x00, 0x00, 0x07, 0x07, 0x21};
    const char* why = menu::training::startMatch(setup, 0, NULL);
    CHECK(why == NULL);
    CHECK_EQ(0, static_cast<int>(stageData[0x24]));
    CHECK_EQ(0, static_cast<int>(stageData[0x25]));

    // Explicit Looks with hazardOff = 1 writes +0x24 = 0, +0x25 = 1.
    host::fakes::reset();
    seedRecordingAndStartPath();
    host::fakes::setTrainingSelectRunning(true);
    stageData = reinterpret_cast<u8*>(g_GameGlobal->m_selStageData);
    stageData[0x24] = 0xAA;
    stageData[0x25] = 0x00;

    menu::training::Looks looks = {0, 0, 0, 0, 0x0400, 1};
    why = menu::training::startMatch(setup, 0, &looks);
    CHECK(why == NULL);
    CHECK_EQ(0, static_cast<int>(stageData[0x24]));
    CHECK_EQ(1, static_cast<int>(stageData[0x25]));
}

TEST(drill_list, browser_load_passes_recording_hazard_off_to_stage_data)
{
    for (int haz = 0; haz <= 1; haz++)
    {
        host::fakes::reset();
        seedRecordingAndStartPath();
        // Rewrite the seeded recording with hazardOff = haz.
        u8 rec[storage::kRecordingMinBytes];
        memcpy(rec, host::card::data("260102-030405.tmr"), sizeof(rec));
        storage::FileHeader* hdr = reinterpret_cast<storage::FileHeader*>(rec);
        storage::RecordingInfo* info = reinterpret_cast<storage::RecordingInfo*>(rec + sizeof(storage::FileHeader));
        info->stage = 0x21;
        info->hazardOff = static_cast<u8>(haz);
        hdr->checksum = storage::checksum(rec + storage::kChecksumFrom, hdr->size - storage::kChecksumFrom);
        host::card::put("260102-030405.tmr", rec, sizeof(rec));

        u8* stageData = reinterpret_cast<u8*>(g_GameGlobal->m_selStageData);
        stageData[0x24] = 0xCC;
        stageData[0x25] = 0xDD;

        gfPadStatus pads[iface::kPadCount];
        initPads(pads, 0x01u);
        host::fakes::setMenuPort(0);
        host::fakes::setTrainingSelectRunning(true);
        menu::list::attach();
        stepFrame(pads);

        setPadInput(pads[0], kButtonX);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        for (int i = 0; i < 4; i++)
        {
            menu::list::onRender(0);
            stepFrame(pads);
        }

        setPadInput(pads[0], kButtonA);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        for (int i = 0; i < 6; i++)
        {
            menu::list::onRender(0);
            stepFrame(pads);
        }

        CHECK_EQ(0, static_cast<int>(stageData[0x24]));
        CHECK_EQ(haz, static_cast<int>(stageData[0x25]));
        menu::list::detach();
    }
}

TEST(drill_list, per_controller_footer_and_browser_labels)
{
    seedRecordingAndStartPath();
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, (1u << 0) | (1u << 4), gfPadType::WIIMOTE);
    host::fakes::setMenuPort(0);
    menu::list::attach();
    stepFrame(pads); // neutral clears wait-release

    // GC owner draws GC footer strip, All Modes section & L/R banner pills.
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("TRAINING MODE"));
    CHECK(canvasDrew("All Modes"));
    CHECK(canvasDrew("L"));
    CHECK(canvasDrew("R"));
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("Select"));
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("Back"));
    CHECK(canvasDrew("X"));
    CHECK(canvasDrew("Recordings"));
    CHECK(canvasDrew("Y"));
    CHECK(canvasDrew("Overlays"));
    CHECK(canvasDrew("L/R"));
    CHECK(canvasDrew("Section"));

    // Sideways Wii Remote takes over -> draws Sideways footer & -/+ banner pills immediately.
    setPadInput(pads[4], game::kPadWiiA);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("-"));
    CHECK(canvasDrew("+"));
    CHECK(canvasDrew("2"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("-/+"));

    // Open recordings browser on Sideways -> Sideways badges, page pills, delete confirm.
    setPadInput(pads[4], game::kPadWiiA);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("2"));
    CHECK(canvasDrew("Load"));
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("Delete"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("Back"));
    CHECK(canvasDrew("-"));
    CHECK(canvasDrew("Page 1/1"));
    CHECK(canvasDrew("+"));

    // Press Sideways A to ask delete -> confirm footer shows [2] Delete  [1] Cancel.
    setPadInput(pads[4], game::kPadWiiA);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("2"));
    CHECK(canvasDrew("Delete"));
    CHECK(canvasDrew("1"));
    CHECK(canvasDrew("Cancel"));

    // Cancel delete and switch to Nunchuk -> Nunchuk browser badges & page pills.
    setPadInput(pads[4], game::kPadB | game::kPadWii1);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    setPadType(pads, 4, gfPadType::NUNCHUK);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("Load"));
    CHECK(canvasDrew("C"));
    CHECK(canvasDrew("Delete"));
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("Back"));
    CHECK(canvasDrew("-"));
    CHECK(canvasDrew("Page 1/1"));
    CHECK(canvasDrew("+"));

    // Leave browser with Nunchuk B -> boot menu shows Nunchuk footer & -/+ pills.
    setPadInput(pads[4], game::kPadB | game::kPadWiiB);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("A"));
    CHECK(canvasDrew("B"));
    CHECK(canvasDrew("C"));
    CHECK(canvasDrew("-/+"));
    menu::list::detach();
}

TEST(drill_list, scenarios_page_navigation_pending_handoff_and_section_cycling)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, (1u << 0) | (1u << 4), gfPadType::WII_CLASSIC);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads); // neutral clears wait-release

    // Starts on Page 0 (All Modes, 27 rows: Lab -> Core Fundamentals ->
    // Defensive & Situational -> P+ Advanced Tech -> P+ Challenges ->
    // Character Trainers -> Browse Recordings).
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));

    // Step through All Modes (28 rows) and verify intuitive ordering & recommendation badges.
    for (int i = 0; i < 27; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: row 1 L-Cancel"));
    CHECK(host::log::contains("drills: row 2 Wavedash / Waveland"));
    CHECK(host::log::contains("drills: row 3 Ledgedash"));
    CHECK(host::log::contains("drills: row 4 Combo Lab"));
    CHECK(host::log::contains("drills: row 8 OoS Reversal - Defense"));
    CHECK(host::log::contains("drills: row 9 Powershield"));
    CHECK(host::log::contains("drills: row 12 Slide-Off"));
    CHECK(host::log::contains("drills: row 14 P+ Multi-Hit SDI"));
    CHECK(host::log::contains("drills: row 15 RAR"));
    CHECK(host::log::contains("drills: row 16 DACUS"));
    CHECK(host::log::contains("drills: row 17 B-Reverse / Wavebounce"));
    CHECK(host::log::contains("drills: row 18 Float / Hover & DJC"));
    CHECK(host::log::contains("drills: row 19 Cancel Practice: Endurance"));
    CHECK(host::log::contains("drills: row 25 Break the Targets!"));
    CHECK(host::log::contains("drills: row 26 Snake Infinite Recovery"));
    CHECK(host::log::contains("drills: row 27 Browse Recordings"));

    // Select row 27 (Browse Recordings) -> opens browser, B returns to Boot Menu.
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK(host::log::contains("browser: opened for port 1"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("browser: closed"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // GC R cycles: All Modes -> Offense -> Defense -> Tech Practice -> All Modes.
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Offense"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Move down to row 3 in Offense (Shield Pressure - Offense, presetId = 4) and select it.
    for (int i = 0; i < 3; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: row 3 Shield Pressure - Offense"));
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK(host::log::contains("drills: scenario 4 (Shield Pressure - Offense) armed"));
    CHECK(host::log::contains("drills: list closed (Shield Pressure - Offense)"));
    CHECK_EQ(4, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(1, static_cast<int>(menu::session().page));
    CHECK_EQ(3, static_cast<int>(menu::session().row));
    CHECK_EQ(1, static_cast<int>(menu::session().labChosen));

    // Reopen with L+R: remembers Page 1 (Offense), Row 3 (Shield Pressure - Offense).
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (L+R) for port 1, row 3 Shield Pressure - Offense"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Cycle R -> Defense (Page 2, 7 rows) and move to bottom row 6 (P+ Multi-Hit SDI, presetId = 9).
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Defense"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    for (int i = 0; i < 6; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: row 6 P+ Multi-Hit SDI"));
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(9, static_cast<int>(menu::session().pendingScenario));
    CHECK(host::log::contains("drills: scenario 9 (P+ Multi-Hit SDI) armed"));

    // Reopen, cycle R -> Tech Practice (Page 3) -> Challenges (Page 4) -> Character Trainers (Page 5)
    // -> All Modes (Page 0), select row 0 (Training Lab) -> clears pendingScenario to 0!
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Tech Practice"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Challenges"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Character Trainers"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page All Modes"));
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));
    CHECK(host::log::contains("drills: list closed (Training Lab)"));

    // Re-arm a scenario (Offense row 0 = Combo Lab, presetId = 1), then detach with exit code 1 vs 0.
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(1, static_cast<int>(menu::session().pendingScenario));
    // Exiting into a match (exitCode == 1) keeps pendingScenario armed for extra::scenarioPresetsAttach().
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    CHECK_EQ(1, static_cast<int>(menu::session().pendingScenario));

    // Exiting Training CSS back to Solo menu (exitCode == 0) clears pendingScenario to 0.
    menu::list::attach();
    gfSceneManager::getInstance()->unk1 = 0;
    menu::list::detach();
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));

    // Verify section cycling works on Classic, Sideways Wii Remote, and Nunchuk.
    static const struct {
        u32 type;
        u32 nextBtn;
        u32 prevBtn;
    } kWiiCycles[] = {
        {gfPadType::WII_CLASSIC, game::kPadR, game::kPadL},
        {gfPadType::WIIMOTE, game::kPadDRight, game::kPadDLeft},
        {gfPadType::NUNCHUK, game::kPadStart, game::kPadWiiMinus},
    };
    for (u32 c = 0; c < sizeof(kWiiCycles) / sizeof(kWiiCycles[0]); c++)
    {
        host::fakes::reset();
        host::log::clear();
        initPads(pads, 1u << 4, kWiiCycles[c].type);
        host::fakes::setMenuPort(4);
        host::fakes::setTrainingSelectRunning(true);
        menu::list::attach();
        stepFrame(pads);

        setPadInput(pads[4], kWiiCycles[c].nextBtn);
        stepFrame(pads);
        CHECK(host::log::contains("drills: page Offense"));
        setPadInput(pads[4], 0);
        stepFrame(pads);

        setPadInput(pads[4], kWiiCycles[c].nextBtn);
        stepFrame(pads);
        CHECK(host::log::contains("drills: page Defense"));
        setPadInput(pads[4], 0);
        stepFrame(pads);

        setPadInput(pads[4], kWiiCycles[c].prevBtn);
        stepFrame(pads);
        setPadInput(pads[4], 0);
        stepFrame(pads);

        setPadInput(pads[4], kWiiCycles[c].prevBtn);
        stepFrame(pads);
        CHECK(host::log::contains("drills: page All Modes"));
        menu::list::detach();
    }
}

TEST(drill_list, skill_drills_section_arms_drill_and_shows_personal_best)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    // 3x R from All Modes reaches Page 3 (Tech Practice, 8 single-page rows).
    for (int p = 0; p < 3; p++)
    {
        setPadInput(pads[0], game::kPadR);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: page Tech Practice"));

    // Details card shows Best Streak placeholder when best == 0.
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("Tech Practice"));
    CHECK(canvasDrew("L-Cancel"));
    CHECK(canvasDrew("Best Streak"));
    CHECK(canvasDrew("--"));

    // Move down to Ledgedash (row 2 of Tech Practice) and select it -> arms drill 1, clears scenario, closes list.
    for (int i = 0; i < 2; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK(host::log::contains("drills: tech practice 1 (Ledgedash) armed"));
    CHECK_EQ(static_cast<int>(drills::kDrillLedgedash), static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(0, static_cast<int>(menu::session().drillRan));
    CHECK_EQ(3, static_cast<int>(menu::session().page));
    CHECK_EQ(2, static_cast<int>(menu::session().row));

    // Detach into match (exitCode == 1) preserves armed drill for extra/drill_hud.cpp.
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    CHECK_EQ(static_cast<int>(drills::kDrillLedgedash), static_cast<int>(menu::session().drill));

    // Simulate match finishing with best streak 8 -> returning to CSS auto-opens launcher
    // on Tech Practice row 2 showing "Best: 8" and clears drill/drillRan.
    menu::session().drillRan = 1;
    menu::session().best[drills::kDrillLedgedash] = 8;
    host::log::clear();
    menu::attachSession(host::fakes::block());
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 1, row 2 Ledgedash"));
    CHECK_EQ(static_cast<int>(drills::kDrillNone), static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().drillRan));

    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("8"));
    menu::list::detach();
}

TEST(drill_list, tech_practice_eight_rows_and_recommendation_badges)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    // Verify all 7 universal Tech Practice rows arm their exact DrillId on one page without scrolling.
    for (int p = 0; p < 3; p++)
    {
        setPadInput(pads[0], game::kPadR);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }

    static const u8 kExpectedDrills[7] = {
        drills::kDrillLCancel,    drills::kDrillWavedash, drills::kDrillLedgedash, drills::kDrillPowershield,
        drills::kDrillRar,        drills::kDrillDacus,    drills::kDrillBReverse
    };
    for (int r = 0; r < 7; r++)
    {
        if (r > 0)
        {
            setPadInput(pads[0], game::kPadL | game::kPadR);
            stepFrame(pads);
            setPadInput(pads[0], 0);
            stepFrame(pads);
            setPadInput(pads[0], kDpadDown);
            stepFrame(pads);
            setPadInput(pads[0], 0);
            stepFrame(pads);
        }
        if (r == 3)
        {
            host::canvas::clear();
            menu::list::onRender(0);
            CHECK(canvasDrew("Projectile CPU"));
        }
        setPadInput(pads[0], kButtonA);
        stepFrame(pads);
        CHECK_EQ(static_cast<int>(kExpectedDrills[r]), static_cast<int>(menu::session().drill));
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }

    // Verify recommendation chips on Defense scenarios (Shield Drop, Ledgetech, Slide-Off).
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL); // Tech Practice -> Defense
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    // Defense rows 2..4 are Shield Drop, Ledgetech, Slide-Off.
    for (int s = 0; s < 2; s++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    static const char* const kDefenseBadges[3] = {"Platform Stage", "Wall Stage", "Platform Stage / PS2"};
    for (int i = 0; i < 3; i++)
    {
        host::canvas::clear();
        menu::list::onRender(0);
        CHECK(canvasDrew(kDefenseBadges[i]));
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    menu::list::detach();
}

TEST(drill_list, natural_b_hierarchy_boot_to_solo_and_css_coin_safe_back)
{
    // Pure truth-table check of select::canBackFromCss().
    menu::select::CssBackState st = {true, true, 1, 0, true, true, false, false};
    CHECK(menu::select::canBackFromCss(st, 0));
    CHECK(!menu::select::canBackFromCss(st, 1)); // wrong port
    st.controllerPort = -1;
    CHECK(menu::select::canBackFromCss(st, 4)); // unmapped port allowed (e.g. Wii)
    st.controllerPort = 4;
    CHECK(menu::select::canBackFromCss(st, 4));  // Wii P1 port match
    CHECK(!menu::select::canBackFromCss(st, 0)); // GC P1 rejected when P1 is Wii P1
    st.controllerPort = 0;
    st.ownCoinPlaced = true;
    CHECK(!menu::select::canBackFromCss(st, 0)); // B recalls P1's placed coin
    st.ownCoinPlaced = false;
    st.handHoldingOwn = false;
    st.handHoldingAny = true;
    CHECK(!menu::select::canBackFromCss(st, 0)); // dragging CPU's coin
    st.handHoldingOwn = true;
    st.anyDropdownOpen = true;
    CHECK(!menu::select::canBackFromCss(st, 0)); // tag or CPU slider open
    st.anyDropdownOpen = false;
    st.playerKind = 0;
    CHECK(!menu::select::canBackFromCss(st, 0)); // not human
    st.playerKind = 1;
    st.selectRunning = false;
    CHECK(!menu::select::canBackFromCss(st, 0));

    // Seed sqTraining::setNext exit-to-Solo words verified by training::exitToSolo().
    host::mem::setWord(0x806F154Cu, 0x3B40000Bu);
    host::mem::setWord(0x806F1628u, 0x80110284u);
    host::mem::setWord(0x806F1644u, 0x934F0008u);
    host::mem::setWord(0x807038C4u, 0x806F1628u);

    // Live Boot Menu B exits cleanly to Solo (m->unk1 = 0, m->processStep = 2).
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    setPadInput(pads[0], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: exiting to Solo (B)"));
    CHECK_EQ(0u, gfSceneManager::getInstance()->unk1);
    CHECK_EQ(2u, gfSceneManager::getInstance()->processStep);
    CHECK_EQ(0, static_cast<int>(menu::session().labChosen));
    menu::list::detach();

    // CSS -> Boot Menu via B when coin/dropdown safety gate allows it.
    host::fakes::reset();
    host::log::clear();
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::session().labChosen = 1;
    menu::list::attach();
    stepFrame(pads);

    // Gate false (e.g. P1 coin placed): B passes through to CSS, does NOT reopen Boot Menu.
    host::fakes::setCssCanBack(false);
    setPadInput(pads[0], kButtonB);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list opened (B)"));
    CHECK_EQ(static_cast<u32>(kButtonB), pads[0].m_buttonsCurrentFrame.bits);
    CHECK_EQ(0, host::fakes::cssBHoldCleared());
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Gate true (no placed coin, no dropdown): B reopens Boot Menu, hides B, clears B-hold counter.
    host::fakes::setCssCanBack(true);
    setPadInput(pads[0], kButtonB);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (B) for port 1"));
    CHECK_EQ(0u, pads[0].m_buttonsCurrentFrame.bits);
    CHECK_EQ(1, host::fakes::cssBHoldCleared());
    menu::list::detach();
}

TEST(drill_list, y_quick_osds_patches_osd_area_in_place_without_clobbering_other_areas)
{
    // Build a full 16-area v2 settings file on the fake SD card with custom non-OSD areas.
    main::loadSettings(host::fakes::block());
    main::settings().general.ledgeMaxFrames = 55;
    main::settings().controls.advanceButton = main::kAdvanceX;
    main::settings().display.hitboxes = 2;
    main::settings().osd.wavedash = 1;
    main::settings().osd.lCancel = 1;
    main::settings().osd.maxMessages = 4;

    u8 origFile[storage::kSettingsFileCap];
    memset(origFile, 0, sizeof(origFile));
    storage::FileHeader* oh = reinterpret_cast<storage::FileHeader*>(origFile);
    storage::SettingsHead* ohead = reinterpret_cast<storage::SettingsHead*>(origFile + sizeof(storage::FileHeader));
    u32 at = sizeof(storage::FileHeader) + sizeof(storage::SettingsHead);
    u16 areaCount = 0;
    u32 bodyBytes = storage::writeAreas(origFile + at, sizeof(origFile) - at, at,
                                        reinterpret_cast<const u8*>(&main::settings()), storage::kAllAreas, areaCount);
    REQUIRE(bodyBytes > 0 && areaCount == 16);
    ohead->areaCount = areaCount;
    ohead->layout = main::kSettingsVersion;
    oh->magic = storage::kSettingsMagic;
    oh->format = storage::kSettingsFormat;
    oh->headerSize = sizeof(storage::FileHeader);
    oh->size = at + bodyBytes;
    oh->sequence = 7;
    oh->checksum = storage::checksum(origFile + storage::kChecksumFrom, oh->size - storage::kChecksumFrom);

    // Pure patchOsdAreaInFile test: changes kAreaOsd only; all other 15 areas stay byte-for-byte identical.
    main::OsdSettings editedOsd = main::settings().osd;
    editedOsd.wavedash = 0;
    editedOsd.dashback = 1;
    editedOsd.maxMessages = 6;
    editedOsd.preset = main::kOsdPresetCustom;

    u8 patched[storage::kSettingsFileCap];
    u32 patchedSize = 0;
    REQUIRE(menu::osdQuick::patchOsdAreaInFile(origFile, oh->size, editedOsd, patched, sizeof(patched), 8, patchedSize));
    CHECK_EQ(static_cast<int>(storage::kOk),
             storage::checkHeader(patched, patchedSize, storage::kSettingsMagic, storage::kSettingsFormat, true));

    const storage::SettingsHead* phead = reinterpret_cast<const storage::SettingsHead*>(patched + sizeof(storage::FileHeader));
    CHECK_EQ(16, static_cast<int>(phead->areaCount));
    CHECK_EQ(static_cast<int>(main::kSettingsVersion), static_cast<int>(phead->layout));

    // Read patched file back into fresh defaults and confirm EVERY non-OSD area survived verbatim.
    main::Settings restored;
    main::Settings before = main::settings();
    main::resetSettings();
    storage::AreaStats stats;
    REQUIRE(storage::readAreas(patched, patchedSize, at, phead->areaCount,
                               reinterpret_cast<u8*>(&main::settings()), storage::kAllAreas, stats));
    restored = main::settings();
    CHECK_EQ(16u, stats.copied);
    CHECK_EQ(0u, stats.grown);
    CHECK_EQ(0u, stats.reset);
    CHECK_EQ(55, static_cast<int>(restored.general.ledgeMaxFrames));
    CHECK_EQ(static_cast<int>(main::kAdvanceX), static_cast<int>(restored.controls.advanceButton));
    CHECK_EQ(2, static_cast<int>(restored.display.hitboxes));
    CHECK_EQ(0, static_cast<int>(restored.osd.wavedash));
    CHECK_EQ(1, static_cast<int>(restored.osd.dashback));
    CHECK_EQ(6, static_cast<int>(restored.osd.maxMessages));
    // Restore original settings before testing interactive Y: OSDs UI flow.
    memcpy(&main::settings(), &before, sizeof(main::Settings));

    // Interactive Y: OSDs flow from Boot Menu -> opens screen, edits Max OSDs, backs out -> saves to SD + live block.
    host::card::put("settings-a.bin", origFile, oh->size);
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    setPadInput(pads[0], game::kPadY);
    stepFrame(pads);
    CHECK(menu::osdQuick::isOpen());
    CHECK(host::log::contains("osdquick: opened for port 1"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Move down to row 2 (Max OSDs) and press Right (4 -> 5).
    for (int i = 0; i < 2; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    setPadInput(pads[0], game::kPadDRight);
    stepFrame(pads);
    CHECK_EQ(5, static_cast<int>(main::settings().osd.maxMessages));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Press B to close Quick OSDs and flush to settings-b.bin (sequence 8).
    setPadInput(pads[0], kButtonB);
    stepFrame(pads);
    CHECK(!menu::osdQuick::isOpen());
    setPadInput(pads[0], 0);
    for (int i = 0; i < 4; i++)
    {
        menu::list::onRender(0);
        stepFrame(pads);
    }
    CHECK(host::card::has("settings-b.bin"));
    CHECK(host::log::contains("osdquick: saved to settings-b.bin (save 8"));
    menu::list::detach();
}

TEST(drill_list, y_quick_osds_blocks_edits_on_v1_and_newer_saves)
{
    static const u16 kLayouts[2] = {1u, static_cast<u16>(main::kSettingsVersion + 1)};
    static const char* const kNotes[2] = {"v1 save on SD: upgrade in match", "Newer save on SD: read-only"};

    for (int c = 0; c < 2; c++)
    {
        host::fakes::reset();
        host::card::reset();
        host::log::clear();
        main::loadSettings(host::fakes::block());
        main::settings().osd.maxMessages = 4;
        main::settings().osd.wavedash = 1;

        u8 file[storage::kSettingsFileCap];
        memset(file, 0, sizeof(file));
        storage::FileHeader* h = reinterpret_cast<storage::FileHeader*>(file);
        storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(file + sizeof(storage::FileHeader));
        u32 at = sizeof(storage::FileHeader) + sizeof(storage::SettingsHead);
        u16 areaCount = 0;
        u32 body = storage::writeAreas(file + at, sizeof(file) - at, at,
                                       reinterpret_cast<const u8*>(&main::settings()), storage::kAllAreas, areaCount);
        REQUIRE(body > 0);
        head->areaCount = areaCount;
        head->layout = kLayouts[c];
        h->magic = storage::kSettingsMagic;
        h->format = storage::kSettingsFormat;
        h->headerSize = sizeof(storage::FileHeader);
        h->size = at + body;
        h->sequence = 3;
        h->checksum = storage::checksum(file + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
        host::card::put("settings-a.bin", file, h->size);

        gfPadStatus pads[iface::kPadCount];
        initPads(pads, 1u << 0);
        host::fakes::setMenuPort(0);
        host::fakes::setTrainingSelectRunning(true);
        menu::list::attach();
        stepFrame(pads);

        // Open Quick OSDs with Y.
        setPadInput(pads[0], game::kPadY);
        stepFrame(pads);
        CHECK(menu::osdQuick::isOpen());
        setPadInput(pads[0], 0);
        stepFrame(pads);

        // Footer shows "Back" (not "Back & Save") and header shows the read-only/upgrade note.
        host::canvas::clear();
        menu::list::onRender(0);
        CHECK(canvasDrew(kNotes[c]));
        CHECK(canvasDrew("Back"));
        CHECK(!canvasDrew("Back & Save"));

        // Attempting to edit row 2 (Max OSDs) with Right or A must not mutate live settings.
        for (int i = 0; i < 2; i++)
        {
            setPadInput(pads[0], kDpadDown);
            stepFrame(pads);
            setPadInput(pads[0], 0);
            stepFrame(pads);
        }
        setPadInput(pads[0], game::kPadDRight);
        stepFrame(pads);
        CHECK_EQ(4, static_cast<int>(main::settings().osd.maxMessages));
        setPadInput(pads[0], 0);
        stepFrame(pads);
        setPadInput(pads[0], kButtonA);
        stepFrame(pads);
        CHECK_EQ(4, static_cast<int>(main::settings().osd.maxMessages));
        setPadInput(pads[0], 0);
        stepFrame(pads);

        // Closing with B writes nothing to SD.
        setPadInput(pads[0], kButtonB);
        stepFrame(pads);
        CHECK(!menu::osdQuick::isOpen());
        setPadInput(pads[0], 0);
        for (int i = 0; i < 4; i++)
        {
            menu::list::onRender(0);
            stepFrame(pads);
        }
        CHECK(!host::card::has("settings-b.bin"));
        menu::list::detach();
    }
}

TEST(drill_list, return_from_match_reopens_but_stage_select_back_stays_closed)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    // Arm a scenario (Offense row 0 = Combo Lab, preset 1) -> list closes.
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(1, static_cast<int>(menu::session().labChosen));
    CHECK_EQ(1, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(0, static_cast<int>(menu::session().matchRan));
    CHECK_EQ(1, static_cast<int>(menu::session().page));
    CHECK_EQ(0, static_cast<int>(menu::session().row));

    // Advance from CSS to Stage Select (exitCode == 1), then press B on Stage
    // Select back to CSS without starting a match (matchRan stays 0).
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    host::log::clear();
    menu::attachSession(host::fakes::block());
    menu::list::attach();
    CHECK(host::log::contains("drills: list stays closed (Training Lab chosen)"));
    CHECK(!host::log::contains("drills: list opened"));
    CHECK_EQ(1, static_cast<int>(menu::session().pendingScenario));

    // Same for an armed Skill Drill when backing out of Stage Select.
    menu::session().pendingScenario = 0;
    menu::session().drill = drills::kDrillLedgedash;
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    host::log::clear();
    menu::attachSession(host::fakes::block());
    menu::list::attach();
    CHECK(host::log::contains("drills: list stays closed (Training Lab chosen)"));
    CHECK_EQ(static_cast<int>(drills::kDrillLedgedash), static_cast<int>(menu::session().drill));

    // Now simulate actually running a Training match (matchRan = 1) and
    // returning to CSS: Draft B MUST auto-reopen on the remembered page/row
    // and clear matchRan + armed scenario/drill.
    menu::session().pendingScenario = 1;
    menu::session().matchRan = 1;
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    host::log::clear();
    menu::attachSession(host::fakes::block());
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 1, row 0 Combo Lab"));
    CHECK_EQ(0, static_cast<int>(menu::session().matchRan));
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(static_cast<int>(drills::kDrillNone), static_cast<int>(menu::session().drill));
    menu::list::detach();
}

TEST(drill_list, challenges_and_character_trainers_tabs_arm_all_seven_and_show_snake_card)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    menu::list::attach();
    stepFrame(pads);

    // L from All Modes wraps directly to Page 5 (Character Trainers, 1 row: Snake Infinite Recovery).
    setPadInput(pads[0], game::kPadL);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Character Trainers"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    menu::session().best[drills::kDrillSnakeRecovery] = 4;
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("Character Trainers"));
    CHECK(canvasDrew("Snake Infinite Recovery"));
    CHECK(canvasDrew("Duelist - Cypher & C4 recovery chain"));
    CHECK(canvasDrew("Recommended: Snake"));
    CHECK(canvasDrew("4"));

    // Select Snake Infinite Recovery -> arms kDrillSnakeRecovery (15) and shows CSS mode pill.
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(static_cast<int>(drills::kDrillSnakeRecovery), static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(5, static_cast<int>(menu::session().page));
    CHECK_EQ(0, static_cast<int>(menu::session().row));

    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("SNAKE RECOVERY"));

    // Reopen and press L -> Page 4 (Challenges, 7 rows arming IDs 9..14 + 17).
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Challenges"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    menu::session().best[drills::kDrillComboBreakRush] = 1250;
    static const u8 kExpectedChallenges[7] = {
        drills::kDrillCancelSprint,       drills::kDrillFlowCourse,        drills::kDrillReactionLab,
        drills::kDrillTechchaseEscape,    drills::kDrillRecoverySweetspot, drills::kDrillFloorhug,
        drills::kDrillComboBreakRush
    };
    for (int r = 0; r < 7; r++)
    {
        if (r > 0)
        {
            setPadInput(pads[0], game::kPadL | game::kPadR);
            stepFrame(pads);
            setPadInput(pads[0], 0);
            stepFrame(pads);
            setPadInput(pads[0], kDpadDown);
            stepFrame(pads);
            setPadInput(pads[0], 0);
            stepFrame(pads);
        }
        if (r == 4)
        {
            host::canvas::clear();
            menu::list::onRender(0);
            CHECK(canvasDrew("3D Ledge Guides"));
        }
        if (r == 6)
        {
            host::canvas::clear();
            menu::list::onRender(0);
            CHECK(canvasDrew("Brawl Targets"));
            CHECK(canvasDrew("Best Score"));
            CHECK(canvasDrew("1250"));
        }
        setPadInput(pads[0], kButtonA);
        stepFrame(pads);
        CHECK_EQ(static_cast<int>(kExpectedChallenges[r]), static_cast<int>(menu::session().drill));
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    menu::list::detach();
}
