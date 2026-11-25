// Training's pause and the CPU's damage (src/main/game/training_damage.cpp):
// the two calls with which Training's pause, as it closes, sets every CPU's
// damage and respawn damage to its CPU Damage value are taken out of the
// game's code while CPU Damage is what it was before the pause, and put
// back otherwise. The image here holds the two call sites and Training's
// menu; whether the game then keeps the damage is what make test-lab checks.
#include <if/if_mngr.h>
#include <string.h>

#include "host/mem.h"
#include "host/test.h"
#include "main/game/training_damage.h"

using namespace tm;

namespace {
    const u32 kSetDamage = 0x80962594u;
    const u32 kSetStartDamage = 0x809625BCu;
    const u32 kSetHud = 0x809625ECu;
    const u32 kBctrl = 0x4E800421u;
    const u32 kBlHud = 0x4B77EEB9u; // bl 0x800E14A4 at kSetHud (read in the game's code)
    const u32 kNop = 0x60000000u;

    struct TrainingMenu {
        u8 raw[0x40]; // +0x28 CPU Damage
    };

    TrainingMenu* s_menu = NULL;

    void setCpuDamage(u32 value)
    {
        memcpy(s_menu->raw + 0x28, &value, sizeof(value));
    }

    // The game's code as Project+ leaves it, and Training's menu at CPU
    // Damage `value`.
    void setUp(u32 value)
    {
        host::mem::setWord(kSetDamage, kBctrl);
        host::mem::setWord(kSetStartDamage, kBctrl);
        host::mem::setWord(kSetHud, kBlHud);
        g_IfMngr = host::mem::make<IfMngr>();
        s_menu = host::mem::make<TrainingMenu>();
        u32 menu = reinterpret_cast<u32>(s_menu);
        memcpy(g_IfMngr->raw + 0x78, &menu, sizeof(menu));
        setCpuDamage(value);
        game::trainingDamageRestore();
    }

    game::MatchState state(bool training, bool paused)
    {
        game::MatchState m = {training, training, paused};
        return m;
    }

    bool callsOut()
    {
        return host::mem::word(kSetDamage) == kNop && host::mem::word(kSetStartDamage) == kNop
               && host::mem::word(kSetHud) == kNop;
    }

    bool callsIn()
    {
        return host::mem::word(kSetDamage) == kBctrl && host::mem::word(kSetStartDamage) == kBctrl
               && host::mem::word(kSetHud) == kBlHud;
    }
}

TEST(training_damage, a_pause_closing_does_not_reset_the_cpus_damage)
{
    setUp(0);
    game::trainingDamageStep(state(true, false));
    CHECK(callsOut());
    // Paused, CPU Damage untouched (the lab menu was used, or nothing): the
    // calls stay out for the step on which the pause closes.
    game::trainingDamageStep(state(true, true));
    game::trainingDamageStep(state(true, true));
    CHECK(callsOut());
    game::trainingDamageStep(state(true, false));
    CHECK(callsOut());
    game::trainingDamageRestore();
}

TEST(training_damage, cpu_damage_changed_in_the_pause_still_applies)
{
    setUp(5);
    game::trainingDamageStep(state(true, false));
    game::trainingDamageStep(state(true, true));
    CHECK(callsOut());
    setCpuDamage(8);
    game::trainingDamageStep(state(true, true));
    CHECK(callsIn());
    // Changed back before closing: as if untouched.
    setCpuDamage(5);
    game::trainingDamageStep(state(true, true));
    CHECK(callsOut());
    setCpuDamage(8);
    game::trainingDamageStep(state(true, true));
    game::trainingDamageStep(state(true, false));
    // After the pause, 8 is the value to compare the next pause with.
    CHECK(callsOut());
    game::trainingDamageStep(state(true, true));
    CHECK(callsOut());
    game::trainingDamageRestore();
}

TEST(training_damage, the_calls_are_put_back_outside_training_and_at_detach)
{
    setUp(0);
    game::trainingDamageStep(state(true, false));
    CHECK(callsOut());
    game::trainingDamageStep(state(false, false));
    CHECK(callsIn());
    game::trainingDamageStep(state(true, false));
    CHECK(callsOut());
    game::trainingDamageRestore();
    CHECK(callsIn());
    // No Training menu to read: left as the game has it.
    game::trainingDamageStep(state(true, false));
    memset(g_IfMngr->raw + 0x78, 0, 4);
    game::trainingDamageStep(state(true, false));
    CHECK(callsIn());
    game::trainingDamageRestore();
}

TEST(training_damage, a_call_site_holding_other_code_is_left_alone)
{
    setUp(0);
    host::mem::setWord(kSetStartDamage, 0x48000001u); // something else's branch
    game::trainingDamageStep(state(true, false));
    CHECK_EQ(kBctrl, host::mem::word(kSetDamage));
    CHECK_EQ(0x48000001u, host::mem::word(kSetStartDamage));
    CHECK_EQ(kBlHud, host::mem::word(kSetHud));
    game::trainingDamageRestore();
    // Looked at again after the detach.
    host::mem::setWord(kSetStartDamage, kBctrl);
    game::trainingDamageStep(state(true, false));
    CHECK(callsOut());
    game::trainingDamageRestore();
}
