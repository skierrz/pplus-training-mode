#include "shared/game/heap.h"

#include <gf/gf_memory_pool.h>

#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace game {
    // gfMemoryPool internals, read from the game's own getMaxFreeBlockSize
    // (0x8002606C), alloc (0x80025C58) and pool dump (0x800260B4):
    //  pool +0x4 start, +0x8 end, +0x14 u16 allocated blocks, +0x16 u16 peak
    //  of that, +0x18 peak of +0x1C, +0x1C bytes in allocated blocks,
    //  +0x20 -> sentinel block.
    // Free blocks form a list through block+0x8, starting at the sentinel's,
    // in address order and wrapping back to the sentinel. Each block starts
    // with a 0x20-byte header whose +0x4 is the block size including it.
    enum {
        kPoolStart = 0x4,
        kPoolEnd = 0x8,
        kPoolAllocated = 0x14,
        kPoolPeakAllocated = 0x16,
        kPoolPeakUsed = 0x18,
        kPoolUsed = 0x1C,
        kPoolSentinel = 0x20,
        kBlockSize = 0x4,
        kBlockNext = 0x8,
        kBlockHeader = 0x20,
        kMaxBlocks = 512
    };

    static u32 field(u32 address, u32 offset)
    {
        return *reinterpret_cast<const u32*>(address + offset);
    }

    static u32 half(u32 address, u32 offset)
    {
        return *reinterpret_cast<const u16*>(address + offset);
    }

    bool heapUsage(u32 id, HeapUsage& out)
    {
        u32 pool = reinterpret_cast<u32>(g_HeapInfos[id].m_memoryPool);
        if (pool == 0)
        {
            return false;
        }
        out.start = field(pool, kPoolStart);
        out.end = field(pool, kPoolEnd);
        out.freeBytes = 0;
        out.freeBlocks = 0;
        out.largest = 0;
        u32 block = field(field(pool, kPoolSentinel), kBlockNext);
        while (block >= out.start && block < out.end && out.freeBlocks < kMaxBlocks)
        {
            u32 bytes = field(block, kBlockSize) - kBlockHeader;
            out.freeBytes += bytes;
            out.freeBlocks++;
            if (bytes > out.largest)
            {
                out.largest = bytes;
            }
            u32 next = field(block, kBlockNext);
            if (next <= block)
            {
                break;
            }
            block = next;
        }
        out.allocated = half(pool, kPoolAllocated);
        out.peakAllocated = half(pool, kPoolPeakAllocated);
        out.used = field(pool, kPoolUsed);
        out.peakUsed = field(pool, kPoolPeakUsed);
        return true;
    }

    const char* heapName(u32 id)
    {
        const char* name = g_HeapInfos[id].m_name;
        return isRam(name) ? name : "?";
    }

    void logHeap(Heaps::HeapType heap)
    {
        HeapUsage usage;
        if (!heapUsage(heap, usage))
        {
            TM_LOG("heap %d: not created\n", heap);
            return;
        }
        TM_LOG("heap %s: size 0x%x, free 0x%x in %u block(s), largest free 0x%x\n",
               heapName(heap), g_HeapInfos[heap].m_size, usage.freeBytes,
               usage.freeBlocks, usage.largest);
    }
}
}
