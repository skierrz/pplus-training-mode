// The menu module, loaded for Training's character select: runs the drill
// list from the stub's render and pads callbacks.
#include "menu/module.h"

#include <types.h>

#include "menu/bests_file.h"
#include "menu/drill_list.h"
#include "menu/services.h"
#include "menu/session.h"
#include "menu/sound.h"
#include "shared/game/heap.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "shared/version.h"

namespace tm {
namespace menu {
    static void ignoreLog(const char*, ...)
    {
    }

    static u8* noSettings()
    {
        return NULL;
    }

    static const iface::Services kNoServices = {ignoreLog, noSettings};
    static iface::Interface* s_iface = NULL;

    const iface::Services& services()
    {
        return s_iface != NULL ? s_iface->services : kNoServices;
    }

    void sound(int id)
    {
        TM_LOG("menu: sound %d (handle %d)\n", id, game::playMenuSound(id));
    }

    static void onRender(u32 call)
    {
        bests::update();
        list::onRender(call);
    }

    static bool attach(iface::Interface* iface)
    {
        if (iface->version != iface::kVersion || iface->size != sizeof(iface::Interface))
        {
            return false;
        }
        // Before the session: its bests file logs through the services.
        s_iface = iface;
        if (attachSession(iface->services.settings()) == NULL)
        {
            s_iface = NULL;
            return false;
        }
        // The character select's own allocations go to MenuInstance; this
        // module sits in OverlayMenu (see the stub's load policy).
        game::HeapUsage menuHeap;
        game::HeapUsage overlay;
        bool m = game::heapUsage(Heaps::MenuInstance, menuHeap);
        bool o = game::heapUsage(Heaps::OverlayMenu, overlay);
        TM_LOG("menu: attached in %s / %s; MenuInstance free %x largest %x, OverlayMenu free %x largest %x\n",
               game::sceneName(), game::sequenceName(), m ? menuHeap.freeBytes : 0, m ? menuHeap.largest : 0,
               o ? overlay.freeBytes : 0, o ? overlay.largest : 0);
        list::attach();
        iface->callbacks.render = onRender;
        iface->callbacks.pads = list::onPads;
        return true;
    }

    static void detach()
    {
        list::detach();
        // A bests write in flight uses this module's memory: wait it out.
        bests::detach();
        TM_LOG("menu: detaching\n");
        s_iface = NULL;
    }

    const iface::ModuleInfo kModule = {
        iface::kModuleMagic,
        iface::kVersion,
        iface::kKindMenu,
        TM_VERSION,
        attach,
        detach
    };
}
}
