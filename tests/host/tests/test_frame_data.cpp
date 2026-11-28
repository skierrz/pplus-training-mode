// The frame data service (src/extra/frame_data.cpp): the walk over a move's
// animation commands and the count of frames until a fighter can act.
//
// The mod's source is included here, unchanged, so that its file-local
// functions (walk, scan, build, ticksUntil, query) can be called directly.
// Expected values are worked out by hand from the game's rules as that file
// states them; where the emulator tests (make test-frames) measured the
// same case, the comment says so.
#define TM_MODULE_EXTRA 1
#include "extra/frame_data.cpp"

#include "host/dump.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/script.h"
#include "host/test.h"

namespace tm {
namespace extra {
    enum {
        kAllowInterrupt = 0x64, // group; type 0 allows, 1 disallows
        kStatus = 0x02,         // group: type 0/1 change status, 4 one more requirement, 0xA enable a group
        kMotion = 0x04,         // group: type 7 frame speed
        kCollision = 0x06,      // group: type 5 body, 0 hitbox, 3 remove one, 4 remove all, 0xA/0xD grab, 0xF throw
        kStatusEscapeF = 0x1F,
        kStatusEscapeB = 0x20
    };

    // What a fighter's modules would say at the start of a move whose
    // subaction script is `sub`: clock 0, nothing run yet, speed 1.
    static Live liveAt(const u32* sub, int status)
    {
        Live v;
        memset(&v, 0, sizeof(v));
        v.subStart = sub;
        v.subCurrent = sub;
        v.rate = 1.0f;
        v.kind = -1;
        v.status = status;
        return v;
    }

    static FrameRecord recordOf(const host::Script& sub)
    {
        Live v = liveAt(sub.list(), game::kStatusEscape);
        FrameRecord rec;
        build(rec, v);
        return rec;
    }

    static ActIn ask(const Live& v, FrameRecord* rec = NULL)
    {
        Slot slot;
        memset(&slot, 0, sizeof(slot));
        ActIn a = query(slot, v);
        if (rec != NULL)
        {
            *rec = slot.rec;
        }
        return a;
    }

    static void checkWindow(const FdWindow& w, float on, float off, int kind)
    {
        CHECK_EQ(on, w.on);
        CHECK_EQ(off, w.off);
        CHECK_EQ(kind, w.kind);
    }

    // ---- timers -----------------------------------------------------------

    TEST(frame_data, timers_are_thresholds_on_the_clock)
    {
        host::Script s;
        s.frame(5).cmd(kCollision, 5).integer(2) // intangible from 5
            .wait(3).cmd(kCollision, 5).integer(0) // normal again at 5 + 3
            .frame(4)                              // already past: does nothing
            .wait(2).cmd(kAllowInterrupt, 0)       // 8 + 2
            .end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(10.0f, rec.openTime);
        CHECK_EQ(0x80000000u, rec.openGroups);
        CHECK_EQ(-1.0f, rec.throwTime);
        REQUIRE(rec.bodyCount == 1);
        checkWindow(rec.body[0], 5.0f, 8.0f, kFdIntangible);
        CHECK_EQ(0, rec.hitCount);
        CHECK_EQ(8, rec.commands);
    }

    TEST(frame_data, a_timer_without_a_scalar_does_not_wait)
    {
        host::Script s;
        s.cmd(0, 1).integer(30) // Wait with an integer
            .cmd(0, 2)          // Wait until frame with nothing
            .cmd(0, 2).variable(0x20000001)
            .frame(3).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(3.0f, recordOf(s).openTime);
    }

    TEST(frame_data, only_the_first_open_counts_and_a_reclose_is_flagged)
    {
        host::Script s;
        s.frame(12).cmd(kAllowInterrupt, 0).frame(20).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(12.0f, rec.openTime);
        CHECK_EQ(kFdValid, rec.flags);

        host::Script again;
        again.frame(12).cmd(kAllowInterrupt, 0).frame(20).cmd(kAllowInterrupt, 1).end();
        rec = recordOf(again);
        CHECK_EQ(12.0f, rec.openTime);
        CHECK_EQ(kFdValid | kFdReclose, rec.flags);

        // Disallowed before anything was allowed: nothing to flag.
        host::Script before;
        before.cmd(kAllowInterrupt, 1).frame(9).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(kFdValid, recordOf(before).flags);
    }

    TEST(frame_data, enable_group_opens_action_groups_only)
    {
        host::Script s;
        s.frame(6)
            .cmd(kStatus, 0xA).integer(game::kGroupGroundItem)   // not an action group: ignored
            .frame(8)
            .cmd(kStatus, 0xA).integer(game::kGroupGroundAttack) // opens
            .cmd(kStatus, 0xA).integer(game::kGroupGroundJump)   // same time: added
            .frame(9)
            .cmd(kStatus, 0xA).integer(game::kGroupGroundGuard)  // later: not part of the first open
            .end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(8.0f, rec.openTime);
        CHECK_EQ((1u << game::kGroupGroundAttack) | (1u << game::kGroupGroundJump), rec.openGroups);
        CHECK_EQ(kFdValid, rec.flags);
    }

    // ---- animation speed --------------------------------------------------

    // Bowser's rolls as read in the game (frame_data.cpp, notes of that job):
    // forward, Frame 23: speed 1.875, Frame 38: Allow Interrupt. Counted by
    // hand: 23 frames at speed 1 bring the clock to 23, then 23 + 8 x 1.875
    // = 38: the interrupts open in frame 31. make test-frames measured 31.
    TEST(frame_data, bowser_forward_roll_speed_1_875_from_23_opens_in_frame_31)
    {
        host::Script s;
        s.frame(3).cmd(kCollision, 5).integer(2).frame(19).cmd(kCollision, 5).integer(0)
            .frame(23).cmd(kMotion, 7).real(1.875f)
            .frame(38).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec;
        ActIn a = ask(liveAt(s.list(), kStatusEscapeF), &rec);
        REQUIRE(rec.rateCount == 1);
        CHECK_EQ(23.0f, rec.rate[0].time);
        CHECK_EQ(1.875f, rec.rate[0].value);
        CHECK_EQ(0, rec.rate[0].multiply);
        CHECK_EQ(38.0f, rec.openTime);
        CHECK_EQ(31, a.frames);
        CHECK_EQ(main::kActScript, a.how);
        CHECK_EQ(1, a.certain);

        // Part-way: after 27 frames the clock is 23 + 4 x 1.875 = 30.5 and
        // the speed is 1.875; four more frames reach 38.
        Live v = liveAt(s.list(), kStatusEscapeF);
        v.subCurrent = s.at(7);
        v.clock = 30.5f;
        v.rate = 1.875f;
        CHECK_EQ(4, ask(v).frames);
        // On the frame the clock has just reached 38 the fighter's groups
        // are open; the status alone does not say so here, so the walk's
        // answer is "allowed already, yet closed": not certain.
        v.clock = 38.0f;
        a = ask(v);
        CHECK_EQ(0, a.certain);
    }

