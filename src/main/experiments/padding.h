#pragma once

#include <types.h>

// EXPERIMENTS=1 only: `make EXPERIMENTS=1 PAD_KIB=n` adds n KiB of read-only
// filler to the main module, so loading it can be tested at that size.
namespace tm {
namespace experiments {
    // Logs the filler's size and address once, at attach.
    void logPadding();

    // Reads one filler byte per call, so the linker keeps the array.
    void touchPadding(u32 call);
}
}
