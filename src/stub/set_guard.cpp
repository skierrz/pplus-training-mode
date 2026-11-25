// Fighter set guard. Each different character in a match gets one of four
// fighter heap sets; set n loads its code module into OverlayFighter(n+1).
// The extra module lives in OverlayFighter4, which Training alone never
// needs, but P+'s Code Menu ("P1..P4 Character Select") can give each CPU
// its own character: with four different characters the fourth set's module
// does not fit ("gfModule::create Error : Can't Alloc Heap Buffer", then a
// freeze). A character that would take that set is swapped, just before it
// loads, for one already loaded (the CPU's first). Changes that fit, and
// matches without the extra module, are left alone. Covered by make
// test-fighter-sets.
//
// Read in code (RSBE01 rev 2, P+ 3.2.1; names from the Brawl decompilation):
//  - stLoaderPlayer::processBegin (sora_melee 0x80946004; r30 the loader,
//    r31 its playerInitData entry: byte 0 character, byte 1 state, 3 =
//    absent) reads byte 0 at 0x80946174 and passes it to ftManager
//    0x80813E8C (call at 0x80946188), which takes a set. A changed character
//    (the Code Menu writes byte 0) is removed (state 5) and created again
//    here: the one place a character enters a Training match.
//  - The set allocator (0x8082C3C8) shares a live set holding one of the
//    character's kinds (ftKindConversion::convertKind 0x808545EC: up to
//    four, e.g. Zelda and Sheik); else it takes the lowest set i whose
//    Fighter Resoruce2 id (22 + i) no live set has. Its code heap is 53 + i.
//  - g_ftSlotManager (pointer at 0x80B87C50) +8: a ring of four 24-byte
//    entries (+0 Resoruce2 id, +4 code heap id, +8 users, +12 set index,
//    +16 kind count, +17 kinds). The ring's word at +8: first entry in its
//    top 4 bits, count in the next 4 but 4 (0x8082D4F8, 0x8082CD2C); entry
//    i is at ring + 12 + 24 * ((first + i) mod 4).
#include "stub/set_guard.h"

#include <sr/sr_common.h>
#include <types.h>

#include "shared/drill_session.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "stub/dispatch.h"
#include "stub/hook_entry.h"
#include "stub/loader.h"
#include "stub/module_hook.h"

namespace tm {
namespace stub {
    // The costume computed just before the character is read, reached on
    // both of its paths (0x80946150, 0x80946154). r0 and CR are reloaded
    // right after; the hook entry preserves CR, CTR, XER and the FPRs.
    static const u32 kSiteWords[] = {
        0x8803001B, // lbz r0,27(r3)
        0x5400F7FF, // rlwinm. r0,r0,30,31,31
        0x41820008, // beq- +8
        0x60A50080  // ori r5,r5,128
    };
    // Entry of state 3 of stLoaderPlayer::processBegin (sora_melee
    // 0x80946348): runs before any read of playerInitData[0] (0x80946358,
    // 0x809463C4) or colorNo (0x809463EC), because sy_core's inline stub
    // executes the replaced word before calling the hook. r30 is the loader,
    // r31 the player entry.
    static const u32 kDrillSiteWords[] = {
        0x889F0001, // lbz r4,1(r31)
        0x28040003, // cmplwi r4,3
        0x4182006C  // beq- +0x6c (0x809463bc)
    };
    // sora_melee is module 27; its .text started at 0x8070AA14 in every boot
    // logged so far (the sites are given relative to it).
    static const hook::ModuleSite kSite = {"fighter set guard", 27, 0x80946160 - 0x8070AA14, kSiteWords, 4};
    static const hook::ModuleSite kDrillSite = {"drill swap guard", 27, 0x80946348 - 0x8070AA14, kDrillSiteWords, 3};

