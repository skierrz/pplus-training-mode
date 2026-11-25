// CPU counter action selection and fighter-specific technique scripts (src/main/cpu/actions.cpp).
#include "host/test.h"
#include "main/cpu/actions.h"
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
