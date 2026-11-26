// Host unit tests for Project+ 3.2.1 Tech Practice drivers:
//  - Powershield Projectile (Neutral B) + Physical (Jab) CPU driver
//  - Ledgedash Active Cliff-Hang Box Snap, Auto-Reset, Side & Invuln Policy
//  - L-Cancel & Float/Hover/DJC Target Driver (Stationary/Shielding/Moving/Off)
//  - Wavedash / Waveland GX Target Zone (55% - 80% real P+ wavedash distance)
//  - Solo Tech Practice CPU Standby State Machine
#include <math.h>
#include <string.h>

#include "extra/drill_practice_core.h"
#include "host/test.h"

using namespace tm::extra;

namespace {
    StageGeometry mkBattlefieldGeom()
    {
        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.floor.valid = true;
        g.floor.leftX = -68.4f;
        g.floor.rightX = 68.4f;
        g.floor.centerY = 0.0f;
        g.hasLedges = true;
        g.leftLedge.x = -68.4f;
        g.leftLedge.y = 0.0f;
        g.leftLedge.dir = 1.0f;
        g.rightLedge.x = 68.4f;
        g.rightLedge.y = 0.0f;
        g.rightLedge.dir = -1.0f;
        g.blast.valid = true;
        g.blast.left = -224.0f;
        g.blast.right = 224.0f;
        g.blast.top = 200.0f;
        g.blast.bottom = -108.0f;
        return g;
    }
}

TEST(tech_practice, config_defaults_and_sanitization)
{
    PracticeConfig cfg;
    practiceConfigDefaults(cfg);
    CHECK_EQ(static_cast<u8>(kPsAtkProjectile), cfg.psAttackMode);
    CHECK_EQ(static_cast<u8>(kPsHeightLow), cfg.psHeight);
    CHECK_EQ(static_cast<u8>(kPsCadenceMedium), cfg.psCadence);
    CHECK_EQ(static_cast<u8>(kLedgeSideSame), cfg.ledgeResetSide);
    CHECK_EQ(static_cast<u8>(1), cfg.ledgeAutoReset);
    CHECK_EQ(static_cast<u8>(kLedgeInvulnPPlus5), cfg.ledgeInvulnLimit);
    CHECK_EQ(static_cast<u8>(kTargetStationary), cfg.lcancelTarget);
    CHECK_EQ(static_cast<u8>(kTargetStationary), cfg.floatDjcTarget);
    CHECK_EQ(static_cast<u8>(0), cfg.wdTargetZone);

    // Out-of-range values sanitize cleanly back to defaults.
    cfg.psAttackMode = 99;
    cfg.psHeight = 99;
    cfg.psCadence = 99;
    cfg.ledgeResetSide = 99;
    cfg.ledgeAutoReset = 7;
    cfg.ledgeInvulnLimit = 99;
    cfg.lcancelTarget = 99;
    cfg.floatDjcTarget = 99;
    cfg.wdTargetZone = 42;
    sanitizePracticeConfig(cfg);

    CHECK_EQ(static_cast<u8>(kPsAtkProjectile), cfg.psAttackMode);
    CHECK_EQ(static_cast<u8>(kPsHeightLow), cfg.psHeight);
    CHECK_EQ(static_cast<u8>(kPsCadenceMedium), cfg.psCadence);
    CHECK_EQ(static_cast<u8>(kLedgeSideSame), cfg.ledgeResetSide);
    CHECK_EQ(static_cast<u8>(1), cfg.ledgeAutoReset);
    CHECK_EQ(static_cast<u8>(kLedgeInvulnPPlus5), cfg.ledgeInvulnLimit);
    CHECK_EQ(static_cast<u8>(kTargetStationary), cfg.lcancelTarget);
    CHECK_EQ(static_cast<u8>(kTargetStationary), cfg.floatDjcTarget);
    CHECK_EQ(static_cast<u8>(1), cfg.wdTargetZone);
}

