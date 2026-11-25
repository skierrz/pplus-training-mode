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
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "menu/drill_list.h"
#include "menu/game/training.h"
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

    // Reopen combos require both buttons held and a fresh edge on at least one.
    CHECK(game::reopenPressed(gcc, game::kPadL | game::kPadR, game::kPadR));
    CHECK(!game::reopenPressed(gcc, game::kPadL | game::kPadR, 0));
    CHECK(!game::reopenPressed(gcc, game::kPadL, game::kPadL));
    CHECK(!game::reopenPressed(wii, game::kPadWiiMinus, game::kPadWiiMinus));
    CHECK(!game::reopenPressed(wii, game::kPadWiiB | game::kPadL, game::kPadWiiB | game::kPadL));
    CHECK(game::reopenPressed(wii, game::kPadWiiMinus | game::kPadWiiB | game::kPadL, game::kPadWiiB | game::kPadL));
    CHECK(!game::reopenPressed(wii, game::kPadWiiMinus | game::kPadWiiB | game::kPadL, 0));
    CHECK(game::reopenPressed(nun, game::kPadX | game::kPadNunchukC | game::kPadZ, game::kPadZ));
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
    setPadInput(pads[4], 0);
    stepFrame(pads);

    // Sideways 1 (0x10200) closes the list and hides both bits of 1 until released,
    // without hiding another button (2 = 0x20100) pressed after closing.
    setPadInput(pads[4], game::kPadB | game::kPadWii1);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (1)"));
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);

    // While 1 is still held and 2 is newly pressed: 1 stays hidden, 2 passes through.
    setPadInput(pads[4], game::kPadB | game::kPadWii1 | game::kPadA | game::kPadWii2);
    stepFrame(pads);
    CHECK_EQ(static_cast<u32>(game::kPadA | game::kPadWii2), pads[4].m_buttonsCurrentFrame.bits);

    // Releasing 1 clears its mask; pressing - alone neither opens the list nor
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
    setPadInput(pads[4], game::kPadB | game::kPadWiiB);
    stepFrame(pads);
    CHECK(!host::log::contains("drills: list closed (B)"));
    setPadInput(pads[4], 0);
    stepFrame(pads);

    // Nunchuk B (0x400200) closes the list with stick held; stick stays hidden until neutral.
    setPadInput(pads[4], game::kPadB | game::kPadWiiB, 0, -80);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list closed (B)"));
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

