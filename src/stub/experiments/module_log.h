#pragma once

class CoreApi;

// EXPERIMENTS=1 only: logs every module the game loads, with the heap it went
// into, to show what competes for the main module's heap.
namespace tm {
namespace experiments {
    void logModuleLoads(CoreApi* api);
}
}
