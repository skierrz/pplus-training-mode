#include "main/game/advantage.h"

#include "main/game/tracker.h"
#include "shared/game/heap.h"
#include "shared/log.h"

// Adapted from ProjectPunch's on-hit / on-shield advantage (main.cpp,
// playerdata.cpp; MIT, see THIRD_PARTY_NOTICES.md). What is kept and what
// changed: docs/internals.md, "Frame advantage".
namespace tm {
namespace game {
    enum { kGiveUp = 180, kShownLimit = 50 };

    struct Exchange {
        bool active;
        s8 attacker;
        u8 shield;
        u8 hits;
        u8 how;
        s32 hit;
        s32 attackerFrame;
        s32 defenderFrame;
        s16 attackerStatus;
        s16 attackerPrev;
        s16 defenderStatus;
        s16 defenderPrev;
    };

    struct AdvantageNet {
        Exchange exchanges[kMaxTracked];
        Advantage last;
    };

    static AdvantageNet* s_net = NULL;
    static bool s_haveLast = false;
    static int s_tolerance = 1;
    static AdvantageSink s_sink = NULL;

    void advantageAttach()
    {
        if (s_net == NULL)
        {
            s_net = static_cast<AdvantageNet*>(allocNetworkZero(static_cast<u32>(sizeof(AdvantageNet))));
        }
    }

    void advantageDetach()
    {
        freeNetworkSlot(reinterpret_cast<void**>(&s_net));
        s_haveLast = false;
        s_sink = NULL;
    }

    void advantageReset(AdvantageSink sink)
    {
        if (s_net != NULL)
        {
            for (int i = 0; i < kMaxTracked; i++)
            {
                s_net->exchanges[i].active = false;
            }
        }
        s_haveLast = false;
        s_sink = sink;
    }

    void advantageSetTolerance(int frames)
    {
        s_tolerance = frames < 0 ? 0 : frames;
    }

    const Advantage* lastAdvantage()
    {
        return s_net != NULL && s_haveLast ? &s_net->last : NULL;
    }

    static void finish(int d, const Exchange& ex)
    {
        const Tracked* a = tracked(ex.attacker);
        const Tracked* def = tracked(d);
        Advantage& r = s_net->last;
        r.attacker = ex.attacker;
        r.defender = static_cast<s8>(d);
        r.attackerPlayer = a != NULL ? a->playerNo : -1;
        r.defenderPlayer = def != NULL ? def->playerNo : -1;
        r.shield = ex.shield;
        r.hits = ex.hits;
        r.how = ex.how;
        r.value = static_cast<s16>(ex.defenderFrame - ex.attackerFrame);
        r.hitFrame = ex.hit;
        r.attackerFrame = ex.attackerFrame;
        r.defenderFrame = ex.defenderFrame;
        r.attackerStatus = ex.attackerStatus;
        r.attackerPrev = ex.attackerPrev;
        r.defenderStatus = ex.defenderStatus;
        r.defenderPrev = ex.defenderPrev;
        s_haveLast = true;
        bool shown = r.value > -kShownLimit && r.value < kShownLimit;
        TM_LOG("advantage: P%d%s -> P%d%s on %s %+d (hit %d, attacker %d status %x from %x, defender %d status %x from "
               "%x, hits %d, by %s)%s\n",
               r.attackerPlayer + 1, a != NULL && a->partner ? "b" : "", r.defenderPlayer + 1,
               def != NULL && def->partner ? "b" : "", r.shield ? "shield" : "hit", r.value, r.hitFrame,
               r.attackerFrame, r.attackerStatus, r.attackerPrev, r.defenderFrame, r.defenderStatus, r.defenderPrev,
               r.hits, r.how == kByInflict ? "inflict" : "elimination", shown ? "" : " not shown");
        r.hidden = shown ? 0 : 1;
        if (s_sink != NULL)
        {
            s_sink(r);
        }
    }

    void advantageFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        s32 now = trackerFrame();
        for (int d = 0; d < kMaxTracked; d++)
        {
            Exchange& ex = s_net->exchanges[d];
            const Tracked* def = tracked(d);
            if (def == NULL)
            {
                ex.active = false;
                continue;
            }
            const FighterFrame& df = def->cur;
            if ((df.hitReceived || df.shieldReceived) && def->lastReceived.frame == now)
            {
                int a = def->lastReceived.other;
                if (a < 0)
                {
                    ex.active = false;
                    continue;
                }
                if (ex.active && ex.attacker == a && ex.attackerFrame == kNoFrame)
                {
                    if (ex.hits < 255)
                    {
                        ex.hits++;
                    }
                }
                else
                {
                    ex.active = true;
                    ex.attacker = static_cast<s8>(a);
                    ex.hits = 1;
                    ex.attackerFrame = kNoFrame;
                }
                ex.shield = df.shieldReceived;
                ex.how = def->lastReceived.how;
                ex.hit = now;
                ex.defenderFrame = kNoFrame;
            }
            if (!ex.active)
            {
                continue;
            }
            const Tracked* att = tracked(ex.attacker);
            if (att == NULL)
            {
                ex.active = false;
                continue;
            }
            if (ex.attackerFrame == kNoFrame && att->cur.actionable)
            {
                ex.attackerFrame = now;
                ex.attackerStatus = att->cur.status;
                ex.attackerPrev = att->cur.prevStatus;
            }
            if (ex.defenderFrame == kNoFrame && now > ex.hit && df.actionable)
            {
                ex.defenderFrame = now;
                ex.defenderStatus = df.status;
                ex.defenderPrev = df.prevStatus;
            }
            if (ex.defenderFrame != kNoFrame && ex.attackerFrame != kNoFrame && now - ex.defenderFrame > s_tolerance)
            {
                finish(d, ex);
                ex.active = false;
            }
            else if (now - ex.hit > kGiveUp)
            {
                ex.active = false;
            }
        }
    }
}
}
