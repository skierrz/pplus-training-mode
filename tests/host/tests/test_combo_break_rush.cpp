#include <string.h>

#include "extra/combo_break_rush_core.h"
#include "host/test.h"
#include "shared/drill_session.h"

namespace {
    tm::extra::StageGeometry makeFdGeom()
    {
        tm::extra::StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.blast.left = -246.0f;
        g.blast.right = 246.0f;
        g.blast.top = 188.0f;
        g.blast.bottom = -140.0f;
        g.blast.valid = true;
        g.floor.leftX = -85.0f;
        g.floor.rightX = 85.0f;
        g.floor.leftY = 0.0f;
        g.floor.rightY = 0.0f;
        g.floor.centerY = 0.0f;
        g.floor.valid = true;
        g.platformCount = 0;
        return g;
    }

    tm::extra::StageGeometry makeBfGeom()
    {
        tm::extra::StageGeometry g = makeFdGeom();
        g.floor.leftX = -68.4f;
        g.floor.rightX = 68.4f;
        g.platforms[0].leftX = -57.6f;
        g.platforms[0].rightX = -20.0f;
        g.platforms[0].y = 27.2f;
        g.platforms[0].valid = true;
        g.platforms[1].leftX = 20.0f;
        g.platforms[1].rightX = 57.6f;
        g.platforms[1].y = 27.2f;
        g.platforms[1].valid = true;
        g.platforms[2].leftX = -18.8f;
        g.platforms[2].rightX = 18.8f;
        g.platforms[2].y = 54.4f;
        g.platforms[2].valid = true;
        g.platformCount = 3;
        return g;
    }
}

TEST(combo_break_rush, spawns_inside_fd_and_bf_stage_airspace)
{
    using namespace tm::extra;
    StageGeometry fd = makeFdGeom();
    StageGeometry bf = makeBfGeom();

    ComboBreakState st;
    rushInit(st, kRushTimed30, 0u, 0x12345678u);
    CHECK_EQ(static_cast<int>(kRushTimed30), static_cast<int>(st.mode));
    CHECK_EQ(static_cast<int>(kRushTimed30Frames), static_cast<int>(st.timerFrames));
    CHECK_EQ(1, static_cast<int>(st.multiplier));

    for (int i = 0; i < kRushActiveSlots; i++)
    {
        CHECK(rushSpawnOne(st, i, fd, 0.0f, 0.0f));
        const RushTarget& t = st.targets[i];
        CHECK_EQ(1, static_cast<int>(t.active));
        CHECK(t.x >= fd.floor.leftX + 9.0f && t.x <= fd.floor.rightX - 9.0f);
        CHECK(t.y >= fd.floor.centerY + 7.0f && t.y <= fd.floor.centerY + 60.0f);
        CHECK(t.radius >= 3.5f && t.radius <= 5.5f);
    }
    CHECK_EQ(static_cast<int>(kRushActiveSlots), static_cast<int>(rushCountLive(st)));

    // Battlefield spawns include platform arcs (tier 1 / elevated Y above platforms).
    rushInit(st, kRushFree, 0u, 0x87654321u);
    int sawPlatformArc = 0;
    for (int trial = 0; trial < 30; trial++)
    {
        int slot = trial % kRushActiveSlots;
        CHECK(rushSpawnOne(st, slot, bf, -20.0f, 0.0f));
        const RushTarget& t = st.targets[slot];
        CHECK(t.x >= bf.floor.leftX && t.x <= bf.floor.rightX);
        CHECK(t.y >= bf.floor.centerY + 7.0f && t.y <= bf.blast.top - 30.0f);
        if (t.y > 32.0f)
        {
            sawPlatformArc++;
        }
    }
    CHECK(sawPlatformArc > 0);
}

