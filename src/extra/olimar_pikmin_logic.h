#pragma once

#include <stddef.h>
#include <types.h>

#include "main/settings.h"

// Pure Olimar / Pikmin lab logic (no Brawl headers): bit-packed settings
// helpers, legacy migration, pull-pool weights with all-0 safety clamp,
// per-color maturity resolution, Down-B whistle seed math, 16 competitive
// lineup presets, live B+D-Pad pluck mapping, D-pad suppression gating, and
// crash-free mid-match lineup swap state gates + 15-frame pending latch.
namespace tm {
namespace extra {
    enum OlimarColor {
        kOliColorRed = 0,
        kOliColorYellow = 1,
        kOliColorBlue = 2,
        kOliColorWhite = 3,
        kOliColorPurple = 4,
        kOliColorCount = 5
    };

    enum OlimarDpadDir {
        kOliDirUp = 0,
        kOliDirDown = 1,
        kOliDirLeft = 2,
        kOliDirRight = 3,
        kOliDirCount = 4
    };

    // Match gfPadButtons D-pad bit positions (lab.cpp:71-75).
    enum OlimarPadBit {
        kOliPadLeft = 0x1u,
        kOliPadRight = 0x2u,
        kOliPadDown = 0x4u,
        kOliPadUp = 0x8u,
        kOliPadAll = 0xFu
    };

    // Olimar & Pikmin status / work-variable IDs verified in ft_pikmin.rel.
    enum OlimarStatusAndVar {
        kOliStatusCatchFirst = 0x34,
        kOliStatusThrowLast = 0x40,
        kOliStatusDamageFirst = 0x42,
        kOliStatusDamageLast = 0x55,
        kOliStatusAirLassoFirst = 0x7F,
        kOliStatusAirLassoLast = 0x83,
        kOliStatusDeadFirst = 0xBB,
        kOliStatusRebirth = 0xBE,
        kOliStatusEntry = 0x10E,
        kOliStatusSpecialN = 0x112,
        kOliStatusSpecialS = 0x113,
        kOliStatusSpecialHi = 0x114,
        kOliStatusSpecialLw = 0x115,
        kOliStatusFinalFirst = 0x116,
        kOliStatusFinalLast = 0x11B,
        // Olimar pull history work variables (LA-Basic[64..65]).
        kOliVarLastPull = 0x10000040,
        kOliVarPrevPull = 0x10000041,
        // wnPikminPikmin color work variable (LA-Basic[5]).
        kOliVarPikminColor = 0x10000005,
        // PikminManager embedded offsets inside ftPikmin (ft_pikmin.rel:0x00138).
        kOliMgrOffset = 0x1FD08,
        kOliMgrFromGlowOffset = 0x1CF4,
        kOliMgrList1Offset = 0x80,
        kOliMgrList2Offset = 0x100,
        kOliMgrWhistleColorOffset = 0x180,
        kOliArrayListVptr = 0x80B19960u,
        kOliPendingMaxFrames = 15
    };

    struct OlimarLineupSpec {
        s8 colors[4]; // 0..4 explicit color, -1 draw from active pull pool
        u8 count;     // 0..4 Pikmin to spawn
    };

    struct OlimarPendingLatch {
        s8 presetId; // -1 = none, 0..15 = pending preset
        u8 ttl;      // frames remaining (up to kOliPendingMaxFrames)

        void clear()
        {
            presetId = -1;
            ttl = 0;
        }

        void latch(s8 id)
        {
            if (id >= 0 && id < main::kOlimarPresetCount)
            {
                presetId = id;
                ttl = kOliPendingMaxFrames;
            }
        }

        // Returns preset ID to execute now when safe, or -1 while waiting/empty.
        int step(bool safeNow)
        {
            if (presetId < 0)
            {
                return -1;
            }
            if (safeNow)
            {
                int out = presetId;
                clear();
                return out;
            }
            if (ttl > 0)
            {
                ttl--;
            }
            if (ttl == 0)
            {
                presetId = -1;
            }
            return -1;
        }
    };

    inline u8 olimarRawPullMask(const main::FighterMechanicsSettings& s)
    {
        return static_cast<u8>(s.olimarPullAndWhistle & main::kOlimarPullPoolAll);
    }

    inline void olimarSetRawPullMask(main::FighterMechanicsSettings& s, u8 mask)
    {
        s.olimarPullAndWhistle =
            static_cast<u8>((s.olimarPullAndWhistle & ~main::kOlimarPullPoolAll) | (mask & main::kOlimarPullPoolAll));
    }

