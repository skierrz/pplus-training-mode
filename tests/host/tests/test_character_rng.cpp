// Character RNG swap safety (src/extra/character_rng.cpp): cached Peach odds
// pointer is tied to live g_ftCommonDataAccesser data, never written after
// FitPeach.pac unloads, and re-found when Peach swaps back in.
#define TM_MODULE_EXTRA 1
#include "extra/character_rng.cpp"

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/settings.h"

namespace {
    using namespace tm;

    static bool s_fakeSwapBusy = false;

    struct PeachDataBlock {
        u8 header[0xA0];
        s32 odds;
        extra::ItemEntry items[3];
    };

    struct DededeDataBlock {
        u8 header[0xA0];
        float table[12];
    };

    static PeachDataBlock* makePeachData(s32 initialOdds)
    {
        PeachDataBlock* b = host::mem::make<PeachDataBlock>();
        b->odds = initialOdds;
        b->items[0].kind = Item_BobOmb;
        b->items[0].weight = 0.0f;
        b->items[1].kind = Item_MrSaturn;
        b->items[1].weight = 3.0f;
        b->items[2].kind = Item_BeamSword;
        b->items[2].weight = 0.0f;
        u32 oddsAddr = reinterpret_cast<u32>(&b->odds);
        u32 itemsAddr = reinterpret_cast<u32>(&b->items[0]);
        u32 count = 3;
        memcpy(b->header + extra::kDataOdds, &oddsAddr, 4);
        memcpy(b->header + extra::kDataItems, &itemsAddr, 4);
        memcpy(b->header + extra::kDataItemCount, &count, 4);
        return b;
    }

    static DededeDataBlock* makeDededeData()
    {
        DededeDataBlock* b = host::mem::make<DededeDataBlock>();
        memset(b, 0, sizeof(*b));
        b->table[6] = 35.0f; // +0x18 Waddle Dee
        b->table[7] = 10.0f; // +0x1C Waddle Doo
        b->table[8] = 4.0f;  // +0x20 Gordo
        b->table[9] = 0.0f;  // +0x24 Capsule
        u32 tableAddr = reinterpret_cast<u32>(&b->table[0]);
        memcpy(b->header + 0x80, &tableAddr, 4);
        return b;
    }

    static host::fakes::FakeFighter* s_lastFake = NULL;

    static ftEntry* addEntry(int slotIdx, int playerNo, int entryId, int kind, s32 paramOdds)
    {
        host::fakes::FakeFighter& ff = host::fakes::addFighter();
        s_lastFake = &ff;
        ff.accesser->setConstant(extra::kParamOdds, static_cast<float>(paramOdds));
        if (kind == extra::kFtDedede)
        {
            ff.accesser->setConstant(4015, 35.0f);
            ff.accesser->setConstant(4016, 10.0f);
            ff.accesser->setConstant(4017, 4.0f);
        }
        ftEntry* e = host::mem::make<ftEntry>();
        e->m_entryId = entryId;
        e->m_playerNo = playerNo;
        e->m_activeInstanceIndex = 0;
        e->m_instances[0].m_kind = kind;
        e->m_instances[0].m_fighter = ff.fighter;
        e->m_owner = host::mem::make<ftOwner>();
        g_ftEntryManager->setSlot(slotIdx, e);
        return e;
    }

    static void setupRngWorld()
    {
        s_fakeSwapBusy = false;
        g_ftCommonDataAccesser.reset();
        g_ftEntryManager = host::mem::make<ftEntryManager>();
        g_ftEntryManager->reset();
        g_ftManager = host::mem::make<ftManager>();
        main::loadSettings(host::fakes::block());
        main::settings().characterRng.peachTurnip = main::kTurnipStitchFace;
        main::settings().characterRng.peachSmash = main::kSmashFryingPan;
        extra::characterRngAttach();
        // Host heapUsage leaves heap.start/end at 0; skip ItmCommonParam scan.
        extra::s_drawTried = true;
    }
}

namespace tm {
namespace extra {
    bool fighterSwapBusy()
    {
        return s_fakeSwapBusy;
    }
}
}

