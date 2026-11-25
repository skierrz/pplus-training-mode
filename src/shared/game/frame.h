#pragma once

#include <types.h>

namespace tm {
namespace game {
    // The game's own frame counter (gfApplication+0x100): advanced once per
    // main-loop pass at 0x80017504, in every scene, about 60 times a second.
    u32 frameCounter();
}
}
