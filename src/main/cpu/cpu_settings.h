#pragma once

#include <types.h>

#include "shared/version.h"

// The lab's CPU control settings, kept in main::Settings' CPU area. Choice
// values are the indices of the rows' labels in ui/pages/cpu_options.cpp and
// tech_options.cpp; values whose label is marked not built behave as None.
namespace tm {
namespace cpu {
    // A full shield in P+ 3.2.1: one common fighter parameter for every
    // character (Source/Injects/Fighter/Constants.asm:36, "3233 Max Shield
    // Strength [50->60]"; Brawl's own value is 50). A build of P+ that
    // changes the parameter needs this changed with it.
    const u8 kShieldFull = 60;

    enum Control { kControlTraining, kControlLab };

    enum Behavior { kBehaviorStand, kBehaviorShield, kBehaviorCrouch, kBehaviorJump, kBehaviorPerfectShield };

    enum Tdi {
        kTdiRandom = 0,
        kTdiInwards,
        kTdiOutwards,
        kTdiSurvival,
        kTdiNone,
        kTdiCount
    };

    // Smash DI direction; Auto follows the trajectory DI.
    enum SdiDir { kSdiAuto, kSdiRandom, kSdiAway, kSdiTowards, kSdiLeft, kSdiRight, kSdiUp, kSdiDown };

    enum Tech { kTechRandom, kTechInPlace, kTechAway, kTechTowards, kTechNone };
    enum Getup { kGetupRandom, kGetupStand, kGetupAway, kGetupTowards, kGetupAttack };
    enum InfiniteShields { kShieldsOff, kShieldsUntilHit, kShieldsOn };
    enum Asdi { kAsdiAuto, kAsdiAway, kAsdiTowards, kAsdiLeft, kAsdiRight, kAsdiUp, kAsdiDown };
    enum ShieldAngle { kAngleNeutral, kAngleUp, kAngleTowards, kAngleDown, kAngleAway };
    enum GrabEscape { kMashNone, kMashMedium, kMashHigh, kMashPerfect };
    enum GrabRelease { kReleaseGrounded, kReleaseAirborne };
    enum TechTrap { kTrapOff, kTrapEarliest, kTrapLatest };
    enum TechLockout { kLockoutEarliest, kLockoutLatest };
    enum CounterLogic { kLogicDefault, kLogicDisable, kLogicCustom };
    // CPU offstage recovery mode: native Game AI (default), PPlusAI in extra, or Off.
    enum Recovery { kRecoveryGame = 0, kRecoveryPPlusAi = 1, kRecoveryOff = 2, kRecoveryCount = 3 };
    // CPU on-ledge reversal / getup option on Cliff_Wait (0x75).
    enum LedgeOption {
        kLedgeDefault = 0,
        kLedgeNeutral,
        kLedgeRoll,
        kLedgeAttack,
        kLedgeJump,
        kLedgeDrop,
        kLedgeRandom,
        kLedgeOptionCount
    };
    // Adaptive CPU tracking and counter-biasing mode (extra/adaptive_cpu.*).
    enum AdaptiveMode {
        kAdaptiveOff = 0,
        kAdaptiveTrack,
        kAdaptiveActive,
        kAdaptiveModeCount
    };
    // Aerial and fastfall timing for CPU SHFFL and FFL counter actions.
    enum ShfflTiming {
        kShfflFramePerfect = 0,
        kShfflEarly = 1,
        kShfflNormal = 2,
        kShfflLate = 3,
        kShfflRandom = 4,
        kShfflTimingCount = 5
    };
    // The lab row sets NoReaction (hits connect with no knockback reaction and
    // no grabs); PassThrough makes attacks pass through completely.
    enum Intangible { kIntangibleOff, kIntangibleNoReaction, kIntangiblePassThrough };

    enum { kAdvancedHits = 10 };

    struct CpuOptions {
        u8 control;         // Control: who drives the CPU
        u8 behavior;        // Behavior
        u8 tdi;             // Tdi
        u8 sdiAmount;       // 0-7 inputs during hitlag
        u8 sdiDir;          // SdiDir
        u8 counterGround;   // index into the ground list (actions.cpp)
        u8 counterAir;      // air list
        u8 counterShield;   // shield list
        u8 counterDelay;    // actionable frames before countering
        u8 infiniteShields; // InfiniteShields
        u8 shieldHealth;    // 0-60 (kShieldFull)
        u8 intangible;      // Intangible
        u8 tech;            // Tech
        u8 getup;           // Getup
        u8 techChance[4];   // percent: in place, away, towards, miss (sum 100)
        u8 getupChance[4];  // percent: stand, away, towards, attack (sum 100)
        u8 asdi;            // Asdi
        u8 shieldAngle;     // ShieldAngle
        u8 grabEscape;      // GrabEscape
        u8 grabRelease;     // GrabRelease
        u8 controlledBy;    // 0 none, 1-4 the controller port
        u8 techSound;       // 0/1
        u8 techTrap;        // TechTrap
        u8 techLockout;     // TechLockout
        u8 missTechWait;    // percent
        // Advanced Counter Options: the hit shown on the page (1-10) and its
        // values; the ten hits themselves live in main::Settings::cpuTables
        // and are copied in and out of these on each change.
        u8 advHit;
        u8 advLogic;        // CounterLogic
        u8 advGround;       // index into the ground list
        u8 advAir;
        u8 advShield;
        u8 advDelayGround;
        u8 advDelayAir;
        u8 advDelayShield;
        u8 recovery;      // Recovery (0 Game AI, 1 PPlusAI, 2 Off)
        u8 ledgeOption;   // LedgeOption (0 Default, 1..6 Neutral/Roll/Attack/Jump/Drop/Random)
        u8 clatterEscape; // GrabEscape (0 None, 1 Medium, 2 High, 3 Max) for Bury/Ice/FuraFura/Sleep
        u8 adaptiveMode;  // AdaptiveMode (0 Off, 1 Track, 2 Adaptive)
        u8 dualStickSdi;  // 0 Off, 1 On (consecutive-frame alternating SDI on hitlag > 1)
        u8 autoLCancel;   // 0 Off, 1 On (default 1: L-cancel active AttackAir before landing)
        u8 shfflTiming;   // ShfflTiming (0 Frame-Perfect .. 4 Random, default 0)
    };
    TM_STATIC_ASSERT(sizeof(CpuOptions) == 46, tm_cpu_options_is_46_bytes);

    // One Advanced Counter page: Counter Logic, and per list (ground, air,
    // shield) the action index and its delay.
    struct AdvancedHit {
        u8 logic;     // CounterLogic
        u8 action[3];
        u8 delay[3];
    };

    // The ten Advanced Counter pages, kept with the other settings so they
    // last between matches.
    struct CpuTables {
        u8 ready; // 0 after a settings reset: the pages get their defaults
        AdvancedHit hits[kAdvancedHits];
        u8 reserved[5];
    };
    TM_STATIC_ASSERT(sizeof(CpuTables) == 76, tm_cpu_tables_is_76_bytes);
}
}
