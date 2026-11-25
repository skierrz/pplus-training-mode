// Host unit tests for mid-session Fighter Swap core logic
// (src/extra/fighter_swap_core.h) and P_FIGHTER_SWAP baseline menu wiring.
#include <string.h>

#include "extra/fighter_swap_core.h"
#include "host/test.h"
#include "main/cpu/actions.h"
#include "main/extra.h"
#include "main/ui/pages/fighter_swap.h"
#include "main/ui/pages/pages.h"

using namespace tm;

TEST(fighter_swap, roster_is_complete_alphabetical_and_excludes_invalid_ids)
{
    const extra::FighterSwapEntry* roster = extra::fighterSwapRoster();
    CHECK_EQ(45, extra::kFighterSwapCount);

    bool seenKind[64];
    memset(seenKind, 0, sizeof(seenKind));

    for (int i = 0; i < extra::kFighterSwapCount; i++)
    {
        REQUIRE(roster[i].name != NULL);
        CHECK(roster[i].name[0] != '\0');
        CHECK(roster[i].gmKind < 64);
        CHECK(!seenKind[roster[i].gmKind]);
        seenKind[roster[i].gmKind] = true;
        CHECK_EQ(i, extra::fighterSwapIndexOfKind(roster[i].gmKind));
        CHECK_EQ(extra::fighterSwapSetGroup(roster[i].gmKind), roster[i].setGroup);
        if (i > 0)
        {
            CHECK(strcmp(roster[i - 1].name, roster[i].name) < 0);
        }
    }

    // Brawl Pokemon Trainer trio (0x1D, 0x1F, 0x21), solo Nana (0x12), and
    // Alloys (0x2E..0x31) must be omitted.
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x1D));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x1F));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x21));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x12));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x2E));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x2F));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x30));
    CHECK_EQ(-1, extra::fighterSwapIndexOfKind(0x31));

    // Verify key P+ 3.2.1 CSS icons (MuSelchkind) and Hold-L variants.
    const extra::FighterSwapEntry* zard = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x1E));
    const extra::FighterSwapEntry* squirtle = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x20));
    const extra::FighterSwapEntry* ivy = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x22));
    const extra::FighterSwapEntry* roy = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x32));
    const extra::FighterSwapEntry* mewtwo = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x33));
    const extra::FighterSwapEntry* knuckles = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x35));
    const extra::FighterSwapEntry* sopo = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x11));
    const extra::FighterSwapEntry* giga = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x2C));
    const extra::FighterSwapEntry* wman = extra::fighterSwapAt(extra::fighterSwapIndexOfKind(0x2D));
    REQUIRE(zard != NULL && squirtle != NULL && ivy != NULL);
    REQUIRE(roy != NULL && mewtwo != NULL && knuckles != NULL);
    REQUIRE(sopo != NULL && giga != NULL && wman != NULL);
    CHECK_EQ(0x2A, zard->cssIcon);
    CHECK_EQ(0x2B, squirtle->cssIcon);
    CHECK_EQ(0x2C, ivy->cssIcon);
    CHECK_EQ(0x2D, roy->cssIcon);
    CHECK_EQ(0x2E, mewtwo->cssIcon);
    CHECK_EQ(0x30, knuckles->cssIcon);
    CHECK_EQ(0x10, sopo->cssIcon);
    CHECK_EQ(0x0C, giga->cssIcon);
    CHECK_EQ(0x15, wman->cssIcon);
    CHECK(extra::fighterSwapAt(-1) == NULL);
    CHECK(extra::fighterSwapAt(extra::kFighterSwapCount) == NULL);
}

TEST(fighter_swap, set_budget_honors_shared_sets_and_extra_limit)
{
    // Shared pairs must collapse to one set.
    CHECK_EQ(extra::fighterSwapSetGroup(0x10), extra::fighterSwapSetGroup(0x11)); // ICs / Sopo
    CHECK_EQ(extra::fighterSwapSetGroup(0x0E), extra::fighterSwapSetGroup(0x0F)); // Zelda / Sheik
    CHECK_EQ(extra::fighterSwapSetGroup(0x03), extra::fighterSwapSetGroup(0x04)); // Samus / ZSS
    CHECK_EQ(extra::fighterSwapSetGroup(0x0C), extra::fighterSwapSetGroup(0x2C)); // Bowser / Giga Bowser
    CHECK_EQ(extra::fighterSwapSetGroup(0x17), extra::fighterSwapSetGroup(0x2D)); // Wario / Wario-Man

    // Solo Pokemon use distinct sets in P+.
    CHECK(extra::fighterSwapSetGroup(0x1E) != extra::fighterSwapSetGroup(0x20));
    CHECK(extra::fighterSwapSetGroup(0x20) != extra::fighterSwapSetGroup(0x22));
    CHECK(extra::fighterSwapSetGroup(0x1E) != extra::fighterSwapSetGroup(0x22));

    // 4 players with 4 distinct sets: refused when extra holds set 3, allowed without.
    const u8 fourDistinct[4] = {0x00, 0x07, 0x09, 0x27}; // Mario, Fox, Luigi, Jigglypuff
    const bool allActive[4] = {true, true, true, true};
    CHECK_EQ(4, extra::fighterSwapDistinctSets(fourDistinct, allActive, 4));
    CHECK(!extra::fighterSwapFitsBudget(fourDistinct, allActive, 4, true));
    CHECK(extra::fighterSwapFitsBudget(fourDistinct, allActive, 4, false));

    // 4 players where 2 share a set (Zelda + Sheik, plus Mario + Fox) = 3 sets -> allowed!
    const u8 sharedZeldaSheik[4] = {0x00, 0x07, 0x0E, 0x0F};
    CHECK_EQ(3, extra::fighterSwapDistinctSets(sharedZeldaSheik, allActive, 4));
    CHECK(extra::fighterSwapFitsBudget(sharedZeldaSheik, allActive, 4, true));

    // 4 players where ICs + Sopo share a set, Samus + ZSS share a set = 2 sets -> allowed!
    const u8 twoSharedPairs[4] = {0x10, 0x11, 0x03, 0x04};
    CHECK_EQ(2, extra::fighterSwapDistinctSets(twoSharedPairs, allActive, 4));
    CHECK(extra::fighterSwapFitsBudget(twoSharedPairs, allActive, 4, true));

    // Inactive slots do not consume sets.
    const bool twoActive[4] = {true, true, false, false};
    CHECK_EQ(2, extra::fighterSwapDistinctSets(fourDistinct, twoActive, 4));
    CHECK(extra::fighterSwapFitsBudget(fourDistinct, twoActive, 4, true));
}

