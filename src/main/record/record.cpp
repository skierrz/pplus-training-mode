#include "main/record/record.h"

#include <OS/OSTime.h>
#include <gf/gf_application.h>
#include <gf/gf_pad_status.h>
#include <gf/gf_pad_system.h>
#include <gm/gm_global.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "main/cpu/driver.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/match.h"
#include "main/game/pad_inmatch.h"
#include "main/game/training_controller.h"
#include "main/lab.h"
#include "main/record/hud.h"
#include "main/record/slots.h"
#include "main/record/start_input.h"
#include "main/record/start_state.h"
#include "main/savestate/savestate.h"
#include "main/savestate/snapshot.h"
#include "main/settings.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/game/pad_bits.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "shared/perf.h"

namespace tm {
namespace record {
    using main::settings;

    enum Side { kHmn = 0, kCpu = 1 };

    // CPU Mode values; HMN Mode values (Off, Record, Playback, Re-Record) map
    // onto them by adding 1 for non-Off entries.
    enum Mode { kModeOff, kModeControl, kModeRecord, kModePlayback, kModeReRecord };
    enum Counter { kCounterOff, kCounterEnds, kCounterCpuHit, kCounterHmnHit, kCounterAnyHit };
    enum AutoRestore { kAutoOff, kAutoPlaybackEnds, kAutoCounter };
    enum Takeover { kTakeHmn, kTakeCpu, kTakeNone };
    enum Mirror { kMirrorOff, kMirrorOn, kMirrorRandom };

    enum {
        kAutoRestoreDelay = 24, // 0.4 s pause before automatic restore
        kMessageFrames = 150,
        // Live input that takes over a playback or ends Start Paused: these
        // buttons, a stick past this raw value, a trigger pressed this far.
        kLiveButtons = gfPadButtons::A | gfPadButtons::B | gfPadButtons::X | gfPadButtons::Y | gfPadButtons::Z
                       | gfPadButtons::L | gfPadButtons::R | gfPadButtons::DUp,
        kLiveStick = 24,
        kLiveTrigger = 43,
        kPorts = 4,
        // gmGlobalModeMelee +0x98: per-player gmPlayerInitData, 0x5C bytes
        // (gm_global_mode_melee.h): +1 state (0 human, 1 CPU), +7 controller
        // number. Training sets each fighter input's controller (aiInput
        // +0x8) to it minus one every frame (observed in an instruction trace).
        kModePlayers = 0x98,
        kPlayerSize = 0x5C,
        kPlayerState = 0x1,
        kPlayerController = 0x7,
        kStateHuman = 0,
        kStateCpu = 1,
        kStatusCliffWait = 0x75,
        kTestMagic = 0x544D5243 // "TMRC"
    };

    // Requests the harness writes into the test block (the menu actions).
    enum Request {
        kReqNone,
        kReqSaveOrRestore,
        kReqRestore,
        kReqDelete,
        kReqResave,
        kReqPrune,
        kReqCopySlot,
        kReqDeleteSlot,
        kReqHmnMode,
        kReqCpuMode,
        kReqSlot,
        kReqMirror
    };

    // Fixed layout: scripts/record_tests.py reads and writes it by offset.
    struct TestBlock {
        u32 magic;          // 0x00
        u32 request;        // 0x04 Request; set back to 0 when carried out
        u32 result;         // 0x08 requests carried out
        u32 pausedCalls;    // 0x0C the player's input hook calls while Training was paused
        u32 steps;          // 0x10 game steps the recorded fighters ran
        u32 t[2];           // 0x14 game steps since the restore (both the same)
        u32 flags;          // 0x1C TestFlags
        u32 restores;       // 0x20
        u32 storage;        // 0x24
        u32 bytes;          // 0x28
        u16 start[2][6];    // 0x2C
        u16 end[2][6];      // 0x44
        u32 lossy;          // 0x5C
        u32 takeover[2];    // 0x60 frame of the last takeover per side
        u32 loops;          // 0x68
        u32 held;           // 0x6C game-pads passes held by Start Paused
        u32 playSlot[2];    // 0x70 slot played (Random resolved)
        u32 stepT;          // 0x78 the frame (t) of the last step the player's fighter ran
        u32 startFrame;     // 0x7C the match's frame counter when the run started (t = counter - this - 1)
        u32 rewinds;        // 0x80 Frame Decrement presses taken
        u32 rewound;        // 0x84 rewinds that reached their frame
        u32 rewindTarget;   // 0x88 the frame the last one went back to
        u32 rewindTicks;    // 0x8C its time from the press to the frame (OS ticks)
        u32 rewindPasses;   // 0x90 main-loop passes it took after the load
        u32 refused;        // 0x94 Frame Decrement presses refused
    };

    enum TestFlags {
        kFlagSaved = 0x1,
        kFlagHolding = 0x2,
        kFlagHmnCancelled = 0x4,
        kFlagCpuCancelled = 0x8,
        kFlagPadSwapped = 0x10,
        kFlagStorage = 0x20,
        kFlagMirrored = 0x40,
        kFlagHmnNeutral = 0x80,
        kFlagExact = 0x100,      // the positions are a savestate
        kFlagDetached = 0x200,   // a state saved from other positions was loaded
        kFlagSavePending = 0x400,
        kFlagMessage = 0x800,
        kFlagRecArmed = 0x1000
    };

    struct SideState {
        int player;      // player number of this side's fighter, -1 if none
        bool cancelled;  // playback over for this run (takeover, end, counter)
        bool takenOver;  // by live input
        bool counter;    // the CPU went to its counter behaviour
        int playSlot;    // slot index played (Random resolved at the restore)
        // This step's control, decided before the driver runs.
        bool want;
        bool pass;       // keep what update() produced (the player's pad)
        bool neutral;
        Input out;
        int refused;     // the mode refused last, logged once
        bool fullLogged;
        bool pendingClear; // stale slot data to clear on first armed write
    };

    class RecordSource : public cpu::Source {
    public:
        int side;
        virtual bool wants(const cpu::Owner& who);
        virtual bool next(const cpu::Owner& who, const cpu::Frame& produced, cpu::Frame& out);
        virtual void released(const cpu::Owner&)
        {
        }
    };

    static TestBlock s_test;
    static SideState s_side[kSides];
    static RecordSource s_source[kSides];
    static StartState* s_start = NULL;
    static void (*s_innerInput)(const iface::FighterInput*) = NULL;
    static void (*s_innerPads)(u32, gfPadStatus*) = NULL;
    static bool s_active = false;
    // Passes to retry storageAllocate() if FighterTechqniq is not empty at
    // match start (in Training it is empty once Mr. Resetti is disabled;
    // costumes decompress in Network, not FighterTechqniq).
    enum { kStorageTries = 600 };
    static int s_storageTries = 0;
    static bool s_stepRan = false;
    // The sides whose fighters ran a step since the last afterStep(): the
    // game runs two steps in one pass of the main loop when it catches up
    // after a slow frame, and the second one must not reuse frame t.
    static u8 s_sidesRan = 0;
    static bool s_doubleLogged = false;
    static void afterStep();
    static u32 s_t = 0; // game steps since the positions were restored
    static bool s_restorePending = false;
    // The start state: always the lite one (positions, for the fallback and
    // mirroring) and, when the savestate took it, the recording's savestate
    // slot (s_exact). Saves wait for a top of the main loop that allows one.
    enum SaveKind { kSaveNone, kSaveNew, kSaveAgain, kSavePrune, kSaveImport };
    static bool s_exact = false;
    static int s_savePending = kSaveNone;
    static u32 s_gen = 0;         // +1 at every save of the positions
    static u32 s_runId = 0;       // +1 at every resetRun()
    static bool s_detached = false; // a quick state from other positions was loaded
    struct QuickMark {
        bool valid;
        u32 gen;
        u32 runId;
        u32 t;
        int restoreTimer;
        int mode[kSides];
        bool cancelled[kSides];
        bool takenOver[kSides];
        bool counter[kSides];
        u32 takeAt[kSides];
        bool mirrored;
        bool looped;
        bool holding;
        bool recArmed;
    };
    static QuickMark s_quick;
    static bool s_holding = false;
    static bool s_recArmed = false;
    static u8 s_prevStartOnInput = 0;
    static bool s_mirrored = false;
    static bool s_cpuTrainingControl = false;
    static bool s_swapPad = false;   // the CPU reads the player's controller
    static bool s_hmnNeutral = false; // the player's own fighter is held neutral
    static u32 s_hmnFlick = cpu::button::kFlickJump; // the player's released-pad bits
    static bool s_live = false; // the player's pad shows input this pass
    static bool s_paused = false; // Training's pause menu is open
    static int s_restoreTimer = 0;
    static u32 s_hadInputs[kSides];  // slots with inputs, for the chance rows
    static const char* s_message = NULL;
    static int s_messageTimer = 0;
    static u32 s_rng = 1;

