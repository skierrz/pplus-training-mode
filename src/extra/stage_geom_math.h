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

    enum {
        kPlatSlotLeft = 0,
        kPlatSlotRight = 1,
        kPlatSlotTop = 2,
        kPlatSlotRandom = 3
    };

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

    inline bool isOffstageGeom(const StageGeometry& g, float x, float y, float margin = 0.0f)
    {
        if (!g.floor.valid)
        {
            return false;
        }
        return x < g.floor.leftX - margin || x > g.floor.rightX + margin || y < g.floor.centerY - 2.0f;
    }

#if defined(__MWERKS__) && !defined(TM_STAGE_GEOM_IMPL)
    u32 geomNextRng(u32& state);
    float geomRand01(u32& state);
    float geomRandRange(u32& state, float lo, float hi);
    bool nearestLedge(const StageGeometry& g, float x, float y, LedgeInfo& out);
    void classifyPlatforms(const StageGeometry& g, int slotIdx[3]);
    bool selectPlatformBySlot(const StageGeometry& g, u8 slot, u32& rng, StagePlatform& out,
                              u8* outActualSlot = NULL);
    int sortPlatformsLowestLeftToRight(const StageGeometry& g, StagePlatform out[kMaxStagePlatforms]);
    bool selectPlatform(const StageGeometry& g, bool preferLeft, StagePlatform& out);
    bool sampleSafeCandidate(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY);
    bool sampleOffstageMode(const StageGeometry& g, int sideChoice, u8 mode, u32& rng, float& outX, float& outY);
    bool sampleRecoveryCandidate(const StageGeometry& g, u32& rng, float& outX, float& outY, int sideChoice = 2);
    bool sampleEdgeguardSpawn(const StageGeometry& g, u8 distMode, int sideChoice, u32& rng, float& outX,
                              float& outY);
