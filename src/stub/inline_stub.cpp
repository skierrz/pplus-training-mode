// Checks the stubs sy_core writes for inline hooks, and repairs the r0/LR
// restore order of the 3.2.1 build's stubs.
#include "stub/inline_stub.h"

#include "shared/log.h"
#include "stub/ppc.h"

namespace tm {
namespace inline_stub {
    // Stub layout as observed in memory on 3.2.1 (sy_core's own hooks at
    // 0x80026DB4 and 0x800272E0 use it too). The hooked site branches to
    // word 0, which holds the instruction that was replaced:
    //   0  original instruction
    //   1  stwu r1,-0x90(r1)
    //   2  stw r0,0x8(r1)         saves r0
    //   3  stmw r3,0xC(r1)
    //   4  mflr r0
    //   5  stw r0,0x94(r1)        saves LR
    //   6  bl <hook>
    //   7  lwz r0,...  \
    //   8  mtlr r0      > restore; see below
    //   9  lwz r0,...  /
    //  10  lmw r3,0xC(r1)
    //  11  addi r1,r1,0x90
    //  12  b <site + 4>
    // Words 7 and 9 must load LR's slot (0x94) first and r0's (0x8) last;
    // the 3.2.1 build has them the other way round, so hooked code resumes
    // with r0 and LR exchanged. Swapping the two words repairs it.
    // Fault and repair are Brawl Online's finding (its PPOnline loader,
    // game-code/PPOnlineLoader/source/loader.cpp in its repository); only
    // the method is taken, the check and the fix are the mod's own.
    enum {
        kWords = 13,
        kLoadSavedR0 = 0x80010008, // lwz r0,0x8(r1)
        kLoadSavedLr = 0x80010094  // lwz r0,0x94(r1)
    };

    static const u32 kFixedWords[kWords] = {
        0,          // original instruction (per site)
        0x9421FF70, 0x90010008, 0xBC61000C, 0x7C0802A6, 0x90010094,
        0,          // bl <hook> (per hook)
        0,          // restore, checked separately
        0x7C0803A6, // mtlr r0
        0,          // restore, checked separately
        0xB861000C, 0x38210090,
        0           // b <site + 4> (per site)
    };

    bool belongsTo(const u32* stub, u32 site, u32 original)
    {
        if (stub[0] != original || !ppc::isBranchLink(stub[6]) || !ppc::isBranch(stub[12]))
        {
            return false;
        }
        if (ppc::branchTarget(reinterpret_cast<u32>(&stub[12]), stub[12]) != site + 4)
        {
            return false;
        }
        for (int i = 1; i < kWords; i++)
        {
            if (kFixedWords[i] != 0 && stub[i] != kFixedWords[i])
            {
                return false;
            }
        }
        return true;
    }

    Result checkAndFix(u32* stub, u32 site)
    {
        TM_LOG("inline stub for %08x at %08x: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
               site, stub, stub[0], stub[1], stub[2], stub[3], stub[4], stub[5], stub[6],
               stub[7], stub[8], stub[9], stub[10], stub[11], stub[12]);

        if (!belongsTo(stub, site, stub[0]))
        {
            return kUnknown;
        }
        if (stub[7] == kLoadSavedLr && stub[9] == kLoadSavedR0)
        {
            return kCorrect;
        }
        if (stub[7] != kLoadSavedR0 || stub[9] != kLoadSavedLr)
        {
            return kUnknown;
        }

        // The hook has not run yet (we are still in _prolog), so nothing can
        // be executing these words while they change.
        stub[7] = kLoadSavedLr;
        stub[9] = kLoadSavedR0;
        ppc::flushCode(&stub[7], 3 * sizeof(u32));
        return (stub[7] == kLoadSavedLr && stub[9] == kLoadSavedR0) ? kFixed : kUnknown;
    }

    const char* describe(Result result)
    {
        switch (result)
        {
        case kCorrect:
            return "restore order correct, no fix needed";
        case kFixed:
            return "swapped r0/LR restore found and fixed";
        default:
            return "unrecognised layout";
        }
    }
}
}
