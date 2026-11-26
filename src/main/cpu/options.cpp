// The CPU Options' state machine (options.h), with Move CPU, Freeze CPU,
// Controlled By and the cues and holds of cue.h. Changes a driven CPU's
// inputs, controller port and CPU type (Controlled By), motion rate
// (Freeze), position (Move), shield health and intangibility.
#include "main/cpu/options.h"

#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "main/cpu/actions.h"
#include "main/cpu/clatter_logic.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/cue.h"
#include "main/cpu/di_logic.h"
#include "main/cpu/driver.h"
#include "main/cpu/lab_data.h"
#include "main/cpu/ledge_logic.h"
#include "main/cpu/sdi_logic.h"
#include "main/cpu/shffl_core.h"
#include "main/cpu/tech_logic.h"
#include "main/cpu/view.h"
#include "main/display/display.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/match.h"
#include "main/game/status_ids.h"
#include "main/game/tech_frames.h"
#include "main/game/tracker.h"
#include "main/game/training_controller.h"
#include "main/record/record.h"
#include "main/settings.h"
#include "shared/game/frame.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/stick.h"
#include "shared/log.h"

namespace tm {
namespace cpu {
    using namespace button;

    enum State {
        kIdle,       // the Behavior option
        kHit,        // hitlag (smash DI, then trajectory DI), then hitstun
        kShieldStun, // hit on shield: guard held until the stun ends
        kDown,       // missed tech: the Get Up option
        kCounter,    // counting actionable frames, then the counter action
        kAfter,      // the counter's script, then back to idle once grounded
        kTechFall,   // no air counter: falling in tumble, tech on landing
        kGrabbed,    // held: Grab Escape, then a counter after the release
        kClatter     // non-grab clatter (Bury/Ice/FuraFura/Sleep): Clatter Escape
    };

    enum {
        kStatusDown = 0x4A,
        kStatusDownWait = 0x4D,
        kStatusDownWaitContinue = 0x4E,
        kStatusLayDown = 0x50,
        kStatusDownStandAttack = 0x53,
        kStatusDamage = 0x43,
        kStatusDamageFly = 0x45,
        kStatusDamageFall = 0x49,
        kStatusCapturePulled = 0x3D,
        kStatusCaptureDamage = 0x3F,
        kStatusCaptureCut = 0x40,
        kStatusCaptureJump = 0x41,
        kStatusCaptureThrown = 0x42,
        kStatusPassive = 0x60,
        kStatusPassiveFb = 0x61,
        kStatusFall = 0x0E,
        kStatusCliffFirst = 0x73,
        kStatusCliffWait = 0x75,
        kStatusCliffLast = 0x7B,
        // Tech window, leads and gap (params 23052/23053, not checked here):
        // docs/internals.md, "CPU tech timing".
        kTechLead = 5,
        kTechLeadEarly = 15,
        kTechGap = 41,
        // Tech trap window inside P+'s 40-frame tech lockout.
        kTrapEarly = 24,
        // Frames after hitlag the trajectory DI stick is still held.
        kDiHold = 3,
        kMaxWait = 30,
        // Shield tilt ramp: a stick pushed in one frame, or over 6, rolls or
        // spotdodges; over 8 it tilts (observed; character not recorded).
        kTiltRamp = 12,
        // Missed tech ground wait before re-rolling (0.3 s).
        kMissTechWait = 18,
        // Grab-escape mash frequency tiers, percent of frames.
        kMashChanceMedium = 40,
        kMashChanceHigh = 65,
        // Perfect Shield: guard hold frames when no reflect/hit arrives.
        kPowershieldHold = 6,
        kRayDown = 400,
        kRecordSlots = 6,
        kMaxFlight = 150, // frames of flight Survival looks at
        kTechSoundId = 0 // snd_se_system_cursor
    };

    static const float kMoveSpeed = 1.6f;    // Move CPU speed per frame
    static const float kFastfallJump = 0.3f; // fall speed gained in one frame by a fastfall

    enum {
        // Common float parameter p at kParams + 4p: id 3233 is 0x80B88440, P+'s
        // Max Shield Strength (Source/Injects/Fighter/Constants.asm:36-37).
        kParams = 0x80B851BC,
        kParamDiAngle = 3290,
        kParamDecay = 3294,
        kParamRollStick = 3198,
        kParamWiggleStick = 3299
    };

    static float param(int id, float fallback)
    {
        float v = *reinterpret_cast<const float*>(kParams + 4 * id);
        return v > 0.0f && v < 2.0f ? v : fallback;
    }

    static float lesser(float a, float b)
    {
        return a < b ? a : b;
    }

    static float sign(float v)
    {
        return v < 0.0f ? -1.0f : 1.0f;
    }

    static inline float length(float x, float y)
    {
        return game::stickRoot(x * x + y * y);
    }

    static const CpuOptions& opts()
    {
        return main::settings().cpu.options;
    }

    // Driven while CPU Control is Lab and Training's own CPU option is not
    // Control (a controller drives the CPU then).
    static bool s_portChanged[kMaxSlots];

    static bool labControl(const Owner& who)
    {
        // Controlled By sets the type Training's Control option sets (it is
        // ours then, not Training's).
        bool ours = who.playerNo >= 0 && who.playerNo < kMaxSlots && s_portChanged[who.playerNo];
        return opts().control == kControlLab && (!who.trainingControl || ours);
    }

    static u32 s_rng = 1;
    static SlotFrame s_slotFrame = NULL;
    static int s_mashChance = 0; // setMashChance

    // A hit the recording hands over as a CPU playback ends, by player; the
    // Brain takes it the next frame, a stale one is dropped.
    struct Handed {
        s32 frame;
        bool counter;
        int hits;
    };
    enum { kHandFrames = 2 };
    // playbackCounterHit's view of the CPU on the step before, by player.
    struct PlaybackSeen {
        s32 frame;
        int hitlag, hitstun, status;
        int hits;
    };

    // xorshift32 stirred with the frame counter, so a replayed savestate does
    // not repeat its choices; the game's own generators are never touched.
    static u32 rnd()
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

    static int rndBelow(int n)
    {
        return n > 0 ? static_cast<int>(rnd() % static_cast<u32>(n)) : 0;
    }

