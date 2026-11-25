// CPU counter action selection and fighter-specific technique scripts (src/main/cpu/actions.cpp).
#include <math.h>
#include <string.h>

#include "host/test.h"
#include "main/cpu/actions.h"
#include "main/cpu/di_logic.h"
#include "main/cpu/fighter_input.h"

using namespace tm::cpu;

namespace {
    enum {
        kMario = 0x0,
        kFox = 0x6,
        kNess = 0xA,
        kPeach = 0xC,
        kGanon = 0x14,
        kLucas = 0x1A,
        kMewtwo = 0x26
    };

    const Step* lastStep(const Action* a)
    {
        const Step* s = a->steps;
        while (s[1].frames != 0)
        {
            s++;
        }
        return s;
    }
}

TEST(cpu_actions, fits_kind_for_float_and_djc)
{
    const int groundFloat0 = 46;
    const int shieldFloat0 = 39;
    const int lists[2] = {kListGround, kListShield};
    const int floatStarts[2] = {groundFloat0, shieldFloat0};

    for (int l = 0; l < 2; l++)
    {
        for (int i = 0; i < 5; i++)
        {
            const Action* f = action(lists[l], floatStarts[l] + i);
            CHECK(f != NULL);
            CHECK((f->flags & kForFloat) != 0);
            CHECK(fitsKind(f, kPeach));
            CHECK(fitsKind(f, kMewtwo));
            CHECK(fitsKind(f, kGanon));
            CHECK(!fitsKind(f, kMario));
            CHECK(!fitsKind(f, kFox));
            CHECK(!fitsKind(f, kNess));
            CHECK(!fitsKind(f, kLucas));

            const Action* d = action(lists[l], floatStarts[l] - 5 + i);
            CHECK(d != NULL);
            CHECK((d->flags & kForDjc) != 0);
            CHECK(fitsKind(d, kNess));
            CHECK(fitsKind(d, kLucas));
            CHECK(fitsKind(d, kMewtwo));
            CHECK(!fitsKind(d, kGanon));
            CHECK(!fitsKind(d, kPeach));
            CHECK(!fitsKind(d, kFox));
        }
    }
}

TEST(cpu_actions, action_for_returns_ganon_float_scripts_vs_peach_mewtwo)
{
    const int lists[2] = {kListGround, kListShield};
    const s8 wantX[5] = {0, 100, -100, 0, 0};
    const s8 wantY[5] = {0, 0, 0, 100, -100};
    const u8 wantRef[5] = {kRefAbs, kRefForward, kRefForward, kRefAbs, kRefAbs};
    const u16 wantBtn[5] = {
        button::kAttack,
        button::kAttack | button::kSmash | button::kCStick,
        button::kAttack | button::kSmash | button::kCStick,
        button::kAttack | button::kSmash | button::kCStick,
        button::kAttack | button::kSmash | button::kCStick
    };

    const int floatStarts[2] = {46, 39};
    for (int l = 0; l < 2; l++)
    {
        int list = lists[l];
        int float0 = floatStarts[l];

        // Non-technique action passes through identically for all fighters.
        const Action* plain = action(list, 1);
        CHECK(plain != NULL);
        CHECK(actionFor(list, 1, kGanon) == plain);
        CHECK(actionFor(list, 1, kPeach) == plain);
        CHECK(actionFor(list, 1, kMario) == plain);

        // None / out-of-range returns NULL.
        CHECK(actionFor(list, 0, kGanon) == NULL);
        CHECK(actionFor(list, actionCount(list), kGanon) == NULL);

        for (int i = 0; i < 5; i++)
        {
            int idx = float0 + i;
            const Action* base = action(list, idx);
            const Action* peach = actionFor(list, idx, kPeach);
            const Action* mewtwo = actionFor(list, idx, kMewtwo);
            const Action* ganon = actionFor(list, idx, kGanon);

            // Peach and Mewtwo get the table's jump+down float scripts.
            CHECK(peach == base);
            CHECK(mewtwo == base);
            CHECK_EQ(kUntilSpecial, peach->steps[1].until);
            CHECK_EQ(-100, peach->steps[1].y);

            // Characters without float get NULL.
            CHECK(actionFor(list, idx, kMario) == NULL);
            CHECK(actionFor(list, idx, kFox) == NULL);
            CHECK(actionFor(list, idx, kNess) == NULL);

            // Ganondorf gets his airborne Neutral B Dead Man's Float scripts.
            CHECK(ganon != NULL);
            CHECK(ganon != base);
            CHECK((ganon->flags & kForFloat) != 0);
            CHECK_EQ(button::kJump, ganon->steps[0].buttons);
            CHECK_EQ(kUntilTakeoff, ganon->steps[1].until);
            CHECK_EQ(button::kSpecial, ganon->steps[2].buttons);
            CHECK_EQ(1, ganon->steps[2].frames);
            CHECK_EQ(0, ganon->steps[2].x);
            CHECK_EQ(0, ganon->steps[2].y);
            // Neutral wait through async frame 5 before the aerial.
            CHECK_EQ(0, ganon->steps[3].buttons);
            CHECK_EQ(0, ganon->steps[3].x);
            CHECK_EQ(0, ganon->steps[3].y);
            CHECK_EQ(5, ganon->steps[3].frames);
            CHECK_EQ(kUntilNone, ganon->steps[3].until);

            // Final step is the expected aerial without holding Jump.
            const Step* aerial = lastStep(ganon);
            CHECK(aerial == &ganon->steps[4]);
            CHECK_EQ(wantBtn[i], aerial->buttons);
            CHECK_EQ(wantX[i], aerial->x);
            CHECK_EQ(wantY[i], aerial->y);
            CHECK_EQ(wantRef[i], aerial->ref);
            CHECK_EQ(1, aerial->frames);
        }
    }
}