TEST(drill_list, draw_pending_keeps_css_uncluttered_while_reopen_works)
{
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 4, gfPadType::NUNCHUK);
    host::fakes::setMenuPort(4);
    host::fakes::setTrainingSelectRunning(true);
    menu::session().labChosen = 1; // list stays closed so onRender runs drawPending()
    menu::list::attach();
    stepFrame(pads);

    // CSS stays uncluttered (zero runs, zero rects) across all 4 controller types.
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK_EQ(0, host::canvas::runs());
    CHECK_EQ(0, host::canvas::rects());
    CHECK(strcmp(menu::list::reopenHint(), "C+Z = Training Menu") == 0);

    setPadType(pads, 4, gfPadType::WIIMOTE);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK_EQ(0, host::canvas::runs());
    CHECK_EQ(0, host::canvas::rects());
    CHECK(strcmp(menu::list::reopenHint(), "- + B = Training Menu") == 0);

    setPadType(pads, 4, gfPadType::WII_CLASSIC);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK_EQ(0, host::canvas::runs());
    CHECK_EQ(0, host::canvas::rects());
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

    pads[0].m_error = gfPadError::NONE;
    host::fakes::setPadConnected(0, true);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK_EQ(0, host::canvas::runs());
    CHECK_EQ(0, host::canvas::rects());
    CHECK(strcmp(menu::list::reopenHint(), "L+R = Training Menu") == 0);

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
    CHECK(canvasDrew("Close"));
    CHECK(canvasDrew("X"));
    CHECK(canvasDrew("Import"));
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

    // Open recordings browser on Sideways -> Sideways header, page line, delete confirm.
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
    CHECK(canvasDrew("2: Load   A: Delete   1: Back"));
    CHECK(canvasDrew("< -   Page 1/1   + >"));

    // Press Sideways A to ask delete -> confirm line shows "2: Delete   1: Cancel".
    setPadInput(pads[4], game::kPadWiiA);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("2: Delete   1: Cancel"));

    // Cancel delete and switch to Nunchuk -> Nunchuk browser labels & confirm.
    setPadInput(pads[4], game::kPadB | game::kPadWii1);
    stepFrame(pads);
    setPadInput(pads[4], 0);
    stepFrame(pads);
    setPadType(pads, 4, gfPadType::NUNCHUK);
    stepFrame(pads);
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("A: Load   C: Delete   B: Back"));
    CHECK(canvasDrew("< -   Page 1/1   + >"));

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

    // Starts on Page 0 (All Modes, 12 rows): Row 0 = Training Lab, pendingScenario == 0.
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));

    // Scroll down past row 10 (P+ Multi-Hit SDI) to row 11 (Browse Recordings) and open browser.
    for (int i = 0; i < 12; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: row 10 P+ Multi-Hit SDI"));
    CHECK(host::log::contains("drills: row 11 Browse Recordings"));
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

    // GC R cycles: All Modes -> Offense -> Defense -> All Modes.
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Offense"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Move down to row 3 in Offense (Shield Pressure, presetId = 4) and select it.
    for (int i = 0; i < 3; i++)
    {
        setPadInput(pads[0], kDpadDown);
        stepFrame(pads);
        setPadInput(pads[0], 0);
        stepFrame(pads);
    }
    CHECK(host::log::contains("drills: row 3 Shield Pressure"));
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK(host::log::contains("drills: scenario 4 (Shield Pressure) armed"));
    CHECK(host::log::contains("drills: list closed (Shield Pressure)"));
    CHECK_EQ(4, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(1, static_cast<int>(menu::session().page));
    CHECK_EQ(3, static_cast<int>(menu::session().row));
    CHECK_EQ(1, static_cast<int>(menu::session().labChosen));

    // Reopen with L+R: remembers Page 1 (Offense), Row 3 (Shield Pressure).
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: list opened (L+R) for port 1, row 3 Shield Pressure"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Cycle R -> Defense (Page 2, 5 rows) and move to bottom row 4 (P+ Multi-Hit SDI, presetId = 10).
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
    CHECK(host::log::contains("drills: row 4 P+ Multi-Hit SDI"));
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK_EQ(10, static_cast<int>(menu::session().pendingScenario));
    CHECK(host::log::contains("drills: scenario 10 (P+ Multi-Hit SDI) armed"));

    // Reopen, cycle R -> Skill Drills (Page 3) -> All Modes (Page 0), select row 0 (Training Lab) -> clears pendingScenario to 0!
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadL | game::kPadR);
    stepFrame(pads);
    setPadInput(pads[0], 0);
    stepFrame(pads);
    setPadInput(pads[0], game::kPadR);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Skill Drills"));
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

    // L from All Modes wraps directly to Page 3 (Skill Drills).
    setPadInput(pads[0], game::kPadL);
    stepFrame(pads);
    CHECK(host::log::contains("drills: page Skill Drills"));
    setPadInput(pads[0], 0);
    stepFrame(pads);

    // Details card shows Personal Best placeholder when best == 0.
    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("Skill Drills"));
    CHECK(canvasDrew("Ledgedash"));
    CHECK(canvasDrew("Personal Best"));
    CHECK(canvasDrew("Best: --"));

    // Select Ledgedash (row 0 of Skill Drills) -> arms drill 1, clears scenario, closes list.
    setPadInput(pads[0], kButtonA);
    stepFrame(pads);
    CHECK(host::log::contains("drills: skill drill 1 (Ledgedash) armed"));
    CHECK_EQ(static_cast<int>(drills::kDrillLedgedash), static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().pendingScenario));
    CHECK_EQ(0, static_cast<int>(menu::session().drillRan));
    CHECK_EQ(3, static_cast<int>(menu::session().page));
    CHECK_EQ(0, static_cast<int>(menu::session().row));

    // Detach into match (exitCode == 1) preserves armed drill for extra/drill_hud.cpp.
    gfSceneManager::getInstance()->unk1 = 1;
    menu::list::detach();
    CHECK_EQ(static_cast<int>(drills::kDrillLedgedash), static_cast<int>(menu::session().drill));

    // Simulate match finishing with score 8/10 -> returning to CSS auto-opens launcher
    // on Skill Drills row 0 showing "Best: 8/10" and clears drill/drillRan.
    menu::session().drillRan = 1;
    menu::session().best[drills::kDrillLedgedash] = 8;
    host::log::clear();
    menu::attachSession(host::fakes::block());
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened (auto) for port 1, row 0 Ledgedash"));
    CHECK_EQ(static_cast<int>(drills::kDrillNone), static_cast<int>(menu::session().drill));
    CHECK_EQ(0, static_cast<int>(menu::session().drillRan));

    host::canvas::clear();
    menu::list::onRender(0);
    CHECK(canvasDrew("Best: 8/10"));
    menu::list::detach();
}
