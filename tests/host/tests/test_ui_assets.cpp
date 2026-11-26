// Host unit tests for native Brawl/P+ UI asset parsing, sprite UV table, and
// StockFaceTex ID formatting (src/extra/ui_assets_core.h).
#include <stdio.h>
#include <string.h>

#include "extra/ui_assets_core.h"
#include "host/test.h"
#include "shared/ui_icons.h"

using namespace tm;

namespace {
    static u8* align32(u8* p)
    {
        u32 a = reinterpret_cast<u32>(p);
        return p + ((32u - (a & 31u)) & 31u);
    }

    static void putBe16(u8* p, u16 v)
    {
        p[0] = static_cast<u8>((v >> 8) & 0xFF);
        p[1] = static_cast<u8>(v & 0xFF);
    }

    static void putBe32(u8* p, u32 v)
    {
        p[0] = static_cast<u8>((v >> 24) & 0xFF);
        p[1] = static_cast<u8>((v >> 16) & 0xFF);
        p[2] = static_cast<u8>((v >> 8) & 0xFF);
        p[3] = static_cast<u8>(v & 0xFF);
    }

    struct SynthEntry {
        const char* name;
        u16 width;
        u16 height;
        u32 texFmt;
        u32 pltFmt;
        u16 pltEntries;
        u8 texFill;
        u8 pltFill;
    };

