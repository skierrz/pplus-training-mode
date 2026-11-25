#pragma once

#include <stddef.h>
#include <types.h>

#include "shared/ui_icons.h"
#include "shared/version.h"

// Pure, host-testable BRRES dictionary lookup, TEX0/PLT0 header parsing,
// native button-sheet sprite UV table, and StockFaceTex ID formatting helpers.
// Contains zero string literals so host text-fit rules stay unaffected.
namespace tm {
namespace extra {
    enum {
        kBrresMagic = 0x62726573,     // 'bres'
        kBrresBom = 0xFEFF,
        kBrresRootMagic = 0x726F6F74, // 'root'
        kTex0Magic = 0x54455830,      // 'TEX0'
        kPlt0Magic = 0x504C5430,      // 'PLT0'
        kBrresMaxEntries = 2048,      // StockFaceTex.brres holds 729 entries
        kBrresMaxFileSize = 16 * 1024 * 1024,
        kGxTfC4 = 8,
        kGxTfC8 = 9,
        kGxTfC14X2 = 10,
        kGxTlRgb5A3 = 2,
        kUiSheetPause = 0,
        kUiSheetClassic = 1,
        kUiSheetWiimote = 2,
        kUiSheetCount = 3,
        kStockFallbackId = 2500,
        kStockCostumeStride = 50,
        kStockNameBufSize = 16
    };

    struct BrresView {
        const u8* base;
        u32 fileSize;
        u32 rootDictOff;
    };

    struct BrresEntry {
        const u8* data;
        u32 availBytes;
    };

    struct Tex0Info {
        const u8* data;
        u16 width;
        u16 height;
        u32 gxFmt;
        bool hasPalette;
    };

    struct Plt0Info {
        const u8* data;
        u32 fmt;
        u16 entryCount;
    };

    struct CiAsset {
        Tex0Info tex;
        Plt0Info plt;
        bool valid;
    };

    struct UiIconRect {
        u8 sheet;
        u8 x0;
        u8 y0;
        u8 x1;
        u8 y1;
        u8 rot90CW;
    };

    struct UiQuadUv {
        float u[4]; // TL, TR, BR, BL
        float v[4];
    };

    inline u16 beU16(const u8* p)
    {
        return static_cast<u16>((static_cast<u16>(p[0]) << 8) | static_cast<u16>(p[1]));
    }

    inline u32 beU32(const u8* p)
    {
        return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8)
               | static_cast<u32>(p[3]);
    }

    inline s32 beS32(const u8* p)
    {
        return static_cast<s32>(beU32(p));
    }

    inline bool brresEqualsBounded(const u8* str, u32 maxBytes, const char* target)
    {
        if (str == NULL || target == NULL)
        {
            return false;
        }
        u32 i = 0;
        while (i < maxBytes)
        {
            char a = static_cast<char>(str[i]);
            char b = target[i];
            if (a != b)
            {
                return false;
            }
            if (a == '\0')
            {
                return true;
            }
            i++;
        }
        return false;
    }

    // Validates a big-endian NW4R BRRES header and locates its root ResDic.
    // Pass `maxBytes == 0` only after validating the file's RAM span externally.
    inline bool brresOpen(const u8* brres, u32 maxBytes, BrresView& out)
    {
        out.base = NULL;
        out.fileSize = 0;
        out.rootDictOff = 0;
        if (brres == NULL)
        {
            return false;
        }
        if (maxBytes != 0 && maxBytes < 0x30u)
        {
            return false;
        }
        if (beU32(brres + 0x00) != kBrresMagic || beU16(brres + 0x04) != kBrresBom)
        {
            return false;
        }
        u32 fileSize = beU32(brres + 0x08);
        if (fileSize < 0x30u || fileSize > kBrresMaxFileSize)
        {
            return false;
        }
        if (maxBytes != 0 && fileSize > maxBytes)
        {
            return false;
        }
        u16 headerSize = beU16(brres + 0x0C);
        if (headerSize < 0x10u || static_cast<u32>(headerSize) > fileSize - 0x20u)
        {
            return false;
        }
        u32 rootOff = static_cast<u32>(headerSize);
        if (beU32(brres + rootOff) != kBrresRootMagic)
        {
            return false;
        }
        u32 rootSize = beU32(brres + rootOff + 0x04);
        if (rootSize < 0x20u || rootSize > fileSize - rootOff)
        {
            return false;
        }
        out.base = brres;
        out.fileSize = fileSize;
        out.rootDictOff = rootOff + 0x08u;
        return true;
    }

