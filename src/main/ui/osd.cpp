#include "main/ui/osd.h"

#include <cstdarg>
#include <cstdio>
#include <string.h>
#ifdef __MWERKS__
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#endif

#include "main/extra.h"
#include "main/game/freeze.h"
#include "main/game/tracker.h"
#include "main/gfx/text.h"
#include "main/record/record.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/osd_theme_core.h"
#include "main/ui/style.h"
#include "shared/game/scene.h"
#include "shared/log.h"

// Per-player OSD message stacks rendered in a themed P+ card with a bottom
// lifetime strip, smoothstep glide transitions, and fold-out expiry.
// Placement geometry lives in osd_place.h.
namespace tm {
namespace osd {
    static const float kStrip = 3.0f;
    static const float kTextCap = 9.0f;
    static const float kLinePitch = 13.5f;
    // 6 px margin each side inside the 124 px card; wider lines scale down.
    static const float kTextWidth = 112.0f;
    static const gfx::Rgba kBar = 0x38BDF8FF;

    static int s_position = kPositionSides;

    struct Message {
        bool used;
        s8 player;
        u8 lines;
        u8 measured; // bit per line: width known
        s16 id;
        s16 slot;     // place in the stack, 0 = newest
        s32 born;     // tracker frame
        s32 moved;    // tracker frame the glide to `slot` began
        float from;   // stack place the glide started from (kEnterFrom = below)
        float width[kMaxLines];
        gfx::Rgba color[kMaxLines];
        char text[kMaxLines][kLineChars + 1];
    };

#ifdef __MWERKS__
    static Message* s_messages = NULL;
#else
    static Message s_messagesStatic[kPlayers * kPerPlayer];
    static Message* s_messages = s_messagesStatic;
#endif

    void attach()
    {
#ifdef __MWERKS__
        if (s_messages == NULL)
        {
            s_messages = static_cast<Message*>(game::allocNetworkZero(static_cast<u32>(sizeof(Message) * kPlayers * kPerPlayer)));
            if (s_messages == NULL)
            {
                TM_LOG("osd: messages alloc failed (%u B in Network)\n",
                       static_cast<unsigned>(sizeof(Message) * kPlayers * kPerPlayer));
            }
        }
#endif
        reset();
    }

