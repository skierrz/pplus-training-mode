#pragma once

#include <string.h>
#include <types.h>

// Pure host-testable 44-fighter Project+ 3.2.1 data-driven tech tracker core.
// Zero Brawl headers; compiles identically on PowerPC (MWCC 4.2) and PC host tests.
//
// All status IDs and work-variable indices are verified against BrawlHeaders
// (ft/fighter.h, ft/ft_entry.h) and Project+ 3.2.1 Source/:
//  - Clone ftKinds: Source/ProjectM/CloneEngine.asm:43-64 (Mewtwo 0x26, Roy 0x27, Knuckles 0x2D).
//  - Peach Float (0x11E -> 0x16, LA-Bit[62]): FitPeach.pac entry 0x11E.
//  - Mewtwo Hover (0x120 -> 0x16, LA-Bit[97]): FitMewtwo.pac entry 0x120.
//  - Ganondorf Float (0x112 -> 0x33): FitGanon.pac SpecialN float.
//  - DJC (0x0C -> 0x33): Source/Project+/Physics.asm:128-264.
//  - RAR (0x04 -> 0x08 -> 0x0A -> 0x0B -> 0x33 motion 0x64): ft/fighter.h.
//  - DACUS (0x26 -> 0x30): Source/Injects/MoveDef/Misc.asm:565-572.
//  - B-Reverse (0x112..0x115, LA-Basic[55] 1 -> 0 + lr flip): Constants.asm:105-106.
//  - Waveland / Wavedash (0x21 -> 0x19): Source/Project+/AirDodge.asm:77-302.
//  - AGT / Glide Toss (0x21/0x1F/0x20 -> 0x9B): Source/Injects/MoveDef/Tethers.asm:148-204.
//  - Glide Cancel (0x84/0x85 -> 0x33/0x0C/0x16/0x87): ft/fighter.h.
//  - Tether / Zair (0x7F / 0x81): Source/Injects/MoveDef/Tethers.asm:120-210.
//  - Wall Climb / Walljump (0x68/0x69 -> 0x67): ft/fighter.h, wall_info_core.h.
namespace tm {
namespace extra {
    enum TechTrackerConst {
        kTechTrackerMagic = 0x544D5454u, // "TMTT"
        kTechTrackerVersion = 1u,
        kTechTrackerMaxPlayers = 4,
        kTechTrackerMaxEvents = 8,
        kTechTrackerRosterCount = 44,
        kTechTrackerMaxLabelLen = 24,
        kTechTrackerNoFrame = -1
    };

    // Common verified Brawl/P+ status IDs used by the generic evaluator.
    enum TechStatusId {
        kTtStRun = 0x04,
        kTtStRunBrake = 0x05,
        kTtStTurnRun = 0x08,
        kTtStJumpSquat = 0x0A,
        kTtStJump = 0x0B,
        kTtStJumpAerial = 0x0C,
        kTtStLanding = 0x16,
        kTtStLandingLight = 0x17,
        kTtStLandingAttackAir = 0x18,
        kTtStLandingFallSpecial = 0x19,
        kTtStEscapeF = 0x1F,
        kTtStEscapeB = 0x20,
        kTtStEscapeAir = 0x21,
        kTtStAttackDash = 0x26,
        kTtStAttackHi4Start = 0x30,
        kTtStAttackAir = 0x33,
        kTtStWallJump = 0x67,
        kTtStAttachWall = 0x68,
        kTtStAttachWallWait = 0x69,
        kTtStAirLasso = 0x7F,
        kTtStAirLassoHang = 0x81,
        kTtStGlideStart = 0x84,
        kTtStGlide = 0x85,
        kTtStGlideAttack = 0x87,
        kTtStItemThrow = 0x9B,
        kTtStSpecialN = 0x112,
        kTtStSpecialS = 0x113,
        kTtStSpecialHi = 0x114,
        kTtStSpecialLw = 0x115,
        kTtStFinal = 0x116,
        kTtStPeachFloat = 0x11E,
        kTtStMewtwoHover = 0x120,
        kTtMotionAttackAirN = 0x62,
        kTtMotionAttackAirB = 0x64,
        kTtMotionAttackAirLw = 0x66
    };

