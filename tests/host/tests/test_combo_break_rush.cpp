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
        CHECK(t.y >= bf.floor.centerY + 11.9f && t.y <= bf.floor.centerY + 46.1f);
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

    // Free Practice never times out and best-slot matches kDrillComboBreakRush (17).
    CHECK_EQ(static_cast<int>(tm::drills::kDrillComboBreakRush), static_cast<int>(kRushBestSlot));
    CHECK(kRushBestSlot < tm::drills::kBestSlots);
    CHECK_EQ(static_cast<int>(kRushTimed30), static_cast<int>(rushNextMode(kRushFree)));
    CHECK_EQ(static_cast<int>(kRushTimed60), static_cast<int>(rushNextMode(kRushTimed30)));
    CHECK_EQ(static_cast<int>(kRushFree), static_cast<int>(rushNextMode(kRushTimed60)));
    rushInit(st, kRushFree, st.bestScore, 0x11112222u);
    CHECK_EQ(0, static_cast<int>(st.timerFrames));
    for (int f = 0; f < 300; f++)
    {
        rushStepFrame(st, fd, 0.0f, 0.0f);
    }
    CHECK_EQ(0, static_cast<int>(st.finished));
    CHECK_EQ(static_cast<int>(kRushActiveSlots), static_cast<int>(rushCountLive(st)));
}

TEST(combo_break_rush, moving_platform_clamp_and_same_frame_milestone_sfx)
{
    using namespace tm::extra;
    StageGeometry sv = makeFdGeom();
    sv.floor.leftX = -70.0f;
    sv.floor.rightX = 70.0f;
    // Moving platform drifted off the right edge of safe floor bounds (pr < pl).
    sv.platforms[0].leftX = 85.0f;
    sv.platforms[0].rightX = 115.0f;
    sv.platforms[0].y = 22.0f;
    sv.platforms[0].valid = true;
    sv.platformCount = 1;

    ComboBreakState st;
    rushInit(st, kRushFree, 0u, 0x55AA1234u);
    for (int i = 0; i < 20; i++)
    {
        CHECK(rushSpawnOne(st, i % kRushActiveSlots, sv, 0.0f, 0.0f));
        const RushTarget& t = st.targets[i % kRushActiveSlots];
        CHECK(t.x >= sv.floor.leftX + 10.0f - 0.01f && t.x <= sv.floor.rightX - 10.0f + 0.01f);
    }

    // Breaking a x4 milestone target and a x5 normal target on the same frame keeps milestone chime (2).
    st.multiplier = 3;
    st.chainFrames = 100;
    st.hitEvent = 0;
    st.targets[0].active = 1;
    st.targets[1].active = 1;
    rushBreakTarget(st, 0); // x4 -> hitEvent = 2
    CHECK_EQ(4, static_cast<int>(st.multiplier));
    CHECK_EQ(2, static_cast<int>(st.hitEvent));
    rushBreakTarget(st, 1); // x5 -> normal break (1) must not downgrade milestone chime (2)
    CHECK_EQ(5, static_cast<int>(st.multiplier));
    CHECK_EQ(2, static_cast<int>(st.hitEvent));
}

