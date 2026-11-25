#pragma once

#include "shared/interface.h"

// The one stub<->main interface instance. The stub's hooks read its callback
// slots; a loaded main module fills them.
extern "C" tm::iface::Interface g_tmDispatchTable;

namespace tm {
namespace dispatch {
    iface::Interface& table();

    // Nulls every callback slot (before the main module is detached, or when
    // it must no longer be called).
    void clearCallbacks();
}
}
