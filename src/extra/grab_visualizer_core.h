#pragma once

#include <string.h>
#include <types.h>

// Pure logic, colors, filtering, and geometry validation for the Grab, Tether
// Search, and Item-Pickup Visualizer (zero Brawl headers, zero heap).
namespace tm {
namespace extra {
    enum {
        kGrabVizWhoNone = 0,
        kGrabVizWhoHuman = 1,
        kGrabVizWhoCpu = 2,
        kGrabVizWhoBoth = 3,
        kGrabVizMaxWho = 3,
        kGrabVizMaxFighters = 4
    };

    enum GrabVizBoxKind {
        kGrabVizGrab = 0,
        kGrabVizSearch = 1,
        kGrabVizItem = 2,
        kGrabVizKindCount = 3
    };

    // High-contrast RGBA (0xRRGGBBAA) per collision family:
    //   Grab boxes (soCollisionCatchModule): vivid magenta/purple
    //   Search / Tether boxes (soCollisionSearchModule): vivid cyan/teal
    //   Item-pickup boxes (soAreaModule Fighter_Group_Item_Pickup): bright sky-cyan
    static const u32 kGrabVizGrabColor = 0xD946EFB8u;
    static const u32 kGrabVizSearchColor = 0x06B6D4B8u;
    static const u32 kGrabVizItemBubbleColor = 0x38BDF8B8u;
    static const u32 kGrabVizItemLineColor = 0x38BDF8E0u;

    inline u8 grabVizClampWho(u8 who)
    {
        return who > kGrabVizMaxWho ? kGrabVizMaxWho : who;
    }

    inline bool grabVizWhoMatches(u8 who, bool cpu)
    {
        return (who & (cpu ? kGrabVizWhoCpu : kGrabVizWhoHuman)) != 0;
    }

    inline u32 grabVizColor(u8 kind)
    {
        switch (kind)
        {
        case kGrabVizGrab:
            return kGrabVizGrabColor;
        case kGrabVizSearch:
            return kGrabVizSearchColor;
        case kGrabVizItem:
            return kGrabVizItemBubbleColor;
        default:
            return kGrabVizGrabColor;
        }
    }

    // Half-intensity secondary color for kBubble shading (matches P+ 0x80576768).
    inline u32 grabVizShadeColor(u32 rgba)
    {
        u32 r = ((rgba >> 24) & 0xFFu) >> 1;
        u32 g = ((rgba >> 16) & 0xFFu) >> 1;
        u32 b = ((rgba >> 8) & 0xFFu) >> 1;
        u32 a = rgba & 0xFFu;
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    inline bool grabVizValidRadius(float r)
    {
        return r > 0.0f && r < 500.0f;
    }

    inline bool grabVizValidCoord(float v)
    {
        return v > -10000.0f && v < 10000.0f;
    }

    // Validates a 2D quad from gfAreaRect: finite, bounded coordinates and
    // non-degenerate width or height (> 0.01 units, < 500 units).
    inline bool grabVizValidRect(const float xs[4], const float ys[4])
    {
        float minX = xs[0];
        float maxX = xs[0];
        float minY = ys[0];
        float maxY = ys[0];
        for (int i = 0; i < 4; i++)
        {
            if (!grabVizValidCoord(xs[i]) || !grabVizValidCoord(ys[i]))
            {
                return false;
            }
            if (xs[i] < minX)
            {
                minX = xs[i];
            }
            if (xs[i] > maxX)
            {
                maxX = xs[i];
            }
            if (ys[i] < minY)
            {
                minY = ys[i];
            }
            if (ys[i] > maxY)
            {
                maxY = ys[i];
            }
        }
        float w = maxX - minX;
        float h = maxY - minY;
        return (w > 0.01f || h > 0.01f) && w < 500.0f && h < 500.0f;
    }

    inline void grabVizIdentityMatrix34(float m[3][4])
    {
        memset(m, 0, sizeof(float) * 12);
        m[0][0] = 1.0f;
        m[1][1] = 1.0f;
        m[2][2] = 1.0f;
    }
}
}
