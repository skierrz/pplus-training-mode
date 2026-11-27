// Stage 6B offline local Versus read-only displays policy, settings layout,
// menu placement, and SD storage backward-compatible migration tests.
#include <sr/sr_common.h>
#include <string.h>

#include "extra/menu_descriptions.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/notice.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/pages/pages.h"
#include "shared/block_layout.h"
#include "shared/drill_session.h"
#include "stub/dispatch.h"
#include "stub/load_policy_core.h"

extern "C" {
    void tmDrillGuardBody(const u8* loader, u8* player);
    void tmSetGuardBody(const u8* loader, u8* player);
}

using namespace tm;
using main::Settings;

namespace {
    const char kFileA[] = "settings-a.bin";
    const char kFileB[] = "settings-b.bin";

    policy::SceneGateInput localOfflineVs(u32 activePlayers, bool optIn, bool assistTrophies = false)
    {
        policy::SceneGateInput in;
        in.sceneIsMelee = true;
        in.trainingMode = false;
        in.isSqVsMelee = true;
        in.gameMode = policy::kMeleeGameMode;
        in.isNetplayGct = false;
        in.isNetplayVsync = false;
        in.isReplayFlag = false;
        in.optInEnabled = optIn;
        in.assistTrophiesEnabled = assistTrophies;
        in.activePlayers = activePlayers;
        return in;
    }

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

    void pollStorage(int count = 4)
    {
        for (int i = 0; i < count; i++)
        {
            storage::onRender(host::fakes::match());
        }
    }

    void closeAndFlush()
    {
        storage::settingsMenuClosed();
        pollStorage();
    }

    storage::FileHeader* fileHeader(const char* name)
    {
        return reinterpret_cast<storage::FileHeader*>(host::card::data(name));
    }

    const char* newestSettingsFile()
    {
        if (!host::card::has(kFileB))
        {
            return kFileA;
        }
        if (!host::card::has(kFileA))
        {
            return kFileB;
        }
        return fileHeader(kFileB)->sequence > fileHeader(kFileA)->sequence ? kFileB : kFileA;
    }

    storage::AreaEntry* findAreaEntry(const char* name, int id)
    {
        u8* file = host::card::data(name);
        storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(file + sizeof(storage::FileHeader));
        storage::AreaEntry* table =
            reinterpret_cast<storage::AreaEntry*>(file + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
        for (int i = 0; i < head->areaCount; i++)
        {
            if (table[i].id == id)
            {
                return &table[i];
            }
        }
        return NULL;
    }

    void resealFile(const char* name)
    {
        storage::FileHeader* h = fileHeader(name);
        h->checksum = storage::checksum(host::card::data(name) + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    }
}

TEST(stage6b_vs, opt_in_default_off_loads_zero_modules_in_versus)
{
    CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(localOfflineVs(2, false))));
    CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(localOfflineVs(3, false))));
    CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(localOfflineVs(4, false))));

    // Resident block opt-in helper: NULL, zeroed, wrong magic, and initialized-off all return false.
    CHECK(!policy::offlineVersusOptIn(NULL));
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    CHECK(!policy::offlineVersusOptIn(block));
    block[block::kOfflineVersusOffset] = 1;
    CHECK(!policy::offlineVersusOptIn(block));

    main::loadSettings(block);
    CHECK_EQ(0u, main::settings().general.offlineVersus);
    CHECK(!policy::offlineVersusOptIn(block));

    main::settings().general.offlineVersus = 1;
    CHECK(policy::offlineVersusOptIn(block));
}

TEST(stage6b_vs, hard_blocks_netplay_replays_and_non_vs_modes_even_when_opted_in)
{
    // Primary Netplay GCT signature ("NETBOOST.GCT" at *0x80550000).
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.isNetplayGct = true;
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
    // Defense-in-depth Netplay vsync nop (0x80023B88 == 0x60000000).
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.isNetplayVsync = true;
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
    // Defense-in-depth Replay flag (0x805BBFC0 == 1).
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.isReplayFlag = true;
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
    // Non-sqVsMelee sequences (sqReplay, sqNetFriendMelee, sqNetAnyOkiraku, sqSpMelee, sqToMelee, sqQuMelee).
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.isSqVsMelee = false;
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
    // Non-Melee gameMode values (modeMelee+0x08 >> 2 != 0).
    static const int kBlockedModes[] = {-1, 1, 2, 3, 4, 0x10};
    for (u32 i = 0; i < sizeof(kBlockedModes) / sizeof(kBlockedModes[0]); i++)
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.gameMode = kBlockedModes[i];
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
    // Non-scMelee scene.
    {
        policy::SceneGateInput in = localOfflineVs(2, true);
        in.sceneIsMelee = false;
        CHECK_EQ(static_cast<int>(policy::kPlanNone), static_cast<int>(policy::decideMeleeLoad(in)));
    }
}

TEST(stage6b_vs, allowed_offline_versus_respects_assist_and_4p_safety)
{
    // Assist Trophies OFF (standard competitive VS): 2P, 3P, and 4P all load
    // match + extra into AssistFigureResource (leaving OverlayFighter1..4 untouched).
    for (u32 players = 2; players <= 4; players++)
    {
        policy::SceneGateInput in = localOfflineVs(players, true, false);
        CHECK_EQ(static_cast<int>(policy::kPlanMatchAndExtra), static_cast<int>(policy::decideMeleeLoad(in)));
        CHECK_EQ(static_cast<int>(policy::kExtraHeapAssist), static_cast<int>(policy::decideExtraHeap(in)));
    }

    // Assist Trophies ON: 2P, 3P, and 4P all load match ONLY so Brawl keeps
    // AssistFigureResource free for Assist Trophy preloads (extra exceeds OverlayFighter4).
    for (u32 players = 2; players <= 4; players++)
    {
        policy::SceneGateInput in = localOfflineVs(players, true, true);
        CHECK_EQ(static_cast<int>(policy::kPlanMatchOnly), static_cast<int>(policy::decideMeleeLoad(in)));
        CHECK_EQ(static_cast<int>(policy::kExtraHeapNone), static_cast<int>(policy::decideExtraHeap(in)));
    }
}

