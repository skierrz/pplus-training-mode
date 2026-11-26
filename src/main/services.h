#pragma once

#include "shared/interface.h"

namespace tm {
namespace main {
    // The stub's services while attached; before attach and after detach, a
    // set whose functions do nothing.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    const iface::Services& x_services();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline const iface::Services& services() { return x_services(); }
#else
    const iface::Services& services();
#endif
#else
    const iface::Services& services();
#endif
}
}
