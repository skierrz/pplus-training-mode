#pragma once

#include <types.h>

// Stand-in (PC build): the interface manager as bytes at the game's offsets
// (the tested sources index it themselves); a test makes one in the arena.
class IfMngr {
public:
    u8 raw[0x100]; // +0x78 Training's pause menu (IfMinigameTraining)
};

extern IfMngr* g_IfMngr;