    // Builds a valid big-endian NW4R BRRES containing root -> Textures(NW4R) &
    // Palettes(NW4R) with `count` matched TEX0/PLT0 pairs.
    static u32 buildSyntheticBrres(u8* buf, u32 cap, const SynthEntry* entries, u32 count)
    {
        memset(buf, 0, cap);
        u32 rootOff = 0x10;
        u32 rootDictOff = rootOff + 0x08;
        u32 rootDictSize = 8u + (2u + 1u) * 16u; // 2 groups: Textures(NW4R), Palettes(NW4R)
        u32 texDictOff = rootDictOff + rootDictSize;
        u32 subDictSize = 8u + (count + 1u) * 16u;
        u32 pltDictOff = texDictOff + subDictSize;
        u32 cur = pltDictOff + subDictSize;

        // Place group name strings inside the root block.
        u32 texGrpStrOff = cur;
        const char* kTexGrp = "Textures(NW4R)";
        memcpy(buf + cur, kTexGrp, strlen(kTexGrp) + 1);
        cur += static_cast<u32>(strlen(kTexGrp) + 1);

        u32 pltGrpStrOff = cur;
        const char* kPltGrp = "Palettes(NW4R)";
        memcpy(buf + cur, kPltGrp, strlen(kPltGrp) + 1);
        cur += static_cast<u32>(strlen(kPltGrp) + 1);

        // Place entry name strings.
        u32 strOffs[1024];
        for (u32 i = 0; i < count; i++)
        {
            strOffs[i] = cur;
            u32 len = static_cast<u32>(strlen(entries[i].name) + 1);
            memcpy(buf + cur, entries[i].name, len);
            cur += len;
        }
        cur = (cur + 31u) & ~31u;
        u32 rootSize = cur - rootOff;

        // Write BRRES file header & root header.
        putBe32(buf + 0x00, extra::kBrresMagic);
        putBe16(buf + 0x04, extra::kBrresBom);
        putBe16(buf + 0x06, 0);
        putBe16(buf + 0x0C, 0x10);
        putBe16(buf + 0x0E, static_cast<u16>(1 + count * 2));
        putBe32(buf + rootOff + 0x00, extra::kBrresRootMagic);
        putBe32(buf + rootOff + 0x04, rootSize);

        // Root dictionary: entry 1 = Textures(NW4R), entry 2 = Palettes(NW4R).
        putBe32(buf + rootDictOff + 0x00, rootDictSize);
        putBe32(buf + rootDictOff + 0x04, 2);
        putBe16(buf + rootDictOff + 0x08, 0xFFFF); // sentinel node 0
        putBe32(buf + rootDictOff + 0x08 + 16 + 0x08, texGrpStrOff - rootDictOff);
        putBe32(buf + rootDictOff + 0x08 + 16 + 0x0C, texDictOff - rootDictOff);
        putBe32(buf + rootDictOff + 0x08 + 32 + 0x08, pltGrpStrOff - rootDictOff);
        putBe32(buf + rootDictOff + 0x08 + 32 + 0x0C, pltDictOff - rootDictOff);

        // Sub-dictionary headers.
        putBe32(buf + texDictOff + 0x00, subDictSize);
        putBe32(buf + texDictOff + 0x04, count);
        putBe16(buf + texDictOff + 0x08, 0xFFFF);

        putBe32(buf + pltDictOff + 0x00, subDictSize);
        putBe32(buf + pltDictOff + 0x04, count);
        putBe16(buf + pltDictOff + 0x08, 0xFFFF);

        // Emit TEX0 and PLT0 blocks.
        for (u32 i = 0; i < count; i++)
        {
            const SynthEntry& e = entries[i];
            u32 texOff = cur;
            u32 pixBytes = extra::ciTexMinBytes(e.width, e.height, e.texFmt);
            if (pixBytes < 32u)
            {
                pixBytes = 32u;
            }
            u32 texBlockSize = 0x40u + pixBytes;
            putBe32(buf + texOff + 0x00, extra::kTex0Magic);
            putBe32(buf + texOff + 0x04, texBlockSize);
            putBe32(buf + texOff + 0x08, 1);
            putBe32(buf + texOff + 0x0C, static_cast<u32>(-static_cast<s32>(texOff)));
            putBe32(buf + texOff + 0x10, 0x40);
            putBe32(buf + texOff + 0x14, static_cast<u32>(static_cast<s32>(strOffs[i]) - static_cast<s32>(texOff)));
            putBe32(buf + texOff + 0x18, extra::isCiTexFormat(e.texFmt) ? 1u : 0u);
            putBe16(buf + texOff + 0x1C, e.width);
            putBe16(buf + texOff + 0x1E, e.height);
            putBe32(buf + texOff + 0x20, e.texFmt);
            putBe32(buf + texOff + 0x24, 1);
            memset(buf + texOff + 0x40, e.texFill, pixBytes);
            cur = (texOff + texBlockSize + 31u) & ~31u;

            u32 pltOff = cur;
            u32 pltBytes = static_cast<u32>(e.pltEntries) * 2u;
            u32 pltBlockSize = 0x40u + pltBytes;
            putBe32(buf + pltOff + 0x00, extra::kPlt0Magic);
            putBe32(buf + pltOff + 0x04, pltBlockSize);
            putBe32(buf + pltOff + 0x08, 1);
            putBe32(buf + pltOff + 0x0C, static_cast<u32>(-static_cast<s32>(pltOff)));
            putBe32(buf + pltOff + 0x10, 0x40);
            putBe32(buf + pltOff + 0x14, static_cast<u32>(static_cast<s32>(strOffs[i]) - static_cast<s32>(pltOff)));
            putBe32(buf + pltOff + 0x18, e.pltFmt);
            putBe16(buf + pltOff + 0x1C, e.pltEntries);
            memset(buf + pltOff + 0x40, e.pltFill, pltBytes);
            cur = (pltOff + pltBlockSize + 31u) & ~31u;

            // Link into Textures(NW4R) and Palettes(NW4R) dicts.
            u32 texNodeOff = texDictOff + 8u + (i + 1u) * 16u;
            putBe32(buf + texNodeOff + 0x08, static_cast<u32>(static_cast<s32>(strOffs[i]) - static_cast<s32>(texDictOff)));
            putBe32(buf + texNodeOff + 0x0C, static_cast<u32>(static_cast<s32>(texOff) - static_cast<s32>(texDictOff)));

            u32 pltNodeOff = pltDictOff + 8u + (i + 1u) * 16u;
            putBe32(buf + pltNodeOff + 0x08, static_cast<u32>(static_cast<s32>(strOffs[i]) - static_cast<s32>(pltDictOff)));
            putBe32(buf + pltNodeOff + 0x0C, static_cast<u32>(static_cast<s32>(pltOff) - static_cast<s32>(pltDictOff)));
        }

        putBe32(buf + 0x08, cur);
        return cur;
    }
}