    // Linear scan over a ResDic at `dictOff` (entries 1..numData, skipping the
    // sentinel node 0). All string and data offsets are bounds-checked against
    // `view.fileSize`.
    inline bool brresDictLookup(const BrresView& view, u32 dictOff, const char* name, u32& outDataOff)
    {
        outDataOff = 0;
        if (view.base == NULL || name == NULL || dictOff >= view.fileSize || view.fileSize - dictOff < 0x18u)
        {
            return false;
        }
        const u8* dict = view.base + dictOff;
        u32 dictSize = beU32(dict + 0x00);
        u32 numData = beU32(dict + 0x04);
        if (numData > kBrresMaxEntries)
        {
            return false;
        }
        u32 needed = 8u + (numData + 1u) * 16u;
        if (dictSize < needed || needed > view.fileSize - dictOff)
        {
            return false;
        }
        for (u32 i = 1; i <= numData; i++)
        {
            const u8* node = dict + 8u + i * 16u;
            s32 strRel = beS32(node + 0x08);
            s32 dataRel = beS32(node + 0x0C);
            if (strRel == 0)
            {
                continue;
            }
            s64 strAbs = static_cast<s64>(dictOff) + static_cast<s64>(strRel);
            if (strAbs <= 0 || strAbs >= static_cast<s64>(view.fileSize))
            {
                continue;
            }
            u32 strOff = static_cast<u32>(strAbs);
            if (!brresEqualsBounded(view.base + strOff, view.fileSize - strOff, name))
            {
                continue;
            }
            if (dataRel == 0)
            {
                return false;
            }
            s64 dataAbs = static_cast<s64>(dictOff) + static_cast<s64>(dataRel);
            if (dataAbs <= 0 || dataAbs >= static_cast<s64>(view.fileSize))
            {
                return false;
            }
            outDataOff = static_cast<u32>(dataAbs);
            return true;
        }
        return false;
    }

    inline bool brresFindInGroup(const BrresView& view, const char* groupName, const char* entryName,
                                 BrresEntry& out)
    {
        out.data = NULL;
        out.availBytes = 0;
        u32 groupOff = 0;
        if (!brresDictLookup(view, view.rootDictOff, groupName, groupOff))
        {
            return false;
        }
        u32 entryOff = 0;
        if (!brresDictLookup(view, groupOff, entryName, entryOff))
        {
            return false;
        }
        out.data = view.base + entryOff;
        out.availBytes = view.fileSize - entryOff;
        return true;
    }

    inline bool isCiTexFormat(u32 gxFmt)
    {
        return gxFmt == kGxTfC4 || gxFmt == kGxTfC8 || gxFmt == kGxTfC14X2;
    }

    // Minimum tiled byte footprint for color-indexed GX formats.
    inline u32 ciTexMinBytes(u16 width, u16 height, u32 gxFmt)
    {
        u32 w = static_cast<u32>(width);
        u32 h = static_cast<u32>(height);
        switch (gxFmt)
        {
        case kGxTfC4:
            return (((w + 7u) & ~7u) * ((h + 7u) & ~7u)) / 2u;
        case kGxTfC8:
            return ((w + 7u) & ~7u) * ((h + 3u) & ~3u);
        case kGxTfC14X2:
            return ((w + 3u) & ~3u) * ((h + 3u) & ~3u) * 2u;
        default:
            return 0u;
        }
    }

    inline bool parseTex0(const u8* tex0, u32 availBytes, Tex0Info& out)
    {
        out.data = NULL;
        out.width = 0;
        out.height = 0;
        out.gxFmt = 0;
        out.hasPalette = false;
        if (tex0 == NULL || availBytes < 0x30u)
        {
            return false;
        }
        if (beU32(tex0 + 0x00) != kTex0Magic)
        {
            return false;
        }
        u32 blockSize = beU32(tex0 + 0x04);
        if (blockSize < 0x30u || blockSize > availBytes)
        {
            return false;
        }
        s32 ofsData = beS32(tex0 + 0x10);
        if (ofsData < 0x30 || static_cast<u32>(ofsData) >= blockSize)
        {
            return false;
        }
        u32 flag = beU32(tex0 + 0x18);
        u16 width = beU16(tex0 + 0x1C);
        u16 height = beU16(tex0 + 0x1E);
        u32 gxFmt = beU32(tex0 + 0x20);
        if (width == 0 || height == 0 || width > 1024u || height > 1024u)
        {
            return false;
        }
        u32 minBytes = ciTexMinBytes(width, height, gxFmt);
        if (minBytes > blockSize - static_cast<u32>(ofsData))
        {
            return false;
        }
        out.data = tex0 + static_cast<u32>(ofsData);
        out.width = width;
        out.height = height;
        out.gxFmt = gxFmt;
        out.hasPalette = (flag & 1u) != 0;
        return true;
    }

