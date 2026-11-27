// Tech, slip-tech, getup, and ledge behavior for CPU Options (Brain).
// Factored domain section included by options.cpp so -ipa file optimizes Brain as one TU.
#ifndef TM_OPTIONS_CPP_INTERNAL
#error "Include from main/cpu/options.cpp only"
#endif



namespace tm {
namespace cpu {
    void Brain::stepKb(float& kx, float& ky, float& kl, float decay, float& gy, float accel, float maxFall)
    {
        float k = kl > decay ? (kl - decay) / kl : 0.0f;
        kl *= k;
        kx *= k;
        ky *= k;
        gy = gy + accel < -maxFall ? -maxFall : gy + accel;
    }

    void Brain::chooseTech(const Owner& who)
    {
        const CpuOptions& c = opts();
        static const u8 kFromRandom[4] = {kTechInPlace, kTechAway, kTechTowards, kTechNone};
        tech = c.tech == kTechRandom ? kFromRandom[cpuWeighted(c.techChance)] : c.tech;
        preTechRoll = cpuRnd();
        TM_LOG("cpu: P%d tech choice %d, frame %u\n", who.playerNo + 1, tech, frameOf(slot));
        techChosen = true;
        techPressed = false;
    }

    // Frames until the fighter lands, if within `lead`; -1 otherwise.
    // `fallbackGround`: when false, only real ground rays count (no
    // phantom `groundY` plane offstage). docs/internals.md, "CPU tech timing".
    int Brain::framesToLand(int lead, float lift, bool fallbackGround)
    {
        float x = v.x, y = v.y, vx = v.vx, vy = v.vy;
        // At the fall speed cap the speed no longer grows.
        const float dv = v.vy - prevVy;
        const bool capped = havePrev && prevAir && v.vy < 0.0f && dv < 0.005f && dv > -0.005f;
        const float g = capped ? 0.0f : gravity;
        // In a damage status, the flight followed as the DI Display does.
        float kx = 0.0f, ky = 0.0f, accel = 0.0f, maxFall = 0.0f, gy = 0.0f;
        const bool flying = v.status >= kStatusDamage && v.status <= kStatusDamageFall
                            && fallSpeeds(f, accel, maxFall, gy) && knockback(f, kx, ky);
        float kl = length(kx, ky);
        const float decay = param(kParamDecay, 0.051f);
        for (int i = 0; i <= lead; i++)
        {
            if (flying)
            {
                stepKb(kx, ky, kl, decay, gy, accel, maxFall);
                vx = kx;
                vy = ky + gy;
            }
            else
            {
                vy -= g;
            }
            float nx = x + vx, ny = y + vy;
            if (vy < 0.0f)
            {
                float ground = 0.0f;
                // Cast straight down from the collision shape's bottom (`lift` up).
                const float b = y + lift;
                float rayDx = 0.0f, rayDy = 0.0f;
                groundRayDir(static_cast<float>(kRayDown), rayDx, rayDy);
                bool found = groundBetween(f, nx, b, rayDx, rayDy, ground) && ground <= b + 0.5f;
                if (!found && fallbackGround && groundKnown)
                {
                    ground = groundY;
                    found = true;
                }
                // Not when already under it (Damage_Air sinks below).
                if (found && ny + lift <= ground && y + lift > ground)
                {
                    return i;
                }
            }
            x = nx;
            y = ny;
        }
        return -1;
    }

    // Frames until the fighter meets a solid wall or ceiling along its
    // damage flight, if within `lead`; -1 otherwise.
    int Brain::framesToWall(int lead, float lift)
    {
        float kx = 0.0f, ky = 0.0f, accel = 0.0f, maxFall = 0.0f, gy = 0.0f;
        if (!(v.status >= kStatusDamageFly && v.status <= kStatusDamageFall)
            || !fallSpeeds(f, accel, maxFall, gy) || !knockback(f, kx, ky))
        {
            return -1;
        }
        float kl = length(kx, ky);
        if (kl < 0.5f)
        {
            return -1;
        }
        const float decay = param(kParamDecay, 0.051f);
        float x = v.x, y = v.y;
        for (int i = 0; i <= lead; i++)
        {
            stepKb(kx, ky, kl, decay, gy, accel, maxFall);
            float vx = kx;
            float vy = ky + gy;
            // Cast from body centre with a small ECB half-width margin.
            float padX = vx > 0.0f ? 4.0f : (vx < 0.0f ? -4.0f : 0.0f);
            if (wallOrCeilBetween(x, y + lift + 4.0f, vx + padX, vy))
            {
                return i;
            }
            x += vx;
            y += vy;
        }
        return -1;
    }

