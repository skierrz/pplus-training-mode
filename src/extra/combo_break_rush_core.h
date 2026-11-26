#pragma once

#include <string.h>
#include <types.h>

#include "extra/stage_geom_math.h"

// Pure host-testable Break the Targets! challenge engine: stage-aware 3D target
// spawning, live attack hitbox intersection, combo multiplier x1..x8, chain
// decay timer, Timed (30s/60s) and Free Practice modes, plus streaming LZ77
// extractor for Brawl's native STGTARGET CMPR bullseye TEX0 (zero Brawl headers).
namespace tm {
namespace extra {
    enum ComboBreakMode {
        kRushOff = 0,
        kRushTimed30 = 1,
        kRushTimed60 = 2,
        kRushFree = 3,
        kRushModeCount = 4
    };

    enum {
        kRushMaxTargets = 4,
        kRushActiveSlots = 3,
        kRushMaxMult = 8,
        kRushChainMaxFrames = 210, // 3.5 seconds at 60fps
        kRushTimed30Frames = 1800, // 30 seconds
        kRushTimed60Frames = 3600, // 60 seconds
        kRushBurstFrames = 14,
        kRushRespawnDelay = 10,
        kRushBestSlot = 17, // drills::kDrillComboBreakRush (drills::Session::best[17])
        kRushDebugMagic = 0x544D4252u, // "TMBR"
        kRushDebugVersion = 2,
        kRushCmdIdle = 0xFF,
        kRushCmdSnapTarget = 0xFE,
        kRushTexSrcWidth = 256,
        kRushTexSrcHeight = 256,
        kRushTexWidth = 128,
        kRushTexHeight = 128,
        kRushTexFmtCmpr = 14,
        kRushTexMip0Bytes = 32768, // 256x256 GX_TF_CMPR mip 0 (skipped)
        kRushTexMip1Bytes = 8192,  // 128x128 GX_TF_CMPR mip 1 (resident, 8 KiB)
        kRushLzRingSize = 4096
    };

    inline u8 rushNextMode(u8 mode)
    {
        if (mode == kRushFree) return static_cast<u8>(kRushTimed30);
        if (mode == kRushTimed30) return static_cast<u8>(kRushTimed60);
        return static_cast<u8>(kRushFree);
    }

    struct RushTarget {
        float x;
        float y;
        float radius;
        u8 active;     // 0 = empty, 1 = live, 2 = breaking shatter
        u8 tier;       // 0 = standard floor/arc, 1 = platform arc, 2 = gold bonus
        u8 animFrames; // spawn pop (0..12) when live, shatter countdown (14..0) when breaking
        u8 cooldown;   // frames before respawning when empty
    };

    struct ComboBreakState {
        u8 mode;       // ComboBreakMode
        u8 finished;   // 1 when timed countdown reached 0
        u8 multiplier; // 1..kRushMaxMult
        u8 hitEvent;   // transient SFX cue: 0=none, 1=break, 2=milestone, 3=finish
        u32 score;
        u32 bestScore;
        u16 streak;
        u16 bestStreak;
        u16 breaks;
        u16 chainFrames;
        u16 timerFrames;
        u16 tickCount;
        u32 rng;
        RushTarget targets[kRushMaxTargets];
    };

    // RAM debug block exposed for live verification ("TMBR").
    // Offsets 0x00..0x57 are layout-compatible with v1; v2 appends texLoaded/cpuHidden at 0x58.
    struct ComboBreakDebugBlock {
        u32 magic;          // 0x00 "TMBR"
        u32 version;        // 0x04 = 2
        u8 mode;            // 0x08 active ComboBreakMode
        u8 cmdMode;         // 0x09 0xFF idle; 0..3 switch mode; 0xFE snap target 0 in front of P1
        u8 active;          // 0x0A 1 if mode != kRushOff
        u8 finished;        // 0x0B 1 if timed round ended
        u8 multiplier;      // 0x0C 1..8
        u8 activeTargets;   // 0x0D count of live targets
        u8 stagePlatforms;  // 0x0E live platform count from StageGeometry
        u8 shapeOk;         // 0x0F collShapeSupported()
        u32 score;          // 0x10
        u32 bestScore;      // 0x14
        u16 streak;         // 0x18
        u16 breaks;         // 0x1A
        u16 chainFrames;    // 0x1C
        u16 timerFrames;    // 0x1E
        u32 draw3dCalls;    // 0x20
        u32 draw2dCalls;    // 0x24
        float targetX[kRushMaxTargets]; // 0x28
        float targetY[kRushMaxTargets]; // 0x38
        float targetR[kRushMaxTargets]; // 0x48
        u8 texLoaded;       // 0x58 1 when Brawl STGTARGET CMPR texture is resident
        u8 cpuHidden;       // 0x59 1 when CPU is parked via shared solo standby
        u8 lastSfxOk;       // 0x5A 1 if native Brawl break SFX handle >= 0
        u8 lastVfxOk;       // 0x5B 1 if native ef_common burst spawned
    };

