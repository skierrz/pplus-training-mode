// Adapted from SyriingePluginTemplate source/rel.cpp @ 2b3ba22 (MIT, see THIRD_PARTY_NOTICES.md).
//
// REL module entry points. Syriinge calls _prolog when it loads the plugin
// and uses the returned PluginMeta to identify it.

#include <sy_core.h>

#include "stub/plugin.h"

namespace Syringe {
    extern "C" {
    typedef void (*PFN_voidfunc)();
    // Linker-generated, null-terminated tables. The section attributes only
    // document intent: MWCC emits identical output with or without them.
    __attribute__((section(".ctors"))) extern PFN_voidfunc _ctors[];
    __attribute__((section(".dtors"))) extern PFN_voidfunc _dtors[];

    const PluginMeta* _prolog(CoreApi* api);
    void _epilog();
    void _unresolved();
    }

    const PluginMeta* _prolog(CoreApi* api)
    {
        // Run static constructors (null-terminated list from the linker).
        for (PFN_voidfunc* ctor = _ctors; *ctor; ctor++)
        {
            (*ctor)();
        }

        return tm::stub::init(api);
    }

    // .dtors is empty unless a file-scope object with a destructor exists; then
    // the linker adds one entry, __destroy_global_chain (see global_chain.cpp).
    // Syriinge never unloads plugins (its Plugin::unloadPlugin, source/
    // plugin.cpp, has no caller and would not call _epilog), so this never
    // runs.
    void _epilog()
    {
        for (PFN_voidfunc* dtor = _dtors; *dtor; dtor++)
        {
            (*dtor)();
        }
    }

    // Target for any references elf2rel could not resolve.
    void _unresolved()
    {
    }
}
