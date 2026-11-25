#include "main/cpu/script_source.h"

#include <string.h>

#include "main/cpu/driver.h"
#include "shared/game/frame.h"
#include "shared/log.h"

namespace tm {
namespace cpu {
    static ScriptBlock s_blocks[kScriptBlocks];

    class ScriptSource : public Source {
    public:
        ScriptBlock* block;
        u32 generation; // of the script being played
        u32 framesLeft; // in the current step

        bool valid() const
        {
            return block->playerNo >= 0 && block->stepCount != 0 && block->stepCount <= kScriptSteps;
        }

        virtual bool wants(const Owner& who)
        {
            return valid() && block->playerNo == who.playerNo && block->generation == generation
                   && block->step < block->stepCount;
        }

        virtual bool next(const Owner&, const Frame& produced, Frame& out)
        {
            ScriptBlock& b = *block;
            u32 frame = game::frameCounter();
            b.aiButtons = produced.buttons;
            b.aiStickX = produced.x;
            b.aiStickY = produced.y;
            b.callsThisFrame = frame == b.lastFrame ? b.callsThisFrame + 1 : 1;
            b.lastFrame = frame;
            const ScriptStep& step = b.steps[b.step];
            out.buttons = step.buttons;
            out.x = step.x;
            out.y = step.y;
            b.framesDriven++;
            if (--framesLeft == 0)
            {
                if (++b.step == b.stepCount && b.loop != 0)
                {
                    b.step = 0;
                }
                if (b.step < b.stepCount)
                {
                    framesLeft = b.steps[b.step].frames != 0 ? b.steps[b.step].frames : 1;
                }
                else
                {
                    TM_LOG("cpu script: %u finished after %u frames\n", generation, b.framesDriven);
                }
            }
            return true;
        }

        virtual void released(const Owner&)
        {
        }
    };

    static ScriptSource s_sources[kScriptBlocks];

    void scriptAttach()
    {
        for (int i = 0; i < kScriptBlocks; i++)
        {
            ScriptBlock& b = s_blocks[i];
            memset(&b, 0, sizeof(b));
            b.magic = kScriptMagic;
            b.playerNo = -1;
            s_sources[i].block = &b;
            s_sources[i].generation = 0;
            s_sources[i].framesLeft = 0;
        }
        TM_LOG("cpu script: control block at %08x (%u bytes), %d blocks\n", &s_blocks[0], sizeof(ScriptBlock),
               kScriptBlocks);
    }

    void scriptClaim(const Owner& who)
    {
        for (int i = 0; i < kScriptBlocks; i++)
        {
            ScriptSource& src = s_sources[i];
            ScriptBlock& b = s_blocks[i];
            if (!src.valid() || b.playerNo != who.playerNo || b.generation == src.generation)
            {
                continue;
            }
            src.generation = b.generation;
            b.step = 0;
            b.framesDriven = 0;
            b.startFrame = game::frameCounter();
            src.framesLeft = b.steps[0].frames != 0 ? b.steps[0].frames : 1;
            u32 flags = (b.aiOff != 0 ? kSilenceAi : 0) | (b.alsoSub != 0 ? kWithNana : 0);
            TM_LOG("cpu script: %u started for P%d, %u steps, loop %u (block %d)\n", b.generation, who.playerNo + 1,
                   b.stepCount, b.loop, i);
            take(who, &src, flags);
            return;
        }
    }
}
}
