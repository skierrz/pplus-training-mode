#pragma once

#include <sy_core.h>

namespace tm {
namespace stub {
    // Called once from _prolog after static constructors have run.
    // Returns the metadata block that _prolog hands back to Syriinge.
    const Syringe::PluginMeta* init(CoreApi* api);
}
}
