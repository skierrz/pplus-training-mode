#pragma once

#include <string.h>
#include <types.h>

// Hitbox heatmap ring buffer, color-band mapping, actionable-frame
// counter, and Z-plane AABB reach bounds (algorithm adapted from fudgepop01's
// SuperTraining hitbox heatmap; see CREDITS.md and THIRD_PARTY_NOTICES.md).
//
// Zero Brawl headers and zero heap allocation: compiled directly into both the
// extra module (BSS) and PC host unit tests.
namespace tm {
namespace extra {
    enum {
        kHeatmapRing = 192,
        kHeatmapMaxFighters = 4,
        kHeatmapDefaultLife = 60,
        kHeatmapMinOpacity = 20,
        kHeatmapMaxOpacity = 100,
        kHeatmapDefaultOpacity = 60,
        kHeatmapMaxAlpha = 120,
        kHeatmapMinColorFrames = 1,
        kHeatmapMaxColorFrames = 30,
        kHeatmapDefaultColorFrames = 5
    };

    // Fighter-relative and world-space AABB reach across Z-plane-intersecting
    // attack capsules (-r < z < r).
    struct HitboxBounds {
        float xmin;
        float xmax;
        float ymin;
        float ymax;
        float width;
        float height;
        float worldXMin;
        float worldXMax;
        float worldYMin;
        float worldYMax;
        float originX;
        float originY;
        u8 valid;
        u8 pad[3];
    };

    struct HeatmapEntry {
        float pos1[3];
        float pos2[3];
        float scaleMatrix[3][4];
        float radius;
        u8 playerSlot;
        u8 frameOffset;
        u8 life;
        u8 active;
    };

    struct HeatmapRing {
        HeatmapEntry entries[kHeatmapRing];
        u16 head;
        u16 count;
        u32 totalPushed;

        void clear()
        {
            memset(entries, 0, sizeof(entries));
            head = 0;
            count = 0;
            totalPushed = 0;
        }

        void push(const HeatmapEntry& in)
        {
            HeatmapEntry& dst = entries[head];
            if (dst.active == 0)
            {
                count++;
            }
            dst = in;
            dst.active = 1;
            head = static_cast<u16>((head + 1) % kHeatmapRing);
            totalPushed++;
        }

        void tickAge()
        {
            for (int i = 0; i < kHeatmapRing; i++)
            {
                HeatmapEntry& e = entries[i];
                if (e.active == 0)
                {
                    continue;
                }
                if (e.life <= 1)
                {
                    e.life = 0;
                    e.active = 0;
                    if (count > 0)
                    {
                        count--;
                    }
                }
                else
                {
                    e.life--;
                }
            }
        }
    };

