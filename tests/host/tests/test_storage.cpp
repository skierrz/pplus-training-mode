// The files on the SD card (src/main/storage/): the lab's settings file and
// a recording file, written and read by the mod's own code against the
// card of host/card.h, and the recorded inputs' encoding
// (src/main/record/slots.cpp).
//
// The mod writes its structures as they lie in memory, which on the console
// is big-endian. Here they lie the PC's way round, so a file written by
// these tests is not a file the console could read, and the console's files
// are not read here: what is tested is the logic (what is kept, what is
// refused, what an older or damaged file does), not the byte order.
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <sr/sr_common.h>

#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/record/slots.h"
#include "main/cpu/lab_data.h"
#include "main/cpu/options.h"
#include "main/game/overlays.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/notice.h"
#include "main/storage/recording_file.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/osd_place.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/settings_modal.h"
#include "menu/browser.h"
#include "menu/osd_quick.h"
#include "shared/game/pad_map.h"

using namespace tm;
using main::Settings;

namespace {
    const char kFileA[] = "settings-a.bin";
    const char kFileB[] = "settings-b.bin";

    // A boot: the stub's block is all zero, the match module resets its
    // settings and reads the card.
    u8* boot()
    {
        // Whatever an earlier test left in flight is finished first (the
        // mod's state outlives a test).
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
        return block;
    }

    // Rendered frames: the storage is polled once in each.
    void frames(int count = 4)
    {
        for (int i = 0; i < count; i++)
        {
            storage::onRender(host::fakes::match());
        }
    }

    void closeMenu()
    {
        storage::settingsMenuClosed();
        frames();
    }

    storage::FileHeader* header(const char* name)
    {
        return reinterpret_cast<storage::FileHeader*>(host::card::data(name));
    }

    // After a file's bytes were changed on purpose: the checksum a writer
    // would have given it.
    void reseal(const char* name)
    {
        storage::FileHeader* h = header(name);
        h->checksum = storage::checksum(host::card::data(name) + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    }

    storage::AreaEntry* areaEntry(const char* name, int id)
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

    // Settings a player might choose, in fields the card keeps and in
    // fields it does not.
    void choose(Settings& s)
    {
        s.general.ledgeHud = 0;
        s.general.ledgeMaxFrames = 48;
        s.general.cameraMode = 1;
        s.general.diDisplay = 1;
        s.controls.advanceButton = main::kAdvanceX;
        s.controls.advanceDelay = 12;
        s.controls.dpadDown = main::kDownFrameAdvance;
        s.display.hitboxes = 2;
        s.display.grabVisualizer = 1;
        s.info[0].size = main::kInfoLarge;
        s.info[1].rows[3] = 2;
        s.osd.wavedash = 0;
        s.osd.advantageTolerance = 3;
        s.osd.techSparkle = main::kTechSparkleBoth;
        s.recording.randomPercent[1] = 25;
        s.overlay[1].groups[4] = 1;
        s.techInvis.mode = main::kTechInvisAll;
        s.techInvis.delay = 4;
        s.characterRng.peachTurnip = main::kTurnipStitchFace;
        s.characterRng.peachSmash = main::kSmashFryingPan;
        s.fighterMechanics.samusBeam = main::kBeamIce;
        s.fighterMechanics.mewtwoHover = main::kMechAlways;
        s.cpu.options.recovery = cpu::kRecoveryGame;
        s.cpu.options.shieldTiming = cpu::kShieldTimingFirstGap;
        s.cpu.options.ledgeOption = cpu::kLedgeRoll;
        s.cpu.options.clatterEscape = cpu::kMashHigh;
        s.cpu.options.adaptiveMode = cpu::kAdaptiveTrack;
        s.comboTrainer.enabled = 1;
        s.comboTrainer.noclip = 1;
        s.comboTrainer.fixPosition = 1;
        s.comboTrainer.comboTimerAdjustment = -4;
        s.comboTrainer.fixedDamage = 35;
        s.comboTrainer.enabledMeters = main::kMeterHitstun | main::kMeterDi;
        // Kept for the session only.
        s.general.playerPercent = 123;
        s.general.lockPlayerPercent = 1;
        s.general.frameAdvance = 1;
        s.display.debugOverlay = 1;
        s.cpu.percent = 45;
        s.cpu.lockPercent = 1;
        s.recording.hmnSlot = 3;
        s.recording.loop = 1;
    }

    // What the next boot must hold after `chosen` was saved: the same,
    // with the session-only fields (settings_file.cpp lists them and says
    // why) back at the defaults.
    Settings afterReboot(const Settings& chosen, const Settings& defaults)
    {
        Settings e = chosen;
        e.general.playerPercent = defaults.general.playerPercent;
        e.general.lockPlayerPercent = defaults.general.lockPlayerPercent;
        e.general.frameAdvance = defaults.general.frameAdvance;
        e.general.gameSpeed = defaults.general.gameSpeed;
        e.display.debugOverlay = defaults.display.debugOverlay;
        e.display.hud = defaults.display.hud;
        e.display.models = defaults.display.models;
        e.display.stageModel = defaults.display.stageModel;
        e.display.effects = defaults.display.effects;
        e.cpu.percent = defaults.cpu.percent;
        e.cpu.lockPercent = defaults.cpu.lockPercent;
        e.cpu.options.control = defaults.cpu.options.control;
        e.comboTrainer.enabled = defaults.comboTrainer.enabled;
        u32 from = offsetof(main::RecordingSettings, hmnMode);
        u32 to = offsetof(main::RecordingSettings, reserved);
        memcpy(reinterpret_cast<u8*>(&e.recording) + from, reinterpret_cast<const u8*>(&defaults.recording) + from,
               to - from);
        return e;
    }

    void sameSettings(const char* file, int line, const Settings& expected, const Settings& found)
    {
        const u8* e = reinterpret_cast<const u8*>(&expected);
        const u8* f = reinterpret_cast<const u8*>(&found);
        for (u32 i = 0; i < sizeof(Settings); i++)
        {
            if (e[i] != f[i])
            {
                host::test::fail(file, line, "the settings differ at byte %lu of main::Settings: expected %d, found %d", i,
                                 e[i], f[i]);
                return;
            }
        }
    }
#define CHECK_SETTINGS(expected, found) sameSettings(__FILE__, __LINE__, (expected), (found))

    // Saves `chosen` over the defaults and leaves the card holding it.
    void saveChosen(Settings& defaults, Settings& chosen)
    {
        boot();
        defaults = main::settings();
        choose(main::settings());
        chosen = main::settings();
        closeMenu();
    }
}

// ---- the settings file ------------------------------------------------------

TEST(settings_file, a_first_boot_with_an_empty_card)
{
    u8* block = boot();
    CHECK_LOG("storage: settings files: a file not found (0), b file not found (0)");
    CHECK(!storage::noticeActive());
    const storage::State* state = storage::blockState(block);
    CHECK_EQ(storage::kStateMagic, state->magic);
    CHECK_EQ(0, state->sequence);

    // Closing the menu with nothing changed writes nothing.
    closeMenu();
    CHECK_EQ(0, host::card::writes());
    CHECK_EQ(0, host::card::fileCount());
    storage::detach();
}

TEST(settings_file, written_then_read_gives_the_same_settings)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    CHECK(host::card::folderMade());
    REQUIRE(host::card::has(kFileA));
    CHECK(!host::card::has(kFileB));
    CHECK_LOG("storage: settings saved to settings-a.bin (save 1,");

    const storage::FileHeader* h = header(kFileA);
    CHECK_EQ(storage::kSettingsMagic, h->magic);
    CHECK_EQ(storage::kSettingsFormat, h->format);
    CHECK_EQ(sizeof(storage::FileHeader), h->headerSize);
    CHECK_EQ(1, h->sequence);
    CHECK_EQ(storage::padded(h->size), host::card::size(kFileA));
    CHECK_EQ(storage::checksum(host::card::data(kFileA) + storage::kChecksumFrom, h->size - storage::kChecksumFrom),
             h->checksum);
    CHECK_EQ(storage::kOk, storage::checkHeader(host::card::data(kFileA), h->size, storage::kSettingsMagic,
                                                storage::kSettingsFormat, true));
    storage::detach();

    boot();
    CHECK_LOG("storage: settings loaded from settings-a.bin (save 1, layout 2): 16 areas, 0 with new fields, "
              "0 at defaults, 0 unknown");
    CHECK(!storage::noticeActive());
    CHECK_SETTINGS(afterReboot(chosen, defaults), main::settings());
    // What was loaded is what the card holds: closing the menu writes nothing.
    int writes = host::card::writes();
    closeMenu();
    CHECK_EQ(writes, host::card::writes());
    storage::detach();
}

// Each save goes to the other of the two files, so the last good one stays
// until the new one is complete; the higher save number wins at boot.
TEST(settings_file, saves_alternate_and_the_newest_is_loaded)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen); // save 1 to a
    main::settings().general.ledgeMaxFrames = 50;
    closeMenu(); // save 2 to b
    main::settings().general.ledgeMaxFrames = 60;
    closeMenu(); // save 3 to a
    REQUIRE(host::card::has(kFileA) && host::card::has(kFileB));
    CHECK_EQ(3, header(kFileA)->sequence);
    CHECK_EQ(2, header(kFileB)->sequence);
    storage::detach();

    boot();
    CHECK_LOG("storage: settings files: a done (3), b done (2)");
    CHECK_EQ(60, main::settings().general.ledgeMaxFrames);
    storage::detach();

    // The newest damaged: the other one is used, and nothing is said.
    host::card::data(kFileA)[80] ^= 0x40;
    boot();
    CHECK_LOG("storage: settings files: a the file is damaged (0), b done (2)");
    CHECK_LOG("storage: settings loaded from settings-b.bin (save 2,");
    CHECK_EQ(50, main::settings().general.ledgeMaxFrames);
    CHECK(!storage::noticeActive());
    // The next save replaces the damaged file, numbered after the good one.
    main::settings().general.ledgeMaxFrames = 40;
    closeMenu();
    CHECK_EQ(3, header(kFileA)->sequence);
    CHECK_EQ(storage::kOk, storage::checkHeader(host::card::data(kFileA), header(kFileA)->size, storage::kSettingsMagic,
                                                storage::kSettingsFormat, true));
    CHECK_EQ(2, header(kFileB)->sequence);
    storage::detach();
}

// Every way a lone file can be wrong: the defaults stay, the player is
// told, and no read is ever asked for bytes the file does not hold (the
// card fails the test if one is).
TEST(settings_file, a_damaged_file_is_refused)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    u32 size = host::card::size(kFileA);
    u8 good[1024];
    REQUIRE(size <= sizeof(good));
    memcpy(good, host::card::data(kFileA), size);

    struct Damage {
        const char* what;
        int kind;
    };
    static const Damage kDamages[] = {
        {"a changed byte in an area", 0},  {"a changed byte in the header's checked part", 1},
        {"another magic", 2},              {"cut short on the card", 3},
        {"shorter than a header", 4},      {"a size past what a settings file can be", 5},
        {"a size below the header", 6},    {"an area table that points outside the file", 7},
        {"an older format", 8},            {"an area count past the table's limit", 9},
    };
    for (u32 i = 0; i < sizeof(kDamages) / sizeof(kDamages[0]); i++)
    {
        host::card::put(kFileA, good, size);
        storage::FileHeader* h = header(kFileA);
        u32 fileSize = h->size;
        switch (kDamages[i].kind)
        {
        case 0:
            host::card::data(kFileA)[fileSize - 5] ^= 0x01;
            break;
        case 1:
            host::card::data(kFileA)[20] ^= 0x80; // the header's reserved bytes are under the checksum
            break;
        case 2:
            h->magic = storage::kRecordingMagic;
            break;
        case 3:
            host::card::put(kFileA, good, fileSize - 40);
            break;
        case 4:
            host::card::put(kFileA, good, 10);
            break;
        case 5:
            h->size = 4000;
            break;
        case 6:
            h->size = 24;
            break;
        case 7:
            areaEntry(kFileA, storage::kAreaOsd)->offset = fileSize + 100;
            reseal(kFileA);
            break;
        case 8:
            h->format = 0;
            break;
        case 9:
            reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileA) + sizeof(storage::FileHeader))->areaCount = 200;
            reseal(kFileA);
            break;
        }
        boot();
        if (!host::log::contains("storage: notice: Lab settings file damaged: defaults used"))
        {
            host::test::fail(__FILE__, __LINE__, "%s: expected the \"damaged: defaults used\" notice; the mod logged:\n%s",
                             kDamages[i].what, host::log::text());
        }
        const u8* e = reinterpret_cast<const u8*>(&defaults);
        if (memcmp(e, &main::settings(), sizeof(Settings)) != 0)
        {
            host::test::fail(__FILE__, __LINE__, "%s: the settings are not the defaults", kDamages[i].what);
        }
        storage::detach();
    }
}

