#pragma once

#include <types.h>

#include "stub/hook.h"

class CoreApi;

// Inline hooks in REL modules that are not loaded yet when the plugin loads
// (sora_melee is linked right after Syriinge has loaded every plugin).
namespace tm {
namespace hook {
    // `offset` is from the start of the module's .text section.
    struct ModuleSite {
        const char* name;
        u32 moduleId;
        u32 offset;
        const u32* expected;
        u32 count;
    };

    // Arranges for `entry` to be inline-hooked at `site` each time its module
    // is loaded. At that moment the words are checked and the hook installed
    // exactly as installInline does, so a module whose code differs is left
    // untouched. Call from _prolog. Returns false if no slot is left.
    bool installInlineOnModuleLoad(CoreApi* api, const ModuleSite& site, void (*entry)());

    // Address the site was hooked at on the module's last load, or 0 if the
    // module has not loaded or the hook was refused.
    u32 moduleSiteAddress(const ModuleSite& site);

    // True while the site of the module's last load still branches to our stub.
    bool moduleSiteInstalled(const ModuleSite& site);
}
}
