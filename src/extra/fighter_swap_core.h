#pragma once

#include <stddef.h>
#include <types.h>

#include "shared/version.h"

// Pure, host-testable Project+ 3.2.1 fighter swap roster table, set-sharing
// rules, and costume helpers.
//
// Sources (RSBE01 rev 2, Project+ 3.2.1):
//  - Roster entries (45 total, alphabetical): pf/MENU3/data.cmnu P1..P4
//    Character Select table (45 entries, omitting Brawl Pokemon Trainer trio
//    0x1D/0x1F/0x21, solo Nana 0x12, and Alloys 0x2E..0x31), cross-checked
//    against src/menu/names.cpp.
//  - CSS icons (MuSelchkind): BrawlHeaders mu/menu.h + P+ 3.2.1 CSS table
//    (0x80585B00; Charizard 0x2A, Squirtle 0x2B, Ivysaur 0x2C, Roy 0x2D,
//    Mewtwo 0x2E, Knuckles 0x30; Hold-L variants Sopo/Giga Bowser/Wario-Man
//    map back to their base CSS icons 0x10/0x0C/0x15).
//  - Shared fighter-set groups: ftKindConversion::convertKind (0x808545EC)
//    and stLoaderPlayer set allocator (0x8082C3C8): Ice Climbers/Sopo,
//    Zelda/Sheik, Samus/Zero Suit Samus, Bowser/Giga Bowser, Wario/Wario-Man.
namespace tm {
namespace extra {
    struct FighterSwapEntry {
        const char* name;
        u8 gmKind;   // gmCharacterKind (gmPlayerInitData + 0)
        u8 cssIcon;  // MuSelchkind (gmSelCharData + 0x8 and 0x80585B00 color table)
        u8 setGroup; // canonical fighter-set group (shared sets share the same value)
    };

    enum {
        kFighterSwapCount = 45,
        kFighterSwapMaxSets = 4,
        kFighterSwapBudgetWithExtra = 3,
        kFighterSwapMaxCostume = 31
    };

    // Canonical set-group IDs for the five fighter pairs that share one
    // OverlayFighter(i+1) + Fighter(i+1)Resoruce2 heap set in Brawl/P+.
    inline u8 fighterSwapSetGroup(u8 gmKind)
    {
        switch (gmKind)
        {
        case 0x03: // Samus
        case 0x04: // Zero Suit Samus
            return 0x03;
        case 0x0C: // Bowser
        case 0x2C: // Giga Bowser
            return 0x0C;
        case 0x0E: // Zelda
        case 0x0F: // Sheik
            return 0x0E;
        case 0x10: // Ice Climbers
        case 0x11: // Sopo
        case 0x12: // Nana
            return 0x10;
        case 0x17: // Wario
        case 0x2D: // Wario-Man
            return 0x17;
        default:
            return gmKind;
        }
    }

    // Alphabetical P+ 3.2.1 roster (41 standard fighters + Ice Climbers/Sopo +
    // Giga Bowser + Wario-Man = 45 entries).
    inline const FighterSwapEntry* fighterSwapRoster()
    {
        static const FighterSwapEntry kRoster[kFighterSwapCount] = {
            {"Bowser",           0x0C, 0x0C, 0x0C},
            {"Captain Falcon",   0x0A, 0x0A, 0x0A},
            {"Charizard",        0x1E, 0x2A, 0x1E},
            {"Dedede",           0x23, 0x1F, 0x23},
            {"Diddy Kong",       0x1C, 0x1A, 0x1C},
            {"Donkey Kong",      0x01, 0x01, 0x01},
            {"Falco",            0x15, 0x13, 0x15},
            {"Fox",              0x07, 0x07, 0x07},
            {"Ganondorf",        0x16, 0x14, 0x16},
            {"Giga Bowser",      0x2C, 0x0C, 0x0C},
            {"Ice Climbers",     0x10, 0x10, 0x10},
            {"Ike",              0x25, 0x21, 0x25},
            {"Ivysaur",          0x22, 0x2C, 0x22},
            {"Jigglypuff",       0x27, 0x23, 0x27},
            {"Kirby",            0x06, 0x06, 0x06},
            {"Knuckles",         0x35, 0x30, 0x35},
            {"Link",             0x02, 0x02, 0x02},
            {"Lucario",          0x24, 0x20, 0x24},
            {"Lucas",            0x1B, 0x19, 0x1B},
            {"Luigi",            0x09, 0x09, 0x09},
            {"Mario",            0x00, 0x00, 0x00},
            {"Marth",            0x13, 0x11, 0x13},
            {"Meta Knight",      0x18, 0x16, 0x18},
            {"Mewtwo",           0x33, 0x2E, 0x33},
            {"Mr. Game & Watch", 0x14, 0x12, 0x14},
            {"Ness",             0x0B, 0x0B, 0x0B},
            {"Olimar",           0x1A, 0x18, 0x1A},
            {"Peach",            0x0D, 0x0D, 0x0D},
            {"Pikachu",          0x08, 0x08, 0x08},
            {"Pit",              0x19, 0x17, 0x19},
            {"R.O.B.",           0x26, 0x22, 0x26},
            {"Roy",              0x32, 0x2D, 0x32},
            {"Samus",            0x03, 0x03, 0x03},
            {"Sheik",            0x0F, 0x0F, 0x0E},
            {"Snake",            0x2A, 0x26, 0x2A},
            {"Sonic",            0x2B, 0x27, 0x2B},
            {"Sopo",             0x11, 0x10, 0x10},
            {"Squirtle",         0x20, 0x2B, 0x20},
            {"Toon Link",        0x28, 0x24, 0x28},
            {"Wario",            0x17, 0x15, 0x17},
            {"Wario-Man",        0x2D, 0x15, 0x17},
            {"Wolf",             0x29, 0x25, 0x29},
            {"Yoshi",            0x05, 0x05, 0x05},
            {"Zelda",            0x0E, 0x0E, 0x0E},
            {"Zero Suit Samus",  0x04, 0x04, 0x03}
        };
        return kRoster;
    }