TEST(tech_practice, powershield_projectile_profiles_and_fallback)
{
    // Verified P+ 3.2.1 Neutral B projectile startup frames for spacie trio & cast.
    const ProjectileProfile* fox = findProjectileProfile(0x06);
    const ProjectileProfile* falco = findProjectileProfile(0x13);
    const ProjectileProfile* wolf = findProjectileProfile(0x2C);
    CHECK(fox != NULL && falco != NULL && wolf != NULL);
    CHECK_EQ(static_cast<u8>(8), fox->shStartup);
    CHECK_EQ(static_cast<u8>(12), falco->shStartup);
    CHECK_EQ(static_cast<u8>(14), wolf->shStartup);

    // Low laser fires later in short-hop than Mid, which fires later than High.
    CHECK(psShortHopPressDelay(*falco, kPsHeightHigh) < psShortHopPressDelay(*falco, kPsHeightMid));
    CHECK(psShortHopPressDelay(*falco, kPsHeightMid) < psShortHopPressDelay(*falco, kPsHeightLow));
    CHECK_EQ(static_cast<u8>(0), psShortHopPressDelay(*falco, kPsHeightGround));

    // Additional verified P+ projectile fighters exist.
    CHECK(hasNeutralBProjectile(0x00)); // Mario
    CHECK(hasNeutralBProjectile(0x08)); // Luigi
    CHECK(hasNeutralBProjectile(0x03)); // Samus
    CHECK(hasNeutralBProjectile(0x02)); // Link
    CHECK(hasNeutralBProjectile(0x29)); // Toon Link
    CHECK(hasNeutralBProjectile(0x17)); // Pit
    CHECK(hasNeutralBProjectile(0x07)); // Pikachu
    CHECK(hasNeutralBProjectile(0x21)); // Lucario
    CHECK(hasNeutralBProjectile(0x26)); // Mewtwo
    CHECK(hasNeutralBProjectile(0x23)); // R.O.B.
    CHECK(hasNeutralBProjectile(0x0E)); // Sheik

    // Melee-only fighters, Ivysaur (Synthesis/Solarbeam), and Snake (timed grenade)
    // cleanly fall back to Physical (Jab).
    CHECK(!hasNeutralBProjectile(0x11)); // Marth
    CHECK(!hasNeutralBProjectile(0x09)); // Captain Falcon
    CHECK(!hasNeutralBProjectile(0x14)); // Ganondorf
    CHECK(!hasNeutralBProjectile(0x1F)); // Ivysaur
    CHECK(!hasNeutralBProjectile(0x2E)); // Snake

    CHECK_EQ(static_cast<u8>(kPsAtkProjectile), resolvePsAttackMode(kPsAtkProjectile, 0x13));
    CHECK_EQ(static_cast<u8>(kPsAtkPhysical), resolvePsAttackMode(kPsAtkPhysical, 0x13));
    CHECK_EQ(static_cast<u8>(kPsAtkPhysical), resolvePsAttackMode(kPsAtkProjectile, 0x11));
    CHECK_EQ(static_cast<u8>(kPsAtkPhysical), resolvePsAttackMode(kPsAtkProjectile, 0x1F));
    CHECK_EQ(static_cast<u8>(kPsAtkPhysical), resolvePsAttackMode(kPsAtkProjectile, 0x2E));

    // Charge-tap fighters (Samus/Lucario/Mewtwo/Sheik), Popo, and long-hold bows
    // (Link/Toon Link) force Ground fire so they never land mid-charge or mid-hold.
    CHECK(psMustGroundFire(*findProjectileProfile(0x03))); // Samus
    CHECK(psMustGroundFire(*findProjectileProfile(0x21))); // Lucario
    CHECK(psMustGroundFire(*findProjectileProfile(0x26))); // Mewtwo
    CHECK(psMustGroundFire(*findProjectileProfile(0x0E))); // Sheik
    CHECK(psMustGroundFire(*findProjectileProfile(0x0F))); // Popo
    CHECK(psMustGroundFire(*findProjectileProfile(0x02))); // Link (10f hold)
    CHECK(psMustGroundFire(*findProjectileProfile(0x29))); // Toon Link (10f hold)

    // Aerial hold-release fighters (Pit, Diddy, ZSS) always release BEFORE landing.
    const int aerialHoldKinds[3] = {0x17, 0x1B, 0x18};
    for (int i = 0; i < 3; i++)
    {
        const ProjectileProfile* p = findProjectileProfile(aerialHoldKinds[i]);
        CHECK(p != NULL && !psMustGroundFire(*p));
        u8 delay = psShortHopPressDelay(*p, kPsHeightLow);
        CHECK(static_cast<int>(delay) + static_cast<int>(p->holdBFrames) + static_cast<int>(p->shStartup)
              <= static_cast<int>(p->shAirFrames));
    }

    // Ground-only resolves to Ground height regardless of selected height.
    u32 rng = 0x12345678u;
    CHECK_EQ(static_cast<u8>(kPsHeightGround), psResolveHeight(kPsHeightLow, true, rng));
    CHECK_EQ(static_cast<u8>(kPsHeightGround), psResolveHeight(kPsHeightRandom, true, rng));

    // Cadence ordering: Fast < Medium < Slow, Random stays within [18, 68].
    CHECK(psCadenceDelay(kPsCadenceFast, rng) < psCadenceDelay(kPsCadenceMedium, rng));
    CHECK(psCadenceDelay(kPsCadenceMedium, rng) < psCadenceDelay(kPsCadenceSlow, rng));
    for (int i = 0; i < 32; i++)
    {
        u16 d = psCadenceDelay(kPsCadenceRandom, rng);
        CHECK(d >= 18u && d <= 68u);
    }
}

