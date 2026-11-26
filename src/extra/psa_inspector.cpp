#include "extra/psa_inspector.h"

#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/di_picker_core.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"
#include "extra/psa_inspector_core.h"
#include "extra/psa_opcodes.h"
#include "main/display/display.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Live read-only PSA AnimCmd 11-Thread & FSM Inspector (RSBE01 rev 2, P+ 3.2.1).
// Concept adapted from fudgepop01's SuperTraining / FudgeMenu PSA inspector
// (used with express permission; see CREDITS.md), implemented from scratch on
// verified P+ 3.2.1 runtime layouts:
//  - soAnimCmdModuleImpl (soModuleEnumeration +0x6C) -> m_animCmdThreads (+0x20):
//    vtable +0x18 size() (11 threads), vtable +0x5C atIndexFast(i) (0x8077BE28-5C).
//  - soAnimCmdControlUnit: +0x00 acCmdInterpreter*, +0x08 m_id, +0x0C s16 m_type
//    (0x8077BE84, 0x8077BEB8).
//  - acCmdInterpreter: +0x08 float m_frameSpeed, +0x0C acAnimCmd* m_currentCmd,
//    +0x10 acAnimCmd* m_startCmd, +0x20 soArray<acCmdStackEntry>* m_cmdStack
//    (vtable +0x14 size(), +0x0C at(); +0x00 == 0 SyncWait remaining at +0x10,
//    == 1 active AsyncWait / == 7 AsyncWait marker with target logical frame at
//    +0x10), +0x24 float m_logicalFrame (0x80139800-88, 0x8013AD54, 0x8013AF0C).
//  - soMotionModuleImpl (soModuleEnumeration +0x08): +0x40 m_mainAnim.m_animFrame,
//    +0x4C m_mainAnim.m_rate (getRate 0x8071F43C), +0x58 m_kind, +0x7C
//    m_secondaryAnim.m_rate, +0x88 m_secondaryKind.
namespace tm {
namespace extra {
    namespace {
        enum {
            kAnimThreadsOff = 0x20,
            kThreadsSizeSlot = 0x18 / 4,
            kThreadsAtFastSlot = 0x5C / 4,

            kUnitInterpOff = 0x00,
            kUnitIdOff = 0x08,
            kUnitTypeOff = 0x0C,

            kInterpSpeedOff = 0x08,
            kInterpCurCmdOff = 0x0C,
            kInterpStartCmdOff = 0x10,
            kInterpStackOff = 0x20,
            kInterpLogicalFrameOff = 0x24,

            kStackSizeSlot = 0x14 / 4,
            kStackAtSlot = 0x0C / 4,
            kStackKindOff = 0x00,
            kStackValOff = 0x10,
            kStackMaxDepth = 64,

            kMotionFrameOff = 0x40,
            kMotionRateOff = 0x4C,
            kMotionKindOff = 0x58,
            kMotionSecRateOff = 0x7C,
            kMotionSecKindOff = 0x88
        };

        typedef s32 (*VSizeFn)(const void*);
        typedef const void* (*VAtFn)(const void*, s32);

        struct PsaFocusCache {
            u32 hdr;
            u32 argCount;
            PsaArgSnapshot args[kPsaMaxArgs];
            char text[48];
            bool valid;
        };

        struct PsaNet {
            PsaDebugBlock debug;
            PsaFocusCache focusCache;
        };

        PsaNet* s_net = NULL;

        s16 s16At(const void* base, u32 offset)
        {
            return *reinterpret_cast<const s16*>(reinterpret_cast<u32>(base) + offset);
        }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        bool alignedRam(const void* p)
        {
            return game::isRam(p) && (reinterpret_cast<u32>(p) & 3u) == 0u;
        }

        void clearSnapshot()
        {
            if (s_net == NULL)
            {
                return;
            }
            PsaDebugBlock* dbg = &s_net->debug;
            u32 calls = dbg->drawCalls;
            memset(dbg, 0, sizeof(*dbg));
            dbg->magic = kPsaDebugMagic;
            dbg->drawCalls = calls;
            dbg->playerNo = -1;
            dbg->status = -1;
            dbg->subaction = -1;
            dbg->secondaryKind = -1;
            dbg->focusThread = -1;
        }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

