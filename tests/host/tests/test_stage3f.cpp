// Host unit tests for Stage 3F pure P+ backend upgrades:
//   - src/main/cpu/ledge_logic.h (On-Ledge reversal / getup logic)
//   - src/main/cpu/clatter_logic.h (Non-grab clatter mash-rate engine)
//   - src/extra/adaptive_cpu_core.h (MovementTracker, bug-fixed PatternManager,
//     and RPS adaptive counter biasing)
#include <math.h>
#include <string.h>

#include "extra/adaptive_cpu_core.h"
#include "host/test.h"
#include "main/cpu/clatter_logic.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/ledge_logic.h"

using namespace tm;
using namespace tm::cpu;
using namespace tm::extra;

// ---- 1. On-Ledge Reversal / CPU Ledge Option (ledge_logic.h) ----------------

TEST(stage3f_ledge, default_passes_through_and_non_cliff_statuses_ignore)
{
    LedgeState st;
    ledgeStateReset(st);
    LedgeInput out;

    // kLedgeDefault (0) must never claim the frame, even on Cliff_Wait (0x75).
    CHECK(!stepLedgeOption(kLedgeDefault, 0, kLedgeStatusWait, 1.0f, 123u, st, out));
    CHECK(!out.active);
    CHECK_EQ(0u, out.buttons);
    CHECK_EQ(0.0f, out.x);
    CHECK_EQ(0.0f, out.y);

    // Non-cliff statuses (Wait 0x00, Fall 0x0E, Cliff_Jump1 0x79) must not fire.
    CHECK(!stepLedgeOption(kLedgeNeutral, 0, 0x00, 1.0f, 1u, st, out));
    CHECK(!stepLedgeOption(kLedgeNeutral, 0, 0x0E, 1.0f, 1u, st, out));
    CHECK(!stepLedgeOption(kLedgeDrop, 0, kLedgeStatusJump1, -1.0f, 1u, st, out));
    CHECK(!st.active);
}

TEST(stage3f_ledge, all_concrete_options_and_facing_signs)
{
    // Left ledge (lr = +1.0f facing right toward stage) and right ledge (lr = -1.0f).
    const float facings[2] = {1.0f, -1.0f};
    for (int f = 0; f < 2; f++)
    {
        float lr = facings[f];
        LedgeState st;
        LedgeInput out;

        // Neutral (Cliff_Climb): stick toward stage (+lr, 0).
        ledgeStateReset(st);
        CHECK(stepLedgeOption(kLedgeNeutral, 0, kLedgeStatusWait, lr, 0u, st, out));
        CHECK(out.active);
        CHECK_EQ(0u, out.buttons);
        CHECK_EQ(lr, out.x);
        CHECK_EQ(0.0f, out.y);

        // Roll (Cliff_Escape): guard button.
        ledgeStateReset(st);
        CHECK(stepLedgeOption(kLedgeRoll, 0, kLedgeStatusWait, lr, 0u, st, out));
        CHECK(out.active);
        CHECK_EQ(static_cast<u32>(button::kGuard), out.buttons);
        CHECK_EQ(0.0f, out.x);
        CHECK_EQ(0.0f, out.y);

        // Attack (Cliff_Attack): attack button.
        ledgeStateReset(st);
        CHECK(stepLedgeOption(kLedgeAttack, 0, kLedgeStatusWait, lr, 0u, st, out));
        CHECK(out.active);
        CHECK_EQ(static_cast<u32>(button::kAttack), out.buttons);
        CHECK_EQ(0.0f, out.x);
        CHECK_EQ(0.0f, out.y);

        // Jump (Cliff_Jump1..3): jump button.
        ledgeStateReset(st);
        CHECK(stepLedgeOption(kLedgeJump, 0, kLedgeStatusWait, lr, 0u, st, out));
        CHECK(out.active);
        CHECK_EQ(static_cast<u32>(button::kJump), out.buttons);
        CHECK_EQ(0.0f, out.x);
        CHECK_EQ(0.0f, out.y);

        // Drop (Fall without fastfall): stick away (-lr, 0).
        ledgeStateReset(st);
        CHECK(stepLedgeOption(kLedgeDrop, 0, kLedgeStatusWait, lr, 0u, st, out));
        CHECK(out.active);
        CHECK_EQ(0u, out.buttons);
        CHECK_EQ(-lr, out.x);
        CHECK_EQ(0.0f, out.y);
    }
}

