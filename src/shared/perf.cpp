// Time measurements (shared/perf.h), built only with `make PERF=1`.
#ifdef TM_PERF
#include "shared/perf.h"

#include <stddef.h>
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#endif

#include "shared/log.h"

namespace tm {
namespace perf {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
    // Allocated in Heaps::Network on attach so PERF=1 keeps full MEM1 match headroom.
    static Block* s_blockPtr = NULL;
#else
    static Block s_block;
    static Block* s_blockPtr = &s_block;
#endif

    // The sections' names, in the order of their enums, for the script.
#ifdef TM_MODULE_STUB
    const char* const kStubNames[kStubSections] = {
        "frame", "busy", "render", "pads_system", "pads_game", "fighter_pad", "fighter_input",
        "top_to_game", "game_to_render", "render_to_top"};
#endif
#ifdef TM_MODULE_MAIN
    const char* const kMainNames[kMainSections] = {
        "tracker_handover", "tracker_step", "advantage", "frame_data", "techniques", "character_rng",
        "savestate_render", "lab_render", "render3d", "di_draw", "heatmap_draw", "grab_viz_draw",
        "osd_draw", "info_draw", "ledge_hud",
        "menu_draw", "watermark", "debug_overlay", "record_hud", "extra_render", "match_hud",
        "storage", "pads_damage", "pads_savestate", "pads_lab", "pads_record", "input_cpu",
        "input_savestate", "input_lab", "input_record", "fighter_pad_lab", "save_capture",
        "save_fingerprint", "save_wait_gpu", "save_write", "save_check", "save_measure_v3",
        "load_restore", "load_fingerprint", "load_check", "load_wait_gpu", "load_copy", "load_flush",
        "n_rects", "n_texts", "n_chars", "n_widths", "n_logs"};
#endif

    void attach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        if (s_blockPtr != NULL)
        {
            return;
        }
        u32 need = (static_cast<u32>(sizeof(Block)) + 31u) & ~31u;
        game::HeapUsage u;
        if (game::heapUsage(Heaps::Network, u) && u.largest >= need)
        {
            void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), need, 32);
            if (game::isRam(p))
            {
                memset(p, 0, need);
                s_blockPtr = static_cast<Block*>(p);
            }
        }
        if (s_blockPtr == NULL)
        {
            TM_LOG("perf: main block alloc failed (%u B in Network)\n", need);
        }
#endif
    }

    void detach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        if (s_blockPtr != NULL)
        {
            free(s_blockPtr);
            s_blockPtr = NULL;
        }
#endif
    }

    Block* blockPtr()
    {
        return s_blockPtr;
    }

    bool legacy()
    {
        return s_blockPtr != NULL && (s_blockPtr->control & kControlLegacy) != 0;
    }

#ifndef TM_MODULE_MAIN
    Block& block()
    {
        return s_block;
    }
#endif

    Scope::Scope(u32 section) : m_section(section), m_start(OSGetTick())
    {
    }

    Scope::~Scope()
    {
        add(m_section, OSGetTick() - m_start);
    }

    void add(u32 section, u32 ticks)
    {
        if (s_blockPtr == NULL)
        {
            return;
        }
        Section& s = s_blockPtr->s[section];
        s.calls++;
        s.ticks += ticks;
        s.frameTicks += ticks;
        if (ticks > s.worstCall)
        {
            s.worstCall = ticks;
        }
    }

    void frameEnd()
    {
        if (s_blockPtr == NULL)
        {
            return;
        }
        for (u32 i = 0; i < s_blockPtr->count; i++)
        {
            Section& s = s_blockPtr->s[i];
            if (s.frameTicks > s.worstFrame)
            {
                s.worstFrame = s.frameTicks;
            }
            s.frameTicks = 0;
        }
        s_blockPtr->frames++;
    }

    void announce(const char* who, const char* const* names, u32 count)
    {
        if (s_blockPtr == NULL)
        {
            return;
        }
        s_blockPtr->magic = kMagic;
        s_blockPtr->count = count;
        // One line the script parses: the address, then the names in order.
        char line[1024];
        u32 n = 0;
        for (u32 i = 0; i < count && n + 24 < sizeof(line); i++)
        {
            for (const char* p = names[i]; *p != '\0' && n + 2 < sizeof(line); p++)
            {
                line[n++] = *p;
            }
            line[n++] = ' ';
        }
        line[n] = '\0';
        TM_LOG("perf: %s block at %08x, %u sections: %s\n", who, s_blockPtr, count, line);
    }
}
}
#endif
