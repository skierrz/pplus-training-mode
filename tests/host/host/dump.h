#pragma once

#include <types.h>

// Finding things in a loaded memory dump of a running match, for tests that
// run the mod's code over real game data (TEST_DUMP). Everything is found
// from the game's own structures and checked on the way, so any dump of a
// match works; nothing here is an address of one particular dump.
//
// Layouts (RSBE01 rev 2, Project+ 3.2.1; the sources named hold how each was
// learned): the animation-command module and its threads
// (src/extra/frame_data.cpp); Fighter +0x60 -> module accesser, its module
// enumeration at +0xC and the pointer to it at +0xD8, the motion module's
// subaction at +0x58, the status module's status at +0x34, and a fighter
// entry's instances {kind, Fighter*} at +0x30 (src/main/game/fighters.cpp,
// lib/BrawlHeaders). The subaction table (one pointer per subaction to its
// main script, in the fighter's moveset data) is found as the table whose
// entry for the fighter's current subaction is the script its subaction
// thread is running. The common action scripts' table (one pointer per
// status kind, 0x80FBFF8C) was read in a dump.
namespace host {
namespace dump {
    // Fighter kinds (lib/BrawlHeaders ft/ft_entry.h).
    enum { kMario = 0x00, kFox = 0x06, kBowser = 0x0B, kJigglypuff = 0x25 };

    struct FighterInfo {
        u32 fighter;    // the Fighter
        u32 modules;    // its soModuleEnumeration
        u32 animCmd;    // its animation-command module
        u32 subactions; // the subaction table, 0 if it was not found
        int kind;       // fighter kind, -1 if its entry was not found
        int subaction;  // what it is doing in the dump
        int status;
    };

    // The fighters of the dump's match (at most `max`).
    int fighters(FighterInfo* out, int max);
    bool fighterOfKind(int kind, FighterInfo& out);
    // The main script of subaction `index`, NULL if there is none.
    const u32* subaction(const FighterInfo& fighter, int index);
    // The common action script of status `status`, NULL if not readable.
    const u32* actionScript(int status);
}
}
