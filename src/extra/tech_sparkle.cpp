#include "extra/tech_sparkle.h"

#include <ec/ec_mgr.h>
#include <ef/ef_id.h>
#include <mt/mt_vector.h>
#include <string.h>

#include "extra/tech_sparkle_core.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/match.h"
#include "main/game/tracker.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kDebugMagic = 0x544D5453u, // "TMTS"
            kDebugVersion = 1
        };

        struct TechSparkleDebugBlock {
            u32 magic;          // 0x00 "TMTS"
            u32 version;        // 0x04
            u32 frame;          // 0x08
            u8 mode;            // 0x0C
            s8 lastPlayer;      // 0x0D
            u8 lastCue;         // 0x0E
            u8 pad;             // 0x0F
            u32 wavedashSparks; // 0x10
            u32 shorthopSparks; // 0x14
            u32 lastEffectId;   // 0x18
            u32 lastHandle;     // 0x1C
            s32 lastFrame;      // 0x20
        };

        TechSparkleSlot s_slots[game::kMaxFighters];
        TechSparkleDebugBlock s_debug;
        s32 s_prevFrame = kTsNoFrame;

        // Spawns a strictly non-looping (emitLife == 1) native ef_common burst
        // in world space via g_ecMgr->setEffect (direct RSBE01.lst symbol,
        // zero vtable dispatch). Both ef_ptc_common_spflash (0x7D, 12f) and
        // ef_ptc_common_smash_flash (0x10, 5f) self-terminate and roll back
        // atomically with Heaps::Effect / g_ecMgr on savestate load, so we
        // intentionally never call killEffect on saved/recycled handles.
        void spawnCueEffect(const game::Tracked& t, int p, u8 cue, s32 frame)
        {
            if (game::outOfPlay(t.cur.status, true) || !game::isRam(g_ecMgr))
            {
                return;
            }
            game::Body body;
            if (!game::readBody(t.fighter, body))
            {
                return;
            }
            EfID efId = cue == kSparklePerfectWavedash ? ef_ptc_common_spflash : ef_ptc_common_smash_flash;
            float yOff = cue == kSparklePerfectWavedash ? 6.0f : 5.0f;
            float scale = 0.85f;
            Vec3f pos(body.x, body.y + yOff, 0.0f);
            Vec3f rot(0.0f, 0.0f, 0.0f);
            Vec3f scl(scale, scale, scale);
            u32 handle = g_ecMgr->setEffect(efId, &pos, &rot, &scl);
            s_debug.lastPlayer = static_cast<s8>(p);
            s_debug.lastCue = cue;
            s_debug.lastEffectId = static_cast<u32>(efId);
            s_debug.lastHandle = handle;
            s_debug.lastFrame = frame;
            if (cue == kSparklePerfectWavedash)
            {
                s_debug.wavedashSparks++;
            }
            else if (cue == kSparkleShorthop)
            {
                s_debug.shorthopSparks++;
            }
        }
#pragma dont_inline on
        void resetAllSlots()
        {
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                techSparkleResetSlot(s_slots[i]);
            }
        }
#pragma dont_inline reset
    }

    void techSparkleReset()
    {
        s_prevFrame = kTsNoFrame;
        resetAllSlots();
    }

    void techSparkleAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        s_debug.version = kDebugVersion;
        s_debug.lastPlayer = -1;
        s_debug.lastFrame = kTsNoFrame;
        s_prevFrame = kTsNoFrame;
        resetAllSlots();
        TM_LOG("tech_sparkle: debug block at %08x\n", &s_debug);
    }

    void techSparkleDetach()
    {
        techSparkleReset();
    }

    void techSparkleFrame()
    {
        const game::MatchState& match = game::matchState();
        u8 mode = main::settings().osd.techSparkle;
        s32 f = game::trackerFrame();
        s_debug.frame = static_cast<u32>(f >= 0 ? f : 0);
        s_debug.mode = mode;

        if (!match.running || mode == main::kTechSparkleOff)
        {
            s_prevFrame = kTsNoFrame;
            resetAllSlots();
            return;
        }
        if (match.paused)
        {
            return;
        }
        if (s_prevFrame != kTsNoFrame)
        {
            if (f < s_prevFrame)
            {
                resetAllSlots();
            }
            else if (f == s_prevFrame)
            {
                return;
            }
        }
        s_prevFrame = f;

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || t->partner || t->cur.frame != f)
            {
                continue;
            }
            int p = t->playerNo;
            if (p < 0 || p >= game::kMaxFighters || !game::humanPlayer(p))
            {
                continue;
            }
            TechSparkleInput in;
            memset(&in, 0, sizeof(in));
            in.frame = f;
            in.mode = mode;
            int evCount = t->eventCount < kTsMaxEvents ? static_cast<int>(t->eventCount) : kTsMaxEvents;
            in.eventCount = static_cast<u8>(evCount);
            for (int k = 0; k < evCount; k++)
            {
                in.events[k].status = t->events[k].status;
                in.events[k].prev = t->events[k].prev;
                in.events[k].flags = t->events[k].flags;
            }
            u8 cue = stepTechSparkle(s_slots[p], in);
            if (cue != kSparkleNone)
            {
                spawnCueEffect(*t, p, cue, f);
            }
        }
    }
}
}