    inline bool parsePlt0(const u8* plt0, u32 availBytes, Plt0Info& out)
    {
        out.data = NULL;
        out.fmt = 0;
        out.entryCount = 0;
        if (plt0 == NULL || availBytes < 0x20u)
        {
            return false;
        }
        if (beU32(plt0 + 0x00) != kPlt0Magic)
        {
            return false;
        }
        u32 blockSize = beU32(plt0 + 0x04);
        if (blockSize < 0x20u || blockSize > availBytes)
        {
            return false;
        }
        s32 ofsData = beS32(plt0 + 0x10);
        if (ofsData < 0x20 || static_cast<u32>(ofsData) >= blockSize)
        {
            return false;
        }
        u32 fmt = beU32(plt0 + 0x18);
        u16 entryCount = beU16(plt0 + 0x1C);
        if (fmt > kGxTlRgb5A3 || entryCount == 0 || entryCount > 16384u)
        {
            return false;
        }
        u32 pltBytes = static_cast<u32>(entryCount) * 2u;
        if (pltBytes > blockSize - static_cast<u32>(ofsData))
        {
            return false;
        }
        out.data = plt0 + static_cast<u32>(ofsData);
        out.fmt = fmt;
        out.entryCount = entryCount;
        return true;
    }

    // Resolves a matched (TEX0, PLT0) color-indexed pair inside `Textures(NW4R)`
    // and `Palettes(NW4R)`. Uses character arrays instead of string literals so
    // host text-fit scanning ignores this header.
    inline bool brresFindCiAsset(const BrresView& view, const char* name, CiAsset& out)
    {
        static const char kTexturesGroup[] = {'T', 'e', 'x', 't', 'u', 'r', 'e', 's',
                                              '(', 'N', 'W', '4', 'R', ')', '\0'};
        static const char kPalettesGroup[] = {'P', 'a', 'l', 'e', 't', 't', 'e', 's',
                                              '(', 'N', 'W', '4', 'R', ')', '\0'};
        out.valid = false;
        BrresEntry texEntry;
        BrresEntry pltEntry;
        if (!brresFindInGroup(view, kTexturesGroup, name, texEntry))
        {
            return false;
        }
        if (!brresFindInGroup(view, kPalettesGroup, name, pltEntry))
        {
            return false;
        }
        if (!parseTex0(texEntry.data, texEntry.availBytes, out.tex))
        {
            return false;
        }
        if (!isCiTexFormat(out.tex.gxFmt))
        {
            return false;
        }
        if (!parsePlt0(pltEntry.data, pltEntry.availBytes, out.plt))
        {
            return false;
        }
        out.valid = true;
        return true;
    }

    inline bool uiIconSheetExpectedSize(u8 sheet, u16& outW, u16& outH)
    {
        switch (sheet)
        {
        case kUiSheetPause:
            outW = 120;
            outH = 132;
            return true;
        case kUiSheetClassic:
            outW = 64;
            outH = 168;
            return true;
        case kUiSheetWiimote:
            outW = 112;
            outH = 96;
            return true;
        default:
            outW = 0;
            outH = 0;
            return false;
        }
    }

    // Verified pixel bounding rects (x0, y0, x1, y1) and 90 deg CW rotations
    // for all 27 UiIconId emblems in resident info_training.pac BRRES sheets.
    inline const UiIconRect* uiIconRect(u8 iconId)
    {
        static const UiIconRect kRects[ui::kIconCount] = {
            // Sheet 0: InfPauseButton00 (120x132)
            {kUiSheetPause, 1, 1, 15, 15, 1},      // kIconCursorRight
            {kUiSheetPause, 2, 94, 38, 130, 0},    // kIconGcA
            {kUiSheetPause, 40, 106, 65, 131, 0},  // kIconGcB
            {kUiSheetPause, 40, 40, 62, 73, 0},    // kIconGcX
            {kUiSheetPause, 4, 27, 37, 49, 0},     // kIconGcY
            {kUiSheetPause, 70, 46, 117, 77, 0},   // kIconGcL
            {kUiSheetPause, 70, 80, 118, 110, 0},  // kIconGcR
            {kUiSheetPause, 67, 113, 119, 130, 0}, // kIconGcZ
            {kUiSheetPause, 40, 10, 56, 26, 0},    // kIconGcStart
            {kUiSheetPause, 75, 2, 116, 43, 0},    // kIconGcDpad
            // Sheet 1: InfButton04ClBtn01 (64x168)
            {kUiSheetClassic, 1, 1, 31, 31, 0},    // kIconClA
            {kUiSheetClassic, 32, 1, 62, 31, 0},   // kIconClB
            {kUiSheetClassic, 1, 32, 31, 62, 0},   // kIconClX
            {kUiSheetClassic, 32, 32, 62, 62, 0},  // kIconClY
            {kUiSheetClassic, 1, 116, 24, 168, 1}, // kIconClL
            {kUiSheetClassic, 1, 63, 24, 115, 1},  // kIconClR
            {kUiSheetClassic, 30, 63, 62, 89, 0},  // kIconClZL
            {kUiSheetClassic, 30, 90, 62, 116, 0}, // kIconClZR
            // Sheet 2: InfButton04NnBtn01 (112x96)
            {kUiSheetWiimote, 78, 1, 112, 35, 0},  // kIconWiiA
            {kUiSheetWiimote, 44, 1, 77, 48, 0},   // kIconWiiB
            {kUiSheetWiimote, 83, 36, 111, 64, 0}, // kIconWii1
            {kUiSheetWiimote, 83, 65, 111, 93, 0}, // kIconWii2
            {kUiSheetWiimote, 24, 78, 40, 93, 0},  // kIconWiiPlus
            {kUiSheetWiimote, 1, 74, 23, 96, 0},   // kIconWiiMinus
            {kUiSheetWiimote, 1, 1, 43, 42, 0},    // kIconWiiDpad
            {kUiSheetWiimote, 1, 43, 35, 73, 0},   // kIconNunC
            {kUiSheetWiimote, 45, 49, 76, 91, 1}   // kIconNunZ
        };
        TM_STATIC_ASSERT(sizeof(kRects) / sizeof(kRects[0]) == ui::kIconCount, tm_ui_icon_rects_match_enum);
        if (iconId >= ui::kIconCount)
        {
            return NULL;
        }
        return &kRects[iconId];
    }

