#pragma once

#include <types.h>

// Checks for reading game structures that may not exist yet.
namespace tm {
namespace game {
    // True if `p` points into MEM1 or MEM2.
    inline bool isRam(const void* p)
    {
        u32 a = reinterpret_cast<u32>(p);
        return (a >= 0x80000000 && a < 0x81800000) || (a >= 0x90000000 && a < 0x94000000);
    }

    inline bool sameString(const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b)
        {
            a++;
            b++;
        }
        return *a == *b;
    }

    // The word, byte at `offset` bytes into the object at `base`.
    inline u32 wordAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const u32*>(reinterpret_cast<u32>(base) + offset);
    }
    inline u8 byteAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const u8*>(reinterpret_cast<u32>(base) + offset);
    }
}
}
