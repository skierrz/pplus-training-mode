#pragma once

#include <cstdio>
#include <types.h>

#include "shared/interface.h"
#include "shared/version.h"

// Pure logic for the read-only PSA AnimCmd 11-Thread & FSM Inspector.
// Header-only and free of Brawl/Wii headers so host unit tests exercise the
// exact same decoding, thread selection, and formatting used in-game.
// Concept adapted from fudgepop01's SuperTraining / FudgeMenu PSA inspector
// (used with express permission; see CREDITS.md).
namespace tm {
namespace extra {
    enum {
        kPsaDebugMagic = 0x544D5053u, // "TMPS"
        kPsaThreadCount = 11,
        kPsaDisplayRows = 6,
        kPsaWaitTagSync = 0u,
        kPsaWaitTagAsync = 1u,
        kPsaWaitTagAsyncDone = 7u
    };

    enum PsaWaitKind {
        kPsaWaitNone = 0,
        kPsaWaitSync = 1,
        kPsaWaitAsync = 2
    };

    struct PsaCmdHeader {
        u8 module;
        u8 code;
        u8 numArgs;
        u8 option;
    };

    struct PsaThreadSnapshot {
        u32 id;             // +0x00 thread index 0..10
        s16 type;           // +0x04 soAnimCmdControlUnit +0x0C
        u8 active;          // +0x06 1 when running or waiting
        u8 waitKind;        // +0x07 PsaWaitKind (0 none, 1 SyncWait, 2 AsyncWait)
        u32 cmdHeader;      // +0x08 acAnimCmd[0] header word
        float logicalFrame; // +0x0C acCmdInterpreter +0x24
        float frameSpeed;   // +0x10 acCmdInterpreter +0x08
        float waitFrames;   // +0x14 remaining SyncWait or AsyncWait frames
    };

    struct PsaDebugBlock {
        u32 magic;             // +0x00 "TMPS" (0x544D5053)
        u32 drawCalls;         // +0x04 incremented on each rendered frame
        s32 playerNo;          // +0x08 target playerNo (0..3), -1 when off/none
        s32 status;            // +0x0C fighter status (action) id
        s32 subaction;         // +0x10 soMotionModuleImpl m_kind (subaction) id
        float animFrame;       // +0x14 soMotionModuleImpl +0x40
        float fsmRate;         // +0x18 soMotionModuleImpl +0x4C (main rate)
        float secondaryRate;   // +0x1C soMotionModuleImpl +0x7C (when secondaryKind != -1)
        s32 secondaryKind;     // +0x20 soMotionModuleImpl +0x88 (-1 when inactive)
        u32 activeThreadCount; // +0x24 count of active threads out of 11
        PsaThreadSnapshot threads[kPsaThreadCount]; // +0x28 (11 * 24 = 264 bytes)
    };

    TM_STATIC_ASSERT(sizeof(PsaThreadSnapshot) == 0x18, tm_psa_thread_snapshot_size);
    TM_STATIC_ASSERT(sizeof(PsaDebugBlock) == 0x130, tm_psa_debug_block_size);

    inline PsaCmdHeader psaDecodeCmdHeader(u32 hdr)
    {
        PsaCmdHeader out;
        out.module = static_cast<u8>((hdr >> 24) & 0xFFu);
        out.code = static_cast<u8>((hdr >> 16) & 0xFFu);
        out.numArgs = static_cast<u8>((hdr >> 8) & 0xFFu);
        out.option = static_cast<u8>(hdr & 0xFFu);
        return out;
    }

    // Decodes an acCmdStackEntry wait timer:
    //  - tag 0 (SyncWait, 0x80139870): remaining frames directly at +0x10
    //  - tag 1 (AsyncWait active, 0x8013AF0C) or tag 7 (AsyncWait marker,
    //    0x8013ADFC): target logical frame at +0x10; remaining = target - logicalFrame.
    inline u8 psaDecodeWait(u32 stackEntryKind, float stackVal, float logicalFrame, float& outWait)
    {
        if (stackEntryKind == kPsaWaitTagSync)
        {
            outWait = (stackVal == stackVal && stackVal > 0.0f) ? stackVal : 0.0f;
            return kPsaWaitSync;
        }
        if (stackEntryKind == kPsaWaitTagAsync || stackEntryKind == kPsaWaitTagAsyncDone)
        {
            float rem = stackVal - logicalFrame;
            if (rem == rem && rem > 0.0f)
            {
                outWait = rem;
                return kPsaWaitAsync;
            }
            if (stackEntryKind == kPsaWaitTagAsync)
            {
                outWait = 0.0f;
                return kPsaWaitAsync;
            }
        }
        outWait = 0.0f;
        return kPsaWaitNone;
    }

