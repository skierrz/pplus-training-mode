#include "host/mem.h"

// The one file of the layer that talks to the operating system.
#undef free
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace host {
namespace mem {
    enum { kArenaSize = 0x00800000 };

    u32 g_arenaStart = 0;
    u32 g_arenaEnd = 0;
    static u32 s_arenaNext = 0;

    static bool map(u32 address, u32 size, const char* name)
    {
        void* got = VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (got != reinterpret_cast<void*>(address))
        {
            printf("host: cannot map %s at %08lx (%lu bytes), error %lu. The test program must be a 32-bit program "
                   "linked large-address-aware, run on 64-bit Windows.\n",
                   name, address, size, GetLastError());
            return false;
        }
        return true;
    }

    // Gives the range back and takes it again: the pages come back zeroed
    // without being written.
    static void zero(u32 address, u32 size)
    {
        VirtualFree(reinterpret_cast<void*>(address), size, MEM_DECOMMIT);
        if (VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_COMMIT, PAGE_READWRITE) == NULL)
        {
            printf("host: cannot get %lu bytes at %08lx again, error %lu\n", size, address, GetLastError());
            exit(3);
        }
    }

    bool init()
    {
        if (!map(kMem1, kMem1Size, "MEM1") || !map(kMem2, kMem2Size, "MEM2"))
        {
            return false;
        }
        void* arena = VirtualAlloc(NULL, kArenaSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (arena == NULL || inImage(reinterpret_cast<u32>(arena)))
        {
            printf("host: cannot get the arena (%d bytes)\n", static_cast<int>(kArenaSize));
            return false;
        }
        g_arenaStart = reinterpret_cast<u32>(arena);
        g_arenaEnd = g_arenaStart + kArenaSize;
        s_arenaNext = g_arenaStart;
        return true;
    }

    void clearArena()
    {
        if (s_arenaNext != g_arenaStart)
        {
            zero(g_arenaStart, kArenaSize);
            s_arenaNext = g_arenaStart;
        }
    }

    void clear()
    {
        // A test may write anywhere in the image through plain pointers, so
        // there is no telling what it touched: both ranges are taken afresh.
        zero(kMem1, kMem1Size);
        zero(kMem2, kMem2Size);
        clearArena();
    }

    static bool readInto(const char* path, u32 address, u32 size, char* why, u32 whyCap)
    {
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            snprintf(why, whyCap, "cannot open %s", path);
            return false;
        }
        size_t got = fread(reinterpret_cast<void*>(address), 1, size, f);
        bool more = fgetc(f) != EOF;
        fclose(f);
        if (got != size || more)
        {
            snprintf(why, whyCap, "%s is not %lu bytes", path, size);
            return false;
        }
        // Big-endian words into the PC's order.
        u32* p = reinterpret_cast<u32*>(address);
        for (u32 i = 0; i < size / 4; i++)
        {
            p[i] = _byteswap_ulong(p[i]);
        }
        return true;
    }

    bool loadDump(const char* mem1, const char* mem2, char* why, u32 whyCap)
    {
        clear();
        if (!readInto(mem1, kMem1, kMem1Size, why, whyCap))
        {
            clear();
            return false;
        }
        if (mem2 != NULL && !readInto(mem2, kMem2, kMem2Size, why, whyCap))
        {
            clear();
            return false;
        }
        return true;
    }

    void* alloc(u32 bytes, u32 align)
    {
        u32 at = (s_arenaNext + align - 1) & ~(align - 1);
        if (bytes > g_arenaEnd - at)
        {
            printf("host: the arena is full (%lu bytes asked for); raise kArenaSize in tests/host/host/mem.cpp\n", bytes);
            exit(3);
        }
        s_arenaNext = at + bytes;
        return reinterpret_cast<void*>(at);
    }
}
}