// A file of a newer format is left alone: not used, and while it is the
// newest file on the card no settings write happens for the session (neither
// replacing the newer file nor superseding it in the other slot).
TEST(settings_file, a_newer_format_is_not_used_and_not_overwritten)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    u8 older[1024];
    u32 size = host::card::size(kFileA);
    header(kFileA)->sequence = 10;
    reseal(kFileA);
    memcpy(older, host::card::data(kFileA), size);

    // Lone newer-format file in slot A.
    header(kFileA)->format = storage::kSettingsFormat + 1;
    header(kFileA)->sequence = 9;
    u8 newer[1024];
    memcpy(newer, host::card::data(kFileA), size);

    boot();
    CHECK_LOG("storage: settings files: a made by a newer version (9), b file not found (0)");
    CHECK_LOG("storage: notice: Lab settings are from a newer version: not used");
    CHECK_SETTINGS(defaults, main::settings());
    int writes = host::card::writes();
    host::log::clear();
    main::settings().general.ledgeHud = 0;
    closeMenu();
    CHECK_EQ(writes, host::card::writes());
    CHECK(!host::card::has(kFileB));
    CHECK(memcmp(newer, host::card::data(kFileA), size) == 0);
    CHECK_EQ(0, main::settings().general.ledgeHud);
    CHECK(!host::log::contains("storage: notice: Lab settings are from a newer version"));
    // Even Reset Lab Settings must not write while a newer format is on the card.
    storage::settingsReset();
    closeMenu();
    CHECK_EQ(writes, host::card::writes());
    CHECK(!host::card::has(kFileB));
    storage::detach();

    // Dual-slot downgrade: valid format-1 slot A (seq 10) + newer-format slot B (seq 11).
    host::card::put(kFileA, older, size);
    memcpy(newer, older, size);
    reinterpret_cast<storage::FileHeader*>(newer)->format = storage::kSettingsFormat + 1;
    reinterpret_cast<storage::FileHeader*>(newer)->sequence = 11;
    host::card::put(kFileB, newer, size);

    boot();
    CHECK_LOG("storage: notice: Lab settings are from a newer version: not used");
    CHECK_SETTINGS(defaults, main::settings());
    writes = host::card::writes();
    main::settings().general.ledgeHud = 0;
    closeMenu();
    CHECK_EQ(writes, host::card::writes());
    CHECK(memcmp(older, host::card::data(kFileA), size) == 0);
    CHECK(memcmp(newer, host::card::data(kFileB), size) == 0);
    storage::detach();
}

// Upgrading from a pre-v2 (layout 1) SD card file cleanly adopts the fresh v2
// defaults, posts a calm informational toast, and rewrites layout 2 on the
// next menu close without requiring any manual change. A layout > kSettingsVersion
// file is refused without overwriting.
TEST(settings_file, layout_1_auto_upgrades_to_v2_defaults_and_newer_layout_is_refused)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();

    // Verify fresh v2 out-of-the-box defaults.
    CHECK_EQ(2, static_cast<int>(main::kSettingsVersion));
    CHECK_EQ(static_cast<int>(main::kOsdPresetEssentials), static_cast<int>(defaults.osd.preset));
    CHECK_EQ(4, static_cast<int>(defaults.osd.maxMessages));
    CHECK_EQ(1, static_cast<int>(defaults.osd.wavedash));
    CHECK_EQ(1, static_cast<int>(defaults.osd.lCancel));
    CHECK_EQ(1, static_cast<int>(defaults.osd.ledgedashInfo));
    CHECK_EQ(1, static_cast<int>(defaults.osd.actOos));
    CHECK_EQ(1, static_cast<int>(defaults.osd.frameAdvantage));
    CHECK_EQ(1, static_cast<int>(defaults.osd.frameAdvantageOnHit));
    CHECK_EQ(static_cast<int>(main::kTechSparkleWavedash), static_cast<int>(defaults.osd.techSparkle));
    CHECK_EQ(static_cast<int>(cpu::kRecoveryPPlusAi), static_cast<int>(defaults.cpu.options.recovery));
    CHECK_EQ(static_cast<int>(cpu::kShieldTimingFullMove), static_cast<int>(defaults.cpu.options.shieldTiming));
    CHECK_EQ(1, static_cast<int>(defaults.general.ledgeHud));

    // Rewrite settings-a.bin as an older layout 1 file holding non-default chosen values.
    storage::SettingsHead* head =
        reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileA) + sizeof(storage::FileHeader));
    CHECK_EQ(2, static_cast<int>(head->layout));
    head->layout = 1;
    reseal(kFileA);

    // Booting with a layout 1 save keeps defaults active and arms the v1 -> v2 upgrade modal.
    boot();
    CHECK(storage::settingsUpgradePending());
    CHECK_LOG("storage: settings layout 1 older than 2");
    CHECK_SETTINGS(defaults, main::settings());

    // Choosing Option 0 (Reset All Settings) resets all to v2 defaults and saves clean layout 2 immediately.
    int writesBefore = host::card::writes();
    storage::settingsUpgradeApply(storage::kUpgradeResetAll);
    frames();
    CHECK(!storage::settingsUpgradePending());
    CHECK(host::card::writes() > writesBefore);
    REQUIRE(host::card::has(kFileB));
    CHECK_EQ(2, static_cast<int>(header(kFileB)->sequence));
    storage::SettingsHead* headB =
        reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileB) + sizeof(storage::FileHeader));
    CHECK_EQ(2, static_cast<int>(headB->layout));
    storage::detach();

    // Next boot loads layout 2 cleanly with zero notice and no upgrade modal.
    boot();
    CHECK(!storage::settingsUpgradePending());
    CHECK_LOG("storage: settings loaded from settings-b.bin (save 2, layout 2)");
    CHECK(!storage::noticeActive());
    CHECK_SETTINGS(defaults, main::settings());
    storage::detach();

    // Layout > kSettingsVersion (same format 1): refused cleanly and never overwritten.
    headB = reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileB) + sizeof(storage::FileHeader));
    headB->layout = main::kSettingsVersion + 1;
    reseal(kFileB);

    boot();
    CHECK(!storage::settingsUpgradePending());
    CHECK_LOG("storage: notice: Lab settings are from a newer version: not used");
    CHECK_SETTINGS(defaults, main::settings());
    writesBefore = host::card::writes();
    main::settings().general.ledgeHud = 0;
    closeMenu();
    CHECK_EQ(writesBefore, host::card::writes());
    storage::detach();
}

