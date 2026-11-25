#pragma once

#include <gf/gf_pad_status.h>

// Stand-in (PC build) for Brawl's gfPadSystem.
class gfPadSystem {
public:
    void getSysPadStatus(int padNum, gfPadStatus* dest);
};

extern gfPadSystem* g_gfPadSystem;
