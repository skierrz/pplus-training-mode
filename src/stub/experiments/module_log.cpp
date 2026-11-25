#include "stub/experiments/module_log.h"

#include <gf/gf_memory_pool.h>
#include <gf/gf_module.h>
#include <sy_core.h>

#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/log.h"

namespace tm {
namespace experiments {
    enum {
        kHeaps = 0x47,
        kInfoName = 0x1A, // gfModuleInfo: module name, NUL-terminated (Brawllib layout)
        kNameMax = 32
    };

    // The g_HeapInfos index whose pool is `heap`, or -1.
    static int heapIndex(const void* heap)
    {
        for (int id = 0; id < kHeaps; id++)
        {
            if (g_HeapInfos[id].m_memoryPool == heap)
            {
                return id;
            }
        }
        return -1;
    }

    static void onModuleLoaded(gfModuleInfo* info)
    {
        if (!game::isRam(info) || !game::isRam(info->m_module) || !game::isRam(info->m_module->m_header))
        {
            TM_LOG("modload: unreadable module info %08x\n", info);
            return;
        }
        char name[kNameMax + 1];
        const char* raw = reinterpret_cast<const char*>(info) + kInfoName;
        int n = 0;
        for (; n < kNameMax && raw[n] >= 0x20 && raw[n] < 0x7F; n++)
        {
            name[n] = raw[n];
        }
        name[n] = '\0';
        const gfModuleHeader* header = info->m_module->m_header;
        int heap = heapIndex(info->m_heap);
        TM_LOG("modload: id %u %s at %08x, heap %d %s, in %s\n", header->info.id, name, header,
               heap, heap >= 0 ? game::heapName(heap) : "?", game::sceneName());
    }

    void logModuleLoads(CoreApi* api)
    {
        api->moduleLoadEventSubscribe(onModuleLoaded);
    }
}
}
