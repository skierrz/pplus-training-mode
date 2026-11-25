// Host unit tests for:
//  - Multi-Slot Reversals (src/extra/reversal_slots_core.h, P_REVERSALS)
//  - 44-Fighter P+ Tech Tracker (src/extra/pplus_tech_tracker_core.h)
//  - 100% SD backward-compatible CpuOptions (60 B, reserved[1]) migration
#include <string.h>

#include "extra/menu_descriptions.h"
#include "extra/pplus_tech_tracker_core.h"
#include "extra/reversal_slots_core.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/cpu/cpu_settings.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/pages/pages.h"

using namespace tm;
using namespace tm::extra;

namespace {
    void bootStorage()
    {
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }

    TechFrameInput makeTt(s32 frame, int ftKind, s16 status, s16 motionKind = -1, u8 air = 0, float lr = 1.0f,
                          s32 la55 = 0)
    {
        TechFrameInput in;
        memset(&in, 0, sizeof(in));
        in.frame = frame;
        in.ftKind = ftKind;
        in.status = status;
        in.motionKind = motionKind;
        in.airborne = air;
        in.lr = lr;
        in.laBasic55 = la55;
        return in;
    }

    void addTtEv(TechFrameInput& in, s16 status, s16 prev, s16 motionKind = -1)
    {
        if (in.eventCount < kTechTrackerMaxEvents)
        {
            TechStatusEv& e = in.events[in.eventCount++];
            e.status = status;
            e.prev = prev;
            e.motionKind = motionKind;
        }
    }
}

// ---- Multi-Slot Reversal Picker --------------------------------------------

TEST(reversals_tech, pick_reversal_slot_weights_skip_zero_and_per_slot_delay)
{
    ReversalSlot slots[kRevSlotsPerTrigger];
    memset(slots, 0, sizeof(slots));
    // Slot 0: action 13 (Jab), delay 2, weight 30
    slots[0].action = 13;
    slots[0].delay = 2;
    slots[0].weight = 30;
    slots[0].flags = kRevFlagEnabled;
    // Slot 1: zero weight -> MUST be skipped
    slots[1].action = 8;
    slots[1].delay = 9;
    slots[1].weight = 0;
    slots[1].flags = kRevFlagEnabled;
    // Slot 2: disabled flag -> MUST be skipped
    slots[2].action = 4;
    slots[2].delay = 15;
    slots[2].weight = 50;
    slots[2].flags = 0;
    // Slot 3: action 1 (Spotdodge), delay 7, weight 70
    slots[3].action = 1;
    slots[3].delay = 7;
    slots[3].weight = 70;
    slots[3].flags = kRevFlagEnabled;

    int hit0 = 0, hit3 = 0;
    for (u32 r = 0; r < 100; r++)
    {
        int act = -1, del = -1;
        REQUIRE(pickReversalSlot(slots, kRevSlotsPerTrigger, r, act, del));
        if (r < 30)
        {
            CHECK_EQ(13, act);
            CHECK_EQ(2, del);
            hit0++;
        }
        else
        {
            CHECK_EQ(1, act);
            CHECK_EQ(7, del);
            hit3++;
        }
    }
    CHECK_EQ(30, hit0);
    CHECK_EQ(70, hit3);

    // Empty action (0) is skipped even if weight > 0 and enabled.
    ReversalSlot emptyOnly[2] = {{0, 5, 100, kRevFlagEnabled}, {4, 12, 0, kRevFlagEnabled}};
    int act = -1, del = -1;
    CHECK(!pickReversalSlot(emptyOnly, 2, 0, act, del));
}

