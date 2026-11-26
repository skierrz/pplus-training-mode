#pragma once

#include <types.h>

#include "main/game/status_ids.h"

class Fighter;

// Where a recording starts (Save Positions). record.cpp keeps two: the
// savestate's recording slot, an exact copy of the match when available (a
// two-fighter Training match with spare heaps empty), and always this lite
// fallback (also used to mirror placements after a load).
//
// The lite start state: each fighter's position, facing, percent and
// shield health; velocity, the last hit's launch, hitlag and hitstun
// cleared; status forced to Wait. Not restored: stale-move queues, jumps,
// character state (charges, item held), items and projectiles, the stage,
// the camera, the game's random number generators. Both calls run between
// game steps.
namespace tm {
namespace record {
    struct Players {
        int hmn; // player numbers (0-3) of the two recorded fighters
        int cpu;
    };

    class StartState {
    public:
        virtual const char* name() const = 0;
        // Takes the start state from the fighters as they are now. `why`
        // says what went wrong when it returns false.
        virtual bool save(const Players& who, const char*& why) = 0;
        // Puts it back, mirrored in x (stage centre x = 0) with `mirror`.
        // `extra[side]` percent is added to the saved damage; with `extra`
        // NULL the damage is left as it is.
        virtual bool restore(bool mirror, const int* extra, const char*& why) = 0;
        virtual void clear() = 0;
        virtual bool saved() const = 0;
        virtual Players players() const = 0;
    };

    StartState& liteStartState();

    // Pure decision helper for LiteStartState::place(): resolves target status,
    // ground-correct mode (soGroundShapeImpl::Correct_Ground_Cliff_Stop = 2,
    // Correct_Air = 5), and jump count (LA-Basic[1]) across grounded,
    // mirrored-over-void, mid-air-near-floor, and tumble-to-ground restores.
    enum {
        kLiteCorrectGroundCliffStop = 2,
        kLiteCorrectAir = 5
    };

    inline void resolveLiteRestore(bool savedAir, bool attachOk, s32 savedJumps,
                                   int& outStatus, u32& outCorrect, s32& outJumps)
    {
        bool airborne = savedAir || !attachOk;
        outStatus = airborne ? game::kStatusFall : game::kStatusWait;
        outCorrect = airborne ? static_cast<u32>(kLiteCorrectAir) : static_cast<u32>(kLiteCorrectGroundCliffStop);
        outJumps = airborne ? (savedJumps > 0 ? savedJumps : 1) : 0;
    }

    // Puts `f` standing in Wait at (x, y), facing `lr`, the way a restore of
    // the lite start state places a fighter (DPad Down's Place CPU).
    bool placeStanding(Fighter* f, float x, float y, float lr);

    // The lite start state's values for one fighter, for Export and import.
    struct LitePlacement {
        float x, y, z;
        float lr;
        float damage;
        float shield;
        bool haveShield;
    };
    // Its two placements (HMN, CPU) and players; false without positions.
    bool liteGet(LitePlacement out[2], Players& who);
    // Sets it from imported placements for the fighters of `who` in this
    // match; false (and `why`) if one is missing.
    bool liteSet(const Players& who, const LitePlacement in[2], const char*& why);
}
}
