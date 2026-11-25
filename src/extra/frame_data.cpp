#include "extra/frame_data.h"

#include <ft/fighter.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "main/cpu/shield_timing_logic.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/settings.h"
#include "main/ui/osd.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// The frame data service (frame_data.h): walks a fighter's scripts once per
// status or script change and counts ahead to its first actionable frame;
// works out a hit's frame advantage in the frame it lands (shown at once only
// for a grounded attacker's hit on shield). Reads the game, changes nothing.
// RSBE01 rev 2, P+ 3.2.1. READ: the game's code in a memory dump; MEASURED:
// scripts/frame_tests.py (Mario, Fox, Bowser, Jigglypuff) against the
// trackers' live reading on every frame of every move it plays; ASSUMED: not
// checked. What matters was learned from ProjectMCodes' SuperTraining (thread
// list, Allow Interrupt, Body Collision, throws); written from the game's code.
//
// READ:
//  - soAnimCmdModuleImpl (vtable 0x80AE70D8) +0x20: 11 control units of 16
//    bytes from +0x1C (interpreter, address packs, index, type mask;
//    soAnimCmdControlUnitKind): 0 ActionMain 0x01, 1 SubactionMain 0x02, 2-3
//    graphics and sound 0x40, 4 SubactionOther 0x02, 5-8 a second animation,
//    9 concurrent loop 0x04, 10 hidden action 0x10. acCmdInterpreter (main
//    program 0x80139748, as the Brawl decompilation's class): current command
//    +0x0C, first +0x10, stack +0x20, clock +0x24.
//  - Commands: 8 bytes (group, type, argument count, option, argument
//    pointer); arguments 8 bytes, kind (0 integer, 1 scalar x 60000, 2
//    pointer, 3 boolean, 5 variable, 6 requirement) and value. Group 0: the
//    interpreter's (25 types, table 0x80409520).
//  - Timers (acCmdInterpreter::interpretCmd): "Wait until frame" (type 2)
//    holds until the clock reaches its argument; "Wait" (type 1) for its
//    argument less the last overshoot: chained timers are clock thresholds.
//  - A frame: the status's code, soMotionModuleImpl::updateFrame (0x8071EB48);
//    then, from 0x80710084, the subaction threads (mask 0x02) once at slow
//    rate x animation rate (0x80726944; not in hitlag), the status module
//    (0x80780098) applying queued changes or running the action threads (mask
//    0x1D, step 1; only the hidden one in hitlag),
//    soStatusModuleImpl::checkTransition (0x8077F0C4). changeStatusRequest
//    (0x8077F5A8) applies at once in scheduler phase 6+, queued in 2-5,
//    running the new action script at step 0 and the terms again
//    (0x8077FE50, 0x8077FE70). So an open reached in a
//    frame serves that frame's input (MEASURED: spot dodges and rolls of the
//    four fighters; a jab a frame earlier does not come out).
//  - Group 4 type 7 is soMotionModule::setRate (0x8072464C; two arguments:
//    P+ multiplies, hook 0x80724820), one rate for animation and clock.
//    soAnimChr::isEnd (0x8072A00C): not looping, frame >= count - 1.
//  - Allow Interrupt (0x64/0) goes to ftCancelModuleImpl (0x8084B834): its
//    table's groups on for the situation, the other's off (0x8084B6D8),
//    unless LA-Bit 11, 24, 43 or 45 is set or it runs before the
//    fix-position phase. 2/0xA enables one group (game/tracker.cpp); both
//    end in soStatusModule::enableTransitionTermGroup. Attacks, dodges,
//    rolls: Allow Interrupt or "change status at animation end" (2/0
//    or 2/1, requirement 1, plus 3 ground / 4 air); landing, shield release,
//    hitstun: the status's loop (StatusRule below).
//  - Body: 6/5 is soCollisionHitModule::setWhole (0 normal, 1 invincible, 2
//    intangible; 0x807504C4), reset by a status change (0x80750EDC);
//    per-bone (6/8) noted, not tracked. MEASURED: spot dodge and both rolls.
//  - Hitboxes (table 0x80AE2520; meanings ASSUMED from the usual names): 6/0
//    and 0x15 offensive (id in argument 0's low byte), 0x2B/0x2C thrown, 3
//    removes an id, 4 all, 0xA/0xD grab boxes on/off, 0xF throw release.
//  - Hitlag: soStopModuleImpl::update (0x80772480) counts down before isStop
//    is asked: n after a frame freezes the next n - 1 (MEASURED: the count on
//    the hit's frame names the attacker's first actionable frame).
// MEASURED as a whole: jab, up tilt, forward smash, grab, the five aerials
// (in the air, landing, L-cancelled), spot dodge, both rolls, air dodge
// landing, plain landing, shield release. ASSUMED still: the hitbox, grab and
// throw meanings (no consumer, no test); Allow Interrupt's blocking flags
// clear in normal play; slow motion (its rate is not counted).
// Long form: docs/internals-extra.md, "Frame data" and "Frame data: statuses
// that decide for themselves".
namespace tm {
namespace extra {
    enum {
        // soAnimCmdModuleImpl and its units
        kThreads = 0x20,
        kUnits = 0x1C,
        kUnitSize = 0x10,
        kUnitIndex = 0x8,
        kUnitActionMain = 0,
        kUnitSubactionMain = 1,
        kUnitLoop = 9,
        // acCmdInterpreter
        kInterpCurrent = 0xC,
        kInterpStart = 0x10,
        kInterpClock = 0x24,
        // soMotionModuleImpl: the main animation's flags (soAnimChr +0x18)
        kMotionFlags = 0x54,
        kMotionLoop = 0x80,

        kMaxDepth = 4,
        kMaxLoops = 4,
        kMaxAlts = 8,
        kMaxTicks = 600,
        kShownLimit = 50, // as the advantage tracker's
        kGiveUp = 200,    // past the tracker's own 180 frames

        kStatusFallSpecial = 0x10,
        kLandingFrame = 3074,       // ftValueAccesser customize parameters (float)
        kLandingLightFrame = 3073,
        kNoLandingInterrupt = 0x12000003, // LA-Bit[3]
        kGuardOffTimer = 0x20000005,      // RA-Basic[5]

        kGroundActionGroups = (1 << game::kGroupGroundSpecial) | (1 << game::kGroupGroundCatch)
                              | (1 << game::kGroupGroundAttack) | (1 << game::kGroupGroundEscape)
                              | (1 << game::kGroupGroundGuard) | (1 << game::kGroupGroundJump),
        kAirActionGroups = (1 << game::kGroupAirSpecial) | (1 << game::kGroupAirEscape)
                           | (1 << game::kGroupAirAttack) | (1 << game::kGroupAirJumpAerial)
    };
    static const u32 kOpenAll = 0x80000000;
    static const float kNoTime = -1.0f;

    // Statuses whose own code decides, on top of the walk (read in code;
    // unchecked here):
    //  - Landing, LandingLight: exec sets a flag once the frame, before its
    //    advance, reaches the landing frame parameter (0x808746B8; not with
    //    LA-Bit[3] set or -1); the loop then opens every ground group.
    //  - LandingAttackAir: P+ made its loop's condition never true.
    //  - GuardOff: the loop counts RA-Basic[5] down from 15, then allows
    //    interrupts (script 0x80FB0EBC).
    enum StatusRule { kRuleNone = 0, kRuleLanding, kRuleEndOnly, kRuleGuardOff };

