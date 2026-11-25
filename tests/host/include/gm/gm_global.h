#pragma once

#include <types.h>

// Stand-in (PC build): the two records of the game's globals the tested
// sources read, as bytes at the game's offsets (the sources index them
// themselves). host/fakes.h makes them in the arena.
struct gmGlobalModeMelee {
    u8 raw[0x400]; // +0x1A the stage (16 bits), +0x98 the players, 0x5C bytes each
};

struct gmSelCharData {
    u8 raw[0x400]; // +0x8 the icons (a word per port), +0xB8 the players
};

struct gmSetRule {
    u8 raw[0x40]; // +0x24 the controlling menu port (word)
};

struct gmMenuData {
    bool m_isWidescreen;
};

struct gmRecord {
    gmMenuData m_menuData;
};

class GameGlobal {
public:
    void* m_0x00;
    void* m_0x04;
    gmGlobalModeMelee* m_modeMelee; // 0x08
    void* m_0x0C;
    gmSelCharData* m_selCharData;   // 0x10
    void* m_selStageData;           // 0x14
    gmRecord* m_record;             // 0x18
    gmSetRule* m_setRule;           // 0x1C
};

extern GameGlobal* g_GameGlobal;