TEST(stage3f_ledge, cliff_catch_startup_and_counter_delay)
{
    LedgeState st;
    ledgeStateReset(st);
    LedgeInput out;

    // During Cliff_Catch (0x74) startup frames, output stays neutral and delay
    // does not tick down until Cliff_Wait (0x75) begins.
    for (int i = 0; i < 4; i++)
    {
        CHECK(stepLedgeOption(kLedgeAttack, 3, kLedgeStatusCatch, -1.0f, 7u, st, out));
        CHECK(out.active);
        CHECK_EQ(0u, out.buttons);
        CHECK_EQ(0.0f, out.x);
        CHECK(!st.active);
    }

    // First 3 frames of Cliff_Wait (counterDelay = 3) remain neutral.
    for (int i = 0; i < 3; i++)
    {
        CHECK(stepLedgeOption(kLedgeAttack, 3, kLedgeStatusWait, -1.0f, 7u, st, out));
        CHECK(out.active);
        CHECK_EQ(0u, out.buttons);
        CHECK(st.active);
    }

    // Frame 4 of Cliff_Wait fires Attack immediately.
    CHECK(stepLedgeOption(kLedgeAttack, 3, kLedgeStatusWait, -1.0f, 7u, st, out));
    CHECK_EQ(static_cast<u32>(button::kAttack), out.buttons);

    // Next frame pulses neutral so fresh edge retries work if still hanging.
    CHECK(stepLedgeOption(kLedgeAttack, 3, kLedgeStatusWait, -1.0f, 7u, st, out));
    CHECK_EQ(0u, out.buttons);
    CHECK(stepLedgeOption(kLedgeAttack, 3, kLedgeStatusWait, -1.0f, 7u, st, out));
    CHECK_EQ(static_cast<u32>(button::kAttack), out.buttons);
}

TEST(stage3f_ledge, random_covers_all_five_options_and_locks_per_grab)
{
    bool seen[kLedgeOptionCount];
    memset(seen, 0, sizeof(seen));
    for (u32 seed = 0; seed < 5; seed++)
    {
        LedgeState st;
        ledgeStateReset(st);
        LedgeInput out;
        CHECK(stepLedgeOption(kLedgeRandom, 0, kLedgeStatusWait, 1.0f, seed, st, out));
        CHECK(st.chosen >= kLedgeNeutral && st.chosen <= kLedgeDrop);
        seen[st.chosen] = true;
        u8 locked = st.chosen;
        // Different RNG value on subsequent frames of the same grab keeps the choice locked.
        CHECK(stepLedgeOption(kLedgeRandom, 0, kLedgeStatusWait, 1.0f, seed + 999u, st, out));
        CHECK_EQ(static_cast<int>(locked), static_cast<int>(st.chosen));
    }
    for (int opt = kLedgeNeutral; opt <= kLedgeDrop; opt++)
    {
        CHECK(seen[opt]);
    }
}

// ---- 2. Clatter / Mash-Rate Engine (clatter_logic.h) ------------------------

