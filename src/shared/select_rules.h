#pragma once

#include <types.h>

#include "shared/drill_session.h"

// Character-select icon filtering and locking rules for P+ skill drills.
// Characters use CSS icon ids (MuSelchkind, BrawlHeaders mu/menu.h; P+ roster
// in Source/ProjectM/CSS.ASM and CloneEngine.asm, mirrored in scripts/nav.py).
namespace tm {
namespace drills {
    enum {
        kIconYoshi = 0x05,
        kIconFox = 0x07,
        kIconNess = 0x0B,
        kIconPeach = 0x0D,
        kIconSheik = 0x0F,
        kIconMarth = 0x11,
        kIconFalco = 0x13,
        kIconLucas = 0x19,
        kIconLucario = 0x20,
        kIconWolf = 0x25,
        kIconSnake = 0x26,
        kIconMewtwo = 0x2E, // P+ clone slot at 0x80585DE0 (CloneEngine.asm:216)
        kIconEnd = 0xFF,    // ends a set; a set that starts with it allows anyone
        kSetSize = 8        // up to 7 icons plus kIconEnd terminator
    };

    struct SelectRule {
        u8 drill;            // DrillId
        u8 solo;             // a one-player select (the drill hides its CPU)
        u8 player[kSetSize]; // icons the player may take
        u8 cpu[kSetSize];    // icons the CPU may take (ignored when solo)
    };

    // Drill `drill`'s rule, NULL if it has none (2P, anyone on both sides).
    inline const SelectRule* selectRuleOf(int drill)
    {
        static const SelectRule kRules[] = {
            // Strictly solo modes: 1P select, any fighter.
            {kDrillLedgedash, 1, {kIconEnd}, {kIconEnd}},
            {kDrillWavedash, 1, {kIconEnd}, {kIconEnd}},
            {kDrillFlowCourse, 1, {kIconEnd}, {kIconEnd}},
            {kDrillRecoverySweetspot, 1, {kIconEnd}, {kIconEnd}},
            {kDrillComboBreakRush, 1, {kIconEnd}, {kIconEnd}},
            // Float / Hover & DJC: 2P (Target = CPU supported), P1 filtered.
            {kDrillHoverFloat,
             0,
             {kIconPeach, kIconMewtwo, kIconNess, kIconLucas, kIconYoshi, kIconEnd},
             {kIconEnd}},
            // Cancel Sprint (Endurance): 1P solo, 7 cancel-tech fighters.
            {kDrillCancelSprint,
             1,
             {kIconFox, kIconFalco, kIconWolf, kIconNess, kIconLucas, kIconLucario, kIconMewtwo, kIconEnd},
             {kIconEnd}},
            // Snake Infinite Recovery: 1P solo, locked to Snake (costume & tag open).
            {kDrillSnakeRecovery, 1, {kIconSnake, kIconEnd}, {kIconEnd}}
        };
        for (u32 i = 0; i < sizeof(kRules) / sizeof(kRules[0]); i++)
        {
            if (kRules[i].drill == drill)
            {
                return &kRules[i];
            }
        }
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

    // Maps a CSS icon id to its match-setup gmCharacterKind (gm_lib.h:68-112,
    // CloneEngine.asm:216 for Mewtwo); -1 if unmapped.
    inline int kindOfIcon(int icon)
    {
        switch (icon)
        {
        case kIconYoshi:
            return 5;
        case kIconFox:
            return 7;
        case kIconNess:
            return 11;
        case kIconPeach:
            return 13;
        case kIconSheik:
            return 15;
        case kIconMarth:
            return 19;
        case kIconFalco:
            return 21;
        case kIconLucas:
            return 27;
        case kIconLucario:
            return 36;
        case kIconWolf:
            return 41;
        case kIconSnake:
            return 42;
        case kIconMewtwo:
            return 51;
        default:
            return -1;
        }
    }
}
}
