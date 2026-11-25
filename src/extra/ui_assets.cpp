#include "extra/ui_assets.h"

#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#include <GX/GXPixel.h>
#include <GX/GXTev.h>
#include <GX/GXTexture.h>
#include <GX/GXTransform.h>
#include <gf/gf_camera.h>
#include <gf/gf_draw.h>
#include <mu/menu.h>
#include <string.h>

#include "extra/di_display.h"
#include "extra/fighter_swap_core.h"
#include "extra/net_alloc.h"
#include "extra/ui_assets_core.h"
#include "main/gfx/draw.h"
#include "main/ui/menu.h"
#include "main/ui/pages/fighter_swap.h"
#include "main/ui/pages/pages.h"
#include "main/ui/style.h"
#include "shared/game/mem.h"
#include "shared/ui_icons.h"

// Native Brawl / Project+ UI button emblem and MEM2 stock-icon renderer.
// Draws zero text of its own (only shapes and resident CI8/CI4 textures).
namespace tm {
namespace extra {
    namespace {
        enum {
            kIfMngrAddr = 0x805A02D0,      // g_IfMngr pointer (MEM1)
            kIfMngrBrresOff = 0x1C,        // main info_training.pac BRRES
            kStockHandleAddr = 0x8053EEC0, // P+ permanent MEM2 StockFaceTex handle
            kFileReqSizeOff = 0x08,        // gfFileIORequest::m_size
            kFileReqBufOff = 0x0C,         // gfFileIORequest::m_buffer
            k2dCameraIndex = 4,
            kMuStockTableSize = 61
        };

        struct StockSlotCache {
            const u8* brres;
            u8 gmKind;
            u8 costumeIdx;
            bool resolved;
            CiAsset asset;
        };

        const char* const kSheetNames[kUiSheetCount] = {
            "InfPauseButton00",
            "InfButton04ClBtn01",
            "InfButton04NnBtn01"
        };

        const UiQuadUv kFullQuadUv = {
            {0.0f, 1.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 1.0f}
        };

        struct UiAssetsNet {
            const u8* infoBrres;
            CiAsset sheets[kUiSheetCount];
            StockSlotCache stockCache[2];
        };

        UiAssetsNet* s_net = NULL;

        void clearCaches()
        {
            if (s_net != NULL)
            {
                memset(s_net, 0, sizeof(*s_net));
            }
        }

        // Resolves and caches the 3 CI8 button sheets from resident info_training BRRES.
        void refreshInfoSheets()
        {
            if (s_net == NULL)
            {
                return;
            }
            const u8* mngr = *reinterpret_cast<const u8* const*>(kIfMngrAddr);
            const u8* brres = NULL;
            if (game::isRam(mngr))
            {
                const u8* candidate = reinterpret_cast<const u8*>(game::wordAt(mngr, kIfMngrBrresOff));
                if (game::isRam(candidate) && game::wordAt(candidate, 0) == kBrresMagic)
                {
                    brres = candidate;
                }
            }
            if (brres == s_net->infoBrres)
            {
                return;
            }
            s_net->infoBrres = brres;
            memset(s_net->sheets, 0, sizeof(s_net->sheets));
            if (brres == NULL)
            {
                return;
            }
            u32 fileSize = beU32(brres + 0x08);
            if (fileSize < 0x30u || fileSize > kBrresMaxFileSize || !game::isRam(brres + fileSize - 1))
            {
                return;
            }
            BrresView view;
            if (!brresOpen(brres, fileSize, view))
            {
                return;
            }
            for (int i = 0; i < kUiSheetCount; i++)
            {
                if (brresFindCiAsset(view, kSheetNames[i], s_net->sheets[i]))
                {
                    u16 expW = 0;
                    u16 expH = 0;
                    bool okSize = uiIconSheetExpectedSize(i, expW, expH) && s_net->sheets[i].tex.width == expW
                                  && s_net->sheets[i].tex.height == expH;
                    bool okTlut = s_net->sheets[i].tex.gxFmt != kGxTfC14X2 && s_net->sheets[i].plt.entryCount <= 256u;
                    if (!okSize || !okTlut)
                    {
                        s_net->sheets[i].valid = false;
                    }
                }
            }
        }