    // The walk's place in a script.
    struct State {
        float t;      // script time at which the commands being read run
        u32 hitMask;  // offensive collision ids out
        bool hitActive; // offensive hitbox wave open (tracks past kFdMaxHit)
        s8 hitOpen;   // open window indices, -1 if none
        s8 catchOpen;
        s8 bodyOpen;
        u8 bodyState;
        // The Change Status command being read, with its added requirements.
        s16 termStatus; // -1: none
        bool termEnd;
        bool termOther;
        bool termGround;
        bool termAir;
    };

    struct Budget {
        u16 used;
        u16 limit;
    };

    struct Alt {
        const u32* start; // NULL: no branch runs
        const u32* end;
    };

    // What one alternative of a condition says about the animation's end.
    struct AltEnd {
        u8 end[2];
        s16 endStatus[2];
        u16 flags;
    };

    // What a fighter's modules say now.
    struct Live {
        const u32* subStart;
        const u32* subCurrent;
        const u32* actStart;
        const u32* loopStart;
        float clock;
        float frame;
        float rate;
        int length;
        int kind;
        int status;
        int hitlag;
        int hitstun;
        int shieldStun;
        u32 groups;
        bool looped;
        bool air;
        const soModuleEnumeration* modules;
    };

    struct Pending {
        bool have;
        bool shown;
        u8 shield;
        s8 attacker;
        s16 value;
        s32 hitFrame;
        const char* why; // why nothing was predicted
    };

    struct Slot {
        Fighter* fighter;
        FrameRecord rec;
        Pending pending; // the hit this fighter took last
    };

    struct ShieldHoldSlot {
        s8 attackerEntry;
        bool attackerSub;
        s16 attackerStatus;
        u8 holdFrames;
    };

    static Slot s_slots[game::kMaxTracked];
    static ShieldHoldSlot s_shieldHold[game::kMaxFighters];
    static u32 s_walks = 0;
    u8 g_fdTrace = 0;

    // ---- reading commands -------------------------------------------------

    static bool readCmd(const u32* pc, u32& head, const u32*& args)
    {
        if (!game::isRam(pc) || !game::isRam(pc + 1) || (reinterpret_cast<u32>(pc) & 3) != 0)
        {
            return false;
        }
        head = pc[0];
        args = reinterpret_cast<const u32*>(pc[1]);
        return true;
    }

    static bool readArg(const u32* args, u32 argc, u32 i, u32& kind, s32& value)
    {
        if (i >= argc || !game::isRam(args + 2 * i) || !game::isRam(args + 2 * i + 1))
        {
            return false;
        }
        kind = args[2 * i];
        value = static_cast<s32>(args[2 * i + 1]);
        return true;
    }

    static float scalar(s32 value)
    {
        return static_cast<float>(value) / 60000.0f;
    }

    static const u32* pointerArg(const u32* args, u32 argc)
    {
        u32 kind;
        s32 value;
        if (!readArg(args, argc, 0, kind, value) || kind != 2)
        {
            return NULL;
        }
        const u32* p = reinterpret_cast<const u32*>(value);
        return game::isRam(p) && game::isRam(p + 1) ? p : NULL;
    }

    static bool actionGroup(const u32* args, u32 argc, u32& bit)
    {
        u32 kind;
        s32 value;
        if (!readArg(args, argc, 0, kind, value) || kind != 0 || value <= 0 || value >= 32)
        {
            return false;
        }
        bit = 1u << value;
        return (bit & (kGroundActionGroups | kAirActionGroups)) != 0;
    }

    // ---- what the animation's end leads to --------------------------------

    static bool freeStatus(int s)
    {
        return s == game::kStatusWait || s == game::kStatusFall || s == game::kStatusFallAerial
               || s == game::kStatusSquat || s == game::kStatusSquatWait || s == game::kStatusGuard;
    }

    static void applyEnd(FrameRecord& rec, int sit, int kind, int status, bool cond)
    {
        u16 flag = sit == 0 ? kFdEndCondGround : kFdEndCondAir;
        if (cond)
        {
            rec.flags |= flag;
        }
        if (rec.end[sit] == kFdEndNone)
        {
            rec.end[sit] = static_cast<u8>(kind);
            rec.endStatus[sit] = static_cast<s16>(status);
        }
        else if (rec.endStatus[sit] != status)
        {
            rec.flags |= flag;
            if (kind == kFdEndOther)
            {
                rec.end[sit] = kFdEndOther;
            }
        }
    }

    static void flushTerm(State& st, FrameRecord& rec)
    {
        if (st.termStatus < 0)
        {
            return;
        }
        if (st.termEnd && !st.termOther)
        {
            int kind = freeStatus(st.termStatus) ? kFdEndFree : kFdEndOther;
            if (!(st.termAir && !st.termGround))
            {
                applyEnd(rec, 0, kind, st.termStatus, false);
            }
            if (!(st.termGround && !st.termAir))
            {
                applyEnd(rec, 1, kind, st.termStatus, false);
            }
        }
        st.termStatus = -1;
    }

    // Requirements: 1 animation end, 3 on the ground, 4 in the air; any
    // other, or a negated one, makes the term somebody's choice, not the
    // move's end.
    static void addReq(State& st, const u32* args, u32 argc, u32 i)
    {
        u32 kind;
        s32 value;
        if (!readArg(args, argc, i, kind, value) || kind != 6 || value < 0)
        {
            st.termOther = true;
            return;
        }
        switch (value)
        {
        case 0x1:
            st.termEnd = true;
            break;
        case 0x3:
            st.termGround = true;
            break;
        case 0x4:
            st.termAir = true;
            break;
        default:
            st.termOther = true;
            break;
        }
    }

    // Group 2 type 0 (term id, status, requirement...), type 1 (status,
    // requirement...) and type 4 (one more requirement).
    static void termCommand(u32 type, const u32* args, u32 argc, State& st)
    {
        if (type == 4)
        {
            if (st.termStatus >= 0)
            {
                addReq(st, args, argc, 0);
            }
            return;
        }
        u32 kind;
        s32 value;
        u32 at = type == 0 ? 1 : 0;
        st.termStatus = readArg(args, argc, at, kind, value) && kind == 0 && value >= 0 && value < 0x7FFF
                            ? static_cast<s16>(value)
                            : 0x7FFF;
        st.termEnd = st.termOther = st.termGround = st.termAir = false;
        addReq(st, args, argc, at + 1);
    }

    // Folds what the alternatives of a condition found into the record: the
    // same end in every one counts as unconditional.
    static void keepEnd(AltEnd& out, const FrameRecord& rec)
    {
        out.end[0] = rec.end[0];
        out.end[1] = rec.end[1];
        out.endStatus[0] = rec.endStatus[0];
        out.endStatus[1] = rec.endStatus[1];
        out.flags = rec.flags;
    }

    static void mergeEnds(FrameRecord& rec, const AltEnd* alts, int n)
    {
        for (int sit = 0; sit < 2; sit++)
        {
            bool same = true;
            for (int i = 1; i < n; i++)
            {
                same = same && alts[i].end[sit] == alts[0].end[sit] && alts[i].endStatus[sit] == alts[0].endStatus[sit];
            }
            for (int i = 0; i < n; i++)
            {
                if (alts[i].end[sit] != kFdEndNone && (i == 0 || !same))
                {
                    applyEnd(rec, sit, alts[i].end[sit], alts[i].endStatus[sit], !same);
                }
            }
        }
        for (int i = 0; i < n; i++)
        {
            rec.flags |= alts[i].flags & (kFdEndCondGround | kFdEndCondAir);
        }
    }

    // ---- windows ----------------------------------------------------------