    // Index 0..3 picked with the given weights (all 0: uniform).
    static int weighted(const u8* w)
    {
        int total = w[0] + w[1] + w[2] + w[3];
        if (total == 0)
        {
            return rndBelow(4);
        }
        int r = rndBelow(total);
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

    static bool downStatus(int s)
    {
        return isDownStatus(s);
    }

    static bool slipTechStatus(int s)
    {
        return isSlipTechStatus(s);
    }

    static bool passiveStatus(int s)
    {
        return s == kStatusPassive || s == kStatusPassiveFb;
    }

    static bool tumbleStatus(int s)
    {
        return s >= kStatusDamageFly && s <= kStatusDamageFall;
    }

    // Held by an opponent: the statuses P+'s grab code treats as grabbed
    // (ProjectM/Grab.asm: grabs, command grabs, inhales, eggs).
    static bool grabbedStatus(int s)
    {
        return (s >= kStatusCapturePulled && s <= kStatusCaptureDamage)
               || (s >= 0xCC && s <= 0xDB && s != 0xD5 && s != 0xD8) || (s >= 0xE6 && s <= 0xEB) || s == 0xEE
               || s == 0xFA || (s >= 0x102 && s <= 0x10A);
    }

    static bool cliffStatus(int s)
    {
        return s >= kStatusCliffFirst && s <= kStatusCliffLast;
    }

    static bool guardStatus(int s)
    {
        return s == game::kStatusGuardOn || s == game::kStatusGuard || s == game::kStatusGuardDamage;
    }

    // LA-Basic[90] (0x1000005A): P+'s 7-frame L-Cancel window (Source/Project+/L-Cancel.asm:49,98,146,188).
    static int readLCancelTimer(Fighter* fighter)
    {
        enum { kLCancelTimer = 0x1000005A };
        const soModuleEnumeration* m = game::fighterModules(fighter);
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return 0;
        }
        return m->m_workManageModule->getInt(kLCancelTimer);
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

    static void restorePort(int playerNo, bool updatePatch = true)
    {
        if (playerNo >= 0 && playerNo < kMaxSlots && s_portChanged[playerNo])
        {
            setControllerOf(playerNo, s_savedPort[playerNo]);
            void* input = liveCpuInput(playerNo);
            if (input != NULL)
            {
                setCpuType(input, s_savedType[playerNo]);
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

    struct Cue {
        u8 state;   // CueState
        s8 list;
        u8 index;
        bool hold;
        bool repeat;
        u8 holdRef;
        u16 holdButtons;
        s32 frame;  // kCueAsap or the frame to start on
        s32 start;  // the frame its first input was written in
        float holdX, holdY;
        const Action* custom; // cueAction's own action (NULL: the list's)
    };

    // Test hook (scripts/cpu_tests.py), read each frame the options drive that
    // player: a new generation runs `op` once.
    struct CueTestBlock {
        u32 magic;      // 0x00 kCueTestMagic
        u32 generation; // 0x04
        s32 playerNo;   // 0x08
        u32 op;         // 0x0C CueTestOp
        s32 list;       // 0x10
        s32 index;      // 0x14
        s32 delay;      // 0x18 0: as soon as it can; n: the nth frame from the one the op runs in
        u32 buttons;    // 0x1C hold
        float x, y;     // 0x20 hold
        u32 ref;        // 0x28 hold
        // Written by the module:
        u32 seen;       // 0x2C the generation that ran
        s32 opFrame;    // 0x30 the frame it ran in
        s32 state;      // 0x34 cueState
        s32 start;      // 0x38 cueStart
    };
    enum { kCueTestMagic = 0x544D4355 }; // "TMCU"
    enum CueTestOp { kOpCue = 1, kOpCancel, kOpHold, kOpHoldRepeat, kOpRelease };

    struct OptionsAux {
        Handed handed[kMaxSlots];
        PlaybackSeen playbackSeen[kMaxSlots];
        Cue cues[kMaxSlots];
        CueTestBlock cueTest;
    };
    static OptionsAux* s_aux = NULL;

    static Cue* cueOf(int playerNo)
    {
        return s_aux != NULL && playerNo >= 0 && playerNo < kMaxSlots ? &s_aux->cues[playerNo] : NULL;
    }

    static void cueTest(int playerNo)
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
        s32 now = game::trackerFrame();
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

    // ---- the state machine ------------------------------------------------------

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    class Brain : public Source {
    public:
        static void* operator new(size_t, void* p)
        {
            return p;
        }
        static void operator delete(void*, void*) {}

        int slot;
        int player; // the player driven last
        bool noReaction; // set on the CPU: undone when the option goes off (not by reset)
        u8 revAction;
        u8 revDelay;
        int state;
        bool shieldKind; // the counter follows a hit on shield
        int hits;        // hits taken since the CPU was last idle
        bool havePrev;
        int prevHitlag, prevHitstun, prevStatus;
        bool prevDamageHitlag;
        float prevVy;
        bool prevAir;
        // trajectory, smash and automatic smash DI for the current hit
        float tdiX, tdiY, sdiX, sdiY, asdiX, asdiY;
        bool haveAsdi;
        int diKind;   // the Tdi the hit's trajectory DI follows (Random resolved)
        u32 diRoll;   // per-hit RNG seed for Slight Random DI
        int sdiDone, hitlagFrame, afterHitlag;
        // teching
        int tech;
        bool techChosen, techPressed;
        u32 preTechRoll;
        float groundY, gravity;
        bool groundKnown;
        s32 lastPress;    // the frame of the CPU's last guard press (any)
        s32 techFrom;     // no tech press before this frame (Simulate Tech Trap)
        bool trapPress;   // Simulate Tech Trap Latest: press guard now
        bool noTech;  // the air counter was Wait
        // getting up
        int getup;
        int downFrames;
        int missWait;
        // countering
        int actionable;
        bool counterAir;
        // the script being played
        const Step* script;
        int playSlot; // Play Slot N: the recording slot played (-1: none)
        u32 playFrame;
        const Action* pending; // the counter action until it has started
        int tries;
        int stepFrames;
        bool scriptHeld; // a release frame was put in front of it
        bool scriptAirStarted;
        Step shfflBuf[kMaxShfflSteps];
        float randomSide;
        // shield
        int tiltFrames;
        int powershield; // frames guard has been held for a perfect shield
        // grabs
        bool mashFlip;
        // Freeze CPU
        bool frozen;
        int frozenStatus;
        // Move CPU
        bool placed;
        bool revPicked;
        float moveX, moveY;
        bool techSounded;
        bool recovering;
        LedgeState ledgeState;

        void reset();

        virtual bool wants(const Owner& who)
        {
            return labControl(who);
        }

        virtual void released(const Owner& who)
        {
            noReactionOn(who, false);
            restorePort(who.playerNo);
            unfreeze(who);
            reset();
        }

        virtual bool next(const Owner& who, const Frame& produced, Frame& out);

    private:
        // Intangibility (NoReaction): hits connect with no knockback or grab
        // reaction; its 0% damage lock lives in main/settings.cpp.
        void noReactionOn(const Owner& who, bool on)
        {
            if (on || noReaction)
            {
                noReaction = on;
                setNoReaction(fighterOf(who.entryId, false), on);
                setNoReaction(fighterOf(who.entryId, true), on);
            }
        }

        float toward; // +1 if the opponent is to the right
        float oppDist;
        View v;
        Fighter* f;

        void stick(Frame& out, float x, float y, int ref)
        {
            switch (ref)
            {
            case kRefToward:
                x *= toward;
                break;
            case kRefForward:
                x *= v.lr;
                break;
            case kRefAway:
                x *= -toward;
                break;
            case kRefRandom:
                x *= randomSide;
                break;
            default:
                break;
            }
            out.x = x;
            out.y = y;
        }

        void enterIdle(Frame& out, const Owner& who)
        {
            state = kIdle;
            hits = 0;
            shieldKind = false;
            noTech = false;
            idle(out, who);
        }

        void enterDown(Frame& out, const Owner& who)
        {
            state = kDown;
            downFrames = 0;
            missWait = 0;
            getupInput(out, who);
        }

        void clearAction(int nextState)
        {
            state = nextState;
            shieldKind = false;
            script = NULL;
            pending = NULL;
        }
        void startScript(const Step* steps, bool again = false);

        bool reached(int until) const
        {
            switch (until)
            {
            case kUntilAir:
                return v.air;
            case kUntilTakeoff:
                return takeoffNext(v) || v.air;
            case kUntilDash:
                return v.status == game::kStatusDash || v.status == game::kStatusRun;
            case kUntilFalling:
                // Falling in the coming frame; frozen in hitlag (or the first
                // frame out of it, while posture pos - prevPos is still 0) is
                // not falling. Fastfall conditions (Jigglypuff):
                // docs/internals.md, "Fastfall conditions".
                return v.air && v.hitlag == 0 && prevHitlag == 0 && v.vy - gravity < 0.0f;
            case kUntilJumped:
                return isJumpStatus(v.status);
            case kUntilSpecial:
                return v.status >= game::kStatusSpecialN;
            case kUntilFastfall:
                return v.air && havePrev && prevVy - v.vy > kFastfallJump;
            case kUntilCanAct:
                // or Guard_Off's last frame: its animation ends with it
                return canAct(v) || (v.status == game::kStatusGuardOff && animationEndsNext(v));
            default:
                return false;
            }
        }

        // Writes the recording's frame; false once it has ended.
        bool runPlay(Frame& out);

        // Play Random Slot: a recorded slot weighted by the slot chances; -1 if none.
        int randomSlot()
        {
            Frame unused;
            bool recorded[kRecordSlots];
            int total = 0, first = -1;
            const u8* chance = main::settings().recording.cpuChance;
            for (int i = 0; i < kRecordSlots; i++)
            {
                recorded[i] = s_slotFrame != NULL && s_slotFrame(i, 0, unused);
                if (recorded[i])
                {
                    total += chance[i];
                    first = first < 0 ? i : first;
                }
            }
            if (total == 0)
            {
                return first;
            }
            int r = rndBelow(total);
            for (int i = 0; i < kRecordSlots; i++)
            {
                if (!recorded[i])
                {
                    continue;
                }
                if (r < chance[i])
                {
                    return i;
                }
                r -= chance[i];
            }
            return first;
        }

        // Writes the script's frame; false once it has ended.
        bool runScript(Frame& out);

        // Survival: the full turn (+1 counter-clockwise, -1 clockwise) whose
        // flight, followed as the DI Display does, keeps farther from the blast
        // zones until hitstun ends; on a tie or with no blast zones, up.
        float survivalTurn(float kx, float ky, float len) const
        {
            float up = kx >= 0.0f ? 1.0f : -1.0f;
            void* camera = display::cameraController();
            if (camera == NULL)
            {
                return up;
            }
            const float* z = reinterpret_cast<const float*>(reinterpret_cast<u32>(camera) + 0x158);
            // The DI angle's sine and cosine (series to the 5th power: within
            // 1e-6 at 18 degrees), and the knockback decay (parameter 3294).
            float a = param(kParamDiAngle, 0.31416f), a2 = a * a;
            Flight fl = {z, -gravity, 3.0f, a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f)),
                         1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f), param(kParamDecay, 0.051f)};
            float gy = 0.0f;
            fallSpeeds(f, fl.accel, fl.maxFall, gy);
            return flight(fl, kx, ky, len, -up) > flight(fl, kx, ky, len, up) + 0.01f ? -up : up;
        }

        struct Flight {
            const float* zones;
            float accel, maxFall;
            float sin, cos, decay;
        };

        // Smallest blast zone distance (negative past one) along the flight of
        // launch (kx, ky) turned by `turn`.
        float flight(const Flight& fl, float kx, float ky, float len, float turn) const
        {
            const float* z = fl.zones;
            float t = turn * fl.sin;
            float dx = (kx * fl.cos - ky * t) / len, dy = (ky * fl.cos + kx * t) / len;
            float x = v.x, y = v.y, g = 0.0f, m = 100000.0f;
            for (int i = 0; i < v.hitstun && i < kMaxFlight; i++)
            {
                len = len > fl.decay ? len - fl.decay : 0.0f;
                g = g + fl.accel < -fl.maxFall ? -fl.maxFall : g + fl.accel;
                x += dx * len;
                y += dy * len + g;
                m = lesser(lesser(m, lesser(z[1] - x, x - z[0])), lesser(z[2] - y, y - z[3]));
            }
            return m;
        }

        // Trajectory DI from the launch vector (read from knockback energy
        // before DI is applied) for all 9 Tdi modes.
        void relativeDi()
        {
            if (diKind == kTdiNone)
            {
                tdiX = tdiY = 0.0f;
                return;
            }
            float kx = 0.0f, ky = 0.0f;
            if (!knockback(f, kx, ky) || (kx == 0.0f && ky == 0.0f))
            {
                kx = -toward;
                ky = 1.0f;
            }
            float len = length(kx, ky);
            float ux = kx / len, uy = ky / len;
            float st = diKind == kTdiSurvival ? survivalTurn(kx, ky, len) : 1.0f;
            const CpuTables& t = main::settings().cpuTables;
            computeTdiStick(diKind, ux, uy, toward, st, diRoll,
                            game::i2fScale(t.customDiX, 0.01f),
                            game::i2fScale(t.customDiY, 0.01f), tdiX, tdiY);
        }

        static bool dirStick(int code, float away, float& x, float& y)
        {
            // code 0..5: Away, Towards, Left, Right, Up, Down
            if (static_cast<u32>(code) >= 6u)
            {
                return false;
            }
            static const s8 kVec[6][2] = {{2, 0}, {-2, 0}, {-1, 0}, {1, 0}, {0, 1}, {0, -1}};
            int vx = kVec[code][0];
            x = vx == 2 ? away : (vx == -2 ? -away : game::i2f(vx));
            y = game::i2f(kVec[code][1]);
            return true;
        }

        static void stepKb(float& kx, float& ky, float& kl, float decay, float& gy, float accel, float maxFall)
        {
            float k = kl > decay ? (kl - decay) / kl : 0.0f;
            kl *= k;
            kx *= k;
            ky *= k;
            gy = gy + accel < -maxFall ? -maxFall : gy + accel;
        }

        static void setBodiesMotion(int entryId, Fighter* main, float rate)
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

        void chooseDi(const Owner&)
        {
            const CpuOptions& c = opts();
            int kind = c.tdi;
            if (kind == kTdiRandom)
            {
                kind = resolveRandomTdi(rnd());
            }
            float away = -toward;
            diKind = kind;
            diRoll = rnd();
            relativeDi();
            // ASDI enum: 1=Away, 2=Towards, 3=Left, 4=Right, 5=Up, 6=Down
            haveAsdi = dirStick(c.asdi - kAsdiAway, away, asdiX, asdiY);
            const float d = 0.7071f;
            static const float kDirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {d, d}, {-d, d}, {d, -d}, {-d, -d}};
            int sdi = c.sdiDir;
            if (sdi == kSdiRandom)
            {
                int i = v.air ? rndBelow(8) : rndBelow(2);
                sdiX = kDirs[i][0];
                sdiY = kDirs[i][1];
                return;
            }
            if (sdi == kSdiAuto)
            {
                sdiX = tdiX;
                sdiY = tdiY;
                return;
            }
            sdiX = sdiY = 0.0f;
            // SDI enum: 2=Away, 3=Towards, 4=Left, 5=Right, 6=Up, 7=Down
            dirStick(sdi - kSdiAway, away, sdiX, sdiY);
        }

        // Smash DI: single-stick pushes every other hitlag frame; Dual-Stick
        // SDI alternates primary and secondary vectors on every hitlag frame
        // (hitlag > 1) up to sdiAmount. The last hitlag frame gets ASDI with
        // holdStill, the next gets trajectory DI:
        // docs/internals.md, "When P+ reads ASDI and trajectory DI".
        void hitlag(Frame& out)
        {
            const CpuOptions& c = opts();
            // Read again each hitlag frame: the launch speed is there before
            // DI, and smash DI moves the fighter (Survival's lines).
            relativeDi();
            HitlagSdiInput in;
            in.dualStick = c.dualStickSdi != 0;
            in.grounded = !v.air;
            in.hitlag = v.hitlag;
            in.hitlagFrame = hitlagFrame;
            in.sdiDone = sdiDone;
            in.sdiAmount = c.sdiAmount;
            in.sdiX = sdiX;
            in.sdiY = sdiY;
            in.haveAsdi = haveAsdi;
            in.asdiX = asdiX;
            in.asdiY = asdiY;
            in.tdiX = tdiX;
            in.tdiY = tdiY;
            in.awayDir = -toward;
            HitlagSdiOutput res = stepHitlagSdi(in);
            out.x = res.x;
            out.y = res.y;
            if (res.holdStill)
            {
                holdStill(slot);
            }
            if (res.consumedSdi)
            {
                sdiDone++;
            }
            hitlagFrame++;
        }

        void chooseTech(const Owner& who);

        // Frames until the fighter lands, if within `lead`; -1 otherwise.
        // `fallbackGround`: when false, only real ground rays count (no
        // phantom `groundY` plane offstage). docs/internals.md, "CPU tech timing".
        int framesToLand(int lead = kTechLead, float lift = 0.0f, bool fallbackGround = true)
        {
            float x = v.x, y = v.y, vx = v.vx, vy = v.vy;
            // At the fall speed cap the speed no longer grows.
            const float dv = v.vy - prevVy;
            const bool capped = havePrev && prevAir && v.vy < 0.0f && dv < 0.005f && dv > -0.005f;
            const float g = capped ? 0.0f : gravity;
            // In a damage status, the flight followed as the DI Display does.
            float kx = 0.0f, ky = 0.0f, accel = 0.0f, maxFall = 0.0f, gy = 0.0f;
            const bool flying = v.status >= kStatusDamage && v.status <= kStatusDamageFall
                                && fallSpeeds(f, accel, maxFall, gy) && knockback(f, kx, ky);
            float kl = length(kx, ky);
            const float decay = param(kParamDecay, 0.051f);
            for (int i = 0; i <= lead; i++)
            {
                if (flying)
                {
                    stepKb(kx, ky, kl, decay, gy, accel, maxFall);
                    vx = kx;
                    vy = ky + gy;
                }
                else
                {
                    vy -= g;
                }
                float nx = x + vx, ny = y + vy;
                if (vy < 0.0f)
                {
                    float ground = 0.0f;
                    // Cast straight down from the collision shape's bottom (`lift` up).
                    const float b = y + lift;
                    float rayDx = 0.0f, rayDy = 0.0f;
                    groundRayDir(static_cast<float>(kRayDown), rayDx, rayDy);
                    bool found = groundBetween(f, nx, b, rayDx, rayDy, ground) && ground <= b + 0.5f;
                    if (!found && fallbackGround && groundKnown)
                    {
                        ground = groundY;
                        found = true;
                    }
                    // Not when already under it (Damage_Air sinks below).
                    if (found && ny + lift <= ground && y + lift > ground)
                    {
                        return i;
                    }
                }
                x = nx;
                y = ny;
            }
            return -1;
        }

        // Frames until the fighter meets a solid wall or ceiling along its
        // damage flight, if within `lead`; -1 otherwise.
        int framesToWall(int lead, float lift)
        {
            float kx = 0.0f, ky = 0.0f, accel = 0.0f, maxFall = 0.0f, gy = 0.0f;
            if (!(v.status >= kStatusDamageFly && v.status <= kStatusDamageFall)
                || !fallSpeeds(f, accel, maxFall, gy) || !knockback(f, kx, ky))
            {
                return -1;
            }
            float kl = length(kx, ky);
            if (kl < 0.5f)
            {
                return -1;
            }
            const float decay = param(kParamDecay, 0.051f);
            float x = v.x, y = v.y;
            for (int i = 0; i <= lead; i++)
            {
                stepKb(kx, ky, kl, decay, gy, accel, maxFall);
                float vx = kx;
                float vy = ky + gy;
                // Cast from body centre with a small ECB half-width margin.
                float padX = vx > 0.0f ? 4.0f : (vx < 0.0f ? -4.0f : 0.0f);
                if (wallOrCeilBetween(x, y + lift + 4.0f, vx + padX, vy))
                {
                    return i;
                }
                x += vx;
                y += vy;
            }
            return -1;
        }

        // Collision shape's bottom over the position; 0 outside 0 to 10.
        float ecbLift() const
        {
            const soModuleEnumeration* m = game::fighterModules(f);
            float lift = 0.0f;
            if (m != NULL && game::isRam(m->m_groundModule))
            {
                lift = m->m_groundModule->getDownPos(0).m_y - v.y;
            }
            return lift > 0.0f && lift < 10.0f ? lift : 0.0f;
        }

        void techInput(Frame& out, const Owner& who, bool allowStick = true);

        bool pickMultiRev(int trigger, int entryId)
        {
            revAction = 0;
            int ra, rd;
            if (opts().multiReversal == 0 || !main::extraReversalPick(trigger, fighterKind(entryId), rnd(), ra, rd))
            {
                return false;
            }
            revAction = static_cast<u8>(ra);
            revDelay = static_cast<u8>(rd);
            return true;
        }

        void getupInput(Frame& out, const Owner& who);

        // Shield Angle: the stick pushed in over kTiltRamp frames once the
        // shield is up.
        void shieldTilt(Frame& out)
        {
            int angle = opts().shieldAngle;
            if (angle == kAngleNeutral)
            {
                return;
            }
            bool up = v.status == game::kStatusGuard || v.status == game::kStatusGuardDamage;
            if (!up)
            {
                tiltFrames = 0;
                return;
            }
            if (tiltFrames < kTiltRamp)
            {
                tiltFrames++;
            }
            float k = game::i2fScale(tiltFrames, 1.0f / kTiltRamp);
            switch (angle)
            {
            case kAngleUp:
                out.y = k;
                break;
            case kAngleDown:
                out.y = -k;
                break;
            case kAngleTowards:
                out.x = k * toward;
                break;
            default:
                out.x = -k * toward;
                break;
            }
        }

        // The hold of cue.h, if one is set.
        bool held(Frame& out, const Owner& who)
        {
            const Cue& q = s_aux->cues[who.playerNo & 3];
            if (q.hold)
            {
                out.buttons = q.repeat && (lastButtons(slot) & q.holdButtons) != 0 ? 0 : q.holdButtons;
                stick(out, q.holdX, q.holdY, q.holdRef);
            }
            return q.hold;
        }

        void idle(Frame& out, const Owner& who);

        // Resolves the counter action and delay from the base rows or the
        // Advanced Counter hit entry; false if that hit's counter is disabled.
        bool counterChoice(int list, int& index, int& delay) const
        {
            const CpuOptions& c = opts();
            index = list == kListShield ? c.counterShield : (list == kListAir ? c.counterAir : c.counterGround);
            delay = c.counterDelay;
            if (hits < 1 || hits > kAdvancedHits || actionKind(list, index) == kActionRandomAdvanced)
            {
                return true;
            }
            const AdvancedHit& h = advancedHit(hits);
            if (h.logic == kLogicDisable)
            {
                return false;
            }
            if (h.logic == kLogicCustom)
            {
                index = h.action[list];
                delay = h.delay[list];
            }
            return true;
        }

        // Random, Random Advanced, and Custom Pool picked once per counter.
        int resolve(int list, int index, int ftKind)
        {
            int kind = actionKind(list, index);
            if (kind == kActionRandom)
            {
                return randomAction(list, rnd());
            }
            if (kind == kActionCustomPool)
            {
                return randomPoolAction(list, main::settings().cpuTables.poolBits[list], ftKind, rnd());
            }
            if (kind == kActionRandomAdvanced)
            {
                u8 picks[kAdvancedHits];
                int n = 0;
                for (int i = 1; i <= kAdvancedHits; i++)
                {
                    const AdvancedHit& h = advancedHit(i);
                    int hk = actionKind(list, h.action[list]);
                    if (h.logic == kLogicCustom && (hk == kActionScript || hk == kActionPlaySlot))
                    {
                        picks[n++] = h.action[list];
                    }
                }
                return n > 0 ? picks[rndBelow(n)] : 0;
            }
            return index;
        }

        // The frame after hitstun's last: the fighter can act though its status
        // may not show it (observed, character not recorded: after landing
        // in hitstun, a held guard took effect, a jump pressed then came out).
        bool justOut() const
        {
            return cpuJustOut(havePrev, prevHitstun, v.hitstun, v.hitlag, v.air, v.status);
        }

        void counter(Frame& out, const Owner& who);

        // Whether the game takes `a`'s first input this frame (`fresh`:
        // hitstun's last frame or the one after). On the last, only a plain
        // guard acts; a jump, attack, special, stick guard or air action waits
        // a frame (observed with scripted inputs). A grounded attack or special
        // also waits out the next unless the fighter shows it can act
        // (observed, character not recorded: an up smash a frame after a jump
        // at the earliest).
        bool takeable(const Action* a, bool airNow, bool fresh) const
        {
            if (a == NULL || a->steps == NULL)
            {
                return true;
            }
            const u32 first = a->steps[0].buttons;
            const bool stick = a->steps[0].x != 0 || a->steps[0].y != 0;
            if (v.hitstun == 1 && (v.air || stick || (first & (kAttack | kSpecial | kJump)) != 0))
            {
                return false;
            }
            return !(fresh && !airNow && !canAct(v) && (first & (kAttack | kSpecial)) != 0);
        }

        // Starts a waiting cue once its frame has come and the CPU can act in
        // its list's situation (the counter actions' rules), unless busy. True
        // if so.
        bool cueNow(Frame& out, const Owner& who, Cue& q);

        // Injects a fresh Guard press during active non-IASA AttackAir (0x33)
        // when approaching landing to trigger P+'s 7-frame L-Cancel window.
        void applyAutoLCancel(Frame& out)
        {
            const CpuOptions& c = opts();
            if (c.autoLCancel == 0 || v.status != game::kStatusAttackAirFirst || !v.air)
            {
                return;
            }
            const float lift = ecbLift();
            float ground = 0.0f;
            bool hasGround = groundBetween(f, v.x, v.y + lift + 0.5f, 0.0f, -24.0f, ground);
            float height = hasGround ? ((v.y + lift) - ground) : 999.0f;
            AutoLCancelInput in;
            in.enabled = true;
            in.status = v.status;
            in.air = v.air;
            in.canActNow = canAct(v) || (v.groups & kAutoLCancelUnsafeGroups) != 0;
            in.actInFrames = framesUntilActionable(f);
            in.hitlag = v.hitlag;
            in.lCancelTimer = readLCancelTimer(f);
            in.prevGuardHeld = (lastButtons(slot) & kGuard) != 0;
            in.attackPressed = (out.buttons & kAttack) != 0;
            in.vy = v.vy;
            in.framesToLand = framesToLand(kAutoLCancelLead, lift, false);
            in.hasGround = hasGround;
            in.heightAboveGround = height;
            if (shouldAutoLCancel(in))
            {
                out.buttons |= kGuard;
            }
        }

        // Starts action `a` (NULL: none) from this frame.
        void start(Frame& out, const Action* a)
        {
            revPicked = false;
            // When no aerial jump remains, wiggle out of tumble or do nothing
            // instead of retrying jump.
            if (a != NULL && (a->flags & kNeedsJump) != 0 && !jumpLeft(f))
            {
                a = tumbleStatus(v.status) ? wiggleOut() : NULL;
            }
            if (a == NULL)
            {
                state = (v.air && tumbleStatus(v.status)) ? kTechFall : kAfter;
                return;
            }
            if (a->playSlot != 0)
            {
                startPlay(out, a->playSlot - 1);
                return;
            }
            noTech = (a->flags & kNoTech) != 0;
            state = kAfter;
            playSlot = -1;
            pending = a;
            tries = 0;
            const Step* steps = a->steps;
            if ((a->flags & (kForShffl | kForFfl)) != 0)
            {
                const bool isShffl = (a->flags & kForShffl) != 0;
                const Step& aerial = isShffl ? a->steps[2] : a->steps[0];
                buildShfflScript(shfflBuf, kMaxShfflSteps, aerial, isShffl, opts().shfflTiming,
                                 static_cast<u32>(rndBelow(4)));
                steps = shfflBuf;
            }
            startScript(steps);
            runScript(out);
        }

        // Play Slot N: the recording from this frame on, no retry.
        void startPlay(Frame& out, int recording)
        {
            state = kAfter;
            pending = NULL;
            script = NULL;
            tries = 0;
            playSlot = recording;
            playFrame = 0;
            runPlay(out);
        }

        // The counter has visibly started: the fighter left the statuses it
        // waits, lands or takes hits in. LandingFallSpecial counts (only an air
        // dodge or a special lands in it).
        bool started() const
        {
            return counterActionStarted(v.status, shieldKind);
        }

        // Grab Escape: alternate A and B with horizontal stick swings on the
        // configured share of frames. Airborne holds Y >= 0.635 (param 3141,
        // MoveDef/Grabs.asm:42-47) while swinging X past 0.5 (clatter param 3204,
        // 0x8076568C) so stick clatter still fires on mash frames.
        void grabbed(Frame& out)
        {
            const CpuOptions& c = opts();
            bool airborne = c.grabRelease == kReleaseAirborne;
            if (airborne)
            {
                out.y = 1.0f;
            }
            int chance = c.grabEscape == kMashPerfect ? 100
                                                      : (c.grabEscape == kMashHigh
                                                             ? kMashChanceHigh
                                                             : (c.grabEscape == kMashMedium
                                                                    ? (s_mashChance > 0 ? s_mashChance
                                                                                        : static_cast<int>(kMashChanceMedium))
                                                                    : 0));
            if (chance == 0 || v.hitlag > 0 || rndBelow(100) >= chance)
            {
                return;
            }
            mashFlip = !mashFlip;
            u32 last = lastButtons(slot);
            u32 button = mashFlip ? kAttack : kSpecial;
            if ((last & button) != 0)
            {
                button = button == kAttack ? kSpecial : kAttack;
            }
            out.buttons = button;
            if (airborne)
            {
                out.x = mashFlip ? 0.68f : -0.68f;
                out.y = 0.72f;
            }
            else
            {
                out.x = mashFlip ? 1.0f : -1.0f;
            }
        }

        // Clatter Escape: same alternating button + horizontal stick mash cadence
        // as Grab Escape for Bury, Ice, FuraFura, and Sleep (Damage_Song).
        void clatter(Frame& out)
        {
            const CpuOptions& c = opts();
            u32 btn = 0;
            float sx = 0.0f, sy = 0.0f;
            if (stepClatterMash(c.clatterEscape, v.status, v.hitlag, lastButtons(slot), rndBelow(100), mashFlip, btn,
                                sx, sy, s_mashChance))
            {
                out.buttons = btn;
                out.x = sx;
                out.y = sy;
            }
        }

        void unfreeze(const Owner& who);

        // Freeze CPU, Move CPU, Controlled By: true if one has this frame.
        bool tools(const Owner& who, const Frame& produced, Frame& out);

        // What the next frame compares with.
        bool stepLedge(Frame& out, const Owner& who, const CpuOptions& c, bool busy)
        {
            u8 base = busy ? 0 : (c.ledgeOption ? c.ledgeOption : (c.multiReversal ? c.revSlot2Action[kRevOnLedge] : 0));
            bool hold = false;
            if (!base || (!(hold = isCliffHoldStatus(v.status)) && !isCliffGetupStatus(v.status)))
            {
                ledgeStateReset(ledgeState);
                return false;
            }
            if (recovering)
            {
                recovering = false;
                main::extraPPlusRecoveryReset(slot);
            }
            if (!hold)
            {
                ledgeStateReset(ledgeState);
                return true;
            }
            state = kIdle;
            hits = 0;
            shieldKind = false;
            script = NULL;
            pending = NULL;
            playSlot = -1;
            noTech = false;
            revPicked = false;
            if (!isCliffWaitStatus(v.status))
            {
                revAction = base;
                revDelay = c.counterDelay;
            }
            else if (!ledgeState.active)
            {
                if (!pickMultiRev(kRevOnLedge, who.entryId))
                {
                    if (c.ledgeOption == kLedgeDefault)
                    {
                        // Slot 0 (Default ledge behavior) won the weighted roll:
                        // latch active/kLedgeDefault so we yield cleanly without re-rolling.
                        ledgeState.active = true;
                        ledgeState.chosen = kLedgeDefault;
                        revAction = kLedgeDefault;
                        return false;
                    }
                    revAction = c.ledgeOption;
                    revDelay = c.counterDelay;
                }
            }
            else if (ledgeState.chosen == kLedgeDefault)
            {
                return false;
            }
            LedgeInput li;
            if (stepLedgeOption(revAction, revDelay, v.status, v.lr, rnd(), ledgeState, li))
            {
                out.buttons = li.buttons;
                out.x = li.x;
                out.y = li.y;
                return true;
            }
            return false;
        }

        void remember()
        {
            havePrev = true;
            prevHitlag = v.hitlag;
            prevDamageHitlag = v.damageHitlag;
            prevHitstun = v.hitstun;
            prevStatus = v.status;
            prevVy = v.vy;
            prevAir = v.air;
        }
    };
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

#pragma dont_inline on
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

