#include "main/lab.h"

#include <cstdio>
#include <string.h>
#include <gf/gf_pad_status.h>

#include "extra/olimar_pikmin_logic.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/display/debug_draw.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/extra.h"
#include "main/game/advantage.h"
#include "main/game/code_menu.h"
#include "main/game/fighters.h"
#include "main/game/freeze.h"
#include "main/game/match.h"
#include "main/game/overlays.h"
#include "main/game/pad_inmatch.h"
#include "main/game/techniques.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/storage/settings_file.h"
#include "main/ui/debug_overlay.h"
#include "main/ui/hud_layout.h"
#include "main/ui/info_display.h"
#include "main/ui/lab_menu.h"
#include "main/ui/ledgedash_hud.h"
#include "main/ui/match_hud.h"
#include "main/ui/menu.h"
#include "main/ui/osd.h"
#include "main/ui/pages/olimar.h"
#include "main/ui/pages/scenarios.h"
#include "main/ui/pages/pages.h"
#include "main/ui/settings_modal.h"
#include "main/ui/style.h"
#include "main/ui/watermark.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"
#include "shared/game/scene.h"
#include "shared/game/stick.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "shared/perf.h"

namespace tm {
namespace lab {
    using main::settings;

    enum {
        kPorts = iface::kPadCount, // 0-3 GameCube, 4-7 Wii
        kButtonA = 0x100,
        kButtonB = 0x200,
        kButtonX = 0x400,
        kButtonY = 0x800,
        kButtonZ = 0x10,
        kButtonL = 0x40,
        kButtonR = 0x20,
        kButtonStart = 0x1000,
        kDpadUp = 0x8,
        kDpadDown = 0x4,
        kDpadRight = 0x2,
        kDpadLeft = 0x1,
        kDpadAll = 0xF,
        kAllButtons = 0xFFFFFFFF,
        kStickDeadZone = 40,
        // R pressed this far counts as held (the menu's fast key repeat).
        kTriggerHeld = 43,
        // Steps after Start in frame advance waiting for Training's pause.
        kStartWindow = 30,
        // Steps after Training's pause closes in frame advance with only the
        // gameplay tasks paused (its menu animates away, the camera returns).
        kCatchUp = 45,
        // aiInput button bits of the up and down taunts: ipPadButton's
        // MASK_APPEAL_HI and MASK_APPEAL_LW (lib/BrawlHeaders ip/input.h).
        // D-up read as 0x480 (MASK_APPEAL_HI with MASK_DIR_Hi), D-down as
        // 0x1000 (how and on what setup not recorded).
        kTauntUp = 0x400,
        kTauntDown = 0x1000,
        kTauntSide = (1 << 11) | (1 << 13) | (1 << 14),
        kAppealAll = (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14)
    };

    // Start while frame advance has the game stopped: Training's pause trigger
    // runs in a task and never sees it (observed; setup not recorded), and P+'s
    // pad read (freeze.cpp) makes it a "debug unpause" step with input ignored.
    // So the press is hidden until P+ is done, then the game gets a clean one.
    enum StartState {
        kStartIdle,
        kStartWait,   // Start hidden; waiting for P+'s unpause to pass
        kStartInject, // present one Start press next (system, then game pads)
        kStartAwait   // pressed; waiting for Training's pause to open
    };

    // What the players' controllers really do, before anything is hidden.
    typedef main::MenuPad Raw;

    // Key repeat per direction (adapted from ProjectPunch ui.cpp, MIT; see
    // THIRD_PARTY_NOTICES.md), using ui/style.h repeat timings.
    struct Repeat {
        u16 held;
    };

    // Per-match state zeroed together by attach() (owner/pausePort set to -1).
    struct MatchPadState {
        Raw sys[kPorts];
        Raw game[kPorts];
        u32 releaseMask[kPorts]; // buttons hidden until released
        const char* lastHint;
        Repeat repeat[4];
        u16 hold;
        u16 decrementHold;
        u8 stickWaitMask; // bit i: sticks hidden on port i until back to neutral
        s8 owner;
        s8 pausePort;
        u8 startState;
        u8 startPort;
        u8 startTimer;
        u8 catchUp;
        u8 nextCatHold;
        bool nextCatUsedFast;
        bool open;
        // Opened with Start in the match (Start closes, B steps back on
        // subpages only, game held while open); else from Training's pause with Z.
        bool openStart;
        bool startInjectedSystem;
        bool wasPaused;
        bool diPickActive;
        s8 diPickSavedX;
        s8 diPickSavedY;
        bool diCstickLocked;
        s8 diCstickLockX;
        s8 diCstickLockY;
        // Exit was chosen and the game is ending the match (exitMatch): from
        // then until the module detaches every step runs and none is handed
        // over, the observers are off, savestates are refused and the pads
        // reach neither the game nor the lab.
        bool exiting;
    };

    struct LabNet {
        MatchPadState st;
        ui::MenuState menu;
        bool playerShieldHit[game::kMaxFighters][2];
        bool menuFresh; // the menu starts on its first row at the next open
        bool prevRan;
        bool fightersLogged;
        bool vsReadOnly;
        u32 systemCalls;
        u32 gameCalls;
        u32 frozenGameCalls;
        u32 advancedSteps;
    };

#ifdef __MWERKS__
    static LabNet* s_net = NULL;
#else
    static LabNet s_netHost;
    static LabNet* s_net = &s_netHost;
    static ui::MenuState& s_menu = s_netHost.menu;
#endif

#pragma dont_inline on
    static void capture(Raw* raw, const gfPadStatus* pads, bool resetWaits)
    {
        MatchPadState& st = s_net->st;
        for (int i = 0; i < kPorts; i++)
        {
            bool conn = game::padConnectedInMatch(i, pads[i]);
            u8 type = static_cast<u8>(pads[i].m_controllerType);
            bool same = conn && raw[i].connected && type == raw[i].controllerType;
            if (resetWaits && !same)
            {
                st.releaseMask[i] = 0;
                st.stickWaitMask &= static_cast<u8>(~(1u << i));
            }
            if (!conn)
            {
                memset(&raw[i], 0, sizeof(raw[i]));
                continue;
            }
            u32 held = pads[i].m_buttonsCurrentFrame.bits;
            raw[i].pressed = same ? (held & ~raw[i].held) : 0;
            raw[i].held = held;
            raw[i].stickX = pads[i].m_stickX;
            raw[i].stickY = pads[i].m_stickY;
            raw[i].rAnalog = static_cast<u8>(pads[i].m_rTriggerAnalog);
            raw[i].cStickX = pads[i].m_subStickX;
            raw[i].cStickY = pads[i].m_subStickY;
            raw[i].controllerType = type;
            raw[i].connected = true;
        }
    }

    // which: 0 = Start pressed, 1 = Start held, 2 = pauseLab pressed.
    static int firstPadPort(const Raw* raw, int which)
    {
        for (int i = 0; i < kPorts; i++)
        {
            u32 w = which == 1 ? raw[i].held : raw[i].pressed;
            if (w == 0)
            {
                continue;
            }
            const game::InMatchPad& m = game::inMatchPad(raw[i].controllerType);
            if ((w & (which == 2 ? m.pauseLab : m.start)) != 0)
            {
                return i;
            }
        }
        return -1;
    }

    static u32 dpadBits(const Raw* raw, const game::MatchState& m, bool held)
    {
        bool allowMinus = !minusBoundToFrameAdvance(m);
        u32 mask = 0;
        for (int i = 0; i < kPorts; i++)
        {
            u32 w = (held ? raw[i].held : raw[i].pressed) & kDpadAll;
            if (w != 0
                && (raw[i].controllerType != gfPadType::WIIMOTE
                    || (allowMinus && (raw[i].held & game::kPadWiiMinus) != 0)))
            {
                mask |= w;
            }
        }
        return mask;
    }

    // Bit 0: held on any port; bit 1: fresh press on any port.
    static int scanFa(const Raw* raw, bool dec)
    {
        int res = 0;
        u8 btn = dec ? settings().controls.decrementButton : settings().controls.advanceButton;
        for (int i = 0; i < kPorts; i++)
        {
            u32 held = raw[i].held;
            if (held == 0)
            {
                continue;
            }
            u32 bits = game::faButtonBits(raw[i].controllerType, btn, dec);
            if (held & bits)
            {
                res |= 1;
            }
            if (raw[i].pressed & bits)
            {
                res |= 2;
            }
        }
        return res;
    }

    static u32 faBits(u32 controllerType)
    {
        const main::ControlSettings& c = settings().controls;
        return game::faButtonBits(controllerType, c.advanceButton, false)
               | game::faButtonBits(controllerType, c.decrementButton, true);
    }

