#pragma once

#include <types.h>

namespace tm {
namespace game {
    // The game's own frame counter (gfApplication+0x100): advanced once per
    // main-loop pass at 0x80017504, in every scene, about 60 times a second.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    u32 x_frameCounter();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline u32 frameCounter() { return x_frameCounter(); }
#else
    u32 frameCounter();
#endif
#else
    u32 frameCounter();
#endif
}
}