// Comprehensive v1 -> v2 Settings Update Modal test covering all 3 choices
// (Reset All, Reset Affected, Keep My Settings), obsolete area trimming,
// clamping/sanitization, wait-for-neutral input navigation, and 1-area osd_quick load.
TEST(settings_file, v1_to_v2_upgrade_modal_three_choices_and_input_flow)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();

    // Build a realistic v1 file (kMaxAreas == 16) from settings-a.bin with:
    //  - kAreaGeneral truncated (used changed -> affected)
    //  - kAreaComboTrainer generation set to legacy gen 1 (gen changed -> affected)
    //  - kAreaFighterMechanics replaced by an obsolete area id 60 (missing in v1 -> affected, id 60 trimmed)
    //  - kAreaDisplay untouched (same gen + used -> unaffected, with out-of-range hitboxes=99 to verify clamp)
    //  - kAreaControls & kAreaOsd untouched (same gen + used -> unaffected)
    u8 v1Raw[storage::kSettingsFileCap];
    memset(v1Raw, 0, sizeof(v1Raw));
    const u8* orig = host::card::data(kFileA);
    const storage::SettingsHead* origHead =
        reinterpret_cast<const storage::SettingsHead*>(orig + sizeof(storage::FileHeader));
    u32 tableOff = sizeof(storage::FileHeader) + sizeof(storage::SettingsHead);
    const storage::AreaEntry* origEntries = reinterpret_cast<const storage::AreaEntry*>(orig + tableOff);

    u16 newCount = origHead->areaCount; // 16 == kMaxAreas
    u32 at = tableOff + newCount * sizeof(storage::AreaEntry);
    storage::AreaEntry* dstEntries = reinterpret_cast<storage::AreaEntry*>(v1Raw + tableOff);
    for (u16 i = 0; i < origHead->areaCount; i++)
    {
        dstEntries[i] = origEntries[i];
        dstEntries[i].offset = at;
        if (dstEntries[i].id == storage::kAreaFighterMechanics)
        {
            // Obsolete v1 area id 60 (must be trimmed on all 3 upgrade choices).
            dstEntries[i].id = 60;
            dstEntries[i].generation = 1;
            dstEntries[i].used = 4;
            v1Raw[at] = 0xDE;
            v1Raw[at + 1] = 0xAD;
            at += 4;
            continue;
        }
        if (dstEntries[i].id == storage::kAreaGeneral)
        {
            dstEntries[i].used = offsetof(main::GeneralSettings, diDisplay); // grown/changed
        }
        else if (dstEntries[i].id == storage::kAreaComboTrainer)
        {
            dstEntries[i].generation = 1; // legacy gen 1
        }
        u32 room = (static_cast<u32>(dstEntries[i].used) + 3u) & ~3u;
        memcpy(v1Raw + at, orig + origEntries[i].offset, dstEntries[i].used);
        // Inject an out-of-range byte in Display (hitboxes = 99) to verify sanitization.
        if (dstEntries[i].id == storage::kAreaDisplay)
        {
            v1Raw[at + offsetof(main::DisplaySettings, hitboxes)] = 99;
        }
        at += room;
    }

    storage::FileHeader* v1h = reinterpret_cast<storage::FileHeader*>(v1Raw);
    storage::SettingsHead* v1head = reinterpret_cast<storage::SettingsHead*>(v1Raw + sizeof(storage::FileHeader));
    v1head->areaCount = newCount;
    v1head->layout = 1;
    v1h->magic = storage::kSettingsMagic;
    v1h->format = storage::kSettingsFormat;
    v1h->headerSize = sizeof(storage::FileHeader);
    v1h->size = at;
    v1h->sequence = 1;
    v1h->checksum = storage::checksum(v1Raw + storage::kChecksumFrom, at - storage::kChecksumFrom);

    // ---- Choice 1: Reset Affected Settings ----
    host::card::reset();
    host::card::put(kFileA, v1Raw, at);
    boot();
    CHECK(storage::settingsUpgradePending());
    CHECK_EQ(3, storage::settingsUpgradeAffectedCount()); // kAreaGeneral + kAreaComboTrainer + missing kAreaFighterMechanics

    // Exercise modal input: held A on first frame is ignored (wait-for-neutral), then Down + A picks Choice 1.
    ui::settingsModalAttach();
    CHECK(ui::settingsModalOpen());
    CHECK_EQ(0, ui::settingsModalCursor());
    ui::SettingsModalInput heldA = {game::kPadA, game::kPadA, 0, 0, static_cast<u32>(gfPadType::GCC)};
    CHECK(!ui::settingsModalStep(heldA));
    CHECK(ui::settingsModalOpen());
    ui::SettingsModalInput neutral = {0, 0, 0, 0, static_cast<u32>(gfPadType::GCC)};
    CHECK(!ui::settingsModalStep(neutral));
    ui::SettingsModalInput down = {game::kPadDDown, game::kPadDDown, 0, 0, static_cast<u32>(gfPadType::GCC)};
    CHECK(!ui::settingsModalStep(down));
    CHECK_EQ(1, ui::settingsModalCursor());
    CHECK(!ui::settingsModalStep(neutral));
    CHECK(ui::settingsModalStep(heldA));
    CHECK(!ui::settingsModalOpen());
    frames();

    // Affected areas (General, ComboTrainer) reset to v2 defaults; untouched areas (Controls, OSD) preserved!
    CHECK_EQ(static_cast<int>(defaults.general.ledgeMaxFrames),
             static_cast<int>(main::settings().general.ledgeMaxFrames));
    CHECK_EQ(static_cast<int>(defaults.comboTrainer.noclip), static_cast<int>(main::settings().comboTrainer.noclip));
    CHECK_EQ(static_cast<int>(chosen.controls.advanceButton),
             static_cast<int>(main::settings().controls.advanceButton));
    CHECK_EQ(static_cast<int>(chosen.osd.wavedash), static_cast<int>(main::settings().osd.wavedash));
    // Display had out-of-range hitboxes=99 -> clamped to max valid value (3)!
    CHECK_EQ(3, static_cast<int>(main::settings().display.hitboxes));
    // Clean v2 file written to slot B with obsolete area 60 trimmed (16 areas, not 17).
    REQUIRE(host::card::has(kFileB));
    storage::SettingsHead* savedHead =
        reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileB) + sizeof(storage::FileHeader));
    CHECK_EQ(2, static_cast<int>(savedHead->layout));
    CHECK_EQ(16, static_cast<int>(savedHead->areaCount));
    CHECK(areaEntry(kFileB, 60) == NULL);
    storage::detach();

    // ---- Choice 2: Keep My Settings ----
    host::card::reset();
    host::card::put(kFileA, v1Raw, at);
    boot();
    CHECK(storage::settingsUpgradePending());
    storage::settingsUpgradeApply(storage::kUpgradeKeepMine);
    frames();
    // Compatible prefix of kAreaGeneral (ledgeMaxFrames) preserved; new fields past old used at defaults;
    // gen-mismatched kAreaComboTrainer safely reset; out-of-range Display clamped; obsolete area 60 trimmed.
    CHECK_EQ(static_cast<int>(chosen.general.ledgeMaxFrames),
             static_cast<int>(main::settings().general.ledgeMaxFrames));
    CHECK_EQ(static_cast<int>(defaults.general.diDisplay), static_cast<int>(main::settings().general.diDisplay));
    CHECK_EQ(static_cast<int>(defaults.comboTrainer.noclip), static_cast<int>(main::settings().comboTrainer.noclip));
    CHECK_EQ(static_cast<int>(chosen.controls.advanceButton),
             static_cast<int>(main::settings().controls.advanceButton));
    CHECK_EQ(3, static_cast<int>(main::settings().display.hitboxes));
    REQUIRE(host::card::has(kFileB));
    savedHead = reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileB) + sizeof(storage::FileHeader));
    CHECK_EQ(2, static_cast<int>(savedHead->layout));
    CHECK_EQ(16, static_cast<int>(savedHead->areaCount));
    CHECK(areaEntry(kFileB, 60) == NULL);
    storage::detach();

    // ---- Brand-new 1-area file created by Boot Menu Y: OSDs loads silently with zero modal/notice ----
    host::card::reset();
    main::OsdSettings quickOsd;
    menu::osdQuick::defaultOsdSettings(quickOsd);
    quickOsd.dashback = 1;
    quickOsd.maxMessages = 6;
    u8 oneArea[storage::kSettingsFileCap];
    u32 oneSize = 0;
    REQUIRE(menu::osdQuick::patchOsdAreaInFile(NULL, 0, quickOsd, oneArea, sizeof(oneArea), 1, oneSize));
    host::card::put(kFileA, oneArea, oneSize);
    boot();
    CHECK(!storage::settingsUpgradePending());
    CHECK(!storage::noticeActive());
    CHECK_EQ(1, static_cast<int>(main::settings().osd.dashback));
    CHECK_EQ(6, static_cast<int>(main::settings().osd.maxMessages));
    storage::detach();
}

// A file written before a field was added to an area: the area's entry
// says how many bytes the writer knew. Those are taken, the rest keep this
// version's defaults, and the player is told.
TEST(settings_file, an_area_from_an_older_version_keeps_new_fields_at_their_defaults)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    // As if General had ended before DI Display and Player Infinite Shields,
    // and OSD before Item Throw.
    storage::AreaEntry* general = areaEntry(kFileA, storage::kAreaGeneral);
    storage::AreaEntry* osd = areaEntry(kFileA, storage::kAreaOsd);
    REQUIRE(general != NULL && osd != NULL);
    CHECK_EQ(offsetof(main::GeneralSettings, reserved), general->used);
    general->used = offsetof(main::GeneralSettings, diDisplay);
    osd->used = offsetof(main::OsdSettings, itemThrow);
    reseal(kFileA);

    boot();
    CHECK_LOG("16 areas, 2 with new fields, 0 at defaults, 0 unknown");
    CHECK_LOG("storage: notice: Lab settings from an older version: new ones at defaults");
    Settings expected = afterReboot(chosen, defaults);
    expected.general.diDisplay = defaults.general.diDisplay;
    expected.general.playerInfiniteShields = defaults.general.playerInfiniteShields;
    expected.osd.itemThrow = defaults.osd.itemThrow;
    expected.osd.techSparkle = defaults.osd.techSparkle;
    CHECK(chosen.general.diDisplay != defaults.general.diDisplay);
    CHECK_SETTINGS(expected, main::settings());
    storage::detach();
}

// An older settings file whose Controls area ends before allowCodeMenu reads 0
// (blocked default), and a saved 1 (allowed) round-trips through kAreaControls.
TEST(settings_file, a_controls_area_without_allow_code_menu_reads_blocked_default)
{
    CHECK_EQ(2, static_cast<int>(main::kSettingsVersion));
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    CHECK_EQ(0, static_cast<int>(defaults.controls.allowCodeMenu));
    main::settings().controls.allowCodeMenu = 1;
    closeMenu();
    storage::detach();

    const char* newest = header(kFileB)->sequence > header(kFileA)->sequence ? kFileB : kFileA;
    storage::AreaEntry* controls = areaEntry(newest, storage::kAreaControls);
    REQUIRE(controls != NULL);
    CHECK_EQ(offsetof(main::ControlSettings, reserved), controls->used);

    // Current file preserves allowCodeMenu = 1 across a reboot.
    boot();
    CHECK_EQ(1, static_cast<int>(main::settings().controls.allowCodeMenu));
    storage::detach();

    // Truncate kAreaControls to its pre-allowCodeMenu length (8 bytes): loads
    // as 0 (blocked default) while keeping the earlier Controls fields.
    controls = areaEntry(newest, storage::kAreaControls);
    REQUIRE(controls != NULL);
    controls->used = offsetof(main::ControlSettings, allowCodeMenu);
    reseal(newest);

    boot();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_EQ(0, static_cast<int>(main::settings().controls.allowCodeMenu));
    CHECK_EQ(static_cast<int>(chosen.controls.advanceButton),
             static_cast<int>(main::settings().controls.advanceButton));
    storage::detach();
}

// An older settings file whose Controls area ends before menuStyle (9 bytes)
// migrates cleanly to 0 = Category Rail, while Top Tabs (1) round-trips cleanly.
TEST(settings_file, a_controls_area_without_menu_style_defaults_to_rail_c)
{
    CHECK_EQ(2, static_cast<int>(main::kSettingsVersion));
    CHECK_EQ(16u, sizeof(main::ControlSettings));
    CHECK_EQ(464u, sizeof(main::Settings));
    CHECK_EQ(33u, offsetof(main::Settings, controls.menuStyle));

    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    CHECK_EQ(static_cast<int>(main::kMenuStyleRail), static_cast<int>(defaults.controls.menuStyle));
    main::settings().controls.allowCodeMenu = 1;
    main::settings().controls.menuStyle = main::kMenuStyleTabs;
    closeMenu();
    storage::detach();

    const char* newest = header(kFileB)->sequence > header(kFileA)->sequence ? kFileB : kFileA;
    storage::AreaEntry* controls = areaEntry(newest, storage::kAreaControls);
    REQUIRE(controls != NULL);
    CHECK_EQ(offsetof(main::ControlSettings, reserved), controls->used);
    CHECK_EQ(10u, controls->used);

    // Current file preserves menuStyle = Top Tabs across a reboot.
    boot();
    CHECK_EQ(static_cast<int>(main::kMenuStyleTabs), static_cast<int>(main::settings().controls.menuStyle));
    CHECK_EQ(1, static_cast<int>(main::settings().controls.allowCodeMenu));
    storage::detach();

    // Truncate kAreaControls to its pre-menuStyle length (9 bytes): loads
    // as Category Rail (0) while keeping allowCodeMenu = 1 and earlier fields.
    controls = areaEntry(newest, storage::kAreaControls);
    REQUIRE(controls != NULL);
    controls->used = offsetof(main::ControlSettings, menuStyle);
    CHECK_EQ(9u, controls->used);
    reseal(newest);

    boot();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_EQ(static_cast<int>(main::kMenuStyleRail), static_cast<int>(main::settings().controls.menuStyle));
    CHECK_EQ(1, static_cast<int>(main::settings().controls.allowCodeMenu));
    CHECK_EQ(static_cast<int>(chosen.controls.advanceButton),
             static_cast<int>(main::settings().controls.advanceButton));
    storage::detach();
}

// An older settings file whose General area ends before playerInfiniteShields
// keeps the default On Until Hit (1), while Off (0) and On (2) round-trip.
TEST(settings_file, general_area_without_player_infinite_shields_defaults_to_until_hit)
{
    CHECK_EQ(2, static_cast<int>(main::kSettingsVersion));
    CHECK_EQ(16u, sizeof(main::GeneralSettings));
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    CHECK_EQ(static_cast<int>(cpu::kShieldsUntilHit), static_cast<int>(defaults.general.playerInfiniteShields));

    // Round-trip Off (0).
    main::settings().general.playerInfiniteShields = cpu::kShieldsOff;
    closeMenu();
    storage::detach();
    boot();
    CHECK_EQ(static_cast<int>(cpu::kShieldsOff), static_cast<int>(main::settings().general.playerInfiniteShields));

    // Round-trip On (2).
    main::settings().general.playerInfiniteShields = cpu::kShieldsOn;
    closeMenu();
    storage::detach();
    boot();
    CHECK_EQ(static_cast<int>(cpu::kShieldsOn), static_cast<int>(main::settings().general.playerInfiniteShields));
    storage::detach();

    // Truncate kAreaGeneral.used to before playerInfiniteShields (12 bytes):
    // loading must preserve the default On Until Hit (1), not zero-fill to Off.
    const char* newest = header(kFileB)->sequence > header(kFileA)->sequence ? kFileB : kFileA;
    storage::AreaEntry* area = areaEntry(newest, storage::kAreaGeneral);
    REQUIRE(area != NULL);
    CHECK_EQ(offsetof(main::GeneralSettings, reserved), area->used);
    area->used = offsetof(main::GeneralSettings, playerInfiniteShields);
    CHECK_EQ(12u, area->used);
    reseal(newest);

    boot();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    CHECK_EQ(static_cast<int>(cpu::kShieldsUntilHit),
             static_cast<int>(main::settings().general.playerInfiniteShields));
    storage::detach();
}