    // Removes `mask` from every button word of the record, edges included.
    static void hide(gfPadStatus& pad, u32 mask, bool sticks)
    {
        if (mask != 0)
        {
            game::clearPadBits(pad, mask);
            if (game::hasAnalogTriggers(pad.m_controllerType))
            {
                if (mask & kButtonL)
                {
                    pad.m_lTriggerAnalog = 0;
                }
                if (mask & kButtonR)
                {
                    pad.m_rTriggerAnalog = 0;
                }
            }
        }
        if (sticks)
        {
            pad.m_stickX = 0;
            pad.m_stickY = 0;
            pad.m_subStickX = 0;
            pad.m_subStickY = 0;
        }
    }

    static void hideAll(gfPadStatus* pads)
    {
        for (int i = 0; i < kPorts; i++)
        {
            hide(pads[i], kAllButtons, true);
        }
    }
#pragma dont_inline reset

    static bool stickNeutral(const gfPadStatus& pad)
    {
        int v[4] = {pad.m_stickX, pad.m_stickY, pad.m_subStickX, pad.m_subStickY};
        for (int i = 0; i < 4; i++)
        {
            if (v[i] > kStickDeadZone || v[i] < -kStickDeadZone)
            {
                return false;
            }
        }
        return true;
    }

    static bool frameAdvanceOn(const game::MatchState& m)
    {
        return settings().general.frameAdvance && m.running && m.training;
    }

    bool minusBoundToFrameAdvance(const game::MatchState& m)
    {
        return frameAdvanceOn(m) && (faBits(gfPadType::WIIMOTE) & game::kPadWiiMinus) != 0;
    }

    static bool dpadAllowed(const Raw& r, const game::MatchState& m)
    {
        return r.connected
               && (r.controllerType != gfPadType::WIIMOTE
                   || ((r.held & game::kPadWiiMinus) != 0 && !minusBoundToFrameAdvance(m)));
    }

    static bool startOpensMenu()
    {
        return settings().controls.startOpensMenu != 0;
    }

#pragma dont_inline on
    // One fresh press of Start/+ on `pad`, whatever the controller does.
    static void injectStart(gfPadStatus& pad)
    {
        u32 bits = game::inMatchPad(pad.m_controllerType).start;
        pad.m_buttonsCurrentFrame.bits |= bits;
        pad.m_buttonsCurrentFrame2.bits |= bits;
        pad.m_buttonsPressedThisFrame.bits |= bits;
        pad.m_buttonsPressedThisFrame2.bits |= bits;
        pad.m_buttonsHeld.bits &= ~bits;
        pad.m_buttonsReleasedThisFrame.bits &= ~bits;
    }

    static u8 activeScenarioPreset()
    {
        return main::extraLoaded() ? ui::pages::scenarioMenuState()->preset : 0;
    }

    static bool spacingShortcutActive(u8 p)
    {
        return p == ui::pages::kPresetShieldDrop || p == ui::pages::kPresetLedgetech
               || (p == ui::pages::kPresetShieldPressure && ui::pages::scenarioMenuState()->shieldRole == 1);
    }

    static bool comboShortcutActive()
    {
        const main::ComboTrainerSettings& ct = settings().comboTrainer;
        return main::extraLoaded() && ct.enabled != 0 && ct.recoveryTrainer == 0;
    }

    static bool comboLHeld(const game::MatchState& m)
    {
        if (s_net == NULL)
        {
            return false;
        }
        const MatchPadState& st = s_net->st;
        if (!m.running || !m.training || m.paused || st.open || !comboShortcutActive())
        {
            return false;
        }
        for (int i = 0; i < kPorts; i++)
        {
            if (st.sys[i].connected)
            {
                u32 lBit = game::inMatchPad(st.sys[i].controllerType).prevCat;
                if (lBit != 0 && (st.sys[i].held & lBit) != 0)
                {
                    return true;
                }
            }
        }
        return false;
    }
#pragma dont_inline reset

    static inline bool resetLeftPreset(u8 p)
    {
        return p != ui::pages::kPresetOff && p != ui::pages::kPresetComboLab;
    }

    bool comboLHeldOnPads(const gfPadStatus* pads)
    {
        if (s_net == NULL)
        {
            return false;
        }
        game::MatchState m = game::matchState();
        if (!m.running || !m.training || m.paused || s_net->st.open || pads == NULL)
        {
            return false;
        }
        bool comboOn = comboShortcutActive();
        u8 preset = activeScenarioPreset();
        bool spaceOn = spacingShortcutActive(preset);
        u32 dMask = spaceOn ? (kDpadLeft | kDpadRight) : (resetLeftPreset(preset) ? kDpadLeft : 0u);
        if (!comboOn && dMask == 0)
        {
            return false;
        }
        for (int i = 0; i < kPorts; i++)
        {
            if (!game::padConnectedInMatch(i, pads[i]))
            {
                continue;
            }
            u32 cur = pads[i].m_buttonsCurrentFrame.bits;
            u32 anyD = cur | pads[i].m_buttonsPressedThisFrame.bits;
            u32 type = static_cast<u32>(pads[i].m_controllerType);
            if (comboOn)
            {
                u32 lBit = game::inMatchPad(type).prevCat;
                if (lBit != 0 && (cur & lBit) != 0)
                {
                    return true;
                }
            }
            bool dpadOk = type != gfPadType::WIIMOTE || (cur & game::kPadWiiMinus) != 0;
            if (dpadOk && (anyD & dMask) != 0 && (spaceOn || (anyD & kDpadRight) == 0))
            {
                return true;
            }
        }
        return false;
    }

    bool olimarDpadActive(const game::MatchState& m)
    {
        u8 mode = extra::olimarDpadMode(settings().fighterMechanics);
        if (mode == main::kOlimarDpadOff || !m.running || !m.training || m.paused || comboLHeld(m))
        {
            return false;
        }
        return main::extraOlimarP1();
    }

    // D-pad directions owned by Lab shortcuts (Up None, Shield Drop platform
    // cycle, Techchase platform stepping, active scenario reset/spacing, Combo Lab,
    // or Olimar D-Pad Mode), hidden from the game.
#pragma dont_inline on
    static u32 boundDpad(const game::MatchState& m)
    {
        if (!m.running || !m.training || m.paused)
        {
            return 0;
        }
        if (comboLHeld(m) || olimarDpadActive(m))
        {
            return kDpadAll;
        }
        const main::ControlSettings& c = settings().controls;
        u8 p = activeScenarioPreset();
        bool spaceOn = spacingShortcutActive(p);
        u32 up = (c.dpadUp == main::kUpNone || p == ui::pages::kPresetShieldDrop || p == ui::pages::kPresetTechchase)
                     ? kDpadUp
                     : 0;
        u32 down = (c.dpadDown != main::kDownNone || comboShortcutActive() || p == ui::pages::kPresetTechchase)
                       ? kDpadDown
                       : 0;
        u32 left = (spaceOn || resetLeftPreset(p)) ? kDpadLeft : 0;
        u32 right = spaceOn ? kDpadRight : 0;
        return up | down | left | right;
    }

    static void hidePort(gfPadStatus& pad, u32 port, const game::MatchState& m, bool fighterPad)
    {
        if (s_net == NULL)
        {
            return;
        }
        const MatchPadState& st = s_net->st;
        if (st.open)
        {
            hide(pad, kAllButtons, true);
            return;
        }
        if (port >= kPorts || !game::padConnectedInMatch(static_cast<int>(port), pad))
        {
            return;
        }
        u32 type = static_cast<u32>(pad.m_controllerType);
        u32 mask = frameAdvanceOn(m) ? faBits(type) : 0;
        u32 dpad = boundDpad(m);
        if (fighterPad && m.running && m.training && !m.paused)
        {
            dpad |= savestate::boundBits();
            u32 drillFlags = main::extraDrill(0, NULL);
            if ((drillFlags & 4u) != 0u)
            {
                dpad |= kDpadLeft | kDpadRight;
            }
            if ((drillFlags & 16u) != 0u)
            {
                dpad |= kDpadDown;
            }
        }
        if (dpad != 0
            && (type != gfPadType::WIIMOTE
                || ((pad.m_buttonsCurrentFrame.bits & game::kPadWiiMinus) != 0 && !(mask & game::kPadWiiMinus))))
        {
            mask |= dpad;
            if (fighterPad && type == gfPadType::WIIMOTE && (pad.m_buttonsCurrentFrame.bits & dpad) != 0)
            {
                mask |= game::kPadWiiMinus;
            }
        }
        if (st.startState == kStartWait)
        {
            mask |= game::inMatchPad(type).start;
        }
        hide(pad, mask | st.releaseMask[port], (st.stickWaitMask & (1u << port)) != 0);
    }

    static void applyHiding(gfPadStatus* pads, const game::MatchState& m)
    {
        for (u32 i = 0; i < kPorts; i++)
        {
            hidePort(pads[i], i, m, false);
        }
    }
#pragma dont_inline reset

    // A fighter's controls come from the port's system pad (P+ reads it, the
    // pad thread writes it: stub's pad_hooks.cpp), so the lab's buttons are
    // taken out of the game's copy here, one by one: whatever is held with them
    // still reaches the fighter.
    void onFighterPad(u32 port, gfPadStatus* pad)
    {
        TM_PERF_SCOPE(kFighterPadLab);
        display::inputFighterPad(port, pad);
        hidePort(*pad, port, game::matchState(), true);
    }

