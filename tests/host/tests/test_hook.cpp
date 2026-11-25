// Inline hook installation and cache maintenance (src/stub/hook.cpp and
// src/stub/inline_stub.cpp): sy_core writes a 13-word trampoline in MEM1 and a
// branch at the hooked word, then calls only ICInvalidateRange(site, 4). After
// installing a hook, hook::installInline must flush both the whole trampoline
// and the patched word from D-cache and invalidate them from I-cache.
#include <OS/OSCache.h>
#include <sy_core.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"
#include "stub/hook.h"

using namespace tm;

namespace {
    const u32 kSiteAddr = 0x8002A274u;
    const u32 kStubAddr = 0x81060F4Cu; // +0x0C mod 32, matching live sy_core allocations
    const u32 kEntryAddr = 0x817C0100u;
    const u32 kSiteWords[3] = {0x3B9F0644u, 0x3BBF0244u, 0x3B600000u};
    const u32 kStubBytes = 13u * sizeof(u32);

    u32 encodeBranch(u32 from, u32 to, bool link)
    {
        return (link ? 0x48000001u : 0x48000000u) | ((to - from) & 0x03FFFFFCu);
    }

    enum StubKind { kSwapped321, kCorrectOrder, kUnknownLayout };

    // Mimics P+ 3.2.1's sy_core.rel _inlineHook (0x817c9b90): writes the
    // 13-word stub and patches site, calling only ICInvalidateRange(site, 4).
    class FakeCoreApi : public CoreApi {
    public:
        StubKind kind;
        u32 stubAddr;
        int calls;

        FakeCoreApi(StubKind k, u32 s) : kind(k), stubAddr(s), calls(0) {}

        void syInlineHook(u32 address, const void* replacement)
        {
            calls++;
            u32 target = reinterpret_cast<u32>(replacement);
            u32* stub = reinterpret_cast<u32*>(stubAddr);
            u32 original = host::mem::word(address);
            stub[0] = original;
            stub[1] = 0x9421FF70u; // stwu r1,-0x90(r1)
            stub[2] = 0x90010008u; // stw r0,0x8(r1)
            stub[3] = 0xBC61000Cu; // stmw r3,0xC(r1)
            stub[4] = 0x7C0802A6u; // mflr r0
            stub[5] = 0x90010094u; // stw r0,0x94(r1)
            stub[6] = encodeBranch(stubAddr + 6 * 4, target, true);
            if (kind == kSwapped321)
            {
                stub[7] = 0x80010008u; // lwz r0,0x8(r1)  (swapped)
                stub[8] = 0x7C0803A6u; // mtlr r0
                stub[9] = 0x80010094u; // lwz r0,0x94(r1) (swapped)
            }
            else if (kind == kCorrectOrder)
            {
                stub[7] = 0x80010094u; // lwz r0,0x94(r1)
                stub[8] = 0x7C0803A6u; // mtlr r0
                stub[9] = 0x80010008u; // lwz r0,0x8(r1)
            }
            else
            {
                stub[7] = 0x60000000u; // nop (unrecognised)
                stub[8] = 0x7C0803A6u;
                stub[9] = 0x60000000u;
            }
            stub[10] = 0xB861000Cu; // lmw r3,0xC(r1)
            stub[11] = 0x38210090u; // addi r1,r1,0x90
            stub[12] = encodeBranch(stubAddr + 12 * 4, address + 4, false);

            host::mem::setWord(address, encodeBranch(address, stubAddr, false));
            // sy_core omits DCFlushRange on site and both flushes on stub.
            ICInvalidateRange(reinterpret_cast<void*>(address), sizeof(u32));
        }
    };

    void seedSite()
    {
        for (u32 i = 0; i < 3; i++)
        {
            host::mem::setWord(kSiteAddr + i * 4, kSiteWords[i]);
        }
    }

    // True if a cache op of `icache` covers [addr, addr + bytes) in full, and
    // occurred after `afterIndex` (-1 for any). Returns its index, or -1.
    int findFlush(bool icache, u32 addr, u32 bytes, int afterIndex = -1)
    {
        const host::fakes::CacheOp* ops = host::fakes::cacheOps();
        int n = host::fakes::cacheOpCount();
        for (int i = afterIndex + 1; i < n; i++)
        {
            if (ops[i].icache == icache && ops[i].addr <= addr && ops[i].addr + ops[i].bytes >= addr + bytes)
            {
                return i;
            }
        }
        return -1;
    }

    // Checks that [addr, addr + bytes) was flushed from D-cache and then
    // invalidated from I-cache (in that order).
    bool flushedCode(u32 addr, u32 bytes)
    {
        int dc = findFlush(false, addr, bytes);
        return dc >= 0 && findFlush(true, addr, bytes, dc) > dc;
    }
}

TEST(hook, install_flushes_whole_trampoline_and_patched_word_after_321_fix)
{
    seedSite();
    FakeCoreApi api(kSwapped321, kStubAddr);
    const hook::Site site = {"test site", kSiteAddr, kSiteWords, 3};

    CHECK(hook::installInline(&api, site, reinterpret_cast<void (*)()>(kEntryAddr)));
    CHECK_EQ(1, api.calls);
    CHECK(hook::stillInstalled(site));
    CHECK_EQ(0x80010094u, host::mem::word(kStubAddr + 7 * 4));
    CHECK_EQ(0x80010008u, host::mem::word(kStubAddr + 9 * 4));

    // Full 13-word stub (both 32-byte cache lines) and the 4-byte branch site
    // must each get DCFlushRange followed by ICInvalidateRange.
    CHECK(flushedCode(kStubAddr, kStubBytes));
    CHECK(flushedCode(kSiteAddr, sizeof(u32)));
}

TEST(hook, install_flushes_whole_trampoline_and_patched_word_when_stub_already_correct)
{
    seedSite();
    FakeCoreApi api(kCorrectOrder, kStubAddr);
    const hook::Site site = {"test site", kSiteAddr, kSiteWords, 3};

    CHECK(hook::installInline(&api, site, reinterpret_cast<void (*)()>(kEntryAddr)));
    CHECK(hook::stillInstalled(site));
    CHECK(flushedCode(kStubAddr, kStubBytes));
    CHECK(flushedCode(kSiteAddr, sizeof(u32)));
}

TEST(hook, unknown_stub_restores_site_and_flushes_restored_word)
{
    seedSite();
    FakeCoreApi api(kUnknownLayout, kStubAddr);
    const hook::Site site = {"test site", kSiteAddr, kSiteWords, 3};

    CHECK(!hook::installInline(&api, site, reinterpret_cast<void (*)()>(kEntryAddr)));
    CHECK_EQ(kSiteWords[0], host::mem::word(kSiteAddr));
    CHECK(!hook::stillInstalled(site));
    CHECK(flushedCode(kSiteAddr, sizeof(u32)));
}

TEST(hook, mismatched_site_does_not_patch_or_flush)
{
    seedSite();
    host::mem::setWord(kSiteAddr + 4, 0x60000000u);
    FakeCoreApi api(kSwapped321, kStubAddr);
    const hook::Site site = {"test site", kSiteAddr, kSiteWords, 3};

    CHECK(!hook::installInline(&api, site, reinterpret_cast<void (*)()>(kEntryAddr)));
    CHECK_EQ(0, api.calls);
    CHECK_EQ(0, host::fakes::cacheOpCount());
    CHECK_EQ(kSiteWords[0], host::mem::word(kSiteAddr));
}
