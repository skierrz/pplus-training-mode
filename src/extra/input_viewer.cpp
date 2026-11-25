#include "extra/input_viewer.h"

#include <GX/GXTypes.h>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "extra/di_cstick_core.h"
#include "extra/input_viewer_core.h"
#include "extra/ui_assets.h"
#include "main/cpu/driver.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kDebugMagic = 0x544D4956u, // "TMIV"
            kMaxCards = 2,
            kAiInputMain = 0x0C,
            kAiButtons = 0x0C,
            kAiStickX = 0x10,
            kAiStickY = 0x14,
            kAiTrigL = 0x20,
            kAiTrigR = 0x24
        };

        struct InputViewerDebugBlock {
            u32 magic;             // 0x00 "TMIV"
            u32 cardsDrawn;        // 0x04
            u32 totalFramesDrawn;  // 0x08
            u32 lastButtons[2];    // 0x0C
            s8 lastStickX[2];      // 0x14
            s8 lastStickY[2];      // 0x16
            u8 lastLTrig[2];       // 0x18
            u8 lastRTrig[2];       // 0x1A
            u8 lastType[2];        // 0x1C
            u8 overrideType;       // 0x1E (0xFF = live pad)
            u8 pad;                // 0x1F
        };

        InputViewerDebugBlock s_debug;

        // First-quadrant cosine values for 16-segment circle (step 22.5 deg;
        // every 2nd step gives the 8-segment octagon).
        const float kCosQ[5] = {1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f};

        void unitPoint16(int idx, float& ox, float& oy)
        {
            int i = idx & 15;
            int q = i >> 2;
            int k = i & 3;
            float a = kCosQ[k];
            float b = kCosQ[4 - k];
            if (q == 0) { ox = a; oy = b; }
            else if (q == 1) { ox = -b; oy = a; }
            else if (q == 2) { ox = -a; oy = -b; }
            else { ox = b; oy = -a; }
        }

        float floatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        bool readFighterViewerPad(const game::FighterRef& ref, display::ViewerPad& out)
        {
            memset(&out, 0, sizeof(out));
            ftEntryManager* entries = g_ftEntryManager;
            ftEntry* entry = game::isRam(entries) ? entries->getEntity(ref.entryId) : NULL;
            int slot = game::isRam(entry) ? entry->m_slotIndex : -1;

            if (!ref.cpu && !(slot >= 0 && cpu::controlled(slot)))
            {
                const u8* cfg = reinterpret_cast<const u8*>(ipPadConfig::getInstance());
                int port = ref.port;
                if (game::isRam(cfg) && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
                {
                    int c = cfg[kPadConfigPorts + ref.playerNo] & 0xF;
                    if (c >= 0 && c < display::kPortPads)
                    {
                        port = c;
                    }
                }
                if (display::inputViewerPad(port, out))
                {
                    return true;
                }
            }
            if (game::isRam(entry) && game::isRam(entry->m_input))
            {
                const void* aiIn = reinterpret_cast<const void*>(game::wordAt(entry->m_input, kAiInputMain));
                if (game::isRam(aiIn))
                {
                    viewerAiInputToPad(game::wordAt(aiIn, kAiButtons), floatAt(aiIn, kAiStickX),
                                       floatAt(aiIn, kAiStickY), floatAt(aiIn, kAiTrigL),
                                       floatAt(aiIn, kAiTrigR), out);
                    return true;
                }
            }
            out.controllerType = kViewerGcc;
            return true;
        }

        void drawStickGate(const ui::layout::Box& b, const ViewerStickSpec& spec, s8 rawX, s8 rawY, float xs)
        {
            if (!spec.present)
            {
                return;
            }
            float cx = b.x0 + static_cast<float>(spec.nxPct) * 0.01f * (b.x1 - b.x0);
            float cy = b.y0 + static_cast<float>(spec.nyPct) * 0.01f * (b.y1 - b.y0);
            float ry = static_cast<float>(spec.r2) * 0.5f;
            float rx = ry * xs;

            gfx::fillEllipse(cx, cy, rx, ry, 0x060E14CCu);
            gfx::lineBegin(GX_LINES, 4, 6);
            gfx::vertex(cx - rx * 0.75f, cy, 0x3A586870u);
            gfx::vertex(cx + rx * 0.75f, cy, 0x3A586870u);
            gfx::vertex(cx, cy - ry * 0.75f, 0x3A586870u);
            gfx::vertex(cx, cy + ry * 0.75f, 0x3A586870u);

            int step = spec.octagonal ? 2 : 1;
            int verts = spec.octagonal ? 9 : 17;
            gfx::Rgba rim = spec.isCStick ? 0xC8A430D8u : 0x5A96A8E0u;
            gfx::lineBegin(GX_LINESTRIP, verts, 9);
            for (int i = 0; i < verts; i++)
            {
                float ux, uy;
                unitPoint16(i * step, ux, uy);
                gfx::vertex(cx + ux * rx, cy - uy * ry, rim);
            }

            float nx, ny;
            viewerStickNorm(rawX, rawY, nx, ny);
            float dx = cx + nx * rx * 0.85f;
            float dy = cy - ny * ry * 0.85f;
            if (nx != 0.0f || ny != 0.0f)
            {
                gfx::lineBegin(GX_LINES, 2, 10);
                gfx::Rgba ray = spec.isCStick ? 0xFFD300C0u : 0x36CFC2D8u;
                gfx::vertex(cx, cy, ray);
                gfx::vertex(dx, dy, ray);
            }
            float dr = spec.isCStick ? 2.6f : 3.2f;
            gfx::fillEllipse(dx, dy, (dr + 0.8f) * xs, dr + 0.8f, 0x050C12E0u);
            gfx::fillEllipse(dx, dy, dr * xs, dr, spec.isCStick ? 0xFFD300FFu : 0xF1F5F9FFu);
        }

        void drawCard(const ui::layout::Box& b, int playerNo, bool cpu, const ViewerLayout& layout,
                      const display::ViewerPad& pad)
        {
            float xs = gfx::xScale();
            float x = b.x0;
            float y = b.y0;
            float bw = b.x1 - b.x0;
            float bh = b.y1 - b.y0;
            float px = 1.5f * xs;

            gfx::fillRect(x, y, bw, bh, 0x050C12E4u);
            gfx::fillRect(x + px, y + 1.5f, bw - 2.0f * px, bh - 3.0f, 0x3A5868D8u);
            gfx::fillGradV(x + 2.0f * px, y + 3.0f, bw - 4.0f * px, bh - 6.0f, 0x132632DCu, 0x09141CE0u);
            gfx::fillRect(x + px, y + 1.5f, bw - 2.0f * px, 2.5f, cpu ? 0x4E86B8FFu : 0xD83838FFu);

            char tag[8];
            viewerTagText(playerNo, cpu, tag);
            float scale = ui::style::scaleForCap(6.5f);
            float tx = x + bw * 0.5f - (cpu ? 10.0f : 6.0f) * xs;
            float ty = ui::style::textYForCap(y + 5.0f, scale);
            gfx::text(tx + 0.8f, ty + 0.8f, tag, 0x000000C8u, scale);
            gfx::text(tx, ty, tag, cpu ? 0x9AD0F8FFu : 0xFFB0B0FFu, scale);

            for (int t = 0; t < 2; t++)
            {
                const ViewerTriggerSpec& tr = layout.triggers[t];
                if (!tr.present)
                {
                    continue;
                }
                float rx = x + static_cast<float>(tr.nxPct) * 0.01f * bw;
                float ry = y + static_cast<float>(tr.nyPct) * 0.01f * bh;
                float rw = static_cast<float>(tr.nwPct) * 0.01f * bw;
                float rh = static_cast<float>(tr.h);
                bool dig = (pad.buttons & tr.digitalMask) != 0;
                float fill = viewerTriggerFill(tr.isRight ? pad.rTrig : pad.lTrig, dig);
                gfx::fillRect(rx, ry, rw, rh, 0x060E14D8u);
                gfx::frameRect(rx, ry, rw, rh, 0x3A5868B8u);
                if (fill > 0.01f)
                {
                    float iw = (rw - 2.0f * xs) * fill;
                    float ix = tr.isRight ? (rx + rw - xs - iw) : (rx + xs);
                    gfx::fillRect(ix, ry + 1.0f, iw, rh - 2.0f, dig ? kViewerColorTrigClick : kViewerColorTrigFill);
                }
            }

            drawStickGate(b, layout.sticks[0], pad.sx, pad.sy, xs);
            drawStickGate(b, layout.sticks[1], pad.cx, pad.cy, xs);

            const ViewerButtonSpec* btns = viewerLayoutButtons(layout);
            for (int pass = 0; pass < 2; pass++)
            {
                for (u8 i = 0; i < layout.buttonCount; i++)
                {
                    const ViewerButtonSpec& btn = btns[i];
                    bool pressed = viewerButtonPressed(pad.buttons, btn);
                    if (pass == 0 && !pressed)
                    {
                        continue;
                    }
                    float cx = x + static_cast<float>(btn.nxPct) * 0.01f * bw;
                    float cy = y + static_cast<float>(btn.nyPct) * 0.01f * bh;
                    float ih = static_cast<float>(btn.h2) * 0.5f;
                    float iw = ih * ui::uiIconAspect(btn.iconId) * xs;
                    if (pass == 0)
                    {
                        gfx::fillEllipse(cx, cy, iw * 0.62f, ih * 0.62f, kViewerColorGlow);
                        continue;
                    }
                    if (!uiAssetsDrawIcon(btn.iconId, cx - iw * 0.5f, cy - ih * 0.5f, iw, ih, viewerIconTint(pressed)))
                    {
                        gfx::fillEllipse(cx, cy, iw * 0.42f, ih * 0.42f, pressed ? 0xFFD300FFu : 0x5A6B78A0u);
                    }
                    if (pressed && viewerIsDpadIcon(btn.iconId))
                    {
                        int dx, dy;
                        viewerDpadDir(pad.buttons, dx, dy);
                        if (dx != 0 || dy != 0)
                        {
                            gfx::fillEllipse(cx + static_cast<float>(dx) * iw * 0.30f,
                                             cy - static_cast<float>(dy) * ih * 0.30f, 2.2f * xs, 2.2f, 0xFFD300FFu);
                        }
                    }
                }
            }
        }
    }

    void inputViewerAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        s_debug.overrideType = 0xFFu;
        TM_LOG("input_viewer: debug block at %08x\n", &s_debug);
    }

    void inputViewerDetach()
    {
        s_debug.cardsDrawn = 0;
    }

    void inputViewerDraw(const game::MatchState& match, bool menuOpen)
    {
        s_debug.cardsDrawn = 0;
        u8 who = main::settings().display.inputViewer;
        if (!match.running || match.paused || menuOpen || who == display::kWhoNone)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        ui::layout::Box band = ui::layout::topBand();
        bool wide = gfx::widescreen();
        int slot = 0;
        // Draw human fighters first, then CPU fighters, ordered by playerNo so P1 is always top card.
        for (int pass = 0; pass < 2 && slot < kMaxCards; pass++)
        {
            bool wantCpu = (pass == 1);
            if ((who & (wantCpu ? display::kWhoCpu : display::kWhoHuman)) == 0)
            {
                continue;
            }
            for (int p = 0; p < game::kMaxFighters && slot < kMaxCards; p++)
            {
                for (int i = 0; i < n; i++)
                {
                    if (refs[i].playerNo != p || display::cpuPlayer(refs[i]) != wantCpu)
                    {
                        continue;
                    }
                    display::ViewerPad pad;
                    readFighterViewerPad(refs[i], pad);
                    u8 type = (s_debug.overrideType <= kViewerNunchuk) ? s_debug.overrideType : pad.controllerType;
                    drawCard(ui::layout::inputViewerCardBox(slot, band, wide), refs[i].playerNo, wantCpu,
                             viewerLayoutForType(type), pad);
                    s_debug.lastButtons[slot] = pad.buttons;
                    s_debug.lastStickX[slot] = pad.sx;
                    s_debug.lastStickY[slot] = pad.sy;
                    s_debug.lastLTrig[slot] = pad.lTrig;
                    s_debug.lastRTrig[slot] = pad.rTrig;
                    s_debug.lastType[slot] = type;
                    slot++;
                    break;
                }
            }
        }
        s_debug.cardsDrawn = static_cast<u32>(slot);
        if (slot > 0)
        {
            s_debug.totalFramesDrawn++;
        }
    }
}
}