#else
#ifdef __MWERKS__
#define TM_GEOM_FN
#else
#define TM_GEOM_FN inline
#endif
    TM_GEOM_FN u32 geomNextRng(u32& state)
    {
        u32 x = state != 0 ? state : 0x6D2B79F5u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    TM_GEOM_FN float geomRand01(u32& state)
    {
        return static_cast<float>(geomNextRng(state) & 0x00FFFFFFu) / 16777216.0f;
    }

    TM_GEOM_FN float geomRandRange(u32& state, float lo, float hi)
    {
        return lo + geomRand01(state) * (hi - lo);
    }

    TM_GEOM_FN bool nearestLedge(const StageGeometry& g, float x, float y, LedgeInfo& out)
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

    // Maps live platforms to canonical Left / Right / Top slots (-1 if absent).
    TM_GEOM_FN void classifyPlatforms(const StageGeometry& g, int slotIdx[3])
    {
        slotIdx[0] = slotIdx[1] = slotIdx[2] = -1;
        int n = g.platformCount;
        if (n <= 0)
        {
            return;
        }
        if (n > kMaxStagePlatforms)
        {
            n = kMaxStagePlatforms;
        }
        float floorMid = g.floor.valid ? 0.5f * (g.floor.leftX + g.floor.rightX) : 0.0f;
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (!p.valid || p.rightX <= p.leftX + 4.0f)
            {
                continue;
            }
            float mid = 0.5f * (p.leftX + p.rightX);
            if (mid < floorMid - 4.0f)
            {
                if (slotIdx[kPlatSlotLeft] < 0 || p.y < g.platforms[slotIdx[kPlatSlotLeft]].y)
                {
                    slotIdx[kPlatSlotLeft] = i;
                }
            }
            else if (mid > floorMid + 4.0f)
            {
                if (slotIdx[kPlatSlotRight] < 0 || p.y < g.platforms[slotIdx[kPlatSlotRight]].y)
                {
                    slotIdx[kPlatSlotRight] = i;
                }
            }
        }
        // Top slot: highest valid platform distinct from Left/Right (e.g. Battlefield,
        // Dream Land, FoD top), or the sole centered platform on 1-platform stages.
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (!p.valid || p.rightX <= p.leftX + 4.0f || i == slotIdx[kPlatSlotLeft] || i == slotIdx[kPlatSlotRight])
            {
                continue;
            }
            if (slotIdx[kPlatSlotTop] < 0 || p.y > g.platforms[slotIdx[kPlatSlotTop]].y)
            {
                slotIdx[kPlatSlotTop] = i;
            }
        }
    }

    // Selects a pass-through platform by canonical slot (Left / Right / Top /
    // Random), cleanly skipping absent slots on 1- or 2-platform stages.
    TM_GEOM_FN bool selectPlatformBySlot(const StageGeometry& g, u8 slot, u32& rng, StagePlatform& out,
                                         u8* outActualSlot = NULL)
    {
        int idx[3];
        classifyPlatforms(g, idx);
        u8 availSlots[3];
        int availCount = 0;
        for (int s = 0; s < 3; s++)
        {
            if (idx[s] >= 0)
            {
                availSlots[availCount++] = static_cast<u8>(s);
            }
        }
        if (availCount == 0)
        {
            return false;
        }
        u8 chosenSlot = availSlots[0];
        if (slot == kPlatSlotRandom)
        {
            chosenSlot = availSlots[geomNextRng(rng) % static_cast<u32>(availCount)];
        }
        else if (slot < 3 && idx[slot] >= 0)
        {
            chosenSlot = slot;
        }
        out = g.platforms[idx[chosenSlot]];
        if (outActualSlot != NULL)
        {
            *outActualSlot = chosenSlot;
        }
        return true;
    }

    // Collects all valid pass-through platforms sorted lowest-first (ascending Y),
    // tie-breaking left-to-right (ascending midX within 2.0u Y tolerance).
    TM_GEOM_FN int sortPlatformsLowestLeftToRight(const StageGeometry& g, StagePlatform out[kMaxStagePlatforms])
    {
        int n = g.platformCount;
        if (n > kMaxStagePlatforms) n = kMaxStagePlatforms;
        int count = 0;
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (p.valid && p.rightX > p.leftX + 4.0f) out[count++] = p;
        }
        for (int i = 0; i < count; i++)
        {
            for (int j = i + 1; j < count; j++)
            {
                float dy = out[i].y - out[j].y;
                if (dy > 2.0f || (dy >= -2.0f && (out[i].leftX + out[i].rightX) > (out[j].leftX + out[j].rightX)))
                {
                    StagePlatform tmp = out[i];
                    out[i] = out[j];
                    out[j] = tmp;
                }
            }
        }
        return count;
    }

    // Selects an active pass-through platform from live geometry, preferring
    // the lowest platform on the requested side (left or right of main floor
    // center) and falling back to the lowest valid platform on the stage.
    TM_GEOM_FN bool selectPlatform(const StageGeometry& g, bool preferLeft, StagePlatform& out)
    {
        StagePlatform sorted[kMaxStagePlatforms];
        int count = sortPlatformsLowestLeftToRight(g, sorted);
        if (count <= 0)
        {
            return false;
        }
        float floorMid = g.floor.valid ? 0.5f * (g.floor.leftX + g.floor.rightX) : 0.0f;
        for (int i = 0; i < count; i++)
        {
            float mid = 0.5f * (sorted[i].leftX + sorted[i].rightX);
            if (preferLeft ? (mid <= floorMid + 2.0f) : (mid >= floorMid - 2.0f))
            {
                out = sorted[i];
                return true;
            }
        }
        out = sorted[0];
        return true;
    }

    // Generates one candidate point inside the live stage's floor and blast
    // zones (never hardcoded to any single stage). Callers on the console
    // verify the point with downward and upward stRayCheck calls.
    TM_GEOM_FN bool sampleSafeCandidate(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY)
    {
        if (!g.floor.valid || !g.blast.valid)
        {
            return false;
        }
        float loX = onGround ? (g.floor.leftX + 10.0f) : geomMax(g.blast.left + 25.0f, g.floor.leftX - 30.0f);
        float hiX = onGround ? (g.floor.rightX - 10.0f) : geomMin(g.blast.right - 25.0f, g.floor.rightX + 30.0f);
        if (hiX <= loX)
        {
            loX = g.floor.leftX;
            hiX = g.floor.rightX;
        }
        outX = geomRandRange(rng, loX, hiX);
        if (onGround)
        {
            outY = g.floor.centerY + 45.0f;
            return true;
        }
        float loY = geomMax(g.blast.bottom + 35.0f, g.floor.centerY + 8.0f);
        float hiY = geomMin(g.blast.top - 30.0f, g.floor.centerY + 75.0f);
        if (hiY <= loY)
        {
            hiY = loY + 10.0f;
        }
        outY = geomRandRange(rng, loY, hiY);
        return true;
    }

    // Shared offstage box sampler relative to the live stage's ledges & blast zones.
    // mode: 0 = Recovery, 1 = Edgeguard Mid, 2 = Edgeguard Deep, 3 = Edgeguard High.
    TM_GEOM_FN bool sampleOffstageMode(const StageGeometry& g, int sideChoice, u8 mode, u32& rng, float& outX,
                                       float& outY)
    {
        if (!g.floor.valid || !g.blast.valid || !g.hasLedges)
        {
            return false;
        }
        static const float kBoxes[4][4] = {
            {25.0f, 80.0f, -42.0f, 30.0f},
            {32.0f, 50.0f, -18.0f, 6.0f},
            {48.0f, 74.0f, -44.0f, -16.0f},
            {28.0f, 52.0f, 18.0f, 44.0f}
        };
        const float* b = kBoxes[mode < 4 ? mode : 0];
        float pad = mode == 0 ? 28.0f : 26.0f;
        float loFrac = mode == 0 ? 0.50f : 0.75f;
        bool rightSide = sideChoice < 2 ? (sideChoice == 0) : ((geomNextRng(rng) & 1u) != 0);
        const LedgeInfo& ledge = rightSide ? g.rightLedge : g.leftLedge;
        float outward = rightSide ? 1.0f : -1.0f;
        float maxOut = geomMax(20.0f, rightSide ? (g.blast.right - ledge.x - pad) : (ledge.x - g.blast.left - pad));
        float hiD = geomMin(b[1], maxOut);
        float loD = geomMin(b[0], hiD * loFrac);
        outX = ledge.x + outward * geomRandRange(rng, loD, hiD);

        float loY = geomMax(g.blast.bottom + 38.0f, ledge.y + b[2]);
        float hiY = geomMin(g.blast.top - 32.0f, ledge.y + b[3]);
        if (hiY <= loY) hiY = loY + 4.0f;
        outY = geomRandRange(rng, loY, hiY);
        return true;
    }

    // Generates an offstage recovery-practice spawn point relative to the live
    // stage's ledges and blast zones. sideChoice: 0 = Right, 1 = Left, 2 = Random.
    TM_GEOM_FN bool sampleRecoveryCandidate(const StageGeometry& g, u32& rng, float& outX, float& outY,
                                            int sideChoice = 2)
    {
        return sampleOffstageMode(g, sideChoice, 0, rng, outX, outY);
    }

    // Generates an offstage Edgeguard spawn point for explicit distance presets:
    // distMode: 0 = Mid, 1 = Deep, 2 = High, 3 = Random.
    // sideChoice: 0 = Right, 1 = Left, 2 = Random.
    TM_GEOM_FN bool sampleEdgeguardSpawn(const StageGeometry& g, u8 distMode, int sideChoice, u32& rng, float& outX,
                                         float& outY)
    {
        u8 mode = distMode >= 3 ? static_cast<u8>(geomNextRng(rng) % 3u) : distMode;
        return sampleOffstageMode(g, sideChoice, static_cast<u8>(mode + 1), rng, outX, outY);
    }
#undef TM_GEOM_FN
#endif
}
}