TEST(ui_assets, brres_parses_ci8_and_ci4_assets_and_handles_over_729_entries)
{
    static u8 bufRaw[65536 + 32];
    u8* buf = align32(bufRaw);
    const SynthEntry items[3] = {
        {"InfPauseButton00", 120, 132, extra::kGxTfC8, extra::kGxTlRgb5A3, 256, 0xA1, 0x55},
        {"InfStc.0301", 32, 32, extra::kGxTfC8, extra::kGxTlRgb5A3, 32, 0xB2, 0x66},
        {"InfStc.2500", 16, 16, extra::kGxTfC4, extra::kGxTlRgb5A3, 16, 0xC3, 0x77}
    };
    u32 total = buildSyntheticBrres(buf, 65536u, items, 3);
    REQUIRE(total > 0 && total <= 65536u);

    extra::BrresView view;
    REQUIRE(extra::brresOpen(buf, total, view));
    CHECK_EQ(total, view.fileSize);

    extra::CiAsset btn;
    REQUIRE(extra::brresFindCiAsset(view, "InfPauseButton00", btn));
    CHECK(btn.valid);
    CHECK_EQ(120, btn.tex.width);
    CHECK_EQ(132, btn.tex.height);
    CHECK_EQ(extra::kGxTfC8, btn.tex.gxFmt);
    CHECK(btn.tex.hasPalette);
    CHECK_EQ(0xA1, btn.tex.data[0]);
    CHECK_EQ(extra::kGxTlRgb5A3, btn.plt.fmt);
    CHECK_EQ(256, btn.plt.entryCount);
    CHECK_EQ(0x55, btn.plt.data[0]);

    extra::CiAsset fox;
    REQUIRE(extra::brresFindCiAsset(view, "InfStc.0301", fox));
    CHECK_EQ(32, fox.tex.width);
    CHECK_EQ(32, fox.tex.height);
    CHECK_EQ(extra::kGxTfC8, fox.tex.gxFmt);
    CHECK_EQ(32, fox.plt.entryCount);
    CHECK_EQ(0xB2, fox.tex.data[0]);
    CHECK_EQ(0x66, fox.plt.data[0]);

    extra::CiAsset fb;
    REQUIRE(extra::brresFindCiAsset(view, "InfStc.2500", fb));
    CHECK_EQ(16, fb.tex.width);
    CHECK_EQ(16, fb.tex.height);
    CHECK_EQ(extra::kGxTfC4, fb.tex.gxFmt);
    CHECK_EQ(16, fb.plt.entryCount);
    CHECK_EQ(0xC3, fb.tex.data[0]);
    CHECK_EQ(0x77, fb.plt.data[0]);

    extra::CiAsset missing;
    CHECK(!extra::brresFindCiAsset(view, "DoesNotExist", missing));
    CHECK(!missing.valid);

    // StockFaceTex.brres has 729 entries: verify lookups past entry 729 succeed!
    static u8 bigBufRaw[256 * 1024 + 32];
    u8* bigBuf = align32(bigBufRaw);
    static SynthEntry many[760];
    static char names[760][16];
    for (u32 i = 0; i < 760; i++)
    {
        snprintf(names[i], sizeof(names[i]), "InfStc.%04u", i + 1);
        many[i].name = names[i];
        many[i].width = 8;
        many[i].height = 4;
        many[i].texFmt = extra::kGxTfC8;
        many[i].pltFmt = extra::kGxTlRgb5A3;
        many[i].pltEntries = 16;
        many[i].texFill = static_cast<u8>(i & 0xFF);
        many[i].pltFill = static_cast<u8>((i * 3) & 0xFF);
    }
    u32 bigTotal = buildSyntheticBrres(bigBuf, 256u * 1024u, many, 760);
    REQUIRE(bigTotal > 0 && bigTotal <= 256u * 1024u);
    extra::BrresView bigView;
    REQUIRE(extra::brresOpen(bigBuf, bigTotal, bigView));
    extra::CiAsset last;
    REQUIRE(extra::brresFindCiAsset(bigView, "InfStc.0760", last));
    CHECK_EQ(8, last.tex.width);
    CHECK_EQ(4, last.tex.height);
    CHECK_EQ(static_cast<u8>(759 & 0xFF), last.tex.data[0]);
}