TEST(character_rng, swap_out_peach_never_writes_freed_odds_on_detach)
{
    setupRngWorld();
    PeachDataBlock* peachPac1 = makePeachData(80);
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac1->header);
    addEntry(0, 0, 0x10000, Fighter_Peach, 80);
    addEntry(1, 1, 0x20001, Fighter_Fox, 0);

    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac1->odds);

    // Swap Peach out: ftDataProvider::remove zeroes getData(Fighter_Peach),
    // and Mario's FitMario.pac reuses that memory.
    g_ftEntryManager->setSlot(0, NULL);
    g_ftCommonDataAccesser.setData(Fighter_Peach, NULL);
    peachPac1->odds = 0x11010A00;
    addEntry(0, 0, 0x30000, Fighter_Mario, 0);

    // Case A: detach immediately without another characterRng frame.
    extra::characterRngDetach();
    CHECK_EQ(0x11010A00, peachPac1->odds);
}

TEST(character_rng, swap_back_to_peach_re_finds_new_and_same_address)
{
    setupRngWorld();
    PeachDataBlock* peachPac1 = makePeachData(80);
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac1->header);
    addEntry(0, 0, 0x10000, Fighter_Peach, 80);
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac1->odds);

    // Swap Peach out -> Mario occupies old address.
    g_ftEntryManager->setSlot(0, NULL);
    g_ftCommonDataAccesser.setData(Fighter_Peach, NULL);
    peachPac1->odds = 0x11010A00;
    addEntry(0, 0, 0x20000, Fighter_Mario, 0);
    CHECK(!extra::characterRng(main::kRngApply));
    CHECK_EQ(0x11010A00, peachPac1->odds);

    // Swap back to Peach at a DIFFERENT address (peachPac2): must re-find
    // peachPac2 and leave peachPac1 untouched.
    PeachDataBlock* peachPac2 = makePeachData(80);
    g_ftEntryManager->setSlot(0, NULL);
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac2->header);
    addEntry(0, 0, 0x30000, Fighter_Peach, 80);
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac2->odds);
    CHECK_EQ(0x11010A00, peachPac1->odds);

    // Detach while Peach #2 is still resident: restores peachPac2 only.
    extra::characterRngDetach();
    CHECK_EQ(80, peachPac2->odds);
    CHECK_EQ(0x11010A00, peachPac1->odds);
}

TEST(character_rng, swap_back_at_same_address_re_finds_and_applies)
{
    setupRngWorld();
    PeachDataBlock* peachPac = makePeachData(80);
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac->header);
    addEntry(0, 0, 0x10000, Fighter_Peach, 80);
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac->odds);

    // Unload Peach -> Mario frame -> reload Peach at the exact same address.
    g_ftEntryManager->setSlot(0, NULL);
    g_ftCommonDataAccesser.setData(Fighter_Peach, NULL);
    CHECK(!extra::characterRng(main::kRngApply));

    peachPac->odds = 80;
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac->header);
    addEntry(0, 0, 0x20000, Fighter_Peach, 80);
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac->odds);

    extra::characterRngDetach();
    CHECK_EQ(80, peachPac->odds);
}