    void Brain::startScript(const Step* steps, bool again)
    {
        script = steps;
        stepFrames = 0;
        scriptAirStarted = false;
        if (!again)
        {
            randomSide = rndBelow(2) == 0 ? toward : -toward;
        }
        u32 mask = (!again || shieldKind) ? ~static_cast<u32>(kGuard) : ~0u;
        bool heldBtn = (lastButtons(slot) & steps[0].buttons & mask) != 0;
        bool oosStick = again && shieldKind && (steps[0].buttons & kGuard) != 0
                        && (steps[0].x != 0 || steps[0].y != 0);
        scriptHeld = heldBtn || oosStick;
    }
#pragma dont_inline reset

    bool Brain::runPlay(Frame& out)
    {
        if (playSlot < 0)
        {
            return false;
        }
        if (s_slotFrame != NULL && s_slotFrame(playSlot, playFrame, out))
        {
            playFrame++;
            return true;
        }
        TM_LOG("cpu: slot %d played %u frames\n", playSlot + 1, playFrame);
        playSlot = -1;
        return false;
    }

    bool Brain::runScript(Frame& out)
    {
        if (script == NULL)
        {
            return false;
        }
        if (scriptHeld)
        {
            scriptHeld = false;
            u32 release = shieldKind ? (script[0].buttons & ~static_cast<u32>(kGuard)) : script[0].buttons;
            out.buttons = lastButtons(slot) & ~release;
            return true;
        }
        if (v.air)
        {
            scriptAirStarted = true;
        }
        const bool isShfflScript = script >= shfflBuf && script < shfflBuf + kMaxShfflSteps;
        while (script->frames != 0)
        {
            const Step& s = *script;
            // Landed: a fall, fastfall, or SHFFL/FFL air phase can no
            // longer continue, so the action is over cleanly.
            const bool airWait = s.until == kUntilFalling || s.until == kUntilFastfall;
            if ((!v.air && scriptAirStarted && (isShfflScript || airWait))
                || (airWait && !v.air && v.status != game::kStatusJumpSquat))
            {
                TM_LOG("cpu: P%d landed while waiting to fall (status %x): action over, frame %u\n",
                       player + 1, v.status, frameOf(slot));
                script = NULL;
                pending = NULL;
                return false;
            }
            if (stepFrames < s.frames && !(s.until != kUntilNone && reached(s.until)))
            {
                out.buttons = s.buttons;
                stick(out, s.x / 100.0f, s.y / 100.0f, s.ref);
                stepFrames++;
                return true;
            }
            script++;
            stepFrames = 0;
        }
        script = NULL;
        return false;
    }

