#pragma once

#include <stddef.h>
#include <types.h>

#include "main/settings.h"

// Pure fighter-mechanics logic (no Brawl headers): maps FighterMechanicsSettings
// fields to P+ 3.2.1 fighter work-variable operations, Dedede Waddle Toss
// weights, and ftKind -> lab-menu row queries. Compiled by both the extra
// module (src/extra/fighter_mechanics.cpp) and host unit tests.
namespace tm {
namespace extra {
    // Active fighter kinds (ftKind in lib/BrawlHeaders/Brawl/Include/ft/ft_entry.h
    // and P+ 3.2.1 Source/ProjectM/CloneEngine.asm:43-64).
    enum FighterKindId {
        kFtMario = 0x00,
        kFtDonkeyKong = 0x01,
        kFtLink = 0x02,
        kFtSamus = 0x03,
        kFtLuigi = 0x08,
        kFtPeach = 0x0C,
        kFtSheik = 0x0E,
        kFtPopo = 0x0F,
        kFtNana = 0x10,
        kFtGameWatch = 0x12,
        kFtGanondorf = 0x14,
        kFtWario = 0x15,
        kFtOlimar = 0x19,
        kFtLucas = 0x1A,
        kFtSquirtle = 0x1E,
        kFtIvysaur = 0x1F,
        kFtDedede = 0x20,
        kFtLucario = 0x21,
        kFtRob = 0x23,
        kFtMewtwo = 0x26,
        kFtRoy = 0x27,
        kFtKnuckles = 0x2D,
        kFtSnake = 0x2E
    };

    // P+ 3.2.1 fighter work variables (soWorkManageModule IDs) and status IDs.
    enum FighterMechVar {
        // Peach (FitPeach.pac): float-used flag, float frames remaining (status 0x11E).
        kVarPeachFloatUsed = 0x1200003E,   // LA-Bit[62]
        kVarPeachFloatFrames = 0x20000002, // RA-Basic[2]
        kStatusPeachFloat = 0x11E,
        kPeachFloatMaxFrames = 150,

        // Mr. Game & Watch (ft_gamewatch.rel / FitGameWatch.pac): Oil Panic bucket.
        kVarGnwBucketCount = 0x11000013,  // LA-Float[19], 0.0..3.0
        kVarGnwBucketDamage = 0x11000014, // LA-Float[20], stored damage
        kVarGnwBucketFull = 0x1200003D,   // LA-Bit[61], full glow flag

        // Samus (FitSamus.pac): Charge Shot, Plasma/Ice Beam, Air Bomb count.
        kVarSamusCharge = 0x10000041,   // LA-Basic[65], 0..7
        kVarSamusBeam = 0x10000063,     // LA-Basic[99], 0=Plasma, 1=Ice
        kVarSamusAirBombs = 0x10000043, // LA-Basic[67], 0..2

        // Mewtwo (FitMewtwo.pac / ft_mewtwo.rel): Shadow Ball (LA-Basic[64..66]
        // + LA-Bit[61..62]), Hover active flag, used flag & frames (status 0x120).
        kVarMewtwoBallFrames = 0x10000040,  // LA-Basic[64], 0..127
        kVarMewtwoShadowBall = 0x10000041,  // LA-Basic[65], -1..6
        kVarMewtwoBallStored = 0x10000042,  // LA-Basic[66], -1..6
        kVarMewtwoBallHas = 0x1200003D,     // LA-Bit[61]
        kVarMewtwoBallFull = 0x1200003E,    // LA-Bit[62]
        kVarMewtwoHoverActive = 0x12000061, // LA-Bit[97]
        kVarMewtwoHoverUsed = 0x12000073,   // LA-Bit[115] (HasUsedHover, FitMewtwo.pac)
        kVarMewtwoHoverFrames = 0x20000002, // RA-Basic[2]
        kStatusMewtwoHover = 0x120,
        kMewtwoHoverMaxFrames = 60,

        // Lucario (FitLucario.pac / ft_lucario.rel): P+ Aura Charges (0..120),
        // Aura Sphere frame/level/stored level & flags (LA-Basic[64..66], LA-Bit[61..62]).
        kVarLucarioAura = 0x1100001B,         // LA-Float[27], 0.0 / 60.0 / 120.0
        kVarLucarioSphereFrames = 0x10000040, // LA-Basic[64], 0..16
        kVarLucarioSphere = 0x10000041,       // LA-Basic[65], -1..4
        kVarLucarioSphereStored = 0x10000042, // LA-Basic[66], -1..4
        kVarLucarioSphereHas = 0x1200003D,    // LA-Bit[61]
        kVarLucarioSphereFull = 0x1200003E,   // LA-Bit[62]

