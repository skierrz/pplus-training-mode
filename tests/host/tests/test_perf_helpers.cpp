// Host unit tests for performance helpers: fixed-point decimal formatting
// (src/main/gfx/format_core.h), glyph texture coordinate computation, and
// savestate unrolled sampled-word checksum equivalence.
#include <cstdio>
#include <limits.h>
#include <string.h>

#include "host/test.h"
#include "main/gfx/format_core.h"

using namespace tm;

namespace {
    u32 hashRecordScalar(u32 h, u32 addr, u32 n, const u32* data)
    {
        h = ((h << 1) | (h >> 31)) + addr;
        h = ((h << 1) | (h >> 31)) + n;
        u32 words = n >> 2;
        for (u32 i = 0; i < words; i += 64)
        {
            h = ((h << 1) | (h >> 31)) + data[i];
        }
        return h;
    }

    u32 hashRecordUnrolled(u32 h, u32 addr, u32 n, const u32* data)
    {
        h = ((h << 1) | (h >> 31)) + addr;
        h = ((h << 1) | (h >> 31)) + n;
        u32 words = n >> 2;
        u32 i = 0;
        for (; i + 192 < words; i += 256)
        {
            h = ((h << 1) | (h >> 31)) + data[i];
            h = ((h << 1) | (h >> 31)) + data[i + 64];
            h = ((h << 1) | (h >> 31)) + data[i + 128];
            h = ((h << 1) | (h >> 31)) + data[i + 192];
        }
        for (; i < words; i += 64)
        {
            h = ((h << 1) | (h >> 31)) + data[i];
        }
        return h;
    }
}

TEST(perf_helpers, int_and_fixed_formatting_matches_snprintf)
{
    char fast[64];
    char ref[64];

    const int ints[] = {0, 1, -1, 42, -999, 123456, INT_MIN, INT_MAX};
    for (u32 i = 0; i < sizeof(ints) / sizeof(ints[0]); i++)
    {
        gfx::appendInt(fast, 0, sizeof(fast), ints[i]);
        snprintf(ref, sizeof(ref), "%d", ints[i]);
        CHECK_EQ(0, strcmp(fast, ref));
    }

    const float vals[] = {0.0f, -0.0f, 0.001f, -0.001f, 1.234f, -12.345f, 99.9f, -150.56f, 999.0f};
    for (u32 i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        u32 pos = 0;
        CHECK(gfx::appendFixed(fast, pos, sizeof(fast), vals[i], 3, true));
        snprintf(ref, sizeof(ref), "%+.3f", vals[i]);
        CHECK_EQ(0, strcmp(fast, ref));

        pos = 0;
        CHECK(gfx::appendFixed(fast, pos, sizeof(fast), vals[i], 2, true));
        snprintf(ref, sizeof(ref), "%+.2f", vals[i]);
        CHECK_EQ(0, strcmp(fast, ref));

        pos = 0;
        CHECK(gfx::appendFixed(fast, pos, sizeof(fast), vals[i], 1, false));
        snprintf(ref, sizeof(ref), "%.1f", vals[i]);
        CHECK_EQ(0, strcmp(fast, ref));
    }

    // Out-of-range and NaN fall back cleanly.
    u32 pos = 0;
    CHECK(!gfx::appendFixed(fast, pos, sizeof(fast), 200000.0f, 3, true));
    CHECK(!gfx::appendFixed(fast, pos, sizeof(fast), -200000.0f, 3, true));
    u32 nanBits = 0x7FC00000u;
    float nanVal = 0.0f;
    memcpy(&nanVal, &nanBits, sizeof(nanVal));
    CHECK(!gfx::appendFixed(fast, pos, sizeof(fast), nanVal, 3, true));

    // Full formatFast matches info_display formats and rejects unsupported specifiers.
    gfx::FmtArg aVel[3] = {gfx::FmtArg::fromStr("Vel (Self)"), gfx::FmtArg::fromFloat(1.25f),
                           gfx::FmtArg::fromFloat(-0.5f)};
    CHECK(gfx::formatFast(fast, sizeof(fast), "%s: (%+.3f , %+.3f)", aVel, 3) > 0);
    CHECK_EQ(0, strcmp(fast, "Vel (Self): (+1.250 , -0.500)"));

    gfx::FmtArg aPos[2] = {gfx::FmtArg::fromFloat(-12.34f), gfx::FmtArg::fromFloat(5.67f)};
    CHECK(gfx::formatFast(fast, sizeof(fast), "Pos: (%+.2f, %+.2f)", aPos, 2) > 0);
    CHECK_EQ(0, strcmp(fast, "Pos: (-12.34, +5.67)"));

    gfx::FmtArg aDmg[1] = {gfx::FmtArg::fromFloat(42.5f)};
    CHECK(gfx::formatFast(fast, sizeof(fast), "Damage: %.1f%%", aDmg, 1) > 0);
    CHECK_EQ(0, strcmp(fast, "Damage: 42.5%"));

    gfx::FmtArg aStun[2] = {gfx::FmtArg::fromInt(4), gfx::FmtArg::fromInt(12)};
    CHECK(gfx::formatFast(fast, sizeof(fast), "Shield Stun: %d/%d", aStun, 2) > 0);
    CHECK_EQ(0, strcmp(fast, "Shield Stun: 4/12"));

    gfx::FmtArg aHex[1] = {gfx::FmtArg::fromInt(0x1A)};
    CHECK_EQ(-1, gfx::formatFast(fast, sizeof(fast), "State: 0x%X", aHex, 1));
    gfx::FmtArg aSub[3] = {gfx::FmtArg::fromStr("Attack11"), gfx::FmtArg::fromInt(3), gfx::FmtArg::fromInt(19)};
    CHECK_EQ(-1, gfx::formatFast(fast, sizeof(fast), "Subaction: %.20s %d/%d", aSub, 3));
}

TEST(perf_helpers, glyph_tex_coord_matches_print_glyph_formula)
{
    CHECK_EQ(0u, static_cast<unsigned>(gfx::glyphTexCoord(0, 256)));
    CHECK_EQ(16384u, static_cast<unsigned>(gfx::glyphTexCoord(128, 256)));
    CHECK_EQ(32768u, static_cast<unsigned>(gfx::glyphTexCoord(256, 256)));
    CHECK_EQ(0u, static_cast<unsigned>(gfx::glyphTexCoord(64, 0)));
    CHECK_EQ(static_cast<unsigned>(static_cast<u16>((19u << 15) / 512u)),
             static_cast<unsigned>(gfx::glyphTexCoord(19, 512)));
}

TEST(perf_helpers, unrolled_savestate_record_hash_matches_scalar)
{
    u32 buf[600];
    for (u32 i = 0; i < 600; i++)
    {
        buf[i] = 0x9E3779B9u * (i + 1u);
    }
    const u32 wordCounts[] = {0, 1, 63, 64, 65, 128, 192, 193, 255, 256, 257, 320, 512, 599, 600};
    u32 hs = 0x12345678u;
    u32 hu = 0x12345678u;
    for (u32 k = 0; k < sizeof(wordCounts) / sizeof(wordCounts[0]); k++)
    {
        u32 bytes = wordCounts[k] * 4u;
        hs = hashRecordScalar(hs, 0x805A0000u + k * 0x100u, bytes, buf);
        hu = hashRecordUnrolled(hu, 0x805A0000u + k * 0x100u, bytes, buf);
        CHECK_EQ(hs, hu);
    }
}
