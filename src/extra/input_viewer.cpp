#include "extra/input_viewer.h"

#include <GX/GXTypes.h>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ip/ip_padconfig.h>
#include <string.h>

#include "extra/di_cstick_core.h"
#include "extra/hud_draw.h"
#include "extra/input_viewer_core.h"
#include "extra/net_alloc.h"
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

        InputViewerDebugBlock* s_debug = NULL;

        bool readFighterViewerPad(const game::FighterRef& ref, display::ViewerPad& out)
        {
            memset(&out, 0, sizeof(out));
            ftEntry* entry = extraEntry(ref.entryId);
            int slot = game::isRam(entry) ? entry->m_slotIndex : -1;

            if (!ref.cpu && !(slot >= 0 && cpu::controlled(slot)))
            {
                if (display::inputViewerPad(extraHumanPort(ref), out))
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

        void fillOctagon(float cx, float cy, float rx, float ry, gfx::Rgba col)
        {
            float vx[8], vy[8];
            for (int i = 0; i < 8; i++)
            {
                float ux, uy;
                unitPoint16(i * 2, ux, uy);
                vx[i] = cx + ux * rx;
                vy[i] = cy - uy * ry;
            }
            for (int i = 0; i < 8; i++)
            {
                int next = (i + 1) & 7;
                gfx::fillTri(cx, cy, vx[i], vy[i], vx[next], vy[next], col);
            }
        }

        void drawOctagonGate(float cx, float cy, float rx, float ry, s8 rawX, s8 rawY, float xs, bool isCStick,
                             gfx::Rgba rimCol)
        {
            // True octagonal 3-layer silhouette: dark outer frame, crisp rim, deep smoked interior.
            fillOctagon(cx, cy, rx + 2.2f * xs, ry + 2.2f, 0x05090FF4u);
            fillOctagon(cx, cy, rx + 0.6f * xs, ry + 0.6f, rimCol);
            fillOctagon(cx, cy, rx - 1.1f * xs, ry - 1.1f, 0x101822DCu);

            // Subtle crosshairs for 480i cardinal readability.
            gfx::lineBegin(GX_LINES, 4, 9);
            gfx::vertex(cx - rx * 0.78f, cy, 0x98B0C458u);
            gfx::vertex(cx + rx * 0.78f, cy, 0x98B0C458u);
            gfx::vertex(cx, cy - ry * 0.78f, 0x98B0C458u);
            gfx::vertex(cx, cy + ry * 0.78f, 0x98B0C458u);

            float nx, ny;
            viewerStickNorm(rawX, rawY, nx, ny);
            bool tilted = nx != 0.0f || ny != 0.0f;
            drawDialRayDot(cx, cy, rx, ry, nx, ny, 0.84f, isCStick ? 3.2f : 3.6f, xs,
                           isCStick ? 0xFFD300E0u : 0x70E6F2E8u, isCStick ? 0xFFD300FFu : 0xFFFFFFFFu, 12, tilted);
        }

        void fillStadiumShape(float x, float y, float w, float h, float xs, gfx::Rgba col)
        {
            if (w >= h * xs)
            {
                float ry = h * 0.5f;
                float rx = ry * xs;
                float cy = y + ry;
                if (w <= 2.0f * rx + 0.5f)
                {
                    gfx::fillEllipse(x + w * 0.5f, cy, w * 0.5f, ry, col);
                    return;
                }
                gfx::fillEllipse(x + rx, cy, rx, ry, col);
                gfx::fillEllipse(x + w - rx, cy, rx, ry, col);
                gfx::fillRect(x + rx, y, w - 2.0f * rx, h, col);
            }
            else
            {
                float rx = w * 0.5f;
                float ry = rx / (xs > 0.01f ? xs : 1.0f);
                float cx = x + rx;
                if (h <= 2.0f * ry + 0.5f)
                {
                    gfx::fillEllipse(cx, y + h * 0.5f, rx, h * 0.5f, col);
                    return;
                }
                gfx::fillEllipse(cx, y + ry, rx, ry, col);
                gfx::fillEllipse(cx, y + h - ry, rx, ry, col);
                gfx::fillRect(x, y + ry, w, h - 2.0f * ry, col);
            }
        }

        void drawPillSilhouette(float x, float y, float w, float h, float xs, gfx::Rgba fillCol, gfx::Rgba rimCol)
        {
            float px = 1.6f * xs;
            fillStadiumShape(x - px * 0.5f, y - 0.8f, w + px, h + 1.6f, xs, 0x05090FF2u);
            fillStadiumShape(x + px * 0.35f, y + 0.6f, w - 0.7f * px, h - 1.2f, xs, rimCol);
            fillStadiumShape(x + px * 0.95f, y + 1.6f, w - 1.9f * px, h - 3.2f, xs, fillCol);
        }

        void drawBareGcc(const ui::layout::Box& b, int playerNo, bool cpu, bool showTag,
                         const display::ViewerPad& pad)
        {
            float xs = gfx::xScale();
            float x = b.x0;
            float y = b.y0;
            float bw = b.x1 - b.x0;
            float bh = b.y1 - b.y0;

            // Clean P1/CPU tag when dual controllers are active (matching 01-final-everyday).
            if (showTag)
            {
                char tag[8];
                viewerTagText(playerNo, cpu, tag);
                drawShadowCapCenter(x + bw * 0.45f, y + 0.5f, tag, cpu ? 0x86DCEEFFu : 0xFF8C8CFFu, 9.5f);
            }

            // Rounded stadium L & R shoulder triggers aligned tightly above the stick & face cluster.
            const float trigH = 7.4f;
            const float trigY = y + 3.0f;
            const float trigW = bw * 0.28f;
            for (int t = 0; t < 2; t++)
            {
                bool isR = (t == 1);
                float rx = isR ? (x + bw - trigW - 2.0f * xs) : (x + 2.0f * xs);
                bool dig = (pad.buttons & (isR ? game::kPadR : game::kPadL)) != 0;
                float fill = viewerTriggerFill(isR ? pad.rTrig : pad.lTrig, dig);
                float bx = 1.6f * xs;
                fillStadiumShape(rx - bx * 0.5f, trigY - 0.8f, trigW + bx, trigH + 1.6f, xs, 0x05090FF2u);
                fillStadiumShape(rx + bx * 0.3f, trigY + 0.6f, trigW - 0.6f * bx, trigH - 1.2f, xs,
                                 dig ? 0xFFF4A8FFu : 0xA8BAC8E0u);
                fillStadiumShape(rx + bx * 0.9f, trigY + 1.6f, trigW - 1.8f * bx, trigH - 3.2f, xs, 0x1A2430E8u);
                if (fill > 0.01f)
                {
                    float iw = (trigW - 2.2f * bx) * fill;
                    float ix = isR ? (rx + trigW - 1.1f * bx - iw) : (rx + 1.1f * bx);
                    gfx::fillRect(ix, trigY + 1.8f, iw, trigH - 3.6f, dig ? 0xFFD300FFu : 0x4CE2D2FFu);
                }
            }

            // Z indigo stadium pill tucked cleanly directly below R, clearing Y and A on 480i.
            {
                bool zOn = (pad.buttons & game::kPadZ) != 0;
                float zw = bw * 0.25f;
                float zx = x + bw - zw - 3.0f * xs;
                float zy = y + 13.0f;
                drawPillSilhouette(zx, zy, zw, 6.4f, xs, zOn ? 0xB8A6FFFFu : 0x5E4EC8E8u,
                                   zOn ? 0xFFFFFFFFu : 0x9E90F0F0u);
            }

            // True octagonal L-stick gate (left) and unmistakable gold C-stick octagon (lower-center).
            gfx::Rgba lRim = showTag ? (cpu ? 0x76D4ECFFu : 0xF27474FFu) : 0xDCE8F2F0u;
            drawOctagonGate(x + bw * 0.20f, y + bh * 0.57f, 15.0f * xs, 15.0f, pad.sx, pad.sy, xs, false, lRim);
            drawOctagonGate(x + bw * 0.48f, y + bh * 0.77f, 9.2f * xs, 9.2f, pad.cx, pad.cy, xs, true, 0xFFD300F8u);

            // Compact Start pill + crisp outlined D-pad cross.
            {
                bool stOn = (pad.buttons & game::kPadStart) != 0;
                float scx = x + bw * 0.45f;
                float scy = y + bh * 0.44f;
                gfx::fillEllipse(scx, scy, 3.6f * xs, 3.6f, 0x05090FF0u);
                gfx::fillEllipse(scx, scy, 2.4f * xs, 2.4f, stOn ? 0xFFFFFFFFu : 0x9AAAB8D0u);
            }
            {
                float dcx = x + bw * 0.34f;
                float dcy = y + bh * 0.85f;
                float arm = 5.4f;
                float th = 3.4f;
                gfx::fillRect(dcx - arm * xs, dcy - th * 0.5f, 2.0f * arm * xs, th, 0x05090FF0u);
                gfx::fillRect(dcx - th * 0.5f * xs, dcy - arm, th * xs, 2.0f * arm, 0x05090FF0u);
                gfx::fillRect(dcx - (arm - 1.2f) * xs, dcy - 0.9f, 2.0f * (arm - 1.2f) * xs, 1.8f, 0x8CA0B2D8u);
                gfx::fillRect(dcx - 0.9f * xs, dcy - (arm - 1.2f), 1.8f * xs, 2.0f * (arm - 1.2f), 0x8CA0B2D8u);
                int dx, dy;
                viewerDpadDir(pad.buttons, dx, dy);
                if (dx != 0 || dy != 0)
                {
                    gfx::fillEllipse(dcx + i2f(dx) * 3.4f * xs, dcy - i2f(dy) * 3.4f, 2.4f * xs, 2.4f, 0xFFD300FFu);
                }
            }

            // Iconic GameCube right face cluster (matching 01-final-everyday & 10-grounding):
            //  - Y rounded horizontal pill cleanly above A
            //  - Bold emerald A circle with bright mint rim and crisp centered 'A' glyph
            //  - Outlined crimson B circle tucked lower-left of A
            //  - X rounded vertical pill tucked right of A
            {
                bool yOn = (pad.buttons & game::kPadY) != 0;
                float yw = 15.5f * xs;
                drawPillSilhouette(x + bw * 0.72f - yw * 0.5f, y + 22.0f, yw, 5.8f, xs,
                                   yOn ? 0xFFFFFFFFu : 0x62707EE8u, yOn ? 0xFFFFFFFFu : 0xBCCAD8E8u);
            }
            {
                bool aOn = (pad.buttons & game::kPadA) != 0;
                float acx = x + bw * 0.73f;
                float acy = y + 38.5f;
                if (aOn)
                {
                    gfx::fillEllipse(acx, acy, 10.6f * xs, 10.6f, 0x48FFA870u);
                }
                gfx::fillEllipse(acx, acy, 9.4f * xs, 9.4f, 0x05090FF4u);
                gfx::fillEllipse(acx, acy, 7.9f * xs, 7.9f, aOn ? 0x9AFFD0FFu : 0x4CE2B4F0u);
                gfx::fillEllipse(acx, acy, 6.7f * xs, 6.7f, aOn ? 0x46F09CFFu : 0x1E8F64E8u);
                float aCap = 8.4f;
                float aSc = ui::style::scaleForCap(aCap);
                float aW = gfx::textWidth("A", aSc);
                float aX = acx - aW * 0.5f;
                float aY = ui::style::textYForCap(acy - aCap * 0.5f, aSc);
                gfx::text(aX + 0.6f * xs, aY + 0.8f, "A", 0x04120CE0u, aSc);
                gfx::text(aX, aY, "A", aOn ? 0x041C12FFu : 0xE8FFF4FFu, aSc);
            }
            {
                bool bOn = (pad.buttons & game::kPadB) != 0;
                float bcx = x + bw * 0.59f;
                float bcy = y + 46.5f;
                if (bOn)
                {
                    gfx::fillEllipse(bcx, bcy, 7.6f * xs, 7.6f, 0xFF5A5A70u);
                }
                gfx::fillEllipse(bcx, bcy, 6.5f * xs, 6.5f, 0x05090FF4u);
                gfx::fillEllipse(bcx, bcy, 5.2f * xs, 5.2f, bOn ? 0xFF8C8CFFu : 0xF05454F0u);
                gfx::fillEllipse(bcx, bcy, 4.3f * xs, 4.3f, bOn ? 0xFF4242FFu : 0xC82828E8u);
            }
            {
                bool xOn = (pad.buttons & game::kPadX) != 0;
                float xw = 6.0f * xs;
                drawPillSilhouette(x + bw * 0.90f - xw * 0.5f, y + 31.0f, xw, 14.5f, xs,
                                   xOn ? 0xFFFFFFFFu : 0x62707EE8u, xOn ? 0xFFFFFFFFu : 0xBCCAD8E8u);
            }
        }

        void drawStickGate(const ui::layout::Box& b, const ViewerStickSpec& spec, s8 rawX, s8 rawY, float xs)
        {
            if (!spec.present)
            {
                return;
            }
            float cx = b.x0 + i2fScale(spec.nxPct, 0.01f) * (b.x1 - b.x0);
            float cy = b.y0 + i2fScale(spec.nyPct, 0.01f) * (b.y1 - b.y0);
            float ry = i2fScale(spec.r2, 0.5f);
            float rx = ry * xs;

            gfx::fillEllipse(cx, cy, rx + 1.5f * xs, ry + 1.5f, 0x050B10DCu);
            gfx::fillEllipse(cx, cy, rx, ry, 0x0E161EC0u);
            int step = spec.octagonal ? 2 : 1;
            int verts = spec.octagonal ? 9 : 17;
            gfx::Rgba rim = spec.isCStick ? 0xE8C238E0u : 0xB8CCD8DCu;
            gfx::lineBegin(GX_LINESTRIP, verts, 10);
            for (int i = 0; i < verts; i++)
            {
                float ux, uy;
                unitPoint16(i * step, ux, uy);
                gfx::vertex(cx + ux * rx, cy - uy * ry, rim);
            }

            float nx, ny;
            viewerStickNorm(rawX, rawY, nx, ny);
            drawDialRayDot(cx, cy, rx, ry, nx, ny, 0.84f, spec.isCStick ? 2.6f : 3.2f, xs,
                           spec.isCStick ? 0xFFD300C0u : 0x60DCE8D8u, spec.isCStick ? 0xFFD300FFu : 0xF8FAFCFFu, 10,
                           nx != 0.0f || ny != 0.0f);
        }

        void drawCard(const ui::layout::Box& b, int playerNo, bool cpu, bool showTag, const ViewerLayout& layout,
                      const display::ViewerPad& pad)
        {
            if (layout.kind == kViewerGcc)
            {
                drawBareGcc(b, playerNo, cpu, showTag, pad);
                return;
            }
            float xs = gfx::xScale();
            float x = b.x0;
            float y = b.y0;
            float bw = b.x1 - b.x0;
            float bh = b.y1 - b.y0;

            if (showTag)
            {
                char tag[8];
                viewerTagText(playerNo, cpu, tag);
                drawShadowCapCenter(x + bw * 0.5f, y + 1.0f, tag, cpu ? 0x86D4E8F0u : 0xFF9494F0u, 8.5f);
            }

            for (int t = 0; t < 2; t++)
            {
                const ViewerTriggerSpec& tr = layout.triggers[t];
                if (!tr.present)
                {
                    continue;
                }
                float rx = x + i2fScale(tr.nxPct, 0.01f) * bw;
                float ry = y + i2fScale(tr.nyPct, 0.01f) * bh;
                float rw = i2fScale(tr.nwPct, 0.01f) * bw;
                float rh = 6.5f;
                bool dig = (pad.buttons & tr.digitalMask) != 0;
                float fill = viewerTriggerFill(tr.isRight ? pad.rTrig : pad.lTrig, dig);
                float bx = 1.8f * xs;
                gfx::fillRect(rx, ry, rw, rh, 0x050B10E4u);
                gfx::fillRect(rx + bx, ry + 1.6f, rw - 2.0f * bx, rh - 3.2f, 0x16222CC8u);
                if (fill > 0.01f)
                {
                    float iw = (rw - 2.0f * bx) * fill;
                    float ix = tr.isRight ? (rx + rw - bx - iw) : (rx + bx);
                    gfx::fillRect(ix, ry + 1.6f, iw, rh - 3.2f, dig ? kViewerColorTrigClick : kViewerColorTrigFill);
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
                    float cx = x + i2fScale(btn.nxPct, 0.01f) * bw;
                    float cy = y + i2fScale(btn.nyPct, 0.01f) * bh;
                    float ih = i2fScale(btn.h2, 0.5f);
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
                            gfx::fillEllipse(cx + i2f(dx) * iw * 0.30f, cy - i2f(dy) * ih * 0.30f, 2.2f * xs, 2.2f,
                                             0xFFD300FFu);
                        }
                    }
                }
            }
        }
    }

    void inputViewerAttach()
    {
        freeNetPtr(s_debug);
        s_debug = allocNetArray<InputViewerDebugBlock>(1);
        if (s_debug == NULL)
        {
            return;
        }
        s_debug->magic = kDebugMagic;
        s_debug->overrideType = 0xFFu;
        TM_LOG("input_viewer: debug block at %08x\n", s_debug);
    }

    void inputViewerDetach()
    {
        freeNetPtr(s_debug);
    }

    void inputViewerDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_debug == NULL)
        {
            return;
        }
        InputViewerDebugBlock& dbg = *s_debug;
        dbg.cardsDrawn = 0;
        u8 who = main::settings().display.inputViewer;
        if (!match.running || match.paused || menuOpen || who == display::kWhoNone)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        int totalSlots = countInputViewerSlots(refs, n);
        bool showTag = totalSlots > 1;
        bool wide = gfx::widescreen();
        int slot = 0;
        // Draw human fighters first, then CPU fighters, ordered by playerNo so P1 is always slot 0.
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
                    u8 type = (dbg.overrideType <= kViewerNunchuk) ? dbg.overrideType : pad.controllerType;
                    drawCard(hudViewerCardBox(slot, wide), refs[i].playerNo, wantCpu, showTag,
                             viewerLayoutForType(type), pad);
                    dbg.lastButtons[slot] = pad.buttons;
                    dbg.lastStickX[slot] = pad.sx;
                    dbg.lastStickY[slot] = pad.sy;
                    dbg.lastLTrig[slot] = pad.lTrig;
                    dbg.lastRTrig[slot] = pad.rTrig;
                    dbg.lastType[slot] = type;
                    slot++;
                    break;
                }
            }
        }
        dbg.cardsDrawn = static_cast<u32>(slot);
        if (slot > 0)
        {
            dbg.totalFramesDrawn++;
        }
    }
}
}