    // Streaming LZ77 + TEX0 extractor for Brawl STGTARGET*.PAC Outer[1].
    // Uses only a 4 KiB sliding window and extracts the 128x128 CMPR mip 1
    // (8,192 B) from Brawl's 256x256 mipped target TEX0 without decompressing
    // the 4.3 MiB stage archive, fitting comfortably above kCostumeReserve even
    // on Ice Climbers vs Olimar on Fountain of Dreams.
    struct RushTexStream {
        u8* ring;       // caller-supplied 4096 B sliding window
        u8* outTex;     // caller-supplied 8192 B destination (128x128 CMPR mip 1)
        u32 outPos;     // total decompressed bytes produced so far
        u32 outLimit;   // uncompressed stream length from LZ77 header
        u32 texCopied;  // bytes written into outTex (0..8192)
        u32 skipData;   // bytes between TEX0 header end and mip 1 data
        u8 hdrPos;      // bytes read of 4-byte LZ77 header
        u8 lzHeader[4];
        u8 flags;       // current LZ77 flag byte
        u8 bitIdx;      // 0..8 remaining flag bits
        u8 pendingHi;   // 1 if holding first byte of 2-byte back-ref
        u8 hiByte;
        u8 scanState;   // 0=seek TEX0, 1=read 64B TEX0 hdr, 2=skip to mip1, 3=copy mip1
        u8 texHdrPos;
        u8 texHdr[64];
        u8 done;
        u8 failed;
    };

    // Parses an outer Brawl ARC header (first 256 bytes) to locate Entry 1's
    // file offset and compressed byte length (e.g. STGTARGETLv5.PAC Outer[1]).
    inline bool rushParseOuterArcEntry1(const u8* hdr, u32 hdrLen, u32& outStreamOff, u32& outCompSize)
    {
        outStreamOff = 0u;
        outCompSize = 0u;
        if (hdr == NULL || hdrLen < 0xA0u) return false;
        if (hdr[0] != 'A' || hdr[1] != 'R' || hdr[2] != 'C' || hdr[3] != 0) return false;
        u16 count = static_cast<u16>((static_cast<u16>(hdr[6]) << 8) | hdr[7]);
        if (count < 2u) return false;
        // Entry 0 header at 0x40: +4..+7 = size0 (big-endian).
        u32 size0 = (static_cast<u32>(hdr[0x44]) << 24) | (static_cast<u32>(hdr[0x45]) << 16)
                    | (static_cast<u32>(hdr[0x46]) << 8) | hdr[0x47];
        // Guard against u32 wrap in ((size0 + 31) & ~31) + 0x60 + 0x20.
        if (size0 > hdrLen - 0x80u || size0 > 0x00100000u) return false;
        u32 e1 = 0x60u + ((size0 + 31u) & ~31u);
        if (e1 + 0x20u > hdrLen) return false;
        u32 compSize = (static_cast<u32>(hdr[e1 + 4]) << 24) | (static_cast<u32>(hdr[e1 + 5]) << 16)
                       | (static_cast<u32>(hdr[e1 + 6]) << 8) | hdr[e1 + 7];
        if (compSize < 16u || compSize > 8u * 1024u * 1024u) return false;
        outStreamOff = e1 + 0x20u;
        outCompSize = compSize;
        return true;
    }

    inline void rushTexStreamInit(RushTexStream& s, u8* ring4096, u8* outTex8192)
    {
        memset(&s, 0, sizeof(s));
        s.ring = ring4096;
        s.outTex = outTex8192;
    }

    inline u32 rushBe32(const u8* p)
    {
        return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16)
               | (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    }

    inline u16 rushBe16(const u8* p)
    {
        return static_cast<u16>((static_cast<u16>(p[0]) << 8) | static_cast<u16>(p[1]));
    }

