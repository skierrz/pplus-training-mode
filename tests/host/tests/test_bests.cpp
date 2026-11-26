// The drills' bests on the SD card (src/menu/bests_file.cpp): loaded when
// the drill list makes the session, saved when it comes back after a match,
// against the card of host/card.h. As in test_storage.cpp, files are laid
// out the PC's way round: what is tested is the logic.
#include <sr/sr_common.h>
#include <string.h>

#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/settings.h"
#include "menu/bests_file.h"
#include "menu/session.h"
#include "shared/drill_session.h"

using namespace tm;
using drills::Session;

namespace {
    const char kFileA[] = "bests-a.bin";
    const char kFileB[] = "bests-b.bin";
    const u32 kBestsMagic = 0x544D4246; // "TMBF"
    const u32 kHead = sizeof(storage::FileHeader) + 4;

    // A boot: the stub's block is all zero; the drill list's first load
    // makes the session (and reads the card).
    Session& boot()
    {
        menu::bests::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        return *menu::attachSession(block);
    }

    // The list loaded again after a match, then a few rendered frames.
    Session& backFromMatch()
    {
        Session& s = *menu::attachSession(host::fakes::block());
        for (int i = 0; i < 4; i++)
        {
            menu::bests::update();
        }
        return s;
    }

    storage::FileHeader* header(const char* name)
    {
        return reinterpret_cast<storage::FileHeader*>(host::card::data(name));
    }

    u16* fileBests(const char* name)
    {
        return reinterpret_cast<u16*>(host::card::data(name) + kHead);
    }