        bool selectTargetFighter(u8 target, const game::FighterRef* refs, int count, game::FighterRef& out)
        {
            bool wantCpu = (target == main::kPsaCpu);
            for (int p = 0; p < game::kMaxFighters; p++)
            {
                for (int i = 0; i < count; i++)
                {
                    if (refs[i].playerNo == p && display::cpuPlayer(refs[i]) == wantCpu)
                    {
                        out = refs[i];
                        return true;
                    }
                }
            }
            return false;
        }

        void readThread(const void* unit, int fallbackIndex, PsaThreadSnapshot& out, const u32*& outCmdPtr)
        {
            memset(&out, 0, sizeof(out));
            outCmdPtr = NULL;
            out.id = static_cast<u32>(fallbackIndex);
            if (!alignedRam(unit))
            {
                return;
            }
            u32 unitId = game::wordAt(unit, kUnitIdOff);
            if (unitId < kPsaThreadCount)
            {
                out.id = unitId;
            }
            out.type = s16At(unit, kUnitTypeOff);

            const void* interp = reinterpret_cast<const void*>(game::wordAt(unit, kUnitInterpOff));
            if (!alignedRam(interp))
            {
                return;
            }
            out.frameSpeed = floatAt(interp, kInterpSpeedOff);
            out.logicalFrame = floatAt(interp, kInterpLogicalFrameOff);

            const u32* curCmd = reinterpret_cast<const u32*>(game::wordAt(interp, kInterpCurCmdOff));
            const u32* startCmd = reinterpret_cast<const u32*>(game::wordAt(interp, kInterpStartCmdOff));
            if (alignedRam(curCmd))
            {
                out.cmdHeader = curCmd[0];
                outCmdPtr = curCmd;
            }

            const void* stack = reinterpret_cast<const void*>(game::wordAt(interp, kInterpStackOff));
            if (alignedRam(stack))
            {
                const u32* svt = reinterpret_cast<const u32*>(game::wordAt(stack, 0));
                if (alignedRam(svt))
                {
                    const void* szPtr = reinterpret_cast<const void*>(svt[kStackSizeSlot]);
                    const void* atPtr = reinterpret_cast<const void*>(svt[kStackAtSlot]);
                    if (alignedRam(szPtr) && alignedRam(atPtr))
                    {
                        s32 sz = reinterpret_cast<VSizeFn>(szPtr)(stack);
                        if (sz > 0 && sz < kStackMaxDepth)
                        {
                            // Walk top-down (matching acCmdInterpreter @ 0x8013AD80) to find
                            // the active SyncWait (0) or AsyncWait (7) stack frame.
                            for (s32 k = sz - 1; k >= 0; k--)
                            {
                                const void* entry = reinterpret_cast<VAtFn>(atPtr)(stack, k);
                                if (!alignedRam(entry))
                                {
                                    break;
                                }
                                u32 kind = game::wordAt(entry, kStackKindOff);
                                float val = floatAt(entry, kStackValOff);
                                u8 wk = psaDecodeWait(kind, val, out.logicalFrame, out.waitFrames);
                                if (wk != kPsaWaitNone)
                                {
                                    out.waitKind = wk;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // A thread is active when its cursor points to a live command or it
            // holds a SyncWait/AsyncWait entry on its interpreter stack.
            if (alignedRam(curCmd) && (out.cmdHeader != 0u || out.waitKind != kPsaWaitNone || alignedRam(startCmd)))
            {
                if (out.cmdHeader != 0u || out.waitKind != kPsaWaitNone)
                {
                    out.active = 1;
                }
            }
        }

        bool sampleFighter(const game::FighterRef& ref)
        {
            if (s_net == NULL)
            {
                return false;
            }
            PsaDebugBlock* dbg = &s_net->debug;
            clearSnapshot();
            dbg->playerNo = ref.playerNo;

            const soModuleEnumeration* m = game::fighterModules(ref.fighter);
            if (m == NULL)
            {
                return false;
            }
            dbg->status = game::fighterStatus(m);

            const void* motion = m->m_motionModule;
            if (alignedRam(motion))
            {
                dbg->animFrame = floatAt(motion, kMotionFrameOff);
                dbg->fsmRate = floatAt(motion, kMotionRateOff);
                dbg->subaction = static_cast<s32>(game::wordAt(motion, kMotionKindOff));
                dbg->secondaryKind = static_cast<s32>(game::wordAt(motion, kMotionSecKindOff));
                if (dbg->secondaryKind != -1)
                {
                    dbg->secondaryRate = floatAt(motion, kMotionSecRateOff);
                }
            }

            const void* animCmd = m->m_animCmdModule;
            if (!alignedRam(animCmd))
            {
                return true;
            }
            const void* threads = reinterpret_cast<const void*>(game::wordAt(animCmd, kAnimThreadsOff));
            if (!alignedRam(threads))
            {
                return true;
            }
            const u32* vt = reinterpret_cast<const u32*>(game::wordAt(threads, 0));
            if (!alignedRam(vt))
            {
                return true;
            }
            const void* sizePtr = reinterpret_cast<const void*>(vt[kThreadsSizeSlot]);
            const void* atPtr = reinterpret_cast<const void*>(vt[kThreadsAtFastSlot]);
            if (!alignedRam(sizePtr) || !alignedRam(atPtr))
            {
                return true;
            }

            s32 count = reinterpret_cast<VSizeFn>(sizePtr)(threads);
            if (count < 0)
            {
                count = 0;
            }
            if (count > kPsaThreadCount)
            {
                count = kPsaThreadCount;
            }
            const u32* cmdPtrs[kPsaThreadCount];
            memset(cmdPtrs, 0, sizeof(cmdPtrs));
            u32 activeCount = 0;
            for (s32 i = 0; i < count; i++)
            {
                const void* unit = reinterpret_cast<VAtFn>(atPtr)(threads, i);
                PsaThreadSnapshot snap;
                const u32* cmdPtr = NULL;
                readThread(unit, i, snap, cmdPtr);
                int slot = (snap.id < kPsaThreadCount) ? static_cast<int>(snap.id) : static_cast<int>(i);
                dbg->threads[slot] = snap;
                cmdPtrs[slot] = cmdPtr;
                if (snap.active != 0)
                {
                    activeCount++;
                }
            }
            dbg->activeThreadCount = activeCount;

            // Read relocated acCmdArgConv* pointer (curCmd + 0x04; verified at
            // acAnimCmdImpl::getArg 0x8013CAB8) for the focused active thread.
            int focus = psaSelectFocusThread(dbg->threads);
            dbg->focusThread = focus;
            if (focus >= 0 && focus < kPsaThreadCount)
            {
                u32 hdr = dbg->threads[focus].cmdHeader;
                dbg->focusCmdHeader = hdr;
                u8 numArgs = psaDecodeCmdHeader(hdr).numArgs;
                u32 want = numArgs < kPsaMaxArgs ? static_cast<u32>(numArgs) : static_cast<u32>(kPsaMaxArgs);
                const u32* cmdPtr = cmdPtrs[focus];
                u32 got = 0;
                if (want > 0u && alignedRam(cmdPtr))
                {
                    const u32* argsPtr = reinterpret_cast<const u32*>(cmdPtr[1]);
                    if (alignedRam(argsPtr))
                    {
                        for (u32 k = 0; k < want; k++)
                        {
                            const u32* entry = argsPtr + k * 2u;
                            if (!alignedRam(entry))
                            {
                                break;
                            }
                            dbg->focusArgs[got].type = entry[0];
                            dbg->focusArgs[got].content = entry[1];
                            got++;
                        }
                    }
                }
                dbg->focusArgCount = got;
                PsaFocusCache* fc = &s_net->focusCache;
                bool hit = fc->valid && fc->hdr == hdr && fc->argCount == got;
                for (u32 k = 0; hit && k < got; k++)
                {
                    if (fc->args[k].type != dbg->focusArgs[k].type ||
                        fc->args[k].content != dbg->focusArgs[k].content)
                    {
                        hit = false;
                    }
                }
                if (hit)
                {
                    memcpy(dbg->focusText, fc->text, sizeof(dbg->focusText));
                }
                else
                {
                    psaFormatFocusCommand(hdr, dbg->focusArgs, got, dbg->focusText,
                                          static_cast<int>(sizeof(dbg->focusText)));
                    fc->hdr = hdr;
                    fc->argCount = got;
                    if (got > 0u)
                    {
                        memcpy(fc->args, dbg->focusArgs, got * sizeof(PsaArgSnapshot));
                    }
                    memcpy(fc->text, dbg->focusText, sizeof(fc->text));
                    fc->valid = true;
                }
            }
            return true;
        }

        void drawCard(const ui::layout::Box& b, bool isCpu, bool stackedUnderInfo)
        {
            const PsaDebugBlock* dbg = &s_net->debug;
            float xs = gfx::xScale();
            float x = b.x0;
            float y = b.y0;
            float bw = b.x1 - b.x0;
            int shown[kPsaDisplayRows];
            int rowCount = psaSelectDisplayThreads(dbg->threads, shown, kPsaDisplayRows);
            bool hasFocus = dbg->focusText[0] != '\0';
            int fitRows = rowCount > 0 ? rowCount : 1;
            float maxBh = b.y1 - b.y0;
            float rowPitch = 12.0f;
            float rowsTop = 32.0f;
            float rowsBot = rowsTop + i2fScale(fitRows, rowPitch);
            float bh = rowsBot + (hasFocus ? 16.0f : 5.0f);
            if (bh > maxBh)
            {
                bh = maxBh;
            }

            // Quiet borderless Panel B smoked plate (zero window border, zero colored title tab).
            drawSmokedPlate(x, y, bw, bh);
            if (stackedUnderInfo)
            {
                drawPlateDivider(x, y, bw, xs);
            }

            float insetX = 8.0f * xs;
            float colName = x + insetX;
            float innerW = bw - 2.0f * insetX;

            // Header line 1: subtle target + FSM rate (>= 10.5 scanlines, crisp 1px drop shadow).
            char title[16];
            char fsm[20];
            psaFormatTitle(isCpu, dbg->playerNo, title, sizeof(title));
            psaFormatFsm(dbg->fsmRate, fsm, sizeof(fsm));
            float hdrSc = ui::style::scaleForCap(10.5f);
            float hdrY = ui::style::textYForCap(y + 5.0f, hdrSc);
            drawShadowText(colName, hdrY, title, 0xC6D4E0FFu, hdrSc);
            gfx::Rgba fsmCol = psaFsmIsModified(dbg->fsmRate) ? 0xFFD300FFu : 0xF4F8FCFFu;
            float fsmW = gfx::textWidth(fsm, hdrSc);
            drawShadowText(x + bw - insetX - fsmW, hdrY, fsm, fsmCol, hdrSc);

            // Header line 2: Action / Subaction / AnimFrame.
            char subLine[40];
            psaFormatSubHeader(dbg->status, dbg->subaction, dbg->animFrame, dbg->secondaryKind,
                               dbg->secondaryRate, subLine, sizeof(subLine));
            float subSc = ui::style::scaleForCap(10.5f);
            float subW = gfx::textWidth(subLine, subSc);
            float subSx = (subW > innerW && subW > 0.0f) ? (subSc * innerW / subW) : subSc;
            float subY = ui::style::textYForCap(y + 18.0f, subSc);
            gfx::text(colName + xs, subY + 1.0f, subLine, 0x000000C8u, subSx, subSc);
            gfx::text(colName, subY, subLine, 0xD8E6F0FFu, subSx, subSc);

            drawPlateDivider(x, y + 29.0f, bw, xs);

            // Active threads: Name | Frame | Wait | Decoded Opcode Name (fit to <= 218 px on 4:3).
            float rowScale = ui::style::scaleForCap(10.5f);
            if (rowCount == 0)
            {
                drawShadowText(colName, ui::style::textYForCap(y + rowsTop, rowScale), "No active threads",
                               0x96AAB8FFu, rowScale);
                return;
            }

            float colFrame = x + 64.0f * xs;
            float colWait = x + 104.0f * xs;
            float colOp = x + 142.0f * xs;
            float opMaxW = (x + bw - insetX) - colOp;
            float nameMaxW = colFrame - colName - 3.0f * xs;
            float frameMaxW = colWait - colFrame - 3.0f * xs;
            float waitMaxW = colOp - colWait - 3.0f * xs;

            int maxRows = static_cast<int>((bh - rowsTop - (hasFocus ? 16.0f : 4.0f)) / rowPitch);
            if (maxRows < 1)
            {
                maxRows = 1;
            }
            int drawRows = rowCount < maxRows ? rowCount : maxRows;
            for (int r = 0; r < drawRows; r++)
            {
                int idx = shown[r];
                const PsaThreadSnapshot& th = dbg->threads[idx];
                float ry = ui::style::textYForCap(y + rowsTop + i2fScale(r, rowPitch), rowScale);

                char frameBuf[12];
                char waitBuf[12];
                char opBuf[20];
                psaFormatFrame(th.logicalFrame, frameBuf, sizeof(frameBuf));
                psaFormatWait(th.waitKind, th.waitFrames, waitBuf, sizeof(waitBuf));
                psaFormatOpcodeName(th.cmdHeader, opBuf, sizeof(opBuf));
                clipTextToWidth(opBuf, rowScale, opMaxW);

                gfx::Rgba nameCol = (idx == dbg->focusThread) ? 0xFFE24AFFu
                                                               : ((idx <= 1) ? 0xF4F8FCFFu : 0xC6D4E0FFu);
                static const gfx::Rgba kWaitCols[3] = {0x8094A4FFu, 0x36CFC2FFu, 0xFFE24AFFu};
                gfx::Rgba waitCol = kWaitCols[th.waitKind < 3u ? th.waitKind : 0u];
                gfx::Rgba opCol = th.cmdHeader != 0u ? 0xE2EEF6FFu : 0x8094A4FFu;

                const char* nm = psaThreadName(idx);
                float nW = gfx::textWidth(nm, rowScale);
                float nSx = (nW > nameMaxW && nW > 0.0f) ? (rowScale * nameMaxW / nW) : rowScale;
                gfx::text(colName + xs, ry + 1.0f, nm, 0x000000C8u, nSx, rowScale);
                gfx::text(colName, ry, nm, nameCol, nSx, rowScale);

                float fW = gfx::textWidth(frameBuf, rowScale);
                float fSx = (fW > frameMaxW && fW > 0.0f) ? (rowScale * frameMaxW / fW) : rowScale;
                gfx::text(colFrame + xs, ry + 1.0f, frameBuf, 0x000000C8u, fSx, rowScale);
                gfx::text(colFrame, ry, frameBuf, 0xC6D8E0FFu, fSx, rowScale);

                float wW = gfx::textWidth(waitBuf, rowScale);
                float wSx = (wW > waitMaxW && wW > 0.0f) ? (rowScale * waitMaxW / wW) : rowScale;
                gfx::text(colWait + xs, ry + 1.0f, waitBuf, 0x000000C8u, wSx, rowScale);
                gfx::text(colWait, ry, waitBuf, waitCol, wSx, rowScale);

                drawShadowText(colOp, ry, opBuf, opCol, rowScale);
            }

            if (hasFocus)
            {
                float divY = y + rowsTop + i2fScale(drawRows, rowPitch) + 1.0f;
                if (divY + 13.0f <= y + bh)
                {
                    drawPlateDivider(x, divY, bw, xs);
                    char focusLine[56];
                    snprintf(focusLine, sizeof(focusLine), "%s:%s", psaThreadName(dbg->focusThread),
                             dbg->focusText);
                    clipTextToWidth(focusLine, rowScale, innerW);
                    drawShadowText(colName, ui::style::textYForCap(divY + 3.0f, rowScale), focusLine, 0x8EF5EAFFu,
                                   rowScale);
                }
            }
        }
    }

    void psaInspectorAttach()
    {
        psaOpcodesAttach();
        freeNetPtr(s_net);
        s_net = allocNetArray<PsaNet>(1);
        if (s_net == NULL)
        {
            return;
        }
        clearSnapshot();
        TM_LOG("psa: debug block at %08x\n", &s_net->debug);
    }

    void psaInspectorDetach()
    {
        clearSnapshot();
        freeNetPtr(s_net);
        psaOpcodesDetach();
    }

    void psaInspectorDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_net == NULL)
        {
            return;
        }
        PsaDebugBlock* dbg = &s_net->debug;
        u8 target = main::settings().display.psaInspector;
        if (!match.running || match.paused || menuOpen || target == main::kPsaOff || target >= main::kPsaTargetCount)
        {
            dbg->playerNo = -1;
            dbg->activeThreadCount = 0;
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef chosen;
        if (!selectTargetFighter(target, refs, n, chosen))
        {
            clearSnapshot();
            return;
        }
        if (!sampleFighter(chosen))
        {
            return;
        }
        bool wide = gfx::widescreen();
        int viewerSlots = match.training ? countInputViewerSlots(refs, n) : 0;
        ui::layout::Box box = hudRightCardBox(kHudCardPsa, wide, viewerSlots, 0);
        float infoBot = hudInfoStackBottom(wide);
        bool stacked = (infoBot > 0.0f && box.x0 < 320.0f);
        drawCard(box, target == main::kPsaCpu, stacked);
        dbg->drawCalls++;
    }
}
}
