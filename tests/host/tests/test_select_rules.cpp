// Host unit tests for per-mode CSS icon filtering, locking, solo 1P select,
// and MuSelchkind -> gmCharacterKind mappings (src/shared/select_rules.h).
#include <string.h>

#include "host/test.h"
#include "menu/names.h"
#include "shared/drill_session.h"
#include "shared/select_rules.h"

using namespace tm;

namespace {
    int countIcons(const u8* set)
    {
        int n = 0;
        while (n < drills::kSetSize && set[n] != drills::kIconEnd)
        {
            n++;
        }
        return n;
    }
}

TEST(select_rules, kind_of_icon_matches_source_verified_ids_and_names)
{
    struct Mapping {
        int icon;
        int kind;
        const char* name;
    };
    static const Mapping kExpected[] = {
        {drills::kIconYoshi, 5, "Yoshi"},
        {drills::kIconFox, 7, "Fox"},
        {drills::kIconNess, 11, "Ness"},
        {drills::kIconPeach, 13, "Peach"},
        {drills::kIconSheik, 15, "Sheik"},
        {drills::kIconMarth, 19, "Marth"},
        {drills::kIconFalco, 21, "Falco"},
        {drills::kIconLucas, 27, "Lucas"},
        {drills::kIconLucario, 36, "Lucario"},
        {drills::kIconWolf, 41, "Wolf"},
        {drills::kIconSnake, 42, "Snake"},
        {drills::kIconMewtwo, 51, "Mewtwo"}
    };

    // Exact source-verified hex constants (mu/menu.h, CSS.ASM, CloneEngine.asm:216).
    CHECK_EQ(0x05, static_cast<int>(drills::kIconYoshi));
    CHECK_EQ(0x07, static_cast<int>(drills::kIconFox));
    CHECK_EQ(0x0B, static_cast<int>(drills::kIconNess));
    CHECK_EQ(0x0D, static_cast<int>(drills::kIconPeach));
    CHECK_EQ(0x0F, static_cast<int>(drills::kIconSheik));
    CHECK_EQ(0x11, static_cast<int>(drills::kIconMarth));
    CHECK_EQ(0x13, static_cast<int>(drills::kIconFalco));
    CHECK_EQ(0x19, static_cast<int>(drills::kIconLucas));
    CHECK_EQ(0x20, static_cast<int>(drills::kIconLucario));
    CHECK_EQ(0x25, static_cast<int>(drills::kIconWolf));
    CHECK_EQ(0x26, static_cast<int>(drills::kIconSnake));
    CHECK_EQ(0x2E, static_cast<int>(drills::kIconMewtwo));

    for (u32 i = 0; i < sizeof(kExpected) / sizeof(kExpected[0]); i++)
    {
        int kind = drills::kindOfIcon(kExpected[i].icon);
        CHECK_EQ(kExpected[i].kind, kind);
        CHECK(strcmp(menu::characterName(static_cast<u32>(kind)), kExpected[i].name) == 0);
        // Every mapped icon fits in select.cpp's 64-bit allowed/grid bitmasks.
        CHECK(kExpected[i].icon >= 0 && kExpected[i].icon < 64);
    }

    // Unmapped / sentinel / out-of-range icons return -1.
    CHECK_EQ(-1, drills::kindOfIcon(drills::kIconEnd));
    CHECK_EQ(-1, drills::kindOfIcon(0x00)); // Mario (unmapped, not preset)
    CHECK_EQ(-1, drills::kindOfIcon(0x28)); // SelectNone
    CHECK_EQ(-1, drills::kindOfIcon(0x29)); // Random
    CHECK_EQ(-1, drills::kindOfIcon(-1));
    CHECK_EQ(-1, drills::kindOfIcon(64));
}

