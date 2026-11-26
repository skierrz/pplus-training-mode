#include "extra/net_alloc.h"

#include <cstdarg>
#include <cstdio>
#include <ft/ft_common_data_accesser.h>
#include <ft/ft_entry_manager.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <st/st_utility.h>
#include <st/stage.h>
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
    namespace {
        // Single contiguous Heaps::Network slab for all resident extra buffers
        // (eliminates ~20 separate pool headers and prevents fragmenting
        // Heaps::Network against P+'s 0x10BA20 costume decompression buffer).
        enum {
            kArenaCap = 80 * 1024,
            kCostumeReserve = 0x110000
        };

        u8* s_arena = NULL;
        u32 s_arenaUsed = 0;
        u32 s_arenaCap = 0;
        u32 s_arenaRefs = 0;
    }

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
        if (s_arena == NULL && need <= kArenaCap)
        {
            game::HeapUsage u;
            u.largest = 0;
            u32 cap = kArenaCap;
            if (game::heapUsage(Heaps::Network, u) && u.largest >= cap + kCostumeReserve)
            {
                void* slab = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), cap, 32);
                if (game::isRam(slab))
                {
                    s_arena = static_cast<u8*>(slab);
                    s_arenaUsed = 0;
                    s_arenaCap = cap;
                    s_arenaRefs = 0;
                }
            }
        }
        if (s_arena != NULL && need <= s_arenaCap - s_arenaUsed)
        {
            void* p = s_arena + s_arenaUsed;
            s_arenaUsed += need;
            s_arenaRefs++;
            memset(p, 0, need);
            return p;
        }
        game::HeapUsage u;
        u.largest = 0;
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
        if (s_arena != NULL)
        {
            u32 a = reinterpret_cast<u32>(ptr);
            u32 lo = reinterpret_cast<u32>(s_arena);
            if (a >= lo && a < lo + s_arenaCap)
            {
                if (s_arenaRefs > 0)
                {
                    s_arenaRefs--;
                }
                if (s_arenaRefs == 0)
                {
                    free(s_arena);
                    s_arena = NULL;
                    s_arenaUsed = 0;
                    s_arenaCap = 0;
                }
                return;
            }
        }
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

    void freeNetSlot(void** slot)
    {
        if (slot != NULL && *slot != NULL)
        {
            freeNetRaw(*slot);
            *slot = NULL;
        }
    }

    u32 decompressLz77Ascii(const u8* src, u32 srcLen, char* dst, u32 dstCap)
    {
        return decompressLz77AsciiImpl(src, srcLen, dst, dstCap);
    }

    bool indexPackedStrings(const char* text, u32 len, u16* offsets, u32 count)
    {
        return indexPackedStringsImpl(text, len, offsets, count);
    }

    Stage* extraStage()
    {
        return g_Stage;
    }

    ftEntryManager* extraEntryManager()
    {
        return g_ftEntryManager;
    }

    const void* extraFtCommonData(int ftKind)
    {
        return g_ftCommonDataAccesser.getData(static_cast< ::ftKind>(ftKind));
    }

    float extraConstFloat(soModuleAccesser* acc, u32 id)
    {
        return soValueAccesser::getConstantFloat(acc, id, 0);
    }

    s32 extraConstInt(soModuleAccesser* acc, u32 id)
    {
        return soValueAccesser::getConstantInt(acc, id, 0);
    }

    bool extraRayCheck(Vec3f* pos, Vec3f* dir, Vec3f* hit, Vec3f* normal, bool passThrough, grCollision* col,
                       bool b2, u8 mask)
    {
        return stRayCheck(pos, dir, hit, normal, passThrough, col, b2, mask);
    }

    float i2f(s32 v)
    {
        return static_cast<float>(v);
    }

    float u2f(u32 v)
    {
        return static_cast<float>(v);
    }

    s32 f2i(float v)
    {
        return static_cast<s32>(v);
    }

    float i2fScale(s32 v, float s)
    {
        return static_cast<float>(v) * s;
    }

    float minf(float a, float b)
    {
        return a < b ? a : b;
    }

    float maxf(float a, float b)
    {
        return a > b ? a : b;
    }

    float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    const void* workBank(const soModuleEnumeration* m, u32 which)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return NULL;
        }
        const void* w = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, which));
        return game::isRam(w) ? w : NULL;
    }

    s32 workInt(const soModuleEnumeration* m, u32 which, u32 index)
    {
        const void* w = workBank(m, which);
        if (w == NULL || index >= game::wordAt(w, 0x10))
        {
            return 0;
        }
        const s32* ints = reinterpret_cast<const s32*>(game::wordAt(w, 0x0C));
        return game::isRam(ints) ? ints[index] : 0;
    }

    float workFloat(const soModuleEnumeration* m, u32 which, u32 index)
    {
        const void* w = workBank(m, which);
        if (w == NULL || index >= game::wordAt(w, 0x18))
        {
            return 0.0f;
        }
        const float* flts = reinterpret_cast<const float*>(game::wordAt(w, 0x14));
        return game::isRam(flts) ? flts[index] : 0.0f;
    }

    bool workBit(const soModuleEnumeration* m, u32 which, u32 index)
    {
        const void* w = workBank(m, which);
        u32 wordIdx = index >> 5;
        if (w == NULL || wordIdx >= game::wordAt(w, 0x20))
        {
            return false;
        }
        const u32* words = reinterpret_cast<const u32*>(game::wordAt(w, 0x1C));
        return game::isRam(words) && ((words[wordIdx] >> (index & 31u)) & 1u) != 0u;
    }
}
}