    enum FighterTechKind {
        kTechNone = 0,
        kTechSpecialJc,
        kTechFloatCancel,
        kTechFloatAerial,
        kTechDjc,
        kTechRar,
        kTechDacus,
        kTechBReverse,
        kTechWaveland,
        kTechAgt,
        kTechGlideCancel,
        kTechTetherZair,
        kTechWallClimb,
        kTechKindCount
    };

    struct FighterTechDef {
        u8 ftKind;
        u8 techKind;
        u16 paramA;
        u16 paramB;
        u8 laBit;
        u8 reserved;
        const char* label;
    };

    // Data-driven 44-fighter P+ 3.2.1 signature tech table (1:1 over all 44
    // playable ftKinds; Nana 0x10 aliases to Popo 0x0F via findFighterTechDef).
#ifdef __MWERKS__
    inline u32 findFighterTechDef(int ftKind, const FighterTechDef*& outDef)
    {
        if (ftKind == 0x10)
        {
            ftKind = 0x0F;
        }
        // Packed 4-byte rows (176 B vs 528 B) + NUL-separated label pool (20 unique labels).
        // variant: 0=default for techKind; SpecialJc: 0=Lw->JS, 1=S->JS, 2=Hi->Air; FloatCancel: 0=Peach, 1=Mewtwo.
        struct PackedTechRow { u8 ftKind, techKind, variant, labelIdx; };
        static const PackedTechRow kRows[kTechTrackerRosterCount] = {
            {0x00, kTechRar,         0,  0}, {0x01, kTechRar,         0,  1}, {0x02, kTechAgt,         0,  2},
            {0x03, kTechTetherZair,  0,  3}, {0x04, kTechDjc,         0,  4}, {0x05, kTechBReverse,    0,  5},
            {0x06, kTechSpecialJc,   0,  6}, {0x07, kTechBReverse,    0,  5}, {0x08, kTechWaveland,    0,  7},
            {0x09, kTechRar,         0,  0}, {0x0A, kTechDjc,         0,  4}, {0x0B, kTechDacus,       0,  8},
            {0x0C, kTechFloatCancel, 0,  9}, {0x0D, kTechBReverse,    0,  5}, {0x0E, kTechDacus,       0,  8},
            {0x0F, kTechWaveland,    0,  7}, {0x11, kTechWaveland,    0, 10}, {0x12, kTechWaveland,    0, 10},
            {0x13, kTechSpecialJc,   0,  6}, {0x14, kTechFloatAerial, 0, 11}, {0x15, kTechDacus,       0,  8},
            {0x16, kTechGlideCancel, 0, 12}, {0x17, kTechGlideCancel, 0, 12}, {0x18, kTechTetherZair,  0,  3},
            {0x19, kTechTetherZair,  0, 13}, {0x1A, kTechDjc,         0,  4}, {0x1B, kTechAgt,         0, 14},
            {0x1D, kTechGlideCancel, 0, 12}, {0x1E, kTechSpecialJc,   1, 15}, {0x1F, kTechTetherZair,  0, 16},
            {0x20, kTechDacus,       0,  8}, {0x21, kTechSpecialJc,   2, 17}, {0x22, kTechSpecialJc,   1, 18},
            {0x23, kTechAgt,         0, 19}, {0x25, kTechWaveland,    0, 10}, {0x26, kTechFloatCancel, 1, 20},
            {0x27, kTechRar,         0,  0}, {0x29, kTechAgt,         0,  2}, {0x2C, kTechSpecialJc,   0,  6},
            {0x2D, kTechWallClimb,   0, 21}, {0x2E, kTechDacus,       0, 22}, {0x2F, kTechSpecialJc,   0, 23},
            {0x30, kTechRar,         0,  0}, {0x31, kTechDacus,       0,  8}
        };
        static const u16 kKindParams[kTechKindCount][2] = {
            {0, 0}, {kTtStSpecialLw, kTtStJumpSquat}, {kTtStPeachFloat, kTtStLanding},
            {kTtStSpecialN, kTtStAttackAir}, {kTtStJumpAerial, kTtStAttackAir},
            {kTtStTurnRun, kTtStAttackAir}, {kTtStAttackDash, kTtStAttackHi4Start},
            {kTtStSpecialN, kTtStSpecialLw}, {kTtStEscapeAir, kTtStLandingFallSpecial},
            {kTtStEscapeAir, kTtStItemThrow}, {kTtStGlideStart, kTtStAttackAir},
            {kTtStAirLasso, kTtStAirLassoHang}, {kTtStAttachWall, kTtStWallJump}
        };
        static const char kLabels[] =
            "RAR Bair\0DK RAR Bair\0Bomb AGT\0Zair / Tether\0DJC Aerial\0B-Reverse\0"
            "JC Shine\0Wavedash\0DACUS\0Float Cancel\0Waveland\0Float Aerial\0"
            "Glide Cancel\0Tether\0Banana AGT\0Withdraw JC\0Tether / Zair\0Aura Cancel\0"
            "Quick Draw JC\0Gyro AGT\0Hover Cancel\0Wall Climb\0Snake DACUS\0Spin Dash JC";
        static FighterTechDef s_def;
        for (int i = 0; i < kTechTrackerRosterCount; i++)
        {
            if (static_cast<int>(kRows[i].ftKind) == ftKind)
            {
                const PackedTechRow& r = kRows[i];
                s_def.ftKind = r.ftKind;
                s_def.techKind = r.techKind;
                s_def.paramA = kKindParams[r.techKind][0];
                s_def.paramB = kKindParams[r.techKind][1];
                s_def.laBit = 0;
                s_def.reserved = 0;
                if (r.techKind == kTechSpecialJc)
                {
                    if (r.variant == 1) s_def.paramA = kTtStSpecialS;
                    else if (r.variant == 2) { s_def.paramA = kTtStSpecialHi; s_def.paramB = kTtStAttackAir; }
                }
                else if (r.techKind == kTechFloatCancel)
                {
                    s_def.paramA = r.variant != 0 ? kTtStMewtwoHover : kTtStPeachFloat;
                    s_def.laBit = r.variant != 0 ? 97 : 62;
                }
                const char* p = kLabels;
                for (u8 n = r.labelIdx; n > 0; n--)
                {
                    while (*p != '\0') p++;
                    p++;
                }
                s_def.label = p;
                outDef = &s_def;
                return static_cast<u32>(i + 1);
            }
        }
        outDef = NULL;
        return 0;
    }
#else
    inline const FighterTechDef* fighterTechTable()
    {
        static const FighterTechDef kTable[kTechTrackerRosterCount] = {
            {0x00, kTechRar,         kTtStTurnRun,     kTtStAttackAir,          0,  0, "RAR Bair"},
            {0x01, kTechRar,         kTtStTurnRun,     kTtStAttackAir,          0,  0, "DK RAR Bair"},
            {0x02, kTechAgt,         kTtStEscapeAir,   kTtStItemThrow,          0,  0, "Bomb AGT"},
            {0x03, kTechTetherZair,  kTtStAirLasso,    kTtStAirLassoHang,       0,  0, "Zair / Tether"},
            {0x04, kTechDjc,         kTtStJumpAerial,  kTtStAttackAir,          0,  0, "DJC Aerial"},
            {0x05, kTechBReverse,    kTtStSpecialN,    kTtStSpecialLw,          0,  0, "B-Reverse"},
            {0x06, kTechSpecialJc,   kTtStSpecialLw,   kTtStJumpSquat,          0,  0, "JC Shine"},
            {0x07, kTechBReverse,    kTtStSpecialN,    kTtStSpecialLw,          0,  0, "B-Reverse"},
            {0x08, kTechWaveland,    kTtStEscapeAir,   kTtStLandingFallSpecial, 0,  0, "Wavedash"},
            {0x09, kTechRar,         kTtStTurnRun,     kTtStAttackAir,          0,  0, "RAR Bair"},
            {0x0A, kTechDjc,         kTtStJumpAerial,  kTtStAttackAir,          0,  0, "DJC Aerial"},
            {0x0B, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "DACUS"},
            {0x0C, kTechFloatCancel, kTtStPeachFloat,  kTtStLanding,            62, 0, "Float Cancel"},
            {0x0D, kTechBReverse,    kTtStSpecialN,    kTtStSpecialLw,          0,  0, "B-Reverse"},
            {0x0E, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "DACUS"},
            {0x0F, kTechWaveland,    kTtStEscapeAir,   kTtStLandingFallSpecial, 0,  0, "Wavedash"},
            {0x11, kTechWaveland,    kTtStEscapeAir,   kTtStLandingFallSpecial, 0,  0, "Waveland"},
            {0x12, kTechWaveland,    kTtStEscapeAir,   kTtStLandingFallSpecial, 0,  0, "Waveland"},
            {0x13, kTechSpecialJc,   kTtStSpecialLw,   kTtStJumpSquat,          0,  0, "JC Shine"},
            {0x14, kTechFloatAerial, kTtStSpecialN,    kTtStAttackAir,          0,  0, "Float Aerial"},
            {0x15, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "DACUS"},
            {0x16, kTechGlideCancel, kTtStGlideStart,  kTtStAttackAir,          0,  0, "Glide Cancel"},
            {0x17, kTechGlideCancel, kTtStGlideStart,  kTtStAttackAir,          0,  0, "Glide Cancel"},
            {0x18, kTechTetherZair,  kTtStAirLasso,    kTtStAirLassoHang,       0,  0, "Zair / Tether"},
            {0x19, kTechTetherZair,  kTtStAirLasso,    kTtStAirLassoHang,       0,  0, "Tether"},
            {0x1A, kTechDjc,         kTtStJumpAerial,  kTtStAttackAir,          0,  0, "DJC Aerial"},
            {0x1B, kTechAgt,         kTtStEscapeAir,   kTtStItemThrow,          0,  0, "Banana AGT"},
            {0x1D, kTechGlideCancel, kTtStGlideStart,  kTtStAttackAir,          0,  0, "Glide Cancel"},
            {0x1E, kTechSpecialJc,   kTtStSpecialS,    kTtStJumpSquat,          0,  0, "Withdraw JC"},
            {0x1F, kTechTetherZair,  kTtStAirLasso,    kTtStAirLassoHang,       0,  0, "Tether / Zair"},
            {0x20, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "DACUS"},
            {0x21, kTechSpecialJc,   kTtStSpecialHi,   kTtStAttackAir,          0,  0, "Aura Cancel"},
            {0x22, kTechSpecialJc,   kTtStSpecialS,    kTtStJumpSquat,          0,  0, "Quick Draw JC"},
            {0x23, kTechAgt,         kTtStEscapeAir,   kTtStItemThrow,          0,  0, "Gyro AGT"},
            {0x25, kTechWaveland,    kTtStEscapeAir,   kTtStLandingFallSpecial, 0,  0, "Waveland"},
            {0x26, kTechFloatCancel, kTtStMewtwoHover, kTtStLanding,            97, 0, "Hover Cancel"},
            {0x27, kTechRar,         kTtStTurnRun,     kTtStAttackAir,          0,  0, "RAR Bair"},
            {0x29, kTechAgt,         kTtStEscapeAir,   kTtStItemThrow,          0,  0, "Bomb AGT"},
            {0x2C, kTechSpecialJc,   kTtStSpecialLw,   kTtStJumpSquat,          0,  0, "JC Shine"},
            {0x2D, kTechWallClimb,   kTtStAttachWall,  kTtStWallJump,           0,  0, "Wall Climb"},
            {0x2E, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "Snake DACUS"},
            {0x2F, kTechSpecialJc,   kTtStSpecialLw,   kTtStJumpSquat,          0,  0, "Spin Dash JC"},
            {0x30, kTechRar,         kTtStTurnRun,     kTtStAttackAir,          0,  0, "RAR Bair"},
            {0x31, kTechDacus,       kTtStAttackDash,  kTtStAttackHi4Start,     0,  0, "DACUS"}
        };
        return kTable;
    }

