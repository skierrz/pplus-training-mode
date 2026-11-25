#pragma once

#include <types.h>

// Pure formatting and glyph-UV helpers (zero Brawl/GX headers, zero varargs,
// and zero string literals; shared by info_display.cpp, text.cpp, and PC host
// unit tests).
namespace tm {
namespace gfx {
    inline u16 glyphTexCoord(u32 cellPos, u32 texSize)
    {
        return texSize != 0 ? static_cast<u16>((cellPos << 15) / texSize) : 0;
    }

    inline u32 appendStr(char* dst, u32 pos, u32 size, const char* s)
    {
        while (*s != '\0' && pos + 1 < size)
        {
            dst[pos++] = *s++;
        }
        if (size > 0)
        {
            dst[pos] = '\0';
        }
        return pos;
    }

    inline u32 appendInt(char* dst, u32 pos, u32 size, int v)
    {
        if (pos + 1 >= size)
        {
            return pos;
        }
        u32 u;
        if (v < 0)
        {
            dst[pos++] = '-';
            u = static_cast<u32>(-(v + 1)) + 1u;
        }
        else
        {
            u = static_cast<u32>(v);
        }
        char tmp[12];
        u32 n = 0;
        do
        {
            tmp[n++] = static_cast<char>('0' + (u % 10u));
            u /= 10u;
        } while (u != 0);
        while (n > 0 && pos + 1 < size)
        {
            dst[pos++] = tmp[--n];
        }
        dst[pos] = '\0';
        return pos;
    }

    // Fast fixed-point decimal formatting for finite values in (-100000, 100000)
    // with 0..4 decimal places. Returns false for NaN, Inf, or out-of-range
    // inputs so callers can fall back to libc snprintf.
    inline bool appendFixed(char* dst, u32& pos, u32 size, float v, int decimals, bool plusSign)
    {
        if (decimals < 0 || decimals > 4 || !(v > -100000.0f && v < 100000.0f))
        {
            return false;
        }
        u32 bits = *reinterpret_cast<const u32*>(&v);
        bool neg = (bits & 0x80000000u) != 0;
        double av = static_cast<double>(neg ? -v : v);
        static const u32 kPow10[5] = {1u, 10u, 100u, 1000u, 10000u};
        u32 scale = kPow10[decimals];
        u32 scaled = static_cast<u32>(av * static_cast<double>(scale) + 0.5);
        u32 whole = scaled / scale;
        u32 frac = scaled % scale;
        if (pos + 1 < size)
        {
            if (neg)
            {
                dst[pos++] = '-';
            }
            else if (plusSign)
            {
                dst[pos++] = '+';
            }
        }
        pos = appendInt(dst, pos, size, static_cast<int>(whole));
        if (decimals > 0 && pos + 1 < size)
        {
            dst[pos++] = '.';
            for (int d = decimals - 1; d >= 0 && pos + 1 < size; d--)
            {
                u32 div = kPow10[d];
                dst[pos++] = static_cast<char>('0' + (frac / div));
                frac %= div;
            }
            dst[pos] = '\0';
        }
        return true;
    }

    // Typed argument list for formatFast() without varargs (__va_arg).
    struct FmtArg {
        enum Kind { kInt, kStr, kFloat };
        u8 kind;
        int i;
        const char* s;
        float f;

        static FmtArg fromInt(int v)
        {
            FmtArg a = {kInt, v, NULL, 0.0f};
            return a;
        }
        static FmtArg fromStr(const char* v)
        {
            FmtArg a = {kStr, 0, v, 0.0f};
            return a;
        }
        static FmtArg fromFloat(float v)
        {
            FmtArg a = {kFloat, 0, NULL, v};
            return a;
        }
    };

    // Fast subset formatter supporting literal text, %%, %s, %d, and
    // %[+].[0-4]f. Returns written length on success, or -1 if any specifier
    // or value needs full libc snprintf fallback.
    inline int formatFast(char* dst, u32 size, const char* fmt, const FmtArg* args, int argCount)
    {
        if (size == 0)
        {
            return 0;
        }
        u32 pos = 0;
        int idx = 0;
        for (const char* p = fmt; *p != '\0'; p++)
        {
            if (*p != '%')
            {
                if (pos + 1 < size)
                {
                    dst[pos++] = *p;
                }
                continue;
            }
            p++;
            if (*p == '%')
            {
                if (pos + 1 < size)
                {
                    dst[pos++] = '%';
                }
                continue;
            }
            if (idx >= argCount)
            {
                return -1;
            }
            const FmtArg& a = args[idx++];
            if (*p == 'd')
            {
                if (a.kind != FmtArg::kInt)
                {
                    return -1;
                }
                pos = appendInt(dst, pos, size, a.i);
                continue;
            }
            if (*p == 's')
            {
                if (a.kind != FmtArg::kStr || a.s == NULL)
                {
                    return -1;
                }
                pos = appendStr(dst, pos, size, a.s);
                continue;
            }
            bool plus = false;
            if (*p == '+')
            {
                plus = true;
                p++;
            }
            if (*p == '.' && p[1] >= '0' && p[1] <= '4' && p[2] == 'f')
            {
                if (a.kind != FmtArg::kFloat)
                {
                    return -1;
                }
                int dec = p[1] - '0';
                p += 2;
                if (!appendFixed(dst, pos, size, a.f, dec, plus))
                {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        dst[pos] = '\0';
        return static_cast<int>(pos);
    }
}
}
