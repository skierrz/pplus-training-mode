#pragma once

#include <types.h>

namespace tm {
namespace game {
    // The name of a status kind ("Wait", "GuardDamage", "SpecialN"), or NULL
    // for a fighter's own statuses past the shared ones.
    const char* statusName(int status);

    // Its name, or for a fighter's own status its number ("0x11A").
    void statusText(int status, char* buf, u32 size);
}
}
