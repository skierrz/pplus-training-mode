#pragma once

#include <types.h>

// Status (action) kinds and interrupt groups the trackers use. Common
// statuses follow Fighter::Status::Kind in lib/BrawlHeaders ft/fighter.h
// (0x00-0x111, the same for every fighter); interrupt groups follow
// Fighter::Status::Transition::TermGroupId there. The ones whose meaning the
// trackers rely on were seen in the emulator (see tracker.cpp).
namespace tm {
namespace game {
    enum {
        kStatusWait = 0x0,
        kStatusWalk = 0x1,
        kStatusWalkBrake = 0x2,
        kStatusDash = 0x3,
        kStatusRun = 0x4,
        kStatusRunBrake = 0x5,
        kStatusTurn = 0x6,
        kStatusTurnDash = 0x7,
        kStatusTurnRun = 0x8,
        kStatusTurnRunBrake = 0x9,
        kStatusJumpSquat = 0xA,
        kStatusJump = 0xB,
        kStatusJumpAerial = 0xC,
        kStatusFly = 0xD,
        kStatusFall = 0xE,
        kStatusFallAerial = 0xF,
        kStatusSquat = 0x11,
        kStatusSquatWait = 0x12,
        kStatusSquatF = 0x13,
        kStatusSquatB = 0x14,
        kStatusSquatRv = 0x15,
        kStatusLanding = 0x16,
        kStatusLandingLight = 0x17,
        kStatusLandingAttackAir = 0x18,
        kStatusLandingFallSpecial = 0x19,
        kStatusGuardOn = 0x1A,
        kStatusGuard = 0x1B,
        kStatusGuardOff = 0x1C,
        kStatusGuardDamage = 0x1D,
        kStatusEscape = 0x1E,
        kStatusEscapeAir = 0x21,
        kStatusAttack = 0x24,
        kStatusAttackAirFirst = 0x33, // AttackAir
        kStatusCatch = 0x34,
        kStatusThrow = 0x3C,
        kStatusDamage = 0x43,
        kStatusDamageFlyRoll = 0x46,
        kStatusDownDamage = 0x54,
        // Teetering at a ledge: Ottoto (its start) and Ottoto_Wait (its
        // loop), Melee's Ottotto and OttottoWait. From ft/fighter.h
        // (Fighter::Status::Kind); unchecked in game.
        kStatusOttoto = 0x7C,
        kStatusOttotoWait = 0x7D,
        kStatusDead = 0xBD,
        kStatusRebirth = 0xBE,
        kStatusStandby = 0x10B,
        kStatusSpecialN = 0x112,
        kStatusFinal = 0x116,
        kCommonStatusCount = 0x112
    };

    // Interrupt (transition term) groups, as the Enable/Disable Interrupt
    // Group commands and soStatusModule::isEnableTransitionTermGroup number
    // them.
    enum {
        kGroupGroundSpecial = 0x1,
        kGroupGroundItem = 0x2,
        kGroupGroundCatch = 0x3,
        kGroupGroundAttack = 0x4,
        kGroupGroundEscape = 0x5,
        kGroupGroundGuard = 0x6,
        kGroupGroundJump = 0x7,
        kGroupGround = 0x8,
        kGroupAirLanding = 0x9,
        kGroupAirCliff = 0xA,
        kGroupAirSpecial = 0xB,
        kGroupAirItemThrow = 0xC,
        kGroupAirLasso = 0xD,
        kGroupAirEscape = 0xE,
        kGroupAirAttack = 0xF,
        kGroupAirTreadJump = 0x10,
        kGroupAirWallJump = 0x11,
        kGroupAirJumpAerial = 0x12,
        kGroupCount = 0x13
    };
}
}
