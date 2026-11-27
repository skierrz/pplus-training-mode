// Counter actions, idle behavior, shield/grab/clatter responses, SHFFL scripts,
// Factored domain section included by options.cpp so -ipa file optimizes Brain as one TU.
#ifndef TM_OPTIONS_CPP_INTERNAL
#error "Include from main/cpu/options.cpp only"
#endif

// auto L-cancel, and cue execution for CPU Options (Brain).


namespace tm {
namespace cpu {
    void Brain::stick(Frame& out, float x, float y, int ref)
    {
        switch (ref)
        {
        case kRefToward:
            x *= toward;
            break;
        case kRefForward:
            x *= v.lr;
            break;
        case kRefAway:
            x *= -toward;
            break;
        case kRefRandom:
            x *= randomSide;
            break;
        default:
            break;
        }
        out.x = x;
        out.y = y;
    }

    inline bool Brain::reached(int until) const
    {
        switch (until)
        {
        case kUntilAir:
            return v.air;
        case kUntilTakeoff:
            return takeoffNext(v) || v.air;
        case kUntilDash:
            return v.status == game::kStatusDash || v.status == game::kStatusRun;
        case kUntilFalling:
            return v.air && v.hitlag == 0 && prevHitlag == 0 && v.vy - gravity < 0.0f;
        case kUntilJumped:
            return isJumpStatus(v.status);
        case kUntilSpecial:
            return v.status >= game::kStatusSpecialN;
        case kUntilFastfall:
            return v.air && havePrev && prevVy - v.vy > kFastfallJump;
        case kUntilCanAct:
            return canAct(v) || (v.status == game::kStatusGuardOff && animationEndsNext(v));
        default:
            return false;
        }
    }

    bool Brain::justOut() const
    {
        return cpuJustOut(havePrev, prevHitstun, v.hitstun, v.hitlag, v.air, v.status);
    }

