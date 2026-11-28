#include "main/savestate/savestate.h"

#include <OS/OSTime.h>
#include <cstdio>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <gf/gf_file_io_manager.h>
#include <gf/gf_pad_status.h>
#include <gm/gm_global.h>
#include <if/if_mngr.h>
#include <so/effect/so_effect_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>
#include <types.h>

#include "extra/olimar_pikmin_logic.h"
#include "main/cpu/driver.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/pad_inmatch.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/savestate/snapshot.h"
#include "main/savestate/trace.h"
#include "main/settings.h"
#include "main/ui/menu.h"
#include "main/ui/osd.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"
#include "shared/interface.h"
#include "shared/log.h"

// Savestates (savestate.h): saves and loads the match's memory between game
// steps. Preconditions, preserved state, and feedback: docs/internals.md,
// "Savestate preconditions", "Our own state across a savestate load", and
// "Savestate feedback".
namespace tm {
namespace savestate {
    using main::settings;

    enum {
        kPorts = iface::kPadCount, // 0-3 GameCube, 4-7 Wii
        kDpadLeft = 0x1,
        kDpadRight = 0x2,
        // Hold D-pad right 12 frames (0.2 s) to save; 24 frames (0.4 s) of
        // lockout after a save prevents accidental double saves or loads.
        kSaveHold = 12,
        kLockout = 24,
        kOsdId = osd::kIdSavestate,
        // g_IfMngr +0x78 -> IfMinigameTraining: +0x20 is No. of CPUs (1-3;
        // read in the emulator while stepping it in the pause menu: it
        // changes at once, the fighters are added after the pause closes).
        kIfTraining = 0x78,
        kTrainingCpus = 0x20,
        // g_gfFileIOManager: +0x8 and +0xC point at its two request queues,
        // whose +0x18 points at a ring {u16 capacity, u16 count, entries};
        // count is 0 whenever nothing is being loaded (sampled every two
        // frames from the stage select through a match start).
        kIoQueue1 = 0x8,
        kIoQueue2 = 0xC,
        kQueueRing = 0x18,
        kRingCount = 0x2,
        // soSoundModuleImpl's vtable slot 11 is stopStatusSE: it stops the
        // sound handle at +0x38 (read from its code on this build).
        kStopStatusSeSlot = 11
    };

    enum Request {
        kNone = 0,
        kSave = 1,
        kLoad = 2
    };

    // Feedback tiers: silent internal saves, menu/recording sound-only saves,
    // and D-pad saves with sound plus a subtle screen flash.
    enum Feedback {
        kQuiet = 0,
        kSounds = 1,
        kFlash = 2
    };

    // Brief cool-tinted fade drawn before our HUD/overlays so text stays crisp.
    static const u8 kFlashAlpha[] = {36, 26, 16, 8, 4};
    enum { kFlashFrames = sizeof(kFlashAlpha) / sizeof(kFlashAlpha[0]) };
    // The flash moves one frame per main-loop pass and starts on the pass
    // after the save: the saving pass's picture is never shown (measured
    // frame by frame: started on that pass, its second frame showed first).

    static bool s_known = false;
    static u32 s_loop = 0;
    static u32 s_heldRight = 0;
    static bool s_holdUsed = false; // this hold of D-pad right has saved
    static u32 s_prevHeld[kPorts];
    static u8 s_prevType[kPorts]; // 0xFF when disconnected
    static u32 s_lockout = 0;
    static int s_menuRequest = kNone;
    static int s_recRequest = kNone;
    static bool s_recFeedback = false;
    static int s_feedback = kQuiet; // of the save or load running now
    static Listener s_listener = NULL;
    static u32 s_saves = 0;
    static u32 s_loads = 0;
    static u32 s_refusals = 0;
    static u32 s_random = 1;
    // Match-start auto-save: retried until taken or permanently refused;
    // s_autoHeld while the quick slot holds it so it can be dropped silently.
    static bool s_autoDone = false;
    static bool s_autoHeld = false;
    static bool s_flash = false; // a save's flash is on
    static u32 s_flashLoop = 0;  // s_loop of the pass that saved

    static void updateStateText();