    inline float psaClampDisplay(float v, float lo, float hi)
    {
        if (!(v == v))
        {
            return lo;
        }
        if (v < lo)
        {
            return lo;
        }
        if (v > hi)
        {
            return hi;
        }
        return v;
    }

    inline const char* psaThreadName(int index)
    {
        static const char* const kThreadNames[kPsaThreadCount] = {
            "ActMain", "SubMain", "SubGfx", "SubSfx", "SubOth",
            "Alt0",    "Alt1",    "Alt2",   "Alt3",   "Loop",   "ActHid"
        };
        if (index >= 0 && index < kPsaThreadCount)
        {
            return kThreadNames[index];
        }
        return "Thr?";
    }

    // Selects up to `maxOut` active thread indices in display priority order:
    // ActMain, SubMain, SubGfx, SubSfx, SubOth, Loop, ActHid, Alt0..3.
    inline int psaSelectDisplayThreads(const PsaThreadSnapshot* threads, int* outIndices, int maxOut)
    {
        static const u8 kPriority[kPsaThreadCount] = {0, 1, 2, 3, 4, 9, 10, 5, 6, 7, 8};
        int count = 0;
        for (int i = 0; i < kPsaThreadCount && count < maxOut; i++)
        {
            int idx = kPriority[i];
            if (threads[idx].active != 0)
            {
                outIndices[count++] = idx;
            }
        }
        return count;
    }

    inline bool psaFsmIsModified(float fsmRate)
    {
        float d = fsmRate - 1.0f;
        return !(fsmRate == fsmRate) || d > 0.005f || d < -0.005f;
    }

    inline void psaFormatTitle(bool isCpu, int playerNo, char* out, int outSize)
    {
        if (isCpu)
        {
            snprintf(out, static_cast<size_t>(outSize), "PSA CPU");
        }
        else
        {
            int p = (playerNo >= 0 && playerNo < 4) ? (playerNo + 1) : 1;
            snprintf(out, static_cast<size_t>(outSize), "PSA P%d", p);
        }
    }

    inline void psaFormatFsm(float fsmRate, char* out, int outSize)
    {
        float r = psaClampDisplay(fsmRate, 0.0f, 99.99f);
        snprintf(out, static_cast<size_t>(outSize), "FSM x%.2f", static_cast<double>(r));
    }

    inline void psaFormatSubHeader(s32 status, s32 subaction, float animFrame, s32 secondaryKind,
                                   float secondaryRate, char* out, int outSize)
    {
        float f = psaClampDisplay(animFrame, 0.0f, 999.9f);
        u32 st = status >= 0 ? (static_cast<u32>(status) & 0xFFFu) : 0u;
        u32 sub = subaction >= 0 ? (static_cast<u32>(subaction) & 0xFFFu) : 0u;
        if (secondaryKind != -1)
        {
            float sr = psaClampDisplay(secondaryRate, 0.0f, 99.99f);
            snprintf(out, static_cast<size_t>(outSize), "A:%03X S:%03X f%.1f 2x%.2f",
                     static_cast<unsigned>(st), static_cast<unsigned>(sub), static_cast<double>(f),
                     static_cast<double>(sr));
        }
        else
        {
            snprintf(out, static_cast<size_t>(outSize), "A:%03X S:%03X f%.1f",
                     static_cast<unsigned>(st), static_cast<unsigned>(sub), static_cast<double>(f));
        }
    }

    inline void psaFormatFrame(float logicalFrame, char* out, int outSize)
    {
        float f = psaClampDisplay(logicalFrame, 0.0f, 999.9f);
        snprintf(out, static_cast<size_t>(outSize), "f%.1f", static_cast<double>(f));
    }

    inline void psaFormatWait(u8 waitKind, float waitFrames, char* out, int outSize)
    {
        if (waitKind == kPsaWaitSync)
        {
            float w = psaClampDisplay(waitFrames, 0.0f, 99.9f);
            snprintf(out, static_cast<size_t>(outSize), "w%.1f", static_cast<double>(w));
        }
        else if (waitKind == kPsaWaitAsync)
        {
            float w = psaClampDisplay(waitFrames, 0.0f, 99.9f);
            snprintf(out, static_cast<size_t>(outSize), "a%.1f", static_cast<double>(w));
        }
        else
        {
            snprintf(out, static_cast<size_t>(outSize), "-");
        }
    }

    inline void psaFormatOpcode(u32 cmdHeader, char* out, int outSize)
    {
        PsaCmdHeader h = psaDecodeCmdHeader(cmdHeader);
        snprintf(out, static_cast<size_t>(outSize), "%02X:%02X", static_cast<unsigned>(h.module),
                 static_cast<unsigned>(h.code));
    }
}
}