    inline void rushTexEmitByte(RushTexStream& s, u8 b)
    {
        s.ring[s.outPos & (kRushLzRingSize - 1u)] = b;
        s.outPos++;
        if (s.done) return;

        if (s.scanState == 0u)
        {
            static const u8 kMagic[4] = {'T', 'E', 'X', '0'};
            if (b == kMagic[s.texHdrPos])
            {
                s.texHdr[s.texHdrPos++] = b;
                if (s.texHdrPos == 4u)
                {
                    s.scanState = 1u;
                }
            }
            else
            {
                s.texHdrPos = (b == 'T') ? 1u : 0u;
                if (s.texHdrPos == 1u) s.texHdr[0] = 'T';
            }
        }
        else if (s.scanState == 1u)
        {
            s.texHdr[s.texHdrPos++] = b;
            if (s.texHdrPos == 64u)
            {
                u32 blkSz = rushBe32(s.texHdr + 4);
                u32 ofsData = rushBe32(s.texHdr + 16);
                u16 w = rushBe16(s.texHdr + 28);
                u16 h = rushBe16(s.texHdr + 30);
                u32 fmt = rushBe32(s.texHdr + 32);
                if (w == kRushTexSrcWidth && h == kRushTexSrcHeight && fmt == kRushTexFmtCmpr
                    && ofsData >= 64u && ofsData <= 128u
                    && blkSz >= ofsData + kRushTexMip0Bytes + kRushTexMip1Bytes)
                {
                    s.skipData = (ofsData - 64u) + kRushTexMip0Bytes;
                    s.texCopied = 0u;
                    s.scanState = 2u;
                }
                else
                {
                    s.scanState = 0u;
                    s.texHdrPos = 0u;
                }
            }
        }
        else if (s.scanState == 2u)
        {
            if (--s.skipData == 0u) s.scanState = 3u;
        }
        else if (s.scanState == 3u)
        {
            s.outTex[s.texCopied++] = b;
            if (s.texCopied >= kRushTexMip1Bytes)
            {
                s.done = 1u;
            }
        }
    }

    inline void rushTexStreamFeed(RushTexStream& s, const u8* src, u32 len)
    {
        if (s.ring == NULL || s.outTex == NULL || src == NULL || s.done || s.failed) return;
        u32 i = 0u;
        while (i < len && !s.done && !s.failed)
        {
            if (s.hdrPos < 4u)
            {
                s.lzHeader[s.hdrPos++] = src[i++];
                if (s.hdrPos == 4u)
                {
                    if (s.lzHeader[0] != 0x10u)
                    {
                        s.failed = 1u;
                        return;
                    }
                    s.outLimit = static_cast<u32>(s.lzHeader[1]) | (static_cast<u32>(s.lzHeader[2]) << 8)
                                 | (static_cast<u32>(s.lzHeader[3]) << 16);
                    if (s.outLimit < kRushTexMip0Bytes)
                    {
                        s.failed = 1u;
                        return;
                    }
                }
                continue;
            }
            if (s.outPos >= s.outLimit)
            {
                s.failed = 1u;
                return;
            }
            if (s.pendingHi)
            {
                u8 b2 = src[i++];
                s.pendingHi = 0u;
                u32 count = static_cast<u32>((s.hiByte >> 4) & 0xFu) + 3u;
                u32 disp = (static_cast<u32>(s.hiByte & 0xFu) << 8) | static_cast<u32>(b2);
                disp += 1u;
                if (disp > s.outPos)
                {
                    s.failed = 1u;
                    return;
                }
                for (u32 k = 0u; k < count && s.outPos < s.outLimit && !s.done; k++)
                {
                    u8 v = s.ring[(s.outPos - disp) & (kRushLzRingSize - 1u)];
                    rushTexEmitByte(s, v);
                }
                continue;
            }
            if (s.bitIdx == 0u)
            {
                s.flags = src[i++];
                s.bitIdx = 8u;
                continue;
            }
            bool comp = (s.flags & 0x80u) != 0u;
            s.flags = static_cast<u8>(s.flags << 1);
            s.bitIdx--;
            if (comp)
            {
                s.hiByte = src[i++];
                s.pendingHi = 1u;
            }
            else
            {
                rushTexEmitByte(s, src[i++]);
            }
        }
    }

#if defined(__MWERKS__) && !defined(TM_COMBO_BREAK_CORE_IMPL)
    void rushInit(ComboBreakState& st, u8 mode, u32 priorBest, u32 seed);
    bool rushHitboxHitsTarget(const float pos1[3], const float pos2[3], float hitRadius, const RushTarget& t);
    bool rushSpawnOne(ComboBreakState& st, int slot, const StageGeometry& g, float fighterX, float fighterY);
    u8 rushCountLive(const ComboBreakState& st);
    void rushBreakTarget(ComboBreakState& st, int slot);
    void rushStepFrame(ComboBreakState& st, const StageGeometry& g, float fighterX, float fighterY);
#else
#ifdef __MWERKS__
#define TM_RUSH_CORE_FN
#pragma force_active on
#pragma dont_inline on
#else
#define TM_RUSH_CORE_FN inline
#endif
    static inline float rushRandRange(u32& rng, float lo, float hi)
    {
        u32 x = rng != 0u ? rng : 0x6D2B79F5u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng = x;
        return lo + static_cast<float>(x & 0xFFFFu) * (hi - lo) * (1.0f / 65536.0f);
    }

