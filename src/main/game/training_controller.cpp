#include "main/game/training_controller.h"

#include <OS/OSCache.h>

#include "shared/game/scene.h"
#include "shared/log.h"

namespace tm {
namespace game {
    enum {
        kReassignSite = 0x80962D0C,
        kReassignOrig = 0x8B1BFFFF, // lbz r24, -1(r27)
        kReassignSkip = 0x48000170  // b 0x80962e7c
    };

    static u32 s_owners = 0;
    static bool s_refused = false;

    static void writeCode(u32 at, u32 word)
    {
        *reinterpret_cast<u32*>(at) = word;
        DCFlushRange(reinterpret_cast<void*>(at), 4);
        ICInvalidateRange(reinterpret_cast<void*>(at), 4);
    }

    static void apply()
    {
        u32 word = *reinterpret_cast<const u32*>(kReassignSite);
        if (s_owners != 0 && trainingGameMode())
        {
            if (s_refused)
            {
                return;
            }
            if (word != kReassignOrig && word != kReassignSkip)
            {
                s_refused = true;
                TM_LOG("training controller: site %08x holds %08x, not the game's instruction: left alone\n",
                       kReassignSite, word);
                return;
            }
            if (word != kReassignSkip)
            {
                writeCode(kReassignSite, kReassignSkip);
            }
            return;
        }
        // Restore without the Training gate, and only if our skip branch is there.
        if (word == kReassignSkip)
        {
            writeCode(kReassignSite, kReassignOrig);
        }
    }

    void trainingControllerSuppress(u32 owner, bool on)
    {
        s_owners = on ? (s_owners | owner) : (s_owners & ~owner);
        apply();
    }

    u32 trainingControllerOwners()
    {
        return s_owners;
    }

    void trainingControllerRestore()
    {
        s_owners = 0;
        apply();
        s_refused = false;
    }
}
}
