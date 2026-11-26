// Drill swap guard (src/stub/set_guard.cpp): refuses mid-match character
// changes in stLoaderPlayer state 3 while a drill is active and restores
// character (+0) and m_colorFileNo (+5) before removeEntity() runs, leaving
// m_colorNo (+6) untouched.
#define TM_MODULE_STUB 1
#define TM_HOST_TEST 1

#include <sr/sr_common.h>
#include <string.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/fighters.h"
#include "shared/drill_session.h"
#include "stub/dispatch.h"
#include "stub/loader.h"

namespace {
    static s32 s_testExtraHeap = Heaps::AssistFigureResource;
}

namespace tm {
namespace loader {
    static s32 set_guard_test_extraHeap()
    {
        return s_testExtraHeap;
    }
}
}

#define extraHeap set_guard_test_extraHeap
#include "stub/set_guard.cpp"
#undef extraHeap

namespace {
    using namespace tm;

    static u8* hostSettings()
    {
        return host::fakes::block();
    }

    static void setupGameGlobal(u8*& modePlayers, u8*& loaderBuf)
    {
        // Place GameGlobal pointer at kGameGlobal (0x805A00E0) in the image.
        u32 globalAddr = 0x805A1000u;
        u32 modeAddr = 0x805A2000u;
        host::mem::setWord(stub::kGameGlobal, globalAddr);
        host::mem::setWord(globalAddr + 8, modeAddr);
        modePlayers = reinterpret_cast<u8*>(modeAddr + stub::kPlayers);
        memset(modePlayers, 0, game::kMaxFighters * stub::kPlayerSize);
        loaderBuf = static_cast<u8*>(host::mem::alloc(0x80, 8));
        dispatch::table().services.settings = hostSettings;
        s_testExtraHeap = Heaps::AssistFigureResource;
    }

    static drills::Session* initDrillSession(u8 drill, u8 drillRan)
    {
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        drills::Session* s = reinterpret_cast<drills::Session*>(block + drills::kSessionOffset);
        s->magic = drills::kSessionMagic;
        s->version = drills::kSessionVersion;
        s->drill = drill;
        s->drillRan = drillRan;
        return s;
    }
}

TEST(set_guard, drill_guard_refuses_swap_and_restores_color_file_only)
{
    u8* players = NULL;
    u8* loader = NULL;
    setupGameGlobal(players, loader);
    initDrillSession(1, 1);

    // Pairs of (m_colorFileNo, sentinel m_colorNo at +6) where the two differ:
    // Mario slot 2 -> file 3, Wario slot 0 -> file 6, AltR file 61, AltZ file 62.
    static const u8 kFileNos[game::kMaxFighters] = {3, 6, 61, 62};
    static const u8 kColorSentinels[game::kMaxFighters] = {0, 0, 2, 5};

    for (int p = 0; p < game::kMaxFighters; p++)
    {
        u8* entry = players + p * stub::kPlayerSize;
        u8 origChar = static_cast<u8>(p == 0 ? 0x00 : 0x07);
        u8 origFile = kFileNos[p];
        u8 colorSentinel = kColorSentinels[p];
        loader[stub::kLoaderCharacter] = origChar;
        loader[stub::kLoaderCostume] = static_cast<u8>(origFile | 0x80); // bit 7 flag set
        // Simulate Code Menu overwriting kind to Falco (0x15) and zeroing +5.
        entry[0] = 0x15;
        entry[stub::kPlayerColorFile] = 0;
        entry[6] = colorSentinel;
        entry[stub::kPlayerFlags] = 0;

        stub::tmDrillGuardBody(loader, entry);

        CHECK_EQ(origChar, entry[0]);
        CHECK_EQ(origFile, entry[stub::kPlayerColorFile]);
        CHECK_EQ(colorSentinel, entry[6]);
    }
}

TEST(set_guard, drill_guard_ignores_lab_or_unstarted_or_no_extra)
{
    u8* players = NULL;
    u8* loader = NULL;
    setupGameGlobal(players, loader);

    u8* p1 = players;
    loader[stub::kLoaderCharacter] = 0x00;
    loader[stub::kLoaderCostume] = 3;

    // Training Lab (kDrillNone): allowed.
    initDrillSession(drills::kDrillNone, 0);
    p1[0] = 0x0D;
    stub::tmDrillGuardBody(loader, p1);
    CHECK_EQ(0x0Du, p1[0]);

    // Drill chosen on CSS, but not started in this match (drillRan == 0): allowed.
    initDrillSession(1, 0);
    p1[0] = 0x0D;
    stub::tmDrillGuardBody(loader, p1);
    CHECK_EQ(0x0Du, p1[0]);

    // Absent player slot (player[1] == kAbsent): left alone.
    initDrillSession(1, 1);
    s_testExtraHeap = Heaps::AssistFigureResource;
    p1[0] = 0x0D;
    p1[1] = stub::kAbsent;
    stub::tmDrillGuardBody(loader, p1);
    CHECK_EQ(0x0Du, p1[0]);
    p1[1] = 0;

    // Extra module not loaded (e.g. regular Versus match): allowed.
    s_testExtraHeap = -1;
    p1[0] = 0x0D;
    stub::tmDrillGuardBody(loader, p1);
    CHECK_EQ(0x0Du, p1[0]);
}

TEST(set_guard, assist_heap_allows_4th_set_while_drill_guard_stays_active)
{
    u8* players = NULL;
    u8* loader = NULL;
    setupGameGlobal(players, loader);
    s_testExtraHeap = Heaps::AssistFigureResource;

    // With extra in AssistFigureResource (not OverlayFighter1..4), tmSetGuardBody
    // returns immediately without touching the 4th player's character choice.
    u8* p4 = players + 3 * stub::kPlayerSize;
    p4[0] = 0x27; // Jigglypuff (4th distinct fighter)
    p4[1] = 0;
    stub::tmSetGuardBody(loader, p4);
    CHECK_EQ(0x27u, p4[0]);

    // Meanwhile tmDrillGuardBody still guards active drills because extraHeap() >= 0.
    initDrillSession(1, 1);
    loader[stub::kLoaderCharacter] = 0x00;
    loader[stub::kLoaderCostume] = 2;
    players[0] = 0x15;
    players[1] = 0;
    players[stub::kPlayerColorFile] = 0;
    players[stub::kPlayerFlags] = 0;
    stub::tmDrillGuardBody(loader, players);
    CHECK_EQ(0x00u, players[0]);
    CHECK_EQ(2u, players[stub::kPlayerColorFile]);
}

TEST(set_guard, drill_guard_preserves_dark_fake_color_override)
{
    u8* players = NULL;
    u8* loader = NULL;
    setupGameGlobal(players, loader);
    initDrillSession(1, 1);

    static const u8 kFlagBits[2] = {0x10, 0x08}; // dark (12) and fake (13) bits
    for (int i = 0; i < 2; i++)
    {
        u8* p1 = players;
        loader[stub::kLoaderCharacter] = 0x00;
        loader[stub::kLoaderCostume] = static_cast<u8>(12 + i);
        p1[0] = 0x15;
        p1[stub::kPlayerColorFile] = 0;
        p1[6] = 4;
        p1[stub::kPlayerFlags] = kFlagBits[i];

        stub::tmDrillGuardBody(loader, p1);

        CHECK_EQ(0x00u, p1[0]);
        CHECK_EQ(0u, p1[stub::kPlayerColorFile]);
        CHECK_EQ(4u, p1[6]);
    }
}