TEST(cpu_actions, custom_counter_pool_defaults_sanitize_and_pick)
{
    for (int list = 0; list < kListCount; list++)
    {
        int idx = customPoolIndex(list);
        CHECK(idx > 0 && idx < actionCount(list));
        CHECK_EQ(static_cast<int>(kActionCustomPool), actionKind(list, idx));

        u8 bits[8];
        poolDefaults(list, bits);
        int pick = randomPoolAction(list, bits, kMario, 0);
        CHECK(pick > 0 && pick < idx);

        // Restrict Ground/Air/Shield pool strictly to two chosen actions and verify picks stay in subset.
        memset(bits, 0, sizeof(bits));
        int a1 = 1;
        int a2 = (list == kListAir) ? 11 : 3;
        bits[a1 >> 3] |= static_cast<u8>(1u << (a1 & 7));
        bits[a2 >> 3] |= static_cast<u8>(1u << (a2 & 7));
        // Set out-of-range / meta bit (idx itself) and verify poolSanitize strips it.
        bits[idx >> 3] |= static_cast<u8>(1u << (idx & 7));
        poolSanitize(list, bits);
        CHECK_EQ(0, bits[idx >> 3] & (1u << (idx & 7)));

        bool saw1 = false, saw2 = false;
        for (u32 r = 0; r < 16; r++)
        {
            int p = randomPoolAction(list, bits, kMario, r);
            CHECK(p == a1 || p == a2);
            if (p == a1) saw1 = true;
            if (p == a2) saw2 = true;
        }
        CHECK(saw1 && saw2);

        // Empty pool returns 0 (None).
        memset(bits, 0, sizeof(bits));
        CHECK_EQ(0, randomPoolAction(list, bits, kMario, 5));
    }
}

TEST(cpu_actions, tdi_ten_modes_custom_vector_and_just_out_damage_air)
{
    // Stable 0..4 indices preserved.
    CHECK_EQ(0, static_cast<int>(kTdiRandom));
    CHECK_EQ(4, static_cast<int>(kTdiNone));
    CHECK_EQ(9, static_cast<int>(kTdiCustom));
    CHECK_EQ(10, static_cast<int>(kTdiCount));

    // resolveRandomTdi cycles through 7 dynamic modes (never Random or Custom).
    for (u32 r = 0; r < 14; r++)
    {
        int k = resolveRandomTdi(r);
        CHECK(k != kTdiRandom && k != kTdiCustom && k < kTdiCount);
    }

    // Slight Away has ~0.42x magnitude; Down & Away points away and down; Custom normalizes >1 vectors.
    float ox = 0.0f, oy = 0.0f;
    computeTdiStick(kTdiSlightAway, 0.7071f, 0.7071f, 1.0f, 1.0f, 0, 0.0f, 0.0f, ox, oy);
    float mag = sqrtf(ox * ox + oy * oy);
    CHECK(fabsf(mag - 0.42f) < 0.02f);

    computeTdiStick(kTdiDownAway, 0.0f, 1.0f, 1.0f, 1.0f, 0, 0.0f, 0.0f, ox, oy);
    CHECK(ox < -0.65f && oy < -0.65f);

    computeTdiStick(kTdiCustom, 0.0f, 1.0f, 1.0f, 1.0f, 0, 1.0f, 1.0f, ox, oy);
    CHECK(fabsf(ox - 0.7071f) < 0.01f && fabsf(oy - 0.7071f) < 0.01f);

    // cpuJustOut: handles CC hitstun >1 -> 0 skips, but NEVER triggers during Landing/LandingLight lag.
    CHECK(cpuJustOut(true, 1, 0, 0, false, tm::game::kStatusDamage));
    CHECK(cpuJustOut(true, 3, 0, 0, false, tm::game::kStatusDamage));
    CHECK(!cpuJustOut(true, 1, 0, 0, false, tm::game::kStatusLanding));
    CHECK(!cpuJustOut(true, 2, 0, 0, false, tm::game::kStatusLandingLight));
    CHECK(!cpuJustOut(true, 1, 0, 1, false, tm::game::kStatusDamage));
    CHECK(!cpuJustOut(false, 1, 0, 0, false, tm::game::kStatusDamage));
}