    inline void boundsReset(HitboxBounds& b)
    {
        memset(&b, 0, sizeof(b));
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline void boundsExpandPoint(HitboxBounds& b, float originX, float originY, float px, float py, float r)
    {
        float x0 = px - r;
        float x1 = px + r;
        float y0 = py - r;
        float y1 = py + r;
        if (!b.valid)
        {
            b.originX = originX;
            b.originY = originY;
            b.worldXMin = x0;
            b.worldXMax = x1;
            b.worldYMin = y0;
            b.worldYMax = y1;
            b.valid = 1;
        }
        else
        {
            if (x0 < b.worldXMin)
            {
                b.worldXMin = x0;
            }
            if (x1 > b.worldXMax)
            {
                b.worldXMax = x1;
            }
            if (y0 < b.worldYMin)
            {
                b.worldYMin = y0;
            }
            if (y1 > b.worldYMax)
            {
                b.worldYMax = y1;
            }
        }
        b.xmin = b.worldXMin - b.originX;
        b.xmax = b.worldXMax - b.originX;
        b.ymin = b.worldYMin - b.originY;
        b.ymax = b.worldYMax - b.originY;
        b.width = b.worldXMax - b.worldXMin;
        b.height = b.worldYMax - b.worldYMin;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Accumulates a capsule segment [pos1, pos2] of world radius `radius` into
    // `b` whenever either endpoint intersects the Z=0 gameplay plane (-r < z < r)
    // or the segment crosses Z=0.
    inline bool boundsAccumulateCapsule(HitboxBounds& b, float originX, float originY, const float pos1[3],
                                        const float pos2[3], float radius)
    {
        if (!(radius > 0.0f))
        {
            return false;
        }
        bool hit = false;
        if (-radius < pos1[2] && pos1[2] < radius)
        {
            boundsExpandPoint(b, originX, originY, pos1[0], pos1[1], radius);
            hit = true;
        }
        if (-radius < pos2[2] && pos2[2] < radius)
        {
            boundsExpandPoint(b, originX, originY, pos2[0], pos2[1], radius);
            hit = true;
        }
        if (!hit && ((pos1[2] < 0.0f && pos2[2] > 0.0f) || (pos1[2] > 0.0f && pos2[2] < 0.0f)))
        {
            float dz = pos2[2] - pos1[2];
            float t = -pos1[2] / dz;
            float ix = pos1[0] + t * (pos2[0] - pos1[0]);
            float iy = pos1[1] + t * (pos2[1] - pos1[1]);
            boundsExpandPoint(b, originX, originY, ix, iy, radius);
            hit = true;
        }
        return hit;
    }

    // Statuses that reset framesAfterActionableGround to 0: Wait (0x0),
    // Walk (0x1), SquatWait (0x12), Guard (0x1B), DamageFall/tumble (0x49),
    // or late Landing (0x16) / LandingLight (0x17) past frame 4.
    inline bool isHeatmapResetStatus(int status, float animFrame)
    {
        if (status == 0x0 || status == 0x1 || status == 0x12 || status == 0x1B || status == 0x49)
        {
            return true;
        }
        if ((status == 0x16 || status == 0x17) && animFrame > 4.0f)
        {
            return true;
        }
        return false;
    }

    inline u8 stepActionableGroundFrames(u8 cur, int status, float animFrame)
    {
        if (isHeatmapResetStatus(status, animFrame))
        {
            return 0;
        }
        return cur < 255 ? static_cast<u8>(cur + 1) : 255;
    }

    // Strictly guards colorChangeFrame == 0 division by zero by clamping >= 1.
    inline u8 clampColorFrames(u8 colorChangeFrame)
    {
        return colorChangeFrame < 1 ? 1 : colorChangeFrame;
    }

    // Scales opacity percentage (0..100) into a translucent per-capsule alpha
    // ceiling (0..kHeatmapMaxAlpha = 0x78) so overlapping trail capsules build
    // density smoothly without saturating into an opaque wall over fighters.
    inline u8 opacityPercentToAlpha(u8 pct)
    {
        u32 p = pct > 100 ? 100u : static_cast<u32>(pct);
        return static_cast<u8>((p * static_cast<u32>(kHeatmapMaxAlpha) + 50u) / 100u);
    }

    // Maps `frameOffset` across 4 color bands of `colorChangeFrame` frames each:
    //   Band 0: Red    (255,   0,   0) -> Yellow (255, 255,   0)
    //   Band 1: Yellow (255, 255,   0) -> Green  (  0, 255,   0)
    //   Band 2: Green  (  0, 255,   0) -> Cyan   (  0, 255, 255)
    //   Band 3: Cyan   (  0, 255, 255) -> Blue   (  0,   0, 255)
    //   Band 4+: Blue  (  0,   0, 255)
    // Returns big-endian 0xRRGGBBAA with alphaByte (0..255).
    inline u32 heatmapColor(u8 frameOffset, u8 colorChangeFrame, u8 alphaByte)
    {
        u32 step = clampColorFrames(colorChangeFrame);
        u32 band = static_cast<u32>(frameOffset) / step;
        u32 frac = ((static_cast<u32>(frameOffset) % step) * 255u) / step;
        u32 r = 0;
        u32 g = 0;
        u32 b = 0;
        switch (band)
        {
        case 0:
            r = 255u;
            g = frac;
            b = 0u;
            break;
        case 1:
            r = 255u - frac;
            g = 255u;
            b = 0u;
            break;
        case 2:
            r = 0u;
            g = 255u;
            b = frac;
            break;
        case 3:
            r = 0u;
            g = 255u - frac;
            b = 255u;
            break;
        default:
            r = 0u;
            g = 0u;
            b = 255u;
            break;
        }
        return (r << 24) | (g << 16) | (b << 8) | static_cast<u32>(alphaByte);
    }

    // Half-intensity secondary color for kBubble shading (matches P+ 0x80576768).
    inline u32 heatmapShadeColor(u32 rgba)
    {
        u32 r = ((rgba >> 24) & 0xFFu) >> 1;
        u32 g = ((rgba >> 16) & 0xFFu) >> 1;
        u32 b = ((rgba >> 8) & 0xFFu) >> 1;
        u32 a = rgba & 0xFFu;
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    inline void identityMatrix34(float m[3][4])
    {
        memset(m, 0, sizeof(float) * 12);
        m[0][0] = 1.0f;
        m[1][1] = 1.0f;
        m[2][2] = 1.0f;
    }
}
}