    static void hitOn(State& st, FrameRecord& rec, u8 kind)
    {
        if (kind == kFdHit && !st.hitActive)
        {
            st.hitActive = true;
            if (rec.hitWaves < 255)
            {
                rec.hitWaves++;
            }
            rec.lastHitOn = st.t;
            rec.lastHitOff = kNoTime;
        }
        s8& open = kind == kFdHit ? st.hitOpen : st.catchOpen;
        if (open >= 0)
        {
            return;
        }
        // A window that closed at this very time goes on.
        for (int i = rec.hitCount - 1; i >= 0; i--)
        {
            if (rec.hit[i].kind == kind)
            {
                if (rec.hit[i].off == st.t)
                {
                    rec.hit[i].off = kNoTime;
                    open = static_cast<s8>(i);
                    return;
                }
                break;
            }
        }
        if (rec.hitCount >= kFdMaxHit)
        {
            rec.flags |= kFdOverflow;
            return;
        }
        FdWindow& w = rec.hit[rec.hitCount];
        w.on = st.t;
        w.off = kNoTime;
        w.kind = kind;
        open = static_cast<s8>(rec.hitCount++);
    }

    static void hitOff(State& st, FrameRecord& rec, u8 kind)
    {
        s8& open = kind == kFdHit ? st.hitOpen : st.catchOpen;
        if (open >= 0)
        {
            rec.hit[open].off = st.t;
        }
        open = -1;
        if (kind == kFdHit)
        {
            if (st.hitActive)
            {
                st.hitActive = false;
                rec.lastHitOff = st.t;
            }
            st.hitMask = 0;
        }
    }

    static void body(State& st, FrameRecord& rec, s32 state)
    {
        if (st.bodyOpen >= 0 && state != st.bodyState)
        {
            rec.body[st.bodyOpen].off = st.t;
            st.bodyOpen = -1;
        }
        st.bodyState = static_cast<u8>(state);
        if (state != 0 && st.bodyOpen < 0)
        {
            if (rec.bodyCount >= kFdMaxBody)
            {
                rec.flags |= kFdOverflow;
                return;
            }
            FdWindow& w = rec.body[rec.bodyCount];
            w.on = st.t;
            w.off = kNoTime;
            w.kind = static_cast<u8>(state);
            st.bodyOpen = static_cast<s8>(rec.bodyCount++);
        }
    }

    // A command of the subaction script that is not the interpreter's own.
    // False stops the walk.
    static bool command(u32 head, const u32* args, State& st, FrameRecord& rec)
    {
        u32 group = head >> 24;
        u32 type = (head >> 16) & 0xFF;
        u32 argc = (head >> 8) & 0xFF;
        if (group == 2 && (type == 0 || type == 1 || type == 4))
        {
            if (type != 4)
            {
                flushTerm(st, rec);
            }
            termCommand(type, args, argc, st);
            return true;
        }
        flushTerm(st, rec);
        u32 kind = 0;
        s32 value = 0;
        bool have = readArg(args, argc, 0, kind, value);
        if (group == 0x64)
        {
            if (type == 0)
            {
                if (rec.openTime < 0.0f)
                {
                    rec.openTime = st.t;
                    rec.openGroups = kOpenAll;
                }
            }
            else if (type == 1 && rec.openTime >= 0.0f)
            {
                rec.flags |= kFdReclose;
            }
        }
        else if (group == 2)
        {
            u32 bit;
            if (type == 0xA && actionGroup(args, argc, bit))
            {
                if (rec.openTime < 0.0f)
                {
                    rec.openTime = st.t;
                    rec.openGroups = bit;
                }
                else if (rec.openTime == st.t)
                {
                    rec.openGroups |= bit;
                }
            }
        }
        else if (group == 4)
        {
            if (type == 7)
            {
                if (!have || kind != 1)
                {
                    rec.flags |= kFdRateVar;
                }
                else if (rec.rateCount >= kFdMaxRate)
                {
                    rec.flags |= kFdOverflow | kFdRateVar;
                }
                else
                {
                    FdRate& r = rec.rate[rec.rateCount++];
                    r.time = st.t;
                    r.value = scalar(value);
                    r.multiply = argc == 2 ? 1 : 0;
                }
            }
            else if (type == 6 || type == 0x14)
            {
                rec.flags |= kFdFrameSet;
            }
            else if (type <= 3)
            {
                rec.flags |= kFdFlow;
                return false;
            }
        }
        else if (group == 6)
        {
            s32 v = have && kind == 0 ? value : 0;
            switch (type)
            {
            case 0x5:
                body(st, rec, v);
                break;
            case 0x0:
            case 0x15:
            case 0x2B:
            case 0x2C:
                st.hitMask |= 1u << (v & 31);
                hitOn(st, rec, kFdHit);
                break;
            case 0x3:
                st.hitMask &= ~(1u << (v & 31));
                if (st.hitMask == 0)
                {
                    hitOff(st, rec, kFdHit);
                }
                break;
            case 0x4:
                hitOff(st, rec, kFdHit);
                break;
            case 0xA:
                hitOn(st, rec, kFdCatch);
                break;
            case 0xD:
                hitOff(st, rec, kFdCatch);
                break;
            case 0xF:
                if (rec.throwTime < 0.0f)
                {
                    rec.throwTime = st.t;
                }
                break;
            case 0x8:
                rec.flags |= kFdPartialBody;
                break;
            default:
                break;
            }
        }
        return true;
    }

    // ---- structure --------------------------------------------------------

    static void clearRecord(FrameRecord& rec)
    {
        memset(&rec, 0, sizeof(rec));
        rec.openTime = kNoTime;
        rec.throwTime = kNoTime;
        rec.lastHitOn = kNoTime;
        rec.lastHitOff = kNoTime;
        rec.endStatus[0] = rec.endStatus[1] = -1;
    }

    static void clearState(State& st)
    {
        st.t = 0.0f;
        st.hitMask = 0;
        st.hitActive = false;
        st.hitOpen = st.catchOpen = st.bodyOpen = -1;
        st.bodyState = 0;
        st.termStatus = -1;
        st.termEnd = st.termOther = st.termGround = st.termAir = false;
    }

    // Past the And / Or terms that follow an If or an Else If.
    static const u32* skipTerms(const u32* pc)
    {
        u32 head;
        const u32* args;
        while (readCmd(pc, head, args) && (head >> 16 == 0x000B || head >> 16 == 0x000C))
        {
            pc += 2;
        }
        return pc;
    }

    // The alternatives of the If (or Switch) at `pc`, one of them "no branch
    // runs" unless there is an Else (a Default), and the command after its
    // End If (End Switch). False if the structure cannot be read.
    static bool branches(const u32* pc, Budget& b, bool isSwitch, Alt* alts, int& count, const u32*& after)
    {
        int nest = 0;
        bool hasElse = false;
        const u32* q = pc + 2;
        const u32* start = NULL;
        count = 0;
        if (!isSwitch)
        {
            q = skipTerms(q);
            start = q;
        }
        for (;;)
        {
            u32 head;
            const u32* args;
            b.used++;
            if (!readCmd(q, head, args) || b.used > b.limit || count >= kMaxAlts - 1)
            {
                return false;
            }
            u32 key = head >> 16;
            if (key == 0x0000)
            {
                return false;
            }
            if (!isSwitch)
            {
                if (key == 0x000A)
                {
                    nest++;
                }
                else if (key == 0x000F)
                {
                    if (nest == 0)
                    {
                        alts[count].start = start;
                        alts[count++].end = q;
                        break;
                    }
                    nest--;
                }
                else if ((key == 0x000D || key == 0x000E) && nest == 0)
                {
                    alts[count].start = start;
                    alts[count++].end = q;
                    if (key == 0x000E)
                    {
                        hasElse = true;
                        start = q + 2;
                    }
                    else
                    {
                        start = skipTerms(q + 2);
                        q = start - 2;
                    }
                }
            }
            else
            {
                if (key == 0x0010)
                {
                    nest++;
                }
                else if (key == 0x0013)
                {
                    if (nest == 0)
                    {
                        if (start != NULL)
                        {
                            alts[count].start = start;
                            alts[count++].end = q;
                        }
                        break;
                    }
                    nest--;
                }
                else if ((key == 0x0011 || key == 0x0012) && nest == 0)
                {
                    if (start != NULL && start != q)
                    {
                        alts[count].start = start;
                        alts[count++].end = q;
                    }
                    if (key == 0x0012)
                    {
                        hasElse = true;
                    }
                    start = q + 2;
                }
            }
            q += 2;
        }
        if (!hasElse)
        {
            alts[count].start = NULL;
            alts[count++].end = NULL;
        }
        after = q + 2;
        return true;
    }