        // Opens the permanent MEM2 StockFaceTex.brres view.
        bool openStockBrres(BrresView& out)
        {
            const u8* req = *reinterpret_cast<const u8* const*>(kStockHandleAddr);
            if (!game::isRam(req))
            {
                return false;
            }
            u32 sizeBytes = game::wordAt(req, kFileReqSizeOff);
            const u8* brres = reinterpret_cast<const u8*>(game::wordAt(req, kFileReqBufOff));
            if (!game::isRam(brres) || sizeBytes < 0x30u || sizeBytes > kBrresMaxFileSize)
            {
                return false;
            }
            if (!game::isRam(brres + sizeBytes - 1) || game::wordAt(brres, 0) != kBrresMagic)
            {
                return false;
            }
            return brresOpen(brres, sizeBytes, out);
        }

        // Resolves (with per-slot caching) a fighter's stock icon from StockFaceTex.brres.
        const CiAsset* resolveStockIcon(int slot, const BrresView& view, u8 gmKind, u8 costumeIdx)
        {
            if (s_net == NULL)
            {
                return NULL;
            }
            StockSlotCache& cache = s_net->stockCache[slot];
            if (cache.resolved && cache.brres == view.base && cache.gmKind == gmKind
                && cache.costumeIdx == costumeIdx)
            {
                return cache.asset.valid ? &cache.asset : NULL;
            }
            cache.brres = view.base;
            cache.gmKind = gmKind;
            cache.costumeIdx = costumeIdx;
            cache.resolved = true;
            cache.asset.valid = false;

            int tblIdx = muMenu::exchangeGmCharacterKind2MuStockchkind(static_cast<int>(gmKind));
            int cosmeticId = (tblIdx >= 0 && tblIdx < kMuStockTableSize)
                                 ? muMenu::exchangeGmCharacterKind2Something(tblIdx)
                                 : -1;
            char name[kStockNameBufSize];
            stockIconName(cosmeticId, static_cast<int>(costumeIdx), name);
            if (!brresFindCiAsset(view, name, cache.asset))
            {
                stockFallbackName(name);
                brresFindCiAsset(view, name, cache.asset);
            }
            if (cache.asset.valid && (cache.asset.tex.gxFmt == kGxTfC14X2 || cache.asset.plt.entryCount > 256u))
            {
                cache.asset.valid = false;
            }
            return cache.asset.valid ? &cache.asset : NULL;
        }

        // Exact GX state for CI8/CI4 textured quads inside a 2D pass (including
        // the 5 states omitted by gfDrawSetVtxPosColorTexPrimEnvironment).
        void setupTexturedQuadGx()
        {
            gfDrawSetVtxPosColorTexPrimEnvironment();
            GXSetCullMode(GX_CULL_NONE);
            GXLoadPosMtxImm(
                *reinterpret_cast<const Mtx*>(&gfCameraManager::getManager()->m_cameras[k2dCameraIndex]),
                GX_PNMTX0);
            GXSetZMode(FALSE, GX_ALWAYS, FALSE);
            GXSetZCompLoc(FALSE);
            GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
            GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
        }

        void bindCiAsset(const CiAsset& asset, GXTexFilter filter)
        {
            GXTlutObj tlutObj;
            GXInitTlutObj(&tlutObj, const_cast<u8*>(asset.plt.data),
                          static_cast<GXTlutFmt>(asset.plt.fmt), asset.plt.entryCount);
            GXLoadTlut(&tlutObj, GX_TLUT0);

            GXTexObj texObj;
            GXInitTexObjCI(&texObj, const_cast<u8*>(asset.tex.data), asset.tex.width, asset.tex.height,
                           static_cast<GXTexFmt>(asset.tex.gxFmt), GX_CLAMP, GX_CLAMP, FALSE, GX_TLUT0);
            GXInitTexObjLOD(&texObj, filter, filter, 0.0f, 0.0f, 0.0f, FALSE, FALSE, GX_ANISO_1);
            GXLoadTexObj(&texObj, GX_TEXMAP0);
        }

