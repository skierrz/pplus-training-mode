#include "main/ui/debug_overlay.h"

#include <cstdio>

#include "main/game/advantage.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/style.h"
#include "shared/game/frame.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "shared/version.h"

namespace tm {
namespace ui {
    static const float kLeft = 36.0f;
    static const float kTop = 28.0f;
    static const float kPadding = 4.0f;
    static const float kScale = style::kTypeMicro / style::kCapHeight;
    static const gfx::Rgba kBackground = 0x000000A0;
    static const gfx::Rgba kTextColor = 0xFFFFFFFF;

    // Width of the widest line last frame: the box is drawn before the text,
    // so it follows the text one frame late.
    static float s_boxWidth = 0.0f;
    static bool s_loggedFirstText = false;

    static const char* matchText(const game::MatchState& match)
    {
        if (!match.running)
        {
            return game::inTraining() ? " (Training)" : "";
        }
        if (!match.training)
        {
            return " - match";
        }
        return match.paused ? " - Training match, paused" : " - Training match";
    }

    void drawDebugOverlay(u32 call, const game::MatchState& match)
    {
        char frame[24];
        char scene[80];
        snprintf(frame, sizeof(frame), "frame %u", game::frameCounter());
        snprintf(scene, sizeof(scene), "%s / %s%s", game::sceneName(), game::sequenceName(),
                 matchText(match));
        // Once there is one, the last frame advantage's inputs (hit frame,
        // each side's first actionable frame with its status and the one
        // before it), for checking a message against the game.
        char adv[96];
        const game::Advantage* a = game::lastAdvantage();
        if (a != NULL)
        {
            snprintf(adv, sizeof(adv), "adv P%d>P%d %s %+d: hit %d, A %d %X<%X, D %d %X<%X, x%d",
                     a->attackerPlayer + 1, a->defenderPlayer + 1, a->shield ? "shield" : "hit", a->value, a->hitFrame,
                     a->attackerFrame, a->attackerStatus, a->attackerPrev, a->defenderFrame, a->defenderStatus,
                     a->defenderPrev, a->hits);
        }
        const char* lines[4] = {TM_VERSION_MARK, frame, scene, adv};
        int count = a != NULL ? 4 : 3;

        gfx::begin();
        float lineHeight = gfx::lineHeight(kScale);
        if (lineHeight > 0.0f)
        {
            gfx::fillRect(kLeft - kPadding, kTop - kPadding, s_boxWidth + 2 * kPadding,
                          count * lineHeight + 2 * kPadding, kBackground);
            float widest = 0.0f;
            for (int i = 0; i < count; i++)
            {
                float right = gfx::text(kLeft, kTop + i * lineHeight, lines[i], kTextColor, kScale);
                if (right - kLeft > widest)
                {
                    widest = right - kLeft;
                }
            }
            s_boxWidth = widest;
            if (!s_loggedFirstText)
            {
                s_loggedFirstText = true;
                TM_LOG("overlay: text first drawn at render call %u (%s)\n", call, game::sceneName());
            }
        }
        gfx::end();
    }
}
}
