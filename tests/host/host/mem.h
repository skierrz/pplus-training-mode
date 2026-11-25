#pragma once

#include <types.h>

// The game's memory on the PC.
//
// The image: MEM1 (0x80000000, 24 MiB) and MEM2 (0x90000000, 64 MiB) are
// mapped at the console's own addresses, so a game address the mod's code
// keeps in a pointer or a u32 is a valid pointer here and the sources read it
// as they do on the console. The image is all zero (a test then writes a small
// synthetic one with the set functions) or holds a memory dump of the running
// game.
//
// Byte order: the console is big-endian, the PC little-endian. The image is
// kept as 32-bit words in the PC's order: a dump has every word's bytes
// reversed as it is loaded. So a word or a float of the game reads right
// through any pointer (*p, p[i], the mod's wordAt). A single byte or a
// 16-bit value does not sit where its address says: read and write those
// with byte() and half() (the stand-in for the mod's byteAt does). Text and
// byte arrays read through a plain char pointer therefore do not work in
// the image; code that needs them is given host objects instead.
//
// The arena: host objects that stand in for game objects (a fake fighter,
// its modules, a buffer from a game heap) are allocated here. It is ordinary
// PC memory in the PC's layout, emptied before every test, and counts as RAM
// for the mod's isRam().
namespace host {
namespace mem {
    static const u32 kMem1 = 0x80000000u;
    static const u32 kMem1Size = 0x01800000u;
    static const u32 kMem2 = 0x90000000u;
    static const u32 kMem2Size = 0x04000000u;

    // Maps the image and the arena. False (with a message printed) if the
    // address ranges cannot be had.
    bool init();

    // An all-zero image and an empty arena.
    void clear();
    // Only an empty arena (the image is kept: a loaded dump stays).
    void clearArena();

    // Loads a dump: `mem1` is 24 MiB from 0x80000000; `mem2`, if not NULL,
    // 64 MiB from 0x90000000 (MEM2 is zero without it). False with the
    // reason in `why`.
    bool loadDump(const char* mem1, const char* mem2, char* why, u32 whyCap);

    extern u32 g_arenaStart;
    extern u32 g_arenaEnd;

    inline bool inImage(u32 a)
    {
        return (a >= kMem1 && a < kMem1 + kMem1Size) || (a >= kMem2 && a < kMem2 + kMem2Size);
    }
    inline bool inArena(u32 a)
    {
        return a >= g_arenaStart && a < g_arenaEnd;
    }

    // The image by game address.
    inline u32 word(u32 addr)
    {
        return *reinterpret_cast<const u32*>(addr);
    }
    inline void setWord(u32 addr, u32 value)
    {
        *reinterpret_cast<u32*>(addr) = value;
    }
    inline float real(u32 addr)
    {
        return *reinterpret_cast<const float*>(addr);
    }
    inline void setReal(u32 addr, float value)
    {
        *reinterpret_cast<float*>(addr) = value;
    }
    inline u16 half(u32 addr)
    {
        return *reinterpret_cast<const u16*>(addr ^ 2);
    }
    inline void setHalf(u32 addr, u16 value)
    {
        *reinterpret_cast<u16*>(addr ^ 2) = value;
    }
    inline u8 byte(u32 addr)
    {
        return *reinterpret_cast<const u8*>(addr ^ 3);
    }
    inline void setByte(u32 addr, u8 value)
    {
        *reinterpret_cast<u8*>(addr ^ 3) = value;
    }

    // Zeroed memory from the arena (never given back before the next
    // clear). The run stops if the arena is full.
    void* alloc(u32 bytes, u32 align = 8);

    template <class T> T* make()
    {
        return new (alloc(sizeof(T), 8)) T();
    }
}
}
