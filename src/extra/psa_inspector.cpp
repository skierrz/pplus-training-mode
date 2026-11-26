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

        PsaDebugBlock* s_debug = NULL;

        float floatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        s16 s16At(const void* base, u32 offset)
        {
            return *reinterpret_cast<const s16*>(reinterpret_cast<u32>(base) + offset);
        }

        bool alignedRam(const void* p)
        {
            return game::isRam(p) && (reinterpret_cast<u32>(p) & 3u) == 0u;
        }

        void clearSnapshot()
        {
            if (s_debug == NULL)
            {
                return;
            }
            u32 calls = s_debug->drawCalls;
            memset(s_debug, 0, sizeof(*s_debug));
            s_debug->magic = kPsaDebugMagic;
            s_debug->drawCalls = calls;
            s_debug->playerNo = -1;
            s_debug->status = -1;
            s_debug->subaction = -1;
            s_debug->secondaryKind = -1;
            s_debug->focusThread = -1;
        }

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
            if (s_debug == NULL)
            {
                return false;
            }
            clearSnapshot();
            s_debug->playerNo = ref.playerNo;

            const soModuleEnumeration* m = game::fighterModules(ref.fighter);
            if (m == NULL)
            {
                return false;
            }
            s_debug->status = game::fighterStatus(m);

            const void* motion = m->m_motionModule;
            if (alignedRam(motion))
            {
                s_debug->animFrame = floatAt(motion, kMotionFrameOff);
                s_debug->fsmRate = floatAt(motion, kMotionRateOff);
                s_debug->subaction = static_cast<s32>(game::wordAt(motion, kMotionKindOff));
                s_debug->secondaryKind = static_cast<s32>(game::wordAt(motion, kMotionSecKindOff));
                if (s_debug->secondaryKind != -1)
                {
                    s_debug->secondaryRate = floatAt(motion, kMotionSecRateOff);
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
                s_debug->threads[slot] = snap;
                cmdPtrs[slot] = cmdPtr;
                if (snap.active != 0)
                {
                    activeCount++;
                }
            }
            s_debug->activeThreadCount = activeCount;

            // Read relocated acCmdArgConv* pointer (curCmd + 0x04; verified at
            // acAnimCmdImpl::getArg 0x8013CAB8) for the focused active thread.
            int focus = psaSelectFocusThread(s_debug->threads);
            s_debug->focusThread = focus;
            if (focus >= 0 && focus < kPsaThreadCount)
            {
                u32 hdr = s_debug->threads[focus].cmdHeader;
                s_debug->focusCmdHeader = hdr;
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
                            s_debug->focusArgs[got].type = entry[0];
                            s_debug->focusArgs[got].content = entry[1];
                            got++;
                        }
                    }
                }
                s_debug->focusArgCount = got;
                psaFormatFocusCommand(hdr, s_debug->focusArgs, got, s_debug->focusText,
                                      static_cast<int>(sizeof(s_debug->focusText)));
            }
            return true;
        }

        int countActiveViewerSlots(const game::FighterRef* refs, int n)
        {
            u8 who = main::settings().display.inputViewer;
            if (who == display::kWhoNone)
            {
                return 0;
            }
            int slots = 0;
            for (int i = 0; i < n && slots < 2; i++)
            {
                bool cpu = display::cpuPlayer(refs[i]);
                if ((who & (cpu ? display::kWhoCpu : display::kWhoHuman)) != 0)
                {
                    slots++;
                }
            }
            return slots;
        }

        void clipToWidth(char* buf, float scale, float maxW)
        {
            if (gfx::textWidth(buf, scale) <= maxW)
            {
                return;
            }
            int len = static_cast<int>(strlen(buf));
            while (len > 3)
            {
                len--;
                buf[len - 2] = '.';
                buf[len - 1] = '.';
                buf[len] = '\0';
                if (gfx::textWidth(buf, scale) <= maxW)
                {
                    return;
                }
            }
        }

        void drawCard(const ui::layout::Box& b, bool isCpu)
        {
            float xs = gfx::xScale();
            float x = b.x0;
            float y = b.y0;
            float bw = b.x1 - b.x0;
            int shown[kPsaDisplayRows];
            int rowCount = psaSelectDisplayThreads(s_debug->threads, shown, kPsaDisplayRows);
            bool hasFocus = s_debug->focusText[0] != '\0';
            int fitRows = rowCount > 0 ? rowCount : 1;
            float maxBh = b.y1 - b.y0;
            float rowsBot = 28.0f + static_cast<float>(fitRows) * 10.0f;
            float bh = rowsBot + (hasFocus ? 16.0f : 4.0f);
            if (bh > maxBh)
            {
                bh = maxBh;
            }

            drawHudCard(x, y, bw, bh, xs, isCpu ? 0x4E86B8FFu : 0xD83838FFu, 0xFFFFFF40u);

            // Crisp 2.0px integer header divider below Act/Sub/AnimFrame line.
            gfx::fillRect(x + 4.0f * xs, y + 24.0f, bw - 8.0f * xs, 2.0f, 0x2A4C58C0u);

            // Header line 1: PSA target + FSM rate.
            char title[16];
            char fsm[20];
            psaFormatTitle(isCpu, s_debug->playerNo, title, sizeof(title));
            psaFormatFsm(s_debug->fsmRate, fsm, sizeof(fsm));
            float hdrScale = ui::style::scaleForCap(6.5f);
            float hdrY = ui::style::textYForCap(y + 6.0f, hdrScale);
            drawShadowText(x + 6.0f * xs, hdrY, title, isCpu ? 0x9AD0F8FFu : 0xFFB0B0FFu, hdrScale);
            gfx::Rgba fsmCol = psaFsmIsModified(s_debug->fsmRate) ? 0xFFD300FFu : 0xF1F5F9FFu;
            drawShadowText(x + 92.0f * xs, hdrY, fsm, fsmCol, hdrScale);

            // Header line 2: Action / Subaction / AnimFrame (+ secondary rate if active).
            char subLine[40];
            psaFormatSubHeader(s_debug->status, s_debug->subaction, s_debug->animFrame, s_debug->secondaryKind,
                               s_debug->secondaryRate, subLine, sizeof(subLine));
            float subScale = ui::style::scaleForCap(6.0f);
            float subY = ui::style::textYForCap(y + 16.0f, subScale);
            drawShadowText(x + 6.0f * xs, subY, subLine, 0xB8CDD6FFu, subScale);

            // Up to 6 active threads: Name | Frame | Wait | Decoded Opcode Name.
            float rowScale = ui::style::scaleForCap(6.0f);
            if (rowCount == 0)
            {
                float ry = ui::style::textYForCap(y + 29.0f, rowScale);
                drawShadowText(x + 6.0f * xs, ry, "No active threads", 0x7A8E9CFFu, rowScale);
                return;
            }

            float colName = x + 6.0f * xs;
            float colFrame = x + 49.0f * xs;
            float colWait = x + 80.0f * xs;
            float colOp = x + 111.0f * xs;
            float opMaxW = (x + bw - 6.0f * xs) - colOp;

            for (int r = 0; r < rowCount; r++)
            {
                int idx = shown[r];
                const PsaThreadSnapshot& th = s_debug->threads[idx];
                float ry = ui::style::textYForCap(y + 28.0f + static_cast<float>(r) * 10.0f, rowScale);

                char frameBuf[12];
                char waitBuf[12];
                char opBuf[20];
                psaFormatFrame(th.logicalFrame, frameBuf, sizeof(frameBuf));
                psaFormatWait(th.waitKind, th.waitFrames, waitBuf, sizeof(waitBuf));
                psaFormatOpcodeName(th.cmdHeader, opBuf, sizeof(opBuf));
                clipToWidth(opBuf, rowScale, opMaxW);

                gfx::Rgba nameCol = (idx == s_debug->focusThread) ? 0xFFE24AFFu
                                                                   : ((idx <= 1) ? 0xF1F5F9FFu : 0xB8CDD6FFu);
                gfx::Rgba waitCol = 0x6E8694FFu;
                if (th.waitKind == kPsaWaitSync)
                {
                    waitCol = 0x36CFC2FFu;
                }
                else if (th.waitKind == kPsaWaitAsync)
                {
                    waitCol = 0xFFE24AFFu;
                }
                gfx::Rgba opCol = th.cmdHeader != 0u ? 0xD8E8F0FFu : 0x6E8694FFu;

                drawShadowText(colName, ry, psaThreadName(idx), nameCol, rowScale);
                drawShadowText(colFrame, ry, frameBuf, 0xC6D8E0FFu, rowScale);
                drawShadowText(colWait, ry, waitBuf, waitCol, rowScale);
                drawShadowText(colOp, ry, opBuf, opCol, rowScale);
            }

            // Focused command + typed arguments footer strip with 2.0px divider.
            if (hasFocus)
            {
                float divY = y + rowsBot + 1.0f;
                if (divY + 11.0f <= y + bh)
                {
                    gfx::fillRect(x + 4.0f * xs, divY, bw - 8.0f * xs, 2.0f, 0x2A4C58C0u);
                    char focusLine[56];
                    snprintf(focusLine, sizeof(focusLine), "%s:%s", psaThreadName(s_debug->focusThread),
                             s_debug->focusText);
                    clipToWidth(focusLine, rowScale, bw - 12.0f * xs);
                    float fy = ui::style::textYForCap(divY + 4.0f, rowScale);
                    drawShadowText(colName, fy, focusLine, 0x8EF5EAFFu, rowScale);
                }
            }
        }
    }

    void psaInspectorAttach()
    {
        psaOpcodesAttach();
        freeNetPtr(s_debug);
        s_debug = allocNetArray<PsaDebugBlock>(1);
        if (s_debug == NULL)
        {
            return;
        }
        memset(s_debug, 0, sizeof(*s_debug));
        s_debug->magic = kPsaDebugMagic;
        s_debug->playerNo = -1;
        s_debug->status = -1;
        s_debug->subaction = -1;
        s_debug->secondaryKind = -1;
        s_debug->focusThread = -1;
        TM_LOG("psa: debug block at %08x\n", s_debug);
    }

    void psaInspectorDetach()
    {
        clearSnapshot();
        freeNetPtr(s_debug);
        psaOpcodesDetach();
    }

    void psaInspectorDraw(const game::MatchState& match, bool menuOpen)
    {
        if (s_debug == NULL)
        {
            return;
        }
        u8 target = main::settings().display.psaInspector;
        if (!match.running || match.paused || menuOpen || target == main::kPsaOff || target >= main::kPsaTargetCount)
        {
            s_debug->playerNo = -1;
            s_debug->activeThreadCount = 0;
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
        int viewerSlots = match.training ? countActiveViewerSlots(refs, n) : 0;
        bool hasCompass = match.training && diCompassVisible(match, menuOpen);
        ui::layout::Box box = ui::layout::psaInspectorBox(ui::layout::topBand(), viewerSlots, hasCompass,
                                                          gfx::widescreen());
        drawCard(box, target == main::kPsaCpu);
        s_debug->drawCalls++;
    }
}
}