    // Frame Decrement (rewind). The run log (slots.h) holds both sides' inputs of frames 0 .. s_logged - 1 of this run. A rewind asks
    // the savestate for the start state (kRewindLoad), then replays the log
    // (kRewindReplay) until frame s_rewindTarget is the next one to run. The
    // replay also repeats what a live takeover did (the frame of each is
    // kept) and the damage the run started with (its random percent was
    // rolled once). Bursts of game steps run in one main-loop pass, drawn
    // once (see burst()).
    enum RewindPhase { kRewindNone, kRewindLoad, kRewindReplay };
    enum { kNoTake = 0xFFFFFFFF };
    static int s_rewind = kRewindNone;
    static u32 s_rewindTarget = 0;
    static u32 s_rewindStart = 0; // OSGetTick at the press
    static u32 s_logged[kSides];
    static u32 s_takeAt[kSides];
    static bool s_looped = false; // Loop restarted the inputs in this run
    static float s_startDamage[kSides];

    // ---- helpers ----

#ifdef __MWERKS__
#pragma dont_inline on
#endif
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

    static void message(const char* text)
    {
        s_message = text;
        s_messageTimer = kMessageFrames;
        TM_LOG("record: %s\n", text);
    }

    static u8* playerData(int player)
    {
        GameGlobal* g = g_GameGlobal;
        if (player < 0 || player >= kPorts || !game::isRam(g) || !game::isRam(g->m_modeMelee))
        {
            return NULL;
        }
        return reinterpret_cast<u8*>(g->m_modeMelee) + kModePlayers + player * kPlayerSize;
    }

    static int sideMode(int side)
    {
        const main::RecordingSettings& r = settings().recording;
        if (side == kHmn)
        {
            return r.hmnMode == 0 ? kModeOff : r.hmnMode + 1;
        }
        return r.cpuMode;
    }

    static int slotSetting(int side)
    {
        return side == kHmn ? settings().recording.hmnSlot : settings().recording.cpuSlot;
    }

    static bool controlledByActive()
    {
        return settings().cpu.options.control == cpu::kControlLab && settings().cpu.options.controlledBy != 0;
    }

    static bool cpuLab()
    {
        return settings().cpu.options.control == cpu::kControlLab && !s_cpuTrainingControl;
    }

    static bool recordingMode(int m)
    {
        return m == kModeRecord || m == kModeReRecord;
    }

    static bool playingMode(int m)
    {
        return m == kModePlayback || m == kModeReRecord;
    }

    // The mode in effect: Off without positions, storage, or (CPU) Lab control
    // when Controlled By does not own the CPU pad. A refusal is logged once per
    // cause.
    static int mode(int side)
    {
        int m = sideMode(side);
        int why = 0;
        if (m != kModeOff)
        {
            if (!s_start->saved() || !storageReady())
            {
                why = 1;
            }
            else if (s_detached)
            {
                why = 3;
            }
            else if (side == kCpu && controlledByActive())
            {
                // Controlled By owns the CPU pad and bypasses recording CPU mode.
                why = 4;
            }
            else if (side == kCpu && !cpuLab())
            {
                why = 2;
            }
        }
        if (why != s_side[side].refused)
        {
            s_side[side].refused = why;
            if (why == 4)
            {
                message("CPU Mode is off while Port Control is on.");
            }
            else if (why != 0)
            {
                TM_LOG("record: %s mode %d refused: %s\n", side == kHmn ? "HMN" : "CPU", m,
                       why == 1 ? "no saved positions"
                                : (why == 2 ? "CPU Options > CPU Control is not Lab"
                                            : "a state saved from other positions was loaded"));
            }
        }
        return why != 0 ? kModeOff : m;
    }

    static Slot* playSlot(int side)
    {
        return slotAt(side, s_side[side].playSlot);
    }

    // Never Random (a mode change moves it to Slot 1; until then nothing is recorded).
    static Slot* recordSlot(int side)
    {
        int s = slotSetting(side);
        return s > 0 ? slotAt(side, s - 1) : NULL;
    }

    static u32 slotEnd(Slot* s)
    {
        return s != NULL && !slotEmpty(*s) ? s->end : 0;
    }

    static int takeoverTarget()
    {
        if (mode(kHmn) == kModeReRecord)
        {
            return kTakeHmn;
        }
        if (mode(kCpu) == kModeReRecord)
        {
            return kTakeCpu;
        }
        return settings().recording.takeover;
    }

    // Weighted pick among the recorded slots (Random slot mode).
    static int randomSlot(int side)
    {
        const u8* chance = side == kHmn ? settings().recording.hmnChance : settings().recording.cpuChance;
        int total = 0;
        int first = -1;
        for (int i = 0; i < kSlotCount; i++)
        {
            Slot* s = slotAt(side, i);
            if (s != NULL && !slotEmpty(*s))
            {
                total += chance[i];
                first = first < 0 ? i : first;
            }
        }
        if (total == 0)
        {
            return first < 0 ? 0 : first;
        }
        int r = static_cast<int>(rnd() % 100u);
        for (int i = 0; i < kSlotCount; i++)
        {
            Slot* s = slotAt(side, i);
            if (s == NULL || slotEmpty(*s))
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

    static void resolveSlots()
    {
        for (int side = 0; side < kSides; side++)
        {
            int s = slotSetting(side);
            s_side[side].playSlot = s > 0 ? s - 1 : randomSlot(side);
            s_test.playSlot[side] = s_side[side].playSlot;
        }
    }

    // Back to frame 0 of the run (a new run, or a rewind's replay).
    static void restartSides()
    {
        for (int side = 0; side < kSides; side++)
        {
            SideState& s = s_side[side];
            s.cancelled = false;
            s.takenOver = false;
            s.counter = false;
            s.fullLogged = false;
            s.pendingClear = false;
        }
        s_t = 0;
        s_restoreTimer = 0;
        s_stepRan = false;
        s_sidesRan = 0;
        s_detached = false;
    }

    static void armRecordingIfNeeded()
    {
        s_recArmed = shouldArmRecording(settings().recording.startOnFirstInput != 0, mode(kHmn) == kModeRecord,
                                        mode(kCpu) == kModeRecord);
        for (int side = 0; side < kSides; side++)
        {
            s_side[side].pendingClear = s_recArmed && mode(side) == kModeRecord;
        }
    }

    static void resetRun()
    {
        s_runId++;
        restartSides();
        armRecordingIfNeeded();
        s_doubleLogged = false;
        s_rewind = kRewindNone;
        s_looped = false;
        for (int side = 0; side < kSides; side++)
        {
            s_logged[side] = 0;
            runLogClear(side);
            s_takeAt[side] = kNoTake;
        }
    }

    // The next game step's frame: s_t, or s_t + 1 between two steps of
    // one main-loop pass (afterStep() has not run yet).
    static u32 nextFrame()
    {
        return s_t + (s_stepRan ? 1 : 0);
    }

    static void requestRestore(const char* why)
    {
        if (!s_start->saved())
        {
            return;
        }
        s_restorePending = true;
        TM_LOG("record: restore requested (%s)\n", why);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // ---- controllers ----

    // Hands the CPU the player's controller (Control, Record, Re-Record, or
    // CPU playback takeover) by assigning the player's controller port to the
    // CPU's player data while holding the human fighter neutral (including P+'s
    // analog variables) so only the CPU acts on that pad.
    static int s_ctrlPlayer = -1; // the CPU player whose number we changed
    static s8 s_ctrlSaved = 0;
    static int s_ctrlOrigPlayer = -1;
    static s8 s_ctrlOrig = 0;

    static void cpuReadsPad(bool on)
    {
        int cpu = s_side[kCpu].player;
        u8* h = playerData(s_side[kHmn].player);
        if (s_ctrlPlayer >= 0 && (!on || s_ctrlPlayer != cpu || h == NULL))
        {
            u8* p = playerData(s_ctrlPlayer);
            if (p != NULL)
            {
                p[kPlayerController] = static_cast<u8>(s_ctrlSaved);
            }
            TM_LOG("record: P%d controller back to %d\n", s_ctrlPlayer + 1, s_ctrlSaved);
            s_ctrlPlayer = -1;
            game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
        }
        u8* c = playerData(cpu);
        if (!on || c == NULL || h == NULL)
        {
            return;
        }
        if (s_ctrlPlayer < 0)
        {
            s_ctrlSaved = cpu == s_ctrlOrigPlayer ? s_ctrlOrig : static_cast<s8>(c[kPlayerController]);
            s_ctrlPlayer = cpu;
            TM_LOG("record: P%d reads controller %d (was %d)\n", cpu + 1, h[kPlayerController], s_ctrlSaved);
        }
        game::trainingControllerSuppress(game::kControllerOwnerRecord, true);
        c[kPlayerController] = h[kPlayerController];
    }

    static void releaseSide(int side)
    {
        for (int i = 0; i < cpu::kMaxSlots; i++)
        {
            if (cpu::sourceOf(i) == &s_source[side])
            {
                cpu::release(i, "savestate loaded");
            }
        }
    }

    // Reconcile the CPU's controller byte and recording's pad-swap bookkeeping
    // to the current CPU Mode, both every pass (loaded=false) and immediately
    // after any savestate load (loaded=true). When CPU Options > Controlled By
    // is active it owns the CPU's controller byte, so recording yields its own
    // owner bit and bookkeeping without touching Controlled By's port or patch
    // owner bit.
    static void syncController(bool loaded)
    {
        if (controlledByActive())
        {
            s_swapPad = false;
            s_hmnNeutral = false;
            if (loaded)
            {
                // cpu::optionsLoaded() already reconciled the port and type;
                // only drop recording's own bookkeeping, patch owner bit and slots.
                s_ctrlPlayer = -1;
                game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
                releaseSide(kCpu);
                if (mode(kHmn) == kModeOff)
                {
                    releaseSide(kHmn);
                }
            }
            else
            {
                cpuReadsPad(false);
                game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
            }
            return;
        }
        int cm = mode(kCpu);
        const SideState& c = s_side[kCpu];
        s_swapPad = cm == kModeControl || cm == kModeRecord || cm == kModeReRecord
                    || (cm == kModePlayback && (takeoverTarget() == kTakeCpu || c.takenOver));
        s_hmnNeutral = s_swapPad;
        if (s_swapPad)
        {
            cpuReadsPad(true);
            return;
        }
        if (loaded)
        {
            int cpu = s_ctrlPlayer >= 0 ? s_ctrlPlayer : s_ctrlOrigPlayer;
            s8 orig = s_ctrlPlayer >= 0 ? s_ctrlSaved : s_ctrlOrig;
            if (cpu >= 0)
            {
                u8* p = playerData(cpu);
                if (p != NULL)
                {
                    p[kPlayerController] = static_cast<u8>(orig);
                }
            }
            s_ctrlPlayer = -1;
            game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
            if (cm == kModeOff)
            {
                releaseSide(kCpu);
            }
            if (mode(kHmn) == kModeOff && !s_hmnNeutral)
            {
                releaseSide(kHmn);
            }
            return;
        }
        cpuReadsPad(false);
        game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
    }

    static int hmnPort()
    {
        u8* h = playerData(s_side[kHmn].player);
        if (h == NULL)
        {
            return -1;
        }
        int c = static_cast<s8>(h[kPlayerController]) - 1;
        return c >= 0 && c < iface::kPadCount ? c : s_side[kHmn].player;
    }

    // ---- driving ----

    bool RecordSource::wants(const cpu::Owner& who)
    {
        return s_active && s_side[side].want && who.playerNo == s_side[side].player;
    }

    // P+'s analog variables (slots.h): instance work floats 34-37 and int
    // 77, written in the input's update by its ButtonPresses codeset (hook at
    // 0x80913190) before the fighter-input hook; read by its shield and
    // C-stick code after it.
    enum {
        kWorkInstance = 0x20,
        kWorkInts = 0xC,
        kWorkIntCount = 0x10,
        kWorkFloats = 0x14,
        kWorkFloatCount = 0x18,
        kPpCStickX = 34, // then C-stick y, analog L, analog R
        kPpPress = 77
    };

    static bool ppVars(int entryId, float*& floats, s32*& ints)
    {
        const soModuleEnumeration* m = game::fighterModules(cpu::fighterOf(entryId, false));
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return false;
        }
        const void* work = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, kWorkInstance));
        if (!game::isRam(work) || game::wordAt(work, kWorkFloatCount) <= kPpCStickX + 3
            || game::wordAt(work, kWorkIntCount) <= kPpPress)
        {
            return false;
        }
        floats = reinterpret_cast<float*>(game::wordAt(work, kWorkFloats));
        ints = reinterpret_cast<s32*>(game::wordAt(work, kWorkInts));
        return game::isRam(floats) && game::isRam(ints);
    }