TEST(stage3f_clatter, classifies_all_four_clatter_families_and_excludes_exits)
{
    // Active clatter statuses (verified against BrawlHeaders ft/fighter.h).
    CHECK_EQ(static_cast<int>(kClatterBury), clatterKind(0xC7));      // Bury
    CHECK_EQ(static_cast<int>(kClatterBury), clatterKind(0xC8));      // Bury_Wait
    CHECK_EQ(static_cast<int>(kClatterIce), clatterKind(0xCA));       // Ice
    CHECK_EQ(static_cast<int>(kClatterFuraFura), clatterKind(0x59));  // FuraFura_Stand
    CHECK_EQ(static_cast<int>(kClatterFuraFura), clatterKind(0x5A));  // FuraFura
    CHECK_EQ(static_cast<int>(kClatterSleep), clatterKind(0x5C));     // Damage_Song_Start
    CHECK_EQ(static_cast<int>(kClatterSleep), clatterKind(0x5D));     // Damage_Song

    // Exit animations must NOT keep mashing.
    CHECK(!isClatterStatus(0xC9)); // Bury_Jump
    CHECK(!isClatterStatus(0xCB)); // Ice_Jump
    CHECK(!isClatterStatus(0x5B)); // FuraFura_End
    CHECK(!isClatterStatus(0x5E)); // Damage_Song_End
    CHECK(isClatterExitStatus(0xC9));
    CHECK(isClatterExitStatus(0xCB));
    CHECK(isClatterExitStatus(0x5B));
    CHECK(isClatterExitStatus(0x5E));

    // Ordinary statuses must not classify as clatter.
    CHECK(!isClatterStatus(0x00)); // Wait
    CHECK(!isClatterStatus(0x43)); // Damage
    CHECK(!isClatterStatus(0x3D)); // Capture_Pulled (handled by Grab Escape)
    CHECK(!isClatterStatus(0x75)); // Cliff_Wait
}

TEST(stage3f_clatter, mash_tiers_alternation_and_none_noop)
{
    CHECK_EQ(0, clatterMashChance(kMashNone));
    CHECK_EQ(40, clatterMashChance(kMashMedium));
    CHECK_EQ(65, clatterMashChance(kMashHigh));
    CHECK_EQ(100, clatterMashChance(kMashPerfect));

    bool flip = false;
    u32 btn = 0;
    float sx = 0.0f, sy = 0.0f;

    // kMashNone is always a no-op.
    CHECK(!stepClatterMash(kMashNone, 0xC8, 0, 0, 0, flip, btn, sx, sy));
    CHECK_EQ(0u, btn);
    CHECK_EQ(0.0f, sx);

    // Hitlag suppresses mashing.
    CHECK(!stepClatterMash(kMashPerfect, 0xC8, 4, 0, 0, flip, btn, sx, sy));

    // Threshold gating for Medium (40) and High (65).
    CHECK(stepClatterMash(kMashMedium, 0xC8, 0, 0, 39, flip, btn, sx, sy));
    CHECK(!stepClatterMash(kMashMedium, 0xC8, 0, 0, 40, flip, btn, sx, sy));
    CHECK(stepClatterMash(kMashHigh, 0x5A, 0, 0, 64, flip, btn, sx, sy));
    CHECK(!stepClatterMash(kMashHigh, 0x5A, 0, 0, 65, flip, btn, sx, sy));

    // Max (100) alternates A/B and +1.0/-1.0 stick X every single frame.
    flip = false;
    u32 prevBtn = 0;
    float prevX = 0.0f;
    for (int f = 0; f < 6; f++)
    {
        CHECK(stepClatterMash(kMashPerfect, 0x5D, 0, prevBtn, 99, flip, btn, sx, sy));
        CHECK(btn == button::kAttack || btn == button::kSpecial);
        if (f > 0)
        {
            CHECK(btn != prevBtn);
            CHECK(sx == -prevX);
        }
        CHECK(fabsf(sx) == 1.0f);
        CHECK_EQ(0.0f, sy);
        prevBtn = btn;
        prevX = sx;
    }
}

// ---- 3. Clean-Room fudgepop01 Adaptive CPU Backend (adaptive_cpu_core.h) ----

