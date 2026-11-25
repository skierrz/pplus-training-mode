// The plugin's setup, run once from _prolog: installs the render, pad,
// fighter input, set guard, boot target and scene hooks, then reads the
// appended modules (only if the scene hooks are in place).
#include <sy_core.h>

#include "shared/game/heap.h"
#include "shared/log.h"
#include "shared/version.h"
#include "stub/boot_target.h"
#include "stub/input_hook.h"
#include "stub/loader.h"
#include "stub/pad_hooks.h"
#include "stub/plugin.h"
#include "stub/render_hook.h"
#include "stub/scene_hooks.h"
#include "stub/set_guard.h"

#ifdef TM_EXPERIMENTS
#include "stub/experiments/module_log.h"
#endif

namespace tm {
namespace stub {
    // Version has a constructor, so META is filled in by a static
    // initializer; _prolog runs those before calling init().
    static const Syringe::PluginMeta META = {
        TM_PLUGIN_NAME,
        TM_PLUGIN_AUTHOR,
        Syringe::Version(TM_VERSION),
        Syringe::Version(SYRINGE_VERSION),
        NULL // PLUGIN_MAIN: unused; _prolog does the plugin's setup
    };

    const Syringe::PluginMeta* init(CoreApi* api)
    {
        TM_LOG("v" TM_VERSION " loaded\n");
        game::logHeap(Heaps::Syringe);
        installRenderHook(api);
        installPadHooks(api);
        installInputHook(api);
        installCursorHook(api);
        installSetGuard(api);
        installBootTarget(api);
        if (installSceneHooks(api))
        {
            loader::init();
        }
        else
        {
            TM_LOG("modules: scene hooks missing; none will be loaded\n");
        }
#ifdef TM_EXPERIMENTS
        experiments::logModuleLoads(api);
#endif
        return &META;
    }
}
}