    static void readInput(const void* input, int entryId, Input& out)
    {
        cpu::Frame f;
        cpu::readFrame(input, f);
        out.buttons = f.buttons;
        out.x = f.x;
        out.y = f.y;
        float* floats;
        s32* ints;
        if (ppVars(entryId, floats, ints))
        {
            out.cx = floats[kPpCStickX];
            out.cy = floats[kPpCStickX + 1];
            out.l = floats[kPpCStickX + 2];
            out.r = floats[kPpCStickX + 3];
            out.press = static_cast<u8>(ints[kPpPress]);
        }
        else
        {
            out.cx = out.cy = out.l = out.r = 0.0f;
            out.press = 0;
        }
    }

    static void writePp(int entryId, const Input& in)
    {
        float* floats;
        s32* ints;
        if (ppVars(entryId, floats, ints))
        {
            floats[kPpCStickX] = in.cx;
            floats[kPpCStickX + 1] = in.cy;
            floats[kPpCStickX + 2] = in.l;
            floats[kPpCStickX + 3] = in.r;
            ints[kPpPress] = in.press;
        }
    }

    bool RecordSource::next(const cpu::Owner& who, const cpu::Frame& produced, cpu::Frame& out)
    {
        const SideState& s = s_side[side];
        if (s.pass)
        {
            out = produced;
            return true;
        }
        Input in;
        if (s.neutral)
        {
            memset(&in, 0, sizeof(in));
            in.buttons = side == kHmn ? s_hmnFlick : produced.buttons & cpu::button::kFlickJump;
        }
        else
        {
            in = s.out;
        }
        out.buttons = in.buttons;
        out.x = in.x;
        out.y = in.y;
        writePp(who.entryId, in);
        return true;
    }

    static void cancel(int side, bool takenOver, const char* why)
    {
        SideState& s = s_side[side];
        if (s.cancelled)
        {
            return;
        }
        s.cancelled = true;
        s.takenOver = takenOver;
        s.counter = side == kCpu && !takenOver && mode(kCpu) == kModePlayback;
        if (takenOver)
        {
            s_test.takeover[side] = s_t;
        }
        else if (mode(side) == kModeReRecord && settings().recording.startOnFirstInput != 0)
        {
            s_recArmed = true;
        }
        TM_LOG("record: %s playback %s at frame %u\n", side == kHmn ? "HMN" : "CPU", why, s_t);
    }

    // Playback of frame t into `s.out` (neutral before the slot's first frame).
    static void play(int side)
    {
        SideState& s = s_side[side];
        s.want = true;
        if (!slotRead(*playSlot(side), static_cast<int>(s_t), s.out))
        {
            s.neutral = true;
        }
        else if (s_mirrored)
        {
            // A centred stick stays +0.0.
            s.out.x = s.out.x != 0.0f ? -s.out.x : 0.0f;
            s.out.cx = s.out.cx != 0.0f ? -s.out.cx : 0.0f;
        }
    }

    // Before the driver runs on the side's input this step.
    static void decide(int side)
    {
        SideState& s = s_side[side];
        int m = mode(side);
        s.want = false;
        s.pass = false;
        s.neutral = false;
        if (playingMode(m) && !s.cancelled)
        {
            Slot* slot = playSlot(side);
            if (slot != NULL && s_t < slotEnd(slot))
            {
                play(side);
                return;
            }
            if (side == kHmn && s_hmnNeutral)
            {
                // While the player drives the CPU, hold the player's own
                // fighter neutral even after its playback ends.
                s.want = true;
                s.neutral = true;
                return;
            }
            if (side == kCpu && m == kModePlayback
                && (settings().recording.cpuCounter == kCounterOff || takeoverTarget() == kTakeCpu))
            {
                // Counter Off (or CPU kept for takeover): hold CPU neutral
                // after its recording finishes.
                s.want = true;
                s.neutral = true;
                return;
            }
            cancel(side, false, "ended");
        }
        if (side == kHmn)
        {
            if (s_hmnNeutral)
            {
                s.want = true;
                s.neutral = true;
            }
            return;
        }
        if (m == kModeControl || m == kModeRecord || ((m == kModeReRecord || m == kModePlayback) && s.takenOver)
            || (m == kModeReRecord && s.cancelled))
        {
            s.want = s_swapPad;
            s.pass = s_swapPad;
        }
    }

    static int sideOf(const cpu::Owner& who)
    {
        if (who.sub)
        {
            return -1;
        }
        if (who.playerNo == s_side[kHmn].player)
        {
            return kHmn;
        }
        if (who.playerNo == s_side[kCpu].player)
        {
            return kCpu;
        }
        return -1;
    }

