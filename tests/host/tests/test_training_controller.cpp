// Training's per-step CPU controller reassignment patch (src/main/game/training_controller.cpp):
// 2-bit owner mask (Controlled By and Record), patched iff mask != 0 in
// Training, restored without the Training gate whenever mask == 0 and the
// site holds our skip branch.
#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "main/game/training_controller.h"

using namespace tm;

namespace {
    const u32 kSite = 0x80962D0Cu;
    const u32 kOrig = 0x8B1BFFFFu;
    const u32 kSkip = 0x48000170u;

    void setUp(bool training)
    {
        host::fakes::reset();
        host::fakes::match().training = training;
        host::mem::setWord(kSite, kOrig);
        game::trainingControllerRestore();
        host::fakes::clearCacheOps();
    }
}

TEST(training_controller, two_owners_share_patch_until_both_release)
{
    setUp(true);
    CHECK_EQ(0u, game::trainingControllerOwners());
    CHECK_EQ(kOrig, host::mem::word(kSite));

    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    CHECK_EQ(static_cast<u32>(game::kControllerOwnerControlledBy), game::trainingControllerOwners());
    CHECK_EQ(2, host::fakes::cacheOpCount());

    host::fakes::clearCacheOps();
    game::trainingControllerSuppress(game::kControllerOwnerRecord, true);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    CHECK_EQ(static_cast<u32>(game::kControllerOwnerControlledBy | game::kControllerOwnerRecord),
             game::trainingControllerOwners());
    CHECK_EQ(0, host::fakes::cacheOpCount());

    // Dropping Controlled By while Record still owns keeps the patch intact with zero cache flushes.
    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    CHECK_EQ(static_cast<u32>(game::kControllerOwnerRecord), game::trainingControllerOwners());
    CHECK_EQ(0, host::fakes::cacheOpCount());

    // Dropping the last owner restores the original instruction.
    game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
    CHECK_EQ(kOrig, host::mem::word(kSite));
    CHECK_EQ(0u, game::trainingControllerOwners());
    CHECK_EQ(2, host::fakes::cacheOpCount());
}

TEST(training_controller, restores_without_training_gate_and_on_detach)
{
    setUp(false);
    // Outside Training: never patches.
    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
    CHECK_EQ(kOrig, host::mem::word(kSite));
    game::trainingControllerRestore();

    // Patched in Training, then Training ends before unpatch: still restores.
    setUp(true);
    game::trainingControllerSuppress(game::kControllerOwnerRecord, true);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    host::fakes::match().training = false;
    game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
    CHECK_EQ(kOrig, host::mem::word(kSite));

    // Detach restore clears both owners and restores even outside Training.
    setUp(true);
    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
    game::trainingControllerSuppress(game::kControllerOwnerRecord, true);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    host::fakes::match().training = false;
    game::trainingControllerRestore();
    CHECK_EQ(0u, game::trainingControllerOwners());
    CHECK_EQ(kOrig, host::mem::word(kSite));
}

TEST(training_controller, unknown_word_is_left_alone_on_patch_and_restore)
{
    setUp(true);
    host::mem::setWord(kSite, 0x60000000u);
    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
    CHECK_EQ(0x60000000u, host::mem::word(kSite));
    game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
    CHECK_EQ(0x60000000u, host::mem::word(kSite));
    game::trainingControllerRestore();
    CHECK_EQ(0x60000000u, host::mem::word(kSite));
    // Re-checked on the next match after restore cleared the refusal.
    host::mem::setWord(kSite, kOrig);
    game::trainingControllerSuppress(game::kControllerOwnerRecord, true);
    CHECK_EQ(kSkip, host::mem::word(kSite));
    game::trainingControllerRestore();
    CHECK_EQ(kOrig, host::mem::word(kSite));
}
