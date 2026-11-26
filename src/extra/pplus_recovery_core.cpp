#include "extra/pplus_recovery_core.h"

#include <string.h>

#include "extra/stage_geom_math.h"

namespace tm {
namespace extra {
    // Common fighter status IDs (BrawlHeaders ft/fighter.h).
    enum {
        kStJump = 0x0B,
        kStJumpAerial = 0x0C,
        kStFly = 0x0D,
        kStFall = 0x0E,
        kStFallAerial = 0x0F,
        kStFallSpecial = 0x10,
        kStLandingFallSpecial = 0x19,
        kStWallJump = 0x67,
        kStAttachWall = 0x68,
        kStAttachWallWait = 0x69,
        kStCliffCatchMove = 0x73,
        kStCliffCatch = 0x74,
        kStCliffWait = 0x75,
        kStCliffJump2 = 0x7A,
        kStCliffLast = 0x7B,
        kStAirLasso = 0x7F,
        kStAirLassoReach = 0x80,
        kStAirLassoHang = 0x81,
        kStAirLassoRewind = 0x82,
        kStAirLassoFailure = 0x83,
        kStGlideStart = 0x84,
        kStGlide = 0x85,
        kStSpecialN = 0x112,
        kStSpecialS = 0x113,
        kStSpecialHi = 0x114,
        kStSpecialLw = 0x115,
        kStDamageFall = 0x49,
        kMixupMixed = 0,
        kMixupLow = 1,
        kMixupHigh = 2,
        kMixupDirect = 3
    };

    // Portable fast square root (zero libm dependency for MWCC and MSVC).
#pragma dont_inline on
    static float recSqrt(float s)
    {
        if (s <= 0.0f)
        {
            return 0.0f;
        }
        float g = s > 1.0f ? s * 0.5f : 1.0f;
        for (int i = 0; i < 8; i++)
        {
            g = 0.5f * (g + s / g);
        }
        return g;
    }
#pragma dont_inline reset

    static inline float recAbs(float v)
    {
        return v < 0.0f ? -v : v;
    }

    static inline float recSign(float v)
    {
        return v < 0.0f ? -1.0f : 1.0f;
    }

    static inline u32 freshButton(u32 btn, u32 lastButtons)
    {
        return (lastButtons & btn) != 0 ? 0u : btn;
    }

#pragma dont_inline on
    static float recClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static float recRand01(u32& rng)
    {
        return geomRand01(rng);
    }
#pragma dont_inline reset

    // ---- WeightedDie -----------------------------------------------------------

    void WeightedDie::clear()
    {
        m_count = 0;
    }

    void WeightedDie::add(int option, float weight)
    {
        if (weight <= 0.0f)
        {
            return;
        }
        for (int i = 0; i < m_count; i++)
        {
            if (m_entries[i].option == option)
            {
                m_entries[i].weight += weight;
                return;
            }
        }
        if (m_count >= kWeightedDieCap)
        {
            return;
        }
        m_entries[m_count].option = option;
        m_entries[m_count].weight = weight;
        m_count++;
    }

    float WeightedDie::totalWeight() const
    {
        float sum = 0.0f;
        for (int i = 0; i < m_count; i++)
        {
            sum += m_entries[i].weight;
        }
        return sum;
    }

    int WeightedDie::roll(float u01, int defaultOpt) const
    {
        if (m_count <= 0)
        {
            return defaultOpt;
        }
        float total = totalWeight();
        if (total <= 0.0f)
        {
            return defaultOpt;
        }
        float u = recClamp(u01, 0.0f, 0.999999f) * total;
        for (int i = 0; i < m_count; i++)
        {
            u -= m_entries[i].weight;
            if (u < 0.0f)
            {
                return m_entries[i].option;
            }
        }
        return m_entries[m_count - 1].option;
    }

    // ---- 44-Fighter ROM Table (indexed by ftKind 0x00..0x31) -------------------
    // Constants sourced from fudgepop01/PPlusAI Chars/*/ai_*/103_recoveryHub.as.
#define SPEC(fam, flg, ux, uy, sx, sy, ax, ay, nco, ncn, fs, sf, ps, pe, pt, la, dj) \
    {flg, fs, 1, fam, ux, uy, sx, sy, ax, ay, sf, la}
#define UNUSED_SPEC {kRfNone, 0, 0, kFamArc, 40, 45, 0, 0, 0, 0, 0, 0}

