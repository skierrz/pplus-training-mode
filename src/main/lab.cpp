#include "main/lab.h"

#include <cstdio>
#include <string.h>
#include <gf/gf_pad_status.h>

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
#include "main/ui/pages/scenarios.h"
#include "main/ui/pages/pages.h"
#include "main/ui/settings_modal.h"
#include "main/ui/style.h"
#include "main/ui/watermark.h"
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
        // Exit was chosen and the game is ending the match (exitMatch): from
        // then until the module detaches every step runs and none is handed
        // over, the observers are off, savestates are refused and the pads
        // reach neither the game nor the lab.
        bool exiting;
    };

    static MatchPadState s_st;
    static ui::MenuState s_menu;
    static bool s_menuFresh = true; // the menu starts on its first row at the next open
    static bool s_prevRan = true;
    static bool s_fightersLogged = false;
    static bool s_vsReadOnly = false;
    static u32 s_systemCalls = 0;
    static u32 s_gameCalls = 0;
    static u32 s_frozenGameCalls = 0;
    static u32 s_advancedSteps = 0;

    static void capture(Raw* raw, const gfPadStatus* pads, bool resetWaits)
    {
        for (int i = 0; i < kPorts; i++)
        {
            bool conn = game::padConnectedInMatch(i, pads[i]);
            u8 type = static_cast<u8>(pads[i].m_controllerType);
            bool same = conn && raw[i].connected && type == raw[i].controllerType;
            if (resetWaits && !same)
            {
                s_st.releaseMask[i] = 0;
                s_st.stickWaitMask &= static_cast<u8>(~(1u << i));
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

    static bool anyDpadDir(const Raw* raw, const game::MatchState& m, u32 bit, bool held)
    {
        bool allowMinus = !minusBoundToFrameAdvance(m);
        for (int i = 0; i < kPorts; i++)
        {
            u32 w = held ? raw[i].held : raw[i].pressed;
            if ((w & bit) == 0)
            {
                continue;
            }
            if (raw[i].controllerType != gfPadType::WIIMOTE
                || (allowMinus && (raw[i].held & game::kPadWiiMinus) != 0))
            {
                return true;
            }
        }
        return false;
    }

    static bool anyDpadDown(const Raw* raw, const game::MatchState& m, bool held)
    {
        return anyDpadDir(raw, m, kDpadDown, held);
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

    static bool shieldDropShortcutActive()
    {
        return main::extraLoaded() && ui::pages::scenarioMenuState()->preset == ui::pages::kPresetShieldDrop;
    }

    static bool comboShortcutActive()
    {
        const main::ComboTrainerSettings& ct = settings().comboTrainer;
        return main::extraLoaded() && ct.enabled != 0 && ct.recoveryTrainer == 0;
    }

    static bool anyLHeld(const Raw* raw)
    {
        for (int i = 0; i < kPorts; i++)
        {
            if (raw[i].connected)
            {
                u32 lBit = game::inMatchPad(raw[i].controllerType).prevCat;
                if (lBit != 0 && (raw[i].held & lBit) != 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool comboLHeld(const game::MatchState& m)
    {
        return m.running && m.training && !m.paused && !s_st.open && comboShortcutActive() && anyLHeld(s_st.sys);
    }

    bool comboLHeldOnPads(const gfPadStatus* pads)
    {
        game::MatchState m = game::matchState();
        if (!m.running || !m.training || m.paused || s_st.open || !comboShortcutActive() || pads == NULL)
        {
            return false;
        }
        for (int i = 0; i < kPorts; i++)
        {
            if (!game::padConnectedInMatch(i, pads[i]))
            {
                continue;
            }
            u32 lBit = game::inMatchPad(static_cast<u32>(pads[i].m_controllerType)).prevCat;
            if (lBit != 0 && (pads[i].m_buttonsCurrentFrame.bits & lBit) != 0)
            {
                return true;
            }
        }
        return false;
    }

    // D-pad Up/Down bound to something other than their taunt (Up None: to
    // nothing; Shield Drop preset: live platform cycle; Combo Lab: L+D-Pad %
    // and D-Pad Down anchor reposition), hidden from the game.
    static u32 boundDpad(const game::MatchState& m)
    {
        if (!m.running || !m.training || m.paused)
        {
            return 0;
        }
        if (comboLHeld(m))
        {
            return kDpadAll;
        }
        const main::ControlSettings& c = settings().controls;
        u32 up = (c.dpadUp == main::kUpNone || shieldDropShortcutActive()) ? kDpadUp : 0;
        u32 down = (c.dpadDown != main::kDownNone || comboShortcutActive()) ? kDpadDown : 0;
        return up | down;
    }

    static void hidePort(gfPadStatus& pad, u32 port, const game::MatchState& m, bool fighterPad)
    {
        if (s_st.open)
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
        if (fighterPad && type == gfPadType::WIIMOTE && m.running && m.training && !m.paused)
        {
            dpad |= savestate::boundBits();
        }
        if (dpad != 0
            && (type != gfPadType::WIIMOTE
                || ((pad.m_buttonsCurrentFrame.bits & game::kPadWiiMinus) != 0 && !(mask & game::kPadWiiMinus))))
        {
            mask |= dpad;
        }
        if (s_st.startState == kStartWait)
        {
            mask |= game::inMatchPad(type).start;
        }
        hide(pad, mask | s_st.releaseMask[port], (s_st.stickWaitMask & (1u << port)) != 0);
    }

    static void applyHiding(gfPadStatus* pads, const game::MatchState& m)
    {
        for (u32 i = 0; i < kPorts; i++)
        {
            hidePort(pads[i], i, m, false);
        }
    }

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
        for (int i = 0; i < kPorts; i++)
        {
            s_st.releaseMask[i] &= s_st.sys[i].held;
            u8 bit = static_cast<u8>(1u << i);
            if ((s_st.stickWaitMask & bit) && (!s_st.sys[i].connected || stickNeutral(pads[i])))
            {
                s_st.stickWaitMask &= static_cast<u8>(~bit);
            }
        }
    }

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

    static void openMenu(int port, bool start)
    {
        s_st.open = true;
        g_resetLabel[0] = 0;
        s_st.diPickActive = false;
        strcpy(g_diPickLabel, "Tilt + A");
        record::clearDeleteConfirm();
        s_st.openStart = start;
        s_st.owner = port;
        s_st.nextCatHold = 0;
        s_st.nextCatUsedFast = false;
        if (s_menuFresh)
        {
            ui::menuReset(ui::labMenu(), s_menu);
            s_menuFresh = false;
        }
        s_menu.openStart = start;
        s_menu.controllerType = s_st.sys[port].controllerType;
        memset(s_st.repeat, 0, sizeof(s_st.repeat));
        int shown = ui::currentPage(s_menu);
        TM_LOG("lab: menu opened by port %d (%s) on page %d (%s) row %d, depth %d, main row %d, rail %d\n", port + 1,
               start ? "Start" : "Z in pause", shown, ui::labMenu().pages[shown]->title, s_menu.cursor[shown],
               s_menu.depth, s_menu.railFocus ? s_menu.railCursor : s_menu.category, s_menu.railFocus ? 1 : 0);
        ui::menuSound(game::kSoundOpen);
    }

    // Also called from the extra module (fighter_swap): keep exported.
#pragma force_active on
    bool menuOpen()
    {
        return s_st.open;
    }

    void closeMenu()
    {
        if (!s_st.open)
        {
            return;
        }
        s_st.open = false;
        g_resetLabel[0] = 0;
        if (s_st.diPickActive)
        {
            settings().cpuTables.customDiX = s_st.diPickSavedX;
            settings().cpuTables.customDiY = s_st.diPickSavedY;
            s_st.diPickActive = false;
        }
        strcpy(g_diPickLabel, "Tilt + A");
        record::clearDeleteConfirm();
        // Whatever is still held when the menu closes stays hidden until it
        // is let go, so it neither moves Training's menu nor the fighters.
        for (int i = 0; i < kPorts; i++)
        {
            s_st.releaseMask[i] = s_st.sys[i].held;
        }
        s_st.stickWaitMask = 0xFF;
        TM_LOG("lab: menu closed\n");
        storage::settingsMenuClosed();
    }
#pragma force_active reset

    // Closed by the player (its button, or B on the top page), with the sound;
    // Exit sounds as its function row, a menu the game closes is silent.
    static void closeByButton()
    {
        ui::menuSound(game::kSoundClose);
        closeMenu();
    }

    static void runMenu()
    {
        if (s_st.openStart && firstPadPort(s_st.sys, 0) >= 0)
        {
            TM_LOG("lab: menu closed with Start\n");
            closeByButton();
            return;
        }
        if (static_cast<u32>(s_st.owner) >= kPorts || !s_st.sys[s_st.owner].connected)
        {
            // Owner disconnected while open: let any connected port close in
            // Z-in-Pause mode (pauseLab/start/B) or take ownership on a fresh
            // press, without auto-closing on a transient 1-frame hot-swap.
            for (int i = 0; i < kPorts; i++)
            {
                u32 pressed = s_st.sys[i].pressed;
                if (pressed == 0)
                {
                    continue;
                }
                const game::InMatchPad& m = game::inMatchPad(s_st.sys[i].controllerType);
                if (!s_st.openStart && (pressed & (m.pauseLab | m.start | kButtonB)) != 0)
                {
                    closeByButton();
                }
                else
                {
                    s_st.owner = static_cast<s8>(i);
                    s_st.nextCatHold = 0;
                    s_st.nextCatUsedFast = false;
                }
                return;
            }
            return;
        }
        const Raw& in = s_st.sys[s_st.owner];
        s_menu.controllerType = in.controllerType;
        const game::InMatchPad& map = game::inMatchPad(in.controllerType);
        // Z (Classic ZL/ZR, sideways -) jumps to Training's pause menu either
        // way: from over it, by closing; opened with Start, via openTrainingMenu.
        if ((in.pressed & map.pauseLab) != 0)
        {
            ui::menuSound(game::kSoundClose);
            openTrainingMenu();
            return;
        }
        // Live 2D stick capture for CPU > DI & SDI > Pick DI Angle.
        if (s_st.diPickActive)
        {
            if (ui::currentPage(s_menu) != ui::pages::P_CPU_DI
                || settings().cpu.options.tdi != cpu::kTdiCustom)
            {
                s_st.diPickActive = false;
                strcpy(g_diPickLabel, "Tilt + A");
            }
            else
            {
                float sx = 0.0f, sy = 0.0f;
                game::mainStick(in.stickX, in.stickY, &sx, &sy);
                if (sx * sx + sy * sy < 0.15f * 0.15f)
                {
                    sx = sy = 0.0f;
                }
                int ix = static_cast<int>(sx >= 0.0f ? (sx * 100.0f + 0.5f) : (sx * 100.0f - 0.5f));
                int iy = static_cast<int>(sy >= 0.0f ? (sy * 100.0f + 0.5f) : (sy * 100.0f - 0.5f));
                if (ix < -100) ix = -100;
                else if (ix > 100) ix = 100;
                if (iy < -100) iy = -100;
                else if (iy > 100) iy = 100;
                settings().cpuTables.customDiX = static_cast<s8>(ix);
                settings().cpuTables.customDiY = static_cast<s8>(iy);
                if ((in.pressed & kButtonA) != 0)
                {
                    s_st.diPickActive = false;
                    strcpy(g_diPickLabel, "Tilt + A");
                    ui::menuSound(game::kSoundConfirm);
                }
                else if ((in.pressed & kButtonB) != 0)
                {
                    settings().cpuTables.customDiX = s_st.diPickSavedX;
                    settings().cpuTables.customDiY = s_st.diPickSavedY;
                    s_st.diPickActive = false;
                    strcpy(g_diPickLabel, "Tilt + A");
                    ui::menuSound(game::kSoundMove);
                }
                return;
            }
        }
        ui::MenuInput mi;
        bool up = (in.held & kDpadUp) || in.stickY > kStickDeadZone;
        bool down = (in.held & kDpadDown) || in.stickY < -kStickDeadZone;
        bool left = (in.held & kDpadLeft) || in.stickX < -kStickDeadZone;
        bool right = (in.held & kDpadRight) || in.stickX > kStickDeadZone;
        bool anyDir = up || down || left || right;
        bool fast = (in.held & map.fastRepeat) != 0
                    || (game::hasAnalogTriggers(in.controllerType) && in.rAnalog >= kTriggerHeld);
        mi.up = repeat(s_st.repeat[0], up && !down, fast);
        mi.down = repeat(s_st.repeat[1], down && !up, fast);
        mi.left = repeat(s_st.repeat[2], left && !right, fast);
        mi.right = repeat(s_st.repeat[3], right && !left, fast);
        mi.a = (in.pressed & kButtonA) != 0;
        mi.b = (in.pressed & kButtonB) != 0;
        mi.prevCat = (in.pressed & map.prevCat) != 0;
        mi.nextCat = false;
        if (map.nextCat != 0)
        {
            if ((map.nextCat & map.fastRepeat) == 0)
            {
                mi.nextCat = (in.pressed & map.nextCat) != 0;
                s_st.nextCatHold = 0;
                s_st.nextCatUsedFast = false;
            }
            else
            {
                bool nextHeld = (in.held & map.nextCat) != 0;
                if (nextHeld)
                {
                    if (s_st.nextCatHold == 0)
                    {
                        if ((in.pressed & map.nextCat) != 0)
                        {
                            s_st.nextCatHold = 1;
                            s_st.nextCatUsedFast = anyDir || mi.prevCat;
                        }
                    }
                    else
                    {
                        if (anyDir || mi.prevCat)
                        {
                            s_st.nextCatUsedFast = true;
                        }
                        if (s_st.nextCatHold < 255)
                        {
                            s_st.nextCatHold++;
                        }
                    }
                }
                else if (s_st.nextCatHold > 0)
                {
                    if (!s_st.nextCatUsedFast && s_st.nextCatHold <= ui::style::kRepeatStart)
                    {
                        mi.nextCat = true;
                    }
                    s_st.nextCatHold = 0;
                    s_st.nextCatUsedFast = false;
                }
            }
        }
        else
        {
            s_st.nextCatHold = 0;
            s_st.nextCatUsedFast = false;
        }
        // B on the top page (Tabs) or left rail (Rail) closes the menu when
        // opened from Training's pause; in Start mode, Start closes.
        int prevPage = ui::currentPage(s_menu);
        u8 prevCur = s_menu.cursor[prevPage];
        bool prevRail = s_menu.railFocus;
        u8 prevRailCur = s_menu.railCursor;
        if (!ui::menuStep(ui::labMenu(), s_menu, mi) && !s_st.openStart)
        {
            closeByButton();
            return;
        }
        int nextPage = ui::currentPage(s_menu);
        if (nextPage != prevPage || s_menu.cursor[nextPage] != prevCur || s_menu.railFocus != prevRail
            || s_menu.railCursor != prevRailCur)
        {
            g_resetLabel[0] = 0;
            record::clearDeleteConfirm();
        }
    }

    static void onSystemPads(gfPadStatus* pads)
    {
        s_systemCalls++;
        capture(s_st.sys, pads, true);
        display::systemPads(pads);
        game::MatchState m = game::matchState();
        bool inPause = m.running && m.training && m.paused;
        bool inMatch = m.running && m.training && !m.paused;
        if (s_st.exiting)
        {
            // The match is ending: no menu opens (Training's pause byte stays
            // set when the end starts from its pause, as after Finish, so Z
            // would find "in pause") and no press reaches the game.
            hideAll(pads);
            return;
        }
        if (s_st.open && !(s_st.openStart ? inMatch : inPause))
        {
            closeMenu();
        }
        if (s_st.open)
        {
            runMenu();
        }
        else if (inPause)
        {
            // Z on Finish is P+'s "reset the match" shortcut: left alone.
            if (game::trainingMenuCursor() != game::kTrainingMenuFinish)
            {
                int p = firstPadPort(s_st.sys, 2);
                if (p >= 0)
                {
                    openMenu(p, false);
                }
            }
        }
        else if (inMatch)
        {
            int p = firstPadPort(s_st.sys, 0);
            if (p >= 0)
            {
                if (startOpensMenu())
                {
                    if (s_st.startState == kStartIdle)
                    {
                        openMenu(p, true);
                    }
                }
                else
                {
                    s_st.pausePort = static_cast<s8>(p);
                    if (frameAdvanceOn(m) && s_st.startState == kStartIdle)
                    {
                        s_st.startState = kStartWait;
                        s_st.startPort = static_cast<u8>(p);
                        s_st.startTimer = kStartWindow;
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
            int delta = 0;
            if (anyDpadDir(s_st.sys, m, kDpadRight, false))
            {
                delta = 1;
            }
            else if (anyDpadDir(s_st.sys, m, kDpadLeft, false))
            {
                delta = -1;
            }
            else if (anyDpadDir(s_st.sys, m, kDpadUp, false))
            {
                delta = 10;
            }
            else if (anyDpadDir(s_st.sys, m, kDpadDown, false))
            {
                delta = -10;
            }
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
        // DPad Down = Frame Advance: a press turns it on or off.
        if (!comboL && (boundDpad(m) & kDpadDown) && !s_st.open
            && settings().controls.dpadDown == main::kDownFrameAdvance && anyDpadDown(s_st.sys, m, false))
        {
            settings().general.frameAdvance ^= 1;
            frameAdvanceChanged();
        }
        else if (!comboL && !s_st.open && !m.paused && m.running && m.training && comboShortcutActive()
                 && settings().controls.dpadDown != main::kDownFrameAdvance && anyDpadDown(s_st.sys, m, false))
        {
            main::extraComboReposition();
        }
        // Scenario-scoped DPad Up while Shield Drop is active: cycle platforms.
        if (!comboL && !s_st.open && !m.paused && m.running && m.training && shieldDropShortcutActive()
            && anyDpadDir(s_st.sys, m, kDpadUp, false))
        {
            main::extraScenarioCyclePlatform();
        }
        // Frame Decrement: in frame advance a press steps back one frame; held
        // past the hold delay, repeats once per pass after the last arrives.
        int dec = scanFa(s_st.sys, true);
        bool hold = frameAdvanceOn(m) && !m.paused && !s_st.open && s_st.startState == kStartIdle && s_st.catchUp == 0;
        s_st.decrementHold = hold && (dec & 1) ? static_cast<u16>(s_st.decrementHold + 1) : 0;
        if (hold && ((dec & 2) || s_st.decrementHold > settings().controls.advanceDelay))
        {
            record::rewindRequest((dec & 2) != 0);
        }
        applyHiding(pads, m);
        if (s_st.startState == kStartInject && !s_st.startInjectedSystem && !m.paused)
        {
            injectStart(pads[s_st.startPort]);
            s_st.startInjectedSystem = true;
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
            if (main::lockSetting(side) && game::damage(refs[i]) != static_cast<float>(percent))
            {
                game::setDamage(refs[i], percent);
            }
        }
    }

    static bool s_playerShieldHit[game::kMaxFighters][2];

    static void maintainPlayerShields(bool stepping)
    {
        if (!stepping)
        {
            return;
        }
        u8 mode = settings().general.playerInfiniteShields;
        if (mode == cpu::kShieldsOff)
        {
            memset(s_playerShieldHit, 0, sizeof(s_playerShieldHit));
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
                if (stepPlayerShield(mode, status, s_playerShieldHit[p][sub]))
                {
                    cpu::setShieldHealth(f, static_cast<float>(cpu::kShieldFull));
                }
            }
        }
    }

    static void logFighters(const game::MatchState& m)
    {
        if (s_fightersLogged || !m.running)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        if (n == 0)
        {
            return;
        }
        s_fightersLogged = true;
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
        u32 type = (validPort && s_st.sys[port].connected)
                       ? static_cast<u32>(s_st.sys[port].controllerType)
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
        if (bound == 0 || who.cpu || who.sub)
        {
            return;
        }
        u32* buttons = reinterpret_cast<u32*>(reinterpret_cast<u32>(call->input) + 0xC);
        if (type == gfPadType::WIIMOTE)
        {
            if (validPort && dpadAllowed(s_st.sys[port], m))
            {
                *buttons &= ~kAppealAll;
            }
            return;
        }
        *buttons &= ~(((bound & kDpadUp) ? kTauntUp : 0) | ((bound & kDpadDown) ? kTauntDown : 0));
    }

    static void onGamePads(gfPadStatus* pads)
    {
        s_gameCalls++;
        if (game::frozen())
        {
            s_frozenGameCalls++;
        }
        capture(s_st.game, pads, false);
        display::capturePads(pads);
        game::MatchState m = game::matchState();
        bool inject = false;
        if (s_st.startState != kStartIdle && (m.paused || --s_st.startTimer <= 0))
        {
            s_st.startState = kStartIdle;
        }
        else if (s_st.startState == kStartWait && game::otherStopBits() == 0)
        {
            s_st.startState = kStartInject;
            s_st.startInjectedSystem = false;
        }
        else if (s_st.startState == kStartInject && s_st.startInjectedSystem)
        {
            inject = true;
            s_st.startState = kStartAwait;
        }
        bool want = frameAdvanceOn(m) && !m.paused && !s_st.open && s_st.startState == kStartIdle && !s_st.exiting;
        // The Start-opened lab menu holds the whole game. P+'s
        // pad read runs steps while Start is held despite our stop bit
        // (freeze.cpp), so the gameplay and camera tasks are paused instead.
        bool menuHold = s_st.open && s_st.openStart && m.running && !m.paused;
        bool startHeld = firstPadPort(s_st.game, 1) >= 0;
        // Frame advance stops every task, HUD and camera
        // included, which would freeze Training's closing menu and close-up
        // camera: for kCatchUp steps after its pause only the gameplay
        // categories are paused.
        if (s_st.wasPaused && !m.paused && frameAdvanceOn(m))
        {
            s_st.catchUp = kCatchUp;
        }
        s_st.wasPaused = m.paused;
        if (!want)
        {
            s_st.catchUp = 0;
        }
        bool run = true;
        bool gameplayPaused = false;
        // Asked before every step: a rewind holds the game for its load,
        // then runs it to the frame and holds it there.
        int rewind = record::rewindStep();
        // No menu sounds while the lab loads and replays frames on its own.
        ui::g_menuQuiet = rewind != record::kRewindIdle;
        if (s_st.exiting)
        {
            // Training's operator must step to end the match (match.cpp), so
            // nothing is held: without Training's pause (Finish keeps it until
            // the scene ends) the fighters run on, inputless, for the end's
            // steps (a wait of 15, then the scene's end). Pausing the gameplay
            // tasks stops the end (observed, Mario vs Fox on Final Destination:
            // the operator reached 392, the scene never exited).
            s_st.hold = 0;
        }
        else if (menuHold)
        {
            s_st.hold = 0;
            run = false;
            gameplayPaused = startHeld;
        }
        else if (rewind != record::kRewindIdle)
        {
            s_st.hold = 0;
            run = rewind == record::kRewindRun;
        }
        else if (want && (s_st.catchUp > 0 || (startOpensMenu() && startHeld)))
        {
            s_st.catchUp -= s_st.catchUp > 0 ? 1 : 0;
            s_st.hold = 0;
            run = false;
            gameplayPaused = true;
        }
        else if (want)
        {
            // A press runs one step, holding past the hold delay runs the game:
            // adapted from ProjectPunch's frame_advance.cpp (MIT; see
            // THIRD_PARTY_NOTICES.md).
            int adv = scanFa(s_st.game, false);
            s_st.hold = (adv & 1) ? static_cast<u16>(s_st.hold + 1) : 0;
            run = (adv & 2) != 0 || s_st.hold > settings().controls.advanceDelay;
            if (run)
            {
                s_advancedSteps++;
            }
        }
        else
        {
            s_st.hold = 0;
        }
        // The step before's frame went to its render callback (module.cpp
        // onRender) unless no picture followed (game/tracker.h); the coming
        // step's overlays, display and pads are set after. Off while Exit
        // ends the match.
        bool live = m.running && !m.paused && !s_st.exiting;
        {
            TM_PERF_SCOPE(kTrackerStep);
            game::trackerStep(live);
        }
        applyOverlays(live);
        display::step(m, s_st.open);
        if (main::extraInfoWanted())
        {
            main::extraInfoStep(s_prevRan);
        }
        // A game held by ProjectPunch's pause or frame advance (the same
        // stop bit) runs no step: the tracker, match timer and frame counts
        // stop. The steps of a match ending after Exit are not handed over.
        s_prevRan = run && m.running && !m.paused && !game::heldByOthers() && !s_st.exiting;
        game::trackerStepRuns(s_prevRan);
        game::setGameplayPaused(gameplayPaused);
        game::setCameraPaused(menuHold && startHeld);
        game::setFrozen(!run && !gameplayPaused);
        lockPercents(m);
        maintainPlayerShields(s_prevRan);
        logFighters(m);
        applyHiding(pads, m);
        if (s_st.exiting)
        {
            hideAll(pads);
        }
        else if (inject)
        {
            injectStart(pads[s_st.startPort]);
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
        if (!s_vsReadOnly && !s_st.exiting && ui::settingsModalOpen())
        {
            bool confirmed = false;
            if (phase == iface::kPadsSystem)
            {
                capture(s_st.sys, pads, true);
                int active = -1;
                for (int i = 0; i < kPorts; i++)
                {
                    if (!s_st.sys[i].connected)
                    {
                        continue;
                    }
                    bool busy = s_st.sys[i].held != 0 || s_st.sys[i].stickX <= -kStickDeadZone
                                || s_st.sys[i].stickX >= kStickDeadZone || s_st.sys[i].stickY <= -kStickDeadZone
                                || s_st.sys[i].stickY >= kStickDeadZone;
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
                    const Raw& r = s_st.sys[active];
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
                for (int i = 0; i < kPorts; i++)
                {
                    s_st.releaseMask[i] = s_st.sys[i].held;
                }
                s_st.stickWaitMask = 0xFF;
                game::setFrozen(false);
                onSettingsUpgraded();
            }
            return;
        }
        if (!s_vsReadOnly && !s_st.exiting && !s_st.open && main::extraDrill(phase, pads) != 0u)
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
    char g_diPickLabel[16] = "Tilt + A";

    void toggleDiStickPicker()
    {
        if (!s_st.diPickActive)
        {
            s_st.diPickSavedX = settings().cpuTables.customDiX;
            s_st.diPickSavedY = settings().cpuTables.customDiY;
            s_st.diPickActive = true;
            strcpy(g_diPickLabel, "Pinning");
        }
        else
        {
            s_st.diPickActive = false;
            strcpy(g_diPickLabel, "Tilt + A");
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
        main::extraScenarioSettingsReset();
        memset(s_playerShieldHit, 0, sizeof(s_playerShieldHit));
        TM_LOG("lab: settings reset to the defaults\n");
    }

    void openTrainingMenu()
    {
        bool start = s_st.openStart;
        int port = s_st.owner;
        closeMenu();
        if (start)
        {
            // Hand the game one Start press so Training's pause opens.
            s_st.startState = kStartInject;
            s_st.startPort = port;
            s_st.startTimer = kStartWindow;
            s_st.startInjectedSystem = false;
            s_st.pausePort = port;
        }
    }

    // Ends the match as Training's Finish does; what a match end must undo
    // runs at the module's detach: docs/internals.md, "Ending the match from
    // the lab".
    void exitMatch()
    {
        closeMenu();
        game::EndResult how = game::endTrainingMatch();
        s_st.exiting = how != game::kEndRefused;
        if (s_st.exiting)
        {
            s_st.startState = kStartIdle;
            s_st.catchUp = 0;
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
                float angle = static_cast<int>(t.angle * 10.0f) / 10.0f;
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
                gfx::Rgba c = t.flag ? osd::kGreen : osd::kRed;
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
            break;
        case game::kTechActOos:
            if (o.actOos)
            {
                osd::post(t.player, osd::kIdActOos, first ? osd::kGreen : osd::kWhite, "OoS Action\nFrame %d",
                          t.frame);
            }
            break;
        case game::kTechActOoLag:
            if (o.actOoLag)
            {
                m = osd::post(t.player, osd::kIdActOoLag, osd::kWhite, "%s\nFrame %d",
                              t.flag ? "Autocancel" : "Post-Lag", t.frame);
                osd::lineColor(m, 1, first ? osd::kGreen : osd::kRed);
            }
            break;
        case game::kTechJumpCancel:
            if (o.jumpCancel)
            {
                osd::post(t.player, osd::kIdJumpCancel, first ? osd::kGreen : osd::kRed, "Jump Cancel\nFrame %d",
                          t.frame);
            }
            break;
        case game::kTechFastfall:
            if (o.fastfall)
            {
                m = osd::post(t.player, osd::kIdFastfall, osd::kWhite, "Fastfall\nFrame %d", t.frame);
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
                osd::post(t.player, osd::kIdItemThrow, osd::kWhite,
                          t.flag ? "Air Item Throw\nFrame %d" : "Glide Toss\nFrame %d", t.frame);
            }
            break;
        case game::kTechBoostGrab:
            if (o.boostGrab)
            {
                osd::post(t.player, osd::kIdBoostGrab, osd::kWhite, t.flag ? "DACUS\nFrame %d" : "Boost Grab\nFrame %d",
                          t.frame);
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
                osd::post(t.player, osd::kIdActOoHitstun, first ? osd::kGreen : osd::kWhite, "Out of Hitstun\nFrame %d",
                          t.frame);
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
        if (!s_vsReadOnly)
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
        int p = s_st.pausePort;
        if (static_cast<u32>(p) >= kPorts || !s_st.sys[p].connected)
        {
            p = game::playerPort(0);
            if (static_cast<u32>(p) >= kPorts || !s_st.sys[p].connected)
            {
                for (p = 0; p < kPorts && !s_st.sys[p].connected; p++)
                {
                }
            }
        }
        return game::pauseHint(static_cast<u32>(p) < kPorts ? s_st.sys[p].controllerType : 0u);
    }

    // Info panels and OSD messages hide while the lab menu or pause is open.
    void onRender(u32 call, const game::MatchState& m)
    {
        bool ro = s_vsReadOnly;
        bool modal = !ro && ui::settingsModalOpen();
        bool info = m.running && !s_st.open && !modal && !m.paused && main::extraInfoWanted();
        bool messages = m.running && !m.paused && !s_st.open && !modal && osd::visible();
        // Z opens the lab menu from Training's pause in both modes; the hint
        // shows only where that is the way in.
        bool hint = !ro && m.running && m.training && m.paused && !s_st.open && !modal && !startOpensMenu();
        bool hud = ro || display::hudShown();
        setLayout(hud);
        bool drillHud = !ro && m.running && !m.paused && !s_st.open && !modal && (main::extraDrill(2, NULL) & 1u) != 0u;
        bool ledge = m.running && !m.paused && !s_st.open && !modal && !drillHud && ui::ledgedashHudVisible();
        // Training leaves Brawl's own timer off: it is turned on here instead
        // (ui/match_hud.cpp), hidden while the Ledgedash HUD or Skill Drill HUD is shown.
        bool timer = !ro && m.running && hud && !drillHud && !ui::ledgedashHudVisible() && (!m.paused || s_st.open);
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
                    if (text != s_st.lastHint)
                    {
                        gfx::forgetWidths();
                        s_st.lastHint = text;
                    }
                    gfx::textBox(kSlotHint, ui::layout::kHintX, ui::layout::kHintY, text, kYellow, kBox, 0.5f, false);
                }
                if (!ro)
                {
                    display::render2d(m, s_st.open);
                    if (!m.paused && !s_st.open)
                    {
                        main::extraInputViewerDraw(m, s_st.open);
                        main::extraDiCompassDraw(m, s_st.open);
                    }
                    if (s_st.open)
                    {
                        TM_PERF_SCOPE(kMenuDraw);
                        ui::menuDraw(ui::labMenu(), s_menu);
                    }
                    if (modal)
                    {
                        ui::settingsModalDraw();
                    }
                }
                else if (!m.paused && !s_st.open)
                {
                    main::extraPsaInspectorDraw(m, s_st.open);
                }
                if (hud && !ro && !(s_st.open && storage::noticeActive()))
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
        return s_st.open || s_st.exiting || s_st.startState != kStartIdle || s_st.catchUp > 0
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
        memset(s_playerShieldHit, 0, sizeof(s_playerShieldHit));
        s_st.hold = 0;
    }
#pragma force_active reset

    // Before a save or load copies memory the colour overlays and hiding are
    // put back (a slot never holds them); the next step applies them again.
    void savestateCopying()
    {
        game::overlaysClear();
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
        main::extraScenarioSavestateLoaded();
        main::extraCharacterRng();
        main::extraStageOptionsSavestateLoaded();
        game::reapplyFrozen();
        display::savestateLoaded();
        memset(s_playerShieldHit, 0, sizeof(s_playerShieldHit));
        s_st.hold = 0;
    }

    void attach()
    {
        memset(&s_st, 0, sizeof(s_st));
        s_st.owner = -1;
        s_st.pausePort = -1;
        s_vsReadOnly = !game::trainingGameMode();
        memset(s_playerShieldHit, 0, sizeof(s_playerShieldHit));
        game::trackerAttach();
        game::advantageReset(onAdvantage);
        game::techniquesAttach();
        game::techniquesReset(onTechnique);
        game::setFrameListener(onTrackerFrame);
        gfx::textAttach();
        osd::attach();
        osdSettingsChanged();
        ui::settingsModalAttach();
        s_menuFresh = true;
        if (!s_vsReadOnly)
        {
            display::attach();
        }
        bool wide = game::widescreen();
        gfx::setWidescreen(wide);
        if (!s_vsReadOnly)
        {
            TM_LOG("lab: screen %s; frame advance %s, overlay %s\n", wide ? "16:9" : "4:3",
                   settings().general.frameAdvance ? "on" : "off", settings().display.debugOverlay ? "on" : "off");
        }
    }

    void detach()
    {
        if (s_vsReadOnly)
        {
            osd::detach();
            gfx::textDetach();
            game::techniquesDetach();
            game::trackerDetach();
            s_vsReadOnly = false;
            return;
        }
        display::detach();
        osd::detach();
        gfx::textDetach();
        game::overlaysClear();
        game::techniquesDetach();
        game::trackerDetach();
        game::setFrozen(false);
        game::setGameplayPaused(false);
        game::setCameraPaused(false);
        TM_LOG("lab: pad callbacks: system %u, game %u (%u while stopped), frame-advance steps %u\n", s_systemCalls,
               s_gameCalls, s_frozenGameCalls, s_advancedSteps);
    }
}
}