TEST(tech_practice, powershield_cpu_driver_shl_charge_hold_and_physical)
{
    PracticeConfig cfg;
    practiceConfigDefaults(cfg);
    cfg.psAttackMode = kPsAtkProjectile;
    cfg.psHeight = kPsHeightLow;
    cfg.psCadence = kPsCadenceMedium;

    PsCpuState st;
    memset(&st, 0, sizeof(st));
    psCpuStateReset(st);
    st.cooldown = 0u;

    PsCpuInput in;
    memset(&in, 0, sizeof(in));
    in.cpuFtKind = 0x13; // Falco
    in.cpuCanAct = true;
    in.cpuX = 20.0f;
    in.cpuLr = -1.0f;
    in.hmnX = -16.0f; // spacing = 36 (ideal)
    in.floorLeft = -68.4f;
    in.floorRight = 68.4f;

    // Frame 1: Falco taps Jump for SHL and requests CPU intangibility.
    PsCpuOutput out;
    stepPsPracticeCpu(in, cfg, st, out);
    CHECK(out.active);
    CHECK(out.wantIntangible);
    CHECK_EQ(static_cast<u32>(kPrBtnJump), out.buttons);
    CHECK_EQ(static_cast<u8>(kPsCpuJumpSquat), st.phase);

    // Jump squat frames (grounded, jump released for short-hop).
    for (int f = 0; f < 4; f++)
    {
        stepPsPracticeCpu(in, cfg, st, out);
        CHECK_EQ(0u, out.buttons);
    }

    // Airborne: counts up to airWaitTarget then fires Neutral B.
    in.cpuAir = true;
    bool fired = false;
    for (int f = 0; f < 20; f++)
    {
        stepPsPracticeCpu(in, cfg, st, out);
        if ((out.buttons & kPrBtnSpecial) != 0u)
        {
            fired = true;
            break;
        }
    }
    CHECK(fired);
    CHECK_EQ(static_cast<u8>(kPsCpuIdle), st.phase);
    CHECK_EQ(static_cast<u16>(38), st.cooldown);

    // Charge-tap fighter (Samus): even when Low height is requested, forces Ground
    // fire so she never lands mid-charge and cancels the shot.
    psCpuStateReset(st);
    st.cooldown = 0u;
    cfg.psHeight = kPsHeightLow;
    in.cpuFtKind = 0x03; // Samus
    in.cpuAir = false;
    in.cpuX = 22.0f;
    in.hmnX = -16.0f;
    stepPsPracticeCpu(in, cfg, st, out);
    CHECK_EQ(static_cast<u8>(kPsHeightGround), st.resolvedHeight);
    CHECK_EQ(static_cast<u32>(kPrBtnSpecial), out.buttons);
    CHECK_EQ(static_cast<u8>(kPsCpuChargeWait), st.phase);
    bool released = false;
    for (int f = 0; f < 20; f++)
    {
        stepPsPracticeCpu(in, cfg, st, out);
        if ((out.buttons & kPrBtnSpecial) != 0u)
        {
            released = true;
            break;
        }
    }
    CHECK(released);
    CHECK_EQ(static_cast<u8>(kPsCpuIdle), st.phase);

    // Physical mode: walks in if far, jabs once in range, no intangibility.
    psCpuStateReset(st);
    st.cooldown = 0u;
    cfg.psAttackMode = kPsAtkPhysical;
    in.cpuFtKind = 0x11; // Marth
    in.cpuX = 20.0f;
    in.hmnX = -10.0f;
    stepPsPracticeCpu(in, cfg, st, out);
    CHECK(out.active);
    CHECK(!out.wantIntangible);
    CHECK(out.stickX < 0.0f);
    CHECK_EQ(0u, out.buttons);

    in.cpuX = -2.0f; // within jab range (absDx = 8 <= 11)
    stepPsPracticeCpu(in, cfg, st, out);
    CHECK_EQ(static_cast<u32>(kPrBtnAttack), out.buttons);
}

