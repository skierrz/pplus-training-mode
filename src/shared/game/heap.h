#pragma once

#include <sr/sr_common.h>
#include <types.h>

namespace tm {
namespace game {
    // One gfMemoryPool, read from its header and free list.
    struct HeapUsage {
        u32 start;      // first and one-past-last address the pool manages
        u32 end;
        u32 freeBytes;  // sum of free blocks, headers excluded
        u32 freeBlocks;
        u32 largest;    // largest free block, header excluded
        u32 allocated;  // allocated blocks now, and the most there ever were
        u32 peakAllocated;
        u32 used;       // bytes in allocated blocks (headers included), now and peak
        u32 peakUsed;
    };

    // Fills `out` for heap `id` (an index into g_HeapInfos); false if that
    // heap does not exist right now.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_heapUsage(u32 id, HeapUsage& out);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool heapUsage(u32 id, HeapUsage& out) { return x_heapUsage(id, out); }
#else
    bool heapUsage(u32 id, HeapUsage& out);
#endif
#else
    bool heapUsage(u32 id, HeapUsage& out);
#endif

    // The heap's name from g_HeapInfos, or "?" if it has none.
    const char* heapName(u32 id);

    // Logs one heap's size, free bytes, free-block count and largest block.
    void logHeap(Heaps::HeapType heap);

#ifdef __MWERKS__
    // Shared MAIN-resident Heaps::Network allocator and compact FP/mem helpers
    // (exported from MAIN; stub/menu do not link these).
    void* allocNetworkZero(u32 bytes);
    void freeNetworkSlot(void** slot);
    float i2f(s32 v);
    float u2f(u32 v);
    s32 f2i(float v);
    float i2fScale(s32 v, float s);
    float minf(float a, float b);
    float maxf(float a, float b);
    float clampf(float v, float lo, float hi);
    float floatAt(const void* base, u32 offset);
#else
    inline float i2f(s32 v) { return static_cast<float>(v); }
    inline float u2f(u32 v) { return static_cast<float>(v); }
    inline s32 f2i(float v) { return static_cast<s32>(v); }
    inline float i2fScale(s32 v, float s) { return static_cast<float>(v) * s; }
    inline float minf(float a, float b) { return a < b ? a : b; }
    inline float maxf(float a, float b) { return a > b ? a : b; }
    inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    inline float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(static_cast<const u8*>(base) + offset);
    }
#endif
}
}
