// In-match Wii controller savestate D-pad support (src/main/savestate/savestate.cpp):
// - Classic & Nunchuk bare D-pad Right (12f) saves, D-pad Left loads, D-pad hidden,
//   and side taunt appeal bits stripped in onFighterInput
// - Sideways Wii Remote bare D-pad Left/Right never saves, loads, or hides D-pad
// - Sideways Wii Remote - + D-pad Right/Left saves/loads, hides D-pad (never '-'),
//   and strips appeal bits in onFighterInput
// - Frame Advance with '-' bound gates out Sideways - + D-pad.
#define TM_MODULE_MAIN 1

#include <OS/OSTime.h>
#include <cstdio>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_file_io_manager.h>
#include <gf/gf_pad_status.h>
#include <gm/gm_global.h>
#include <if/if_mngr.h>
#include <ip/ip_padconfig.h>
#include <so/effect/so_effect_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>
#include <types.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/cpu/driver.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/savestate/snapshot.h"
#include "main/savestate/trace.h"
#include "main/settings.h"
#include "main/ui/menu.h"
#include "main/ui/osd.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_map.h"
#include "shared/interface.h"
#include "shared/log.h"

namespace {
    struct SsStubs {
        int captures;
        int restores;
        bool valid;
        tm::savestate::SlotInfo info;
        bool ownerValid;
        tm::cpu::Owner owner;
    };
    static SsStubs s_ss;

    static void resetSsStubs()
    {
        memset(&s_ss, 0, sizeof(s_ss));
    }
}

namespace tm {
namespace lab {
    bool savestateBusy() { return false; }
    void savestateCopying() {}
    void savestateLoaded() {}
}
namespace cpu {
    void driverSaved(int) {}
    void driverLoaded(int) {}
    void optionsSaved(int) {}
    void optionsLoaded(int) {}
}
namespace savestate {
    Probe g_probe;
    void traceTop(char) {}
    bool parkTop() { return false; }
    void probeReset() { memset(&g_probe, 0, sizeof(g_probe)); }
    void heldPads(gfPadStatus*) {}
    bool layoutKnown() { return true; }
    Result capture(int, u32 loopPass)
    {
        s_ss.captures++;
        s_ss.valid = true;
        s_ss.info.valid = true;
        s_ss.info.loopPass = loopPass;
        return kOk;
    }
    Result restore(int, void (*)())
    {
        s_ss.restores++;
        return kOk;
    }
    void invalidate(int) { s_ss.valid = false; s_ss.info.valid = false; }
    bool droppedOther() { return false; }
    void release() { s_ss.valid = false; s_ss.info.valid = false; }
    bool holdingBuffers() { return s_ss.valid; }
    const SlotInfo& slotInfo(int) { return s_ss.info; }
    const char* resultText(Result) { return "ok"; }
    void forgetStall() {}
    u32 usageHash() { return 0; }
    u32 fighterHeapHash() { return 0; }
}
}

// Keep savestate::* snapshot/trace symbols from above while renaming savestate.cpp's
// public entry points via savestate_uut.
namespace tm {
namespace savestate_uut {
    using savestate::kSlotQuick;
    using savestate::kSlotRecording;
    using savestate::kOk;
    using savestate::kErrNoRoom;
    using savestate::kErrTooBig;
    using savestate::kErrMatch;
    using savestate::kErrLayout;
    using savestate::kErrResources;
    using savestate::kErrChecksum;
    using savestate::kLeftLoadState;
    using savestate::kRightSaveState;
    using savestate::kMaxRandomPercent;
    using savestate::Result;
    using savestate::SlotInfo;
    using savestate::g_probe;
    using savestate::traceTop;
    using savestate::parkTop;
    using savestate::probeReset;
    using savestate::heldPads;
    using savestate::layoutKnown;
    using savestate::capture;
    using savestate::restore;
    using savestate::invalidate;
    using savestate::droppedOther;
    using savestate::release;
    using savestate::holdingBuffers;
    using savestate::slotInfo;
    using savestate::resultText;
    using savestate::forgetStall;
}
}