    void Brain::chooseTech(const Owner& who)
    {
        const CpuOptions& c = opts();
        static const u8 kFromRandom[4] = {kTechInPlace, kTechAway, kTechTowards, kTechNone};
        tech = c.tech == kTechRandom ? kFromRandom[weighted(c.techChance)] : c.tech;
        preTechRoll = rnd();
        TM_LOG("cpu: P%d tech choice %d, frame %u\n", who.playerNo + 1, tech, frameOf(slot));
        techChosen = true;
        techPressed = false;
    }

    void Brain::techInput(Frame& out, const Owner& who, bool allowStick)
    {
        // The air counter Wait means no tech: known from the hit on, as
        // the press can come before the counter is chosen.
        const Action* air = action(kListAir, opts().counterAir);
        if (noTech || (air != NULL && (air->flags & kNoTech) != 0))
        {
            return;
        }
        if (!techChosen)
        {
            chooseTech(who);
        }
        const bool slipping = slipTechStatus(v.status);
        const float lift = slipping ? 0.0f : ecbLift();
        int nLand = -1;
        bool checkedLand = false;
        bool appliedRoll = false;
        if (tech != kTechNone)
        {
            s32 now = game::trackerFrame();
            if (techPressed && now - lastPress >= kTechGap)
            {
                techPressed = false;
            }
            if (!techPressed)
            {
                // Do not double-press if a valid tech window is already open:
                // a second guard press within 40 frames overwrites m_countPrev
                // and locks out the tech.
                if (techWindowActive(f))
                {
                    techPressed = true;
                }
                else
                {
                    // In the window and kTechGap after the press before:
                    // docs/internals.md, "CPU tech timing". While hitlag > 1,
                    // soControllerImpl::update (0x80764F64, 0x80765394; isStop from
                    // 0x8085B0D8) keeps OR-ing m_trigger on every hitlag frame,
                    // overwriting m_countPrev with 0 (self-lockout) if guard is
                    // pressed before the last hitlag frame (v.hitlag == 1).
                    if (v.hitlag > 1)
                    {
                        return;
                    }
                    int lead = opts().techLockout == kLockoutEarliest ? kTechLeadEarly : kTechLead;
                    int n = -1;
                    if (slipping)
                    {
                        nLand = framesToSlipTech(v.motionFrame, v.motionRate);
                        checkedLand = true;
                        if (nLand >= 0 && nLand <= lead - v.hitlag)
                        {
                            n = nLand;
                        }
                    }
                    else
                    {
                        nLand = framesToLand(lead - v.hitlag, lift);
                        checkedLand = true;
                        n = nLand >= 0 ? nLand : framesToWall(lead - v.hitlag, lift);
                    }
                    if (n >= 0 && now - lastPress >= kTechGap && now >= techFrom)
                    {
                        out.buttons |= kGuard;
                        techPressed = true;
                        TM_LOG("cpu: P%d tech %d: guard %d frames before the predicted landing, frame %u\n",
                               who.playerNo + 1, tech, n + v.hitlag, frameOf(slot));
                    }
                }
            }
            // Fighter.pac samples stickX on the landing/tech frame itself
            // (Term_Passive_Fb 0x80FAB024, Passive_Fb entry 0x80FB563C). Wait
            // until within kTechRollLead frames of impact so the CPU does not
            // drift in tumble before landing unless Pre-Tech Drift is active.
            if (techPressed && v.hitlag == 0 && (tech == kTechAway || tech == kTechTowards))
            {
                if (!checkedLand)
                {
                    nLand = slipping ? framesToSlipTech(v.motionFrame, v.motionRate)
                                     : framesToLand(kTechRollLead, lift);
                    checkedLand = true;
                }
                float rx = techRollStickX(tech, toward, nLand, techPressed, 0, allowStick,
                                          param(kParamRollStick, 0.2f), param(kParamWiggleStick, 0.75f));
                if (rx != 0.0f)
                {
                    out.x = rx;
                    appliedRoll = true;
                }
            }
        }
        if (!appliedRoll && !slipping && v.air && tumbleStatus(v.status) && v.hitlag == 0 && allowStick)
        {
            int driftMode = main::settings().techInvis.preTechDrift;
            if (driftMode != main::kPreTechDriftOff)
            {
                if (!checkedLand)
                {
                    nLand = framesToLand(kTechRollLead, lift);
                }
                float dx = preTechDriftStickX(driftMode, tech, toward, preTechRoll, nLand, 0, true,
                                              param(kParamWiggleStick, 0.75f));
                if (dx != 0.0f)
                {
                    out.x = dx;
                }
            }
        }
    }

