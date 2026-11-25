#include "main/display/display.h"

#include <gf/gf_application.h>
#include <gf/gf_pad_status.h>

#include "main/display/debug_draw.h"
#include "main/display/input_display.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/overlays.h"
#include "main/game/pad_inmatch.h"
#include "main/lab.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "shared/perf.h"

// How each option changes the game (scene layers, Move Staling, Game Speed,
// Camera; RSBE01 rev 2, P+ 3.2.1): docs/internals.md, "Display options".
// Every change is put back when its option goes off, at detach, and where
// Training's pause menu needs it.
namespace tm {
namespace display {
    using main::settings;

    enum {
        kSceneRootPointer = 0x8059FF80,
        kSetLayer = 0x8000D234,
        // What kSetLayer writes: the root's byte 0x399 + 0x18 per layer, bit 0x80 on.
        kLayerByte = 0x399,
        kLayerStep = 0x18,
        kLayerOn = 0x80,
        kClearPattern = 0x808E01E4,
        kStalePattern = 0xCC8,
        kFrameRate = 0xF8,
        kCameraGetInstance = 0x8009BE74,
        kCameraChangeInput = 0x8009C69C,
        kSetCameraType = 0x8010535C,
        kCameraKind = 0x40,
        kKindFixed = 0xC,
        kIfManager = 0x805A02D0,
        kIfTraining = 0x78,
        kTrainingCamera = 0x2C,
        kLayerStage = 0,
        kLayerEffect = 4,
        kLayerEffectFighter = 5,
        kLayerHud = 8,
        kLayers = 9,
        kNormalRate = 60,
        kPads = iface::kPadCount
    };

    static const u32 kSetLayerWords[] = {0x1C040018, 0x7C630214, 0x88030399, 0x50A03E30, 0x98030399, 0x4E800020};
    static const u32 kClearPatternWords[] = {0x81830000, 0x818C0028, 0x7D8903A6, 0x4E800420};
    static const u32 kGetInstanceWords[] = {0x806DBE58, 0x4E800020};
    static const u32 kChangeInputWords[] = {0x9421FFD0, 0x7C0802A6, 0x90010034, 0x93E1002C,
                                            0x93C10028, 0x7C9E2378, 0x93A10024, 0x7C7D1B78};
    static const u32 kSetCameraTypeWords[] = {0x9421FFB0, 0x7C0802A6, 0x90010054, 0x93E1004C,
                                              0x3FE08041, 0x3BFF9208, 0x93C10048, 0x93A10044};
    static const u16 kRates[kSpeedCount] = {60, 50, 40, 30, 15};

    typedef void (*SetLayerFn)(void* root, int layer, int on);
    typedef void (*ClearPatternFn)(void* pattern);
    typedef void* (*GetInstanceFn)();
    typedef void (*ChangeInputFn)(void* controller, int kind);
    typedef void (*SetCameraTypeFn)(void* menu, int type);

    static bool s_layersOk = false;
    static bool s_staleOk = false;
    static bool s_cameraOk = false;
    static bool s_hidden[kLayers];
    static bool s_rateOwned = false;
    static u16 s_rateSaved = 60;
    static u16 s_rateWritten = 0;
    static u8 s_camera = kCameraNormal; // the mode last applied
    static u32 s_heldBefore[kPads]; // system pads' buttons on the last pass, ports 1-8
    static u32 s_pending[kPads];    // presses no game step has seen yet

    static bool codeIs(u32 address, const u32* words, int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (reinterpret_cast<const u32*>(address)[i] != words[i])
            {
                return false;
            }
        }
        return true;
    }

    // ---- layers ---------------------------------------------------------------

    static void setLayer(int layer, bool hidden)
    {
        void* root = reinterpret_cast<void*>(game::wordAt(reinterpret_cast<const void*>(kSceneRootPointer), 0));
        if (!s_layersOk || !game::isRam(root) || (!hidden && !s_hidden[layer]))
        {
            return;
        }
        reinterpret_cast<SetLayerFn>(kSetLayer)(root, layer, hidden ? 0 : 1);
        s_hidden[layer] = hidden;
    }

