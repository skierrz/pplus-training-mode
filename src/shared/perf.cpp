// Time measurements (shared/perf.h), built only with `make PERF=1`.
#ifdef TM_PERF
#include "shared/perf.h"

#include "shared/log.h"

namespace tm {
namespace perf {
    static Block s_block;

    // The sections' names, in the order of their enums, for the script.
    const char* const kStubNames[kStubSections] = {
        "frame", "busy", "render", "pads_system", "pads_game", "fighter_pad", "fighter_input",
        "top_to_game", "game_to_render", "render_to_top"};
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


    Block& block()
    {
        return s_block;
    }

    void add(u32 section, u32 ticks)
    {
        Section& s = s_block.s[section];
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
        for (u32 i = 0; i < s_block.count; i++)
        {
            Section& s = s_block.s[i];
            if (s.frameTicks > s.worstFrame)
            {
                s.worstFrame = s.frameTicks;
            }
            s.frameTicks = 0;
        }
        s_block.frames++;
    }

    void announce(const char* who, const char* const* names, u32 count)
    {
        s_block.magic = kMagic;
        s_block.count = count;
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
        TM_LOG("perf: %s block at %08x, %u sections: %s\n", who, &s_block, count, line);
    }
}
}
#endif
