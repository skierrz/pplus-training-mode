#pragma once

#include <types.h>

// Pure host-testable stage geometry structures and sampling helpers (zero
// Brawl headers).
namespace tm {
namespace extra {
    struct BlastZones {
        float left;
        float right;
        float top;
        float bottom;
        bool valid;
    };

    struct StageFloor {
        float leftX;
        float rightX;
        float leftY;
        float rightY;
        float centerY;
        bool valid;
    };

    struct LedgeInfo {
        float x;
        float y;
        float dir; // +1 for left ledge (faces right into stage), -1 for right ledge
    };

    enum { kMaxStagePlatforms = 4 };

    struct StagePlatform {
        float leftX;
        float rightX;
        float y;
        bool valid;
    };

    struct StageGeometry {
        BlastZones blast;
        StageFloor floor;
        LedgeInfo leftLedge;
        LedgeInfo rightLedge;
        bool hasLedges;
        StagePlatform platforms[kMaxStagePlatforms];
        int platformCount;
    };

    inline u32 geomNextRng(u32& state)
    {
        u32 x = state != 0 ? state : 0x6D2B79F5u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    inline float geomRand01(u32& state)
    {
        return static_cast<float>(geomNextRng(state) & 0x00FFFFFFu) / 16777216.0f;
    }

    inline float geomClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float geomMax(float a, float b)
    {
        return a > b ? a : b;
    }

    inline float geomMin(float a, float b)
    {
        return a < b ? a : b;
    }

    inline bool nearestLedge(const StageGeometry& g, float x, float y, LedgeInfo& out)
    {
        if (!g.hasLedges)
        {
            return false;
        }
        float dlx = g.leftLedge.x - x;
        float dly = g.leftLedge.y - y;
        float drx = g.rightLedge.x - x;
        float dry = g.rightLedge.y - y;
        float dl2 = dlx * dlx + dly * dly;
        float dr2 = drx * drx + dry * dry;
        out = dl2 <= dr2 ? g.leftLedge : g.rightLedge;
        return true;
    }

    // Selects an active pass-through platform from live geometry, preferring
    // the lowest platform on the requested side (left or right of main floor
    // center) and falling back to the lowest valid platform on the stage.
    inline bool selectPlatform(const StageGeometry& g, bool preferLeft, StagePlatform& out)
    {
        int n = g.platformCount;
        if (n <= 0)
        {
            return false;
        }
        if (n > kMaxStagePlatforms)
        {
            n = kMaxStagePlatforms;
        }
        float floorMid = g.floor.valid ? 0.5f * (g.floor.leftX + g.floor.rightX) : 0.0f;
        int bestSide = -1;
        int bestAny = -1;
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (!p.valid || p.rightX <= p.leftX + 4.0f)
            {
                continue;
            }
            if (bestAny < 0 || p.y < g.platforms[bestAny].y)
            {
                bestAny = i;
            }
            float mid = 0.5f * (p.leftX + p.rightX);
            bool matchesSide = preferLeft ? (mid <= floorMid + 2.0f) : (mid >= floorMid - 2.0f);
            if (matchesSide && (bestSide < 0 || p.y < g.platforms[bestSide].y))
            {
                bestSide = i;
            }
        }
        int chosen = bestSide >= 0 ? bestSide : bestAny;
        if (chosen < 0)
        {
            return false;
        }
        out = g.platforms[chosen];
        return true;
    }

    inline bool isOffstageGeom(const StageGeometry& g, float x, float y, float margin = 0.0f)
    {
        if (!g.floor.valid)
        {
            return false;
        }
        return x < g.floor.leftX - margin || x > g.floor.rightX + margin || y < g.floor.centerY - 2.0f;
    }

    // Generates one candidate point inside the live stage's floor and blast
    // zones (never hardcoded to any single stage). Callers on the console
    // verify the point with downward and upward stRayCheck calls.
    inline bool sampleSafeCandidate(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY)
    {
        if (!g.floor.valid || !g.blast.valid)
        {
            return false;
        }
        if (onGround)
        {
            float loX = g.floor.leftX + 10.0f;
            float hiX = g.floor.rightX - 10.0f;
            if (hiX <= loX)
            {
                loX = g.floor.leftX;
                hiX = g.floor.rightX;
            }
            outX = loX + geomRand01(rng) * (hiX - loX);
            outY = g.floor.centerY + 45.0f;
            return true;
        }
        float loX = geomMax(g.blast.left + 25.0f, g.floor.leftX - 30.0f);
        float hiX = geomMin(g.blast.right - 25.0f, g.floor.rightX + 30.0f);
        if (hiX <= loX)
        {
            loX = g.floor.leftX;
            hiX = g.floor.rightX;
        }
        float loY = geomMax(g.blast.bottom + 35.0f, g.floor.centerY + 8.0f);
        float hiY = geomMin(g.blast.top - 30.0f, g.floor.centerY + 75.0f);
        if (hiY <= loY)
        {
            hiY = loY + 10.0f;
        }
        outX = loX + geomRand01(rng) * (hiX - loX);
        outY = loY + geomRand01(rng) * (hiY - loY);
        return true;
    }

    // Generates an offstage recovery-practice spawn point relative to the live
    // stage's ledges and blast zones.
    inline bool sampleRecoveryCandidate(const StageGeometry& g, u32& rng, float& outX, float& outY)
    {
        if (!g.floor.valid || !g.blast.valid || !g.hasLedges)
        {
            return false;
        }
        bool rightSide = (geomNextRng(rng) & 1u) != 0;
        const LedgeInfo& ledge = rightSide ? g.rightLedge : g.leftLedge;
        float outward = rightSide ? 1.0f : -1.0f;
        float maxOut = rightSide ? (g.blast.right - ledge.x - 28.0f) : (ledge.x - g.blast.left - 28.0f);
        maxOut = geomClamp(maxOut, 20.0f, 80.0f);
        float minOut = geomMin(25.0f, maxOut * 0.5f);
        float distOut = minOut + geomRand01(rng) * (maxOut - minOut);
        outX = ledge.x + outward * distOut;

        float loY = geomMax(g.blast.bottom + 38.0f, ledge.y - 42.0f);
        float hiY = geomMin(g.blast.top - 35.0f, ledge.y + 30.0f);
        if (hiY <= loY)
        {
            hiY = loY + 5.0f;
        }
        outY = loY + geomRand01(rng) * (hiY - loY);
        return true;
    }
}
}