    void detach()
    {
        reset();
#ifdef __MWERKS__
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_messages));
#endif
    }

    void reset()
    {
        if (s_messages == NULL)
        {
            return;
        }
        for (int i = 0; i < kPlayers * kPerPlayer; i++)
        {
            s_messages[i].used = false;
        }
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static float smooth(float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    // The stack place a message is drawn at now (between `from` and `slot`
    // while it glides). No animation while frame advance holds the game.
    static float place(const Message& m, s32 now)
    {
        if (game::frozen())
        {
            return m.slot;
        }
        float t = smooth(static_cast<float>(now - m.moved) / kAnimFrames);
        return m.from + (m.slot - m.from) * t;
    }

    static bool expired(const Message& m, s32 now)
    {
        return now < m.born || now - m.born >= kLifetime + (game::frozen() ? 0 : kAnimFrames);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Drops expired messages and gives the rest of `player`'s stack their
    // places by age, starting a glide for every message whose place changed.
    static void restack(int player, s32 now)
    {
        if (s_messages == NULL)
        {
            return;
        }
        Message* order[kPerPlayer];
        int n = 0;
        for (int i = 0; i < kPlayers * kPerPlayer; i++)
        {
            Message& m = s_messages[i];
            if (!m.used || m.player != player)
            {
                continue;
            }
            if (expired(m, now))
            {
                m.used = false;
                continue;
            }
            int j = n++;
            while (j > 0 && order[j - 1]->born < m.born)
            {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = &m;
        }
        for (int k = 0; k < n; k++)
        {
            Message& m = *order[k];
            if (m.slot != k)
            {
                // Pushing older cards up to make room for a new arrival completes
                // immediately so they never overlap the entering card mid-glide.
                m.from = place(m, now);
                m.moved = (m.slot >= 0 && k > m.slot) ? now - kAnimFrames : now;
                m.slot = static_cast<s16>(k);
            }
        }
    }

    Message* post(int player, int id, gfx::Rgba color, const char* format, ...)
    {
        if (s_messages == NULL || player < 0 || player >= kPlayers)
        {
            return NULL;
        }
        s32 now = game::trackerFrame();
        restack(player, now);
        // Same id: the old message goes; if it was the newest, the new one
        // takes its place at once instead of sliding in.
        bool instant = false;
        Message* oldest = NULL;
        Message* free = NULL;
        int count = 0;
        for (int i = 0; i < kPlayers * kPerPlayer; i++)
        {
            Message& m = s_messages[i];
            if (m.used && m.player == player && m.id == id)
            {
                instant = instant || m.slot == 0;
                m.used = false;
            }
            if (!m.used)
            {
                free = free == NULL ? &m : free;
                continue;
            }
            if (m.player == player)
            {
                count++;
                oldest = oldest == NULL || m.born < oldest->born ? &m : oldest;
            }
        }
        int limit = main::settings().osd.maxMessages;
        limit = limit < 1 || limit > kPerPlayer ? kPerPlayer : limit;
        Message* m = count >= limit ? oldest : free;
        if (m == NULL)
        {
            return NULL;
        }
        char buf[kMaxLines * (kLineChars + 1)];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);

        m->used = true;
        m->player = static_cast<s8>(player);
        m->id = static_cast<s16>(id);
        m->born = now;
        m->measured = 0;
        m->lines = 0;
        const char* p = buf;
        while (m->lines < kMaxLines)
        {
            const char* end = p;
            while (*end != '\0' && *end != '\n')
            {
                end++;
            }
            u32 len = static_cast<u32>(end - p);
            len = len > kLineChars ? kLineChars : len;
            memcpy(m->text[m->lines], p, len);
            m->text[m->lines][len] = '\0';
            m->color[m->lines] = color;
            m->lines++;
            if (*end == '\0')
            {
                break;
            }
            p = end + 1;
        }
        // Placed below the stack and glided up into place, unless it
        // replaced the newest message.
        m->slot = -1;
        restack(player, now);
        if (instant)
        {
            m->from = 0.0f;
        }
        else
        {
            m->from = kEnterFrom;
        }
        m->moved = now;

        TM_LOG("osd: P%d id %d: %s%s%s%s%s\n", player + 1, id, m->text[0], m->lines > 1 ? " | " : "",
               m->lines > 1 ? m->text[1] : "", m->lines > 2 ? " | " : "", m->lines > 2 ? m->text[2] : "");
        return m;
    }

    void lineColor(Message* m, int line, gfx::Rgba color)
    {
        if (m != NULL && line >= 0 && line < m->lines)
        {
            m->color[line] = color;
        }
    }

    void setPosition(int position)
    {
        s_position = position >= 0 && position < kPositionCount ? position : kPositionHud;
    }

    bool visible()
    {
        if (s_messages == NULL)
        {
            return false;
        }
        s32 now = game::trackerFrame();
        for (int i = 0; i < kPlayers * kPerPlayer; i++)
        {
            const Message& m = s_messages[i];
            if (m.used && !expired(m, now))
            {
                return true;
            }
        }
        return false;
    }

    static void drawMessage(Message& m, s32 now, float scale, float floorY, float topY, int playerCount,
                            const ThemeColors& t)
    {
        float xs = gfx::xScale();
        float w = kBoxWidth * xs;
        StackPlace at = stackPlace(s_position, m.player, xs, floorY, topY, playerCount);
        float slot = place(m, now);
        float cx = at.x + slot * at.dx;
        float cy = at.y + slot * at.dy;
        int age = now - m.born;
        // Closing: the box and its text squash to their centre line.
        float fold = age < kLifetime ? 1.0f : 1.0f - smooth(static_cast<float>(age - kLifetime) / kAnimFrames);
        if (fold <= 0.0f)
        {
            return;
        }
        float h = kBoxHeight * fold;
        float left = cx - w * 0.5f;
        float top = cy - h * 0.5f;
        float strip = kStrip * fold;
        float barY = top + h - strip;
        if (t.bodyColor != 0)
        {
            gfx::fillRect(left, top, w, h - strip, t.bodyColor);
            gfx::fillRect(left, barY, w, strip, t.trackColor);
        }
        float life = age < kLifetime ? 1.0f - static_cast<float>(age) / kLifetime : 0.0f;
        if (life > 0.0f)
        {
            float barW = w * life;
            if (t.shadowColor != 0)
            {
                gfx::fillRect(left + xs, barY + 1.0f, barW, strip, t.shadowColor);
            }
            gfx::fillRect(left, barY, barW, strip, kBar);
        }
        // Centre the lines' capital-height block vertically inside the body.
        float sy = scale * fold;
        float pitch = kLinePitch * fold;
        float cap = kTextCap * fold;
        float block = (m.lines - 1) * pitch + cap;
        float capTop = top + (h - strip - block) * 0.5f;
        float y = ui::style::textYForCap(capTop, sy);
        for (int i = 0; i < m.lines; i++)
        {
            if (!(m.measured & (1 << i)))
            {
                m.width[i] = gfx::textWidth(m.text[i], scale);
                m.measured |= 1 << i;
            }
            float sx = scale;
            float tw = m.width[i];
            if (tw > kTextWidth * xs)
            {
                sx = scale * kTextWidth * xs / tw;
                tw = kTextWidth * xs;
            }
            float tx = cx - tw * 0.5f;
            if (t.shadowColor != 0)
            {
                gfx::text(tx + xs, y + 1.0f, m.text[i], t.shadowColor, sx, sy);
            }
            gfx::text(tx, y, m.text[i], m.color[i], sx, sy);
            y += pitch;
        }
    }

    void draw()
    {
        if (s_messages == NULL)
        {
            return;
        }
        float unit = gfx::lineHeight(1.0f);
        if (unit <= 0.0f)
        {
            return;
        }
        float scale = ui::style::scaleForCap(kTextCap);
        const main::Settings& s = main::settings();
        ThemeColors theme;
        resolveThemeTo(s.size ? s.osd.bgColor : static_cast<u8>(main::kOsdBgTeal),
                       s.size ? s.osd.bgAlpha : static_cast<u8>(main::kDefaultOsdAlpha), theme);
        const main::RecordingSettings& rec = s.recording;
        bool recBar = record::positionsSaved()
                      && (rec.hmnMode != 0
                          || (rec.cpuMode != 0 && s.cpu.options.control == cpu::kControlLab));
        float floorY = recBar ? ui::layout::recBarOsdFloor() : 0.0f;
        if (!game::trainingGameMode())
        {
            // Stock Versus renders stock icons at y ~ 384..397 above portraits;
            // cap bottom OSDs at 374 so even frame-0 glide-in (+4.9 px) stays clear.
            if (floorY <= 0.0f || floorY > 374.0f)
            {
                floorY = 374.0f;
            }
        }
        float topY = 0.0f;
        if (s_position == kPositionTopLeft || s_position == kPositionTopRight)
        {
            topY = main::extraOsdTopStartY(s_position == kPositionTopRight);
            if (topY <= 0.0f)
            {
                using namespace ui::layout;
                Box band = topBand();
                topY = (!empty(band) && band.y1 + kStepGap > kSafeMarginY) ? (band.y1 + kStepGap) : kSafeMarginY;
            }
        }
        int playerCount = 2;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* tr = game::tracked(i);
            if (tr != NULL && tr->playerNo >= 2)
            {
                playerCount = 4;
                break;
            }
        }
        for (int i = 0; i < kPlayers * kPerPlayer && playerCount <= 2; i++)
        {
            if (s_messages[i].used && s_messages[i].player >= 2)
            {
                playerCount = 4;
            }
        }
        s32 now = game::trackerFrame();
        for (int p = 0; p < kPlayers; p++)
        {
            restack(p, now);
            // Oldest first, so newer boxes cover older ones while they glide.
            for (int slot = kPerPlayer - 1; slot >= 0; slot--)
            {
                for (int i = 0; i < kPlayers * kPerPlayer; i++)
                {
                    Message& m = s_messages[i];
                    if (m.used && m.player == p && m.slot == slot)
                    {
                        drawMessage(m, now, scale, floorY, topY, playerCount, theme);
                    }
                }
            }
        }
    }
}
}
