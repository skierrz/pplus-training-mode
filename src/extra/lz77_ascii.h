#pragma once

#include <types.h>

// Compact ASCII bit-7 LZ77 decompressor (v2: 2-byte short + 3-byte long match).
// - 0x00..0x7F: literal ASCII byte
// - 0x80..0xBF: 2-byte short match: len = ((b >> 3) & 7) + 3 (3..10),
//               dist = (((b & 7) << 8) | src[i++]) + 1 (1..2048)
// - 0xC0..0xFF: 3-byte long match:  len = (b & 0x3F) + 3 (3..66),
//               dist = ((src[i] << 8) | src[i+1]) + 1 (1..65536)
namespace tm {
namespace extra {
    inline u32 decompressLz77AsciiImpl(const u8* src, u32 srcLen, char* dst, u32 dstCap)
    {
        u32 i = 0;
        u32 out = 0;
        while (i < srcLen)
        {
            u8 b = src[i++];
            if ((b & 0x80u) == 0u)
            {
                if (out >= dstCap)
                {
                    return 0;
                }
                dst[out++] = static_cast<char>(b);
            }
            else
            {
                u32 len = 0;
                u32 dist = 0;
                if ((b & 0x40u) == 0u)
                {
                    if (i >= srcLen)
                    {
                        return 0;
                    }
                    len = ((static_cast<u32>(b) >> 3) & 7u) + 3u;
                    dist = (((static_cast<u32>(b) & 7u) << 8) | static_cast<u32>(src[i++])) + 1u;
                }
                else
                {
                    if (i + 1u >= srcLen)
                    {
                        return 0;
                    }
                    len = (static_cast<u32>(b) & 0x3Fu) + 3u;
                    dist = ((static_cast<u32>(src[i]) << 8) | static_cast<u32>(src[i + 1u])) + 1u;
                    i += 2u;
                }
                if (dist == 0u || dist > out || out + len > dstCap)
                {
                    return 0;
                }
                for (u32 k = 0; k < len; k++)
                {
                    dst[out] = dst[out - dist];
                    out++;
                }
            }
        }
        return out;
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
    u32 decompressLz77Ascii(const u8* src, u32 srcLen, char* dst, u32 dstCap);
    bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count);
#else
    inline u32 decompressLz77Ascii(const u8* src, u32 srcLen, char* dst, u32 dstCap)
    {
        return decompressLz77AsciiImpl(src, srcLen, dst, dstCap);
    }

    inline bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count)
    {
        return indexPackedStringsImpl(text, len, offsets, count);
    }
#endif
}
}
