#pragma once

#include <types.h>

// Stand-in (PC build) for Brawl's gfFileIOManager (+0x8 / +0xC: request queues).
struct gfFileIOManager {
    u8 raw[0x20];
};

extern gfFileIOManager* g_gfFileIOManager;