TEST(select_rules, set_helpers_handle_any_single_multi_and_full_7_icon_sets)
{
    u8 anySet[drills::kSetSize] = {drills::kIconEnd};
    CHECK(drills::anyIcon(anySet));
    CHECK_EQ(static_cast<int>(drills::kIconEnd), drills::onlyIcon(anySet));
    CHECK(drills::setHas(anySet, 0x00));
    CHECK(drills::setHas(anySet, drills::kIconMewtwo));
    CHECK(drills::setHas(anySet, 0x29));

    u8 singleSet[drills::kSetSize] = {drills::kIconSnake, drills::kIconEnd};
    CHECK(!drills::anyIcon(singleSet));
    CHECK_EQ(static_cast<int>(drills::kIconSnake), drills::onlyIcon(singleSet));
    CHECK(drills::setHas(singleSet, drills::kIconSnake));
    CHECK(!drills::setHas(singleSet, drills::kIconFox));

    u8 sevenSet[drills::kSetSize] = {
        drills::kIconFox, drills::kIconFalco, drills::kIconWolf, drills::kIconNess,
        drills::kIconLucas, drills::kIconLucario, drills::kIconMewtwo, drills::kIconEnd
    };
    CHECK(!drills::anyIcon(sevenSet));
    CHECK_EQ(static_cast<int>(drills::kIconEnd), drills::onlyIcon(sevenSet));
    CHECK_EQ(7, countIcons(sevenSet));
    CHECK(drills::setHas(sevenSet, drills::kIconFox));
    CHECK(drills::setHas(sevenSet, drills::kIconMewtwo));
    CHECK(!drills::setHas(sevenSet, drills::kIconPeach));
    CHECK(!drills::setHas(sevenSet, drills::kIconSnake));
    CHECK(!drills::setHas(sevenSet, 0x29)); // Random
}

TEST(select_rules, hover_float_is_solo_with_6_filtered_p1_icons_including_ganondorf)
{
    const drills::SelectRule* r = drills::selectRuleOf(drills::kDrillHoverFloat);
    REQUIRE(r != NULL);
    CHECK_EQ(static_cast<int>(drills::kDrillHoverFloat), static_cast<int>(r->drill));
    // Solo CSS view trims P1 to the 6 hover/float/DJC fighters while addCpu spawns Target CPU.
    CHECK_EQ(1, static_cast<int>(r->solo));
    CHECK(!drills::anyIcon(r->player));
    CHECK(drills::anyIcon(r->cpu));
    CHECK_EQ(static_cast<int>(drills::kIconEnd), drills::onlyIcon(r->player));
    CHECK_EQ(6, countIcons(r->player));
    CHECK_EQ(static_cast<int>(drills::kIconPeach), static_cast<int>(r->player[0]));

    CHECK(drills::setHas(r->player, drills::kIconPeach));
    CHECK(drills::setHas(r->player, drills::kIconMewtwo));
    CHECK(drills::setHas(r->player, drills::kIconGanondorf));
    CHECK(drills::setHas(r->player, drills::kIconYoshi));
    CHECK(drills::setHas(r->player, drills::kIconNess));
    CHECK(drills::setHas(r->player, drills::kIconLucas));
    CHECK_EQ(22, drills::kindOfIcon(drills::kIconGanondorf));

    // Non-float/DJC fighters and Random are rejected for P1, allowed for CPU.
    static const int kRejected[] = {0x00, drills::kIconFox, drills::kIconFalco, drills::kIconWolf,
                                    drills::kIconLucario, drills::kIconSnake, 0x28, 0x29};
    for (u32 i = 0; i < sizeof(kRejected) / sizeof(kRejected[0]); i++)
    {
        CHECK(!drills::setHas(r->player, kRejected[i]));
        CHECK(drills::setHas(r->cpu, kRejected[i]));
    }
    // Preset P1 icon (player[0]) must have a valid gmCharacterKind.
    CHECK(drills::kindOfIcon(r->player[0]) >= 0);
}

TEST(select_rules, cancel_sprint_is_solo_with_7_filtered_p1_icons)
{
    const drills::SelectRule* r = drills::selectRuleOf(drills::kDrillCancelSprint);
    REQUIRE(r != NULL);
    CHECK_EQ(static_cast<int>(drills::kDrillCancelSprint), static_cast<int>(r->drill));
    CHECK_EQ(1, static_cast<int>(r->solo));
    CHECK(!drills::anyIcon(r->player));
    CHECK(drills::anyIcon(r->cpu));
    CHECK_EQ(static_cast<int>(drills::kIconEnd), drills::onlyIcon(r->player));
    CHECK_EQ(7, countIcons(r->player));
    CHECK_EQ(static_cast<int>(drills::kIconEnd), static_cast<int>(r->player[7]));
    CHECK_EQ(static_cast<int>(drills::kIconFox), static_cast<int>(r->player[0]));

    CHECK(drills::setHas(r->player, drills::kIconFox));
    CHECK(drills::setHas(r->player, drills::kIconFalco));
    CHECK(drills::setHas(r->player, drills::kIconWolf));
    CHECK(drills::setHas(r->player, drills::kIconNess));
    CHECK(drills::setHas(r->player, drills::kIconLucas));
    CHECK(drills::setHas(r->player, drills::kIconLucario));
    CHECK(drills::setHas(r->player, drills::kIconMewtwo));

    CHECK(!drills::setHas(r->player, drills::kIconPeach));
    CHECK(!drills::setHas(r->player, drills::kIconYoshi));
    CHECK(!drills::setHas(r->player, drills::kIconSnake));
    CHECK(!drills::setHas(r->player, 0x00));
    CHECK(!drills::setHas(r->player, 0x28));
    CHECK(!drills::setHas(r->player, 0x29));
    CHECK(drills::kindOfIcon(r->player[0]) >= 0);
}

