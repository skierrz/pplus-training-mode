#pragma once

#include <sr/sr_common.h>
#include <types.h>

// Stand-in (PC build): a game heap is a counter of blocks and bytes kept by
// host/fakes.h (heapRoom, heapBlocks); the memory itself comes from the
// arena.
class gfHeapManager {
public:
    static void* getHeap(Heaps::HeapType heapType);
};