TEST(tech_practice, ledgedash_cliff_box_snap_camera_and_driver)
{
    // Verified real P+ 3.2.1 scaled cliff-hang boxes (all have negative offsetX = -4.0 * scale;
    // Ledge.asm:16-52 cancels negative offsetX on front reach -> [posX + lr*offsetX, posX + lr*width]).
    const CliffHangBox boxes[6] = {
        {-4.18f,  8.88f, 11.49f,  9.40f}, // Fox
        {-4.90f, 10.41f, 13.48f, 11.03f}, // Wolf
        {-3.90f, 11.21f,  9.75f, 10.73f}, // Marth
        {-3.94f,  5.91f,  8.87f,  9.85f}, // Pikachu
        {-4.08f, 13.26f,  8.67f, 11.22f}, // Snake
        {-3.76f,  5.64f, 12.37f, 12.81f}  // Bowser
    };
    for (int i = 0; i < 6; i++)
    {
        float sx = 0.0f, sy = 0.0f;
        // Left ledge at (-68.4, 0.0), facing +1 (right).
        computeCliffSnapSpawn(-68.4f, 0.0f, 1.0f, boxes[i], sx, sy);
        CHECK(sx <= -68.4f - 4.5f); // strictly outside left stage wall lip (no wall overlap)
        CHECK(sx + boxes[i].offsetX <= -68.4f && sx + boxes[i].width >= -68.4f);
        CHECK(sy + boxes[i].offsetY <= 0.0f && sy + boxes[i].offsetY + boxes[i].height >= 0.0f);

        // Right ledge at (+68.4, 0.0), facing -1 (left).
        computeCliffSnapSpawn(68.4f, 0.0f, -1.0f, boxes[i], sx, sy);
        CHECK(sx >= 68.4f + 4.5f); // strictly outside right stage wall lip
        CHECK(sx - boxes[i].offsetX >= 68.4f && sx - boxes[i].width <= 68.4f);
        CHECK(sy + boxes[i].offsetY <= 0.0f && sy + boxes[i].offsetY + boxes[i].height >= 0.0f);
    }

    // Invalid cliff box gracefully falls back to safe default.
    CliffHangBox bad = {-99.0f, 99.0f, 0.0f, -5.0f};
    float fx = 0.0f, fy = 0.0f;
    computeCliffSnapSpawn(-68.4f, 0.0f, 1.0f, bad, fx, fy);
    CHECK(fx <= -68.4f - 4.5f && fx > -85.0f);
    CHECK(fy < -5.0f && fy > -25.0f);

    // Side selection policy.
    u32 rng = 0xA5A5A5A5u;
    CHECK_EQ(static_cast<u8>(0), pickNextLedgeSide(0, kLedgeSideSame, rng));
    CHECK_EQ(static_cast<u8>(1), pickNextLedgeSide(1, kLedgeSideSame, rng));
    CHECK_EQ(static_cast<u8>(1), pickNextLedgeSide(0, kLedgeSideOpposite, rng));
    CHECK_EQ(static_cast<u8>(0), pickNextLedgeSide(1, kLedgeSideOpposite, rng));

    // Steady ledge camera rect frames ledge and adjacent stage lip cleanly.
    float cl = 0.0f, cr = 0.0f, ct = 0.0f, cb = 0.0f;
    computeLedgeCameraRect(-68.4f, 0.0f, 1.0f, cl, cr, ct, cb);
    CHECK(cl < -68.4f && cr > -40.0f && cb < 0.0f && ct > 20.0f);

    // Ledgedash driver state machine: initial snap, RA-Basic[0] hold, D-Pad switch,
    // Opposite Auto-Reset, Unlimited LA-Basic[79] clear, Dead deferral, and retry cap.
    PracticeConfig cfg;
    practiceConfigDefaults(cfg);
    cfg.ledgeAutoReset = 1;
    cfg.ledgeResetSide = kLedgeSideOpposite;
    cfg.ledgeInvulnLimit = kLedgeInvulnUnlimited;

    LedgeDriverState st;
    memset(&st, 0, sizeof(st));
    ledgeDriverReset(st);

    LedgeDriverInput in;
    memset(&in, 0, sizeof(in));
    in.hmnStatus = 0x00;
    in.ledgeSeq = 0;

    // First frame triggers initial snap to Left ledge (0).
    LedgeDriverAction act;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(act.doSnap);
    CHECK_EQ(static_cast<u8>(0), act.snapSide);
    CHECK(act.clearGrabCounter);
    CHECK(act.applyCamera);

    // Fighter catches ledge (Cliff_Wait = 0x75): hold cliff timer, don't clear LA-Basic[79] while hanging.
    in.hmnStatus = 0x75;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(!act.doSnap);
    CHECK(act.holdCliffTimer);
    CHECK(!act.clearGrabCounter);

    // Instant D-Pad Right switches immediately to Right ledge (1), then catches Cliff_Wait.
    in.dpadRightEdge = true;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(act.doSnap);
    CHECK_EQ(static_cast<u8>(1), act.snapSide);
    CHECK_EQ(static_cast<u8>(1), st.activeSide);
    in.dpadRightEdge = false;
    in.hmnStatus = 0x75;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(!act.doSnap);

    // Rep finishes (seq increments): Auto-Reset counts down 28f then snaps to Opposite side (0).
    in.hmnStatus = 0x00;
    in.ledgeSeq = 1;
    bool autoSnapped = false;
    u8 autoSide = 99;
    for (int f = 0; f < 35; f++)
    {
        stepLedgeDriver(in, cfg, st, act);
        if (act.doSnap)
        {
            autoSnapped = true;
            autoSide = act.snapSide;
            break;
        }
    }
    CHECK(autoSnapped);
    CHECK_EQ(static_cast<u8>(0), autoSide);

    // While Dead (0xBD), snap is deferred until Rebirth (0xBE).
    in.hmnStatus = 0xBD;
    in.dpadLeftEdge = true;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(!act.doSnap);
    in.dpadLeftEdge = false;
    in.hmnStatus = 0xBE;
    stepLedgeDriver(in, cfg, st, act);
    CHECK(act.doSnap);

    // Watchdog retries at most once if fighter never catches ledge (no infinite loop).
    in.hmnStatus = 0x00;
    int retries = 0;
    for (int f = 0; f < 30; f++)
    {
        stepLedgeDriver(in, cfg, st, act);
        if (act.doSnap)
        {
            retries++;
        }
    }
    CHECK_EQ(1, retries);

    // With Auto-Reset Off, completing a rep does NOT auto-snap.
    cfg.ledgeAutoReset = 0;
    in.hmnStatus = 0x75;
    stepLedgeDriver(in, cfg, st, act);
    in.hmnStatus = 0x00;
    in.ledgeSeq = 2;
    for (int f = 0; f < 40; f++)
    {
        stepLedgeDriver(in, cfg, st, act);
        CHECK(!act.doSnap);
    }
}