    // Collision shape's bottom over the position; 0 outside 0 to 10.
    float Brain::ecbLift() const
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        float lift = 0.0f;
        if (m != NULL && game::isRam(m->m_groundModule))
        {
            lift = m->m_groundModule->getDownPos(0).m_y - v.y;
        }
        return lift > 0.0f && lift < 10.0f ? lift : 0.0f;
    }

    void Brain::techInput(Frame& out, const Owner& who, bool allowStick)
    {
        // The air counter Wait means no tech: known from the hit on, as
        // the press can come before the counter is chosen.
        const Action* air = action(kListAir, opts().counterAir);
        if (noTech || (air != NULL && (air->flags & kNoTech) != 0))
        {
            return;
        }
        if (!techChosen)
        {
            chooseTech(who);
        }
        const bool slipping = slipTechStatus(v.status);
        const float lift = slipping ? 0.0f : ecbLift();
        int nLand = -1;
        bool checkedLand = false;
        bool appliedRoll = false;
        if (tech != kTechNone)
        {
            s32 now = game::trackerFrame();
            if (techPressed && now - lastPress >= kTechGap)
            {
                techPressed = false;
            }
            if (!techPressed)
            {
                // Do not double-press if a valid tech window is already open:
                // a second guard press within 40 frames overwrites m_countPrev
                // and locks out the tech.
                if (techWindowActive(f))
                {
                    techPressed = true;
                }
                else
                {
                    // In the window and kTechGap after the press before:
                    // docs/internals.md, "CPU tech timing". While hitlag > 1,
                    // soControllerImpl::update (0x80764F64, 0x80765394; isStop from
                    // 0x8085B0D8) keeps OR-ing m_trigger on every hitlag frame,
                    // overwriting m_countPrev with 0 (self-lockout) if guard is
                    // pressed before the last hitlag frame (v.hitlag == 1).
                    if (v.hitlag > 1)
                    {
                        return;
                    }
                    int lead = opts().techLockout == kLockoutEarliest ? kTechLeadEarly : kTechLead;
                    int n = -1;
                    if (slipping)
                    {
                        nLand = framesToSlipTech(v.motionFrame, v.motionRate);
                        checkedLand = true;
                        if (nLand >= 0 && nLand <= lead - v.hitlag)
                        {
                            n = nLand;
                        }
                    }
                    else
                    {
                        nLand = framesToLand(lead - v.hitlag, lift);
                        checkedLand = true;
                        n = nLand >= 0 ? nLand : framesToWall(lead - v.hitlag, lift);
                    }
                    if (n >= 0 && now - lastPress >= kTechGap && now >= techFrom)
                    {
                        out.buttons |= kGuard;
                        techPressed = true;
                        TM_LOG("cpu: P%d tech %d: guard %d frames before the predicted landing, frame %u\n",
                               who.playerNo + 1, tech, n + v.hitlag, frameOf(slot));
                    }
                }
            }
            // Fighter.pac samples stickX on the landing/tech frame itself
            // (Term_Passive_Fb 0x80FAB024, Passive_Fb entry 0x80FB563C). Wait
            // until within kTechRollLead frames of impact so the CPU does not
            // drift in tumble before landing unless Pre-Tech Drift is active.
            if (techPressed && v.hitlag == 0 && (tech == kTechAway || tech == kTechTowards))
            {
                if (!checkedLand)
                {
                    nLand = slipping ? framesToSlipTech(v.motionFrame, v.motionRate)
                                     : framesToLand(kTechRollLead, lift);
                    checkedLand = true;
                }
                float rx = techRollStickX(tech, toward, nLand, techPressed, 0, allowStick,
                                          param(kParamRollStick, 0.2f), param(kParamWiggleStick, 0.75f));
                if (rx != 0.0f)
                {
                    out.x = rx;
                    appliedRoll = true;
                }
            }
        }
        if (!appliedRoll && !slipping && v.air && tumbleStatus(v.status) && v.hitlag == 0 && allowStick)
        {
            int driftMode = main::settings().techInvis.preTechDrift;
            if (driftMode != main::kPreTechDriftOff)
            {
                if (!checkedLand)
                {
                    nLand = framesToLand(kTechRollLead, lift);
                }
                float dx = preTechDriftStickX(driftMode, tech, toward, preTechRoll, nLand, 0, true,
                                              param(kParamWiggleStick, 0.75f));
                if (dx != 0.0f)
                {
                    out.x = dx;
                }
            }
        }
    }

