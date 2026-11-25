#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Keeps a match from giving a new character the fighter heap set whose
    // code heap holds the extra module (see set_guard.cpp). Call from _prolog.
    bool installSetGuard(CoreApi* api);
}
}
