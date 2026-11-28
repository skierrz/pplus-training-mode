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
#include "main/ui/state_colors.h"
#include "main/ui/style.h"
#include "shared/game/scene.h"
#include "shared/log.h"

// Per-player append-only FIFO OSD stacks rendered with crisp 4-way outlined text
// over a subtle translucent wash and a 3px bottom lifetime countdown bar,
// easeOutCubic stack glide transitions, and smooth 4-frame fade-out.
// Placement geometry lives in osd_place.h.
namespace tm {
namespace osd {
    enum {
        kEnterFrames = 7,
        kGlideFrames = 7,
        kExitFrames = 4
    };
    static const float kStrip = 3.0f;
    static const float kTextCap = 9.0f;
    static const float kLinePitch = 13.5f;
    // 4 px margin each side inside the compact 96 px card; wider lines scale down.
    static const float kTextWidth = 88.0f;
    static const gfx::Rgba kBar = ui::style::kLabTealTop;
    static const gfx::Rgba kOutline = 0x000000E8u;

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
    // The stack place a message is drawn at now (between `from` and `slot`
    // while it glides). No animation while frame advance holds the game.
    static float place(const Message& m, s32 now)
    {
        if (game::frozen())
        {
            return static_cast<float>(m.slot);
        }
        float t = ui::style::easeOutCubic(static_cast<float>(now - m.moved + 1) / kGlideFrames);
        return m.from + (static_cast<float>(m.slot) - m.from) * t;
    }

