// Custom Counter Pools UX, completeness, mix-and-match, slot dispatch, and
// Scenarios 1-click jump tests.
#include <string.h>

#include "extra/menu_descriptions.h"
#include "host/card.h"
#include "host/test.h"
#include "main/cpu/actions.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/storage/storage.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"
#include "main/ui/pages/scenarios.h"

using namespace tm;

namespace {
    enum {
        kMario = 0x0,
        kFox = 0x6,
        kNess = 0xA,
        kPeach = 0xC,
        kGanon = 0x14,
        kLucas = 0x1A,
        kMewtwo = 0x26
    };

    u8 s_block[block::kSize];

    bool rowVisible(const ui::Option& o)
    {
        return !((o.flags & ui::kFlagHideWhenDisabled) != 0 && o.enabled != NULL && !o.enabled());
    }

    int findRowByName(const ui::Page& p, const char* name)
    {
        for (int i = 0; i < p.count; i++)
        {
            if (p.options[i].name != NULL && strcmp(p.options[i].name, name) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    bool bitSet(const u8 bits[8], int idx)
    {
        return (bits[idx >> 3] & (1u << (idx & 7))) != 0;
    }

    void setBit(u8 bits[8], int idx)
    {
        bits[idx >> 3] |= static_cast<u8>(1u << (idx & 7));
    }

    main::ExtraHooks makeEmptyHooks()
    {
        main::ExtraHooks h;
        memset(&h, 0, sizeof(h));
        return h;
    }
    const main::ExtraHooks kExtraEnabled = makeEmptyHooks();
}

TEST(counter_pool, mix_and_match_never_locks_out_single_rows_or_hides_custom_pools)
{
    memset(s_block, 0, sizeof(s_block));
    host::card::reset();
    storage::attach(s_block);
    main::loadSettings(s_block);
    memset(ui::menuScratch(), 0, ui::kScratchBytes);
    main::setExtraHooks(&kExtraEnabled);

    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);
    const ui::Page& ctr = ui::pages::kCpuCounters;
    const u8 ctrPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_COUNTERS};

    // Default Single Action (1): Ground/Air/Shield AND Custom Pools are all visible.
    REQUIRE(ui::menuJump(menu, st, ctrPath, 2));
    CHECK_EQ(1, ui::optionValue(menu, ctr.options[0]));
    CHECK(rowVisible(ctr.options[1])); // Ground Counter
    CHECK(rowVisible(ctr.options[2])); // Air Counter
    CHECK(rowVisible(ctr.options[3])); // Shield Counter
    CHECK(rowVisible(ctr.options[6])); // Custom Pools

    // Set mixed Ground = None (0), Air = Custom Pool, Shield = Grab (1).
    cpu::CpuOptions& co = main::settings().cpu.options;
    co.counterGround = 0;
    co.counterAir = static_cast<u8>(cpu::customPoolIndex(cpu::kListAir));
    co.counterShield = 1;

    ui::MenuInput noop = {0, 0, 0, 0, false, false, false, false};
    ui::menuStep(menu, st, noop);
    // Must stay in Single Action (1), NOT flip to Custom Pool (2), and keep all 3 rows + Custom Pools visible.
    CHECK_EQ(1, ui::optionValue(menu, ctr.options[0]));
    CHECK(rowVisible(ctr.options[1]));
    CHECK(rowVisible(ctr.options[2]));
    CHECK(rowVisible(ctr.options[3]));
    CHECK(rowVisible(ctr.options[6]));

    // Close and reopen menu (simulating pause/unpause): mixed settings persist untouched.
    ui::menuReset(menu, st);
    REQUIRE(ui::menuJump(menu, st, ctrPath, 2));
    CHECK_EQ(0, static_cast<int>(co.counterGround));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListAir), static_cast<int>(co.counterAir));
    CHECK_EQ(1, static_cast<int>(co.counterShield));
    CHECK_EQ(1, ui::optionValue(menu, ctr.options[0]));
    CHECK(rowVisible(ctr.options[1]));
    CHECK(rowVisible(ctr.options[2]));
    CHECK(rowVisible(ctr.options[3]));
    CHECK(rowVisible(ctr.options[6]));

    // Switching Counter Mode right to Custom Pool (2) sets all 3 to Custom Pool,
    // AND STILL keeps Ground/Air/Shield visible so any trigger can be tweaked individually!
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};
    ui::MenuInput inLeft = {0, 0, 1, 0, false, false, false, false};
    ui::menuStep(menu, st, inRight);
    CHECK_EQ(2, ui::optionValue(menu, ctr.options[0]));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListGround), static_cast<int>(co.counterGround));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListAir), static_cast<int>(co.counterAir));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListShield), static_cast<int>(co.counterShield));
    CHECK(rowVisible(ctr.options[1]));
    CHECK(rowVisible(ctr.options[2]));
    CHECK(rowVisible(ctr.options[3]));
    CHECK(rowVisible(ctr.options[6]));

    // Tweaking Ground back to None while in Custom Pool mode leaves Air/Shield on Custom Pool without lockout.
    co.counterGround = 0;
    co.counterShield = 1;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(1, ui::optionValue(menu, ctr.options[0]));
    CHECK_EQ(0, static_cast<int>(co.counterGround));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListAir), static_cast<int>(co.counterAir));
    CHECK_EQ(1, static_cast<int>(co.counterShield));

    // Switching Counter Mode -> Off hides Custom Pools; stepping back -> Single Action restores defaults and reveals Custom Pools.
    ui::menuStep(menu, st, inLeft);
    CHECK_EQ(0, ui::optionValue(menu, ctr.options[0]));
    CHECK(!rowVisible(ctr.options[6]));
    ui::menuStep(menu, st, inRight);
    CHECK_EQ(1, ui::optionValue(menu, ctr.options[0]));
    CHECK_EQ(13, static_cast<int>(co.counterGround));
    CHECK_EQ(1, static_cast<int>(co.counterAir));
    CHECK_EQ(1, static_cast<int>(co.counterShield));
    CHECK(rowVisible(ctr.options[6]));

    // Intentional mixed single actions (Ground = None, Air = Custom Pool, Shield = Grab) are NOT clobbered
    // when switching through Per-Hit (4) -> Single Action (1) via restoreDefaultSingleCounters.
    co.counterGround = 0;
    co.counterAir = static_cast<u8>(cpu::customPoolIndex(cpu::kListAir));
    co.counterShield = 1;
    main::settings().cpuTables.hits[0].logic = cpu::kLogicCustom;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(4, ui::optionValue(menu, ctr.options[0]));
    CHECK(rowVisible(ctr.options[6])); // Custom Pools accessible in Per-Hit too!
    ui::menuStep(menu, st, inLeft);    // 3 = Reversal Slots (keeps mixed single counters!)
    CHECK_EQ(3, ui::optionValue(menu, ctr.options[0]));
    CHECK(rowVisible(ctr.options[6])); // Custom Pools accessible in Reversal Slots too!
    CHECK_EQ(0, static_cast<int>(co.counterGround));
    CHECK_EQ(cpu::customPoolIndex(cpu::kListAir), static_cast<int>(co.counterAir));
    CHECK_EQ(1, static_cast<int>(co.counterShield));

    // Pool Status row in P_COUNTER_POOL shows "A: Use Pool" when inactive, and pressing A activates ONLY that category.
    const u8 poolPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_COUNTERS, ui::pages::P_COUNTER_POOL};
    REQUIRE(ui::menuJump(menu, st, poolPath, 3));
    const ui::Page& pool = ui::pages::kCounterPool;
    ui::menuStep(menu, st, noop);
    CHECK_EQ(0, ui::optionValue(menu, pool.options[0])); // Ground
    CHECK(strcmp(static_cast<const char*>(pool.options[1].data), "A: Use Pool") == 0);
    ui::MenuInput inDown = {0, 1, 0, 0, false, false, false, false};
    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::menuStep(menu, st, inDown);
    CHECK_EQ(1, static_cast<int>(st.cursor[ui::pages::P_COUNTER_POOL]));
    ui::menuStep(menu, st, inA);
    CHECK_EQ(cpu::customPoolIndex(cpu::kListGround), static_cast<int>(co.counterGround));
    CHECK_EQ(1, static_cast<int>(co.counterShield)); // Shield untouched!
    ui::menuStep(menu, st, noop);
    CHECK(strcmp(static_cast<const char*>(pool.options[1].data), "Active") == 0);

    main::setExtraHooks(NULL);
    storage::detach();
}

