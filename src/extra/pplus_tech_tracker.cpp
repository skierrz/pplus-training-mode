#include "extra/pplus_tech_tracker.h"

#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/net_alloc.h"

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

        struct TechTrackerNet {
            TechTrackerBlock block;
            TechWatch watches[game::kMaxTracked];
        };

        TechTrackerNet* s_net = NULL;
        s32 s_prevFrame = game::kNoFrame;
        u32 s_resetCount = 0;

        void resetAll(bool countReset)
        {
            if (s_net == NULL)
            {
                return;
            }
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                techWatchReset(s_net->watches[i]);
            }
            if (countReset)
            {
                s_resetCount++;
            }
            memset(&s_net->block, 0, sizeof(s_net->block));
            s_net->block.magic = kTechTrackerMagic;
            s_net->block.version = kTechTrackerVersion;
            s_net->block.resetCount = s_resetCount;
            for (int p = 0; p < kTechTrackerMaxPlayers; p++)
            {
                techSlotStatsReset(s_net->block.slots[p]);
            }
        }
    }

    void techTrackerAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<TechTrackerNet>(1);
        s_resetCount = 0;
        s_prevFrame = game::kNoFrame;
        if (s_net == NULL)
        {
            return;
        }
        resetAll(false);
        TM_LOG("tech_tracker: debug block at %08x\n", &s_net->block);
    }

    void techTrackerDetach()
    {
        s_prevFrame = game::kNoFrame;
        resetAll(false);
        freeNetPtr(s_net);
    }

    void techTrackerReset()
    {
        s_prevFrame = game::kNoFrame;
        resetAll(true);
    }

    void techTrackerFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        const s32 f = game::trackerFrame();
        if (s_prevFrame != game::kNoFrame && f <= s_prevFrame)
        {
            resetAll(true);
        }
        s_prevFrame = f;
        s_net->block.frame = static_cast<u32>(f >= 0 ? f : 0);

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
            TechSlotStats& stats = s_net->block.slots[t->playerNo];
            if (stepFighterTech(in, s_net->watches[i], stats, label) && postOsd && label != NULL)
            {
                osd::post(t->playerNo, osd::kIdPPlusTech, osd::kGreen, "%s\nCount: %u", label, stats.successes);
                stats.osdPosted++;
            }
        }
    }

    const TechTrackerBlock* techTrackerBlock()
    {
        return s_net != NULL ? &s_net->block : NULL;
    }
}
}
