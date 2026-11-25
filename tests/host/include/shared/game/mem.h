#pragma once

#include <types.h>

#include "host/mem.h"

// The PC build's shared/game/mem.h: the same helpers as the mod's
// (src/shared/game/mem.h), over the memory image and the arena of
// tests/host/host/mem.h. This file is found first because tests/host/include
// comes before src on the include path. tests/host/tests/test_mem.cpp
// compares it with the mod's own file, so a helper added there and not here
// is noticed.
//
// What differs, and why:
//  - isRam is also true for the arena, where host objects that stand in for
//    game objects live.
//  - byteAt looks where the byte is in the image (kept as words in the PC's
//    byte order). In the arena it reads the address as given.
namespace tm {
namespace game {
    inline bool isRam(const void* p)
    {
        u32 a = reinterpret_cast<u32>(p);
        return host::mem::inImage(a) || host::mem::inArena(a);
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

    inline u32 wordAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const u32*>(reinterpret_cast<u32>(base) + offset);
    }
    inline u8 byteAt(const void* base, u32 offset)
    {
        u32 a = reinterpret_cast<u32>(base) + offset;
        return host::mem::inImage(a) ? host::mem::byte(a) : *reinterpret_cast<const u8*>(a);
    }
}
}
