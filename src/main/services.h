#pragma once

#include "shared/interface.h"

namespace tm {
namespace main {
    // The stub's services while attached; before attach and after detach, a
    // set whose functions do nothing.
    const iface::Services& services();
}
}
