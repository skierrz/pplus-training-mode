#include "main/savestate/trace.h"

#include <OS/OSTime.h>
#include <cstdio>
#include <ft/ft_entry.h>
#include <gf/gf_pad_status.h>
#include <ft/ft_entry_manager.h>
#include <so/so_module_accesser.h>
#include <so/stop/so_stop_module_impl.h>
#include <types.h>

#include "main/game/fighters.h"
#include "main/savestate/snapshot.h"
#include "main/ui/osd.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace savestate {
    enum {
        kProbeMagic = 0x544D5350, // "TMSP"
        // soMotionModuleImpl +0x40: the animation frame (float; read by the
        // emulator tests, scripts/lab_tests.py).
        kMotionFrame = 0x40,
        // soPostureModule +0xC: position (fighters.cpp).
        kPosturePos = 0xC,
        // A park gives up after this many timebase ticks (60 s) unreleased.
        kParkTimeoutTicks = 60u * 60750000u
    };

    static const volatile u32* const kGameFrame = reinterpret_cast<const volatile u32*>(0x901812A4);
    static const volatile u32* const kRngSeed = reinterpret_cast<const volatile u32*>(0x805A00BC);

    Probe g_probe;
    static u32 s_lastFrame = 0xFFFFFFFF;

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    static int hitlag(const soModuleEnumeration* m)
    {
        if (!game::isRam(m->m_stopModule))
        {
            return 0;
        }
        return static_cast<soStopModuleImpl*>(m->m_stopModule)->getHitStopRealFrame();
    }

    // x y status motion-frame (the partner's after a '+').
    static int partnerText(Fighter* f, char* out, int size)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_postureModule) || !game::isRam(m->m_motionModule))
        {
            return 0;
        }
        return snprintf(out, size, " +%.3f %.3f %x %.2f", floatAt(m->m_postureModule, kPosturePos),
                        floatAt(m->m_postureModule, kPosturePos + 4), game::fighterStatus(m),
                        floatAt(m->m_motionModule, kMotionFrame));
    }

    void traceTop(char tag)
    {
        u32 frame = *kGameFrame;
        if (!g_probe.trace || (tag == ' ' && frame == s_lastFrame))
        {
            return;
        }
        s_lastFrame = frame;
        char text[240];
        int n = 0;
        game::FighterRef refs[game::kMaxFighters];
        int count = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < count && n < 180; i++)
        {
            game::FighterState st;
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (!game::readState(refs[i], st) || m == NULL || !game::isRam(m->m_motionModule))
            {
                continue;
            }
            // player x y status motion-frame damage hitstun hitlag
            n += snprintf(text + n, sizeof(text) - n, " | P%d %.3f %.3f %x %.2f %.1f %d %d", refs[i].playerNo + 1,
                          st.x, st.y, st.status, floatAt(m->m_motionModule, kMotionFrame), st.damage, st.hitstun,
                          hitlag(m));
            if (refs[i].partner != NULL && n < 180)
            {
                n += partnerText(refs[i].partner, text + n, sizeof(text) - n);
            }
        }
        text[n < 0 ? 0 : (n < static_cast<int>(sizeof(text)) ? n : sizeof(text) - 1)] = '\0';
        TM_LOG("sst %c f=%u r=%08x h=%08x u=%08x%s\n", tag, frame, *kRngSeed, fighterHeapHash(), usageHash(), text);
    }

    bool parkTop()
    {
        u32 target = g_probe.parkAt;
        if (target == 0 || *kGameFrame < target)
        {
            return false;
        }
        // Consumed: the harness sets the next target while this park lasts.
        g_probe.parkAt = 0;
        u32 seq = g_probe.parkSeq + 1;
        g_probe.osdVisible = osd::visible() ? 1 : 0;
        g_probe.parkedFrame = *kGameFrame;
        g_probe.parkSeq = seq;
        u32 t0 = OSGetTick();
        while (g_probe.release != seq && OSGetTick() - t0 < kParkTimeoutTicks)
        {
        }
        g_probe.parkedFrame = 0;
        return true;
    }

    void heldPads(gfPadStatus* pads)
    {
        if (g_probe.heldPasses == 0)
        {
            return;
        }
        g_probe.heldPasses = g_probe.heldPasses - 1;
        pads[0].m_buttonsCurrentFrame.bits |= g_probe.heldBits;
    }

    void probeReset()
    {
        s_lastFrame = 0xFFFFFFFF;
        g_probe.magic = kProbeMagic;
        g_probe.trace = 0;
        g_probe.request = 0;
        g_probe.rulesV3 = 0;
        g_probe.parkAt = 0;
        g_probe.parkedFrame = 0;
        g_probe.heldPasses = 0;
    }
}
}