    // The human's HUD slot (the OSD stack a refusal goes to).
    static int humanSlot()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (game::humanPlayer(refs[i].playerNo))
            {
                return refs[i].playerNo;
            }
        }
        return n > 0 ? refs[0].playerNo : 0;
    }

    // A refusal the player could not otherwise tell the reason of, as an
    // OSD message in the human's stack (lines short enough for its box).
#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void refusal(const char* text)
    {
        osd::post(humanSlot(), kOsdId, osd::kRed, "%s", text);
    }

    static void saveFailed(Result r)
    {
        refusal(r == kErrTooBig ? "Save failed:\nno room beside\nthe recording"
                                : (r == kErrNoRoom ? "Save failed:\nout of memory"
                                                   : "Save failed:\nmemory\nunreadable"));
    }

    // Nothing saved: plays the error sound alone without posting an OSD.
    static void loadFailed(Result r)
    {
        if (r == kErrMatch || r == kErrLayout || r == kErrResources || r == kErrChecksum)
        {
            refusal(r == kErrMatch       ? "Load failed:\nanother match"
                    : r == kErrLayout    ? "Load failed:\nmemory moved"
                    : r == kErrResources ? "Load failed:\nfiles changed\nsince the save"
                                         : "Load failed:\nstate overwritten");
        }
    }

    static void sound(int id)
    {
        ui::menuSound(id);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    static void savedFeedback()
    {
        if (s_feedback == kQuiet)
        {
            return;
        }
        sound(game::kSoundConfirm);
        if (s_feedback == kFlash && !settings().general.frameAdvance)
        {
            s_flash = true;
            s_flashLoop = s_loop;
        }
    }

    static u32 nextRandom()
    {
        s_random = s_random * 1103515245u + 12345u;
        return s_random >> 8;
    }

    // ---- preconditions --------------------------------------------------

    static int trainingCpus()
    {
        IfMngr* manager = g_IfMngr;
        if (!game::isRam(manager))
        {
            return -1;
        }
        const void* menu = reinterpret_cast<const void*>(game::wordAt(manager, kIfTraining));
        return game::isRam(menu) ? static_cast<int>(game::wordAt(menu, kTrainingCpus)) : -1;
    }

    static bool queueBusy(u32 queue)
    {
        if (!game::isRam(reinterpret_cast<void*>(queue)))
        {
            return true;
        }
        u32 ring = game::wordAt(reinterpret_cast<void*>(queue), kQueueRing);
        if (!game::isRam(reinterpret_cast<void*>(ring)))
        {
            return true;
        }
        return *reinterpret_cast<const u16*>(ring + kRingCount) != 0;
    }

#pragma force_active on
    bool loadingFiles()
    {
        gfFileIOManager* io = g_gfFileIOManager;
        if (!game::isRam(io))
        {
            return true;
        }
        return queueBusy(game::wordAt(io, kIoQueue1)) || queueBusy(game::wordAt(io, kIoQueue2));
    }
#pragma force_active reset

    static bool transformed(const game::FighterRef& ref)
    {
        ftEntryManager* entries = g_ftEntryManager;
        ftEntry* entry = game::isRam(entries) ? entries->getEntity(ref.entryId) : NULL;
        if (!game::isRam(entry) || entry->m_activeInstanceIndex != 0)
        {
            return true;
        }
        Fighter* f[2] = {ref.fighter, ref.partner};
        for (int i = 0; i < 2; i++)
        {
            const soModuleEnumeration* m = f[i] != NULL ? game::fighterModules(f[i]) : NULL;
            if (m != NULL && game::fighterStatus(m) == game::kStatusFinal)
            {
                return true;
            }
        }
        return false;
    }

    // NULL if a save or load may run now, else why not.
    static const char* blocked(const game::MatchState& m)
    {
        if (!s_known)
        {
            return "unknown memory layout";
        }
        if (!m.running || !m.training || m.paused)
        {
            return "not in a running Training match";
        }
        if (lab::savestateBusy() || game::otherStopBits() != 0)
        {
            return "busy";
        }
        ftManager* manager = g_ftManager;
        if (!game::isRam(manager) || manager->getEntryCount() != 2 || trainingCpus() != 1)
        {
            return "needs exactly 2 fighters";
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        if (n != 2)
        {
            return "needs exactly 2 fighters";
        }
        for (int i = 0; i < n; i++)
        {
            if (transformed(refs[i]))
            {
                return "transformation or Final Smash";
            }
        }
        if (loadingFiles())
        {
            return "files loading";
        }
        return NULL;
    }

    // ---- save and load ----------------------------------------------------

    static const u32 kStopStatusSeCode[] = {
        0x7C641B78, // mr r4,r3
        0x3C60805A, // lis r3,0x805A
        0x806301D0, // lwz r3,0x1D0(r3)   g_sndSystem
        0x38A00000, // li r5,0
        0x80840038  // lwz r4,0x38(r4)    the status sound's handle
    };

    static void stopStatusSound(Fighter* f)
    {
        const soModuleEnumeration* m = f != NULL ? game::fighterModules(f) : NULL;
        if (m == NULL || !game::isRam(m->m_soundModule))
        {
            return;
        }
        const u32* vtable = reinterpret_cast<const u32*>(game::wordAt(m->m_soundModule, 0));
        if (!game::isRam(vtable) || !game::isRam(reinterpret_cast<void*>(vtable[kStopStatusSeSlot])))
        {
            return;
        }
        const u32* code = reinterpret_cast<const u32*>(vtable[kStopStatusSeSlot]);
        for (u32 i = 0; i < sizeof(kStopStatusSeCode) / sizeof(kStopStatusSeCode[0]); i++)
        {
            if (code[i] != kStopStatusSeCode[i])
            {
                return;
            }
        }
        reinterpret_cast<void (*)(void*)>(vtable[kStopStatusSeSlot])(m->m_soundModule);
    }

    // Revival platform effect handle in Rebirth's RA-Basic[3] (0x20000003,
    // ftStatusUniqProcessRebirth::exitStatus, 0x8087EC04-0x8087EC58): loading
    // a savestate over Rebirth would overwrite the handle without removing the
    // effect in ecMgr.
    static void clearRebirthEffect(Fighter* f)
    {
        enum { kRaRebirthEffect = 0x20000003 };
        const soModuleEnumeration* m = f != NULL ? game::fighterModules(f) : NULL;
        if (m == NULL || !game::isRam(m->m_statusModule) || !game::isRam(m->m_workManageModule)
            || !game::isRam(m->m_effectModule))
        {
            return;
        }
        if (m->m_statusModule->getStatusKind() != game::kStatusRebirth)
        {
            return;
        }
        int handle = m->m_workManageModule->getInt(kRaRebirthEffect);
        if (handle != 0)
        {
            m->m_effectModule->kill(handle, true, true);
            m->m_workManageModule->setInt(0, kRaRebirthEffect);
        }
    }

    // Before a load overwrites them: status sounds of the timeline we leave
    // (charging and other loops) and any active revival platform effect would
    // lose the handles that stop them.
    static void beforeLoadCopy()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            stopStatusSound(refs[i].fighter);
            stopStatusSound(refs[i].partner);
            clearRebirthEffect(refs[i].fighter);
            clearRebirthEffect(refs[i].partner);
        }
    }

    static float ms(u32 ticks)
    {
        return ticks * 1000.0f / 60750000.0f;
    }

    static void tell(int slot, int event, bool passing, const char* why)
    {
        if (s_listener != NULL)
        {
            s_listener(slot, event, passing, why);
        }
    }

    static void doSave(int slot)
    {
        lab::savestateCopying();
        game::trackerSuspend();
        Result r = capture(slot, s_loop);
        game::trackerResume(false);
        if (r != kOk)
        {
            s_refusals++;
            TM_LOG("savestate: %ssave refused: %s\n", slot == kSlotQuick ? "" : "recording state ", resultText(r));
            if (slot == kSlotQuick && !s_autoHeld)
            {
                sound(game::kSoundError);
                saveFailed(r);
            }
            tell(slot, kEventRefused, false, resultText(r));
            return;
        }
        s_saves++;
        cpu::driverSaved(slot);
        cpu::optionsSaved(slot);
        traceTop('S');
        const SlotInfo& info = slotInfo(slot);
#ifdef TM_PERF
        TM_LOG("savestate: %ssaved frame %u: %u bytes in %u segments (without fighter free space %u), slot %u, "
               "copy %.2f ms, maxIrqOff %.3f ms (%u ticks)%s\n",
               slot == kSlotQuick ? "" : "recording state ", info.frame, info.bytes, info.segments, info.setBytesV3,
               info.capacity, ms(info.saveTicks), ms(info.saveMaxIrqOffTicks), info.saveMaxIrqOffTicks,
               droppedOther() ? "; quick state dropped for room" : "");
#else
        TM_LOG("savestate: %ssaved frame %u: %u bytes in %u segments (without fighter free space %u), slot %u, "
               "copy %.2f ms%s\n",
               slot == kSlotQuick ? "" : "recording state ", info.frame, info.bytes, info.segments, info.setBytesV3,
               info.capacity, ms(info.saveTicks), droppedOther() ? "; quick state dropped for room" : "");
#endif
        savedFeedback();
        if (slot != kSlotQuick && droppedOther())
        {
            if (!s_autoHeld)
            {
                refusal("State cleared:\nno room beside\nthe recording");
            }
            s_autoHeld = false;
        }
        tell(slot, kEventSaved, false, NULL);
    }

    // Human or CPU as the match was set up (gmGlobalModeMelee +0x98 + 0x5C
    // per player, +1: 0 human, 1 CPU). Not the fighter's operation type: it
    // says human for a CPU whose AI the lab switched off.
    static bool setupCpu(int player)
    {
        GameGlobal* g = g_GameGlobal;
        if (player < 0 || player > 3 || !game::isRam(g) || !game::isRam(g->m_modeMelee))
        {
            return false;
        }
        return game::byteAt(g->m_modeMelee, 0x98 + 0x5C * player + 1) == 1;
    }

    static void addRandomPercent()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            u32 most = settings().recording.randomPercent[setupCpu(refs[i].playerNo) ? main::kSideCpu : main::kSideHuman];
            if (most > kMaxRandomPercent)
            {
                most = kMaxRandomPercent;
            }
            if (most != 0)
            {
                float d = game::damage(refs[i]) + static_cast<float>(nextRandom() % (most + 1));
                game::setDamage(refs[i], d > 999.0f ? 999.0f : d);
            }
        }
    }

    static void doLoad(int slot)
    {
        lab::savestateCopying();
        game::trackerSuspend();
        Result r = restore(slot, beforeLoadCopy);
        game::trackerResume(r == kOk);
        if (r != kOk)
        {
            s_refusals++;
            TM_LOG("savestate: %sload refused: %s\n", slot == kSlotQuick ? "" : "recording state ", resultText(r));
            if (slot == kSlotQuick)
            {
                sound(game::kSoundError);
                loadFailed(r);
            }
            tell(slot, kEventRefused, false, resultText(r));
            return;
        }
        s_loads++;
        cpu::driverLoaded(slot);
        cpu::optionsLoaded(slot);
        lab::savestateLoaded();
        addRandomPercent();
        traceTop('L');
        const SlotInfo& info = slotInfo(slot);
#ifdef TM_PERF
        TM_LOG("savestate: %sloaded frame %u (load %u): copy %.2f ms, checksum %.2f ms, maxIrqOff %.3f ms (%u ticks)\n",
               slot == kSlotQuick ? "" : "recording state ", info.frame, s_loads, ms(info.loadTicks),
               ms(info.checkTicks), ms(info.loadMaxIrqOffTicks), info.loadMaxIrqOffTicks);
#else
        TM_LOG("savestate: %sloaded frame %u (load %u): copy %.2f ms, checksum %.2f ms\n",
               slot == kSlotQuick ? "" : "recording state ", info.frame, s_loads, ms(info.loadTicks),
               ms(info.checkTicks));
#endif
        if (s_feedback != kQuiet)
        {
            sound(game::kSoundBack);
        }
        tell(slot, kEventLoaded, false, NULL);
    }

    // Refusals that go away by themselves (the next pass may succeed).
    static bool passing(const char* why)
    {
        return game::sameString(why, "not in a running Training match") || game::sameString(why, "busy")
               || game::sameString(why, "files loading");
    }

    static void run(int request, const game::MatchState& m, int slot, int feedback)
    {
        s_feedback = feedback;
        const char* why = blocked(m);
        if (why == NULL && request == kLoad && !slotInfo(slot).valid)
        {
            why = "no state saved";
        }
        if (why != NULL)
        {
            s_refusals++;
            TM_LOG("savestate: %s%s refused: %s\n", slot == kSlotQuick ? "" : "recording state ",
                   request == kSave ? "save" : "load", why);
            if (slot == kSlotQuick)
            {
                sound(game::kSoundError);
                if (game::sameString(why, "needs exactly 2 fighters"))
                {
                    refusal(request == kSave ? "Cannot save:\nneeds two fighters" : "Cannot load:\nneeds two fighters");
                }
                else if (game::sameString(why, "transformation or Final Smash"))
                {
                    refusal(request == kSave ? "Cannot save:\ntransformed"
                                             : "Cannot load:\ntransformed");
                }
                else if (game::sameString(why, "unknown memory layout"))
                {
                    refusal(request == kSave ? "Cannot save:\nunknown version"
                                             : "Cannot load:\nunknown version");
                }
                if (request == kSave)
                {
                    s_lockout = kLockout;
                }
            }
            tell(slot, kEventRefused, passing(why), why);
            return;
        }
        if (request == kSave)
        {
            doSave(slot);
        }
        else
        {
            doLoad(slot);
        }
        // Lock the D-pad out for kLockout frames after a save (not after a load).
        if (slot == kSlotQuick && request == kSave)
        {
            s_lockout = kLockout;
        }
    }

    static void run(int request, const game::MatchState& m, int feedback)
    {
        if (request == kSave)
        {
            s_autoHeld = false;
        }
        run(request, m, kSlotQuick, feedback);
    }

    // Take a silent initial state on the first live frame so D-pad Left works
    // immediately before any manual save.
    static void autoSave(const game::MatchState& m)
    {
        if (slotInfo(kSlotQuick).valid)
        {
            s_autoDone = true;
            return;
        }
        const char* why = blocked(m);
        if (why != NULL)
        {
            if (!passing(why))
            {
                s_autoDone = true;
                TM_LOG("savestate: no match-start state: %s\n", why);
            }
            return;
        }
        s_autoDone = true;
        s_autoHeld = true;
        s_feedback = kQuiet;
        doSave(kSlotQuick);
        s_autoHeld = slotInfo(kSlotQuick).valid;
        if (s_autoHeld)
        {
            TM_LOG("savestate: match-start state taken at frame %u\n", slotInfo(kSlotQuick).frame);
        }
    }

    static void runRecording(const game::MatchState& m)
    {
        int rec = s_recRequest;
        s_recRequest = kNone;
        run(rec, m, kSlotRecording, s_recFeedback ? kSounds : kQuiet);
    }

    // ---- input -------------------------------------------------------------

    // D-pad directions bound to Load State and Save State (none while the lab
    // menu is open, since the D-pad navigates the menu).
    u32 boundBits()
    {
        u32 bits = 0;
        if (lab::menuOpen())
        {
            return 0;
        }
        if (settings().controls.dpadLeft == kLeftLoadState)
        {
            bits |= kDpadLeft;
        }
        if (settings().controls.dpadRight == kRightSaveState)
        {
            bits |= kDpadRight;
        }
        return bits;
    }

    static void hide(gfPadStatus* pads, u32 mask, const game::MatchState& m)
    {
        if (mask == 0)
        {
            return;
        }
        bool allowMinus = !lab::minusBoundToFrameAdvance(m);
        for (int i = 0; i < kPorts; i++)
        {
            if (!game::padConnectedInMatch(i, pads[i]))
            {
                continue;
            }
            gfPadStatus& pad = pads[i];
            if (pad.m_controllerType == gfPadType::WIIMOTE
                && (!allowMinus || (pad.m_buttonsCurrentFrame.bits & game::kPadWiiMinus) == 0))
            {
                continue;
            }
            game::clearPadBits(pad, mask);
        }
    }

    // The slot lives in the heaps a third and fourth fighter load into, and
    // Training adds them right after its pause closes with No. of CPUs raised.
    static void giveUpForMoreFighters(const game::MatchState& m)
    {
        if (holdingBuffers() && m.running && trainingCpus() > 1)
        {
            // read before release() clears it
            bool hadRecording = recordingStateValid();
            release();
            TM_LOG("savestate: No. of CPUs raised: state cleared\n");
            if (!s_autoHeld || hadRecording)
            {
                refusal("State cleared:\ntoo many CPUs");
            }
            s_autoHeld = false;
        }
    }

    static void onSystemPads(gfPadStatus* pads)
    {
        s_loop++;
        if (s_lockout > 0)
        {
            s_lockout--;
        }
        game::MatchState m = game::matchState();
        giveUpForMoreFighters(m);
        bool stalled = false;
        if (m.running)
        {
            traceTop(' ');
            stalled = parkTop();
        }
        heldPads(pads);
        bool allowMinus = !lab::minusBoundToFrameAdvance(m);
        u32 held = 0;
        u32 pressed = 0;
        for (int i = 0; i < kPorts; i++)
        {
            if (!game::padConnectedInMatch(i, pads[i]))
            {
                s_prevHeld[i] = 0;
                s_prevType[i] = 0xFF;
                continue;
            }
            u8 type = static_cast<u8>(pads[i].m_controllerType);
            u32 now = pads[i].m_buttonsCurrentFrame.bits;
            u32 edge = type == s_prevType[i] ? (now & ~s_prevHeld[i]) : 0;
            s_prevHeld[i] = now;
            s_prevType[i] = type;
            if (type == gfPadType::WIIMOTE && (!allowMinus || (now & game::kPadWiiMinus) == 0))
            {
                continue;
            }
            held |= now;
            pressed |= edge;
        }
        int request = kNone;
        if (g_probe.request == kSave || g_probe.request == kLoad)
        {
            request = g_probe.request;
        }
        g_probe.request = kNone;
        bool live = m.running && m.training && !m.paused;
        if (live)
        {
            bool olimarDpad = extra::olimarDpadMode(settings().fighterMechanics) != main::kOlimarDpadOff
                              && main::extraOlimarP1();
            u32 bound = (lab::comboLHeldOnPads(pads) || (main::extraDrill(0, NULL) & 4u) != 0u || olimarDpad)
                            ? 0u
                            : boundBits();
            s_heldRight = (bound & kDpadRight) && (held & kDpadRight) ? s_heldRight + 1 : 0;
            if (s_heldRight == 0)
            {
                s_holdUsed = false;
            }
            // One save per hold.
            if (request == kNone && s_lockout == 0)
            {
                if (s_heldRight >= kSaveHold && !s_holdUsed)
                {
                    request = kSave;
                    s_holdUsed = true;
                }
                else if ((bound & kDpadLeft) && (pressed & kDpadLeft))
                {
                    request = kLoad;
                }
            }
            if (s_recRequest != kNone)
            {
                runRecording(m);
                stalled = true;
            }
            if (!s_autoDone && request == kNone && s_menuRequest == kNone)
            {
                autoSave(m);
                stalled |= s_autoDone;
            }
            int feedback = kFlash;
            if (request == kNone && s_menuRequest != kNone)
            {
                request = s_menuRequest;
                feedback = kSounds;
            }
            if (request != kNone)
            {
                s_menuRequest = kNone;
                run(request, m, feedback);
                stalled = true;
            }
            // Not in the system pads the debug pads are copied from: the pad
            // thread refreshes those only when it samples, so a change made
            // there can be what the next pass reads back as the controller.
            hide(pads, bound, m);
        }
        else
        {
            s_heldRight = 0;
            if (request != kNone)
            {
                run(request, m, kFlash);
            }
            if (s_recRequest != kNone)
            {
                runRecording(m);
            }
            if (m.paused)
            {
                updateStateText();
            }
        }
        if (stalled)
        {
            forgetStall();
        }
    }

    void onPads(u32 phase, gfPadStatus* pads)
    {
        if (phase == iface::kPadsSystem)
        {
            onSystemPads(pads);
            return;
        }
        game::MatchState m = game::matchState();
        if (m.running && m.training && !m.paused)
        {
            hide(pads, boundBits(), m);
        }
    }

    void onRender(const game::MatchState& m)
    {
        if (!s_flash || !m.running || s_loop == s_flashLoop)
        {
            return;
        }
        u32 frame = s_loop - s_flashLoop - 1;
        if (frame >= kFlashFrames)
        {
            s_flash = false;
            return;
        }
        gfx::begin();
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0xDCE8FF00u | kFlashAlpha[frame]);
        gfx::end();
    }

    // ipPadButton in the human's input object (+0xC): read while holding
    // each D-pad direction in a match, D-pad left gives 0x2000 | 0x800 (side
    // taunt, left) and right 0x4000 | 0x800, besides direction bits.
    enum {
        kInputButtons = 0x0C,
        kTauntSide = 0x800,
        kTauntLeft = 0x2000,
        kTauntRight = 0x4000,
        kAppealAll = (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14)
    };

    // The D-pad is hidden in the debug and game pads, but the fighters'
    // controls also come from the system pads, which the pad thread refreshes
    // on its own (hiding it there would hide it from our next reading too),
    // so a bound direction's taunt is taken out of the human's controls here.
    void onFighterInput(const iface::FighterInput* call)
    {
        cpu::Owner who;
        if (!cpu::findOwner(call->input, call->entryId, who) || who.cpu || who.sub)
        {
            return;
        }
        game::MatchState m = game::matchState();
        if (!m.running || !m.training || m.paused)
        {
            return;
        }
        u32 bound = boundBits();
        if (bound == 0)
        {
            return;
        }
        int port = game::playerPort(who.playerNo);
        if (static_cast<u32>(port) < kPorts && s_prevType[port] == gfPadType::WIIMOTE
            && !lab::minusBoundToFrameAdvance(m) && (s_prevHeld[port] & game::kPadWiiMinus) != 0)
        {
            u32* buttons = reinterpret_cast<u32*>(reinterpret_cast<u32>(call->input) + kInputButtons);
            *buttons &= ~kAppealAll;
        }
    }

    void requestSave()
    {
        s_menuRequest = kSave;
    }

    void requestLoad()
    {
        s_menuRequest = kLoad;
    }

    void setListener(Listener listener)
    {
        s_listener = listener;
    }

    void requestRecordingSave(bool feedback)
    {
        s_recRequest = kSave;
        s_recFeedback = feedback;
    }

    void requestRecordingLoad(bool feedback)
    {
        s_recRequest = kLoad;
        s_recFeedback = feedback;
    }

    bool recordingStateValid()
    {
        return slotInfo(kSlotRecording).valid;
    }

    void dropRecordingState()
    {
        invalidate(kSlotRecording);
    }