TEST(reversals_tech, configured_reversals_all_four_triggers_and_off_passthrough)
{
    cpu::CpuOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.counterGround = 13; // Jab
    opts.counterShield = 1;  // Grab
    opts.counterDelay = 3;
    opts.getup = cpu::kGetupStand;
    opts.ledgeOption = cpu::kLedgeNeutral;
    opts.revSlot2Action[cpu::kRevOnHit] = 8;    // Up Smash
    opts.revSlot2Delay[cpu::kRevOnHit] = 6;
    opts.revSlot2Weight[cpu::kRevOnHit] = 50;
    opts.revSlot2Action[cpu::kRevOnShield] = 7; // Up B OoS
    opts.revSlot2Delay[cpu::kRevOnShield] = 9;
    opts.revSlot2Weight[cpu::kRevOnShield] = 100;
    opts.revSlot2Action[cpu::kRevOnGetup] = cpu::kGetupAttack;
    opts.revSlot2Delay[cpu::kRevOnGetup] = 12;
    opts.revSlot2Weight[cpu::kRevOnGetup] = 100;
    opts.revSlot2Action[cpu::kRevOnLedge] = cpu::kLedgeRoll;
    opts.revSlot2Delay[cpu::kRevOnLedge] = 8;
    opts.revSlot2Weight[cpu::kRevOnLedge] = 100;

    // multiReversal == 0 -> strict pass-through (returns false on all 4 triggers).
    int act = 0, del = 0;
    for (int t = 0; t < cpu::kRevTriggerCount; t++)
    {
        CHECK(!pickConfiguredReversal(t, opts, NULL, 0, act, del));
    }

    // Enable multiReversal -> verify all 4 triggers + per-slot delays!
    opts.multiReversal = 1;

    // On-Hit: 50/50 split between Primary (13, delay 3) and Slot 2 (8, delay 6).
    REQUIRE(pickConfiguredReversal(cpu::kRevOnHit, opts, NULL, 10, act, del));
    CHECK_EQ(13, act);
    CHECK_EQ(3, del);
    REQUIRE(pickConfiguredReversal(cpu::kRevOnHit, opts, NULL, 75, act, del));
    CHECK_EQ(8, act);
    CHECK_EQ(6, del);

    // On-Shield: weight 100 -> always Slot 2 (7, delay 9).
    REQUIRE(pickConfiguredReversal(cpu::kRevOnShield, opts, NULL, 0, act, del));
    CHECK_EQ(7, act);
    CHECK_EQ(9, del);

    // On-Getup: weight 100 -> always Slot 2 (Attack, delay 12).
    REQUIRE(pickConfiguredReversal(cpu::kRevOnGetup, opts, NULL, 0, act, del));
    CHECK_EQ(static_cast<int>(cpu::kGetupAttack), act);
    CHECK_EQ(12, del);

    // On-Ledge: weight 100 -> always Slot 2 (Roll, delay 8).
    REQUIRE(pickConfiguredReversal(cpu::kRevOnLedge, opts, NULL, 0, act, del));
    CHECK_EQ(static_cast<int>(cpu::kLedgeRoll), act);
    CHECK_EQ(8, del);

    // Verify 4-slot expansion via ReversalTable extra slots 2 & 3!
    ReversalTable extraTbl;
    reversalTableClear(extraTbl);
    opts.revSlot2Weight[cpu::kRevOnHit] = 25; // Slot 0 = 75, Slot 1 = 25
    extraTbl.slots[cpu::kRevOnHit][2].action = 1;
    extraTbl.slots[cpu::kRevOnHit][2].delay = 14;
    extraTbl.slots[cpu::kRevOnHit][2].weight = 50;
    extraTbl.slots[cpu::kRevOnHit][2].flags = kRevFlagEnabled;
    extraTbl.slots[cpu::kRevOnHit][3].action = 4;
    extraTbl.slots[cpu::kRevOnHit][3].delay = 20;
    extraTbl.slots[cpu::kRevOnHit][3].weight = 50;
    extraTbl.slots[cpu::kRevOnHit][3].flags = kRevFlagEnabled;
    // Total weight = 75 + 25 + 50 + 50 = 200.
    REQUIRE(pickConfiguredReversal(cpu::kRevOnHit, opts, &extraTbl, 120, act, del));
    CHECK_EQ(1, act);
    CHECK_EQ(14, del);
    REQUIRE(pickConfiguredReversal(cpu::kRevOnHit, opts, &extraTbl, 175, act, del));
    CHECK_EQ(4, act);
    CHECK_EQ(20, del);

    // P1 fix: multiReversal == 1 with empty Slot 2 (action == 0) or 0% weight and no extra Slots 3..4
    // MUST return false (pass-through so Per-Hit Counters and On-Getup 0-delay stay untouched).
    cpu::CpuOptions emptySlot2 = opts;
    for (int t = 0; t < cpu::kRevTriggerCount; t++)
    {
        emptySlot2.revSlot2Action[t] = 0;
        CHECK(!pickConfiguredReversal(t, emptySlot2, NULL, 0, act, del));
        CHECK(!pickConfiguredReversal(t, emptySlot2, NULL, 99, act, del));
    }
    cpu::CpuOptions zeroWeightSlot2 = opts;
    zeroWeightSlot2.revSlot2Action[cpu::kRevOnHit] = 13;
    zeroWeightSlot2.revSlot2Weight[cpu::kRevOnHit] = 0;
    CHECK(!pickConfiguredReversal(cpu::kRevOnHit, zeroWeightSlot2, NULL, 0, act, del));
    CHECK(!pickConfiguredReversal(cpu::kRevOnHit, zeroWeightSlot2, NULL, 99, act, del));

    // P1 fix: On-Ledge with Ledge Option == Default (0) + Slot 2 Roll at 50% weight
    // must weigh Default (50%, returns false with action 0 so stepLedge latches Default)
    // against Roll (50%, returns true with kLedgeRoll) rather than firing Roll 100%.
    cpu::CpuOptions ledgeDef = opts;
    ledgeDef.ledgeOption = cpu::kLedgeDefault;
    ledgeDef.revSlot2Action[cpu::kRevOnLedge] = cpu::kLedgeRoll;
    ledgeDef.revSlot2Delay[cpu::kRevOnLedge] = 4;
    ledgeDef.revSlot2Weight[cpu::kRevOnLedge] = 50;
    CHECK(!pickConfiguredReversal(cpu::kRevOnLedge, ledgeDef, NULL, 10, act, del));
    CHECK_EQ(0, act);
    CHECK_EQ(0, del);
    REQUIRE(pickConfiguredReversal(cpu::kRevOnLedge, ledgeDef, NULL, 75, act, del));
    CHECK_EQ(static_cast<int>(cpu::kLedgeRoll), act);
    CHECK_EQ(4, del);
}

