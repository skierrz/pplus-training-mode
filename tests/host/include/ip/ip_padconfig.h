#pragma once

#include <types.h>

// Stand-in (PC build) for Brawl's ipPadConfig (+0xB5..+0xB8: player 0-3 pad port).
class ipPadConfig {
public:
    u8 raw[0xC0];
    static ipPadConfig* getInstance();
};