    static bool expired(const Message& m, s32 now)
    {
        return now < m.born || now - m.born >= kLifetime + (game::frozen() ? 0 : kExitFrames);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Drops expired messages and gives active messages of `player`'s stack their
    // places by age, starting a 7f easeOutCubic glide for every shifted card.
    static void restack(int player, s32 now)
    {
        if (s_messages == NULL)
        {
            return;
        }
        bool frozen = game::frozen();
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
            // Already-exiting cards stay at their final slot while fading out.
            if (now - m.born >= kLifetime || n >= kPerPlayer)
            {
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
                if (frozen)
                {
                    m.from = static_cast<float>(k);
                    m.moved = now - kGlideFrames;
                }
                else
                {
                    m.from = m.slot < 0 ? static_cast<float>(k) : place(m, now);
                    m.moved = now;
                }
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
        bool frozen = game::frozen();
        restack(player, now);
        // Pure append-only FIFO: every new event appends at slot 0, pushing older
        // cards smoothly upward chronologically until count reaches maxMessages.
        Message* oldest = NULL;
        Message* oldestExiting = NULL;
        Message* free = NULL;
        int count = 0;
        for (int i = 0; i < kPlayers * kPerPlayer; i++)
        {
            Message& m = s_messages[i];
            if (!m.used)
            {
                free = free == NULL ? &m : free;
                continue;
            }
            if (now - m.born >= kLifetime)
            {
                if (oldestExiting == NULL || m.born < oldestExiting->born)
                {
                    oldestExiting = &m;
                }
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
        Message* m = free != NULL ? free : oldestExiting;
        if (count >= limit && oldest != NULL)
        {
            if (!frozen && m != NULL)
            {
                // Smoothly evict oldest card over kExitFrames while reusing a free/exiting slot.
                oldest->born = now - kLifetime;
            }
            else
            {
                m = oldest;
            }
        }
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
        m->slot = -1;
        restack(player, now);
        if (frozen)
        {
            // Stamp settled state so stepping 1-4 frames then unfreezing never rewinds.
            m->from = static_cast<float>(m->slot);
            m->moved = now - kGlideFrames;
        }
        else
        {
            m->from = kEnterFrom;
            m->moved = now;
        }

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

    static gfx::Rgba fadeA(gfx::Rgba c, u32 a256)
    {
        return a256 >= 256u ? c : ((c & 0xFFFFFF00u) | (((c & 0xFFu) * a256) >> 8));
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void drawMessage(Message& m, s32 now, float scale, float floorY, float topY, int playerCount,
                            const ThemeColors& t, bool& hasPrevBot, float& prevBot)
    {
        bool frozen = game::frozen();
        int age = now - m.born;
        float alpha = 1.0f;
        float exitSlide = 0.0f;
        if (age >= kLifetime)
        {
            float u = 1.0f - static_cast<float>(age - kLifetime + 1) / static_cast<float>(kExitFrames + 1);
            if (u <= 0.0f)
            {
                return;
            }
            alpha = u * u;
            exitSlide = (1.0f - alpha) * 6.0f;
        }
        else if (!frozen && m.from < 0.0f && age < kEnterFrames)
        {
            alpha = ui::style::easeOutCubic(static_cast<float>(age + 1) / kEnterFrames);
        }
        u32 a256 = alpha >= 0.999f ? 256u : static_cast<u32>(alpha * 256.0f);

        float xs = gfx::xScale();
        float ws = widthScale(xs);
        float w = kBoxWidth * ws;
        float h = kBoxHeight;
        StackPlace at = stackPlace(s_position, m.player, xs, floorY, topY, playerCount);
        float slot = place(m, now);
        float cx = at.x + slot * at.dx;
        float cy = at.y + slot * at.dy;
        if (exitSlide > 0.0f)
        {
            if (at.dy < 0.0f) cy -= exitSlide;
            else if (at.dx != 0.0f) cx += at.dx > 0.0f ? exitSlide : -exitSlide;
        }
        bool atRest = frozen || (now - m.moved + 1 >= kGlideFrames && age < kLifetime);
        float left = cx - w * 0.5f;
        float top = cy - h * 0.5f;
        if (atRest)
        {
            left = ui::style::snapPx(left);
            top = ui::style::snapPx(top);
            cx = left + w * 0.5f;
        }
        float bottom = top + h;
        // Prevent overlap while an older card glides up above an entering card:
        // keep bottom anchored and reveal the entering card cleanly below prevBot.
        if (at.dy < 0.0f && age < kLifetime)
        {
            float minTop = prevBot + ((-at.dy) - kBoxHeight);
            if (hasPrevBot && top < minTop && m.from < static_cast<float>(m.slot))
            {
                top = minTop;
                h = bottom - top;
                if (h <= kStrip + 2.0f)
                {
                    return;
                }
            }
            if (!hasPrevBot || bottom > prevBot)
            {
                prevBot = bottom;
                hasPrevBot = true;
            }
        }
        float barY = top + h - kStrip;
        if (t.bodyColor != 0)
        {
            gfx::fillRect(left, top, w, h - kStrip, fadeA(t.bodyColor, a256));
            gfx::fillRect(left, barY, w, kStrip, fadeA(t.trackColor, a256));
        }
        float life = age < kLifetime ? 1.0f - static_cast<float>(age) / kLifetime : 0.0f;
        if (life > 0.0f)
        {
            float barW = w * life;
            if (t.bodyColor == 0)
            {
                gfx::fillRect(left + xs, barY + 1.0f, barW, kStrip, fadeA(kOutline, a256));
            }
            gfx::fillRect(left, barY, barW, kStrip, fadeA(kBar, a256));
        }
        gfx::Rgba outCol = fadeA(kOutline, a256);
        u8 cbMode = ui::activeColorblindMode();
        // Centre the lines' capital-height block vertically inside the card body (above the 3px timer bar).
        float block = (m.lines - 1) * kLinePitch + kTextCap;
        float capTop = top + (h - kStrip - block) * 0.5f;
        float y = ui::style::textYForCap(capTop, scale);
        for (int i = 0; i < m.lines; i++)
        {
            float lineCapTop = capTop + i * kLinePitch;
            if (h < kBoxHeight - 0.5f && (lineCapTop < top + 1.0f || lineCapTop + kTextCap > barY - 1.0f))
            {
                y += kLinePitch;
                continue;
            }
            if (!(m.measured & (1 << i)))
            {
                m.width[i] = gfx::textWidth(m.text[i], scale);
                m.measured |= 1 << i;
            }
            float sx = scale;
            float tw = m.width[i];
            if (tw > kTextWidth * ws)
            {
                sx = scale * kTextWidth * ws / tw;
                tw = kTextWidth * ws;
            }
            float tx = cx - tw * 0.5f;
            float ty = y;
            if (atRest)
            {
                tx = ui::style::snapPx(tx);
                ty = ui::style::snapPx(ty);
            }
            gfx::text(tx - xs, ty, m.text[i], outCol, sx, scale);
            gfx::text(tx + xs, ty, m.text[i], outCol, sx, scale);
            gfx::text(tx, ty - 1.0f, m.text[i], outCol, sx, scale);
            gfx::text(tx, ty + 1.0f, m.text[i], outCol, sx, scale);
            gfx::text(tx, ty, m.text[i], fadeA(ui::remapGradeColor(m.color[i], cbMode), a256), sx, scale);
            y += kLinePitch;
        }
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

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
            bool hasPrevBot = false;
            float prevBot = 0.0f;
            // Oldest first, so newer boxes cover older ones while they glide.
            for (int slot = kPerPlayer - 1; slot >= 0; slot--)
            {
                for (int i = 0; i < kPlayers * kPerPlayer; i++)
                {
                    Message& m = s_messages[i];
                    if (m.used && m.player == p && m.slot == slot)
                    {
                        drawMessage(m, now, scale, floorY, topY, playerCount, theme, hasPrevBot, prevBot);
                    }
                }
            }
        }
    }
}
}
