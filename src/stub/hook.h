#pragma once

#include <types.h>

class CoreApi;

namespace tm {
namespace hook {
    // A place in game code we patch, with the instruction words found there on
    // RSBE01 rev 2 + Project+ 3.2.1 (read from memory in the emulator).
    // expected[0] is the word at `address`, the rest follow it.
    struct Site {
        const char* name;
        u32 address;
        const u32* expected;
        u32 count;
    };

    // True if every expected word is in place; logs the first mismatch.
    bool matches(const Site& site);

    // Installs a Syriinge inline hook: `entry` runs, then the game continues
    // after the site. Nothing is patched unless the site matches and the
    // stub sy_core builds has a known layout. Returns true if installed.
    bool installInline(CoreApi* api, const Site& site, void (*entry)());

    // True while the site still branches to the stub we installed (Gecko
    // codes or a module load could have rewritten it since).
    bool stillInstalled(const Site& site);
}
}