#pragma force_active on
    void resetForSwap()
    {
        release();
        s_autoDone = false;
        s_autoHeld = false;
        s_menuRequest = kNone;
        s_recRequest = kNone;
        s_lockout = 0;
        s_flash = false;
        updateStateText();
        TM_LOG("savestate: cleared for fighter swap; match-start auto-save re-armed\n");
    }
#pragma force_active reset

    char g_stateText[kStateTextSize];

    // The menu's State row shows g_stateText; written while the match is
    // paused (the only time the menu is open).
    static void updateStateText()
    {
        const SlotInfo& info = slotInfo(kSlotQuick);
        if (s_menuRequest != kNone)
        {
            snprintf(g_stateText, sizeof(g_stateText), "%s", s_menuRequest == kSave ? "Saves on resume"
                                                                                      : "Loads on resume");
            return;
        }
        if (!info.valid)
        {
            snprintf(g_stateText, sizeof(g_stateText), "Empty");
            return;
        }
        u32 age = (s_loop - info.loopPass) / 60;
        snprintf(g_stateText, sizeof(g_stateText), "%u:%02u ago", age / 60, age % 60);
    }

    void attach()
    {
        s_known = layoutKnown();
        s_loop = 0;
        s_heldRight = 0;
        s_lockout = 0;
        s_menuRequest = kNone;
        s_recRequest = kNone;
        s_flash = false;
        s_autoDone = false;
        s_autoHeld = false;
        s_saves = s_loads = s_refusals = 0;
        memset(s_prevHeld, 0, sizeof(s_prevHeld));
        memset(s_prevType, 0xFF, sizeof(s_prevType));
        probeReset();
        updateStateText();
        s_random = OSGetTick() | 1;
        TM_LOG("savestate: test block at %08x; layout %s\n", &g_probe, s_known ? "known" : "UNKNOWN");
    }

    void detach()
    {
        bool held = holdingBuffers();
        release();
        TM_LOG("savestate: %u saves, %u loads, %u refused; slot %s\n", s_saves, s_loads, s_refusals,
               held ? "freed" : "never taken");
    }
}
}
