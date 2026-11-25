#pragma once

#include "shared/game/menu_sound.h"

namespace tm {
namespace menu {
    // Plays one of the game's menu sounds (game::MenuSound) and logs its id:
    // move for navigation, confirm for selection, back for cancel, and error
    // for unavailable actions.
    void sound(int id);
}
}
