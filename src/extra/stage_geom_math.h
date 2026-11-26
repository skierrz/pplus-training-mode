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

    enum {
        kPlatSlotLeft = 0,
        kPlatSlotRight = 1,
        kPlatSlotTop = 2,
        kPlatSlotRandom = 3
    };

    // Maps live platforms to canonical Left / Right / Top slots (-1 if absent).
    inline void classifyPlatforms(const StageGeometry& g, int slotIdx[3])
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
        int validCount = 0;
        int onlyIdx = -1;
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (!p.valid || p.rightX <= p.leftX + 4.0f)
            {
                continue;
            }
            validCount++;
            onlyIdx = i;
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
        // Dream Land, FoD top), or the sole platform on 1-platform stages (Smashville).
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
        if (validCount == 1 && slotIdx[kPlatSlotTop] < 0 && onlyIdx >= 0)
        {
            // Keep Left/Right if Smashville's platform is currently off-center,
            // otherwise expose it as Top (center).
            if (slotIdx[kPlatSlotLeft] < 0 && slotIdx[kPlatSlotRight] < 0)
            {
                slotIdx[kPlatSlotTop] = onlyIdx;
            }
        }
    }

    // Selects a pass-through platform by canonical slot (Left / Right / Top /
    // Random), cleanly skipping absent slots on 1- or 2-platform stages.
    inline bool selectPlatformBySlot(const StageGeometry& g, u8 slot, u32& rng, StagePlatform& out,
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
    inline int sortPlatformsLowestLeftToRight(const StageGeometry& g, StagePlatform out[kMaxStagePlatforms])
    {
        int n = g.platformCount;
        if (n <= 0)
        {
            return 0;
        }
        if (n > kMaxStagePlatforms)
        {
            n = kMaxStagePlatforms;
        }
        int count = 0;
        for (int i = 0; i < n; i++)
        {
            const StagePlatform& p = g.platforms[i];
            if (p.valid && p.rightX > p.leftX + 4.0f)
            {
                out[count++] = p;
            }
        }
        for (int i = 1; i < count; i++)
        {
            StagePlatform key = out[i];
            float keyMid = 0.5f * (key.leftX + key.rightX);
            int j = i - 1;
            while (j >= 0)
            {
                float dy = out[j].y - key.y;
                float jMid = 0.5f * (out[j].leftX + out[j].rightX);
                if (!((dy > 2.0f) || (dy >= -2.0f && jMid > keyMid)))
                {
                    break;
                }
                out[j + 1] = out[j];
                j--;
            }
            out[j + 1] = key;
        }
        return count;
    }

    // Selects an active pass-through platform from live geometry, preferring
    // the lowest platform on the requested side (left or right of main floor
    // center) and falling back to the lowest valid platform on the stage.
    inline bool selectPlatform(const StageGeometry& g, bool preferLeft, StagePlatform& out)
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

    // Shared offstage box sampler relative to the live stage's ledges & blast zones.
    inline bool sampleOffstageBox(const StageGeometry& g, int sideChoice, float minD, float maxD, float minRelY,
                                  float maxRelY, float edgePad, float loFrac, u32& rng, float& outX, float& outY)
    {
        if (!g.floor.valid || !g.blast.valid || !g.hasLedges)
        {
            return false;
        }
        bool rightSide = sideChoice == 0 ? true : (sideChoice == 1 ? false : ((geomNextRng(rng) & 1u) != 0));
        const LedgeInfo& ledge = rightSide ? g.rightLedge : g.leftLedge;
        float outward = rightSide ? 1.0f : -1.0f;
        float maxOut = rightSide ? (g.blast.right - ledge.x - edgePad) : (ledge.x - g.blast.left - edgePad);
        maxOut = geomMax(20.0f, maxOut);
        float hiD = geomMin(maxD, maxOut);
        float loD = geomMin(minD, hiD * loFrac);
        outX = ledge.x + outward * (loD + geomRand01(rng) * (hiD - loD));

        float loY = geomMax(g.blast.bottom + 38.0f, ledge.y + minRelY);
        float hiY = geomMin(g.blast.top - 32.0f, ledge.y + maxRelY);
        if (hiY <= loY)
        {
            hiY = loY + 4.0f;
        }
        outY = loY + geomRand01(rng) * (hiY - loY);
        return true;
    }

    // Generates an offstage recovery-practice spawn point relative to the live
    // stage's ledges and blast zones. sideChoice: 0 = Right, 1 = Left, 2 = Random.
    inline bool sampleRecoveryCandidate(const StageGeometry& g, u32& rng, float& outX, float& outY, int sideChoice = 2)
    {
        return sampleOffstageBox(g, sideChoice, 25.0f, 80.0f, -42.0f, 30.0f, 28.0f, 0.5f, rng, outX, outY);
    }

    // Generates an offstage Edgeguard spawn point for explicit distance presets:
    // distMode: 0 = Mid, 1 = Deep, 2 = High, 3 = Random.
    // sideChoice: 0 = Right, 1 = Left, 2 = Random.
    inline bool sampleEdgeguardSpawn(const StageGeometry& g, u8 distMode, int sideChoice, u32& rng, float& outX,
                                     float& outY)
    {
        static const float kEgg[3][4] = {
            {32.0f, 50.0f, -18.0f, 6.0f},   // Mid
            {48.0f, 74.0f, -44.0f, -16.0f}, // Deep
            {28.0f, 52.0f, 18.0f, 44.0f}    // High
        };
        u8 mode = distMode >= 3 ? static_cast<u8>(geomNextRng(rng) % 3u) : distMode;
        return sampleOffstageBox(g, sideChoice, kEgg[mode][0], kEgg[mode][1], kEgg[mode][2], kEgg[mode][3], 26.0f,
                                 0.75f, rng, outX, outY);
    }
}
}
