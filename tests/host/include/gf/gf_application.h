#pragma once

#include <types.h>

// Stand-in (PC build) for Brawl's gfApplication (+0xF8: u16 frame rate).
struct gfApplication {
    u8 raw[0x100];
};

extern gfApplication* g_gfApplication;