// ---- 44-Fighter P+ Tech Tracker Table & State Machines ---------------------

TEST(reversals_tech, all_44_fighters_mapped_unique_and_valid_labels)
{
    static const u8 kExpected44[kTechTrackerRosterCount] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
        0x22, 0x23, 0x25, 0x26, 0x27, 0x29, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31
    };

    const FighterTechDef* table = fighterTechTable();
    bool seen[0x40];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < kTechTrackerRosterCount; i++)
    {
        u8 kind = table[i].ftKind;
        CHECK(kind < 0x40);
        CHECK(!seen[kind]);
        seen[kind] = true;
        CHECK(table[i].techKind > kTechNone && table[i].techKind < kTechKindCount);
        REQUIRE(table[i].label != NULL);
        size_t len = strlen(table[i].label);
        CHECK(len >= 3 && len <= kTechTrackerMaxLabelLen);
        for (size_t c = 0; c < len; c++)
        {
            unsigned char ch = static_cast<unsigned char>(table[i].label[c]);
            CHECK(ch >= 0x20 && ch <= 0x7E);
        }
    }

    for (int i = 0; i < kTechTrackerRosterCount; i++)
    {
        const FighterTechDef* def = NULL;
        u32 id = findFighterTechDef(kExpected44[i], def);
        CHECK(id >= 1 && id <= kTechTrackerRosterCount);
        REQUIRE(def != NULL);
        CHECK_EQ(static_cast<int>(kExpected44[i]), static_cast<int>(def->ftKind));
    }

    // Nana (0x10) aliases cleanly to Popo (0x0F); unused Brawl slots return 0.
    const FighterTechDef* nanaDef = NULL;
    CHECK(findFighterTechDef(0x10, nanaDef) != 0);
    REQUIRE(nanaDef != NULL);
    CHECK_EQ(0x0F, static_cast<int>(nanaDef->ftKind));

    const FighterTechDef* unusedDef = NULL;
    CHECK_EQ(0u, findFighterTechDef(0x1C, unusedDef));
    CHECK_EQ(0u, findFighterTechDef(0x24, unusedDef));
    CHECK_EQ(0u, findFighterTechDef(0x28, unusedDef));

    const FighterTechDef* squirtleDef = NULL;
    CHECK(findFighterTechDef(0x1E, squirtleDef) != 0);
    REQUIRE(squirtleDef != NULL);
    CHECK(strcmp(squirtleDef->label, "Withdraw JC") == 0);
}