TEST(tech_practice, lcancel_and_float_target_driver_modes)
{
    StageGeometry geom = mkBattlefieldGeom();
    PracticeConfig cfg;
    practiceConfigDefaults(cfg);
    CHECK_EQ(static_cast<u8>(kTargetStationary), activeTargetMode(kPracticeDrillLCancel, cfg));
    CHECK_EQ(static_cast<u8>(kTargetStationary), activeTargetMode(kPracticeDrillHoverFloat, cfg));
    CHECK_EQ(static_cast<u8>(kTargetOff), activeTargetMode(kPracticeDrillWavedash, cfg));

    // Stationary CPU uses NoReaction (full P+ attacker hitlag, 0 knockback, 0 damage);
    // Shielding / Moving / Off do NOT enable NoReaction.
    CHECK(targetWantsNoReaction(kTargetStationary));
    CHECK(!targetWantsNoReaction(kTargetShielding));
    CHECK(!targetWantsNoReaction(kTargetMoving));
    CHECK(!targetWantsNoReaction(kTargetOff));

    // Target anchor places CPU in front of P1 and flips inward near stage edge.
    float ax = 0.0f, ay = 0.0f, alr = 0.0f;
    computeTargetAnchor(0.0f, 1.0f, 12.0f, geom, ax, ay, alr);
    CHECK(fabsf(ax - 12.0f) < 0.01f);
    CHECK(alr < 0.0f);

    computeTargetAnchor(62.0f, 1.0f, 12.0f, geom, ax, ay, alr);
    CHECK(ax <= geom.floor.rightX - 10.0f);
    CHECK(ax < 62.0f);

    TargetCpuState st;
    targetCpuReset(st);
    CHECK(!st.anchorValid);
    TargetCpuInput in;
    memset(&in, 0, sizeof(in));
    in.cpuCanAct = true;
    in.cpuX = 12.0f;
    in.cpuLr = 1.0f;
    in.hmnX = 0.0f;
    in.floorLeft = geom.floor.leftX;
    in.floorRight = geom.floor.rightX;

    // Stationary turns toward P1 without attacking.
    in.targetMode = kTargetStationary;
    TargetCpuOutput out;
    stepTargetCpu(in, st, out);
    CHECK(out.active);
    CHECK_EQ(0u, out.buttons);
    CHECK(out.stickX < 0.0f);

    // Shielding holds guard.
    in.targetMode = kTargetShielding;
    stepTargetCpu(in, st, out);
    CHECK(out.active);
    CHECK_EQ(static_cast<u32>(kPrBtnGuard), out.buttons);

    // Moving patrols smoothly and reverses at floor bounds.
    in.targetMode = kTargetMoving;
    in.cpuX = geom.floor.rightX - 14.0f;
    stepTargetCpu(in, st, out);
    CHECK(out.active);
    CHECK(out.stickX < 0.0f);

    in.cpuX = geom.floor.leftX + 14.0f;
    stepTargetCpu(in, st, out);
    CHECK(out.stickX > 0.0f);

    // Off disables target driver.
    in.targetMode = kTargetOff;
    stepTargetCpu(in, st, out);
    CHECK(!out.active);
}

