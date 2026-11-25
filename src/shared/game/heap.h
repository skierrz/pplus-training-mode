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
    bool heapUsage(u32 id, HeapUsage& out);

    // The heap's name from g_HeapInfos, or "?" if it has none.
    const char* heapName(u32 id);

    // Logs one heap's size, free bytes, free-block count and largest block.
    void logHeap(Heaps::HeapType heap);
}
}