    static void onFighterInput(const iface::FighterInput* call)
    {
        TM_PERF_SCOPE(kInputRecord);
        cpu::Owner who;
        int side = -1;
        if (s_active && cpu::findOwner(call->input, call->entryId, who))
        {
            side = sideOf(who);
        }
        if (side == kCpu)
        {
            s_cpuTrainingControl = who.trainingControl && !controlledByActive();
        }
        if (side >= 0 && s_paused)
        {
            // Training's pause stops the match but not every fighter's
            // controller update: nothing is recorded or advanced then (a
            // source keeps writing the frame it wrote last).
            if (side == kHmn)
            {
                s_test.pausedCalls++;
            }
            side = -1;
        }
        if (side >= 0 && (s_sidesRan & (1 << side)) != 0)
        {
            s_stepRan = false;
            s_sidesRan = 0;
            afterStep();
            if (!s_doubleLogged)
            {
                s_doubleLogged = true;
                TM_LOG("record: two game steps in one pass of the main loop at frame %u\n", s_t);
            }
        }
        if (side >= 0)
        {
            s_sidesRan |= static_cast<u8>(1 << side);
            decide(side);
            SideState& s = s_side[side];
            if (s.want && cpu::sourceOf(who.slot) != &s_source[side])
            {
                cpu::take(who, &s_source[side], side == kCpu ? (cpu::kSilenceAi | cpu::kWithNana) : 0);
            }
        }
        if (s_innerInput != NULL)
        {
            s_innerInput(call);
        }
        if (side < 0)
        {
            return;
        }
        SideState& s = s_side[side];
        Slot* log = runLogSlot(side);
        if (s_rewind == kRewindReplay && s_t < s_rewindTarget)
        {
            // The input the fighter acted on when this frame first ran,
            // whatever produced it then (playback, the pad, the CPU).
            Input in;
            if (log != NULL && runLogRead(side, static_cast<int>(s_t), in))
            {
                u32* words = reinterpret_cast<u32*>(reinterpret_cast<u32>(call->input) + 0xC);
                words[0] = in.buttons;
                reinterpret_cast<float*>(words)[1] = in.x;
                reinterpret_cast<float*>(words)[2] = in.y;
                writePp(who.entryId, in);
            }
        }
        if (log != NULL && s_t <= s_logged[side] && s_t < kMaxFrames && positionsSaved())
        {
            // During rewind replay frames 0..s_rewindTarget-1 are already packed in
            // the run log; keep them intact until replay finishes, then log the new
            // future normally.
            if (s_rewind != kRewindReplay || s_t >= s_rewindTarget)
            {
                Input logged;
                readInput(call->input, who.entryId, logged);
                runLogWrite(side, static_cast<int>(s_t), logged);
            }
            s_logged[side] = s_t + 1;
        }
        int m = mode(side);
        if (m == kModeRecord || (m == kModeReRecord && s.cancelled))
        {
            Slot* slot = recordSlot(side);
            if (slot != NULL && s_t < kMaxFrames)
            {
                Input in;
                readInput(call->input, who.entryId, in);
                bool clearSlot = false;
                if (consumeArmedInput(s_recArmed, in, m == kModeRecord, clearSlot))
                {
                    if (clearSlot || s.pendingClear)
                    {
                        slotClear(*slot);
                        s.pendingClear = false;
                    }
                    slotWrite(*slot, static_cast<int>(s_t), in);
                }
            }
            else if (slot != NULL && !s.fullLogged)
            {
                s.fullLogged = true;
                TM_LOG("record: %s slot %d full at %u frames\n", side == kHmn ? "HMN" : "CPU",
                       slotSetting(side), kMaxFrames);
            }
        }
        if (side == kHmn)
        {
            s_test.steps++;
            s_test.stepT = s_t;
            if (!s.want)
            {
                s_hmnFlick = game::wordAt(call->input, 0xC) & cpu::button::kFlickJump;
            }
        }
        s_stepRan = true;
    }

    // ---- between steps ----

    static bool liveInput(const gfPadStatus& p)
    {
        u32 mask = kLiveButtons;
        if (p.m_controllerType == gfPadType::WIIMOTE)
        {
            mask |= game::kPadDLeft | game::kPadDRight | game::kPadDDown | game::kPadWiiA;
        }
        if ((p.m_buttonsCurrentFrame.bits & mask) != 0)
        {
            return true;
        }
        int v[4] = {p.m_stickX, p.m_stickY, p.m_subStickX, p.m_subStickY};
        for (int i = 0; i < 4; i++)
        {
            if (v[i] >= kLiveStick || v[i] <= -kLiveStick)
            {
                return true;
            }
        }
        return p.m_lTriggerAnalog >= kLiveTrigger || p.m_rTriggerAnalog >= kLiveTrigger;
    }

    static void findPlayers()
    {
        int hmn = -1;
        int cpu = -1;
        for (int p = 0; p < kPorts; p++)
        {
            u8* d = playerData(p);
            if (d == NULL)
            {
                break;
            }
            if (d[kPlayerState] == kStateHuman && hmn < 0)
            {
                hmn = p;
            }
            else if (d[kPlayerState] == kStateCpu && cpu < 0)
            {
                cpu = p;
            }
        }
        s_side[kHmn].player = hmn;
        s_side[kCpu].player = cpu;
        if (cpu >= 0 && (cpu != s_ctrlOrigPlayer || game::trainingControllerOwners() == 0))
        {
            u8* c = playerData(cpu);
            if (c != NULL)
            {
                s_ctrlOrig = static_cast<s8>(c[kPlayerController]);
                s_ctrlOrigPlayer = cpu;
            }
        }
    }