TEST(settings_file, areas_of_another_generation_or_unknown_or_longer)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    // Tech Invisibility's fields changed meaning; Controls became an area
    // this version does not know; OSD comes from a version with two more
    // bytes of fields.
    areaEntry(kFileA, storage::kAreaTechInvis)->generation = 2;
    areaEntry(kFileA, storage::kAreaOsd)->used += 2;
    areaEntry(kFileA, storage::kAreaControls)->id = 60;
    reseal(kFileA);

    boot();
    CHECK_LOG("14 areas, 0 with new fields, 2 at defaults, 1 unknown");
    CHECK_LOG("storage: notice: Lab settings from an older version: new ones at defaults");
    Settings expected = afterReboot(chosen, defaults);
    expected.techInvis = defaults.techInvis;
    expected.controls = defaults.controls;
    CHECK_SETTINGS(expected, main::settings());
    storage::detach();
}

// An area whose generation differs from this version's reads at defaults.
TEST(settings_file, character_rng_of_another_generation_reads_as_default)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    storage::AreaEntry* rng = areaEntry(kFileA, storage::kAreaCharacterRng);
    REQUIRE(rng != NULL);
    CHECK_EQ(1, rng->generation);
    rng->generation = 2;
    reseal(kFileA);

    boot();
    CHECK_LOG("15 areas, 0 with new fields, 1 at defaults, 0 unknown");
    Settings expected = afterReboot(chosen, defaults);
    expected.characterRng = defaults.characterRng;
    CHECK(chosen.characterRng.peachTurnip != defaults.characterRng.peachTurnip);
    CHECK_EQ(main::kTurnipDefault, main::settings().characterRng.peachTurnip);
    CHECK_EQ(main::kSmashDefault, main::settings().characterRng.peachSmash);
    CHECK_SETTINGS(expected, main::settings());
    storage::detach();
}

// Values no menu row allows (a file edited by hand, or damaged in a way
// the checksum was made to agree with) are brought into range on load.
TEST(settings_file, values_out_of_range_are_clamped_on_load)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    u8* file = host::card::data(kFileA);
    u32 from = sizeof(storage::FileHeader) + sizeof(storage::SettingsHead) + 16 * sizeof(storage::AreaEntry);
    for (u32 i = from; i < header(kFileA)->size; i++)
    {
        file[i] = 0xEE;
    }
    reseal(kFileA);

    boot();
    CHECK_LOG("storage: settings loaded from settings-a.bin");
    const ui::MenuDef& menu = ui::labMenu();
    int checked = 0;
    for (int p = 0; p < menu.pageCount; p++)
    {
        const ui::Page& page = *menu.pages[p];
        for (int i = 0; i < page.count; i++)
        {
            const ui::Option& o = page.options[i];
            if (!ui::hasValue(o) || (o.flags & (ui::kFlagUnavailable | ui::kFlagScratch)) != 0)
            {
                continue;
            }
            int v = ui::optionValue(menu, o);
            int lo = o.kind == ui::kKindInt ? o.min : 0;
            int hi = o.kind == ui::kKindToggle ? 1 : o.max;
            if (v < lo || v > hi)
            {
                host::test::fail(__FILE__, __LINE__, "page \"%s\" row \"%s\": value %d is outside %d..%d", page.title,
                                 o.name != NULL ? o.name : "?", v, lo, hi);
            }
            checked++;
        }
    }
    CHECK(checked > 100);
    NOTE("%d menu values checked", checked);
    storage::detach();
}

// Fields that are not bound directly to a visible lab-menu row (cpuTables,
// overlay Jumps) must also be brought into range on load.
TEST(settings_file, fields_outside_menu_rows_are_sanitized_on_load)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    u8* file = host::card::data(kFileA);
    u32 from = sizeof(storage::FileHeader) + sizeof(storage::SettingsHead) + 16 * sizeof(storage::AreaEntry);
    for (u32 i = from; i < header(kFileA)->size; i++)
    {
        file[i] = 0xFF;
    }
    reseal(kFileA);

    boot();
    CHECK_LOG("storage: settings loaded from settings-a.bin");
    const Settings& s = main::settings();
    CHECK(s.cpuTables.ready <= 1);
    for (int i = 0; i < cpu::kAdvancedHits; i++)
    {
        const cpu::AdvancedHit& h = s.cpuTables.hits[i];
        CHECK(h.logic <= cpu::kLogicCustom);
        CHECK(h.action[0] < ui::pages::kCounterGroundSize);
        CHECK(h.action[1] < ui::pages::kCounterAirSize);
        CHECK(h.action[2] < ui::pages::kCounterShieldSize);
        for (int k = 0; k < 3; k++)
        {
            CHECK(h.delay[k] <= 99);
        }
    }
    for (int side = 0; side < main::kSideCount; side++)
    {
        CHECK(s.overlay[side].groups[game::kGroupJumps] < game::kOverlayChoiceCount);
    }
    CHECK_EQ(static_cast<int>(main::kMeterAll), static_cast<int>(s.comboTrainer.enabledMeters));
    storage::detach();
}

TEST(settings_file, combo_meters_corrupt_bit7_is_masked_on_load_and_round_trips)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();

    const u8 subset = main::kMeterHitlag | main::kMeterDi | main::kMeterIntang;
    storage::AreaEntry* comboArea = areaEntry(kFileA, storage::kAreaComboTrainer);
    REQUIRE(comboArea != NULL);
    u8* metersByte = host::card::data(kFileA) + comboArea->offset
                     + offsetof(main::ComboTrainerSettings, enabledMeters);

    // Corrupt bit 7 set alongside valid bits: bit 7 is masked off, valid bits stay.
    *metersByte = static_cast<u8>(0x80u | subset);
    reseal(kFileA);
    boot();
    CHECK_EQ(static_cast<int>(subset), static_cast<int>(main::settings().comboTrainer.enabledMeters));
    storage::detach();

    // Only corrupt bit 7 set: loads cleanly as 0.
    *metersByte = 0x80u;
    reseal(kFileA);
    boot();
    CHECK_EQ(0, static_cast<int>(main::settings().comboTrainer.enabledMeters));

    // Round-trip a clean mask across save and reboot.
    const u8 nextMask = main::kMeterHitstun | main::kMeterLedgeIntang;
    main::settings().comboTrainer.enabledMeters = nextMask;
    closeMenu();
    storage::detach();
    boot();
    CHECK_EQ(static_cast<int>(nextMask), static_cast<int>(main::settings().comboTrainer.enabledMeters));

    // Corrupt bit 7 in the in-memory settings block is also masked on loadSettings().
    u8* block = host::fakes::block();
    main::settings().comboTrainer.enabledMeters = static_cast<u8>(0x80u | main::kMeterShieldstun);
    CHECK(!main::loadSettings(block));
    CHECK_EQ(static_cast<int>(main::kMeterShieldstun), static_cast<int>(main::settings().comboTrainer.enabledMeters));
    storage::detach();
}

// Pre-generation-2 SD saves wrote enabledMeters = 0x7F (all 7 meters on) while
// floating meters were hidden behind comboTrainer.enabled. Bumping
// kAreaComboTrainer to generation 2 resets legacy gen-1 blobs to safe defaults
// (enabledMeters == 0, noclip == 1, fixedDamageMode == 0) while preserving all
// other 15 areas.
TEST(settings_file, combo_trainer_gen1_legacy_save_resets_to_safe_defaults)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();

    storage::AreaEntry* comboArea = areaEntry(kFileA, storage::kAreaComboTrainer);
    REQUIRE(comboArea != NULL);
    CHECK_EQ(2, comboArea->generation);
    comboArea->generation = 1;
    comboArea->used = offsetof(main::ComboTrainerSettings, fixedDamageMode);
    u8* comboBytes = host::card::data(kFileA) + comboArea->offset;
    comboBytes[offsetof(main::ComboTrainerSettings, noclip)] = 0;
    comboBytes[offsetof(main::ComboTrainerSettings, enabledMeters)] = main::kMeterAll;
    reseal(kFileA);

    boot();
    CHECK_LOG("15 areas, 0 with new fields, 1 at defaults, 0 unknown");
    CHECK_LOG("storage: notice: Lab settings from an older version: new ones at defaults");
    CHECK_EQ(0, static_cast<int>(main::settings().comboTrainer.enabledMeters));
    CHECK_EQ(1, static_cast<int>(main::settings().comboTrainer.noclip));
    CHECK_EQ(static_cast<int>(main::kComboDmgCpu), static_cast<int>(main::settings().comboTrainer.fixedDamageMode));
    Settings expected = afterReboot(chosen, defaults);
    expected.comboTrainer = defaults.comboTrainer;
    CHECK_SETTINGS(expected, main::settings());
    storage::detach();
}

TEST(settings_file, a_locked_or_full_card)
{
    boot();
    main::settings().general.ledgeHud = 0;
    host::card::setLocked(true);
    closeMenu();
    CHECK_LOG("storage: notice: Lab settings not saved: the file could not be written");
    CHECK_EQ(0, host::card::fileCount());

    // Unlocked: the change is still to be saved.
    host::card::setLocked(false);
    closeMenu();
    REQUIRE(host::card::has(kFileA));
    CHECK_EQ(1, header(kFileA)->sequence);

    // No room for the second save: half a file lands in b, and the player
    // is told.
    main::settings().general.ledgeMaxFrames = 40;
    host::card::setFull(true);
    host::log::clear();
    closeMenu();
    CHECK_LOG("storage: notice: Lab settings not saved: the SD card is full");
    REQUIRE(host::card::has(kFileB));
    storage::detach();

    // The next boot refuses the half file and loads the whole one.
    host::card::setFull(false);
    boot();
    CHECK_LOG("storage: settings files: a done (1), b the file is damaged (0)");
    CHECK_EQ(0, main::settings().general.ledgeHud);
    CHECK(main::settings().general.ledgeMaxFrames != 40);
    storage::detach();
}

TEST(settings_file, a_slow_card_and_a_second_close_while_writing)
{
    boot();
    host::card::setDelay(3);
    main::settings().general.ledgeHud = 0;
    storage::settingsMenuClosed();
    frames(2);
    CHECK_EQ(0, host::card::fileCount()); // the folder request is still running
    // Changed and closed again before the first save is done.
    main::settings().general.ledgeMaxFrames = 40;
    storage::settingsMenuClosed();
    frames(40);
    REQUIRE(host::card::has(kFileA) && host::card::has(kFileB));
    CHECK_EQ(1, header(kFileA)->sequence);
    CHECK_EQ(2, header(kFileB)->sequence);
    storage::detach();

    host::card::setDelay(0);
    boot();
    CHECK_EQ(40, main::settings().general.ledgeMaxFrames);
    storage::detach();
}

// The module is unloaded while a save is in flight: detach finishes it.
TEST(settings_file, detach_finishes_a_save)
{
    boot();
    host::card::setDelay(5);
    main::settings().general.ledgeHud = 0;
    storage::settingsMenuClosed();
    frames(1);
    storage::detach();
    CHECK_LOG("storage: settings write finished at detach: done");
    REQUIRE(host::card::has(kFileA));
    CHECK_EQ(storage::kOk, storage::checkHeader(host::card::data(kFileA), header(kFileA)->size, storage::kSettingsMagic,
                                                storage::kSettingsFormat, true));
}

TEST(settings_file, reset_lab_settings_is_saved_even_when_nothing_differs)
{
    boot();
    storage::settingsReset();
    closeMenu();
    CHECK(host::card::has(kFileA));
    storage::detach();
}