TEST(ui_assets, brres_rejects_corrupt_headers_oob_offsets_and_truncated_blocks)
{
    static u8 baseRaw[4096 + 32];
    static u8 copyRaw[4096 + 32];
    u8* base = align32(baseRaw);
    u8* copy = align32(copyRaw);
    const SynthEntry item = {"TestTex", 16, 16, extra::kGxTfC8, extra::kGxTlRgb5A3, 32, 0x11, 0x22};
    u32 total = buildSyntheticBrres(base, 4096u, &item, 1);
    REQUIRE(total > 0 && total <= 4096u);

    extra::BrresView view;
    extra::CiAsset asset;

    // NULL / truncated buffer.
    CHECK(!extra::brresOpen(NULL, total, view));
    CHECK(!extra::brresOpen(base, 0x20, view));
    CHECK(!extra::brresOpen(base, total - 1, view));

    // Bad magic.
    memcpy(copy, base, total);
    copy[0] = 'X';
    CHECK(!extra::brresOpen(copy, total, view));

    // Bad BOM.
    memcpy(copy, base, total);
    putBe16(copy + 0x04, 0xFFFE);
    CHECK(!extra::brresOpen(copy, total, view));

    // Bad root magic.
    memcpy(copy, base, total);
    copy[0x10] = 'X';
    CHECK(!extra::brresOpen(copy, total, view));

    // Excess entry count (> kBrresMaxEntries).
    memcpy(copy, base, total);
    putBe32(copy + 0x18 + 0x04, extra::kBrresMaxEntries + 1);
    REQUIRE(extra::brresOpen(copy, total, view));
    CHECK(!extra::brresFindCiAsset(view, "TestTex", asset));

    // OOB data offset in root dict.
    memcpy(copy, base, total);
    putBe32(copy + 0x18 + 8 + 16 + 0x0C, 0x00100000);
    REQUIRE(extra::brresOpen(copy, total, view));
    CHECK(!extra::brresFindCiAsset(view, "TestTex", asset));

    // OOB string offset in root dict.
    memcpy(copy, base, total);
    putBe32(copy + 0x18 + 8 + 16 + 0x08, 0x00100000);
    REQUIRE(extra::brresOpen(copy, total, view));
    CHECK(!extra::brresFindCiAsset(view, "TestTex", asset));

    // Direct TEX0 / PLT0 corruption checks.
    REQUIRE(extra::brresOpen(base, total, view));
    extra::BrresEntry texE;
    extra::BrresEntry pltE;
    REQUIRE(extra::brresFindInGroup(view, "Textures(NW4R)", "TestTex", texE));
    REQUIRE(extra::brresFindInGroup(view, "Palettes(NW4R)", "TestTex", pltE));

    extra::Tex0Info ti;
    CHECK(extra::parseTex0(texE.data, texE.availBytes, ti));
    // Truncated pixel data fails.
    CHECK(!extra::parseTex0(texE.data, 0x40 + 100, ti));
    // Corrupt TEX0 magic fails.
    u8 badTex[0x200];
    memcpy(badTex, texE.data, 0x40 + 256);
    badTex[0] = 'B';
    CHECK(!extra::parseTex0(badTex, sizeof(badTex), ti));
    // Non-CI format (RGBA8 = 6) fails brresFindCiAsset.
    const SynthEntry nonCi = {"RgbaTex", 8, 8, 6, extra::kGxTlRgb5A3, 16, 0x11, 0x22};
    u32 nonCiTotal = buildSyntheticBrres(copy, 4096u, &nonCi, 1);
    REQUIRE(extra::brresOpen(copy, nonCiTotal, view));
    CHECK(!extra::brresFindCiAsset(view, "RgbaTex", asset));

    // Corrupt PLT0 format / entry count / truncated palette fails.
    extra::Plt0Info pi;
    CHECK(extra::parsePlt0(pltE.data, pltE.availBytes, pi));
    CHECK(!extra::parsePlt0(pltE.data, 0x40 + 10, pi));
    u8 badPltRaw[0x100 + 32];
    u8* badPlt = align32(badPltRaw);
    memcpy(badPlt, pltE.data, 0x40 + 64);
    putBe32(badPlt + 0x18, 99); // invalid GXTlutFmt
    CHECK(!extra::parsePlt0(badPlt, 0x100u, pi));
    memcpy(badPlt, pltE.data, 0x40 + 64);
    putBe16(badPlt + 0x1C, 0); // zero entries
    CHECK(!extra::parsePlt0(badPlt, 0x100u, pi));

    // Non-32-byte-aligned TEX0/PLT0 data pointers must be rejected (GX addr >> 5).
    u8 unalignedTexRaw[0x240 + 32];
    u8* unalignedTex = align32(unalignedTexRaw);
    memcpy(unalignedTex, texE.data, 0x40 + 256);
    CHECK(extra::parseTex0(unalignedTex, 0x200u, ti));
    putBe32(unalignedTex + 0x04, 0x44u + 256u);
    putBe32(unalignedTex + 0x10, 0x44u);
    CHECK(!extra::parseTex0(unalignedTex, 0x240u, ti));
    memcpy(unalignedTex + 4, texE.data, 0x40 + 256);
    CHECK(!extra::parseTex0(unalignedTex + 4, 0x200u, ti));

    memcpy(badPlt, pltE.data, 0x40 + 64);
    CHECK(extra::parsePlt0(badPlt, 0x100u, pi));
    putBe32(badPlt + 0x04, 0x44u + 64u);
    putBe32(badPlt + 0x10, 0x44u);
    CHECK(!extra::parsePlt0(badPlt, 0x100u, pi));
    memcpy(badPlt + 4, pltE.data, 0x40 + 64);
    CHECK(!extra::parsePlt0(badPlt + 4, 0xFCu, pi));
}

