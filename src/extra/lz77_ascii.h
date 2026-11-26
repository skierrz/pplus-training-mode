#pragma once

#include <types.h>

// Shared Canonical Huffman + LZSS decompressor (used by status_names,
// page_strings, menu_descriptions, and psa_opcodes) plus packed-string indexer.
// Stream format (all raw bytes in 0x00..0x7F):
// - 80 bytes: 160 4-bit code lengths for literal/length symbols (0..127 = ASCII,
//             128..159 = match length 3..34)
// - 15 bytes: 30 4-bit code lengths for distance slots (0..29, DEFLATE-style)
// - Remaining bytes: MSB-first canonical Huffman bitstream producing `rawLen` bytes.
namespace tm {
namespace extra {
    struct HuffTab {
        u16 count[16];
        u16 firstCode[16];
        u16 firstSym[16];
        u8 syms[160];
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline void buildHuffTab(HuffTab& t, const u8* nib, int nSyms)
    {
        for (int b = 0; b < 16; b++)
        {
            t.count[b] = 0;
        }
        for (int s = 0; s < nSyms; s++)
        {
            int l = (s & 1) == 0 ? (nib[s >> 1] >> 4) : (nib[s >> 1] & 0xF);
            if (l != 0)
            {
                t.count[l]++;
            }
        }
        u16 code = 0;
        u16 off = 0;
        u16 next[16];
        for (int b = 1; b < 16; b++)
        {
            t.firstCode[b] = code;
            t.firstSym[b] = off;
            next[b] = off;
            code = static_cast<u16>((code + t.count[b]) << 1);
            off = static_cast<u16>(off + t.count[b]);
        }
        for (int s = 0; s < nSyms; s++)
        {
            int l = (s & 1) == 0 ? (nib[s >> 1] >> 4) : (nib[s >> 1] & 0xF);
            if (l != 0)
            {
                t.syms[next[l]++] = static_cast<u8>(s);
            }
        }
    }

    inline int huffReadBit(const u8* src, u32& bp)
    {
        int bit = (src[bp >> 3] >> (7 - (bp & 7))) & 1;
        bp++;
        return bit;
    }

    inline int huffDecodeSym(const HuffTab& t, const u8* src, u32& bp)
    {
        int code = 0;
        for (int b = 1; b < 16; b++)
        {
            code = (code << 1) | huffReadBit(src, bp);
            int d = code - static_cast<int>(t.firstCode[b]);
            if (d < static_cast<int>(t.count[b]))
            {
                return t.syms[t.firstSym[b] + d];
            }
        }
        return 0;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline bool decompressHuffLzssImpl(const u8* src, char* dst, u32 rawLen)
    {
        HuffTab ll, dt;
        buildHuffTab(ll, src, 160);
        buildHuffTab(dt, src + 80, 30);
        u32 bp = 95u * 8u;
        u32 out = 0;
        while (out < rawLen)
        {
            int sym = huffDecodeSym(ll, src, bp);
            if (sym < 128)
            {
                dst[out++] = static_cast<char>(sym);
            }
            else
            {
                u32 ln = static_cast<u32>(sym - 125);
                int sl = huffDecodeSym(dt, src, bp);
                u32 dist = static_cast<u32>(sl + 1);
                if (sl >= 4)
                {
                    int eb = (sl - 2) >> 1;
                    u32 ex = 0;
                    for (int k = 0; k < eb; k++)
                    {
                        ex = (ex << 1) | static_cast<u32>(huffReadBit(src, bp));
                    }
                    dist = ((2u + static_cast<u32>(sl & 1)) << eb) + 1u + ex;
                }
                if (dist == 0u || dist > out || out + ln > rawLen)
                {
                    return false;
                }
                for (u32 k = 0; k < ln; k++)
                {
                    dst[out] = dst[out - dist];
                    out++;
                }
            }
        }
        return true;
    }

    // Populates `offsets[0..count-1]` with the start index of each consecutive
    // NUL-terminated string inside `text[0..len-1]`. Returns true if all
    // `count` strings were found inside bounds.
    inline bool indexPackedStringsImpl(const char* text, u32 len, u16* offsets, u32 count)
    {
        u32 pos = 0;
        for (u32 k = 0; k < count; k++)
        {
            if (pos >= len || pos > 65535u)
            {
                return false;
            }
            offsets[k] = static_cast<u16>(pos);
            while (pos < len && text[pos] != '\0')
            {
                pos++;
            }
            if (pos >= len)
            {
                return false;
            }
            pos++;
        }
        return true;
    }

#ifdef __MWERKS__
    bool decompressHuffLzss(const u8* src, char* dst, u32 rawLen);
    bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count);
#else
    inline bool decompressHuffLzss(const u8* src, char* dst, u32 rawLen)
    {
        return decompressHuffLzssImpl(src, dst, rawLen);
    }

    inline bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count)
    {
        return indexPackedStringsImpl(text, len, offsets, count);
    }
#endif
}
}