    // Returns 1-based tech ID (1..44) and `outDef` for `ftKind` (Nana 0x10
    // maps to Popo 0x0F). Returns 0 if `ftKind` is not a playable fighter.
    inline u32 findFighterTechDef(int ftKind, const FighterTechDef*& outDef)
    {
        if (ftKind == 0x10)
        {
            ftKind = 0x0F;
        }
        const FighterTechDef* table = fighterTechTable();
        for (int i = 0; i < kTechTrackerRosterCount; i++)
        {
            if (static_cast<int>(table[i].ftKind) == ftKind)
            {
                outDef = &table[i];
                return static_cast<u32>(i + 1);
            }
        }
        outDef = NULL;
        return 0;
    }
#endif

    struct TechStatusEv {
        s16 status;
        s16 prev;
        s16 motionKind;
    };

    struct TechFrameInput {
        s32 frame;
        int ftKind;
        s16 status;
        s16 motionKind;
        s16 hitstun;
        u8 airborne;
        u8 outOfPlay;
        u8 laBitActive; // value of LA-Bit[def.laBit] if def.laBit != 0
        s32 laBasic55;  // LA-Basic[55] B-Reverse countdown
        float lr;
        u8 eventCount;
        TechStatusEv events[kTechTrackerMaxEvents];
    };

