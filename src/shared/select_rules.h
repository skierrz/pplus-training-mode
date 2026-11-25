#pragma once

#include <types.h>

#include "shared/drill_session.h"

// Character-select icon filtering and locking rules for future P+ scenarios
// and skill drills. Characters use CSS icon ids (MuSelchkind, BrawlHeaders
// mu/menu.h; P+ additions in scripts/nav.py's CSS_GRID).
namespace tm {
namespace drills {
    enum {
        kIconFox = 0x07,
        kIconPeach = 0x0D,
        kIconSheik = 0x0F,
        kIconMarth = 0x11,
        kIconFalco = 0x13,
        kIconWolf = 0x25,
        kIconEnd = 0xFF, // ends a set; a set that starts with it allows anyone
        kSetSize = 3
    };

    struct SelectRule {
        u8 drill;            // DrillId
        u8 solo;             // a one-player select (the drill hides its CPU)
        u8 player[kSetSize]; // icons the player may take
        u8 cpu[kSetSize];    // icons the CPU may take (ignored when solo)
    };

    // Drill `drill`'s rule, NULL if it has none (anyone on both sides).
    inline const SelectRule* selectRuleOf(int drill)
    {
        (void)drill;
        return NULL;
    }

    inline bool anyIcon(const u8* set)
    {
        return set[0] == kIconEnd;
    }

    inline bool setHas(const u8* set, int icon)
    {
        if (anyIcon(set))
        {
            return true;
        }
        for (int i = 0; i < kSetSize && set[i] != kIconEnd; i++)
        {
            if (set[i] == icon)
            {
                return true;
            }
        }
        return false;
    }

    // The one icon of a set that has exactly one, else kIconEnd.
    inline int onlyIcon(const u8* set)
    {
        return set[0] != kIconEnd && set[1] == kIconEnd ? set[0] : kIconEnd;
    }

    // Maps a CSS icon id to its match-setup gmCharacterKind; -1 if unmapped.
    inline int kindOfIcon(int icon)
    {
        switch (icon)
        {
        case kIconFox:
            return 7;
        case kIconPeach:
            return 13;
        case kIconSheik:
            return 15;
        case kIconMarth:
            return 19;
        case kIconFalco:
            return 21;
        case kIconWolf:
            return 41;
        default:
            return -1;
        }
    }
}
}