TEST(settings_file, osd_position_defaults_to_sides_and_keeps_existing_file)
{
    boot();
    CHECK_EQ(osd::kPositionSides, main::settings().osd.position);
    main::settings().osd.position = osd::kPositionHud;
    closeMenu();
    REQUIRE(host::card::has(kFileA));
    storage::detach();

    // An existing settings file keeps the player's stored HUD choice.
    boot();
    CHECK_EQ(osd::kPositionHud, main::settings().osd.position);
    // Reset All Settings restores Sides.
    main::resetSettings();
    CHECK_EQ(osd::kPositionSides, main::settings().osd.position);
    storage::detach();
}

TEST(settings_file, a_short_read_uses_heap_and_does_not_double_free)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen); // save 1 to a
    main::settings().general.ledgeMaxFrames = 55;
    closeMenu(); // save 2 to b
    storage::detach();

    // Short read on slot 0's header read: slot 0 fails cleanly, slot 1 loads.
    host::card::setShortRead(1);
    boot();
    CHECK_EQ(55, main::settings().general.ledgeMaxFrames);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    storage::detach();

    // Short read on slot 0's body read: slot 0 fails cleanly, slot 1 loads.
    host::card::setShortRead(2);
    boot();
    CHECK_EQ(55, main::settings().general.ledgeMaxFrames);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    storage::detach();
}

TEST(settings_file, corrupt_sequence_is_ignored_and_wrap_around_is_harmless)
{
    Settings defaults, chosen;
    saveChosen(defaults, chosen);
    storage::detach();
    u32 size = host::card::size(kFileA);
    u8 good[1024];
    REQUIRE(size <= sizeof(good));
    memcpy(good, host::card::data(kFileA), size);

    // A lone file that failed its checksum must never seed state->sequence.
    header(kFileA)->sequence = 0xFFFFFFFFu;
    host::card::data(kFileA)[80] ^= 0x40; // bad checksum
    u8* block = boot();
    CHECK_EQ(0u, storage::blockState(block)->sequence);
    main::settings().general.ledgeMaxFrames = 44;
    closeMenu();
    REQUIRE(host::card::has(kFileA));
    CHECK_EQ(1u, header(kFileA)->sequence);
    storage::detach();

    // Valid slot A (seq 5) + slot B with valid checksum but corrupt area table (seq 6):
    // slot B is rejected and slot A wins.
    host::card::put(kFileA, good, size);
    header(kFileA)->sequence = 5;
    reseal(kFileA);
    host::card::put(kFileB, good, size);
    header(kFileB)->sequence = 6;
    reinterpret_cast<storage::SettingsHead*>(host::card::data(kFileB) + sizeof(storage::FileHeader))->areaCount = 200;
    reseal(kFileB);
    block = boot();
    CHECK_EQ(5u, storage::blockState(block)->sequence);
    CHECK_EQ(48, main::settings().general.ledgeMaxFrames);
    storage::detach();

    // Wrap-around at 0xFFFFFFFF: next save wraps to 1, and 1 beats 0xFFFFFFFF.
    host::card::remove(kFileB);
    host::card::put(kFileA, good, size);
    header(kFileA)->sequence = 0xFFFFFFFFu;
    reseal(kFileA);
    block = boot();
    CHECK_EQ(0xFFFFFFFFu, storage::blockState(block)->sequence);
    CHECK_EQ(48, main::settings().general.ledgeMaxFrames);
    main::settings().general.ledgeMaxFrames = 58;
    closeMenu();
    REQUIRE(host::card::has(kFileB));
    CHECK_EQ(1u, header(kFileB)->sequence);
    storage::detach();

    block = boot();
    CHECK_EQ(1u, storage::blockState(block)->sequence);
    CHECK_EQ(58, main::settings().general.ledgeMaxFrames);
    main::settings().general.ledgeMaxFrames = 62;
    closeMenu();
    CHECK_EQ(2u, header(kFileA)->sequence);
    storage::detach();

    block = boot();
    CHECK_EQ(2u, storage::blockState(block)->sequence);
    CHECK_EQ(62, main::settings().general.ledgeMaxFrames);
    storage::detach();
}

// ---- the areas --------------------------------------------------------------

TEST(areas, the_table_and_the_bounds)
{
    boot();
    storage::detach();
    const u8* settings = reinterpret_cast<const u8*>(&main::settings());
    u8 out[1024];
    u16 count = 0;
    u32 n = storage::writeAreas(out, sizeof(out), 40, settings, storage::kAllAreas, count);
    CHECK_EQ(16, count);
    CHECK(n > 16 * sizeof(storage::AreaEntry));
    // Every area lies inside what was written, 4-byte aligned, in order.
    const storage::AreaEntry* table = reinterpret_cast<const storage::AreaEntry*>(out);
    u32 next = 40 + 16 * sizeof(storage::AreaEntry);
    for (int i = 0; i < count; i++)
    {
        CHECK_EQ(next, table[i].offset);
        CHECK_EQ(table[i].id == storage::kAreaComboTrainer ? 2 : 1, table[i].generation);
        next += (table[i].used + 3u) & ~3u;
    }
    CHECK_EQ(40 + n, next);

    // A buffer one byte too small holds nothing.
    CHECK_EQ(0, storage::writeAreas(out, n - 1, 40, settings, storage::kAllAreas, count));
    // A mask picks areas.
    storage::writeAreas(out, sizeof(out), 0, settings, storage::areaBit(storage::kAreaCpu) | storage::areaBit(storage::kAreaOsd),
                        count);
    CHECK_EQ(2, count);

    // Reading: a table or an area outside the file is refused before
    // anything is copied.
    u8 file[1024];
    memset(file, 0, sizeof(file));
    n = storage::writeAreas(file + 40, sizeof(file) - 40, 40, settings, storage::kAllAreas, count);
    Settings into;
    memset(&into, 0x55, sizeof(into));
    Settings untouched = into;
    storage::AreaStats stats;
    CHECK(!storage::readAreas(file, 40 + n, 40 + n, 16, reinterpret_cast<u8*>(&into), storage::kAllAreas, stats));
    CHECK(!storage::readAreas(file, 40 + n, 40, 18, reinterpret_cast<u8*>(&into), storage::kAllAreas, stats));
    // One byte short of the last area's fields (the padding after them is
    // not part of the area).
    const storage::AreaEntry& last = reinterpret_cast<const storage::AreaEntry*>(file + 40)[15];
    CHECK(!storage::readAreas(file, last.offset + last.used - 1, 40, 16, reinterpret_cast<u8*>(&into),
                              storage::kAllAreas, stats));
    CHECK_SETTINGS(untouched, into);
    CHECK(storage::readAreas(file, 40 + n, 40, 16, reinterpret_cast<u8*>(&into), storage::kAllAreas, stats));
    CHECK_EQ(16, stats.copied);
    CHECK_EQ(0, stats.grown + stats.reset + stats.skipped);
    CHECK(memcmp(&into.osd, &main::settings().osd, offsetof(main::OsdSettings, reserved)) == 0);

    // An area longer than this version knows (a later version added
    // fields): only the bytes this version has fields for are taken.
    u8 later[64];
    memset(later, 0xAA, sizeof(later));
    storage::AreaEntry* entry = reinterpret_cast<storage::AreaEntry*>(later);
    entry->id = storage::kAreaInfoHmn;
    entry->generation = 1;
    entry->used = sizeof(main::InfoSettings) + 8;
    entry->offset = 16;
    memset(&into, 0x55, sizeof(into));
    CHECK(storage::readAreas(later, sizeof(later), 0, 1, reinterpret_cast<u8*>(&into), storage::kAllAreas, stats));
    CHECK_EQ(1, stats.copied);
    CHECK_EQ(0, stats.grown);
    CHECK_EQ(15, stats.reset);
    u32 known = offsetof(main::InfoSettings, reserved);
    const u8* info = reinterpret_cast<const u8*>(&into.info[0]);
    for (u32 i = 0; i < sizeof(main::InfoSettings) * 2; i++)
    {
        if (info[i] != (i < known ? 0xAA : 0x55))
        {
            host::test::fail(__FILE__, __LINE__, "byte %lu from the start of the HMN info area: found %02x", i, info[i]);
            break;
        }
    }
}

// ---- the settings block -----------------------------------------------------

TEST(settings_block, an_unknown_block_is_reset_and_a_known_one_is_kept_and_clamped)
{
    u8* block = host::fakes::block();
    CHECK(main::loadSettings(block)); // all zero: reset
    Settings defaults = main::settings();
    CHECK_EQ(main::kSettingsMagic, defaults.magic);
    CHECK_EQ(main::kSettingsVersion, defaults.version);
    CHECK_EQ(sizeof(Settings), defaults.size);

    main::settings().general.ledgeMaxFrames = 44;
    CHECK(!main::loadSettings(block)); // the next match: kept
    CHECK_EQ(44, main::settings().general.ledgeMaxFrames);

    // An older layout's block, or one of another size: reset.
    main::settings().version = main::kSettingsVersion - 1;
    CHECK(main::loadSettings(block));
    CHECK_SETTINGS(defaults, main::settings());
    main::settings().size = sizeof(Settings) - 4;
    main::settings().general.ledgeMaxFrames = 44;
    CHECK(main::loadSettings(block));
    CHECK_SETTINGS(defaults, main::settings());

    // A kept block with a value out of range: clamped, not reset.
    main::settings().general.ledgeMaxFrames = 250;
    main::settings().general.ledgeHud = 7;
    CHECK(!main::loadSettings(block));
    CHECK_EQ(64, main::settings().general.ledgeMaxFrames);
    CHECK_EQ(1, main::settings().general.ledgeHud);
}

// ---- recorded inputs --------------------------------------------------------

namespace {
    record::Input input(int t)
    {
        record::Input in;
        memset(&in, 0, sizeof(in));
        in.buttons = (t % 3 == 0 ? 0x0100u : 0) | (t % 5 == 0 ? 0x0002u : 0);
        in.x = static_cast<float>((t * 3) % 161 - 80) / 80.0f;
        in.y = static_cast<float>((t * 13) % 161 - 80) / 80.0f;
        in.cx = t % 4 == 0 ? 0.3125f : 0.0f;
        in.cy = t % 6 == 0 ? -1.0f : 0.0f;
        in.l = t % 10 < 3 ? 0.35f : 0.0f;
        in.r = t % 9 == 0 ? 1.0f : 0.0f;
        in.press = static_cast<u8>(t % 7 == 0 ? 1 : 0);
        return in;
    }

    bool sameInput(const record::Input& a, const record::Input& b)
    {
        return a.buttons == b.buttons && memcmp(&a.x, &b.x, 6 * sizeof(float)) == 0 && a.press == b.press;
    }

    void checkSlot(const char* file, int line, const record::Slot& s, int start, int end)
    {
        if (s.start != start || s.end != end)
        {
            host::test::fail(file, line, "the slot holds frames %d..%d, expected %d..%d", s.start, s.end, start, end);
            return;
        }
        for (int t = start; t < end; t++)
        {
            record::Input out;
            if (!record::slotRead(s, t, out) || !sameInput(input(t), out))
            {
                host::test::fail(file, line, "frame %d does not read back as it was written", t);
                return;
            }
        }
    }
#define CHECK_SLOT(slot, start, end) checkSlot(__FILE__, __LINE__, (slot), (start), (end))

    void fill(record::Slot& s, int start, int end)
    {
        for (int t = start; t < end; t++)
        {
            record::slotWrite(s, t, input(t));
        }
    }

    record::Slot* slots()
    {
        record::storageFree();
        if (!record::storageAllocate())
        {
            return NULL;
        }
        return record::slotAt(0, 0);
    }
}

