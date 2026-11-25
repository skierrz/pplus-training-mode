#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Makes the game boot to Training's character select where Project+
    // would boot to Versus'. Holding X or Y on a GameCube controller at boot
    // keeps Versus. Changes nothing if P+'s boot code or the game's differ
    // from what is expected.
    void installBootTarget(CoreApi* api);
}
}
