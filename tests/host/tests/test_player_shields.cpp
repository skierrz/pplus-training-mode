// Unit tests for lab::stepPlayerShield (General > Player Infinite Shields).
#define TM_MODULE_MAIN 1

#include "host/test.h"
#include "main/lab.h"

namespace {
    using namespace tm;
}

TEST(player_shields, off_never_refills)
{
    bool hit = false;
    CHECK(!lab::stepPlayerShield(cpu::kShieldsOff, game::kStatusWait, hit));
    CHECK(!hit);
    CHECK(!lab::stepPlayerShield(cpu::kShieldsOff, game::kStatusGuardOn, hit));
    CHECK(!hit);
    CHECK(!lab::stepPlayerShield(cpu::kShieldsOff, game::kStatusGuard, hit));
    CHECK(!hit);
    CHECK(!lab::stepPlayerShield(cpu::kShieldsOff, game::kStatusGuardDamage, hit));
    CHECK(hit);
    CHECK(!lab::stepPlayerShield(cpu::kShieldsOff, game::kStatusGuardOff, hit));
    CHECK(!hit);
}

TEST(player_shields, until_hit_holds_passively_then_drains_on_hit_and_refills_on_drop)
{
    bool hit = false;
    // Standing and passive guard hold full shield.
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusWait, hit));
    CHECK(!hit);
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardOn, hit));
    CHECK(!hit);
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuard, hit));
    CHECK(!hit);

    // First hit latches before writing so shield damage applies immediately.
    CHECK(!lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardDamage, hit));
    CHECK(hit);
    // Still holding shield after stun (or through follow-up hits): stays un-refilled.
    CHECK(!lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuard, hit));
    CHECK(hit);
    CHECK(!lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardDamage, hit));
    CHECK(hit);

    // Dropping shield (GuardOff, jump, wait, etc.) clears the latch and refills.
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardOff, hit));
    CHECK(!hit);
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardOn, hit));
    CHECK(!hit);
}

TEST(player_shields, on_holds_even_through_guard_damage)
{
    bool hit = false;
    CHECK(lab::stepPlayerShield(cpu::kShieldsOn, game::kStatusGuardOn, hit));
    CHECK(lab::stepPlayerShield(cpu::kShieldsOn, game::kStatusGuard, hit));
    CHECK(lab::stepPlayerShield(cpu::kShieldsOn, game::kStatusGuardDamage, hit));
    CHECK(hit);
    CHECK(lab::stepPlayerShield(cpu::kShieldsOn, game::kStatusGuard, hit));
    CHECK(lab::stepPlayerShield(cpu::kShieldsOn, game::kStatusWait, hit));
    CHECK(!hit);
}

TEST(player_shields, partner_latch_is_independent)
{
    bool popoHit = false;
    bool nanaHit = false;
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuard, popoHit));
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuard, nanaHit));

    // Only Popo is hit; Nana keeps full shield while holding guard.
    CHECK(!lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuardDamage, popoHit));
    CHECK(lab::stepPlayerShield(cpu::kShieldsUntilHit, game::kStatusGuard, nanaHit));
    CHECK(popoHit);
    CHECK(!nanaHit);
}