    void Brain::getupInput(Frame& out, const Owner& who)
    {
        // Lay_Down (0x50, e.g. Snake down throw) and Slip_Wait (0x8D, e.g.
        // missed banana trip tech) share Down_Wait's getup inputs
        // (Fighter.pac 0x80FB4AA4 -> 0x80FB47AC, Slip_Wait 0x80FB755C).
        if (!isGetupWaitStatus(v.status))
        {
            return;
        }
        // Chance to stay lying down for kMissTechWait frames, re-rolled
        // each time the wait finishes.
        if (missWait > 0)
        {
            missWait--;
            return;
        }
        if (downFrames == 0 && opts().missTechWait > 0 && rndBelow(100) < opts().missTechWait)
        {
            missWait = kMissTechWait;
            TM_LOG("cpu: P%d waits %d frames lying down\n", who.playerNo + 1, missWait);
            return;
        }
        if (downFrames == 0)
        {
            const CpuOptions& c = opts();
            u8 wait = 0;
            if (pickMultiRev(kRevOnGetup, who.entryId))
            {
                getup = revAction;
                wait = revDelay;
            }
            else
            {
                static const u8 kFromRandom[4] = {kGetupStand, kGetupAway, kGetupTowards, kGetupAttack};
                getup = c.getup == kGetupRandom ? kFromRandom[weighted(c.getupChance)] : c.getup;
            }
            TM_LOG("cpu: P%d get up %d\n", who.playerNo + 1, getup);
            if (wait > 0)
            {
                missWait = wait - 1;
                downFrames = 2;
                return;
            }
        }
        // Pushed every other frame, so each push is a fresh input.
        if ((downFrames++ & 1) != 0)
        {
            return;
        }
        switch (getup)
        {
        case kGetupStand:
            out.y = 1.0f;
            break;
        case kGetupAway:
            out.x = -toward;
            break;
        case kGetupTowards:
            out.x = toward;
            break;
        default:
            out.buttons = kAttack;
            break;
        }
    }