TEST(tech_practice, wavedash_gx_target_zone_physics_and_scoring)
{
    StageGeometry geom = mkBattlefieldGeom();

    // Real P+ 3.2.1 Ground_Brake & Walk_Speed_Max ordering with AirDodge.asm 0.9x decay:
    // Luigi (0.025, 1.1) > Marth (0.055, 1.5) > Fox (0.080, 1.6) > Peach (0.100, 0.96).
    const float dLuigi = computeWavedashMaxDist(0.025f, 1.10f);
    const float dMarth = computeWavedashMaxDist(0.055f, 1.50f);
    const float dFox = computeWavedashMaxDist(0.080f, 1.60f);
    const float dPeach = computeWavedashMaxDist(0.100f, 0.96f);
    CHECK(dLuigi > dMarth);
    CHECK(dMarth > dFox);
    CHECK(dFox > dPeach);
    CHECK(dPeach > 8.0f && dLuigi < 30.0f);

    // Zone spans 55% - 80% of fighter's calibrated wavedash distance on walkable floor.
    WdZoneState z;
    wdZoneReset(z);
    CHECK(spawnWavedashZone(z, 0.0f, 0.0f, 1.0f, 0.080f, geom, 1.60f));
    CHECK_EQ(static_cast<u8>(1), z.active);
    CHECK(fabsf(z.minX - dFox * 0.55f) < 0.05f);
    CHECK(fabsf(z.maxX - dFox * 0.80f) < 0.05f);
    CHECK(z.minX >= geom.floor.leftX + 4.0f && z.maxX <= geom.floor.rightX - 4.0f);

    // Near right edge facing right: zone flips cleanly inward onto walkable floor.
    CHECK(spawnWavedashZone(z, 62.0f, 0.0f, 1.0f, 0.025f, geom, 1.10f));
    CHECK(z.dir < 0.0f);
    CHECK(z.minX >= geom.floor.leftX + 4.0f && z.maxX <= geom.floor.rightX - 4.0f);

    // Scoring keeps the attempted zone flashing for 18f before respawn triggers!
    CHECK(spawnWavedashZone(z, 0.0f, 0.0f, 1.0f, 0.080f, geom, 1.60f));
    float origMin = z.minX;
    float mid = 0.5f * (z.minX + z.maxX);
    CHECK_EQ(static_cast<u8>(kWdZoneHit), scoreWavedashZone(z, mid, true));
    CHECK_EQ(static_cast<u8>(18), z.flashFrames);
    CHECK_EQ(static_cast<u8>(1), z.respawnPending);
    for (int f = 0; f < 18; f++)
    {
        CHECK(!wdZoneTickFlash(z));
        CHECK(fabsf(z.minX - origMin) < 0.001f); // zone stays put while flashing
    }
    CHECK(wdZoneTickFlash(z)); // frame 19: ready to spawn next zone
    CHECK(!wdZoneTickFlash(z));

    CHECK_EQ(static_cast<u8>(kWdZoneHit), scoreWavedashZone(z, mid, true));
    CHECK_EQ(2u, z.hits);
    CHECK_EQ(2u, z.streak);
    CHECK_EQ(2u, z.bestStreak);

    CHECK_EQ(static_cast<u8>(kWdZoneShort), scoreWavedashZone(z, z.minX - 2.0f, true));
    CHECK_EQ(0u, z.streak);
    CHECK_EQ(2u, z.bestStreak);

    CHECK_EQ(static_cast<u8>(kWdZoneOvershoot), scoreWavedashZone(z, z.maxX + 2.0f, true));
    CHECK_EQ(4u, z.attempts);

    // Camera rect encloses both P1 and the target zone with margin.
    float cl = 0.0f, cr = 0.0f, ct = 0.0f, cb = 0.0f;
    computeWavedashCameraRect(0.0f, 0.0f, z, cl, cr, ct, cb);
    CHECK(cl < 0.0f && cr > z.maxX && cb < 0.0f && ct > 25.0f);
}

