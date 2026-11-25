#include "main/cpu/cpu.h"

#include "main/cpu/driver.h"
#include "main/cpu/options.h"
#include "main/cpu/script_source.h"

namespace tm {
namespace cpu {
    void moduleAttach(iface::Interface* iface)
    {
        scriptAttach();
        optionsAttach();
        attach(scriptClaim, optionsClaim);
        iface->callbacks.fighterInput = onFighterInput;
    }

    void moduleDetach()
    {
        detach();
        optionsDetach();
        scriptDetach();
    }
}
}