    void savestateLoaded()
    {
        const u8* root = reinterpret_cast<const u8*>(game::wordAt(reinterpret_cast<const void*>(kSceneRootPointer), 0));
        if (!s_layersOk || !game::isRam(root))
        {
            return;
        }
        // A state saved with a layer hidden keeps it hidden after the load,
        // while this module may hold it as shown: then the setting never
        // brought it back.
        for (int i = 0; i < kLayers; i++)
        {
            s_hidden[i] = (root[kLayerByte + kLayerStep * i] & kLayerOn) == 0;
        }
    }

    static void showAllLayers()
    {
        for (int i = 0; i < kLayers; i++)
        {
            setLayer(i, false);
        }
    }

    // Hide the HUD layer while the lab menu is open over a running match
    // (Start mode); in Training's pause menu it stays visible to hold that UI.
    static void applyLayers(const game::MatchState& m)
    {
        const main::DisplaySettings& d = settings().display;
        setLayer(kLayerHud, (!d.hud || lab::menuOpen()) && !m.paused);
        setLayer(kLayerStage, !d.stageModel);
        setLayer(kLayerEffect, !d.effects);
        setLayer(kLayerEffectFighter, !d.effects);
    }

    // ---- staling --------------------------------------------------------------

    static void clearStaling()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            const void* data = reinterpret_cast<const void*>(game::wordAt(refs[i].owner, 0));
            if (!game::isRam(data))
            {
                continue;
            }
            void* pattern = reinterpret_cast<void*>(reinterpret_cast<u32>(data) + kStalePattern);
            if (game::isRam(reinterpret_cast<const void*>(game::wordAt(pattern, 0))))
            {
                reinterpret_cast<ClearPatternFn>(kClearPattern)(pattern);
            }
        }
    }

    // ---- speed ----------------------------------------------------------------

    static u16* rateField()
    {
        gfApplication* app = g_gfApplication;
        return game::isRam(app) ? reinterpret_cast<u16*>(reinterpret_cast<u32>(app) + kFrameRate) : NULL;
    }

    static void applySpeed(bool want, int speed)
    {
        u16* rate = rateField();
        if (rate == NULL)
        {
            return;
        }
        if (want && speed > kSpeed1 && speed < kSpeedCount)
        {
            if (!s_rateOwned || *rate != s_rateWritten)
            {
                // First time, or Training changed it since: that is the
                // rate to put back.
                s_rateSaved = *rate;
            }
            s_rateOwned = true;
            if (*rate != kRates[speed])
            {
                TM_LOG("display: frame rate %d -> %d\n", *rate, kRates[speed]);
            }
            *rate = kRates[speed];
            s_rateWritten = kRates[speed];
        }
        else if (s_rateOwned)
        {
            if (*rate == s_rateWritten)
            {
                *rate = s_rateSaved;
            }
            TM_LOG("display: frame rate back to %d\n", *rate);
            s_rateOwned = false;
        }
    }

    // ---- camera ---------------------------------------------------------------

    static void* trainingMenu()
    {
        const void* manager = reinterpret_cast<const void*>(game::wordAt(reinterpret_cast<const void*>(kIfManager), 0));
        if (!game::isRam(manager))
        {
            return NULL;
        }
        void* menu = reinterpret_cast<void*>(game::wordAt(manager, kIfTraining));
        return game::isRam(menu) ? menu : NULL;
    }

    void* cameraController()
    {
        if (!s_cameraOk)
        {
            return NULL;
        }
        void* c = reinterpret_cast<GetInstanceFn>(kCameraGetInstance)();
        return game::isRam(c) ? c : NULL;
    }

    // Training's own camera (its option), as its menu would set it.
    static void trainingCamera(void* menu, bool zoom)
    {
        *reinterpret_cast<u32*>(reinterpret_cast<u32>(menu) + kTrainingCamera) = zoom ? 1 : 0;
        reinterpret_cast<SetCameraTypeFn>(kSetCameraType)(menu, zoom ? 2 : 0);
    }

    static void applyCamera(const game::MatchState& m)
    {
        void* menu = trainingMenu();
        void* controller = cameraController();
        if (controller == NULL || menu == NULL || !m.training)
        {
            return;
        }
        int want = settings().general.cameraMode;
        if (want >= kCameraModeCount)
        {
            want = kCameraNormal;
        }
        if (want != s_camera)
        {
            TM_LOG("display: camera mode %d -> %d\n", s_camera, want);
            if (s_camera == kCameraFixed && want == kCameraNormal)
            {
                trainingCamera(menu, game::wordAt(menu, kTrainingCamera) != 0);
            }
            else if (s_camera == kCameraZoom || want == kCameraZoom)
            {
                trainingCamera(menu, want == kCameraZoom);
            }
            s_camera = static_cast<u8>(want);
        }
        if (want == kCameraFixed && !m.paused && game::wordAt(controller, kCameraKind) != kKindFixed)
        {
            reinterpret_cast<ChangeInputFn>(kCameraChangeInput)(controller, kKindFixed);
        }
    }

    // ---- per frame ------------------------------------------------------------

    void attach()
    {
        s_layersOk = codeIs(kSetLayer, kSetLayerWords, 6);
        s_staleOk = codeIs(kClearPattern, kClearPatternWords, 4);
        s_cameraOk = codeIs(kCameraGetInstance, kGetInstanceWords, 2)
                     && codeIs(kCameraChangeInput, kChangeInputWords, 8)
                     && codeIs(kSetCameraType, kSetCameraTypeWords, 8);
        for (int i = 0; i < kLayers; i++)
        {
            s_hidden[i] = false;
        }
        for (int i = 0; i < kPads; i++)
        {
            s_heldBefore[i] = 0;
            s_pending[i] = 0;
        }
        s_rateOwned = false;
        s_camera = kCameraNormal;
        TM_LOG("display: layers %s, staling %s, camera %s\n", s_layersOk ? "ok" : "not as expected: off",
               s_staleOk ? "ok" : "not as expected: off", s_cameraOk ? "ok" : "not as expected: off");
        debugDrawCheck();
    }

    void detach()
    {
        showAllLayers();
        applySpeed(false, kSpeed1);
        // A Zoom set here goes back to Training's Normal; the next match sets
        // its camera up from that option.
        void* menu = trainingMenu();
        if (s_camera == kCameraZoom && menu != NULL)
        {
            *reinterpret_cast<u32*>(reinterpret_cast<u32>(menu) + kTrainingCamera) = 0;
        }
        s_camera = kCameraNormal;
    }

    void capturePads(const gfPadStatus* pads)
    {
        inputCapture(pads);
        // This step has seen the presses kept for it.
        for (int i = 0; i < kPads; i++)
        {
            s_pending[i] = 0;
        }
    }

    void systemPads(gfPadStatus* pads)
    {
        u16* rate = rateField();
        bool slow = rate != NULL && *rate < kNormalRate;
        for (int i = 0; i < kPads; i++)
        {
            if (!game::padConnectedInMatch(i, pads[i]))
            {
                s_pending[i] = 0;
                s_heldBefore[i] = 0;
                continue;
            }
            u32 held = pads[i].m_buttonsCurrentFrame.bits;
            // Kept even once released: a press shorter than the time between
            // two steps (up to 4 passes at 1/4) still reaches one.
            s_pending[i] = slow ? s_pending[i] | (held & ~s_heldBefore[i]) : 0;
            pads[i].m_buttonsPressedThisFrame.bits |= s_pending[i];
            pads[i].m_buttonsPressedThisFrame2.bits |= s_pending[i];
            s_heldBefore[i] = held;
        }
    }

    void step(const game::MatchState& m, bool menuOpen)
    {
        bool live = m.running && !m.paused;
        // Before the match runs (the start, from the scene's first frame) too,
        // so that HUD Off hides the damage HUD from its first picture.
        if (!m.running)
        {
            applyLayers(m);
        }
        if (live && !settings().general.moveStaling && s_staleOk)
        {
            clearStaling();
        }
        applySpeed(live && !menuOpen, settings().general.gameSpeed);
        main::extraDiHideDrawDi(m.running && settings().general.diDisplay != 0);
        if (m.running)
        {
            applyCamera(m);
        }
    }

    static bool shows(int who, bool cpu)
    {
        return (who & (cpu ? kWhoCpu : kWhoHuman)) != 0;
    }

    // Hitboxes and hurtboxes for `f`: Display Options > Hitboxes for its
    // side, or a colour overlay group set to Show Collision applying to it.
    static bool boxesFor(const Fighter* f, int who, bool cpu)
    {
        return shows(who, cpu) || game::overlayShowsCollision(f);
    }

    static bool anyCollisionOverlay(const game::FighterRef* refs, int n)
    {
        for (int i = 0; i < n; i++)
        {
            if (game::overlayShowsCollision(refs[i].fighter) || game::overlayShowsCollision(refs[i].partner))
            {
                return true;
            }
        }
        return false;
    }

    void render3d(const game::MatchState& m)
    {
        applyLayers(m);
        if (m.paused)
        {
            return;
        }
        const main::DisplaySettings& d = settings().display;
        bool di = settings().general.diDisplay != 0;
        bool heatmap = d.heatmap != kWhoNone && main::extraLoaded();
        bool grabViz = d.grabVisualizer != kWhoNone && main::extraLoaded();
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        if (d.hitboxes == kWhoNone && !d.envCollision && !di && !heatmap && !grabViz
            && !anyCollisionOverlay(refs, n))
        {
            return;
        }
        debugDrawBegin();
        if (d.envCollision)
        {
            drawStageCollision();
            for (int i = 0; i < n; i++)
            {
                drawEcb(refs[i].fighter);
                if (refs[i].partner != NULL)
                {
                    drawEcb(refs[i].partner);
                }
            }
        }
        for (int i = 0; i < n; i++)
        {
            bool cpu = cpuPlayer(refs[i]);
            if (boxesFor(refs[i].fighter, d.hitboxes, cpu))
            {
                drawBoxes(refs[i].fighter);
            }
            if (refs[i].partner != NULL && boxesFor(refs[i].partner, d.hitboxes, cpu))
            {
                drawBoxes(refs[i].partner);
            }
        }
        if (heatmap)
        {
            TM_PERF_SCOPE(kHeatmapDraw);
            main::extraHeatmapDraw(m);
        }
        if (grabViz)
        {
            TM_PERF_SCOPE(kGrabVizDraw);
            main::extraGrabVizDraw(m);
        }
        if (di)
        {
            main::extraDiDraw(m);
        }
        debugDrawEnd();
    }

    void render2d(const game::MatchState& m, bool menuOpen)
    {
        if (!m.paused && !menuOpen)
        {
            main::extraPsaInspectorDraw(m, menuOpen);
        }
    }

    bool hudShown()
    {
        return settings().display.hud != 0;
    }

    bool cpuPlayer(const game::FighterRef& ref)
    {
        return ref.cpu; // from the match setup (game::cpuPlayer)
    }

    u8 hiddenModels()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        u8 hidden = 0;
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p >= 0 && p < 8 && !shows(settings().display.models, cpuPlayer(refs[i])))
            {
                hidden |= static_cast<u8>(1 << p);
            }
        }
        return hidden;
    }
}
}
