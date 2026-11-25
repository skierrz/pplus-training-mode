#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Hooks gfSceneManager::process just before it calls the current scene's
    // start() and exit(), and hands both moments to the loader. Returns true
    // only if both hooks are in place.
    bool installSceneHooks(CoreApi* api);
}
}
