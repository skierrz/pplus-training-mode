// Which module loads in which scene, and into which heap.
#include "stub/load_policy.h"

#include <gm/gm_global.h>

#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/interface.h"
#include "stub/dispatch.h"
#include "stub/load_policy_core.h"

namespace tm {
namespace policy {
    // Match/menu modules: OverlayMenu (unused in Training and 2P/4P Versus).
    static const Heaps::HeapType kOverlayMenu = Heaps::OverlayMenu;

    // Extra module: Training and opted-in 2P/3P local offline Versus only;
    // 4P Versus skips extra so OverlayFighter1..4 stay untouched.
#ifdef TM_EXTRA_HEAP
    static const Heaps::HeapType kExtraHeap = static_cast<Heaps::HeapType>(TM_EXTRA_HEAP);
#else
    static const Heaps::HeapType kExtraHeap = Heaps::OverlayFighter4;
#endif

    // P+ 3.2.1 ground-truth addresses for Netplay, Replay, and player slots:
    //  - 0x80550000: MultiGCT filename pointer ("BOOST.GCT" offline in
    //    Source/Project+/MultiGCT.asm:4-9 vs "NETBOOST.GCT" in
    //    Source/Netplay/Net-MultiGCT.asm:7-12).
    //  - 0x80023B88: v-sync branch patched to nop (0x60000000) by
    //    Source/Extras/Netplay.asm:4-5.
    //  - 0x805BBFC0: 1 while viewing a replay (Source/LegacyTE/Replays.asm:360-362).
    //  - modeMelee + 0x98 + i*0x5C, byte +1 != 3 for active players i = 0..3.
    enum {
        kMultiGctPtr = 0x80550000,
        kNetplayVsyncAddr = 0x80023B88,
        kNetplayVsyncNop = 0x60000000,
        kReplayFlagAddr = 0x805BBFC0,
        kModeMeleeGameMode = 0x8,
        kPlayersOffset = 0x98,
        kPlayerStride = 0x5C,
        kPlayerAbsent = 3
    };

    u32 forSceneStart(const gfScene* scene, Placement out[kMaxPlacements])
    {
        out[0].heap = kOverlayMenu;
        out[0].onlyIfEmpty = false;
        if (game::sceneIs(scene, "scMelee"))
        {
            out[0].kind = iface::kKindMatch;
            out[1].kind = iface::kKindExtra;
            out[1].heap = kExtraHeap;
            out[1].onlyIfEmpty = true;
            if (!game::isRam(g_GameGlobal) || !game::isRam(g_GameGlobal->m_modeMelee))
            {
                return kPlanNone;
            }
            const u8* mode = reinterpret_cast<const u8*>(g_GameGlobal->m_modeMelee);
            u32 gm = mode[kModeMeleeGameMode] >> 2;
            if (gm == kTrainingGameMode)
            {
                return kPlanMatchAndExtra;
            }
            const u8* block = dispatch::table().services.settings();
            const char* gct = *reinterpret_cast<const char* const*>(kMultiGctPtr);
            if (gm != kMeleeGameMode || block == NULL
                || *reinterpret_cast<const u32*>(block + block::kSettings) != block::kSettingsMagic
                || block[block::kOfflineVersusOffset] == 0 || !game::sequenceIs("sqVsMelee")
                || (game::isRam(gct) && gct[0] == 'N' && gct[1] == 'E' && gct[2] == 'T')
                || *reinterpret_cast<const u32*>(kNetplayVsyncAddr) == kNetplayVsyncNop
                || *reinterpret_cast<const u32*>(kReplayFlagAddr) == 1u)
            {
#ifdef TM_LOAD_ANY_MATCH
                return kPlanMatchOnly;
#else
                return kPlanNone;
#endif
            }
            u32 active = 0;
            for (u32 i = 0; i < 4; i++)
            {
                active += (mode[kPlayersOffset + i * kPlayerStride + 1] != kPlayerAbsent);
            }
            return active <= kMaxVsPlayersForExtra ? kPlanMatchAndExtra : kPlanMatchOnly;
        }
        if (game::sceneIs(scene, "scSelctCharacter") && game::sequenceIs("sqTraining"))
        {
            out[0].kind = iface::kKindMenu;
            return 1;
        }
        return 0;
    }
}
}
