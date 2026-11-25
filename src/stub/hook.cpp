// Inline hooks at fixed game addresses, installed through Syriinge only
// when the site holds the expected words and the stub has a known layout.
#include "stub/hook.h"

#include <sy_core.h>

#include "shared/log.h"
#include "stub/inline_stub.h"
#include "stub/ppc.h"

namespace tm {
namespace hook {
    static u32* words(u32 address) { return reinterpret_cast<u32*>(address); }

    // The stub the site branches to, or NULL if the site holds no `b` into MEM1.
    static u32* stubOf(const Site& site)
    {
        u32 word = *words(site.address);
        if (!ppc::isBranch(word))
        {
            return NULL;
        }
        u32 target = ppc::branchTarget(site.address, word);
        if (target < 0x80000000 || target > 0x817FFFFF - 13 * 4)
        {
            return NULL;
        }
        return words(target);
    }

    bool matches(const Site& site)
    {
        for (u32 i = 0; i < site.count; i++)
        {
            u32 found = words(site.address)[i];
            if (found != site.expected[i])
            {
                TM_LOG("hook %s: %08x holds %08x, expected %08x; not installing\n",
                       site.name, site.address + i * 4, found, site.expected[i]);
                return false;
            }
        }
        return true;
    }

    bool installInline(CoreApi* api, const Site& site, void (*entry)())
    {
        if (!matches(site))
        {
            return false;
        }
        api->syInlineHook(site.address, reinterpret_cast<const void*>(entry));

        u32* stub = stubOf(site);
        inline_stub::Result result =
            stub != NULL ? inline_stub::checkAndFix(stub, site.address) : inline_stub::kUnknown;
        TM_LOG("hook %s: stub check: %s\n", site.name, inline_stub::describe(result));
        if (result == inline_stub::kUnknown)
        {
            // Put the original instruction back rather than run through a
            // stub we cannot vouch for.
            *words(site.address) = site.expected[0];
            ppc::flushCode(words(site.address), sizeof(u32));
            TM_LOG("hook %s: removed again, site restored\n", site.name);
            return false;
        }
        // sy_core only invalidates the instruction cache at the hooked word;
        // flush both the whole 13-word trampoline and the patched branch.
        ppc::flushCode(stub, 13 * sizeof(u32));
        ppc::flushCode(words(site.address), sizeof(u32));
        TM_LOG("hook %s: installed at %08x\n", site.name, site.address);
        return true;
    }

    bool stillInstalled(const Site& site)
    {
        const u32* stub = stubOf(site);
        return stub != NULL && inline_stub::belongsTo(stub, site.address, site.expected[0]);
    }
}
}