TEST(combo_break_rush, hitbox_capsule_intersection_detects_direct_and_swept_hits)
{
    using namespace tm::extra;
    RushTarget t;
    memset(&t, 0, sizeof(t));
    t.x = 20.0f;
    t.y = 14.0f;
    t.radius = 4.8f;
    t.active = 1;

    // Direct overlap.
    float p1[3] = {22.0f, 15.0f, 0.0f};
    float p2[3] = {22.0f, 15.0f, 0.0f};
    CHECK(rushHitboxHitsTarget(p1, p2, 3.5f, t));

    // Swept capsule segment crossing through target even though endpoints are outside.
    float s1[3] = {10.0f, 14.0f, 0.0f};
    float s2[3] = {30.0f, 14.0f, 0.0f};
    CHECK(rushHitboxHitsTarget(s1, s2, 2.0f, t));

    // Clear miss.
    float m1[3] = {-10.0f, 14.0f, 0.0f};
    float m2[3] = {-4.0f, 14.0f, 0.0f};
    CHECK(!rushHitboxHitsTarget(m1, m2, 3.0f, t));

    // Inactive / breaking target never re-triggers.
    t.active = 2;
    CHECK(!rushHitboxHitsTarget(p1, p2, 6.0f, t));
}

TEST(combo_break_rush, combo_multiplier_chain_timer_and_modes)
{
    using namespace tm::extra;
    StageGeometry fd = makeFdGeom();
    ComboBreakState st;
    rushInit(st, kRushTimed30, 500u, 0xCAFEBABEu);
    CHECK_EQ(500u, st.bestScore);

    // 10 consecutive breaks inside chain window climb multiplier x1 -> x8 and stay capped at x8.
    for (int i = 0; i < 10; i++)
    {
        st.targets[0].active = 1;
        st.targets[0].tier = 0; // 100 base pts
        rushBreakTarget(st, 0);
        int expectedMult = (i + 1) < kRushMaxMult ? (i + 1) : kRushMaxMult;
        CHECK_EQ(expectedMult, static_cast<int>(st.multiplier));
        CHECK_EQ(static_cast<int>(kRushChainMaxFrames), static_cast<int>(st.chainFrames));
    }
    // Score = 100*(1+2+3+4+5+6+7+8+8+8) = 5200.
    CHECK_EQ(5200u, st.score);
    CHECK_EQ(5200u, st.bestScore);
    CHECK_EQ(10, static_cast<int>(st.breaks));
    CHECK_EQ(10, static_cast<int>(st.streak));

    // Stepping past chain decay window resets multiplier to x1 and streak to 0.
    for (int f = 0; f < kRushChainMaxFrames; f++)
    {
        rushStepFrame(st, fd, 0.0f, 0.0f);
    }
    CHECK_EQ(0, static_cast<int>(st.chainFrames));
    CHECK_EQ(1, static_cast<int>(st.multiplier));
    CHECK_EQ(0, static_cast<int>(st.streak));
    CHECK_EQ(10, static_cast<int>(st.bestStreak));

    // Timed 30s finishes cleanly at 0 frames left.
    st.timerFrames = 2;
    rushStepFrame(st, fd, 0.0f, 0.0f);
    CHECK_EQ(0, static_cast<int>(st.finished));
    rushStepFrame(st, fd, 0.0f, 0.0f);
    CHECK_EQ(1, static_cast<int>(st.finished));
    CHECK_EQ(3, static_cast<int>(st.hitEvent));

    // Free Practice never times out and supports best-slot index inside drills::Session.
    CHECK(kRushBestSlot >= tm::drills::kDrillIdCount && kRushBestSlot < tm::drills::kBestSlots);
    rushInit(st, kRushFree, st.bestScore, 0x11112222u);
    CHECK_EQ(0, static_cast<int>(st.timerFrames));
    for (int f = 0; f < 300; f++)
    {
        rushStepFrame(st, fd, 0.0f, 0.0f);
    }
    CHECK_EQ(0, static_cast<int>(st.finished));
    CHECK_EQ(static_cast<int>(kRushActiveSlots), static_cast<int>(rushCountLive(st)));
}
