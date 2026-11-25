#include "stub/dispatch.h"

#include <OS/OSError.h>

namespace tm {
namespace dispatch {
    // Lives as long as the stub (the whole session), so a module's settings
    // survive it being unloaded between matches. Zero at boot.
    static u32 s_settings[iface::kSettingsBlockSize / sizeof(u32)];

    static u8* settingsBlock()
    {
        return reinterpret_cast<u8*>(s_settings);
    }
}
}

extern "C" tm::iface::Interface g_tmDispatchTable = {
    tm::iface::kVersion,
    sizeof(tm::iface::Interface),
    {OSReport, tm::dispatch::settingsBlock},
    {NULL, NULL, NULL, NULL, NULL}
};

namespace tm {
namespace dispatch {
    iface::Interface& table()
    {
        return g_tmDispatchTable;
    }

    void clearCallbacks()
    {
        g_tmDispatchTable.callbacks.render = NULL;
        g_tmDispatchTable.callbacks.pads = NULL;
        g_tmDispatchTable.callbacks.fighterPad = NULL;
        g_tmDispatchTable.callbacks.fighterInput = NULL;
        g_tmDispatchTable.callbacks.fighterFixCamera = NULL;
    }
}
}
