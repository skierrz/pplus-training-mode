#include "extra/pplus_recovery_core.h"

#include <string.h>

#include "extra/net_alloc.h"
#include "extra/stage_geom_math.h"
#ifdef __MWERKS__
#include "shared/game/stick.h"
#endif

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
#ifdef __MWERKS__
    static inline float recSqrt(float s)
    {
        return game::stickRoot(s);
    }
#else
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
#endif

    static inline float recAbs(float v)
    {
        return v < 0.0f ? -v : v;
    }

    static inline float recSign(float v)
    {
        return v < 0.0f ? -1.0f : 1.0f;
    }

#pragma dont_inline on
    static void pressBtn(RecoveryOutput& out, u32 btn, u32 lastButtons)
    {
        out.buttons = (lastButtons & btn) != 0 ? 0u : btn;
    }

    static inline float recClamp(float v, float lo, float hi)
    {
        return clampf(v, lo, hi);
    }
#pragma dont_inline reset

    static inline float recRand01(u32& rng)
    {
        return geomRand01(rng);
    }

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
        float total = totalWeight();
        if (total <= 0.0f) return defaultOpt;
        float u = recClamp(u01, 0.0f, 0.999999f) * total;
        for (int i = 0; i < m_count; i++)
        {
            u -= m_entries[i].weight;
            if (u < 0.0f) return m_entries[i].option;
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
        float ax = recAbs(x);
        if (ax > 0.1f && ax < 0.25f) x = x < 0.0f ? -0.3f : 0.3f;
        if (y > 0.0f && y < 0.3f) y = 0.3f;
    }

    void recoveryStateReset(RecoveryState& st)
    {
        memset(&st, 0, sizeof(st));
        st.prevStatus = -1;
    }

