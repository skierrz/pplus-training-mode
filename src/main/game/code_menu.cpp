#include "main/game/code_menu.h"

#include <types.h>

#include "main/settings.h"
#include "shared/game/code_menu.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/log.h"

// Blocks P+'s in-match Code Menu combo (L+R+D-pad Down) during Training
// matches by writing P+'s own "Code Menu Activation" selection line in RAM to
// OFF (2) while a drill is active or Controls > P+ Code Menu Shortcut is Off,
// and restoring the player's exact value when allowed in the lab or on detach.
//
// In P+ 3.2.1 (Source/CodeMenu/CodeMenu.asm:756-768), value 2 cancels every
// controller combo before pad masking or pausing, while still running the
// per-frame debug option writers (lines 1109-1189) so Debug options configured
// on the Main Menu continue to apply in Training. Main Menu VERSUS > CODES
// sets state 1 (bypassing lines 756-768 via lines 746-754) and is unaffected.
namespace tm {
namespace game {
    enum {
        kActivationLineDefault = 0x804E0C24, // data.cmnu and data_AUTOSAVE_ON.cmnu (value at 0x804E0C2C)
        kScanStart = 0x804E07D8,
        kScanEnd = 0x804E3000,
        kLineSize = 0x58,
        kLineKindSelection = 0,
        kLineTextOffset = 0x28,
        kLineValueOffset = 0x8,
        kLineDefaultOffset = 0x10,
        kLineMaxOffset = 0x14,
        kActivationOff = 2
    };

    static const char kActivationLabel[] = "Code Menu Activation:  %s";

    static u32 s_activationAddr = 0;
    static u32 s_savedActivation = 0;
    static bool s_attached = false;
    static bool s_haveSaved = false;
    static bool s_missingLogged = false;

    static bool isActivationLine(u32 addr)
    {
        const void* line = reinterpret_cast<const void*>(addr);
        if (byteAt(line, 0) != 0 || byteAt(line, 1) != kLineSize)
        {
            return false;
        }
        if (byteAt(line, 2) != kLineKindSelection)
        {
            return false;
        }
        if (byteAt(line, 5) != 0 || byteAt(line, 6) != kLineTextOffset)
        {
            return false;
        }
        if (wordAt(line, kLineDefaultOffset) != 0 || wordAt(line, kLineMaxOffset) != kActivationOff)
        {
            return false;
        }
        if (wordAt(line, kLineValueOffset) > kActivationOff)
        {
            return false;
        }
        for (u32 i = 0; i < sizeof(kActivationLabel); i++)
        {
            if (byteAt(line, kLineTextOffset + i) != static_cast<u8>(kActivationLabel[i]))
            {
                return false;
            }
        }
        return true;
    }

    static u32 findActivationValueAddr()
    {
        if (isActivationLine(kActivationLineDefault))
        {
            return kActivationLineDefault + kLineValueOffset;
        }
        for (u32 addr = kScanStart; addr + kLineSize <= kScanEnd; addr += 4)
        {
            if (isActivationLine(addr))
            {
                return addr + kLineValueOffset;
            }
        }
        return 0;
    }

    static void restoreSaved()
    {
        if (!s_haveSaved)
        {
            return;
        }
        if (codeMenuLoaded() && s_savedActivation <= kActivationOff)
        {
            *reinterpret_cast<u32*>(s_activationAddr) = s_savedActivation;
            TM_LOG("code menu: in-match combo restored at %08x (%u)\n", s_activationAddr, s_savedActivation);
        }
        s_haveSaved = false;
        s_activationAddr = 0;
        s_savedActivation = 0;
    }

    bool codeMenuBlockedInTraining()
    {
        return main::settings().controls.allowCodeMenu == 0;
    }

    void codeMenuSync()
    {
        if (!s_attached || !trainingGameMode() || !codeMenuLoaded())
        {
            return;
        }
        if (!codeMenuBlockedInTraining())
        {
            restoreSaved();
            return;
        }
        if (!s_haveSaved)
        {
            u32 addr = findActivationValueAddr();
            if (addr == 0)
            {
                if (!s_missingLogged)
                {
                    s_missingLogged = true;
                    TM_LOG("code menu: activation line not found, left alone\n");
                }
                return;
            }
            u32 prev = wordAt(reinterpret_cast<const void*>(addr), 0);
            if (prev >= kActivationOff)
            {
                // Already OFF (2), or out of range (rejected by isActivationLine):
                // leave the player's own value untouched.
                return;
            }
            s_activationAddr = addr;
            s_savedActivation = prev;
            s_haveSaved = true;
        }
        if (wordAt(reinterpret_cast<const void*>(s_activationAddr), 0) != kActivationOff)
        {
            *reinterpret_cast<u32*>(s_activationAddr) = kActivationOff;
            TM_LOG("code menu: in-match combo blocked at %08x (%u -> %u)\n", s_activationAddr, s_savedActivation,
                   kActivationOff);
        }
    }

    void codeMenuMatchAttach()
    {
        if (!trainingGameMode())
        {
            s_attached = false;
            return;
        }
        s_attached = true;
        s_missingLogged = false;
        codeMenuSync();
    }

    void codeMenuMatchDetach()
    {
        restoreSaved();
        s_attached = false;
        s_missingLogged = false;
    }
}
}
