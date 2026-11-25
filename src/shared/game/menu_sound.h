#pragma once

#include <types.h>

// The game's own menu sounds, asked for the way its menus ask for them:
// sndSystem::playSE(id, -1, 0, 0, -1) on g_sndSystem. Read in the game's
// code (RSBE01 rev 2): Training's pause menu opens with 5 (0x8010567C),
// closes with 6 (0x80105818), moves its cursor and changes its options with
// 0 (0x80105750 and the calls after it) and confirms with 1 (0x80106000);
// the match's pause window plays 2 on B (0x800BB128) and 3 for a choice it
// refuses (0x800BB5F0). The names are those of snd/snd_id.h.
namespace tm {
namespace game {
    enum MenuSound {
        kSoundMove = 0,    // snd_se_system_cursor: the cursor moved, a value changed
        kSoundConfirm = 1, // snd_se_system_fixed: a page opened, a function run
        kSoundBack = 2,    // snd_se_system_cancel: back, or no
        kSoundError = 3,   // snd_se_system_Beep: not available
        kSoundOpen = 5,    // snd_se_system_InfoWindow_Open: the menu opens
        kSoundClose = 6    // snd_se_system_InfoWindow_Close: the menu closes
    };

    // Clear playSE's stage AUX_A reverb send (0x80074D40-54) for menu and
    // drill sounds; 0 leaves it on, matching Brawl's Training pause menu.
    enum { kDryMenuSounds = 1 };

    // g_sndSystem, playSE and BasicSound::SetFxSend in lib/BrawlHeaders/RSBE01.lst
    // (snd/snd_system.h does not compile without warnings here).
    enum {
        kSndSystemPointer = 0x805A01D0,
        kPlaySe = 0x800742B0,
        kBasicSoundSetFxSend = 0x801BD4A4,
        kSeSlotTable = 724,  // g_sndSystem + 0x2D4 (32 slots, 0x20 each)
        kSeSlotStride = 32,  // +0x00 BasicSound*, +0x04 handle (0x80077DB0)
        kSeSlotCount = 32,
        kAuxA = 0
    };

    inline void clearReverbSend(void* system, int handle)
    {
        if (system == NULL || handle < 0)
        {
            return;
        }
        u32 idx = static_cast<u32>(handle) & 0xFFu;
        if (idx >= kSeSlotCount)
        {
            return;
        }
        u8* slot = static_cast<u8*>(system) + kSeSlotTable + idx * kSeSlotStride;
        s32 stored = *reinterpret_cast<const s32*>(slot + 4);
        void* sound = *reinterpret_cast<void* const*>(slot);
        if (stored == handle && sound != NULL)
        {
            typedef void (*SetFxSendFn)(void* sound, int bus, float send);
            reinterpret_cast<SetFxSendFn>(kBasicSoundSetFxSend)(sound, kAuxA, 0.0f);
        }
    }

    // The sound's handle, or -1 if refused; dry clears the stage AUX_A send.
    inline int playSound(int id, bool dry)
    {
        typedef int (*PlaySeFn)(void* system, int id, int, int, int, int);
        void* system = *reinterpret_cast<void**>(kSndSystemPointer);
        if (system == NULL)
        {
            return -1;
        }
        int handle = reinterpret_cast<PlaySeFn>(kPlaySe)(system, id, -1, 0, 0, -1);
        if (dry)
        {
            clearReverbSend(system, handle);
        }
        return handle;
    }

    // The sound's handle, or -1 if the game did not take it.
    inline int playMenuSound(int id)
    {
        return playSound(id, kDryMenuSounds != 0);
    }
}
}
