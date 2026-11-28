#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/cpu/cue.h"
#include "main/game/status_ids.h"

namespace tm {
namespace cpu {
    // Pure CPU state-machine helpers shared with host unit tests.
    inline bool shieldHitRefreshed(int status, int hitlag, int prevHitlag, int shieldStun, int prevShieldStun,
                                   int prevStatus)
    {
        return status == game::kStatusGuardDamage
               && (hitlag > prevHitlag || shieldStun > prevShieldStun || prevStatus != game::kStatusGuardDamage);
    }
}
}

#ifndef TM_CPU_BRAIN_PURE_ONLY
#ifndef TM_OPTIONS_CPP_INTERNAL
#error "Include from main/cpu/options.cpp only"
#endif

// Internal header factored out of main/cpu/options.cpp so domain sections
// (options_di_sdi.hpp, options_tech.hpp, options_counters.hpp) share Brain
// without breaking single-TU -ipa file optimization or Brain's exact layout.
#include <ft/ft_entry.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>

#include "main/cpu/actions.h"
#include "main/cpu/clatter_logic.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/cue.h"
#include "main/cpu/di_logic.h"
#include "main/cpu/driver.h"
#include "main/cpu/lab_data.h"
#include "main/cpu/ledge_logic.h"
#include "main/cpu/options.h"
#include "main/cpu/shffl_core.h"
#include "main/cpu/tech_logic.h"
#include "main/cpu/view.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/match.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/settings.h"
#include "shared/game/frame.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/stick.h"

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
        kTechSoundId = 0  // snd_se_system_cursor
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

    struct Handed {
        s32 frame;
        bool counter;
        int hits;
    };
    enum { kHandFrames = 2 };
    struct PlaybackSeen {
        s32 frame;
        int hitlag, hitstun, status;
        int hits;
    };

    struct Cue {
        u8 state; // CueState
        s8 list;
        u8 index;
        bool hold;
        bool repeat;
        u8 holdRef;
        u16 holdButtons;
        s32 frame; // kCueAsap or the frame to start on
        s32 start; // the frame its first input was written in
        float holdX, holdY;
        const Action* custom; // cueAction's own action (NULL: the list's)
    };

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
        u32 seen;    // 0x2C the generation that ran
        s32 opFrame; // 0x30 the frame it ran in
        s32 state;   // 0x34 cueState
        s32 start;   // 0x38 cueStart
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
    static SlotFrame s_slotFrame = NULL;
    static int s_mashChance = 0;

    static u32 cpuRnd();
    static int cpuRndBelow(int n);
    static int cpuWeighted(const u8* w);
    static void restorePort(int playerNo, bool updatePatch = true);

    static Cue* cueOf(int playerNo)
    {
        return s_aux != NULL && playerNo >= 0 && playerNo < kMaxSlots ? &s_aux->cues[playerNo] : NULL;
    }

    static bool downStatus(int s) { return isDownStatus(s); }
    static bool slipTechStatus(int s) { return isSlipTechStatus(s); }
    static bool passiveStatus(int s) { return s == kStatusPassive || s == kStatusPassiveFb; }
    static bool tumbleStatus(int s) { return s >= kStatusDamageFly && s <= kStatusDamageFall; }

    static bool cliffStatus(int s) { return s >= kStatusCliffFirst && s <= kStatusCliffLast; }

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

    // ---- the state machine ------------------------------------------------------

    class Brain : public Source {
    public:
        static void* operator new(size_t, void* p) { return p; }
        static void operator delete(void*, void*) {}

        int slot;
        int player;      // the player driven last
        bool noReaction; // set on the CPU: undone when the option goes off (not by reset)
        u8 revAction;
        u8 revDelay;
        int state;
        bool shieldKind; // the counter follows a hit on shield
        int hits;        // hits taken since the CPU was last idle
        bool havePrev;
        int prevHitlag, prevHitstun, prevShieldStun, prevStatus;
        bool prevDamageHitlag;
        float prevVy;
        bool prevAir;
        // trajectory, smash and automatic smash DI for the current hit
        float tdiX, tdiY, sdiX, sdiY, asdiX, asdiY;
        bool haveAsdi;
        int diKind; // the Tdi the hit's trajectory DI follows (Random resolved)
        u32 diRoll; // per-hit RNG seed for Slight Random DI
        int sdiDone, hitlagFrame, afterHitlag;
        // teching
        int tech;
        bool techChosen, techPressed;
        u32 preTechRoll;
        float groundY, gravity;
        bool groundKnown;
        s32 lastPress;  // the frame of the CPU's last guard press (any)
        s32 techFrom;   // no tech press before this frame (Simulate Tech Trap)
        bool trapPress; // Simulate Tech Trap Latest: press guard now
        bool noTech;    // the air counter was Wait
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

        virtual bool wants(const Owner& who) { return labControl(who); }
        virtual void released(const Owner& who);
        virtual bool next(const Owner& who, const Frame& produced, Frame& out);

    private:
        friend void optionsAttach();
        friend void optionsPrepareSwap(bool, int);
        friend void optionsResetSlot(int);
        friend void optionsResetAll();
        friend void cueReset(int);
        friend s32 techReady(int);
        friend void cueCancel(int);
        friend bool optionsDi(int, float&, float&);
        friend void optionsSaved(int);
        friend void optionsLoaded(int);
        friend void optionsClaim(const Owner&);

        void noReactionOn(const Owner& who, bool on);

        float toward; // +1 if the opponent is to the right
        float oppDist;
        View v;
        Fighter* f;

        void stick(Frame& out, float x, float y, int ref);
        void enterIdle(Frame& out, const Owner& who);
        void enterDown(Frame& out, const Owner& who);
        void clearAction(int nextState);
        void startScript(const Step* steps, bool again = false);
        bool reached(int until) const;
        bool runPlay(Frame& out);
        int randomSlot();
        bool runScript(Frame& out);

        struct Flight {
            const float* zones;
            float accel, maxFall;
            float sin, cos, decay;
        };

        float survivalTurn(float kx, float ky, float len) const;
        float flight(const Flight& fl, float kx, float ky, float len, float turn) const;
        void relativeDi();
        static bool dirStick(int code, float away, float& x, float& y);
        static void stepKb(float& kx, float& ky, float& kl, float decay, float& gy, float accel, float maxFall);
        static void setBodiesMotion(int entryId, Fighter* main, float rate);
        void chooseDi(const Owner&);
        void hitlag(Frame& out);
        void chooseTech(const Owner& who);
        int framesToLand(int lead = kTechLead, float lift = 0.0f, bool fallbackGround = true);
        int framesToWall(int lead, float lift);
        float ecbLift() const;
        void techInput(Frame& out, const Owner& who, bool allowStick = true);
        bool pickMultiRev(int trigger, int entryId);
        void getupInput(Frame& out, const Owner& who);
        void shieldTilt(Frame& out);
        bool held(Frame& out, const Owner& who);
        void idle(Frame& out, const Owner& who);
        bool counterChoice(int list, int& index, int& delay) const;
        int resolve(int list, int index, int ftKind);
        bool justOut() const;
        void counter(Frame& out, const Owner& who);
        bool takeable(const Action* a, bool airNow, bool fresh) const;
        bool cueNow(Frame& out, const Owner& who, Cue& q);
        void applyAutoLCancel(Frame& out);
        void start(Frame& out, const Action* a);
        void startPlay(Frame& out, int recording);

        bool started() const { return counterActionStarted(v.status, shieldKind); }

        void grabbed(Frame& out);
        void clatter(Frame& out);
        void unfreeze(const Owner& who);
        bool tools(const Owner& who, const Frame& produced, Frame& out);
        bool stepLedge(Frame& out, const Owner& who, const CpuOptions& c, bool busy);
        void remember();
    };
}
}
#endif // !TM_CPU_BRAIN_PURE_ONLY