TEST(stage3f_adaptive, movement_tracker_ring_wrap_and_markov_probabilities)
{
    MovementTracker mt;
    mt.reset();
    CHECK_EQ(0u, mt.sampleCount());
    CHECK_EQ(static_cast<int>(kMovNone), static_cast<int>(mt.currentMov()));
    CHECK_EQ(0.0f, mt.approxChance(kMovAttack));
    CHECK_EQ(0.0f, mt.approxCommit());

    // Push > 128 alternating Idle -> Attack sequences to exercise circular wrap.
    for (int i = 0; i < 80; i++)
    {
        mt.trackStatus(0x00); // Wait -> kMovIdle
        mt.incrementTimer();
        mt.updateData(0, 128, 20, 100, 20, kMovIdle);

        mt.trackStatus(0x24); // Attack -> kMovAttack
        mt.incrementTimer();
        mt.updateData(0, 128, 20, 100, 20, kMovIdle);
    }
    // 160 pushes wraps 128-entry ring cleanly: idx == 160 % 128 == 32.
    CHECK_EQ(32u, static_cast<u32>(mt.currentIndex()));
    CHECK_EQ(160u, mt.sampleCount());
    CHECK_EQ(static_cast<int>(kMovAttack), static_cast<int>(mt.currentMov()));

    float atkProb = mt.approxChance(kMovAttack);
    float shdProb = mt.approxChance(kMovShield);
    float commit = mt.approxCommit();
    CHECK(atkProb > 0.25f);
    CHECK_EQ(0.0f, shdProb);
    CHECK(commit >= atkProb);
    CHECK(commit <= 1.0f);

    // Status-to-RPS mapping checks.
    CHECK_EQ(static_cast<int>(kRpsAttack), static_cast<int>(statusToRps(0x24)));  // Jab
    CHECK_EQ(static_cast<int>(kRpsAttack), static_cast<int>(statusToRps(0x112))); // SpecialN
    CHECK_EQ(static_cast<int>(kRpsGrab), static_cast<int>(statusToRps(0x34)));    // Catch
    CHECK_EQ(static_cast<int>(kRpsDefend), static_cast<int>(statusToRps(0x1B)));  // Guard
    CHECK_EQ(static_cast<int>(kRpsDefend), static_cast<int>(statusToRps(0x1E)));  // Escape
    CHECK_EQ(static_cast<int>(kRpsNull), static_cast<int>(statusToRps(0x00)));    // Wait
}

TEST(stage3f_adaptive, pattern_manager_fixes_slot14_wrap_and_latest_entry_start)
{
    PatternManager pm;
    pm.clearAll();

    // Push 14 Defends (slots 0..13), then 1 Attack into slot 14!
    // Upstream bug 1 wrapped at index 14 (skipping slot 14); our fix MUST
    // write slot 14 and wrap nextIndex to 0 on the 15th push.
    for (int i = 0; i < 14; i++)
    {
        pm.pushNew(0, kRpsDefend);
    }
    CHECK_EQ(14u, static_cast<u32>(pm.nextIndex(0)));
    pm.pushNew(0, kRpsAttack); // MUST land in slot 14!
    CHECK_EQ(0u, static_cast<u32>(pm.nextIndex(0)));
    CHECK_EQ(static_cast<int>(kRpsAttack), static_cast<int>(pm.entryAt(0, 14)));
    CHECK_EQ(15u, pm.count(0));

    // Upstream bug 2 started runCalc at nextIdx (slot 0, oldest/unwritten)
    // instead of (nextIdx + 14) % 15 (slot 14, most recent!).
    // With lookAmount = 1, runCalc MUST read ONLY slot 14 (the Attack)!
    int atk = 0, grab = 0, def = 0;
    pm.runCalc(0, 1, atk, grab, def);
    CHECK_EQ(1, atk);
    CHECK_EQ(0, grab);
    CHECK_EQ(0, def);
    CHECK_EQ(static_cast<int>(kRpsAttack), static_cast<int>(pm.dominantRps(0, 1)));
    CHECK(fabsf(pm.confidence(0, 1) - 1.0f) < 0.001f);

    // Full 15-slot confidence: 15 identical Grabs -> confidence == 1.0.
    pm.clear(1);
    for (int i = 0; i < 15; i++)
    {
        pm.pushNew(1, kRpsGrab);
    }
    CHECK_EQ(static_cast<int>(kRpsGrab), static_cast<int>(pm.dominantRps(1)));
    CHECK(fabsf(pm.confidence(1) - 1.0f) < 0.001f);

    // 6 identical Attacks -> (15+14+13+12+11+10)/120 = 75/120 = 0.625 >= 0.55.
    pm.clear(2);
    for (int i = 0; i < 6; i++)
    {
        pm.pushNew(2, kRpsAttack);
    }
    CHECK_EQ(static_cast<int>(kRpsAttack), static_cast<int>(pm.dominantRps(2)));
    CHECK(fabsf(pm.confidence(2) - 0.625f) < 0.001f);
}