TEST(ui_assets, all_27_icons_have_valid_rects_aspects_and_uv_rotations)
{
    CHECK_EQ(27, ui::kIconCount);
    CHECK(extra::uiIconRect(ui::kIconCount) == NULL);
    CHECK(extra::uiIconRect(255) == NULL);

    for (u8 id = 0; id < ui::kIconCount; id++)
    {
        const extra::UiIconRect* r = extra::uiIconRect(id);
        REQUIRE(r != NULL);
        u16 sw = 0;
        u16 sh = 0;
        REQUIRE(extra::uiIconSheetExpectedSize(r->sheet, sw, sh));
        CHECK(r->x0 < r->x1);
        CHECK(r->y0 < r->y1);
        CHECK(r->x1 <= sw);
        CHECK(r->y1 <= sh);

        bool expectRot = (id == ui::kIconCursorRight || id == ui::kIconClL || id == ui::kIconClR
                          || id == ui::kIconNunZ);
        CHECK_EQ(expectRot ? 1 : 0, r->rot90CW);

        // Verify natural aspect matches ui::uiIconAspect(id) within 1/41 (WiiDpad is 42x41 -> 1.0f).
        float rw = static_cast<float>(r->x1 - r->x0);
        float rh = static_cast<float>(r->y1 - r->y0);
        float naturalAspect = expectRot ? (rh / rw) : (rw / rh);
        CHECK_NEAR(ui::uiIconAspect(id), naturalAspect, 0.03f);

        extra::UiQuadUv uv;
        REQUIRE(extra::computeIconUv(*r, sw, sh, uv));
        for (int c = 0; c < 4; c++)
        {
            CHECK(uv.u[c] >= 0.0f && uv.u[c] <= 1.0f);
            CHECK(uv.v[c] >= 0.0f && uv.v[c] <= 1.0f);
        }

        float u0 = (static_cast<float>(r->x0) + 0.25f) / static_cast<float>(sw);
        float v0 = (static_cast<float>(r->y0) + 0.25f) / static_cast<float>(sh);
        float u1 = (static_cast<float>(r->x1) - 0.25f) / static_cast<float>(sw);
        float v1 = (static_cast<float>(r->y1) - 0.25f) / static_cast<float>(sh);
        if (!expectRot)
        {
            // Normal: TL=(u0,v0), TR=(u1,v0), BR=(u1,v1), BL=(u0,v1)
            CHECK_NEAR(u0, uv.u[0], 1e-6f);
            CHECK_NEAR(v0, uv.v[0], 1e-6f);
            CHECK_NEAR(u1, uv.u[1], 1e-6f);
            CHECK_NEAR(v0, uv.v[1], 1e-6f);
            CHECK_NEAR(u1, uv.u[2], 1e-6f);
            CHECK_NEAR(v1, uv.v[2], 1e-6f);
            CHECK_NEAR(u0, uv.u[3], 1e-6f);
            CHECK_NEAR(v1, uv.v[3], 1e-6f);
        }
        else
        {
            // Rotated 90 deg CW: TL=(u0,v1), TR=(u0,v0), BR=(u1,v0), BL=(u1,v1)
            CHECK_NEAR(u0, uv.u[0], 1e-6f);
            CHECK_NEAR(v1, uv.v[0], 1e-6f);
            CHECK_NEAR(u0, uv.u[1], 1e-6f);
            CHECK_NEAR(v0, uv.v[1], 1e-6f);
            CHECK_NEAR(u1, uv.u[2], 1e-6f);
            CHECK_NEAR(v0, uv.v[2], 1e-6f);
            CHECK_NEAR(u1, uv.u[3], 1e-6f);
            CHECK_NEAR(v1, uv.v[3], 1e-6f);
        }
    }

    // Edge cases: tiny rect (<= 2px) omits 0.25f inset; out-of-bounds rect rejected.
    extra::UiIconRect tiny = {0, 4, 4, 6, 6, 0};
    extra::UiQuadUv uv;
    REQUIRE(extra::computeIconUv(tiny, 16, 16, uv));
    CHECK_NEAR(4.0f / 16.0f, uv.u[0], 1e-6f);
    CHECK_NEAR(6.0f / 16.0f, uv.u[1], 1e-6f);

    extra::UiIconRect oob = {0, 0, 0, 121, 10, 0};
    CHECK(!extra::computeIconUv(oob, 120, 132, uv));
}