    TM_RUSH_CORE_FN void rushInit(ComboBreakState& st, u8 mode, u32 priorBest, u32 seed)
    {
        memset(&st, 0, sizeof(st));
        st.mode = mode < kRushModeCount ? mode : static_cast<u8>(kRushOff);
        st.multiplier = 1;
        st.bestScore = priorBest;
        st.rng = seed != 0u ? seed : 0xC0B0B2E4u;
        st.timerFrames = st.mode == kRushTimed30 ? kRushTimed30Frames
                                                 : (st.mode == kRushTimed60 ? kRushTimed60Frames : 0);
    }

    // Exact 2D segment-to-circle intersection for swept hitbox capsule vs target disc.
    TM_RUSH_CORE_FN bool rushHitboxHitsTarget(const float pos1[3], const float pos2[3], float hitRadius,
                                              const RushTarget& t)
    {
        if (t.active != 1 || hitRadius <= 0.0f)
        {
            return false;
        }
        float sumR = hitRadius + t.radius;
        float z = pos1[2] < 0.0f ? -pos1[2] : pos1[2];
        if (z > sumR + 10.0f)
        {
            return false;
        }
        float abx = pos1[0] - pos2[0];
        float aby = pos1[1] - pos2[1];
        float len2 = abx * abx + aby * aby;
        float u = 0.0f;
        if (len2 > 1e-5f)
        {
            u = ((t.x - pos2[0]) * abx + (t.y - pos2[1]) * aby) / len2;
            if (u < 0.0f) u = 0.0f;
            else if (u > 1.0f) u = 1.0f;
        }
        float dx = t.x - (pos2[0] + u * abx);
        float dy = t.y - (pos2[1] + u * aby);
        return (dx * dx + dy * dy) <= (sumR * sumR);
    }

    // Spawns one target inside reachable live StageGeometry airspace/platform arcs.
    TM_RUSH_CORE_FN bool rushSpawnOne(ComboBreakState& st, int slot, const StageGeometry& g, float fighterX,
                                      float fighterY)
    {
        if (slot < 0 || slot >= kRushMaxTargets)
        {
            return false;
        }
        float floorL = g.floor.valid ? g.floor.leftX : -68.0f;
        float floorR = g.floor.valid ? g.floor.rightX : 68.0f;
        float floorY = g.floor.valid ? g.floor.centerY : 0.0f;
        float safeL = floorL + 10.0f;
        float safeR = floorR - 10.0f;
        if (g.blast.valid)
        {
            if (g.blast.left + 28.0f > safeL) safeL = g.blast.left + 28.0f;
            if (g.blast.right - 28.0f < safeR) safeR = g.blast.right - 28.0f;
        }
        if (safeR <= safeL + 8.0f) { safeL = floorL; safeR = floorR; }
        // Keep vertical target airspace comfortably between bottom % portraits and top HUD plate.
        float minY = floorY + 12.0f;
        float maxY = floorY + 46.0f;
        if (g.blast.valid && g.blast.top - 32.0f < maxY) maxY = g.blast.top - 32.0f;
        if (maxY < minY + 10.0f) maxY = minY + 10.0f;

        int lim = g.platformCount > kMaxStagePlatforms ? kMaxStagePlatforms : g.platformCount;
        int eligPlats[kMaxStagePlatforms];
        int eligCount = 0;
        for (int i = 0; i < lim; i++)
        {
            if (g.platforms[i].valid && g.platforms[i].rightX > g.platforms[i].leftX + 4.0f
                && g.platforms[i].y <= maxY - 8.5f)
            {
                eligPlats[eligCount++] = i;
            }
        }
        float cx = 0.5f * (safeL + safeR);
        float cy = minY + 6.0f;
        u8 tier = 0;

        for (int attempt = 0; attempt < 8; attempt++)
        {
            float r0 = rushRandRange(st.rng, 0.0f, 1.0f);
            u32 roll = st.rng;
            int pi = (eligCount > 0 && (roll % 5u) < 3u)
                         ? eligPlats[static_cast<int>((roll >> 3) % static_cast<u32>(eligCount))]
                         : -1;
            if (pi >= 0)
            {
                const StagePlatform& p = g.platforms[pi];
                float pl = p.leftX + 3.0f; if (pl < safeL) pl = safeL;
                float pr = p.rightX - 3.0f; if (pr > safeR) pr = safeR;
                if (pr < pl) { pl = safeL; pr = safeR; }
                cx = pl + r0 * (pr - pl);
                if (cx < safeL) cx = safeL;
                if (cx > safeR) cx = safeR;
                float hiY = p.y + 14.5f;
                if (hiY > maxY) hiY = maxY;
                float loY = p.y + 7.5f;
                if (loY > hiY) loY = hiY;
                cy = rushRandRange(st.rng, loY, hiY);
                tier = ((roll >> 8) % 5u == 0u) ? 2 : 1;
            }
            else
            {
                cx = safeL + r0 * (safeR - safeL);
                cy = rushRandRange(st.rng, minY, floorY + 30.0f);
                tier = ((roll >> 8) % 6u == 0u) ? 2 : 0;
            }
            if (cy < minY) cy = minY;
            if (cy > maxY) cy = maxY;

            bool clear = true;
            for (int k = 0; k < kRushActiveSlots && clear; k++)
            {
                if (k != slot && st.targets[k].active == 1)
                {
                    float dx = cx - st.targets[k].x, dy = cy - st.targets[k].y;
                    if (dx * dx + dy * dy < 324.0f) clear = false;
                }
            }
            if (clear && attempt < 6)
            {
                float dx = cx - fighterX, dy = cy - (fighterY + 8.0f);
                if (dx * dx + dy * dy < 121.0f) clear = false;
            }
            if (clear) break;
        }

        RushTarget& t = st.targets[slot];
        t.x = cx;
        t.y = cy;
        t.radius = (tier == 2) ? 4.0f : 4.8f;
        t.active = 1;
        t.tier = tier;
        t.animFrames = 0;
        t.cooldown = 0;
        return true;
    }