    void Brain::idle(Frame& out, const Owner& who)
    {
        if (held(out, who))
        {
            return;
        }
        if (!v.air && !guardStatus(v.status) && canAct(v) && opts().adaptiveMode == kAdaptiveActive
            && main::extraLoaded())
        {
            int biasIdx = main::extraAdaptiveBias(who.playerNo, kListGround, false, oppDist, false);
            if (biasIdx > 0)
            {
                const Action* a = actionFor(kListGround, biasIdx, fighterKind(who.entryId));
                if (a != NULL)
                {
                    main::extraAdaptiveBias(who.playerNo, kListGround, false, oppDist, true);
                    start(out, a);
                    return;
                }
            }
        }
        switch (opts().behavior)
        {
        case kBehaviorShield:
            if (!v.air)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            break;
        case kBehaviorCrouch:
            if (!v.air)
            {
                out.y = -1.0f;
            }
            break;
        case kBehaviorJump:
            if (runScript(out))
            {
                break;
            }
            if (!v.air && canAct(v) && v.status != game::kStatusJumpSquat)
            {
                startScript(shortHop());
                runScript(out);
            }
            break;
        case kBehaviorPerfectShield:
            // Guard as a hitbox is predicted to reach the shield: P+'s
            // powershield is a press in Guard_On's first two frames
            // (Injects/MoveDef/PowerShield.asm: IC-Basic[0] <= 1). Pressed
            // on the hit's frame or the one before, no damage.
            if (v.air)
            {
                powershield = 0;
                break;
            }
            if (powershield > 0)
            {
                // After a reflect (no Guard_Damage), move straight to the
                // shield counter action.
                if (justReflected(f))
                {
                    powershield = 0;
                    hits++;
                    actionable = 0;
                    revPicked = false;
                    script = NULL;
                    pending = NULL;
                    state = kCounter;
                    shieldKind = true;
                    TM_LOG("cpu: P%d perfect shield: reflected, counter, frame %u\n", who.playerNo + 1,
                           frameOf(slot));
                    counter(out, who);
                    break;
                }
                if (powershield++ < kPowershieldHold)
                {
                    out.buttons = kGuard;
                }
                else if (canAct(v))
                {
                    powershield = 0;
                }
                break;
            }
            {
                ShieldBubble bubble;
                int kind = canAct(v) ? incomingHit(f, who.entryId, v, bubble) : kHitNone;
                if (kind != kHitNone)
                {
                    out.buttons = kGuard;
                    powershield = 1;
                    TM_LOG("cpu: P%d perfect shield: guard for %s predicted next frame "
                           "(shield %.2f %.2f r %.2f%s), frame %u\n",
                           who.playerNo + 1,
                           kind == kHitWeapon ? "weapon" : (kind == kHitItem ? "item" : "fighter"),
                           bubble.x, bubble.y, bubble.radius, bubble.fallback ? " fallback" : "", frameOf(slot));
                }
            }
            break;
        default:
            break;
        }
    }