    // Effective 5-bit pull pool mask (guaranteed non-zero so 0x80a6b864 never
    // returns -1 and aborts plucking). Preserves legacy olimarPikmin saves.
    inline u8 olimarEffectivePullMask(const main::FighterMechanicsSettings& s)
    {
        u8 pool = olimarRawPullMask(s);
        u8 leg = s.olimarPikmin;
        if ((pool == 0u || pool == main::kOlimarPullPoolAll) && leg >= 1u && leg < main::kOlimarPikminCount)
        {
            return static_cast<u8>(1u << (leg - 1u));
        }
        return pool != 0u ? pool : static_cast<u8>(main::kOlimarPullPoolAll);
    }

    inline void olimarPullPoolWeights(const main::FighterMechanicsSettings& s, const float orig[5], float out[5])
    {
        u8 mask = olimarEffectivePullMask(s);
        if (mask == main::kOlimarPullPoolAll)
        {
            for (int i = 0; i < kOliColorCount; i++)
            {
                out[i] = orig[i];
            }
            return;
        }
        for (int i = 0; i < kOliColorCount; i++)
        {
            out[i] = ((mask >> i) & 1u) != 0u ? orig[i] : 0.0f;
        }
    }

    inline void olimarSingleColorWeights(int color, float out[5])
    {
        for (int i = 0; i < kOliColorCount; i++)
        {
            out[i] = (i == color) ? 1.0f : 0.0f;
        }
    }

    inline u8 olimarWhistleColor(const main::FighterMechanicsSettings& s)
    {
        u8 v = static_cast<u8>((s.olimarPullAndWhistle >> 5) & 0x7u);
        return v < main::kOlimarPikminCount ? v : 0u;
    }

    inline void olimarSetWhistleColor(main::FighterMechanicsSettings& s, u8 choice)
    {
        u8 v = choice < main::kOlimarPikminCount ? choice : 0u;
        s.olimarPullAndWhistle =
            static_cast<u8>((s.olimarPullAndWhistle & main::kOlimarPullPoolAll) | (v << 5));
    }

    // ft_pikmin.rel:0x24e78 places (m_whistleLastLeadColor + 1) % 5 at index 0.
    inline s32 olimarWhistleSeedForColor(int color)
    {
        return (color >= 0 && color < kOliColorCount) ? static_cast<s32>((color + 4) % 5) : 4;
    }

    inline u8 olimarColorMaturityRaw(const main::FighterMechanicsSettings& s, int color)
    {
        if (static_cast<u32>(color) >= kOliColorCount)
        {
            return 0u;
        }
        return static_cast<u8>((s.olimarColorMatAndMode >> (color * 2)) & 0x3u);
    }

    inline void olimarSetColorMaturityRaw(main::FighterMechanicsSettings& s, int color, u8 choice)
    {
        if (static_cast<u32>(color) >= kOliColorCount)
        {
            return;
        }
        u16 shift = static_cast<u16>(color * 2);
        u16 v = static_cast<u16>((choice < main::kOlimarMaturityCount ? choice : 0u) & 0x3u);
        s.olimarColorMatAndMode = static_cast<u16>((s.olimarColorMatAndMode & ~(0x3u << shift)) | (v << shift));
    }

    // Returns target maturity (0 Leaf, 1 Bud, 2 Flower) or -1 for Default
    // (falls back to global olimarMaturity when the per-color slot is 0).
    inline int olimarResolvedMaturityTarget(const main::FighterMechanicsSettings& s, int color)
    {
        if (static_cast<u32>(color) >= kOliColorCount)
        {
            return -1;
        }
        u8 per = olimarColorMaturityRaw(s, color);
        if (per != 0u)
        {
            return static_cast<int>(per) - 1;
        }
        u8 glob = s.olimarMaturity < main::kOlimarMaturityCount ? s.olimarMaturity : 0u;
        return glob == 0u ? -1 : static_cast<int>(glob) - 1;
    }

    inline u8 olimarEffectiveColorMaturityChoice(const main::FighterMechanicsSettings& s, int color)
    {
        int t = olimarResolvedMaturityTarget(s, color);
        return t < 0 ? 0u : static_cast<u8>(t + 1);
    }

    inline bool olimarHasAnyMaturityLock(const main::FighterMechanicsSettings& s)
    {
        return (s.olimarMaturity > 0u && s.olimarMaturity < main::kOlimarMaturityCount)
               || (s.olimarColorMatAndMode & 0x03FFu) != 0u;
    }

    inline u8 olimarDpadMode(const main::FighterMechanicsSettings& s)
    {
        u8 v = static_cast<u8>((s.olimarColorMatAndMode >> 10) & 0x3u);
        return v < main::kOlimarDpadModeCount ? v : 0u;
    }

