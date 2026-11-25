#include "extra/pplus_recovery_core.h"

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
        kStSpecialLw = 0x115
    };

    // Portable fast square root (zero libm dependency for MWCC and MSVC).
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

    static float recAbs(float v)
    {
        return v < 0.0f ? -v : v;
    }

    static float recSign(float v)
    {
        return v < 0.0f ? -1.0f : 1.0f;
    }

    static float recClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
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
    {1, fam, nco, ncn, flg, ux, uy, sx, sy, ax, ay, fs, sf, ps, pe, pt, la, dj}
#define UNUSED_SPEC {0, kFamArc, 6, 4, kRfNone, 40, 45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32}

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
        1, kFamArc, 6, 4, kRfNone, 45, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32
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
        st.initialized = 0;
        st.option = kOptNone;
        st.highMixup = 0;
        st.timer = 0;
        st.mashPhase = 0;
        st.holdFrames = 0;
        st.usedFloat = 0;
        st.ledgeWaitFrames = 0;
        st.prevStatus = -1;
        st.cliffChoice = 0;
        st.reserved = 0;
    }

    // Helper: emit a fresh press of `btn`, inserting a 1-frame release if it
    // was held on the previous frame so Brawl sees an edge.
    static u32 freshButton(u32 btn, u32 lastButtons)
    {
        return (lastButtons & btn) != 0 ? 0u : btn;
    }

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

    // Active PK Thunder orbit steering (Ness/Lucas 103_recoveryHub.as).
    static void steerPkThunder(const FighterRecoverySpec& spec, const RecoveryInput& in, RecoveryState& st,
                               float toLedgeX, float toLedgeY, float towardGoal, float stageCenterDir,
                               RecoveryOutput& out)
    {
        bool spawningOrHolding = in.status == kStSpecialHi || in.status == 0x11A;
        bool wasPkt = st.prevStatus == kStSpecialHi || st.prevStatus == 0x11A;
        if (!wasPkt && spawningOrHolding)
        {
            st.timer = spec.pktTime;
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
        float tFrac = spec.pktTime > 0 ? static_cast<float>(st.timer) / static_cast<float>(spec.pktTime) : 0.0f;
        float startDist = spec.pktStartDist > 38 ? 38.0f : static_cast<float>(spec.pktStartDist);
        float orbitDist = static_cast<float>(spec.pktEndDist) + tFrac * (startDist - static_cast<float>(spec.pktEndDist));
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

    static bool isUpBStatus(int ftKind, int s)
    {
        if (s == kStSpecialHi)
        {
            return true;
        }
        // Fighter-specific Up-B continuation statuses (PPlusAI $genActions).
        switch (ftKind)
        {
        case 0x06: // Fox
        case 0x13: // Falco
        case 0x2C: // Wolf
            return s == 0x117;
        case 0x0A: // Ness
        case 0x1A: // Lucas
            return s >= 0x11A && s <= 0x11C;
        case 0x1B: // Diddy
            return s == 0x125 || s == 0x127;
        case 0x21: // Lucario
        case 0x26: // Mewtwo
            return s == 0x11B || s == 0x11C;
        case 0x22: // Ike
            return s >= 0x11E && s <= 0x120;
        case 0x0C: // Peach
            return s >= 0x11A && s <= 0x11C;
        case 0x16: // Meta Knight
            return s == 0x11D;
        default:
            return false;
        }
    }

    static bool isSideBStatus(int ftKind, int s)
    {
        if (s == kStSpecialS)
        {
            return true;
        }
        switch (ftKind)
        {
        case 0x06: // Fox
            return s == 0x129;
        case 0x1B: // Diddy
            return s == 0x11B || s == 0x11C || s == 0x124;
        case 0x1E: // Squirtle
            return s == 0x119;
        case 0x22: // Ike
            return s >= 0x11A && s <= 0x11C;
        case 0x16: // Meta Knight
            return s == 0x119 || s == 0x11A;
        default:
            return false;
        }
    }

    static bool isDownBStatus(int ftKind, int s)
    {
        if (s == kStSpecialLw)
        {
            return true;
        }
        switch (ftKind)
        {
        case 0x03: // Samus
            return s == 0x11D || s == 0x11E || s == 0x120 || s == 0x121;
        case 0x16: // Meta Knight
            return s == 0x11B || s == 0x11C;
        default:
            return false;
        }
    }

    static bool isSpecialFallStatus(int ftKind, int s)
    {
        if (s == kStFallSpecial)
        {
            return true;
        }
        switch (ftKind)
        {
        case 0x06: // Fox
        case 0x13: // Falco
        case 0x2C: // Wolf (Fire Fox/Bird/Wolf end fall 0x118)
            return s == 0x118;
        case 0x22: // Ike Quick Draw special fall
            return s == 0x11D;
        case 0x2E: // Snake Cypher special fall
            return s == 0x118;
        default:
            return false;
        }
    }

    static bool isTetherStatus(int s)
    {
        return (s >= kStAirLasso && s <= kStAirLassoFailure) || s == kStCliffJump2;
    }

    // Recovery rectangle helper matching PPlusAI $if_recoveryRect:
    // absX in [xMin, xMax] and yBelowLedge in [yMin, yMax].
    static bool inRect(float absX, float xMin, float xMax, float yVal, float yMin, float yMax)
    {
        return absX >= xMin && absX <= xMax && yVal >= yMin && yVal <= yMax;
    }

    void stepRecovery(const RecoveryInput& in, RecoveryState& st, u32& rng, RecoveryOutput& out)
    {
        out.buttons = 0;
        out.stickX = 0.0f;
        out.stickY = 0.0f;
        out.active = false;

        const FighterRecoverySpec& spec = recoverySpec(in.ftKind);

        // 1. Ledge hanging (CliffCatch / CliffWait / Cliff*): cleanly getup.
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
                // Wait 2 frames in CliffWait so Brawl registers the getup input cleanly.
                if (st.ledgeWaitFrames >= 2)
                {
                    float towardStage = in.ledgeDir != 0.0f ? in.ledgeDir : -recSign(in.x);
                    switch (st.cliffChoice)
                    {
                    case 0: // Regular getup toward stage
                        out.stickX = towardStage;
                        break;
                    case 1: // Ledge roll
                        out.buttons = freshButton(kRecBtnGuard, in.lastButtons);
                        break;
                    case 2: // Ledge jump
                        out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                        out.stickX = towardStage;
                        break;
                    default: // Ledge attack
                        out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
                        break;
                    }
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

        // If grounded safely on stage and not in a recovery special, stop recovering.
        bool onStageFloor = !in.air && in.x >= in.stageLeftX - 2.0f && in.x <= in.stageRightX + 2.0f;
        if (onStageFloor)
        {
            recoveryStateReset(st);
            return;
        }

        // Airborne over the stage floor with nothing urgent: stop once safely back.
        bool overStage = in.x >= in.stageLeftX + 2.0f && in.x <= in.stageRightX - 2.0f && in.y >= in.stageY - 6.0f;
        bool safeLandingStatus =
            (in.status <= kStLandingFallSpecial && in.status != kStFallSpecial) || (in.ftKind == 0x26 && in.status == 0x11E);
        if (overStage && in.floorBelow && !in.ceilingAbove && safeLandingStatus)
        {
            recoveryStateReset(st);
            return;
        }

        out.active = true;

        // First frame of a new recovery: roll high-recovery mixup (PPlusAI:
        // ledgeOccupied && rnd < 0.80, else rnd < 0.25).
        if (!st.initialized)
        {
            st.initialized = 1;
            st.option = kOptNone;
            st.usedFloat = 0;
            st.holdFrames = 0;
            st.mashPhase = 0;
            float r = geomRand01(rng);
            bool goHigh = (in.ledgeOccupied && r < 0.80f) || (!in.ledgeOccupied && r < 0.25f);
            st.highMixup = goHigh ? static_cast<u8>(15.0f + 40.0f * geomRand01(rng)) : 0;
        }

        // Vector from fighter to nearest ledge.
        // nearCliffX: horizontal vector to ledge (+ right, - left)
        // belowLedgeY: positive when fighter is BELOW the ledge, negative when ABOVE.
        float toLedgeX = in.ledgeX - in.x;
        float toLedgeY = in.ledgeY - in.y;
        float absNCX = recAbs(toLedgeX);
        float belowLedgeY = toLedgeY; // > 0 below ledge, < 0 above ledge
        float highBelowY = belowLedgeY + (!in.ceilingAbove ? static_cast<float>(st.highMixup) : 0.0f);
        float stageCenterX = 0.5f * (in.stageLeftX + in.stageRightX);
        float stageCenterDir = in.x > stageCenterX ? -1.0f : 1.0f;
        float towardLedge = toLedgeX == 0.0f ? stageCenterDir : recSign(toLedgeX);
        bool insideStageX = in.x >= in.stageLeftX && in.x <= in.stageRightX;
        bool overStageSurface = insideStageX && !in.ceilingAbove && in.y >= in.stageY - 8.0f;
        // Once horizontally over the stage surface, steer inward toward stage center
        // rather than back outward toward the ledge lip.
        float towardGoal = overStageSurface ? stageCenterDir : towardLedge;

        // 2. Active recovery state handlers (specials, glide, float, tether, special fall).

        // 2a. Special Fall (0x10, Spacie 0x118, Ike 0x11D, Snake 0x118): drift directly toward stage center.
        if (isSpecialFallStatus(in.ftKind, in.status))
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : stageCenterDir;
            st.prevStatus = static_cast<s16>(in.status);
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
                // Pitch up when below ledge, level/down when well above, cancel near stage.
                if (belowLedgeY > 2.0f)
                {
                    out.stickX = -towardGoal; // pull back to pitch up in Brawl glide
                }
                else if (belowLedgeY < -15.0f && in.vy > -1.25f)
                {
                    out.stickX = towardGoal;
                }
                if ((absNCX < 10.0f && belowLedgeY < 0.0f) || overStage || overStageSurface)
                {
                    out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                }
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2c. Tether hang / reel-in (0x7F..0x83, ZSS 0x7A/0x81).
        if (isTetherStatus(in.status))
        {
            st.mashPhase++;
            out.stickX = towardGoal;
            if (in.status == kStAirLassoHang || in.status == kStCliffJump2)
            {
                // Reel in with Jump or Up.
                out.buttons = (st.mashPhase & 1u) != 0 ? freshButton(kRecBtnJump, in.lastButtons) : 0u;
                out.stickY = 1.0f;
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2d. Peach Float (0x11E) & Mewtwo Hover (0x120).
        if (spec.floatStatus != 0 && in.status == static_cast<int>(spec.floatStatus))
        {
            st.usedFloat = 1;
            if (in.ftKind == 0x26) // Mewtwo 8-way hover
            {
                bool safeOverFloor = in.floorBelow && in.y >= in.stageY - 2.0f;
                float dist = recSqrt(toLedgeX * toLedgeX + toLedgeY * toLedgeY);
                if (dist > 8.0f && !overStage && !overStageSurface && !safeOverFloor)
                {
                    out.buttons = kRecBtnJump;
                    // Aim cleanly above the ledge lip so hover never dives underneath the stage.
                    float aimY = belowLedgeY >= -8.0f ? (toLedgeY + 14.0f) : 0.0f;
                    if (in.ledgeOccupied && !in.ceilingAbove)
                    {
                        aimY += 25.0f;
                    }
                    aim360(toLedgeX, aimY, out.stickX, out.stickY);
                }
            }
            else // Peach horizontal float
            {
                if (absNCX > 16.0f && !overStage && !overStageSurface)
                {
                    out.buttons = kRecBtnJump;
                    out.stickX = towardGoal;
                }
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2e. Wall attach / wall climb (Knuckles, Lucario, etc.).
        if (in.status == kStAttachWall || in.status == kStAttachWallWait)
        {
            out.stickX = -towardLedge;
            out.buttons = freshButton(kRecBtnJump, in.lastButtons);
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2f. Active Up-B handlers.
        if (isUpBStatus(in.ftKind, in.status))
        {
            // Article-guided PK Thunder (Ness / Lucas).
            if ((spec.flags & kRfPkThunder) != 0)
            {
                steerPkThunder(spec, in, st, toLedgeX, toLedgeY, towardGoal, stageCenterDir, out);
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // 360-Aim Up-B (Fox, Falco, Wolf, Sheik, Zelda, Mewtwo, Pikachu, Lucario).
            if ((spec.flags & kRf360Aim) != 0)
            {
                float targetDx = toLedgeX;
                float targetDy = belowLedgeY >= 0.0f ? (toLedgeY + 8.0f) : 4.0f;
                if (insideStageX && !in.ceilingAbove && in.y >= in.stageY - 4.0f)
                {
                    // Already horizontally over the stage: aim inward onto stage, never outward offstage.
                    targetDx = stageCenterX - in.x;
                    if (recAbs(targetDx) < 8.0f)
                    {
                        targetDx = stageCenterDir * 8.0f;
                    }
                    targetDy = (in.stageY + 8.0f) - in.y;
                }
                else if (in.ceilingAbove)
                {
                    targetDx += -stageCenterDir * 12.0f;
                }
                else if (in.ledgeOccupied || st.option == kOptUpBHigh)
                {
                    targetDy += 25.0f;
                    targetDx += towardGoal * 15.0f;
                }
                if (!in.ceilingAbove && in.y >= in.stageY - 4.0f && targetDx * stageCenterDir < 0.0f)
                {
                    targetDx = stageCenterDir * recAbs(targetDx);
                }
                // Pikachu 2-angle Quick Attack (subactions 0x1DB first zip, 0x1DC second zip).
                if ((spec.flags & kRfQuickAttack) != 0 && in.subaction == 0x1DB)
                {
                    float dist = recSqrt(targetDx * targetDx + targetDy * targetDy);
                    if (dist > 45.0f)
                    {
                        targetDy += 25.0f;
                    }
                }
                // Lucario Aura cancel on frame >= 15 when opting for cancel.
                if ((spec.flags & kRfAuraCancel) != 0 && st.option == kOptUpBCancel && in.status == 0x11B
                    && in.animFrame >= 15.0f)
                {
                    out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
                    out.stickX = towardGoal;
                    st.prevStatus = static_cast<s16>(in.status);
                    return;
                }
                aim360(targetDx, targetDy, out.stickX, out.stickY);
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Diddy Kong charged barrel jetpack (Up-B 0x114 / 0x125).
            if ((spec.flags & kRfChargeUpB) != 0 && (in.status == kStSpecialHi || in.status == 0x125))
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
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // ROB / Snake boost Up-B.
            if ((spec.flags & kRfBoostUpB) != 0)
            {
                st.mashPhase++;
                out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;
                if (in.ftKind == 0x23) // ROB pulses B while below ledge
                {
                    if (belowLedgeY > -10.0f && (st.mashPhase & 1u) != 0)
                    {
                        out.buttons = kRecBtnSpecial;
                    }
                }
                else if (in.ftKind == 0x2E && belowLedgeY < -15.0f && absNCX < 20.0f) // Snake jump-cancel Cypher
                {
                    out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                }
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Meta Knight glide entry via Up-B.
            if (in.ftKind == 0x16 && st.option == kOptGlide)
            {
                if (in.animFrame < 5.0f)
                {
                    out.stickX = towardGoal;
                }
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Standard Arc Up-B: {BREVERSE} early check + drift toward stage.
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
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2g. Active Side-B handlers.
        if (isSideBStatus(in.ftKind, in.status))
        {
            // Spacie Side-B shorten (Fox 18f, Falco 14f, Wolf 17f).
            // Never midair-shorten a full Illusion while still offstage with no floor below.
            if ((spec.flags & kRfSideBShorten) != 0)
            {
                if ((st.option == kOptSideBShort || (in.floorBelow && absNCX < 12.0f))
                    && in.animFrame >= static_cast<float>(spec.shortenFrame))
                {
                    out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
                }
                out.stickX = towardGoal;
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Ike charged Quick Draw (0x113 / 0x11A / 0x11B).
            if (in.ftKind == 0x22)
            {
                out.stickX = towardLedge;
                if (in.status == kStSpecialS && in.animFrame < 5.0f)
                {
                    // Ensure Ike turns toward the stage during Quick Draw startup.
                    out.buttons = kRecBtnSpecial;
                }
                else if (in.status == 0x11B && in.floorBelow && in.y >= in.stageY - 4.0f)
                {
                    // Stop cleanly once over the stage floor instead of flying off the far side.
                    out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
                }
                else if (in.status == 0x11B && st.option == kOptSideBWallJump && in.wallAhead && in.wallDist < 18.0f)
                {
                    out.stickX = -towardLedge;
                    out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                }
                else if (in.status != 0x11B)
                {
                    float travelDist = 45.0f + (in.animFrame * 0.125f - 3.0f) * 9.0f;
                    // Never keep charging once Ike drops to ledge height (otherwise Quick Draw flies under the stage).
                    if (belowLedgeY < -2.0f && travelDist < absNCX - 8.0f && in.animFrame < 32.0f)
                    {
                        out.buttons = kRecBtnSpecial;
                    }
                }
                else if (absNCX <= 15.0f && belowLedgeY > -10.0f && belowLedgeY < 10.0f)
                {
                    out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
                }
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Luigi Green Missile charge.
            if (in.ftKind == 0x08 && (spec.flags & kRfChargeSideB) != 0)
            {
                out.stickX = towardLedge;
                if (in.animFrame < 22.0f && absNCX > 40.0f)
                {
                    out.buttons = kRecBtnSpecial;
                }
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Diddy Monkey Flip jump cancel (0x124 -> X).
            if (in.ftKind == 0x1B && in.status == 0x124)
            {
                out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                out.stickX = towardLedge;
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Squirtle Withdraw cancel (0x119 -> B).
            if ((spec.flags & kRfSideBCancel) != 0 && in.status == 0x119)
            {
                out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
                out.stickX = towardLedge;
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Meta Knight Drill Rush pitch.
            if (in.ftKind == 0x16)
            {
                out.stickX = towardLedge;
                out.stickY = belowLedgeY > 0.0f ? 1.0f : (belowLedgeY < -10.0f ? -1.0f : 0.0f);
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Jigglypuff Pound angled upward while drifting toward ledge (fudgepop01 PPlusAI purin).
            if (in.ftKind == 0x25)
            {
                out.stickX = towardGoal;
                out.stickY = 1.0f;
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            // Default Side-B drift + B-reverse check.
            out.stickX = towardGoal;
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2h. Active Down-B handlers (Luigi/Mario mash, Samus bomb, Meta Knight cape, Lucario cancel).
        if (isDownBStatus(in.ftKind, in.status))
        {
            st.mashPhase++;
            out.stickX = towardGoal;
            if ((spec.flags & kRfMashDownB) != 0)
            {
                if ((st.mashPhase & 1u) != 0)
                {
                    out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
                }
            }
            else if ((spec.flags & kRfBombStall) != 0 && in.status == 0x11E && in.animFrame > 51.0f
                     && absNCX > 25.0f && belowLedgeY > -15.0f)
            {
                out.stickY = -1.0f;
                out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            }
            else if (in.ftKind == 0x16) // Meta Knight Dimensional Cape aim
            {
                float capeDx = overStageSurface ? (stageCenterX - in.x) : toLedgeX;
                float capeDy = overStageSurface ? ((in.stageY + 8.0f) - in.y) : toLedgeY;
                aim360(capeDx, capeDy, out.stickX, out.stickY);
            }
            else if ((spec.flags & kRfAuraCancel) != 0 && st.option == kOptDownBCancel)
            {
                out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
                out.stickY = -1.0f;
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2i. Meta Knight Neutral-B (Mach Tornado mash).
        if (in.ftKind == 0x16 && (in.status == kStSpecialN || in.status == 0x117 || in.status == 0x118))
        {
            st.mashPhase++;
            if ((st.mashPhase % 3u) != 2u)
            {
                out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            }
            out.stickX = towardGoal;
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 2j. Active aerial jump rising (Jump, JumpAerial, multi-jump Fly):
        // steer cleanly toward stage/ledge (or out from under stage).
        if ((in.status == kStJump || in.status == kStJumpAerial || in.status == kStFly)
            && (in.vy > 0.0f || in.animFrame < 4.0f))
        {
            out.stickX = in.ceilingAbove ? -stageCenterDir : ((absNCX > 5.0f || overStageSurface) ? towardGoal : 0.0f);
            // If we picked Float (Peach/Mewtwo), hold jump + down to enter float on takeoff.
            if (st.option == kOptFloat && (spec.flags & kRfFloat) != 0)
            {
                out.buttons = kRecBtnJump;
                out.stickY = -1.0f;
            }
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // 3. Free offstage airborne decision: drift + WeightedDie roll.
        // Default drift: move out from under stage if ceiling is directly above,
        // otherwise drift inward toward the stage / nearest ledge.
        out.stickX = in.ceilingAbove ? -stageCenterDir : towardGoal;

        // Already horizontally over the stage surface: drift inward to land cleanly
        // instead of triggering a recovery special over the stage.
        if (overStageSurface)
        {
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        if (!in.canAct)
        {
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // Wall-jump / wall-climb check when touching or right beside a stage wall.
        if (in.wallAhead && in.wallDist < 16.0f && belowLedgeY > 0.0f)
        {
            if ((spec.flags & kRfWallClimb) != 0 && in.laInt > 0)
            {
                out.stickX = towardLedge;
                out.stickY = 1.0f;
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
            if ((spec.flags & kRfWallJump) != 0 && in.laInt > 0 && in.vy < 0.5f)
            {
                out.stickX = -towardLedge;
                out.buttons = freshButton(kRecBtnJump, in.lastButtons);
                st.prevStatus = static_cast<s16>(in.status);
                return;
            }
        }

        // If directly underneath the stage, steer outward first unless dangerously close
        // to the bottom blastzone (where emergency recovery must trigger).
        bool emergency = (in.y - in.blastBottom) <= 25.0f || belowLedgeY >= static_cast<float>(spec.upBY) + 40.0f;
        if (in.ceilingAbove && !emergency)
        {
            out.stickX = -stageCenterDir;
            st.prevStatus = static_cast<s16>(in.status);
            return;
        }

        // Populate stack-only WeightedDie based on fighter family & recovery rects.
        WeightedDie die;
        die.add(kOptNone, 3.0f);

        float dist2D = recSqrt(toLedgeX * toLedgeX + toLedgeY * toLedgeY);

        // Double jump option (all fighters).
        if (in.jumpsLeft)
        {
            if (inRect(absNCX, 0.0f, 300.0f, highBelowY, -15.0f, 150.0f))
            {
                float jumpW = spec.family == kFamMultiJump
                                  ? static_cast<float>(in.numJumps > 0 ? in.numJumps * 8 : 10)
                                  : (in.ftKind == 0x04 ? 25.0f : 8.0f);
                // Prefer jumping sooner when falling below ledge height.
                if (belowLedgeY > 5.0f && in.vy <= 0.0f)
                {
                    jumpW += 12.0f;
                }
                die.add(kOptJump, jumpW);
            }
        }

        // Family-specific recovery options when outside stage (not under ceiling).
        if (!in.ceilingAbove)
        {
            // Tether grab (Z / Guard in air when facing ledge within tether box).
            if ((spec.flags & kRfTether) != 0 && absNCX <= static_cast<float>(spec.upBX)
                && belowLedgeY >= -15.0f && belowLedgeY <= static_cast<float>(spec.upBY) + 10.0f)
            {
                // Ivysaur Vine Whip shoots diagonally upward and enters freefall on miss:
                // require being below ledge, within whip reach, and not wasting an unused jump.
                if (in.ftKind == 0x1F)
                {
                    if ((!in.jumpsLeft || belowLedgeY > 20.0f) && absNCX <= 30.0f && belowLedgeY >= 2.0f
                        && belowLedgeY <= static_cast<float>(spec.upBY))
                    {
                        die.add(kOptUpB, 24.0f);
                    }
                }
                else if (in.ftKind == 0x19) // Olimar Up-B tether
                {
                    if (!in.jumpsLeft || belowLedgeY > 20.0f)
                    {
                        die.add(kOptUpB, 24.0f);
                    }
                }
                else if (in.lr * towardLedge > 0.0f && absNCX >= 8.0f)
                {
                    die.add(kOptTether, 18.0f);
                }
            }

            // Float / Hover (Peach, Mewtwo, Ganondorf).
            if ((spec.flags & kRfFloat) != 0 && st.usedFloat == 0)
            {
                if (in.ftKind == 0x0C && !in.laFlag
                    && inRect(absNCX, 15.0f, static_cast<float>(spec.altX), highBelowY, -25.0f, 30.0f))
                {
                    die.add(kOptFloat, 30.0f);
                }
                else if (in.ftKind == 0x26 && in.jumpsLeft && !in.laFlag
                         && inRect(absNCX, 10.0f, static_cast<float>(spec.altX), highBelowY, -20.0f, 65.0f))
                {
                    die.add(kOptFloat, 28.0f);
                }
                else if (in.ftKind == 0x14 && in.laInt < 2 && absNCX > 25.0f && belowLedgeY < 20.0f)
                {
                    die.add(kOptNeutralB, 14.0f);
                }
            }

            // Glide (Meta Knight, Pit, Charizard).
            if ((spec.flags & kRfGlide) != 0
                && inRect(absNCX, 15.0f, static_cast<float>(spec.altX), belowLedgeY, -35.0f,
                          static_cast<float>(spec.altY) + 25.0f))
            {
                die.add(kOptGlide, 14.0f);
            }

            // Side-B recovery (Spacies, Marth, Roy, Falcon, Ike, Diddy, Luigi, Mario, MK, Wario, etc.).
            if (spec.sideBX > 0)
            {
                float sbMinY = static_cast<float>(spec.sideBY) - 25.0f;
                float sbMaxY = static_cast<float>(spec.sideBY) + 35.0f;
                bool allowSideB = inRect(absNCX, 12.0f, static_cast<float>(spec.sideBX), highBelowY, sbMinY, sbMaxY);
                // Horizontal freefall Side-B (Spacies Illusion/Flash, Falcon/Ganon Raptor Boost/Flame Choke):
                // only fire near or above ledge height and within real travel reach, never deep below ledge.
                if (in.ftKind == 0x06 || in.ftKind == 0x13 || in.ftKind == 0x2C)
                {
                    float maxBelow = in.ftKind == 0x2C ? 8.0f : 4.0f;
                    allowSideB = inRect(absNCX, 12.0f, 62.0f, belowLedgeY, -35.0f, maxBelow);
                    if (in.jumpsLeft && absNCX > 45.0f)
                    {
                        allowSideB = false;
                    }
                }
                else if (in.ftKind == 0x09 || in.ftKind == 0x14)
                {
                    allowSideB = inRect(absNCX, 12.0f, static_cast<float>(spec.sideBX), belowLedgeY, -40.0f, 2.0f);
                }
                // Jigglypuff Pound (fudgepop01 PPlusAI purin): y_rangeAbove(100, 250) covers [-150, 100].
                if (in.ftKind == 0x25)
                {
                    allowSideB = inRect(absNCX, 0.0f, static_cast<float>(spec.sideBX), belowLedgeY, -150.0f,
                                        static_cast<float>(spec.sideBY));
                }
                // Mewtwo Side-B (Confusion) only stalls horizontal momentum once; skip when falling vertically.
                if (in.ftKind == 0x26 && (recAbs(in.vx) <= 1.2f || belowLedgeY >= 0.0f))
                {
                    allowSideB = false;
                }
                // Ike Quick Draw drops vertically during charge; only start Side-B above ledge height.
                if (in.ftKind == 0x22 && belowLedgeY > 2.0f)
                {
                    allowSideB = false;
                }
                // Diddy Kong Side-B (Monkey Flip) is available once per airtime (LA-Bit[0x71] == false).
                if (in.ftKind == 0x1B && in.laFlag)
                {
                    allowSideB = false;
                }
                if (allowSideB)
                {
                    if ((spec.flags & kRfSideBShorten) != 0)
                    {
                        die.add(kOptSideB, 4.0f);
                        if (absNCX <= 16.0f)
                        {
                            die.add(kOptSideBShort, 12.0f);
                        }
                    }
                    else if (in.ftKind == 0x22 && in.wallAhead && in.laInt > 0)
                    {
                        die.add(kOptSideBWallJump, 25.0f);
                        die.add(kOptSideB, 15.0f);
                    }
                    else
                    {
                        die.add(kOptSideB, 12.0f);
                    }
                }
            }

            // Down-B stall/recovery (Samus bomb, ZSS flip, Lucario Double Team, Luigi/Mario tornado, MK cape, Wario waft).
            if ((spec.flags & kRfBombStall) != 0 && in.vy < -1.0f && absNCX > 25.0f && belowLedgeY > -20.0f
                && belowLedgeY < spec.upBY)
            {
                die.add(kOptDownB, 14.0f);
            }
            else if ((spec.flags & kRfFlipDownB) != 0 && inRect(absNCX, 15.0f, 75.0f, belowLedgeY, -20.0f, 35.0f))
            {
                die.add(kOptDownB, 16.0f);
            }
            else if ((spec.flags & kRfMashDownB) != 0 && !in.laFlag && absNCX > 15.0f && belowLedgeY > -10.0f
                     && belowLedgeY < spec.upBY + 15.0f)
            {
                die.add(kOptDownB, 14.0f);
            }
            else if ((spec.flags & kRfAuraCancel) != 0 && !in.laFlag && absNCX <= 90.0f && belowLedgeY < 20.0f)
            {
                die.add(in.laFloat >= 65.0f ? kOptDownBCancel : kOptDownB, 14.0f);
            }
            else if ((spec.flags & kRfWaft) != 0 && in.laInt >= 2 && belowLedgeY > 15.0f && absNCX < 45.0f)
            {
                die.add(kOptDownB, 16.0f);
            }

            // Meta Knight Neutral-B Mach Tornado recovery.
            if (in.ftKind == 0x16 && inRect(absNCX, 10.0f, 80.0f, belowLedgeY, -10.0f, 45.0f))
            {
                die.add(kOptNeutralB, 10.0f);
            }

            // Ivysaur Dair stall when high and far.
            if ((spec.flags & kRfDairStall) != 0 && in.vy < -0.5f && absNCX > 45.0f && belowLedgeY < -10.0f)
            {
                die.add(kOptDairStall, 8.0f);
            }

            // Up-B recovery window (360-aim radius vs rectangular Up-B reach).
            bool inUpB = false;
            if (spec.upBY > 0)
            {
                if ((spec.flags & kRf360Aim) != 0)
                {
                    inUpB = dist2D <= static_cast<float>(spec.upBX) + 8.0f
                            && (belowLedgeY >= -15.0f || absNCX > 25.0f);
                }
                else
                {
                    inUpB = inRect(absNCX, 0.0f, static_cast<float>(spec.upBX) + 8.0f, belowLedgeY, -15.0f,
                                   static_cast<float>(spec.upBY) + 15.0f);
                }
            }
            // Snake Cypher (LA-Bit[0x3D] == true once used): prefer remaining jump before repeating Up-B.
            if (in.ftKind == 0x2E && in.laFlag && in.jumpsLeft)
            {
                inUpB = false;
            }
            // Ivysaur Up-B (Vine Whip) cannot tether from above the ledge or far out.
            if (in.ftKind == 0x1F && (belowLedgeY < 2.0f || absNCX > 32.0f))
            {
                inUpB = false;
            }
            // PK Thunder (Ness / Lucas): skip when already above ledge and close enough to drift onto stage.
            if ((spec.flags & kRfPkThunder) != 0 && absNCX < 22.0f && belowLedgeY < -4.0f)
            {
                inUpB = false;
            }
            // Multi-jumpers and PK Thunder fighters use remaining midair jumps before Up-B;
            // other fighters only commit to Up-B with a jump left when within vertical Up-B reach.
            bool allowUpBWithJump = spec.family != kFamMultiJump && spec.family != kFamPkThunder
                                    && belowLedgeY > static_cast<float>(spec.upBY) * 0.45f
                                    && belowLedgeY <= static_cast<float>(spec.upBY);
            if (inUpB && (!in.jumpsLeft || allowUpBWithJump))
            {
                if ((spec.flags & kRfAuraCancel) != 0 && in.laFloat >= 65.0f)
                {
                    die.add(kOptUpBCancel, 25.0f);
                }
                die.add(kOptUpB, 40.0f);
                if (spec.family == kFamSpacie)
                {
                    die.add(kOptUpBHigh, 15.0f);
                }
            }
        }

        // Emergency blastzone fallback (PPlusAI: below <= 20 || nearCliffY >= UpBY + 40).
        if (emergency)
        {
            die.clear();
            if (in.jumpsLeft)
            {
                if ((spec.flags & kRfFloat) != 0 && in.ftKind == 0x26 && st.usedFloat == 0 && !in.laFlag)
                {
                    die.add(kOptFloat, 100.0f);
                }
                else
                {
                    die.add(kOptJump, 100.0f);
                }
            }
            else if (in.ftKind == 0x16) // Meta Knight emergency glide/Up-B
            {
                die.add(kOptGlide, 100.0f);
            }
            else if (in.ftKind == 0x04 || in.ftKind == 0x25) // Yoshi / Jigglypuff have no vertical Up-B
            {
                die.add(kOptSideB, 100.0f);
            }
            else
            {
                die.add(kOptUpB, 100.0f);
            }
        }

        int pick = die.roll(geomRand01(rng), kOptNone);
        if (pick != kOptNone)
        {
            st.option = static_cast<u8>(pick);
        }

        // Execute chosen recovery option on this frame.
        switch (pick)
        {
        case kOptJump:
            out.buttons = freshButton(kRecBtnJump, in.lastButtons);
            out.stickX = in.ceilingAbove ? -stageCenterDir : towardLedge;
            break;
        case kOptFloat:
            out.buttons = freshButton(kRecBtnJump, in.lastButtons);
            out.stickX = 0.0f;
            out.stickY = -1.0f;
            break;
        case kOptUpB:
        case kOptUpBHigh:
        case kOptUpBCancel:
            out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            out.stickX = 0.0f;
            out.stickY = 1.0f;
            if (in.ftKind == 0x0A || in.ftKind == 0x1A)
            {
                st.timer = spec.pktTime;
            }
            break;
        case kOptGlide:
            // Fighter-specific glide entry (fudgepop01 PPlusAI 103_recoveryHub.as):
            // Meta Knight (0x16) Up-B, Pit (0x17) Side-B toward ledge, Charizard (0x1D) Down-B.
            out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            if (in.ftKind == 0x17)
            {
                out.stickX = towardLedge;
                out.stickY = 0.0f;
            }
            else if (in.ftKind == 0x1D)
            {
                out.stickX = 0.0f;
                out.stickY = -1.0f;
            }
            else
            {
                out.stickX = 0.0f;
                out.stickY = 1.0f;
            }
            break;
        case kOptSideB:
        case kOptSideBShort:
        case kOptSideBWallJump:
            out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            out.stickX = towardLedge;
            out.stickY = 0.0f;
            break;
        case kOptDownB:
        case kOptDownBCancel:
            out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            out.stickX = 0.0f;
            out.stickY = -1.0f;
            break;
        case kOptNeutralB:
            out.buttons = freshButton(kRecBtnSpecial, in.lastButtons);
            out.stickX = 0.0f;
            out.stickY = 0.0f;
            break;
        case kOptTether:
            // Air tether (Z = Guard + Attack in Brawl/P+ aiInput).
            out.buttons = freshButton(kRecBtnGuard | kRecBtnAttack, in.lastButtons);
            out.stickX = towardLedge;
            out.stickY = 0.0f;
            break;
        case kOptDairStall:
            out.buttons = freshButton(kRecBtnAttack, in.lastButtons);
            out.stickX = 0.0f;
            out.stickY = -1.0f;
            break;
        default:
            break;
        }

        st.prevStatus = static_cast<s16>(in.status);
    }
}
}