    void Brain::counter(Frame& out, const Owner& who)
    {
        if (shieldKind && (v.air || !guardStatus(v.status)))
        {
            actionable = 0;
            revPicked = false;
            enterIdle(out, who);
            return;
        }
        bool fresh = justOut();
        if (!canAct(v) && !fresh)
        {
            // Reset while unactionable so pre-landing air frames do not
            // shorten the delay after landing.
            actionable = 0;
            revPicked = false;
            if (shieldKind)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            return;
        }
        // Landing this frame counts as grounded.
        const bool airNow = v.air && framesToLand() != 0;
        if (actionable == 0)
        {
            counterAir = airNow;
        }
        int list = shieldKind ? kListShield : (counterAir && airNow ? kListAir : kListGround);
        int index = 0, delay = 0;
        bool comboJumpInvuln = false;
        const main::ComboTrainerSettings& combo = main::settings().comboTrainer;
        static const u8 kComboDelayF[main::kComboDelayCount] = {0, 15, 30, 45, 60};
        const int comboDelayF = (combo.resetDelay < main::kComboDelayCount ? kComboDelayF[combo.resetDelay] : 30)
                                + static_cast<int>(combo.comboTimerAdjustment);
        const bool comboEscapeActive = !shieldKind && combo.enabled != 0 && combo.fixPosition != 0
                                       && combo.recoveryTrainer == 0 && comboDelayF > 0
                                       && combo.postHitstunEscape != main::kComboEscapeCounter;
        if (comboEscapeActive)
        {
            delay = 0;
            switch (combo.postHitstunEscape)
            {
            case main::kComboEscapeDodge:
                index = 1; // Ground: Spotdodge (1), Air: Airdodge (1)
                break;
            case main::kComboEscapeJumpInvuln:
                if (list == kListAir && !jumpLeft(f))
                {
                    index = 1; // No double jump left: fall back to Airdodge without phantom intangibility
                }
                else
                {
                    index = (list == kListAir) ? 2 : 18; // Air: Jump Away (2), Ground: Short Hop (18)
                    comboJumpInvuln = true;
                }
                break;
            case main::kComboEscapeCustomPool:
                index = customPoolIndex(list);
                break;
            case main::kComboEscapeNone:
            default:
                index = 0;
                break;
            }
        }
        else if (!counterChoice(list, index, delay))
        {
            TM_LOG("cpu: P%d counter disabled for hit %d\n", who.playerNo + 1, hits);
            revPicked = false;
            state = kAfter;
            return;
        }
        if (list == kListAir || comboEscapeActive)
        {
            revPicked = false;
        }
        else
        {
            if (!revPicked)
            {
                revPicked = true;
                TM_STATIC_ASSERT((kListGround >> 1) == kRevOnHit && (kListShield >> 1) == kRevOnShield,
                                 tm_list_to_rev_trigger);
                pickMultiRev(list >> 1, who.entryId);
            }
            if (revAction != 0)
            {
                index = revAction;
                delay = revDelay;
            }
        }
        // A held guard acts on hitstun's last frame, but delays count from
        // the first actionable frame, the next one.
        bool lastStun = v.hitstun == 1;
        if (actionable < delay)
        {
            if (!lastStun)
            {
                actionable++;
            }
            if (shieldKind)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            return;
        }
        bool adaptiveUsed = false;
        if (revAction == 0 && opts().adaptiveMode == kAdaptiveActive && list != kListAir && !comboEscapeActive
            && main::extraLoaded())
        {
            int biasIdx = main::extraAdaptiveBias(who.playerNo, list, true, oppDist, false);
            if (biasIdx > 0)
            {
                index = biasIdx;
                adaptiveUsed = true;
            }
        }
        int fk = fighterKind(who.entryId);
        int chosen = resolve(list, index, fk);
        if (actionKind(list, chosen) == kActionPlayRandomSlot)
        {
            int pick = randomSlot();
            TM_LOG("cpu: P%d counter list %d: Play Random Slot picks slot %d, frame %u\n", who.playerNo + 1, list,
                   pick + 1, frameOf(slot));
            revPicked = false;
            startPlay(out, pick);
            return;
        }
        const Action* a = actionFor(list, chosen, fk);
        if (!takeable(a, airNow, fresh))
        {
            return;
        }
        if (adaptiveUsed)
        {
            main::extraAdaptiveBias(who.playerNo, list, true, oppDist, true);
        }
        if (comboJumpInvuln && a != NULL && ((a->flags & kNeedsJump) == 0 || jumpLeft(f)))
        {
            setIntangible(f, 10);
        }
        TM_LOG("cpu: P%d counter list %d action %d (%d) after %d actionable frames, hit %d, frame %u, "
               "air %d y %d vy %d\n",
               who.playerNo + 1, list, chosen, index, actionable, hits, frameOf(slot), v.air ? 1 : 0,
               game::f2i(v.y * 100.0f), game::f2i(v.vy * 100.0f));
        start(out, a);
    }