    static bool walk(const u32* pc, const u32* end, int depth, Budget& b, State& st, FrameRecord& rec);

    // A condition: every alternative is walked on a copy, none evaluated.
    // All ending at the same clock time with nothing uncertain (hitbox
    // variants of one move): the first is the record. Else the record is
    // uncertain and the walk goes on from the latest time, so a reported
    // frame is never early. Returns the command to go on from, NULL to stop.
    static const u32* conditional(const u32* pc, int depth, Budget& b, State& st, FrameRecord& rec, bool isSwitch)
    {
        Alt alts[kMaxAlts];
        int count = 0;
        const u32* after = NULL;
        if (!branches(pc, b, isSwitch, alts, count, after))
        {
            rec.flags |= kFdLoop;
            return NULL;
        }
        if (depth >= kMaxDepth)
        {
            rec.flags |= kFdCall | kFdCond;
            return after;
        }
        AltEnd found[kMaxAlts];
        FrameRecord r2;
        State first = st;
        int firstHits = 0, firstBodies = 0;
        bool firstThrow = false;
        bool same = true;
        bool windows = true;
        float latest = st.t;
        u8 maxBranchWaves = 0;
        float maxBranchOn = kNoTime;
        float maxBranchOff = kNoTime;
        bool branchOpenEnd = false;
        for (int i = 0; i < count; i++)
        {
            State s2 = st;
            clearRecord(r2);
            bool ended = true;
            if (alts[i].start != NULL)
            {
                ended = walk(alts[i].start, alts[i].end, depth + 1, b, s2, r2);
            }
            keepEnd(found[i], r2);
            if (r2.hitWaves > maxBranchWaves)
            {
                maxBranchWaves = r2.hitWaves;
            }
            if (r2.lastHitOn > maxBranchOn)
            {
                maxBranchOn = r2.lastHitOn;
            }
            if (r2.hitWaves > 0)
            {
                if (r2.lastHitOff < 0.0f)
                {
                    branchOpenEnd = true;
                }
                else if (r2.lastHitOff > maxBranchOff)
                {
                    maxBranchOff = r2.lastHitOff;
                }
            }
            bool critical = !ended || r2.openTime >= 0.0f || r2.rateCount > 0
                            || (r2.flags & (kFdUncertain | kFdOverflow)) != 0;
            if (i == 0)
            {
                first = s2;
                firstHits = r2.hitCount;
                firstBodies = r2.bodyCount;
                firstThrow = r2.throwTime >= 0.0f;
            }
            if (critical || s2.t != first.t)
            {
                same = false;
            }
            if (s2.t != first.t || s2.hitMask != first.hitMask || s2.bodyState != first.bodyState
                || r2.hitCount != firstHits || r2.bodyCount != firstBodies || (r2.throwTime >= 0.0f) != firstThrow)
            {
                windows = false;
            }
            if (s2.t > latest)
            {
                latest = s2.t;
            }
        }
        if (same)
        {
            if (!windows)
            {
                rec.flags |= kFdWindows;
            }
            if (alts[0].start != NULL)
            {
                // The first alternative again, into the record; what it says
                // about the animation's end is merged below instead.
                u8 end0 = rec.end[0], end1 = rec.end[1];
                s16 status0 = rec.endStatus[0], status1 = rec.endStatus[1];
                u16 endFlags = rec.flags & (kFdEndCondGround | kFdEndCondAir);
                if (!walk(alts[0].start, alts[0].end, depth + 1, b, st, rec))
                {
                    return NULL;
                }
                rec.end[0] = end0;
                rec.end[1] = end1;
                rec.endStatus[0] = status0;
                rec.endStatus[1] = status1;
                rec.flags = static_cast<u16>((rec.flags & ~(kFdEndCondGround | kFdEndCondAir)) | endFlags);
            }
        }
        else
        {
            rec.flags |= kFdCond;
            if (maxBranchWaves > 0)
            {
                int sum = static_cast<int>(rec.hitWaves) + static_cast<int>(maxBranchWaves);
                rec.hitWaves = static_cast<u8>(sum > 255 ? 255 : sum);
                if (maxBranchOn > rec.lastHitOn)
                {
                    rec.lastHitOn = maxBranchOn;
                }
                if (branchOpenEnd)
                {
                    rec.lastHitOff = kNoTime;
                }
                else if (maxBranchOff > rec.lastHitOff)
                {
                    rec.lastHitOff = maxBranchOff;
                }
            }
            st.t = latest;
        }
        mergeEnds(rec, found, count);
        return after;
    }

    // Walks a command list of the subaction script from `pc` to `end`, to
    // the list's End or to a Return (true), or stops (false).
    static bool walk(const u32* pc, const u32* end, int depth, Budget& b, State& st, FrameRecord& rec)
    {
        const u32* loopStart[kMaxLoops];
        s32 loopLeft[kMaxLoops];
        int loops = 0;
        for (;;)
        {
            if (end != NULL && pc == end)
            {
                flushTerm(st, rec);
                return true;
            }
            u32 head;
            const u32* args;
            b.used++;
            if (!readCmd(pc, head, args) || b.used > b.limit)
            {
                rec.flags |= kFdLoop;
                return false;
            }
            u32 type = (head >> 16) & 0xFF;
            u32 argc = (head >> 8) & 0xFF;
            if (head >> 24 != 0)
            {
                if (!command(head, args, st, rec))
                {
                    return false;
                }
                pc += 2;
                continue;
            }
            flushTerm(st, rec);
            u32 kind = 0;
            s32 value = 0;
            bool have = readArg(args, argc, 0, kind, value);
            switch (type)
            {
            case 0x0: // End
            case 0x8: // Return
                return true;
            case 0x1: // Wait
                if (have && kind == 1)
                {
                    st.t = st.t + scalar(value);
                }
                break;
            case 0x2: // Wait until frame
                if (have && kind == 1 && scalar(value) > st.t)
                {
                    st.t = scalar(value);
                }
                break;
            case 0x3: // the clock is reset
            case 0x6: // Break
                rec.flags |= kFdFlow;
                return false;
            case 0x4: // Loop: the body runs max(count, 1) times, forever below 0
                if (have && kind == 0)
                {
                    if (value < 0 || loops >= kMaxLoops)
                    {
                        rec.flags |= kFdLoop;
                        return false;
                    }
                    loopStart[loops] = pc;
                    loopLeft[loops++] = value;
                }
                break;
            case 0x5: // the loop's end
                if (loops > 0)
                {
                    if (--loopLeft[loops - 1] > 0)
                    {
                        pc = loopStart[loops - 1];
                    }
                    else
                    {
                        loops--;
                    }
                }
                break;
            case 0x7: // Subroutine
            case 0x9: // Goto
            {
                const u32* target = pointerArg(args, argc);
                if (target == NULL || (type == 0x7 && depth >= kMaxDepth))
                {
                    rec.flags |= kFdCall;
                }
                else if (type == 0x7)
                {
                    if (!walk(target, NULL, depth + 1, b, st, rec))
                    {
                        return false;
                    }
                }
                else
                {
                    pc = target;
                    continue;
                }
                break;
            }
            case 0xA: // If
            case 0x10: // Switch
                pc = conditional(pc, depth, b, st, rec, type == 0x10);
                if (pc == NULL)
                {
                    return false;
                }
                continue;
            case 0x14: // While and its parts
            case 0x15:
            case 0x16:
            case 0x17:
                rec.flags |= kFdLoop;
                return false;
            case 0x18: // out of a Switch
                if (end != NULL)
                {
                    return true;
                }
                break;
            default:
                break;
            }
            pc += 2;
        }
    }