TEST(tech_practice, solo_cpu_standby_rules_and_transitions)
{
    PracticeConfig cfg;
    practiceConfigDefaults(cfg);

    // Always Solo Standby for solo tech drills.
    CHECK(shouldCpuStandby(kPracticeDrillLedgedash, cfg));
    CHECK(shouldCpuStandby(kPracticeDrillWavedash, cfg));
    CHECK(shouldCpuStandby(kPracticeDrillRar, cfg));
    CHECK(shouldCpuStandby(kPracticeDrillDacus, cfg));
    CHECK(shouldCpuStandby(kPracticeDrillBReverse, cfg));

    // Powershield & None NEVER enter Solo Standby.
    CHECK(!shouldCpuStandby(kPracticeDrillPowershield, cfg));
    CHECK(!shouldCpuStandby(kPracticeDrillNone, cfg));

    // L-Cancel & Float/Hover/DJC enter Solo Standby ONLY when Target == Off.
    CHECK(!shouldCpuStandby(kPracticeDrillLCancel, cfg));
    CHECK(!shouldCpuStandby(kPracticeDrillHoverFloat, cfg));
    cfg.lcancelTarget = kTargetOff;
    cfg.floatDjcTarget = kTargetOff;
    CHECK(shouldCpuStandby(kPracticeDrillLCancel, cfg));
    CHECK(shouldCpuStandby(kPracticeDrillHoverFloat, cfg));

    // State transitions: Noop -> Enter -> Hold -> Exit -> Noop.
    bool isStandby = false;
    CHECK_EQ(static_cast<u8>(kStandbyNoop), stepStandbyState(false, isStandby));
    CHECK(!isStandby);
    CHECK_EQ(static_cast<u8>(kStandbyEnter), stepStandbyState(true, isStandby));
    CHECK(isStandby);
    CHECK_EQ(static_cast<u8>(kStandbyHold), stepStandbyState(true, isStandby));
    CHECK(isStandby);
    CHECK_EQ(static_cast<u8>(kStandbyExit), stepStandbyState(false, isStandby));
    CHECK(!isStandby);
    CHECK_EQ(static_cast<u8>(kStandbyNoop), stepStandbyState(false, isStandby));
}