    bool Brain::takeable(const Action* a, bool airNow, bool fresh) const
    {
        if (a == NULL || a->steps == NULL)
        {
            return true;
        }
        const u32 first = a->steps[0].buttons;
        const bool hasStick = a->steps[0].x != 0 || a->steps[0].y != 0;
        if (v.hitstun == 1 && (v.air || hasStick || (first & (kAttack | kSpecial | kJump)) != 0))
        {
            return false;
        }
        return !(fresh && !airNow && !canAct(v) && (first & (kAttack | kSpecial)) != 0);
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    void Brain::startScript(const Step* steps, bool again)
    {
        script = steps;
        stepFrames = 0;
        scriptAirStarted = false;
        if (!again)
        {
            randomSide = cpuRndBelow(2) == 0 ? toward : -toward;
        }
        u32 mask = (!again || shieldKind) ? ~static_cast<u32>(kGuard) : ~0u;
        bool heldBtn = (lastButtons(slot) & steps[0].buttons & mask) != 0;
        bool oosStick = again && shieldKind && (steps[0].buttons & kGuard) != 0
                        && (steps[0].x != 0 || steps[0].y != 0);
        scriptHeld = heldBtn || oosStick;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    // Writes the recording's frame; false once it has ended.
    bool Brain::runPlay(Frame& out)
    {
        if (playSlot < 0)
        {
            return false;
        }
        if (s_slotFrame != NULL && s_slotFrame(playSlot, playFrame, out))
        {
            playFrame++;
            return true;
        }
        TM_LOG("cpu: slot %d played %u frames\n", playSlot + 1, playFrame);
        playSlot = -1;
        return false;
    }

    // Writes the script's frame; false once it has ended.
    bool Brain::runScript(Frame& out)
    {
        if (script == NULL)
        {
            return false;
        }
        if (scriptHeld)
        {
            scriptHeld = false;
            u32 release = shieldKind ? (script[0].buttons & ~static_cast<u32>(kGuard)) : script[0].buttons;
            out.buttons = lastButtons(slot) & ~release;
            return true;
        }
        if (v.air)
        {
            scriptAirStarted = true;
        }
        const bool isShfflScript = script >= shfflBuf && script < shfflBuf + kMaxShfflSteps;
        while (script->frames != 0)
        {
            const Step& s = *script;
            // Landed: a fall, fastfall, or SHFFL/FFL air phase can no
            // longer continue, so the action is over cleanly.
            const bool airWait = s.until == kUntilFalling || s.until == kUntilFastfall;
            if ((!v.air && scriptAirStarted && (isShfflScript || airWait))
                || (airWait && !v.air && v.status != game::kStatusJumpSquat))
            {
                TM_LOG("cpu: P%d landed while waiting to fall (status %x): action over, frame %u\n",
                       player + 1, v.status, frameOf(slot));
                script = NULL;
                pending = NULL;
                return false;
            }
            if (stepFrames < s.frames && !(s.until != kUntilNone && reached(s.until)))
            {
                out.buttons = s.buttons;
                stick(out, s.x / 100.0f, s.y / 100.0f, s.ref);
                stepFrames++;
                return true;
            }
            script++;
            stepFrames = 0;
        }
        script = NULL;
        return false;
    }

    // Play Random Slot: a recorded slot weighted by the slot chances; -1 if none.
    int Brain::randomSlot()
    {
        Frame unused;
        bool recorded[kRecordSlots];
        int total = 0, first = -1;
        const u8* chance = main::settings().recording.cpuChance;
        for (int i = 0; i < kRecordSlots; i++)
        {
            recorded[i] = s_slotFrame != NULL && s_slotFrame(i, 0, unused);
            if (recorded[i])
            {
                total += chance[i];
                first = first < 0 ? i : first;
            }
        }
        if (total == 0)
        {
            return first;
        }
        int r = cpuRndBelow(total);
        for (int i = 0; i < kRecordSlots; i++)
        {
            if (!recorded[i])
            {
                continue;
            }
            if (r < chance[i])
            {
                return i;
            }
            r -= chance[i];
        }
        return first;
    }

    // Shield Angle: the stick pushed in over kTiltRamp frames once the shield is up.
    void Brain::shieldTilt(Frame& out)
    {
        int angle = opts().shieldAngle;
        if (angle == kAngleNeutral)
        {
            return;
        }
        bool up = v.status == game::kStatusGuard || v.status == game::kStatusGuardDamage;
        if (!up)
        {
            tiltFrames = 0;
            return;
        }
        if (tiltFrames < kTiltRamp)
        {
            tiltFrames++;
        }
        float k = game::i2fScale(tiltFrames, 1.0f / kTiltRamp);
        switch (angle)
        {
        case kAngleUp:
            out.y = k;
            break;
        case kAngleDown:
            out.y = -k;
            break;
        case kAngleTowards:
            out.x = k * toward;
            break;
        default:
            out.x = -k * toward;
            break;
        }
    }

    // Resolves the counter action and delay from the base rows or the
    // Advanced Counter hit entry; false if that hit's counter is disabled.
    inline bool Brain::counterChoice(int list, int& index, int& delay) const
    {
        const CpuOptions& c = opts();
        index = list == kListShield ? c.counterShield : (list == kListAir ? c.counterAir : c.counterGround);
        delay = c.counterDelay;
        if (hits < 1 || hits > kAdvancedHits || actionKind(list, index) == kActionRandomAdvanced)
        {
            return true;
        }
        const AdvancedHit& h = advancedHit(hits);
        if (h.logic == kLogicDisable)
        {
            return false;
        }
        if (h.logic == kLogicCustom)
        {
            index = h.action[list];
            delay = h.delay[list];
        }
        return true;
    }

    // Random, Random Advanced, and Custom Pool picked once per counter.
    int Brain::resolve(int list, int index, int ftKind)
    {
        int kind = actionKind(list, index);
        if (kind == kActionRandom)
        {
            return randomAction(list, cpuRnd());
        }
        if (kind == kActionCustomPool)
        {
            return randomPoolAction(list, main::settings().cpuTables.poolBits[list], ftKind, cpuRnd());
        }
        if (kind == kActionRandomAdvanced)
        {
            u8 picks[kAdvancedHits];
            int n = 0;
            for (int i = 1; i <= kAdvancedHits; i++)
            {
                const AdvancedHit& h = advancedHit(i);
                int hk = actionKind(list, h.action[list]);
                if (h.logic == kLogicCustom && (hk == kActionScript || hk == kActionPlaySlot))
                {
                    picks[n++] = h.action[list];
                }
            }
            return n > 0 ? picks[cpuRndBelow(n)] : 0;
        }
        return index;
    }

    // Injects a fresh Guard press during active non-IASA AttackAir (0x33)
    // when approaching landing to trigger P+'s 7-frame L-Cancel window.
    inline void Brain::applyAutoLCancel(Frame& out)
    {
        const CpuOptions& c = opts();
        if (c.autoLCancel == 0 || v.status != game::kStatusAttackAirFirst || !v.air)
        {
            return;
        }
        const float lift = ecbLift();
        float ground = 0.0f;
        bool hasGround = groundBetween(f, v.x, v.y + lift + 0.5f, 0.0f, -24.0f, ground);
        float height = hasGround ? ((v.y + lift) - ground) : 999.0f;
        AutoLCancelInput in;
        in.enabled = true;
        in.status = v.status;
        in.air = v.air;
        in.canActNow = canAct(v) || (v.groups & kAutoLCancelUnsafeGroups) != 0;
        in.actInFrames = framesUntilActionable(f);
        in.hitlag = v.hitlag;
        in.lCancelTimer = readLCancelTimer(f);
        in.prevGuardHeld = (lastButtons(slot) & kGuard) != 0;
        in.attackPressed = (out.buttons & kAttack) != 0;
        in.vy = v.vy;
        in.framesToLand = framesToLand(kAutoLCancelLead, lift, false);
        in.hasGround = hasGround;
        in.heightAboveGround = height;
        if (shouldAutoLCancel(in))
        {
            out.buttons |= kGuard;
        }
    }

    // Starts action `a` (NULL: none) from this frame.
    void Brain::start(Frame& out, const Action* a)
    {
        revPicked = false;
        // When no aerial jump remains, wiggle out of tumble or do nothing
        // instead of retrying jump.
        if (a != NULL && (a->flags & kNeedsJump) != 0 && !jumpLeft(f))
        {
            a = tumbleStatus(v.status) ? wiggleOut() : NULL;
        }
        if (a == NULL)
        {
            state = (v.air && tumbleStatus(v.status)) ? kTechFall : kAfter;
            return;
        }
        if (a->playSlot != 0)
        {
            startPlay(out, a->playSlot - 1);
            return;
        }
        noTech = (a->flags & kNoTech) != 0;
        state = kAfter;
        playSlot = -1;
        pending = a;
        tries = 0;
        const Step* steps = a->steps;
        if ((a->flags & (kForShffl | kForFfl)) != 0)
        {
            const bool isShffl = (a->flags & kForShffl) != 0;
            const Step& aerial = isShffl ? a->steps[2] : a->steps[0];
            buildShfflScript(shfflBuf, kMaxShfflSteps, aerial, isShffl, opts().shfflTiming,
                             static_cast<u32>(cpuRndBelow(4)));
            steps = shfflBuf;
        }
        startScript(steps);
        runScript(out);
    }

    // Play Slot N: the recording from this frame on, no retry.
    void Brain::startPlay(Frame& out, int recording)
    {
        state = kAfter;
        pending = NULL;
        script = NULL;
        tries = 0;
        playSlot = recording;
        playFrame = 0;
        if (!runPlay(out) && v.air && tumbleStatus(v.status))
        {
            state = kTechFall;
        }
    }

    // Grab Escape: alternate A and B with horizontal stick swings on the
    // configured share of frames. Airborne holds Y >= 0.635 (param 3141,
    // MoveDef/Grabs.asm:42-47) while swinging X past 0.5 (clatter param 3204,
    // 0x8076568C) so stick clatter still fires on mash frames.
    inline void Brain::grabbed(Frame& out)
    {
        const CpuOptions& c = opts();
        bool airborne = c.grabRelease == kReleaseAirborne;
        if (airborne)
        {
            out.y = 1.0f;
        }
        int chance = c.grabEscape == kMashPerfect ? 100
                                                  : (c.grabEscape == kMashHigh
                                                         ? kMashChanceHigh
                                                         : (c.grabEscape == kMashMedium
                                                                ? (s_mashChance > 0 ? s_mashChance
                                                                                    : static_cast<int>(kMashChanceMedium))
                                                                : 0));
        if (chance == 0 || v.hitlag > 0 || cpuRndBelow(100) >= chance)
        {
            return;
        }
        mashFlip = !mashFlip;
        u32 last = lastButtons(slot);
        u32 button = mashFlip ? kAttack : kSpecial;
        if ((last & button) != 0)
        {
            button = button == kAttack ? kSpecial : kAttack;
        }
        out.buttons = button;
        if (airborne)
        {
            out.x = mashFlip ? 0.68f : -0.68f;
            out.y = 0.72f;
        }
        else
        {
            out.x = mashFlip ? 1.0f : -1.0f;
        }
    }

    // Clatter Escape: same alternating button + horizontal stick mash cadence
    // as Grab Escape for Bury, Ice, FuraFura, and Sleep (Damage_Song).
    inline void Brain::clatter(Frame& out)
    {
        const CpuOptions& c = opts();
        u32 btn = 0;
        float sx = 0.0f, sy = 0.0f;
        if (stepClatterMash(c.clatterEscape, v.status, v.hitlag, lastButtons(slot), cpuRndBelow(100), mashFlip, btn,
                            sx, sy, s_mashChance))
        {
            out.buttons = btn;
            out.x = sx;
            out.y = sy;
        }
    }

    void Brain::idle(Frame& out, const Owner& who)
    {
        if (held(out, who))
        {
            return;
        }
        if (!v.air && !guardStatus(v.status) && canAct(v) && opts().adaptiveMode == kAdaptiveActive
            && main::extraLoaded())
        {
            int biasIdx = main::extraAdaptiveBias(who.playerNo, kListGround, false, oppDist, false);
            if (biasIdx > 0)
            {
                const Action* a = actionFor(kListGround, biasIdx, fighterKind(who.entryId));
                if (a != NULL)
                {
                    main::extraAdaptiveBias(who.playerNo, kListGround, false, oppDist, true);
                    start(out, a);
                    return;
                }
            }
        }
        switch (opts().behavior)
        {
        case kBehaviorShield:
            if (!v.air)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            break;
        case kBehaviorCrouch:
            if (!v.air)
            {
                out.y = -1.0f;
            }
            break;
        case kBehaviorJump:
            if (runScript(out))
            {
                break;
            }
            if (!v.air && canAct(v) && v.status != game::kStatusJumpSquat)
            {
                startScript(shortHop());
                runScript(out);
            }
            break;
        case kBehaviorPerfectShield:
            // Guard as a hitbox is predicted to reach the shield: P+'s
            // powershield is a press in Guard_On's first two frames
            // (Injects/MoveDef/PowerShield.asm: IC-Basic[0] <= 1). Pressed
            // on the hit's frame or the one before, no damage.
            if (v.air)
            {
                powershield = 0;
                break;
            }
            if (powershield > 0)
            {
                // After a reflect (no Guard_Damage), move straight to the
                // shield counter action.
                if (justReflected(f))
                {
                    powershield = 0;
                    hits++;
                    actionable = 0;
                    revPicked = false;
                    script = NULL;
                    pending = NULL;
                    state = kCounter;
                    shieldKind = true;
                    TM_LOG("cpu: P%d perfect shield: reflected, counter, frame %u\n", who.playerNo + 1,
                           frameOf(slot));
                    counter(out, who);
                    break;
                }
                if (powershield++ < kPowershieldHold)
                {
                    out.buttons = kGuard;
                }
                else if (canAct(v))
                {
                    powershield = 0;
                }
                break;
            }
            {
                ShieldBubble bubble;
                int kind = canAct(v) ? incomingHit(f, who.entryId, v, bubble) : kHitNone;
                if (kind != kHitNone)
                {
                    out.buttons = kGuard;
                    powershield = 1;
                    TM_LOG("cpu: P%d perfect shield: guard for %s predicted next frame "
                           "(shield %.2f %.2f r %.2f%s), frame %u\n",
                           who.playerNo + 1,
                           kind == kHitWeapon ? "weapon" : (kind == kHitItem ? "item" : "fighter"),
                           bubble.x, bubble.y, bubble.radius, bubble.fallback ? " fallback" : "", frameOf(slot));
                }
            }
            break;
        default:
            break;
        }
    }

    void Brain::counter(Frame& out, const Owner& who)
    {
        if (shieldKind && (v.air || !guardStatus(v.status)))
        {
            actionable = 0;
            revPicked = false;
            enterIdle(out, who);
            return;
        }
        bool fresh = justOut();
        if (!canAct(v) && !fresh)
        {
            // Reset while unactionable so pre-landing air frames do not
            // shorten the delay after landing.
            actionable = 0;
            revPicked = false;
            if (shieldKind)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            return;
        }
        // Landing this frame counts as grounded.
        const bool airNow = v.air && framesToLand() != 0;
        if (actionable == 0)
        {
            counterAir = airNow;
        }
        int list = shieldKind ? kListShield : (counterAir && airNow ? kListAir : kListGround);
        int index = 0, delay = 0;
        bool comboJumpInvuln = false;
        const main::ComboTrainerSettings& combo = main::settings().comboTrainer;
        static const u8 kComboDelayF[main::kComboDelayCount] = {0, 15, 30, 45, 60};
        const int comboDelayF = (combo.resetDelay < main::kComboDelayCount ? kComboDelayF[combo.resetDelay] : 30)
                                + static_cast<int>(combo.comboTimerAdjustment);
        const bool comboEscapeActive = !shieldKind && combo.enabled != 0 && combo.fixPosition != 0
                                       && combo.recoveryTrainer == 0 && comboDelayF > 0
                                       && combo.postHitstunEscape != main::kComboEscapeCounter;
        if (comboEscapeActive)
        {
            delay = 0;
            switch (combo.postHitstunEscape)
            {
            case main::kComboEscapeDodge:
                index = 1; // Ground: Spotdodge (1), Air: Airdodge (1)
                break;
            case main::kComboEscapeJumpInvuln:
                if (list == kListAir && !jumpLeft(f))
                {
                    index = 1; // No double jump left: fall back to Airdodge without phantom intangibility
                }
                else
                {
                    index = (list == kListAir) ? 2 : 18; // Air: Jump Away (2), Ground: Short Hop (18)
                    comboJumpInvuln = true;
                }
                break;
            case main::kComboEscapeCustomPool:
                index = customPoolIndex(list);
                break;
            case main::kComboEscapeNone:
            default:
                index = 0;
                break;
            }
        }
        else if (!counterChoice(list, index, delay))
        {
            TM_LOG("cpu: P%d counter disabled for hit %d\n", who.playerNo + 1, hits);
            revPicked = false;
            state = kAfter;
            return;
        }
        if (list == kListAir || comboEscapeActive)
        {
            revPicked = false;
        }
        else
        {
            if (!revPicked)
            {
                revPicked = true;
                TM_STATIC_ASSERT((kListGround >> 1) == kRevOnHit && (kListShield >> 1) == kRevOnShield,
                                 tm_list_to_rev_trigger);
                pickMultiRev(list >> 1, who.entryId);
            }
            if (revAction != 0)
            {
                index = revAction;
                delay = revDelay;
            }
        }
        // A held guard acts on hitstun's last frame, but delays count from
        // the first actionable frame, the next one.
        bool lastStun = v.hitstun == 1;
        if (actionable < delay)
        {
            if (!lastStun)
            {
                actionable++;
            }
            if (shieldKind)
            {
                out.buttons = kGuard;
                shieldTilt(out);
            }
            return;
        }
        bool adaptiveUsed = false;
        if (revAction == 0 && opts().adaptiveMode == kAdaptiveActive && list != kListAir && !comboEscapeActive
            && main::extraLoaded())
        {
            int biasIdx = main::extraAdaptiveBias(who.playerNo, list, true, oppDist, false);
            if (biasIdx > 0)
            {
                index = biasIdx;
                adaptiveUsed = true;
            }
        }
        int fk = fighterKind(who.entryId);
        int chosen = resolve(list, index, fk);
        if (actionKind(list, chosen) == kActionPlayRandomSlot)
        {
            int pick = randomSlot();
            TM_LOG("cpu: P%d counter list %d: Play Random Slot picks slot %d, frame %u\n", who.playerNo + 1, list,
                   pick + 1, frameOf(slot));
            revPicked = false;
            startPlay(out, pick);
            return;
        }
        const Action* a = actionFor(list, chosen, fk);
        if (!takeable(a, airNow, fresh))
        {
            return;
        }
        if (adaptiveUsed)
        {
            main::extraAdaptiveBias(who.playerNo, list, true, oppDist, true);
        }
        if (comboJumpInvuln && a != NULL && ((a->flags & kNeedsJump) == 0 || jumpLeft(f)))
        {
            setIntangible(f, 10);
        }
        TM_LOG("cpu: P%d counter list %d action %d (%d) after %d actionable frames, hit %d, frame %u, "
               "air %d y %d vy %d\n",
               who.playerNo + 1, list, chosen, index, actionable, hits, frameOf(slot), v.air ? 1 : 0,
               game::f2i(v.y * 100.0f), game::f2i(v.vy * 100.0f));
        start(out, a);
    }

    bool Brain::cueNow(Frame& out, const Owner& who, Cue& q)
    {
        s32 now = game::trackerFrame();
        bool busy = v.hitlag > 0 || state == kGrabbed || state == kClatter
                    || (state == kAfter && (script != NULL || pending != NULL || playSlot >= 0));
        if (q.state != kCueWaiting || (q.frame != kCueAsap && now < q.frame) || busy)
        {
            return false;
        }
        bool fresh = justOut();
        // Leaving a jump squat is airborne: an air action can start in it.
        // The situation decides, not the predicted landing (observed,
        // character not recorded: with it, a Jab cued three frames before
        // an empty short hop landed came out as a neutral air).
        const bool takeoff = q.list == kListAir && takeoffNext(v);
        const bool airNow = v.air || takeoff;
        if ((!canAct(v) && !fresh && !takeoff) || airNow != (q.list == kListAir))
        {
            return false;
        }
        int fk = fighterKind(who.entryId);
        int chosen = q.custom != NULL ? 0 : resolve(q.list, q.index, fk);
        const Action* a = q.custom != NULL ? q.custom : actionFor(q.list, chosen, fk);
        bool randomSlotPick = q.custom == NULL && actionKind(q.list, chosen) == kActionPlayRandomSlot;
        if (!randomSlotPick && !takeable(a, airNow, fresh))
        {
            return false;
        }
        TM_LOG("cpu: P%d cue list %d action %d, frame %d\n", who.playerNo + 1, q.list, chosen, now);
        out.buttons = 0;
        out.x = out.y = 0.0f;
        if (randomSlotPick)
        {
            startPlay(out, randomSlot());
        }
        else
        {
            start(out, a);
        }
        q.state = kCueRunning;
        q.start = now;
        return true;
    }
}
}
