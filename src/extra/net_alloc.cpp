#include "extra/net_alloc.h"

#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <string.h>

#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    void* allocNetZero(u32 bytes)
    {
        u32 need = (bytes + 31u) & ~31u;
        if (need == 0)
        {
            return NULL;
        }
        game::HeapUsage u;
        if (!game::heapUsage(Heaps::Network, u) || u.largest < need)
        {
            TM_LOG("extra: net_alloc refused %u B (largest %x)\n", need, u.largest);
            return NULL;
        }
        void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), need, 32);
        if (!game::isRam(p))
        {
            TM_LOG("extra: net_alloc failed %u B\n", need);
            return NULL;
        }
        memset(p, 0, need);
        return p;
    }

    void freeNetRaw(void* ptr)
    {
        if (game::isRam(ptr))
        {
            free(ptr);
        }
    }
}
}
