// The CPU Options' state machine (options.h), with Move CPU, Freeze CPU,
// Controlled By and the cues and holds of cue.h. Changes a driven CPU's
// inputs, controller port and CPU type (Controlled By), motion rate
// (Freeze), position (Move), shield health and intangibility.
// Domain methods are factored cleanly into options_di_sdi.hpp,
// options_tech.hpp, and options_counters.hpp via internal brain.h and included
// here so MWCC's -ipa file keeps single-TU codegen across Brain::next().
#define TM_OPTIONS_CPP_INTERNAL
#include "main/cpu/brain.h"

#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <string.h>

#include "main/cpu/sdi_logic.h"
#include "main/cpu/shield_timing_logic.h"
#include "main/display/display.h"
#include "main/game/tech_frames.h"
#include "main/game/tracker.h"
#include "main/game/training_controller.h"
#include "main/record/record.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace cpu {
    static u32 s_rng = 1;
    static Brain* s_brains = NULL;

    // xorshift32 stirred with the frame counter, so a replayed savestate does
    // not repeat its choices; the game's own generators are never touched.
    static u32 cpuRnd()
    {
        u32 x = s_rng ^ (game::frameCounter() * 0x9E3779B9u);
        if (x == 0)
        {
            x = 1;
        }
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s_rng = x;
        return x;
    }

    static int cpuRndBelow(int n)
    {
        return n > 0 ? static_cast<int>(cpuRnd() % static_cast<u32>(n)) : 0;
    }

    // Index 0..3 picked with the given weights (all 0: uniform).
    static int cpuWeighted(const u8* w)
    {
        int total = w[0] + w[1] + w[2] + w[3];
        if (total == 0)
        {
            return cpuRndBelow(4);
        }
        int r = cpuRndBelow(total);
        for (int i = 0; i < 4; i++)
        {
            if (r < w[i])
            {
                return i;
            }
            r -= w[i];
        }
        return 3;
    }

    // ---- Move CPU, Freeze CPU ---------------------------------------------------

    char g_moveCpuLabel[12] = "";
    char g_freezeCpuLabel[12] = "";
    static bool s_moving = false;
    static float s_moveX = 0.0f, s_moveY = 0.0f; // the player's stick this frame

    // Moving the CPU: the player's fighter stands, its stick moves the CPU.
    class Mover : public Source {
    public:
        int playerNo;

        virtual bool wants(const Owner& who)
        {
            return s_moving && who.playerNo == playerNo && opts().control == kControlLab;
        }

        virtual bool next(const Owner&, const Frame& produced, Frame& out)
        {
            // Inputs keep arriving while Training's pause (and the lab menu
            // in it) is open: those are the menus'.
            bool paused = game::matchState().paused;
            s_moveX = paused ? 0.0f : produced.x;
            s_moveY = paused ? 0.0f : produced.y;
            out.buttons = 0;
            out.x = out.y = 0.0f;
            return true;
        }

        virtual void released(const Owner&)
        {
            s_moveX = s_moveY = 0.0f;
        }
    };

    static Mover s_mover;

    void moveCpuToggle()
    {
        s_moving = !s_moving;
        s_mover.playerNo = -1;
        s_moveX = s_moveY = 0.0f;
        strcpy(g_moveCpuLabel, s_moving ? "Finish" : "");
        TM_LOG("cpu: move CPU %s\n", s_moving ? "on" : "off");
    }

    static bool s_freezeRequest = false;

    void freezeCpuToggle()
    {
        s_freezeRequest = !s_freezeRequest;
        strcpy(g_freezeCpuLabel, s_freezeRequest ? "Frozen" : "");
        TM_LOG("cpu: freeze CPU %s\n", s_freezeRequest ? "on" : "off");
    }

    // ---- Controlled By ----------------------------------------------------------

    // Training's Control for the chosen port: the setup's controller port and
    // CPU type 25. What we changed, to put back.
    static s8 s_savedPort[kMaxSlots];
    static s8 s_origPort[kMaxSlots];
    static u8 s_savedType[kMaxSlots];
    static u8 s_savedHumanFlag[kMaxSlots];

    // Resolves a CPU player's live main aiInput on demand (never cached across
    // fighter re-creation or savestate loads): ftManager::getEntryId ->
    // ftEntryManager::getEntity -> ftEntry::m_input + 0xC.
    static void* liveCpuInput(int playerNo)
    {
        enum { kFtInputMain = 0x0C };
        ftManager* manager = g_ftManager;
        ftEntryManager* entries = g_ftEntryManager;
        if (playerNo < 0 || playerNo >= kMaxSlots || !game::isRam(manager) || !game::isRam(entries))
        {
            return NULL;
        }
        int entryId = manager->getEntryId(playerNo);
        if (!manager->isValidEntryId(entryId))
        {
            return NULL;
        }
        ftEntry* entry = entries->getEntity(entryId);
        if (!game::isRam(entry) || entry->m_entryId != entryId || entry->m_playerNo != playerNo
            || !game::isRam(entry->m_input)
            || !game::cpuPlayer(playerNo, manager->getFighterOperationType(entryId) != 0))
        {
            return NULL;
        }
        void* input = reinterpret_cast<void*>(game::wordAt(entry->m_input, kFtInputMain));
        return game::isRam(input) ? input : NULL;
    }

    static bool anyPortChanged()
    {
        for (int i = 0; i < kMaxSlots; i++)
        {
            if (s_portChanged[i])
            {
                return true;
            }
        }
        return false;
    }

    static void restorePort(int playerNo, bool updatePatch)
    {
        if (playerNo >= 0 && playerNo < kMaxSlots && s_portChanged[playerNo])
        {
            setControllerOf(playerNo, s_savedPort[playerNo]);
            void* input = liveCpuInput(playerNo);
            if (input != NULL)
            {
                setCpuType(input, s_savedType[playerNo]);
                setHumanPadFlag(input, s_savedHumanFlag[playerNo]);
            }
            s_portChanged[playerNo] = false;
            TM_LOG("cpu: P%d controller port back to %d, CPU type %d\n", playerNo + 1, s_savedPort[playerNo],
                   s_savedType[playerNo]);
            if (updatePatch && !anyPortChanged())
            {
                game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
            }
        }
    }

    void controlledByChanged()
    {
        if (opts().controlledBy == 0)
        {
            for (int i = 0; i < kMaxSlots; i++)
            {
                restorePort(i, false);
            }
            // Let recording claim the port and patch owner bit before dropping
            // ours, so 0x80962d0c never unpatches when CPU Mode stays active.
            record::reconcileController();
            game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
        }
    }

    // ---- cues and holds (cue.h) ---------------------------------------------------

    static void cueTest(int playerNo, s32 now)
    {
        if (s_aux == NULL)
        {
            return;
        }
        CueTestBlock& b = s_aux->cueTest;
        if (b.playerNo != playerNo)
        {
            return;
        }
        if (b.generation != b.seen)
        {
            b.seen = b.generation;
            b.opFrame = now;
            switch (b.op)
            {
            case kOpCue:
                cue(playerNo, b.list, b.index, b.delay > 0 ? now + b.delay - 1 : kCueAsap);
                break;
            case kOpCancel:
                cueCancel(playerNo);
                break;
            case kOpHold:
            case kOpHoldRepeat:
                hold(playerNo, b.buttons, b.x, b.y, b.ref, b.op == kOpHoldRepeat);
                break;
            default:
                holdRelease(playerNo);
                break;
            }
        }
        b.state = cueState(playerNo);
        b.start = cueStart(playerNo);
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    void Brain::reset()
    {
        memset(&state, 0, reinterpret_cast<u8*>(&ledgeState) - reinterpret_cast<u8*>(&state));
        prevStatus = -1;
        gravity = 0.1f;
        lastPress = techFrom = -1000;
        playSlot = -1;
        randomSide = 1.0f;
        frozenStatus = -1;
        ledgeStateReset(ledgeState);
        main::extraPPlusRecoveryReset(slot);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    void Brain::released(const Owner& who)
    {
        noReactionOn(who, false);
        restorePort(who.playerNo);
        unfreeze(who);
        reset();
    }

    void Brain::noReactionOn(const Owner& who, bool on)
    {
        if (on || noReaction)
        {
            noReaction = on;
            setNoReaction(fighterOf(who.entryId, false), on);
            setNoReaction(fighterOf(who.entryId, true), on);
        }
    }

    void Brain::setBodiesMotion(int entryId, Fighter* main, float rate)
    {
        if (main != NULL)
        {
            setMotionRate(main, rate);
        }
        Fighter* nana = fighterOf(entryId, true);
        if (nana != NULL && nana != main)
        {
            setMotionRate(nana, rate);
        }
    }

    void Brain::enterIdle(Frame& out, const Owner& who)
    {
        state = kIdle;
        hits = 0;
        shieldKind = false;
        noTech = false;
        idle(out, who);
    }

    void Brain::enterDown(Frame& out, const Owner& who)
    {
        state = kDown;
        downFrames = 0;
        missWait = 0;
        getupInput(out, who);
    }

    void Brain::clearAction(int nextState)
    {
        resetActionState(state, shieldKind, powershield, nextState);
        script = NULL;
        pending = NULL;
    }

    bool Brain::pickMultiRev(int trigger, int entryId)
    {
        revAction = 0;
        int ra, rd;
        if (opts().multiReversal == 0 || !main::extraReversalPick(trigger, fighterKind(entryId), cpuRnd(), ra, rd))
        {
            return false;
        }
        revAction = static_cast<u8>(ra);
        revDelay = static_cast<u8>(rd);
        return true;
    }

    bool Brain::held(Frame& out, const Owner& who)
    {
        const Cue& q = s_aux->cues[who.playerNo & 3];
        if (q.hold)
        {
            out.buttons = q.repeat && (lastButtons(slot) & q.holdButtons) != 0 ? 0 : q.holdButtons;
            stick(out, q.holdX, q.holdY, q.holdRef);
        }
        return q.hold;
    }

    void Brain::remember()
    {
        havePrev = true;
        prevHitlag = v.hitlag;
        prevDamageHitlag = v.damageHitlag;
        prevHitstun = v.hitstun;
        prevShieldStun = v.shieldStun;
        prevStatus = v.status;
        prevVy = v.vy;
        prevAir = v.air;
    }

    void Brain::unfreeze(const Owner& who)
    {
        if (!frozen)
        {
            return;
        }
        frozen = false;
        setBodiesMotion(who.entryId, fighterOf(who.entryId, false), 1.0f);
        TM_LOG("cpu: P%d unfrozen\n", who.playerNo + 1);
    }

    bool Brain::tools(const Owner& who, const Frame& produced, Frame& out)
    {
        const CpuOptions& c = opts();
        if (c.controlledBy != 0)
        {
            if (!s_portChanged[who.playerNo])
            {
                if (game::trainingControllerOwners() == 0)
                {
                    s_origPort[who.playerNo] = static_cast<s8>(controllerOf(who.playerNo));
                }
                s_savedPort[who.playerNo] = s_origPort[who.playerNo];
                s_savedType[who.playerNo] = static_cast<u8>(cpuType(who.input));
                s_savedHumanFlag[who.playerNo] = humanPadFlag(who.input);
                s_portChanged[who.playerNo] = true;
                TM_LOG("cpu: P%d controlled by port %d\n", who.playerNo + 1, c.controlledBy);
            }
            game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
            setControllerOf(who.playerNo, c.controlledBy);
            setCpuType(who.input, kCpuTypeControl);
            // Enable P+ ButtonPresses.asm (0x80575C04) analog C-Stick & L/R
            // capture into LA-Float[34..37] and pass hardware pad through untouched.
            setHumanPadFlag(who.input, 1);
            passThrough(who.slot);
            out = produced;
            reset();
            return true;
        }
        restorePort(who.playerNo);
        if (who.playerNo >= 0 && who.playerNo < kMaxSlots && game::trainingControllerOwners() == 0)
        {
            s_origPort[who.playerNo] = static_cast<s8>(controllerOf(who.playerNo));
        }
        if (s_freezeRequest && !frozen && frozenStatus != v.status)
        {
            frozen = true;
            frozenStatus = v.status;
            TM_LOG("cpu: P%d frozen in status %x\n", who.playerNo + 1, v.status);
        }
        if (frozen && (!s_freezeRequest || v.status != frozenStatus))
        {
            // A hit or anything else that changes the status lets it go,
            // as the game resets the animation speed then.
            unfreeze(who);
            if (s_freezeRequest)
            {
                s_freezeRequest = false;
                g_freezeCpuLabel[0] = '\0';
            }
        }
        if (!s_freezeRequest)
        {
            frozenStatus = -1;
        }
        if (frozen)
        {
            setBodiesMotion(who.entryId, f, 0.0f);
            return true;
        }
        if (s_moving)
        {
            if (!placed)
            {
                placed = true;
                moveX = v.x;
                moveY = v.y;
            }
            if (!v.air)
            {
                requestStatus(f, kStatusFall);
            }
            moveX += s_moveX * kMoveSpeed;
            moveY += s_moveY * kMoveSpeed;
            placeAt(f, moveX, moveY);
            return true;
        }
        if (placed)
        {
            placed = false;
            reset();
        }
        return false;
    }

    bool cue(int playerNo, int list, int index, s32 frame)
    {
        Cue* q = cueOf(playerNo);
        if (q == NULL || actionKind(list, index) == kActionNone)
        {
            return false;
        }
        q->list = static_cast<s8>(list);
        q->index = static_cast<u8>(index);
        q->frame = frame;
        q->start = -1;
        q->state = kCueWaiting;
        q->custom = NULL;
        return true;
    }

    bool cueAction(int playerNo, const Action* a, int list, s32 frame)
    {
        // Any index with a script passes cue's check; the action replaces it.
        if (a == NULL || a->steps == NULL || !cue(playerNo, list, 1, frame))
        {
            return false;
        }
        cueOf(playerNo)->custom = a;
        return true;
    }

    void cueReset(int playerNo)
    {
        cueCancel(playerNo);
        holdRelease(playerNo);
        if (s_brains == NULL)
        {
            return;
        }
        for (int i = 0; i < kMaxSlots; i++)
        {
            Brain& b = s_brains[i];
            if (b.player == playerNo && !b.frozen)
            {
                // The game still counts from the CPU's last guard press.
                s32 press = b.lastPress;
                b.reset();
                b.lastPress = press;
            }
        }
    }

    s32 techReady(int playerNo)
    {
        if (s_brains == NULL)
        {
            return 0;
        }
        for (int i = 0; i < kMaxSlots; i++)
        {
            if (s_brains[i].player == playerNo)
            {
                return s_brains[i].lastPress + kTechGap;
            }
        }
        return 0;
    }

    int cueState(int playerNo)
    {
        Cue* q = cueOf(playerNo);
        return q != NULL ? q->state : static_cast<int>(kCueNone);
    }

    s32 cueStart(int playerNo)
    {
        Cue* q = cueOf(playerNo);
        return q != NULL && q->state >= kCueRunning ? q->start : -1;
    }

    void cueCancel(int playerNo)
    {
        Cue* q = cueOf(playerNo);
        if (q == NULL)
        {
            return;
        }
        for (int i = 0; s_brains != NULL && i < kMaxSlots && q->state == kCueRunning; i++)
        {
            Brain& b = s_brains[i];
            if (b.player == playerNo && b.state == kAfter)
            {
                b.script = NULL;
                b.pending = NULL;
                b.playSlot = -1;
            }
        }
        q->state = kCueNone;
    }

    void hold(int playerNo, u32 buttons, float x, float y, int ref, bool repeat)
    {
        Cue* q = cueOf(playerNo);
        if (q != NULL)
        {
            q->hold = true;
            q->repeat = repeat;
            q->holdButtons = static_cast<u16>(buttons);
            q->holdX = x;
            q->holdY = y;
            q->holdRef = static_cast<u8>(ref);
        }
    }

    void holdRelease(int playerNo)
    {
        Cue* q = cueOf(playerNo);
        if (q != NULL)
        {
            q->hold = false;
        }
    }

    // The opponent the CPU orients to: the human first, else any other.
    static float opponentX(const Owner& who, float fallback)
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int pass = 0; pass < 2; pass++)
        {
            for (int i = 0; i < n; i++)
            {
                if (refs[i].entryId == who.entryId || (pass == 0 && !humanPlayer(refs[i].playerNo)))
                {
                    continue;
                }
                game::FighterState st;
                if (game::readState(refs[i], st))
                {
                    return st.x;
                }
            }
        }
        return fallback;
    }

}
}
#include "main/cpu/options_di_sdi.hpp"
#include "main/cpu/options_tech.hpp"
#include "main/cpu/options_counters.hpp"
#undef TM_OPTIONS_CPP_INTERNAL

