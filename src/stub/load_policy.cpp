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

    // Extra module: AssistFigureResource (MEM2, 410 KiB) in Training (1P-4P)
    // and opted-in local offline Versus when Assist Trophies are off (2P-4P).
    // When Assist Trophies are on in local offline Versus, load match only so
    // Brawl keeps AssistFigureResource free for Assist Trophy preloads.
#ifdef TM_EXTRA_HEAP
    static const Heaps::HeapType kExtraHeap = static_cast<Heaps::HeapType>(TM_EXTRA_HEAP);
#else
    static const Heaps::HeapType kExtraHeap = Heaps::AssistFigureResource;
#endif

    // P+ 3.2.1 ground-truth addresses for Netplay, Replay, items, and players:
    //  - 0x80550000: MultiGCT filename pointer ("BOOST.GCT" offline in
    //    Source/Project+/MultiGCT.asm:4-9 vs "NETBOOST.GCT" in
    //    Source/Netplay/Net-MultiGCT.asm:7-12).
    //  - 0x80023B88: v-sync branch patched to nop (0x60000000) by
    //    Source/Extras/Netplay.asm:4-5.
    //  - 0x805BBFC0: 1 while viewing a replay (Source/LegacyTE/Replays.asm:360-362).
    //  - gmGlobalModeMelee (gm_global_mode_melee.h):
    //      +0x08 (>>2): m_meleeInitData.m_gameMode
    //      +0x16:       m_meleeInitData.m_itemFrequency (0 = Frequency_None)
    //      +0x37 bit 1: m_meleeInitData.m_itSwitch.m_item.m_assistTrophy
    enum {
        kMultiGctPtr = 0x80550000,
        kNetplayVsyncAddr = 0x80023B88,
        kNetplayVsyncNop = 0x60000000,
        kReplayFlagAddr = 0x805BBFC0,
        kModeMeleeGameMode = 0x8,
        kItemFrequencyOffset = 0x16,
        kItemSwitchLowByte = 0x37,
        kAssistTrophyMask = 0x02
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
            if (gm != kMeleeGameMode || !offlineVersusOptIn(block) || !game::sequenceIs("sqVsMelee")
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
            bool assistEnabled =
                mode[kItemFrequencyOffset] != 0 && (mode[kItemSwitchLowByte] & kAssistTrophyMask) != 0;
            return assistEnabled ? kPlanMatchOnly : kPlanMatchAndExtra;
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
