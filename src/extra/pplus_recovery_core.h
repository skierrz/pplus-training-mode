#pragma once

#include <types.h>

// 44-fighter Project+ recovery decision core adapted from
// fudgepop01's PPlusAI (103_recoveryHub.as). Zero Brawl headers so all 44
// fighters, all 8 recovery families, and WeightedDie compile and run on PC
// via `make check-host`.
namespace tm {
namespace extra {
    enum {
        kFtKindMax = 0x32, // covers 0x00 (Mario) .. 0x31 (Wario-Man)
        kWeightedDieCap = 16
    };

    // Button bit values matching tm::cpu::button (src/main/cpu/fighter_input.h).
    enum RecoveryButton {
        kRecBtnAttack = 0x01,
        kRecBtnSpecial = 0x02,
        kRecBtnJump = 0x04,
        kRecBtnGuard = 0x08
    };

    enum RecoveryFamily {
        kFamArc = 0,       // Standard Arc Up-B (+ Side-B / Stall)
        kFamSpacie = 1,    // 360-Aim Spacies (+ Side-B Shorten & notch clamp)
        kFamTeleport = 2,  // 360-Aim Teleport / Quick Attack / Aura
        kFamMultiJump = 3, // Multi-Jump / Glide
        kFamFloat = 4,     // Float / Hover
        kFamTether = 5,    // Tether (+ Bomb / Stall / Flip)
        kFamCharged = 6,   // Charged / Timed / Mash / Wall-Climb
        kFamPkThunder = 7, // Article-Guided PK Thunder
        kFamCount = 8
    };

    enum RecoveryFlag {
        kRfNone = 0,
        kRf360Aim = 1 << 0,
        kRfSideBShorten = 1 << 1,
        kRfGlide = 1 << 2,
        kRfFloat = 1 << 3,
        kRfTether = 1 << 4,
        kRfBombStall = 1 << 5,
        kRfDairStall = 1 << 6,
        kRfFlipDownB = 1 << 7,
        kRfChargeUpB = 1 << 8,
        kRfChargeSideB = 1 << 9,
        kRfMashDownB = 1 << 10,
        kRfWallJump = 1 << 11,
        kRfWallClimb = 1 << 12,
        kRfAuraCancel = 1 << 13,
        kRfQuickAttack = 1 << 14,
        kRfPkThunder = 1 << 15,
        kRfNeutralBRecovery = 1 << 16,
        kRfSideBCancel = 1 << 17,
        kRfBoostUpB = 1 << 18,
        kRfWaft = 1 << 19
    };

    enum RecoveryOption {
        kOptNone = 0,
        kOptJump = 1,
        kOptFloat = 2,
        kOptUpB = 3,
        kOptUpBHigh = 4,
        kOptUpBCancel = 5,
        kOptGlide = 6,
        kOptSideB = 7,
        kOptSideBShort = 8,
        kOptSideBWallJump = 9,
        kOptDownB = 10,
        kOptDownBCancel = 11,
        kOptNeutralB = 12,
        kOptTether = 13,
        kOptDairStall = 14
    };

    // Compact per-fighter ROM recovery spec (indexed by ftKind).
    struct FighterRecoverySpec {
        u8 valid;
        u8 family; // RecoveryFamily
        u8 ncxOffs;
        u8 ncxOffsNear;
        u32 flags; // RecoveryFlag bitmask
        s16 upBX;  // horizontal Up-B reach (or radius for kRf360Aim)
        s16 upBY;  // vertical Up-B reach
        s16 sideBX;
        s16 sideBY;
        s16 altX;  // Down-B / Glide / Float / Neutral-B / Tether range
        s16 altY;
        u16 floatStatus; // Peach 0x11E, Mewtwo 0x120, else 0
        u8 shortenFrame; // Fox 18, Falco 14, Wolf 17, Diddy max charge 45
        u8 pktStartDist; // Ness 60, Lucas 40
        u8 pktEndDist;   // Ness 6, Lucas 6
        u8 pktTime;      // Ness 20, Lucas 30
        u8 laResourceIdx;
        u8 djHeight;
    };

    // Stack-only weighted die adapted from fudgepop01's WeightedDie (zero heap).
    struct WeightedEntry {
        int option;
        float weight;
    };

    class WeightedDie {
    public:
        WeightedDie() : m_count(0) {}

        void clear();
        void add(int option, float weight);
        int size() const { return m_count; }
        float totalWeight() const;
        // Rolls with uniform u01 in [0, 1). Returns defaultOpt when empty.
        int roll(float u01, int defaultOpt = kOptNone) const;

    private:
        WeightedEntry m_entries[kWeightedDieCap];
        int m_count;
    };

    struct RecoveryState {
        u8 initialized;
        u8 option; // RecoveryOption
        u8 highMixup;
        u8 timer;
        u8 mashPhase;
        u8 holdFrames;
        u8 usedFloat;
        u8 ledgeWaitFrames;
        s16 prevStatus;
        u8 cliffChoice;
        u8 reserved;
    };

    struct RecoveryInput {
        int ftKind;
        int status;
        int subaction;
        float animFrame;
        float x, y;
        float vx, vy;
        float lr;
        float ledgeX, ledgeY, ledgeDir;
        float stageLeftX, stageRightX, stageY;
        float blastBottom;
        bool air;
        bool canAct;
        bool jumpsLeft;
        int numJumps;
        bool ledgeOccupied;
        bool ceilingAbove;
        bool wallAhead;
        float wallDist;
        bool floorBelow;
        float yDistFloor;
        bool laFlag;
        int laInt;
        float laFloat;
        bool hasPkt;
        float pktX, pktY;
        float pktVx, pktVy;
        u32 lastButtons;
    };

    struct RecoveryOutput {
        u32 buttons;
        float stickX;
        float stickY;
        bool active;
    };

    // Lookup by ftKind (0x00..0x31). Out-of-range returns a safe Arc fallback.
    const FighterRecoverySpec& recoverySpec(int ftKind);

    // Dead-zone notch clamp from PPlusAI 360-aim Up-B scripts:
    // 0.1 < |x| < 0.25 -> +/-0.3, 0 < y < 0.3 -> 0.3.
    void clampAimNotches(float& x, float& y);

    void recoveryStateReset(RecoveryState& st);

    // Steps one frame of the recovery state machine.
    void stepRecovery(const RecoveryInput& in, RecoveryState& st, u32& rng, RecoveryOutput& out);
}
}