    // A pass over one of the status's own scripts (the action script, or
    // with `loopList` its concurrent loop): what the animation's end leads
    // to, and whether the script opens interrupts at once (kFdStatusOpen) or
    // under a condition, after a timer or in the loop (kFdStatusDecides).
    // Subroutines and gotos are followed; loops are passed through once.
    static void scan(const u32* pc, const u32* end, int depth, Budget& b, State& st, FrameRecord& rec, bool cond,
                     bool loopList, bool& timed)
    {
        for (;;)
        {
            u32 head;
            const u32* args;
            if (end != NULL && pc == end)
            {
                break;
            }
            b.used++;
            if (!readCmd(pc, head, args) || b.used > b.limit)
            {
                break;
            }
            u32 group = head >> 24;
            u32 type = (head >> 16) & 0xFF;
            u32 argc = (head >> 8) & 0xFF;
            if (group == 2 && (type == 0 || type == 1 || type == 4))
            {
                if (type != 4)
                {
                    flushTerm(st, rec);
                }
                termCommand(type, args, argc, st);
                pc += 2;
                continue;
            }
            flushTerm(st, rec);
            u32 bit;
            if (group == 0)
            {
                if (type == 0x0 || type == 0x8)
                {
                    break;
                }
                else if (type == 0x1 || type == 0x2)
                {
                    if (argc > 0)
                    {
                        timed = true;
                    }
                }
                else if (type == 0x7 || type == 0x9)
                {
                    const u32* target = pointerArg(args, argc);
                    if (target != NULL && depth < kMaxDepth)
                    {
                        if (type == 0x7)
                        {
                            scan(target, NULL, depth + 1, b, st, rec, cond, loopList, timed);
                        }
                        else
                        {
                            pc = target;
                            continue;
                        }
                    }
                    else if (type == 0x9)
                    {
                        break;
                    }
                }
                else if (type == 0xA || type == 0x10)
                {
                    Alt alts[kMaxAlts];
                    int count = 0;
                    const u32* after = NULL;
                    if (!branches(pc, b, type == 0x10, alts, count, after))
                    {
                        break;
                    }
                    if (depth < kMaxDepth)
                    {
                        AltEnd found[kMaxAlts];
                        FrameRecord r2;
                        for (int i = 0; i < count; i++)
                        {
                            State s2 = st;
                            clearRecord(r2);
                            if (alts[i].start != NULL)
                            {
                                scan(alts[i].start, alts[i].end, depth + 1, b, s2, r2, true, loopList, timed);
                            }
                            keepEnd(found[i], r2);
                            rec.flags |= r2.flags & kFdStatusDecides;
                        }
                        mergeEnds(rec, found, count);
                    }
                    pc = after;
                    continue;
                }
            }
            else if ((group == 0x64 && type == 0) || (group == 2 && type == 0xA && actionGroup(args, argc, bit)))
            {
                rec.flags |= (loopList || cond || timed) ? kFdStatusDecides : kFdStatusOpen;
            }
            pc += 2;
        }
        flushTerm(st, rec);
    }

    static u8 statusRule(int status)
    {
        switch (status)
        {
        case game::kStatusLanding:
        case game::kStatusLandingLight:
            return kRuleLanding;
        case game::kStatusLandingAttackAir:
            return kRuleEndOnly;
        case game::kStatusGuardOff:
            return kRuleGuardOff;
        default:
            return kRuleNone;
        }
    }

    // The record of what the fighter is doing now.
    static void build(FrameRecord& rec, const Live& v)
    {
        clearRecord(rec);
        rec.subStart = v.subStart;
        rec.actStart = v.actStart;
        rec.loopStart = v.loopStart;
        rec.status = static_cast<s16>(v.status);
        rec.motionKind = static_cast<s16>(v.kind);
        State st;
        Budget b = {0, kFdMaxActCommands};
        bool timed = false;
        clearState(st);
        if (v.actStart != NULL)
        {
            scan(v.actStart, NULL, 0, b, st, rec, false, false, timed);
        }
        if (v.loopStart != NULL)
        {
            timed = false;
            clearState(st);
            scan(v.loopStart, NULL, 0, b, st, rec, false, true, timed);
        }
        rec.commands = b.used;
        b.used = 0;
        b.limit = kFdMaxSubCommands;
        clearState(st);
        if (v.subStart != NULL)
        {
            walk(v.subStart, NULL, 0, b, st, rec);
            flushTerm(st, rec);
        }
        rec.commands = static_cast<u16>(rec.commands + b.used);
        rec.rule = statusRule(v.status);
        if (rec.rule != kRuleNone)
        {
            rec.flags &= ~kFdStatusDecides;
        }
        rec.flags |= kFdValid;
        s_walks++;
        if (g_fdTrace)
        {
            TM_LOG("fd walk %u st=%x sub=%d open=%d groups=%08x end=%d/%x,%d/%x rule=%d flags=%04x hit=%d body=%d "
                   "rate=%d throw=%d cmds=%d\n",
                   s_walks, v.status, v.kind, static_cast<int>(rec.openTime * 100.0f), rec.openGroups, rec.end[0],
                   rec.endStatus[0], rec.end[1], rec.endStatus[1], rec.rule, rec.flags, rec.hitCount, rec.bodyCount,
                   rec.rateCount, static_cast<int>(rec.throwTime * 100.0f), rec.commands);
            for (int i = 0; i < rec.hitCount + rec.bodyCount; i++)
            {
                const FdWindow& w = i < rec.hitCount ? rec.hit[i] : rec.body[i - rec.hitCount];
                TM_LOG("fd window %u %s kind=%d on=%d off=%d\n", s_walks, i < rec.hitCount ? "hit" : "body", w.kind,
                       static_cast<int>(w.on * 100.0f), static_cast<int>(w.off * 100.0f));
            }
        }
    }

    // ---- the fighter now --------------------------------------------------

    static const void* interpreter(const void* threads, u32 index)
    {
        const void* unit = reinterpret_cast<const void*>(reinterpret_cast<u32>(threads) + kUnits + kUnitSize * index);
        if (!game::isRam(unit) || game::wordAt(unit, kUnitIndex) != index)
        {
            return NULL;
        }
        const void* interp = reinterpret_cast<const void*>(game::wordAt(unit, 0));
        return game::isRam(interp) ? interp : NULL;
    }

    static const u32* scriptAt(const void* interp, u32 offset)
    {
        const u32* p = reinterpret_cast<const u32*>(game::wordAt(interp, offset));
        return game::isRam(p) ? p : NULL;
    }