        // Wario (ft_wario.rel / FitWario.pac): Waft frames, stage, full flag.
        kVarWarioWaftFrames = 0x10000041, // LA-Basic[65], 0..3600
        kVarWarioWaftStage = 0x10000042,  // LA-Basic[66], 0..3
        kVarWarioWaftFull = 0x1200003F,   // LA-Bit[63]

        // Donkey Kong (FitDonkey.pac): Giant Punch swings (0..10).
        kVarDkPunch = 0x10000040, // LA-Basic[64]

        // Ivysaur (FitPokeFushigisou.pac): Solarbeam / Synthesis charge (0..26).
        kVarIvysaurSolarbeam = 0x1100001B, // LA-Float[27]

        // R.O.B. (FitRobot.pac): Air Boosts, Exhausted flag, Gyro, Robo Beam timer.
        // P+ 3.2.1 status 0x114 (SpecialHi) transitions directly to 0x118 (subaction 0x1d2),
        // consuming LA-Basic[75] (3 discrete air boosts) and LA-Bit[111] exclusively; Brawl's
        // legacy fuel tank LA-Float[21] (FT_ROBOT.REL) is unused in P+ 3.2.1.
        kVarRobAirBoosts = 0x1000004B,     // LA-Basic[75], 0..3
        kVarRobBoostExhausted = 0x1200006F, // LA-Bit[111]
        kVarRobGyro = 0x11000014,          // LA-Float[20], 0.0..90.0 (IC-Float[4047])
        kVarRobBeam = 0x11000013,          // LA-Float[19], <100 / 100..899 / >=900

        // Lucas (FitLucas.pac): Offense Up flags, charge progress & attack serial.
        kVarLucasAttackSerial = 0x10000035,   // LA-Basic[53] (Wait1 clears Offense Up while 0)
        kVarLucasOffenseFlag = 0x2200006B,    // RA-Bit[107]
        kVarLucasOffenseConsume = 0x2200006C, // RA-Bit[108]
        kVarLucasOffenseProg = 0x1100003D,    // LA-Float[61], 0.0..120.0
        kStatusAttackS4Start = 0x2A,
        kStatusAttackS4Hold = 0x2B,
        kStatusAttackS4 = 0x2C,

        // Squirtle (FitPokeZenigame.pac): Water Gun charge (0..60).
        kVarSquirtleWaterGun = 0x10000040, // LA-Basic[64]

        // Sheik (FitSheik.pac): Needles (0..6) & max-6 glow flag.
        kVarSheikNeedles = 0x10000040,     // LA-Basic[64]
        kVarSheikNeedlesFull = 0x1200003D, // LA-Bit[61]

        // King Dedede (ft_dedede.rel): non-Waddle-Dee cooldown.
        kVarDededeWaddleCd = 0x10000041, // LA-Basic[65]

        // Snake (FitSnake.pac): Tranq shots fired (0..3), Cypher air uses & flag.
        kVarSnakeTranq = 0x1000005D,      // LA-Basic[93]
        kVarSnakeCypherUses = 0x11000034, // LA-Float[52], 0.0..6.0
        kVarSnakeCypherUsed = 0x1200003D, // LA-Bit[61]

        // Ganondorf (FitGanon.pac): Dead Man's Float frame counter.
        kVarGanonFloatCount = 0x10000062 // LA-Basic[98]
    };

    enum WorkOpKind {
        kOpInt = 0,
        kOpFloat = 1,
        kOpFlag = 2
    };

    struct WorkOp {
        u8 kind; // WorkOpKind
        u8 flagValue;
        u32 id;
        s32 intValue;
        float floatValue;
    };

    enum CustomVisualKind {
        kVisNone = 0,
        kVisLucasOffense = 1, // FitLucas.pac ActExit[6] sub@0x28544: 0x1B0001 on bones 28 & 51
        kVisIvysaurBulb = 2,  // FitPokeFushigisou.pac Wait1 0x206cc: 0x84 on bone 33 (0.06 / 0.16)
        kVisLucarioAura = 3   // FitLucario.pac 0x29b40: 0x220001 on bones 31 & 53 (0.8 / 1.0)
    };