    static bool sideView(int side, cpu::View& v)
    {
        if (s_side[side].player < 0)
        {
            return false;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].playerNo == s_side[side].player && cpu::readView(refs[i].fighter, v))
            {
                return true;
            }
        }
        return false;
    }

    static bool victim(int side)
    {
        cpu::View v;
        return sideView(side, v) && v.hitlag > 0 && v.hitstun > 0;
    }

    static int cpuStatus()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            cpu::View v;
            if (refs[i].playerNo == s_side[kCpu].player && cpu::readView(refs[i].fighter, v))
            {
                return v.status;
            }
        }
        return -1;
    }

    // Slot chances: a slot that gets inputs takes an equal share, and one that
    // loses inputs returns its share to the remaining recorded slots.
    static void rebalance(u8* chance, u32 enabled, int keep)
    {
        int count = 0;
        int others = 0;
        for (int i = 0; i < kSlotCount; i++)
        {
            if (!(enabled & (1u << i)))
            {
                chance[i] = 0;
            }
            else
            {
                count++;
                others += i == keep ? 0 : chance[i];
            }
        }
        if (count == 0)
        {
            return;
        }
        int left = 100 - (keep >= 0 ? chance[keep] : 0);
        int given = 0;
        int last = -1;
        for (int i = 0; i < kSlotCount; i++)
        {
            if (!(enabled & (1u << i)) || i == keep)
            {
                continue;
            }
            int n = keep >= 0 ? count - 1 : count;
            chance[i] = static_cast<u8>(others > 0 ? chance[i] * left / others : left / n);
            given += chance[i];
            last = i;
        }
        if (last >= 0)
        {
            chance[last] = static_cast<u8>(chance[last] + left - given);
        }
        else if (keep >= 0)
        {
            chance[keep] = 100;
        }
    }

    static void updateChances()
    {
        for (int side = 0; side < kSides; side++)
        {
            u32 has = 0;
            for (int i = 0; i < kSlotCount; i++)
            {
                Slot* s = slotAt(side, i);
                if (s != NULL && !slotEmpty(*s))
                {
                    has |= 1u << i;
                }
            }
            if (has == s_hadInputs[side])
            {
                continue;
            }
            u8* chance = side == kHmn ? settings().recording.hmnChance : settings().recording.cpuChance;
            u32 added = has & ~s_hadInputs[side];
            s_hadInputs[side] = has;
            int keep = -1;
            int count = 0;
            for (int i = 0; i < kSlotCount; i++)
            {
                count += (has >> i) & 1;
                if (added & (1u << i))
                {
                    keep = i;
                }
            }
            if (keep >= 0)
            {
                chance[keep] = static_cast<u8>(100 / count);
            }
            rebalance(chance, has, keep);
        }
    }

    static u32 playbackEnd(bool& any)
    {
        u32 end = 0;
        any = false;
        for (int side = 0; side < kSides; side++)
        {
            if (mode(side) == kModePlayback)
            {
                any = true;
                u32 e = slotEnd(playSlot(side));
                end = e > end ? e : end;
            }
        }
        return end;
    }

    // After a game step ran: CPU Counter, Loop, Auto Restore.
    static void afterStep()
    {
        bool anyPlaying = hasActivePlayback(playingMode(mode(kHmn)), s_side[kHmn].cancelled,
                                            playingMode(mode(kCpu)), s_side[kCpu].cancelled);
        if (!shouldHoldRecordingClock(s_recArmed, anyPlaying) && s_t < 0x7FFFFFFF)
        {
            s_t++;
        }
        SideState& c = s_side[kCpu];
        int cm = mode(kCpu);
        int counterRule = settings().recording.cpuCounter;
        if (cm == kModePlayback && !c.cancelled && takeoverTarget() != kTakeCpu)
        {
            // On CPU Hit: count each hit (and grab) the CPU takes and end
            // playback on one whose counter is enabled; CPU Options then runs
            // the counter on the first actionable frame (cpu::playbackCounterHit).
            // On Any Hit hands a CPU hit over as a new hit.
            cpu::View v;
            bool counted = counterRule == kCounterCpuHit && sideView(kCpu, v)
                           && cpu::playbackCounterHit(s_side[kCpu].player, v.hitlag, v.hitstun, v.status);
            bool cpuHit = counterRule == kCounterAnyHit && victim(kCpu);
            bool hmnHit = (counterRule == kCounterHmnHit || counterRule == kCounterAnyHit) && victim(kHmn);
            if (cpuHit)
            {
                cpu::playbackHitHanded(s_side[kCpu].player);
            }
            if (counted || cpuHit || hmnHit)
            {
                cancel(kCpu, false, counted || cpuHit ? "countered (CPU hit)" : "countered (HMN hit)");
            }
        }
        bool any = false;
        u32 end = playbackEnd(any);
        bool recording = recordingMode(mode(kHmn)) || recordingMode(cm);
        bool allow = !recording && cm != kModeControl;
        bool past = end > 0 && s_t >= end;
        if (settings().recording.loop && allow && past)
        {
            s_t = 0;
            s_looped = true;
            for (int side = 0; side < kSides; side++)
            {
                if (slotSetting(side) == 0)
                {
                    s_side[side].playSlot = randomSlot(side);
                    s_test.playSlot[side] = s_side[side].playSlot;
                }
            }
            s_test.loops++;
            TM_LOG("record: loop at frame %u\n", end);
        }
        if (allow)
        {
            int rule = settings().recording.autoRestore;
            bool restore = false;
            if (rule == kAutoPlaybackEnds)
            {
                restore = past;
            }
            else if (rule == kAutoCounter)
            {
                int st = cpuStatus();
                restore = c.counter || game::outOfPlay(st, false) || st == kStatusCliffWait;
            }
            if (restore || s_restoreTimer > 0)
            {
                s_restoreTimer++;
            }
            if (s_restoreTimer >= kAutoRestoreDelay)
            {
                s_restoreTimer = 0;
                requestRestore("auto");
            }
        }
        updateChances();
    }

    // The match's frame counter, +1 per game step (g_GameFrame +0x4, in the
    // GameGlobal heap; the address is not a symbol: measured, setup not
    // recorded; the savestate restores it). For the tests only.
    static const volatile u32* const kGameFrame = reinterpret_cast<const volatile u32*>(0x901812A4);

    // Each side's damage when the run started (`put` false), or that damage
    // given back (`put` true: a rewind's load rolled its random percent again).
    static void startDamages(bool put)
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            for (int side = 0; side < kSides; side++)
            {
                if (refs[i].playerNo != s_side[side].player)
                {
                    continue;
                }
                if (!put)
                {
                    s_startDamage[side] = game::damage(refs[i]);
                }
                else if (game::damage(refs[i]) != s_startDamage[side])
                {
                    game::setDamage(refs[i], s_startDamage[side]);
                }
            }
        }
    }

    // A restore just happened (lite or savestate): the run starts again.
    static void restarted(const char* how)
    {
        s_test.startFrame = *kGameFrame;
        resetRun();
        startDamages(false);
        resolveSlots();
        s_test.restores++;
        bool any = playingMode(mode(kHmn)) || playingMode(mode(kCpu));
        s_holding = settings().recording.startPaused && any;
        TM_LOG("record: restored (%s%s), slots %d/%d%s\n", how, s_mirrored ? ", mirrored" : "",
               s_side[kHmn].playSlot + 1, s_side[kCpu].playSlot + 1, s_holding ? ", held until input" : "");
    }

    static void pickMirror()
    {
        int mirror = settings().recording.mirrored;
        s_mirrored = mirror == kMirrorOn || (mirror == kMirrorRandom && (rnd() & 1) != 0);
    }

    static void applyRestore()
    {
        s_restorePending = false;
        pickMirror();
        int extra[kSides];
        extra[kHmn] = settings().recording.randomPercent[main::kSideHuman];
        extra[kCpu] = settings().recording.randomPercent[main::kSideCpu];
        for (int side = 0; side < kSides; side++)
        {
            extra[side] = extra[side] > 0 ? static_cast<int>(rnd() % static_cast<u32>(extra[side] + 1)) : 0;
        }
        const char* why = NULL;
        if (!s_start->restore(s_mirrored, extra, why))
        {
            message(why);
            return;
        }
        restarted(s_start->name());
    }

    // The savestate brought the recording's start state back (its random
    // percent added). It cannot be mirrored: a mirrored run then places the
    // fighters mirrored with the lite placement, keeping their damage.
    static void rewound();

    static void afterLoad()
    {
        s_restorePending = false;
        if (s_rewind == kRewindLoad)
        {
            rewound();
            return;
        }
        pickMirror();
        if (s_mirrored)
        {
            const char* why = NULL;
            if (!s_start->restore(true, NULL, why))
            {
                message(why);
            }
        }
        restarted("savestate");
    }

    static void clearSlots();
    static void message(const char* text);

    // Save / Re-Save / Prune Positions, once the savestate took the state
    // (exact) or refused it for good (the lite state then).
    static void finishSave(bool exact, const char* why)
    {
        int kind = s_savePending;
        s_savePending = kSaveNone;
        Players who = s_start->players();
        int frames = static_cast<int>(s_t);
        if (kind == kSaveNew)
        {
            clearSlots();
        }
        else if (kind == kSavePrune)
        {
            for (int side = 0; side < kSides; side++)
            {
                for (int i = 0; i < kSlotCount; i++)
                {
                    Slot* sl = slotAt(side, i);
                    if (sl != NULL)
                    {
                        slotDropFront(*sl, frames);
                    }
                }
            }
            TM_LOG("record: dropped the first %d frames of every slot\n", frames);
        }
        s_gen++;
        s_exact = exact;
        s_mirrored = false;
        s_test.startFrame = *kGameFrame;
        resetRun();
        startDamages(false);
        resolveSlots();
        TM_LOG("record: positions %s (%s%s%s), P%d and P%d\n",
               kind == kSaveNew ? "saved"
                                : (kind == kSaveAgain ? "saved again" : (kind == kSaveImport ? "imported" : "pruned")),
               exact ? "savestate" : "lite", exact ? "" : ": ", exact ? "" : why, who.hmn + 1, who.cpu + 1);
        if (exact)
        {
            s_restorePending = false;
        }
        else
        {
            // The lite state is not the fighters' exact state: every run,
            // the first recording included, starts from a restore. A
            // savestate of earlier positions is no use any more.
            savestate::dropRecordingState();
            s_restorePending = true;
            message("Positions saved without a savestate.");
        }
    }

    static void onQuickLoaded()
    {
        if (!positionsSaved())
        {
            return;
        }
        s_rewind = kRewindNone;
        if (s_quick.valid && s_quick.gen == s_gen)
        {
            // The match is back where the quick state was taken in this run
            // of the positions: so is the recording. Its log holds this
            // frame's past; the next frame logged drops the rest.
            s_t = s_quick.t;
            s_test.startFrame = *kGameFrame - s_t;
            s_restoreTimer = s_quick.restoreTimer;
            s_mirrored = s_quick.mirrored;
            s_looped = s_quick.looped;
            s_holding = s_quick.holding;
            if (shouldRestoreQuickArmed(settings().recording.startOnFirstInput != 0,
                                        s_quick.mode[kHmn], mode(kHmn), s_quick.mode[kCpu], mode(kCpu)))
            {
                s_recArmed = s_quick.recArmed;
                for (int side = 0; side < kSides; side++)
                {
                    s_side[side].pendingClear = s_recArmed && mode(side) == kModeRecord;
                }
            }
            else
            {
                s_recArmed = false;
                for (int side = 0; side < kSides; side++)
                {
                    s_side[side].pendingClear = false;
                }
                if (s_t == 0)
                {
                    armRecordingIfNeeded();
                }
            }
            s_stepRan = false;
            s_sidesRan = 0;
            s_detached = false;
            s_restorePending = false;
            bool otherRun = s_quick.runId != s_runId;
            // Re-roll any Random slot on a quick savestate load.
            for (int side = 0; side < kSides; side++)
            {
                int cur = mode(side);
                bool sameMode = s_quick.mode[side] == cur;
                s_side[side].cancelled = sameMode ? s_quick.cancelled[side] : false;
                s_side[side].takenOver = sameMode ? s_quick.takenOver[side] : false;
                s_side[side].counter = sameMode ? s_quick.counter[side] : false;
                s_takeAt[side] = sameMode ? s_quick.takeAt[side] : static_cast<u32>(kNoTake);
                if (side == kCpu && cur == kModePlayback && takeoverTarget() == kTakeCpu
                    && s_side[side].cancelled && !s_side[side].takenOver)
                {
                    s_side[side].cancelled = false;
                    s_side[side].counter = false;
                }
                if (otherRun)
                {
                    s_logged[side] = 0;
                    runLogClear(side);
                }
                if (slotSetting(side) == 0)
                {
                    s_side[side].playSlot = randomSlot(side);
                    s_test.playSlot[side] = s_side[side].playSlot;
                }
            }
            TM_LOG("record: quick state loaded: back to frame %u, slots %d/%d\n", s_t, s_side[kHmn].playSlot + 1,
                   s_side[kCpu].playSlot + 1);
            return;
        }
        s_stepRan = false;
        s_sidesRan = 0;
        s_detached = true;
        s_holding = false;
        s_recArmed = false;
        for (int side = 0; side < kSides; side++)
        {
            s_side[side].pendingClear = false;
        }
        TM_LOG("record: quick state from other positions loaded: recording and playback stop until a restore\n");
        if (sideMode(kHmn) != kModeOff || sideMode(kCpu) != kModeOff)
        {
            message("Recording stopped: the state is from other positions.");
        }
    }

    static void updateTest();

    static void onSavestate(int slot, int event, bool passing, const char* why)
    {
        if (slot == savestate::kSlotQuick)
        {
            if (event == savestate::kEventSaved)
            {
                s_quick.valid = true;
                s_quick.gen = s_gen;
                s_quick.runId = s_runId;
                s_quick.t = s_t;
                s_quick.restoreTimer = s_restoreTimer;
                s_quick.mirrored = s_mirrored;
                s_quick.looped = s_looped;
                s_quick.holding = s_holding;
                s_quick.recArmed = s_recArmed;
                for (int side = 0; side < kSides; side++)
                {
                    s_quick.mode[side] = mode(side);
                    s_quick.cancelled[side] = s_side[side].cancelled;
                    s_quick.takenOver[side] = s_side[side].takenOver;
                    s_quick.counter[side] = s_side[side].counter;
                    s_quick.takeAt[side] = s_takeAt[side];
                }
            }
            else if (event == savestate::kEventLoaded)
            {
                onQuickLoaded();
                syncController(true);
                updateTest();
            }
            return;
        }
        if (event == savestate::kEventSaved)
        {
            if (savestate::droppedOther())
            {
                s_quick.valid = false;
            }
            finishSave(true, NULL);
        }
        else if (event == savestate::kEventLoaded)
        {
            afterLoad();
            syncController(true);
            updateTest();
        }
        else if (!passing && s_rewind == kRewindLoad)
        {
            // The rewind is given up; the run goes on where it was.
            s_rewind = kRewindNone;
            s_restorePending = false;
            s_test.refused++;
            TM_LOG("record: frame decrement refused (load: %s)\n", why);
        }
        else if (!passing)
        {
            if (s_savePending != kSaveNone)
            {
                finishSave(false, why);
            }
            else if (s_restorePending)
            {
                // Lite from now on: restored here, between two steps.
                s_exact = false;
                TM_LOG("record: savestate refused (%s): lite restore\n", why);
                message("Restored without the savestate.");
                applyRestore();
            }
        }
    }

    static void runRequest();

    // An imported recording waiting for both fighters (see importStart).
    struct ImportStart {
        bool pending;
        LitePlacement place[2];
    };
    static ImportStart s_import;

    static void runImport()
    {
        Players who;
        who.hmn = s_side[kHmn].player;
        who.cpu = s_side[kCpu].player;
        if (who.hmn < 0 || who.cpu < 0 || !storageReady())
        {
            return;
        }
        s_import.pending = false;
        const char* why = NULL;
        int damage[kSides] = {0, 0};
        if (!liteSet(who, s_import.place, why) || !s_start->restore(false, damage, why))
        {
            message(why);
            return;
        }
        // Saved here, before a step runs (beforeTop asks in this pass).
        s_savePending = kSaveImport;
        TM_LOG("record: imported positions placed: P%d (%.2f, %.2f) P%d (%.2f, %.2f)\n", who.hmn + 1,
               s_import.place[0].x, s_import.place[0].y, who.cpu + 1, s_import.place[1].x, s_import.place[1].y);
    }

    void importStart(const LitePlacement place[2])
    {
        s_import.place[0] = place[0];
        s_import.place[1] = place[1];
        s_import.pending = true;
    }

    bool importPending()
    {
        return s_import.pending;
    }

    bool storageGaveUp()
    {
        return !storageReady() && s_storageTries >= kStorageTries;
    }

    static void updateTest()
    {
        TestBlock& b = s_test;
        b.t[0] = s_t;
        b.t[1] = s_t;
        b.flags = (s_start->saved() ? kFlagSaved : 0) | (s_holding ? kFlagHolding : 0)
                  | (s_side[kHmn].cancelled ? kFlagHmnCancelled : 0) | (s_side[kCpu].cancelled ? kFlagCpuCancelled : 0)
                  | (s_ctrlPlayer >= 0 ? kFlagPadSwapped : 0) | (storageReady() ? kFlagStorage : 0)
                  | (s_mirrored ? kFlagMirrored : 0) | (s_hmnNeutral ? kFlagHmnNeutral : 0)
                  | (s_exact ? kFlagExact : 0) | (s_detached ? kFlagDetached : 0)
                  | (s_savePending != kSaveNone ? kFlagSavePending : 0)
                  | (s_message != NULL ? kFlagMessage : 0)
                  | (s_recArmed ? kFlagRecArmed : 0);
        b.storage = reinterpret_cast<u32>(storageAddress());
        b.bytes = storageBytes();
        b.lossy = 0;
        for (int side = 0; side < kSides; side++)
        {
            for (int i = 0; i < kSlotCount; i++)
            {
                Slot* s = slotAt(side, i);
                b.start[side][i] = s != NULL ? s->start : kNoFrame;
                b.end[side][i] = s != NULL ? s->end : 0;
                b.lossy += s != NULL ? s->lossy : 0;
            }
        }
    }

    static void onGamePads(gfPadStatus* pads)
    {
        game::MatchState m = game::matchState();
        if (!m.running)
        {
            return;
        }
        bool paused = m.paused || lab::menuOpen();
        s_paused = paused;
        if (paused)
        {
            updateTest();
            return;
        }
        if (s_restorePending && !s_exact)
        {
            if (!lab::savestateBusy())
            {
                applyRestore();
            }
        }
        else if ((s_restorePending || s_savePending != kSaveNone) && !lab::savestateBusy())
        {
            // The savestate runs at the next top of the main loop: no step
            // in between (lab menu actions and test requests arrive here).
            game::setFrozen(true);
        }
        // A replay takes no live input: it repeats the run's takeovers.
        bool live = s_live && s_rewind == kRewindNone;
        int taken = -1;
        if (live && !s_restorePending)
        {
            int target = takeoverTarget();
            if (target == kTakeHmn && playingMode(mode(kHmn)) && !s_hmnNeutral)
            {
                taken = kHmn;
            }
            else if (target == kTakeCpu && playingMode(mode(kCpu)))
            {
                taken = kCpu;
            }
        }
        else if (s_rewind == kRewindReplay)
        {
            taken = s_takeAt[kHmn] == nextFrame() ? kHmn : (s_takeAt[kCpu] == nextFrame() ? kCpu : -1);
        }
        if (taken >= 0 && !s_side[taken].cancelled)
        {
            s_takeAt[taken] = nextFrame();
            cancel(taken, true, "taken over");
        }
        syncController(false);
        u8 startOnInput = settings().recording.startOnFirstInput;
        if (startOnInput != s_prevStartOnInput)
        {
            s_prevStartOnInput = startOnInput;
            if (startOnInput == 0)
            {
                s_recArmed = false;
            }
            else if (s_t == 0)
            {
                armRecordingIfNeeded();
            }
        }
        if (s_holding)
        {
            if (live)
            {
                s_holding = false;
                TM_LOG("record: Start Paused released by input\n");
            }
            else
            {
                game::setFrozen(true);
                s_test.held++;
            }
        }
        updateTest();
    }

    // Before the savestate code at the top of the main loop: the last step's
    // bookkeeping, then what the savestate should do in this pass.
    static void beforeTop()
    {
        game::MatchState m = game::matchState();
        if (!m.running)
        {
            return;
        }
        if (!storageReady() && s_storageTries < kStorageTries)
        {
            // Only the first and the last refusal are logged (the heap as
            // the wait began and as it was given up).
            int n = s_storageTries++;
            storageAllocate(n > 0 && n < kStorageTries - 1);
        }
        if (s_stepRan)
        {
            s_stepRan = false;
            s_sidesRan = 0;
            afterStep();
        }
        findPlayers();
        runRequest();
        if (m.paused || lab::menuOpen())
        {
            return;
        }
        if (s_messageTimer > 0 && --s_messageTimer == 0)
        {
            s_message = NULL;
        }
        if (s_import.pending && s_savePending == kSaveNone && !s_restorePending)
        {
            runImport();
        }
        if (lab::savestateBusy() || game::otherStopBits() != 0)
        {
            return;
        }
        // Play feedback sounds on saves and manual/auto restores; stay silent
        // during frame-decrement rewind loads.
        if (s_savePending != kSaveNone)
        {
            savestate::requestRecordingSave(true);
        }
        else if (s_restorePending && s_exact)
        {
            if (savestate::recordingStateValid())
            {
                savestate::requestRecordingLoad(s_rewind == kRewindNone);
            }
            else
            {
                s_exact = false;
                TM_LOG("record: the positions' savestate is gone: lite restore\n");
                message("Restored without the savestate.");
            }
        }
    }

    // gfApplication +0xFC: the main loop's step budget; gfPadSystem +0x30:
    // video frames since the last pass. How a budget of 1 - 60 (k - 1) runs
    // k steps, and why the frame count is zeroed: docs/internals.md, "The
    // main loop's step budget and the rewind burst".
    enum { kStepBudget = 0xFC, kBurstSteps = 60, kPadFrames = 0x30 };
    static bool s_replayed = false;

    static void forgetFrames()
    {
        gfPadSystem* pads = g_gfPadSystem;
        if (game::isRam(pads))
        {
            *reinterpret_cast<s32*>(reinterpret_cast<u32>(pads) + kPadFrames) = 0;
        }
    }

    static void burst()
    {
        u32 left = s_rewindTarget - s_t;
        gfApplication* app = g_gfApplication;
        s_test.rewindPasses++;
        if (left < 2 || !game::isRam(app))
        {
            return;
        }
        s32 k = static_cast<s32>(left < kBurstSteps ? left : kBurstSteps);
        *reinterpret_cast<s32*>(reinterpret_cast<u32>(app) + kStepBudget) = 1 - 60 * (k - 1);
    }

    static void onPads(u32 phase, gfPadStatus* pads)
    {
        TM_PERF_SCOPE(kPadsRecord);
        if (s_active && phase == iface::kPadsSystem)
        {
            beforeTop();
        }
        if (s_innerPads != NULL)
        {
            s_innerPads(phase, pads);
        }
        if (s_active && phase == iface::kPadsSystem)
        {
            // The fighters' controllers read this pass's system pads (a
            // press reached the player's input a step before the game pads
            // showed it; measured, setup not recorded), so live input is
            // taken here, after the lab hid what it uses.
            int port = hmnPort();
            s_live = port >= 0 && port < iface::kPadCount && game::padConnectedInMatch(port, pads[port])
                     && liveInput(pads[port]);
            if (s_rewind == kRewindReplay || s_replayed)
            {
                forgetFrames();
                s_replayed = s_rewind == kRewindReplay;
            }
            if (s_rewind == kRewindReplay)
            {
                burst();
            }
        }
        if (s_active && phase == iface::kPadsGame)
        {
            onGamePads(pads);
        }
    }

    // ---- menu actions ----

    bool positionsSaved()
    {
        return s_start != NULL && s_start->saved();
    }

    static void clearSlots()
    {
        for (int side = 0; side < kSides; side++)
        {
            for (int i = 0; i < kSlotCount; i++)
            {
                Slot* s = slotAt(side, i);
                if (s != NULL)
                {
                    slotClear(*s);
                }
            }
        }
    }

    static bool savePositions(int kind)
    {
        if (!storageReady() && !storageAllocate())
        {
            message("No room for the recording in memory.");
            return false;
        }
        // The lite state is taken now (the menu's rows follow it at once);
        // the savestate at the next top of the main loop that allows one,
        // with the game held in between.
        Players who;
        who.hmn = s_side[kHmn].player;
        who.cpu = s_side[kCpu].player;
        const char* why = NULL;
        if (who.hmn < 0 || who.cpu < 0)
        {
            why = "both fighters must be in the match";
        }
        if (why != NULL || !s_start->save(who, why))
        {
            message(why);
            return false;
        }
        s_savePending = kind;
        TM_LOG("record: positions to save (%d)\n", kind);
        return true;
    }

    void saveOrRestore()
    {
        if (positionsSaved())
        {
            requestRestore("Restore Positions");
            return;
        }
        savePositions(kSaveNew);
    }

    void resavePositions()
    {
        if (positionsSaved())
        {
            savePositions(kSaveAgain);
        }
    }

    void prunePositions()
    {
        if (positionsSaved())
        {
            savePositions(kSavePrune);
        }
    }

    static void resetRows()
    {
        main::RecordingSettings& r = settings().recording;
        r.hmnMode = 0;
        r.cpuMode = 0;
        r.hmnSlot = 1;
        r.cpuSlot = 1;
        r.mirrored = kMirrorOff;
        for (int i = 0; i < kSlotCount; i++)
        {
            r.hmnChance[i] = 0;
            r.cpuChance[i] = 0;
        }
        s_hadInputs[kHmn] = 0;
        s_hadInputs[kCpu] = 0;
    }

    char g_deletePosLabel[12];
    char g_deleteSlotLabel[12];

    void clearDeleteConfirm()
    {
        g_deletePosLabel[0] = '\0';
        g_deleteSlotLabel[0] = '\0';
    }

    void deletePositionsPressed()
    {
        if (g_deletePosLabel[0] == '\0')
        {
            strcpy(g_deletePosLabel, "A: Confirm");
            return;
        }
        g_deletePosLabel[0] = '\0';
        deletePositions();
    }

    void deletePositions()
    {
        clearDeleteConfirm();
        s_start->clear();
        savestate::dropRecordingState();
        s_exact = false;
        s_savePending = kSaveNone;
        s_quick.valid = false;
        clearSlots();
        resetRows();
        resetRun();
        s_restorePending = false;
        s_holding = false;
        s_recArmed = false;
        for (int side = 0; side < kSides; side++)
        {
            s_side[side].pendingClear = false;
        }
        s_mirrored = false;
        TM_LOG("record: positions deleted\n");
    }

    // Mode changes: recording never targets Random, disables Loop and Auto
    // Restore, and Playback / Re-Record immediately restore saved positions.
    static void modeChanged(int side)
    {
        main::RecordingSettings& r = settings().recording;
        int m = sideMode(side);
        if (side == kCpu && m != kModeOff && controlledByActive())
        {
            s_side[kCpu].refused = 4;
            message("CPU Mode is off while Port Control is on.");
            TM_LOG("record: CPU mode %d\n", m);
            return;
        }
        if (recordingMode(m))
        {
            if (side == kHmn && r.hmnSlot == 0)
            {
                r.hmnSlot = 1;
            }
            if (side == kCpu && r.cpuSlot == 0)
            {
                r.cpuSlot = 1;
            }
            r.loop = 0;
            r.autoRestore = kAutoOff;
            resolveSlots();
        }
        s_side[side].cancelled = false;
        s_side[side].takenOver = false;
        s_side[side].counter = false;
        armRecordingIfNeeded();
        bool activePlay = hasActivePlayback(playingMode(mode(kHmn)), s_side[kHmn].cancelled,
                                            playingMode(mode(kCpu)), s_side[kCpu].cancelled);
        if (s_recArmed && !activePlay)
        {
            s_t = 0;
        }
        TM_LOG("record: %s mode %d\n", side == kHmn ? "HMN" : "CPU", m);
        if (playingMode(m))
        {
            requestRestore("mode");
        }
    }

    void hmnModeChanged()
    {
        modeChanged(kHmn);
    }

    void cpuModeChanged()
    {
        modeChanged(kCpu);
    }

    void reconcileController()
    {
        if (!s_active)
        {
            return;
        }
        syncController(false);
        updateTest();
    }

    // Called from extra/fighter_swap.cpp before triggering a mid-match swap.
