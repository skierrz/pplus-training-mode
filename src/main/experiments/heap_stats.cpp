#include "main/experiments/heap_stats.h"

#include <gf/gf_memory_pool.h>

#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/log.h"

namespace tm {
namespace experiments {
    enum {
        kHeaps = 0x47,        // entries gfHeapManager::dumpAll walks
        kLoadDumpDelay = 30,  // render calls after the module is attached
        kMatchDumpDelay = 120,
        kPauseDumpDelay = 30
    };

    struct SceneMin {
        const char* name; // kept: a heap's entry is cleared when it goes
        u32 pool;
        u32 size;
        u32 minFree;
        u32 minLargest;
        bool seen;
        bool recreated;
    };

    static SceneMin s_min[kHeaps];
    static const char* s_scene = NULL;
    static u32 s_loadDumpAt = 0;
    static u32 s_matchDumpAt = 0;
    static u32 s_pauseDumpAt = 0;
    static game::MatchState s_last = {false, false, false};

    static void dumpTable(const char* label, const char* detail, u32 call)
    {
        TM_LOG("heaps [%s%s] at render call %u in %s: id name arena start-end size free largest "
               "free-blocks allocated/peak used/peak\n",
               label, detail, call, game::sceneName());
        for (u32 id = 0; id < kHeaps; id++)
        {
            game::HeapUsage u;
            if (!game::heapUsage(id, u))
            {
                continue;
            }
            // Pool byte 0x12: what gfMemoryPool::alloc (0x80025C58) does when
            // no block fits: bit 0x01 logs and dumps the pool, bit 0x10 then
            // halts (PPCHalt); with neither it returns NULL silently.
            u8 failFlags = *reinterpret_cast<const u8*>(reinterpret_cast<u32>(g_HeapInfos[id].m_memoryPool) + 0x12);
            TM_LOG("heaps [%s%s] %2u %-20s %s %08x-%08x size %7x free %7x largest %7x fb %3u "
                   "alloc %4u/%4u used %7x/%7x fail %02x\n",
                   label, detail, id, game::heapName(id), u.start >= 0x90000000 ? "MEM2" : "MEM1",
                   u.start, u.end, g_HeapInfos[id].m_size, u.freeBytes, u.largest, u.freeBlocks,
                   u.allocated, u.peakAllocated, u.used, u.peakUsed, failFlags);
        }
    }

    // Least free space per heap over the scene's render calls. Allocations
    // made and freed between two frames are missed here; the pool's own
    // peak-used counter (logged alongside) catches those.
    static void sampleMins()
    {
        for (u32 id = 0; id < kHeaps; id++)
        {
            SceneMin& m = s_min[id];
            game::HeapUsage u;
            if (!game::heapUsage(id, u))
            {
                continue;
            }
            u32 pool = reinterpret_cast<u32>(g_HeapInfos[id].m_memoryPool);
            if (!m.seen)
            {
                m.name = game::heapName(id);
                m.pool = pool;
                m.size = g_HeapInfos[id].m_size;
                m.minFree = u.freeBytes;
                m.minLargest = u.largest;
                m.seen = true;
                continue;
            }
            if (pool != m.pool || g_HeapInfos[id].m_size != m.size)
            {
                m.pool = pool;
                m.size = g_HeapInfos[id].m_size;
                m.recreated = true;
            }
            if (u.freeBytes < m.minFree)
            {
                m.minFree = u.freeBytes;
            }
            if (u.largest < m.minLargest)
            {
                m.minLargest = u.largest;
            }
        }
    }

    static void logMinsAndReset()
    {
        for (u32 id = 0; id < kHeaps; id++)
        {
            SceneMin& m = s_min[id];
            game::HeapUsage u;
            if (m.seen && game::heapUsage(id, u))
            {
                TM_LOG("heapmin [%s] %2u %-20s size %7x min-free %7x min-largest %7x peak-used %7x%s\n",
                       s_scene, id, game::heapName(id), g_HeapInfos[id].m_size, m.minFree, m.minLargest,
                       u.peakUsed, m.recreated ? " (recreated)" : "");
            }
            else if (m.seen)
            {
                TM_LOG("heapmin [%s] %2u %-20s size %7x min-free %7x min-largest %7x (gone at scene end)\n",
                       s_scene, id, m.name, m.size, m.minFree, m.minLargest);
            }
            m.seen = false;
            m.recreated = false;
        }
        TM_LOG("heapmin [%s] ended at detach\n", s_scene);
    }

    void heapStatsOnRender(u32 call, const game::MatchState& match)
    {
        if (s_scene == NULL)
        {
            s_scene = game::sceneName();
            s_loadDumpAt = call + kLoadDumpDelay;
        }
        sampleMins();

        if (match.running && !s_last.running)
        {
            s_matchDumpAt = call + kMatchDumpDelay;
        }
        if (match.paused && !s_last.paused)
        {
            s_pauseDumpAt = call + kPauseDumpDelay;
        }
        s_last = match;

        if (call == s_loadDumpAt)
        {
            dumpTable("loaded ", s_scene, call);
        }
        if (call == s_matchDumpAt && match.running)
        {
            dumpTable("match running 2s", "", call);
        }
        if (call == s_pauseDumpAt && match.paused)
        {
            dumpTable("paused", "", call);
        }
    }

    void heapStatsOnDetach()
    {
        if (s_scene != NULL)
        {
            logMinsAndReset();
        }
    }
}
}