TEST(ui_assets, stock_icon_name_formats_cosmetic_ids_and_fallbacks)
{
    char name[extra::kStockNameBufSize];

    // Mario (cosmeticId 0), Fox (6), Wario (37), Knuckles (42), Giga Bowser (44), Wario-Man (66).
    extra::stockIconName(0, 0, name);
    CHECK_STR("InfStc.0001", name);

    extra::stockIconName(6, 0, name);
    CHECK_STR("InfStc.0301", name);

    extra::stockIconName(6, 14, name);
    CHECK_STR("InfStc.0315", name);

    extra::stockIconName(37, 0, name);
    CHECK_STR("InfStc.1851", name);

    extra::stockIconName(42, 17, name);
    CHECK_STR("InfStc.2118", name);

    extra::stockIconName(44, 0, name);
    CHECK_STR("InfStc.2201", name);

    extra::stockIconName(66, 17, name);
    CHECK_STR("InfStc.3318", name);

    // Invalid / out-of-range inputs fall back cleanly to InfStc.2500.
    extra::stockIconName(-1, 0, name);
    CHECK_STR("InfStc.2500", name);

    extra::stockIconName(6, -1, name);
    CHECK_STR("InfStc.2500", name);

    extra::stockIconName(6, 50, name);
    CHECK_STR("InfStc.2500", name);

    extra::stockIconName(200, 0, name);
    CHECK_STR("InfStc.2500", name);

    extra::stockFallbackName(name);
    CHECK_STR("InfStc.2500", name);
}