        void emitTexturedQuad(float x, float y, float w, float h, u32 rgba, const UiQuadUv& uv)
        {
            GXBegin(GX_QUADS, GX_VTXFMT1, 4);
            WGPIPE.f = x;
            WGPIPE.f = y;
            WGPIPE.f = 0.0f;
            WGPIPE.ui = rgba;
            WGPIPE.f = uv.u[0];
            WGPIPE.f = uv.v[0];
            WGPIPE.f = x + w;
            WGPIPE.f = y;
            WGPIPE.f = 0.0f;
            WGPIPE.ui = rgba;
            WGPIPE.f = uv.u[1];
            WGPIPE.f = uv.v[1];
            WGPIPE.f = x + w;
            WGPIPE.f = y + h;
            WGPIPE.f = 0.0f;
            WGPIPE.ui = rgba;
            WGPIPE.f = uv.u[2];
            WGPIPE.f = uv.v[2];
            WGPIPE.f = x;
            WGPIPE.f = y + h;
            WGPIPE.f = 0.0f;
            WGPIPE.ui = rgba;
            WGPIPE.f = uv.u[3];
            WGPIPE.f = uv.v[3];
        }

        void drawStockCard(float x, float y, float tw, float th, gfx::Rgba accent, gfx::Rgba accentHi)
        {
            float px2 = ui::style::w(2.0f);
            gfx::fillRect(x, y, tw, th, 0x000000E8);
            gfx::fillRect(x + px2, y + 2.0f, tw - 2.0f * px2, th - 4.0f, 0x3A5868FF);
            gfx::fillGradV(x + 2.0f * px2, y + 4.0f, tw - 4.0f * px2, th - 8.0f, 0x182E3AF0, 0x0A151DF0);
            gfx::fillRect(x + px2, y + 2.0f, tw - 2.0f * px2, 4.0f, accent);
            gfx::fillRect(x + px2, y + 2.0f, tw - 2.0f * px2, 2.0f, accentHi);
            gfx::fillRect(x + px2, y + th - 4.0f, tw - 2.0f * px2, 2.0f, 0x264454B0);
        }
    }

    void uiAssetsAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<UiAssetsNet>(1);
        clearCaches();
    }

    void uiAssetsDetach()
    {
        clearCaches();
        freeNetPtr(s_net);
    }

    bool uiAssetsDrawIcon(u8 iconId, float x, float y, float w, float h, u32 rgba)
    {
        if (s_net == NULL || w <= 0.0f || h <= 0.0f)
        {
            return false;
        }
        const UiIconRect* rect = uiIconRect(iconId);
        if (rect == NULL || rect->sheet >= kUiSheetCount)
        {
            return false;
        }
        refreshInfoSheets();
        const CiAsset& sheet = s_net->sheets[rect->sheet];
        if (!sheet.valid)
        {
            return false;
        }
        UiQuadUv uv;
        if (!computeIconUv(*rect, sheet.tex.width, sheet.tex.height, uv))
        {
            return false;
        }
        setupTexturedQuadGx();
        bindCiAsset(sheet, GX_LINEAR);
        emitTexturedQuad(x, y, w, h, rgba, uv);
        gfx::forgetState();
        return true;
    }

