#include "main/game/training_damage.h"

#include <OS/OSCache.h>
#include <if/if_mngr.h>
#include <types.h>

#include "shared/game/mem.h"
#include "shared/log.h"

// Keeps the CPU's percent across Training's pause menu. While CPU Damage is
// unchanged since the pause opened, the pause's three calls (kCalls:
// setDamage, setStartDamage, the HUD's setter) are patched out of game code.
// Details: docs/internals.md, "The CPU's damage after Training's pause".
namespace tm {
namespace game {
    enum {
        kIfTraining = 0x78,
        kCpuDamage = 0x28,
        kBctrl = 0x4E800421,
        kNop = 0x60000000,
        kSites = 3
    };
    static const u32 kHudSite = 0x809625EC;
    static const u32 kHudSetter = 0x800E14A4;
    static const u32 kBlHud = 0x48000001 | ((kHudSetter - kHudSite) & 0x03FFFFFC); // bl kHudSetter
    // setDamage, setStartDamage, the HUD's damage
    static const u32 kCalls[kSites] = {0x80962594, 0x809625BC, kHudSite};
    static const u32 kGameWords[kSites] = {kBctrl, kBctrl, kBlHud};

    static bool s_out = false;       // the calls are taken out
    static bool s_refused = false;   // a site held something else: left alone
    static bool s_haveValue = false; // s_value is CPU Damage before the pause
    static s32 s_value = 0;

    static void writeCode(u32 at, u32 word)
    {
        *reinterpret_cast<u32*>(at) = word;
        DCFlushRange(reinterpret_cast<void*>(at), 4);
        ICInvalidateRange(reinterpret_cast<void*>(at), 4);
    }

    // Puts the calls in or takes them out. The words at the sites are read
    // every time, not trusted from the last write: an emulator savestate
    // restores code too. Whether the lab's own savestates can change them is
    // unchecked (main/savestate/snapshot.cpp lists the heaps it copies).
    static void setOut(bool out)
    {
        if (s_refused)
        {
            return;
        }
        for (int i = 0; i < kSites; i++)
        {
            u32 word = *reinterpret_cast<const u32*>(kCalls[i]);
            if (word != kGameWords[i] && word != kNop)
            {
                s_refused = true;
                TM_LOG("training damage: call site %08x holds %08x, not the game's call: left alone\n", kCalls[i],
                       word);
                return;
            }
        }
        for (int i = 0; i < kSites; i++)
        {
            u32 want = out ? kNop : kGameWords[i];
            if (*reinterpret_cast<const u32*>(kCalls[i]) != want)
            {
                writeCode(kCalls[i], want);
            }
        }
        if (out != s_out)
        {
            s_out = out;
            TM_LOG("training damage: the pause's CPU Damage write %s\n", out ? "taken out" : "put back");
        }
    }

    static bool cpuDamageValue(s32& out)
    {
        IfMngr* manager = g_IfMngr;
        if (!isRam(manager))
        {
            return false;
        }
        const void* menu = reinterpret_cast<const void*>(wordAt(manager, kIfTraining));
        if (!isRam(menu))
        {
            return false;
        }
        out = static_cast<s32>(wordAt(menu, kCpuDamage));
        return true;
    }

    void trainingDamageStep(const MatchState& m)
    {
        s32 value = 0;
        if (!m.training || !cpuDamageValue(value))
        {
            s_haveValue = false;
            setOut(false);
            return;
        }
        if (!m.paused || !s_haveValue)
        {
            s_value = value;
            s_haveValue = true;
        }
        bool changed = m.paused && value != s_value;
        setOut(!changed);
    }

    void trainingDamageRestore()
    {
        s_haveValue = false;
        setOut(false);
        // Looked at again in the next match.
        s_refused = false;
    }
}
}
