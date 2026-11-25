#pragma once

#include "shared/version.h"

// Writes one line to the game's debug log (OSReport), tagged with the plugin
// name. The format must be a string literal and should end in "\n".
// The modules log through the stub's log service (see interface.h); the
// extra module through the match module's services(), which it links to.
#if (defined(TM_MODULE_MAIN) || defined(TM_MODULE_EXTRA)) && defined(TM_PERF)
#include "main/services.h"
#include "shared/perf.h"
// Counted (shared/perf.h): a line costs its formatting on a console.
#define TM_LOG(...) \
    (::tm::perf::add(::tm::perf::kCountLogs, 1), ::tm::main::services().log("[" TM_PLUGIN_NAME "] " __VA_ARGS__))
#elif defined(TM_MODULE_MAIN) || defined(TM_MODULE_EXTRA)
#include "main/services.h"
#define TM_LOG(...) ::tm::main::services().log("[" TM_PLUGIN_NAME "] " __VA_ARGS__)
#elif defined(TM_MODULE_MENU)
#include "menu/services.h"
#define TM_LOG(...) ::tm::menu::services().log("[" TM_PLUGIN_NAME "] " __VA_ARGS__)
#else
#include <OS/OSError.h>
#define TM_LOG(...) OSReport("[" TM_PLUGIN_NAME "] " __VA_ARGS__)
#endif
