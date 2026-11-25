#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Hooks gfPadSystem where it has just refreshed the debug pads (once per
    // main-loop pass) and the game pads (once per game step), and hands each
    // array to the module's pads callback before anything reads it; and
    // ipSwitch where it is about to turn a port's pad into a player's
    // controls, for the module's fighterPad callback. Returns true only if
    // all three hooks are in place.
    bool installPadHooks(CoreApi* api);
}
}
