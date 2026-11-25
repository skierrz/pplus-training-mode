#pragma once

#include <types.h>

#include "main/game/body.h"
#include "main/game/status_ids.h"

// Technique timings for the OSD messages, the Ledgedash HUD and the colour
// overlays, worked out once per finished frame from the trackers and each
// fighter's Body. See techniques.cpp for each definition.
namespace tm {
namespace game {
    // What a message is about; the sink decides whether to show it.
    enum TechniqueKind {
        kTechWavedash,
        kTechLCancel,
        kTechActOos,
        kTechActOoLag,
        kTechJumpCancel,
        kTechFastfall,
        kTechCombo,
        kTechSdi,
        kTechCliffwait,
        kTechGalint,
        kTechActOoHitstun,
        kTechActOoAirborne,
        kTechDashback,
        kTechPowershield,
        kTechTechLockout,
        kTechItemThrow,
        kTechBoostGrab,
        kTechGrabBreakout,
        kTechKindCount
    };

    enum { kNoValue = -32768 };

    // Act OoAirborne (Technique::flag): what the action came out of, plus
    // kAirSpecial for a special (else an aerial) and kAirToJump for a
    // double jump out of a jump.
    enum {
        kAirNone = 0,
        kAirJump = 1,
        kAirDoubleJump = 2,
        kAirFall = 3,
        kAirDrop = 4,
        kAirFromMask = 7,
        kAirSpecial = 8,
        kAirToJump = 16
    };

    // Dashback (flag): failed, straight into a dash, or a slow turn that
    // became a dash on its second frame. Powershield (flag): too early,
    // a success, or a shield that was held before it started.
    enum { kDashbackFailed = 0, kDashbackDirect = 1, kDashbackSlowTurn = 2 };
    enum { kPowershieldEarly = 0, kPowershieldDone = 1, kPowershieldBuffered = 2 };

    struct Technique {
        u8 kind;    // TechniqueKind
        s8 player;  // HUD slot the message belongs to
        s8 tracked; // tracker index of the fighter it describes
        u8 flag;    // wavedash: short hop; L-cancel: success; Act OoLag: autocancel;
                    // Act OoAirborne: kAir*; boost grab: up smash; item throw:
                    // out of an airdodge; GALINT: the hang gave no
                    // intangibility; dashback and powershield: the enums above
        s16 frame;  // the timing (1 = the earliest possible), or kNoValue
        s16 a, b;   // L-cancel: successes, landings; combo: hits, damage; SDI: shifts, inputs;
                    // wavedash: jump button frames; dashback: successes, tries;
                    // grab breakout: grab timer; cliffwait/GALINT: P+ LA-Basic[79] grab count
        float angle; // wavedash: airdodge angle, degrees below horizontal;
                     // grab breakout: mash rate
    };

    typedef void (*TechniqueSink)(const Technique& t);

    // Frame types of the Ledgedash HUD's cells.
    enum LedgeCell {
        kCellLedge,
        kCellFall,
        kCellJump,
        kCellAirdodge,
        kCellLanding,
        kCellAttack,
        kCellOther,
        kCellGalint,
        kCellTypes
    };

    enum { kLedgeCells = 128 };

    struct Ledgedash {
        s32 done;        // tracker frame it finished, kNoFrame if none yet
        s8 player;
        u8 count;        // cells[0..count)
        s16 galint;      // intangible frames left when it could act on the ground
        s16 ledgeFrames; // frames in CliffWait
        u8 noXlu;        // the hang gave no intangibility: galint is from an earlier one
        u8 hasAngle;
        u8 grabCount;    // P+ consecutive ledge-grab counter (LA-Basic[79])
        float angle;     // airdodge angle, degrees below horizontal
        u8 cells[kLedgeCells];
    };

    // Per tracked fighter, for the overlays.
    struct TechniqueState {
        bool valid;
        Body body;
        bool missedLCancel; // in an aerial's landing lag after a missed L-cancel
        bool canFastfall;
    };

    void techniquesReset(TechniqueSink sink);
    // Once per finished tracker frame, after the trackers' frame.
    void techniquesFrame();
    const TechniqueState* techniqueState(int tracked);
    // Human player's last ledgedash (for the Ledgedash HUD and drill).
    const Ledgedash& lastLedgedash();
    // Ledgedash of the fighter that last emitted kTechGalint (human or CPU).
    const Ledgedash& lastLedgedashEvent();

    // Maps a fighter status kind to its Ledgedash HUD cell type.
    // Includes kStatusFly (0x0D) alongside kStatusJumpAerial (0x0C) for
    // multi-jumpers (Kirby, Jigglypuff, Meta Knight, Pit, Charizard, Dedede).
    inline u8 ledgeCellOf(int s)
    {
        if (s == 0x75 || s == 0x74 || s == 0x73)
        {
            return kCellLedge;
        }
        if (s == kStatusFall || s == kStatusFallAerial)
        {
            return kCellFall;
        }
        if (s == kStatusJumpAerial || s == kStatusFly || s == kStatusJump || s == 0x79 || s == 0x7A)
        {
            return kCellJump;
        }
        if (s == kStatusEscapeAir)
        {
            return kCellAirdodge;
        }
        if (s >= kStatusLanding && s <= kStatusLandingFallSpecial)
        {
            return kCellLanding;
        }
        if ((s >= kStatusAttack && s <= kStatusAttackAirFirst) || (s >= kStatusSpecialN && s < kStatusFinal)
            || (s >= kStatusCatch && s <= 0x36))
        {
            return kCellAttack;
        }
        return kCellOther;
    }
}
}