    static void updateReleaseWaits(const gfPadStatus* pads)
    {
        MatchPadState& st = s_net->st;
        for (int i = 0; i < kPorts; i++)
        {
            st.releaseMask[i] &= st.sys[i].held;
            u8 bit = static_cast<u8>(1u << i);
            if ((st.stickWaitMask & bit) && (!st.sys[i].connected || stickNeutral(pads[i])))
            {
                st.stickWaitMask &= static_cast<u8>(~bit);
            }
        }
    }

#pragma dont_inline on
    static int repeat(Repeat& r, bool down, bool fast)
    {
        if (!down)
        {
            r.held = 0;
            return 0;
        }
        r.held++;
        int after = r.held - 1;
        if (after == 0)
        {
            return 1;
        }
        if (fast)
        {
            return after % ui::style::kRepeatFast == 0;
        }
        return after >= ui::style::kRepeatStart && (after - ui::style::kRepeatStart) % ui::style::kRepeatEvery == 0;
    }

    static void syncUnlockedPercents()
    {
        u8 preset = activeScenarioPreset();
        u8 drill = main::extraLoaded() ? ui::pages::practiceMenuState()->activeDrill : 0;
        if (!shouldSyncUnlockedPercentsOnOpen(settings().comboTrainer.enabled, preset, drill))
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        bool synced[main::kSideCount] = {false, false};
        for (int i = 0; i < n; i++)
        {
            int side = refs[i].cpu ? main::kSideCpu : main::kSideHuman;
            if (synced[side])
            {
                continue;
            }
            u16& dst = side == main::kSideHuman ? settings().general.playerPercent : settings().cpu.percent;
            syncUnlockedPercentValue(dst, main::lockSetting(side), game::damage(refs[i]));
            synced[side] = true;
        }
    }

    static bool applyStickToCustomDi(float sx, float sy, bool allowNeutral)
    {
        return stickToCustomDi(sx, sy, allowNeutral, settings().cpuTables.customDiX, settings().cpuTables.customDiY);
    }

    static void openMenu(int port, bool start)
    {
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        st.open = true;
        g_resetLabel[0] = 0;
        st.diPickActive = false;
        st.diCstickLocked = false;
        ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 0;
        strcpy(g_diPickLabel, "C-Stick / A");
        syncUnlockedPercents();
        record::clearDeleteConfirm();
        st.openStart = start;
        st.owner = port;
        st.nextCatHold = 0;
        st.nextCatUsedFast = false;
        if (net.menuFresh)
        {
            ui::menuReset(ui::labMenu(), net.menu);
            net.menuFresh = false;
        }
        net.menu.openStart = start;
        net.menu.controllerType = st.sys[port].controllerType;
        net.menu.resetConfirmPage = 0;
        memset(st.repeat, 0, sizeof(st.repeat));
        int shown = ui::currentPage(net.menu);
        if (shown == ui::pages::P_OLIMAR)
        {
            ui::pages::olimarSyncFromSettings();
        }
        TM_LOG("lab: menu opened by port %d (%s) on page %d (%s) row %d, depth %d, main row %d, rail %d\n", port + 1,
               start ? "Start" : "Z in pause", shown, ui::labMenu().pages[shown]->title, net.menu.cursor[shown],
               net.menu.depth, net.menu.railFocus ? net.menu.railCursor : net.menu.category, net.menu.railFocus ? 1 : 0);
        ui::menuAnim(true);
        ui::menuSound(game::kSoundOpen);
    }

    static void latchReleaseWaits()
    {
        MatchPadState& st = s_net->st;
        for (int i = 0; i < kPorts; i++)
        {
            st.releaseMask[i] = st.sys[i].held;
        }
        st.stickWaitMask = 0xFF;
    }

    static void closeByButton()
    {
        ui::menuSound(game::kSoundClose);
        closeMenu();
    }
#pragma dont_inline reset

    // Also called from the extra module (fighter_swap): keep exported.
#pragma force_active on
    bool menuOpen()
    {
        return s_net != NULL && s_net->st.open;
    }

    void closeMenu()
    {
        if (s_net == NULL || !s_net->st.open)
        {
            return;
        }
        MatchPadState& st = s_net->st;
        st.open = false;
        s_net->menu.resetConfirmPage = 0;
        g_resetLabel[0] = 0;
        if (st.diPickActive)
        {
            settings().cpuTables.customDiX = st.diPickSavedX;
            settings().cpuTables.customDiY = st.diPickSavedY;
            st.diPickActive = false;
        }
        ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 0;
        strcpy(g_diPickLabel, "C-Stick / A");
        record::clearDeleteConfirm();
        // Whatever is still held when the menu closes stays hidden until it
        // is let go, so it neither moves Training's menu nor the fighters.
        latchReleaseWaits();
        TM_LOG("lab: menu closed\n");
        ui::menuAnim(false);
        storage::settingsMenuClosed();
    }
#pragma force_active reset

