// Entry points of the extra module, the same pattern as src/main/rel.cpp
// (adapted from SyriingePluginTemplate, see THIRD_PARTY_NOTICES.md).

#include "extra/module.h"

namespace tm {
namespace extra {
    extern "C" {
    typedef void (*PFN_voidfunc)();
    // Linker-generated, null-terminated tables.
    __attribute__((section(".ctors"))) extern PFN_voidfunc _ctors[];
    __attribute__((section(".dtors"))) extern PFN_voidfunc _dtors[];

    const iface::ModuleInfo* _prolog();
    void _epilog();
    void _unresolved();
    }

    const iface::ModuleInfo* _prolog()
    {
        for (PFN_voidfunc* ctor = _ctors; *ctor; ctor++)
        {
            (*ctor)();
        }
        return &kModule;
    }

    void _epilog()
    {
        for (PFN_voidfunc* dtor = _dtors; *dtor; dtor++)
        {
            (*dtor)();
        }
    }

    // OSUnlink points calls into a module that is unlinked first here.
    void _unresolved()
    {
    }
}
}