    TM_RUSH_CORE_FN u8 rushCountLive(const ComboBreakState& st)
    {
        u8 n = 0;
        for (int i = 0; i < kRushMaxTargets; i++)
        {
            if (st.targets[i].active == 1) n++;
        }
        return n;
    }

    TM_RUSH_CORE_FN void rushBreakTarget(ComboBreakState& st, int slot)
    {
        if (slot < 0 || slot >= kRushMaxTargets || st.targets[slot].active != 1 || st.finished)
        {
            return;
        }
        RushTarget& t = st.targets[slot];
        st.multiplier = (st.chainFrames > 0)
                            ? static_cast<u8>(st.multiplier < kRushMaxMult ? st.multiplier + 1 : kRushMaxMult)
                            : 1;
        st.chainFrames = kRushChainMaxFrames;
        st.score += (t.tier == 2 ? 150u : 100u) * static_cast<u32>(st.multiplier);
        if (st.score > st.bestScore) st.bestScore = st.score;
        if (++st.streak > st.bestStreak) st.bestStreak = st.streak;
        st.breaks++;
        t.active = 2;
        t.animFrames = kRushBurstFrames;
        t.cooldown = 0;
        u8 ev = (st.multiplier == 4 || st.multiplier == kRushMaxMult) ? 2 : 1;
        if (ev > st.hitEvent) st.hitEvent = ev;
    }

    TM_RUSH_CORE_FN void rushStepFrame(ComboBreakState& st, const StageGeometry& g, float fighterX, float fighterY)
    {
        st.hitEvent = 0;
        if (st.mode == kRushOff) return;
        st.tickCount++;
        if (st.finished) return;

        if (st.chainFrames > 0 && --st.chainFrames == 0)
        {
            st.multiplier = 1;
            st.streak = 0;
        }
        if ((st.mode == kRushTimed30 || st.mode == kRushTimed60) && st.timerFrames > 0 && --st.timerFrames == 0)
        {
            st.finished = 1;
            st.hitEvent = 3;
            return;
        }

        for (int i = 0; i < kRushActiveSlots; i++)
        {
            RushTarget& t = st.targets[i];
            if (t.active == 1)
            {
                if (t.animFrames < 20) t.animFrames++;
            }
            else if (t.active == 2)
            {
                if (t.animFrames > 0 && --t.animFrames == 0)
                {
                    t.active = 0;
                    t.cooldown = kRushRespawnDelay;
                }
            }
            else if (t.cooldown > 0)
            {
                t.cooldown--;
            }
            else
            {
                rushSpawnOne(st, i, g, fighterX, fighterY);
            }
        }
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#pragma force_active reset
#endif
#undef TM_RUSH_CORE_FN
#endif
}
}