    static void runMenu()
    {
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        if (st.openStart && firstPadPort(st.sys, 0) >= 0)
        {
            TM_LOG("lab: menu closed with Start\n");
            closeByButton();
            return;
        }
        if (static_cast<u32>(st.owner) >= kPorts || !st.sys[st.owner].connected)
        {
            // Owner disconnected while open: let any connected port close in
            // Z-in-Pause mode (pauseLab/start/B) or take ownership on a fresh
            // press, without auto-closing on a transient 1-frame hot-swap.
            for (int i = 0; i < kPorts; i++)
            {
                u32 pressed = st.sys[i].pressed;
                if (pressed == 0)
                {
                    continue;
                }
                const game::InMatchPad& m = game::inMatchPad(st.sys[i].controllerType);
                if (!st.openStart && (pressed & (m.pauseLab | m.start | kButtonB)) != 0)
                {
                    closeByButton();
                }
                else
                {
                    st.owner = static_cast<s8>(i);
                    st.nextCatHold = 0;
                    st.nextCatUsedFast = false;
                }
                return;
            }
            return;
        }
        const Raw& in = st.sys[st.owner];
        net.menu.controllerType = in.controllerType;
        const game::InMatchPad& map = game::inMatchPad(in.controllerType);
        // Z (Classic ZL/ZR, sideways -) jumps to Training's pause menu either
        // way: from over it, by closing; opened with Start, via openTrainingMenu.
        if ((in.pressed & map.pauseLab) != 0)
        {
            ui::menuSound(game::kSoundClose);
            openTrainingMenu();
            return;
        }
        // Live 2D stick capture for CPU > DI & SDI > Pick DI Angle:
        // Control-Stick modal fallback when A is pressed, plus non-modal C-Stick live aiming.
        bool onCustomDi = !net.menu.railFocus && ui::currentPage(net.menu) == ui::pages::P_CPU_DI
                          && settings().cpu.options.control == cpu::kControlLab
                          && settings().cpu.options.tdi == cpu::kTdiCustom;
        if (st.diPickActive)
        {
            if (!onCustomDi)
            {
                st.diPickActive = false;
                ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 0;
                strcpy(g_diPickLabel, "C-Stick / A");
            }
            else
            {
                float sx = 0.0f, sy = 0.0f;
                game::mainStick(in.stickX, in.stickY, &sx, &sy);
                applyStickToCustomDi(sx, sy, true);
                bool pressA = (in.pressed & kButtonA) != 0;
                if (pressA || (in.pressed & kButtonB) != 0)
                {
                    if (!pressA)
                    {
                        settings().cpuTables.customDiX = st.diPickSavedX;
                        settings().cpuTables.customDiY = st.diPickSavedY;
                    }
                    st.diPickActive = false;
                    ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 0;
                    strcpy(g_diPickLabel, pressA ? "Locked" : "C-Stick / A");
                    ui::menuSound(pressA ? game::kSoundConfirm : game::kSoundMove);
                }
                return;
            }
        }
        else if (onCustomDi)
        {
            float cx = 0.0f, cy = 0.0f;
            game::cStick(in.cStickX, in.cStickY, &cx, &cy);
            s8 aimX = settings().cpuTables.customDiX;
            s8 aimY = settings().cpuTables.customDiY;
            const char* lbl = NULL;
            bool soundLock = false;
            if (stickToCustomDi(cx, cy, false, aimX, aimY))
            {
                int dx = aimX - st.diCstickLockX;
                int dy = aimY - st.diCstickLockY;
                if (st.diCstickLocked && dx * dx + dy * dy <= 100)
                {
                    lbl = "Locked";
                }
                else
                {
                    st.diCstickLocked = false;
                    settings().cpuTables.customDiX = aimX;
                    settings().cpuTables.customDiY = aimY;
                    lbl = "Aiming...";
                    if ((in.pressed & kButtonA) != 0 && net.menu.cursor[ui::pages::P_CPU_DI] == 1)
                    {
                        st.diCstickLocked = true;
                        st.diCstickLockX = aimX;
                        st.diCstickLockY = aimY;
                        lbl = "Locked";
                        soundLock = true;
                    }
                }
            }
            else
            {
                st.diCstickLocked = false;
                if (strcmp(g_diPickLabel, "Aiming...") == 0)
                {
                    lbl = "C-Stick / A";
                }
            }
            if (lbl != NULL)
            {
                strcpy(g_diPickLabel, lbl);
            }
            if (soundLock)
            {
                ui::menuSound(game::kSoundConfirm);
                return;
            }
        }
        else
        {
            st.diCstickLocked = false;
        }
        ui::MenuInput mi;
        bool up = (in.held & kDpadUp) || in.stickY > kStickDeadZone;
        bool down = (in.held & kDpadDown) || in.stickY < -kStickDeadZone;
        bool left = (in.held & kDpadLeft) || in.stickX < -kStickDeadZone;
        bool right = (in.held & kDpadRight) || in.stickX > kStickDeadZone;
        bool anyDir = up || down || left || right;
        bool fast = (in.held & map.fastRepeat) != 0
                    || (game::hasAnalogTriggers(in.controllerType) && in.rAnalog >= kTriggerHeld);
        mi.up = repeat(st.repeat[0], up && !down, fast);
        mi.down = repeat(st.repeat[1], down && !up, fast);
        mi.left = repeat(st.repeat[2], left && !right, fast);
        mi.right = repeat(st.repeat[3], right && !left, fast);
        mi.a = (in.pressed & kButtonA) != 0;
        mi.b = (in.pressed & kButtonB) != 0;
        mi.prevCat = (in.pressed & map.prevCat) != 0;
        mi.nextCat = false;
        mi.resetOpt = (in.pressed & map.resetOpt) != 0;
        mi.resetPage = (in.pressed & map.resetPage) != 0;
        if (map.nextCat != 0)
        {
            if ((map.nextCat & map.fastRepeat) == 0)
            {
                mi.nextCat = (in.pressed & map.nextCat) != 0;
                st.nextCatHold = 0;
                st.nextCatUsedFast = false;
            }
            else
            {
                bool nextHeld = (in.held & map.nextCat) != 0;
                if (nextHeld)
                {
                    if (st.nextCatHold == 0)
                    {
                        if ((in.pressed & map.nextCat) != 0)
                        {
                            st.nextCatHold = 1;
                            st.nextCatUsedFast = anyDir || mi.prevCat;
                        }
                    }
                    else
                    {
                        if (anyDir || mi.prevCat)
                        {
                            st.nextCatUsedFast = true;
                        }
                        if (st.nextCatHold < 255)
                        {
                            st.nextCatHold++;
                        }
                    }
                }
                else if (st.nextCatHold > 0)
                {
                    if (!st.nextCatUsedFast && st.nextCatHold <= ui::style::kRepeatStart)
                    {
                        mi.nextCat = true;
                    }
                    st.nextCatHold = 0;
                    st.nextCatUsedFast = false;
                }
            }
        }
        else
        {
            st.nextCatHold = 0;
            st.nextCatUsedFast = false;
        }
        // B on the left rail closes the menu in both Start and Pause modes;
        // on Top Tabs depth 0, B closes in Pause mode (Start closes in Start mode).
        int prevPage = ui::currentPage(net.menu);
        u8 prevCur = net.menu.cursor[prevPage];
        bool prevRail = net.menu.railFocus;
        u8 prevRailCur = net.menu.railCursor;
        if (!ui::menuStep(ui::labMenu(), net.menu, mi) && (!st.openStart || prevRail))
        {
            closeByButton();
            return;
        }
        int nextPage = ui::currentPage(net.menu);
        if (nextPage != prevPage || net.menu.cursor[nextPage] != prevCur || net.menu.railFocus != prevRail
            || net.menu.railCursor != prevRailCur)
        {
            g_resetLabel[0] = 0;
            record::clearDeleteConfirm();
        }
    }

    static void onSystemPads(gfPadStatus* pads)
    {
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        net.systemCalls++;
        capture(st.sys, pads, true);
        display::systemPads(pads);
        game::MatchState m = game::matchState();
        bool inPause = m.running && m.training && m.paused;
        bool inMatch = m.running && m.training && !m.paused;
        if (st.exiting)
        {
            // The match is ending: no menu opens (Training's pause byte stays
            // set when the end starts from its pause, as after Finish, so Z
            // would find "in pause") and no press reaches the game.
            hideAll(pads);
            return;
        }
        if (st.open && !(st.openStart ? inMatch : inPause))
        {
            closeMenu();
        }
        if (st.open)
        {
            runMenu();
        }
        else if (inPause)
        {
            // Z on Finish is P+'s "reset the match" shortcut: left alone.
            if (game::trainingMenuCursor() != game::kTrainingMenuFinish)
            {
                int p = firstPadPort(st.sys, 2);
                if (p >= 0)
                {
                    openMenu(p, false);
                }
            }
        }
        else if (inMatch)
        {
            int p = firstPadPort(st.sys, 0);
            if (p >= 0)
            {
                if (startOpensMenu())
                {
                    if (st.startState == kStartIdle)
                    {
                        openMenu(p, true);
                    }
                }
                else
                {
                    st.pausePort = static_cast<s8>(p);
                    if (frameAdvanceOn(m) && st.startState == kStartIdle)
                    {
                        st.startState = kStartWait;
                        st.startPort = static_cast<u8>(p);
                        st.startTimer = kStartWindow;
                        TM_LOG("lab: Start on port %d during frame advance\n", p + 1);
                    }
                }
            }
        }
        // Live Combo Lab shortcuts: L + D-Pad adjusts CPU % (+/-1% L/R, +/-10% U/D);
        // bare D-Pad Down repositions CPU anchor in front of P1 (when not bound to FA).
        bool comboL = comboLHeld(m);
        if (comboL)
        {
            u32 dp = dpadBits(st.sys, m, false);
            int delta = (dp & kDpadRight) ? 1 : ((dp & kDpadLeft) ? -1 : ((dp & kDpadUp) ? 10 : ((dp & kDpadDown) ? -10 : 0)));
            if (delta != 0)
            {
                int pct = static_cast<int>(settings().cpu.percent) + delta;
                if (pct < 0)
                {
                    pct = 0;
                }
                else if (pct > 999)
                {
                    pct = 999;
                }
                settings().cpu.percent = static_cast<u16>(pct);
                applyPercent(main::kSideCpu);
                storage::notice(storage::kNoticeOk, "CPU Damage: %d%%", pct);
            }
        }
        // D-Pad shortcuts when menu is closed in active training match.
        if (!comboL && !st.open && !m.paused && m.running && m.training)
        {
            if (olimarDpadActive(m))
            {
                u32 dpHeld = dpadBits(st.sys, m, true);
                u32 dpPressed = dpadBits(st.sys, m, false);
                main::extraOlimarAction(main::kOlimarPreStep, dpHeld, dpPressed);
            }
            else
            {
                u32 dp = dpadBits(st.sys, m, false);
                if (dp != 0)
                {
                    u8 p = activeScenarioPreset();
                    int call = -1;
                    if ((dp & kDpadDown) != 0)
                    {
                        if (settings().controls.dpadDown == main::kDownFrameAdvance)
                        {
                            settings().general.frameAdvance ^= 1;
                            frameAdvanceChanged();
                        }
                        else if (p == ui::pages::kPresetTechchase)
                        {
                            call = main::kScenarioStepPosDown;
                        }
                        else if (comboShortcutActive())
                        {
                            call = main::kScenarioComboReposition;
                        }
                    }
                    if (call < 0 && p != ui::pages::kPresetOff)
                    {
                        if ((dp & kDpadUp) != 0)
                        {
                            if (p == ui::pages::kPresetShieldDrop) call = main::kScenarioCyclePlatform;
                            else if (p == ui::pages::kPresetTechchase) call = main::kScenarioStepPosUp;
                        }
                        else if (spacingShortcutActive(p))
                        {
                            if ((dp & kDpadLeft) != 0) call = main::kScenarioSpacingCloser;
                            else if ((dp & kDpadRight) != 0) call = main::kScenarioSpacingFarther;
                        }
                        else if (resetLeftPreset(p) && (dp & kDpadLeft) != 0)
                        {
                            call = main::kScenarioResetNow;
                        }
                    }
                    if (call >= 0)
                    {
                        main::extraScenarioCall(call);
                    }
                }
            }
        }
        // Frame Decrement: in frame advance a press steps back one frame; held
        // past the hold delay, repeats once per pass after the last arrives.
        int dec = scanFa(st.sys, true);
        bool hold = frameAdvanceOn(m) && !m.paused && !st.open && st.startState == kStartIdle && st.catchUp == 0;
        st.decrementHold = hold && (dec & 1) ? static_cast<u16>(st.decrementHold + 1) : 0;
        if (hold && ((dec & 2) || st.decrementHold > settings().controls.advanceDelay))
        {
            record::rewindRequest((dec & 2) != 0);
        }
        applyHiding(pads, m);
        if (st.startState == kStartInject && !st.startInjectedSystem && !m.paused)
        {
            injectStart(pads[st.startPort]);
            st.startInjectedSystem = true;
        }
        updateReleaseWaits(pads);
    }

