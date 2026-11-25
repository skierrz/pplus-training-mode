#include "extra/pplus_tech_tracker.h"

#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "main/settings.h"
#include "main/ui/osd.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kPostureLr = 0x40,
            kLaStartTurnFrame = 55 // LA-Basic[55] (0x10000037)
        };

        TechTrackerBlock s_block;
        TechWatch s_watches[game::kMaxTracked];
        s32 s_prevFrame = game::kNoFrame;
        u32 s_resetCount = 0;

        void resetAll(bool countReset)
        {
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                techWatchReset(s_watches[i]);
            }
            if (countReset)
            {
                s_resetCount++;
            }
            memset(&s_block, 0, sizeof(s_block));
            s_block.magic = kTechTrackerMagic;
            s_block.version = kTechTrackerVersion;
            s_block.resetCount = s_resetCount;
            for (int p = 0; p < kTechTrackerMaxPlayers; p++)
            {
                techSlotStatsReset(s_block.slots[p]);
            }
        }
    }

    void techTrackerAttach()
    {
        s_resetCount = 0;
        s_prevFrame = game::kNoFrame;
        resetAll(false);
        TM_LOG("tech_tracker: debug block at %08x\n", &s_block);
    }

    void techTrackerDetach()
    {
        s_prevFrame = game::kNoFrame;
        resetAll(false);
    }

    void techTrackerReset()
    {
        s_prevFrame = game::kNoFrame;
        resetAll(true);
    }

    void techTrackerFrame()
    {
        const s32 f = game::trackerFrame();
        if (s_prevFrame != game::kNoFrame && f <= s_prevFrame)
        {
            resetAll(true);
        }
        s_prevFrame = f;
        s_block.frame = static_cast<u32>(f >= 0 ? f : 0);

        const bool postOsd = main::settings().cpu.options.techTrackerOsd != 0;

        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || !t->used || t->cur.frame != f || !game::isRam(t->fighter))
            {
                continue;
            }
            if (t->playerNo < 0 || t->playerNo >= kTechTrackerMaxPlayers)
            {
                continue;
            }
            const int ftKind = game::fighterKind(t->entryId, t->partner);
            const FighterTechDef* def = NULL;
            if (findFighterTechDef(ftKind, def) == 0 || def == NULL)
            {
                continue;
            }

            const soModuleEnumeration* m = game::fighterModules(t->fighter);
            game::Body body;
            const bool haveBody = game::readBody(t->fighter, body);

            TechFrameInput in;
            memset(&in, 0, sizeof(in));
            in.frame = f;
            in.ftKind = ftKind;
            in.status = t->cur.status;
            in.motionKind = haveBody ? static_cast<s16>(body.motionKind) : -1;
            in.hitstun = t->cur.hitstun;
            in.airborne = t->cur.airborne;
            in.outOfPlay = game::outOfPlay(t->cur.status, true) ? 1 : 0;
            in.lr = 1.0f;
            if (m != NULL && game::isRam(m->m_postureModule))
            {
                float rawLr = *reinterpret_cast<const float*>(reinterpret_cast<u32>(m->m_postureModule) + kPostureLr);
                in.lr = rawLr < 0.0f ? -1.0f : 1.0f;
            }
            if (m != NULL)
            {
                in.laBasic55 = game::instanceInt(m, kLaStartTurnFrame);
                if (def->laBit != 0 && game::isRam(m->m_workManageModule))
                {
                    in.laBitActive = m->m_workManageModule->isFlag(0x12000000u | def->laBit) ? 1 : 0;
                }
            }
            const u8 evN = t->eventCount < kTechTrackerMaxEvents ? t->eventCount : static_cast<u8>(kTechTrackerMaxEvents);
            in.eventCount = evN;
            for (u8 k = 0; k < evN; k++)
            {
                in.events[k].status = t->events[k].status;
                in.events[k].prev = t->events[k].prev;
                in.events[k].motionKind = t->events[k].motionKind;
            }

            const char* label = NULL;
            TechSlotStats& stats = s_block.slots[t->playerNo];
            if (stepFighterTech(in, s_watches[i], stats, label) && postOsd && label != NULL)
            {
                osd::post(t->playerNo, osd::kIdPPlusTech, osd::kGreen, "%s\nCount: %u", label, stats.successes);
                stats.osdPosted++;
            }
        }
    }

    const TechTrackerBlock* techTrackerBlock()
    {
        return &s_block;
    }
}
}