    // Compact per-player session counters exposed in the "TMTT" RAM block.
    struct TechSlotStats {
        u32 attempts;
        u32 successes;
        u32 lastTechId;
        s32 lastFrame;
        u8 ftKind;
        u8 techKind;
        u8 active;
        u8 osdPosted;
    };

    struct TechTrackerBlock {
        u32 magic;      // 0x00 "TMTT"
        u32 version;    // 0x04 1
        u32 frame;      // 0x08 trackerFrame
        u32 resetCount; // 0x0C incremented on savestate / explicit reset
        TechSlotStats slots[kTechTrackerMaxPlayers];
    };

    // Per-fighter transient state machine (~24 bytes per slot).
    struct TechWatch {
        u8 phase;
        u8 sawSub;
        s16 lastFtKind;
        s32 startFrame;
        s32 lastRunFrame;
        s32 prevLa55;
        float savedLr;
    };

    inline void techWatchReset(TechWatch& w)
    {
        w.phase = 0;
        w.sawSub = 0;
        w.lastFtKind = -1;
        w.startFrame = kTechTrackerNoFrame;
        w.lastRunFrame = kTechTrackerNoFrame;
        w.prevLa55 = 0;
        w.savedLr = 1.0f;
    }

    inline void techSlotStatsReset(TechSlotStats& s)
    {
        s.attempts = 0;
        s.successes = 0;
        s.lastTechId = 0;
        s.lastFrame = kTechTrackerNoFrame;
        s.ftKind = 0xFF;
        s.techKind = kTechNone;
        s.active = 0;
        s.osdPosted = 0;
    }