#pragma force_active on
    void prepareSwap()
    {
        if (!s_active)
        {
            return;
        }
        deletePositions();
        for (int side = 0; side < kSides; side++)
        {
            s_side[side].want = false;
            for (int slot = 0; slot < cpu::kMaxSlots; slot++)
            {
                if (cpu::sourceOf(slot) == &s_source[side])
                {
                    cpu::release(slot, "fighter swap");
                }
            }
        }
        cpuReadsPad(false);
        game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
        s_swapPad = false;
        s_hmnNeutral = false;
        s_import.pending = false;
        s_rewind = kRewindNone;
        s_message = NULL;
        s_messageTimer = 0;
        updateTest();
    }
#pragma force_active reset

    void slotChanged()
    {
        main::RecordingSettings& r = settings().recording;
        if (recordingMode(sideMode(kHmn)) && r.hmnSlot == 0)
        {
            r.hmnSlot = 1;
        }
        if (recordingMode(sideMode(kCpu)) && r.cpuSlot == 0)
        {
            r.cpuSlot = 1;
        }
        resolveSlots();
        requestRestore("slot");
    }

    void mirroredChanged()
    {
        requestRestore("mirrored playback");
    }

    bool cpuRowsAvailable()
    {
        return positionsSaved() && settings().cpu.options.control == cpu::kControlLab
               && settings().recording.mirrored == kMirrorOff;
    }

    bool modeRowsAvailable()
    {
        return positionsSaved() && settings().recording.mirrored == kMirrorOff;
    }

    bool loopRowsAvailable()
    {
        return positionsSaved() && !recordingMode(sideMode(kHmn)) && !recordingMode(sideMode(kCpu));
    }

    bool mirrorRowAvailable()
    {
        return loopRowsAvailable() && (playingMode(sideMode(kHmn)) || playingMode(sideMode(kCpu)));
    }

    bool slotHasInputs(int side, int slot)
    {
        Slot* s = slotAt(side, slot);
        return s != NULL && !slotEmpty(*s);
    }

    void chanceChanged(int side, int slot)
    {
        u8* chance = side == kHmn ? settings().recording.hmnChance : settings().recording.cpuChance;
        rebalance(chance, s_hadInputs[side], slot);
    }

    void deleteManagedSlotPressed()
    {
        if (g_deleteSlotLabel[0] == '\0')
        {
            strcpy(g_deleteSlotLabel, "A: Confirm");
            return;
        }
        g_deleteSlotLabel[0] = '\0';
        deleteManagedSlot();
    }

    void deleteManagedSlot()
    {
        g_deleteSlotLabel[0] = '\0';
        const main::RecordingSettings& r = settings().recording;
        Slot* s = slotAt(r.manageSide, r.manageSlot);
        if (s != NULL)
        {
            slotClear(*s);
            TM_LOG("record: %s slot %d deleted\n", r.manageSide == kHmn ? "HMN" : "CPU", r.manageSlot + 1);
        }
    }

    void copyManagedSlot()
    {
        const main::RecordingSettings& r = settings().recording;
        Slot* from = slotAt(r.manageSide, r.manageSlot);
        Slot* to = slotAt(r.manageSide, r.manageCopyTo);
        if (from != NULL && to != NULL)
        {
            slotCopy(*to, *from);
            TM_LOG("record: %s slot %d copied to slot %d\n", r.manageSide == kHmn ? "HMN" : "CPU", r.manageSlot + 1,
                   r.manageCopyTo + 1);
        }
    }

    // Only the extra module calls it: kept from the linker's stripping.