    enum {
        kSlotManagerPtr = 0x80B87C50,
        kSets = 4,
        kFirstResource2 = 22,
        kGameGlobal = 0x805A00E0, // +8 modeMelee
        kPlayers = 0x98,          // modeMelee + 0x98: four playerInitData entries
        kPlayerSize = 0x5C,
        kPlayerColorFile = 5,
        kPlayerFlags = 29,
        kAbsent = 3,
        kLoaderCharacter = 0x5A, // the character the loader last created
        kLoaderCostume = 0x5B    // the costume byte the loader last created
    };

    typedef int (*ConvertKind)(int character, int* kinds);
    static const ConvertKind convertKind = reinterpret_cast<ConvertKind>(0x808545EC);

    struct SetEntry {
        s32 resource2;
        s32 codeHeap;
        s32 users;
        s32 index;
        u8 kindCount;
        u8 kinds[7];
    };

    struct LiveSets {
        const SetEntry* entry[kSets];
        u32 count;
    };

    static bool readSets(LiveSets& out)
    {
        const u8* manager = *reinterpret_cast<const u8* const*>(kSlotManagerPtr);
        if (!game::isRam(manager))
        {
            return false;
        }
        const u8* ring = manager + 8;
        u32 word = game::wordAt(ring, 8);
        s32 first = static_cast<s32>(word) >> 28;
        s32 count = static_cast<s32>(word << 8) >> 28;
        if (first < 0 || first >= kSets || count < 0 || count > kSets)
        {
            return false;
        }
        for (s32 i = 0; i < count; i++)
        {
            s32 at = first + i;
            if (at >= kSets)
            {
                at -= kSets;
            }
            out.entry[i] = reinterpret_cast<const SetEntry*>(ring + 12 + 24 * at);
        }
        out.count = static_cast<u32>(count);
        return true;
    }