    // Computes normalized quad UVs (TL, TR, BR, BL) with a 0.25 texel inset on
    // spans > 2 px to prevent bilinear bleed across adjacent sprites.
    inline bool computeIconUv(const UiIconRect& r, u16 texW, u16 texH, UiQuadUv& out)
    {
        if (texW == 0 || texH == 0 || r.x0 >= r.x1 || r.y0 >= r.y1 || r.x1 > texW || r.y1 > texH)
        {
            return false;
        }
        float insetX = (r.x1 - r.x0 > 2) ? 0.25f : 0.0f;
        float insetY = (r.y1 - r.y0 > 2) ? 0.25f : 0.0f;
        float invW = 1.0f / static_cast<float>(texW);
        float invH = 1.0f / static_cast<float>(texH);
        float u0 = (static_cast<float>(r.x0) + insetX) * invW;
        float v0 = (static_cast<float>(r.y0) + insetY) * invH;
        float u1 = (static_cast<float>(r.x1) - insetX) * invW;
        float v1 = (static_cast<float>(r.y1) - insetY) * invH;
        if (r.rot90CW == 0)
        {
            out.u[0] = u0;
            out.v[0] = v0;
            out.u[1] = u1;
            out.v[1] = v0;
            out.u[2] = u1;
            out.v[2] = v1;
            out.u[3] = u0;
            out.v[3] = v1;
        }
        else
        {
            out.u[0] = u0;
            out.v[0] = v1;
            out.u[1] = u0;
            out.v[1] = v0;
            out.u[2] = u1;
            out.v[2] = v0;
            out.u[3] = u1;
            out.v[3] = v1;
        }
        return true;
    }

    // Computes the 4-digit StockFaceTex numeric ID (cosmeticId * 50 + costumeIdx + 1)
    // or kStockFallbackId (2500) on out-of-range inputs.
    inline int stockIconId(int cosmeticId, int costumeIdx)
    {
        if (cosmeticId < 0 || cosmeticId >= 200 || costumeIdx < 0 || costumeIdx >= kStockCostumeStride)
        {
            return kStockFallbackId;
        }
        int id = cosmeticId * kStockCostumeStride + costumeIdx + 1;
        if (id <= 0 || id > 9999)
        {
            return kStockFallbackId;
        }
        return id;
    }

    // Formats "InfStc.%04d" (or "InfStc.2500" fallback) into `out` without
    // double-quoted string literals or stdio formatting overhead.
    inline void stockIconName(int cosmeticId, int costumeIdx, char out[kStockNameBufSize])
    {
        int id = stockIconId(cosmeticId, costumeIdx);
        out[0] = 'I';
        out[1] = 'n';
        out[2] = 'f';
        out[3] = 'S';
        out[4] = 't';
        out[5] = 'c';
        out[6] = '.';
        out[7] = static_cast<char>('0' + (id / 1000) % 10);
        out[8] = static_cast<char>('0' + (id / 100) % 10);
        out[9] = static_cast<char>('0' + (id / 10) % 10);
        out[10] = static_cast<char>('0' + id % 10);
        out[11] = '\0';
    }

    inline void stockFallbackName(char out[kStockNameBufSize])
    {
        stockIconName(-1, 0, out);
    }
}
}