    inline bool isTtAttackAirMotion(s16 mk)
    {
        return mk >= kTtMotionAttackAirN && mk <= kTtMotionAttackAirLw;
    }

    inline bool isTtSpecialStatus(s16 s)
    {
        return s >= kTtStSpecialN && s != kTtStFinal;
    }

    // Steps one finished frame for one fighter against its data-driven
    // FighterTechDef. Returns true on a newly completed signature tech (sets
    // `outLabel` to the table label and updates `stats`).
    inline bool stepFighterTech(const TechFrameInput& in, TechWatch& w, TechSlotStats& stats, const char*& outLabel)
    {
        outLabel = NULL;
        const FighterTechDef* def = NULL;
        const u32 techId = findFighterTechDef(in.ftKind, def);
        if (def == NULL)
        {
            return false;
        }
        if (w.lastFtKind != static_cast<s16>(in.ftKind))
        {
            techWatchReset(w);
            w.lastFtKind = static_cast<s16>(in.ftKind);
        }
        stats.ftKind = static_cast<u8>(in.ftKind);
        stats.techKind = def->techKind;
        stats.active = 1;

        if (in.outOfPlay || in.hitstun > 0)
        {
            w.phase = 0;
            w.sawSub = 0;
            w.prevLa55 = in.laBasic55;
            return false;
        }

        const int evN = in.eventCount < kTechTrackerMaxEvents ? static_cast<int>(in.eventCount) : kTechTrackerMaxEvents;
        bool succeeded = false;
        const s16 pA = static_cast<s16>(def->paramA);
        const s16 pB = static_cast<s16>(def->paramB);

        // Single outer event loop over the frame's status transitions keeps
        // code size compact across all 12 data-driven tech families.
        bool enteredSpecial = false;
        for (int k = 0; k < evN; k++)
        {
            const s16 s = in.events[k].status;
            const s16 prev = in.events[k].prev;
            switch (def->techKind)
            {
            case kTechSpecialJc:
                if (s == pA)
                {
                    w.phase = 1;
                    w.startFrame = in.frame;
                    stats.attempts++;
                }
                else if (w.phase == 1)
                {
                    bool hitTarget = (s == pB)
                                     || (pB == kTtStJumpSquat && (s == kTtStJump || s == kTtStJumpAerial))
                                     || (pB == kTtStAttackAir && (s == kTtStJumpSquat || s == kTtStJumpAerial));
                    if (hitTarget && (in.frame - w.startFrame) <= 180)
                    {
                        succeeded = true;
                        w.phase = 0;
                    }
                    else if (!isTtSpecialStatus(s))
                    {
                        w.phase = 0;
                    }
                }
                break;

            case kTechFloatCancel:
                if (s == pA)
                {
                    w.phase = 1;
                    w.sawSub = 0;
                    w.startFrame = in.frame;
                }
                else if (w.phase == 1 && prev == pA)
                {
                    if (!w.sawSub && isTtAttackAirMotion(in.events[k].motionKind))
                    {
                        w.sawSub = 1;
                        stats.attempts++;
                    }
                    if (s == pB || s == kTtStLandingLight)
                    {
                        if (w.sawSub != 0)
                        {
                            succeeded = true;
                        }
                        w.phase = 0;
                        w.sawSub = 0;
                    }
                    else if (s == kTtStLandingAttackAir || s != pA)
                    {
                        w.phase = 0;
                        w.sawSub = 0;
                    }
                }
                break;

            case kTechFloatAerial:
                if (s == pA && in.airborne != 0)
                {
                    w.phase = 1;
                    w.startFrame = in.frame;
                    stats.attempts++;
                }
                else if (w.phase == 1)
                {
                    if (s == pB)
                    {
                        succeeded = true;
                        w.phase = 0;
                    }
                    else if (s != pA)
                    {
                        w.phase = 0;
                    }
                }
                break;

            case kTechDjc:
                if (s == pA)
                {
                    w.phase = 1;
                    w.startFrame = in.frame;
                }
                else if (s == pB && (w.phase == 1 || prev == pA))
                {
                    stats.attempts++;
                    if (w.startFrame == kTechTrackerNoFrame || (in.frame - w.startFrame) <= 6)
                    {
                        succeeded = true;
                    }
                    w.phase = 0;
                }
                else if (w.phase == 1 && s != pA)
                {
                    w.phase = 0;
                }
                break;

            case kTechRar:
                if (s == kTtStTurnRun
                    && (prev == kTtStRun
                        || (prev == kTtStRunBrake && w.lastRunFrame != kTechTrackerNoFrame
                            && (in.frame - w.lastRunFrame) <= 4)))
                {
                    w.phase = 1;
                }
                else if (s == kTtStJumpSquat && w.phase == 1 && prev == kTtStTurnRun)
                {
                    w.phase = 2;
                    stats.attempts++;
                }
                else if (s == kTtStJump && w.phase == 2 && prev == kTtStJumpSquat)
                {
                    w.phase = (in.lr * w.savedLr < 0.0f) ? 3 : 0;
                    w.startFrame = in.frame;
                }
                else if (s == kTtStAttackAir && w.phase == 3)
                {
                    succeeded = (in.motionKind == kTtMotionAttackAirB);
                    w.phase = 0;
                }
                else if (w.phase == 1 || w.phase == 2)
                {
                    w.phase = 0;
                }
                break;

            case kTechDacus:
            case kTechWaveland:
                if (s == pA)
                {
                    w.phase = 1;
                    stats.attempts++;
                }
                else if (s == pB && prev == pA && w.phase == 1)
                {
                    succeeded = true;
                    w.phase = 0;
                }
                else if (w.phase == 1 && s != pA)
                {
                    w.phase = 0;
                }
                break;

            case kTechBReverse:
                if (isTtSpecialStatus(s) && !isTtSpecialStatus(prev))
                {
                    enteredSpecial = true;
                }
                break;

            case kTechAgt:
                if (s == kTtStEscapeAir || s == kTtStEscapeF || s == kTtStEscapeB)
                {
                    w.phase = 1;
                }
                else if (s == pB && (prev == kTtStEscapeAir || prev == kTtStEscapeF || prev == kTtStEscapeB || w.phase == 1))
                {
                    stats.attempts++;
                    succeeded = true;
                    w.phase = 0;
                }
                else if (w.phase == 1 && s != kTtStEscapeAir && s != kTtStEscapeF && s != kTtStEscapeB)
                {
                    w.phase = 0;
                }
                break;

            case kTechGlideCancel:
                if (s == kTtStGlideStart || s == kTtStGlide)
                {
                    if (w.phase == 0)
                    {
                        stats.attempts++;
                    }
                    w.phase = 1;
                }
                else if (w.phase == 1)
                {
                    succeeded = (s == kTtStAttackAir || s == kTtStJumpAerial || s == kTtStGlideAttack || s == kTtStLanding);
                    w.phase = 0;
                }
                break;

            case kTechTetherZair:
                if (s == pA || s == pB)
                {
                    stats.attempts++;
                    succeeded = true;
                }
                break;

            case kTechWallClimb:
                if (s == kTtStAttachWall || s == kTtStAttachWallWait)
                {
                    if (w.phase == 0)
                    {
                        stats.attempts++;
                    }
                    w.phase = 1;
                }
                else if (s == kTtStWallJump)
                {
                    if (w.phase == 0)
                    {
                        stats.attempts++;
                    }
                    succeeded = true;
                    w.phase = 0;
                }
                break;

            default:
                break;
            }
        }

        // Frame-end updates for multi-frame watches.
        if (def->techKind == kTechFloatCancel)
        {
            if (w.phase == 1 && in.status == pA)
            {
                if (!w.sawSub && isTtAttackAirMotion(in.motionKind))
                {
                    w.sawSub = 1;
                    stats.attempts++;
                }
            }
            else if (w.phase == 1)
            {
                w.phase = 0;
                w.sawSub = 0;
            }
        }
        else if (def->techKind == kTechRar)
        {
            if (w.phase == 3)
            {
                if (in.status == kTtStAttackAir)
                {
                    succeeded = (in.motionKind == kTtMotionAttackAirB);
                    w.phase = 0;
                }
                else if (!in.airborne || (in.frame - w.startFrame) > 35)
                {
                    w.phase = 0;
                }
            }
            if (in.status == kTtStRun)
            {
                w.lastRunFrame = in.frame;
                w.savedLr = in.lr;
            }
        }
        else if (def->techKind == kTechBReverse)
        {
            if ((enteredSpecial || (isTtSpecialStatus(in.status) && in.laBasic55 >= 4 && w.prevLa55 <= 0))
                && in.laBasic55 > 0)
            {
                w.phase = 1;
                w.savedLr = in.lr;
                stats.attempts++;
            }
            else if (w.phase == 1)
            {
                if (!isTtSpecialStatus(in.status))
                {
                    w.phase = 0;
                }
                else if (w.prevLa55 > 0 && in.laBasic55 == 0)
                {
                    succeeded = (in.lr * w.savedLr < 0.0f);
                    w.phase = 0;
                }
            }
        }
        else if (w.phase == 1)
        {
            bool keep = false;
            if (def->techKind == kTechSpecialJc)
            {
                keep = isTtSpecialStatus(in.status);
            }
            else if (def->techKind == kTechFloatAerial)
            {
                keep = in.airborne != 0 && in.status == pA;
            }
            else if (def->techKind == kTechDacus || def->techKind == kTechWaveland)
            {
                keep = in.status == pA;
            }
            else if (def->techKind == kTechGlideCancel)
            {
                keep = in.status == kTtStGlideStart || in.status == kTtStGlide;
            }
            else if (def->techKind == kTechWallClimb)
            {
                keep = in.airborne != 0;
            }
            else
            {
                keep = true;
            }
            if (!keep)
            {
                w.phase = 0;
            }
        }

        w.prevLa55 = in.laBasic55;
        if (succeeded)
        {
            stats.successes++;
            stats.lastTechId = techId;
            stats.lastFrame = in.frame;
            outLabel = def->label;
            return true;
        }
        return false;
    }
}
}