TEST(record_slots, frames_read_back_bit_for_bit)
{
    record::Slot* s = slots();
    REQUIRE(s != NULL);
    CHECK_EQ(1, host::fakes::heapBlocks(Heaps::FighterTechqniq));
    CHECK(record::slotEmpty(*s));
    record::Input out;
    CHECK(!record::slotRead(*s, 0, out));

    fill(*s, 10, 400);
    CHECK_SLOT(*s, 10, 400);
    CHECK_EQ(0, s->lossy);
    CHECK(!record::slotRead(*s, 9, out));
    CHECK(!record::slotRead(*s, 400, out));
    // 161 stick positions a side were written, and a handful of the others.
    CHECK_EQ(161, s->count[record::kTableX]);
    CHECK_EQ(161, s->count[record::kTableY]);
    CHECK_EQ(2, s->count[record::kTableCx]);

    // The last frame of all, and one past it.
    record::slotClear(*s);
    record::slotWrite(*s, record::kMaxFrames - 1, input(1));
    record::slotWrite(*s, record::kMaxFrames, input(2));
    CHECK_EQ(record::kMaxFrames, s->end);
    CHECK_EQ(record::kMaxFrames - 1, s->start);

    // Frames skipped since the slot last ended hold nothing.
    record::slotClear(*s);
    record::slotWrite(*s, 5, input(5));
    record::slotWrite(*s, 9, input(9));
    CHECK_EQ(10, s->end);
    REQUIRE(record::slotRead(*s, 7, out));
    record::Input none;
    memset(&none, 0, sizeof(none));
    CHECK(sameInput(none, out));

    // Dropping the front moves every frame down.
    record::slotClear(*s);
    fill(*s, 0, 100);
    record::slotDropFront(*s, 30);
    CHECK_EQ(0, s->start);
    CHECK_EQ(70, s->end);
    REQUIRE(record::slotRead(*s, 0, out));
    CHECK(sameInput(input(30), out));
    REQUIRE(record::slotRead(*s, 69, out));
    CHECK(sameInput(input(99), out));
    record::slotDropFront(*s, 70);
    CHECK(record::slotEmpty(*s));

    // A copy is the same slot.
    fill(*s, 3, 50);
    record::Slot* other = record::slotAt(1, 5);
    record::slotCopy(*other, *s);
    CHECK_SLOT(*other, 3, 50);

    record::storageFree();
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::FighterTechqniq));
    CHECK_LOG("back to baseline: yes");
}

// A table holds 256 values; past that the nearest is stored and the frame
// counted as stored approximately.
TEST(record_slots, a_full_table_stores_the_nearest_value)
{
    record::Slot* s = slots();
    REQUIRE(s != NULL);
    record::Input in;
    memset(&in, 0, sizeof(in));
    for (int t = 0; t < 256; t++)
    {
        in.x = static_cast<float>(t) / 256.0f;
        record::slotWrite(*s, t, in);
    }
    CHECK_EQ(256, s->count[record::kTableX]);
    CHECK_EQ(0, s->lossy);
    // A 257th value, nearest to 100/256.
    in.x = 100.2f / 256.0f;
    record::slotWrite(*s, 256, in);
    CHECK_EQ(1, s->lossy);
    record::Input out;
    REQUIRE(record::slotRead(*s, 256, out));
    CHECK_EQ(100.0f / 256.0f, out.x);
    // A value already in the table is still exact.
    in.x = 7.0f / 256.0f;
    record::slotWrite(*s, 257, in);
    CHECK_EQ(1, s->lossy);
    // Buttons above the 16 bits a frame keeps count as approximate too.
    in.buttons = 0x00010001;
    record::slotWrite(*s, 258, in);
    CHECK_EQ(2, s->lossy);

    // Alter Inputs: an edit with a value new to the full table says so.
    in.buttons = 0;
    in.x = 3.0f / 256.0f;
    CHECK(record::slotEdit(*s, 10, in));
    in.x = 0.99999f;
    CHECK(!record::slotEdit(*s, 10, in));
    CHECK(!record::slotEdit(*s, 400, in)); // outside the slot
    record::storageFree();
}

// ---- the recording file -----------------------------------------------------

namespace {
    const char kRecording[] = "260102-030405.tmr";

    // A match with positions saved and two slots recorded, exported.
    bool exported(Settings& atExport)
    {
        boot();
        if (slots() == NULL)
        {
            return false;
        }
        fill(*record::slotAt(0, 0), 0, 300);
        fill(*record::slotAt(1, 2), 20, 120);
        host::fakes::Recording& r = host::fakes::recording();
        r.positions = true;
        r.who.hmn = 0;
        r.who.cpu = 1;
        record::LitePlacement hmn = {-20.5f, 0.0f, 0.0f, 1.0f, 12.0f, 50.0f, true};
        record::LitePlacement cpu = {35.25f, 10.0f, 0.0f, -1.0f, 80.0f, 0.0f, false};
        r.place[0] = hmn;
        r.place[1] = cpu;
        host::fakes::setPlayer(0, 0x00, 0, 1, 2);
        host::fakes::setPlayer(1, 0x06, 1, 3, 4);
        host::fakes::setStage(0x21);
        // Project+'s matched alternate-stage button word (StageFiles.asm:761-763
        // saves the matched ASL entry at 0x8053EFBA and zeroes 0x800B9EA2).
        // Both halves of the word at 0x8053EFB8 are set for host/mem.h.
        host::mem::setWord(0x800B9EA0u, 0x00000000u);
        host::mem::setWord(0x8053EFB8u, 0x04000400u);
        Settings& s = main::settings();
        s.recording.randomPercent[0] = 30;
        s.recording.hmnSlot = 1;
        s.recording.cpuSlot = 3;
        s.recording.loop = 1;
        s.cpu.percent = 77;
        s.cpu.lockPercent = 1;
        atExport = s;
        CHECK(storage::exportAvailable());
        storage::exportRecording();
        frames();
        return host::card::has(kRecording);
    }
}

TEST(recording_file, export_writes_the_header_the_browser_lists)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    CHECK_LOG("storage: exported 260102-030405.tmr");
    CHECK_LOG("storage: notice: Exported 260102-030405.tmr - load from Training Menu (X)");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network)); // the temporary buffer is back

    const u8* file = host::card::data(kRecording);
    const storage::FileHeader* h = header(kRecording);
    CHECK_EQ(storage::kRecordingMagic, h->magic);
    CHECK_EQ(storage::kRecordingFormat, h->format);
    CHECK_EQ(0, h->sequence);
    CHECK_EQ(storage::padded(h->size), host::card::size(kRecording));
    CHECK_EQ(storage::kOk,
             storage::checkHeader(file, h->size, storage::kRecordingMagic, storage::kRecordingFormat, true));
    // What the browser reads (the first kRecordingListBytes) passes the
    // header check without the rest.
    CHECK_EQ(storage::kOk, storage::checkHeader(file, storage::kRecordingListBytes, storage::kRecordingMagic,
                                                storage::kRecordingFormat, false));

    const storage::RecordingInfo* info = reinterpret_cast<const storage::RecordingInfo*>(file + sizeof(storage::FileHeader));
    CHECK_STR("260102-030405", info->name);
    CHECK_EQ(0x00, info->hmnKind);
    CHECK_EQ(1, info->hmnColorFile);
    CHECK_EQ(2, info->hmnColor);
    CHECK_EQ(0x06, info->cpuKind);
    CHECK_EQ(3, info->cpuColorFile);
    CHECK_EQ(4, info->cpuColor);
    CHECK_EQ(1, info->hasSelect);
    CHECK_EQ(0x21, info->stage);
    CHECK_EQ(0x0400, info->altStage);
    CHECK_EQ(0, info->hazardOff);
    CHECK_EQ(300, info->frames);
    CHECK_EQ(0x01, info->hmnSlots);
    CHECK_EQ(0x04, info->cpuSlots);
    CHECK_EQ(2026, info->year);
    CHECK_EQ(1, info->month);
    CHECK_EQ(2, info->day);
    CHECK_EQ(3, info->hour);
    CHECK_EQ(4, info->minute);
    CHECK_EQ(5, info->second);

    const storage::RecordingHead* head =
        reinterpret_cast<const storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
    CHECK_EQ(3, head->areaCount);
    CHECK_EQ(2, head->slotCount);
    storage::detach();
    record::storageFree();
}

TEST(recording_file, a_second_export_in_the_same_second_gets_another_name)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    storage::exportRecording();
    frames();
    CHECK(host::card::has("260102-030405-2.tmr"));
    storage::detach();
    record::storageFree();
}

TEST(recording_file, export_captures_stage_hazard_off_and_older_files_default_on)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    const storage::RecordingInfo* infoOn =
        reinterpret_cast<const storage::RecordingInfo*>(host::card::data(kRecording) + sizeof(storage::FileHeader));
    CHECK_EQ(0, infoOn->hazardOff);

    // Flip m_isHazardOff (gmGlobalModeMelee+0x29 bit 0x20) on and export again.
    host::fakes::setHazardOff(true);
    storage::exportRecording();
    frames();
    REQUIRE(host::card::has("260102-030405-2.tmr"));
    const u8* offFile = host::card::data("260102-030405-2.tmr");
    const storage::RecordingInfo* infoOff =
        reinterpret_cast<const storage::RecordingInfo*>(offFile + sizeof(storage::FileHeader));
    CHECK_EQ(1, infoOff->hazardOff);
    CHECK_EQ(0, infoOff->reserved[0]);
    CHECK_EQ(0, infoOff->reserved[1]);
    CHECK_EQ(0, infoOff->reserved[2]);

    // Older .tmr files had zeroed reserved[4] at offset 0x5C..0x5F: decode as Hazards ON (0).
    u8 legacy[0x4000];
    u32 onCard = host::card::size("260102-030405-2.tmr");
    REQUIRE(onCard <= sizeof(legacy));
    memcpy(legacy, offFile, onCard);
    memset(legacy + sizeof(storage::FileHeader) + offsetof(storage::RecordingInfo, hazardOff), 0, 4);
    storage::FileHeader* lh = reinterpret_cast<storage::FileHeader*>(legacy);
    lh->checksum = storage::checksum(legacy + storage::kChecksumFrom, lh->size - storage::kChecksumFrom);
    CHECK_EQ(storage::kOk,
             storage::checkHeader(legacy, lh->size, storage::kRecordingMagic, storage::kRecordingFormat, true));
    const storage::RecordingInfo* legacyInfo =
        reinterpret_cast<const storage::RecordingInfo*>(legacy + sizeof(storage::FileHeader));
    CHECK_EQ(0, legacyInfo->hazardOff);

    storage::detach();
    record::storageFree();
}

TEST(recording_file, export_then_import_restores_slots_placements_and_settings)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();

    // The next match: empty slots, default settings, and the menu
    // module's hand-over in the block.
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    Settings defaults = main::settings();
    main::settings().cpu.percent = 150; // this match's own CPU damage
    for (int side = 0; side < record::kSides; side++)
    {
        for (int i = 0; i < record::kSlotCount; i++)
        {
            record::slotClear(*record::slotAt(side, i));
        }
    }
    fill(*record::slotAt(1, 4), 0, 10); // something an import must clear
    storage::Handoff* hand = storage::blockHandoff(block);
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, kRecording);
    host::log::clear();
    storage::attach(block);
    CHECK_EQ(0, hand->magic); // taken once
    CHECK_LOG("waits for the match");

    // Not while the match is paused or not running.
    host::fakes::match().paused = true;
    frames();
    CHECK_EQ(0, host::card::reads());
    host::fakes::match().paused = false;
    frames();

    CHECK_LOG("storage: notice: Loaded 260102-030405");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_SLOT(*record::slotAt(0, 0), 0, 300);
    CHECK_SLOT(*record::slotAt(1, 2), 20, 120);
    CHECK(record::slotEmpty(*record::slotAt(1, 4)));
    CHECK(record::slotEmpty(*record::slotAt(0, 1)));

    const host::fakes::Recording& r = host::fakes::recording();
    CHECK_EQ(1, r.imports);
    CHECK(memcmp(&r.imported[0], &r.place[0], sizeof(float) * 6) == 0);
    CHECK_EQ(true, r.imported[0].haveShield);
    CHECK(memcmp(&r.imported[1], &r.place[1], sizeof(float) * 6) == 0);
    CHECK_EQ(false, r.imported[1].haveShield);

    // The Recording and CPU areas come from the file; the CPU's damage and
    // its lock stay this match's; nothing else changes.
    Settings expected = defaults;
    expected.recording = atExport.recording;
    expected.cpu = atExport.cpu;
    expected.cpuTables = atExport.cpuTables;
    expected.cpu.percent = 150;
    expected.cpu.lockPercent = defaults.cpu.lockPercent;
    CHECK_SETTINGS(expected, main::settings());
    storage::detach();
    record::storageFree();
}

