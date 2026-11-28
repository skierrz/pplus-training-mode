// Host unit tests for CPU multi-hit shield registration, wall-tech trigger
// spoofing gates, savestate frame timestamp rebasing, and powershield cleanup.
#include <string.h>

#define TM_CPU_BRAIN_PURE_ONLY
#include "host/test.h"
#include "main/cpu/brain.h"

using namespace tm;
using namespace tm::cpu;

namespace {
    CpuOptions makeBaseOptions()
    {
        CpuOptions c;
        memset(&c, 0, sizeof(c));
        c.tech = kTechInPlace;
        c.techTrap = kTrapOff;
        c.techChance[0] = 25;
        c.techChance[1] = 25;
        c.techChance[2] = 25;
        c.techChance[3] = 25;
        return c;
    }
}

TEST(cpu_wave2, shield_hit_refreshed_detects_multihit_shieldstun_without_hitlag_rise)
{
    const int kGD = game::kStatusGuardDamage;
    const int kGuard = game::kStatusGuard;

    // First hit entering Guard_Damage from Guard always registers.
    CHECK(shieldHitRefreshed(kGD, 3, 0, 5, 0, kGuard));
    CHECK(shieldHitRefreshed(kGD, 0, 0, 3, 0, kGuard));

    // Consecutive hit in Guard_Damage with rising hitlag registers.
    CHECK(shieldHitRefreshed(kGD, 4, 0, 4, 2, kGD));
    CHECK(shieldHitRefreshed(kGD, 3, 1, 3, 3, kGD));

    // Consecutive 0-hitlag hit (e.g. Aura Sphere charge / laser) refreshing
    // shieldStun while hitlag stays 0 registers via shieldStun > prevShieldStun.
    CHECK(shieldHitRefreshed(kGD, 0, 0, 3, 2, kGD));
    CHECK(shieldHitRefreshed(kGD, 0, 0, 3, 1, kGD));

    // Consecutive hit with lower or equal hitlag whose shieldStun rises registers.
    CHECK(shieldHitRefreshed(kGD, 2, 4, 6, 4, kGD));
    CHECK(shieldHitRefreshed(kGD, 3, 3, 5, 3, kGD));

    // Normal hitlag and shieldStun countdown in Guard_Damage never false-triggers.
    CHECK(!shieldHitRefreshed(kGD, 2, 3, 5, 5, kGD));
    CHECK(!shieldHitRefreshed(kGD, 1, 2, 5, 5, kGD));
    CHECK(!shieldHitRefreshed(kGD, 0, 1, 4, 5, kGD));
    CHECK(!shieldHitRefreshed(kGD, 0, 0, 3, 4, kGD));
    CHECK(!shieldHitRefreshed(kGD, 0, 0, 1, 2, kGD));
    CHECK(!shieldHitRefreshed(kGD, 0, 0, 3, 3, kGD));

    // Non-Guard_Damage statuses never trigger shieldHitRefreshed.
    CHECK(!shieldHitRefreshed(kGuard, 0, 0, 5, 0, kGuard));
    CHECK(!shieldHitRefreshed(game::kStatusDamage, 5, 0, 5, 0, kGuard));
}

TEST(cpu_wave2, wall_tech_spoof_gates_ledge_onstage_lockout_and_miss_random)
{
    CpuOptions c = makeBaseOptions();

    // Valid offstage recovery with active tech option and no lockout allows spoof.
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));
    c.tech = kTechAway;
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));
    c.tech = kTechTowards;
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));
    c.tech = kTechInPlace;

    // 1) Hanging on ledge (onCliff) blocks spoof.
    CHECK(!canSpoofWallTech(true, false, c, 200, -1000));
    CHECK(!canSpoofWallTech(true, true, c, 200, -1000));

    // 2) Onstage floor below fighter (hasFloor) blocks spoof.
    CHECK(!canSpoofWallTech(false, true, c, 200, -1000));

    // 3) Tech None or Simulate Tech Trap blocks spoof.
    c.tech = kTechNone;
    CHECK(!canSpoofWallTech(false, false, c, 200, -1000));
    c.tech = kTechInPlace;
    c.techTrap = kTrapEarliest;
    CHECK(!canSpoofWallTech(false, false, c, 200, -1000));
    c.techTrap = kTrapLatest;
    CHECK(!canSpoofWallTech(false, false, c, 200, -1000));
    c.techTrap = kTrapOff;

    // 4) Active techFrom window blocks spoof until now >= techFrom.
    CHECK(!canSpoofWallTech(false, false, c, 199, 200));
    CHECK(canSpoofWallTech(false, false, c, 200, 200));
    CHECK(canSpoofWallTech(false, false, c, 210, 200));

    // 5) Tech: Random with 100% miss weight blocks spoof; any non-miss weight
    // or all-zero uniform fallback still allows spoof.
    c.tech = kTechRandom;
    c.techChance[0] = 0;
    c.techChance[1] = 0;
    c.techChance[2] = 0;
    c.techChance[3] = 100;
    CHECK(!canSpoofWallTech(false, false, c, 200, -1000));

    c.techChance[3] = 1;
    CHECK(!canSpoofWallTech(false, false, c, 200, -1000));

    // All-zero falls back to uniform 25% each in cpuWeighted -> not 100% miss.
    c.techChance[3] = 0;
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));

    // Any positive non-miss chance allows spoof.
    c.techChance[0] = 1;
    c.techChance[3] = 99;
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));

    c.techChance[0] = 0;
    c.techChance[2] = 20;
    c.techChance[3] = 80;
    CHECK(canSpoofWallTech(false, false, c, 200, -1000));
}

