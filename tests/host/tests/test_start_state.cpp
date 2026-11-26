// Host unit tests for LiteStartState mid-air capture/restore decision logic
// (src/main/record/start_state.h::resolveLiteRestore).
#include "host/test.h"
#include "main/game/status_ids.h"
#include "main/record/start_state.h"

using namespace tm;

TEST(start_state, resolve_lite_restore_covers_all_four_quadrants)
{
    int status = -1;
    u32 correct = 0;
    s32 jumps = -1;

    // Quadrant 1: grounded -> grounded (attachGround succeeded).
    record::resolveLiteRestore(false, true, 0, status, correct, jumps);
    CHECK_EQ(static_cast<int>(game::kStatusWait), status);
    CHECK_EQ(static_cast<u32>(record::kLiteCorrectGroundCliffStop), correct);
    CHECK_EQ(0, jumps);

    // Quadrant 2: grounded -> mirrored over void (attachGround failed).
    record::resolveLiteRestore(false, false, 0, status, correct, jumps);
    CHECK_EQ(static_cast<int>(game::kStatusFall), status);
    CHECK_EQ(static_cast<u32>(record::kLiteCorrectAir), correct);
    CHECK_EQ(1, jumps);

    // Quadrant 3a: mid-air near floor (attachGround skipped even if floor is below).
    record::resolveLiteRestore(true, true, 2, status, correct, jumps);
    CHECK_EQ(static_cast<int>(game::kStatusFall), status);
    CHECK_EQ(static_cast<u32>(record::kLiteCorrectAir), correct);
    CHECK_EQ(2, jumps);

    // Quadrant 3b: mid-air with 0 saved jumps clamps to 1 airborne jump used.
    record::resolveLiteRestore(true, false, 0, status, correct, jumps);
    CHECK_EQ(static_cast<int>(game::kStatusFall), status);
    CHECK_EQ(static_cast<u32>(record::kLiteCorrectAir), correct);
    CHECK_EQ(1, jumps);

    // Quadrant 4: airborne/tumble -> grounded save (changeStatusForce Wait, jumps reset to 0).
    record::resolveLiteRestore(false, true, 2, status, correct, jumps);
    CHECK_EQ(static_cast<int>(game::kStatusWait), status);
    CHECK_EQ(static_cast<u32>(record::kLiteCorrectGroundCliffStop), correct);
    CHECK_EQ(0, jumps);
}
