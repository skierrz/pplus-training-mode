// Which module loads in which scene, and into which heap.
#include "stub/load_policy.h"

#include "shared/game/scene.h"
#include "shared/interface.h"

namespace tm {
namespace policy {
    // Match modules: OverlayMenu, which holds menu modules and was unused in
    // every match scene measured (Training with 1 and 3 CPUs and items; 2-
    // and 4-player Versus through the results screen).
    static const Heaps::HeapType kMatchHeap = Heaps::OverlayMenu;

    // The extra module:
    //  - every Training match (about 3.5 ms more load than the
    //    match module alone, in Dolphin);
    //  - after the match module and only if it loaded (it links against it);
    //    unloaded first;
    //  - into OverlayFighter4, only while that heap is empty. Each different
    //    character takes the lowest free of four heap sets, set n loading its
    //    code module into OverlayFighter(n+1). Training's extra CPUs copy its
    //    one CPU character, so Training alone uses at most two sets (make
    //    test-extra-heaps); four different characters (Versus, or P+'s Code
    //    Menu in Training) use the fourth, which set_guard.cpp refuses while
    //    this module is loaded.
#ifdef TM_EXTRA_HEAP
    static const Heaps::HeapType kExtraHeap = static_cast<Heaps::HeapType>(TM_EXTRA_HEAP);
#else
    static const Heaps::HeapType kExtraHeap = Heaps::OverlayFighter4;
#endif

    // Training matches only; TM_LOAD_ANY_MATCH (EXPERIMENTS builds with
    // ANY_MATCH=1) loads in every offline match, to measure that case.
    static bool matchWanted()
    {
#ifdef TM_LOAD_ANY_MATCH
        return true;
#else
        return game::trainingGameMode();
#endif
    }

    // The menu module, for Training's character select:
    // OverlayMenu, since that scene allocates hundreds of blocks in
    // MenuInstance and nothing else used OverlayMenu there (measured).
    static const Heaps::HeapType kMenuHeap = Heaps::OverlayMenu;

    u32 forSceneStart(const gfScene* scene, Placement out[kMaxPlacements])
    {
        if (game::sceneIs(scene, "scMelee") && matchWanted())
        {
            out[0].kind = iface::kKindMatch;
            out[0].heap = kMatchHeap;
            out[0].onlyIfEmpty = false;
            if (!game::trainingGameMode())
            {
                return 1;
            }
            out[1].kind = iface::kKindExtra;
            out[1].heap = kExtraHeap;
            out[1].onlyIfEmpty = true;
            return 2;
        }
        if (game::sceneIs(scene, "scSelctCharacter") && game::inTraining())
        {
            out[0].kind = iface::kKindMenu;
            out[0].heap = kMenuHeap;
            out[0].onlyIfEmpty = false;
            return 1;
        }
        return 0;
    }
}
}
