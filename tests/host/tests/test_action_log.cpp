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
        kStatusWait, kStatusGuard, kStatusEscapeAir, 0x49, 0x76, kStatusSpecialN
    };
    for (int i = 0; i < 6; i++)
    {
        actionLogPushFrame(st, f++, kExtra[i], i, false, NULL, NULL);
    }
    REQUIRE(st.count == kActionLogCap);
    CHECK_EQ(10u, st.totalTransitions);
    CHECK(strcmp(actionLogAt(st, 0)->label, "Fair") == 0);
    CHECK(strcmp(actionLogAt(st, 3)->label, "Shield") == 0);
    CHECK_EQ(static_cast<int>(kStateDefense), static_cast<int>(actionLogAt(st, 3)->stateClass));
    CHECK(strcmp(actionLogAt(st, 5)->label, "Tumbl") == 0);
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogAt(st, 5)->stateClass));
    CHECK(strcmp(actionLogAt(st, 6)->label, "Ledge") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogAt(st, 6)->stateClass));
    CHECK(strcmp(actionLogAt(st, 7)->label, "SpecN") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogAt(st, 7)->stateClass));
}

TEST(action_log, verified_brawl_status_labels_and_classes)
{
    using namespace tm::extra;
    char out[kActionLogLabelSize];

    // Down Smash (0x2D..0x2F) vs Up Smash (0x30..0x32).
    actionLogShortLabel(0x2F, NULL, NULL, out);
    CHECK(strcmp(out, "DSmsh") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogClassify(0x2F, false)));

    actionLogShortLabel(0x32, NULL, NULL, out);
    CHECK(strcmp(out, "USmsh") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogClassify(0x32, false)));

    // Platform drop (Pass 0x72), Ledge grab (Cliff_Catch_Move 0x73, Cliff_Catch 0x74).
    actionLogShortLabel(0x72, NULL, NULL, out);
    CHECK(strcmp(out, "Pass") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogClassify(0x72, false)));

    actionLogShortLabel(0x73, NULL, NULL, out);
    CHECK(strcmp(out, "Ledge") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogClassify(0x73, true)));

    actionLogShortLabel(0x74, NULL, NULL, out);
    CHECK(strcmp(out, "Ledge") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogClassify(0x74, true)));

    // Wall jump/cling (0x67..0x6C) and Footstool jump (0x6D) are Ledge/Air; Tread_Damage (0x6E..0x70) is Hitstun.
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogClassify(0x6D, true)));
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogClassify(0x6E, true)));

    // Grabs (0x34..0x3B), Throw (0x3C), Being Grabbed (Capture_Wait 0x3E).
    actionLogShortLabel(0x34, NULL, NULL, out);
    CHECK(strcmp(out, "Grab") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogClassify(0x34, false)));

    actionLogShortLabel(0x3C, NULL, NULL, out);
    CHECK(strcmp(out, "Throw") == 0);
    CHECK_EQ(static_cast<int>(kStateAttack), static_cast<int>(actionLogClassify(0x3C, false)));

    actionLogShortLabel(0x3E, NULL, NULL, out);
    CHECK(strcmp(out, "Grabd") == 0);
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogClassify(0x3E, false)));

    // Damage_Fall (0x49) and Down (0x4A..0x55).
    actionLogShortLabel(0x49, NULL, NULL, out);
    CHECK(strcmp(out, "Tumbl") == 0);
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogClassify(0x49, true)));

    actionLogShortLabel(0x4A, NULL, NULL, out);
    CHECK(strcmp(out, "Down") == 0);
    CHECK_EQ(static_cast<int>(kStateHitstun), static_cast<int>(actionLogClassify(0x4A, false)));

    // Stop_Wall (0x65) is wall bonk, NOT Tech / Defense (Passive ends at 0x64).
    actionLogShortLabel(0x64, NULL, NULL, out);
    CHECK(strcmp(out, "Tech") == 0);
    CHECK_EQ(static_cast<int>(kStateDefense), static_cast<int>(actionLogClassify(0x64, false)));

    actionLogShortLabel(0x65, "StopWall", NULL, out);
    CHECK(strcmp(out, "Tech") != 0);
    CHECK(actionLogClassify(0x65, false) != kStateDefense);

    // LandingFallSpecial (0x19) is distinct from LandingAttackAir (0x18).
    actionLogShortLabel(0x18, NULL, NULL, out);
    CHECK(strcmp(out, "LandA") == 0);
    actionLogShortLabel(0x19, NULL, NULL, out);
    CHECK(strcmp(out, "LandS") == 0);
    CHECK_EQ(static_cast<int>(kStateLedgeAir), static_cast<int>(actionLogClassify(0x19, false)));
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
    CHECK_EQ(480u, static_cast<u32>(sizeof(tm::main::Settings)));
    CHECK_EQ(22u, static_cast<u32>(offsetof(tm::main::Settings, general.actionLog)));

    tm::main::Settings s;
    memset(&s, 0, sizeof(s));
    s.general.actionLog = 99;
    tm::main::sanitizeSettings(s);
    CHECK_EQ(static_cast<int>(tm::main::kActionLogCpu), static_cast<int>(s.general.actionLog));
}