TEST(combo_break_rush, break_the_targets_arc_header_and_streaming_lz77_cmpr_tex0)
{
    using namespace tm::extra;

    // 1. Outer ARC Entry 1 parser validates header and locates stream offset 0xA0.
    u8 arcHdr[256];
    memset(arcHdr, 0, sizeof(arcHdr));
    arcHdr[0] = 'A';
    arcHdr[1] = 'R';
    arcHdr[2] = 'C';
    arcHdr[3] = 0;
    arcHdr[7] = 3;      // 3 entries
    arcHdr[0x47] = 24;  // entry 0 size = 24 -> padded 32 -> entry 1 header at 0x80, stream at 0xA0
    arcHdr[0x85] = 0x01;
    arcHdr[0x86] = 0x00; // entry 1 compSize = 0x010000
    u32 streamOff = 0u, compSize = 0u;
    CHECK(rushParseOuterArcEntry1(arcHdr, sizeof(arcHdr), streamOff, compSize));
    CHECK_EQ(0xA0u, streamOff);
    CHECK_EQ(0x10000u, compSize);

    // Rejects corrupt magic / single-entry ARC / u32 wrap size0 headers.
    arcHdr[0] = 'X';
    CHECK(!rushParseOuterArcEntry1(arcHdr, sizeof(arcHdr), streamOff, compSize));
    arcHdr[0] = 'A';
    arcHdr[7] = 1;
    CHECK(!rushParseOuterArcEntry1(arcHdr, sizeof(arcHdr), streamOff, compSize));
    arcHdr[7] = 3;
    const u32 wrapSizes[3] = {0xFFFFFFE0u, 0xFFFFFFA0u, 0xFFFFFFC0u};
    for (int wi = 0; wi < 3; wi++)
    {
        u32 ws = wrapSizes[wi];
        arcHdr[0x44] = static_cast<u8>((ws >> 24) & 0xFFu);
        arcHdr[0x45] = static_cast<u8>((ws >> 16) & 0xFFu);
        arcHdr[0x46] = static_cast<u8>((ws >> 8) & 0xFFu);
        arcHdr[0x47] = static_cast<u8>(ws & 0xFFu);
        CHECK(!rushParseOuterArcEntry1(arcHdr, sizeof(arcHdr), streamOff, compSize));
    }

    // 2. Streaming LZ77 + TEX0 extractor skips a decoy TEX0 (wrong format) and
    //    extracts the 128x128 GX_TF_CMPR mip 1 (8,192 B) from a mipped 256x256
    //    CMPR TEX0 across chunk boundaries and LZ77 back-references!
    static u8 rawStream[48000];
    memset(rawStream, 0x33, sizeof(rawStream));
    // Decoy TEX0 at offset 32 with fmt = 4 (RGB565) - must be skipped.
    rawStream[32] = 'T'; rawStream[33] = 'E'; rawStream[34] = 'X'; rawStream[35] = '0';
    rawStream[32 + 35] = 4;
    // Valid mipped 256x256 CMPR TEX0 at offset 256 (ofsData = 64, blkSz = 0xAA40 = 43584).
    u32 tOff = 256u;
    memset(rawStream + tOff, 0, 64u);
    rawStream[tOff + 0] = 'T'; rawStream[tOff + 1] = 'E'; rawStream[tOff + 2] = 'X'; rawStream[tOff + 3] = '0';
    rawStream[tOff + 6] = 0xAA; rawStream[tOff + 7] = 0x40;   // blkSz = 43584
    rawStream[tOff + 19] = 64;                                 // ofsData = 64
    rawStream[tOff + 28] = 1; rawStream[tOff + 29] = 0;        // width = 256
    rawStream[tOff + 30] = 1; rawStream[tOff + 31] = 0;        // height = 256
    rawStream[tOff + 35] = kRushTexFmtCmpr;                    // fmt = 14 (GX_TF_CMPR)
    // Pattern for mip 0 (skipped) and mip 1 (8,192 B) exercising LZ77 back-ref.
    u32 mip1Off = tOff + 64u + kRushTexMip0Bytes;
    memset(rawStream + tOff + 64u, 0x55, kRushTexMip0Bytes);
    for (u32 i = 0; i < kRushTexMip1Bytes; i++)
    {
        rawStream[mip1Off + i] = static_cast<u8>((i < 128u) ? (i ^ 0x5Au) : ((i & 7u) + 0xA0u));
    }

    // Encode rawStream[0..mip1Off+8192) as valid Wii LZ77 (0x10).
    static u8 lzBuf[56000];
    u32 totalRaw = mip1Off + kRushTexMip1Bytes;
    lzBuf[0] = 0x10;
    lzBuf[1] = static_cast<u8>(totalRaw & 0xFFu);
    lzBuf[2] = static_cast<u8>((totalRaw >> 8) & 0xFFu);
    lzBuf[3] = static_cast<u8>((totalRaw >> 16) & 0xFFu);
    u32 lp = 4u;
    u32 rp = 0u;
    while (rp < totalRaw)
    {
        u32 flagPos = lp++;
        u8 flags = 0u;
        for (int bit = 0; bit < 8 && rp < totalRaw; bit++)
        {
            if (((rp >= tOff + 64u + 16u && rp + 18u <= mip1Off)
                 || (rp >= mip1Off + 136u && rp + 18u <= totalRaw)))
            {
                flags |= static_cast<u8>(0x80u >> bit);
                u32 count = 18u;
                u32 disp = 8u;
                lzBuf[lp++] = static_cast<u8>(((count - 3u) << 4) | ((disp - 1u) >> 8));
                lzBuf[lp++] = static_cast<u8>((disp - 1u) & 0xFFu);
                rp += count;
            }
            else
            {
                lzBuf[lp++] = rawStream[rp++];
            }
        }
        lzBuf[flagPos] = flags;
    }

    static u8 ring[kRushLzRingSize];
    static u8 outTex[kRushTexMip1Bytes];
    memset(outTex, 0, sizeof(outTex));
    RushTexStream ts;
    rushTexStreamInit(ts, ring, outTex);
    // Feed in odd 73-byte chunks to stress split 2-byte LZ77 tokens across calls.
    for (u32 off = 0u; off < lp && !ts.done; off += 73u)
    {
        u32 n = (lp - off) < 73u ? (lp - off) : 73u;
        rushTexStreamFeed(ts, lzBuf + off, n);
    }
    CHECK_EQ(1, static_cast<int>(ts.done));
    CHECK_EQ(0, static_cast<int>(ts.failed));
    CHECK_EQ(static_cast<u32>(kRushTexMip1Bytes), ts.texCopied);
    CHECK(memcmp(outTex, rawStream + mip1Off, kRushTexMip1Bytes) == 0);

    // Non-LZ77 stream fails cleanly.
    u8 badLz[8] = {0x11, 0x00, 0x80, 0x00, 0, 0, 0, 0};
    rushTexStreamInit(ts, ring, outTex);
    rushTexStreamFeed(ts, badLz, sizeof(badLz));
    CHECK_EQ(1, static_cast<int>(ts.failed));
    CHECK_EQ(0, static_cast<int>(ts.done));
}
