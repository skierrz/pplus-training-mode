#pragma once

class CoreApi;

namespace tm {
namespace stub {
    // Hooks the controller update in sora_melee once that module loads; from
    // then on every fighter input object is handed to the module's
    // fighterInput callback once per frame. False if it could not be arranged.
    bool installInputHook(CoreApi* api);

    // Hooks Fighter::processFixCamera in sora_melee once that module loads;
    // hands each fighter to fighterFixCamera after its per-frame cursor
    // update. False if it could not be arranged.
    bool installCursorHook(CoreApi* api);
}
}