    inline const FighterSwapEntry* fighterSwapAt(int index)
    {
        if (index < 0 || index >= kFighterSwapCount)
        {
            return NULL;
        }
        return &fighterSwapRoster()[index];
    }

    inline int fighterSwapIndexOfKind(u8 gmKind)
    {
        const FighterSwapEntry* r = fighterSwapRoster();
        for (int i = 0; i < kFighterSwapCount; i++)
        {
            if (r[i].gmKind == gmKind)
            {
                return i;
            }
        }
        return -1;
    }

    // Maps gmCharacterKind to the ftKind checked by cpu::fitsKind() for
    // character-specific DJC/Float counter actions (src/main/cpu/actions.cpp:281).
    // Returns 0 (Mario: neither DJC nor Float) for all other fighters.
    inline int fighterSwapCounterFtKind(u8 gmKind)
    {
        switch (gmKind)
        {
        case 0x0B:
            return 0x0A; // Ness (DJC)
        case 0x0D:
            return 0x0C; // Peach (Float)
        case 0x16:
            return 0x14; // Ganondorf (Float)
        case 0x1B:
            return 0x1A; // Lucas (DJC)
        case 0x33:
            return 0x26; // Mewtwo (DJC + Float)
        default:
            return 0x00;
        }
    }

    // Counts distinct fighter heap sets required across `count` player slots
    // (honoring shared sets like ICs/Sopo, Zelda/Sheik, Samus/ZSS, Bowser/Giga
    // Bowser, Wario/Wario-Man).
    inline int fighterSwapDistinctSets(const u8* kinds, const bool* active, int count)
    {
        if (kinds == NULL || active == NULL || count <= 0)
        {
            return 0;
        }
        u8 seen[kFighterSwapMaxSets];
        int distinct = 0;
        for (int i = 0; i < count; i++)
        {
            if (!active[i])
            {
                continue;
            }
            u8 group = fighterSwapSetGroup(kinds[i]);
            bool found = false;
            for (int k = 0; k < distinct; k++)
            {
                if (seen[k] == group)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                if (distinct < kFighterSwapMaxSets)
                {
                    seen[distinct] = group;
                }
                distinct++;
            }
        }
        return distinct;
    }

    // True if the post-swap active fighters fit within the available fighter
    // heap sets (at most 3 while extra holds OverlayFighter4, else 4).
    inline bool fighterSwapFitsBudget(const u8* kinds, const bool* active, int count, bool extraHoldsSet3)
    {
        int limit = extraHoldsSet3 ? kFighterSwapBudgetWithExtra : kFighterSwapMaxSets;
        return fighterSwapDistinctSets(kinds, active, count) <= limit;
    }

    // Clamps a 0-based costume index to [0, numColors - 1] (0 when numColors <= 0).
    inline int fighterSwapClampCostume(int costumeIdx, int numColors)
    {
        if (numColors <= 1 || costumeIdx <= 0)
        {
            return 0;
        }
        int hi = numColors - 1;
        if (hi > kFighterSwapMaxCostume)
        {
            hi = kFighterSwapMaxCostume;
        }
        return costumeIdx > hi ? hi : costumeIdx;
    }

    // Maps a live (colorFileNo, colorNo) pair back to its 0-based costume index
    // in `fileNos[0..numColors-1]`. Checks colorNo first (exact match when the
    // table has duplicate file numbers), then scans fileNos, else returns 0.
    inline int fighterSwapFindCostumeIndex(u8 colorFileNo, u8 colorNo, const u8* fileNos, int numColors)
    {
        if (fileNos == NULL || numColors <= 0)
        {
            return 0;
        }
        if (colorNo < static_cast<u32>(numColors) && fileNos[colorNo] == colorFileNo)
        {
            return static_cast<int>(colorNo);
        }
        for (int i = 0; i < numColors; i++)
        {
            if (fileNos[i] == colorFileNo)
            {
                return i;
            }
        }
        return 0;
    }
}
}