    inline void olimarSetDpadMode(main::FighterMechanicsSettings& s, u8 mode)
    {
        u16 v = static_cast<u16>((mode < main::kOlimarDpadModeCount ? mode : 0u) & 0x3u);
        s.olimarColorMatAndMode = static_cast<u16>((s.olimarColorMatAndMode & ~(0x3u << 10)) | (v << 10));
    }

    inline u8 olimarStartSlot(const main::FighterMechanicsSettings& s, int slotIdx)
    {
        if (static_cast<u32>(slotIdx) >= 4u)
        {
            return 0u;
        }
        u8 v = static_cast<u8>((s.olimarStartLineup >> (slotIdx * 3)) & 0x7u);
        return v < main::kOlimarStartSlotCount ? v : 0u;
    }

    inline void olimarSetStartSlot(main::FighterMechanicsSettings& s, int slotIdx, u8 val)
    {
        if (static_cast<u32>(slotIdx) >= 4u)
        {
            return;
        }
        u16 shift = static_cast<u16>(slotIdx * 3);
        u16 v = static_cast<u16>((val < main::kOlimarStartSlotCount ? val : 0u) & 0x7u);
        s.olimarStartLineup = static_cast<u16>((s.olimarStartLineup & ~(0x7u << shift)) | (v << shift));
    }

    inline bool olimarStartLineupEnabled(const main::FighterMechanicsSettings& s)
    {
        return (s.olimarStartLineup & 0x0FFFu) != 0u;
    }

    inline u8 olimarDpadPreset(const main::FighterMechanicsSettings& s, int dir)
    {
        if (static_cast<u32>(dir) >= kOliDirCount)
        {
            return 0u;
        }
        return static_cast<u8>((s.olimarDpadPresets >> (dir * 4)) & 0xFu);
    }

    inline void olimarSetDpadPreset(main::FighterMechanicsSettings& s, int dir, u8 presetId)
    {
        if (static_cast<u32>(dir) >= kOliDirCount)
        {
            return;
        }
        u16 shift = static_cast<u16>(dir * 4);
        u16 v = static_cast<u16>(presetId & 0xFu);
        s.olimarDpadPresets = static_cast<u16>((s.olimarDpadPresets & ~(0xFu << shift)) | (v << shift));
    }

    inline void olimarSanitizeSettings(main::FighterMechanicsSettings& s)
    {
        if (s.olimarPikmin >= main::kOlimarPikminCount)
        {
            s.olimarPikmin = 0u;
        }
        if (s.olimarMaturity >= main::kOlimarMaturityCount)
        {
            s.olimarMaturity = 0u;
        }
        u8 whistle = static_cast<u8>((s.olimarPullAndWhistle >> 5) & 0x7u);
        if (whistle >= main::kOlimarPikminCount)
        {
            s.olimarPullAndWhistle &= main::kOlimarPullPoolAll;
        }
        u8 mode = static_cast<u8>((s.olimarColorMatAndMode >> 10) & 0x3u);
        if (mode >= main::kOlimarDpadModeCount)
        {
            s.olimarColorMatAndMode &= static_cast<u16>(~(0x3u << 10));
        }
        s.olimarColorMatAndMode &= 0x0FFFu;
        u16 cleanStart = 0u;
        for (int i = 0; i < 4; i++)
        {
            u8 slot = static_cast<u8>((s.olimarStartLineup >> (i * 3)) & 0x7u);
            if (slot >= main::kOlimarStartSlotCount)
            {
                slot = 0u;
            }
            cleanStart |= static_cast<u16>(slot << (i * 3));
        }
        s.olimarStartLineup = cleanStart;
    }

    inline void olimarResolveStartLineup(const main::FighterMechanicsSettings& s, OlimarLineupSpec& out)
    {
        out.count = 0;
        for (int i = 0; i < 4; i++)
        {
            u8 slot = olimarStartSlot(s, i);
            if (slot == main::kStartSlotEmpty)
            {
                continue;
            }
            out.colors[out.count++] = (slot == main::kStartSlotDefault) ? static_cast<s8>(-1)
                                                                        : static_cast<s8>(slot - 1);
        }
    }

