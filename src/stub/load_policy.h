#pragma once

#include <sr/sr_common.h>
#include <types.h>

class gfScene;

namespace tm {
namespace policy {
    enum { kMaxPlacements = 2 };

    // Which module kind to load, and into which heap.
    struct Placement {
        u32 kind; // iface::kKind*
        Heaps::HeapType heap;
        // Load only if the heap holds no block (the game's own use of it
        // would collide with ours).
        bool onlyIfEmpty;
    };

    // Asked just before `scene` starts. Fills `out` with the modules to load
    // for it, in load order, and returns how many. A module after the first
    // may be linked against the ones before it, so the loader stops at the
    // first that does not load, and unloads them in reverse order when the
    // scene exits.
    u32 forSceneStart(const gfScene* scene, Placement out[kMaxPlacements]);
}
}
