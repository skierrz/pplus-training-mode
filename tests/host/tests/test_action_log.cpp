#include <string.h>

#include "extra/action_log_core.h"
#include "host/test.h"
#include "main/game/status_ids.h"
#include "main/settings.h"
#include "main/storage/areas.h"

TEST(action_log, ring_buffer_accumulates_durations_and_wraps_at_eight)
{
    using namespace tm::extra;
    using namespace tm::game;
    ActionLogState st;
    actionLogResetState(st);
    CHECK_EQ(0, static_cast<int>(st.count));

    // Dash 12f -> JumpSq 3f -> Fair 24f -> Land 7f.
    s32 f = 100;
    for (int i = 0; i < 12; i++)
    {
        actionLogPushFrame(st, f++, kStatusDash, 3, false, "Dash", "Dash");
    }
    for (int i = 0; i < 3; i++)
    {
        actionLogPushFrame(st, f++, kStatusJumpSquat, 10, false, "JumpSquat", "JumpSquat");
    }
    for (int i = 0; i < 24; i++)
    {
        actionLogPushFrame(st, f++, kStatusAttackAirFirst, 50, true, "AttackAir", "AttackAirF");
    }
    for (int i = 0; i < 7; i++)
    {
        actionLogPushFrame(st, f++, kStatusLandingAttackAir, 24, false, "LandingAttackAir", "LandingAirF");
    }
    // Duplicate frame number is ignored cleanly.
    actionLogPushFrame(st, f - 1, kStatusLandingAttackAir, 24, false, "LandingAttackAir", "LandingAirF");

    REQUIRE(st.count == 4);
    CHECK_EQ(4u, st.totalTransitions);

    const ActionLogEntry* e0 = actionLogAt(st, 0);
    const ActionLogEntry* e1 = actionLogAt(st, 1);
    const ActionLogEntry* e2 = actionLogAt(st, 2);
    const ActionLogEntry* e3 = actionLogAt(st, 3);
    REQUIRE(e0 != NULL && e1 != NULL && e2 != NULL && e3 != NULL);
    CHECK(strcmp(e0->label, "Dash") == 0);
    CHECK_EQ(12, static_cast<int>(e0->frames));
    CHECK_EQ(static_cast<int>(kStateNeutral), static_cast<int>(e0->stateClass));

    CHECK(strcmp(e1->label, "JmpSq") == 0);
    CHECK_EQ(3, static_cast<int>(e1->frames));
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(e1->stateClass));

    CHECK(strcmp(e2->label, "Fair") == 0);
    CHECK_EQ(24, static_cast<int>(e2->frames));
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(e2->stateClass));

    CHECK(strcmp(e3->label, "LandA") == 0);
    CHECK_EQ(7, static_cast<int>(e3->frames));

    char pill[20];
    actionLogFormatPill(*e2, pill, sizeof(pill));
    CHECK(strcmp(pill, "Fair 24f") == 0);

    // Push 6 more transitions to verify 8-entry ring wrap keeps the 8 newest.
    static const int kExtra[6] = {
        kStatusWait, kStatusGuard, kStatusEscapeAir, kStatusDamageFlyRoll, 0x76, kStatusSpecialN
    };
    for (int i = 0; i < 6; i++)
    {
        actionLogPushFrame(st, f++, kExtra[i], i, false, NULL, NULL);
    }
    REQUIRE(st.count == kActionLogCap);
    CHECK_EQ(10u, st.totalTransitions);
    CHECK(strcmp(actionLogAt(st, 0)->label, "Fair") == 0);
    CHECK(strcmp(actionLogAt(st, 3)->label, "Guard") == 0);
    CHECK_EQ(static_cast<int>(kStateDefense), static_cast<int>(actionLogAt(st, 3)->stateClass));
    CHECK(strcmp(actionLogAt(st, 5)->label, "Tumbl") == 0);
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogAt(st, 5)->stateClass));
    CHECK(strcmp(actionLogAt(st, 6)->label, "Ledge") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogAt(st, 6)->stateClass));
    CHECK(strcmp(actionLogAt(st, 7)->label, "SpecN") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogAt(st, 7)->stateClass));
}

TEST(action_log, aerial_subactions_and_settings_layout_round_trip)
{
    using namespace tm::extra;
    char out[kActionLogLabelSize];
    actionLogShortLabel(tm::game::kStatusAttackAirFirst, "AttackAir", "AttackAirN", out);
    CHECK(strcmp(out, "Nair") == 0);
    actionLogShortLabel(tm::game::kStatusAttackAirFirst, "AttackAir", "AttackAirB", out);
    CHECK(strcmp(out, "Bair") == 0);
    actionLogShortLabel(tm::game::kStatusAttackAirFirst, "AttackAir", "AttackAirHi", out);
    CHECK(strcmp(out, "Uair") == 0);
    actionLogShortLabel(tm::game::kStatusAttackAirFirst, "AttackAir", "AttackAirLw", out);
    CHECK(strcmp(out, "Dair") == 0);

    // Settings layout invariant + sanitize clamp.
    CHECK_EQ(464u, static_cast<u32>(sizeof(tm::main::Settings)));
    CHECK_EQ(22u, static_cast<u32>(offsetof(tm::main::Settings, general.actionLog)));

    tm::main::Settings s;
    memset(&s, 0, sizeof(s));
    s.general.actionLog = 99;
    tm::main::sanitizeSettings(s);
    CHECK_EQ(static_cast<int>(tm::main::kActionLogCpu), static_cast<int>(s.general.actionLog));
}
