// The stub's time measurements (shared/perf.h), built only with PERF=1.
#ifdef TM_PERF
#include "stub/perf_hooks.h"

namespace tm {
namespace stub {
    static bool s_announced = false;
    static u32 s_lastRender = 0;
    static u32 s_loopTop = 0;
    static bool s_topSeen = false;
    static u32 s_lastGame = 0;
    static bool s_gameSeen = false; // a game pads hook since the system pads hook
    static u32 s_renderEnd = 0;
    static bool s_renderSeen = false;

    bool perfBypassed()
    {
        return (perf::block().control & perf::kControlBypass) != 0;
    }

    u32 perfRenderEnter()
    {
        u32 now = OSGetTick();
        if (!s_announced)
        {
            s_announced = true;
            perf::announce("stub", perf::kStubNames, perf::kStubSections);
        }
        else
        {
            perf::add(perf::kStubFrame, now - s_lastRender);
        }
        s_lastRender = now;
        if (s_gameSeen)
        {
            perf::add(perf::kStubGameToRender, now - s_lastGame);
        }
        return now;
    }

    void perfRenderExit(u32 start)
    {
        u32 burn = perf::block().burn;
        if (burn != 0)
        {
            u32 b0 = OSGetTick();
            while (OSGetTick() - b0 < burn)
            {
            }
        }
        u32 now = OSGetTick();
        perf::add(perf::kStubRender, now - start);
        if (s_topSeen)
        {
            perf::add(perf::kStubBusy, now - s_loopTop);
            s_topSeen = false;
        }
        s_renderEnd = now;
        s_renderSeen = true;
        perf::frameEnd();
    }

    void perfLoopTop()
    {
        s_loopTop = OSGetTick();
        s_topSeen = true;
        s_gameSeen = false;
        if (s_renderSeen)
        {
            perf::add(perf::kStubRenderToTop, s_loopTop - s_renderEnd);
            s_renderSeen = false;
        }
    }

    void perfGameTop()
    {
        u32 now = OSGetTick();
        if (s_topSeen && !s_gameSeen)
        {
            perf::add(perf::kStubTopToGame, now - s_loopTop);
        }
        s_gameSeen = true;
        s_lastGame = now;
    }
}
}
#endif
