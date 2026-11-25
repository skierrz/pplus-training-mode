#pragma once

#include <types.h>

#include "main/game/status_ids.h"

class Fighter;
class ftOwner;
class soModuleEnumeration;

// The fighters in the current match and the few values the lab reads and
// changes on them (docs/internals.md, "Fighter values"). Every accessor
// re-checks its pointers; only callers cache across frames.
namespace tm {
namespace game {
    enum { kMaxFighters = 4 };

    struct FighterRef {
        int entryId;      // ftManager entry id
        int playerNo;     // player number 0-3: the HUD slot
        int port;         // controller port 0-3 for a human, -1 for a CPU
        bool cpu;         // a CPU player in the match setup (cpuPlayer)
        Fighter* fighter; // the active instance (the one transforms switch to)
        Fighter* partner; // Nana for Ice Climbers, else NULL (not verified)
        ftOwner* owner;   // holds the damage shared by the entry
    };

    struct FighterState {
        float x, y;         // position
        float vx, vy;       // position change over the last game frame
        int status;         // status (action) kind
        float damage;       // percent, as the HUD shows it (it drops the fraction)
        int hitstun;        // hitstun frames left, 0 when none
        int shieldStun;     // shield stun frames left, 0 when not in GuardDamage
    };

    // Fills out[0..n) with the match's fighters in entry order, skipping
    // entries whose fighter or owner is not readable yet. Returns n.
    int listFighters(FighterRef* out, int max);

    // The first human (cpu false) or CPU fighter, in entry order; false if none.
    bool findFighter(bool cpu, FighterRef& out);

    bool readState(const FighterRef& ref, FighterState& out);

    // The fighter's modules, or NULL if any pointer on the way is not in RAM.
    const soModuleEnumeration* fighterModules(Fighter* fighter);
    // The entry's active fighter kind (ftKind), or Fighter_Nana when `partner`
    // is Ice Climbers' sub-fighter; -1 if unreadable. Transforming fighters
    // switch `m_activeInstanceIndex`, so this tracks the active form.
    int fighterKind(int entryId, bool partner = false);

    // Status kind (-1 if unreadable), hitstun and shield stun left (as in
    // FighterState) from a fighter's modules.
    int fighterStatus(const soModuleEnumeration* m);
    int fighterHitstun(const soModuleEnumeration* m);
    int fighterShieldStun(const soModuleEnumeration* m, int status);

    // Whether a fighter in `status` is out of play after a KO: Dead and
    // Standby always, Rebirth (the revival platform, where it can be driven
    // and placed again) unless `platform` is false.
    bool outOfPlay(int status, bool platform = true);

    // Which players are CPUs, from the match setup (the operation type says
    // "human" for a CPU whose AI is off). `fallback` while it is unreadable.
    bool cpuPlayer(int playerNo, bool fallback);
    bool humanPlayer(int playerNo);
    // The setup's state of player `playerNo` (0 human, 1 CPU), -1 if the
    // setup is not readable.
    int playerState(int playerNo);

    // Whether the match setup still lists player `playerNo`; true if it is
    // unreadable. Training drops a player as its pause closes with fewer
    // CPUs and removes the fighter a frame later.
    bool playerInMatch(int playerNo);

    float damage(const FighterRef& ref);
    // Sets the damage the way Training's own CPU Damage option does.
    void setDamage(const FighterRef& ref, float percent);
}
}