TEST(stage3f_adaptive, rps_counter_bias_mapping_and_mode_gating)
{
    // Off and Track modes never bias CPU actions.
    CHECK_EQ(static_cast<int>(kBiasNone),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveOff, kRpsAttack, 1.0f, false, 0)));
    CHECK_EQ(static_cast<int>(kBiasNone),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveTrack, kRpsAttack, 1.0f, false, 0)));

    // Below confidence threshold (0.55) never biases.
    CHECK_EQ(static_cast<int>(kBiasNone),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsAttack, 0.54f, false, 0)));

    // Attack habit -> defensive response (Shield / Spotdodge / Grab).
    u8 atkBias0 = chooseAdaptiveBias(kAdaptiveActive, kRpsAttack, 0.70f, false, 0);
    u8 atkBias1 = chooseAdaptiveBias(kAdaptiveActive, kRpsAttack, 0.70f, false, 1);
    u8 atkBiasOos = chooseAdaptiveBias(kAdaptiveActive, kRpsAttack, 0.70f, true, 0);
    CHECK_EQ(static_cast<int>(kBiasShield), static_cast<int>(atkBias0));
    CHECK_EQ(static_cast<int>(kBiasSpotdodge), static_cast<int>(atkBias1));
    CHECK_EQ(static_cast<int>(kBiasGrab), static_cast<int>(atkBiasOos));

    // Grab habit -> evasive / interrupt response (Spotdodge / Jab / Jump).
    CHECK_EQ(static_cast<int>(kBiasSpotdodge),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsGrab, 0.75f, false, 0)));
    CHECK_EQ(static_cast<int>(kBiasJab),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsGrab, 0.75f, false, 1)));
    CHECK_EQ(static_cast<int>(kBiasJump),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsGrab, 0.75f, false, 2)));

    // Defend (Shield) habit -> Grab response.
    CHECK_EQ(static_cast<int>(kBiasGrab),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsDefend, 0.80f, false, 0)));
    CHECK_EQ(static_cast<int>(kBiasGrab),
             static_cast<int>(chooseAdaptiveBias(kAdaptiveActive, kRpsDefend, 0.80f, true, 0)));

    // Verify action index mapping matches actions.cpp kGround (0) and kShieldList (2).
    CHECK_EQ(2, adaptiveBiasToActionIndex(kBiasShield, 0));
    CHECK_EQ(1, adaptiveBiasToActionIndex(kBiasSpotdodge, 0));
    CHECK_EQ(3, adaptiveBiasToActionIndex(kBiasGrab, 0));
    CHECK_EQ(13, adaptiveBiasToActionIndex(kBiasJab, 0));
    CHECK_EQ(18, adaptiveBiasToActionIndex(kBiasJump, 0));
    CHECK_EQ(1, adaptiveBiasToActionIndex(kBiasGrab, 2));
    CHECK_EQ(4, adaptiveBiasToActionIndex(kBiasSpotdodge, 2));
    CHECK_EQ(2, adaptiveBiasToActionIndex(kBiasJump, 2));
}

