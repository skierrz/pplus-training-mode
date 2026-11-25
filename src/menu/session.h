#pragma once

#include <types.h>

#include "shared/drill_session.h"

// What the training menu remembers between loads (one per visit to Training's
// character select) until the game is switched off: the shared
// drills::Session at the end of the stub's settings block.
namespace tm {
namespace menu {
    typedef drills::Session Session;

    // The session in the stub's block (reset if the block does not hold one);
    // NULL if the stub handed over no block. Valid while attached.
    Session* attachSession(u8* block);
    Session& session();
}
}