TEST(select_rules, snake_recovery_is_solo_and_single_icon_locked_to_snake)
{
    const drills::SelectRule* r = drills::selectRuleOf(drills::kDrillSnakeRecovery);
    REQUIRE(r != NULL);
    CHECK_EQ(static_cast<int>(drills::kDrillSnakeRecovery), static_cast<int>(r->drill));
    CHECK_EQ(1, static_cast<int>(r->solo));
    CHECK(!drills::anyIcon(r->player));
    CHECK(drills::anyIcon(r->cpu));
    CHECK_EQ(1, countIcons(r->player));
    CHECK_EQ(static_cast<int>(drills::kIconSnake), drills::onlyIcon(r->player));
    CHECK(drills::setHas(r->player, drills::kIconSnake));
    CHECK(!drills::setHas(r->player, drills::kIconFox));
    CHECK(!drills::setHas(r->player, drills::kIconMewtwo));
    CHECK(!drills::setHas(r->player, 0x29));
    CHECK_EQ(42, drills::kindOfIcon(drills::onlyIcon(r->player)));
}

TEST(select_rules, strictly_solo_and_2p_drill_classification_is_complete)
{
    static const int kSoloAny[] = {
        drills::kDrillLedgedash,
        drills::kDrillWavedash,
        drills::kDrillFlowCourse,
        drills::kDrillRecoverySweetspot,
        drills::kDrillComboBreakRush
    };
    for (u32 i = 0; i < sizeof(kSoloAny) / sizeof(kSoloAny[0]); i++)
    {
        const drills::SelectRule* r = drills::selectRuleOf(kSoloAny[i]);
        REQUIRE(r != NULL);
        CHECK_EQ(kSoloAny[i], static_cast<int>(r->drill));
        CHECK_EQ(1, static_cast<int>(r->solo));
        CHECK(drills::anyIcon(r->player));
        CHECK(drills::anyIcon(r->cpu));
        CHECK_EQ(static_cast<int>(drills::kIconEnd), drills::onlyIcon(r->player));
        // Pointer identity stays stable for select::needsRebuild().
        CHECK(r == drills::selectRuleOf(kSoloAny[i]));
    }

    // 2P unrestricted modes return NULL (L-Cancel supports Target = CPU).
    static const int kTwoPlayer[] = {
        drills::kDrillNone,
        drills::kDrillLCancel,
        drills::kDrillPowershield,
        drills::kDrillRar,
        drills::kDrillDacus,
        drills::kDrillBReverse,
        drills::kDrillReactionLab,
        drills::kDrillTechchaseEscape,
        drills::kDrillFloorhug,
        16, // reserved kTechBestSlot
        -1,
        drills::kDrillIdCount,
        255
    };
    for (u32 i = 0; i < sizeof(kTwoPlayer) / sizeof(kTwoPlayer[0]); i++)
    {
        CHECK(drills::selectRuleOf(kTwoPlayer[i]) == NULL);
    }

    // Every restricted rule's icons must all be mapped in kindOfIcon() and < 64.
    for (int d = 0; d < drills::kDrillIdCount; d++)
    {
        const drills::SelectRule* r = drills::selectRuleOf(d);
        if (r == NULL || drills::anyIcon(r->player))
        {
            continue;
        }
        int n = countIcons(r->player);
        CHECK(n > 0 && n < drills::kSetSize);
        CHECK_EQ(static_cast<int>(drills::kIconEnd), static_cast<int>(r->player[n]));
        for (int k = 0; k < n; k++)
        {
            CHECK(r->player[k] < 64);
            CHECK(drills::kindOfIcon(r->player[k]) >= 0);
        }
    }
}
