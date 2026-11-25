#pragma once

#include <types.h>

// Stand-in (PC build), see gf/gf_heap_manager.h. NULL when the heap has no
// room for the block. `free` (host_prefix.h) gives a block back.
class gfMemoryPool {
public:
    static void* alloc(void* heapAddr, size_t size, int align);
};