namespace tm {
namespace cpu {
#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static bool grabbedStatus(int s)
    {
        return (s >= kStatusCapturePulled && s <= kStatusCaptureDamage)
               || (s >= 0xCC && s <= 0xDB && s != 0xD5 && s != 0xD8) || (s >= 0xE6 && s <= 0xEB) || s == 0xEE
               || s == 0xFA || (s >= 0x102 && s <= 0x10A);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    bool Brain::next(const Owner& who, const Frame& produced, Frame& out)
    {
        player = who.playerNo;
        f = fighterOf(who.entryId, false);
        if (f == NULL || !readView(f, v))
        {
            return false;
        }
        const CpuOptions& c = opts();
        out.buttons = 0;
        out.x = 0.0f;
        out.y = 0.0f;
        float ox = opponentX(who, v.x + v.lr);
        float dx = ox - v.x;
        oppDist = dx < 0.0f ? -dx : dx;
        toward = ox == v.x ? v.lr : sign(dx);

        // Tech Sound: fires on the tech's distinguishable frame without Tech
        // Invisibility Delay.
        if (c.techSound && passiveStatus(v.status))
        {
            int dist = game::techDistinguishable(game::fighterKind(who.entryId, who.sub));
            // v.motionFrame is the 0-indexed frame the previous step ended on;
            // the coming step plays motionFrame + 1 (0-indexed), i.e.
            // motionFrame + 2 in 1-indexed animation frames.
            int stateFrame = game::f2i(v.motionFrame) + 2;
            if (!techSounded && game::techSoundFrame(stateFrame, dist))
            {
                techSounded = true;
                int handle = playSound(f, kTechSoundId);
                TM_LOG("cpu: P%d tech sound at tech frame %d (handle %d)\n", who.playerNo + 1, stateFrame, handle);
            }
        }
        else
        {
            techSounded = false;
        }

        if (tools(who, produced, out))
        {
            return true;
        }
        // KO'd: reset state while the CPU is out of play or respawning so the
        // respawned fighter does not finish a stale counter action.
        if ((v.status == game::kStatusDead || v.status == game::kStatusStandby || v.status == game::kStatusRebirth)
            && (state != kIdle || hits != 0 || script != NULL || pending != NULL || playSlot >= 0 || recovering))
        {
            TM_LOG("cpu: P%d KO'd (status %x): options state reset, frame %u\n", who.playerNo + 1, v.status,
                   frameOf(slot));
            reset();
        }
        // Cleanly drop off the respawn halo (Rebirth = 0xBE) instead of waiting
        // out the full platform timer.
        if (v.status == game::kStatusRebirth && !held(out, who))
        {
            out.y = -1.0f;
            remember();
            return true;
        }
        // A hit the recording handed over: the options have the CPU again.
        const s32 now = game::trackerFrame();
        Handed& hand = s_aux->handed[who.playerNo & 3];
        bool handed = false;
        bool handedCounter = false;
        if (hand.frame != game::kNoFrame)
        {
            s32 age = now - hand.frame;
            if (age >= 0 && age <= kHandFrames && !grabbedStatus(v.status))
            {
                handed = true;
                handedCounter = hand.counter;
                if (hand.counter && hand.hits > 0)
                {
                    hits = hand.hits - 1; // counted again below
                }
            }
            hand.frame = game::kNoFrame;
        }
        // Only victim hitlag counts as a hit: attacker hitlag has isDamage()
        // false (0x8083FFC0), damage hitlag true (0x8076D32C). Shield hitlag
        // also has isDamage() false (0x80840690) and keys on Guard_Damage.
        // Pummels while held and hits under Intangibility are not counted.
        bool damageLag = v.damageHitlag && (v.hitlag > prevHitlag || !prevDamageHitlag) && !noReaction;
        bool shieldLag = shieldHitRefreshed(v.status, v.hitlag, prevHitlag, v.shieldStun, prevShieldStun, prevStatus);
        bool newHit = handed
                      || (havePrev && (damageLag || shieldLag || v.hitstun > prevHitstun) && !grabbedStatus(v.status));
        Cue& q = s_aux->cues[who.playerNo & 3];
        cueTest(who.playerNo, now);

        if (!v.air && v.hitstun == 0 && v.hitlag == 0)
        {
            groundY = v.y;
            groundKnown = true;
        }
        if (v.air && prevAir)
        {
            float g = prevVy - v.vy;
            if (g > 0.02f && g < 0.5f)
            {
                gravity = g;
            }
        }

        if (newHit)
        {
            actionable = 0;
            revPicked = false;
            script = NULL;
            pending = NULL;
            powershield = 0;
            hits++;
            if (v.status == game::kStatusGuardDamage)
            {
                state = kShieldStun;
                shieldKind = true;
                if (c.shieldTiming == kShieldTimingFullMove)
                {
                    main::extraShieldHoldFullMove(who.playerNo, who.entryId, v, true);
                }
            }
            else if (handedCounter)
            {
                // On CPU Hit: count the hit and move straight to the counter state.
                state = kCounter;
                shieldKind = false;
                TM_LOG("cpu: P%d hit %d handed over by the playback: counter, hitlag %d hitstun %d, frame %u\n",
                       who.playerNo + 1, hits, v.hitlag, v.hitstun, frameOf(slot));
            }
            else
            {
                // Simulate Tech Trap: docs/internals.md, "CPU tech timing".
                if (tumbleStatus(prevStatus) && c.techTrap != kTrapOff)
                {
                    trapPress = c.techTrap == kTrapLatest;
                    techFrom = now + kTechGap - kTrapEarly;
                }
                state = kHit;
                shieldKind = false;
                noTech = false;
                chooseDi(who);
                sdiDone = hitlagFrame = afterHitlag = 0;
                techChosen = false;
                TM_LOG("cpu: P%d hit %d: hitlag %d hitstun %d, di %d,%d sdi %d,%d asdi %d,%d, frame %u\n",
                       who.playerNo + 1, hits, v.hitlag, v.hitstun, game::f2i(tdiX * 100.0f),
                       game::f2i(tdiY * 100.0f), game::f2i(sdiX * 100.0f), game::f2i(sdiY * 100.0f),
                       haveAsdi ? game::f2i(asdiX * 100.0f) : 0, haveAsdi ? game::f2i(asdiY * 100.0f) : 0,
                       frameOf(slot));
                TM_LOG("cpu: P%d hit %d: di kind %d, status %x after %x\n", who.playerNo + 1, hits, diKind, v.status,
                       prevStatus);
            }
        }
        if (grabbedStatus(v.status) && state != kGrabbed)
        {
            clearAction(kGrabbed);
        }
        if (c.clatterEscape != kMashNone && isClatterStatus(v.status) && state != kClatter)
        {
            clearAction(kClatter);
            playSlot = -1;
        }
        // Entering a techable slip (Slip 0x89 / Slip_Down 0x8A) without a
        // damage hit (e.g. stepping onto a grounded banana): handle via
        // kTechFall so Tech and Slip_Wait Get Up options both run.
        if (slipTechStatus(v.status) && !slipTechStatus(prevStatus) && !newHit && state != kHit)
        {
            clearAction(kTechFall);
            playSlot = -1;
            techChosen = false;
            techPressed = false;
            noTech = false;
        }
        // Put in tumble or down without a hit: tech and get up.
        if (state == kIdle && v.hitlag == 0)
        {
            if (v.air && tumbleStatus(v.status))
            {
                state = kTechFall;
                techChosen = false;
                noTech = false;
            }
            else if (downStatus(v.status))
            {
                state = kDown;
                downFrames = 0;
                missWait = 0;
            }
        }

        if (c.infiniteShields == kShieldsOn || (c.infiniteShields == kShieldsUntilHit && hits == 0))
        {
            float health = c.shieldHealth;
            setShieldHealth(f, health);
            Fighter* nana = fighterOf(who.entryId, true);
            if (nana != NULL && nana != f)
            {
                setShieldHealth(nana, health);
            }
        }
        if (c.intangible == kIntangiblePassThrough)
        {
            setIntangible(f, 2);
        }
        noReactionOn(who, c.intangible == kIntangibleNoReaction);

        // Active Tech Practice drivers (Solo Standby, Powershield projectiles,
        // L-Cancel / Float-DJC targets) preempt Lab recovery, ledge, and counter
        // behaviors while they claim the CPU.
        if ((main::extraDrill(0, NULL) & 1u) != 0u
            && main::extraScenarioCpuStep(who, v, f, out, lastButtons(slot)))
        {
            state = kIdle;
            remember();
            return true;
        }

        // On-ledge reversal / getup option (ledge_logic.h): preempts Game AI /
        // PPlusAI recovery only while hanging on the ledge or completing an
        // on-stage getup. Once airborne after Drop or Jump, normal recovery /
        // lab state resumes cleanly.
        bool busyForLedge = (state == kHit && (v.hitstun > 0 || v.hitlag > 0)) || state == kGrabbed
                            || state == kClatter || v.hitlag > 0 || q.state != kCueNone || q.hold;
        if (stepLedge(out, who, c, busyForLedge))
        {
            remember();
            return true;
        }

        // Offstage / ledge recovery routing (Game AI, PPlusAI, or Off).
        // Runs only after hitlag/hitstun, grabs, cues, and active counter
        // scripts finish so DI/SDI and counter actions are never interrupted.
        int recMode = c.recovery;
        if (main::settings().comboTrainer.enabled && main::settings().comboTrainer.recoveryTrainer)
        {
            recMode = kRecoveryPPlusAi;
        }
        bool wallTechSoon = state == kTechFall && c.tech != kTechNone && v.air && tumbleStatus(v.status)
                            && framesToWall(kTechLead, ecbLift()) >= 0;
        bool busyWithLab = state == kHit || state == kGrabbed || state == kClatter || state == kShieldStun
                           || state == kDown || state == kCounter || wallTechSoon
                           || v.status == kStatusCaptureThrown || downStatus(v.status)
                           || (state == kTechFall && (slipTechStatus(v.status) || !v.air))
                           || q.state != kCueNone || q.hold
                           || (state == kAfter && (script != NULL || pending != NULL || playSlot >= 0));
        if (recMode != kRecoveryOff && !busyWithLab)
        {
            bool onCliff = cliffStatus(v.status);
            float floorY = 0.0f;
            bool hasFloor = !v.air || groundBetween(f, v.x, v.y + 4.0f, 0.0f, -kRayDown, floorY);
            if (onCliff || (v.air && (!hasFloor || recovering)))
            {
                if (canSpoofWallTech(onCliff, hasFloor, c, now, techFrom))
                {
                    spoofWallTechTrigger(f);
                }
                if (recMode == kRecoveryPPlusAi && main::extraLoaded())
                {
                    if (main::extraPPlusRecovery(who, v, f, out, lastButtons(slot)))
                    {
                        recovering = true;
                        remember();
                        return true;
                    }
                    recovering = false;
                }
                else
                {
                    // Game AI (default, or PPlusAI fallback when extra is absent):
                    // returning false unsilences the slot so Brawl/P+'s native AI
                    // recovers and exits CliffWait cleanly.
                    recovering = true;
                    remember();
                    return false;
                }
            }
        }
        if (recovering)
        {
            recovering = false;
            main::extraPPlusRecoveryReset(slot);
        }

        // A cue that starts has this frame, whatever the state.
        bool playingRec = false;
        if (!cueNow(out, who, q))
        {
            switch (state)
            {
            case kIdle:
            if (!main::extraScenarioCpuStep(who, v, f, out, lastButtons(slot)))
            {
                idle(out, who);
            }
            break;
        case kShieldStun:
            if (v.air || !guardStatus(v.status))
            {
                enterIdle(out, who);
                break;
            }
            out.buttons = kGuard;
            shieldTilt(out);
            if (canAct(v))
            {
                if (c.shieldTiming == kShieldTimingFullMove
                    && main::extraShieldHoldFullMove(who.playerNo, who.entryId, v, false))
                {
                    break;
                }
                state = kCounter;
                counter(out, who);
            }
            break;
        case kGrabbed:
            if (grabbedStatus(v.status))
            {
                grabbed(out, c);
            }
            else if (v.status == kStatusCaptureCut || v.status == kStatusCaptureJump)
            {
                // released: the counter follows once actionable (release counts as a hit)
            }
            else if (prevStatus == kStatusCaptureCut || prevStatus == kStatusCaptureJump)
            {
                hits++;
                shieldKind = false;
                actionable = 0;
                revPicked = false;
                state = kCounter;
                TM_LOG("cpu: P%d released from a grab (%x), hit %d, frame %u\n", who.playerNo + 1, prevStatus, hits,
                       frameOf(slot));
                counter(out, who);
            }
            else if (!newHit)
            {
                state = kAfter;
            }
            break;
        case kClatter:
            if (c.clatterEscape != kMashNone && isClatterStatus(v.status))
            {
                clatter(out, c);
            }
            else
            {
                enterIdle(out, who);
            }
            break;
        case kHit:
            if (v.hitlag > 0)
            {
                out.buttons = trapPress ? kGuard : 0;
                trapPress = false;
                hitlag(out);
                if ((v.air && tumbleStatus(v.status)) || slipTechStatus(v.status))
                {
                    // P+ allows tech presses during hitlag (MoveDef/Misc.asm:714-806).
                    techInput(out, who, false);
                }
                break;
            }
            afterHitlag++;
            if (afterHitlag <= kDiHold)
            {
                out.x = tdiX;
                out.y = tdiY;
            }
            if (downStatus(v.status))
            {
                enterDown(out, who);
            }
            else if (passiveStatus(v.status))
            {
                state = kCounter;
            }
            else if (slipTechStatus(v.status))
            {
                techInput(out, who, afterHitlag > kDiHold);
            }
            else if (canAct(v) || justOut())
            {
                state = kCounter;
                out.x = out.y = 0.0f;
                counter(out, who);
            }
            else if (v.air && tumbleStatus(v.status))
            {
                techInput(out, who, afterHitlag > kDiHold);
            }
            break;
        case kTechFall:
            if (slipTechStatus(v.status))
            {
                techInput(out, who);
            }
            else if (!v.air)
            {
                if (downStatus(v.status))
                {
                    enterDown(out, who);
                }
                else
                {
                    state = passiveStatus(v.status) ? kCounter : kAfter;
                    downFrames = 0;
                    missWait = 0;
                }
            }
            else if (!tumbleStatus(v.status))
            {
                state = kAfter;
            }
            else
            {
                techInput(out, who);
            }
            break;
        case kDown:
            if (!downStatus(v.status))
            {
                actionable = 0;
                revPicked = false;
                state = kCounter;
                counter(out, who);
            }
            else if (!held(out, who))
            {
                getupInput(out, who);
            }
            break;
        case kCounter:
            if (!v.air && downStatus(v.status))
            {
                // Missed tech while waiting out Counter Delay: run Get Up.
                actionable = 0;
                revPicked = false;
                enterDown(out, who);
            }
            else
            {
                counter(out, who);
                if (state == kCounter && v.air && tumbleStatus(v.status))
                {
                    techInput(out, who);
                }
            }
            break;
        case kAfter:
            if (runPlay(out))
            {
                playingRec = true;
                break;
            }
            if (runScript(out))
            {
                break;
            }
            // A press the game did not take is made again once the CPU can
            // act, for up to kMaxWait; meanwhile the CPU is not free, so no
            // recovery starts. Actions no status change shows (kEndsWithSteps)
            // are done when their steps are.
            if (pending != NULL)
            {
                bool shown = (pending->flags & kEndsWithSteps) || started();
                if (shown || ++tries > kMaxWait)
                {
                    pending = NULL;
                    if (q.state == kCueRunning)
                    {
                        q.state = shown ? kCueDone : kCueMissed;
                    }
                }
                else
                {
                    if (canAct(v))
                    {
                        TM_LOG("cpu: P%d counter not taken (status %x), again, frame %u\n", who.playerNo + 1,
                               v.status, frameOf(slot));
                        startScript(pending->steps, true);
                        runScript(out);
                    }
                    break;
                }
            }
            if (q.state == kCueRunning)
            {
                q.state = kCueDone;
            }
            if (canAct(v) && !v.air)
            {
                enterIdle(out, who);
            }
            else if (v.air && tumbleStatus(v.status))
            {
                techInput(out, who);
            }
            else if (!v.air && downStatus(v.status))
            {
                // Missed tech after the counter action: run Get Up.
                enterDown(out, who);
            }
            else if (!v.air && passiveStatus(v.status))
            {
                // Successful tech after an air counter also runs the ground counter.
                state = kCounter;
                actionable = 0;
                revPicked = false;
            }
            break;
            default:
                state = kIdle;
                break;
            }
        }
        if (!playingRec && playSlot < 0 && !q.hold)
        {
            applyAutoLCancel(out, c);
        }
        if ((out.buttons & kGuard) != 0 && (lastButtons(slot) & kGuard) == 0)
        {
            lastPress = now;
        }
        // A hit or a grab took the CPU out of the cue's action.
        if (q.state == kCueRunning && state != kAfter)
        {
            q.state = kCueMissed;
        }

        remember();
        return true;
    }

    // What a savestate keeps besides the brains: the random generator, the
    // tools that act across frames, and Controlled By's saved port/CPU type so
    // a load can restore or re-apply them to match the current setting.
    struct Saved {
        Brain brains[kMaxSlots];
        u32 rng;
        s32 savedFrame;
        bool moving;
        int moverPlayer;
        bool freezeRequest;
        char moveLabel[12];
        char freezeLabel[12];
        Cue cues[kMaxSlots];
        bool portChanged[kMaxSlots];
        s8 savedPort[kMaxSlots];
        u8 savedType[kMaxSlots];
    };
    struct OptionsHeap {
        Brain brains[kMaxSlots];
        Saved saved[2];
        OptionsAux aux;
    };
    static OptionsHeap* s_heap = NULL;
    static Saved* s_saved = NULL;

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void copyBrainState(Brain& dst, const Brain& src)
    {
        const u8* s = reinterpret_cast<const u8*>(&src.slot);
        u32 bytes = sizeof(Brain) - static_cast<u32>(s - reinterpret_cast<const u8*>(&src));
        memcpy(&dst.slot, s, bytes);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    static s32 nextStepFrame()
    {
        return nextTrackerStep(game::trackerFrame(), game::trackerFrameOwed(), game::trackerFramePending());
    }

    void optionsSaved(int which)
    {
        if (s_brains == NULL || s_saved == NULL)
        {
            return;
        }
        Saved& c = s_saved[which & 1];
        for (int i = 0; i < kMaxSlots; i++)
        {
            copyBrainState(c.brains[i], s_brains[i]);
            c.portChanged[i] = s_portChanged[i];
            c.savedPort[i] = s_savedPort[i];
            c.savedType[i] = s_savedType[i];
        }
        c.rng = s_rng;
        c.savedFrame = nextStepFrame();
        c.moving = s_moving;
        c.moverPlayer = s_mover.playerNo;
        c.freezeRequest = s_freezeRequest;
        memcpy(c.moveLabel, g_moveCpuLabel, sizeof(c.moveLabel));
        memcpy(c.freezeLabel, g_freezeCpuLabel, sizeof(c.freezeLabel));
        memcpy(c.cues, s_aux->cues, sizeof(s_aux->cues));
    }

    static inline void applyControlledPort(int i, s8 savedPort, u8 savedType, int ctrlPort, void* input)
    {
        s_savedPort[i] = savedPort;
        s_savedType[i] = savedType;
        s_portChanged[i] = true;
        setControllerOf(i, ctrlPort);
        setCpuType(input, kCpuTypeControl);
    }

    void optionsLoaded(int which)
    {
        if (s_brains == NULL || s_saved == NULL)
        {
            return;
        }
        const Saved& c = s_saved[which & 1];
        const s32 delta = nextStepFrame() - c.savedFrame;
        s_rng = c.rng;
        s_moving = c.moving;
        s_mover.playerNo = c.moverPlayer;
        s_freezeRequest = c.freezeRequest;
        memcpy(g_moveCpuLabel, c.moveLabel, sizeof(c.moveLabel));
        memcpy(g_freezeCpuLabel, c.freezeLabel, sizeof(c.freezeLabel));
        memcpy(s_aux->cues, c.cues, sizeof(s_aux->cues));
        int ctrlPort = opts().control == kControlLab ? opts().controlledBy : 0;
        for (int i = 0; i < kMaxSlots; i++)
        {
            copyBrainState(s_brains[i], c.brains[i]);
            rebaseFrame(s_brains[i].lastPress, delta);
            rebaseFrame(s_brains[i].techFrom, delta);
            Cue& q = s_aux->cues[i];
            rebaseCueFrames(q.state, q.frame, q.start, delta);
            void* input = liveCpuInput(i);
            if (ctrlPort != 0 && input != NULL)
            {
                s8 sp = c.savedPort[i];
                u8 st = c.savedType[i];
                bool apply = c.portChanged[i];
                if (!apply)
                {
                    int curType = cpuType(input);
                    if (curType >= 0 && curType != kCpuTypeControl)
                    {
                        sp = s_origPort[i];
                        st = static_cast<u8>(curType);
                        apply = true;
                    }
                }
                if (apply)
                {
                    applyControlledPort(i, sp, st, ctrlPort, input);
                }
                else
                {
                    s_portChanged[i] = false;
                }
            }
            else if (c.portChanged[i])
            {
                s_savedPort[i] = c.savedPort[i];
                s_savedType[i] = c.savedType[i];
                s_portChanged[i] = true;
                restorePort(i, false);
            }
            else
            {
                s_portChanged[i] = false;
            }
        }
        game::trainingControllerSuppress(game::kControllerOwnerControlledBy, anyPortChanged());
    }

    void setSlotFrame(SlotFrame source)
    {
        s_slotFrame = source;
    }

    // Only the extra module calls it: kept from the linker's stripping.
#pragma force_active on
    bool optionsDi(int slot, float& x, float& y)
    {
        if (s_brains == NULL || slot < 0 || slot >= kMaxSlots || sourceOf(slot) != &s_brains[slot]
            || s_brains[slot].state != kHit)
        {
            return false;
        }
        x = s_brains[slot].tdiX;
        y = s_brains[slot].tdiY;
        return true;
    }

    void setMashChance(int percent)
    {
        s_mashChance = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    }

    static void handHit(int playerNo, bool counter, int hits)
    {
        if (s_aux == NULL)
        {
            return;
        }
        Handed& h = s_aux->handed[playerNo & 3];
        h.frame = game::trackerFrame();
        h.counter = counter;
        h.hits = hits;
    }

    bool playbackCounterHit(int playerNo, int hitlag, int hitstun, int status)
    {
        if (s_aux == NULL || playerNo < 0 || playerNo >= kMaxSlots)
        {
            return false;
        }
        PlaybackSeen& p = s_aux->playbackSeen[playerNo];
        s32 now = game::trackerFrame();
        // A new playback (no call on the step before) counts from 0.
        bool havePrev = p.frame != game::kNoFrame && now - p.frame == 1;
        if (!havePrev)
        {
            p.hits = 0;
        }
        bool grabbed = grabbedStatus(status);
        bool newHit = havePrev && !grabbed && hitstun > 0 && (hitlag > p.hitlag || hitstun > p.hitstun);
        bool newGrab = grabbed && (!havePrev || !grabbedStatus(p.status));
        p.frame = now;
        p.hitlag = hitlag;
        p.hitstun = hitstun;
        p.status = status;
        if (!newHit && !newGrab)
        {
            return false;
        }
        p.hits++;
        // Continue playback through a hit whose counter is disabled.
        if (p.hits <= kAdvancedHits && advancedHit(p.hits).logic == kLogicDisable)
        {
            TM_LOG("cpu: P%d playback hit %d: counter disabled, playback goes on\n", playerNo + 1, p.hits);
            return false;
        }
        if (newHit)
        {
            handHit(playerNo, true, p.hits);
        }
        return true;
    }

    void playbackHitHanded(int playerNo)
    {
        if (playerNo >= 0 && playerNo < kMaxSlots)
        {
            handHit(playerNo, false, 0);
        }
    }
#pragma force_active reset

    void optionsAttach()
    {
        if (s_heap == NULL)
        {
            void* p = game::allocNetworkZero(static_cast<u32>(sizeof(OptionsHeap)));
            if (p != NULL)
            {
                s_heap = static_cast<OptionsHeap*>(p);
                // Placement-construct each Brain over zeroed memory so MWCC
                // writes only the Source/Brain vtable pointer without
                // copying uninitialized stack bytes over fields reset() skips.
                for (int i = 0; i < kMaxSlots; i++)
                {
                    new (&s_heap->brains[i]) Brain;
                    new (&s_heap->saved[0].brains[i]) Brain;
                    new (&s_heap->saved[1].brains[i]) Brain;
                }
                s_brains = s_heap->brains;
                s_saved = s_heap->saved;
                s_aux = &s_heap->aux;
            }
        }
        s_moving = false;
        s_freezeRequest = false;
        g_moveCpuLabel[0] = g_freezeCpuLabel[0] = '\0';
        for (int i = 0; i < kMaxSlots; i++)
        {
            s_portChanged[i] = false;
            s_origPort[i] = static_cast<s8>(controllerOf(i));
        }
        if (s_brains == NULL)
        {
            TM_LOG("cpu: options heap alloc failed (%u bytes in Network)\n",
                   static_cast<unsigned>(sizeof(OptionsHeap)));
            return;
        }
        for (int i = 0; i < kMaxSlots; i++)
        {
            s_brains[i].slot = i;
            s_brains[i].player = -1;
            s_brains[i].noReaction = false;
            s_brains[i].revAction = 0;
            s_brains[i].revDelay = 0;
            s_brains[i].reset();
            s_aux->handed[i].frame = game::kNoFrame;
            s_aux->playbackSeen[i].frame = game::kNoFrame;
        }
        memset(s_aux->cues, 0, sizeof(s_aux->cues));
        memset(&s_aux->cueTest, 0, sizeof(s_aux->cueTest));
        s_aux->cueTest.magic = kCueTestMagic;
        s_aux->cueTest.playerNo = -1;
        TM_LOG("cpu: cue test block at %08x\n", &s_aux->cueTest);
        s_rng = game::frameCounter() | 1;
        advancedAttach();
        optionsSaved(0);
        optionsSaved(1);
    }

    void optionsDetach()
    {
        for (int i = 0; i < kMaxSlots; i++)
        {
            restorePort(i, false);
        }
        game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
        s_moving = false;
        s_freezeRequest = false;
        if (s_heap != NULL)
        {
            free(s_heap);
            s_heap = NULL;
            s_brains = NULL;
            s_saved = NULL;
            s_aux = NULL;
        }
    }

    static void clampIncompatibleCounter(u8& index, int list, int ftKind)
    {
        const Action* a = action(list, index);
        if (a != NULL && (a->flags & (kForDjc | kForFloat)) != 0 && !fitsKind(a, ftKind))
        {
            index = 0;
        }
    }

    // Called from extra/fighter_swap.cpp before triggering a mid-match swap.
#pragma force_active on
    void optionsPrepareSwap(bool cpuSwapped, int newCpuFtKind)
    {
        for (int i = 0; i < kMaxSlots; i++)
        {
            if (controlled(i))
            {
                release(i, "fighter swap");
            }
            if (s_brains != NULL)
            {
                s_brains[i].reset();
                s_brains[i].player = -1;
                s_brains[i].noReaction = false;
            }
            restorePort(i, false);
            if (s_aux != NULL)
            {
                s_aux->handed[i].frame = game::kNoFrame;
                s_aux->playbackSeen[i].frame = game::kNoFrame;
            }
        }
        game::trainingControllerSuppress(game::kControllerOwnerControlledBy, false);
        s_moving = false;
        s_mover.playerNo = -1;
        s_moveX = s_moveY = 0.0f;
        s_freezeRequest = false;
        g_moveCpuLabel[0] = g_freezeCpuLabel[0] = '\0';
        if (s_aux != NULL)
        {
            memset(s_aux->cues, 0, sizeof(s_aux->cues));
        }
        if (cpuSwapped)
        {
            CpuOptions& mutableOpts = main::settings().cpu.options;
            clampIncompatibleCounter(mutableOpts.counterGround, kListGround, newCpuFtKind);
            clampIncompatibleCounter(mutableOpts.counterShield, kListShield, newCpuFtKind);
            clampIncompatibleCounter(mutableOpts.revSlot2Action[kRevOnHit], kListGround, newCpuFtKind);
            clampIncompatibleCounter(mutableOpts.revSlot2Action[kRevOnShield], kListShield, newCpuFtKind);
            CpuTables& tables = main::settings().cpuTables;
            for (int h = 0; h < kAdvancedHits; h++)
            {
                clampIncompatibleCounter(tables.hits[h].action[kListGround], kListGround, newCpuFtKind);
                clampIncompatibleCounter(tables.hits[h].action[kListShield], kListShield, newCpuFtKind);
            }
            advancedHitChanged();
        }
    }

    void optionsResetSlot(int playerNo)
    {
        if (playerNo < 0 || playerNo >= kMaxSlots)
        {
            return;
        }
        if (s_brains != NULL)
        {
            for (int i = 0; i < kMaxSlots; i++)
            {
                if (i == playerNo || s_brains[i].player == playerNo)
                {
                    s_brains[i].reset();
                    s_brains[i].revAction = 0;
                    s_brains[i].revDelay = 0;
                }
            }
        }
        if (s_aux != NULL)
        {
            s_aux->handed[playerNo].frame = game::kNoFrame;
            s_aux->playbackSeen[playerNo].frame = game::kNoFrame;
            memset(&s_aux->cues[playerNo], 0, sizeof(s_aux->cues[playerNo]));
        }
    }

    void optionsResetAll()
    {
        for (int i = 0; i < kMaxSlots; i++)
        {
            optionsResetSlot(i);
        }
    }
#pragma force_active reset

    void optionsClaim(const Owner& who)
    {
        if (who.slot < 0 || who.slot >= kMaxSlots || opts().control != kControlLab)
        {
            return;
        }
        if (humanPlayer(who.playerNo))
        {
            if (s_moving && !controlled(who.slot))
            {
                if (s_mover.playerNo < 0)
                {
                    s_mover.playerNo = who.playerNo;
                }
                if (s_mover.playerNo == who.playerNo)
                {
                    take(who, &s_mover, 0);
                }
            }
            return;
        }
        if (s_brains == NULL || !labControl(who) || !who.cpu)
        {
            return;
        }
        Brain& b = s_brains[who.slot];
        b.reset();
        take(who, &b, kSilenceAi | kWithNana);
    }
}
}