TEST(stage3f_adaptive, lazy_weights_and_stationary_timer_do_not_inflate_samples)
{
    MovementTracker mt;
    mt.reset();
    CHECK(!mt.weightsDirty());

    mt.trackStatus(0x00); // Wait -> kMovIdle
    mt.updateData(0, 128, 20, 0, 0, kMovIdle);
    mt.trackStatus(0x24); // Attack -> kMovAttack
    mt.updateData(0, 128, 20, 0, 0, kMovIdle);
    mt.trackStatus(0x00);
    mt.updateData(0, 128, 20, 0, 0, kMovIdle);
    mt.trackStatus(0x24);
    mt.updateData(0, 128, 20, 0, 0, kMovIdle);

    // updateData marks weights dirty lazily without eager calcWeight computation.
    CHECK(mt.weightsDirty());
    CHECK_EQ(4u, mt.sampleCount());

    // Standing still for 100 frames increments duration without rolling over or
    // inflating sampleCount().
    for (int f = 0; f < 100; f++)
    {
        CHECK(!mt.incrementTimer());
    }
    CHECK_EQ(4u, mt.sampleCount());
    CHECK_EQ(100u, static_cast<u32>(mt.currentDuration()));
    CHECK(mt.weightsDirty());

    // First approxChance query computes weights lazily and clears weightsDirty.
    float atkProb = mt.approxChance(kMovAttack);
    CHECK(!mt.weightsDirty());
    CHECK(atkProb > 0.0f);

    // Continuing to 254 frames triggers rollover exactly once.
    bool sawRollover = false;
    for (int f = 100; f < 254; f++)
    {
        if (mt.incrementTimer())
        {
            sawRollover = true;
            CHECK_EQ(253, f);
        }
    }
    CHECK(sawRollover);
    CHECK(mt.weightsDirty());
}

TEST(stage3f_adaptive, peek_commit_semantics_and_guard_neutral_gating)
{
    u32 mix = 0;
    u16 cooldown = 0;
    u32 biasAction = 0;
    u32 biasTriggers = 0;

    // Peek in counter does not mutate mix, cooldown, biasAction, or biasTriggers.
    int peekIdx = selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, true, false, 15.0f, 65.0f, false, mix,
                                       cooldown, 28, biasAction, biasTriggers);
    CHECK_EQ(2, peekIdx); // Shield (mix == 0)
    CHECK_EQ(0u, mix);
    CHECK_EQ(0u, static_cast<u32>(cooldown));
    CHECK_EQ(0u, biasAction);
    CHECK_EQ(0u, biasTriggers);

    // Repeated peeks return the same candidate without skipping mixup steps.
    int peekIdx2 = selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, true, false, 15.0f, 65.0f, false, mix,
                                        cooldown, 28, biasAction, biasTriggers);
    CHECK_EQ(2, peekIdx2);
    CHECK_EQ(0u, mix);
    CHECK_EQ(0u, biasTriggers);

    // Commit advances mixCounter and increments biasTriggers exactly once.
    int commitIdx = selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, true, false, 15.0f, 65.0f, true, mix,
                                         cooldown, 28, biasAction, biasTriggers);
    CHECK_EQ(2, commitIdx);
    CHECK_EQ(1u, mix);
    CHECK_EQ(static_cast<u32>(kBiasShield), biasAction);
    CHECK_EQ(1u, biasTriggers);

    // Guarding CPU in neutral idle (inCounter=false, inGuard=true) must NOT drop
    // shield to run a grounded neutral script.
    int guardIdle = selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, false, true, 15.0f, 65.0f, true, mix,
                                         cooldown, 28, biasAction, biasTriggers);
    CHECK_EQ(-1, guardIdle);
    CHECK_EQ(1u, mix);
    CHECK_EQ(1u, biasTriggers);

    // Unguarded neutral idle commits once and arms neutralCooldown.
    int neutralIdx = selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, false, false, 15.0f, 65.0f, true, mix,
                                          cooldown, 28, biasAction, biasTriggers);
    CHECK_EQ(1, neutralIdx); // Spotdodge (mix == 1)
    CHECK_EQ(2u, mix);
    CHECK_EQ(28u, static_cast<u32>(cooldown));
    CHECK_EQ(static_cast<u32>(kBiasSpotdodge), biasAction);
    CHECK_EQ(2u, biasTriggers);

    // Active cooldown suppresses further neutral triggers, while counters still work.
    CHECK_EQ(-1, selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 0, false, false, 15.0f, 65.0f, false, mix,
                                      cooldown, 28, biasAction, biasTriggers));
    CHECK_EQ(1, selectAdaptiveAction(kAdaptiveActive, kRpsAttack, 0.80f, 2, true, true, 15.0f, 65.0f, false, mix,
                                     cooldown, 28, biasAction, biasTriggers)); // Grab OoS
}
