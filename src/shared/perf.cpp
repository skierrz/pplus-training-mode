// Time measurements (shared/perf.h), built only with `make PERF=1`.
#ifdef TM_PERF
#include "shared/perf.h"

#include <stddef.h>
#ifdef __MWERKS__
#include <OS/OSError.h>
#endif
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

    // NUL-separated section names in the order of their enums (zero pointer relocs).
#ifdef TM_MODULE_STUB
    const char kStubNames[] =
        "frame\0busy\0render\0pads_system\0pads_game\0fighter_pad\0fighter_input\0"
        "top_to_game\0game_to_render\0render_to_top";
#endif
#ifdef TM_MODULE_MAIN
    const char kMainNames[] =
        "tracker_handover\0tracker_step\0advantage\0frame_data\0techniques\0character_rng\0"
        "savestate_render\0lab_render\0render3d\0di_draw\0heatmap_draw\0grab_viz_draw\0"
        "osd_draw\0info_draw\0ledge_hud\0"
        "menu_draw\0watermark\0debug_overlay\0record_hud\0extra_render\0match_hud\0"
        "storage\0pads_damage\0pads_savestate\0pads_lab\0pads_record\0input_cpu\0"
        "input_savestate\0input_lab\0input_record\0fighter_pad_lab\0save_capture\0"
        "save_fingerprint\0save_wait_gpu\0save_write\0save_check\0save_measure_v3\0save_max_irq_off\0"
        "load_restore\0load_fingerprint\0load_check\0load_wait_gpu\0load_copy\0load_flush\0"
        "load_max_irq_off\0"
        "n_rects\0n_texts\0n_chars\0n_widths\0n_logs";
#endif

    void attach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        if (s_blockPtr == NULL)
        {
            s_blockPtr = static_cast<Block*>(game::allocNetworkZero(static_cast<u32>(sizeof(Block))));
            if (s_blockPtr == NULL)
            {
                TM_LOG("perf: main block alloc failed (%u B in Network)\n", static_cast<unsigned>(sizeof(Block)));
            }
        }
#endif
    }

    void detach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_blockPtr));
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

    void announce(const char* who, const char* names, u32 count)
    {
        if (s_blockPtr == NULL)
        {
            return;
        }
        s_blockPtr->magic = kMagic;
        s_blockPtr->count = count;
        // One line the script parses: stream each section name directly via
        // OSReport so long section lists never truncate at logf's 512-byte cap.
#ifdef __MWERKS__
        OSReport("[" TM_PLUGIN_NAME "] perf: %s block at %08x, %u sections: ", who, s_blockPtr, count);
        const char* p = names;
        for (u32 i = 0; i < count; i++)
        {
            OSReport("%s ", p);
            while (*p++ != '\0') {}
        }
        OSReport("\n");
#else
        char line[1024];
        u32 n = 0;
        const char* p = names;
        for (u32 i = 0; i < count && n + 24 < sizeof(line); i++)
        {
            for (; *p != '\0' && n + 2 < sizeof(line); p++)
            {
                line[n++] = *p;
            }
            while (*p++ != '\0') {}
            line[n++] = ' ';
        }
        line[n] = '\0';
        TM_LOG("perf: %s block at %08x, %u sections: %s\n", who, s_blockPtr, count, line);
#endif
    }
}
}
#endif