TEST(counter_pool, all_legal_moves_exposed_nothing_bit0_and_slot_dispatch)
{
    memset(s_block, 0, sizeof(s_block));
    host::card::reset();
    storage::attach(s_block);
    main::loadSettings(s_block);
    memset(ui::menuScratch(), 0, ui::kScratchBytes);

    const ui::Page& pool = ui::pages::kCounterPool;
    // 4 head + 53 Ground + 29 Air + 40 Shield = 126 rows, all with descriptions.
    REQUIRE(pool.count == 126);
    for (int r = 0; r < pool.count; r++)
    {
        const char* desc = extra::optionDesc(ui::pages::P_COUNTER_POOL, static_cast<u8>(r));
        CHECK(desc != NULL && desc[0] != '\0');
    }

    // Verify every non-dropped concrete action in kGround/kAir/kShieldList plus bit 0 appears
    // exactly once in kCounterPool, and no meta or dropped actions appear.
    u8 seen[3][8];
    memset(seen, 0, sizeof(seen));
    const u16 baseOff = static_cast<u16>(offsetof(main::Settings, cpuTables.poolBits));
    for (int r = 4; r < pool.count; r++)
    {
        const ui::Option& o = pool.options[r];
        REQUIRE(o.kind == ui::kKindToggle);
        REQUIRE((o.flags & ui::kFlagBit) != 0);
        REQUIRE(o.offset >= baseOff && o.offset < baseOff + 24);
        int rel = o.offset - baseOff;
        int cat = rel / 8;
        int byteIdx = rel % 8;
        int bit = -1;
        for (int b = 0; b < 8; b++)
        {
            if (o.min == (1 << b))
            {
                bit = b;
            }
        }
        REQUIRE(bit >= 0);
        int idx = byteIdx * 8 + bit;
        CHECK(!bitSet(seen[cat], idx));
        setBit(seen[cat], idx);
        if (idx == 0)
        {
            CHECK(strcmp(o.name, "Nothing (Wait)") == 0);
            CHECK_EQ(0, static_cast<int>(o.def));
        }
        else
        {
            int ak = cpu::actionKind(cat, idx);
            CHECK(ak == cpu::kActionScript || ak == cpu::kActionPlaySlot);
        }
    }

    // Compare against every legal non-dropped entry in kCounterGround / kCounterAir / kCounterShield.
    const char* const* const kLabels[3] = {
        ui::pages::kCounterGround, ui::pages::kCounterAir, ui::pages::kCounterShield
    };
    const int kSizes[3] = {
        ui::pages::kCounterGroundSize, ui::pages::kCounterAirSize, ui::pages::kCounterShieldSize
    };
    for (int cat = 0; cat < 3; cat++)
    {
        CHECK(bitSet(seen[cat], 0)); // Nothing (Wait)
        for (int idx = 1; idx < kSizes[cat]; idx++)
        {
            int ak = cpu::actionKind(cat, idx);
            bool dropped = ui::droppedLabel(kLabels[cat][idx]);
            bool want = !dropped && (ak == cpu::kActionScript || ak == cpu::kActionPlaySlot);
            CHECK_EQ(want, bitSet(seen[cat], idx));
        }
    }

    // Menu defaultAll() matches cpu::poolDefaults() bit-for-bit (bit 0 defaults Off).
    ui::defaultAll(ui::labMenu());
    for (int cat = 0; cat < 3; cat++)
    {
        u8 expected[8];
        cpu::poolDefaults(cat, expected);
        CHECK(memcmp(expected, main::settings().cpuTables.poolBits[cat], 8) == 0);
        CHECK(!bitSet(expected, 0));
    }

    // Bit 0 (Nothing / Wait) survives poolSanitize and rolls 0 alongside Mario's Air Pool moves.
    u8 airBits[8];
    memset(airBits, 0, sizeof(airBits));
    setBit(airBits, 0);  // Nothing (Wait)
    setBit(airBits, 1);  // Airdodge
    setBit(airBits, 2);  // Jump Away
    setBit(airBits, 5);  // Up B Away
    setBit(airBits, 11); // Nair
    setBit(airBits, cpu::customPoolIndex(cpu::kListAir)); // meta bit should be stripped
    cpu::poolSanitize(cpu::kListAir, airBits);
    CHECK(bitSet(airBits, 0));
    CHECK(!bitSet(airBits, cpu::customPoolIndex(cpu::kListAir)));
    bool sawNothing = false, sawDodge = false, sawJump = false, sawUpB = false, sawNair = false;
    for (u32 r = 0; r < 5; r++)
    {
        int pick = cpu::randomPoolAction(cpu::kListAir, airBits, kMario, r);
        if (pick == 0) sawNothing = true;
        else if (pick == 1) sawDodge = true;
        else if (pick == 2) sawJump = true;
        else if (pick == 5) sawUpB = true;
        else if (pick == 11) sawNair = true;
        else CHECK(false);
    }
    CHECK(sawNothing && sawDodge && sawJump && sawUpB && sawNair);

    // Clear Pool (all zeros) safely returns 0 (no counter).
    memset(airBits, 0, sizeof(airBits));
    CHECK_EQ(0, cpu::randomPoolAction(cpu::kListAir, airBits, kMario, 0));
    CHECK_EQ(0, cpu::randomPoolAction(cpu::kListAir, airBits, kMario, 7));

    // Character-gated DJC & Float moves round-trip through poolSanitize and filter by ftKind.
    u8 gndBits[8];
    memset(gndBits, 0, sizeof(gndBits));
    setBit(gndBits, 22); // Wavedash Down
    setBit(gndBits, 24); // Dash Through
    setBit(gndBits, 33); // Roll Random
    setBit(gndBits, 41); // DJC Nair
    setBit(gndBits, 46); // Float Nair
    setBit(gndBits, 51); // SHFFL Nair
    cpu::poolSanitize(cpu::kListGround, gndBits);
    CHECK(bitSet(gndBits, 22) && bitSet(gndBits, 24) && bitSet(gndBits, 33));
    CHECK(bitSet(gndBits, 41) && bitSet(gndBits, 46) && bitSet(gndBits, 51));
    for (u32 r = 0; r < 16; r++)
    {
        int marioPick = cpu::randomPoolAction(cpu::kListGround, gndBits, kMario, r);
        CHECK(marioPick != 41 && marioPick != 46); // Mario never gets DJC or Float
    }
    // Only DJC + Float enabled: Mario gets 0, Ness gets DJC (41), Peach & Ganon get Float (46), Mewtwo gets both.
    memset(gndBits, 0, sizeof(gndBits));
    setBit(gndBits, 41);
    setBit(gndBits, 46);
    CHECK_EQ(0, cpu::randomPoolAction(cpu::kListGround, gndBits, kMario, 0));
    CHECK_EQ(41, cpu::randomPoolAction(cpu::kListGround, gndBits, kNess, 0));
    CHECK_EQ(41, cpu::randomPoolAction(cpu::kListGround, gndBits, kLucas, 0));
    CHECK_EQ(46, cpu::randomPoolAction(cpu::kListGround, gndBits, kPeach, 0));
    CHECK_EQ(46, cpu::randomPoolAction(cpu::kListGround, gndBits, kGanon, 0));
    CHECK_EQ(41, cpu::randomPoolAction(cpu::kListGround, gndBits, kMewtwo, 0));
    CHECK_EQ(46, cpu::randomPoolAction(cpu::kListGround, gndBits, kMewtwo, 1));

    // Recording Slot 1..6 in all 3 categories resolve via randomPoolAction() -> kActionPlaySlot + playSlot 1..6.
    const int kSlotStart[3] = {25, 20, 22};
    for (int cat = 0; cat < 3; cat++)
    {
        for (int s = 0; s < 6; s++)
        {
            u8 slotBits[8];
            memset(slotBits, 0, sizeof(slotBits));
            int idx = kSlotStart[cat] + s;
            setBit(slotBits, idx);
            cpu::poolSanitize(cat, slotBits);
            CHECK(bitSet(slotBits, idx));
            int pick = cpu::randomPoolAction(cat, slotBits, kMario, 0);
            CHECK_EQ(idx, pick);
            CHECK_EQ(static_cast<int>(cpu::kActionPlaySlot), cpu::actionKind(cat, pick));
            const cpu::Action* act = cpu::actionFor(cat, pick, kMario);
            REQUIRE(act != NULL);
            CHECK(act->steps == NULL);
            CHECK_EQ(s + 1, static_cast<int>(act->playSlot));
        }
    }

    // SHFFL / FFL inside an active Custom Pool reveals SHFFL Timing on P_CPU_COUNTERS.
    main::settings().cpu.options.counterAir = static_cast<u8>(cpu::customPoolIndex(cpu::kListAir));
    memset(main::settings().cpuTables.poolBits[cpu::kListAir], 0, 8);
    setBit(main::settings().cpuTables.poolBits[cpu::kListAir], 29); // FFL Nair
    CHECK(rowVisible(ui::pages::kCpuCounters.options[11]));         // SHFFL Timing

    storage::detach();
}