TEST(stage6b_vs, training_mode_policy_unchanged)
{
    policy::SceneGateInput in;
    in.sceneIsMelee = true;
    in.trainingMode = true;
    in.isSqVsMelee = false;
    in.gameMode = 0xD;
    in.isNetplayGct = false;
    in.isNetplayVsync = false;
    in.isReplayFlag = false;
    in.optInEnabled = false;
    in.assistTrophiesEnabled = true;
    in.activePlayers = 4;
    CHECK_EQ(static_cast<int>(policy::kPlanMatchAndExtra), static_cast<int>(policy::decideMeleeLoad(in)));
    CHECK_EQ(static_cast<int>(policy::kExtraHeapAssist), static_cast<int>(policy::decideExtraHeap(in)));
}

TEST(stage6b_vs, general_page_appends_offline_vs_row_and_description)
{
    const ui::Page& gen = ui::pages::kGeneral;
    REQUIRE(gen.count == 13);
    CHECK(strcmp(gen.options[8].name, "Offline VS Displays") == 0);
    CHECK_EQ(static_cast<int>(ui::kKindToggle), static_cast<int>(gen.options[8].kind));
    CHECK_EQ(0, static_cast<int>(gen.options[8].def));
    CHECK_EQ(static_cast<u16>(offsetof(Settings, general.offlineVersus)), gen.options[8].offset);
    CHECK((gen.options[8].flags & ui::kFlagTabsOnly) == 0);
    CHECK(strcmp(gen.options[12].name, "Return to CSS") == 0);

    const char* desc = extra::optionDesc(ui::pages::P_GENERAL, 8);
    REQUIRE(desc != NULL);
    CHECK(desc[0] != '\0');
}

TEST(stage6b_vs, storage_migrates_pre_wave3_general_area_and_roundtrips_offline_vs)
{
    bootStorage();
    CHECK_EQ(0u, main::settings().general.offlineVersus);

    // Save with offlineVersus = 1 and confirm it round-trips across reboot.
    main::settings().general.offlineVersus = 1;
    closeAndFlush();
    storage::detach();

    bootStorage();
    CHECK_EQ(1u, main::settings().general.offlineVersus);
    CHECK(policy::offlineVersusOptIn(host::fakes::block()));
    storage::detach();

    // Truncate kAreaGeneral.used to 13 bytes (pre-Wave-3 layout ending right after
    // playerInfiniteShields): loading must migrate cleanly with offlineVersus = 0.
    const char* newest = newestSettingsFile();
    storage::AreaEntry* area = findAreaEntry(newest, storage::kAreaGeneral);
    REQUIRE(area != NULL);
    CHECK_EQ(15u, area->used);
    CHECK_EQ(offsetof(main::GeneralSettings, reserved), area->used);
    area->used = offsetof(main::GeneralSettings, offlineVersus);
    CHECK_EQ(13u, area->used);
    resealFile(newest);

    bootStorage();
    CHECK_LOG("17 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_LOG("storage: notice: Lab settings from an older version: new ones at defaults");
    CHECK_EQ(0u, main::settings().general.offlineVersus);
    CHECK(!policy::offlineVersusOptIn(host::fakes::block()));

    // Round-trip back to 1, then back to 0.
    main::settings().general.offlineVersus = 1;
    closeAndFlush();
    storage::detach();
    bootStorage();
    CHECK_EQ(1u, main::settings().general.offlineVersus);

    main::settings().general.offlineVersus = 0;
    closeAndFlush();
    storage::detach();
    bootStorage();
    CHECK_EQ(0u, main::settings().general.offlineVersus);
    CHECK(!policy::offlineVersusOptIn(host::fakes::block()));
    storage::detach();
}

namespace {
    u8* stage6bSettingsBlock()
    {
        return host::fakes::block();
    }
}

TEST(stage6b_vs, set_guard_is_inert_outside_training_mode)
{
    u32 globalAddr = 0x805A1000u;
    u32 modeAddr = 0x805A2000u;
    host::mem::setWord(0x805A00E0u, globalAddr);
    host::mem::setWord(globalAddr + 8, modeAddr);
    u8* players = reinterpret_cast<u8*>(modeAddr + 0x98u);
    memset(players, 0, 4 * 0x5C);
    u8* loader = static_cast<u8*>(host::mem::alloc(0x80, 8));
    dispatch::table().services.settings = stage6bSettingsBlock;

    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    drills::Session* s = reinterpret_cast<drills::Session*>(block + drills::kSessionOffset);
    s->magic = drills::kSessionMagic;
    s->version = drills::kSessionVersion;
    s->drill = 1;
    s->drillRan = 1;

    loader[0x5A] = 0x00; // Mario
    loader[0x5B] = 3;
    players[0] = 0x15; // Falco (changed)
    players[1] = 0;    // active
    players[5] = 0;
    players[6] = 2;

    // Outside Training (Versus): both tmDrillGuardBody and tmSetGuardBody must
    // immediately return without touching or swapping any fighter entry.
    host::fakes::match().training = false;
    tmDrillGuardBody(loader, players);
    tmSetGuardBody(loader, players);
    CHECK_EQ(0x15u, players[0]);
    CHECK_EQ(0u, players[5]);
    CHECK_EQ(2u, players[6]);
}