    enum {
        kCommonChargeGlow = 26, // Standard full-charge glow (G&W/Samus/Mewtwo/Lucario/DK/ROB/Squirtle/Sheik)
        kCommonWarioWaft = 39   // Wario Waft full glow (ft_wario.rel:0xf0b8, sora_melee:0x80a629a8)
    };

    struct FighterPlan {
        enum { kMaxOps = 8 };
        WorkOp ops[kMaxOps];
        int count;
        u8 commonEffectId; // 0 = leave native common effect alone; else 26 or 39
        u8 commonEffectOn; // 1 = ensure active, 0 = ensure removed (when commonEffectId != 0)
        u8 customVisual;   // CustomVisualKind (0 = leave custom follow effects alone)
        u8 customTier;     // 0 = off/empty, 1 = half/lvl1/charged, 2 = full/lvl2

        void clear()
        {
            count = 0;
            commonEffectId = 0;
            commonEffectOn = 0;
            customVisual = kVisNone;
            customTier = 0;
        }
#ifdef __MWERKS__
#pragma dont_inline on
#endif
        void addOp(u8 kind, u32 id, s32 iv, float fv, u8 flg)
        {
            if (count < kMaxOps)
            {
                WorkOp& o = ops[count++];
                o.kind = kind;
                o.flagValue = flg;
                o.id = id;
                o.intValue = iv;
                o.floatValue = fv;
            }
        }
        void addInt(u32 id, s32 v) { addOp(kOpInt, id, v, 0.0f, 0); }
        void addFloat(u32 id, float v) { addOp(kOpFloat, id, 0, v, 0); }
        void addFlag(u32 id, bool on) { addOp(kOpFlag, id, 0, 0.0f, on ? 1 : 0); }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
    };

    inline int clampChoice(u8 raw, int count)
    {
        return raw < static_cast<u8>(count) ? static_cast<int>(raw) : 0;
    }

    // True if `ftKind` has any row on the Character RNG / Fighter Mechanics page.
    inline bool fighterHasMechanicsRow(int ftKind)
    {
        static const u32 kLo = (1u << kFtDonkeyKong) | (1u << kFtSamus) | (1u << kFtLuigi) | (1u << kFtPeach)
                               | (1u << kFtSheik) | (1u << kFtGameWatch) | (1u << kFtGanondorf) | (1u << kFtWario)
                               | (1u << kFtOlimar) | (1u << kFtLucas) | (1u << kFtSquirtle) | (1u << kFtIvysaur);
        static const u32 kHi = (1u << (kFtDedede - 32)) | (1u << (kFtLucario - 32)) | (1u << (kFtRob - 32))
                               | (1u << (kFtMewtwo - 32)) | (1u << (kFtSnake - 32));
        if (ftKind < 0 || ftKind >= 64)
        {
            return false;
        }
        return ftKind < 32 ? ((kLo >> ftKind) & 1u) != 0 : ((kHi >> (ftKind - 32)) & 1u) != 0;
    }

    // Computes Olimar's Pikmin pluck weights ({Red, Yellow, Blue, White, Purple})
    // for FitPikmin.pac pullOutParam row 0 (data+0x7C -> +0x08..+0x18).
    // P+ 3.2.1 hooks 0x80a6bbdc (RSBE01.TXT:2963) so all non-HRC ground types use
    // row 0, and 0x80a6b864 draws from these five weights (falling back to
    // LA-Basic[64] when all other weights are 0, so consecutive plucks stay locked).
    inline void olimarPikminWeights(u8 rawChoice, const float orig[5], float out[5])
    {
        int choice = clampChoice(rawChoice, main::kOlimarPikminCount);
        if (choice == main::kPikminDefault)
        {
            for (int i = 0; i < 5; i++)
            {
                out[i] = orig[i];
            }
            return;
        }
        int target = choice - 1;
        for (int i = 0; i < 5; i++)
        {
            out[i] = (i == target) ? 1.0f : 0.0f;
        }
    }