    void uiAssetsDrawPageOverlay(u8 pageId, bool railMode)
    {
        if (pageId == ui::pages::P_CPU_DI)
        {
            diDrawCustomPickerOverlay(railMode);
            return;
        }
        if (pageId != ui::pages::P_FIGHTER_SWAP)
        {
            return;
        }
        const ui::pages::FighterSwapMenuState* st = ui::pages::fighterSwapMenuState();
        if (st == NULL)
        {
            return;
        }
        const FighterSwapEntry* p1 = fighterSwapAt(st->p1Fighter);
        const FighterSwapEntry* cpu = fighterSwapAt(st->cpuFighter);
        if (p1 == NULL || cpu == NULL)
        {
            return;
        }

        // Center inside the active options pane below row 5 ("Apply Swap").
        // Rail pane: x 208..576 (mid 392), row 5 bottom ~198 -> y 216..270.
        // Tabs subpage pane: x 56..574 (mid 315), row 5 bottom ~220 -> y 228..282.
        float paneCenter = railMode ? 392.0f : 315.0f;
        float topY = railMode ? 216.0f : 228.0f;
        float tileW = 54.0f;
        float tileH = 54.0f;
        float gap = 36.0f;

        float p1X = ui::style::cx(paneCenter - gap * 0.5f - tileW);
        float cpuX = ui::style::cx(paneCenter + gap * 0.5f);
        float tw = ui::style::w(tileW);

        // 1. Batched geometric chrome: framed P1/CPU cards and center diamond divider.
        drawStockCard(p1X, topY, tw, tileH, 0xD83838FF, 0xFF7A7AC0);
        drawStockCard(cpuX, topY, tw, tileH, 0x4E86B8FF, 0x8AC0ECC0);

        float midX = ui::style::cx(paneCenter);
        float midY = topY + tileH * 0.5f;
        float diaRx = ui::style::w(6.0f);
        float diaRy = 6.0f;
        float wingW = ui::style::w(7.0f);
        float wingGap = ui::style::w(3.0f);
        gfx::fillRect(midX - diaRx - wingGap - wingW, midY - 1.0f, wingW, 2.0f, 0x3A5868CC);
        gfx::fillRect(midX + diaRx + wingGap, midY - 1.0f, wingW, 2.0f, 0x3A5868CC);
        gfx::fillTri(midX, midY - diaRy, midX + diaRx, midY, midX - diaRx, midY, 0x2A4656FF);
        gfx::fillTri(midX, midY + diaRy, midX - diaRx, midY, midX + diaRx, midY, 0x162A36FF);
        float inRx = ui::style::w(3.5f);
        float inRy = 3.5f;
        gfx::fillTri(midX, midY - inRy, midX + inRx, midY, midX - inRx, midY, 0xFFD300FF);
        gfx::fillTri(midX, midY + inRy, midX - inRx, midY, midX + inRx, midY, 0xC89E00FF);

        // 2. Live 32x32 stock icons from permanent MEM2 StockFaceTex.brres (GX_NEAR).
        BrresView stockView;
        if (!openStockBrres(stockView))
        {
            return;
        }
        const CiAsset* p1Asset = resolveStockIcon(0, stockView, p1->gmKind, st->p1Costume);
        const CiAsset* cpuAsset = resolveStockIcon(1, stockView, cpu->gmKind, st->cpuCostume);
        if (p1Asset == NULL && cpuAsset == NULL)
        {
            return;
        }

        float iconW = ui::style::w(32.0f);
        float iconH = 32.0f;
        float iconY = topY + 4.0f + (tileH - 5.0f - iconH) * 0.5f;
        setupTexturedQuadGx();
        if (p1Asset != NULL)
        {
            bindCiAsset(*p1Asset, GX_NEAR);
            emitTexturedQuad(p1X + (tw - iconW) * 0.5f, iconY, iconW, iconH, 0xFFFFFFFF, kFullQuadUv);
        }
        if (cpuAsset != NULL)
        {
            bindCiAsset(*cpuAsset, GX_NEAR);
            emitTexturedQuad(cpuX + (tw - iconW) * 0.5f, iconY, iconW, iconH, 0xFFFFFFFF, kFullQuadUv);
        }
        gfx::forgetState();
    }
}
}
