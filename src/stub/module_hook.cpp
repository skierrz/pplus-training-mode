#include "stub/module_hook.h"

#include <gf/gf_module.h>
#include <sy_core.h>

#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace hook {
    // sy_core's own *Rel hooks patch blindly when the module loads; doing it
    // from a load callback instead lets us check the words first. sy_core's
    // callback (which applies other plugins' *Rel hooks) is subscribed before
    // any plugin's, so ours sees the module as it is after those patches.
    struct PendingModuleHook {
        const ModuleSite* site;
        void (*entry)();
        u32 address; // where it was installed on the last load, or 0
    };
    enum { kMaxModuleHooks = 5 };
    static PendingModuleHook s_moduleHooks[kMaxModuleHooks];
    static u32 s_moduleHookCount = 0;
    static CoreApi* s_api = NULL;

    static void onModuleLoad(gfModuleInfo* info)
    {
        if (!game::isRam(info) || !game::isRam(info->m_module) || !game::isRam(info->m_module->m_header))
        {
            return;
        }
        gfModuleHeader* header = info->m_module->m_header;
        for (u32 i = 0; i < s_moduleHookCount; i++)
        {
            PendingModuleHook& pending = s_moduleHooks[i];
            if (pending.site->moduleId != header->info.id)
            {
                continue;
            }
            u32 text = header->getTextSectionAddr();
            const Site site = {pending.site->name, text + pending.site->offset, pending.site->expected,
                               pending.site->count};
            TM_LOG("hook %s: module %u loaded, .text at %08x\n", site.name, header->info.id, text);
            pending.address = installInline(s_api, site, pending.entry) ? site.address : 0;
        }
    }

    bool installInlineOnModuleLoad(CoreApi* api, const ModuleSite& site, void (*entry)())
    {
        if (s_moduleHookCount == kMaxModuleHooks)
        {
            TM_LOG("hook %s: no slot left for a module hook\n", site.name);
            return false;
        }
        if (s_api == NULL)
        {
            s_api = api;
            api->moduleLoadEventSubscribe(onModuleLoad);
        }
        PendingModuleHook& pending = s_moduleHooks[s_moduleHookCount++];
        pending.site = &site;
        pending.entry = entry;
        pending.address = 0;
        TM_LOG("hook %s: waiting for module %u (.text+%x)\n", site.name, site.moduleId, site.offset);
        return true;
    }

    u32 moduleSiteAddress(const ModuleSite& site)
    {
        for (u32 i = 0; i < s_moduleHookCount; i++)
        {
            if (s_moduleHooks[i].site == &site)
            {
                return s_moduleHooks[i].address;
            }
        }
        return 0;
    }

    bool moduleSiteInstalled(const ModuleSite& site)
    {
        u32 address = moduleSiteAddress(site);
        if (address == 0)
        {
            return false;
        }
        const Site live = {site.name, address, site.expected, site.count};
        return stillInstalled(live);
    }
}
}