    // Computes King Dedede's Waddle Toss table weights ({Dee, Doo, Gordo, Capsule})
    // and whether to zero LA-Basic[65] (non-Dee cooldown).
    inline void dededeWaddleWeights(u8 rawChoice, const float orig[4], float out[4], bool& clearCooldown)
    {
        int choice = clampChoice(rawChoice, main::kDededeWaddleCount);
        clearCooldown = false;
        if (choice == main::kWaddleDefault)
        {
            for (int i = 0; i < 4; i++)
            {
                out[i] = orig[i];
            }
            return;
        }
        out[0] = choice == main::kWaddleDee ? 100.0f : 0.0f;
        out[1] = choice == main::kWaddleDoo ? 100.0f : 0.0f;
        out[2] = choice == main::kWaddleGordo ? 100.0f : 0.0f;
        out[3] = 0.0f;
        clearCooldown = choice == main::kWaddleDoo || choice == main::kWaddleGordo;
    }

    // Builds the work-variable writes for one active fighter on this frame.
    // `mewtwoHoverActive` is LA-Bit[97] read from the fighter when ftKind == kFtMewtwo.
    // `lucasPreFirstAttack` is true when ftKind == kFtLucas and LA-Basic[53] == 0.
    inline void buildFighterPlan(int ftKind, int status, bool mewtwoHoverActive,
                                 const main::FighterMechanicsSettings& s, FighterPlan& plan,
                                 bool lucasPreFirstAttack = false)
    {
        plan.clear();
        struct MechRule {
            u8 ft, fieldOff, count, kindScale, varLo;
            s8 vals[4]; // choice - 1 (value -2 means skip op; kindScale & 0x80 multiplies value by 30)
        };
        enum { kSkip = -2, kScale30 = 0x80 };
#define MR(f, fld, cnt, knd, id, v1, v2, v3, v4) \
    {f, static_cast<u8>(offsetof(main::FighterMechanicsSettings, fld)), cnt, knd, static_cast<u8>((id) & 0xFFu), \
     {v1, v2, v3, v4}}
        static const MechRule kRules[] = {
            MR(kFtGameWatch, gnwBucket, main::kGnwBucketCount, kOpFloat, kVarGnwBucketCount, 0, 1, 2, 3),
            MR(kFtGameWatch, gnwBucket, main::kGnwBucketCount, kOpFloat, kVarGnwBucketDamage, 0, 12, 24, 36),
            MR(kFtGameWatch, gnwBucket, main::kGnwBucketCount, kOpFlag, kVarGnwBucketFull, 0, 0, 0, 1),
            MR(kFtSamus, samusCharge, main::kCharge3Count, kOpInt, kVarSamusCharge, 0, 4, 7, 0),
            MR(kFtSamus, samusBeam, main::kSamusBeamCount, kOpInt, kVarSamusBeam, 0, 1, 0, 0),
            MR(kFtSamus, samusAirBombs, main::kMechToggleCount, kOpInt, kVarSamusAirBombs, 0, 0, 0, 0),
            MR(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, kOpInt, kVarMewtwoBallFrames, 0, 64, 127, 0),
            MR(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, kOpInt, kVarMewtwoShadowBall, -1, 3, 6, 0),
            MR(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, kOpInt, kVarMewtwoBallStored, -1, 3, 6, 0),
            MR(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, kOpFlag, kVarMewtwoBallHas, 0, 1, 1, 0),
            MR(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, kOpFlag, kVarMewtwoBallFull, 0, 0, 1, 0),
            MR(kFtLucario, lucarioAura, main::kLucarioAuraCount, kOpFloat, kVarLucarioAura, 0, 60, 120, 0),
            MR(kFtLucario, lucarioSphere, main::kCharge2Count, kOpInt, kVarLucarioSphereFrames, 0, 16, 0, 0),
            MR(kFtLucario, lucarioSphere, main::kCharge2Count, kOpInt, kVarLucarioSphere, -1, 4, 0, 0),
            MR(kFtLucario, lucarioSphere, main::kCharge2Count, kOpInt, kVarLucarioSphereStored, -1, 4, 0, 0),
            MR(kFtLucario, lucarioSphere, main::kCharge2Count, kOpFlag, kVarLucarioSphereHas, 0, 1, 0, 0),
            MR(kFtLucario, lucarioSphere, main::kCharge2Count, kOpFlag, kVarLucarioSphereFull, 0, 1, 0, 0),
            MR(kFtWario, warioWaft, main::kCharge3Count, kOpInt | kScale30, kVarWarioWaftFrames, 0, 60, 120, 0),
            MR(kFtWario, warioWaft, main::kCharge3Count, kOpInt, kVarWarioWaftStage, 0, 2, 3, 0),
            MR(kFtWario, warioWaft, main::kCharge3Count, kOpFlag, kVarWarioWaftFull, 0, 0, 1, 0),
            MR(kFtDonkeyKong, dkPunch, main::kCharge2Count, kOpInt, kVarDkPunch, 0, 10, 0, 0),
            MR(kFtIvysaur, ivysaurSolarbeam, main::kCharge3Count, kOpFloat, kVarIvysaurSolarbeam, 0, 13, 26, 0),
            MR(kFtRob, robAirBoosts, main::kMechToggleCount, kOpInt, kVarRobAirBoosts, 3, 0, 0, 0),
            MR(kFtRob, robAirBoosts, main::kMechToggleCount, kOpFlag, kVarRobBoostExhausted, 0, 0, 0, 0),
            MR(kFtRob, robGyro, main::kCharge2Count, kOpFloat, kVarRobGyro, 0, 90, 0, 0),
            MR(kFtRob, robBeam, main::kRobBeamCount, kOpFloat | kScale30, kVarRobBeam, 0, 10, 30, 0),
            MR(kFtSquirtle, squirtleWaterGun, main::kCharge2Count, kOpInt, kVarSquirtleWaterGun, 0, 60, 0, 0),
            MR(kFtSheik, sheikNeedles, main::kSheikNeedlesCount, kOpInt, kVarSheikNeedles, 0, 6, 0, 0),
            MR(kFtSheik, sheikNeedles, main::kSheikNeedlesCount, kOpFlag, kVarSheikNeedlesFull, 0, 1, 0, 0),
            MR(kFtDedede, dededeWaddle, main::kDededeWaddleCount, kOpInt, kVarDededeWaddleCd, kSkip, 0, 0, 0),
            MR(kFtSnake, snakeTranq, main::kMechToggleCount, kOpInt, kVarSnakeTranq, 0, 0, 0, 0),
            MR(kFtSnake, snakeCypher, main::kMechToggleCount, kOpFloat, kVarSnakeCypherUses, 0, 0, 0, 0),
            MR(kFtSnake, snakeCypher, main::kMechToggleCount, kOpFlag, kVarSnakeCypherUsed, 0, 0, 0, 0),
            MR(kFtGanondorf, ganonFloat, main::kMechToggleCount, kOpInt, kVarGanonFloatCount, 0, 0, 0, 0)
        };
#undef MR
        const u8* raw = reinterpret_cast<const u8*>(&s);
        for (int i = 0; i < static_cast<int>(sizeof(kRules) / sizeof(kRules[0])); i++)
        {
            const MechRule& r = kRules[i];
            if (static_cast<int>(r.ft) != ftKind)
            {
                continue;
            }
            int choice = clampChoice(raw[r.fieldOff], r.count);
            if (choice == 0)
            {
                continue;
            }
            int v = r.vals[choice - 1];
            if (v == kSkip)
            {
                continue;
            }
            if ((r.kindScale & kScale30) != 0)
            {
                v *= 30;
            }
            u8 kind = r.kindScale & 0x7Fu;
            u32 varId = 0x10000000u | (static_cast<u32>(kind) << 24) | static_cast<u32>(r.varLo);
            if (kind == kOpInt)
            {
                plan.addInt(varId, v);
            }
            else if (kind == kOpFloat)
            {
                plan.addFloat(varId, static_cast<float>(v));
            }
            else
            {
                plan.addFlag(varId, v != 0);
            }
        }
        if (ftKind == kFtPeach && clampChoice(s.peachFloat, main::kMechToggleCount) == main::kMechAlways)
        {
            plan.addFlag(kVarPeachFloatUsed, false);
            if (status == kStatusPeachFloat)
            {
                plan.addInt(kVarPeachFloatFrames, kPeachFloatMaxFrames);
            }
        }
        else if (ftKind == kFtMewtwo && clampChoice(s.mewtwoHover, main::kMechToggleCount) == main::kMechAlways)
        {
            plan.addFlag(kVarMewtwoHoverUsed, false);
            if (status == kStatusMewtwoHover && mewtwoHoverActive)
            {
                plan.addInt(kVarMewtwoHoverFrames, kMewtwoHoverMaxFrames);
            }
        }
        else if (ftKind == kFtLucas)
        {
            int off = clampChoice(s.lucasOffenseUp, main::kLucasOffenseCount);
            if (off == main::kOffenseOff)
            {
                plan.addFlag(kVarLucasOffenseFlag, false);
                plan.addFlag(kVarLucasOffenseConsume, false);
                plan.addFloat(kVarLucasOffenseProg, 0.0f);
            }
            else if (off == main::kOffenseCharged)
            {
                if (lucasPreFirstAttack)
                {
                    plan.addInt(kVarLucasAttackSerial, 1);
                }
                plan.addFlag(kVarLucasOffenseFlag, true);
                plan.addFloat(kVarLucasOffenseProg, 120.0f);
            }
            if (off == main::kOffenseOff)
            {
                plan.customVisual = kVisLucasOffense;
                plan.customTier = 0;
            }
            else if (off == main::kOffenseCharged
                     && (status < kStatusAttackS4Start || status > kStatusAttackS4))
            {
                // FitLucas.pac 0x2df4c kills 0x1B0001 mid-smash; skip respawning until smash ends.
                plan.customVisual = kVisLucasOffense;
                plan.customTier = 1;
            }
        }
        else if (ftKind == kFtIvysaur)
        {
            int ivy = clampChoice(s.ivysaurSolarbeam, main::kCharge3Count);
            if (ivy != main::kCharge3Default)
            {
                plan.customVisual = kVisIvysaurBulb;
                plan.customTier = static_cast<u8>(ivy - 1);
            }
        }
        else if (ftKind == kFtLucario)
        {
            int aura = clampChoice(s.lucarioAura, main::kLucarioAuraCount);
            if (aura != main::kAuraDefault)
            {
                plan.customVisual = kVisLucarioAura;
                plan.customTier = static_cast<u8>(aura - 1);
            }
        }

        // Systemic common full-charge glow sync for all 9 fighters whose P+ 3.2.1
        // charge status scripts call soEffectModule::reqCommon(0, 26 or 39) on full.
        struct CommonVisRule {
            u8 ft, fieldOff, count, fullVal, commonId;
        };
#define CV(f, fld, cnt, full, cid) \
    {f, static_cast<u8>(offsetof(main::FighterMechanicsSettings, fld)), cnt, full, cid}
        static const CommonVisRule kCommonVis[] = {
            CV(kFtGameWatch, gnwBucket, main::kGnwBucketCount, main::kBucketFull, kCommonChargeGlow),
            CV(kFtSamus, samusCharge, main::kCharge3Count, main::kCharge3Full, kCommonChargeGlow),
            CV(kFtMewtwo, mewtwoShadowBall, main::kCharge3Count, main::kCharge3Full, kCommonChargeGlow),
            CV(kFtLucario, lucarioSphere, main::kCharge2Count, main::kCharge2Full, kCommonChargeGlow),
            CV(kFtWario, warioWaft, main::kCharge3Count, main::kCharge3Full, kCommonWarioWaft),
            CV(kFtDonkeyKong, dkPunch, main::kCharge2Count, main::kCharge2Full, kCommonChargeGlow),
            CV(kFtRob, robGyro, main::kCharge2Count, main::kCharge2Full, kCommonChargeGlow),
            CV(kFtSquirtle, squirtleWaterGun, main::kCharge2Count, main::kCharge2Full, kCommonChargeGlow),
            CV(kFtSheik, sheikNeedles, main::kSheikNeedlesCount, main::kNeedles6, kCommonChargeGlow)
        };
#undef CV
        for (int i = 0; i < static_cast<int>(sizeof(kCommonVis) / sizeof(kCommonVis[0])); i++)
        {
            const CommonVisRule& cv = kCommonVis[i];
            if (static_cast<int>(cv.ft) != ftKind)
            {
                continue;
            }
            int choice = clampChoice(raw[cv.fieldOff], cv.count);
            if (choice != 0)
            {
                plan.commonEffectId = cv.commonId;
                plan.commonEffectOn = choice == static_cast<int>(cv.fullVal) ? 1 : 0;
            }
            break;
        }
    }
}
}