#define savestate savestate_uut
#include "main/savestate/savestate.cpp"
#undef savestate

using namespace tm;

namespace {
    void setupSavestateWorld()
    {
        resetSsStubs();
        main::loadSettings(host::fakes::block());
        main::resetSettings();
        host::fakes::match().running = true;
        host::fakes::match().training = true;
        host::fakes::match().paused = false;
        // Two fighters + trainingCpus == 1 + empty IO queues so blocked() returns NULL.
        g_ftEntryManager = host::mem::make<ftEntryManager>();
        g_ftEntryManager->reset();
        g_ftManager = host::mem::make<ftManager>();
        for (int p = 0; p < 2; p++)
        {
            host::fakes::FakeFighter& ff = host::fakes::addFighter();
            ftEntry* e = host::mem::make<ftEntry>();
            e->m_entryId = p;
            e->m_playerNo = p;
            e->m_activeInstanceIndex = 0;
            e->m_instances[0].m_fighter = ff.fighter;
            e->m_owner = host::mem::make<ftOwner>();
            g_ftEntryManager->setSlot(p, e);
            host::fakes::setPlayer(p, static_cast<u8>(p == 0 ? 0 : 7), static_cast<u8>(p), 0, 0);
        }
        // ftManager::getEntryCount() reads m_entryCount; set via placement on g_ftManager if present,
        // else check ft_manager.h. Also set IfMngr -> IfMinigameTraining (+0x20 = 1 CPU).
        g_IfMngr = host::mem::make<IfMngr>();
        u8* trMenu = static_cast<u8*>(host::mem::alloc(0x40, 8));
        *reinterpret_cast<u32*>(trMenu + 0x20) = 1;
        *reinterpret_cast<u32*>(g_IfMngr->raw + 0x78) = reinterpret_cast<u32>(trMenu);
        // Empty file IO queues (+0x8 and +0xC -> queue -> +0x18 ring -> +0x2 count == 0).
        for (int q = 0; q < 2; q++)
        {
            u8* queue = static_cast<u8*>(host::mem::alloc(0x20, 8));
            u8* ring = static_cast<u8*>(host::mem::alloc(0x10, 8));
            *reinterpret_cast<u32*>(queue + 0x18) = reinterpret_cast<u32>(ring);
            *reinterpret_cast<u32*>(g_gfFileIOManager->raw + 0x8 + q * 4) = reinterpret_cast<u32>(queue);
        }
        savestate_uut::attach();
        savestate_uut::s_autoDone = true; // skip match-start autoSave in unit tests
    }

    void initSsPads(gfPadStatus pads[iface::kPadCount], u32 mask, u32 wiiType)
    {
        memset(pads, 0, sizeof(gfPadStatus) * iface::kPadCount);
        for (int i = 0; i < iface::kPadCount; i++)
        {
            bool on = (mask & (1u << i)) != 0;
            pads[i].m_error = static_cast<s8>(on ? gfPadError::NONE : gfPadError::NO_CONTROLLER);
            pads[i].m_controllerType = static_cast<gfPadType::PadType>(i < 4 ? gfPadType::GCC : wiiType);
        }
    }

    void setSsPad(gfPadStatus& p, u32 bits)
    {
        p.m_buttonsCurrentFrame.bits = bits;
        p.m_buttonsCurrentFrame2.bits = bits;
        p.m_buttonsHeld.bits = bits;
        p.m_buttonsPressedThisFrame.bits = bits;
        p.m_buttonsPressedThisFrame2.bits = bits;
    }
}

