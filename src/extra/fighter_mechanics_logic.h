#pragma once

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
        kVarRobAirBoosts = 0x1000004B,     // LA-Basic[75], 0..3
        kVarRobBoostExhausted = 0x1200006F, // LA-Bit[111]
        kVarRobGyro = 0x11000014,          // LA-Float[20], 0.0..90.0 (IC-Float[4047])
        kVarRobBeam = 0x11000013,          // LA-Float[19], <100 / 100..899 / >=900

        // Lucas (FitLucas.pac): Offense Up flags, charge progress & attack serial.
        kVarLucasAttackSerial = 0x10000035,   // LA-Basic[53] (Wait1 clears Offense Up while 0)
        kVarLucasOffenseFlag = 0x2200006B,    // RA-Bit[107]
        kVarLucasOffenseConsume = 0x2200006C, // RA-Bit[108]
        kVarLucasOffenseProg = 0x1100003D,    // LA-Float[61], 0.0..120.0

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

    struct FighterPlan {
        enum { kMaxOps = 8 };
        WorkOp ops[kMaxOps];
        int count;

        void clear()
        {
            count = 0;
        }
        void addInt(u32 id, s32 v)
        {
            if (count < kMaxOps)
            {
                WorkOp& o = ops[count++];
                o.kind = kOpInt;
                o.flagValue = 0;
                o.id = id;
                o.intValue = v;
                o.floatValue = 0.0f;
            }
        }
        void addFloat(u32 id, float v)
        {
            if (count < kMaxOps)
            {
                WorkOp& o = ops[count++];
                o.kind = kOpFloat;
                o.flagValue = 0;
                o.id = id;
                o.intValue = 0;
                o.floatValue = v;
            }
        }
        void addFlag(u32 id, bool on)
        {
            if (count < kMaxOps)
            {
                WorkOp& o = ops[count++];
                o.kind = kOpFlag;
                o.flagValue = on ? 1 : 0;
                o.id = id;
                o.intValue = 0;
                o.floatValue = 0.0f;
            }
        }
    };

    inline int clampChoice(u8 raw, int count)
    {
        return raw < static_cast<u8>(count) ? static_cast<int>(raw) : 0;
    }

    // True if `ftKind` has any row on the Character RNG / Fighter Mechanics page.
    inline bool fighterHasMechanicsRow(int ftKind)
    {
        switch (ftKind)
        {
        case kFtPeach:
        case kFtLuigi:
        case kFtGameWatch:
        case kFtSamus:
        case kFtMewtwo:
        case kFtLucario:
        case kFtWario:
        case kFtDonkeyKong:
        case kFtIvysaur:
        case kFtRob:
        case kFtLucas:
        case kFtSquirtle:
        case kFtSheik:
        case kFtDedede:
        case kFtSnake:
        case kFtGanondorf:
            return true;
        default:
            return false;
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
        switch (ftKind)
        {
        case kFtPeach:
            if (clampChoice(s.peachFloat, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addFlag(kVarPeachFloatUsed, false);
                if (status == kStatusPeachFloat)
                {
                    plan.addInt(kVarPeachFloatFrames, kPeachFloatMaxFrames);
                }
            }
            break;

        case kFtGameWatch:
        {
            int bucket = clampChoice(s.gnwBucket, main::kGnwBucketCount);
            if (bucket != main::kBucketDefault)
            {
                static const float kCount[main::kGnwBucketCount] = {0.0f, 0.0f, 1.0f, 2.0f, 3.0f};
                static const float kDamage[main::kGnwBucketCount] = {0.0f, 0.0f, 12.0f, 24.0f, 36.0f};
                plan.addFloat(kVarGnwBucketCount, kCount[bucket]);
                plan.addFloat(kVarGnwBucketDamage, kDamage[bucket]);
                plan.addFlag(kVarGnwBucketFull, bucket == main::kBucketFull);
            }
            break;
        }

        case kFtSamus:
        {
            int charge = clampChoice(s.samusCharge, main::kCharge3Count);
            if (charge != main::kCharge3Default)
            {
                static const s32 kSamusLevels[main::kCharge3Count] = {0, 0, 4, 7};
                plan.addInt(kVarSamusCharge, kSamusLevels[charge]);
            }
            int beam = clampChoice(s.samusBeam, main::kSamusBeamCount);
            if (beam != main::kBeamDefault)
            {
                plan.addInt(kVarSamusBeam, beam == main::kBeamIce ? 1 : 0);
            }
            if (clampChoice(s.samusAirBombs, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addInt(kVarSamusAirBombs, 0);
            }
            break;
        }

        case kFtMewtwo:
        {
            int ball = clampChoice(s.mewtwoShadowBall, main::kCharge3Count);
            if (ball != main::kCharge3Default)
            {
                static const s32 kBallFrames[main::kCharge3Count] = {0, 0, 64, 127};
                static const s32 kBallLevels[main::kCharge3Count] = {-1, -1, 3, 6};
                plan.addInt(kVarMewtwoBallFrames, kBallFrames[ball]);
                plan.addInt(kVarMewtwoShadowBall, kBallLevels[ball]);
                plan.addInt(kVarMewtwoBallStored, kBallLevels[ball]);
                plan.addFlag(kVarMewtwoBallHas, ball != main::kCharge3Empty);
                plan.addFlag(kVarMewtwoBallFull, ball == main::kCharge3Full);
            }
            if (clampChoice(s.mewtwoHover, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addFlag(kVarMewtwoHoverUsed, false);
                if (status == kStatusMewtwoHover && mewtwoHoverActive)
                {
                    plan.addInt(kVarMewtwoHoverFrames, kMewtwoHoverMaxFrames);
                }
            }
            break;
        }

        case kFtLucario:
        {
            int aura = clampChoice(s.lucarioAura, main::kLucarioAuraCount);
            if (aura != main::kAuraDefault)
            {
                static const float kAuraVal[main::kLucarioAuraCount] = {0.0f, 0.0f, 60.0f, 120.0f};
                plan.addFloat(kVarLucarioAura, kAuraVal[aura]);
            }
            int sphere = clampChoice(s.lucarioSphere, main::kCharge2Count);
            if (sphere != main::kCharge2Default)
            {
                bool full = sphere == main::kCharge2Full;
                plan.addInt(kVarLucarioSphereFrames, full ? 16 : 0);
                plan.addInt(kVarLucarioSphere, full ? 4 : -1);
                plan.addInt(kVarLucarioSphereStored, full ? 4 : -1);
                plan.addFlag(kVarLucarioSphereHas, full);
                plan.addFlag(kVarLucarioSphereFull, full);
            }
            break;
        }

        case kFtWario:
        {
            int waft = clampChoice(s.warioWaft, main::kCharge3Count);
            if (waft != main::kCharge3Default)
            {
                static const s32 kFrames[main::kCharge3Count] = {0, 0, 1800, 3600};
                static const s32 kStage[main::kCharge3Count] = {0, 0, 2, 3};
                plan.addInt(kVarWarioWaftFrames, kFrames[waft]);
                plan.addInt(kVarWarioWaftStage, kStage[waft]);
                plan.addFlag(kVarWarioWaftFull, waft == main::kCharge3Full);
            }
            break;
        }

        case kFtDonkeyKong:
        {
            int punch = clampChoice(s.dkPunch, main::kCharge2Count);
            if (punch != main::kCharge2Default)
            {
                plan.addInt(kVarDkPunch, punch == main::kCharge2Full ? 10 : 0);
            }
            break;
        }

        case kFtIvysaur:
        {
            int solar = clampChoice(s.ivysaurSolarbeam, main::kCharge3Count);
            if (solar != main::kCharge3Default)
            {
                static const float kSolar[main::kCharge3Count] = {0.0f, 0.0f, 13.0f, 26.0f};
                plan.addFloat(kVarIvysaurSolarbeam, kSolar[solar]);
            }
            break;
        }

        case kFtRob:
        {
            if (clampChoice(s.robAirBoosts, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addInt(kVarRobAirBoosts, 3);
                plan.addFlag(kVarRobBoostExhausted, false);
            }
            int gyro = clampChoice(s.robGyro, main::kCharge2Count);
            if (gyro != main::kCharge2Default)
            {
                // FitRobot.pac data+0x88[0]: IC-Float[4047] = 90.0f (max Gyro charge).
                plan.addFloat(kVarRobGyro, gyro == main::kCharge2Full ? 90.0f : 0.0f);
            }
            int beam = clampChoice(s.robBeam, main::kRobBeamCount);
            if (beam != main::kRobBeamDefault)
            {
                // FitRobot.pac data+0x7c: IC-Float[4005] = 100.0 (Normal),
                // IC-Float[4004] = 300.0, IC-Float[4006] = 900.0 (Super).
                static const float kBeam[main::kRobBeamCount] = {0.0f, 0.0f, 300.0f, 900.0f};
                plan.addFloat(kVarRobBeam, kBeam[beam]);
            }
            break;
        }

        case kFtLucas:
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
            break;
        }

        case kFtSquirtle:
        {
            int gun = clampChoice(s.squirtleWaterGun, main::kCharge2Count);
            if (gun != main::kCharge2Default)
            {
                plan.addInt(kVarSquirtleWaterGun, gun == main::kCharge2Full ? 60 : 0);
            }
            break;
        }

        case kFtSheik:
        {
            int needles = clampChoice(s.sheikNeedles, main::kSheikNeedlesCount);
            if (needles == main::kNeedles0)
            {
                plan.addInt(kVarSheikNeedles, 0);
                plan.addFlag(kVarSheikNeedlesFull, false);
            }
            else if (needles == main::kNeedles6)
            {
                plan.addInt(kVarSheikNeedles, 6);
                plan.addFlag(kVarSheikNeedlesFull, true);
            }
            break;
        }

        case kFtDedede:
        {
            float dummyOrig[4] = {35.0f, 10.0f, 4.0f, 0.0f};
            float dummyOut[4];
            bool clearCd = false;
            dededeWaddleWeights(s.dededeWaddle, dummyOrig, dummyOut, clearCd);
            if (clearCd)
            {
                plan.addInt(kVarDededeWaddleCd, 0);
            }
            break;
        }

        case kFtSnake:
            if (clampChoice(s.snakeTranq, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addInt(kVarSnakeTranq, 0);
            }
            if (clampChoice(s.snakeCypher, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addFloat(kVarSnakeCypherUses, 0.0f);
                plan.addFlag(kVarSnakeCypherUsed, false);
            }
            break;

        case kFtGanondorf:
            if (clampChoice(s.ganonFloat, main::kMechToggleCount) == main::kMechAlways)
            {
                plan.addInt(kVarGanonFloatCount, 0);
            }
            break;

        default:
            break;
        }
    }
}
}