#pragma dont_inline on
    static float dist2D(float dx, float dy)
    {
        return recSqrt(dx * dx + dy * dy);
    }

    // Aim normalized vector from fighter toward (dx, dy) with optional PPlusAI notch clamp.
    static void aim360(float dx, float dy, float& outX, float& outY, bool clampNotch = true)
    {
        float len = dist2D(dx, dy);
        if (len < 0.001f)
        {
            if (clampNotch)
            {
                outX = 0.0f;
                outY = 1.0f;
            }
            return;
        }
        float nx = dx / len;
        float ny = dy / len;
        if (clampNotch)
        {
            clampAimNotches(nx, ny);
        }
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
        if (!spawningOrHolding && !in.hasPkt)
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;
            return;
        }
        if (in.animFrame < 5.0f && in.wallAhead && in.wallDist < 22.0f)
        {
            out.stickX = outwardDir;
            out.stickY = -1.0f;
            return;
        }
        if (!in.hasPkt)
        {
            out.stickX = outwardDir;
            return;
        }

        float bodyCenterY = in.y + 7.0f;
        float relX = in.pktX - in.x;
        float relY = in.pktY - bodyCenterY;
        if (relY > 4.0f && relX * outwardDir < 14.0f)
        {
            out.stickX = outwardDir;
            out.stickY = -0.35f;
            return;
        }

        bool belowL = toLedgeY > 0.0f;
        float launchX = belowL ? toLedgeX : (toLedgeX + towardGoal * 6.0f);
        float minLaunchX = belowL ? 8.0f : 12.0f;
        if (launchX * towardGoal < minLaunchX) launchX = towardGoal * minLaunchX;
        float launchY = toLedgeY + (in.ledgeOccupied && !in.ceilingAbove ? 22.0f : 10.0f);
        float minLaunchY = recAbs(launchX) * 0.75f;
        if (minLaunchY < 24.0f) minLaunchY = 24.0f;
        if (launchY < minLaunchY) launchY = minLaunchY;
        float len = dist2D(launchX, launchY);
        if (len < 0.001f) len = 1.0f;
        float orbitDist = 6.0f + (i2f(st.timer) / i2f(pktTime)) * 32.0f;
        float behindX = -(launchX / len) * orbitDist;
        float behindY = -(launchY / len) * orbitDist * 1.25f - 2.0f;
        float padX = recAbs(behindX) < 3.0f ? 5.0f : 3.0f;
        behindX += behindX >= 0.0f ? padX : -padX;

        float errX = (in.x + behindX) - (in.pktX + in.pktVx * 5.0f);
        float errY = (bodyCenterY + behindY) - (in.pktY + in.pktVy * 5.0f);
        if (st.timer > 0 && (dist2D(errX, errY) <= 28.0f || relY <= -12.0f)) st.timer--;
        aim360(errX, errY, out.stickX, out.stickY, false);
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
        if (s == kBase[kind]) return true;
        int off = s - 0x110;
        if (off < 0 || off > 255) return false;
        struct FtStRange { u8 kind, ft, lo, hi; };
        static const FtStRange kRanges[] = {
            // Up-B continuations
            {kMatchUpB, 0x06, 0x07, 0x07}, {kMatchUpB, 0x13, 0x07, 0x07}, {kMatchUpB, 0x2C, 0x07, 0x07},
            {kMatchUpB, 0x0A, 0x0A, 0x0C}, {kMatchUpB, 0x1A, 0x0A, 0x0C}, {kMatchUpB, 0x1B, 0x15, 0x15},
            {kMatchUpB, 0x1B, 0x17, 0x17}, {kMatchUpB, 0x21, 0x0B, 0x0C}, {kMatchUpB, 0x26, 0x0B, 0x0C},
            {kMatchUpB, 0x22, 0x0E, 0x10}, {kMatchUpB, 0x0C, 0x0A, 0x0C}, {kMatchUpB, 0x16, 0x0D, 0x0D},
            // Side-B continuations
            {kMatchSideB, 0x06, 0x19, 0x19}, {kMatchSideB, 0x1B, 0x0B, 0x0C}, {kMatchSideB, 0x1B, 0x14, 0x14},
            {kMatchSideB, 0x1E, 0x09, 0x09}, {kMatchSideB, 0x22, 0x0A, 0x0C}, {kMatchSideB, 0x16, 0x09, 0x0A},
            // Down-B continuations
            {kMatchDownB, 0x03, 0x0D, 0x0E}, {kMatchDownB, 0x03, 0x10, 0x11}, {kMatchDownB, 0x16, 0x0B, 0x0C},
            // Special fall continuations
            {kMatchSpecFall, 0x06, 0x08, 0x08}, {kMatchSpecFall, 0x13, 0x08, 0x08},
            {kMatchSpecFall, 0x2C, 0x08, 0x08}, {kMatchSpecFall, 0x22, 0x0D, 0x0D},
            {kMatchSpecFall, 0x2E, 0x08, 0x08}
        };
        for (int i = 0; i < static_cast<int>(sizeof(kRanges) / sizeof(kRanges[0])); i++)
        {
            if (kRanges[i].kind == kind && kRanges[i].ft == ftKind && off >= kRanges[i].lo && off <= kRanges[i].hi)
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

    static bool inRect16(int x16, int xMin, int xMax, int y16, int yMin, int yMax)
    {
        return x16 >= xMin * 16 && x16 <= xMax * 16 && y16 >= yMin * 16 && y16 <= yMax * 16;
    }

    static void addDieI(WeightedDie& die, int opt, int weight)
    {
        die.add(opt, i2f(weight));
    }
#pragma dont_inline reset

#define R16(n) ((n) * 16)

    static void stepRecoveryBody(const FighterRecoverySpec& spec, const RecoveryInput& in, RecoveryState& st, u32& rng,
                                 bool overStage, RecoveryOutput& out)
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
            st.option = st.usedFloat = st.holdFrames = st.mashPhase = st.highMixup = 0;
            if (mix == kMixupHigh)
            {
                st.highMixup = static_cast<u8>(f2i(28.0f + 27.0f * recRand01(rng)));
            }
            else if (mix == kMixupMixed)
            {
                float r = recRand01(rng);
                if ((in.ledgeOccupied && r < 0.80f) || (!in.ledgeOccupied && r < 0.25f))
                {
                    st.highMixup = static_cast<u8>(f2i(15.0f + 40.0f * recRand01(rng)));
                }
            }
        }

        // Vector from fighter to nearest ledge.
        float toLedgeX = in.ledgeX - in.x;
        float toLedgeY = in.ledgeY - in.y;
        float absNCX = recAbs(toLedgeX);
        float belowLedgeY = toLedgeY; // > 0 below ledge, < 0 above ledge
        float highBelowY = belowLedgeY + (!in.ceilingAbove ? i2f(st.highMixup) : 0.0f);
        const int ix = f2i(absNCX * 16.0f);
        const int iy = f2i(belowLedgeY * 16.0f);
        const int iyH = f2i(highBelowY * 16.0f);
        const int iAnim = f2i(in.animFrame * 16.0f);
        const int iWall = f2i(in.wallDist * 16.0f);
        float stageCenterX = 0.5f * (in.stageLeftX + in.stageRightX);
        float stageCenterDir = in.x > stageCenterX ? -1.0f : 1.0f;
        float towardLedge = toLedgeX == 0.0f ? stageCenterDir : recSign(toLedgeX);
        bool insideStageX = in.x >= in.stageLeftX && in.x <= in.stageRightX;
        bool overStageSurface = insideStageX && !in.ceilingAbove && in.y >= in.stageY - 8.0f;
        float towardGoal = overStageSurface ? stageCenterDir : towardLedge;
        float driftX = in.ceilingAbove ? -stageCenterDir : towardGoal;

        // 2a. Special Fall (0x10, Spacie 0x118, Ike 0x11D, Snake 0x118): drift directly toward stage center.
        if (matchFtStatus(kMatchSpecFall, ft, in.status))
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : stageCenterDir;
            return;
        }

        // 2b. Glide (0x84 GlideStart / 0x85 Glide): {GLIDE} pitch + cancel.
        if (in.status == kStGlideStart) { out.stickX = towardGoal; return; }
        if (in.status == kStGlide)
        {
            if (iy > R16(2)) out.stickX = -towardGoal;
            else if (iy < R16(-15) && in.vy > -1.25f) out.stickX = towardGoal;
            if ((ix < R16(10) && iy < 0) || overStage || overStageSurface)
            {
                pressBtn(out, kRecBtnJump, lastB);
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
                if ((st.mashPhase & 1u) != 0) pressBtn(out, kRecBtnJump, lastB);
                out.stickY = 1.0f;
            }
            return;
        }

        // 2d. Peach Float (0x11E) & Mewtwo Hover (0x120).
        if (spec.floatStatus != 0 && in.status == static_cast<int>(spec.floatStatus))
        {
            st.usedFloat = 1;
            if (overStage || overStageSurface) return;
            if (ft == 0x26)
            {
                if (dist2D(toLedgeX, toLedgeY) > 8.0f && !(in.floorBelow && in.y >= in.stageY - 2.0f))
                {
                    out.buttons = kRecBtnJump;
                    float aimY = iy >= R16(-8) ? (toLedgeY + 14.0f) : 0.0f;
                    if (in.ledgeOccupied && !in.ceilingAbove) aimY += 25.0f;
                    aim360(toLedgeX, aimY, out.stickX, out.stickY);
                }
            }
            else if (ix > R16(16))
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
            pressBtn(out, kRecBtnJump, lastB);
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
                const bool isLow = mix == kMixupLow;
                const bool isHigh = mix == kMixupHigh;
                const bool aboveStage = !in.ceilingAbove && in.y >= in.stageY - 4.0f;
                float targetDx = toLedgeX;
                float targetDy = iy >= 0 ? (toLedgeY + (isLow ? 4.0f : 8.0f)) : (isLow ? 2.0f : 4.0f);
                if (insideStageX && aboveStage)
                {
                    targetDx = stageCenterX - in.x;
                    if (recAbs(targetDx) < 8.0f) targetDx = stageCenterDir * 8.0f;
                    targetDy = (in.stageY + 8.0f) - in.y;
                }
                else if (in.ceilingAbove) { targetDx = -stageCenterDir * 35.0f; targetDy = 12.0f; }
                else if (in.wallAhead && iWall < R16(20) && iy > R16(6))
                {
                    targetDx = towardGoal * 4.0f;
                    targetDy = belowLedgeY + 18.0f;
                }
                else if (isHigh || (mix != kMixupDirect && (in.ledgeOccupied || (!isLow && st.option == kOptUpBHigh))))
                {
                    float off = isHigh ? 3.0f : 0.0f;
                    targetDy += 25.0f + off;
                    targetDx += towardGoal * (15.0f + off);
                }
                if (aboveStage && targetDx * stageCenterDir < 0.0f) targetDx = stageCenterDir * recAbs(targetDx);
                if ((flg & kRfQuickAttack) != 0 && in.subaction == 0x1DB && dist2D(targetDx, targetDy) > 45.0f)
                {
                    targetDy += 25.0f;
                }
                if ((flg & kRfAuraCancel) != 0 && st.option == kOptUpBCancel && in.status == 0x11B && iAnim >= R16(15))
                {
                    pressBtn(out, kRecBtnAttack, lastB);
                    out.stickX = towardGoal;
                    return;
                }
                aim360(targetDx, targetDy, out.stickX, out.stickY);
                return;
            }
            if ((flg & kRfChargeUpB) != 0 && (in.status == kStSpecialHi || in.status == 0x125))
            {
                float wantCharge = recClamp((dist2D(toLedgeX, toLedgeY) - 30.0f) * 0.6f, 8.0f, i2f(spec.shortenFrame));
                if (in.animFrame <= wantCharge) out.buttons = kRecBtnSpecial;
                if (in.animFrame > wantCharge * 0.45f) out.stickX = towardGoal;
                return;
            }
            if ((flg & kRfBoostUpB) != 0)
            {
                st.mashPhase++;
                out.stickX = driftX;
                if (ft == 0x23) { if (iy > R16(-10) && (st.mashPhase & 1u) != 0) out.buttons = kRecBtnSpecial; }
                else if (ft == 0x2E && iy < R16(-15) && ix < R16(20)) pressBtn(out, kRecBtnJump, lastB);
                return;
            }
            if (ft == 0x16 && st.option == kOptGlide) { if (iAnim < R16(5)) out.stickX = towardGoal; return; }
            if (iAnim <= R16(12) && in.vx * towardGoal < 0.0f) out.stickX = towardGoal;
            else if (in.ceilingAbove || ix > R16(5) || overStageSurface) out.stickX = driftX;
            return;
        }

        // 2g. Active Side-B handlers.
        if (matchFtStatus(kMatchSideB, ft, in.status))
        {
            out.stickX = ((flg & kRfSideBShorten) != 0 || ft == 0x25) ? towardGoal : towardLedge;
            if ((flg & kRfSideBShorten) != 0)
            {
                if ((st.option == kOptSideBShort || (in.floorBelow && ix < R16(12)))
                    && iAnim >= (static_cast<int>(spec.shortenFrame) << 4))
                {
                    pressBtn(out, kRecBtnSpecial, lastB);
                }
            }
            else if (ft == 0x22)
            {
                if (in.status == 0x11B)
                {
                    if (in.floorBelow && in.y >= in.stageY - 4.0f) pressBtn(out, kRecBtnAttack, lastB);
                    else if (st.option == kOptSideBWallJump && in.wallAhead && iWall < R16(18))
                    {
                        out.stickX = -towardLedge;
                        pressBtn(out, kRecBtnJump, lastB);
                    }
                    else if (ix <= R16(15) && iy > R16(-10) && iy < R16(10))
                    {
                        pressBtn(out, kRecBtnSpecial, lastB);
                    }
                }
                else if ((in.status == kStSpecialS && iAnim < R16(5))
                         || (iy < R16(-2) && (in.animFrame * 1.125f) < absNCX - 26.0f && iAnim < R16(32)))
                {
                    out.buttons = kRecBtnSpecial;
                }
            }
            else if (ft == 0x08 && (flg & kRfChargeSideB) != 0)
            {
                if (iAnim < R16(22) && ix > R16(40)) out.buttons = kRecBtnSpecial;
            }
            else if (ft == 0x1B && in.status == 0x124) pressBtn(out, kRecBtnJump, lastB);
            else if ((flg & kRfSideBCancel) != 0 && in.status == 0x119) pressBtn(out, kRecBtnSpecial, lastB);
            else if (ft == 0x16) out.stickY = iy > 0 ? 1.0f : (iy < R16(-10) ? -1.0f : 0.0f);
            else if (ft == 0x25) out.stickY = 1.0f;
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
                    pressBtn(out, kRecBtnSpecial, lastB);
                }
            }
            else if ((flg & kRfBombStall) != 0 && in.status == 0x11E && iAnim > R16(51) && ix > R16(25)
                     && iy > R16(-15))
            {
                out.stickY = -1.0f;
                pressBtn(out, kRecBtnSpecial, lastB);
            }
            else if (ft == 0x16)
            {
                float capeDx = overStageSurface ? (stageCenterX - in.x) : toLedgeX;
                float capeDy = overStageSurface ? ((in.stageY + 8.0f) - in.y) : toLedgeY;
                aim360(capeDx, capeDy, out.stickX, out.stickY);
            }
            else if ((flg & kRfAuraCancel) != 0 && st.option == kOptDownBCancel)
            {
                pressBtn(out, kRecBtnAttack, lastB);
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
                pressBtn(out, kRecBtnSpecial, lastB);
            }
            out.stickX = towardGoal;
            return;
        }

        // 2j. Active aerial jump rising.
        if ((in.status == kStJump || in.status == kStJumpAerial || in.status == kStFly)
            && (in.vy > 0.0f || iAnim < R16(4)))
        {
            out.stickX = (in.ceilingAbove || ix > R16(5) || overStageSurface) ? driftX : 0.0f;
            if (st.option == kOptFloat && (flg & kRfFloat) != 0)
            {
                out.buttons = kRecBtnJump;
                out.stickY = -1.0f;
            }
            return;
        }

        // 3. Free offstage airborne decision: drift + WeightedDie roll.
        out.stickX = driftX;
        if (overStageSurface || !in.canAct)
        {
            return;
        }

        if (in.wallAhead && iWall < R16(16) && iy > 0 && in.laInt > 0)
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
                pressBtn(out, kRecBtnJump, lastB);
                return;
            }
        }

        float upBX = i2f(spec.upBX);
        float upBY = i2f(spec.upBY);
        bool emergency = (in.y - in.blastBottom) <= 25.0f || iy >= ((static_cast<int>(spec.upBY) + 40) << 4);
        if (in.ceilingAbove)
        {
            out.stickX = -stageCenterDir;
            if (emergency && in.jumpsLeft)
            {
                pressBtn(out, kRecBtnJump, lastB);
            }
            return;
        }

        WeightedDie die;
        die.add(kOptNone, mix == kMixupDirect ? 0.5f : 3.0f);
        float d2 = dist2D(toLedgeX, toLedgeY);
        const bool hasAura = in.laFloat >= 65.0f;

        if (in.jumpsLeft)
        {
            bool preserveDj = in.vy > 0.2f
                              || (iy < R16(-10) && ix < R16(65) && mix != kMixupHigh)
                              || (mix == kMixupLow && iy <= R16(2) && absNCX <= upBX * 0.85f);
            if (!preserveDj && inRect16(ix, 0, 300, iyH, -15, 150))
            {
                int jumpW = spec.family == kFamMultiJump ? (in.numJumps > 0 ? in.numJumps * 8 : 10)
                                                         : (ft == 0x04 ? 25 : 8);
                if (mix == kMixupHigh && iy < R16(10)) jumpW += 16;
                if (iy > R16(5) && in.vy <= 0.0f) jumpW += 12;
                addDieI(die, kOptJump, jumpW);
            }
        }

        bool mkSaveSpecials = ft == 0x16 && in.numJumps >= 2 && iy < R16(20);

        // Table-driven rect scoring for Float, Glide, Down-B, MK Neutral-B, DairStall, and Tether.
        // Compact u8/s8 encoding: xMax 248..255 = kLim[0..7]; yMax 120..127 = kLim[0..7]; yMin -128 = -999.
        enum {
            kLimAltX = 248, kLimUpBX = 252, kXInf = 255,
            kYLimAltY25 = 121, kYLimUpBYm1 = 122, kYLimUpBY14 = 123, kYLimUpBY = 125, kYLimUpBY10 = 126,
            kYInf = 127, kYNegInf = -128
        };
        struct DieRule {
            u8 optHi, w, xMin, xMax;
            s8 yMin, yMax;
        };
        static const DieRule kRules[14] = {
            {kOptFloat | 0x80, 30, 15, kLimAltX, -25, 30},           // 0: Peach Float
            {kOptFloat | 0x80, 28, 10, kLimAltX, -20, 65},           // 1: Mewtwo Hover
            {kOptNeutralB, 14, 26, kXInf, kYNegInf, 19},             // 2: Ganon Float
            {kOptGlide, 14, 15, kLimAltX, -35, kYLimAltY25},         // 3: Glide
            {kOptDownB, 14, 26, kXInf, -19, kYLimUpBYm1},            // 4: Samus Bomb
            {kOptDownB, 16, 15, 75, -20, 35},                        // 5: ZSS Flip
            {kOptDownB, 14, 16, kXInf, -9, kYLimUpBY14},             // 6: Mario/Luigi Mash
            {kOptDownB, 14, 0, 90, kYNegInf, 19},                    // 7: Lucario Down-B / Cancel
            {kOptDownB, 16, 0, 44, 16, kYInf},                       // 8: Wario Waft
            {kOptNeutralB, 10, 10, 80, -10, 45},                     // 9: MK Mach Tornado
            {kOptDairStall, 8, 46, kXInf, kYNegInf, -11},            // 10: Ivysaur DairStall
            {kOptUpB, 24, 0, 30, 2, kYLimUpBY},                      // 11: Ivysaur Tether Up-B
            {kOptUpB, 24, 0, kLimUpBX, -15, kYLimUpBY10},            // 12: Olimar Tether Up-B
            {kOptTether, 18, 8, kLimUpBX, -15, kYLimUpBY10}          // 13: Standard Tether
        };
        u32 rMask = 0;
        if ((flg & kRfFloat) != 0 && st.usedFloat == 0)
        {
            if (ft == 0x0C && !in.laFlag) rMask |= 1u << 0;
            else if (ft == 0x26 && in.jumpsLeft && !in.laFlag) rMask |= 1u << 1;
            else if (ft == 0x14 && in.laInt < 2) rMask |= 1u << 2;
        }
        if ((flg & kRfGlide) != 0 && !mkSaveSpecials) rMask |= 1u << 3;
        if ((flg & kRfBombStall) != 0 && mix != kMixupDirect && in.vy < -1.0f) rMask |= 1u << 4;
        if ((flg & kRfFlipDownB) != 0) rMask |= 1u << 5;
        if ((flg & kRfMashDownB) != 0 && !in.laFlag) rMask |= 1u << 6;
        if ((flg & kRfAuraCancel) != 0 && !in.laFlag) rMask |= 1u << 7;
        if ((flg & kRfWaft) != 0 && in.laInt >= 2) rMask |= 1u << 8;
        if (ft == 0x16 && !mkSaveSpecials) rMask |= 1u << 9;
        if ((flg & kRfDairStall) != 0 && mix != kMixupDirect && in.vy < -0.5f) rMask |= 1u << 10;
        if ((flg & kRfTether) != 0)
        {
            if (ft == 0x1F || ft == 0x19) { if (!in.jumpsLeft || iy > R16(20)) rMask |= 1u << (ft == 0x1F ? 11 : 12); }
            else if (in.lr * towardLedge > 0.0f) rMask |= 1u << 13;
        }
        const int kLim[8] = {
            spec.altX, spec.altY + 25, spec.upBY - 1, spec.upBY + 14, spec.upBX, spec.upBY, spec.upBY + 10, 999
        };
        for (int i = 0; i < 14; i++)
        {
            if (((rMask >> i) & 1u) == 0) continue;
            const DieRule& r = kRules[i];
            int xMax = r.xMax >= kLimAltX ? kLim[r.xMax - kLimAltX] : static_cast<int>(r.xMax);
            int yMin = r.yMin == kYNegInf ? -999 : static_cast<int>(r.yMin);
            int yMax = r.yMax >= 120 ? kLim[r.yMax - 120] : static_cast<int>(r.yMax);
            if (inRect16(ix, r.xMin, xMax, (r.optHi & 0x80) != 0 ? iyH : iy, yMin, yMax))
            {
                addDieI(die, (i == 7 && hasAura) ? kOptDownBCancel : static_cast<int>(r.optHi & 0x7F), r.w);
            }
        }

        if (spec.sideBX > 0 && !mkSaveSpecials)
        {
            bool allowSideB = inRect16(ix, 12, spec.sideBX, iyH, spec.sideBY - 25, spec.sideBY + 35);
            const bool wallLow = in.wallAhead && iWall < R16(24) && iy > R16(4);
            struct SbRect { u8 ft, mode, x0, x1; s8 y0, y1; };
            // mode: 0=spacie, 1=falcon, 2=ganon, 3=mario, 4=marth, 5=puff
            // x1: 254=sideBX, 255=999; y0: -127=-150, -128=-999; y1: 127=sideBY
            static const SbRect kSb[8] = {
                {0x06, 0, 12, 62, -35, 4},   {0x13, 0, 12, 62, -35, 4},   {0x2C, 0, 12, 62, -35, 8},
                {0x09, 1, 12, 48, -40, -4},  {0x14, 2, 12, 254, -40, 2},  {0x00, 3, 26, 255, -128, 12},
                {0x11, 4, 25, 255, -128, 25},{0x25, 5, 0, 254, -127, 127}
            };
            bool matchedSb = false;
            for (int si = 0; si < 8; si++)
            {
                if (kSb[si].ft != ft) continue;
                matchedSb = true;
                int x1 = kSb[si].x1 >= 254 ? (kSb[si].x1 == 254 ? static_cast<int>(spec.sideBX) : 999) : static_cast<int>(kSb[si].x1);
                int y0 = kSb[si].y0 <= -127 ? (kSb[si].y0 == -127 ? -150 : -999) : static_cast<int>(kSb[si].y0);
                int y1 = kSb[si].y1 == 127 ? static_cast<int>(spec.sideBY) : static_cast<int>(kSb[si].y1);
                bool ok = inRect16(ix, kSb[si].x0, x1, iy, y0, y1);
                u8 md = kSb[si].mode;
                if (md == 0) allowSideB = ok && !(in.jumpsLeft && ix > R16(45)) && !wallLow && !(mix == kMixupLow && iy < R16(-8));
                else if (md == 1) allowSideB = ok && !in.wallAhead;
                else if (md == 2) allowSideB = ok && !wallLow;
                else if (md == 3 || md == 4) allowSideB = allowSideB && ok && mix != kMixupDirect && in.vy < (md == 4 ? -0.2f : 0.0f);
                else allowSideB = ok;
                break;
            }
            if (!matchedSb && ((ft == 0x26 && (recAbs(in.vx) <= 1.2f || iy >= 0)) || (ft == 0x22 && iy > R16(2))
                               || (ft == 0x1B && in.laFlag)))
            {
                allowSideB = false;
            }
            if (allowSideB)
            {
                if ((flg & kRfSideBShorten) != 0)
                {
                    addDieI(die, kOptSideB, mix == kMixupHigh ? 14 : 4);
                    if (ix <= R16(16) && mix != kMixupHigh) addDieI(die, kOptSideBShort, 12);
                }
                else if (ft == 0x22 && in.wallAhead && in.laInt > 0)
                {
                    addDieI(die, kOptSideBWallJump, 25);
                    addDieI(die, kOptSideB, 15);
                }
                else addDieI(die, kOptSideB, 12);
            }
        }

        bool inUpB = false;
        if (spec.upBY > 0 && !mkSaveSpecials)
        {
            inUpB = (flg & kRf360Aim) != 0 ? (d2 <= upBX + 8.0f && (iy >= R16(-15) || ix > R16(25)))
                                           : inRect16(ix, 0, spec.upBX + 8, iy, -15, spec.upBY + 15);
        }
        if ((ft == 0x2E && in.laFlag && in.jumpsLeft) || (ft == 0x1F && (iy < R16(2) || ix > R16(32)))
            || ((flg & kRfPkThunder) != 0 && ix < R16(22) && iy < R16(-4)))
        {
            inUpB = false;
        }
        bool allowUpBWithJump = spec.family != kFamMultiJump && spec.family != kFamPkThunder
                                && belowLedgeY > upBY * 0.45f && iy <= (static_cast<int>(spec.upBY) << 4);
        if (inUpB && (!in.jumpsLeft || allowUpBWithJump))
        {
            if ((flg & kRfAuraCancel) != 0 && hasAura) addDieI(die, kOptUpBCancel, 25);
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
                emOpt = ((flg & kRfFloat) != 0 && ft == 0x26 && st.usedFloat == 0 && !in.laFlag) ? kOptFloat : kOptJump;
            }
            else if (ft == 0x16) emOpt = kOptGlide;
            else if (ft == 0x04 || ft == 0x25) emOpt = kOptSideB;
            addDieI(die, emOpt, 100);
        }

        int pick = die.roll(recRand01(rng), kOptNone);
        if (pick != kOptNone) st.option = static_cast<u8>(pick);

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
            {0, 0, 0}, {kRecBtnJump, 2, 0}, {kRecBtnJump, 0, -1},
            {kRecBtnSpecial, 0, 1}, {kRecBtnSpecial, 0, 1}, {kRecBtnSpecial, 0, 1}, {kRecBtnSpecial, 0, 1},
            {kRecBtnSpecial, 1, 0}, {kRecBtnSpecial, 1, 0}, {kRecBtnSpecial, 1, 0},
            {kRecBtnSpecial, 0, -1}, {kRecBtnSpecial, 0, -1}, {kRecBtnSpecial, 0, 0},
            {kRecBtnGuard | kRecBtnAttack, 1, 0}, {kRecBtnAttack, 0, -1}
        };
        static const float kSy[3] = {-1.0f, 0.0f, 1.0f};
        if (pick > 0 && pick < 15)
        {
            const OptExec& e = kExec[pick];
            pressBtn(out, e.btn, lastB);
            out.stickX = e.sxKind == 1 ? towardLedge
                         : (e.sxKind == 2 ? (in.ceilingAbove ? -stageCenterDir : towardLedge) : 0.0f);
            out.stickY = kSy[e.sy + 1];
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
#undef R16

    void stepRecovery(const RecoveryInput& in, RecoveryState& st, u32& rng, RecoveryOutput& out)
    {
        memset(&out, 0, sizeof(out));
        const FighterRecoverySpec& spec = recoverySpec(in.ftKind);

        if (in.status >= kStCliffCatchMove && in.status <= kStCliffLast && in.status != kStCliffJump2)
        {
            out.active = true;
            if (in.status == kStCliffWait)
            {
                if (st.ledgeWaitFrames == 0) st.cliffChoice = static_cast<u8>(geomNextRng(rng) & 3u);
                if (++st.ledgeWaitFrames >= 2)
                {
                    static const u8 kCliffBtn[4] = {0, kRecBtnGuard, kRecBtnJump, kRecBtnAttack};
                    u8 ch = st.cliffChoice & 3u;
                    if (kCliffBtn[ch] != 0) pressBtn(out, kCliffBtn[ch], in.lastButtons);
                    if (ch == 0 || ch == 2) out.stickX = in.ledgeDir != 0.0f ? in.ledgeDir : -recSign(in.x);
                }
            }
            else st.ledgeWaitFrames = 0;
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

        stepRecoveryBody(spec, in, st, rng, overStage, out);
        st.prevStatus = static_cast<s16>(in.status);
    }
}
}