    // True if `character` shares a live set (the allocator's first rule).
    static bool shares(u8 character, const LiveSets& sets)
    {
        int kinds[4];
        int n = convertKind(character, kinds);
        for (u32 s = 0; s < sets.count; s++)
        {
            const SetEntry& e = *sets.entry[s];
            for (u32 k = 0; k < e.kindCount && k < sizeof(e.kinds); k++)
            {
                for (int j = 0; j < n && j < 4; j++)
                {
                    if (e.kinds[k] == kinds[j])
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // The set a character that shares none would take, or -1 if none is free.
    static s32 nextFreeSet(const LiveSets& sets)
    {
        for (s32 i = 0; i < kSets; i++)
        {
            bool used = false;
            for (u32 s = 0; s < sets.count; s++)
            {
                used = used || sets.entry[s]->resource2 == kFirstResource2 + i;
            }
            if (!used)
            {
                return i;
            }
        }
        return -1;
    }

    static u8* playerEntries()
    {
        const u8* global = *reinterpret_cast<const u8* const*>(kGameGlobal);
        if (!game::isRam(global))
        {
            return NULL;
        }
        u8* mode = reinterpret_cast<u8*>(game::wordAt(global, 8));
        return game::isRam(mode) ? mode + kPlayers : NULL;
    }

    static s32 playerIndex(const u8* player, u8* entries)
    {
        if (entries == NULL || player < entries)
        {
            return -1;
        }
        u32 offset = static_cast<u32>(player - entries);
        return offset < 4 * kPlayerSize && offset % kPlayerSize == 0 ? static_cast<s32>(offset / kPlayerSize) : -1;
    }

    extern "C" void tmDrillGuardBody(const u8* loader, u8* player)
    {
        if (!game::trainingGameMode())
        {
            return;
        }
        if (loader::extraHeap() < 0 || !game::isRam(loader) || !game::isRam(player) || player[1] == kAbsent)
        {
            return;
        }
        const drills::Session* s = drills::sessionIn(dispatch::table().services.settings());
        if (s == NULL || s->drill == drills::kDrillNone || s->drillRan == 0)
        {
            return;
        }
        u8 current = loader[kLoaderCharacter];
        u8 wanted = player[0];
        if (wanted == current)
        {
            return;
        }
        player[0] = current;
        // P+'s Code Menu also zeroes m_colorFileNo (+5) and m_colorNo (+6) on
        // a character change. State 3 (0x809463E8..0x80946428) only compares
        // m_colorFileNo (+5) against loader+0x5B (unless the dark/fake bits
        // player[29] & 0x18 override it at 0x809463E8..0x80946408), so put
        // back only +5 and leave m_colorNo (+6) untouched (m_colorFileNo and
        // m_colorNo differ on non-default costumes).
        if ((player[kPlayerFlags] & 0x18) == 0)
        {
            player[kPlayerColorFile] = loader[kLoaderCostume] & 0x7F;
        }
        s32 self = playerIndex(player, playerEntries());
        TM_LOG("fighter sets: player %d's change to character %u refused: drill %u active; keeps character %u\n",
               self + 1, wanted, s->drill, current);
    }

    extern "C" void tmSetGuardBody(const u8* loader, u8* player)
    {
        if (!game::trainingGameMode())
        {
            return;
        }
        s32 heap = loader::extraHeap();
        if (heap < Heaps::OverlayFighter1 || heap > Heaps::OverlayFighter4 || !game::isRam(player))
        {
            return;
        }
        LiveSets sets;
        u8 wanted = player[0];
        if (!readSets(sets) || shares(wanted, sets) || nextFreeSet(sets) != heap - Heaps::OverlayFighter1)
        {
            return;
        }
        // A character already loaded: the CPU's (player 2) first, then the
        // one this player had, then any other player's.
        u8* entries = playerEntries();
        s32 self = playerIndex(player, entries);
        u8 options[2 + 4];
        u32 count = 0;
        if (entries != NULL)
        {
            if (self != 1 && entries[kPlayerSize + 1] != kAbsent)
            {
                options[count++] = entries[kPlayerSize];
            }
        }
        if (game::isRam(loader))
        {
            options[count++] = loader[kLoaderCharacter];
        }
        for (u32 p = 0; entries != NULL && p < 4; p++)
        {
            if (static_cast<s32>(p) != self && entries[p * kPlayerSize + 1] != kAbsent)
            {
                options[count++] = entries[p * kPlayerSize];
            }
        }
        for (u32 i = 0; i < count; i++)
        {
            if (shares(options[i], sets))
            {
                player[0] = options[i];
                TM_LOG("fighter sets: player %d's change to character %u refused: it would take set %d, whose "
                       "code heap %s holds the extra module; keeps character %u\n",
                       self + 1, wanted, heap - Heaps::OverlayFighter1, game::heapName(heap), options[i]);
                return;
            }
        }
        TM_LOG("fighter sets: player %d's character %u takes the extra module's heap set; no loaded character "
               "to keep instead\n",
               self + 1, wanted);
    }

#ifndef TM_HOST_TEST
    // Entered from sy_core's stub, which saved r3-r31 with stmw r3,0xC(r1)
    // (inline_stub checks that word): the site's r30 and r31 are at
    // 0xC + 27 * 4 and 0xC + 28 * 4.
    static asm void setGuardEntry()
    {
        nofralloc
        lwz r3, 0x78(r1)
        lwz r4, 0x7C(r1)
        lis r12, tmSetGuardBody@h
        ori r12, r12, tmSetGuardBody@l
        b tmPreservingCall
    }

    static asm void drillGuardEntry()
    {
        nofralloc
        lwz r3, 0x78(r1)
        lwz r4, 0x7C(r1)
        lis r12, tmDrillGuardBody@h
        ori r12, r12, tmDrillGuardBody@l
        b tmPreservingCall
    }

    bool installSetGuard(CoreApi* api)
    {
        bool ok = hook::installInlineOnModuleLoad(api, kSite, setGuardEntry);
        ok = hook::installInlineOnModuleLoad(api, kDrillSite, drillGuardEntry) && ok;
        return ok;
    }
#endif
}
}