    // Back roll, Frame 26: speed 2.4, Frame 38: Allow Interrupt. 26 frames,
    // then five steps of 2.4 (28.4, 30.8, 33.2, 35.6, 38.0): frame 31, as
    // measured. 2.4 is not exact in single precision; the fifth step lands a
    // hair above 38, not below.
    TEST(frame_data, bowser_back_roll_speed_2_4_from_26_opens_in_frame_31)
    {
        host::Script s;
        s.frame(26).cmd(kMotion, 7).real(2.4f).frame(38).cmd(kAllowInterrupt, 0).end();
        ActIn a = ask(liveAt(s.list(), kStatusEscapeB));
        CHECK_EQ(31, a.frames);
        CHECK_EQ(main::kActScript, a.how);
        CHECK_EQ(1, a.certain);
    }

    // A second argument makes Project+ multiply the speed. Speed 2 from 4,
    // times 0.5 from 10, open at 14: clock 1 2 3 4 | 6 8 10 | 11 12 13 14:
    // frame 11.
    TEST(frame_data, speed_set_then_multiplied)
    {
        host::Script s;
        s.frame(4).cmd(kMotion, 7).real(2.0f)
            .frame(10).cmd(kMotion, 7).real(0.5f).integer(0)
            .frame(14).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec;
        ActIn a = ask(liveAt(s.list(), kStatusEscapeF), &rec);
        REQUIRE(rec.rateCount == 2);
        CHECK_EQ(0, rec.rate[0].multiply);
        CHECK_EQ(1, rec.rate[1].multiply);
        CHECK_EQ(11, a.frames);
    }

    TEST(frame_data, speed_from_a_variable_or_a_set_frame_is_not_certain)
    {
        host::Script s;
        s.frame(4).cmd(kMotion, 7).variable(0x21000004).frame(20).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec;
        ActIn a = ask(liveAt(s.list(), kStatusEscapeF), &rec);
        CHECK_EQ(kFdValid | kFdRateVar, rec.flags);
        CHECK_EQ(20, a.frames); // the latest reading
        CHECK_EQ(0, a.certain);

        host::Script frameSet;
        frameSet.frame(4).cmd(kMotion, 6).real(10.0f).frame(20).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(kFdValid | kFdFrameSet, recordOf(frameSet).flags);

        // A change of subaction ends what can be known.
        host::Script change;
        change.frame(4).cmd(kMotion, 0).integer(0x44).frame(20).cmd(kAllowInterrupt, 0).end();
        FrameRecord r = recordOf(change);
        CHECK_EQ(kFdValid | kFdFlow, r.flags);
        CHECK_EQ(-1.0f, r.openTime);
    }

    TEST(frame_data, a_speed_of_zero_never_gets_there)
    {
        host::Script s;
        s.frame(5).cmd(kMotion, 7).real(0.0f).frame(20).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(-1, ask(liveAt(s.list(), kStatusEscapeF)).frames);
    }

    // ---- conditions -------------------------------------------------------

    // Hitbox variants of one move: both sides take the same time and open
    // nothing, so the condition does not matter for the timeline.
    TEST(frame_data, a_condition_with_the_same_timeline_on_both_sides_is_certain)
    {
        host::Script s;
        s.frame(7).cmd(kCollision, 0).integer(0x2E0002)
            .iff().cmd(kCollision, 0).integer(0x000001)
            .orElse().cmd(kCollision, 0).integer(0x130001)
            .endIf()
            .wait(5).cmd(kCollision, 4)
            .frame(40).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(40.0f, rec.openTime);
        REQUIRE(rec.hitCount == 1);
        checkWindow(rec.hit[0], 7.0f, 12.0f, kFdHit);
    }

    TEST(frame_data, different_hitboxes_on_the_sides_are_noted_not_uncertain)
    {
        host::Script s;
        s.frame(7)
            .iff().cmd(kCollision, 0).integer(0x000001)
            .orElse() // no hitbox on this side
            .endIf()
            .frame(40).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid | kFdWindows, rec.flags);
        CHECK_EQ(0, (rec.flags & kFdUncertain));
        CHECK_EQ(40.0f, rec.openTime);
    }

    // An open inside a condition is nobody's to promise: the record is
    // uncertain, nothing inside opens, and the walk goes on from the latest
    // time a side reached.
    TEST(frame_data, an_open_under_a_condition_makes_the_move_uncertain)
    {
        host::Script s;
        s.iff().frame(10).cmd(kAllowInterrupt, 0).endIf().frame(30).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec;
        ActIn a = ask(liveAt(s.list(), kStatusEscapeF), &rec);
        CHECK_EQ(kFdValid | kFdCond, rec.flags);
        CHECK_EQ(30.0f, rec.openTime);
        CHECK_EQ(30, a.frames);
        CHECK_EQ(0, a.certain);
    }