TEST(reversals_tech, tech_tracker_detectors_cover_verified_pplus_families)
{
    TechWatch w;
    TechSlotStats st;
    const char* label = NULL;

    // 1. Fox (0x06) JC Shine: 0x115 -> 0x0A
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput f1 = makeTt(10, 0x06, kTtStSpecialLw);
    addTtEv(f1, kTtStSpecialLw, 0x00);
    CHECK(!stepFighterTech(f1, w, st, label));
    CHECK_EQ(1u, st.attempts);
    TechFrameInput f2 = makeTt(14, 0x06, kTtStJumpSquat);
    addTtEv(f2, kTtStJumpSquat, kTtStSpecialLw);
    CHECK(stepFighterTech(f2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "JC Shine") == 0);

    // 1b. Ike (0x22) charged Quick Draw JC held 90 frames (within 180f window):
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput iq1 = makeTt(10, 0x22, kTtStSpecialS);
    addTtEv(iq1, kTtStSpecialS, 0x00);
    CHECK(!stepFighterTech(iq1, w, st, label));
    CHECK_EQ(1u, st.attempts);
    TechFrameInput iq2 = makeTt(100, 0x22, kTtStJumpSquat);
    addTtEv(iq2, kTtStJumpSquat, 0x11B);
    CHECK(stepFighterTech(iq2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Quick Draw JC") == 0);

    // 2. Peach (0x0C) Float Cancel: 0x11E + AttackAirN (0x62) -> Landing (0x16)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput p1 = makeTt(20, 0x0C, kTtStPeachFloat, 0x1E0, 1);
    addTtEv(p1, kTtStPeachFloat, kTtStJump);
    CHECK(!stepFighterTech(p1, w, st, label));
    TechFrameInput p2 = makeTt(22, 0x0C, kTtStPeachFloat, kTtMotionAttackAirN, 1);
    CHECK(!stepFighterTech(p2, w, st, label));
    CHECK_EQ(1u, st.attempts);
    TechFrameInput p3 = makeTt(26, 0x0C, kTtStLanding, kTtMotionAttackAirN, 0);
    addTtEv(p3, kTtStLanding, kTtStPeachFloat, kTtMotionAttackAirN);
    CHECK(stepFighterTech(p3, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Float Cancel") == 0);

    // 3. Mewtwo (0x26) Hover Cancel: 0x120 + AttackAir (0x63) -> Landing (0x16)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput m1 = makeTt(30, 0x26, kTtStMewtwoHover, 0x1F0, 1);
    addTtEv(m1, kTtStMewtwoHover, kTtStJump);
    CHECK(!stepFighterTech(m1, w, st, label));
    TechFrameInput m2 = makeTt(32, 0x26, kTtStMewtwoHover, 0x63, 1);
    CHECK(!stepFighterTech(m2, w, st, label));
    TechFrameInput m3 = makeTt(35, 0x26, kTtStLanding, 0x63, 0);
    addTtEv(m3, kTtStLanding, kTtStMewtwoHover, 0x63);
    CHECK(stepFighterTech(m3, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Hover Cancel") == 0);

    // 4. Ness (0x0A) DJC: Jump_Aerial (0x0C) -> Attack_Air (0x33) within 6f
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput n1 = makeTt(40, 0x0A, kTtStJumpAerial, -1, 1);
    addTtEv(n1, kTtStJumpAerial, kTtStJump);
    CHECK(!stepFighterTech(n1, w, st, label));
    TechFrameInput n2 = makeTt(42, 0x0A, kTtStAttackAir, 0x62, 1);
    addTtEv(n2, kTtStAttackAir, kTtStJumpAerial, 0x62);
    CHECK(stepFighterTech(n2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "DJC Aerial") == 0);

    // 5. Mario (0x00) RAR Bair: Run -> Turn_Run -> Jump_Squat -> Jump (flipped lr) -> Attack_Air (0x64)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput r0 = makeTt(50, 0x00, kTtStRun, -1, 0, 1.0f);
    CHECK(!stepFighterTech(r0, w, st, label));
    TechFrameInput r1 = makeTt(51, 0x00, kTtStTurnRun, -1, 0, 1.0f);
    addTtEv(r1, kTtStTurnRun, kTtStRun);
    CHECK(!stepFighterTech(r1, w, st, label));
    TechFrameInput r2 = makeTt(52, 0x00, kTtStJumpSquat, -1, 0, -1.0f);
    addTtEv(r2, kTtStJumpSquat, kTtStTurnRun);
    CHECK(!stepFighterTech(r2, w, st, label));
    TechFrameInput r3 = makeTt(55, 0x00, kTtStJump, -1, 1, -1.0f);
    addTtEv(r3, kTtStJump, kTtStJumpSquat);
    CHECK(!stepFighterTech(r3, w, st, label));
    TechFrameInput r4 = makeTt(56, 0x00, kTtStAttackAir, kTtMotionAttackAirB, 1, -1.0f);
    addTtEv(r4, kTtStAttackAir, kTtStJump, kTtMotionAttackAirB);
    CHECK(stepFighterTech(r4, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "RAR Bair") == 0);

    // 6. Pikachu (0x07) B-Reverse: SpecialN + LA-Basic[55] 4 -> 1 -> 0 with lr flip
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput b1 = makeTt(60, 0x07, kTtStSpecialN, -1, 1, 1.0f, 4);
    addTtEv(b1, kTtStSpecialN, kTtStJump);
    CHECK(!stepFighterTech(b1, w, st, label));
    TechFrameInput b2 = makeTt(63, 0x07, kTtStSpecialN, -1, 1, 1.0f, 1);
    CHECK(!stepFighterTech(b2, w, st, label));
    TechFrameInput b3 = makeTt(64, 0x07, kTtStSpecialN, -1, 1, -1.0f, 0);
    CHECK(stepFighterTech(b3, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "B-Reverse") == 0);

    // 7. Snake (0x2E) DACUS: Attack_Dash (0x26) -> Attack_Hi4_Start (0x30)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput d1 = makeTt(70, 0x2E, kTtStAttackDash);
    addTtEv(d1, kTtStAttackDash, kTtStRun);
    CHECK(!stepFighterTech(d1, w, st, label));
    TechFrameInput d2 = makeTt(72, 0x2E, kTtStAttackHi4Start);
    addTtEv(d2, kTtStAttackHi4Start, kTtStAttackDash);
    CHECK(stepFighterTech(d2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Snake DACUS") == 0);

    // 8. Knuckles (0x2D) Wall Climb -> Walljump: Attach_Wall (0x68) -> Wall_Jump (0x67)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput k1 = makeTt(80, 0x2D, kTtStAttachWall, -1, 1);
    addTtEv(k1, kTtStAttachWall, 0x0E);
    CHECK(!stepFighterTech(k1, w, st, label));
    TechFrameInput k2 = makeTt(84, 0x2D, kTtStWallJump, -1, 1);
    addTtEv(k2, kTtStWallJump, kTtStAttachWall);
    CHECK(stepFighterTech(k2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Wall Climb") == 0);

    // 9. Luigi (0x08) Wavedash: Escape_Air (0x21) -> Landing_Fall_Special (0x19)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput l1 = makeTt(90, 0x08, kTtStEscapeAir, -1, 1);
    addTtEv(l1, kTtStEscapeAir, kTtStJump);
    CHECK(!stepFighterTech(l1, w, st, label));
    TechFrameInput l2 = makeTt(91, 0x08, kTtStLandingFallSpecial, -1, 0);
    addTtEv(l2, kTtStLandingFallSpecial, kTtStEscapeAir);
    CHECK(stepFighterTech(l2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Wavedash") == 0);

    // 10. Link (0x02) Bomb AGT: Escape_Air (0x21) -> Item_Throw (0x9B)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput a1 = makeTt(100, 0x02, kTtStEscapeAir, -1, 1);
    addTtEv(a1, kTtStEscapeAir, kTtStJump);
    CHECK(!stepFighterTech(a1, w, st, label));
    TechFrameInput a2 = makeTt(101, 0x02, kTtStItemThrow, -1, 1);
    addTtEv(a2, kTtStItemThrow, kTtStEscapeAir);
    CHECK(stepFighterTech(a2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Bomb AGT") == 0);

    // 11. Ganondorf (0x14) Float Aerial: airborne SpecialN (0x112) -> Attack_Air (0x33)
    techWatchReset(w);
    techSlotStatsReset(st);
    TechFrameInput g1 = makeTt(110, 0x14, kTtStSpecialN, -1, 1);
    addTtEv(g1, kTtStSpecialN, kTtStJump);
    CHECK(!stepFighterTech(g1, w, st, label));
    TechFrameInput g2 = makeTt(114, 0x14, kTtStAttackAir, 0x62, 1);
    addTtEv(g2, kTtStAttackAir, kTtStSpecialN);
    CHECK(stepFighterTech(g2, w, st, label));
    CHECK_EQ(1u, st.successes);
    CHECK(label != NULL && strcmp(label, "Float Aerial") == 0);
}

// ---- SD Backward-Compatible Migration & Menu Layout ------------------------

TEST(reversals_tech, sd_migration_from_46_byte_cpu_options_and_round_trip)
{
    CHECK_EQ(60u, sizeof(cpu::CpuOptions));
    CHECK_EQ(64u, sizeof(main::CpuSettings));
    CHECK_EQ(436u, sizeof(main::Settings));
    CHECK_EQ(1u, sizeof(main::settings().cpu.reserved));

    // Streamlined CPU & OSD menu placement: Counters & Reversals hosts Auto L-Cancel,
    // SHFFL Timing, and Reversal Slots; OSDs hosts P+ Tech OSD at row 4.
    const ui::Page& cpuPage = ui::pages::kCpuOptions;
    REQUIRE(cpuPage.count == 14);
    CHECK(strcmp(cpuPage.options[2].name, "CPU Load Random %") == 0);
    CHECK(strcmp(cpuPage.options[6].name, "Counters & Reversals") == 0);
    CHECK_EQ(static_cast<int>(ui::pages::P_CPU_COUNTERS), static_cast<int>(cpuPage.options[6].max));

    const ui::Page& countersPage = ui::pages::kCpuCounters;
    REQUIRE(countersPage.count == 9);
    CHECK(strcmp(countersPage.options[5].name, "Reversal Slots") == 0);
    CHECK_EQ(ui::kKindMenu, countersPage.options[5].kind);
    CHECK_EQ(static_cast<int>(ui::pages::P_REVERSALS), static_cast<int>(countersPage.options[5].max));
    CHECK(strcmp(countersPage.options[7].name, "Auto L-Cancel") == 0);
    CHECK(strcmp(countersPage.options[8].name, "SHFFL Timing") == 0);

    const ui::Page& osdPage = ui::pages::kOsdMenu;
    REQUIRE(osdPage.count == 25);
    CHECK(strcmp(osdPage.options[4].name, "P+ Tech OSD") == 0);
    CHECK_EQ(ui::kKindToggle, osdPage.options[4].kind);
    CHECK_EQ(0, osdPage.options[4].def);

    // P_REVERSALS has 15 rows (including live Slot 1 primary readout) and every row has a description in extra.
    const ui::Page& revPage = ui::pages::kReversalSlots;
    REQUIRE(revPage.count == 15);
    for (int r = 0; r < revPage.count; r++)
    {
        const char* desc = extra::optionDesc(ui::pages::P_REVERSALS, static_cast<u8>(r));
        CHECK(desc != NULL && desc[0] != '\0');
    }

    host::card::reset();
    bootStorage();
    // Fresh defaults: multiReversal=0, revSlot2Weight[0..3]=50, techTrackerOsd=0.
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.multiReversal));
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.techTrackerOsd));
    for (int t = 0; t < cpu::kRevTriggerCount; t++)
    {
        CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.revSlot2Action[t]));
        CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.revSlot2Delay[t]));
        CHECK_EQ(50, static_cast<int>(main::settings().cpu.options.revSlot2Weight[t]));
    }

    // Save custom values and verify full round-trip.
    main::settings().cpu.options.dualStickSdi = 1;
    main::settings().cpu.options.autoLCancel = 1;
    main::settings().cpu.options.shfflTiming = cpu::kShfflNormal;
    main::settings().cpu.options.multiReversal = 1;
    main::settings().cpu.options.revSlot2Action[cpu::kRevOnHit] = 8;
    main::settings().cpu.options.revSlot2Delay[cpu::kRevOnHit] = 6;
    main::settings().cpu.options.revSlot2Weight[cpu::kRevOnHit] = 75;
    main::settings().cpu.options.revSlot2Action[cpu::kRevOnShield] = 7;
    main::settings().cpu.options.revSlot2Delay[cpu::kRevOnShield] = 9;
    main::settings().cpu.options.revSlot2Weight[cpu::kRevOnShield] = 100;
    main::settings().cpu.options.revSlot2Action[cpu::kRevOnGetup] = cpu::kGetupAttack;
    main::settings().cpu.options.revSlot2Delay[cpu::kRevOnGetup] = 10;
    main::settings().cpu.options.revSlot2Weight[cpu::kRevOnGetup] = 60;
    main::settings().cpu.options.revSlot2Action[cpu::kRevOnLedge] = cpu::kLedgeRoll;
    main::settings().cpu.options.revSlot2Delay[cpu::kRevOnLedge] = 5;
    main::settings().cpu.options.revSlot2Weight[cpu::kRevOnLedge] = 80;
    main::settings().cpu.options.techTrackerOsd = 1;
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::settingsUpdate();
    }
    storage::detach();

    bootStorage();
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.multiReversal));
    CHECK_EQ(8, static_cast<int>(main::settings().cpu.options.revSlot2Action[cpu::kRevOnHit]));
    CHECK_EQ(6, static_cast<int>(main::settings().cpu.options.revSlot2Delay[cpu::kRevOnHit]));
    CHECK_EQ(75, static_cast<int>(main::settings().cpu.options.revSlot2Weight[cpu::kRevOnHit]));
    CHECK_EQ(7, static_cast<int>(main::settings().cpu.options.revSlot2Action[cpu::kRevOnShield]));
    CHECK_EQ(9, static_cast<int>(main::settings().cpu.options.revSlot2Delay[cpu::kRevOnShield]));
    CHECK_EQ(100, static_cast<int>(main::settings().cpu.options.revSlot2Weight[cpu::kRevOnShield]));
    CHECK_EQ(static_cast<int>(cpu::kGetupAttack),
             static_cast<int>(main::settings().cpu.options.revSlot2Action[cpu::kRevOnGetup]));
    CHECK_EQ(static_cast<int>(cpu::kLedgeRoll),
             static_cast<int>(main::settings().cpu.options.revSlot2Action[cpu::kRevOnLedge]));
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.techTrackerOsd));
    storage::detach();

    // Simulate pre-Wave-3 46-byte CpuOptions SD file (kAreaCpu.used == 49).
    const char* file = host::card::has("settings-b.bin") ? "settings-b.bin" : "settings-a.bin";
    REQUIRE(host::card::has(file));
    u8* raw = host::card::data(file);
    storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(raw + sizeof(storage::FileHeader));
    storage::AreaEntry* table =
        reinterpret_cast<storage::AreaEntry*>(raw + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
    storage::AreaEntry* cpuArea = NULL;
    for (int i = 0; i < head->areaCount; i++)
    {
        if (table[i].id == storage::kAreaCpu)
        {
            cpuArea = &table[i];
            break;
        }
    }
    REQUIRE(cpuArea != NULL);
    CHECK_EQ(63u, static_cast<u32>(cpuArea->used));
    const u32 preRevUsed = offsetof(main::CpuSettings, options) + offsetof(cpu::CpuOptions, multiReversal);
    CHECK_EQ(49u, preRevUsed);
    cpuArea->used = static_cast<u8>(preRevUsed);
    storage::FileHeader* fh = reinterpret_cast<storage::FileHeader*>(raw);
    fh->checksum = storage::checksum(raw + storage::kChecksumFrom, fh->size - storage::kChecksumFrom);

    bootStorage();
    CHECK_LOG("16 areas, 1 with new fields, 0 at defaults, 0 unknown");
    // Pre-existing fields intact:
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.dualStickSdi));
    CHECK_EQ(1, static_cast<int>(main::settings().cpu.options.autoLCancel));
    CHECK_EQ(static_cast<int>(cpu::kShfflNormal), static_cast<int>(main::settings().cpu.options.shfflTiming));
    // New reversal slot and tech OSD fields cleanly migrated to defaults:
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.multiReversal));
    CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.techTrackerOsd));
    for (int t = 0; t < cpu::kRevTriggerCount; t++)
    {
        CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.revSlot2Action[t]));
        CHECK_EQ(0, static_cast<int>(main::settings().cpu.options.revSlot2Delay[t]));
        CHECK_EQ(50, static_cast<int>(main::settings().cpu.options.revSlot2Weight[t]));
    }
    storage::detach();
}
