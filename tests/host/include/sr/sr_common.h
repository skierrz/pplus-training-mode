#pragma once

#include <types.h>

// Stand-in (PC build): the game heaps the tested sources name, with the
// game's ids (lib/BrawlHeaders sr/sr_common.h).
namespace Heaps {
    enum HeapType {
        Network = 0x6,
        ItemResource = 0x8,
        FighterTechqniq = 0x1f,
        MenuInstance = 0x2a,
        OverlayMenu = 0x34,
        OverlayFighter1 = 0x35,
        OverlayFighter4 = 0x38,
        Invalid = 0xff
    };
}

typedef Heaps::HeapType HeapType;