TEST(recording_file, a_damaged_recording_is_refused)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();
    u8 good[0x4000];
    u32 onCard = host::card::size(kRecording);
    REQUIRE(onCard <= sizeof(good));
    memcpy(good, host::card::data(kRecording), onCard);

    static const char* const kWhat[] = {"a changed byte", "cut short on the card", "a slot index past the last",
                                        "a slot longer than the file", "placements outside the file"};
    for (int kind = 0; kind < 5; kind++)
    {
        host::card::put(kRecording, good, onCard);
        u8* file = host::card::data(kRecording);
        storage::RecordingHead* head =
            reinterpret_cast<storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
        storage::SlotHead* first =
            reinterpret_cast<storage::SlotHead*>(file + head->placements + 2 * sizeof(storage::Placement));
        switch (kind)
        {
        case 0:
            file[size - 9] ^= 0x10;
            break;
        case 1:
            host::card::put(kRecording, good, size - 64);
            break;
        case 2:
            first->index = 6;
            reseal(kRecording);
            break;
        case 3:
            first->end = 3000;
            reseal(kRecording);
            break;
        case 4:
            head->placements = size - 8;
            reseal(kRecording);
            break;
        }
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::Handoff* hand = storage::blockHandoff(block);
        hand->magic = storage::kHandoffMagic;
        hand->size = size;
        strcpy(hand->name, kRecording);
        host::fakes::recording().imports = 0;
        host::log::clear();
        storage::attach(block);
        frames();
        if (!host::log::contains("storage: notice: Import failed: the file is damaged") || host::fakes::recording().imports != 0)
        {
            host::test::fail(__FILE__, __LINE__, "%s: expected \"Import failed: the file is damaged\" and no import; the mod "
                                                 "logged:\n%s",
                             kWhat[kind], host::log::text());
        }
        CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
        storage::detach();
    }
    record::storageFree();
}

static void runImport(const char* name, u32 size)
{
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::Handoff* hand = storage::blockHandoff(block);
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, name);
    host::fakes::recording().imports = 0;
    host::log::clear();
    storage::attach(block);
    frames();
}

static u32 floatBits(float f)
{
    union {
        float f;
        u32 u;
    } v;
    v.f = f;
    return v.u;
}

static float bitsToFloat(u32 u)
{
    union {
        float f;
        u32 u;
    } v;
    v.u = u;
    return v.f;
}

TEST(recording_file, malformed_headers_palettes_and_placements_are_refused)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();
    u8 good[0x4000];
    u32 onCard = host::card::size(kRecording);
    REQUIRE(onCard <= sizeof(good));
    memcpy(good, host::card::data(kRecording), onCard);

    // 1. Files shorter than FileHeader + RecordingInfo + RecordingHead (96, 100)
    //    and a 104-byte file whose placements overlap the header.
    const u32 shortSizes[3] = {storage::kRecordingListBytes, storage::kRecordingListBytes + 4,
                               sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo)
                                   + sizeof(storage::RecordingHead)};
    for (int s = 0; s < 3; s++)
    {
        u8 tiny[128];
        memset(tiny, 0, sizeof(tiny));
        storage::FileHeader* h = reinterpret_cast<storage::FileHeader*>(tiny);
        h->magic = storage::kRecordingMagic;
        h->format = storage::kRecordingFormat;
        h->headerSize = sizeof(storage::FileHeader);
        h->size = shortSizes[s];
        h->checksum = storage::checksum(tiny + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
        host::card::put(kRecording, tiny, storage::padded(h->size));
        runImport(kRecording, h->size);
        CHECK_LOG("storage: notice: Import failed: the file is damaged");
        CHECK_EQ(0, host::fakes::recording().imports);
        CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
        storage::detach();
    }

    // 2. Bad palette indices, bad palette floats, and bad placement floats.
    static const char* const kCases[] = {
        "frame x index out of table",  "frame y index out of table",    "frame cx index out of table",
        "frame cy index out of table", "frame lr index out of table",   "palette NaN float",
        "palette Inf float",           "palette out-of-range float",    "lr palette NaN float",
        "placement x NaN",             "placement y Inf",               "placement x out of range",
        "placement lr NaN",            "placement damage NaN",          "placement damage negative",
        "placement damage above 999",  "placement shield NaN",          "placement shield above 200",
        "placement shield below -200", "placements inside header",
    };
    const int kCaseCount = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));
    for (int kind = 0; kind < kCaseCount; kind++)
    {
        host::card::put(kRecording, good, onCard);
        u8* file = host::card::data(kRecording);
        storage::RecordingHead* head =
            reinterpret_cast<storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
        storage::Placement* place = reinterpret_cast<storage::Placement*>(file + head->placements);
        storage::SlotHead* sh =
            reinterpret_cast<storage::SlotHead*>(file + head->placements + 2 * sizeof(storage::Placement));
        u32* tableX = reinterpret_cast<u32*>(reinterpret_cast<u8*>(sh) + sizeof(storage::SlotHead));
        u32* lrTable = tableX + sh->count[0] + sh->count[1] + sh->count[2] + sh->count[3];
        record::Packed* f0 = reinterpret_cast<record::Packed*>(lrTable + 2 * sh->lrCount);
        switch (kind)
        {
        case 0:
            f0->x = static_cast<u8>(sh->count[0]);
            break;
        case 1:
            f0->y = static_cast<u8>(sh->count[1]);
            break;
        case 2:
            f0->cx = static_cast<u8>(sh->count[2]);
            break;
        case 3:
            f0->cy = static_cast<u8>(sh->count[3]);
            break;
        case 4:
            f0->lr = static_cast<u8>(sh->lrCount);
            break;
        case 5:
            tableX[0] = 0x7FC00000u; // NaN
            break;
        case 6:
            tableX[0] = 0x7F800000u; // +Inf
            break;
        case 7:
            tableX[0] = floatBits(10.0f);
            break;
        case 8:
            lrTable[0] = 0x7FC00000u; // NaN
            break;
        case 9:
            place[0].x = bitsToFloat(0x7FC00000u);
            break;
        case 10:
            place[0].y = bitsToFloat(0x7F800000u);
            break;
        case 11:
            place[0].x = 1e9f;
            break;
        case 12:
            place[1].lr = bitsToFloat(0x7FC00000u);
            break;
        case 13:
            place[0].damage = bitsToFloat(0x7FC00000u);
            break;
        case 14:
            place[0].damage = -1.0f;
            break;
        case 15:
            place[0].damage = 1000.0f;
            break;
        case 16:
            place[0].shield = bitsToFloat(0x7FC00000u);
            break;
        case 17:
            place[0].shield = 250.0f;
            break;
        case 18:
            place[0].shield = -250.0f;
            break;
        case 19:
            head->placements = sizeof(storage::FileHeader);
            break;
        }
        reseal(kRecording);
        // Existing slot 1/5 must not be cleared if the import is refused.
        fill(*record::slotAt(1, 5), 0, 5);
        runImport(kRecording, size);
        if (!host::log::contains("storage: notice: Import failed: the file is damaged")
            || host::fakes::recording().imports != 0)
        {
            host::test::fail(__FILE__, __LINE__, "%s: expected damaged notice and no import; logged:\n%s", kCases[kind],
                             host::log::text());
        }
        CHECK(!record::slotEmpty(*record::slotAt(1, 5)));
        CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
        storage::detach();
    }
    record::storageFree();
}

// Shield is only meaningful when haveShield is set (earlier exports left it
// uninitialised when no shield was read), and a save on a shield-break frame
// can hold a negative shield. Both must load.
TEST(recording_file, unshielded_garbage_and_negative_shield_load)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    u32 onCard = host::card::size(kRecording);
    u8 good[16384];
    REQUIRE(onCard <= sizeof(good));
    memcpy(good, host::card::data(kRecording), onCard);
    storage::detach();

    // 1. haveShield == 0 with NaN in shield (and boundary damage/lr/pos) loads.
    host::card::put(kRecording, good, onCard);
    u8* file = host::card::data(kRecording);
    storage::RecordingHead* head =
        reinterpret_cast<storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
    storage::Placement* place = reinterpret_cast<storage::Placement*>(file + head->placements);
    place[0].haveShield = 0;
    place[0].shield = bitsToFloat(0x7FC00000u); // NaN
    place[0].damage = 999.0f;
    place[0].lr = 0.0f;
    place[0].x = -3500.0f;
    reseal(kRecording);
    runImport(kRecording, size);
    CHECK_LOG("storage: notice: Loaded 260102-030405");
    CHECK_EQ(1, host::fakes::recording().imports);
    CHECK(!host::fakes::recording().imported[0].haveShield);
    CHECK_EQ(0.0f, host::fakes::recording().imported[0].shield);
    CHECK_EQ(999.0f, host::fakes::recording().imported[0].damage);
    CHECK_EQ(-3500.0f, host::fakes::recording().imported[0].x);
    storage::detach();

    // 2. haveShield == 1 with negative shield (shield-break frame) loads.
    host::card::put(kRecording, good, onCard);
    file = host::card::data(kRecording);
    head = reinterpret_cast<storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
    place = reinterpret_cast<storage::Placement*>(file + head->placements);
    place[0].haveShield = 1;
    place[0].shield = -15.0f;
    reseal(kRecording);
    runImport(kRecording, size);
    CHECK_LOG("storage: notice: Loaded 260102-030405");
    CHECK_EQ(1, host::fakes::recording().imports);
    CHECK(host::fakes::recording().imported[0].haveShield);
    CHECK_EQ(-15.0f, host::fakes::recording().imported[0].shield);
    storage::detach();
    record::storageFree();
}

TEST(recording_file, unterminated_info_name_does_not_overread)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();

    u8* file = host::card::data(kRecording);
    storage::RecordingInfo* info = reinterpret_cast<storage::RecordingInfo*>(file + sizeof(storage::FileHeader));
    memset(info->name, 'A', sizeof(info->name));
    info->hmnKind = 'B';
    reseal(kRecording);

    runImport(kRecording, size);
    CHECK_EQ(1, host::fakes::recording().imports);
    CHECK(!host::log::contains("AB"));
    CHECK_LOG("storage: notice: Loaded AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
    storage::detach();
    record::storageFree();
}