TEST(counter_pool, scenarios_one_click_edit_air_and_shield_pool_jumps)
{
    memset(s_block, 0, sizeof(s_block));
    host::card::reset();
    storage::attach(s_block);
    main::loadSettings(s_block);
    memset(ui::menuScratch(), 0, ui::kScratchBytes);
    main::setExtraHooks(&kExtraEnabled);

    const ui::MenuDef& menu = ui::labMenu();
    ui::MenuState st;
    ui::menuReset(menu, st);
    const ui::Page& scen = ui::pages::kScenarios;
    const ui::Page& pool = ui::pages::kCounterPool;

    int postHitstunRow = findRowByName(scen, "Post-Hitstun");
    int editAirRow = findRowByName(scen, "Edit Air Pool");
    int cpuOosRow = findRowByName(scen, "CPU OoS");
    int editShieldRow = findRowByName(scen, "Edit Shield Pool");
    REQUIRE(postHitstunRow >= 0 && editAirRow == postHitstunRow + 1);
    REQUIRE(cpuOosRow >= 0 && editShieldRow == cpuOosRow + 1);
    CHECK_EQ(ui::kKindMenu, static_cast<int>(scen.options[editAirRow].kind));
    CHECK_EQ(ui::pages::P_COUNTER_POOL, static_cast<int>(scen.options[editAirRow].max));
    CHECK_EQ(ui::kKindMenu, static_cast<int>(scen.options[editShieldRow].kind));
    CHECK_EQ(ui::pages::P_COUNTER_POOL, static_cast<int>(scen.options[editShieldRow].max));

    ui::MenuInput inA = {0, 0, 0, 0, true, false, false, false};
    ui::MenuInput inB = {0, 0, 0, 0, false, true, false, false};
    ui::MenuInput inLeft = {0, 0, 1, 0, false, false, false, false};
    ui::MenuInput inRight = {0, 0, 0, 1, false, false, false, false};

    // 1. Combo Lab: Edit Air Pool hidden unless Post-Hitstun == Custom Pool.
    ui::pages::ScenarioMenuState* sm = ui::pages::scenarioMenuState();
    memset(sm, 0, sizeof(*sm));
    sm->preset = ui::pages::kPresetComboLab;
    main::settings().comboTrainer.postHitstunEscape = main::kComboEscapeDodge;
    CHECK(!rowVisible(scen.options[editAirRow]));
    CHECK(!rowVisible(scen.options[editShieldRow]));

    main::settings().comboTrainer.postHitstunEscape = main::kComboEscapeCustomPool;
    CHECK(rowVisible(scen.options[editAirRow]));
    CHECK(!rowVisible(scen.options[editShieldRow]));

    // Start Category scratch on Ground (0); pressing A on Edit Air Pool pre-switches to Air (1).
    const u8 scenPath[] = {ui::pages::P_SCENARIOS};
    REQUIRE(ui::menuJump(menu, st, scenPath, 1));
    ui::menuScratch()[34] = 0; // Ground
    st.cursor[ui::pages::P_SCENARIOS] = static_cast<u8>(editAirRow);
    ui::menuStep(menu, st, inA);
    CHECK_EQ(ui::pages::P_COUNTER_POOL, ui::currentPage(st));
    CHECK_EQ(1, ui::optionValue(menu, pool.options[0])); // Pre-switched to Air (1)!
    // User can freely change Category once inside P_COUNTER_POOL (not locked to Air!).
    ui::menuStep(menu, st, inLeft);
    CHECK_EQ(0, ui::optionValue(menu, pool.options[0])); // Ground (0)
    ui::menuStep(menu, st, inRight);
    ui::menuStep(menu, st, inRight);
    CHECK_EQ(2, ui::optionValue(menu, pool.options[0])); // Shield (2)
    ui::menuStep(menu, st, inB);
    CHECK_EQ(ui::pages::P_SCENARIOS, ui::currentPage(st));

    // 2. Shield Pressure - Offense: Edit Shield Pool hidden unless Role == Offense (0) && CPU OoS == Custom Pool (7).
    sm->preset = ui::pages::kPresetShieldPressure;
    sm->shieldRole = 0;
    sm->oosAction = 0; // Grab
    CHECK(!rowVisible(scen.options[editShieldRow]));
    sm->shieldRole = 1; // Defense
    sm->oosAction = 7;
    CHECK(!rowVisible(scen.options[editShieldRow]));
    sm->shieldRole = 0; // Offense
    sm->oosAction = 7;  // Custom Pool
    CHECK(rowVisible(scen.options[editShieldRow]));
    CHECK(!rowVisible(scen.options[editAirRow]));

    ui::menuScratch()[34] = 0; // Ground
    st.cursor[ui::pages::P_SCENARIOS] = static_cast<u8>(editShieldRow);
    ui::menuStep(menu, st, inA);
    CHECK_EQ(ui::pages::P_COUNTER_POOL, ui::currentPage(st));
    CHECK_EQ(2, ui::optionValue(menu, pool.options[0])); // Pre-switched to Shield (2)!
    ui::menuStep(menu, st, inLeft);
    CHECK_EQ(1, ui::optionValue(menu, pool.options[0])); // User can switch freely!

    // 3. Visiting CPU > Counters & Reversals clears any pending Scenarios category prime,
    // so opening Custom Pools from CPU keeps the user's chosen Category untouched.
    sm->preset = ui::pages::kPresetComboLab;
    main::settings().comboTrainer.postHitstunEscape = main::kComboEscapeCustomPool;
    CHECK(rowVisible(scen.options[editAirRow])); // primes Air (1)
    ui::menuScratch()[34] = 0;                   // user last browsed Ground (0)
    const u8 cpuPoolPath[] = {ui::pages::P_CPU, ui::pages::P_CPU_COUNTERS, ui::pages::P_COUNTER_POOL};
    REQUIRE(ui::menuJump(menu, st, cpuPoolPath, 3));
    CHECK_EQ(0, ui::optionValue(menu, pool.options[0])); // Ground (0) preserved, NOT overridden to Air!

    sm->preset = ui::pages::kPresetOff;
    main::setExtraHooks(NULL);
    storage::detach();
}
