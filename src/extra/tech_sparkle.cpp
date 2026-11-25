#include "extra/tech_sparkle.h"

#include <ef/ef_id.h>
#include <mt/mt_vector.h>
#include <so/effect/so_effect_module_impl.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/tech_sparkle_core.h"
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
        u32 s_lastHandles[game::kMaxFighters];
        TechSparkleDebugBlock s_debug;

        void killLastHandles()
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int i = 0; i < n; i++)
            {
                int p = refs[i].playerNo;
                if (p < 0 || p >= game::kMaxFighters || s_lastHandles[p] == 0)
                {
                    continue;
                }
                const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
                if (m != NULL && game::isRam(m->m_effectModule))
                {
                    m->m_effectModule->kill(static_cast<int>(s_lastHandles[p]), true, true);
                }
                s_lastHandles[p] = 0;
            }
            memset(s_lastHandles, 0, sizeof(s_lastHandles));
        }

        // Spawns a strictly non-looping (emitLife == 1) native ef_common burst
        // in world space via soEffectModule::req (verified in P+ 3.2.1
        // COMMON3.PAC EFLS/REFF and DoubleCherry.asm:378).
        void spawnCueEffect(const game::Tracked& t, int p, u8 cue, s32 frame)
        {
            if (game::outOfPlay(t.cur.status, true))
            {
                return;
            }
            const soModuleEnumeration* m = game::fighterModules(t.fighter);
            if (m == NULL || !game::isRam(m->m_effectModule))
            {
                return;
            }
            game::FighterRef ref;
            ref.entryId = t.entryId;
            ref.playerNo = t.playerNo;
            ref.fighter = t.fighter;
            ref.partner = NULL;
            game::FighterState st;
            if (!game::readState(ref, st))
            {
                return;
            }
            EfID efId = cue == kSparklePerfectWavedash ? ef_ptc_common_spflash : ef_ptc_common_smash_flash;
            float yOff = cue == kSparklePerfectWavedash ? 6.0f : 5.0f;
            float scale = 0.85f;
            Vec3f pos(st.x, st.y + yOff, 0.0f);
            u32 handle = m->m_effectModule->req(efId, &pos, NULL, scale, 0, -1);
            s_lastHandles[p] = handle;
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
    }

    void techSparkleReset()
    {
        killLastHandles();
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            techSparkleResetSlot(s_slots[i]);
        }
    }

    void techSparkleAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        s_debug.version = kDebugVersion;
        s_debug.lastPlayer = -1;
        s_debug.lastFrame = kTsNoFrame;
        memset(s_lastHandles, 0, sizeof(s_lastHandles));
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            techSparkleResetSlot(s_slots[i]);
        }
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

        if (!match.running || match.paused || mode == main::kTechSparkleOff)
        {
            if (mode == main::kTechSparkleOff)
            {
                for (int i = 0; i < game::kMaxFighters; i++)
                {
                    techSparkleResetSlot(s_slots[i]);
                }
            }
            return;
        }

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
