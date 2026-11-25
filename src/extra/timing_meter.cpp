#include "extra/timing_meter.h"

#include <so/ground/so_ground_module_impl.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/combo_trainer.h"
#include "extra/frame_data.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    enum {
        kTimingMeterDebugMagic = 0x544D544Du, // "TMTM"
        kTimingMeterDebugVersion = 1
    };

    struct TimingMeterDebugSlot {
        u8 active;        // TimingMeterPhase (0 hidden, 1 active, 2 hold)
        u8 kind;          // TimingMeterKind (0 None, 1 LCancel, 2 Wavedash, 3 ActOut)
        u8 outcome;       // TimingMeterOutcome (last outcome: 1 hit, 2 early, 3 late, 4 miss)
        u8 holdFrames;    // remaining hold frames
        s16 lastDelta;    // last outcome delta frames
        s16 actInFrames;  // latest framesUntilActionable reading
        float ratio;      // current bar fill ratio [0..1]
        float worldX;     // last rendered world X
        float worldY;     // last rendered world Y
        u32 eventCount;   // cumulative outcomes recorded
        u32 lCancelHits;
        u32 wavedashHits;
        u32 actOutFlashes;
        u32 drawCount;    // total frames rendered for this slot
    };

    struct TimingMeterDebugBlock {
        u32 magic;   // kTimingMeterDebugMagic ("TMTM")
        u32 version; // kTimingMeterDebugVersion
        u32 frame;   // tracker frame
        u8 mode;     // main::settings().osd.timingMeter
        u8 pad[3];
        TimingMeterDebugSlot slots[game::kMaxFighters];
    };

    static TimingSlotState s_slots[game::kMaxFighters];
    static TimingMeterDebugBlock s_debug;

    static void syncDebugSlot(int p, s16 actIn)
    {
        const TimingSlotState& st = s_slots[p];
        TimingMeterDebugSlot& d = s_debug.slots[p];
        d.active = st.view.phase;
        d.kind = st.view.phase != kTmPhaseHidden ? st.view.kind : st.lastKind;
        d.outcome = st.lastOutcome;
        d.holdFrames = st.holdFrames;
        d.lastDelta = st.lastDelta;
        d.actInFrames = actIn;
        d.ratio = st.view.ratio;
        d.eventCount = st.eventCount;
        d.lCancelHits = st.lCancelHits;
        d.wavedashHits = st.wavedashHits;
        d.actOutFlashes = st.actOutFlashes;
    }

    void timingMeterReset()
    {
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            tmResetSlot(s_slots[i]);
        }
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kTimingMeterDebugMagic;
        s_debug.version = kTimingMeterDebugVersion;
    }

    void timingMeterAttach()
    {
        timingMeterReset();
        TM_LOG("timing_meter: debug block at %08x\n", &s_debug);
    }

    void timingMeterDetach()
    {
        timingMeterReset();
    }

    void timingMeterFrame()
    {
        u8 mode = main::settings().osd.timingMeter;
        s32 f = game::trackerFrame();
        s_debug.frame = static_cast<u32>(f >= 0 ? f : 0);
        s_debug.mode = mode;

        if (mode == main::kTimingMeterOff)
        {
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                s_slots[i].view.phase = kTmPhaseHidden;
                s_debug.slots[i].active = kTmPhaseHidden;
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
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            game::Body body;
            if (!game::readBody(t->fighter, body))
            {
                continue;
            }

            TimingStepInput in;
            memset(&in, 0, sizeof(in));
            in.frame = f;
            in.status = t->cur.status;
            in.prevStatus = t->prev.status;
            in.hitlag = t->cur.hitlag;
            in.hitstun = t->cur.hitstun;
            int how = main::kActNone;
            in.actIn = static_cast<s16>(framesUntilActionable(t->fighter, &how));
            if (in.status == kTmStJumpSquat)
            {
                game::MotionInfo mi;
                if (game::readMotion(t->fighter, mi) && mi.length >= 2 && mi.length <= 12)
                {
                    in.jumpSquatLen = static_cast<s16>(mi.length);
                }
            }
            in.lCancelCounter = body.lCancel;
            in.mode = mode;
            int evCount = t->eventCount < kTmMaxEvents ? static_cast<int>(t->eventCount) : kTmMaxEvents;
            in.eventCount = static_cast<u8>(evCount);
            for (int k = 0; k < evCount; k++)
            {
                in.events[k].status = t->events[k].status;
                in.events[k].prev = t->events[k].prev;
                in.events[k].lCancel = t->events[k].lCancel;
            }

            tmStepSlot(s_slots[p], in);
            syncDebugSlot(p, in.actIn);
        }
    }

    // Extra vertical clearance above Combo Trainer's active floating meters.
    static float comboMeterClearance(int p)
    {
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        if (!cfg.enabled || cfg.enabledMeters == 0)
        {
            return 0.0f;
        }
        const ComboMeterValues* cm = comboTrainerMeters(p);
        if (cm == NULL || cm->activeMask == 0)
        {
            return 0.0f;
        }
        float extra = 0.0f;
        if ((cm->activeMask & (main::kMeterShieldstun | main::kMeterHitlag | main::kMeterHitstun | main::kMeterIasa))
            != 0)
        {
            extra += 3.8f;
        }
        if ((cm->activeMask & (main::kMeterLedgeIntang | main::kMeterIntang)) != 0)
        {
            extra += 3.0f;
        }
        if ((cm->activeMask & main::kMeterDi) != 0)
        {
            extra += 6.2f;
        }
        return extra > 0.0f ? (extra + 2.4f) : 0.0f;
    }

    static void drawOverheadBar(float cx, float cy, const TimingBarView& v)
    {
        const float kBarW = 16.0f;
        const float kBarH = 2.6f;
        const float x0 = cx - kBarW * 0.5f;
        const float y0 = cy - kBarH * 0.5f;

        // Crisp dark outer border + inner beveled dark slate backing.
        u32 frameCol = v.isFlash ? 0xBBF7D0F0u : 0x060C12E8u;
        gfx::fillRect(x0 - 0.45f, y0 - 0.45f, kBarW + 0.90f, kBarH + 0.90f, frameCol);
        gfx::fillRect(x0, y0, kBarW, kBarH, 0x121C26DCu);

        // Optional green target timing zone inside the active track.
        if (v.hasZone != 0 && v.zoneEnd > v.zoneStart)
        {
            float zx = x0 + v.zoneStart * kBarW;
            float zw = (v.zoneEnd - v.zoneStart) * kBarW;
            gfx::fillRect(zx, y0, zw, kBarH, 0x1E6B38B8u);
            gfx::fillRect(zx, y0 + kBarH - 0.55f, zw, 0.55f, 0x4ADE80E0u);
        }

        // Fill bar + subtle top sheen.
        if (v.ratio > 0.0f)
        {
            float fw = kBarW * (v.ratio > 1.0f ? 1.0f : v.ratio);
            gfx::fillRect(x0, y0, fw, kBarH, v.fillColor);
            gfx::fillRect(x0, y0 + kBarH - 0.55f, fw, 0.55f, 0xFFFFFF58u);
        }

        // Active moving cursor tick for immediate frame readability.
        if (v.phase == kTmPhaseActive)
        {
            float tx = x0 + v.ratio * kBarW;
            gfx::fillRect(tx - 0.35f, y0 - 0.45f, 0.70f, kBarH + 0.90f, 0xFFFFFFFFu);
        }
    }

    void timingMeterDraw(const game::MatchState& match)
    {
        u8 mode = main::settings().osd.timingMeter;
        if (!match.running || match.paused || mode == main::kTimingMeterOff)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            const TimingBarView& v = s_slots[p].view;
            if (v.phase == kTmPhaseHidden)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m == NULL || !game::isRam(m->m_groundModule))
            {
                continue;
            }
            Vec2f top = m->m_groundModule->getUpPos(0);
            float cx = top.m_x;
            float cy = top.m_y + 10.5f + comboMeterClearance(p);
            drawOverheadBar(cx, cy, v);
            s_debug.slots[p].worldX = cx;
            s_debug.slots[p].worldY = cy;
            s_debug.slots[p].drawCount++;
        }
    }
}
}