    TEST(frame_data, sides_of_different_length_go_on_from_the_latest)
    {
        host::Script s;
        s.iff().wait(5).elseIf().wait(9).orElse().wait(2).endIf().wait(1).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid | kFdCond, rec.flags);
        CHECK_EQ(10.0f, rec.openTime);
    }

    TEST(frame_data, a_condition_that_never_ends_stops_the_walk)
    {
        host::Script s;
        s.frame(3).iff().wait(2).end(); // the list ends inside the If
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid | kFdLoop, rec.flags);
        CHECK_EQ(-1.0f, rec.openTime);
    }

    // ---- subroutines, gotos, loops ----------------------------------------

    TEST(frame_data, subroutine_and_goto_are_followed)
    {
        host::Script sub;
        sub.wait(4).cmd(kCollision, 5).integer(1).ret();
        host::Script main;
        main.frame(2).call(sub).wait(1).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(main);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(7.0f, rec.openTime);
        REQUIRE(rec.bodyCount == 1);
        checkWindow(rec.body[0], 6.0f, -1.0f, kFdInvincible);

        host::Script target;
        target.wait(2).cmd(kAllowInterrupt, 0).end();
        host::Script from;
        from.frame(3).jump(target).frame(1).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(5.0f, recordOf(from).openTime);
    }

    // kMaxDepth is 4: four nested calls are walked, the fifth is not.
    TEST(frame_data, subroutines_nest_four_deep)
    {
        host::Script level[5];
        for (int i = 4; i >= 0; i--)
        {
            level[i].wait(1);
            if (i < 4)
            {
                level[i].call(level[i + 1]);
            }
            level[i].ret();
        }
        host::Script four;
        four.call(level[1]).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(four);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(4.0f, rec.openTime);

        host::Script five;
        five.call(level[0]).cmd(kAllowInterrupt, 0).end();
        rec = recordOf(five);
        CHECK_EQ(kFdValid | kFdCall, rec.flags);
        CHECK_EQ(4.0f, rec.openTime); // the fifth level's wait is not counted
    }

    TEST(frame_data, a_call_that_cannot_be_read_is_flagged)
    {
        host::Script s;
        s.cmd(0, 7).pointer(0x00001234).frame(5).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid | kFdCall, rec.flags);
        CHECK_EQ(5.0f, rec.openTime);
    }

    TEST(frame_data, loops_run_their_count)
    {
        host::Script three;
        three.loop(3).wait(2).endLoop().cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(6.0f, recordOf(three).openTime);

        // A count of 0 still runs the body once.
        host::Script zero;
        zero.loop(0).wait(2).endLoop().cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(2.0f, recordOf(zero).openTime);

        host::Script nested;
        nested.loop(2).loop(3).wait(1).endLoop().wait(10).endLoop().cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(nested);
        CHECK_EQ(26.0f, rec.openTime);
        CHECK_EQ(kFdValid, rec.flags);
    }

    TEST(frame_data, endless_and_too_deep_loops_stop_the_walk)
    {
        host::Script endless;
        endless.loop(-1).wait(2).endLoop().cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(endless);
        CHECK_EQ(kFdValid | kFdLoop, rec.flags);
        CHECK_EQ(-1.0f, rec.openTime);
        CHECK_EQ(-1, ask(liveAt(endless.list(), kStatusEscapeF)).frames);

        // kMaxLoops is 4.
        host::Script deep;
        deep.loop(1).loop(1).loop(1).loop(1).loop(1).wait(1).endLoop().endLoop().endLoop().endLoop().endLoop()
            .cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(kFdValid | kFdLoop, recordOf(deep).flags);

        host::Script whileLoop;
        whileLoop.cmd(0, 0x14).requirement(7).wait(1).cmd(0, 0x15).cmd(kAllowInterrupt, 0).end();
        CHECK_EQ(kFdValid | kFdLoop, recordOf(whileLoop).flags);
    }

    // ---- the caps ---------------------------------------------------------

    // kFdMaxSubCommands is 256 commands read, the End included.
    TEST(frame_data, the_command_limit)
    {
        host::Script fits;
        fits.filler(253).frame(9).cmd(kAllowInterrupt, 0).end();
        FrameRecord rec = recordOf(fits);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(9.0f, rec.openTime);
        CHECK_EQ(256, rec.commands);

        host::Script tooLong;
        tooLong.filler(300).frame(9).cmd(kAllowInterrupt, 0).end();
        rec = recordOf(tooLong);
        CHECK_EQ(kFdValid | kFdLoop, rec.flags);
        CHECK_EQ(-1.0f, rec.openTime);
        CHECK_EQ(-1, ask(liveAt(tooLong.list(), kStatusEscapeF)).frames);
    }

    TEST(frame_data, more_windows_than_the_record_holds)
    {
        // Seven hitbox windows; the record keeps kFdMaxHit (6).
        host::Script hits;
        for (int i = 0; i < 7; i++)
        {
            hits.frame(10.0f * i + 1).cmd(kCollision, 0).integer(0).wait(2).cmd(kCollision, 4);
        }
        hits.end();
        FrameRecord rec = recordOf(hits);
        CHECK_EQ(kFdValid | kFdOverflow, rec.flags);
        CHECK_EQ(6, rec.hitCount);
        checkWindow(rec.hit[5], 51.0f, 53.0f, kFdHit);

        // Three body windows; the record keeps kFdMaxBody (2).
        host::Script bodies;
        for (int i = 0; i < 3; i++)
        {
            bodies.frame(10.0f * i + 1).cmd(kCollision, 5).integer(2).wait(2).cmd(kCollision, 5).integer(0);
        }
        bodies.end();
        rec = recordOf(bodies);
        CHECK_EQ(kFdValid | kFdOverflow, rec.flags);
        CHECK_EQ(2, rec.bodyCount);

        // Five speed changes; the record keeps kFdMaxRate (4) and the count
        // ahead is no longer certain.
        host::Script rates;
        for (int i = 0; i < 5; i++)
        {
            rates.frame(2.0f * i + 1).cmd(kMotion, 7).real(1.0f);
        }
        rates.frame(30).cmd(kAllowInterrupt, 0).end();
        rec = recordOf(rates);
        CHECK_EQ(kFdValid | kFdOverflow | kFdRateVar, rec.flags);
        CHECK_EQ(4, rec.rateCount);
    }

    // ---- hitboxes, grab boxes, the throw ----------------------------------

    TEST(frame_data, hit_windows_follow_the_ids)
    {
        host::Script s;
        s.frame(4).cmd(kCollision, 0).integer(0x110000).cmd(kCollision, 0x15).integer(0x110001)
            .frame(6).cmd(kCollision, 3).integer(0) // id 1 is still out
            .frame(8).cmd(kCollision, 3).integer(1) // the last one goes: the window ends
            .frame(8).cmd(kCollision, 0).integer(0) // out again at that very time: the window goes on
            .frame(12).cmd(kCollision, 4)
            .frame(20).cmd(kCollision, 0xA).frame(22).cmd(kCollision, 0xD) // a grab box
            .frame(30).cmd(kCollision, 0xF)                                // the throw's release
            .frame(31).cmd(kCollision, 8).integer(3)                       // a per-bone body state
            .end();
        FrameRecord rec = recordOf(s);
        CHECK_EQ(kFdValid | kFdPartialBody, rec.flags);
        REQUIRE(rec.hitCount == 2);
        checkWindow(rec.hit[0], 4.0f, 12.0f, kFdHit);
        checkWindow(rec.hit[1], 20.0f, 22.0f, kFdCatch);
        CHECK_EQ(30.0f, rec.throwTime);
    }

    // ---- the status's own scripts -----------------------------------------

    // The dodges' action script as read in the game: Fall when in the air,
    // Wait at the animation's end on the ground.
    static void dodgeAction(host::Script& act)
    {
        act.cmd(kStatus, 1).integer(game::kStatusFall).requirement(4)
            .cmd(kStatus, 1).integer(game::kStatusWait).requirement(1)
            .cmd(kStatus, 4).requirement(3)
            .end();
    }

    // No Allow Interrupt in the subaction: the animation's end frees the
    // fighter. isEnd is frame >= length - 1, and the frame after n frames at
    // speed 1 is n: with 32 frames, frame 31 (make test-frames measured
    // Fox's rolls, which have no Allow Interrupt, at 31).
    TEST(frame_data, the_animations_end_frees_the_fighter)
    {
        host::Script act;
        dodgeAction(act);
        host::Script sub;
        sub.frame(3).cmd(kCollision, 5).integer(2).frame(19).cmd(kCollision, 5).integer(0).end();
        Live v = liveAt(sub.list(), kStatusEscapeF);
        v.actStart = act.list();
        v.length = 32;
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        CHECK_EQ(kFdEndFree, rec.end[0]);
        CHECK_EQ(game::kStatusWait, rec.endStatus[0]);
        CHECK_EQ(kFdEndNone, rec.end[1]);
        CHECK_EQ(31, a.frames);
        CHECK_EQ(main::kActEnd, a.how);
        CHECK_EQ(1, a.certain);

        // The script's open comes first when it is earlier.
        host::Script early;
        early.frame(22).cmd(kAllowInterrupt, 0).end();
        v.subStart = v.subCurrent = early.list();
        a = ask(v);
        CHECK_EQ(22, a.frames);
        CHECK_EQ(main::kActScript, a.how);

        // A looping animation never ends, and in the air this script frees
        // nothing.
        v.subStart = v.subCurrent = sub.list();
        v.looped = true;
        CHECK_EQ(-1, ask(v).frames);
        v.looped = false;
        v.air = true;
        CHECK_EQ(-1, ask(v).frames);
    }

    TEST(frame_data, an_end_that_leads_into_another_status_frees_nothing)
    {
        host::Script act;
        act.cmd(kStatus, 1).integer(game::kStatusAttack).requirement(1).end();
        host::Script sub;
        sub.frame(3).end();
        Live v = liveAt(sub.list(), kStatusEscapeF);
        v.actStart = act.list();
        v.length = 32;
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdEndOther, rec.end[0]);
        CHECK_EQ(kFdEndOther, rec.end[1]);
        CHECK_EQ(game::kStatusAttack, rec.endStatus[0]);
        CHECK_EQ(-1, a.frames);
    }

    TEST(frame_data, a_term_with_another_requirement_is_not_the_moves_end)
    {
        host::Script act;
        act.cmd(kStatus, 0).integer(0x277F).integer(game::kStatusWait).requirement(1) // term id, status, requirement
            .cmd(kStatus, 4).requirement(0x30)                                        // and something else
            .end();
        host::Script sub;
        sub.frame(3).end();
        Live v = liveAt(sub.list(), kStatusEscapeF);
        v.actStart = act.list();
        FrameRecord rec;
        ask(v, &rec);
        CHECK_EQ(kFdEndNone, rec.end[0]);
        CHECK_EQ(kFdEndNone, rec.end[1]);
    }

    TEST(frame_data, ends_that_differ_between_the_sides_of_a_condition)
    {
        host::Script act;
        act.iff().cmd(kStatus, 1).integer(game::kStatusWait).requirement(1)
            .orElse().cmd(kStatus, 1).integer(game::kStatusSquatWait).requirement(1)
            .endIf().end();
        host::Script sub;
        sub.frame(3).end();
        Live v = liveAt(sub.list(), kStatusEscapeF);
        v.actStart = act.list();
        v.length = 20;
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid | kFdEndCondGround | kFdEndCondAir, rec.flags);
        CHECK_EQ(kFdEndFree, rec.end[0]);
        CHECK_EQ(19, a.frames);
        CHECK_EQ(0, a.certain);
    }

    TEST(frame_data, a_status_script_that_opens_interrupts_itself)
    {
        host::Script sub;
        sub.frame(30).cmd(kAllowInterrupt, 0).end();

        // At once when the status begins: noted; the groups being closed
        // all the same makes the count uncertain.
        host::Script atOnce;
        atOnce.cmd(kAllowInterrupt, 0).end();
        Live v = liveAt(sub.list(), kStatusEscapeF);
        v.actStart = atOnce.list();
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid | kFdStatusOpen, rec.flags);
        CHECK_EQ(0, a.certain);

        // After a timer, under a condition, or in the status's loop: the
        // status decides.
        host::Script timed;
        timed.wait(5).cmd(kStatus, 0xA).integer(game::kGroupGroundAttack).end();
        v.actStart = timed.list();
        ask(v, &rec);
        CHECK_EQ(kFdValid | kFdStatusDecides, rec.flags);

        host::Script cond;
        cond.iff().cmd(kAllowInterrupt, 0).endIf().end();
        v.actStart = cond.list();
        ask(v, &rec);
        CHECK_EQ(kFdValid | kFdStatusDecides, rec.flags);

        host::Script none;
        none.end();
        v.actStart = none.list();
        v.loopStart = atOnce.list();
        a = ask(v, &rec);
        CHECK_EQ(kFdValid | kFdStatusDecides, rec.flags);
        CHECK_EQ(30, a.frames);
        CHECK_EQ(0, a.certain);
    }

    // ---- the statuses with a rule of their own ----------------------------

    // Landing: the status sets its flag in the frame that begins with the
    // animation at or past the fighter's landing frame parameter. With the
    // parameter at 4 the frames begin at 0, 1, 2, 3, 4: the fifth.
    TEST(frame_data, landing_opens_by_the_landing_frame_parameter)
    {
        host::fakes::FakeFighter& f = host::fakes::addFighter();
        f.accesser->setConstant(kLandingFrame, 4.0f);
        f.accesser->setConstant(kLandingLightFrame, 2.0f);
        host::Script sub;
        sub.end();
        Slot slot;
        memset(&slot, 0, sizeof(slot));
        slot.fighter = f.fighter;
        Live v = liveAt(sub.list(), game::kStatusLanding);
        v.modules = f.modules;
        ActIn a = query(slot, v);
        CHECK_EQ(kRuleLanding, slot.rec.rule);
        CHECK_EQ(5, a.frames);
        CHECK_EQ(main::kActRule, a.how);
        CHECK_EQ(1, a.certain);

        v.frame = 3.0f; // the next frame begins at 4
        CHECK_EQ(2, query(slot, v).frames);

        v.status = game::kStatusLandingLight;
        v.frame = 0.0f;
        CHECK_EQ(3, query(slot, v).frames);

        // LA-Bit[3] set, or a parameter of -1: the rule gives nothing.
        f.work->set(kNoLandingInterrupt, 1);
        CHECK_EQ(-1, query(slot, v).frames);
        f.work->set(kNoLandingInterrupt, 0);
        f.accesser->setConstant(kLandingLightFrame, -1.0f);
        CHECK_EQ(-1, query(slot, v).frames);
    }

    TEST(frame_data, shield_release_counts_its_timer_down)
    {
        host::fakes::FakeFighter& f = host::fakes::addFighter();
        f.work->set(kGuardOffTimer, 7);
        host::Script sub;
        sub.end();
        Slot slot;
        memset(&slot, 0, sizeof(slot));
        slot.fighter = f.fighter;
        Live v = liveAt(sub.list(), game::kStatusGuardOff);
        v.modules = f.modules;
        // Its script lets jumps out from the start: only the jump group on.
        v.groups = 1u << game::kGroupGroundJump;
        ActIn a = query(slot, v);
        CHECK_EQ(kRuleGuardOff, slot.rec.rule);
        CHECK_EQ(7, a.frames);
        CHECK_EQ(main::kActRule, a.how);

        // With the attack group on it can act now.
        v.groups |= 1u << game::kGroupGroundAttack;
        a = query(slot, v);
        CHECK_EQ(0, a.frames);
        CHECK_EQ(main::kActOpen, a.how);
    }

    // ---- stun and hitlag --------------------------------------------------

    // A hit stop reading n > 0 after a frame means n - 1 frozen frames.
    TEST(frame_data, stun_and_hitlag)
    {
        host::Script sub;
        sub.end();
        Live v = liveAt(sub.list(), game::kStatusDamage);
        v.hitstun = 20;
        v.hitlag = 5;
        ActIn a = ask(v);
        CHECK_EQ(24, a.frames);
        CHECK_EQ(main::kActStun, a.how);
        CHECK_EQ(1, a.certain);

        v.status = game::kStatusGuardDamage;
        v.hitstun = 0;
        v.shieldStun = 7;
        v.hitlag = 0;
        CHECK_EQ(7, ask(v).frames);

        // Free to act, but frozen by a hit it dealt.
        v.status = game::kStatusWait;
        v.shieldStun = 0;
        v.hitlag = 4;
        a = ask(v);
        CHECK_EQ(4, a.frames);
        CHECK_EQ(main::kActOpen, a.how);

        // An attacker in hitlag: its frozen frames, then the script's.
        host::Script jab;
        jab.frame(15).cmd(kAllowInterrupt, 0).end();
        v = liveAt(jab.list(), game::kStatusAttack);
        v.subCurrent = jab.at(1);
        v.clock = 3.0f;
        v.hitlag = 6;
        CHECK_EQ(5 + 12, ask(v).frames);
    }

    // ---- through the public functions, from a fighter's threads -----------

    struct Fixture {
        host::fakes::FakeFighter* f;
        host::AnimCmd threads;
        host::Script sub;
        host::Script act;
        host::Script loop;
    };

    static void start(Fixture& x, int status)
    {
        frameDataAttach();
        x.f = &host::fakes::addFighter();
        x.f->modules->m_animCmdModule = x.threads.module();
        x.f->status = status;
        x.act.end();
        x.loop.end();
        x.threads.setThread(kUnitActionMain, x.act.list(), x.act.list(), 0.0f);
        x.threads.setThread(kUnitLoop, x.loop.list(), x.loop.list(), 0.0f);
    }

    TEST(frame_data, the_public_functions_read_the_fighters_threads)
    {
        Fixture x;
        start(x, kStatusEscapeF);
        x.sub.frame(3).cmd(kCollision, 5).integer(2).frame(19).cmd(kCollision, 5).integer(0)
            .frame(31).cmd(kAllowInterrupt, 0).end();
        x.threads.setThread(kUnitSubactionMain, x.sub.list(), x.sub.at(2), 10.0f);
        x.f->motion->m_frame = 10.0f;

        ActIn a = frameDataActIn(x.f->fighter);
        CHECK_EQ(21, a.frames);
        CHECK_EQ(main::kActScript, a.how);
        CHECK_EQ(1, a.certain);
        int how = -1;
        CHECK_EQ(21, framesUntilActionable(x.f->fighter, &how));
        CHECK_EQ(main::kActScript, how);
        // The body window: begun at 3, ends at 19.
        const FrameRecord* rec = frameRecord(x.f->fighter);
        REQUIRE(rec != NULL);
        REQUIRE(rec->bodyCount == 1);
        CHECK_EQ(0, frameDataFramesUntil(x.f->fighter, rec->body[0].on));
        CHECK_EQ(9, frameDataFramesUntil(x.f->fighter, rec->body[0].off));

        // The record is kept until a script or the status changes: the
        // Allow Interrupt's time rewritten in place (31 to 40) is not seen
        // until the status is another.
        u32 args = host::mem::word(x.sub.address() + 8 * 4 + 4);
        host::mem::setWord(args + 4, 40 * 60000);
        CHECK_EQ(21, frameDataActIn(x.f->fighter).frames);
        x.f->status = kStatusEscapeB;
        CHECK_EQ(30, frameDataActIn(x.f->fighter).frames);
    }

    TEST(frame_data, what_cannot_be_read_gives_no_answer)
    {
        Fixture x;
        start(x, kStatusEscapeF);
        x.sub.frame(31).cmd(kAllowInterrupt, 0).end();
        x.threads.setThread(kUnitSubactionMain, x.sub.list(), x.sub.list(), 0.0f);
        CHECK_EQ(31, frameDataActIn(x.f->fighter).frames);

        // A fighter the trackers do not know.
        Fighter* stranger = host::mem::make<Fighter>();
        CHECK_EQ(-1, frameDataActIn(stranger).frames);
        CHECK(frameRecord(stranger) == NULL);
        CHECK_EQ(-1, frameDataActIn(NULL).frames);

        // A control unit that is not where it should be (its index word).
        host::mem::setWord(x.threads.unitAddress(kUnitSubactionMain) + kUnitIndex, 7);
        CHECK_EQ(-1, frameDataActIn(x.f->fighter).frames);
        host::mem::setWord(x.threads.unitAddress(kUnitSubactionMain) + kUnitIndex, kUnitSubactionMain);
        CHECK_EQ(31, frameDataActIn(x.f->fighter).frames);

        // A script pointer outside memory: no script, no count.
        x.threads.setThread(kUnitSubactionMain, reinterpret_cast<const u32*>(0x00001000), NULL, 0.0f);
        x.f->status = kStatusEscapeB;
        CHECK_EQ(-1, frameDataActIn(x.f->fighter).frames);

        // An odd command address stops the walk.
        x.threads.setThread(kUnitSubactionMain, reinterpret_cast<const u32*>(x.sub.address() + 2), NULL, 0.0f);
        x.f->status = kStatusEscapeF;
        const FrameRecord* rec = frameRecord(x.f->fighter);
        REQUIRE(rec != NULL);
        CHECK_EQ(kFdValid | kFdLoop, rec->flags);
    }

    TEST(frame_data, an_uncertain_count_is_not_handed_to_the_match_module)
    {
        Fixture x;
        start(x, kStatusEscapeF);
        x.sub.iff().frame(10).cmd(kAllowInterrupt, 0).endIf().frame(30).cmd(kAllowInterrupt, 0).end();
        x.threads.setThread(kUnitSubactionMain, x.sub.list(), x.sub.list(), 0.0f);
        ActIn a = frameDataActIn(x.f->fighter);
        CHECK_EQ(30, a.frames);
        CHECK_EQ(0, a.certain);
        int how = -1;
        CHECK_EQ(-1, framesUntilActionable(x.f->fighter, &how));
    }

    // ---- the frame advantage worked out when a hit lands ------------------

    struct Exchange {
        Fixture attacker;
        Fixture defender;
    };

    // Slot 0 jabs slot 1 (Allow Interrupt at 15, its clock at 6, 5 frames
    // of hit stop on both): the attacker is frozen 4 frames, then needs 9.
    static void exchange(Exchange& e, s32 now, bool shield)
    {
        main::loadSettings(host::fakes::block());
        main::settings().osd.frameAdvantage = 1;
        start(e.attacker, game::kStatusAttack);
        e.attacker.sub.frame(15).cmd(kAllowInterrupt, 0).end();
        e.attacker.threads.setThread(kUnitSubactionMain, e.attacker.sub.list(), e.attacker.sub.at(1), 6.0f);
        e.attacker.f->stop->m_hitStopRealFrame = 5;

        e.defender.f = &host::fakes::addFighter();
        e.defender.f->modules->m_animCmdModule = e.defender.threads.module();
        e.defender.act.end();
        e.defender.loop.end();
        e.defender.sub.end();
        e.defender.threads.setThread(kUnitActionMain, e.defender.act.list(), e.defender.act.list(), 0.0f);
        e.defender.threads.setThread(kUnitLoop, e.defender.loop.list(), e.defender.loop.list(), 0.0f);
        e.defender.threads.setThread(kUnitSubactionMain, e.defender.sub.list(), e.defender.sub.list(), 0.0f);
        e.defender.f->stop->m_hitStopRealFrame = 5;
        game::Tracked* t = e.defender.f->tracked;
        if (shield)
        {
            e.defender.f->status = game::kStatusGuardDamage;
            e.defender.f->shieldStun = 10;
            t->cur.shieldReceived = 1;
        }
        else
        {
            e.defender.f->status = game::kStatusDamage;
            e.defender.f->hitstun = 20;
            t->cur.hitReceived = 1;
        }
        t->lastReceived.other = 0;
        t->lastReceived.shield = shield ? 1 : 0;
        t->lastReceived.how = game::kByInflict;
        t->lastReceived.frame = now;
        host::fakes::setTrackerFrame(now);
    }

    static game::Advantage measured(s32 hitFrame, int value, bool shield)
    {
        game::Advantage r;
        memset(&r, 0, sizeof(r));
        r.attacker = 0;
        r.defender = 1;
        r.attackerPlayer = 0;
        r.defenderPlayer = 1;
        r.shield = shield ? 1 : 0;
        r.value = static_cast<s16>(value);
        r.hitFrame = hitFrame;
        return r;
    }

    // Defender: 4 frozen frames and 10 of shield stun = 14; attacker 13.
    TEST(frame_data, advantage_on_shield_is_shown_when_the_hit_lands)
    {
        Exchange e;
        exchange(e, 100, true);
        frameDataFrame();
        CHECK_LOG("advantage predicted: P1 -> P2 on shield +1 (hit 100, attacker in 13 how 2, defender in 14)");
        CHECK_EQ(1, host::fakes::osdCount());
        CHECK_STR("Shield Adv.\n+1f", host::fakes::osdText());
        CHECK_EQ(0, host::fakes::osdPlayer());
        CHECK_EQ(osd::kIdFrameAdvantage, host::fakes::osdId());

        // The measurement agrees: nothing more is posted.
        CHECK(frameDataMeasured(measured(100, 1, true)));
        CHECK_LOG("predicted +1 measured +1 same");
        CHECK_EQ(1, host::fakes::osdCount());
    }

    TEST(frame_data, a_measurement_that_differs_replaces_the_message)
    {
        Exchange e;
        exchange(e, 100, true);
        frameDataFrame();
        CHECK(frameDataMeasured(measured(100, -2, true)));
        CHECK_LOG("predicted +1 measured -2 DIFFERENT");
        CHECK_EQ(2, host::fakes::osdCount());
        CHECK_STR("Shield Adv.\nActual: -2f", host::fakes::osdText());
        // That exchange is done: a second measurement finds nothing.
        CHECK(!frameDataMeasured(measured(100, -2, true)));
    }

    TEST(frame_data, advantage_is_not_shown_with_the_row_off)
    {
        Exchange e;
        exchange(e, 100, true);
        main::settings().osd.frameAdvantage = 0;
        frameDataFrame();
        CHECK_LOG("advantage predicted: P1 -> P2 on shield +1");
        CHECK_EQ(0, host::fakes::osdCount());
        CHECK(!frameDataMeasured(measured(100, 1, true)));
    }

    // A hit that connects, the defender in the air: worked out (4 frozen
    // frames and 20 of hitstun against 13), logged, left to the measurement.
    TEST(frame_data, advantage_on_hit_is_logged_and_left_to_the_measurement)
    {
        Exchange e;
        exchange(e, 200, false);
        e.defender.f->situation->m_kind = Situation_Air;
        frameDataFrame();
        CHECK_LOG("advantage predicted: P1 -> P2 on hit +11 (hit 200, attacker in 13 how 2, defender in 24) measured only");
        CHECK_EQ(0, host::fakes::osdCount());
        CHECK(!frameDataMeasured(measured(200, 11, false)));
        CHECK_LOG("predicted +11 measured +11 same");
    }

    // Hitstun on the ground: the defender's way out is not the stun's end.
    TEST(frame_data, no_advantage_for_hitstun_on_the_ground)
    {
        Exchange e;
        exchange(e, 100, false);
        frameDataFrame();
        CHECK_EQ(0, host::log::count("advantage predicted"));
        CHECK(!frameDataMeasured(measured(100, 3, false)));
        CHECK_LOG("predicted none measured +3 (hitstun on the ground)");
    }

    TEST(frame_data, no_advantage_when_the_attackers_move_is_not_certain)
    {
        Exchange e;
        exchange(e, 300, true);
        host::Script cond;
        cond.iff().frame(10).cmd(kAllowInterrupt, 0).endIf().frame(30).cmd(kAllowInterrupt, 0).end();
        e.attacker.threads.setThread(kUnitSubactionMain, cond.list(), cond.at(1), 6.0f);
        frameDataFrame();
        CHECK_EQ(0, host::fakes::osdCount());
        CHECK(!frameDataMeasured(measured(300, 3, true)));
        CHECK_LOG("predicted none measured +3 (attacker's move not certain)");
    }

    // ---- real scripts, from a memory dump of a running match --------------
    //
    // What each script holds was read by hand in a dump (the commands are
    // listed in the comments); the counts are worked out from that, and
    // where make test-frames played the move in the emulator its measured
    // frame is the same number.

    enum { kSubSpotDodge = 67, kSubRollF = 68, kSubRollB = 69, kSubJab = 72, kSubUpTilt = 84, kSubForwardSmash = 88 };

    static bool scriptOf(int fighterKind, const char* name, int subaction, int status, Live& v)
    {
        host::dump::FighterInfo f;
        if (!host::dump::fighterOfKind(fighterKind, f))
        {
            host::test::skip("the dump's match has no %s", name);
            return false;
        }
        const u32* sub = host::dump::subaction(f, subaction);
        if (sub == NULL)
        {
            host::test::fail(__FILE__, __LINE__, "%s's subaction table was not found in the dump (fighter at %08lx)", name,
                             f.fighter);
            return false;
        }
        v = liveAt(sub, status);
        v.actStart = host::dump::actionScript(status);
        return true;
    }

    // Frame 1: body intangible. Frame 15: body normal, speed 0.8. Frame 22:
    // Allow Interrupt. 15 frames to 15, then 15 + 9 x 0.8 = 22.2: frame 24
    // (measured for both fighters). The action script: Wait at the
    // animation's end on the ground.
    static void checkSpotDodge(int fighterKind, const char* name)
    {
        Live v;
        if (!scriptOf(fighterKind, name, kSubSpotDodge, game::kStatusEscape, v))
        {
            return;
        }
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.bodyCount == 1);
        checkWindow(rec.body[0], 1.0f, 15.0f, kFdIntangible);
        REQUIRE(rec.rateCount == 1);
        CHECK_EQ(15.0f, rec.rate[0].time);
        CHECK_EQ(0.8f, rec.rate[0].value);
        CHECK_EQ(0, rec.rate[0].multiply);
        CHECK_EQ(22.0f, rec.openTime);
        CHECK_EQ(0x80000000u, rec.openGroups);
        CHECK_EQ(kFdEndFree, rec.end[0]);
        CHECK_EQ(game::kStatusWait, rec.endStatus[0]);
        CHECK_EQ(kFdEndNone, rec.end[1]);
        CHECK_EQ(24, a.frames);
        CHECK_EQ(main::kActScript, a.how);
        CHECK_EQ(1, a.certain);

        // The body window counted ahead from the first frame, as the
        // emulator test saw it: 1 frame to its start, 15 to its end.
        bool byEnd = false;
        CHECK_EQ(1, ticksUntil(rec, v, rec.body[0].on, false, byEnd));
        CHECK_EQ(15, ticksUntil(rec, v, rec.body[0].off, false, byEnd));
        NOTE("%s: intangible 1 to 15, speed 0.8 from 15, open at 22: actionable in 24", name);
    }

    TEST_DUMP2(frame_data_dump, mario_spot_dodge)
    {
        checkSpotDodge(host::dump::kMario, "Mario");
    }

    TEST_DUMP2(frame_data_dump, fox_spot_dodge)
    {
        checkSpotDodge(host::dump::kFox, "Fox");
    }

    // Frame 3: body intangible. Frame 19: body normal. Frame 31: Allow
    // Interrupt. Measured: actionable on frame 31, intangible from 3 to 19.
    static void checkMarioRoll(int subaction, int status)
    {
        Live v;
        if (!scriptOf(host::dump::kMario, "Mario", subaction, status, v))
        {
            return;
        }
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.bodyCount == 1);
        checkWindow(rec.body[0], 3.0f, 19.0f, kFdIntangible);
        CHECK_EQ(0, rec.rateCount);
        CHECK_EQ(31.0f, rec.openTime);
        CHECK_EQ(kFdEndFree, rec.end[0]);
        CHECK_EQ(game::kStatusWait, rec.endStatus[0]);
        CHECK_EQ(31, a.frames);
        CHECK_EQ(main::kActScript, a.how);
        CHECK_EQ(1, a.certain);
        NOTE("intangible 3 to 19, open at 31: actionable in 31");
    }

    TEST_DUMP2(frame_data_dump, mario_roll_forward)
    {
        checkMarioRoll(kSubRollF, kStatusEscapeF);
    }

    TEST_DUMP2(frame_data_dump, mario_roll_back)
    {
        checkMarioRoll(kSubRollB, kStatusEscapeB);
    }

    // Fox's rolls: Frame 3: body intangible. Frame 19: body normal. No
    // Allow Interrupt: the action script's Wait at the animation's end on
    // the ground frees him (measured: "by end", frame 31). The animation's
    // length is not in the dump (it is not playing); 32 is given here.
    static void checkFoxRoll(int subaction, int status)
    {
        Live v;
        if (!scriptOf(host::dump::kFox, "Fox", subaction, status, v))
        {
            return;
        }
        v.length = 32;
        FrameRecord rec;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.bodyCount == 1);
        checkWindow(rec.body[0], 3.0f, 19.0f, kFdIntangible);
        CHECK_EQ(-1.0f, rec.openTime);
        CHECK_EQ(kFdEndFree, rec.end[0]);
        CHECK_EQ(game::kStatusWait, rec.endStatus[0]);
        CHECK_EQ(kFdEndNone, rec.end[1]);
        CHECK_EQ(31, a.frames);
        CHECK_EQ(main::kActEnd, a.how);
        CHECK_EQ(1, a.certain);
        NOTE("intangible 3 to 19, no Allow Interrupt: free at the animation's end");
    }

    TEST_DUMP2(frame_data_dump, fox_roll_forward)
    {
        checkFoxRoll(kSubRollF, kStatusEscapeF);
    }

    TEST_DUMP2(frame_data_dump, fox_roll_back)
    {
        checkFoxRoll(kSubRollB, kStatusEscapeB);
    }

    // Mario's attacks, the subaction script alone (their status scripts are
    // long and decide by variables). Measured in the emulator: the jab
    // actionable on frame 15, the up tilt on 29, and 37 frames of the
    // forward smash's own status.
    TEST_DUMP2(frame_data_dump, mario_jab_up_tilt_and_forward_smash)
    {
        Live v;
        FrameRecord rec;
        // Jab. Frame 1: three hitboxes. Wait 2: all removed. Frame 15: Allow
        // Interrupt.
        if (!scriptOf(host::dump::kMario, "Mario", kSubJab, game::kStatusAttack, v))
        {
            return;
        }
        v.actStart = NULL;
        ActIn a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.hitCount == 1);
        checkWindow(rec.hit[0], 1.0f, 3.0f, kFdHit);
        CHECK_EQ(15.0f, rec.openTime);
        CHECK_EQ(15, a.frames);

        // Up tilt. Frame 4: three hitboxes. Wait 7: removed. Frame 29:
        // Allow Interrupt.
        REQUIRE(scriptOf(host::dump::kMario, "Mario", kSubUpTilt, 0x28, v));
        v.actStart = NULL;
        a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.hitCount == 1);
        checkWindow(rec.hit[0], 4.0f, 11.0f, kFdHit);
        CHECK_EQ(29.0f, rec.openTime);
        CHECK_EQ(29, a.frames);

        // Forward smash. Frame 7: two hitboxes, then a third on either side
        // of an If / Else. Wait 5: removed. Frame 21: speed 1.22. Frame 40:
        // Allow Interrupt. 21 frames to 21, then 16 steps of 1.22 (15 reach
        // 39.3): frame 37.
        REQUIRE(scriptOf(host::dump::kMario, "Mario", kSubForwardSmash, 0x2C, v));
        v.actStart = NULL;
        a = ask(v, &rec);
        CHECK_EQ(kFdValid, rec.flags);
        REQUIRE(rec.hitCount == 1);
        checkWindow(rec.hit[0], 7.0f, 12.0f, kFdHit);
        REQUIRE(rec.rateCount == 1);
        CHECK_EQ(21.0f, rec.rate[0].time);
        CHECK_EQ(1.22f, rec.rate[0].value);
        CHECK_EQ(40.0f, rec.openTime);
        CHECK_EQ(37, a.frames);
        CHECK_EQ(1, a.certain);
        NOTE("jab 15, up tilt 29, forward smash 37");
    }

    // The mod's own chain from a fighter's animation-command module to the
    // script its subaction thread runs, on every fighter of the dump: it
    // must arrive at the script the fighter's subaction table lists for
    // what the fighter is doing.
    TEST_DUMP2(frame_data_dump, the_thread_chain_on_real_fighters)
    {
        host::dump::FighterInfo found[8];
        int count = host::dump::fighters(found, 8);
        REQUIRE(count > 0);
        for (int i = 0; i < count; i++)
        {
            const void* module = reinterpret_cast<const void*>(found[i].animCmd);
            const void* threads = reinterpret_cast<const void*>(game::wordAt(module, kThreads));
            REQUIRE(game::isRam(threads));
            const void* sub = interpreter(threads, kUnitSubactionMain);
            REQUIRE(sub != NULL);
            CHECK(interpreter(threads, kUnitActionMain) != NULL);
            CHECK(interpreter(threads, kUnitLoop) != NULL);
            CHECK(found[i].subactions != 0);
            CHECK(scriptAt(sub, kInterpStart) == host::dump::subaction(found[i], found[i].subaction));
            // Whatever it is doing, the walk ends and gives a valid record.
            Live v = liveAt(scriptAt(sub, kInterpStart), found[i].status);
            v.actStart = scriptAt(interpreter(threads, kUnitActionMain), kInterpStart);
            v.loopStart = scriptAt(interpreter(threads, kUnitLoop), kInterpStart);
            FrameRecord rec;
            build(rec, v);
            CHECK((rec.flags & kFdValid) != 0);
            CHECK(rec.commands > 0);
        }
        NOTE("%d fighters", count);
    }
}
}