TEST(cpu_wave2, savestate_frame_rebasing_preserves_sentinels_and_step_alignment)
{
    // nextTrackerStep accounts for render owed and savestate pending hand-over.
    const s32 saveNext = nextTrackerStep(100, true, false);  // normal save: next step is 101
    const s32 loadNext = nextTrackerStep(160, true, true);   // normal load: pending load + owed -> next step 162
    const s32 delta = loadNext - saveNext;
    CHECK_EQ(101, saveNext);
    CHECK_EQ(162, loadNext);
    CHECK_EQ(61, delta);

    // Frame-advance load (no owed, no pending) aligns directly to current frame.
    CHECK_EQ(161, nextTrackerStep(161, false, false));

    // Valid Brain timestamps rebase by exact delta while -1000 sentinels stay intact.
    s32 lastPress = 96;
    s32 techFrom = 117;
    s32 sentinelPress = -1000;
    s32 sentinelTechFrom = -1000;
    rebaseFrame(lastPress, delta);
    rebaseFrame(techFrom, delta);
    rebaseFrame(sentinelPress, delta);
    rebaseFrame(sentinelTechFrom, delta);
    CHECK_EQ(157, lastPress);       // 162 - 157 == 5 == 101 - 96
    CHECK_EQ(178, techFrom);        // 178 - 162 == 16 == 117 - 101
    CHECK_EQ(-1000, sentinelPress);
    CHECK_EQ(-1000, sentinelTechFrom);

    // Zero timestamp (frame 0) is valid and rebases cleanly.
    s32 frameZero = 0;
    rebaseFrame(frameZero, delta);
    CHECK_EQ(61, frameZero);

    // Inactive kCueNone (zero-initialized 0, 0) must NEVER be corrupted.
    s32 qFrame = 0;
    s32 qStart = 0;
    rebaseCueFrames(kCueNone, qFrame, qStart, delta);
    CHECK_EQ(0, qFrame);
    CHECK_EQ(0, qStart);

    // Waiting cue with explicit target frame: rebases frame, keeps start == -1.
    qFrame = 125;
    qStart = -1;
    rebaseCueFrames(kCueWaiting, qFrame, qStart, delta);
    CHECK_EQ(186, qFrame); // 186 - 162 == 24 == 125 - 101
    CHECK_EQ(-1, qStart);

    // Waiting cue with kCueAsap (-1): preserves both sentinels.
    qFrame = kCueAsap;
    qStart = -1;
    rebaseCueFrames(kCueWaiting, qFrame, qStart, delta);
    CHECK_EQ(static_cast<s32>(kCueAsap), qFrame);
    CHECK_EQ(-1, qStart);

    // Running / Done / Missed cues rebase start (and non-ASAP frame).
    qFrame = kCueAsap;
    qStart = 90;
    rebaseCueFrames(kCueRunning, qFrame, qStart, delta);
    CHECK_EQ(static_cast<s32>(kCueAsap), qFrame);
    CHECK_EQ(151, qStart);

    qFrame = 85;
    qStart = 88;
    rebaseCueFrames(kCueDone, qFrame, qStart, delta);
    CHECK_EQ(146, qFrame);
    CHECK_EQ(149, qStart);
}

TEST(cpu_wave2, reset_action_state_clears_powershield_and_shield_kind)
{
    int state = 0; // kIdle
    bool shieldKind = true;
    int powershield = 3;

    resetActionState(state, shieldKind, powershield, 7); // kGrabbed
    CHECK_EQ(7, state);
    CHECK(!shieldKind);
    CHECK_EQ(0, powershield);

    shieldKind = true;
    powershield = 5;
    resetActionState(state, shieldKind, powershield, 1); // kHit
    CHECK_EQ(1, state);
    CHECK(!shieldKind);
    CHECK_EQ(0, powershield);

    shieldKind = true;
    powershield = 2;
    resetActionState(state, shieldKind, powershield, 4); // kCounter (handedCounter)
    CHECK_EQ(4, state);
    CHECK(!shieldKind);
    CHECK_EQ(0, powershield);
}