#pragma force_active on
    void showMessage(const char* text)
    {
        message(text);
    }
#pragma force_active reset

    // ---- Frame Decrement ----

    bool rewindRequest(bool press)
    {
        if (s_rewind != kRewindNone || s_restorePending || s_savePending != kSaveNone || s_import.pending)
        {
            return false; // the last one is still running: a held button tries again
        }
        // Why it cannot: 1 no savestate start, 2 no playback, 3 nothing to
        // go back to in this run (its first frame, a loop, a gap in the log).
        int why = 0;
        if (!s_exact || !positionsSaved() || !savestate::recordingStateValid())
        {
            why = 1;
        }
        else if ((!playingMode(sideMode(kHmn)) && !playingMode(sideMode(kCpu))) || s_detached)
        {
            why = 2;
        }
        else if (s_t == 0 || s_looped || s_logged[kHmn] + 1 < s_t || s_logged[kCpu] + 1 < s_t)
        {
            why = 3;
        }
        if (why != 0)
        {
            if (press)
            {
                s_test.refused++;
                TM_LOG("record: frame decrement refused (%d)\n", why);
            }
            return false;
        }
        s_rewind = kRewindLoad;
        s_rewindTarget = s_t - 1;
        s_rewindStart = OSGetTick();
        s_test.rewinds++;
        s_test.rewindTarget = s_rewindTarget;
        s_test.rewindPasses = 0;
        requestRestore("frame decrement");
        return true;
    }

    // The start state is back: the replay starts from the run's first frame,
    // with what the run started with (slots, mirroring, damage).
    static void rewound()
    {
        const char* why = NULL;
        if (s_mirrored)
        {
            s_start->restore(true, NULL, why);
        }
        startDamages(true);
        restartSides();
        s_holding = false;
        s_recArmed = false;
        s_test.startFrame = *kGameFrame;
        s_rewind = kRewindReplay;
    }

    int rewindStep()
    {
        if (s_rewind == kRewindLoad)
        {
            return kRewindHold;
        }
        if (s_rewind != kRewindReplay)
        {
            return kRewindIdle;
        }
        if (nextFrame() < s_rewindTarget)
        {
            return kRewindRun;
        }
        s_rewind = kRewindNone;
        s_test.rewound++;
        s_test.rewindTicks = OSGetTick() - s_rewindStart;
        TM_LOG("record: rewound to frame %u\n", s_rewindTarget);
        return kRewindHold;
    }

    static void runRequest()
    {
        u32 req = s_test.request;
        if (req == kReqNone)
        {
            return;
        }
        s_test.request = kReqNone;
        switch (req)
        {
        case kReqSaveOrRestore:
            saveOrRestore();
            break;
        case kReqRestore:
            requestRestore("harness");
            break;
        case kReqDelete:
            deletePositions();
            break;
        case kReqResave:
            resavePositions();
            break;
        case kReqPrune:
            prunePositions();
            break;
        case kReqCopySlot:
            copyManagedSlot();
            break;
        case kReqDeleteSlot:
            deleteManagedSlot();
            break;
        case kReqHmnMode:
            hmnModeChanged();
            break;
        case kReqCpuMode:
            cpuModeChanged();
            break;
        case kReqSlot:
            slotChanged();
            break;
        case kReqMirror:
            mirroredChanged();
            break;
        default:
            break;
        }
        s_test.result++;
    }

    // ---- HUD ----

    bool hudVisible()
    {
        return s_active && (s_message != NULL || (positionsSaved() && (sideMode(kHmn) != kModeOff
                                                                       || sideMode(kCpu) != kModeOff)));
    }

    void drawHud()
    {
        HudInfo info;
        info.mode = kHudHidden;
        info.message = s_message;
        info.random = false;
        info.frame = static_cast<int>(s_t);
        info.end = 0;
        int hm = mode(kHmn);
        int cm = mode(kCpu);
        if (hm != kModeOff || cm != kModeOff)
        {
            bool recording = hm == kModeRecord || cm == kModeRecord
                             || (hm == kModeReRecord && s_side[kHmn].cancelled)
                             || (cm == kModeReRecord && s_side[kCpu].cancelled);
            info.mode = recording ? (s_recArmed ? kHudArmed : kHudRecord)
                                  : (playingMode(hm) || playingMode(cm) ? kHudPlayback : kHudControl);
            for (int side = 0; side < kSides; side++)
            {
                int m = side == kHmn ? hm : cm;
                if (playingMode(m))
                {
                    u32 e = slotEnd(playSlot(side));
                    info.end = static_cast<int>(e) > info.end ? static_cast<int>(e) : info.end;
                    info.random = info.random || slotSetting(side) == 0;
                }
            }
        }
        hudDraw(info);
    }

    // Play Slot N (CPU counter action): plays the CPU's slot from its first
    // frame using buttons and main stick.
    static bool counterFrame(int slot, u32 t, cpu::Frame& out)
    {
        Slot* s = slotAt(kCpu, slot);
        Input in;
        if (s == NULL || slotEmpty(*s) || !slotRead(*s, static_cast<int>(s->start + t), in))
        {
            return false;
        }
        out.buttons = in.buttons;
        out.x = in.x;
        out.y = in.y;
        return true;
    }

    // ---- attach ----

    void attach(iface::Interface* iface)
    {
        // Mr. Resetti (itKind 0xA1) is the one preloaded item whose archives
        // go into FighterTechqniq instead of AssistFigureResource (itArchive's
        // constructor special case at 0x809BC9B8-0x809BCA04): 0xAB0E0 bytes on
        // Final Destination, leaving 0x6E980 free against our 0x9EAF0 buffer.
        // Clearing his switch bit here, before scMelee::start() runs
        // itManager::setup(), keeps FighterTechqniq empty while the other
        // assist trophies and Pokemon stay on.
        GameGlobal* g = g_GameGlobal;
        if (game::trainingGameMode() && game::isRam(g) && game::isRam(g->m_modeMelee))
        {
            g->m_modeMelee->m_meleeInitData.m_itSwitch.m_assist.m_mrResetti = false;
        }
        memset(&s_test, 0, sizeof(s_test));
        s_test.magic = kTestMagic;
        for (int side = 0; side < kSides; side++)
        {
            SideState& s = s_side[side];
            memset(&s, 0, sizeof(s));
            s.player = -1;
            s_source[side].side = side;
            s_hadInputs[side] = 0;
        }
        s_start = &liteStartState();
        s_start->clear();
        // Recordings are match-scoped: start with modes off and Slot 1 selected.
        resetRows();
        s_storageTries = 0;
        s_stepRan = false;
        s_sidesRan = 0;
        s_restorePending = false;
        s_holding = false;
        s_recArmed = false;
        s_prevStartOnInput = settings().recording.startOnFirstInput;
        s_mirrored = false;
        s_swapPad = false;
        s_hmnNeutral = false;
        s_ctrlPlayer = -1;
        s_ctrlOrigPlayer = -1;
        s_ctrlOrig = 0;
        findPlayers();
        s_hmnFlick = cpu::button::kFlickJump;
        s_restoreTimer = 0;
        s_message = NULL;
        s_messageTimer = 0;
        s_cpuTrainingControl = false;
        s_exact = false;
        s_savePending = kSaveNone;
        s_gen = 0;
        s_runId = 0;
        s_detached = false;
        s_quick.valid = false;
        s_import.pending = false;
        resetRun();
        savestate::setListener(onSavestate);
        cpu::setSlotFrame(counterFrame);
        s_rng = game::frameCounter() | 1;
        s_innerInput = iface->callbacks.fighterInput;
        s_innerPads = iface->callbacks.pads;
        iface->callbacks.fighterInput = onFighterInput;
        iface->callbacks.pads = onPads;
        s_active = true;
        TM_LOG("record: test block at %08x; start state %s\n", &s_test, s_start->name());
    }

    void detach()
    {
        if (!s_active)
        {
            return;
        }
        for (int side = 0; side < kSides; side++)
        {
            s_side[side].want = false;
            for (int slot = 0; slot < cpu::kMaxSlots; slot++)
            {
                if (cpu::sourceOf(slot) == &s_source[side])
                {
                    cpu::release(slot, "recording ending");
                }
            }
        }
        cpuReadsPad(false);
        game::trainingControllerSuppress(game::kControllerOwnerRecord, false);
        savestate::setListener(NULL);
        cpu::setSlotFrame(NULL);
        s_start->clear();
        storageFree();
        s_active = false;
        TM_LOG("record: detached after %u steps, %u restores, %u loops\n", s_test.steps, s_test.restores,
               s_test.loops);
    }
}
}