    void reseal(const char* name)
    {
        storage::FileHeader* h = header(name);
        h->checksum = storage::checksum(host::card::data(name) + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    }
    enum {
        kSlotA = 7,
        kSlotB = 9,
        kSlotC = 16,
        kSlotD = 6,
        kSlotE = 3
    };
}

TEST(bests_file, the_better_of_two_bests)
{
    CHECK_EQ(7, drills::betterBest(kSlotA, 7, 3));
    CHECK_EQ(7, drills::betterBest(kSlotA, 0, 7));
    CHECK_EQ(25, drills::betterBest(kSlotB, 25, 0));
}

TEST(bests_file, an_empty_card_and_no_best_writes_nothing)
{
    Session& s = boot();
    CHECK_LOG("bests: files: a file not found (0), b file not found (0)");
    CHECK_EQ(0, s.best[kSlotA]);
    backFromMatch();
    CHECK_EQ(0, host::card::fileCount());
    CHECK_EQ(0, host::card::writes());
}

TEST(bests_file, saved_after_a_match_and_back_after_a_restart)
{
    Session& s = boot();
    s.best[kSlotA] = 6;
    s.best[kSlotB] = 21;
    backFromMatch();
    REQUIRE(host::card::has(kFileA));
    CHECK_EQ(1, header(kFileA)->sequence);
    CHECK_EQ(storage::kOk, storage::checkHeader(host::card::data(kFileA), header(kFileA)->size, kBestsMagic, 1, true));
    CHECK_EQ(6, fileBests(kFileA)[kSlotA]);
    CHECK_LOG("bests: saved to bests-a.bin (save 1, 100 bytes)");

    // Back again with nothing new: no write.
    int writes = host::card::writes();
    backFromMatch();
    CHECK_EQ(writes, host::card::writes());

    Session& t = boot();
    CHECK_LOG("bests: loaded from bests-a.bin (save 1): 2 drills with a best");
    CHECK_EQ(6, t.best[kSlotA]);
    CHECK_EQ(21, t.best[kSlotB]);

    // A better one goes to the other file; the next boot takes the newest.
    t.best[kSlotA] = 9;
    backFromMatch();
    REQUIRE(host::card::has(kFileB));
    CHECK_EQ(2, header(kFileB)->sequence);
    Session& u = boot();
    CHECK_LOG("bests: loaded from bests-b.bin (save 2)");
    CHECK_EQ(9, u.best[kSlotA]);
    menu::bests::detach();
}

// Reset All Settings is the lab's (main::resetSettings, the block's first
// 576 bytes, and the settings file): the session and these files are not
// part of it.
TEST(bests_file, reset_all_settings_leaves_the_bests)
{
    Session& s = boot();
    s.best[kSlotA] = 4;
    backFromMatch();
    REQUIRE(host::card::has(kFileA));
    int writes = host::card::writes();
    main::loadSettings(host::fakes::block());
    main::resetSettings();
    storage::settingsReset();
    CHECK_EQ(4, s.best[kSlotA]);
    backFromMatch();
    CHECK_EQ(writes, host::card::writes());
    CHECK_EQ(4, fileBests(kFileA)[kSlotA]);
    menu::bests::detach();
}

TEST(bests_file, a_locked_card_keeps_the_bests_for_the_session_only)
{
    Session& s = boot();
    s.best[kSlotC] = 3;
    backFromMatch();
    REQUIRE(host::card::has(kFileA));
    u8 before[128];
    memcpy(before, host::card::data(kFileA), host::card::size(kFileA));

    s.best[kSlotC] = 5;
    host::card::setLocked(true);
    host::log::clear();
    backFromMatch();
    CHECK(!host::card::has(kFileB));
    CHECK(memcmp(before, host::card::data(kFileA), host::card::size(kFileA)) == 0);
    CHECK_EQ(5, s.best[kSlotC]);
    CHECK(!host::log::contains("bests: saved"));
    CHECK_LOG("bests: save to bests-b.bin failed");

    // Unlocked: still to be saved.
    host::card::setLocked(false);
    backFromMatch();
    REQUIRE(host::card::has(kFileB));
    CHECK_EQ(5, fileBests(kFileB)[kSlotC]);
    menu::bests::detach();
}

TEST(bests_file, a_damaged_or_newer_file_is_not_used)
{
    Session& s = boot();
    s.best[kSlotD] = 4;
    backFromMatch();
    menu::bests::detach();

    fileBests(kFileA)[kSlotD] = 40; // not resealed
    Session& t = boot();
    CHECK_LOG("bests: files: a the file is damaged (0), b file not found (0)");
    CHECK_EQ(0, t.best[kSlotD]);

    // A newer format: not used, and nothing is written for the session.
    fileBests(kFileA)[kSlotD] = 4;
    header(kFileA)->format = 2;
    header(kFileA)->sequence = 9;
    reseal(kFileA);
    u8 newerA[128];
    u32 sizeA = host::card::size(kFileA);
    memcpy(newerA, host::card::data(kFileA), sizeA);
    Session& u = boot();
    CHECK_LOG("bests: files: a made by a newer version (9)");
    CHECK_EQ(0, u.best[kSlotD]);
    int writes = host::card::writes();
    u.best[kSlotD] = 2;
    backFromMatch();
    CHECK_EQ(writes, host::card::writes());
    CHECK(!host::card::has(kFileB));
    CHECK(memcmp(newerA, host::card::data(kFileA), sizeA) == 0);
    CHECK_EQ(2, u.best[kSlotD]);
    menu::bests::detach();

    // Dual-slot downgrade: valid format-1 slot A (seq 10) + newer-format slot B (seq 11).
    header(kFileA)->format = 1;
    header(kFileA)->sequence = 10;
    reseal(kFileA);
    u8 olderA[128];
    memcpy(olderA, host::card::data(kFileA), sizeA);
    u8 newerB[128];
    memcpy(newerB, olderA, sizeA);
    reinterpret_cast<storage::FileHeader*>(newerB)->format = 2;
    reinterpret_cast<storage::FileHeader*>(newerB)->sequence = 11;
    host::card::put(kFileB, newerB, sizeA);

    Session& v = boot();
    CHECK_EQ(0, v.best[kSlotD]);
    writes = host::card::writes();
    v.best[kSlotD] = 7;
    backFromMatch();
    CHECK_EQ(writes, host::card::writes());
    CHECK(memcmp(olderA, host::card::data(kFileA), sizeA) == 0);
    CHECK(memcmp(newerB, host::card::data(kFileB), sizeA) == 0);
    CHECK_EQ(7, v.best[kSlotD]);
    menu::bests::detach();
}

// A file with more slots than this version knows (a later one) gives the
// ones it knows; one with fewer (an earlier one) leaves the rest at none.
TEST(bests_file, files_with_more_or_fewer_slots)
{
    u8 file[320];
    memset(file, 0, sizeof(file));
    storage::FileHeader* h = reinterpret_cast<storage::FileHeader*>(file);
    u16* count = reinterpret_cast<u16*>(file + sizeof(storage::FileHeader));
    u16* best = reinterpret_cast<u16*>(file + kHead);
    h->magic = kBestsMagic;
    h->format = 1;
    h->headerSize = sizeof(storage::FileHeader);
    h->sequence = 1;
    best[kSlotA] = 8;
    best[40] = 99;
    *count = 48;
    h->size = kHead + 48 * 2;
    h->checksum = storage::checksum(file + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    host::card::put(kFileA, file, h->size);
    Session& s = boot();
    CHECK_EQ(8, s.best[kSlotA]);

    *count = 4; // ids 0-3: slot 7 is past them
    best[kSlotE] = 2;
    h->size = kHead + 4 * 2;
    h->checksum = storage::checksum(file + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    host::card::put(kFileA, file, h->size);
    Session& t = boot();
    CHECK_EQ(2, t.best[kSlotE]);
    CHECK_EQ(0, t.best[kSlotA]);
    menu::bests::detach();
}

TEST(bests_file, detach_finishes_a_save)
{
    Session& s = boot();
    host::card::setDelay(5);
    s.best[kSlotA] = 3;
    menu::attachSession(host::fakes::block());
    menu::bests::update();
    menu::bests::detach();
    REQUIRE(host::card::has(kFileA));
    CHECK_EQ(3, fileBests(kFileA)[kSlotA]);
    CHECK_EQ(1, s.cardSequence);
}

TEST(bests_file, a_short_read_uses_heap_and_does_not_double_free)
{
    Session& s = boot();
    s.best[kSlotA] = 5;
    backFromMatch(); // save 1 to a
    s.best[kSlotA] = 9;
    backFromMatch(); // save 2 to b
    menu::bests::detach();

    // Short read on slot 0's header read: slot 1 still loads, no non-heap free.
    host::card::setShortRead(1);
    Session& t = boot();
    CHECK_EQ(9, t.best[kSlotA]);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    menu::bests::detach();

    // Short read on slot 0's body read: slot 1 still loads, no double-free.
    host::card::setShortRead(2);
    Session& u = boot();
    CHECK_EQ(9, u.best[kSlotA]);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    menu::bests::detach();

    // Errored read returning status == 1 after readSDFile freed `buf`: no double-free.
    host::card::setReadStatus(1, 1, true);
    Session& v = boot();
    CHECK_EQ(9, v.best[kSlotA]);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    menu::bests::detach();

    // Write uses Heaps::Network buffer and frees it cleanly once complete; on kBusy
    // timeout at detach, leaves the heap block allocated so async DMA stays safe.
    v.best[kSlotA] = 12;
    host::card::setDelay(10);
    menu::attachSession(host::fakes::block());
    CHECK_EQ(1, host::fakes::heapBlocks(Heaps::Network));
    menu::bests::detach();
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::Network));
}

TEST(bests_file, corrupt_sequence_is_ignored_and_wrap_around_is_harmless)
{
    Session& s = boot();
    s.best[kSlotA] = 4;
    backFromMatch();
    REQUIRE(host::card::has(kFileA));
    menu::bests::detach();
    u32 size = host::card::size(kFileA);
    u8 good[128];
    REQUIRE(size <= sizeof(good));
    memcpy(good, host::card::data(kFileA), size);

    // Corrupt file with sequence 0xFFFFFFFF must not seed cardSequence.
    header(kFileA)->sequence = 0xFFFFFFFFu;
    fileBests(kFileA)[kSlotA] = 99; // bad checksum
    Session& t = boot();
    CHECK_EQ(0u, t.cardSequence);
    t.best[kSlotA] = 5;
    backFromMatch();
    CHECK_EQ(1u, header(kFileA)->sequence);
    menu::bests::detach();

    // Wrap-around at 0xFFFFFFFF: next save wraps to 1, and 1 beats 0xFFFFFFFF.
    host::card::remove(kFileB);
    host::card::put(kFileA, good, size);
    header(kFileA)->sequence = 0xFFFFFFFFu;
    reseal(kFileA);
    Session& u = boot();
    CHECK_EQ(0xFFFFFFFFu, u.cardSequence);
    CHECK_EQ(4, u.best[kSlotA]);
    u.best[kSlotA] = 8;
    backFromMatch();
    REQUIRE(host::card::has(kFileB));
    CHECK_EQ(1u, header(kFileB)->sequence);
    menu::bests::detach();

    Session& v = boot();
    CHECK_EQ(1u, v.cardSequence);
    CHECK_EQ(8, v.best[kSlotA]);
    v.best[kSlotA] = 12;
    backFromMatch();
    CHECK_EQ(2u, header(kFileA)->sequence);
    menu::bests::detach();

    Session& w = boot();
    CHECK_EQ(2u, w.cardSequence);
    CHECK_EQ(12, w.best[kSlotA]);
    menu::bests::detach();
}
