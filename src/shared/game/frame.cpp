#include "shared/game/frame.h"

#include <gf/gf_application.h>

namespace tm {
namespace game {
    u32 frameCounter()
    {
        if (g_gfApplication == NULL)
        {
            return 0;
        }
        return *reinterpret_cast<const u32*>(reinterpret_cast<const u8*>(g_gfApplication) + 0x100);
    }
}
}