TEST(character_rng, mid_swap_busy_skips_apply_and_resumes_after_swap)
{
    setupRngWorld();
    main::settings().fighterMechanics.dededeWaddle = main::kWaddleGordo;

    PeachDataBlock* peachPac = makePeachData(80);
    DededeDataBlock* dededePac = makeDededeData();
    g_ftCommonDataAccesser.setData(Fighter_Peach, peachPac->header);
    g_ftCommonDataAccesser.setData(static_cast<ftKind>(extra::kFtDedede), dededePac->header);
    addEntry(0, 0, 0x10000, Fighter_Peach, 80);
    addEntry(1, 1, 0x20001, extra::kFtDedede, 0);

    // Baseline apply patches both Peach and Dedede tables.
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac->odds);
    CHECK_NEAR(0.0f, dededePac->table[6], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[7], 0.001f);
    CHECK_NEAR(100.0f, dededePac->table[8], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[9], 0.001f);

    // Pre-swap cleanup restores both tables, then fighterSwapBusy() stays true
    // while stLoaderPlayer transitions out the old fighters.
    extra::characterRngPrepareSwap();
    CHECK_EQ(80, peachPac->odds);
    CHECK_NEAR(35.0f, dededePac->table[6], 0.001f);
    CHECK_NEAR(10.0f, dededePac->table[7], 0.001f);
    CHECK_NEAR(4.0f, dededePac->table[8], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[9], 0.001f);

    s_fakeSwapBusy = true;
    CHECK(!extra::characterRng(main::kRngApply));
    CHECK(!extra::characterRng(main::kRngApply));
    CHECK_EQ(80, peachPac->odds);
    CHECK_NEAR(35.0f, dededePac->table[6], 0.001f);
    CHECK_NEAR(10.0f, dededePac->table[7], 0.001f);
    CHECK_NEAR(4.0f, dededePac->table[8], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[9], 0.001f);

    // Menu visibility queries still work while swap is in flight.
    CHECK(extra::characterRng(main::kRngPeach));
    CHECK(extra::characterRng(main::kRngDedede));
    CHECK(extra::characterRng(main::kRngAny));

    // Once swap settles, kRngApply resumes patching both tables cleanly.
    s_fakeSwapBusy = false;
    CHECK(extra::characterRng(main::kRngApply));
    CHECK_EQ(-1, peachPac->odds);
    CHECK_NEAR(0.0f, dededePac->table[6], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[7], 0.001f);
    CHECK_NEAR(100.0f, dededePac->table[8], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[9], 0.001f);

    extra::characterRngDetach();
    CHECK_EQ(80, peachPac->odds);
    CHECK_NEAR(35.0f, dededePac->table[6], 0.001f);
    CHECK_NEAR(10.0f, dededePac->table[7], 0.001f);
    CHECK_NEAR(4.0f, dededePac->table[8], 0.001f);
    CHECK_NEAR(0.0f, dededePac->table[9], 0.001f);
}

TEST(character_rng, gnw_judge_preserves_visible_digit_during_status_0x113)
{
    setupRngWorld();
    main::settings().characterRng.peachTurnip = main::kTurnipDefault;
    main::settings().characterRng.peachSmash = main::kSmashDefault;
    ftEntry* gnw = addEntry(0, 0, 0x10000, extra::kFtGameWatch, 0);
    host::fakes::FakeFighter& ff = *s_lastFake;
    const soModuleEnumeration* m = gnw->m_instances[0].m_fighter->m_moduleAccesser->m_enumerationStart;
    soWorkManageModule* work = m->m_workManageModule;
    soStatusModule* st = m->m_statusModule;

    // Default leaves LA-Basic[67] and LA-Basic[68] untouched in idle and Judge.
    main::settings().characterRng.gnwHammer = main::kHammerDefault;
    work->setInt(2, extra::kJudgeNext);
    work->setInt(5, extra::kJudgeBefore);
    ff.status = 0;
    st->m_statusKind = 0;
    extra::characterRng(main::kRngApply);
    CHECK_EQ(2, work->getInt(extra::kJudgeNext));
    CHECK_EQ(5, work->getInt(extra::kJudgeBefore));

    // For every forced Judge 1..9:
    // - Outside status 0x113: sets kJudgeNext = choice - 1 and kJudgeBefore = choice % 9 (distinct).
    // - During status 0x113: ft_gamewatch 0x11AE0 moves choice - 1 into kJudgeBefore; applyJudge must
    //   NOT overwrite kJudgeBefore (which drives wnGameWatchPanel's visible digit), while keeping
    //   kJudgeNext = choice - 1 for the 1-dot/2-dot next-Judge indicator.
    for (int choice = 1; choice <= 9; choice++)
    {
        main::settings().characterRng.gnwHammer = static_cast<u8>(choice);
        ff.status = 0;
        st->m_statusKind = 0;
        extra::characterRng(main::kRngApply);
        CHECK_EQ(choice - 1, work->getInt(extra::kJudgeNext));
        CHECK_EQ(choice % 9, work->getInt(extra::kJudgeBefore));
        CHECK(work->getInt(extra::kJudgeNext) != work->getInt(extra::kJudgeBefore));

        // Simulate ft_gamewatch 0x11AE0 entering status 0x113: active number copied to kJudgeBefore,
        // random next placed in kJudgeNext.
        ff.status = extra::kStatusJudge;
        st->m_statusKind = extra::kStatusJudge;
        work->setInt((choice + 3) % 9, extra::kJudgeNext);
        work->setInt(choice - 1, extra::kJudgeBefore);
        extra::characterRng(main::kRngApply);
        CHECK_EQ(choice - 1, work->getInt(extra::kJudgeNext));
        CHECK_EQ(choice - 1, work->getInt(extra::kJudgeBefore));
    }
}