    static const FighterRecoverySpec kSpecs[kFtKindMax] = {
        // 0x00 Mario
        SPEC(kFamCharged, kRfMashDownB, 36, 45, 140, 4, 40, 35, 5, 3, 0, 0, 0, 0, 0, 0x74, 34),
        // 0x01 Donkey Kong
        SPEC(kFamArc, kRfNone, 84, 40, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 36),
        // 0x02 Link
        SPEC(kFamTether, kRfTether, 33, 47, 0, 0, 30, 20, 6, 4, 0, 0, 0, 0, 0, 0, 30),
        // 0x03 Samus
        SPEC(kFamTether, kRfTether | kRfBombStall, 28, 50, 0, 0, 200, 50, 6, 4, 0, 0, 0, 0, 0, 0, 36),
        // 0x04 Yoshi (DJ-centric)
        SPEC(kFamArc, kRfNone, 20, 25, 15, 17, 0, 0, 5, 3, 0, 0, 0, 0, 0, 0, 48),
        // 0x05 Kirby
        SPEC(kFamMultiJump, kRfNone, 35, 37, 0, 0, 55, 20, 6, 4, 0, 0, 0, 0, 0, 0, 26),
        // 0x06 Fox
        SPEC(kFamSpacie, kRf360Aim | kRfSideBShorten, 85, 85, 100, -5, 100, 0, 1, 1, 0, 18, 0, 0, 0, 0, 34),
        // 0x07 Pikachu
        SPEC(kFamTeleport, kRf360Aim | kRfQuickAttack, 90, 90, 80, 0, 0, 0, 5, 3, 0, 0, 0, 0, 0, 0, 34),
        // 0x08 Luigi
        SPEC(kFamCharged, kRfMashDownB | kRfChargeSideB, 5, 42, 80, 4, 40, 35, 5, 3, 0, 25, 0, 0, 0, 0x3D, 36),
        // 0x09 Captain Falcon
        SPEC(kFamArc, kRfNone, 70, 42, 50, -5, 100, 0, 6, 2, 0, 0, 0, 0, 0, 0, 34),
        // 0x0A Ness
        SPEC(kFamPkThunder, kRfPkThunder, 100, 80, 0, 0, 55, 8, 6, 4, 0, 0, 60, 6, 20, 0, 36),
        // 0x0B Bowser
        SPEC(kFamArc, kRfNone, 75, 33, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x0C Peach
        SPEC(kFamFloat, kRfFloat, 50, 36, 50, -10, 220, 0, 5, 3, 0x11E, 0, 0, 0, 0, 0x3E, 30),
        // 0x0D Zelda
        SPEC(kFamTeleport, kRf360Aim, 70, 60, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 32),
        // 0x0E Sheik
        SPEC(kFamTeleport, kRf360Aim, 73, 70, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 36),
        // 0x0F Popo
        SPEC(kFamArc, kRfNone, 57, 50, 80, 30, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x10 Nana
        SPEC(kFamArc, kRfNone, 57, 50, 80, 30, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x11 Marth
        SPEC(kFamArc, kRfNone, 28, 50, 90, 80, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x12 Mr. Game & Watch
        SPEC(kFamArc, kRfNone, 90, 90, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x13 Falco
        SPEC(kFamSpacie, kRf360Aim | kRfSideBShorten, 55, 55, 95, -5, 100, 0, 6, 4, 0, 14, 0, 0, 0, 0, 34),
        // 0x14 Ganondorf
        SPEC(kFamFloat, kRfFloat | kRfNeutralBRecovery, 37, 42, 40, 1, 65, 20, 6, 4, 0, 0, 0, 0, 0, 0x62, 30),
        // 0x15 Wario
        SPEC(kFamCharged, kRfWaft, 47, 50, 50, 10, 40, 40, 5, 3, 0, 0, 0, 0, 0, 0x42, 34),
        // 0x16 Meta Knight
        SPEC(kFamMultiJump, kRfGlide | kRfNeutralBRecovery, 10, 48, 73, 20, 100, 30, 6, 4, 0, 0, 0, 0, 0, 0, 26),
        // 0x17 Pit
        SPEC(kFamMultiJump, kRfGlide, 23, 30, 0, 0, 85, 5, 6, 4, 0, 0, 0, 0, 0, 0, 26),
        // 0x18 Zero Suit Samus
        SPEC(kFamTether, kRfTether | kRfFlipDownB, 50, 30, 50, 10, 75, 25, 6, 4, 0, 0, 0, 0, 0, 0, 38),
        // 0x19 Olimar
        SPEC(kFamTether, kRfTether, 58, 50, 0, 0, 58, 50, 10, 5, 0, 0, 0, 0, 0, 0, 32),
        // 0x1A Lucas
        SPEC(kFamPkThunder, kRfPkThunder, 100, 80, 0, 0, 65, 8, 6, 4, 0, 0, 40, 6, 30, 0, 36),
        // 0x1B Diddy Kong
        SPEC(kFamCharged, kRfChargeUpB, 85, 58, 90, -4, 105, 15, 10, 5, 0, 45, 0, 0, 0, 0x71, 36),
        // 0x1C Pokemon Trainer (unused directly; forms use 0x1D..0x1F)
        UNUSED_SPEC,
        // 0x1D Charizard
        SPEC(kFamMultiJump, kRfGlide, 37, 45, 0, 0, 85, 5, 6, 4, 0, 0, 0, 0, 0, 0, 30),
        // 0x1E Squirtle
        SPEC(kFamArc, kRfSideBCancel, 51, 70, 150, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x1F Ivysaur
        SPEC(kFamTether, kRfTether | kRfDairStall, 50, 60, 0, 0, 150, 80, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x20 King Dedede
        SPEC(kFamMultiJump, kRfNone, 53, 90, 30, 30, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 26),
        // 0x21 Lucario
        SPEC(kFamTeleport, kRf360Aim | kRfAuraCancel, 70, 70, 0, 0, 90, 20, 6, 4, 0, 0, 0, 0, 0, 0x1B, 34),
        // 0x22 Ike
        SPEC(kFamCharged, kRfChargeSideB | kRfWallJump, 42, 50, 80, -4, 100, 0, 6, 4, 0, 35, 0, 0, 0, 91, 32),
        // 0x23 R.O.B.
        SPEC(kFamCharged, kRfBoostUpB, 150, 150, 0, 0, 0, 0, 6, 4, 0, 23, 0, 0, 0, 0, 32),
        // 0x24 Plusle/Minun (unused)
        UNUSED_SPEC,
        // 0x25 Jigglypuff
        SPEC(kFamMultiJump, kRfNone, 5, 0, 100, 100, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 24),
        // 0x26 Mewtwo (P+ clone)
        SPEC(kFamTeleport, kRf360Aim | kRfFloat, 69, 55, 90, 4, 70, 70, 6, 4, 0x120, 0, 0, 0, 0, 0x73, 40),
        // 0x27 Roy (P+ clone)
        SPEC(kFamArc, kRfNone, 48, 40, 100, 80, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x28 Dr. Mario (unused)
        UNUSED_SPEC,
        // 0x29 Toon Link
        SPEC(kFamTether, kRfTether, 31, 45, 0, 0, 30, 20, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x2A Toon Zelda (unused)
        UNUSED_SPEC,
        // 0x2B Toon Sheik (unused)
        UNUSED_SPEC,
        // 0x2C Wolf
        SPEC(kFamSpacie, kRf360Aim | kRfSideBShorten, 90, 52, 100, 8, 100, 0, 1, 1, 0, 17, 0, 0, 0, 0, 32),
        // 0x2D Knuckles (P+ clone in Dixie slot)
        SPEC(kFamCharged, kRfWallClimb, 34, 40, 80, 50, 0, 0, 6, 4, 0, 0, 0, 0, 0, 89, 34),
        // 0x2E Snake
        SPEC(kFamCharged, kRfBoostUpB, 90, 69, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0x3D, 32),
        // 0x2F Sonic
        SPEC(kFamArc, kRfNone, 25, 69, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 36),
        // 0x30 Giga Bowser
        SPEC(kFamArc, kRfNone, 65, 45, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 34),
        // 0x31 Wario-Man
        SPEC(kFamTether, kRfTether, 60, 60, 70, 15, 60, 40, 6, 4, 0, 0, 0, 0, 0, 0, 38)
    };
#undef SPEC
#undef UNUSED_SPEC

    static const FighterRecoverySpec kFallbackSpec = {
        kRfNone, 0, 1, kFamArc, 45, 50, 0, 0, 0, 0, 0, 0
    };

    const FighterRecoverySpec& recoverySpec(int ftKind)
    {
        if (ftKind >= 0 && ftKind < kFtKindMax && kSpecs[ftKind].valid)
        {
            return kSpecs[ftKind];
        }
        return kFallbackSpec;
    }

    void clampAimNotches(float& x, float& y)
    {
        if (x > 0.1f && x < 0.25f)
        {
            x = 0.3f;
        }
        else if (x < -0.1f && x > -0.25f)
        {
            x = -0.3f;
        }
        if (y > 0.0f && y < 0.3f)
        {
            y = 0.3f;
        }
    }

    void recoveryStateReset(RecoveryState& st)
    {
        memset(&st, 0, sizeof(st));
        st.prevStatus = -1;
    }

#pragma dont_inline on
    // Aim normalized vector from fighter toward (dx, dy) with PPlusAI notch clamp.
    static void aim360(float dx, float dy, float& outX, float& outY)
    {
        float len = recSqrt(dx * dx + dy * dy);
        if (len < 0.001f)
        {
            outX = 0.0f;
            outY = 1.0f;
            return;
        }
        float nx = dx / len;
        float ny = dy / len;
        clampAimNotches(nx, ny);
        outX = recClamp(nx, -1.0f, 1.0f);
        outY = recClamp(ny, -1.0f, 1.0f);
    }
#pragma dont_inline reset

    static inline u8 pktOrbitTime(int ftKind)
    {
        return ftKind == 0x0A ? 20 : 30;
    }

    // Active PK Thunder orbit steering (Ness/Lucas 103_recoveryHub.as).
    static void steerPkThunder(const RecoveryInput& in, RecoveryState& st, float toLedgeX, float toLedgeY,
                               float towardGoal, float stageCenterDir, RecoveryOutput& out)
    {
        u8 pktTime = pktOrbitTime(in.ftKind);
        bool spawningOrHolding = in.status == kStSpecialHi || in.status == 0x11A;
        bool wasPkt = st.prevStatus == kStSpecialHi || st.prevStatus == 0x11A;
        if (!wasPkt && spawningOrHolding)
        {
            st.timer = pktTime;
        }
        float outwardDir = -towardGoal;
        // Post-launch PKT2 flight / end lag (0x11B / 0x11C with no PKT article):
        // always drift cleanly inward onto the stage (never outward).
        if (!spawningOrHolding && !in.hasPkt)
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;
            out.stickY = 0.0f;
            return;
        }
        // While starting PKT near a wall, pull away slightly.
        if (in.animFrame < 5.0f && in.wallAhead && in.wallDist < 22.0f)
        {
            out.stickX = outwardDir;
            out.stickY = -1.0f;
            return;
        }
        if (!in.hasPkt)
        {
            // Startup before PKT spawns: bias outward so PKT arcs away from stage on spawn.
            out.stickX = outwardDir;
            out.stickY = 0.0f;
            return;
        }

        // Commit PKT onto the outward (away-from-stage) side while it is still above
        // Ness/Lucas so the orbit wraps clockwise on the right side and counterclockwise
        // on the left side instead of looping from stage-side.
        float bodyCenterY = in.y + 7.0f;
        float relX = in.pktX - in.x;
        float relY = in.pktY - bodyCenterY;
        if (relY > 4.0f && relX * outwardDir < 14.0f)
        {
            out.stickX = outwardDir;
            out.stickY = -0.35f;
            return;
        }

        // Aim cleanly at the ledge lip when below stage (never into the solid underside hull),
        // and slightly inward + upward when above the ledge so PKT2 never fires downward.
        float launchX = toLedgeY > 0.0f ? toLedgeX : (toLedgeX + towardGoal * 6.0f);
        float minLaunchX = toLedgeY > 0.0f ? 8.0f : 12.0f;
        if (launchX * towardGoal < minLaunchX)
        {
            launchX = towardGoal * minLaunchX;
        }
        float launchY = toLedgeY + (in.ledgeOccupied && !in.ceilingAbove ? 22.0f : 10.0f);
        float minLaunchY = recAbs(launchX) * 0.75f;
        if (minLaunchY < 24.0f)
        {
            minLaunchY = 24.0f;
        }
        if (launchY < minLaunchY)
        {
            launchY = minLaunchY;
        }
        float len = recSqrt(launchX * launchX + launchY * launchY);
        if (len < 0.001f)
        {
            len = 1.0f;
        }
        float tFrac = static_cast<float>(st.timer) / static_cast<float>(pktTime);
        float orbitDist = 6.0f + tFrac * 32.0f;
        // Aim behind the fighter relative to the desired upward-inward launch direction.
        float behindX = -(launchX / len) * orbitDist;
        float behindY = -(launchY / len) * orbitDist * 1.25f;
        float absBx = recAbs(behindX);
        float padX = absBx < 3.0f ? 5.0f : 3.0f;
        behindX += behindX >= 0.0f ? padX : -padX;
        behindY -= 2.0f;

        float targetX = in.x + behindX;
        float targetY = bodyCenterY + behindY;

        // Predict PKT 5 frames forward along its current velocity.
        float predX = in.pktX + in.pktVx * 5.0f;
        float predY = in.pktY + in.pktVy * 5.0f;
        float errX = targetX - predX;
        float errY = targetY - predY;
        float errLen = recSqrt(errX * errX + errY * errY);
        if (st.timer > 0 && (errLen <= 28.0f || relY <= -12.0f))
        {
            st.timer--;
        }
        if (errLen > 0.001f)
        {
            out.stickX = recClamp(errX / errLen, -1.0f, 1.0f);
            out.stickY = recClamp(errY / errLen, -1.0f, 1.0f);
        }
    }

#pragma dont_inline on
    enum {
        kMatchUpB = 0,
        kMatchSideB = 1,
        kMatchDownB = 2,
        kMatchSpecFall = 3
    };

    static bool matchFtStatus(u8 kind, int ftKind, int s)
    {
        static const u16 kBase[4] = {kStSpecialHi, kStSpecialS, kStSpecialLw, kStFallSpecial};
        if (s == kBase[kind])
        {
            return true;
        }
        struct FtStRange {
            u8 kind;
            u8 ft;
            u16 lo;
            u16 hi;
        };
        static const FtStRange kRanges[] = {
            // Up-B continuations
            {kMatchUpB, 0x06, 0x117, 0x117}, {kMatchUpB, 0x13, 0x117, 0x117}, {kMatchUpB, 0x2C, 0x117, 0x117},
            {kMatchUpB, 0x0A, 0x11A, 0x11C}, {kMatchUpB, 0x1A, 0x11A, 0x11C}, {kMatchUpB, 0x1B, 0x125, 0x125},
            {kMatchUpB, 0x1B, 0x127, 0x127}, {kMatchUpB, 0x21, 0x11B, 0x11C}, {kMatchUpB, 0x26, 0x11B, 0x11C},
            {kMatchUpB, 0x22, 0x11E, 0x120}, {kMatchUpB, 0x0C, 0x11A, 0x11C}, {kMatchUpB, 0x16, 0x11D, 0x11D},
            // Side-B continuations
            {kMatchSideB, 0x06, 0x129, 0x129}, {kMatchSideB, 0x1B, 0x11B, 0x11C}, {kMatchSideB, 0x1B, 0x124, 0x124},
            {kMatchSideB, 0x1E, 0x119, 0x119}, {kMatchSideB, 0x22, 0x11A, 0x11C}, {kMatchSideB, 0x16, 0x119, 0x11A},
            // Down-B continuations
            {kMatchDownB, 0x03, 0x11D, 0x11E}, {kMatchDownB, 0x03, 0x120, 0x121}, {kMatchDownB, 0x16, 0x11B, 0x11C},
            // Special fall continuations
            {kMatchSpecFall, 0x06, 0x118, 0x118}, {kMatchSpecFall, 0x13, 0x118, 0x118},
            {kMatchSpecFall, 0x2C, 0x118, 0x118}, {kMatchSpecFall, 0x22, 0x11D, 0x11D},
            {kMatchSpecFall, 0x2E, 0x118, 0x118}
        };
        for (int i = 0; i < static_cast<int>(sizeof(kRanges) / sizeof(kRanges[0])); i++)
        {
            if (kRanges[i].kind == kind && kRanges[i].ft == ftKind && s >= kRanges[i].lo && s <= kRanges[i].hi)
            {
                return true;
            }
        }
        return false;
    }

    static bool isTetherStatus(int s)
    {
        return (s >= kStAirLasso && s <= kStAirLassoFailure) || s == kStCliffJump2;
    }

    static bool inRectI(float absX, int xMin, int xMax, float yVal, int yMin, int yMax)
    {
        return absX >= static_cast<float>(xMin) && absX <= static_cast<float>(xMax)
               && yVal >= static_cast<float>(yMin) && yVal <= static_cast<float>(yMax);
    }

    static void addDieI(WeightedDie& die, int opt, int weight)
    {
        die.add(opt, static_cast<float>(weight));
    }
#pragma dont_inline reset

    static void stepRecoveryBody(const FighterRecoverySpec& spec, const RecoveryInput& in, RecoveryState& st, u32& rng,
                                 RecoveryOutput& out)
    {
        out.active = true;
        const int ft = in.ftKind;
        const u8 mix = in.recoveryMixup;
        const u32 flg = spec.flags;
        const u32 lastB = in.lastButtons;

        // First frame of a new recovery: apply Recovery Mixup profile (Mixed,
        // Low sweetspot, High, or Direct).
        if (!st.initialized)
        {
            st.initialized = 1;
            st.option = kOptNone;
            st.usedFloat = 0;
            st.holdFrames = 0;
            st.mashPhase = 0;
            if (mix == kMixupLow || mix == kMixupDirect)
            {
                st.highMixup = 0;
            }
            else if (mix == kMixupHigh)
            {
                st.highMixup = static_cast<u8>(28.0f + 27.0f * recRand01(rng));
            }
            else
            {
                float r = recRand01(rng);
                bool goHigh = (in.ledgeOccupied && r < 0.80f) || (!in.ledgeOccupied && r < 0.25f);
                st.highMixup = goHigh ? static_cast<u8>(15.0f + 40.0f * recRand01(rng)) : 0;
            }
        }

        // Vector from fighter to nearest ledge.
        float toLedgeX = in.ledgeX - in.x;
        float toLedgeY = in.ledgeY - in.y;
        float absNCX = recAbs(toLedgeX);
        float belowLedgeY = toLedgeY; // > 0 below ledge, < 0 above ledge
        float highBelowY = belowLedgeY + (!in.ceilingAbove ? static_cast<float>(st.highMixup) : 0.0f);
        float stageCenterX = 0.5f * (in.stageLeftX + in.stageRightX);
        float stageCenterDir = in.x > stageCenterX ? -1.0f : 1.0f;
        float towardLedge = toLedgeX == 0.0f ? stageCenterDir : recSign(toLedgeX);
        bool insideStageX = in.x >= in.stageLeftX && in.x <= in.stageRightX;
        bool overStage = in.x >= in.stageLeftX + 2.0f && in.x <= in.stageRightX - 2.0f && in.y >= in.stageY - 6.0f;
        bool overStageSurface = insideStageX && !in.ceilingAbove && in.y >= in.stageY - 8.0f;
        float towardGoal = overStageSurface ? stageCenterDir : towardLedge;

        // 2a. Special Fall (0x10, Spacie 0x118, Ike 0x11D, Snake 0x118): drift directly toward stage center.
        if (matchFtStatus(kMatchSpecFall, ft, in.status))
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : stageCenterDir;
            return;
        }

        // 2b. Glide (0x84 GlideStart / 0x85 Glide): {GLIDE} pitch + cancel.
        if (in.status == kStGlideStart || in.status == kStGlide)
        {
            if (in.status == kStGlideStart)
            {
                out.stickX = towardGoal;
            }
            else
            {
                if (belowLedgeY > 2.0f)
                {
                    out.stickX = -towardGoal;
                }
                else if (belowLedgeY < -15.0f && in.vy > -1.25f)
                {
                    out.stickX = towardGoal;
                }
                if ((absNCX < 10.0f && belowLedgeY < 0.0f) || overStage || overStageSurface)
                {
                    out.buttons = freshButton(kRecBtnJump, lastB);
                }
            }
            return;
        }

        // 2c. Tether hang / reel-in (0x7F..0x83, ZSS 0x7A/0x81).
        if (isTetherStatus(in.status))
        {
            st.mashPhase++;
            out.stickX = towardGoal;
            if (in.status == kStAirLassoHang || in.status == kStCliffJump2)
            {
                out.buttons = (st.mashPhase & 1u) != 0 ? freshButton(kRecBtnJump, lastB) : 0u;
                out.stickY = 1.0f;
            }
            return;
        }

        // 2d. Peach Float (0x11E) & Mewtwo Hover (0x120).
        if (spec.floatStatus != 0 && in.status == static_cast<int>(spec.floatStatus))
        {
            st.usedFloat = 1;
            if (ft == 0x26)
            {
                bool safeOverFloor = in.floorBelow && in.y >= in.stageY - 2.0f;
                float dist = recSqrt(toLedgeX * toLedgeX + toLedgeY * toLedgeY);
                if (dist > 8.0f && !overStage && !overStageSurface && !safeOverFloor)
                {
                    out.buttons = kRecBtnJump;
                    float aimY = belowLedgeY >= -8.0f ? (toLedgeY + 14.0f) : 0.0f;
                    if (in.ledgeOccupied && !in.ceilingAbove)
                    {
                        aimY += 25.0f;
                    }
                    aim360(toLedgeX, aimY, out.stickX, out.stickY);
                }
            }
            else if (absNCX > 16.0f && !overStage && !overStageSurface)
            {
                out.buttons = kRecBtnJump;
                out.stickX = towardGoal;
            }
            return;
        }

        // 2e. Wall attach / wall climb (Knuckles, Lucario, etc.).
        if (in.status == kStAttachWall || in.status == kStAttachWallWait)
        {
            out.stickX = -towardLedge;
            out.buttons = freshButton(kRecBtnJump, lastB);
            return;
        }

        // 2f. Active Up-B handlers.
        if (matchFtStatus(kMatchUpB, ft, in.status))
        {
            if ((flg & kRfPkThunder) != 0)
            {
                steerPkThunder(in, st, toLedgeX, toLedgeY, towardGoal, stageCenterDir, out);
                return;
            }
            if ((flg & kRf360Aim) != 0)
            {
                float targetDx = toLedgeX;
                float targetDy = belowLedgeY >= 0.0f
                                     ? (toLedgeY + (mix == kMixupLow ? 4.0f : 8.0f))
                                     : (mix == kMixupLow ? 2.0f : 4.0f);
                if (insideStageX && !in.ceilingAbove && in.y >= in.stageY - 4.0f)
                {
                    targetDx = stageCenterX - in.x;
                    if (recAbs(targetDx) < 8.0f)
                    {
                        targetDx = stageCenterDir * 8.0f;
                    }
                    targetDy = (in.stageY + 8.0f) - in.y;
                }
                else if (in.ceilingAbove)
                {
                    targetDx = -stageCenterDir * 35.0f;
                    targetDy = 12.0f;
                }
                else if (in.wallAhead && in.wallDist < 20.0f && belowLedgeY > 6.0f)
                {
                    targetDx = towardGoal * 4.0f;
                    targetDy = belowLedgeY + 18.0f;
                }
                else if (mix == kMixupHigh
                         || (mix != kMixupDirect && (in.ledgeOccupied || (mix != kMixupLow && st.option == kOptUpBHigh))))
                {
                    targetDy += mix == kMixupHigh ? 28.0f : 25.0f;
                    targetDx += towardGoal * (mix == kMixupHigh ? 18.0f : 15.0f);
                }
                if (!in.ceilingAbove && in.y >= in.stageY - 4.0f && targetDx * stageCenterDir < 0.0f)
                {
                    targetDx = stageCenterDir * recAbs(targetDx);
                }
                if ((flg & kRfQuickAttack) != 0 && in.subaction == 0x1DB)
                {
                    if (recSqrt(targetDx * targetDx + targetDy * targetDy) > 45.0f)
                    {
                        targetDy += 25.0f;
                    }
                }
                if ((flg & kRfAuraCancel) != 0 && st.option == kOptUpBCancel && in.status == 0x11B
                    && in.animFrame >= 15.0f)
                {
                    out.buttons = freshButton(kRecBtnAttack, lastB);
                    out.stickX = towardGoal;
                    return;
                }
                aim360(targetDx, targetDy, out.stickX, out.stickY);
                return;
            }
            if ((flg & kRfChargeUpB) != 0 && (in.status == kStSpecialHi || in.status == 0x125))
            {
                float dist = recSqrt(toLedgeX * toLedgeX + toLedgeY * toLedgeY);
                float wantCharge = recClamp((dist - 30.0f) * 0.6f, 8.0f, static_cast<float>(spec.shortenFrame));
                if (in.animFrame <= wantCharge)
                {
                    out.buttons = kRecBtnSpecial;
                }
                if (in.animFrame > wantCharge * 0.45f)
                {
                    out.stickX = towardGoal;
                }
                return;
            }
            if ((flg & kRfBoostUpB) != 0)
            {
                st.mashPhase++;
                out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;
                if (ft == 0x23)
                {
                    if (belowLedgeY > -10.0f && (st.mashPhase & 1u) != 0)
                    {
                        out.buttons = kRecBtnSpecial;
                    }
                }
                else if (ft == 0x2E && belowLedgeY < -15.0f && absNCX < 20.0f)
                {
                    out.buttons = freshButton(kRecBtnJump, lastB);
                }
                return;
            }
            if (ft == 0x16 && st.option == kOptGlide)
            {
                if (in.animFrame < 5.0f)
                {
                    out.stickX = towardGoal;
                }
                return;
            }
            if (in.animFrame <= 12.0f && in.vx * towardGoal < 0.0f)
            {
                out.stickX = towardGoal;
            }
            else if (in.ceilingAbove)
            {
                out.stickX = -stageCenterDir;
            }
            else if (absNCX > 5.0f || overStageSurface)
            {
                out.stickX = towardGoal;
            }
            return;
        }

        // 2g. Active Side-B handlers.
        if (matchFtStatus(kMatchSideB, ft, in.status))
        {
            out.stickX = towardGoal;
            if ((flg & kRfSideBShorten) != 0)
            {
                if ((st.option == kOptSideBShort || (in.floorBelow && absNCX < 12.0f))
                    && in.animFrame >= static_cast<float>(spec.shortenFrame))
                {
                    out.buttons = freshButton(kRecBtnSpecial, lastB);
                }
            }
            else if (ft == 0x22)
            {
                out.stickX = towardLedge;
                if (in.status == kStSpecialS && in.animFrame < 5.0f)
                {
                    out.buttons = kRecBtnSpecial;
                }
                else if (in.status == 0x11B && in.floorBelow && in.y >= in.stageY - 4.0f)
                {
                    out.buttons = freshButton(kRecBtnAttack, lastB);
                }
                else if (in.status == 0x11B && st.option == kOptSideBWallJump && in.wallAhead && in.wallDist < 18.0f)
                {
                    out.stickX = -towardLedge;
                    out.buttons = freshButton(kRecBtnJump, lastB);
                }
                else if (in.status != 0x11B)
                {
                    float travelDist = 45.0f + (in.animFrame * 0.125f - 3.0f) * 9.0f;
                    if (belowLedgeY < -2.0f && travelDist < absNCX - 8.0f && in.animFrame < 32.0f)
                    {
                        out.buttons = kRecBtnSpecial;
                    }
                }
                else if (absNCX <= 15.0f && belowLedgeY > -10.0f && belowLedgeY < 10.0f)
                {
                    out.buttons = freshButton(kRecBtnSpecial, lastB);
                }
            }
            else if (ft == 0x08 && (flg & kRfChargeSideB) != 0)
            {
                out.stickX = towardLedge;
                if (in.animFrame < 22.0f && absNCX > 40.0f)
                {
                    out.buttons = kRecBtnSpecial;
                }
            }
            else if (ft == 0x1B && in.status == 0x124)
            {
                out.buttons = freshButton(kRecBtnJump, lastB);
                out.stickX = towardLedge;
            }
            else if ((flg & kRfSideBCancel) != 0 && in.status == 0x119)
            {
                out.buttons = freshButton(kRecBtnSpecial, lastB);
                out.stickX = towardLedge;
            }
            else if (ft == 0x16)
            {
                out.stickX = towardLedge;
                out.stickY = belowLedgeY > 0.0f ? 1.0f : (belowLedgeY < -10.0f ? -1.0f : 0.0f);
            }
            else if (ft == 0x25)
            {
                out.stickY = 1.0f;
            }
            return;
        }

        // 2h. Active Down-B handlers.
        if (matchFtStatus(kMatchDownB, ft, in.status))
        {
            st.mashPhase++;
            out.stickX = towardGoal;
            if ((flg & kRfMashDownB) != 0)
            {
                if ((st.mashPhase & 1u) != 0)
                {
                    out.buttons = freshButton(kRecBtnSpecial, lastB);
                }
            }
            else if ((flg & kRfBombStall) != 0 && in.status == 0x11E && in.animFrame > 51.0f
                     && absNCX > 25.0f && belowLedgeY > -15.0f)
            {
                out.stickY = -1.0f;
                out.buttons = freshButton(kRecBtnSpecial, lastB);
            }
            else if (ft == 0x16)
            {
                float capeDx = overStageSurface ? (stageCenterX - in.x) : toLedgeX;
                float capeDy = overStageSurface ? ((in.stageY + 8.0f) - in.y) : toLedgeY;
                aim360(capeDx, capeDy, out.stickX, out.stickY);
            }
            else if ((flg & kRfAuraCancel) != 0 && st.option == kOptDownBCancel)
            {
                out.buttons = freshButton(kRecBtnAttack, lastB);
                out.stickY = -1.0f;
            }
            return;
        }

        // 2i. Meta Knight Neutral-B (Mach Tornado mash).
        if (ft == 0x16 && (in.status == kStSpecialN || in.status == 0x117 || in.status == 0x118))
        {
            st.mashPhase++;
            if ((st.mashPhase % 3u) != 2u)
            {
                out.buttons = freshButton(kRecBtnSpecial, lastB);
            }
            out.stickX = towardGoal;
            return;
        }

        // 2j. Active aerial jump rising.
        if ((in.status == kStJump || in.status == kStJumpAerial || in.status == kStFly)
            && (in.vy > 0.0f || in.animFrame < 4.0f))
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : ((absNCX > 5.0f || overStageSurface) ? towardGoal : 0.0f);
            if (st.option == kOptFloat && (flg & kRfFloat) != 0)
            {
                out.buttons = kRecBtnJump;
                out.stickY = -1.0f;
            }
            return;
        }

        // 3. Free offstage airborne decision: drift + WeightedDie roll.
        out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;
        if (overStageSurface || !in.canAct)
        {
            return;
        }

        if (in.wallAhead && in.wallDist < 16.0f && belowLedgeY > 0.0f && in.laInt > 0)
        {
            if ((flg & kRfWallClimb) != 0)
            {
                out.stickX = towardLedge;
                out.stickY = 1.0f;
                return;
            }
            if ((flg & kRfWallJump) != 0 && in.vy < 0.5f)
            {
                out.stickX = -towardLedge;
                out.buttons = freshButton(kRecBtnJump, lastB);
                return;
            }
        }

        float upBX = static_cast<float>(spec.upBX);
        float upBY = static_cast<float>(spec.upBY);
        bool emergency = (in.y - in.blastBottom) <= 25.0f || belowLedgeY >= upBY + 40.0f;
        if (in.ceilingAbove)
        {
            out.stickX = -stageCenterDir;
            if (emergency && in.jumpsLeft)
            {
                out.buttons = freshButton(kRecBtnJump, lastB);
            }
            return;
        }

        WeightedDie die;
        die.add(kOptNone, mix == kMixupDirect ? 0.5f : 3.0f);
        float dist2D = recSqrt(toLedgeX * toLedgeX + toLedgeY * toLedgeY);

        if (in.jumpsLeft)
        {
            bool preserveDj = in.vy > 0.2f
                              || (belowLedgeY < -10.0f && absNCX < 65.0f && mix != kMixupHigh)
                              || (mix == kMixupLow && belowLedgeY <= 2.0f && absNCX <= upBX * 0.85f);
            if (!preserveDj && inRectI(absNCX, 0, 300, highBelowY, -15, 150))
            {
                int jumpW = spec.family == kFamMultiJump ? (in.numJumps > 0 ? in.numJumps * 8 : 10)
                                                         : (ft == 0x04 ? 25 : 8);
                if (mix == kMixupHigh && belowLedgeY < 10.0f)
                {
                    jumpW += 16;
                }
                if (belowLedgeY > 5.0f && in.vy <= 0.0f)
                {
                    jumpW += 12;
                }
                addDieI(die, kOptJump, jumpW);
            }
        }

        bool mkSaveSpecials = ft == 0x16 && in.numJumps >= 2 && belowLedgeY < 20.0f;
        if ((flg & kRfTether) != 0 && absNCX <= upBX && belowLedgeY >= -15.0f && belowLedgeY <= upBY + 10.0f)
        {
            if (ft == 0x1F)
            {
                if ((!in.jumpsLeft || belowLedgeY > 20.0f) && absNCX <= 30.0f && belowLedgeY >= 2.0f
                    && belowLedgeY <= upBY)
                {
                    addDieI(die, kOptUpB, 24);
                }
            }
            else if (ft == 0x19)
            {
                if (!in.jumpsLeft || belowLedgeY > 20.0f)
                {
                    addDieI(die, kOptUpB, 24);
                }
            }
            else if (in.lr * towardLedge > 0.0f && absNCX >= 8.0f)
            {
                addDieI(die, kOptTether, 18);
            }
        }

        if ((flg & kRfFloat) != 0 && st.usedFloat == 0)
        {
            if (ft == 0x0C && !in.laFlag && inRectI(absNCX, 15, spec.altX, highBelowY, -25, 30))
            {
                addDieI(die, kOptFloat, 30);
            }
            else if (ft == 0x26 && in.jumpsLeft && !in.laFlag
                     && inRectI(absNCX, 10, spec.altX, highBelowY, -20, 65))
            {
                addDieI(die, kOptFloat, 28);
            }
            else if (ft == 0x14 && in.laInt < 2 && absNCX > 25.0f && belowLedgeY < 20.0f)
            {
                addDieI(die, kOptNeutralB, 14);
            }
        }

        if ((flg & kRfGlide) != 0 && !mkSaveSpecials
            && inRectI(absNCX, 15, spec.altX, belowLedgeY, -35, spec.altY + 25))
        {
            addDieI(die, kOptGlide, 14);
        }

        if (spec.sideBX > 0 && !mkSaveSpecials)
        {
            bool allowSideB = inRectI(absNCX, 12, spec.sideBX, highBelowY, spec.sideBY - 25, spec.sideBY + 35);
            if (ft == 0x06 || ft == 0x13 || ft == 0x2C)
            {
                allowSideB = inRectI(absNCX, 12, 62, belowLedgeY, -35, ft == 0x2C ? 8 : 4)
                             && !(in.jumpsLeft && absNCX > 45.0f)
                             && !(in.wallAhead && in.wallDist < 24.0f && belowLedgeY > 4.0f)
                             && !(mix == kMixupLow && belowLedgeY < -8.0f);
            }
            else if (ft == 0x09)
            {
                allowSideB = inRectI(absNCX, 12, 48, belowLedgeY, -40, -4) && !in.wallAhead;
            }
            else if (ft == 0x14)
            {
                allowSideB = inRectI(absNCX, 12, spec.sideBX, belowLedgeY, -40, 2)
                             && !(in.wallAhead && in.wallDist < 24.0f && belowLedgeY > 4.0f);
            }
            else if (ft == 0x00)
            {
                allowSideB = allowSideB && belowLedgeY <= 12.0f && absNCX > 25.0f && in.vy < 0.0f
                             && mix != kMixupDirect;
            }
            else if (ft == 0x11)
            {
                allowSideB = allowSideB && belowLedgeY <= 25.0f && absNCX > 24.0f && in.vy < -0.2f
                             && mix != kMixupDirect;
            }
            else if (ft == 0x25)
            {
                allowSideB = inRectI(absNCX, 0, spec.sideBX, belowLedgeY, -150, spec.sideBY);
            }
            else if ((ft == 0x26 && (recAbs(in.vx) <= 1.2f || belowLedgeY >= 0.0f))
                     || (ft == 0x22 && belowLedgeY > 2.0f) || (ft == 0x1B && in.laFlag))
            {
                allowSideB = false;
            }
            if (allowSideB)
            {
                if ((flg & kRfSideBShorten) != 0)
                {
                    addDieI(die, kOptSideB, mix == kMixupHigh ? 14 : 4);
                    if (absNCX <= 16.0f && mix != kMixupHigh)
                    {
                        addDieI(die, kOptSideBShort, 12);
                    }
                }
                else if (ft == 0x22 && in.wallAhead && in.laInt > 0)
                {
                    addDieI(die, kOptSideBWallJump, 25);
                    addDieI(die, kOptSideB, 15);
                }
                else
                {
                    addDieI(die, kOptSideB, 12);
                }
            }
        }

        if ((flg & kRfBombStall) != 0 && mix != kMixupDirect && in.vy < -1.0f && absNCX > 25.0f
            && belowLedgeY > -20.0f && belowLedgeY < spec.upBY)
        {
            addDieI(die, kOptDownB, 14);
        }
        else if ((flg & kRfFlipDownB) != 0 && inRectI(absNCX, 15, 75, belowLedgeY, -20, 35))
        {
            addDieI(die, kOptDownB, 16);
        }
        else if ((flg & kRfMashDownB) != 0 && !in.laFlag && absNCX > 15.0f && belowLedgeY > -10.0f
                 && belowLedgeY < spec.upBY + 15.0f)
        {
            addDieI(die, kOptDownB, 14);
        }
        else if ((flg & kRfAuraCancel) != 0 && !in.laFlag && absNCX <= 90.0f && belowLedgeY < 20.0f)
        {
            addDieI(die, in.laFloat >= 65.0f ? kOptDownBCancel : kOptDownB, 14);
        }
        else if ((flg & kRfWaft) != 0 && in.laInt >= 2 && belowLedgeY > 15.0f && absNCX < 45.0f)
        {
            addDieI(die, kOptDownB, 16);
        }

        if (ft == 0x16 && !mkSaveSpecials && inRectI(absNCX, 10, 80, belowLedgeY, -10, 45))
        {
            addDieI(die, kOptNeutralB, 10);
        }
        if ((flg & kRfDairStall) != 0 && mix != kMixupDirect && in.vy < -0.5f && absNCX > 45.0f
            && belowLedgeY < -10.0f)
        {
            addDieI(die, kOptDairStall, 8);
        }

        bool inUpB = false;
        if (spec.upBY > 0 && !mkSaveSpecials)
        {
            inUpB = (flg & kRf360Aim) != 0
                        ? (dist2D <= upBX + 8.0f && (belowLedgeY >= -15.0f || absNCX > 25.0f))
                        : inRectI(absNCX, 0, spec.upBX + 8, belowLedgeY, -15, spec.upBY + 15);
        }
        if ((ft == 0x2E && in.laFlag && in.jumpsLeft) || (ft == 0x1F && (belowLedgeY < 2.0f || absNCX > 32.0f))
            || ((flg & kRfPkThunder) != 0 && absNCX < 22.0f && belowLedgeY < -4.0f))
        {
            inUpB = false;
        }
        bool allowUpBWithJump = spec.family != kFamMultiJump && spec.family != kFamPkThunder
                                && belowLedgeY > upBY * 0.45f && belowLedgeY <= upBY;
        if (inUpB && (!in.jumpsLeft || allowUpBWithJump))
        {
            if ((flg & kRfAuraCancel) != 0 && in.laFloat >= 65.0f)
            {
                addDieI(die, kOptUpBCancel, 25);
            }
            addDieI(die, kOptUpB, mix == kMixupHigh && spec.family == kFamSpacie ? 12 : 40);
            if (spec.family == kFamSpacie && mix != kMixupLow && mix != kMixupDirect)
            {
                addDieI(die, kOptUpBHigh, mix == kMixupHigh ? 40 : 15);
            }
        }

        if (emergency)
        {
            die.clear();
            int emOpt = kOptUpB;
            if (in.jumpsLeft)
            {
                emOpt = ((flg & kRfFloat) != 0 && ft == 0x26 && st.usedFloat == 0 && !in.laFlag)
                            ? kOptFloat
                            : kOptJump;
            }
            else if (ft == 0x16) emOpt = kOptGlide;
            else if (ft == 0x04 || ft == 0x25) emOpt = kOptSideB;
            addDieI(die, emOpt, 100);
        }

        int pick = die.roll(recRand01(rng), kOptNone);
        if (pick != kOptNone)
        {
            st.option = static_cast<u8>(pick);
        }

        if (pick == kOptNone)
        {
            if (in.status == kStDamageFall)
            {
                out.stickX = (st.mashPhase & 1u) != 0 ? -towardGoal * 0.95f : towardGoal * 0.95f;
                st.mashPhase++;
            }
            return;
        }
        // Table-driven option execution: btn, sxKind (0=0, 1=towardLedge, 2=ceil?away:toward), sy (-1/0/+1).
        struct OptExec { u8 btn; u8 sxKind; s8 sy; };
        static const OptExec kExec[15] = {
            {0, 0, 0},                                   // kOptNone
            {kRecBtnJump, 2, 0},                         // kOptJump
            {kRecBtnJump, 0, -1},                        // kOptFloat
            {kRecBtnSpecial, 0, 1},                      // kOptUpB
            {kRecBtnSpecial, 0, 1},                      // kOptUpBHigh
            {kRecBtnSpecial, 0, 1},                      // kOptUpBCancel
            {kRecBtnSpecial, 0, 1},                      // kOptGlide (Pit/Charizard overridden below)
            {kRecBtnSpecial, 1, 0},                      // kOptSideB
            {kRecBtnSpecial, 1, 0},                      // kOptSideBShort
            {kRecBtnSpecial, 1, 0},                      // kOptSideBWallJump
            {kRecBtnSpecial, 0, -1},                     // kOptDownB
            {kRecBtnSpecial, 0, -1},                     // kOptDownBCancel
            {kRecBtnSpecial, 0, 0},                      // kOptNeutralB
            {kRecBtnGuard | kRecBtnAttack, 1, 0},        // kOptTether
            {kRecBtnAttack, 0, -1}                       // kOptDairStall
        };
        if (pick > 0 && pick < 15)
        {
            const OptExec& e = kExec[pick];
            out.buttons = freshButton(e.btn, lastB);
            if (e.sxKind == 1) out.stickX = towardLedge;
            else if (e.sxKind == 2) out.stickX = in.ceilingAbove ? -stageCenterDir : towardLedge;
            else out.stickX = 0.0f;
            out.stickY = static_cast<float>(e.sy);
            if (pick == kOptGlide)
            {
                if (ft == 0x17) { out.stickX = towardLedge; out.stickY = 0.0f; }
                else if (ft == 0x1D) out.stickY = -1.0f;
            }
            else if ((pick >= kOptUpB && pick <= kOptUpBCancel) && (ft == 0x0A || ft == 0x1A))
            {
                st.timer = pktOrbitTime(ft);
            }
        }
    }

    void stepRecovery(const RecoveryInput& in, RecoveryState& st, u32& rng, RecoveryOutput& out)
    {
        out.buttons = 0;
        out.stickX = 0.0f;
        out.stickY = 0.0f;
        out.active = false;

        const FighterRecoverySpec& spec = recoverySpec(in.ftKind);

        if (in.status >= kStCliffCatchMove && in.status <= kStCliffLast && in.status != kStCliffJump2)
        {
            out.active = true;
            if (in.status == kStCliffWait)
            {
                if (st.ledgeWaitFrames == 0)
                {
                    st.cliffChoice = static_cast<u8>(geomNextRng(rng) % 4u);
                }
                st.ledgeWaitFrames++;
                if (st.ledgeWaitFrames >= 2)
                {
                    float towardStage = in.ledgeDir != 0.0f ? in.ledgeDir : -recSign(in.x);
                    if (st.cliffChoice == 0) out.stickX = towardStage;
                    else if (st.cliffChoice == 1) out.buttons = freshButton(kRecBtnGuard, in.lastButtons);
                    else if (st.cliffChoice == 2)
                    {
                        out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                        out.stickX = towardStage;
                    }
                    else out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
                }
            }
            else
            {
                st.ledgeWaitFrames = 0;
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }
        st.ledgeWaitFrames = 0;

        bool onStageFloor = !in.air && in.x >= in.stageLeftX - 2.0f && in.x <= in.stageRightX + 2.0f;
        bool overStage = in.x >= in.stageLeftX + 2.0f && in.x <= in.stageRightX - 2.0f && in.y >= in.stageY - 6.0f;
        bool safeLandingStatus =
            (in.status <= kStLandingFallSpecial && in.status != kStFallSpecial) || (in.ftKind == 0x26 && in.status == 0x11E);
        if (onStageFloor || (overStage && in.floorBelow && !in.ceilingAbove && safeLandingStatus))
        {
            recoveryStateReset(st);
            return;
        }

        stepRecoveryBody(spec, in, st, rng, out);
        st.prevStatus = static_cast<s16>(in.status);
    }
}
}
