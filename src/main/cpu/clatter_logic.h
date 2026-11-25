#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/cpu/fighter_input.h"

// Pure host-testable clatter / mash-rate engine for Project+ 3.2.1 non-grab
// clatter states (Bury, Ice, FuraFura / Shield Break, and Sleep / DamageSong).
// All status IDs verified against lib/BrawlHeaders/Brawl/Include/ft/fighter.h:
//   FuraFura_Stand = 0x59, FuraFura = 0x5A, FuraFura_End = 0x5B (lines 109-111)
//   Damage_Song_Start = 0x5C, Damage_Song = 0x5D, Damage_Song_End = 0x5E (lines 112-114)
//   Bury = 0xC7, Bury_Wait = 0xC8, Bury_Jump = 0xC9 (lines 219-221)
//   Ice = 0xCA, Ice_Jump = 0xCB (lines 222-223)
// Clatter reduction is driven by soControllerModuleImpl::updateClatter
// (sora_melee .text:0x00058488; stick clatter threshold param 3204 at 0x8076568C).
namespace tm {
namespace cpu {
    enum {
        kClatterStatusFuraFuraStand = 0x59,
        kClatterStatusFuraFura = 0x5A,
        kClatterStatusFuraFuraEnd = 0x5B,
        kClatterStatusDamageSongStart = 0x5C,
        kClatterStatusDamageSong = 0x5D,
        kClatterStatusDamageSongEnd = 0x5E,
        kClatterStatusBury = 0xC7,
        kClatterStatusBuryWait = 0xC8,
        kClatterStatusBuryJump = 0xC9,
        kClatterStatusIce = 0xCA,
        kClatterStatusIceJump = 0xCB,
        kClatterChanceMedium = 40,
        kClatterChanceHigh = 65
    };

    enum ClatterKind {
        kClatterNone = 0,
        kClatterBury,
        kClatterIce,
        kClatterFuraFura,
        kClatterSleep
    };

    // Classifies active non-grab clatter statuses where mashing accelerates
    // escape. Exit animations (Bury_Jump 0xC9, Ice_Jump 0xCB, FuraFura_End
    // 0x5B, Damage_Song_End 0x5E) return kClatterNone so mashing stops cleanly
    // the exact frame escape succeeds.
    inline int clatterKind(int status)
    {
        if (status == kClatterStatusBury || status == kClatterStatusBuryWait)
        {
            return kClatterBury;
        }
        if (status == kClatterStatusIce)
        {
            return kClatterIce;
        }
        if (status == kClatterStatusFuraFuraStand || status == kClatterStatusFuraFura)
        {
            return kClatterFuraFura;
        }
        if (status == kClatterStatusDamageSongStart || status == kClatterStatusDamageSong)
        {
            return kClatterSleep;
        }
        return kClatterNone;
    }

    inline bool isClatterStatus(int status)
    {
        return clatterKind(status) != kClatterNone;
    }

    inline bool isClatterExitStatus(int status)
    {
        return status == kClatterStatusBuryJump || status == kClatterStatusIceJump
               || status == kClatterStatusFuraFuraEnd || status == kClatterStatusDamageSongEnd;
    }

    // Per-frame mash probability (percent, 0..100) matching GrabEscape tiers.
    inline int clatterMashChance(int mode, int customMediumChance = 0)
    {
        if (mode == kMashPerfect)
        {
            return 100;
        }
        if (mode == kMashHigh)
        {
            return kClatterChanceHigh;
        }
        if (mode == kMashMedium)
        {
            return customMediumChance > 0 ? customMediumChance : static_cast<int>(kClatterChanceMedium);
        }
        return 0;
    }

    // Generates one frame of alternating button (A/B) + horizontal stick
    // (+/-1.0) clatter mash input when in an active clatter state. Returns
    // true if mash inputs were emitted on this frame.
    inline bool stepClatterMash(int mode, int status, int hitlag, u32 lastButtons, int rngRoll100, bool& mashFlip,
                                u32& outButtons, float& outX, float& outY, int customMediumChance = 0)
    {
        outButtons = 0;
        outX = 0.0f;
        outY = 0.0f;

        if (!isClatterStatus(status))
        {
            return false;
        }
        int chance = clatterMashChance(mode, customMediumChance);
        if (chance <= 0 || hitlag > 0 || rngRoll100 >= chance)
        {
            return false;
        }

        mashFlip = !mashFlip;
        u32 button = mashFlip ? button::kAttack : button::kSpecial;
        if ((lastButtons & button) != 0u)
        {
            button = (button == button::kAttack) ? button::kSpecial : button::kAttack;
        }
        outButtons = button;
        outX = mashFlip ? 1.0f : -1.0f;
        outY = 0.0f;
        return true;
    }
}
}
