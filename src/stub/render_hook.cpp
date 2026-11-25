// The render hook: after each frame is drawn, logs scene changes, checks
// the loaded modules' scene and runs the main module's render callback.
#include "stub/render_hook.h"

#include <types.h>

#include "shared/game/code_menu.h"
#include "shared/game/frame.h"
#include "shared/game/heap.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "stub/dispatch.h"
#include "stub/hook.h"
#include "stub/hook_entry.h"
#include "stub/loader.h"
#include "stub/perf_hooks.h"

namespace tm {
namespace stub {
    // 0x8001792C is in the unnamed gfApplication method at 0x8001789C (no
    // symbol in RSBE01.lst), which gfApplication's main loop calls at
    // 0x80017468 for each rendered frame. By this point the frame has been
    // drawn (P+'s Code Menu hooks the instruction before, 0x80017928, from a
    // Gecko code); the copy to the external framebuffer comes after.
    // r0, LR, CR and the volatile FPRs are all dead here: the next
    // instructions are this `addi` and a `bl`.
    static const u32 kRenderSiteWords[] = {
        0x387E0118, // addi r3,r30,0x118
        0x48002B11  // bl 0x8001A440 (runs the callback list at gfApplication+0x118)
    };
    static const hook::Site kRenderSite = {"render", 0x8001792C, kRenderSiteWords, 2};

    static u32 s_calls = 0;
    static const char* s_lastScene = NULL;
    static bool s_codeMenuOpen = false;
    static u32 s_codeMenuSkipped = 0;

    extern "C" void tmRenderHookBody()
    {
#ifdef TM_PERF
        u32 perfStart = perfRenderEnter();
#endif
        s_calls++;
        if (s_calls == 1)
        {
            TM_LOG("render hook: call 1, frame counter %u, site still hooked: %s\n",
                   game::frameCounter(), hook::stillInstalled(kRenderSite) ? "yes" : "no");
            // Every plugin has been loaded by now: this is the real budget.
            game::logHeap(Heaps::Syringe);
        }
        const char* scene = game::sceneName();
        if (scene != s_lastScene)
        {
            s_lastScene = scene;
            TM_LOG("scene: %s / %s at render call %u\n", scene, game::sequenceName(), s_calls);
        }
        loader::checkScene();
        void (*render)(u32) = dispatch::table().callbacks.render;
        bool cmOpen = game::codeMenuOpen();
        if (cmOpen || s_codeMenuOpen)
        {
            if (cmOpen && !s_codeMenuOpen)
            {
                s_codeMenuOpen = true;
                s_codeMenuSkipped = 0;
                if (render != NULL)
                {
                    TM_LOG("render hook: code menu open, draw skipped\n");
                }
            }
            if (render != NULL)
            {
                s_codeMenuSkipped++;
            }
            if (!cmOpen)
            {
                // The first render pass after state leaves 4 is Print Code
                // Menu's restore frame (savedFov != 0, CodeMenu.asm:273-292):
                // it drew a black quad and skipped gfTaskScheduler::render.
                if (s_codeMenuSkipped > 0)
                {
                    TM_LOG("render hook: code menu closed, %u draw(s) skipped\n", s_codeMenuSkipped);
                }
                s_codeMenuOpen = false;
                s_codeMenuSkipped = 0;
            }
        }
        else if (render != NULL && !perfBypassed())
        {
            render(s_calls);
        }
        if (s_calls == 120)
        {
            TM_LOG("render hook: call 120, frame counter %u, scene %s\n",
                   game::frameCounter(), game::sceneName());
        }
#ifdef TM_PERF
        perfRenderExit(perfStart);
#endif
    }

    static asm void renderEntry()
    {
        nofralloc
        lis r12, tmRenderHookBody@h
        ori r12, r12, tmRenderHookBody@l
        b tmPreservingCall
    }

    bool installRenderHook(CoreApi* api)
    {
        return hook::installInline(api, kRenderSite, renderEntry);
    }
}
}
