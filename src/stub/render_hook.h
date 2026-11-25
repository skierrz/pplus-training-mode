#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Hooks the end of the game's frame drawing. Each call logs scene changes
    // and runs the main module's render callback, if one is attached. Logs
    // and leaves the game untouched if the site does not hold the expected code.
    bool installRenderHook(CoreApi* api);
}
}