// Importing a recording that carries Advanced Counter tables must refresh the
// Advanced Counter page's working copy to Hit 1's imported values, so editing
// one row on Hit 1 does not overwrite the imported hit with stale values.
TEST(recording_file, import_refreshes_advanced_counter_page)
{
    // Export a legitimate recording where Hit 1 was edited to Custom and the
    // player then viewed Hit 2 before exporting.
    boot();
    REQUIRE(slots() != NULL);
    fill(*record::slotAt(0, 0), 0, 60);
    host::fakes::Recording& r = host::fakes::recording();
    r.positions = true;
    r.who.hmn = 0;
    r.who.cpu = 1;
    record::LitePlacement hmn = {-20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, true};
    record::LitePlacement cpuP = {20.0f, 0.0f, 0.0f, -1.0f, 0.0f, 60.0f, true};
    r.place[0] = hmn;
    r.place[1] = cpuP;
    host::fakes::setPlayer(0, 0x00, 0, 0, 0);
    host::fakes::setPlayer(1, 0x06, 1, 0, 0);
    host::fakes::setStage(0x21);
    Settings& exp = main::settings();
    cpu::advancedAttach();
    exp.cpu.options.advHit = 1;
    exp.cpu.options.advLogic = cpu::kLogicCustom;
    exp.cpu.options.advGround = 13;
    exp.cpu.options.advAir = 11;
    exp.cpu.options.advShield = 4;
    exp.cpu.options.advDelayGround = 5;
    exp.cpu.options.advDelayAir = 6;
    exp.cpu.options.advDelayShield = 7;
    cpu::advancedValueChanged();
    // Switch page view to Hit 2 before exporting (Hit 2 remains Default).
    exp.cpu.options.advHit = 2;
    cpu::advancedHitChanged();
    storage::exportRecording();
    frames();
    REQUIRE(host::card::has(kRecording));
    u32 size = header(kRecording)->size;
    storage::detach();

    // Next match: Hit 1 starts with different pre-import values, then imports.
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    Settings& s = main::settings();
    s.cpuTables.hits[0].logic = cpu::kLogicDisable;
    s.cpuTables.hits[0].action[0] = 3;
    cpu::advancedAttach();
    storage::Handoff* hand = storage::blockHandoff(block);
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, kRecording);
    storage::attach(block);
    frames();

    CHECK_LOG("storage: notice: Loaded 260102-030405");
    CHECK_EQ(1, s.cpu.options.advHit);
    CHECK_EQ(static_cast<int>(cpu::kLogicCustom), static_cast<int>(s.cpu.options.advLogic));
    CHECK_EQ(13, s.cpu.options.advGround);
    CHECK_EQ(11, s.cpu.options.advAir);
    CHECK_EQ(4, s.cpu.options.advShield);
    CHECK_EQ(5, s.cpu.options.advDelayGround);
    CHECK_EQ(6, s.cpu.options.advDelayAir);
    CHECK_EQ(7, s.cpu.options.advDelayShield);

    // Editing one row on Hit 1 must preserve the rest of Hit 1's imported values.
    s.cpu.options.advDelayGround = 9;
    cpu::advancedValueChanged();
    CHECK_EQ(static_cast<int>(cpu::kLogicCustom), static_cast<int>(s.cpuTables.hits[0].logic));
    CHECK_EQ(13, s.cpuTables.hits[0].action[0]);
    CHECK_EQ(11, s.cpuTables.hits[0].action[1]);
    CHECK_EQ(4, s.cpuTables.hits[0].action[2]);
    CHECK_EQ(9, s.cpuTables.hits[0].delay[0]);
    CHECK_EQ(6, s.cpuTables.hits[0].delay[1]);
    CHECK_EQ(7, s.cpuTables.hits[0].delay[2]);
    storage::detach();
    record::storageFree();
}

TEST(recording_file, imported_out_of_range_settings_are_sanitized)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();

    u8* file = host::card::data(kRecording);
    const storage::RecordingHead* head =
        reinterpret_cast<const storage::RecordingHead*>(file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo));
    const storage::AreaEntry* table = reinterpret_cast<const storage::AreaEntry*>(
        file + sizeof(storage::FileHeader) + sizeof(storage::RecordingInfo) + sizeof(storage::RecordingHead));
    for (int i = 0; i < head->areaCount; i++)
    {
        if (table[i].id == storage::kAreaCpuTables)
        {
            cpu::CpuTables* t = reinterpret_cast<cpu::CpuTables*>(file + table[i].offset);
            for (int h = 0; h < cpu::kAdvancedHits; h++)
            {
                t->hits[h].logic = 200;
                t->hits[h].action[0] = 200;
                t->hits[h].action[1] = 200;
                t->hits[h].action[2] = 200;
                t->hits[h].delay[0] = 200;
                t->hits[h].delay[1] = 200;
                t->hits[h].delay[2] = 200;
            }
        }
    }
    reseal(kRecording);

    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    storage::Handoff* hand = storage::blockHandoff(block);
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, kRecording);
    storage::attach(block);
    frames();

    CHECK_LOG("storage: notice: Loaded 260102-030405");
    const Settings& s = main::settings();
    for (int h = 0; h < cpu::kAdvancedHits; h++)
    {
        CHECK(s.cpuTables.hits[h].logic <= cpu::kLogicCustom);
        CHECK(s.cpuTables.hits[h].action[0] < ui::pages::kCounterGroundSize);
        CHECK(s.cpuTables.hits[h].action[1] < ui::pages::kCounterAirSize);
        CHECK(s.cpuTables.hits[h].action[2] < ui::pages::kCounterShieldSize);
        for (int k = 0; k < 3; k++)
        {
            CHECK(s.cpuTables.hits[h].delay[k] <= 99);
        }
    }
    storage::detach();
    record::storageFree();
}

TEST(recording_file, what_export_and_import_refuse)
{
    boot();
    // No positions saved.
    CHECK(!storage::exportAvailable());
    storage::exportRecording();
    CHECK_LOG("storage: notice: Export: save positions first");
    CHECK_EQ(0, host::card::writes());
    storage::detach();

    // A hand-over whose size no recording can have.
    u8* block = host::fakes::block();
    storage::Handoff* hand = storage::blockHandoff(block);
    hand->magic = storage::kHandoffMagic;
    hand->size = 12;
    strcpy(hand->name, "x.tmr");
    host::log::clear();
    storage::attach(block);
    CHECK_LOG("storage: import x.tmr refused: size 12");
    frames();
    CHECK_EQ(0, host::card::reads());
    storage::detach();

    // No room for the file in memory: given up after the wait, and said.
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();
    hand = storage::blockHandoff(host::fakes::block());
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, kRecording);
    host::fakes::setHeapRoom(Heaps::Network, 64);
    host::log::clear();
    storage::attach(host::fakes::block());
    frames(1000);
    CHECK_LOG("storage: notice: Import failed: not enough memory");
    CHECK_EQ(0, host::card::reads());
    storage::detach();
    record::storageFree();
}

TEST(recording_file, coexists_with_resident_network_buffers)
{
    // Resident extra working buffers (~32 KiB) in Heaps::Network must coexist
    // cleanly with recording export and import when headroom remains >= 512 KiB.
    host::fakes::setHeapRoom(Heaps::Network, 0x143800);
    void* resident = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), 32768, 32);
    REQUIRE(resident != NULL);
    CHECK_EQ(1, host::fakes::heapBlocks(Heaps::Network));

    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();

    runImport(kRecording, size);
    CHECK_EQ(1, host::fakes::recording().imports);
    CHECK_EQ(1, host::fakes::heapBlocks(Heaps::Network));
    storage::detach();
    record::storageFree();

    // During costume decompression (~0x37D40 left in Network), import waits out
    // the headroom reserve instead of allocating.
    storage::Handoff* hand = storage::blockHandoff(host::fakes::block());
    hand->magic = storage::kHandoffMagic;
    hand->size = size;
    strcpy(hand->name, kRecording);
    host::fakes::setHeapRoom(Heaps::Network, 0x37D40 + 32768);
    int readsBefore = host::card::reads();
    host::log::clear();
    storage::attach(host::fakes::block());
    frames(1000);
    CHECK_LOG("storage: notice: Import failed: not enough memory");
    CHECK_EQ(readsBefore, host::card::reads());
    storage::detach();
    record::storageFree();

    free(resident);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
}

TEST(recording_file, a_short_read_does_not_double_free)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    u32 size = header(kRecording)->size;
    storage::detach();

    host::card::setShortRead(1);
    runImport(kRecording, size);
    CHECK_LOG("storage: notice: Import failed: the file is damaged");
    CHECK_EQ(0, host::fakes::recording().imports);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    storage::detach();
    record::storageFree();
}

TEST(recording_file, browser_short_read_uses_heap_and_does_not_double_free)
{
    Settings atExport;
    REQUIRE(exported(atExport));
    storage::detach();
    record::storageFree();

    // Header read hits a short read: must use a heap buffer and not double-free.
    menu::browser::attach();
    host::card::setShortRead(1);
    menu::browser::open(0);
    for (int i = 0; i < 6; i++)
    {
        menu::browser::update();
    }
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    menu::browser::detach();

    // Header read succeeds, then A triggers whole-file verify which hits a short read.
    menu::browser::attach();
    host::card::setShortRead(2);
    menu::browser::open(0);
    for (int i = 0; i < 6; i++)
    {
        menu::browser::update();
    }
    menu::browser::Input in = {0, 0, 0, 0};
    bool starting = false;
    menu::browser::step(0, in, starting); // clears s_waitRelease
    in.pressed = 0x100;                // A: startVerify
    menu::browser::step(0, in, starting);
    for (int i = 0; i < 6; i++)
    {
        menu::browser::update();
    }
    CHECK(!starting);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    menu::browser::detach();
}

// ---- the file helpers -------------------------------------------------------

TEST(file_io, paths_checksum_and_listing)
{
    boot();
    storage::detach();
    char path[storage::kPathMax];
    CHECK(storage::makePath(path, "settings-a.bin", true));
    CHECK_STR("sd:/Project+/tm/settings-a.bin", path);
    CHECK(storage::makePath(path, "x.tmr", false));
    CHECK_STR("/Project+/tm/x.tmr", path);
    // A name that does not fit is refused, not cut.
    char longName[120];
    memset(longName, 'n', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';
    CHECK(!storage::makePath(path, longName, true));

    // FNV-1a, 32 bits: the published values for "" and "a".
    CHECK_EQ(0x811C9DC5u, storage::checksum("", 0));
    CHECK_EQ(0xE40C292Cu, storage::checksum("a", 1));
    CHECK_EQ(32, storage::padded(1));
    CHECK_EQ(32, storage::padded(32));
    CHECK_EQ(64, storage::padded(33));

    CHECK_STR("the file is damaged", storage::resultText(storage::kCorrupt));
    CHECK_STR("?", storage::resultText(storage::kResultCount));
    CHECK_STR("?", storage::resultText(-1));

    // Listing by suffix, and sizes.
    host::card::put("one.tmr", "12345678", 8);
    host::card::put("two.tmr", "1234", 4);
    host::card::put("settings-a.bin", "1", 1);
    struct Seen {
        static void each(const char* name, u32 size, void* ctx)
        {
            int* total = static_cast<int*>(ctx);
            total[0]++;
            total[1] += static_cast<int>(size);
            total[2] += strcmp(name, "one.tmr") == 0 || strcmp(name, "two.tmr") == 0 ? 1 : 0;
        }
    };
    int total[3] = {0, 0, 0};
    CHECK_EQ(2, storage::listFiles(storage::kRecordingSuffix, Seen::each, total));
    CHECK_EQ(2, total[0]);
    CHECK_EQ(12, total[1]);
    CHECK_EQ(2, total[2]);
    CHECK_EQ(8, storage::fileSize("one.tmr"));
    CHECK_EQ(-1, storage::fileSize("three.tmr"));
    CHECK(storage::exists("two.tmr"));

    // A read of more than the file holds is answered without asking the
    // game; a missing file likewise.
    storage::Job job;
    u8 into[64];
    int status = 0;
    CHECK(job.read("two.tmr", into, 8));
    CHECK_EQ(storage::kCorrupt, job.wait(10, status));
    CHECK(job.read("three.tmr", into, 8));
    CHECK_EQ(storage::kMissing, job.wait(10, status));
    CHECK_EQ(0, host::card::reads());
    CHECK(job.read("one.tmr", into, 8));
    CHECK_EQ(storage::kOk, job.wait(10, status));
    CHECK(memcmp(into, "12345678", 8) == 0);

    // One request at a time; a slow one is "busy" until it is done.
    host::card::setDelay(5);
    CHECK(job.remove("one.tmr"));
    CHECK(!job.read("two.tmr", into, 4));
    CHECK_EQ(storage::kBusy, job.wait(2, status));
    CHECK_EQ(storage::kOk, job.wait(10, status));
    CHECK(!host::card::has("one.tmr"));
}
