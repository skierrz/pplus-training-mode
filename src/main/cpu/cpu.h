#pragma once

#include "shared/interface.h"

// Entry points of the CPU control code for the module (module.cpp).
namespace tm {
namespace cpu {
    // Sets up the driver, the harness script source and the CPU Options,
    // and fills the fighter-input callback.
    void moduleAttach(iface::Interface* iface);
    // Releases every driven slot (AI flags put back).
    void moduleDetach();
}
}