TEST(action_log, sf6_dual_strip_phase_classification_advantage_and_ring_wrap)
{
    using namespace tm::extra;
    using namespace tm::game;
    Sf6StripState sf;
    sf6ResetState(sf);

    // P1 throws an aerial (2f startup, 2f active hitbox, 3f recovery);
    // CPU blocks on frame 102 and takes 7f shieldstun -> P1 is +2f advantage!
    s32 f = 100;
    // f=100..101: P1 startup, CPU actionable.
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, false, false, kStatusGuard, true, false, false);
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, false, false, kStatusGuard, true, false, false);
    // f=102..103: P1 active hitbox, CPU enters shieldstun.
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, true, false, kStatusGuardDamage, false, false, true);
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, true, false, kStatusGuardDamage, false, false, true);
    // f=104..106: P1 recovery, CPU still in shieldstun.
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, false, false, kStatusGuardDamage, false, false, true);
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, false, false, kStatusGuardDamage, false, false, true);
    sf6PushDualFrame(sf, f++, kStatusAttackAirFirst, false, false, false, kStatusGuardDamage, false, false, true);
    // f=107..108: P1 actionable (gap!), CPU still in shieldstun (2f advantage).
    sf6PushDualFrame(sf, f++, kStatusWait, true, false, false, kStatusGuardDamage, false, false, true);
    sf6PushDualFrame(sf, f++, kStatusWait, true, false, false, kStatusGuardDamage, false, false, true);
    // f=109: Both actionable -> exchange settles at +2f.
    sf6PushDualFrame(sf, f++, kStatusWait, true, false, false, kStatusWait, true, false, false);

    CHECK_EQ(10, static_cast<int>(sf.filled));
    CHECK_EQ(static_cast<int>(kSf6Startup), static_cast<int>(sf6CellAt(sf, 0, 0)));
    CHECK_EQ(static_cast<int>(kSf6Startup), static_cast<int>(sf6CellAt(sf, 0, 1)));
    CHECK_EQ(static_cast<int>(kSf6Active), static_cast<int>(sf6CellAt(sf, 0, 2)));
    CHECK_EQ(static_cast<int>(kSf6Active), static_cast<int>(sf6CellAt(sf, 0, 3)));
    CHECK_EQ(static_cast<int>(kSf6Recovery), static_cast<int>(sf6CellAt(sf, 0, 4)));
    CHECK_EQ(static_cast<int>(kSf6Gap), static_cast<int>(sf6CellAt(sf, 0, 7)));
    CHECK_EQ(static_cast<int>(kSf6Stun), static_cast<int>(sf6CellAt(sf, 1, 2)));
    CHECK_EQ(static_cast<int>(kSf6Stun), static_cast<int>(sf6CellAt(sf, 1, 8)));
    CHECK_EQ(1, static_cast<int>(sf.hasAdvantage));
    CHECK_EQ(2, static_cast<int>(sf.advantage));

    // Push 30 more frames to verify 32-cell ring wrap keeps newest 32 cells.
    for (int i = 0; i < 30; i++)
    {
        sf6PushDualFrame(sf, f++, kStatusWait, true, false, false, kStatusWait, true, false, false);
    }
    CHECK_EQ(kSf6MaxCells, static_cast<int>(sf.filled));
    CHECK_EQ(static_cast<int>(kSf6Empty), static_cast<int>(sf6CellAt(sf, 0, kSf6MaxCells - 1)));

    // 4P attacker-hitlag exclusion: P1 hitting P3/P4 incurs attacker hitlag only
    // (hitstun == 0, shieldStun == 0, hitReceived == 0, shieldReceived == 0).
    // sf6IsDefenderStun must return false so idle P2 never triggers a bogus exchange.
    CHECK(!sf6IsDefenderStun(kStatusAttackAirFirst, 0, 0, 0, 0));
    CHECK(sf6IsDefenderStun(kStatusGuardDamage, 0, 0, 0, 0));
    CHECK(sf6IsDefenderStun(kStatusWait, 5, 0, 0, 0));
    CHECK(sf6IsDefenderStun(kStatusWait, 0, 4, 0, 0));
    CHECK(sf6IsDefenderStun(kStatusWait, 0, 0, 1, 0));
    CHECK(sf6IsDefenderStun(kStatusWait, 0, 0, 0, 1));

    Sf6StripState sf4p;
    sf6ResetState(sf4p);
    s32 f4 = 500;
    for (int i = 0; i < 6; i++)
    {
        bool stun0 = sf6IsDefenderStun(kStatusAttackAirFirst, 0, 0, 0, 0);
        bool stun1 = sf6IsDefenderStun(kStatusWait, 0, 0, 0, 0);
        sf6PushDualFrame(sf4p, f4++, kStatusAttackAirFirst, false, i == 2, stun0, kStatusWait, true, false, stun1);
    }
    sf6PushDualFrame(sf4p, f4++, kStatusWait, true, false, false, kStatusWait, true, false, false);
    CHECK_EQ(0, static_cast<int>(sf4p.hasAdvantage));
    CHECK_EQ(0, static_cast<int>(sf4p.activeExchange));

    // Actionable Damage_Fall (0x49), Capture_Jump (0x41), and grounded Down*
    // (0x4A..0x53) with hitstun == 0 must NEVER register as defender stun or
    // emit a bogus 0f advantage or gold stun cells; Down_Damage (0x54) and
    // Down_Reflect_Lr (0x55) ARE defender stun even when hitstun == 0.
    CHECK(!sf6IsDefenderStun(0x40, 0, 0, 0, 0)); // Capture_Cut (grounded grab release)
    CHECK(!sf6IsDefenderStun(0x41, 0, 0, 0, 0)); // Capture_Jump (aerial grab release)
    CHECK(!sf6IsDefenderStun(0x49, 0, 0, 0, 0));
    CHECK(!sf6IsDefenderStun(0x4D, 0, 0, 0, 0));
    CHECK(!sf6IsDefenderStun(0x4F, 0, 0, 0, 0));
    CHECK(!sf6IsDefenderStun(0x53, 0, 0, 0, 0)); // Down_Stand_Attack
    CHECK(sf6IsDefenderStun(0x54, 0, 0, 0, 0));  // Down_Damage (jab lock)
    CHECK(sf6IsDefenderStun(0x55, 0, 0, 0, 0));  // Down_Reflect_Lr
    CHECK(sf6IsDefenderStun(0x49, 12, 0, 0, 0)); // genuine hitstun still counts
    CHECK(sf6IsDefenderStun(0x43, 0, 0, 0, 0));  // Damage..Damage_Fly still count
    CHECK(sf6IsDefenderStun(0x3E, 0, 0, 0, 0));  // Capture_Wait (grabbed) still counts
    CHECK_EQ(static_cast<int>(kSf6Recovery), static_cast<int>(sf6ClassifyCell(0x4F, false, false, false, false)));

    Sf6StripState sfTc;
    sf6ResetState(sfTc);
    s32 ft = 700;
    for (int i = 0; i < 8; i++)
    {
        bool stun1 = sf6IsDefenderStun(0x49, 0, 0, 0, 0);
        sf6PushDualFrame(sfTc, ft++, kStatusWait, true, false, false, 0x49, true, false, stun1);
    }
    for (int i = 0; i < 6; i++)
    {
        bool stun1 = sf6IsDefenderStun(0x4D, 0, 0, 0, 0);
        sf6PushDualFrame(sfTc, ft++, kStatusWait, true, false, false, 0x4D, false, false, stun1);
    }
    sf6PushDualFrame(sfTc, ft++, kStatusWait, true, false, false, kStatusWait, true, false, false);
    CHECK_EQ(0, static_cast<int>(sfTc.hasAdvantage));
    CHECK_EQ(0, static_cast<int>(sfTc.activeExchange));
    for (int i = 0; i < static_cast<int>(sfTc.filled); i++)
    {
        CHECK(sf6CellAt(sfTc, 1, i) != kSf6Stun);
    }

    // Grab active-frame detection + dead matte whiff endlag:
    // Catch (0x34): startup -> active grab (kSf6Grab, Magenta-Purple) -> vulnerable lag (kSf6Recovery, Dead Ash).
    Sf6StripState sfGrab;
    sf6ResetState(sfGrab);
    s32 fg = 900;
    sf6PushDualFrame(sfGrab, fg++, 0x34, false, false, false, kStatusWait, true, false, false, false, false, false);
    sf6PushDualFrame(sfGrab, fg++, 0x34, false, false, false, kStatusWait, true, false, false, false, false, false);
    sf6PushDualFrame(sfGrab, fg++, 0x34, false, false, false, kStatusWait, true, false, false, true, false, false);
    sf6PushDualFrame(sfGrab, fg++, 0x34, false, false, false, kStatusWait, true, false, false, true, false, false);
    sf6PushDualFrame(sfGrab, fg++, 0x34, false, false, false, kStatusWait, true, false, false, false, false, false);
    CHECK_EQ(static_cast<int>(kSf6Startup), static_cast<int>(sf6CellKind(sf6CellAt(sfGrab, 0, 0))));
    CHECK_EQ(static_cast<int>(kSf6Grab), static_cast<int>(sf6CellKind(sf6CellAt(sfGrab, 0, 2))));
    CHECK_EQ(static_cast<int>(kSf6Grab), static_cast<int>(sf6CellKind(sf6CellAt(sfGrab, 0, 3))));
    CHECK_EQ(static_cast<int>(kSf6Recovery), static_cast<int>(sf6CellKind(sf6CellAt(sfGrab, 0, 4))));
    CHECK_EQ(0xCC4CF5FFu, sf6CellColor(sf6CellAt(sfGrab, 0, 2)));
    CHECK_EQ(0x4C505EFFu, sf6CellColor(sf6CellAt(sfGrab, 0, 4)));

    // Ledgedash XLU sync: Ledge (XLU) -> Drop (XLU) -> Jump (XLU) -> Airdodge (XLU) -> Land (XLU) -> GALINT -> Idle.
    Sf6StripState sfLedge;
    sf6ResetState(sfLedge);
    s32 fl = 1000;
    sf6PushDualFrame(sfLedge, fl++, kStatusCliffWait, false, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusFall, false, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusJumpAerial, false, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusEscapeAir, false, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusLandingFallSpecial, false, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusWait, true, false, false, kStatusWait, true, false, false, false, true, false);
    sf6PushDualFrame(sfLedge, fl++, kStatusWait, true, false, false, kStatusWait, true, false, false, false, false, false);
    CHECK_EQ(static_cast<int>(kSf6Ledge), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 0))));
    CHECK(sf6CellHasXlu(sf6CellAt(sfLedge, 0, 0)));
    CHECK_EQ(static_cast<int>(kSf6LedgeDrop), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 1))));
    CHECK(sf6CellHasXlu(sf6CellAt(sfLedge, 0, 1)));
    CHECK_EQ(static_cast<int>(kSf6Jump), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 2))));
    CHECK_EQ(static_cast<int>(kSf6Dodge), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 3))));
    CHECK_EQ(static_cast<int>(kSf6Recovery), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 4))));
    CHECK(sf6CellHasXlu(sf6CellAt(sfLedge, 0, 4)));
    CHECK_EQ(static_cast<int>(kSf6Galint), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 5))));
    CHECK_EQ(static_cast<int>(kSf6Empty), static_cast<int>(sf6CellKind(sf6CellAt(sfLedge, 0, 6))));

    // Out-of-play fighters (Standby / Dead) stay matte empty even with global XLU active.
    CHECK_EQ(static_cast<int>(kSf6Empty),
             static_cast<int>(sf6ClassifyCell(kStatusStandby, true, false, false, false, false, true, false)));
    CHECK_EQ(static_cast<int>(kSf6Empty),
             static_cast<int>(sf6ClassifyCell(kStatusDead, false, false, false, false, false, true, false)));
}