    static void lockPercents(const game::MatchState& m)
    {
        if (!m.running)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int side = refs[i].cpu ? main::kSideCpu : main::kSideHuman;
            u16 percent = main::percentSetting(side);
            if (main::lockSetting(side) && game::damage(refs[i]) != game::i2f(percent))
            {
                game::setDamage(refs[i], percent);
            }
        }
    }

    static void maintainPlayerShields(bool stepping)
    {
        if (!stepping)
        {
            return;
        }
        LabNet& net = *s_net;
        u8 mode = settings().general.playerInfiniteShields;
        if (mode == cpu::kShieldsOff)
        {
            memset(net.playerShieldHit, 0, sizeof(net.playerShieldHit));
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].cpu)
            {
                continue;
            }
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            Fighter* bodies[2] = {refs[i].fighter, refs[i].partner};
            for (int sub = 0; sub < 2; sub++)
            {
                Fighter* f = bodies[sub];
                if (f == NULL || (sub == 1 && f == bodies[0]))
                {
                    continue;
                }
                const soModuleEnumeration* mods = game::fighterModules(f);
                if (mods == NULL)
                {
                    continue;
                }
                int status = game::fighterStatus(mods);
                if (status < 0)
                {
                    continue;
                }
                if (stepPlayerShield(mode, status, net.playerShieldHit[p][sub]))
                {
                    cpu::setShieldHealth(f, static_cast<float>(cpu::kShieldFull));
                }
            }
        }
    }

    static void logFighters(const game::MatchState& m)
    {
        if (s_net->fightersLogged || !m.running)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        if (n == 0)
        {
            return;
        }
        s_net->fightersLogged = true;
        for (int i = 0; i < n; i++)
        {
            const game::FighterRef& r = refs[i];
            TM_LOG("fighters: entry %x player %d port %d %s fighter %08x partner %08x owner %08x damage %.1f\n",
                   r.entryId, r.playerNo + 1, r.port + 1, r.cpu ? "cpu" : "human", r.fighter, r.partner, r.owner,
                   game::damage(r));
        }
    }

    static void applyOverlays(bool live)
    {
        const main::Settings& s = settings();
        game::OverlayConfig config;
        config.groups[0] = s.overlay[main::kSideHuman].groups;
        config.groups[1] = s.overlay[main::kSideCpu].groups;
        config.techInvis = s.techInvis.mode;
        config.techDelay = s.techInvis.delay;
        config.hideModels = display::hiddenModels();
        config.quiet = ui::g_menuQuiet;
        game::overlaysApply(live, config);
    }

    void onFighterInput(const iface::FighterInput* call)
    {
        if (s_net == NULL)
        {
            return;
        }
        const MatchPadState& st = s_net->st;
        game::MatchState m = game::matchState();
        u32 bound = boundDpad(m);
        bool fa = frameAdvanceOn(m);
        cpu::Owner who;
        if ((!fa && bound == 0) || !cpu::findOwner(call->input, call->entryId, who))
        {
            return;
        }
        int port = who.cpu ? -1 : game::playerPort(who.playerNo);
        bool validPort = static_cast<u32>(port) < kPorts;
        u32 type = (validPort && st.sys[port].connected)
                       ? static_cast<u32>(st.sys[port].controllerType)
                       : static_cast<u32>(port >= 4 ? gfPadType::WIIMOTE : gfPadType::GCC);
        // L or R as Frame Advance (or Decrement) Button: P+ also copies the
        // analog triggers into the fighter's variables (cpu/view.cpp), so they
        // are taken out here, before its scripts read them (GC / Classic only).
        if (fa && (who.cpu || game::hasAnalogTriggers(type)))
        {
            u32 triggers = faBits(type) & (kButtonL | kButtonR);
            if (triggers != 0)
            {
                Fighter* fighter = cpu::fighterOf(who.entryId, who.sub);
                if (triggers & kButtonL)
                {
                    cpu::hideAnalogTrigger(fighter, false);
                }
                if (triggers & kButtonR)
                {
                    cpu::hideAnalogTrigger(fighter, true);
                }
            }
        }
        if (bound == 0 || who.cpu || who.sub || type != gfPadType::WIIMOTE)
        {
            return;
        }
        u32* buttons = reinterpret_cast<u32*>(reinterpret_cast<u32>(call->input) + 0xC);
        if (validPort && dpadAllowed(st.sys[port], m))
        {
            *buttons &= ~kAppealAll;
        }
    }

    static void onGamePads(gfPadStatus* pads)
    {
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        net.gameCalls++;
        if (game::frozen())
        {
            net.frozenGameCalls++;
        }
        capture(st.game, pads, false);
        display::capturePads(pads);
        game::MatchState m = game::matchState();
        bool inject = false;
        if (st.startState != kStartIdle && (m.paused || --st.startTimer <= 0))
        {
            st.startState = kStartIdle;
        }
        else if (st.startState == kStartWait && game::otherStopBits() == 0)
        {
            st.startState = kStartInject;
            st.startInjectedSystem = false;
        }
        else if (st.startState == kStartInject && st.startInjectedSystem)
        {
            inject = true;
            st.startState = kStartAwait;
        }
        bool want = frameAdvanceOn(m) && !m.paused && !st.open && st.startState == kStartIdle && !st.exiting;
        // The Start-opened lab menu holds the whole game. P+'s
        // pad read runs steps while Start is held despite our stop bit
        // (freeze.cpp), so the gameplay and camera tasks are paused instead.
        bool menuHold = st.open && st.openStart && m.running && !m.paused;
        bool startHeld = firstPadPort(st.game, 1) >= 0;
        // Frame advance stops every task, HUD and camera
        // included, which would freeze Training's closing menu and close-up
        // camera: for kCatchUp steps after its pause only the gameplay
        // categories are paused.
        if (st.wasPaused && !m.paused && frameAdvanceOn(m))
        {
            st.catchUp = kCatchUp;
        }
        st.wasPaused = m.paused;
        if (!want)
        {
            st.catchUp = 0;
        }
        bool run = true;
        bool gameplayPaused = false;
        // Asked before every step: a rewind holds the game for its load,
        // then runs it to the frame and holds it there.
        int rewind = record::rewindStep();
        // No menu sounds while the lab loads and replays frames on its own.
        ui::g_menuQuiet = rewind != record::kRewindIdle;
        if (st.exiting)
        {
            // Training's operator must step to end the match (match.cpp), so
            // nothing is held: without Training's pause (Finish keeps it until
            // the scene ends) the fighters run on, inputless, for the end's
            // steps (a wait of 15, then the scene's end). Pausing the gameplay
            // tasks stops the end (observed, Mario vs Fox on Final Destination:
            // the operator reached 392, the scene never exited).
            st.hold = 0;
        }
        else if (menuHold)
        {
            st.hold = 0;
            run = false;
            gameplayPaused = startHeld;
        }
        else if (rewind != record::kRewindIdle)
        {
            st.hold = 0;
            run = rewind == record::kRewindRun;
        }
        else if (want && (st.catchUp > 0 || (startOpensMenu() && startHeld)))
        {
            st.catchUp -= st.catchUp > 0 ? 1 : 0;
            st.hold = 0;
            run = false;
            gameplayPaused = true;
        }
        else if (want)
        {
            // A press runs one step, holding past the hold delay runs the game:
            // adapted from ProjectPunch's frame_advance.cpp (MIT; see
            // THIRD_PARTY_NOTICES.md).
            int adv = scanFa(st.game, false);
            st.hold = (adv & 1) ? static_cast<u16>(st.hold + 1) : 0;
            run = (adv & 2) != 0 || st.hold > settings().controls.advanceDelay;
            if (run)
            {
                net.advancedSteps++;
            }
        }
        else
        {
            st.hold = 0;
        }
        // The step before's frame went to its render callback (module.cpp
        // onRender) unless no picture followed (game/tracker.h); the coming
        // step's overlays, display and pads are set after. Off while Exit
        // ends the match.
        bool live = m.running && !m.paused && !st.exiting;
        {
            TM_PERF_SCOPE(kTrackerStep);
            game::trackerStep(live);
        }
        applyOverlays(live);
        display::step(m, st.open);
        if (main::extraInfoWanted())
        {
            main::extraInfoStep(net.prevRan);
        }
        // A game held by ProjectPunch's pause or frame advance (the same
        // stop bit) runs no step: the tracker, match timer and frame counts
        // stop. The steps of a match ending after Exit are not handed over.
        net.prevRan = run && m.running && !m.paused && !game::heldByOthers() && !st.exiting;
        game::trackerStepRuns(net.prevRan);
        game::setGameplayPaused(gameplayPaused);
        game::setCameraPaused(menuHold && startHeld);
        game::setFrozen(!run && !gameplayPaused);
        lockPercents(m);
        maintainPlayerShields(net.prevRan);
        if (net.prevRan && !st.open && olimarDpadActive(m))
        {
            main::extraOlimarAction(main::kOlimarPreStep, dpadBits(st.sys, m, true), 0u);
        }
        logFighters(m);
        applyHiding(pads, m);
        if (st.exiting)
        {
            hideAll(pads);
        }
        else if (inject)
        {
            injectStart(pads[st.startPort]);
        }
    }

    static void onSettingsUpgraded()
    {
        cpu::controlledByChanged();
        osdSettingsChanged();
        frameAdvanceChanged();
        game::codeMenuSync();
        main::extraStageOptionsChanged();
    }

    void onPads(u32 phase, gfPadStatus* pads)
    {
        if (s_net == NULL)
        {
            return;
        }
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        if (!net.vsReadOnly && !st.exiting && game::matchState().running && ui::settingsModalOpen())
        {
            bool confirmed = false;
            if (phase == iface::kPadsSystem)
            {
                capture(st.sys, pads, true);
                int active = -1;
                for (int i = 0; i < kPorts; i++)
                {
                    if (!st.sys[i].connected)
                    {
                        continue;
                    }
                    bool busy = st.sys[i].held != 0 || st.sys[i].stickX <= -kStickDeadZone
                                || st.sys[i].stickX >= kStickDeadZone || st.sys[i].stickY <= -kStickDeadZone
                                || st.sys[i].stickY >= kStickDeadZone;
                    if (active < 0 || busy)
                    {
                        active = i;
                        if (busy)
                        {
                            break;
                        }
                    }
                }
                if (active >= 0)
                {
                    const Raw& r = st.sys[active];
                    ui::SettingsModalInput mi = {r.held, r.pressed, r.stickX, r.stickY, r.controllerType};
                    confirmed = ui::settingsModalStep(mi);
                }
            }
            else if (phase == iface::kPadsGame)
            {
                game::setFrozen(true);
            }
            hideAll(pads);
            if (confirmed)
            {
                latchReleaseWaits();
                game::setFrozen(false);
                onSettingsUpgraded();
            }
            return;
        }
        if (!net.vsReadOnly && !st.exiting && !st.open && main::extraDrill(phase, pads) != 0u)
        {
            return;
        }
        if (phase == iface::kPadsSystem)
        {
            onSystemPads(pads);
        }
        else if (phase == iface::kPadsGame)
        {
            onGamePads(pads);
        }
    }

    void applyPercent(int side)
    {
        u16 percent = main::percentSetting(side);
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].cpu == (side == main::kSideCpu))
            {
                game::setDamage(refs[i], percent);
                TM_LOG("lab: %s percent %d applied to entry %x (P%d), damage now %.1f\n",
                       side == main::kSideCpu ? "CPU" : "Player", percent, refs[i].entryId, refs[i].playerNo + 1,
                       game::damage(refs[i]));
            }
        }
    }

    void osdSettingsChanged()
    {
        game::advantageSetTolerance(settings().osd.advantageTolerance);
        osd::setPosition(settings().osd.position);
    }

    char g_resetLabel[12];
    char g_diPickLabel[16] = "C-Stick / A";

    void toggleDiStickPicker()
    {
        if (s_net == NULL)
        {
            return;
        }
        MatchPadState& st = s_net->st;
        if (!st.diPickActive)
        {
            st.diPickSavedX = settings().cpuTables.customDiX;
            st.diPickSavedY = settings().cpuTables.customDiY;
            st.diPickActive = true;
            ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 1;
            strcpy(g_diPickLabel, "Aiming...");
        }
        else
        {
            st.diPickActive = false;
            ui::menuScratch()[ui::kDiPickActiveScratchOffset] = 0;
            strcpy(g_diPickLabel, "C-Stick / A");
        }
    }

    void resetAllPressed()
    {
        if (g_resetLabel[0] == 0)
        {
            strcpy(g_resetLabel, "A: Confirm");
            return;
        }
        g_resetLabel[0] = 0;
        resetLabSettings();
    }

    void resetLabSettings()
    {
        main::resetSettings();
        storage::settingsReset();
        cpu::controlledByChanged();
        osdSettingsChanged();
        frameAdvanceChanged();
        game::codeMenuSync();
        main::extraStageOptionsChanged();
        main::extraScenarioCall(main::kScenarioSettingsReset);
        if (s_net != NULL)
        {
            memset(s_net->playerShieldHit, 0, sizeof(s_net->playerShieldHit));
        }
        TM_LOG("lab: settings reset to the defaults\n");
    }

    void openTrainingMenu()
    {
        if (s_net == NULL)
        {
            return;
        }
        MatchPadState& st = s_net->st;
        bool start = st.openStart;
        int port = st.owner;
        closeMenu();
        if (start)
        {
            // Hand the game one Start press so Training's pause opens.
            st.startState = kStartInject;
            st.startPort = port;
            st.startTimer = kStartWindow;
            st.startInjectedSystem = false;
            st.pausePort = port;
        }
    }

    // Ends the match as Training's Finish does; what a match end must undo
    // runs at the module's detach: docs/internals.md, "Ending the match from
    // the lab".
    void exitMatch()
    {
        closeMenu();
        game::EndResult how = game::endTrainingMatch();
        if (s_net != NULL)
        {
            MatchPadState& st = s_net->st;
            st.exiting = how != game::kEndRefused;
            if (st.exiting)
            {
                st.startState = kStartIdle;
                st.catchUp = 0;
            }
        }
        TM_LOG("lab: exit: %s\n", how == game::kEndFromPause ? "Training's end of match started, its pause closed"
                                  : how == game::kEndByWait ? "Training's end of match started, no pause"
                                                            : "refused (no running Training match, or it is ending)");
    }

    // Frame advantage OSD for the attacker (human or CPU): sign-based colours
    // (+N green, 0 neutral, -N red).
    static void onAdvantage(const game::Advantage& r)
    {
        // With the extra module a shield hit's value was shown when it landed
        // (extra/frame_data.h). The measured value checks it: the same is not
        // posted again, a different one is posted marked as actual, even past
        // the display limit (true: dealt with). Past the limit with nothing
        // shown stays unshown, as in ProjectPunch.
        if (main::extraAdvantageMeasured(r) || r.hidden)
        {
            return;
        }
        const main::Settings& s = settings();
        if (!(r.shield ? s.osd.frameAdvantage : s.osd.frameAdvantageOnHit))
        {
            return;
        }
        if (r.shield)
        {
            osd::post(r.attackerPlayer, osd::kIdFrameAdvantage, osd::frameAdvantageColor(r.value),
                      "Shield Adv.\n%+d f", r.value);
        }
        else
        {
            osd::post(r.attackerPlayer, osd::kIdFrameAdvantageHit, osd::frameAdvantageColor(r.value),
                      "Hit Adv.\n%+d f", r.value);
        }
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static osd::Message* postTechFrame(const game::Technique& t, osd::Id id, gfx::Rgba col, const char* title)
    {
        return osd::post(t.player, id, col, "%s\nFrame %d", title, t.frame);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Technique OSD messages for the fighter they describe (human or CPU).
    static void onTechnique(const game::Technique& t)
    {
        const main::OsdSettings& o = settings().osd;
        osd::Message* m = NULL;
        bool first = t.frame == 1;
        switch (t.kind)
        {
        case game::kTechWavedash:
            if (o.wavedash)
            {
                float angle = game::i2fScale(game::f2i(t.angle * 10.0f), 0.1f);
                m = osd::post(t.player, osd::kIdWavedash, osd::kWhite, "Wavedash: f%d\nAngle: %2.1f\n%s (%df)",
                              t.frame, angle, t.flag ? "SH" : "FH", t.a);
                osd::lineColor(m, 0, first ? osd::kGreen : osd::kWhite);
                osd::lineColor(m, 1, osd::wavedashAngleColor(angle));
                osd::lineColor(m, 2, t.flag ? osd::kGreen : osd::kWhite);
            }
            break;
        case game::kTechLCancel:
            if (o.lCancel)
            {
                int rate = t.b > 0 ? t.a * 100 / t.b : 0;
                if (t.flag == game::kLCancelAuto)
                {
                    m = osd::post(t.player, osd::kIdLCancel, osd::kWhite, "L-Cancel (%d%%)\nAuto (Rule On)", rate);
                    osd::lineColor(m, 1, osd::kYellow);
                }
                else
                {
                    gfx::Rgba c = t.flag == game::kLCancelHit ? osd::kGreen : osd::kRed;
                    if (t.frame == game::kNoValue)
                    {
                        m = osd::post(t.player, osd::kIdLCancel, osd::kWhite, "L-Cancel (%d%%)\nMissed", rate);
                    }
                    else
                    {
                        m = osd::post(t.player, osd::kIdLCancel, osd::kWhite, "L-Cancel (%d%%)\nFrame %d/7", rate, t.frame);
                    }
                    osd::lineColor(m, 1, c);
                }
            }
            break;
        case game::kTechActOos:
            if (o.actOos)
            {
                postTechFrame(t, osd::kIdActOos, first ? osd::kGreen : osd::kWhite, "OoS Action");
            }
            break;
        case game::kTechActOoLag:
            if (o.actOoLag)
            {
                m = postTechFrame(t, osd::kIdActOoLag, osd::kWhite, t.flag ? "Autocancel" : "Post-Lag");
                osd::lineColor(m, 1, first ? osd::kGreen : osd::kRed);
            }
            break;
        case game::kTechJumpCancel:
            if (o.jumpCancel)
            {
                postTechFrame(t, osd::kIdJumpCancel, first ? osd::kGreen : osd::kRed, "Jump Cancel");
            }
            break;
        case game::kTechFastfall:
            if (o.fastfall)
            {
                m = postTechFrame(t, osd::kIdFastfall, osd::kWhite, "Fastfall");
                osd::lineColor(m, 1, first ? osd::kGreen : osd::kWhite);
            }
            break;
        case game::kTechCombo:
            if (o.comboCounter)
            {
                osd::post(t.player, osd::kIdCombo, osd::kWhite, "Combo\n%d Hits (%d%%)", t.a, t.b);
            }
            break;
        case game::kTechSdi:
            if (o.sdi)
            {
                osd::post(t.player, osd::kIdSdi, t.a > 0 ? osd::kGreen : osd::kRed, "SDI\n%d/%d Shifts", t.a, t.b);
            }
            break;
        case game::kTechCliffwait:
            if (o.ledgedashInfo)
            {
                if (t.frame == game::kNoValue && t.b > 5)
                {
                    osd::post(t.player, osd::kIdGalint, osd::kRed, "Ledge Limit\nGrab %d/5", t.b);
                }
                else
                {
                    osd::post(t.player, osd::kIdCliffwait, t.flag ? osd::kGreen : osd::kRed, "Ledge Hang\n%d Frames",
                              t.frame);
                }
            }
            break;
        case game::kTechGalint:
        {
            // The cells as digits (game::LedgeCell), for the tests.
            const game::Ledgedash& l = game::lastLedgedashEvent();
            char cells[game::kLedgeCells + 1];
            int n = l.count < game::kLedgeCells ? l.count : game::kLedgeCells;
            for (int k = 0; k < n; k++)
            {
                cells[k] = static_cast<char>('0' + l.cells[k]);
            }
            cells[n] = '\0';
            TM_LOG("ledgedash: P%d galint %d ledge %d angle %.1f airdodge %d cells %s no intangibility %d grabs %d\n",
                   t.player + 1, t.frame, t.a, t.angle, l.hasAngle, cells, t.flag, l.grabCount);
        }
            // A hang with no intangibility (P+'s 5-ledge-grab limit) has no
            // GALINT: frames would count from an earlier grab.
            if (o.ledgedashInfo && t.flag)
            {
                if (t.b > 5)
                {
                    osd::post(t.player, osd::kIdGalint, osd::kRed, "GALINT\nNone (Grab %d/5)", t.b);
                }
                else
                {
                    osd::post(t.player, osd::kIdGalint, osd::kRed, "GALINT\nNone (Limit)");
                }
            }
            else if (o.ledgedashInfo && t.frame >= -24)
            {
                osd::post(t.player, osd::kIdGalint, t.frame > 0 ? osd::kGreen : osd::kRed, "GALINT\n%+d Frames",
                          t.frame);
            }
            break;
        case game::kTechActOoAirborne:
            if (o.actOoAirborne)
            {
                // A double jump out of a jump uses its own id so both can show.
                static const char* const kFrom[] = {"", "Jump", "DJ", "Fall", "Drop"};
                int from = t.flag & game::kAirFromMask;
                if (from > game::kAirDrop)
                {
                    break;
                }
                bool dj = (t.flag & game::kAirToJump) != 0;
                osd::post(t.player, dj ? osd::kIdMiscellaneous : osd::kIdActOoAirborne, first ? osd::kGreen : osd::kRed,
                          "%s (%s)\nFrame %d",
                          dj ? "Double Jump" : (t.flag & game::kAirSpecial) != 0 ? "Special" : "Aerial", kFrom[from],
                          t.frame);
            }
            break;
        case game::kTechDashback:
            if (o.dashback)
            {
                static const char* const kText[] = {"Missed Dashback", "Dashback", "Slow Dashback"};
                static const gfx::Rgba kColor[] = {osd::kRed, osd::kGreen, osd::kYellow};
                m = osd::post(t.player, osd::kIdDashback, osd::kWhite, "%s\nRate: %d%%", kText[t.flag],
                              t.b > 0 ? t.a * 100 / t.b : 0);
                osd::lineColor(m, 0, kColor[t.flag]);
            }
            break;
        case game::kTechPowershield:
            if (o.powershield)
            {
                if (t.flag == game::kPowershieldDone)
                {
                    osd::post(t.player, osd::kIdPowershield, osd::kGreen, "Powershield\nFrame %d", t.frame);
                    break;
                }
                m = osd::post(t.player, osd::kIdPowershield, osd::kRed,
                              t.flag == game::kPowershieldEarly ? "Missed PS\n%df Early" : "Missed PS\nBuffered",
                              t.frame);
                osd::lineColor(m, 0, osd::kWhite);
            }
            break;
        case game::kTechTechLockout:
            if (o.lockoutTimers)
            {
                osd::post(t.player, osd::kIdLockout, osd::kRed, "Tech Lockout\n%df Left", t.frame);
            }
            break;
        case game::kTechItemThrow:
            if (o.itemThrow)
            {
                postTechFrame(t, osd::kIdItemThrow, osd::kWhite, t.flag ? "Air Item Throw" : "Glide Toss");
            }
            break;
        case game::kTechBoostGrab:
            if (o.boostGrab)
            {
                postTechFrame(t, osd::kIdBoostGrab, osd::kWhite, t.flag ? "DACUS" : "Boost Grab");
            }
            break;
        case game::kTechGrabBreakout:
            if (o.grabBreakout)
            {
                if (t.flag != 0)
                {
                    osd::post(t.player, osd::kIdGrabBreakout, t.frame <= t.a + 3 ? osd::kGreen : osd::kWhite,
                              "Grab Escape\nFrames %d / %d\n%.1f / frame", t.frame, t.a, t.angle);
                }
                else
                {
                    osd::post(t.player, osd::kIdGrabBreakout, osd::kWhite, "Grab Escape\nTimer: %d\n%.1f / frame", t.a,
                              t.angle);
                }
            }
            break;
        case game::kTechActOoHitstun:
            if (o.actOoHitstun)
            {
                postTechFrame(t, osd::kIdActOoHitstun, first ? osd::kGreen : osd::kWhite, "Out of Hitstun");
            }
            break;
        default:
            break;
        }
    }

    static void onTrackerFrame()
    {
        {
            TM_PERF_SCOPE(kAdvantage);
            game::advantageFrame();
        }
        {
            TM_PERF_SCOPE(kFrameData);
            main::extraFrameData();
        }
        {
            TM_PERF_SCOPE(kTechniques);
            game::techniquesFrame();
        }
        if (s_net != NULL && !s_net->vsReadOnly)
        {
            {
                TM_PERF_SCOPE(kCharacterRng);
                main::extraCharacterRng();
            }
            main::extraStageOptionsTick();
        }
    }

    void frameAdvanceChanged()
    {
        TM_LOG("lab: frame advance %s, button %d\n", settings().general.frameAdvance ? "on" : "off",
               settings().controls.advanceButton);
        if (!settings().general.frameAdvance)
        {
            game::setFrozen(false);
            game::setGameplayPaused(false);
        }
    }

    // Text-width slot (gfx::textBox) of the pause-menu hint.
    enum { kSlotHint = gfx::kTextSlots - 2 };
    static const gfx::Rgba kYellow = 0xF2D45CFF;
    static const gfx::Rgba kBox = 0x000000C0;

    // The top band and the panel at the right as they are drawn this frame
    // (ui/hud_layout.h labBand): the Ledgedash HUD takes its room only while
    // it shows.
    static void setLayout(bool timer)
    {
        using namespace ui::layout;
        bool wide = gfx::widescreen();
        bool ledge = ui::ledgedashHudVisible();
        setLabBand(labBand(timer, ledge ? ui::ledgedashCells() : 0, wide));
    }

    static const char* pauseHint()
    {
        const MatchPadState& st = s_net->st;
        int p = st.pausePort;
        if (static_cast<u32>(p) >= kPorts || !st.sys[p].connected)
        {
            p = game::playerPort(0);
            if (static_cast<u32>(p) >= kPorts || !st.sys[p].connected)
            {
                for (p = 0; p < kPorts && !st.sys[p].connected; p++)
                {
                }
            }
        }
        return game::pauseHint(static_cast<u32>(p) < kPorts ? st.sys[p].controllerType : 0u);
    }

    // Info panels and OSD messages hide while the lab menu or pause is open.
    void onRender(u32 call, const game::MatchState& m)
    {
        if (s_net == NULL)
        {
            return;
        }
        LabNet& net = *s_net;
        MatchPadState& st = net.st;
        bool ro = net.vsReadOnly;
        bool modal = !ro && ui::settingsModalOpen();
        bool info = m.running && !st.open && !modal && !m.paused && main::extraInfoWanted();
        bool messages = m.running && !m.paused && !st.open && !modal && osd::visible();
        // Z opens the lab menu from Training's pause in both modes; the hint
        // shows only where that is the way in.
        bool hint = !ro && m.running && m.training && m.paused && !st.open && !modal && !startOpensMenu();
        bool hud = ro || display::hudShown();
        setLayout(hud);
        bool drillHud = !ro && m.running && !m.paused && !st.open && !modal && (main::extraDrill(2, NULL) & 1u) != 0u;
        bool ledge = m.running && !m.paused && !st.open && !modal && !drillHud && ui::ledgedashHudVisible();
        // Training leaves Brawl's own timer off: it is turned on here instead
        // (ui/match_hud.cpp), hidden while the Ledgedash HUD, Skill Drill HUD,
        // or settings update modal is shown.
        bool timer = !ro && !modal && m.running && hud && !drillHud && !ui::ledgedashHudVisible()
                     && (!m.paused || st.open);
        if (timer)
        {
            ui::showMatchTimer(game::trackerFrame());
        }
        if (m.running)
        {
            if (!ro)
            {
                {
                    TM_PERF_SCOPE(kRender3d);
                    display::render3d(m);
                }
                if (!m.paused && settings().comboTrainer.enabledMeters != 0)
                {
                    display::debugDrawBegin();
                    main::extraComboDraw(m);
                    display::debugDrawEnd();
                }
            }
            gfx::begin();
            if (gfx::lineHeight(0.5f) > 0.0f)
            {
                if (messages)
                {
                    TM_PERF_SCOPE(kOsdDraw);
                    osd::draw();
                }
                if (info)
                {
                    TM_PERF_SCOPE(kInfoDraw);
                    main::extraInfoDraw();
                }
                if (ledge)
                {
                    TM_PERF_SCOPE(kLedgeHud);
                    ui::drawLedgedashHud();
                }
                if (hint)
                {
                    const char* text = pauseHint();
                    if (text != st.lastHint)
                    {
                        gfx::forgetWidths();
                        st.lastHint = text;
                    }
                    const float hintScale = ui::style::scaleForCap(ui::style::kTypeMicro);
                    float hx = ui::layout::kHintRight - gfx::textWidth(text, hintScale);
                    gfx::textBox(kSlotHint, hx, ui::layout::kHintY, text, kYellow, kBox, hintScale, false);
                }
                if (!ro)
                {
                    if (!modal)
                    {
                        display::render2d(m, st.open);
                        if (!m.paused && !st.open)
                        {
                            main::extraInputViewerDraw(m, st.open);
                            main::extraDiCompassDraw(m, st.open);
                        }
                    }
                    if (st.open)
                    {
                        TM_PERF_SCOPE(kMenuDraw);
                        ui::menuDraw(ui::labMenu(), net.menu);
                    }
                    else if (!modal)
                    {
                        ui::menuDrawClosing();
                    }
                    ui::settingsModalDraw();
                }
                else if (!m.paused && !st.open)
                {
                    main::extraPsaInspectorDraw(m, st.open);
                }
                if (hud && !ro && !modal && !st.open)
                {
                    TM_PERF_SCOPE(kWatermark);
                    ui::drawWatermark();
                }
            }
            gfx::end();
        }
        if (!ro && settings().display.debugOverlay)
        {
            TM_PERF_SCOPE(kDebugOverlay);
            ui::drawDebugOverlay(call, m);
        }
    }

    bool savestateBusy()
    {
        if (s_net == NULL)
        {
            return false;
        }
        const MatchPadState& st = s_net->st;
        return st.open || st.exiting || st.startState != kStartIdle || st.catchUp > 0
               || game::categoriesPaused() || main::extraFighterSwapBusy() || (main::extraDrill(0, NULL) & 2u) != 0u
               || ui::settingsModalOpen() || comboLHeld(game::matchState());
    }

    // Only the extra module (fighter_swap) calls this: keep exported.
#pragma force_active on
    void prepareSwap()
    {
        game::overlaysClear();
        osd::reset();
        game::advantageReset(onAdvantage);
        game::techniquesReset(onTechnique);
        main::extraInfoReset();
        if (s_net != NULL)
        {
            memset(s_net->playerShieldHit, 0, sizeof(s_net->playerShieldHit));
            s_net->st.hold = 0;
        }
    }
#pragma force_active reset

    // Before a save or load copies memory the colour overlays and hiding are
    // put back (a slot never holds them); the next step applies them again.
    void savestateCopying()
    {
        game::overlaysClear();
        main::extraScenarioCall(main::kScenarioSavestateSaving);
    }

    // The match is back at the saved frame: messages, advantage exchanges
    // and technique sequences (the Ledgedash HUD's last ledgedash too) of the
    // timeline left behind go; frame advance and the percent locks stay.
    void savestateLoaded()
    {
        osd::reset();
        game::advantageReset(onAdvantage);
        game::techniquesReset(onTechnique);
        main::extraInfoReset();
        main::extraDiReset();
        main::extraComboReset();
        main::extraHeatmapReset();
        main::extraAdaptiveReset();
        main::extraScenarioCall(main::kScenarioSavestateLoaded);
        main::extraOlimarAction(main::kOlimarSavestateLoaded, 0u, 0u);
        main::extraCharacterRng();
        main::extraStageOptionsSavestateLoaded();
        game::reapplyFrozen();
        display::savestateLoaded();
        if (s_net != NULL)
        {
            memset(s_net->playerShieldHit, 0, sizeof(s_net->playerShieldHit));
            s_net->st.hold = 0;
        }
    }

    void attach()
    {
#ifdef __MWERKS__
        if (s_net == NULL)
        {
            s_net = static_cast<LabNet*>(game::allocNetworkZero(static_cast<u32>(sizeof(LabNet))));
        }
        if (s_net == NULL)
        {
            return;
        }
#else
        memset(s_net, 0, sizeof(*s_net));
#endif
        LabNet& net = *s_net;
        net.st.owner = -1;
        net.st.pausePort = -1;
        net.prevRan = true;
        net.menuFresh = true;
        net.vsReadOnly = !game::trainingGameMode();
        game::trackerAttach();
#ifdef __MWERKS__
        game::advantageAttach();
#endif
        game::advantageReset(onAdvantage);
        game::techniquesAttach();
        game::techniquesReset(onTechnique);
#ifdef __MWERKS__
        game::overlaysAttach();
#endif
        game::setFrameListener(onTrackerFrame);
        gfx::textAttach();
        osd::attach();
        osdSettingsChanged();
        ui::settingsModalAttach();
        if (!net.vsReadOnly)
        {
            display::attach();
        }
        bool wide = game::widescreen();
        gfx::setWidescreen(wide);
        if (!net.vsReadOnly)
        {
            TM_LOG("lab: screen %s; frame advance %s, overlay %s\n", wide ? "16:9" : "4:3",
                   settings().general.frameAdvance ? "on" : "off", settings().display.debugOverlay ? "on" : "off");
#ifdef __MWERKS__
            TM_LOG("lab: menu pointers def %08x state %08x\n", reinterpret_cast<u32>(&ui::labMenu()),
                   reinterpret_cast<u32>(&net.menu));
#endif
        }
    }

    void detach()
    {
        if (s_net == NULL)
        {
            return;
        }
        LabNet& net = *s_net;
        bool ro = net.vsReadOnly;
        if (!ro)
        {
            display::detach();
        }
        osd::detach();
        gfx::textDetach();
#ifdef __MWERKS__
        game::overlaysDetach();
#else
        if (!ro)
        {
            game::overlaysClear();
        }
#endif
        game::techniquesDetach();
#ifdef __MWERKS__
        game::advantageDetach();
#endif
        game::trackerDetach();
        if (ro)
        {
            net.vsReadOnly = false;
#ifdef __MWERKS__
            game::freeNetworkSlot(reinterpret_cast<void**>(&s_net));
#endif
            return;
        }
        game::setFrozen(false);
        game::setGameplayPaused(false);
        game::setCameraPaused(false);
        TM_LOG("lab: pad callbacks: system %u, game %u (%u while stopped), frame-advance steps %u\n", net.systemCalls,
               net.gameCalls, net.frozenGameCalls, net.advancedSteps);
#ifdef __MWERKS__
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_net));
#endif
    }
}
}