    void Brain::getupInput(Frame& out, const Owner& who)
    {
        // Lay_Down (0x50, e.g. Snake down throw) and Slip_Wait (0x8D, e.g.
        // missed banana trip tech) share Down_Wait's getup inputs
        // (Fighter.pac 0x80FB4AA4 -> 0x80FB47AC, Slip_Wait 0x80FB755C).
        if (!isGetupWaitStatus(v.status))
        {
            return;
        }
        // Chance to stay lying down for kMissTechWait frames, re-rolled
        // each time the wait finishes.
        if (missWait > 0)
        {
            missWait--;
            return;
        }
        if (downFrames == 0 && opts().missTechWait > 0 && cpuRndBelow(100) < opts().missTechWait)
        {
            missWait = kMissTechWait;
            TM_LOG("cpu: P%d waits %d frames lying down\n", who.playerNo + 1, missWait);
            return;
        }
        if (downFrames == 0)
        {
            const CpuOptions& c = opts();
            u8 wait = 0;
            if (pickMultiRev(kRevOnGetup, who.entryId))
            {
                getup = revAction;
                wait = revDelay;
            }
            else
            {
                static const u8 kFromRandom[4] = {kGetupStand, kGetupAway, kGetupTowards, kGetupAttack};
                getup = c.getup == kGetupRandom ? kFromRandom[cpuWeighted(c.getupChance)] : c.getup;
            }
            TM_LOG("cpu: P%d get up %d\n", who.playerNo + 1, getup);
            if (wait > 0)
            {
                missWait = wait - 1;
                downFrames = 2;
                return;
            }
        }
        // Pushed every other frame, so each push is a fresh input.
        if ((downFrames++ & 1) != 0)
        {
            return;
        }
        switch (getup)
        {
        case kGetupStand:
            out.y = 1.0f;
            break;
        case kGetupAway:
            out.x = -toward;
            break;
        case kGetupTowards:
            out.x = toward;
            break;
        default:
            out.buttons = kAttack;
            break;
        }
    }

    inline bool Brain::stepLedge(Frame& out, const Owner& who, const CpuOptions& c, bool busy)
    {
        u8 base = busy ? 0 : (c.ledgeOption ? c.ledgeOption : (c.multiReversal ? c.revSlot2Action[kRevOnLedge] : 0));
        bool hold = false;
        if (!base || (!(hold = isCliffHoldStatus(v.status)) && !isCliffGetupStatus(v.status)))
        {
            ledgeStateReset(ledgeState);
            return false;
        }
        if (recovering)
        {
            recovering = false;
            main::extraPPlusRecoveryReset(slot);
        }
        if (!hold)
        {
            ledgeStateReset(ledgeState);
            return true;
        }
        state = kIdle;
        hits = 0;
        shieldKind = false;
        script = NULL;
        pending = NULL;
        playSlot = -1;
        noTech = false;
        revPicked = false;
        if (!isCliffWaitStatus(v.status))
        {
            revAction = base;
            revDelay = c.counterDelay;
        }
        else if (!ledgeState.active)
        {
            if (!pickMultiRev(kRevOnLedge, who.entryId))
            {
                if (c.ledgeOption == kLedgeDefault)
                {
                    // Slot 0 (Default ledge behavior) won the weighted roll:
                    // latch active/kLedgeDefault so we yield cleanly without re-rolling.
                    ledgeState.active = true;
                    ledgeState.chosen = kLedgeDefault;
                    revAction = kLedgeDefault;
                    return false;
                }
                revAction = c.ledgeOption;
                revDelay = c.counterDelay;
            }
        }
        else if (ledgeState.chosen == kLedgeDefault)
        {
            return false;
        }
        LedgeInput li;
        if (stepLedgeOption(revAction, revDelay, v.status, v.lr, cpuRnd(), ledgeState, li))
        {
            out.buttons = li.buttons;
            out.x = li.x;
            out.y = li.y;
            return true;
        }
        return false;
    }
}
}