TEST(fighter_swap, costume_clamping_and_live_index_lookup)
{
    CHECK_EQ(0, extra::fighterSwapClampCostume(-1, 15));
    CHECK_EQ(0, extra::fighterSwapClampCostume(5, 0));
    CHECK_EQ(0, extra::fighterSwapClampCostume(5, 1));
    CHECK_EQ(4, extra::fighterSwapClampCostume(4, 15));
    CHECK_EQ(14, extra::fighterSwapClampCostume(20, 15));
    CHECK_EQ(extra::kFighterSwapMaxCostume, extra::fighterSwapClampCostume(99, 50));

    // Wario's P+ costume file table starts at file 6 (costume index 0).
    const u8 warioFiles[6] = {6, 7, 10, 8, 11, 9};
    CHECK_EQ(0, extra::fighterSwapFindCostumeIndex(6, 0, warioFiles, 6));
    CHECK_EQ(2, extra::fighterSwapFindCostumeIndex(10, 2, warioFiles, 6));
    // Fallback scan when colorNo does not match.
    CHECK_EQ(3, extra::fighterSwapFindCostumeIndex(8, 0, warioFiles, 6));
    // Unknown file falls back cleanly to 0.
    CHECK_EQ(0, extra::fighterSwapFindCostumeIndex(62, 0, warioFiles, 6));
    CHECK_EQ(0, extra::fighterSwapFindCostumeIndex(6, 0, NULL, 0));
}

TEST(fighter_swap, counter_ft_kind_matches_djc_and_float_fighters)
{
    // Verify DJC/Float counter compatibility mapping matches cpu/actions.cpp.
    const cpu::Action* djc = cpu::action(cpu::kListGround, cpu::actionCount(cpu::kListGround) - 10);
    const cpu::Action* flt = cpu::action(cpu::kListGround, cpu::actionCount(cpu::kListGround) - 5);
    REQUIRE(djc != NULL && (djc->flags & cpu::kForDjc) != 0);
    REQUIRE(flt != NULL && (flt->flags & cpu::kForFloat) != 0);

    CHECK(cpu::fitsKind(djc, extra::fighterSwapCounterFtKind(0x0B)));  // Ness
    CHECK(cpu::fitsKind(djc, extra::fighterSwapCounterFtKind(0x1B)));  // Lucas
    CHECK(cpu::fitsKind(djc, extra::fighterSwapCounterFtKind(0x33)));  // Mewtwo
    CHECK(!cpu::fitsKind(djc, extra::fighterSwapCounterFtKind(0x07))); // Fox

    CHECK(cpu::fitsKind(flt, extra::fighterSwapCounterFtKind(0x0D)));  // Peach
    CHECK(cpu::fitsKind(flt, extra::fighterSwapCounterFtKind(0x16)));  // Ganondorf
    CHECK(cpu::fitsKind(flt, extra::fighterSwapCounterFtKind(0x33)));  // Mewtwo
    CHECK(!cpu::fitsKind(flt, extra::fighterSwapCounterFtKind(0x07))); // Fox
}

TEST(fighter_swap, page_uses_disjoint_scratch_and_updates_dynamic_bounds)
{
    const ui::Page& page = ui::pages::kFighterSwap;
    CHECK_EQ(5, page.count);
    CHECK(strcmp(page.title, "Fighter Swap") == 0);
    CHECK(strcmp(page.options[0].name, "P1 Fighter") == 0);
    CHECK(strcmp(page.options[1].name, "P1 Costume") == 0);
    CHECK(strcmp(page.options[2].name, "CPU Fighter") == 0);
    CHECK(strcmp(page.options[3].name, "CPU Costume") == 0);
    CHECK(strcmp(page.options[4].name, "Apply Swap") == 0);
    CHECK_EQ(ui::kKindFunc, page.options[4].kind);

    static const char* const kTestNames[3] = {"Alpha", "Beta", "Gamma"};
    main::setFighterSwapLabels(kTestNames, 2);
    main::setFighterSwapCostumeMax(0, 14);
    main::setFighterSwapCostumeMax(1, 17);
    CHECK_EQ(2, page.options[0].max);
    CHECK_EQ(14, page.options[1].max);
    CHECK_EQ(2, page.options[2].max);
    CHECK_EQ(17, page.options[3].max);

    // Detach restores safe 1-entry fallback.
    main::setFighterSwapLabels(NULL, 0);
    main::setFighterSwapCostumeMax(0, 0);
    main::setFighterSwapCostumeMax(1, 0);
    CHECK_EQ(0, page.options[0].max);
    CHECK_EQ(0, page.options[1].max);
    CHECK_EQ(0, page.options[2].max);
    CHECK_EQ(0, page.options[3].max);
}
