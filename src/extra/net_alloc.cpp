#include "extra/net_alloc.h"

#include <cstdarg>
#include <cstdio>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <string.h>

#include "extra/lz77_ascii.h"
#include "main/services.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"
#include "shared/version.h"
#ifdef TM_PERF
#include "shared/perf.h"
#endif

namespace tm {
namespace extra {
    void logf(const char* fmt, ...)
    {
#ifdef TM_PERF
        perf::add(perf::kCountLogs, 1);
#endif
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        buf[sizeof(buf) - 1] = '\0';
        main::services().log("[" TM_PLUGIN_NAME "] %s", buf);
    }

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

    void* freeNetNull(void* ptr)
    {
        if (ptr != NULL)
        {
            freeNetRaw(ptr);
        }
        return NULL;
    }

    u32 decompressLz77Ascii(const u8* src, u32 srcLen, char* dst, u32 dstCap)
    {
        return decompressLz77AsciiImpl(src, srcLen, dst, dstCap);
    }

    bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count)
    {
        return indexPackedStringsImpl(text, len, offsets, count);
    }
}
}