    static bool readLive(Fighter* fighter, Live& v)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_animCmdModule) || !game::isRam(m->m_motionModule)
            || !game::isRam(m->m_statusModule) || !game::isRam(m->m_situationModule) || !game::isRam(m->m_stopModule))
        {
            return false;
        }
        const void* threads = reinterpret_cast<const void*>(game::wordAt(m->m_animCmdModule, kThreads));
        if (!game::isRam(threads))
        {
            return false;
        }
        const void* act = interpreter(threads, kUnitActionMain);
        const void* sub = interpreter(threads, kUnitSubactionMain);
        const void* loop = interpreter(threads, kUnitLoop);
        if (act == NULL || sub == NULL || loop == NULL)
        {
            return false;
        }
        v.modules = m;
        v.actStart = scriptAt(act, kInterpStart);
        v.loopStart = scriptAt(loop, kInterpStart);
        v.subStart = scriptAt(sub, kInterpStart);
        v.subCurrent = scriptAt(sub, kInterpCurrent);
        v.clock = *reinterpret_cast<const float*>(reinterpret_cast<u32>(sub) + kInterpClock);
        v.frame = m->m_motionModule->getFrame();
        v.rate = m->m_motionModule->getRate();
        v.looped = (game::byteAt(m->m_motionModule, kMotionFlags) & kMotionLoop) != 0;
        game::MotionInfo info;
        v.length = 0;
        v.kind = -1;
        if (game::readMotion(fighter, info))
        {
            v.length = info.length;
            v.kind = info.kind;
        }
        v.status = game::fighterStatus(m);
        v.hitstun = game::fighterHitstun(m);
        v.shieldStun = game::fighterShieldStun(m, v.status);
        int stop = m->m_stopModule->getHitStopRealFrame();
        v.hitlag = stop > 0 ? stop : 0;
        v.air = m->m_situationModule->getKind() == Situation_Air;
        v.groups = 0;
        for (int g = 1; g < game::kGroupCount; g++)
        {
            if (m->m_statusModule->isEnableTransitionTermGroup(g))
            {
                v.groups |= 1u << g;
            }
        }
        return true;
    }

    static Slot* slotOf(Fighter* fighter)
    {
        if (fighter == NULL)
        {
            return NULL;
        }
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t != NULL && t->fighter == fighter)
            {
                if (s_slots[i].fighter != fighter)
                {
                    s_slots[i].fighter = fighter;
                    s_slots[i].rec.flags = 0;
                    s_slots[i].pending.have = false;
                }
                return &s_slots[i];
            }
        }
        return NULL;
    }

    // Walks again only if the fighter's status or one of its scripts changed.
    static void ensure(Slot& s, const Live& v)
    {
        const FrameRecord& r = s.rec;
        if ((r.flags & kFdValid) == 0 || r.subStart != v.subStart || r.actStart != v.actStart
            || r.loopStart != v.loopStart || r.status != v.status)
        {
            build(s.rec, v);
        }
    }

    // ---- frames from now --------------------------------------------------

    // Frames in which the subaction script runs ("ticks") until its clock is
    // at or past `target`, or, with `withEnd`, until the animation ends,
    // whichever comes first; -1 if neither comes. Counted as the game counts:
    // each tick the clock and the animation frame go up by the rate, then
    // the speed changes the clock has reached apply. Before the script's
    // first command has run, nothing of it is in the past.
    static int ticksUntil(const FrameRecord& rec, const Live& v, float target, bool withEnd, bool& byEnd)
    {
        float c = v.clock;
        float f = v.frame;
        float r = v.rate;
        bool started = v.subCurrent != v.subStart;
        int next = 0;
        while (started && next < rec.rateCount && rec.rate[next].time <= c)
        {
            next++;
        }
        float last = static_cast<float>(v.length) - 1.0f;
        byEnd = false;
        for (int n = 1; n <= kMaxTicks; n++)
        {
            if (!(r > 0.0f))
            {
                return -1;
            }
            c += r;
            f += r;
            while (next < rec.rateCount && rec.rate[next].time <= c)
            {
                r = rec.rate[next].multiply ? r * rec.rate[next].value : rec.rate[next].value;
                next++;
            }
            if (target >= 0.0f && c >= target)
            {
                return n;
            }
            if (withEnd && f >= last)
            {
                byEnd = true;
                return n;
            }
        }
        return -1;
    }

    static bool openNow(const Live& v)
    {
        switch (v.status)
        {
        case game::kStatusWait:
        case game::kStatusWalk:
        case game::kStatusWalkBrake:
        case game::kStatusDash:
        case game::kStatusRun:
        case game::kStatusTurn:
        case game::kStatusTurnDash:
        case game::kStatusTurnRun:
        case game::kStatusSquat:
        case game::kStatusSquatWait:
        case game::kStatusFall:
        case game::kStatusFallAerial:
        case game::kStatusGuardOn:
        case game::kStatusGuard:
            return true;
        case game::kStatusGuardOff:
            // Its script lets jumps out from the start; the count is about
            // everything else, so the attack group stands for "open".
            return (v.groups & (1u << game::kGroupGroundAttack)) != 0;
        default:
            return (v.groups & (v.air ? kAirActionGroups : kGroundActionGroups)) != 0;
        }
    }

    // The statuses a hit puts a fighter in while its hitstun counts
    // (Damage to DamageFlyRoll; Fighter::Status::Kind).
    static bool hitstunStatus(int s)
    {
        return s >= game::kStatusDamage && s <= game::kStatusDamageFlyRoll;
    }

    // The status's own count (StatusRule), in ticks; -1 if none.
    static int ruleTicks(const FrameRecord& rec, const Live& v, Fighter* fighter)
    {
        if (rec.rule == kRuleGuardOff)
        {
            if (!game::isRam(v.modules->m_workManageModule))
            {
                return -1;
            }
            int left = v.modules->m_workManageModule->getInt(kGuardOffTimer);
            return left > 0 ? left : -1;
        }
        if (rec.rule == kRuleLanding && !v.air)
        {
            soModuleAccesser* accesser = fighter->m_moduleAccesser;
            if (!game::isRam(accesser) || !game::isRam(v.modules->m_workManageModule)
                || v.modules->m_workManageModule->isFlag(kNoLandingInterrupt))
            {
                return -1;
            }
            float at = soValueAccesser::getConstantFloat(
                accesser, v.status == game::kStatusLanding ? kLandingFrame : kLandingLightFrame, 0);
            if (at == -1.0f || !(v.rate > 0.0f))
            {
                return -1;
            }
            // The flag is set in the frame that begins with the animation
            // at or past the parameter.
            float f = v.frame;
            for (int n = 1; n <= kMaxTicks; n++)
            {
                if (f >= at)
                {
                    return n;
                }
                f += v.rate;
            }
        }
        return -1;
    }

    static ActIn query(Slot& s, const Live& v)
    {
        ActIn out = {-1, main::kActNone, 0};
        // Frames the fighter stays frozen before it runs again (see the top
        // of the file).
        int frozen = v.hitlag > 0 ? v.hitlag - 1 : 0;
        int stun = v.status == game::kStatusGuardDamage ? v.shieldStun : (hitstunStatus(v.status) ? v.hitstun : 0);
        if (stun > 0)
        {
            // One down per frame it runs; the frame it reaches 0 is the
            // first an input acts in. A landing or a knockdown on the way
            // can make it later.
            out.frames = static_cast<s16>(frozen + stun);
            out.how = main::kActStun;
            out.certain = 1;
            return out;
        }
        if (openNow(v))
        {
            out.frames = static_cast<s16>(v.hitlag);
            out.how = main::kActOpen;
            out.certain = 1;
            return out;
        }
        ensure(s, v);
        const FrameRecord& rec = s.rec;
        int sit = v.air ? 1 : 0;
        bool certain = (rec.flags & kFdUncertain) == 0
                       && (rec.flags & (sit == 0 ? kFdEndCondGround : kFdEndCondAir)) == 0;
        bool started = v.subCurrent != v.subStart;
        bool withEnd = rec.end[sit] == kFdEndFree && v.length > 0 && !v.looped;
        float target = rec.openTime >= 0.0f && (!started || rec.openTime > v.clock) ? rec.openTime : kNoTime;
        if (rec.openTime >= 0.0f && target < 0.0f)
        {
            // Allowed already, yet the groups are closed: something else
            // holds them.
            certain = false;
        }
        if (rec.flags & kFdStatusOpen)
        {
            // The status opened them when it began, yet they are closed.
            certain = false;
        }
        int best = -1;
        u8 how = main::kActNone;
        if (target >= 0.0f || withEnd)
        {
            bool byEnd = false;
            best = ticksUntil(rec, v, target, withEnd, byEnd);
            how = byEnd ? main::kActEnd : main::kActScript;
        }
        int rule = ruleTicks(rec, v, s.fighter);
        if (rule > 0 && (best < 0 || rule < best))
        {
            best = rule;
            how = main::kActRule;
        }
        if (best < 0)
        {
            return out;
        }
        out.frames = static_cast<s16>(frozen + best);
        out.how = how;
        out.certain = certain ? 1 : 0;
        return out;
    }

    ActIn frameDataActIn(Fighter* fighter)
    {
        ActIn none = {-1, main::kActNone, 0};
        Slot* s = slotOf(fighter);
        Live v;
        if (s == NULL || !readLive(fighter, v))
        {
            return none;
        }
        return query(*s, v);
    }

    int framesUntilActionable(Fighter* fighter, int* how)
    {
        ActIn a = frameDataActIn(fighter);
        *how = a.how;
        return a.certain ? a.frames : -1;
    }

    static bool hasActiveRehit(const soModuleEnumeration* m)
    {
        if (m == NULL || !game::isRam(m->m_collisionAttackModule))
        {
            return false;
        }
        soCollisionAttackModule* attack = m->m_collisionAttackModule;
        u32 n = attack->getPartSize();
        if (n > 32)
        {
            n = 32;
        }
        for (u32 i = 0; i < n; i++)
        {
            if (!attack->isAttack(i, false))
            {
                continue;
            }
            const soCollisionAttackData* data = attack->getData(i, false);
            if (game::isRam(data) && data->m_serialHitFrame > 0)
            {
                return true;
            }
        }
        return false;
    }

    static bool attackerHasMoreHits(Fighter* attacker)
    {
        Slot* s = slotOf(attacker);
        Live v;
        if (s == NULL || !readLive(attacker, v))
        {
            return false;
        }
        ensure(*s, v);
        return frameDataMoreHits(s->rec, v.clock, v.subCurrent != v.subStart, hasActiveRehit(v.modules));
    }

    static void clearShieldHoldSlot(ShieldHoldSlot& h)
    {
        h.attackerEntry = -1;
        h.attackerSub = false;
        h.attackerStatus = -1;
        h.holdFrames = 0;
    }

    bool frameDataShieldHoldFullMove(int defenderPlayerNo, int defenderEntryId, const cpu::View& v, bool newHit)
    {
        if (defenderPlayerNo < 0 || defenderPlayerNo >= game::kMaxFighters)
        {
            return false;
        }
        ShieldHoldSlot& h = s_shieldHold[defenderPlayerNo];
        if (newHit)
        {
            clearShieldHoldSlot(h);
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            float bestDistSq = cpu::kFullMoveMaxDistSq + 1.0f;
            bool bestHitlag = false;
            for (int i = 0; i < n; i++)
            {
                if (refs[i].entryId == defenderEntryId)
                {
                    continue;
                }
                for (int sub = 0; sub < 2; sub++)
                {
                    Fighter* cand = sub == 0 ? refs[i].fighter : refs[i].partner;
                    cpu::View av;
                    if (cand == NULL || !cpu::readView(cand, av) || !cpu::isAttackerMoveStatus(av.status)
                        || av.hitstun > 0 || av.damageHitlag)
                    {
                        continue;
                    }
                    float dx = av.x - v.x;
                    float dy = av.y - v.y;
                    float d2 = dx * dx + dy * dy;
                    if (d2 > cpu::kFullMoveMaxDistSq)
                    {
                        continue;
                    }
                    bool hasLag = av.hitlag > 0;
                    if (!hasLag && !attackerHasMoreHits(cand))
                    {
                        continue;
                    }
                    if (h.attackerEntry < 0 || (hasLag && !bestHitlag)
                        || (hasLag == bestHitlag && d2 < bestDistSq))
                    {
                        h.attackerEntry = static_cast<s8>(refs[i].entryId);
                        h.attackerSub = sub != 0;
                        h.attackerStatus = static_cast<s16>(av.status);
                        bestDistSq = d2;
                        bestHitlag = hasLag;
                    }
                }
            }
            return false;
        }
        if (h.attackerEntry < 0)
        {
            return false;
        }
        Fighter* att = cpu::fighterOf(h.attackerEntry, h.attackerSub);
        cpu::View av;
        cpu::ShieldHoldInput in;
        in.shieldTiming = main::settings().cpu.options.shieldTiming;
        in.attackerReadable = att != NULL && cpu::readView(att, av);
        in.latchedStatus = h.attackerStatus;
        in.attackerStatus = in.attackerReadable ? av.status : -1;
        in.attackerHitstun = in.attackerReadable ? av.hitstun : 0;
        in.attackerDamageHitlag = in.attackerReadable ? av.damageHitlag : false;
        in.attackerCanAct = in.attackerReadable ? cpu::canAct(av) : true;
        in.attackerHasMoreHits = in.attackerReadable ? attackerHasMoreHits(att) : false;
        float dx = in.attackerReadable ? (av.x - v.x) : 999.0f;
        float dy = in.attackerReadable ? (av.y - v.y) : 999.0f;
        in.distSq = dx * dx + dy * dy;
        in.holdFrames = h.holdFrames;
        if (cpu::shouldHoldFullMoveShield(in))
        {
            if (h.holdFrames < cpu::kFullMoveMaxHold)
            {
                h.holdFrames++;
            }
            return true;
        }
        clearShieldHoldSlot(h);
        return false;
    }

    const FrameRecord* frameRecord(Fighter* fighter)
    {
        Slot* s = slotOf(fighter);
        Live v;
        if (s == NULL || !readLive(fighter, v))
        {
            return NULL;
        }
        ensure(*s, v);
        return &s->rec;
    }

    int frameDataFramesUntil(Fighter* fighter, float scriptTime)
    {
        Slot* s = slotOf(fighter);
        Live v;
        if (s == NULL || scriptTime < 0.0f || !readLive(fighter, v))
        {
            return -1;
        }
        ensure(*s, v);
        if (v.subCurrent != v.subStart && scriptTime <= v.clock)
        {
            return 0;
        }
        bool byEnd = false;
        int n = ticksUntil(s->rec, v, scriptTime, false, byEnd);
        return n < 0 ? -1 : n + (v.hitlag > 0 ? v.hitlag - 1 : 0);
    }

    // ---- frame advantage when the hit lands -------------------------------

    // The advantage the tracker will measure, worked out in the hit's frame.
    // Shown only for a grounded attacker's hit on shield (measured: the
    // tracker's value in all 26 comparisons, 13 jabs, tilts and smashes of
    // four fighters). Logged only: hits that connect (a landing or knockdown
    // changes it: 4 of 21 off by 3 to 30, measured) and an airborne
    // attacker's hit on shield (a later L-cancel: Mario's neutral air 8 off).
    static void predict(int d, s32 now)
    {
        const game::Tracked* def = game::tracked(d);
        Slot& slot = s_slots[d];
        if (def == NULL || !(def->cur.hitReceived || def->cur.shieldReceived) || def->lastReceived.frame != now)
        {
            return;
        }
        Pending& p = slot.pending;
        p.have = true;
        p.shown = false;
        p.shield = def->cur.shieldReceived;
        p.attacker = def->lastReceived.other;
        p.hitFrame = now;
        p.value = 0;
        p.why = NULL;
        const game::Tracked* att = game::tracked(p.attacker);
        Live dv;
        if (att == NULL || def->lastReceived.how != game::kByInflict)
        {
            p.why = "attacker not known";
        }
        else if (!readLive(def->fighter, dv))
        {
            p.why = "defender not readable";
        }
        else if (p.shield ? (dv.status != game::kStatusGuardDamage || dv.shieldStun <= 0)
                          : (!hitstunStatus(dv.status) || dv.hitstun <= 0))
        {
            p.why = "no stun";
        }
        else if (!p.shield && !dv.air)
        {
            p.why = "hitstun on the ground";
        }
        if (p.why != NULL)
        {
            return;
        }
        ActIn a = frameDataActIn(att->fighter);
        if (a.frames < 0 || !a.certain || a.how == main::kActStun)
        {
            p.why = a.how == main::kActStun ? "attacker in stun" : "attacker's move not certain";
            return;
        }
        int defender = (dv.hitlag > 0 ? dv.hitlag - 1 : 0) + (p.shield ? dv.shieldStun : dv.hitstun);
        p.value = static_cast<s16>(defender - a.frames);
        const main::Settings& s = main::settings();
        Live av;
        bool grounded = readLive(att->fighter, av) && !av.air;
        bool shownKind = p.shield && grounded;
        bool wanted = shownKind && s.osd.frameAdvantage != 0;
        bool inRange = p.value > -kShownLimit && p.value < kShownLimit;
        TM_LOG("advantage predicted: P%d%s -> P%d%s on %s %+d (hit %d, attacker in %d how %d, defender in %d)%s\n",
               att->playerNo + 1, att->partner ? "b" : "", def->playerNo + 1, def->partner ? "b" : "",
               p.shield ? "shield" : "hit", p.value, now, a.frames, a.how, defender,
               !shownKind ? " measured only" : (inRange ? "" : " not shown"));
        if (wanted && inRange)
        {
            osd::post(att->playerNo, osd::kIdFrameAdvantage, osd::frameAdvantageColor(p.value),
                      "Shield Adv.\n%+d f", p.value);
            p.shown = true;
        }
    }

    bool frameDataMeasured(const game::Advantage& r)
    {
        if (r.defender < 0 || r.defender >= game::kMaxTracked)
        {
            return false;
        }
        Pending& p = s_slots[r.defender].pending;
        bool mine = p.have && p.attacker == r.attacker && p.hitFrame == r.hitFrame;
        if (!mine || p.why != NULL)
        {
            TM_LOG("advantage check: P%d -> P%d on %s predicted none measured %+d (%s)\n", r.attackerPlayer + 1,
                   r.defenderPlayer + 1, r.shield ? "shield" : "hit", r.value,
                   mine ? p.why : "no hit seen for it");
            return false;
        }
        bool same = p.value == r.value;
        TM_LOG("advantage check: P%d -> P%d on %s predicted %+d measured %+d %s\n", r.attackerPlayer + 1,
               r.defenderPlayer + 1, r.shield ? "shield" : "hit", p.value, r.value, same ? "same" : "DIFFERENT");
        p.have = false;
        if (!p.shown)
        {
            return false;
        }
        if (!same)
        {
            // The message shown at the hit was wrong: the measured value
            // takes its place, and says that it is the measured one.
            osd::post(r.attackerPlayer, osd::kIdFrameAdvantage, osd::frameAdvantageColor(r.value),
                      "Shield Adv.\nActual: %+d f", r.value);
        }
        return true;
    }

    // The whole-body collision state the hit module holds now (what the
    // Body Collision command sets): the body's hit group is the first of
    // the module's group array (+0x58, group at +0xC, its m_whole at +0x1C;
    // game/body.cpp). For the trace only.
    static int bodyWhole(const soModuleEnumeration* m)
    {
        if (!game::isRam(m->m_collisionHitModule))
        {
            return -1;
        }
        const void* array = reinterpret_cast<const void*>(game::wordAt(m->m_collisionHitModule, 0x58));
        return game::isRam(array) ? static_cast<int>(game::wordAt(array, 0xC + 0x1C)) : -1;
    }

    static void trace(const game::Tracked* t, Fighter* fighter)
    {
        Slot* s = slotOf(fighter);
        Live v;
        if (s == NULL || !readLive(fighter, v))
        {
            return;
        }
        ActIn a = query(*s, v);
        ensure(*s, v);
        // The body windows against the body itself: the state the record
        // gives for now, the state the hit module holds, and the frames
        // until the next window (the first not yet over) begins and ends.
        const FrameRecord& rec = s->rec;
        bool started = v.subCurrent != v.subStart;
        int window = 0, on = -1, off = -1;
        for (int i = 0; i < rec.bodyCount; i++)
        {
            const FdWindow& w = rec.body[i];
            bool begun = started && w.on <= v.clock;
            if (begun && w.off >= 0.0f && w.off <= v.clock)
            {
                continue;
            }
            window = begun ? w.kind : 0;
            on = begun ? 0 : frameDataFramesUntil(fighter, w.on);
            off = frameDataFramesUntil(fighter, w.off);
            break;
        }
        TM_LOG("fd f=%d p=%d%s st=%x sub=%d clk=%d fr=%d rate=%d len=%d hl=%d in=%d how=%d sure=%d flags=%04x "
               "grp=%x act=%d body=%d win=%d bon=%d boff=%d\n",
               t->cur.frame, t->playerNo + 1, t->partner ? "b" : "", v.status, v.kind,
               static_cast<int>(v.clock * 100.0f), static_cast<int>(v.frame * 100.0f),
               static_cast<int>(v.rate * 100.0f), v.length, v.hitlag, a.frames, a.how, a.certain, s->rec.flags,
               v.groups, t->cur.actionable, bodyWhole(v.modules), window, on, off);
    }

    void frameDataFrame()
    {
        s32 now = game::trackerFrame();
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL)
            {
                s_slots[i].fighter = NULL;
                s_slots[i].rec.flags = 0;
                s_slots[i].pending.have = false;
                continue;
            }
            Slot* s = slotOf(t->fighter);
            Live v;
            if (s != NULL && readLive(t->fighter, v))
            {
                ensure(*s, v);
            }
        }
        for (int d = 0; d < game::kMaxTracked; d++)
        {
            // A value that was shown and never measured (the advantage
            // tracker gives an exchange up after 180 frames): by then its
            // message is long gone; the log says so.
            Pending& p = s_slots[d].pending;
            if (p.have && p.shown && now - p.hitFrame > kGiveUp)
            {
                TM_LOG("advantage check: slot %d on shield predicted %+d measured none (given up)\n", d, p.value);
                p.have = false;
            }
            predict(d, now);
        }
        if (g_fdTrace)
        {
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t != NULL)
                {
                    trace(t, t->fighter);
                }
            }
        }
    }

    void frameDataReset()
    {
        memset(s_slots, 0, sizeof(s_slots));
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            clearShieldHoldSlot(s_shieldHold[i]);
        }
    }

    void frameDataAttach()
    {
        memset(s_slots, 0, sizeof(s_slots));
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            clearShieldHoldSlot(s_shieldHold[i]);
        }
        s_walks = 0;
        g_fdTrace = 0;
        TM_LOG("fd: trace byte at %08x, record %u bytes\n", &g_fdTrace, static_cast<u32>(sizeof(FrameRecord)));
    }
}
}