    bool Brain::cueNow(Frame& out, const Owner& who, Cue& q)
    {
        s32 now = game::trackerFrame();
        bool busy = v.hitlag > 0 || state == kGrabbed || state == kClatter
                    || (state == kAfter && (script != NULL || pending != NULL || playSlot >= 0));
        if (q.state != kCueWaiting || (q.frame != kCueAsap && now < q.frame) || busy)
        {
            return false;
        }
        bool fresh = justOut();
        // Leaving a jump squat is airborne: an air action can start in it.
        // The situation decides, not the predicted landing (observed,
        // character not recorded: with it, a Jab cued three frames before
        // an empty short hop landed came out as a neutral air).
        const bool takeoff = q.list == kListAir && takeoffNext(v);
        const bool airNow = v.air || takeoff;
        if ((!canAct(v) && !fresh && !takeoff) || airNow != (q.list == kListAir))
        {
            return false;
        }
        int fk = fighterKind(who.entryId);
        int chosen = q.custom != NULL ? 0 : resolve(q.list, q.index, fk);
        const Action* a = q.custom != NULL ? q.custom : actionFor(q.list, chosen, fk);
        bool randomSlotPick = q.custom == NULL && actionKind(q.list, chosen) == kActionPlayRandomSlot;
        if (!randomSlotPick && !takeable(a, airNow, fresh))
        {
            return false;
        }
        TM_LOG("cpu: P%d cue list %d action %d, frame %d\n", who.playerNo + 1, q.list, chosen, now);
        out.buttons = 0;
        out.x = out.y = 0.0f;
        if (randomSlotPick)
        {
            startPlay(out, randomSlot());
        }
        else
        {
            start(out, a);
        }
        q.state = kCueRunning;
        q.start = now;
        return true;
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
                s_portChanged[who.playerNo] = true;
                TM_LOG("cpu: P%d controlled by port %d\n", who.playerNo + 1, c.controlledBy);
            }
            game::trainingControllerSuppress(game::kControllerOwnerControlledBy, true);
            setControllerOf(who.playerNo, c.controlledBy);
            setCpuType(who.input, kCpuTypeControl);
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

    static Brain* s_brains = NULL;

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
        Handed& hand = s_aux->handed[who.playerNo & 3];
        bool handed = false;
        bool handedCounter = false;
        if (hand.frame != game::kNoFrame)
        {
            s32 age = game::trackerFrame() - hand.frame;
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
        bool shieldLag = v.status == game::kStatusGuardDamage && v.hitlag > prevHitlag;
        bool newHit = handed
                      || (havePrev && (damageLag || shieldLag || v.hitstun > prevHitstun) && !grabbedStatus(v.status));
        Cue& q = s_aux->cues[who.playerNo & 3];
        cueTest(who.playerNo);

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
            hits++;
            if (v.status == game::kStatusGuardDamage)
            {
                state = kShieldStun;
                shieldKind = true;
                powershield = 0;
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
                    techFrom = game::trackerFrame() + kTechGap - kTrapEarly;
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
            bool offstageNow = onCliff || (v.air && !hasFloor);
            if (offstageNow || (recovering && v.air))
            {
                if (c.tech != kTechNone && c.techTrap == kTrapOff)
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
            else if (recovering)
            {
                recovering = false;
                main::extraPPlusRecoveryReset(slot);
            }
        }
        else if (recovering)
        {
            recovering = false;
            main::extraPPlusRecoveryReset(slot);
        }

        // A cue that starts has this frame, whatever the state.
        bool playingRec = false;
        switch (cueNow(out, who, q) ? -1 : state)
        {
        case -1:
            break;
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
                grabbed(out);
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
                clatter(out);
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
        if (!playingRec && playSlot < 0 && !q.hold)
        {
            applyAutoLCancel(out);
        }
        if ((out.buttons & kGuard) != 0 && (lastButtons(slot) & kGuard) == 0)
        {
            lastPress = game::trackerFrame();
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

#pragma dont_inline on
    static void copyBrainState(Brain& dst, const Brain& src)
    {
        const u8* s = reinterpret_cast<const u8*>(&src.slot);
        u32 bytes = sizeof(Brain) - static_cast<u32>(s - reinterpret_cast<const u8*>(&src));
        memcpy(&dst.slot, s, bytes);
    }
#pragma dont_inline reset

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
        }
        memcpy(c.portChanged, s_portChanged, sizeof(s_portChanged));
        memcpy(c.savedPort, s_savedPort, sizeof(s_savedPort));
        memcpy(c.savedType, s_savedType, sizeof(s_savedType));
        c.rng = s_rng;
        c.moving = s_moving;
        c.moverPlayer = s_mover.playerNo;
        c.freezeRequest = s_freezeRequest;
        memcpy(c.moveLabel, g_moveCpuLabel, sizeof(c.moveLabel));
        memcpy(c.freezeLabel, g_freezeCpuLabel, sizeof(c.freezeLabel));
        memcpy(c.cues, s_aux->cues, sizeof(s_aux->cues));
    }

    void optionsLoaded(int which)
    {
        if (s_brains == NULL || s_saved == NULL)
        {
            return;
        }
        const Saved& c = s_saved[which & 1];
        for (int i = 0; i < kMaxSlots; i++)
        {
            copyBrainState(s_brains[i], c.brains[i]);
        }
        s_rng = c.rng;
        s_moving = c.moving;
        s_mover.playerNo = c.moverPlayer;
        s_freezeRequest = c.freezeRequest;
        memcpy(g_moveCpuLabel, c.moveLabel, sizeof(c.moveLabel));
        memcpy(g_freezeCpuLabel, c.freezeLabel, sizeof(c.freezeLabel));
        memcpy(s_aux->cues, c.cues, sizeof(s_aux->cues));
        bool want = opts().control == kControlLab && opts().controlledBy != 0;
        for (int i = 0; i < kMaxSlots; i++)
        {
            void* input = liveCpuInput(i);
            if (want && input != NULL)
            {
                if (c.portChanged[i])
                {
                    s_savedPort[i] = c.savedPort[i];
                    s_savedType[i] = c.savedType[i];
                    s_portChanged[i] = true;
                    setControllerOf(i, opts().controlledBy);
                    setCpuType(input, kCpuTypeControl);
                }
                else
                {
                    int curType = cpuType(input);
                    if (curType >= 0 && curType != kCpuTypeControl)
                    {
                        s_savedPort[i] = s_origPort[i];
                        s_savedType[i] = static_cast<u8>(curType);
                        s_portChanged[i] = true;
                        setControllerOf(i, opts().controlledBy);
                        setCpuType(input, kCpuTypeControl);
                    }
                    else
                    {
                        s_portChanged[i] = false;
                    }
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
            s_portChanged[i] = false;
            s_origPort[i] = static_cast<s8>(controllerOf(i));
            s_aux->handed[i].frame = game::kNoFrame;
            s_aux->playbackSeen[i].frame = game::kNoFrame;
        }
        memset(s_aux->cues, 0, sizeof(s_aux->cues));
        memset(&s_aux->cueTest, 0, sizeof(s_aux->cueTest));
        s_aux->cueTest.magic = kCueTestMagic;
        s_aux->cueTest.playerNo = -1;
        TM_LOG("cpu: cue test block at %08x\n", &s_aux->cueTest);
        s_rng = game::frameCounter() | 1;
        s_moving = false;
        s_freezeRequest = false;
        g_moveCpuLabel[0] = g_freezeCpuLabel[0] = '\0';
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
            if (s_brains != NULL)
            {
                s_brains[i].reset();
                s_brains[i].revAction = 0;
                s_brains[i].revDelay = 0;
            }
            if (s_aux != NULL)
            {
                s_aux->handed[i].frame = game::kNoFrame;
                s_aux->playbackSeen[i].frame = game::kNoFrame;
            }
        }
        if (s_aux != NULL)
        {
            memset(s_aux->cues, 0, sizeof(s_aux->cues));
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
