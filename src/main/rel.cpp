// Entry points of the main module, following the same pattern as
// src/stub/rel.cpp (adapted from SyriingePluginTemplate, see
// THIRD_PARTY_NOTICES.md). The stub links the module with OSLinkFixed, calls
// _prolog to learn what it is, and calls _epilog before unlinking it.

#include "main/module.h"

namespace tm {
namespace main {
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

    // Target for any references elf2rel could not resolve.
    void _unresolved()
    {
    }
}
}
