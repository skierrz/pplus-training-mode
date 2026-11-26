#pragma once

#include <types.h>

// Checks for reading game structures that may not exist yet.
namespace tm {
namespace game {
    // True if `p` points into MEM1 (0x80000000..0x817FFFFF) or Broadway-safe
    // MEM2 (0x90000000..0x935FFFFF, staying below IOS/Starlet top MEM2).
    inline bool isRam(const void* p)
    {
        u32 hi = reinterpret_cast<u32>(p) >> 16;
        return (hi ^ 0x8000u) < 0x0180u || (hi ^ 0x9000u) < 0x0360u;
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