    // Resolves preset ID 0..15 into an ordered Pikmin lineup (index 0 = lead).
    inline void olimarResolvePreset(u8 presetId, const main::FighterMechanicsSettings& s, OlimarLineupSpec& out)
    {
        u8 id = static_cast<u8>(presetId & 0xFu);
        if (id == 0u)
        {
            olimarResolveStartLineup(s, out);
            return;
        }
        if (id <= 5u)
        {
            s8 c = static_cast<s8>(id - 1u);
            out.count = 4;
            for (int i = 0; i < 4; i++)
            {
                out.colors[i] = c;
            }
            return;
        }
        struct PresetDef {
            u8 count;
            s8 colors[4];
        };
        static const PresetDef kPresets[10] = {
            {4, {kOliColorYellow, kOliColorYellow, kOliColorPurple, kOliColorPurple}}, // 6: 2Y + 2P
            {4, {kOliColorPurple, kOliColorPurple, kOliColorWhite,  kOliColorWhite}},  // 7: 2P + 2W
            {4, {kOliColorRed,    kOliColorRed,    kOliColorPurple, kOliColorPurple}}, // 8: 2R + 2P
            {4, {kOliColorBlue,   kOliColorBlue,   kOliColorPurple, kOliColorPurple}}, // 9: 2B + 2P
            {4, {kOliColorPurple, kOliColorYellow, kOliColorWhite,  kOliColorRed}},    // 10: P+Y+W+R
            {4, {kOliColorYellow, kOliColorPurple, kOliColorBlue,   kOliColorRed}},    // 11: Y+P+B+R
            {2, {kOliColorYellow, kOliColorPurple, -1,              -1}},              // 12: Y + P
            {2, {kOliColorPurple, kOliColorWhite,  -1,              -1}},              // 13: P + W
            {1, {kOliColorPurple, -1,              -1,              -1}},              // 14: 1 Purple
            {1, {kOliColorYellow, -1,              -1,              -1}}               // 15: 1 Yellow
        };
        const PresetDef& p = kPresets[id - 6u];
        out.count = p.count;
        for (int i = 0; i < 4; i++)
        {
            out.colors[i] = p.colors[i];
        }
    }

    // Live B + D-Pad Pluck color mapping:
    //   D-Up -> Yellow (1), D-Left -> Blue (2), D-Right -> White (3), D-Down -> Purple (4),
    //   No D-pad -> Red (0) when full Pull Pool is active, else -1 (use Pull Pool).
    inline int olimarPluckColorForDpad(const main::FighterMechanicsSettings& s, u32 dpadHeld)
    {
        if ((dpadHeld & kOliPadUp) != 0u) return kOliColorYellow;
        if ((dpadHeld & kOliPadLeft) != 0u) return kOliColorBlue;
        if ((dpadHeld & kOliPadRight) != 0u) return kOliColorWhite;
        if ((dpadHeld & kOliPadDown) != 0u) return kOliColorPurple;
        return olimarEffectivePullMask(s) == main::kOlimarPullPoolAll ? kOliColorRed : -1;
    }

    // Maps a freshly pressed D-pad bitmask to direction index 0..3, or -1 if none.
    inline int olimarPresetDirFromDpad(u32 dpadPressed)
    {
        if ((dpadPressed & kOliPadUp) != 0u) return kOliDirUp;
        if ((dpadPressed & kOliPadDown) != 0u) return kOliDirDown;
        if ((dpadPressed & kOliPadLeft) != 0u) return kOliDirLeft;
        if ((dpadPressed & kOliPadRight) != 0u) return kOliDirRight;
        return -1;
    }

    // True when Olimar D-Pad Mode owns the D-pad (suppressing Savestates, Taunts,
    // and Lab D-pad shortcuts, while keeping L+D-Pad CPU % active via !comboL).
    inline bool olimarShouldSuppressDpad(bool running, bool training, bool paused, bool comboL,
                                         u8 dpadMode, bool p1IsOlimar)
    {
        return running && training && !paused && !comboL && p1IsOlimar && dpadMode != main::kOlimarDpadOff;
    }

    // Status blocklist protecting wnPikminChain (Article 2), Link slots 7..12,
    // captures/throws, hitstun, Entry, Rebirth, Final Smash, and specials.
    inline bool olimarStatusBlockedForSwap(int status)
    {
        if (status < 0) return true;
        if (status >= kOliStatusCatchFirst && status <= kOliStatusThrowLast) return true;
        if (status >= kOliStatusDamageFirst && status <= kOliStatusDamageLast) return true;
        if (status >= kOliStatusAirLassoFirst && status <= kOliStatusAirLassoLast) return true;
        if (status >= kOliStatusDeadFirst && status <= kOliStatusRebirth) return true;
        if (status == kOliStatusEntry) return true;
        if (status >= kOliStatusSpecialN && status <= kOliStatusFinalLast) return true;
        return false;
    }

    inline bool olimarCanSwapLineup(int status, bool hitlag, bool existArt1, bool existArt2,
                                    int list1Size, bool swapBusy)
    {
        if (swapBusy || hitlag || existArt1 || existArt2 || list1Size != 0)
        {
            return false;
        }
        return !olimarStatusBlockedForSwap(status);
    }

    inline bool olimarPikminStatusDying(int status)
    {
        return status == 14 || status == 15 || status == 46 || status == 47 || status == 51;
    }
}
}