TEST(wii_savestate, classic_and_nunchuk_dpad_save_load_and_hide)
{
    setupSavestateWorld();
    gfPadStatus pads[iface::kPadCount];
    initSsPads(pads, 1u << 4, gfPadType::WII_CLASSIC);
    host::fakes::setPlayerPort(0, 4);

    // Neutral pass to register Classic port 5.
    savestate_uut::onPads(iface::kPadsSystem, pads);
    savestate_uut::onPads(iface::kPadsGame, pads);

    // Hold D-pad Right 12 passes on Classic -> saves and hides D-pad Right!
    for (int f = 0; f < 12; f++)
    {
        setSsPad(pads[4], game::kPadDRight);
        savestate_uut::onPads(iface::kPadsSystem, pads);
        CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
        setSsPad(pads[4], game::kPadDRight);
        savestate_uut::onPads(iface::kPadsGame, pads);
        CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    }
    CHECK_EQ(1, s_ss.captures);

    // Wait out 24-frame lockout, then Nunchuk D-pad Left loads!
    pads[4].m_controllerType = gfPadType::NUNCHUK;
    for (int f = 0; f < 26; f++)
    {
        setSsPad(pads[4], 0);
        savestate_uut::onPads(iface::kPadsSystem, pads);
    }
    setSsPad(pads[4], game::kPadDLeft);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    CHECK_EQ(1, s_ss.restores);
    CHECK_EQ(0u, pads[4].m_buttonsCurrentFrame.bits);
    savestate_uut::detach();
}

TEST(wii_savestate, sideways_bare_dpad_untouched_minus_chord_saves_and_loads)
{
    setupSavestateWorld();
    gfPadStatus pads[iface::kPadCount];
    initSsPads(pads, 1u << 4, gfPadType::WIIMOTE);
    host::fakes::setPlayerPort(0, 4);
    savestate_uut::onPads(iface::kPadsSystem, pads);

    // Bare D-pad Right held 14 passes on Sideways Wii Remote: MUST NOT save or hide!
    for (int f = 0; f < 14; f++)
    {
        setSsPad(pads[4], game::kPadDRight);
        savestate_uut::onPads(iface::kPadsSystem, pads);
        CHECK_EQ(static_cast<u32>(game::kPadDRight), pads[4].m_buttonsCurrentFrame.bits);
        savestate_uut::onPads(iface::kPadsGame, pads);
        CHECK_EQ(static_cast<u32>(game::kPadDRight), pads[4].m_buttonsCurrentFrame.bits);
    }
    CHECK_EQ(0, s_ss.captures);

    // - + D-pad Right held 12 passes: saves, hides D-pad Right, NEVER hides '-' itself!
    setSsPad(pads[4], 0);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    for (int f = 0; f < 12; f++)
    {
        setSsPad(pads[4], game::kPadWiiMinus | game::kPadDRight);
        savestate_uut::onPads(iface::kPadsSystem, pads);
        CHECK_EQ(static_cast<u32>(game::kPadWiiMinus), pads[4].m_buttonsCurrentFrame.bits);
    }
    CHECK_EQ(1, s_ss.captures);

    // Wait out lockout, then bare D-pad Left must NOT load or hide.
    for (int f = 0; f < 26; f++)
    {
        setSsPad(pads[4], 0);
        savestate_uut::onPads(iface::kPadsSystem, pads);
    }
    setSsPad(pads[4], game::kPadDLeft);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    CHECK_EQ(0, s_ss.restores);
    CHECK_EQ(static_cast<u32>(game::kPadDLeft), pads[4].m_buttonsCurrentFrame.bits);

    // - + D-pad Left loads and hides D-pad Left (never '-' itself).
    setSsPad(pads[4], 0);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    setSsPad(pads[4], game::kPadWiiMinus | game::kPadDLeft);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    CHECK_EQ(1, s_ss.restores);
    CHECK_EQ(static_cast<u32>(game::kPadWiiMinus), pads[4].m_buttonsCurrentFrame.bits);

    // Frame Advance active with '-' bound (advanceButton == L): - + D-pad Left is gated out!
    main::settings().general.frameAdvance = 1;
    main::settings().controls.advanceButton = main::kAdvanceL;
    setSsPad(pads[4], 0);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    setSsPad(pads[4], game::kPadWiiMinus | game::kPadDLeft);
    savestate_uut::onPads(iface::kPadsSystem, pads);
    CHECK_EQ(1, s_ss.restores);
    CHECK_EQ(static_cast<u32>(game::kPadWiiMinus | game::kPadDLeft), pads[4].m_buttonsCurrentFrame.bits);
    savestate_uut::detach();
}
